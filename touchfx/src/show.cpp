#include "show.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <fstream>

#include "platform.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
const char* kExt = ".tfxshow";
const char* kThumbExt = ".thumb";

fs::path toPath(const std::string& utf8) { return fs::u8path(utf8); }
}  // namespace

std::string showsDir() {
  static std::string dir;
  if (dir.empty()) {
    fs::path p = toPath(platformDocumentsDir()) / "TouchFX Shows";
    std::error_code ec;
    fs::create_directories(p, ec);
    dir = p.u8string();
  }
  return dir;
}

std::string showPath(const std::string& name) { return (toPath(showsDir()) / toPath(name + kExt)).u8string(); }

static std::string thumbPath(const std::string& name) {
  return (toPath(showsDir()) / toPath(name + kThumbExt)).u8string();
}

bool showExists(const std::string& name) {
  std::error_code ec;
  return fs::exists(toPath(showPath(name)), ec);
}

std::vector<ShowInfo> listShows() { return listShowsIn(showsDir()); }

std::vector<ShowInfo> listShowsIn(const std::string& dir) {
  std::vector<ShowInfo> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(toPath(dir), ec)) {
    if (!e.is_regular_file() || e.path().extension() != kExt) continue;
    ShowInfo s;
    s.name = e.path().stem().u8string();
    s.path = e.path().u8string();
    auto t = fs::last_write_time(e.path(), ec);
    // file_time_type -> time_t (portable C++17 conversion)
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    s.modified = std::chrono::system_clock::to_time_t(sys);
    out.push_back(s);
  }
  std::sort(out.begin(), out.end(), [](const ShowInfo& a, const ShowInfo& b) { return a.modified > b.modified; });
  return out;
}

std::string safeShowName(const std::string& name) {
  std::string s;
  for (char c : name) {
    if (std::string("\\/:*?\"<>|").find(c) != std::string::npos || (unsigned char)c < 32) continue;
    s += c;
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  if (s.empty()) s = "Show";
  if (s.size() > 60) s.resize(60);
  return s;
}

std::string uniqueShowName(const std::string& base) {
  std::string b = safeShowName(base);
  if (!showExists(b)) return b;
  for (int i = 2; i < 1000; ++i) {
    std::string n = b + " " + std::to_string(i);
    if (!showExists(n)) return n;
  }
  return b + " copy";
}

bool renameShow(const std::string& from, const std::string& to) {
  std::error_code ec;
  if (showExists(to)) return false;
  fs::rename(toPath(showPath(from)), toPath(showPath(to)), ec);
  if (ec) return false;
  fs::rename(toPath(thumbPath(from)), toPath(thumbPath(to)), ec);
  return true;
}

bool duplicateShow(const std::string& from, const std::string& to) {
  std::error_code ec;
  if (showExists(to)) return false;
  fs::copy_file(toPath(showPath(from)), toPath(showPath(to)), ec);
  if (ec) return false;
  fs::copy_file(toPath(thumbPath(from)), toPath(thumbPath(to)), ec);
  return true;
}

bool deleteShow(const std::string& name) {
  std::error_code ec;
  // Deleted shows go to a "Deleted" subfolder instead of vanishing, in case of a wrong tap.
  fs::path bin = toPath(showsDir()) / "Deleted";
  fs::create_directories(bin, ec);
  fs::path dst = bin / toPath(name + kExt);
  fs::remove(dst, ec);
  fs::rename(toPath(showPath(name)), dst, ec);
  if (ec) fs::remove(toPath(showPath(name)), ec);
  fs::remove(toPath(thumbPath(name)), ec);
  return !showExists(name);
}

bool saveThumb(const std::string& name, const std::vector<uint8_t>& rgb) {
  if (rgb.size() != size_t(kThumbW) * kThumbH * 3) return false;
  std::ofstream f(toPath(thumbPath(name)), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write("TFXT", 4);
  f.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
  return bool(f);
}

bool loadThumb(const std::string& name, std::vector<uint8_t>& rgb) {
  std::ifstream f(toPath(thumbPath(name)), std::ios::binary);
  if (!f) return false;
  char magic[4];
  f.read(magic, 4);
  if (!f || std::string(magic, 4) != "TFXT") return false;
  rgb.resize(size_t(kThumbW) * kThumbH * 3);
  f.read(reinterpret_cast<char*>(rgb.data()), std::streamsize(rgb.size()));
  return bool(f);
}

// ---------------------------------------------------------------------------
// Import / export

std::vector<DriveInfo> listDrives() {
  std::vector<DriveInfo> out;
#ifdef _WIN32
  std::wstring showsRoot = toPath(showsDir()).root_name().wstring();  // "C:"
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if (!(mask & (1u << i))) continue;
    wchar_t root[4] = {wchar_t(L'A' + i), L':', L'\\', 0};
    UINT type = GetDriveTypeW(root);
    if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED && type != DRIVE_REMOTE) continue;
    if (_wcsnicmp(root, showsRoot.c_str(), 2) == 0) continue;  // the drive the shows are already on
    wchar_t vol[MAX_PATH + 1] = {0};
    // a card reader with no card: no volume, skip it (and no "insert a disk" dialog)
    UINT old = SetErrorMode(SEM_FAILCRITICALERRORS);
    BOOL ok = GetVolumeInformationW(root, vol, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
    SetErrorMode(old);
    if (!ok) continue;
    DriveInfo d;
    d.root = fs::path(root).u8string();
    d.removable = type == DRIVE_REMOVABLE;
    std::string letter = std::string(1, char('A' + i)) + ":";
    std::string name = fs::path(vol).u8string();
    d.label = std::string(d.removable ? "USB" : (type == DRIVE_REMOTE ? "Network" : "Drive")) + " (" + letter + ")" +
              (name.empty() ? "" : "  " + name);
    out.push_back(d);
  }
#else
  // Linux / testing: mounted media, plus TOUCHFX_TEST_DRIVES (folders separated by ':')
  std::vector<std::string> roots;
  if (const char* t = std::getenv("TOUCHFX_TEST_DRIVES")) {
    std::string v = t;
    size_t a = 0;
    while (a <= v.size()) {
      size_t b = v.find(':', a);
      if (b == std::string::npos) b = v.size();
      if (b > a) roots.push_back(v.substr(a, b - a));
      a = b + 1;
    }
  }
  std::error_code ec;
  for (const char* base : {"/media", "/run/media"})
    for (const auto& u : fs::directory_iterator(base, ec))
      for (const auto& m : fs::directory_iterator(u.path(), ec))
        if (m.is_directory()) roots.push_back(m.path().u8string());
  for (const std::string& r : roots) {
    DriveInfo d;
    d.root = r;
    d.removable = true;
    d.label = "USB  " + toPath(r).filename().u8string();
    out.push_back(d);
  }
#endif
  std::stable_sort(out.begin(), out.end(), [](const DriveInfo& a, const DriveInfo& b) { return a.removable > b.removable; });
  return out;
}

std::string driveShowsDir(const std::string& root) { return (toPath(root) / "TouchFX Shows").u8string(); }

namespace {
// Copies <src>.tfxshow (+ .thumb) to dir as name; mode decides what happens when the name is taken.
TransferResult copyShowTo(const fs::path& srcShow, const fs::path& dir, const std::string& wanted, TransferMode mode,
                          std::string& outName) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::string name = safeShowName(wanted);
  auto exists = [&](const std::string& n) { return fs::exists(dir / toPath(n + kExt), ec); };
  if (exists(name)) {
    if (mode == XFER_ASK) return XFER_EXISTS;
    if (mode == XFER_KEEP_BOTH) {
      std::string b = name;
      for (int i = 2; i < 1000 && exists(name); ++i) name = b + " " + std::to_string(i);
    }
  }
  fs::path dst = dir / toPath(name + kExt);
  // write to a temporary file first, so a pulled stick never leaves half a show under the real name
  fs::path tmp = dir / toPath(name + kExt + ".part");
  fs::remove(tmp, ec);
  fs::copy_file(srcShow, tmp, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return XFER_FAILED;
  }
  fs::remove(dst, ec);
  fs::rename(tmp, dst, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return XFER_FAILED;
  }
  fs::path srcThumb = srcShow;
  srcThumb.replace_extension(kThumbExt);
  fs::path dstThumb = dir / toPath(name + kThumbExt);
  fs::remove(dstThumb, ec);
  if (fs::exists(srcThumb, ec)) fs::copy_file(srcThumb, dstThumb, ec);
  outName = name;
  return XFER_OK;
}
}  // namespace

TransferResult exportShow(const std::string& name, const std::string& root, TransferMode mode, std::string& outName) {
  return copyShowTo(toPath(showPath(name)), toPath(driveShowsDir(root)), name, mode, outName);
}

TransferResult importShow(const std::string& path, TransferMode mode, std::string& outName) {
  fs::path p = toPath(path);
  return copyShowTo(p, toPath(showsDir()), p.stem().u8string(), mode, outName);
}
