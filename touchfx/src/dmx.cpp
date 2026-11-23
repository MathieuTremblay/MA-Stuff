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

void DmxInput::start(int protocol, int universe, uint32_t adapterIp, uint32_t adapterMask) {
  stop();
  protocol_ = protocol;
  universe_ = universe;
  adapterIp_ = adapterIp;
  adapterMask_ = adapterMask;
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
    if (adapterIp) {  // only the chosen network card
      m.imr_interface.s_addr = htonl(adapterIp);
      joined = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof(m)) == 0;
    } else {
      for (uint32_t ip : localAddresses()) {
        if ((ip >> 24) == 127) continue;
        m.imr_interface.s_addr = htonl(ip);
        if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof(m)) == 0) joined = true;
      }
      m.imr_interface.s_addr = htonl(INADDR_ANY);
      if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&m, sizeof(m)) == 0) joined = true;
    }
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

std::string DmxInput::polledBy() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (!pollerIp_) return "";
  return ipStr(pollerIp_) + (pollerName_.empty() ? "" : " (" + pollerName_ + ")");
}

double DmxInput::secondsSincePoll() const {
  int64_t l = lastPollMs_.load();
  return l < 0 ? 1e9 : (nowMs() - l) / 1000.0;
}

// Our IP address as seen from 'toIp' (the local address the OS would use to reach it).
static uint32_t localIpFor(uint32_t toIp) {
  intptr_t s = intptr_t(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (s < 0) return 0;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(toIp);
  a.sin_port = htons(kArtnetPort);
  uint32_t ip = 0;
  if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
    sockaddr_in me{};
    socklen_t l = sizeof(me);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &l) == 0) ip = ntohl(me.sin_addr.s_addr);
  }
  CLOSESOCK(s);
  return ip;
}

// ArtPollReply (Art-Net 4): announces "TouchFX" as a node with one output port on our universe.
void DmxInput::replyArtPoll(uint32_t toIp) {
  uint8_t r[239] = {};
  std::memcpy(r, "Art-Net\0", 8);
  r[8] = 0x00;
  r[9] = 0x21;  // OpPollReply 0x2100, little endian
  uint32_t me = adapterIp_ ? adapterIp_ : localIpFor(toIp);  // the card the console should talk to
  r[10] = (me >> 24) & 255;
  r[11] = (me >> 16) & 255;
  r[12] = (me >> 8) & 255;
  r[13] = me & 255;
  r[14] = 0x36;
  r[15] = 0x19;  // port 6454, little endian
  r[16] = 0;
  r[17] = 7;     // firmware version
  int pa = universe_;
  r[18] = (pa >> 8) & 0x7F;  // NetSwitch
  r[19] = (pa >> 4) & 0x0F;  // SubSwitch
  r[20] = 0x00;
  r[21] = 0xFF;  // OEM unknown
  r[23] = 0xD0;  // Status1: indicators normal, addresses set from the front panel (TouchFX settings)
  std::snprintf(reinterpret_cast<char*>(r + 26), 18, "TouchFX");
  std::snprintf(reinterpret_cast<char*>(r + 44), 64, "TouchFX touch visuals");
  bool receiving = secondsSinceData() < 2.0;
  std::snprintf(reinterpret_cast<char*>(r + 108), 64, "#0001 [%04d] %s", pps_.load() % 10000,
                receiving ? "Receiving DMX" : "Waiting for DMX");
  r[173] = 1;              // NumPorts
  r[174] = 0x80;           // port 1 can output DMX512 from the Art-Net network
  r[182] = receiving ? 0x80 : 0x00;  // GoodOutputA: data being received
  r[190] = pa & 0x0F;      // SwOut
  r[200] = 0x00;           // StNode
  r[207] = r[10];
  r[208] = r[11];
  r[209] = r[12];
  r[210] = r[13];          // BindIp
  r[211] = 1;              // BindIndex
  r[212] = 0x08;           // Status2: 15-bit port addresses
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = htonl(toIp);
  to.sin_port = htons(kArtnetPort);
  sendto(sock_, reinterpret_cast<const char*>(r), sizeof(r), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
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
  // A network card is chosen: ignore anything that does not come from its network (or from this PC on it).
  if (adapterIp_ && fromIp != adapterIp_ && (fromIp & adapterMask_) != (adapterIp_ & adapterMask_)) return;
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
    if (n < 12 || std::memcmp(p, "Art-Net\0", 8) != 0) return;
    int op = p[8] | (p[9] << 8);
    if (op == 0x2000) {  // ArtPoll: a console is looking for nodes -> answer, and remember who asked
      replyArtPoll(fromIp);
      std::lock_guard<std::mutex> lk(mtx_);
      pollerIp_ = fromIp;
      pollerName_.clear();
      for (const NodeName& nn : nodeNames_)
        if (nn.ip == fromIp) pollerName_ = nn.name;
      lastPollMs_ = nowMs();
      return;
    }
    if (op == 0x2100 && n >= 108) {  // another node's ArtPollReply: learn its name (e.g. "grandMA3")
      char nm[65] = {};
      std::memcpy(nm, p + 44, 64);
      if (!nm[0]) std::memcpy(nm, p + 26, 18);
      std::lock_guard<std::mutex> lk(mtx_);
      bool found = false;
      for (NodeName& nn : nodeNames_)
        if (nn.ip == fromIp) {
          nn.name = nm;
          found = true;
        }
      if (!found && nodeNames_.size() < 64) nodeNames_.push_back({fromIp, nm});
      if (fromIp == pollerIp_) pollerName_ = nm;
      return;
    }
    if (op != 0x5000 || n < 18) return;  // ArtDmx
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
