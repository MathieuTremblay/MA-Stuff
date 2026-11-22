// Lightning: electric arcs between the fingers (or from a finger out towards the edges), flickering with
// the tempo, with random side branches.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

class Lightning : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override {
    trail.clear();
    bolts.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const LightningSettings& s = c.look->lightning;
    float dt = std::min(c.dt, 0.05f);
    float px = c.H / 1080.f;
    fxFade(trail, std::exp(-dt * mixf(50.f, 4.f, s.trail)) * c.fadeMul);

    const std::vector<Emitter>& em = c.shapeEmitters();
    // new bolt shapes several times per beat (rate), and on every audio hit
    double cycles = c.beat * mixf(1.f, 8.f, s.rate);
    bool regen = std::floor(cycles) != std::floor(lastCycles) || (c.audioOn && c.onset) || bolts.empty();
    lastCycles = cycles;
    if (em.empty() || c.gain < 0.001f) {
      bolts.clear();
    } else if (regen) {
      bolts.clear();
      seed = hash32(seed + 1);
      rng.s = seed;
      float diag = std::sqrt(float(c.W * c.W + c.H * c.H));
      std::vector<Vec2> pts;
      for (const Emitter& e : em) pts.push_back(Vec2(e.pos.x * c.W, e.pos.y * c.H));
      if (pts.size() >= 2) {
        // chain the fingers together, plus one random cross link
        for (size_t i = 0; i + 1 < pts.size(); ++i) bolt(pts[i], pts[i + 1], s, 0);
        if (pts.size() > 2) bolt(pts.back(), pts.front(), s, 0);
      }
      int forks = 1 + int(s.reach * 3.f);
      for (const Emitter& e : em) {
        if (e.mirror != 0 && pts.size() >= 2) continue;
        Vec2 o(e.pos.x * c.W, e.pos.y * c.H);
        for (int k = 0; k < forks; ++k) {
          float a = rng.range(0, kTau);
          Vec2 end = o + e.m * Vec2(std::cos(a), std::sin(a)) * (diag * mixf(0.08f, 0.6f, s.reach) * rng.range(0.5f, 1.f));
          bolt(o, end, s, 0);
        }
      }
      flash = 1.f;
    }
    flash *= std::exp(-dt * 10.f);

    segs.clear();
    float w = mixf(1.f, 6.f, s.width) * px;
    float glowW = mixf(4.f, 30.f, s.glow) * px;
    float flick = 0.55f + 0.45f * flash;
    float intensity = c.gain * flick * std::min(c.drive, 1.5f);
    for (const Bolt& b : bolts) {
      Vec3 col = c.color(b.t) * (intensity * (b.depth ? 0.55f : 1.f));
      float bw = b.depth ? w * 0.6f : w;
      segs.push_back({b.a, b.b, col, bw * 0.5f + glowW * (b.depth ? 0.5f : 1.f), bw});
    }
    fxDrawGlowSegs(trail, segs, 0.4f + s.glow, true);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  struct Bolt {
    Vec2 a, b;
    float t;
    int depth;
  };

  // Midpoint displacement: split the line, push the middle sideways, recurse. Branches split off sometimes.
  void bolt(Vec2 a, Vec2 b, const LightningSettings& s, int depth) {
    std::vector<Vec2> line = {a, b};
    float len = length(b - a);
    float disp = len * mixf(0.05f, 0.3f, s.chaos);
    for (int it = 0; it < 6; ++it) {
      std::vector<Vec2> next;
      for (size_t i = 0; i + 1 < line.size(); ++i) {
        Vec2 p = line[i], q = line[i + 1];
        Vec2 m = (p + q) * 0.5f;
        Vec2 d = q - p;
        float dl = std::max(0.001f, length(d));
        Vec2 n(-d.y / dl, d.x / dl);
        m += n * rng.range(-disp, disp);
        next.push_back(p);
        next.push_back(m);
        if (depth < 2 && rng.next() < s.branches * 0.08f) {
          float ang = std::atan2(d.y, d.x) + rng.range(-0.8f, 0.8f);
          bolt(m, m + Vec2(std::cos(ang), std::sin(ang)) * dl * rng.range(1.5f, 4.f), s, depth + 1);
        }
      }
      next.push_back(line.back());
      line.swap(next);
      disp *= 0.55f;
    }
    float t = rng.next();
    for (size_t i = 0; i + 1 < line.size() && bolts.size() < 6000; ++i) bolts.push_back({line[i], line[i + 1], t, depth});
  }

  std::vector<Bolt> bolts;
  std::vector<GlowSeg> segs;
  Target trail;
  Rng rng;
  uint32_t seed = 7;
  double lastCycles = 0;
  float flash = 0;
};

}  // namespace

std::unique_ptr<Effect> makeLightning() { return std::make_unique<Lightning>(); }
