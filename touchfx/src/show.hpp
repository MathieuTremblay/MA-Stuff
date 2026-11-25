#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Show files live in "Documents/TouchFX Shows" as <name>.tfxshow (JSON), with <name>.thumb (small preview).
struct ShowInfo {
  std::string name;
  std::string path;
  std::time_t modified = 0;
};

std::string showsDir();
std::vector<ShowInfo> listShows();  // newest first
std::string showPath(const std::string& name);
bool showExists(const std::string& name);
std::string safeShowName(const std::string& name);  // strips characters Windows can't use in file names
std::string uniqueShowName(const std::string& base);
bool renameShow(const std::string& from, const std::string& to);
bool duplicateShow(const std::string& from, const std::string& to);
bool deleteShow(const std::string& name);

// Thumbnails: tiny RGB image of the last look.
constexpr int kThumbW = 192, kThumbH = 108;
bool saveThumb(const std::string& name, const std::vector<uint8_t>& rgb);  // kThumbW*kThumbH*3, top row first
bool loadThumb(const std::string& name, std::vector<uint8_t>& rgb);

// ---- import / export (USB sticks and other drives) ----
struct DriveInfo {
  std::string root;   // e.g. "E:\\" (Linux: a mount folder)
  std::string label;  // e.g. "USB (E:) SANDISK"
  bool removable = false;
};
std::vector<DriveInfo> listDrives();  // removable drives first; the drive with the shows folder is left out
std::string driveShowsDir(const std::string& root);  // <root>/TouchFX Shows
std::vector<ShowInfo> listShowsIn(const std::string& dir);  // newest first
enum TransferResult { XFER_OK, XFER_EXISTS, XFER_FAILED };
enum TransferMode { XFER_ASK, XFER_REPLACE, XFER_KEEP_BOTH };
// Copies the show (and its thumbnail) to <root>/TouchFX Shows. outName: the name it got there.
TransferResult exportShow(const std::string& name, const std::string& root, TransferMode mode, std::string& outName);
// Copies a show file (and its thumbnail next to it) into the shows folder.
TransferResult importShow(const std::string& path, TransferMode mode, std::string& outName);
