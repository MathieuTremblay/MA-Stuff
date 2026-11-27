#pragma once
// Media import: every clip is converted once to HAP Q (or HAP Alpha when it has transparency), without
// audio, into the project's media folder. FFmpeg does the conversion in the background, one clip at a time.
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "process.hpp"
#include "state.hpp"

bool isMediaFile(const std::string& path);  // a video or picture TouchFX can import
bool isStillImage(const std::string& path);

class MediaImporter {
 public:
  MediaImporter();
  ~MediaImporter();
  // The project's media folder. Changing it stops a conversion in progress (the clip stays to be converted).
  void setRoot(const std::string& dir);
  const std::string& root() const { return root_; }
  std::string clipPath(const std::string& id) const;
  std::string thumbPath(const std::string& id) const;

  // Adds the files to the group as clips and converts them in the background. Returns how many were added.
  int import(MediaLibrary& lib, int group, const std::vector<std::string>& files);
  // After a show is opened: clips whose conversion did not finish are converted again; finished clips whose
  // file is gone are marked.
  void resume(MediaLibrary& lib);
  void remove(MediaLibrary& lib, const std::string& id);  // the clip and its files
  void retry(MediaLibrary& lib, const std::string& id);
  void update(MediaLibrary& lib);  // main thread, every frame

  float progress(const std::string& id) const;  // the clip being converted: 0..1, else -1
  int pending() const { return int(queue_.size()) + (job_ ? 1 : 0); }
  // bumped whenever a thumbnail is (re)made, so the UI reloads it
  int thumbVersion() const { return thumbVersion_; }

 private:
  struct Job;
  void startNext(MediaLibrary& lib);
  void finish(MediaLibrary& lib, bool ok, const std::string& err);
  std::string root_;
  std::deque<std::string> queue_;
  std::unique_ptr<Job> job_;
  int thumbVersion_ = 0;
};

// Thumbnails for the MEDIA window's file browser: a strip of frames across each video (one frame for a picture),
// made by FFmpeg in the background and cached on disk, so a folder shows at once the second time.
class FileThumbs {
 public:
  struct Info {
    bool ready = false, failed = false;
    std::string strip;     // .ppm: `frames` thumbnails side by side, kW x kH each
    int frames = 1;
    float duration = -1;   // seconds (videos)
    int width = 0, height = 0;
    std::string codec;
    int version = 0;       // changes when the strip is made
  };
  static constexpr int kW = 160, kH = 90, kFrames = 6;
  ~FileThumbs();
  void setDir(const std::string& cacheDir);
  // Info for a file (asks for it in the background when missing; the most recent requests go first).
  const Info& get(const std::string& path, uint64_t size, long long mtime);
  void update();  // main thread, every frame
  int pending() const { return int(queue_.size()) + (busy_ ? 1 : 0); }

 private:
  struct Item {
    std::string path, key;
    Info info;
  };
  std::string dir_;
  std::map<std::string, Item> items_;  // by key
  std::deque<std::string> queue_;
  Process proc_;
  bool busy_ = false;
  int step_ = 0;
  std::string cur_;
};
