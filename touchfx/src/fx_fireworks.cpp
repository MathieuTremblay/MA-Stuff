// Fireworks: touch (and every beat division while holding) launches a rocket that bursts at the finger
// into falling, sparkling embers.
#include <algorithm>
#include <cmath>
#include <set>

#include "effects.hpp"

namespace {

struct Rocket {
  Vec2 from, to, pos;  // aspect space
  float t, dur, colorT;
  float burstScale;
};
struct Ember {
  Vec2 pos, vel;
  float life, maxLife, colorT, phase;
};

class Fireworks : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override {
    trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    aspect = float(W) / H;
  }
  void clear() override {
    rockets.clear();
    embers.clear();
    trail.clear();
    alphaScale = 1;
  }

  void frame(const FxCtx& c, const Target& out) override {
    const FireworkSettings& s = c.look->fireworks;
    float dt = std::min(c.dt, 0.05f);
    float sdt = dt * c.speed;
    float px = c.H / 1080.f;
    if (c.touching) alphaScale = 1;
    else alphaScale *= c.fadeMul;

    // ---- launches: a new finger fires at once, held fingers fire on the beat division / audio hits ----
    std::set<int> now;
    for (const Emitter& e : c.live) now.insert(e.touch * 64 + e.mirror);
    double cycles = c.beat * kDivisions[s.rate];
    bool tick = std::floor(cycles) != std::floor(lastCycles) || (c.audioOn && c.onset);
    lastCycles = cycles;
    float burstColor = rng.next();
    for (const Emitter& e : c.live) {
      bool fresh = !prev.count(e.touch * 64 + e.mirror);
      if (!(fresh || tick) || rockets.size() > 64 || c.releasing) continue;  // nothing new after release
      Rocket r;
      r.to = Vec2(e.pos.x * aspect, e.pos.y);
      r.from = s.rockets ? Vec2(r.to.x + rng.range(-0.08f, 0.08f), -0.02f) : r.to;
      r.pos = r.from;
      r.t = 0;
      r.dur = s.rockets ? mixf(0.25f, 0.6f, r.to.y) : 0.f;
      r.colorT = c.look->colorMode == COLOR_RANDOM ? rng.next() : c.colorT(&e, burstColor, rng.next());
      r.burstScale = std::max(0.5f, std::min(c.drive, 1.6f));
      rockets.push_back(r);
    }
    prev.swap(now);

    // ---- rockets fly, then burst ----
    for (Rocket& r : rockets) {
      r.t += sdt;
      float u = r.dur > 0 ? std::min(1.f, r.t / r.dur) : 1.f;
      float ease = 1.f - (1.f - u) * (1.f - u);
      r.pos = r.from + (r.to - r.from) * ease;
      if (u >= 1.f) burst(r, s);
    }
    rockets.erase(std::remove_if(rockets.begin(), rockets.end(),
                                 [](const Rocket& r) { return r.dur <= 0 || r.t >= r.dur; }),
                  rockets.end());

    float gravity = mixf(0.f, 0.6f, s.gravity);
    float drag = std::exp(-1.6f * sdt);
    for (Ember& e : embers) {
      e.vel *= drag;
      e.vel.y -= gravity * sdt;
      e.pos += e.vel * sdt;
      e.life -= sdt;
    }
    embers.erase(std::remove_if(embers.begin(), embers.end(), [](const Ember& e) { return e.life <= 0; }), embers.end());

    // ---- draw ----
    fxFade(trail, std::exp(-dt * mixf(30.f, 1.5f, s.trail)) * c.fadeMul);
    pts.clear();
    for (const Rocket& r : rockets) {
      Vec3 col = c.color(r.colorT) * 2.f + Vec3(0.6f, 0.5f, 0.4f);
      pts.push_back({r.pos.x / aspect * c.W, r.pos.y * c.H, col.x, col.y, col.z, alphaScale * c.gain, 10.f * px});
    }
    for (const Ember& e : embers) {
      float u = 1.f - e.life / e.maxLife;
      float a = std::min(1.f, e.life / (e.maxLife * 0.4f));
      // sparkle: embers twinkle more as they age
      float tw = 1.f - s.sparkle * u * (0.5f + 0.5f * std::sin(e.phase + u * 60.f));
      Vec3 col = c.color(c.look->colorMode == COLOR_GRADIENT ? std::min(1.f, e.colorT * 0.3f + u) : e.colorT) * 2.4f;
      pts.push_back({e.pos.x / aspect * c.W, e.pos.y * c.H, col.x, col.y, col.z, a * tw * alphaScale * c.gain,
                     mixf(3.f, 12.f, s.size) * px * (1.f - 0.5f * u)});
    }
    fxDrawPoints(trail, pts);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  void burst(const Rocket& r, const FireworkSettings& s) {
    int n = int(mixf(40.f, 320.f, s.amount));
    float speed = mixf(0.12f, 0.7f, s.size) * r.burstScale;
    for (int i = 0; i < n && embers.size() < 20000; ++i) {
      Ember e;
      float a = rng.range(0, kTau);
      float sp = speed * std::sqrt(rng.range(0.2f, 1.f));
      e.pos = r.to;
      e.vel = Vec2(std::cos(a), std::sin(a)) * sp;
      e.maxLife = e.life = rng.range(0.8f, 2.2f);
      e.colorT = r.colorT;
      e.phase = rng.range(0, kTau);
      embers.push_back(e);
    }
  }

  std::vector<Rocket> rockets;
  std::vector<Ember> embers;
  std::vector<PointVertex> pts;
  std::set<int> prev;
  Target trail;
  Rng rng;
  double lastCycles = 0;
  float aspect = 1, alphaScale = 1;
};

}  // namespace

std::unique_ptr<Effect> makeFireworks() { return std::make_unique<Fireworks>(); }
