// Sparkles: star-shaped glints that pop up around the fingers and twinkle.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

constexpr int kMaxSparkles = 6000;

struct Sparkle {
  Vec2 pos, vel;  // aspect space
  float life, maxLife, size, phase, freq, rnd, speedT, posT;
};

class Sparkles : public Effect {
 public:
  bool init() override { return true; }

  void resize(int W, int H) override {
    trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    aspect = float(W) / H;
  }

  void clear() override {
    sp.clear();
    trail.clear();
    alphaScale = 1;
  }

  void frame(const FxCtx& c, const Target& out) override {
    const SparkleSettings& s = c.look->sparkles;
    float dt = std::min(c.dt, 0.05f);
    float sdt = dt * c.speed;
    float px = c.H / 1080.f;

    if (c.touching) alphaScale = 1;
    else alphaScale *= c.fadeMul;

    // ---- spawn around each finger ----
    if (!c.live.empty()) {
      float rate = mixf(15.f, 900.f, s.amount * s.amount) / std::sqrt(float(c.live.size()));
      float radius = mixf(0.01f, 0.45f, s.spread);
      for (size_t i = 0; i < c.live.size(); ++i) {
        const Emitter& e = c.live[i];
        acc[i % kMaxSplats] += rate * sdt;
        int n = int(acc[i % kMaxSplats]);
        acc[i % kMaxSplats] -= n;
        for (int k = 0; k < n && int(sp.size()) < kMaxSparkles; ++k) {
          Sparkle p;
          float ang = rng.range(0, kTau);
          float r = radius * std::pow(rng.next(), 0.7f);
          p.pos = Vec2(e.pos.x * aspect, e.pos.y) + Vec2(std::cos(ang), std::sin(ang)) * r;
          float dang = rng.range(0, kTau);
          p.vel = Vec2(std::cos(dang), std::sin(dang)) * (s.drift * 0.08f * rng.range(0.2f, 1.f)) + e.vel * (0.15f * s.drift);
          p.maxLife = p.life = mixf(0.3f, 4.f, s.life * s.life) * rng.range(0.5f, 1.f);
          p.size = mixf(30.f, 220.f, s.size * s.size) * px * rng.range(0.4f, 1.f);
          p.phase = rng.range(0, kTau);
          p.freq = mixf(1.f, 14.f, s.twinkle) * rng.range(0.6f, 1.4f);
          p.rnd = rng.next();
          p.speedT = saturate(e.speed / 1.5f);
          p.posT = saturate(e.pos.x);
          sp.push_back(p);
        }
      }
    }

    // ---- update ----
    float drag = std::exp(-0.5f * sdt);
    for (Sparkle& p : sp) {
      p.vel *= drag;
      p.pos += p.vel * sdt;
      p.life -= sdt;
    }
    sp.erase(std::remove_if(sp.begin(), sp.end(), [](const Sparkle& p) { return p.life <= 0; }), sp.end());

    // ---- draw ----
    fxFade(trail, std::exp(-dt * mixf(60.f, 2.f, s.trail)) * c.fadeMul);
    verts.clear();
    float t0 = c.time * c.speed;
    float rays = mixf(0.35f, 1.f, s.rays);
    for (const Sparkle& p : sp) {
      float age = 1.f - p.life / p.maxLife;
      float env = std::sin(saturate(age) * kPi);                              // pop in, fade out
      float tw = std::pow(0.5f + 0.5f * std::sin(t0 * p.freq + p.phase), 3.f);  // sharp twinkle
      float a = env * (0.15f + 0.85f * tw) * alphaScale;
      if (a < 0.01f) continue;
      float t;
      switch (c.look->colorMode) {
        case COLOR_GRADIENT: t = age; break;
        case COLOR_SPEED: t = p.speedT; break;
        case COLOR_POSITION: t = p.posT; break;
        case COLOR_RANDOM: t = p.rnd; break;
        default: t = c.colorT(nullptr, age, p.rnd);
      }
      Vec3 col = c.color(t) * 2.2f;
      float size = p.size * rays * (0.6f + 0.4f * tw);
      verts.push_back({p.pos.x / aspect * c.W, p.pos.y * c.H, col.x, col.y, col.z, a, size});
    }
    fxDrawPoints(trail, verts, POINT_STAR);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  std::vector<Sparkle> sp;
  std::vector<PointVertex> verts;
  Target trail;
  Rng rng;
  float acc[kMaxSplats] = {};
  float aspect = 1;
  float alphaScale = 1;
};

}  // namespace

std::unique_ptr<Effect> makeSparkles() { return std::make_unique<Sparkles>(); }
