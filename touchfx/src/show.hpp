#pragma once
#include <atomic>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Each show is a PROJECT FOLDER in "Documents/TouchFX Projects":
//   <name>/<name>.tfxshow   looks, scenes, cues, palettes, DMX patch, media library (JSON)
//   <name>/<name>.thumb     small preview for the launcher
//   <name>/media/           clips converted to HAP (one .mov per clip) and their thumbnails
// Shows from before 0.9.4 ("Documents/TouchFX Shows/<name>.tfxshow") are copied into project folders once.
struct ShowInfo {
  std::string name;
  std::string path;     // the project folder (or, for an old single-file show on a drive, the .tfxshow file)
  bool folder = true;
  std::time_t modified = 0;
};

std::string showsDir();  // the projects folder
std::string projectDir(const std::string& name);
std::string mediaDir(const std::string& name);  // created on demand
std::vector<ShowInfo> listShows();  // newest first
std::string showPath(const std::string& name);
bool showExists(const std::string& name);
std::string safeShowName(const std::string& name);  // strips characters Windows can't use in file names
std::string uniqueShowName(const std::string& base);
bool renameShow(const std::string& from, const std::string& to);
bool deleteShow(const std::string& name);  // moved to the "Deleted" folder

// Thumbnails: tiny RGB image of the last look.
constexpr int kThumbW = 192, kThumbH = 108;
bool saveThumb(const std::string& name, const std::vector<uint8_t>& rgb);  // kThumbW*kThumbH*3, top row first
bool loadThumb(const std::string& name, std::vector<uint8_t>& rgb);

// ---- import / export (USB sticks, SSDs) ----
struct DriveInfo {
  std::string root;   // e.g. "E:\\" (Linux: a mount folder)
  std::string label;  // e.g. "USB (E:) SANDISK"
  bool removable = false;
};
std::vector<DriveInfo> listDrives(bool includeShowsDrive = false);  // removable drives first
std::string driveProjectsDir(const std::string& root);  // <root>/TouchFX Projects
// Projects on a drive: <root>/TouchFX Projects/*, plus old single-file shows (<root>/TouchFX Shows, <root>)
std::vector<ShowInfo> listDriveShows(const std::string& root);

enum TransferResult { XFER_OK, XFER_EXISTS, XFER_FAILED };
enum TransferMode { XFER_ASK, XFER_REPLACE, XFER_KEEP_BOTH };

// A copy running in the background (projects with media can be gigabytes). Poll done; cancel stops it and
// leaves nothing half-copied behind.
struct CopyJob {
  std::atomic<bool> done{false}, cancel{false};
  std::atomic<uint64_t> bytesDone{0}, bytesTotal{0};
  bool ok = false;           // valid once done
  std::string error;         // valid once done
  std::string name;          // the name the project got
  float progress() const {
    uint64_t t = bytesTotal.load();
    return t ? float(double(bytesDone.load()) / double(t)) : 0.f;
  }
};
// Each returns nullptr with result XFER_EXISTS when the name is taken and mode is XFER_ASK.
std::shared_ptr<CopyJob> exportShow(const std::string& name, const std::string& root, TransferMode mode,
                                    TransferResult& result);
std::shared_ptr<CopyJob> importShow(const ShowInfo& src, TransferMode mode, TransferResult& result);
std::shared_ptr<CopyJob> duplicateShow(const std::string& from, const std::string& to);
