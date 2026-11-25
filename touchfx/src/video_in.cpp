#include "video_in.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>

#include "effects.hpp"
#include "log.hpp"
#include "ndi.hpp"
#include "state.hpp"

#ifdef TOUCHFX_SPOUT
#include "SpoutReceiver.h"
#include "SpoutSenderNames.h"
#endif

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------------------------------------
// GPU conversion: raw planes -> RGBA (row 0 at the bottom, like every other texture in TouchFX)
namespace {
const char* kConvertFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uP0; uniform sampler2D uP1;
uniform int uFmt; uniform vec2 uSize; uniform int uHd; uniform int uFull;
vec3 yuv(float y, float u, float v) {
  // limited (16-235) or full range; BT.709 or BT.601 matrix
  if (uFull == 1) {
    u -= 128.0 / 255.0;
    v -= 128.0 / 255.0;
  } else {
    y = (y - 16.0 / 255.0) * (255.0 / 219.0);
    u = (u - 128.0 / 255.0) * (255.0 / 224.0);
    v = (v - 128.0 / 255.0) * (255.0 / 224.0);
  }
  vec3 c = uHd == 1 ? vec3(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u)
                    : vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
  return clamp(c, 0.0, 1.0);
}
void main() {
  vec2 uv = vec2(vUv.x, 1.0 - vUv.y);  // frames arrive top row first
  if (uFmt == 0) {  // BGRA (uploaded with GL_BGRA, so .rgb is already right)
    o = vec4(texture(uP0, uv).rgb, 1.0);
    return;
  }
  if (uFmt == 3) {  // NV12: Y plane + interleaved UV plane at half size
    float y = texture(uP0, uv).r;
    vec2 c = texture(uP1, uv).rg;
    o = vec4(yuv(y, c.r, c.g), 1.0);
    return;
  }
  // 4:2:2 packed: one RGBA texel = two pixels
  float px = uv.x * uSize.x;
  vec2 tuv = vec2((floor(px * 0.5) + 0.5) / (uSize.x * 0.5), uv.y);
  vec4 t = texture(uP0, tuv);
  bool odd = mod(floor(px), 2.0) > 0.5;
  float y, u, v;
  if (uFmt == 1) {  // UYVY: U Y0 V Y1
    u = t.r; v = t.b; y = odd ? t.a : t.g;
  } else {          // YUY2: Y0 U Y1 V
    u = t.g; v = t.a; y = odd ? t.b : t.r;
  }
  o = vec4(yuv(y, u, v), 1.0);
}
)";
}  // namespace

struct VideoInput::SpoutIn {
#ifdef TOUCHFX_SPOUT
  SpoutReceiver rx;
  bool connected = false;
  std::string name;
  GLuint tex = 0;
  int w = 0, h = 0;
  ~SpoutIn() {
    rx.ReleaseReceiver();
    if (tex) glDeleteTextures(1, &tex);
  }
#endif
};

VideoInput::VideoInput() = default;

VideoInput::~VideoInput() {
  stopThread();
  if (listThread_.joinable()) listThread_.join();
}

bool VideoInput::init() {
  ready_ = convert_.build("video.convert", kFullscreenVS, kConvertFS);
  return ready_;
}

void VideoInput::configure(int type, const std::string& source) {
  if (type == type_ && source == source_) return;
  stopThread();
  type_ = type;
  source_ = source;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    error_.clear();
    backendStatus_.clear();
    info_.clear();
    fresh_ = false;
    opened_ = false;
  }
  state_ = type == VSRC_OFF ? VS_OFF : VS_CONNECTING;
  sinceFrame_ = 1e9;
  spout_.reset();
  if (type != VSRC_OFF && type != VSRC_SPOUT && !(type == VSRC_NDI && ndiPaused_)) startThread();
  logLine("Video input: type %d, source \"%s\"", type, source.c_str());
}

void VideoInput::pauseNdi(bool p) {
  if (p == ndiPaused_) return;
  ndiPaused_ = p;
  if (type_ != VSRC_NDI) return;
  if (p) stopThread();
  else startThread();
}

void VideoInput::startThread() {
  stopThread();
  quit_ = false;
  thread_ = std::thread(&VideoInput::threadMain, this, type_, source_);
}

void VideoInput::stopThread() {
  if (!thread_.joinable()) return;
  quit_ = true;
  thread_.join();
}

void VideoInput::threadMain(int type, std::string source) {
  while (!quit_) {
    std::unique_ptr<VideoBackend> b;
    switch (type) {
      case VSRC_TEST: b = makeTestBackend(); break;
      case VSRC_NDI: b = makeNdiBackend(); break;
      case VSRC_CAPTURE: b = makeCaptureBackend(); break;
      case VSRC_DECKLINK: b = makeDeckLinkBackend(); break;
      case VSRC_AJA: b = makeAjaBackend(); break;
      default: return;
    }
    std::string err;
    bool ok = b && b->open(source, err);
    {
      std::lock_guard<std::mutex> lk(mtx_);
      error_ = ok ? "" : (err.empty() ? "Could not open the video source." : err);
      opened_ = ok;
    }
    if (!ok) {
      if (b) b->close();
      for (int i = 0; i < 20 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;  // try again
    }
    RawFrame f;
    while (!quit_) {
      int r = b->read(f, 100);
      {
        std::lock_guard<std::mutex> lk(mtx_);
        backendStatus_ = b->status();
      }
      if (r < 0) break;
      if (r == 0) continue;
      std::lock_guard<std::mutex> lk(mtx_);
      std::swap(latest_, f);
      fresh_ = true;
    }
    b->close();
    {
      std::lock_guard<std::mutex> lk(mtx_);
      opened_ = false;
    }
    for (int i = 0; i < 10 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void VideoInput::upload(const RawFrame& f) {
  if (!ready_ || f.w <= 0 || f.h <= 0 || f.data.empty()) return;
  int w = f.w, h = f.h;
  // sanity: the buffer must hold the frame
  size_t need = f.fmt == PIX_NV12 ? size_t(f.stride) * h * 3 / 2 : size_t(f.stride) * h;
  if (f.stride <= 0 || f.data.size() < need) return;
  auto plane = [&](int i, int pw, int ph, GLenum ifmt, GLenum fmt, int rowPixels, const uint8_t* data) {
    if (!raw_[i]) glGenTextures(1, &raw_[i]);
    glBindTexture(GL_TEXTURE_2D, raw_[i]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowPixels);
    if (rawW_[i] != pw || rawH_[i] != ph || rawFmt_[i] != ifmt) {
      glTexImage2D(GL_TEXTURE_2D, 0, ifmt, pw, ph, 0, fmt, GL_UNSIGNED_BYTE, data);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      rawW_[i] = pw;
      rawH_[i] = ph;
      rawFmt_[i] = ifmt;
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, fmt, GL_UNSIGNED_BYTE, data);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  };
  int fmtId = 0;
  switch (f.fmt) {
    case PIX_BGRA: plane(0, w, h, GL_RGBA8, GL_BGRA, f.stride / 4, f.data.data()); fmtId = 0; break;
    case PIX_UYVY: plane(0, w / 2, h, GL_RGBA8, GL_RGBA, f.stride / 4, f.data.data()); fmtId = 1; break;
    case PIX_YUY2: plane(0, w / 2, h, GL_RGBA8, GL_RGBA, f.stride / 4, f.data.data()); fmtId = 2; break;
    case PIX_NV12:
      plane(0, w, h, GL_R8, GL_RED, f.stride, f.data.data());
      plane(1, w / 2, h / 2, GL_RG8, GL_RG, f.stride / 2, f.data.data() + size_t(f.stride) * h);
      fmtId = 3;
      break;
    default: return;
  }
  // nearest filtering for packed 4:2:2 (each texel is two pixels)
  glBindTexture(GL_TEXTURE_2D, raw_[0]);
  GLint filt = (fmtId == 1 || fmtId == 2) ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);

  // limit the converted picture to 1920 wide (output is at most 1080p)
  int ow = w, oh = h;
  if (ow > 1920) {
    oh = int(std::lround(oh * 1920.0 / ow));
    ow = 1920;
  }
  if (out_.w != ow || out_.h != oh) out_.create(ow, oh, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  glDisable(GL_BLEND);
  out_.bind();
  convert_.use();
  bindTex(0, raw_[0]);
  bindTex(1, raw_[1] ? raw_[1] : raw_[0]);
  convert_.seti("uP0", 0);
  convert_.seti("uP1", 1);
  convert_.seti("uFmt", fmtId);
  convert_.set2f("uSize", float(w), float(h));
  convert_.seti("uHd", f.matrix == 709 || (f.matrix == 0 && h >= 720) ? 1 : 0);
  convert_.seti("uFull", f.fullRange ? 1 : 0);
  drawFullscreen();
}

void VideoInput::spoutUpdate() {
#ifdef TOUCHFX_SPOUT
  if (!spout_) spout_ = std::make_unique<SpoutIn>();
  SpoutIn& s = *spout_;
  if (s.name != source_) {
    s.rx.ReleaseReceiver();
    s.rx.SetReceiverName(source_.empty() ? nullptr : source_.c_str());
    s.name = source_;
  }
  if (!s.tex) {
    glGenTextures(1, &s.tex);
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  // one call per frame: connects, and copies the sender's frame into our texture (shared top-down: flip)
  if (!s.rx.ReceiveTexture(s.tex, GL_TEXTURE_2D, true, 0)) {
    s.connected = false;
    std::lock_guard<std::mutex> lk(mtx_);
    backendStatus_ = source_.empty() ? "No Spout sender running" : "Spout sender \"" + source_ + "\" is not running";
    return;
  }
  {
    std::lock_guard<std::mutex> lk(mtx_);
    backendStatus_.clear();
  }
  if (s.rx.IsUpdated()) {  // new sender or new size: re-allocate, the next call copies
    s.w = int(s.rx.GetSenderWidth());
    s.h = int(s.rx.GetSenderHeight());
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.w, s.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    std::lock_guard<std::mutex> lk(mtx_);
    info_ = std::to_string(s.w) + "x" + std::to_string(s.h) + "  " + s.rx.GetSenderName();
    return;
  }
  s.connected = true;
  if (!s.rx.IsFrameNew() || s.w <= 0 || s.h <= 0) return;
  if (out_.w != s.w || out_.h != s.h) out_.create(s.w, s.h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  fxCopy(out_, s.tex, 1.f);
  sinceFrame_ = 0;
  ++frames_;
  ++fpsCount_;
#endif
}

void VideoInput::update(float dt) {
  if (type_ == VSRC_OFF) {
    state_ = VS_OFF;
    level_ = std::max(0.f, level_ - dt * 2.f);
    return;
  }
  sinceFrame_ += dt;
  if (frozen_ && out_.tex) {  // hold the picture: nothing new is shown, nothing fades
    std::lock_guard<std::mutex> lk(mtx_);
    fresh_ = false;  // drop what arrives meanwhile
    level_ = std::min(1.f, level_ + dt * 2.f);
    if (wantThumb_ && thumb_.empty()) makeThumb();
    return;
  }
  if (type_ == VSRC_SPOUT) {
    spoutUpdate();
  } else {
    bool got = false;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      if (fresh_) {
        std::swap(work_, latest_);
        fresh_ = false;
        got = true;
        if (!work_.info.empty()) info_ = work_.info;
      }
    }
    if (got) {
      upload(work_);
      sinceFrame_ = 0;
      ++frames_;
      ++fpsCount_;
    }
  }
  if (wantThumb_ && frames_ != thumbFrame_ && out_.tex) makeThumb();
  fpsAcc_ += dt;
  if (fpsAcc_ >= 1.0) {
    fps_ = fpsCount_ / fpsAcc_;
    fpsAcc_ = 0;
    fpsCount_ = 0;
  }
  // live / lost: hold the last picture 2 s, then fade it out over 1 s
  bool live = sinceFrame_ < 1.0;
  if (live) state_ = VS_LIVE;
  else state_ = frames_ > 0 && sinceFrame_ < 1e8 ? VS_LOST : VS_CONNECTING;
  if (live) level_ = std::min(1.f, level_ + dt * 2.f);
  else if (sinceFrame_ > 2.0) level_ = std::max(0.f, level_ - dt);
}

// 1920 -> 256 -> 64 wide in two linear steps (an average over the area), then read back (small: cheap).
void VideoInput::makeThumb() {
  thumbFrame_ = frames_;
  if (!thumbA_.tex) {
    thumbA_.create(256, 144, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    thumbB_.create(64, 36, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  }
  fxCopy(thumbA_, out_.tex, 1.f);
  fxCopy(thumbB_, thumbA_.tex, 1.f);
  std::vector<uint8_t> px(64 * 36 * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, thumbB_.fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, 64, 36, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  thumb_.resize(64 * 36 * 3);
  for (int i = 0; i < 64 * 36; ++i)
    for (int k = 0; k < 3; ++k) thumb_[size_t(i) * 3 + k] = px[size_t(i) * 4 + k] / 255.f;
}

std::string VideoInput::statusText() const {
  std::lock_guard<std::mutex> lk(mtx_);
  if (frozen_ && out_.tex) return "FROZEN (tap FREEZE or press F to go live)";
  switch (state_) {
    case VS_OFF: return "Off";
    case VS_LIVE: return "Live";
    case VS_LOST:
      if (!error_.empty()) return "Lost: " + error_;
      if (!backendStatus_.empty()) return "Lost: " + backendStatus_;
      return "Signal lost - waiting for it to come back";
    default:
      if (type_ == VSRC_NDI && ndiPaused_) return "Restarting NDI...";
      if (!error_.empty()) return error_;
      if (!backendStatus_.empty()) return backendStatus_;
      return opened_ ? "Waiting for the first frame..." : "Connecting...";
  }
}

std::string VideoInput::formatText() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return info_;
}

// ---- source lists ----
void VideoInput::requestSourceList(int type) {
  if (listBusy_) return;
  if (listThread_.joinable()) listThread_.join();
  listBusy_ = true;
  listThread_ = std::thread([this, type] {
    std::vector<std::string> v;
    if (!(type == VSRC_NDI && ndiPaused_)) v = listSources(type);
    {
      std::lock_guard<std::mutex> lk(listMtx_);
      if (type >= 0 && type < 8) lists_[type] = v;
    }
    listBusy_ = false;
  });
}

std::vector<std::string> VideoInput::sourceList(int type) {
  std::lock_guard<std::mutex> lk(listMtx_);
  return (type >= 0 && type < 8) ? lists_[type] : std::vector<std::string>();
}

// ---------------------------------------------------------------------------------------------------------
// Test source: colour bars with a moving sweep, a clock and a bouncing ball (no hardware needed).
// "Flaky" drops out for 4 seconds every 15 seconds, to try the lost-signal handling.
namespace {
class TestBackend : public VideoBackend {
 public:
  bool open(const std::string& source, std::string&) override {
    flaky_ = source.find("Flaky") != std::string::npos;
    yuv_ = source.find("UYVY") != std::string::npos;
    start_ = Clock::now();
    next_ = start_;
    return true;
  }
  int read(RawFrame& f, int timeoutMs) override {
    auto now = Clock::now();
    if (now < next_) {
      auto wait = std::min(next_ - now, std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(timeoutMs)));
      std::this_thread::sleep_for(wait);
      if (Clock::now() < next_) return 0;
    }
    next_ += std::chrono::microseconds(33333);
    if (Clock::now() > next_ + std::chrono::milliseconds(200)) next_ = Clock::now();
    double t = std::chrono::duration<double>(Clock::now() - start_).count();
    if (flaky_ && std::fmod(t, 15.0) > 11.0) {
      status_ = "Test source dropped out (Flaky)";
      return 0;
    }
    status_.clear();
    const int W = 1280, H = 720;
    f.w = W;
    f.h = H;
    f.fmt = yuv_ ? PIX_UYVY : PIX_BGRA;
    f.stride = yuv_ ? W * 2 : W * 4;
    f.data.resize(size_t(f.stride) * H);
    f.info = std::string("1280x720 30p test ") + (yuv_ ? "UYVY" : "BGRA");
    static const uint8_t bars[8][3] = {{235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
                                       {235, 16, 235},  {235, 16, 16},  {16, 16, 235},  {16, 16, 16}};
    int sweep = int(std::fmod(t * 300.0, double(W)));
    float bx = float(0.5 + 0.4 * std::sin(t * 1.3)) * W, by = float(0.5 + 0.35 * std::cos(t * 1.7)) * H;
    for (int y = 0; y < H; ++y) {
      uint8_t* row = f.data.data() + size_t(y) * f.stride;
      for (int x = 0; x < W; ++x) {
        int r, g, b;
        if (y < H * 2 / 3) {
          const uint8_t* c = bars[x * 8 / W];
          r = c[0]; g = c[1]; b = c[2];
        } else {  // gradient ramp
          r = g = b = 16 + x * 219 / W;
        }
        if (std::abs(x - sweep) < 6) r = g = b = 255;
        float dx = x - bx, dy = y - by;
        if (dx * dx + dy * dy < 70 * 70) { r = 255; g = 60; b = 20; }
        if (yuv_) {
          // BT.709 limited (matches the converter)
          float R = (r - 16) / 219.f, G = (g - 16) / 219.f, B = (b - 16) / 219.f;
          float Y = 0.2126f * R + 0.7152f * G + 0.0722f * B;
          float U = (B - Y) / 1.8556f, V = (R - Y) / 1.5748f;
          uint8_t yy = uint8_t(std::clamp(16.f + Y * 219.f, 0.f, 255.f));
          uint8_t* p = row + (x / 2) * 4;
          if (x % 2 == 0) {
            p[0] = uint8_t(std::clamp(128.f + U * 224.f, 0.f, 255.f));
            p[1] = yy;
            p[2] = uint8_t(std::clamp(128.f + V * 224.f, 0.f, 255.f));
          } else {
            p[3] = yy;
          }
        } else {
          uint8_t* p = row + x * 4;
          p[0] = uint8_t(b); p[1] = uint8_t(g); p[2] = uint8_t(r); p[3] = 255;
        }
      }
    }
    return 1;
  }
  void close() override {}
  std::string status() override { return status_; }

 private:
  bool flaky_ = false, yuv_ = false;
  Clock::time_point start_, next_;
  std::string status_;
};

// ---- NDI receive (runtime loaded by ndi.cpp) ----
struct NdiSource {
  const char* p_ndi_name;
  const char* p_url_address;
};
struct NdiFindCreate {
  bool show_local_sources;
  const char* p_groups;
  const char* p_extra_ips;
};
struct NdiRecvCreateV3 {
  NdiSource source_to_connect_to;
  int color_format;  // 0 = BGRX_BGRA
  int bandwidth;     // 100 = highest
  bool allow_video_fields;
  const char* p_ndi_recv_name;
};
struct NdiVideoFrameV2 {
  int xres, yres;
  uint32_t FourCC;
  int frame_rate_N, frame_rate_D;
  float picture_aspect_ratio;
  int frame_format_type;
  int64_t timecode;
  uint8_t* p_data;
  int line_stride_in_bytes;
  const char* p_metadata;
  int64_t timestamp;
};
using FnFindCreate = void* (*)(const NdiFindCreate*);
using FnFindDestroy = void (*)(void*);
using FnFindWait = bool (*)(void*, uint32_t);
using FnFindSources = const NdiSource* (*)(void*, uint32_t*);
using FnRecvCreate = void* (*)(const NdiRecvCreateV3*);
using FnRecvDestroy = void (*)(void*);
using FnRecvCapture = int (*)(void*, NdiVideoFrameV2*, void*, void*, uint32_t);
using FnRecvFree = void (*)(void*, const NdiVideoFrameV2*);

struct NdiRecvApi {
  FnFindCreate findCreate = nullptr;
  FnFindDestroy findDestroy = nullptr;
  FnFindWait findWait = nullptr;
  FnFindSources findSources = nullptr;
  FnRecvCreate recvCreate = nullptr;
  FnRecvDestroy recvDestroy = nullptr;
  FnRecvCapture recvCapture = nullptr;
  FnRecvFree recvFree = nullptr;
  bool load(std::string& err) {
    if (!ndiRuntimeInit(err)) return false;
    findCreate = (FnFindCreate)ndiSymbol("NDIlib_find_create_v2");
    findDestroy = (FnFindDestroy)ndiSymbol("NDIlib_find_destroy");
    findWait = (FnFindWait)ndiSymbol("NDIlib_find_wait_for_sources");
    findSources = (FnFindSources)ndiSymbol("NDIlib_find_get_current_sources");
    recvCreate = (FnRecvCreate)ndiSymbol("NDIlib_recv_create_v3");
    recvDestroy = (FnRecvDestroy)ndiSymbol("NDIlib_recv_destroy");
    recvCapture = (FnRecvCapture)ndiSymbol("NDIlib_recv_capture_v2");
    recvFree = (FnRecvFree)ndiSymbol("NDIlib_recv_free_video_v2");
    if (!findCreate || !findDestroy || !findSources || !recvCreate || !recvDestroy || !recvCapture || !recvFree) {
      err = "The installed NDI Runtime cannot receive video (too old?).";
      return false;
    }
    return true;
  }
};

std::vector<std::string> ndiFind(NdiRecvApi& api, uint32_t waitMs) {
  std::vector<std::string> out;
  NdiFindCreate fc{true, nullptr, nullptr};
  void* f = api.findCreate(&fc);
  if (!f) return out;
  auto until = Clock::now() + std::chrono::milliseconds(waitMs);
  while (Clock::now() < until) {
    if (api.findWait) api.findWait(f, 250);
    else std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  uint32_t n = 0;
  const NdiSource* s = api.findSources(f, &n);
  for (uint32_t i = 0; s && i < n; ++i)
    if (s[i].p_ndi_name) out.push_back(s[i].p_ndi_name);
  api.findDestroy(f);
  return out;
}

class NdiBackend : public VideoBackend {
 public:
  bool open(const std::string& source, std::string& err) override {
    if (!api_.load(err)) return false;
    std::string name = source;
    if (name.empty()) {  // first source found
      auto list = ndiFind(api_, 2000);
      if (list.empty()) {
        err = "No NDI source found on the network.";
        return false;
      }
      name = list[0];
    }
    name_ = name;
    NdiRecvCreateV3 rc{};
    rc.source_to_connect_to.p_ndi_name = name_.c_str();
    rc.source_to_connect_to.p_url_address = nullptr;
    rc.color_format = 0;      // BGRX / BGRA
    rc.bandwidth = 100;       // full quality
    rc.allow_video_fields = false;
    rc.p_ndi_recv_name = "TouchFX video in";
    recv_ = api_.recvCreate(&rc);
    if (!recv_) {
      err = "Could not connect to NDI source \"" + name_ + "\".";
      return false;
    }
    lastFrame_ = Clock::now();
    return true;
  }
  int read(RawFrame& f, int timeoutMs) override {
    NdiVideoFrameV2 v{};
    int type = api_.recvCapture(recv_, &v, nullptr, nullptr, uint32_t(timeoutMs));
    if (type == 4) return -1;  // error: the connection is gone
    if (type != 1) {
      if (Clock::now() - lastFrame_ > std::chrono::seconds(1)) status_ = "No video from \"" + name_ + "\"";
      return 0;
    }
    lastFrame_ = Clock::now();
    status_.clear();
    if (v.p_data && v.xres > 0 && v.yres > 0 && v.line_stride_in_bytes >= v.xres * 4) {
      f.w = v.xres;
      f.h = v.yres;
      f.fmt = PIX_BGRA;
      f.stride = v.line_stride_in_bytes;
      f.data.assign(v.p_data, v.p_data + size_t(v.line_stride_in_bytes) * v.yres);
      char b[160];
      double fps = v.frame_rate_D ? double(v.frame_rate_N) / v.frame_rate_D : 0;
      std::snprintf(b, sizeof(b), "%dx%d %.2f fps  %s", v.xres, v.yres, fps, name_.c_str());
      f.info = b;
    }
    api_.recvFree(recv_, &v);
    return 1;
  }
  void close() override {
    if (recv_) api_.recvDestroy(recv_);
    recv_ = nullptr;
  }
  std::string status() override { return status_; }

 private:
  NdiRecvApi api_;
  void* recv_ = nullptr;
  std::string name_, status_;
  Clock::time_point lastFrame_;
};

}  // namespace

std::unique_ptr<VideoBackend> makeTestBackend() { return std::make_unique<TestBackend>(); }
std::unique_ptr<VideoBackend> makeNdiBackend() { return std::make_unique<NdiBackend>(); }

std::vector<std::string> listCaptureDevices();   // video_mf.cpp
std::vector<std::string> listDeckLinkDevices();  // video_decklink.cpp
std::vector<std::string> listAjaDevices();       // video_aja.cpp

std::vector<std::string> listSources(int type) {
  switch (type) {
    case VSRC_TEST: return {"Colour bars", "Colour bars UYVY", "Flaky (drops out)"};
    case VSRC_NDI: {
      NdiRecvApi api;
      std::string err;
      if (!api.load(err)) return {};
      return ndiFind(api, 2500);
    }
    case VSRC_CAPTURE: return listCaptureDevices();
    case VSRC_DECKLINK: return listDeckLinkDevices();
    case VSRC_AJA: return listAjaDevices();
    case VSRC_SPOUT: {
#ifdef TOUCHFX_SPOUT
      spoutSenderNames names;
      std::vector<std::string> v;
      std::set<std::string> set;
      if (names.GetSenderNames(&set))
        for (const auto& n : set) v.push_back(n);
      return v;
#else
      return {};
#endif
    }
  }
  return {};
}
