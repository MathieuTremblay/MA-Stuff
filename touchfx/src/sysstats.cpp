#include "sysstats.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

SysStats::SysStats() { t_ = std::thread(&SysStats::run, this); }

SysStats::~SysStats() {
  quit_ = true;
  if (t_.joinable()) t_.join();
}

#ifdef _WIN32

namespace {
// Minimal PDH declarations, loaded at runtime from pdh.dll (no import library needed).
typedef void* PDH_HQUERY;
typedef void* PDH_HCOUNTER;
struct PdhFmtValue {
  DWORD CStatus;
  union {
    LONG longValue;
    double doubleValue;
    LONGLONG largeValue;
    LPCSTR AnsiStringValue;
    LPCWSTR WideStringValue;
  };
};
struct PdhFmtItemW {
  LPWSTR szName;
  PdhFmtValue FmtValue;
};
constexpr DWORD kPdhFmtDouble = 0x00000200;
constexpr LONG kPdhMoreData = (LONG)0x800007D2;
using FnOpen = LONG(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
using FnAdd = LONG(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
using FnCollect = LONG(WINAPI*)(PDH_HQUERY);
using FnArray = LONG(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PdhFmtItemW*);
using FnClose = LONG(WINAPI*)(PDH_HQUERY);

uint64_t ft(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
}  // namespace

void SysStats::run() {
  HMODULE pdh = LoadLibraryW(L"pdh.dll");
  FnOpen open = pdh ? (FnOpen)(void*)GetProcAddress(pdh, "PdhOpenQueryW") : nullptr;
  FnAdd add = pdh ? (FnAdd)(void*)GetProcAddress(pdh, "PdhAddEnglishCounterW") : nullptr;
  FnCollect collect = pdh ? (FnCollect)(void*)GetProcAddress(pdh, "PdhCollectQueryData") : nullptr;
  FnArray getArray = pdh ? (FnArray)(void*)GetProcAddress(pdh, "PdhGetFormattedCounterArrayW") : nullptr;
  FnClose closeQ = pdh ? (FnClose)(void*)GetProcAddress(pdh, "PdhCloseQuery") : nullptr;
  PDH_HQUERY q = nullptr;
  PDH_HCOUNTER gpuCounter = nullptr;
  if (open && add && collect && getArray && open(nullptr, 0, &q) == 0) {
    // Same counter Task Manager uses for the "3D" GPU graph (Windows 10 1709+).
    if (add(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpuCounter) != 0) gpuCounter = nullptr;
    collect(q);
  }
  FILETIME i0, k0, u0;
  GetSystemTimes(&i0, &k0, &u0);
  std::vector<unsigned char> buf;
  while (!quit_) {
    for (int i = 0; i < 10 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    FILETIME i1, k1, u1;
    if (GetSystemTimes(&i1, &k1, &u1)) {
      uint64_t idle = ft(i1) - ft(i0), total = (ft(k1) - ft(k0)) + (ft(u1) - ft(u0));  // kernel includes idle
      if (total > 0) cpu_ = float(100.0 * (1.0 - double(idle) / double(total)));
      i0 = i1;
      k0 = k1;
      u0 = u1;
    }
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
      ramTotal_ = float(m.ullTotalPhys / 1073741824.0);
      ramUsed_ = float((m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0);
    }
    if (q && gpuCounter && collect(q) == 0) {
      DWORD size = 0, count = 0;
      if (getArray(gpuCounter, kPdhFmtDouble, &size, &count, nullptr) == kPdhMoreData && size > 0) {
        buf.resize(size);
        auto* items = reinterpret_cast<PdhFmtItemW*>(buf.data());
        if (getArray(gpuCounter, kPdhFmtDouble, &size, &count, items) == 0) {
          double sum = 0;
          for (DWORD i = 0; i < count; ++i)
            if (items[i].FmtValue.CStatus == 0) sum += items[i].FmtValue.doubleValue;
          gpu_ = float(sum > 100.0 ? 100.0 : sum);
        }
      } else {
        gpu_ = 0;  // no 3D activity reported
      }
    }
  }
  if (q && closeQ) closeQ(q);
  if (pdh) FreeLibrary(pdh);
}

#else

void SysStats::run() {
  unsigned long long pIdle = 0, pTotal = 0;
  while (!quit_) {
    if (FILE* f = std::fopen("/proc/stat", "r")) {
      unsigned long long u, n, s, idle, io, irq, sirq, steal;
      if (std::fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &idle, &io, &irq, &sirq, &steal) == 8) {
        unsigned long long id = idle + io, tot = u + n + s + idle + io + irq + sirq + steal;
        if (pTotal && tot > pTotal) cpu_ = float(100.0 * (1.0 - double(id - pIdle) / double(tot - pTotal)));
        pIdle = id;
        pTotal = tot;
      }
      std::fclose(f);
    }
    if (FILE* f = std::fopen("/proc/meminfo", "r")) {
      char key[64];
      unsigned long long v;
      unsigned long long total = 0, avail = 0;
      while (std::fscanf(f, "%63s %llu kB\n", key, &v) == 2) {
        if (!std::strcmp(key, "MemTotal:")) total = v;
        if (!std::strcmp(key, "MemAvailable:")) avail = v;
      }
      std::fclose(f);
      ramTotal_ = float(total / 1048576.0);
      ramUsed_ = float((total - avail) / 1048576.0);
    }
    for (int i = 0; i < 10 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

#endif
