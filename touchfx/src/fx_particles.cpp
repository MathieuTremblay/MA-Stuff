// Particles: glowing dots sprayed from the fingers, with optional trails and beat strobing.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

constexpr int kMaxParticles = 40000;

struct Particle {
  Vec2 pos, vel;   // aspect space (x: 0..aspect, y: 0..1)
  float life, maxLife, size, rnd, phase;
  const Emitter* src;  // only valid on spawn frame
  float speedT, posT;  // stored for the speed / position colour modes
};

class Particles : public Effect {
 public:
  bool init() override { return true; }

  void resize(int W, int H) override {
    trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    aspect = float(W) / H;
  }

  void clear() override {
    ps.clear();
    trail.clear();
    alphaScale = 1;
  }

  void frame(const FxCtx& c, const Target& out) override {
    const ParticleSettings& s = c.look->particles;
    float dt = std::min(c.dt, 0.05f);
    float sdt = dt * c.speed;
    float px = c.H / 1080.f;  // sizes are designed at 1080p

    if (c.touching) alphaScale = 1;
    else alphaScale *= c.fadeMul;

    // ---- spawn ----
    float rate = mixf(30.f, 2500.f, s.amount * s.amount);
    int perEmitter = int(c.live.size());
    if (perEmitter > 0) {
      rate /= std::sqrt(float(perEmitter));  // keep the total sensible with many mirrors
      for (size_t i = 0; i < c.live.size(); ++i) {
        const Emitter& e = c.live[i];
        spawnAcc[i % 16] += rate * sdt;
        int n = int(spawnAcc[i % 16]);
        spawnAcc[i % 16] -= n;
        for (int k = 0; k < n && int(ps.size()) < kMaxParticles; ++k) spawn(c, s, e, px);
      }
    }

    // ---- update ----
    float gravity = (s.gravity - 0.5f) * 2.f * 0.8f;
    float drag = std::exp(-0.9f * sdt);
    for (Particle& p : ps) {
      p.vel *= drag;
      p.vel.y -= gravity * sdt;
      p.pos += p.vel * sdt;
      p.life -= sdt;
    }
    ps.erase(std::remove_if(ps.begin(), ps.end(),
                            [&](const Particle& p) {
                              return p.life <= 0 || p.pos.y < -0.2f || p.pos.y > 1.2f || p.pos.x < -0.2f ||
                                     p.pos.x > aspect + 0.2f;
                            }),
             ps.end());

    // ---- draw ----
    float decay = std::exp(-dt * mixf(60.f, 1.2f, s.trail));
    fxFade(trail, decay * c.fadeMul);

    double strobeBeat = c.beat * kDivisions[s.strobeRate];
    bool syncOn = fractf(float(strobeBeat)) < 0.5f;
    verts.clear();
    verts.reserve(ps.size());
    for (const Particle& p : ps) {
      float age = 1.f - p.life / p.maxLife;
      float a = std::min(1.f, p.life / (p.maxLife * 0.3f)) * std::min(1.f, age * 12.f + 0.2f);
      if (s.strobe == PSTROBE_SYNC && !syncOn) continue;
      if (s.strobe == PSTROBE_SPARKLE && fractf(float(strobeBeat) + p.phase) > 0.35f) continue;
      float t;
      switch (c.look->colorMode) {
        case COLOR_GRADIENT: t = age; break;
        case COLOR_SPEED: t = p.speedT; break;
        case COLOR_POSITION: t = p.posT; break;
        case COLOR_RANDOM: t = p.rnd; break;
        default: t = c.colorT(nullptr, age, p.rnd);
      }
      Vec3 col = c.color(t) * 1.8f;
      float sx = p.pos.x / aspect * c.W, sy = p.pos.y * c.H;
      verts.push_back({sx, sy, col.x, col.y, col.z, a * alphaScale, p.size * (0.6f + 0.4f * (1.f - age))});
    }
    fxDrawPoints(trail, verts);
    fxComposite(out, trail.tex, c.look->brightness);
  }

 private:
  void spawn(const FxCtx& c, const ParticleSettings& s, const Emitter& e, float px) {
    Particle p;
    float ang = rng.range(0, kTau);
    float jitter = rng.range(0.f, 0.012f);
    p.pos = Vec2(e.pos.x * aspect, e.pos.y) + Vec2(std::cos(ang), std::sin(ang)) * jitter;
    float spd = mixf(0.03f, 0.8f, s.speed) * rng.range(0.25f, 1.f);
    Vec2 dir(std::cos(ang), std::sin(ang));
    p.vel = e.vel * (0.45f * (1.f - 0.6f * s.spread)) + dir * (spd * (0.15f + s.spread));
    p.maxLife = p.life = mixf(0.25f, 5.f, s.life * s.life) * rng.range(0.5f, 1.f);
    p.size = mixf(3.f, 44.f, s.size * s.size) * px * rng.range(0.5f, 1.2f);
    p.rnd = rng.next();
    p.phase = rng.next();
    p.speedT = saturate(e.speed / 1.5f);
    p.posT = saturate(e.pos.x);
    p.src = nullptr;
    (void)c;
    ps.push_back(p);
  }

  std::vector<Particle> ps;
  std::vector<PointVertex> verts;
  Target trail;
  Rng rng;
  float spawnAcc[16] = {};
  float aspect = 1;
  float alphaScale = 1;
};

}  // namespace

std::unique_ptr<Effect> makeParticles() { return std::make_unique<Particles>(); }
