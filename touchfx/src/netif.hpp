#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Network adapters with an IPv4 address (loopback excluded).
struct NetAdapter {
  std::string id;    // stable id (Windows adapter GUID, interface name elsewhere)
  std::string name;  // "Ethernet 2"
  std::string desc;  // "Intel(R) Ethernet Controller I210"
  uint32_t ip = 0, mask = 0;  // host byte order
  bool up = false;
};

std::vector<NetAdapter> listAdapters();
std::string ipToString(uint32_t ip);
// Finds an adapter by id. Returns nullptr if it is not there (unplugged, disabled...).
const NetAdapter* findAdapter(const std::vector<NetAdapter>& list, const std::string& id);
std::string adapterLabel(const NetAdapter& a);  // "Ethernet 2 · 10.0.0.12 · Intel I210"
