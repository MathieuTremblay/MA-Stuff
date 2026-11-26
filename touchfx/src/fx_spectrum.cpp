// Spectrum: audio-reactive visuals around the fingers.
//   CIRCLE       - a circle of bars around each finger
//   BARS         - an equaliser across the screen; the Mirror setting lays it out (L-R: lows in the centre,
//                  U-D: the bars grow up and down from the middle line)
//   WAVE         - a line across the screen moved by the spectrum
//   WATERFALL    - the spectrum's history flowing away from each finger
//   OSCILLOSCOPE - the sound wave itself through each finger
//   RINGS        - rings around each finger, lows inside, highs outside, each one pumping with its band
//   BURST        - every hit throws out a ring of spikes shaped by the spectrum
// Without audio it plays an animated demo on the beat (turn AUDIO on for the real thing).
#include <algorithm>
#include <cmath>
#include <deque>

#include "effects.hpp"

namespace {

class Spectrum : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override {
    trail.clear();
    history.clear();
    bursts.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const SpectrumSettings& s = c.look->spectrum;
    float dt = std::min(c.dt, 0.05f);
    float px = c.H / 1080.f;
    fxFade(trail, std::exp(-dt * 30.f) * c.fadeMul);

    // ---- band values (real audio, or a demo that dances on the beat) ----
    float bands[32];
    float beatEnv = std::pow(1.f - fractf(float(c.beat)), 3.f);
    for (int b = 0; b < 32; ++b) {
      if (c.spectrum) {
        bands[b] = c.spectrum[b];
      } else {
        float base = 0.25f + 0.25f * std::sin(c.time * 2.f + b * 0.45f) * std::sin(c.time * 1.3f + b * 0.2f);
        bands[b] = std::clamp(base + beatEnv * std::exp(-b * 0.12f) * 0.8f, 0.f, 1.f);
      }
      bands[b] = std::clamp(bands[b] * mixf(0.5f, 2.f, s.gain), 0.f, 1.f);
    }
    for (int b = 0; b < 32; ++b) smooth[b] += (bands[b] - smooth[b]) * (bands[b] > smooth[b] ? 0.7f : 0.2f);

    // history for WATERFALL (30 rows a second)
    histClock += dt;
    if (s.mode == SPEC_WATERFALL && histClock > 1.f / 30.f) {
      histClock = 0;
      history.push_front(std::vector<float>(smooth, smooth + 32));
      while (history.size() > kRows) history.pop_back();
    }
    // a hit (or each beat in the demo) for BURST
    bool hit = c.audioOn ? c.onset : std::floor(c.beat) != std::floor(lastBeat);
    lastBeat = c.beat;

    const std::vector<Emitter>& em = c.shapeEmitters();
    segs.clear();
    auto sampleOf = [&](const float* src, float u) {  // u 0..1 across the spectrum (interpolated)
      float f = std::clamp(u, 0.f, 1.f) * 31.f;
      int i = int(f);
      float k = f - i;
      return src[i] * (1 - k) + src[std::min(31, i + 1)] * k;
    };
    auto sample = [&](float u) { return sampleOf(smooth, u); };
    int nb = s.bars;
    float bw = mixf(1.f, 12.f, s.width) * px;
    float size = mixf(0.05f, 0.45f, s.size) * c.H;
    if (!em.empty() && c.gain > 0.001f) {
      int count = 0;
      if (s.mode == SPEC_BARS) drawBars(c, s, size, bw, px, sample);
      for (const Emitter& e : em) {
        if (++count > 8 || s.mode == SPEC_BARS) break;
        Vec2 o(e.pos.x * c.W, e.pos.y * c.H);
        switch (s.mode) {
          case SPEC_CIRCLE: {
            float r0 = size * 0.45f;
            for (int i = 0; i < nb; ++i) {
              float u = float(i) / nb;
              float su = s.symmetric ? (u < 0.5f ? u * 2.f : 2.f - u * 2.f) : u;
              float v = sample(su);
              float a = kPi * 0.5f + kTau * u;
              Vec2 d = e.m * Vec2(std::cos(a), std::sin(a));
              Vec3 col = c.color(c.colorT(&e, v, su)) * (0.3f + v * 0.8f) * c.gain;
              segs.push_back({o + d * r0, o + d * (r0 + size * v), col, bw * 0.5f + 4.f * px, bw});
            }
            break;
          }
          case SPEC_WATERFALL: {
            if (count > 2) break;  // two waterfalls at most: each is a few thousand cells
            // rows flow away from the finger (down, or up for a mirrored copy)
            int cols = std::min(nb, 40);
            float total = std::min(float(c.W) * 0.9f, size * 4.f);
            float rowH = size * 1.6f / kRows;
            Vec2 down = e.m * Vec2(0, -1);
            float cellW = total / cols;
            for (size_t r = 0; r < history.size(); ++r) {
              const float* h = history[r].data();
              float age = float(r) / kRows;
              float fade = (1.f - age) * (1.f - age);
              Vec2 rowO = o + down * (rowH * float(r));
              for (int i = 0; i < cols; ++i) {
                float u = (i + 0.5f) / cols;
                float su = s.symmetric ? std::fabs(u - 0.5f) * 2.f : u;
                float v = sampleOf(h, su);
                if (v < 0.06f) continue;
                float x = (u - 0.5f) * total;
                Vec2 p = rowO + Vec2(x, 0);
                Vec3 col = c.color(c.colorT(&e, v, su)) * (v * 1.1f * fade) * c.gain;
                float half = cellW * 0.42f;
                segs.push_back({p - Vec2(half, 0), p + Vec2(half, 0), col, rowH * 0.5f + 1.f * px, rowH * 0.4f});
              }
            }
            break;
          }
          case SPEC_SCOPE: {
            // the sound wave through the finger (a demo wave without audio)
            float total = std::min(float(c.W) * 0.95f, size * 4.f);
            float amp = size * 0.6f;
            Vec2 prev;
            const int steps = 128;
            for (int i = 0; i <= steps; ++i) {
              float u = float(i) / steps;
              float w;
              if (c.wave) {
                w = c.wave[std::min(255, i * 2)] * std::min(1.f, 0.4f + c.level);
              } else {
                float ph = u * kTau * 2.f + c.time * 3.f;
                w = (std::sin(ph) * 0.6f + std::sin(ph * 3.1f) * 0.25f * beatEnv + std::sin(ph * 7.3f) * 0.1f) *
                    (0.4f + 0.6f * beatEnv);
              }
              float edge = std::sin(u * kPi);  // the line calms down towards its ends
              Vec2 p = o + e.m * Vec2((u - 0.5f) * total, w * amp * edge);
              float v = std::fabs(w);
              Vec3 col = c.color(c.colorT(&e, u, u)) * (0.6f + v) * c.gain;
              if (i) segs.push_back({prev, p, col, bw * 0.5f + 6.f * px, bw});
              prev = p;
            }
            break;
          }
          case SPEC_RINGS: {
            const int rings = 8, sides = 72;
            for (int k = 0; k < rings; ++k) {
              float su = (k + 0.5f) / rings;
              float v = sample(su);
              float r = size * (0.12f + 0.11f * k) + v * size * 0.09f;
              float wobble = v * size * 0.025f;
              Vec3 col = c.color(c.colorT(&e, v, su)) * (0.2f + v * 1.1f) * c.gain;
              float w = bw * (0.4f + v);
              Vec2 prev;
              for (int i = 0; i <= sides; ++i) {
                float a = kTau * i / sides;
                float rr = r + wobble * std::sin(a * (3 + k) + c.time * 2.f * c.speed + k);
                Vec2 p = o + e.m * Vec2(std::cos(a) * rr, std::sin(a) * rr);
                if (i) segs.push_back({prev, p, col, w * 0.5f + 4.f * px, w});
                prev = p;
              }
            }
            break;
          }
          case SPEC_BURST:
            if (hit && bursts.size() < 48) {
              Burst b;
              b.pos = o;
              b.m = e.m;
              b.touch = e.touch;
              std::copy(smooth, smooth + 32, b.bands);
              bursts.push_back(b);
            }
            break;
          default: {  // WAVE: a line across the screen
            Vec2 prev;
            int steps = std::max(16, nb * 2);
            for (int i = 0; i <= steps; ++i) {
              float u = float(i) / steps;
              float su = s.symmetric ? std::fabs(u - 0.5f) * 2.f : u;
              float v = sample(su);
              float y = o.y + std::sin(u * kTau * 3.f + c.time * 4.f * c.speed) * size * v * (e.m.d < 0 ? -1.f : 1.f);
              Vec2 p(u * c.W, y);
              Vec3 col = c.color(c.colorT(&e, v, u)) * (0.5f + v) * c.gain;
              if (i) segs.push_back({prev, p, col, bw * 0.5f + 6.f * px, bw});
              prev = p;
            }
            break;
          }
        }
      }
    }
    // bursts keep flying after release
    if (s.mode != SPEC_BURST) bursts.clear();
    for (Burst& b : bursts) {
      b.age += dt * std::clamp(c.speed, 0.4f, 2.5f);
      float life = 1.f - b.age / 1.1f;
      if (life <= 0) continue;
      float r0 = size * (0.1f + b.age * 1.4f);
      int n = std::max(8, nb);
      for (int i = 0; i < n; ++i) {
        float u = float(i) / n;
        float su = s.symmetric ? (u < 0.5f ? u * 2.f : 2.f - u * 2.f) : u;
        float v = sampleOf(b.bands, su);
        float a = kPi * 0.5f + kTau * u;
        Vec2 d = b.m * Vec2(std::cos(a), std::sin(a));
        Vec3 col = c.color(c.colorT(nullptr, v, hash01(uint32_t(b.touch) * 7u + uint32_t(i)))) * (0.3f + v) * life * life;
        segs.push_back({b.pos + d * r0, b.pos + d * (r0 + size * (0.08f + 0.5f * v)), col, bw * 0.5f + 4.f * px, bw});
      }
    }
    bursts.erase(std::remove_if(bursts.begin(), bursts.end(), [](const Burst& b) { return b.age > 1.1f; }), bursts.end());

    fxDrawGlowSegs(trail, segs, 0.3f + s.glow, true);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  // BARS: one equaliser across the screen, laid out by the Mirror setting
  template <class F>
  void drawBars(const FxCtx& c, const SpectrumSettings& s, float size, float bw, float px, F& sample) {
    int m = c.look->mirror;
    bool lr = m == MIRROR_LR || m == MIRROR_QUAD || m >= MIRROR_RADIAL6;
    bool ud = m == MIRROR_UD || m == MIRROR_QUAD || m >= MIRROR_RADIAL6;
    int nb = s.bars;
    float total = float(c.W) * 0.92f;
    float step = total / nb;
    float w = std::min(bw, step * 0.8f);
    float baseY = ud ? c.H * 0.5f : c.H * 0.06f;
    float maxH = ud ? std::min(size * 1.1f, c.H * 0.46f) : std::min(size * 2.f, c.H * 0.88f);
    for (int i = 0; i < nb; ++i) {
      float u = (i + 0.5f) / nb;
      float su = (lr || s.symmetric) ? std::fabs(u - 0.5f) * 2.f : u;
      float v = sample(su);
      float x = c.W * 0.5f + (u - 0.5f) * total;
      Vec3 col = c.color(c.colorT(nullptr, v, su)) * (0.3f + v * 0.8f) * c.gain;
      float h = std::max(2.f * px, maxH * v);
      if (ud) segs.push_back({Vec2(x, baseY - h), Vec2(x, baseY + h), col, w * 0.5f + 4.f * px, w});
      else segs.push_back({Vec2(x, baseY), Vec2(x, baseY + h), col, w * 0.5f + 4.f * px, w});
    }
  }

  static constexpr size_t kRows = 48;
  struct Burst {
    Vec2 pos;
    Mat2 m;
    int touch = 0;
    float age = 0;
    float bands[32];
  };
  float smooth[32] = {};
  std::deque<std::vector<float>> history;
  std::vector<Burst> bursts;
  float histClock = 0;
  double lastBeat = 0;
  std::vector<GlowSeg> segs;
  Target trail;
};

}  // namespace

std::unique_ptr<Effect> makeSpectrum() { return std::make_unique<Spectrum>(); }
