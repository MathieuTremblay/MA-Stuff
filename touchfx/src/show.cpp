#include "show.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <fstream>

#include "platform.hpp"

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

std::vector<ShowInfo> listShows() {
  std::vector<ShowInfo> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(toPath(showsDir()), ec)) {
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
