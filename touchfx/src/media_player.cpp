#include "media_player.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "hap.hpp"
#include "log.hpp"
#include "mathutil.hpp"
#include "media_lib.hpp"

namespace {

// One clip drawn into the target, premultiplied, times uWeight (added on top: crossfades are two passes).
const char* kClipFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uTex0, uTex1;
uniform int uFmt, uAlphaTex, uFlipY;
uniform vec2 uTexScale;      // picture size / texture size (HAP textures are padded to multiples of 4)
uniform vec2 uCenter, uSize; // where the clip goes (output UV)
uniform float uAspect, uRot; // output aspect, rotation (radians)
uniform vec2 uFlip;          // 1 or -1
uniform vec4 uCrop;          // left, right, top, bottom (0..1 of the clip)
uniform float uGamma, uHue, uSat, uWeight, uLevel;
void main() {
  vec2 p = (vUv - uCenter) * vec2(uAspect, 1.0);
  float c = cos(-uRot), s = sin(-uRot);
  p = vec2(p.x * c - p.y * s, p.x * s + p.y * c);
  p /= vec2(uAspect, 1.0);
  vec2 l = p / uSize * uFlip + 0.5;  // 0..1 inside the clip, y up
  if (l.x < uCrop.x || l.x > 1.0 - uCrop.y || l.y < uCrop.w || l.y > 1.0 - uCrop.z) { o = vec4(0.0); return; }
  vec2 t = vec2(l.x, uFlipY == 1 ? 1.0 - l.y : l.y) * uTexScale;
  vec4 v = texture(uTex0, t);
  vec3 rgb;
  float a = 1.0;
  if (uFmt == 2) {  // HAP Q: scaled YCoCg in DXT5
    float sc = v.b * (255.0 / 8.0) + 1.0;
    float co = (v.r - 0.5 * 256.0 / 255.0) / sc;
    float cg = (v.g - 0.5 * 256.0 / 255.0) / sc;
    rgb = vec3(v.a + co - cg, v.a + cg, v.a - co - cg);
  } else {
    rgb = v.rgb;
    if (uFmt == 1) a = v.a;
  }
  if (uAlphaTex == 1) a = texture(uTex1, t).r;
  // grading
  rgb = pow(max(rgb, vec3(0.0)), vec3(1.0 / uGamma));
  if (uHue != 0.0) {  // hue rotation in YIQ
    float Y = dot(rgb, vec3(0.299, 0.587, 0.114));
    float I = dot(rgb, vec3(0.596, -0.274, -0.322));
    float Q = dot(rgb, vec3(0.211, -0.523, 0.312));
    float ch = cos(uHue), sh = sin(uHue);
    float I2 = I * ch - Q * sh, Q2 = I * sh + Q * ch;
    rgb = vec3(Y + 0.956 * I2 + 0.621 * Q2, Y - 0.272 * I2 - 0.647 * Q2, Y - 1.106 * I2 + 1.703 * Q2);
  }
  float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  rgb = max(mix(vec3(luma), rgb, uSat), vec3(0.0));
  // soft edge, one pixel wide (no jaggies when rotated)
  vec2 e = min(l - vec2(uCrop.x, uCrop.w), vec2(1.0 - uCrop.y, 1.0 - uCrop.z) - l);
  vec2 px = max(fwidth(l), vec2(1e-6));
  a *= clamp(min(e.x / px.x, e.y / px.y), 0.0, 1.0);
  float w = uWeight * a * uLevel;
  o = vec4(min(rgb, vec3(1.0)) * w, uWeight * a);
}
)";

// 9-tap gaussian along uDir (two passes = a round blur)
const char* kBlurFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uDir;
void main() {
  vec4 s = texture(uSrc, vUv) * 0.2270270270;
  s += (texture(uSrc, vUv + uDir * 1.3846153846) + texture(uSrc, vUv - uDir * 1.3846153846)) * 0.3162162162;
  s += (texture(uSrc, vUv + uDir * 3.2307692308) + texture(uSrc, vUv - uDir * 3.2307692308)) * 0.0702702703;
  o = s;
}
)";

const char* kWeightFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform float uWeight;
void main() { o = texture(uSrc, vUv) * uWeight; }
)";

GLenum glFormat(int f) {
  switch (f) {
    case HAPTEX_DXT1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    case HAPTEX_ALPHA_RGTC1: return GL_COMPRESSED_RED_RGTC1;
    default: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
  }
}
size_t blockBytes(int f) { return (f == HAPTEX_DXT1 || f == HAPTEX_ALPHA_RGTC1) ? 8 : 16; }

// Reads and decompresses frames on its own thread; the GL upload happens on the main thread.
class Decoder {
 public:
  ~Decoder() {
    {
      std::lock_guard<std::mutex> lk(m_);
      quit_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
  }
  bool open(const std::string& path, std::string& err) {
    if (!reader_.open(path, err)) return false;
    th_ = std::thread([this] { loop(); });
    return true;
  }
  const HapIndex& index() const { return reader_.index(); }
  void request(int f) {
    {
      std::lock_guard<std::mutex> lk(m_);
      if (want_ == f) return;
      want_ = f;
    }
    cv_.notify_all();
  }
  // the newest decoded frame, if there is one we have not taken yet
  bool take(HapFrame& out, int& no) {
    std::lock_guard<std::mutex> lk(m_);
    if (!fresh_) return false;
    std::swap(out, ready_);
    no = readyNo_;
    fresh_ = false;
    return true;
  }
  std::string error() {
    std::lock_guard<std::mutex> lk(m_);
    return err_;
  }

 private:
  void loop() {
    HapFrame f;
    int done = -1;
    for (;;) {
      int w;
      {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return quit_ || (want_ >= 0 && want_ != done); });
        if (quit_) return;
        w = want_;
      }
      std::string e;
      bool ok = reader_.readFrame(w, f, e);
      done = w;
      std::lock_guard<std::mutex> lk(m_);
      if (ok) {
        std::swap(f, ready_);
        readyNo_ = w;
        fresh_ = true;
        err_.clear();
      } else {
        err_ = e;
      }
    }
  }
  HapReader reader_;
  std::thread th_;
  std::mutex m_;
  std::condition_variable cv_;
  int want_ = -1, readyNo_ = -1;
  bool quit_ = false, fresh_ = false;
  HapFrame ready_;
  std::string err_;
};

}  // namespace

struct MediaPlayer::Deck {
  std::string id;
  bool live = false;   // the video input
  bool bad = false;    // cannot be read
  std::unique_ptr<Decoder> dec;
  int frames = 1, w = 0, h = 0;
  double fps = 30;
  double pos = 0;
  int dir = 1;
  bool ended = false;
  GLuint tex[2] = {0, 0};
  int texFmt[2] = {-1, -1};
  int texW = 0, texH = 0, texCount = 0, shown = -1;
  HapFrame frame;
  ~Deck() {
    for (GLuint& t : tex)
      if (t) glDeleteTextures(1, &t);
  }
};

namespace {

const ClipSettings* settingsOf(const MediaEnv& env, const std::string& id) {
  if (id == kMediaLive) return &env.lib->liveSet;
  const MediaClip* c = findClip(*env.lib, id);
  return c ? &c->set : nullptr;
}

// Moves the play position along; returns the frame to show.
int advance(MediaPlayer::Deck& d, const ClipSettings& s, const MediaEnv& env, float dt) {
  if (d.frames <= 1) return 0;
  int n = d.frames;
  int in = std::clamp(int(std::floor(s.in * (n - 1))), 0, n - 1);
  int out = std::clamp(int(std::ceil(s.out * (n - 1))), in, n - 1);
  double len = double(out - in + 1);
  double rate = d.fps * s.speed;
  if (s.beatSync) rate = len / (std::max(1, s.beats) * 60.0 / std::max(20.f, env.bpm));
  int sign = s.reverse ? -1 : 1;
  if (s.beatSync && s.play == PLAY_LOOP) {
    // locked to the tempo: the loop starts on a bar line of `beats` beats
    double ph = std::fmod(env.beat, double(std::max(1, s.beats))) / std::max(1, s.beats);
    if (ph < 0) ph += 1;
    d.pos = sign > 0 ? in + ph * len : in + (1.0 - ph) * len - 1e-6;
  } else {
    d.pos += rate * dt * sign * d.dir;
  }
  double lo = in, hi = out + 1;  // pos in [lo, hi)
  switch (s.play) {
    case PLAY_LOOP:
      d.pos = lo + std::fmod(std::fmod(d.pos - lo, len) + len, len);
      break;
    case PLAY_ONCE_HOLD:
    case PLAY_ONCE_BLACK:
      if (d.pos >= hi || d.pos < lo) {
        d.ended = true;
        d.pos = std::clamp(d.pos, lo, hi - 1e-6);
      }
      break;
    default:  // BOUNCE
      for (int k = 0; k < 4 && (d.pos >= hi || d.pos < lo); ++k) {
        if (d.pos >= hi) d.pos = 2 * hi - d.pos - 2e-6;
        if (d.pos < lo) d.pos = 2 * lo - d.pos;
        d.dir = -d.dir;
      }
      d.pos = std::clamp(d.pos, lo, hi - 1e-6);
      break;
  }
  return std::clamp(int(d.pos), in, out);
}

// Brings the decoded frame into the deck's textures.
void upload(MediaPlayer::Deck& d) {
  int no = -1;
  if (!d.dec || !d.dec->take(d.frame, no)) return;
  const HapIndex& ix = d.dec->index();
  int pw = (ix.width + 3) / 4 * 4, ph = (ix.height + 3) / 4 * 4;
  size_t blocks = size_t(pw / 4) * size_t(ph / 4);
  for (int k = 0; k < d.frame.count && k < 2; ++k) {
    int f = d.frame.format[k];
    size_t need = blocks * blockBytes(f);
    if (d.frame.data[k].size() < need) return;  // damaged frame: keep the last good one
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int k = 0; k < d.frame.count && k < 2; ++k) {
    int f = d.frame.format[k];
    size_t need = blocks * blockBytes(f);
    if (!d.tex[k]) {
      glGenTextures(1, &d.tex[k]);
      glBindTexture(GL_TEXTURE_2D, d.tex[k]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, d.tex[k]);
    if (d.texFmt[k] != f || d.texW != pw || d.texH != ph)
      glCompressedTexImage2D(GL_TEXTURE_2D, 0, glFormat(f), pw, ph, 0, GLsizei(need), d.frame.data[k].data());
    else
      glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw, ph, glFormat(f), GLsizei(need), d.frame.data[k].data());
    d.texFmt[k] = f;
  }
  d.texW = pw;
  d.texH = ph;
  d.texCount = d.frame.count;
  d.shown = no;
}

}  // namespace

MediaPlayer::MediaPlayer() = default;
MediaPlayer::~MediaPlayer() = default;

bool MediaPlayer::init() {
  bool ok = prog_.build("mediaclip", kFullscreenVS, kClipFS);
  ok &= blur_.build("mediablur", kFullscreenVS, kBlurFS);
  ok &= copy_.build("mediaweight", kFullscreenVS, kWeightFS);
  return ok;
}

void MediaPlayer::resize(int W, int H) {
  W_ = W;
  H_ = H;
  if (out_.tex) {  // only once it is in use
    out_.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    tmpA_.destroy();
    tmpB_.destroy();
  }
}

const std::string& MediaPlayer::liveClip() const { return liveId_; }

void MediaPlayer::restart() {
  if (!cur_) return;
  cur_->dir = 1;
  cur_->ended = false;
  cur_->pos = -1;  // placed at the In point on the next update
}
void MediaPlayer::snapNext() { snap_ = true; }
void MediaPlayer::resumeNext() { resume_ = true; }

static float position(const MediaPlayer::Deck* d) {
  if (!d || d->frames <= 1) return 0.f;
  return float(std::clamp(d->pos / std::max(1, d->frames - 1), 0.0, 1.0));
}
float MediaPlayer::livePosition() const { return position(cur_.get()); }
float MediaPlayer::previewPosition() const { return position(pre_.get()); }

std::string MediaPlayer::status() const {
  if (cur_ && cur_->bad) return "The clip cannot be played (file missing or damaged).";
  if (cur_ && cur_->dec) {
    std::string e = cur_->dec->error();
    if (!e.empty()) return e;
  }
  return "";
}

void MediaPlayer::startDeck(const MediaEnv& env, std::unique_ptr<Deck>& d, const std::string& id, bool resume) {
  d = std::make_unique<Deck>();
  d->id = id;
  if (id.empty()) return;
  if (id == kMediaLive) {
    d->live = true;
    return;
  }
  const MediaClip* c = findClip(*env.lib, id);
  std::string err;
  if (!c || c->status != CLIP_READY || !env.imp) {
    d->bad = true;
    return;
  }
  d->dec = std::make_unique<Decoder>();
  if (!d->dec->open(env.imp->clipPath(id), err)) {
    logLine("Media: cannot play \"%s\": %s", c->name.c_str(), err.c_str());
    d->dec.reset();
    d->bad = true;
    return;
  }
  const HapIndex& ix = d->dec->index();
  d->frames = ix.frames();
  d->fps = ix.fps;
  d->w = ix.width;
  d->h = ix.height;
  const ClipSettings& s = c->set;
  int n = d->frames;
  int in = std::clamp(int(std::floor(s.in * (n - 1))), 0, std::max(0, n - 1));
  int out = std::clamp(int(std::ceil(s.out * (n - 1))), in, std::max(0, n - 1));
  d->pos = s.reverse ? out + 1 - 1e-6 : in;
  auto it = saved_.find(id);
  if (resume && it != saved_.end()) {  // back as it was: carry on from where it left, as if it had kept playing
    d->pos = it->second.pos;
    d->dir = it->second.dir;
    if (s.play == PLAY_LOOP && !s.beatSync) d->pos += (clock_ - it->second.when) * d->fps * s.speed * (s.reverse ? -1 : 1);
  }
}

void MediaPlayer::renderDeck(const MediaEnv& env, Deck& d, const Target& dst, float weight) {
  if (weight <= 0.001f || d.bad) return;
  const ClipSettings* sp = settingsOf(env, d.id);
  if (!sp) return;
  const ClipSettings& s = *sp;
  if (d.ended && s.play == PLAY_ONCE_BLACK) return;  // played once: black (ONCE HOLD keeps the last frame)
  GLuint t0 = 0, t1 = 0;
  int fmt = 0, alphaTex = 0, flipY = 1;
  float tsx = 1, tsy = 1, cw = 1, ch = 1;
  if (d.live) {
    if (!env.liveTex) return;
    t0 = env.liveTex;
    fmt = 0;
    flipY = 0;
    cw = float(std::max(1, env.liveW));
    ch = float(std::max(1, env.liveH));
  } else {
    if (!d.tex[0] || d.shown < 0) return;
    t0 = d.tex[0];
    fmt = d.texFmt[0] == HAPTEX_YCOCG ? 2 : (d.texFmt[0] == HAPTEX_DXT5 ? 1 : 0);
    if (d.texCount > 1 && d.tex[1]) {
      t1 = d.tex[1];
      alphaTex = 1;
    }
    tsx = float(d.w) / std::max(1, d.texW);
    tsy = float(d.h) / std::max(1, d.texH);
    cw = float(d.w);
    ch = float(d.h);
  }
  float aspect = float(dst.w) / std::max(1, dst.h);
  float ca = cw / ch;
  // fitted inside the output, then scaled
  float bw = 1, bh = 1;
  if (ca > aspect) bh = aspect / ca;
  else bw = ca / aspect;
  float sx = bw * s.scale * s.scaleX, sy = bh * s.scale * s.scaleY;

  bool blur = s.blur > 0.002f;
  if (blur) {
    if (tmpA_.w != dst.w || tmpA_.h != dst.h) {
      tmpA_.create(dst.w, dst.h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
      tmpB_.create(dst.w, dst.h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    }
    tmpA_.bind();
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
  } else {
    dst.bind();
  }
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  prog_.use();
  bindTex(0, t0);
  bindTex(1, t1 ? t1 : t0);
  prog_.seti("uTex0", 0);
  prog_.seti("uTex1", 1);
  prog_.seti("uFmt", fmt);
  prog_.seti("uAlphaTex", alphaTex);
  prog_.seti("uFlipY", flipY);
  prog_.set2f("uTexScale", tsx, tsy);
  prog_.set2f("uCenter", 0.5f + s.posX * 0.5f, 0.5f + s.posY * 0.5f);
  prog_.set2f("uSize", std::max(1e-4f, sx), std::max(1e-4f, sy));
  prog_.setf("uAspect", aspect);
  prog_.setf("uRot", s.rotation * kPi / 180.f);
  prog_.set2f("uFlip", s.flipH ? -1.f : 1.f, s.flipV ? -1.f : 1.f);
  glUniform4f(prog_.loc("uCrop"), s.cropL, s.cropR, s.cropT, s.cropB);
  prog_.setf("uGamma", std::max(0.05f, s.gamma));
  prog_.setf("uHue", s.hue * kPi / 180.f);
  prog_.setf("uSat", s.saturation);
  prog_.setf("uWeight", blur ? 1.f : weight);
  prog_.setf("uLevel", d.live ? env.liveLevel : 1.f);
  drawFullscreen();
  if (blur) {
    // two separable passes, wider ones for more blur (up to ~60 px at 1080p)
    glDisable(GL_BLEND);
    float r = s.blur * s.blur * 12.f * (dst.h / 1080.f) + 0.5f;
    int passes = 1 + int(s.blur * 2.f);
    blur_.use();
    blur_.seti("uSrc", 0);
    for (int p = 0; p < passes; ++p) {
      float rr = r * float(p + 1) / passes;
      tmpB_.bind();
      bindTex(0, tmpA_.tex);
      blur_.set2f("uDir", rr / dst.w, 0.f);
      drawFullscreen();
      tmpA_.bind();
      bindTex(0, tmpB_.tex);
      blur_.set2f("uDir", 0.f, rr / dst.h);
      drawFullscreen();
    }
    dst.bind();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    copy_.use();
    bindTex(0, tmpA_.tex);
    copy_.seti("uSrc", 0);
    copy_.setf("uWeight", weight);
    drawFullscreen();
  }
  glDisable(GL_BLEND);
}

void MediaPlayer::update(const MediaEnv& env, const std::string& clip, float dt, bool render) {
  clock_ += dt;
  // ---- the clip changed: transition ----
  if (clip != liveId_) {
    if (cur_ && !cur_->id.empty() && !cur_->live)
      saved_[cur_->id] = Saved{cur_->pos, cur_->dir, clock_};
    if (saved_.size() > 64) saved_.clear();
    const ClipSettings* in = settingsOf(env, clip);
    int type = env.lib->transition;
    float dur = env.lib->transTime;
    if (in && in->transition >= 0) {
      type = in->transition;
      dur = in->transTime;
    }
    if (snap_ || dur < 0.02f) type = MTRANS_CUT;
    old_ = std::move(cur_);
    if (type == MTRANS_CUT) old_.reset();
    startDeck(env, cur_, clip, resume_);
    transType_ = type;
    transDur_ = dur;
    trans_ = type == MTRANS_CUT ? 1.f : 0.f;
    liveId_ = clip;
    snap_ = resume_ = false;
    if (!clip.empty()) logLine("Media: playing \"%s\"", clip == kMediaLive ? "LIVE input" : (findClip(*env.lib, clip) ? findClip(*env.lib, clip)->name.c_str() : clip.c_str()));
  }
  if (trans_ < 1.f) trans_ = std::min(1.f, trans_ + dt / std::max(0.02f, transDur_));
  if (trans_ >= 1.f) old_.reset();

  // ---- advance and decode ----
  for (Deck* d : {cur_.get(), old_.get()}) {
    if (!d || d->live || d->bad || !d->dec) continue;
    const ClipSettings* s = settingsOf(env, d->id);
    if (!s) continue;
    if (d->pos < 0) {  // restart
      int n = d->frames;
      d->pos = s->reverse ? std::ceil(s->out * (n - 1)) + 1 - 1e-6 : std::floor(s->in * (n - 1));
      d->ended = false;
    }
    int f = advance(*d, *s, env, dt);
    if (render) {
      d->dec->request(f);
      upload(*d);
    }
  }
  if (!render) return;
  if (!out_.tex || out_.w != W_ || out_.h != H_) out_.create(std::max(16, W_), std::max(16, H_), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  out_.bind();
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  float t = trans_;
  float wNew = 1, wOld = 0;
  if (old_) {
    if (transType_ == MTRANS_BLACK) {
      wOld = std::max(0.f, 1.f - 2.f * t);
      wNew = std::max(0.f, 2.f * t - 1.f);
    } else {
      wOld = 1.f - t;
      wNew = t;
    }
  }
  if (old_) renderDeck(env, *old_, out_, wOld);
  if (cur_) renderDeck(env, *cur_, out_, wNew);
}

void MediaPlayer::updatePreview(const MediaEnv& env, const std::string& clip, float dt, int w, int h) {
  if (!pre_ || pre_->id != clip) startDeck(env, pre_, clip, false);
  if (clip.empty()) return;
  if (prev_.w != w || prev_.h != h) prev_.create(std::max(16, w), std::max(16, h), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  if (!pre_->live && !pre_->bad && pre_->dec) {
    const ClipSettings* s = settingsOf(env, clip);
    if (s) {
      if (pre_->pos < 0) pre_->pos = 0;
      if (pre_->ended) {  // the preview keeps looping so you can adjust things
        pre_->ended = false;
        pre_->pos = s->reverse ? std::ceil(s->out * (pre_->frames - 1)) : std::floor(s->in * (pre_->frames - 1));
      }
      int f = advance(*pre_, *s, env, dt);
      pre_->dec->request(f);
      upload(*pre_);
    }
  }
  prev_.bind();
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  renderDeck(env, *pre_, prev_, 1.f);
}
