#include "netif.hpp"

#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

std::string ipToString(uint32_t ip) {
  char b[24];
  std::snprintf(b, sizeof(b), "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
  return b;
}

const NetAdapter* findAdapter(const std::vector<NetAdapter>& list, const std::string& id) {
  for (const NetAdapter& a : list)
    if (a.id == id) return &a;
  return nullptr;
}

std::string adapterLabel(const NetAdapter& a) {
  std::string s = a.name + "  -  " + ipToString(a.ip);
  if (!a.desc.empty() && a.desc != a.name) s += "  -  " + a.desc;
  if (!a.up) s += "  (down)";
  return s;
}

#ifdef _WIN32

static std::string utf8(const wchar_t* w) {
  if (!w) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

std::vector<NetAdapter> listAdapters() {
  std::vector<NetAdapter> out;
  ULONG size = 16 * 1024;
  std::vector<unsigned char> buf(size);
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &size);
  if (r == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &size);
  }
  if (r != NO_ERROR) return out;
  for (auto* a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) {
    if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
      NetAdapter n;
      n.id = a->AdapterName ? a->AdapterName : "";
      n.name = utf8(a->FriendlyName);
      n.desc = utf8(a->Description);
      n.ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
      unsigned prefix = u->OnLinkPrefixLength;
      n.mask = prefix == 0 ? 0 : (prefix >= 32 ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefix)));
      n.up = a->OperStatus == IfOperStatusUp;
      out.push_back(n);
      break;  // first IPv4 address of the adapter
    }
  }
  return out;
}

#else

std::vector<NetAdapter> listAdapters() {
  std::vector<NetAdapter> out;
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) != 0) return out;
  for (ifaddrs* i = ifs; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
    bool dup = false;
    for (const NetAdapter& a : out)
      if (a.id == i->ifa_name) dup = true;
    if (dup) continue;
    NetAdapter n;
    n.id = n.name = i->ifa_name;
    n.ip = ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr);
    n.mask = i->ifa_netmask ? ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_netmask)->sin_addr.s_addr) : 0;
    n.up = (i->ifa_flags & IFF_UP) && (i->ifa_flags & IFF_RUNNING);
    out.push_back(n);
  }
  freeifaddrs(ifs);
  return out;
}

#endif
