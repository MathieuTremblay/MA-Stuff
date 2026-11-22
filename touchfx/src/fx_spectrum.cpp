// Spectrum: audio-reactive equaliser around the fingers - a circle of bars, straight bars or a waveform line.
// Without audio it plays an animated demo spectrum on the beat (turn AUDIO on for the real thing).
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

class Spectrum : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override { trail.clear(); }

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

    const std::vector<Emitter>& em = c.shapeEmitters();
    segs.clear();
    if (!em.empty() && c.gain > 0.001f) {
      int nb = s.bars;
      auto sample = [&](float u) {  // u 0..1 across the spectrum (interpolated)
        float f = std::clamp(u, 0.f, 1.f) * 31.f;
        int i = int(f);
        float k = f - i;
        return smooth[i] * (1 - k) + smooth[std::min(31, i + 1)] * k;
      };
      float bw = mixf(1.f, 12.f, s.width) * px;
      float size = mixf(0.05f, 0.45f, s.size) * c.H;
      int count = 0;
      for (const Emitter& e : em) {
        if (++count > 8) break;
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
          case SPEC_BARS: {
            float total = std::min(float(c.W) * 0.9f, size * 4.f);
            for (int i = 0; i < nb; ++i) {
              float u = (i + 0.5f) / nb;
              float su = s.symmetric ? std::fabs(u - 0.5f) * 2.f : u;
              float v = sample(su);
              float x = o.x + (u - 0.5f) * total;
              Vec2 dir = e.m * Vec2(0, 1);
              Vec3 col = c.color(c.colorT(&e, v, su)) * (0.3f + v * 0.8f) * c.gain;
              Vec2 base(x, o.y);
              segs.push_back({base - dir * (size * v * 0.5f), base + dir * (size * v * 0.5f), col, bw * 0.5f + 4.f * px, bw});
            }
            break;
          }
          default: {  // waveform-style line across the screen
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
    fxDrawGlowSegs(trail, segs, 0.3f + s.glow, true);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  float smooth[32] = {};
  std::vector<GlowSeg> segs;
  Target trail;
};

}  // namespace

std::unique_ptr<Effect> makeSpectrum() { return std::make_unique<Spectrum>(); }
