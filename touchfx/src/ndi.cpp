#include "ndi.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// ---- Minimal declarations matching the NDI SDK C API (Processing.NDI.*.h, v5/v6) ----
namespace ndi {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
         (uint32_t(uint8_t(d)) << 24);
}

struct send_create_t {
  const char* p_ndi_name;
  const char* p_groups;
  bool clock_video, clock_audio;
};

struct video_frame_v2_t {
  int xres, yres;
  uint32_t FourCC;  // enum in the SDK (32-bit)
  int frame_rate_N, frame_rate_D;
  float picture_aspect_ratio;
  int frame_format_type;  // enum: 1 = progressive
  int64_t timecode;
  uint8_t* p_data;
  int line_stride_in_bytes;  // union with data_size_in_bytes
  const char* p_metadata;
  int64_t timestamp;
};

constexpr int64_t kTimecodeSynthesize = INT64_MAX;

using fn_initialize = bool (*)(void);
using fn_destroy = void (*)(void);
using fn_send_create = void* (*)(const send_create_t*);
using fn_send_destroy = void (*)(void*);
using fn_send_video_v2 = void (*)(void*, const video_frame_v2_t*);
using fn_send_get_no_connections = int (*)(void*, uint32_t);

fn_initialize initialize = nullptr;
fn_destroy destroy = nullptr;
fn_send_create send_create = nullptr;
fn_send_destroy send_destroy = nullptr;
fn_send_video_v2 send_video_v2 = nullptr;
fn_send_get_no_connections send_get_no_connections = nullptr;

}  // namespace ndi

static void* openLib(const std::string& path) {
#ifdef _WIN32
  return (void*)LoadLibraryA(path.c_str());
#else
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* sym(void* lib, const char* name) {
#ifdef _WIN32
  return (void*)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}

bool NdiSender::loadRuntime() {
  if (lib_) return true;
  std::vector<std::string> candidates;
#ifdef _WIN32
  const char* file = "Processing.NDI.Lib.x64.dll";
  const char sep = '\\';
#else
  const char* file = "libndi.so.6";
  const char sep = '/';
#endif
  for (const char* var : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"}) {
    if (const char* dir = std::getenv(var)) candidates.push_back(std::string(dir) + sep + file);
  }
  candidates.push_back(file);  // next to the exe / on PATH
#ifndef _WIN32
  candidates.push_back("libndi.so.5");
  candidates.push_back("libndi.so");
#endif
  for (const auto& c : candidates) {
    lib_ = openLib(c);
    if (lib_) break;
  }
  if (!lib_) {
    error_ = "NDI Runtime not found. Install NDI Tools from ndi.video, then restart TouchFX.";
    return false;
  }
  ndi::initialize = (ndi::fn_initialize)sym(lib_, "NDIlib_initialize");
  ndi::destroy = (ndi::fn_destroy)sym(lib_, "NDIlib_destroy");
  ndi::send_create = (ndi::fn_send_create)sym(lib_, "NDIlib_send_create");
  ndi::send_destroy = (ndi::fn_send_destroy)sym(lib_, "NDIlib_send_destroy");
  ndi::send_video_v2 = (ndi::fn_send_video_v2)sym(lib_, "NDIlib_send_send_video_v2");
  ndi::send_get_no_connections = (ndi::fn_send_get_no_connections)sym(lib_, "NDIlib_send_get_no_connections");
  if (!ndi::initialize || !ndi::send_create || !ndi::send_destroy || !ndi::send_video_v2) {
    error_ = "The installed NDI Runtime is not compatible.";
    lib_ = nullptr;
    return false;
  }
  if (!ndi::initialize()) {
    error_ = "NDI could not start (this CPU is not supported by NDI).";
    lib_ = nullptr;
    return false;
  }
  return true;
}

NdiSender::~NdiSender() {
  stop();
  if (lib_ && ndi::destroy) ndi::destroy();
}

bool NdiSender::start(const std::string& name) {
  stop();
  if (!loadRuntime()) return false;
  ndi::send_create_t desc{name.c_str(), nullptr, false, false};
  instance_ = ndi::send_create(&desc);
  if (!instance_) {
    error_ = "Could not create the NDI sender \"" + name + "\".";
    return false;
  }
  name_ = name;
  error_.clear();
  quit_ = false;
  hasPending_ = false;
  thread_ = std::thread(&NdiSender::threadMain, this);
  return true;
}

void NdiSender::stop() {
  if (thread_.joinable()) {
    {
      std::lock_guard<std::mutex> lk(mtx_);
      quit_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }
  if (instance_) ndi::send_destroy(instance_);
  instance_ = nullptr;
  connections_ = 0;
}

void NdiSender::submit(const uint8_t* bgra, int w, int h, int fps) {
  if (!instance_) return;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t bytes = size_t(w) * h * 4;
    pending_.resize(bytes);
    std::memcpy(pending_.data(), bgra, bytes);
    pw_ = w;
    ph_ = h;
    pfps_ = fps;
    hasPending_ = true;
  }
  cv_.notify_one();
}

void NdiSender::threadMain() {
  using clock = std::chrono::steady_clock;
  auto lastPoll = clock::now() - std::chrono::seconds(1);
  for (;;) {
    int w, h, fps;
    {
      std::unique_lock<std::mutex> lk(mtx_);
      cv_.wait_for(lk, std::chrono::milliseconds(250), [&] { return quit_ || hasPending_; });
      if (quit_) return;
      if (hasPending_) {
        std::swap(pending_, sending_);
        hasPending_ = false;
        w = pw_;
        h = ph_;
        fps = pfps_;
      } else {
        w = 0;
        h = 0;
        fps = 0;
      }
    }
    if (w > 0) {
      ndi::video_frame_v2_t f{};
      f.xres = w;
      f.yres = h;
      f.FourCC = ndi::fourcc('B', 'G', 'R', 'X');
      f.frame_rate_N = fps;
      f.frame_rate_D = 1;
      f.picture_aspect_ratio = float(w) / float(h);
      f.frame_format_type = 1;
      f.timecode = ndi::kTimecodeSynthesize;
      f.p_data = sending_.data();
      f.line_stride_in_bytes = w * 4;
      f.p_metadata = nullptr;
      f.timestamp = 0;
      ndi::send_video_v2(instance_, &f);  // synchronous: done with the buffer when it returns
    }
    if (ndi::send_get_no_connections && clock::now() - lastPoll > std::chrono::milliseconds(500)) {
      connections_ = ndi::send_get_no_connections(instance_, 0);
      lastPoll = clock::now();
    }
  }
}
