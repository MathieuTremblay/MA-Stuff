// Plexus: floating points around the fingers that connect with thin lines when they come close.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

struct Node {
  Vec2 pos, vel;  // aspect space
  float life, maxLife, rnd;
};

class Plexus : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override {
    trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    aspect = float(W) / H;
  }
  void clear() override {
    nodes.clear();
    trail.clear();
    alphaScale = 1;
  }

  void frame(const FxCtx& c, const Target& out) override {
    const PlexusSettings& s = c.look->plexus;
    float dt = std::min(c.dt, 0.05f);
    float sdt = dt * c.speed;
    float px = c.H / 1080.f;
    if (c.touching) alphaScale = 1;
    else alphaScale *= c.fadeMul;

    // ---- keep a population of nodes around the fingers ----
    size_t want = size_t(mixf(20.f, 260.f, s.amount));
    float radius = mixf(0.05f, 0.5f, s.spread);
    if (!c.live.empty()) {
      size_t per = std::max<size_t>(4, want / c.live.size());
      float rate = float(per) / 1.5f;  // refill over ~1.5 s
      acc += rate * dt * float(c.live.size()) * c.drive;
      while (acc >= 1.f && nodes.size() < want * 2) {
        acc -= 1.f;
        const Emitter& e = c.live[size_t(rng.next() * c.live.size()) % c.live.size()];
        Node n;
        float a = rng.range(0, kTau), r = radius * std::sqrt(rng.next());
        n.pos = Vec2(e.pos.x * aspect, e.pos.y) + Vec2(std::cos(a), std::sin(a)) * r;
        float va = rng.range(0, kTau);
        n.vel = Vec2(std::cos(va), std::sin(va)) * (mixf(0.005f, 0.12f, s.drift) * rng.range(0.3f, 1.f)) + e.vel * 0.1f;
        n.maxLife = n.life = rng.range(1.5f, 4.f);
        n.rnd = rng.next();
        nodes.push_back(n);
      }
    }
    for (Node& n : nodes) {
      n.pos += n.vel * sdt;
      n.life -= dt;
    }
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const Node& n) { return n.life <= 0; }), nodes.end());

    // ---- draw: links between close nodes, then the dots ----
    fxFade(trail, std::exp(-dt * mixf(40.f, 3.f, s.trail)) * c.fadeMul);
    float range = mixf(0.04f, 0.25f, s.range);
    float range2 = range * range;
    segs.clear();
    auto alphaOf = [&](const Node& n) {
      float u = 1.f - n.life / n.maxLife;
      return std::min(1.f, u * 6.f) * std::min(1.f, n.life * 1.5f) * alphaScale * c.gain;
    };
    for (size_t i = 0; i < nodes.size(); ++i) {
      for (size_t j = i + 1; j < nodes.size(); ++j) {
        Vec2 d = nodes[i].pos - nodes[j].pos;
        float d2 = dot(d, d);
        if (d2 > range2) continue;
        float k = 1.f - std::sqrt(d2) / range;
        float a = k * std::min(alphaOf(nodes[i]), alphaOf(nodes[j]));
        float t = c.look->colorMode == COLOR_GRADIENT ? k : c.colorT(nullptr, k, nodes[i].rnd);
        Vec3 col = c.color(t) * (a * 0.9f);
        Vec2 pa(nodes[i].pos.x / aspect * c.W, nodes[i].pos.y * c.H), pb(nodes[j].pos.x / aspect * c.W, nodes[j].pos.y * c.H);
        segs.push_back({pa, pb, col, 3.f * px, 1.2f * px});
      }
    }
    fxDrawGlowSegs(trail, segs, 0.5f, false);
    dots.clear();
    for (const Node& n : nodes) {
      float a = alphaOf(n);
      Vec3 col = c.color(c.colorT(nullptr, 1.f, n.rnd)) * 1.8f;
      dots.push_back({n.pos.x / aspect * c.W, n.pos.y * c.H, col.x, col.y, col.z, a, mixf(4.f, 22.f, s.size) * px});
    }
    fxDrawPoints(trail, dots);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  std::vector<Node> nodes;
  std::vector<GlowSeg> segs;
  std::vector<PointVertex> dots;
  Target trail;
  Rng rng;
  float acc = 0, aspect = 1, alphaScale = 1;
};

}  // namespace

std::unique_ptr<Effect> makePlexus() { return std::make_unique<Plexus>(); }
