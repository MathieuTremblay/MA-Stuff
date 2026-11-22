// Strobe / flash: full frame, spots, bars or sparkling particles flashing in time with the BPM.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

const char* kShapeFS = R"(
in vec2 vUv; out vec4 o;
uniform int uShape, uCount;
uniform vec2 uPts[64];
uniform float uAspect, uSize;
uniform vec3 uColor;
void main() {
  float m = 0.0;
  if (uShape == 0) {
    m = 1.0;
  } else {
    for (int i = 0; i < uCount; ++i) {
      vec2 p = uPts[i];
      float d;
      if (uShape == 1)      d = length((vUv - p) * vec2(uAspect, 1.0));
      else if (uShape == 2) d = abs(vUv.y - p.y);
      else                  d = abs(vUv.x - p.x) * uAspect;
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
    double cycles = s.freeHz ? double(c.time) * s.hz : c.beat * kDivisions[s.rate];
    uint32_t flash = uint32_t(std::floor(cycles));
    float ph = float(cycles - std::floor(cycles));
    bool on = ph < std::max(0.03f, s.duty);
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
    drawFullscreen();
  }

 private:
  Program prog;
  std::vector<PointVertex> verts;
};

}  // namespace

std::unique_ptr<Effect> makeStrobe() { return std::make_unique<Strobe>(); }
