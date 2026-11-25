#pragma once
// Video input: NDI, capture cards / cameras, Spout, Blackmagic DeckLink, AJA, or a test source.
// Capture runs on its own thread; the newest frame is uploaded and converted to RGB on the GPU each frame.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "glutil.hpp"

enum PixFmt { PIX_BGRA, PIX_UYVY, PIX_YUY2, PIX_NV12 };

struct RawFrame {
  int w = 0, h = 0;
  int fmt = PIX_BGRA;
  int stride = 0;  // bytes per row (NV12: luma row; chroma rows follow the luma plane with the same stride)
  std::vector<uint8_t> data;
  std::string info;  // e.g. "1920x1080 50p" (filled by the backend when it changes)
  // YUV formats: colour matrix (0 = from the size: BT.709 for HD, BT.601 for SD; 601; 709) and range
  int matrix = 0;
  bool fullRange = false;  // 0-255 (cameras / JPEG-style) instead of 16-235 (video)
};

// One kind of source. Everything runs on the video thread.
class VideoBackend {
 public:
  virtual ~VideoBackend() = default;
  virtual bool open(const std::string& source, std::string& err) = 0;
  // 1 = new frame in f, 0 = nothing yet (keep waiting), -1 = the source is gone (reopen)
  virtual int read(RawFrame& f, int timeoutMs) = 0;
  virtual void close() = 0;
  virtual std::string status() { return ""; }  // extra status, e.g. "No signal on the input"
};

std::unique_ptr<VideoBackend> makeTestBackend();
std::unique_ptr<VideoBackend> makeNdiBackend();
std::unique_ptr<VideoBackend> makeCaptureBackend();   // Windows Media Foundation (USB capture, cameras)
std::unique_ptr<VideoBackend> makeDeckLinkBackend();  // Blackmagic (needs Desktop Video installed)
std::unique_ptr<VideoBackend> makeAjaBackend();       // AJA (needs the AJA driver installed)
std::vector<std::string> listSources(int type);       // blocking; called on the source-list thread

enum VideoState { VS_OFF, VS_CONNECTING, VS_LIVE, VS_LOST };

class VideoInput {
 public:
  VideoInput();
  ~VideoInput();
  bool init();
  // Main thread. Restarts capture when the type / source changes.
  void configure(int type, const std::string& source);
  void update(float dt);   // main thread, GL: uploads the newest frame
  void pauseNdi(bool p);   // NDI restarts when the NDI output card changes
  // FREEZE: hold the current picture (also through a lost signal) until unfrozen. A live control, not saved.
  void setFrozen(bool f) { frozen_ = f; }
  // small RGB copy of the picture (64 x 36, row 0 at the bottom) for the VIDEO colour mode; made only when wanted
  void setWantThumb(bool w) { wantThumb_ = w; }
  const float* thumb() const { return thumb_.empty() ? nullptr : thumb_.data(); }
  int thumbW() const { return 64; }
  int thumbH() const { return 36; }
  bool frozen() const { return frozen_; }
  GLuint texture() const { return out_.tex; }
  int width() const { return out_.w; }
  int height() const { return out_.h; }
  float level() const { return level_; }  // 1 live, fades to 0 two seconds after the signal is lost
  bool hasPicture() const { return out_.tex != 0 && level_ > 0.001f; }
  int state() const { return state_; }
  int type() const { return type_; }
  std::string statusText() const;
  std::string formatText() const;
  double framesPerSecond() const { return fps_; }
  uint64_t framesReceived() const { return frames_; }

  // Source lists, refreshed in the background (NDI discovery takes a moment).
  void requestSourceList(int type);
  std::vector<std::string> sourceList(int type);
  bool listing() const;

  // Spout input lives on the GL thread (Windows only).
  void spoutUpdate();

 private:
  void startThread();
  void stopThread();
  void upload(const RawFrame& f);

 public:
  // The capture thread shares this with VideoInput. TouchFX never waits long for it: a source that hangs
  // (a driver stuck in a read) is left behind on its own copy, and a new one starts.
  struct Shared {
    std::atomic<bool> quit{false}, done{false};
    std::mutex m;
    RawFrame latest;
    bool fresh = false, opened = false;
    std::string error, backendStatus;
  };

 private:
  std::shared_ptr<Shared> sh_ = std::make_shared<Shared>();
  std::thread thread_;
  std::atomic<bool> ndiPaused_{false};
  int type_ = 0;
  std::string source_;

  mutable std::mutex mtx_;  // info_
  std::string info_;

  // main thread
  bool frozen_ = false;
  bool wantThumb_ = false;
  uint64_t thumbFrame_ = 0;
  Target thumbA_, thumbB_;
  std::vector<float> thumb_;
  void makeThumb();
  int state_ = VS_OFF;
  double sinceFrame_ = 1e9;
  float level_ = 0;
  double fps_ = 0, fpsAcc_ = 0;
  int fpsCount_ = 0;
  uint64_t frames_ = 0;
  RawFrame work_;
  GLuint raw_[2] = {0, 0};  // uploaded planes
  int rawW_[2] = {0, 0}, rawH_[2] = {0, 0};
  GLenum rawFmt_[2] = {0, 0};
  Target out_;
  Program convert_;
  bool ready_ = false;

  // source lists
  struct ListJob {
    std::atomic<bool> done{false};
    int type = 0;
    std::vector<std::string> result;
    std::chrono::steady_clock::time_point started;
  };
  std::shared_ptr<ListJob> listJob_;
  std::vector<std::string> lists_[8];

  struct SpoutIn;
  std::unique_ptr<SpoutIn> spout_;
};
