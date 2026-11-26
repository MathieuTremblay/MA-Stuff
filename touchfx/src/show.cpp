#include "show.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "log.hpp"
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

std::time_t fileTime(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  if (ec) return 0;
  auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(sys);
}

// Shows from before 0.9.4 were single files in "Documents/TouchFX Shows": copy each into its project folder
// once (the old files are left where they are, with a note).
void migrateOldShows(const fs::path& projects) {
  std::error_code ec;
  fs::path old = toPath(platformDocumentsDir()) / "TouchFX Shows";
  fs::path note = old / "MOVED - shows are now in TouchFX Projects.txt";
  if (!fs::is_directory(old, ec) || fs::exists(note, ec)) return;
  int moved = 0;
  for (const auto& e : fs::directory_iterator(old, ec)) {
    if (!e.is_regular_file() || e.path().extension() != kExt) continue;
    std::string name = e.path().stem().u8string();
    fs::path dir = projects / toPath(name);
    if (fs::exists(dir, ec)) continue;
    fs::create_directories(dir, ec);
    fs::copy_file(e.path(), dir / toPath(name + kExt), ec);
    fs::path th = e.path();
    th.replace_extension(kThumbExt);
    if (fs::exists(th, ec)) fs::copy_file(th, dir / toPath(name + kThumbExt), ec);
    fs::last_write_time(dir / toPath(name + kExt), fs::last_write_time(e.path(), ec), ec);
    ++moved;
  }
  std::ofstream(note) << "TouchFX 0.9.4 keeps each show in its own project folder (with its media):\n"
                      << (projects.u8string()) << "\nThe shows were copied there. These old files are not used any more.\n";
  if (moved) logLine("Moved %d show(s) into project folders", moved);
}
}  // namespace

std::string showsDir() {
  static std::string dir;
  if (dir.empty()) {
    fs::path p = toPath(platformDocumentsDir()) / "TouchFX Projects";
    std::error_code ec;
    fs::create_directories(p, ec);
    dir = p.u8string();
    migrateOldShows(p);
  }
  return dir;
}

std::string projectDir(const std::string& name) { return (toPath(showsDir()) / toPath(name)).u8string(); }

std::string mediaDir(const std::string& name) {
  fs::path p = toPath(projectDir(name)) / "media";
  std::error_code ec;
  fs::create_directories(p, ec);
  return p.u8string();
}

std::string showPath(const std::string& name) {
  fs::path dir = toPath(projectDir(name));
  std::error_code ec;
  fs::create_directories(dir, ec);  // saving creates the folder
  return (dir / toPath(name + kExt)).u8string();
}

static fs::path thumbFile(const std::string& name) { return toPath(projectDir(name)) / toPath(name + kThumbExt); }

bool showExists(const std::string& name) {
  std::error_code ec;
  return fs::exists(toPath(projectDir(name)), ec);
}

std::vector<ShowInfo> listShows() {
  std::vector<ShowInfo> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(toPath(showsDir()), ec)) {
    if (!e.is_directory()) continue;
    std::string name = e.path().filename().u8string();
    fs::path file = e.path() / toPath(name + kExt);
    if (!fs::exists(file, ec)) continue;
    ShowInfo s;
    s.name = name;
    s.path = e.path().u8string();
    s.modified = fileTime(file);
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
  if (s.empty() || s == "Deleted") s = "Show";
  if (s.size() > 60) s.resize(60);
  return s;
}

static std::string uniqueIn(const fs::path& dir, const std::string& base) {
  std::error_code ec;
  std::string b = safeShowName(base);
  if (!fs::exists(dir / toPath(b), ec)) return b;
  for (int i = 2; i < 1000; ++i) {
    std::string n = b + " " + std::to_string(i);
    if (!fs::exists(dir / toPath(n), ec)) return n;
  }
  return b + " copy";
}

std::string uniqueShowName(const std::string& base) { return uniqueIn(toPath(showsDir()), base); }

// The show / thumbnail files inside a project folder carry the project's name.
static void renameInner(const fs::path& dir, const std::string& from, const std::string& to) {
  if (from == to) return;
  std::error_code ec;
  fs::rename(dir / toPath(from + kExt), dir / toPath(to + kExt), ec);
  fs::rename(dir / toPath(from + kThumbExt), dir / toPath(to + kThumbExt), ec);
}

bool renameShow(const std::string& from, const std::string& to) {
  std::error_code ec;
  if (showExists(to)) return false;
  fs::rename(toPath(projectDir(from)), toPath(projectDir(to)), ec);
  if (ec) return false;
  renameInner(toPath(projectDir(to)), from, to);
  return true;
}

bool deleteShow(const std::string& name) {
  std::error_code ec;
  // Deleted projects go to a "Deleted" folder instead of vanishing, in case of a wrong tap.
  fs::path bin = toPath(showsDir()) / "Deleted";
  fs::create_directories(bin, ec);
  fs::path dst = bin / toPath(name);
  fs::remove_all(dst, ec);
  fs::rename(toPath(projectDir(name)), dst, ec);
  if (ec) fs::remove_all(toPath(projectDir(name)), ec);
  return !showExists(name);
}

bool saveThumb(const std::string& name, const std::vector<uint8_t>& rgb) {
  if (rgb.size() != size_t(kThumbW) * kThumbH * 3) return false;
  std::ofstream f(thumbFile(name), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write("TFXT", 4);
  f.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
  return bool(f);
}

bool loadThumb(const std::string& name, std::vector<uint8_t>& rgb) {
  std::ifstream f(thumbFile(name), std::ios::binary);
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

std::vector<DriveInfo> listDrives(bool includeShowsDrive) {
  std::vector<DriveInfo> out;
#ifdef _WIN32
  std::wstring showsRoot = toPath(showsDir()).root_name().wstring();  // "C:"
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if (!(mask & (1u << i))) continue;
    wchar_t root[4] = {wchar_t(L'A' + i), L':', L'\\', 0};
    UINT type = GetDriveTypeW(root);
    if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED && type != DRIVE_REMOTE) continue;
    if (!includeShowsDrive && _wcsnicmp(root, showsRoot.c_str(), 2) == 0) continue;  // the drive the shows are on
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
  (void)includeShowsDrive;
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

std::string driveProjectsDir(const std::string& root) { return (toPath(root) / "TouchFX Projects").u8string(); }

std::vector<ShowInfo> listDriveShows(const std::string& root) {
  std::vector<ShowInfo> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(toPath(driveProjectsDir(root)), ec)) {
    if (!e.is_directory()) continue;
    std::string name = e.path().filename().u8string();
    fs::path file = e.path() / toPath(name + kExt);
    if (!fs::exists(file, ec)) continue;
    out.push_back({name, e.path().u8string(), true, fileTime(file)});
  }
  // single-file shows (exported by 0.9.2 / 0.9.3, or copied by hand)
  for (fs::path dir : {toPath(root) / "TouchFX Shows", toPath(root)})
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file() || e.path().extension() != kExt) continue;
      out.push_back({e.path().stem().u8string(), e.path().u8string(), false, fileTime(e.path())});
    }
  std::sort(out.begin(), out.end(), [](const ShowInfo& a, const ShowInfo& b) { return a.modified > b.modified; });
  return out;
}

namespace {

struct CopyItem {
  fs::path from, to;
  uint64_t size;
};

// Copies the list into a temporary ".part" folder, then puts it in place under `dst` (replacing what is there
// when asked). Runs on its own thread.
void runCopy(std::shared_ptr<CopyJob> job, std::vector<CopyItem> items, fs::path part, fs::path dst, bool replace) {
  std::error_code ec;
  std::vector<char> buf(4 << 20);
  bool ok = true;
  std::string err;
  for (const CopyItem& it : items) {
    if (job->cancel) {
      ok = false;
      err = "Cancelled.";
      break;
    }
    fs::create_directories(it.to.parent_path(), ec);
    std::ifstream in(it.from, std::ios::binary);
    std::ofstream out(it.to, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
      ok = false;
      err = "Could not copy " + it.from.filename().u8string() + ".";
      break;
    }
    while (in && !job->cancel) {
      in.read(buf.data(), std::streamsize(buf.size()));
      std::streamsize n = in.gcount();
      if (n <= 0) break;
      out.write(buf.data(), n);
      if (!out) {
        ok = false;
        err = "The drive is full or was removed.";
        break;
      }
      job->bytesDone += uint64_t(n);
    }
    if (!ok) break;
    out.close();
    if (!out) {
      ok = false;
      err = "The drive is full or was removed.";
      break;
    }
  }
  if (ok && job->cancel) {
    ok = false;
    err = "Cancelled.";
  }
  if (ok) {
    if (replace && fs::exists(dst, ec)) {
      fs::path old = dst;
      old += ".old";
      fs::remove_all(old, ec);
      fs::rename(dst, old, ec);
      fs::remove_all(old, ec);
    }
    fs::rename(part, dst, ec);
    if (ec) {
      ok = false;
      err = "Could not finish the copy (" + ec.message() + ").";
    }
  }
  if (!ok) fs::remove_all(part, ec);
  job->ok = ok;
  job->error = err;
  job->done = true;
}

// Lists a project folder's files for copying, renaming the show / thumbnail to newName.
void listFolder(const fs::path& src, const fs::path& part, const std::string& oldName, const std::string& newName,
                std::vector<CopyItem>& items, uint64_t& total) {
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec)) continue;
    fs::path rel = fs::relative(it->path(), src, ec);
    std::string fn = rel.filename().u8string();
    if (rel.parent_path().empty()) {  // the show and its thumbnail are named after the project
      if (fn == oldName + kExt) rel = toPath(newName + kExt);
      else if (fn == oldName + kThumbExt) rel = toPath(newName + kThumbExt);
      else if (fn.size() > 5 && fn.substr(fn.size() - 5) == ".part") continue;
    }
    uint64_t sz = it->file_size(ec);
    items.push_back({it->path(), part / rel, sz});
    total += sz;
  }
}

std::shared_ptr<CopyJob> start(std::vector<CopyItem> items, uint64_t total, const fs::path& dst, bool replace,
                               const std::string& name) {
  auto job = std::make_shared<CopyJob>();
  job->bytesTotal = std::max<uint64_t>(total, 1);
  job->name = name;
  fs::path part = dst;
  part += ".part";
  std::error_code ec;
  fs::remove_all(part, ec);  // a copy interrupted earlier
  std::thread(runCopy, job, std::move(items), part, dst, replace).detach();
  return job;
}

// Name in dir for `wanted`, following the mode. Empty = taken (ask).
std::string pickName(const fs::path& dir, const std::string& wanted, TransferMode mode, bool& replace) {
  std::error_code ec;
  std::string name = safeShowName(wanted);
  replace = false;
  if (fs::exists(dir / toPath(name), ec)) {
    if (mode == XFER_ASK) return "";
    if (mode == XFER_KEEP_BOTH) name = uniqueIn(dir, name);
    else replace = true;
  }
  return name;
}

}  // namespace

std::shared_ptr<CopyJob> exportShow(const std::string& name, const std::string& root, TransferMode mode,
                                    TransferResult& result) {
  fs::path dir = toPath(driveProjectsDir(root));
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    result = XFER_FAILED;
    return nullptr;
  }
  bool replace = false;
  std::string n = pickName(dir, name, mode, replace);
  if (n.empty()) {
    result = XFER_EXISTS;
    return nullptr;
  }
  fs::path dst = dir / toPath(n), part = dst;
  part += ".part";
  std::vector<CopyItem> items;
  uint64_t total = 0;
  listFolder(toPath(projectDir(name)), part, name, n, items, total);
  result = XFER_OK;
  return start(std::move(items), total, dst, replace, n);
}

std::shared_ptr<CopyJob> importShow(const ShowInfo& src, TransferMode mode, TransferResult& result) {
  fs::path dir = toPath(showsDir());
  bool replace = false;
  std::string n = pickName(dir, src.name, mode, replace);
  if (n.empty()) {
    result = XFER_EXISTS;
    return nullptr;
  }
  fs::path dst = dir / toPath(n), part = dst;
  part += ".part";
  std::vector<CopyItem> items;
  uint64_t total = 0;
  std::error_code ec;
  if (src.folder) {
    listFolder(toPath(src.path), part, src.name, n, items, total);
  } else {  // an old single-file show: becomes a project folder
    fs::path f = toPath(src.path), th = f;
    th.replace_extension(kThumbExt);
    uint64_t sz = fs::file_size(f, ec);
    items.push_back({f, part / toPath(n + kExt), sz});
    total += sz;
    if (fs::exists(th, ec)) {
      uint64_t ts = fs::file_size(th, ec);
      items.push_back({th, part / toPath(n + kThumbExt), ts});
      total += ts;
    }
  }
  result = XFER_OK;
  return start(std::move(items), total, dst, replace, n);
}

std::shared_ptr<CopyJob> duplicateShow(const std::string& from, const std::string& to) {
  std::string n = uniqueShowName(to);
  fs::path dst = toPath(projectDir(n)), part = dst;
  part += ".part";
  std::vector<CopyItem> items;
  uint64_t total = 0;
  listFolder(toPath(projectDir(from)), part, from, n, items, total);
  return start(std::move(items), total, dst, false, n);
}
