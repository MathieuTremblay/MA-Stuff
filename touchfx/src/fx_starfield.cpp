// Starfield: stars streaking out of the finger like a jump to hyperspace. Speed follows BPM (and audio).
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

struct Star {
  float x, y, z;  // x,y in -1..1 (direction), z = depth 0..1 (1 far)
  float rnd;
};

class Starfield : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override {
    trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    aspect = float(W) / H;
  }
  void clear() override {
    trail.clear();
    stars.clear();
    center = Vec2(0.5f, 0.5f);
  }

  void frame(const FxCtx& c, const Target& out) override {
    const StarfieldSettings& s = c.look->starfield;
    float dt = std::min(c.dt, 0.05f);
    float px = c.H / 1080.f;
    fxFade(trail, std::exp(-dt * 25.f) * c.fadeMul);

    const std::vector<Emitter>& em = c.shapeEmitters();
    size_t want = size_t(mixf(100.f, 1500.f, s.amount));
    while (stars.size() < want) stars.push_back(newStar(rng.range(0.05f, 1.f)));
    if (stars.size() > want) stars.resize(want);

    std::vector<Vec2> centers;  // one warp origin per emitter (mirrors included)
    for (const Emitter& e : em)
      if (centers.size() < 8) centers.push_back(e.pos);  // cap the cost with many fingers x mirrors
    if (centers.empty() || c.gain < 0.001f) {
      fxCopy(out, trail.tex, c.look->brightness);
      return;
    }

    float speed = mixf(0.1f, 1.6f, s.speed) * c.speed * (0.6f + 0.6f * std::min(c.drive, 2.f));
    float streak = mixf(0.f, 0.25f, s.streak);
    segs.clear();
    for (Star& st : stars) {
      float z0 = st.z;
      st.z -= dt * speed;
      if (st.z <= 0.02f) {
        st = newStar(1.f);
        continue;
      }
      float zTail = std::min(1.f, st.z + streak * speed * 0.3f + 0.004f);
      for (size_t k = 0; k < centers.size(); ++k) {
        Vec2 o(centers[k].x * c.W, centers[k].y * c.H);
        float scale = c.H * 0.5f;
        Vec2 head = o + Vec2(st.x / st.z, st.y / st.z) * (scale * 0.12f);
        Vec2 tail = o + Vec2(st.x / zTail, st.y / zTail) * (scale * 0.12f);
        if (head.x < -50 || head.y < -50 || head.x > c.W + 50 || head.y > c.H + 50) continue;
        float near = 1.f - st.z;
        float tw = 1.f - s.twinkle * 0.5f * (1.f + std::sin(c.time * 12.f + st.rnd * 50.f));
        float t = c.look->colorMode == COLOR_GRADIENT ? near : c.colorT(&em[std::min(k, em.size() - 1)], near, st.rnd);
        Vec3 col = c.color(t) * (near * near * 2.2f * tw * c.gain);
        float w = mixf(0.6f, 4.f, s.size) * px * (0.3f + near * 1.4f);
        segs.push_back({tail, head, col, w + 2.f * px, w});
      }
      (void)z0;
    }
    fxDrawGlowSegs(trail, segs, 0.4f, false);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  Star newStar(float z) {
    float a = rng.range(0, kTau), r = std::sqrt(rng.next()) * 1.2f + 0.02f;
    return {std::cos(a) * r * aspect, std::sin(a) * r, z, rng.next()};
  }

  std::vector<Star> stars;
  std::vector<GlowSeg> segs;
  Target trail;
  Rng rng;
  float aspect = 1;
  Vec2 center{0.5f, 0.5f};
};

}  // namespace

std::unique_ptr<Effect> makeStarfield() { return std::make_unique<Starfield>(); }
