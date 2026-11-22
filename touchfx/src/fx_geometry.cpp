// Geometric shapes: neon outlines (triangle, square, pentagon, hexagon, circle, star) that pop at the fingers
// on the beat, grow, spin and fade out.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

struct Shape {
  Vec2 center;  // pixels
  Mat2 m;       // mirror transform (so mirrored shapes spin the other way)
  int type;
  float rot, spin, age, life, maxR;
  float t;  // colour position
};

class Geometry : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override {
    shapes.clear();
    trail.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const GeometrySettings& s = c.look->geometry;
    float dt = std::min(c.dt, 0.05f);
    float sdt = dt * c.speed;
    float px = c.H / 1080.f;

    // ---- spawn on the beat division (and on audio hits) ----
    double cycles = c.beat * kDivisions[s.rate];
    bool tick = std::floor(cycles) != std::floor(lastCycles) || (c.audioOn && c.onset);
    lastCycles = cycles;
    if (tick && !c.live.empty() && shapes.size() < 400) {
      int type = s.shape == SHAPE_MIX ? int(rng.next() * SHAPE_MIX) : s.shape;
      float rot0 = rng.range(0, kTau);
      float t = rng.next();
      for (const Emitter& e : c.live) {
        Shape sh;
        sh.center = Vec2(e.pos.x * c.W, e.pos.y * c.H);
        sh.m = e.m;
        sh.type = type;
        sh.rot = rot0;
        sh.spin = (s.spin - 0.5f) * 2.f * 3.f;
        sh.age = 0;
        sh.life = mixf(0.4f, 4.f, s.life * s.life);
        sh.maxR = mixf(0.05f, 0.6f, s.size) * c.H * std::max(0.4f, std::min(c.drive, 1.6f));
        sh.t = t;
        shapes.push_back(sh);
      }
    }
    for (Shape& sh : shapes) {
      sh.age += sdt;
      sh.rot += sh.spin * sdt;
    }
    shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [](const Shape& sh) { return sh.age >= sh.life; }),
                 shapes.end());

    // ---- draw outlines ----
    fxFade(trail, std::exp(-dt * 30.f) * c.fadeMul);
    segs.clear();
    float w = mixf(1.5f, 14.f, s.width) * px;
    float beatPulse = std::pow(1.f - fractf(float(c.beat)), 4.f);
    for (const Shape& sh : shapes) {
      float u = sh.age / sh.life;
      float grow = 1.f - std::pow(1.f - std::min(1.f, u * 2.5f), 3.f);  // ease out
      float r = sh.maxR * grow * (1.f + 0.08f * beatPulse);
      float fade = (1.f - u) * (1.f - u) * c.gain;
      float t = c.look->colorMode == COLOR_GRADIENT ? u : c.colorT(nullptr, u, sh.t);
      Vec3 col = c.color(t) * fade;
      outline(sh, r, col, w);
    }
    fxDrawGlowSegs(trail, segs, 0.3f + s.glow, true);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  void outline(const Shape& sh, float r, Vec3 col, float w) {
    int sides;
    bool star = false;
    switch (sh.type) {
      case SHAPE_TRI: sides = 3; break;
      case SHAPE_SQUARE: sides = 4; break;
      case SHAPE_PENT: sides = 5; break;
      case SHAPE_HEX: sides = 6; break;
      case SHAPE_STAR: sides = 10; star = true; break;
      default: sides = 64; break;
    }
    Vec2 prev;
    for (int k = 0; k <= sides; ++k) {
      float a = sh.rot + kTau * k / sides;
      float rr = star && (k & 1) ? r * 0.45f : r;
      Vec2 p = sh.center + sh.m * Vec2(std::cos(a) * rr, std::sin(a) * rr);
      if (k) segs.push_back({prev, p, col, w * 0.5f + w * 1.5f, w});
      prev = p;
    }
  }

  std::vector<Shape> shapes;
  std::vector<GlowSeg> segs;
  Target trail;
  Rng rng;
  double lastCycles = 0;
};

}  // namespace

std::unique_ptr<Effect> makeGeometry() { return std::make_unique<Geometry>(); }
