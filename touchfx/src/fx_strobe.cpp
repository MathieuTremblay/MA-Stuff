// Strobe / flash: full frame, spots, bars or sparkling particles flashing in time with the BPM.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

const char* kShapeFS = R"(
in vec2 vUv; out vec4 o;
uniform int uShape, uCount;
uniform vec2 uPts[64];
uniform float uAspect, uSize, uProgress, uFlash;
uniform vec3 uColor;
void main() {
  float m = 0.0;
  float p = uProgress;  // 0..1 through the flash (animated flash types)
  if (uShape == 0) {
    m = 1.0;
  } else if (uShape == 7) {  // blinder: full frame, fast attack, smooth decay
    m = exp(-p * 4.0);
  } else if (uShape == 8) {  // blocks: a new random set of tiles each flash
    vec2 grid = vec2(8.0, max(2.0, floor(8.0 / uAspect + 0.5)));
    vec2 cell = floor(vUv * grid);
    vec2 f = fract(vUv * grid);
    float on = step(hash12(cell + uFlash * 17.13), mix(0.12, 0.7, uSize));
    float edge = smoothstep(0.0, 0.04, min(min(f.x, f.y), min(1.0 - f.x, 1.0 - f.y)));
    m = on * edge * (1.0 - p);
  } else {
    for (int i = 0; i < uCount; ++i) {
      vec2 pt = uPts[i];
      float d;
      vec2 q = (vUv - pt) * vec2(uAspect, 1.0);
      if (uShape == 5) {  // shockwave: expanding ring
        float r = p * uSize * 2.2;
        float w = 0.015 + 0.05 * p;
        m = max(m, (1.0 - smoothstep(0.0, w, abs(length(q) - r))) * (1.0 - p));
        continue;
      }
      if (uShape == 6) {  // radial wipe: a sweep growing around the finger
        float a = fract(atan(q.x, q.y) / 6.2831853 + 0.5);
        float inside = 1.0 - smoothstep(uSize * 1.6, uSize * 1.6 + 0.02, length(q));
        m = max(m, step(a, p) * inside * (1.0 - p * 0.6));
        continue;
      }
      if (uShape == 1)      d = length(q);
      else if (uShape == 2) d = abs(vUv.y - pt.y);
      else                  d = abs(vUv.x - pt.x) * uAspect;
      float r = (uShape == 1) ? uSize : uSize * 0.35;
      m = max(m, 1.0 - smoothstep(r * 0.75, r, d));
    }
  }
  o = vec4(uColor * m, 1.0);
}
)";

class Strobe : public Effect {
 public:
  bool init() override { return prog.build("strobe", kFullscreenVS, kShapeFS); }

  void resize(int W, int H) override {
    (void)W;
    (void)H;
  }

  void clear() override {}

  void frame(const FxCtx& c, const Target& out) override {
    const StrobeSettings& s = c.look->strobe;
    out.clear(0, 0, 0, 1);
    const std::vector<Emitter>& em = c.shapeEmitters();
    if (em.empty() || c.gain <= 0.001f) return;

    // Flash phase: synced to the beat, or free-running in Hz.
    uint32_t flash;
    bool on;
    float progress;  // 0..1 through the flash, for the animated types
    bool animated = s.shape >= STROBE_SHOCKWAVE;
    if (s.audioHits && c.audioOn) {
      // one flash per detected hit in the music
      if (c.onset) {
        ++hitCount;
        hitTime = 0;
      }
      hitTime += c.dt;
      flash = hitCount;
      float len = animated ? mixf(0.12f, 0.9f, s.duty) : mixf(0.03f, 0.25f, s.duty);
      on = hitTime < len;
      progress = std::min(1.f, hitTime / len);
    } else {
      double cycles = s.freeHz ? double(c.time) * s.hz : c.beat * kDivisions[s.rate];
      flash = uint32_t(std::floor(cycles));
      float ph = float(cycles - std::floor(cycles));
      float len = animated ? std::max(0.1f, s.duty * 1.5f) : std::max(0.03f, s.duty);
      if (s.shape == STROBE_BLINDER) len = 1.f;  // the blinder decays over the whole cycle
      on = ph < len;
      progress = std::min(1.f, ph / len);
    }
    if (!on) return;

    Vec3 col;
    if (s.colorCycle) {
      static const float ts[3] = {0.f, 0.5f, 1.f};
      col = c.color(ts[flash % 3]);
    } else {
      col = c.color(c.colorT(&em[0], 0.5f + 0.5f * std::sin(float(flash) * 0.7f), hash01(flash * 37u)));
    }
    col = col * (c.gain * c.look->brightness * 1.6f);

    if (s.shape == STROBE_SPARKLE) {
      // Random dots around each finger, re-scattered on every flash.
      float px = c.H / 1080.f;
      float radius = mixf(0.05f, 0.6f, s.size);
      verts.clear();
      for (size_t k = 0; k < em.size(); ++k) {
        const Emitter& e = em[k];
        for (int i = 0; i < 70; ++i) {
          uint32_t h = flash * 2654435761u + uint32_t(e.touch) * 97u + uint32_t(i) * 7919u;
          float ang = hash01(h) * kTau;
          float r = std::sqrt(hash01(h + 1)) * radius;
          Vec2 q = e.m * Vec2(std::cos(ang) * r, std::sin(ang) * r);
          float x = e.pos.x * c.W + q.x * c.H, y = e.pos.y * c.H + q.y * c.H;
          float sz = mixf(24.f, 70.f, hash01(h + 2)) * px;
          verts.push_back({x, y, col.x, col.y, col.z, 1.4f, sz});
        }
      }
      fxDrawPoints(out, verts, POINT_STAR);
      return;
    }

    float pts[kMaxSplats * 2];
    int n = 0;
    for (const Emitter& e : em) {
      if (n >= kMaxSplats) break;
      pts[n * 2] = e.pos.x;
      pts[n * 2 + 1] = e.pos.y;
      ++n;
    }
    out.bind();
    glDisable(GL_BLEND);
    prog.use();
    prog.seti("uShape", s.shape);
    prog.seti("uCount", n);
    prog.set2fv("uPts", pts, n);
    prog.setf("uAspect", float(c.W) / c.H);
    prog.setf("uSize", mixf(0.04f, 0.7f, s.size));
    prog.set3f("uColor", col.x, col.y, col.z);
    prog.setf("uProgress", progress);
    prog.setf("uFlash", float(flash % 1000));
    drawFullscreen();
  }

 private:
  Program prog;
  std::vector<PointVertex> verts;
  uint32_t hitCount = 0;
  float hitTime = 10.f;
};

}  // namespace

std::unique_ptr<Effect> makeStrobe() { return std::make_unique<Strobe>(); }
