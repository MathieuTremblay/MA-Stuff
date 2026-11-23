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
