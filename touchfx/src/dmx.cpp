#include "dmx.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using socklen_t = int;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
#endif

namespace {

int64_t nowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

constexpr uint16_t kSacnPort = 5568;
constexpr uint16_t kArtnetPort = 6454;

std::string ipStr(uint32_t ipHostOrder) {
  char b[32];
  std::snprintf(b, sizeof(b), "%u.%u.%u.%u", (ipHostOrder >> 24) & 255, (ipHostOrder >> 16) & 255,
                (ipHostOrder >> 8) & 255, ipHostOrder & 255);
  return b;
}

// All local IPv4 addresses (to join sACN multicast on every network card, not just the default one).
std::vector<uint32_t> localAddresses() {
  std::vector<uint32_t> out;
#ifdef _WIN32
  char host[256];
  if (gethostname(host, sizeof(host)) == 0) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
      for (addrinfo* r = res; r; r = r->ai_next)
        out.push_back(ntohl(reinterpret_cast<sockaddr_in*>(r->ai_addr)->sin_addr.s_addr));
      freeaddrinfo(res);
    }
  }
#else
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) == 0) {
    for (ifaddrs* i = ifs; i; i = i->ifa_next)
      if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET)
        out.push_back(ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr));
    freeifaddrs(ifs);
  }
#endif
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace

DmxInput::~DmxInput() { stop(); }

void DmxInput::start(int protocol, int universe) {
  stop();
  protocol_ = protocol;
  universe_ = universe;
  error_.clear();
  {
    std::lock_guard<std::mutex> lk(mtx_);
    have_ = false;
    std::memset(data_, 0, sizeof(data_));
    source_.clear();
  }
  lastMs_ = -1;
  pps_ = 0;
  sacnPriority_ = -1;
  if (protocol == DMXP_OFF) return;

#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  intptr_t s = intptr_t(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (s < 0) {
    error_ = "Could not create a network socket.";
    return;
  }
  int yes = 1;
  // Share the port with other DMX software on this PC (e.g. grandMA3 onPC).
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#ifdef SO_REUSEPORT
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof(yes));
#endif
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
  int rcvbuf = 1 << 20;
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(protocol == DMXP_SACN ? kSacnPort : kArtnetPort);
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error_ = protocol == DMXP_SACN ? "Could not open the sACN port (5568)." : "Could not open the Art-Net port (6454).";
    CLOSESOCK(s);
    return;
  }
  if (protocol == DMXP_SACN) {
    // Multicast group 239.255.<hi>.<lo> on every network card.
    ip_mreq m{};
    m.imr_multiaddr.s_addr = htonl((239u << 24) | (255u << 16) | (uint32_t(universe) & 0xFFFF));
    bool joined = false;
    for (uint32_t ip : localAddresses()) {
      if ((ip >> 24) == 127) continue;
      m.imr_interface.s_addr = htonl(ip);
      if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof(m)) == 0) joined = true;
    }
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof(m)) == 0) joined = true;
    if (!joined) error_ = "Multicast join failed: only unicast sACN will be received.";
  }
  sock_ = s;
  quit_ = false;
  countStart_ = nowMs();
  count_ = 0;
  thread_ = std::thread(&DmxInput::threadMain, this);
}

void DmxInput::stop() {
  if (thread_.joinable()) {
    quit_ = true;
    thread_.join();
  }
  if (sock_ >= 0) CLOSESOCK(sock_);
  sock_ = -1;
}

bool DmxInput::snapshot(uint8_t out[512]) {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!have_) return false;
  std::memcpy(out, data_, 512);
  return true;
}

double DmxInput::secondsSinceData() const {
  int64_t l = lastMs_.load();
  return l < 0 ? 1e9 : (nowMs() - l) / 1000.0;
}

std::string DmxInput::sourceInfo() {
  std::lock_guard<std::mutex> lk(mtx_);
  return source_;
}

void DmxInput::threadMain() {
  std::vector<uint8_t> buf(1500);
  while (!quit_) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET((unsigned)sock_, &fds);
    timeval tv{0, 100000};
    int r = select(int(sock_ + 1), &fds, nullptr, nullptr, &tv);
    int64_t t = nowMs();
    if (t - countStart_ >= 1000) {
      pps_ = int(count_ * 1000 / std::max<int64_t>(1, t - countStart_));
      count_ = 0;
      countStart_ = t;
    }
    if (r <= 0) continue;
    sockaddr_in from{};
    socklen_t fl = sizeof(from);
    int n = int(recvfrom(sock_, (char*)buf.data(), int(buf.size()), 0, reinterpret_cast<sockaddr*>(&from), &fl));
    if (n > 0) handlePacket(buf.data(), n, ntohl(from.sin_addr.s_addr));
  }
}

void DmxInput::handlePacket(const uint8_t* p, int n, uint32_t fromIp) {
  const uint8_t* dmx = nullptr;
  int len = 0;
  std::string name;

  if (protocol_ == DMXP_SACN) {
    // E1.31 data packet
    static const uint8_t kId[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
    if (n < 126 || std::memcmp(p + 4, kId, 12) != 0) return;
    uint32_t rootVec = (uint32_t(p[18]) << 24) | (uint32_t(p[19]) << 16) | (uint32_t(p[20]) << 8) | p[21];
    uint32_t frameVec = (uint32_t(p[40]) << 24) | (uint32_t(p[41]) << 16) | (uint32_t(p[42]) << 8) | p[43];
    if (rootVec != 4 || frameVec != 2 || p[117] != 2) return;
    int universe = (p[113] << 8) | p[114];
    if (universe != universe_) return;
    uint8_t options = p[112];
    if (options & 0x80) return;  // preview data
    if (options & 0x40) return;  // stream terminated
    if (p[125] != 0) return;     // only the DMX start code
    int priority = p[108];
    int64_t t = nowMs();
    // Highest priority source wins; a lower one takes over if the higher one has gone quiet.
    if (priority < sacnPriority_ && t - sacnPriorityMs_ < 2500) return;
    sacnPriority_ = priority;
    sacnPriorityMs_ = t;
    int count = ((p[123] << 8) | p[124]) - 1;
    dmx = p + 126;
    len = std::min({count, 512, n - 126});
    char src[65] = {};
    std::memcpy(src, p + 44, 64);
    name = src;
  } else {
    if (n < 18 || std::memcmp(p, "Art-Net\0", 8) != 0) return;
    int op = p[8] | (p[9] << 8);
    if (op != 0x5000) return;  // ArtDmx only
    int portAddress = ((p[15] & 0x7F) << 8) | p[14];
    if (portAddress != universe_) return;
    int l = (p[16] << 8) | p[17];
    dmx = p + 18;
    len = std::min({l, 512, n - 18});
  }
  if (!dmx || len <= 0) return;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    std::memcpy(data_, dmx, size_t(len));
    if (len < 512) std::memset(data_ + len, 0, size_t(512 - len));
    have_ = true;
    source_ = ipStr(fromIp) + (name.empty() ? "" : " (" + name + ")");
  }
  lastMs_ = nowMs();
  ++count_;
}
