// Ribbons: silky glowing strands that follow each finger, tapering towards the tail, with the palette running
// along their length. Several strands per finger weave around the finger's path.
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>

#include "effects.hpp"

namespace {

struct Trail {
  std::deque<Vec2> pts;  // pixels, newest at the front
  bool seen = false;
  int touch = 0;
};

class Ribbons : public Effect {
 public:
  bool init() override { return true; }
  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override {
    trails.clear();
    trail.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const RibbonSettings& s = c.look->ribbons;
    float dt = std::min(c.dt, 0.05f);
    float px = c.H / 1080.f;
    size_t maxLen = size_t(mixf(12.f, 160.f, s.length));

    for (auto& kv : trails) kv.second.seen = false;
    for (const Emitter& e : c.live) {
      Trail& t = trails[e.touch * 64 + e.mirror];
      t.seen = true;
      t.touch = e.touch;
      Vec2 p(e.pos.x * c.W, e.pos.y * c.H);
      if (t.pts.empty() || length(p - t.pts.front()) > 0.5f) t.pts.push_front(p);
      while (t.pts.size() > maxLen) t.pts.pop_back();
    }
    // released fingers: the tail catches up with the head
    for (auto it = trails.begin(); it != trails.end();) {
      Trail& t = it->second;
      if (!t.seen) {
        int drop = std::max(1, int(t.pts.size() / 12));
        for (int i = 0; i < drop && !t.pts.empty(); ++i) t.pts.pop_back();
      }
      if (t.pts.size() < 2 && !t.seen) it = trails.erase(it);
      else ++it;
    }

    fxFade(trail, std::exp(-dt * mixf(40.f, 2.f, s.trail)) * c.fadeMul);
    segs.clear();
    float width = mixf(2.f, 26.f, s.width) * px * std::sqrt(std::max(0.2f, c.drive));
    float wave = mixf(0.f, 40.f, s.wave) * px;
    float tphase = c.time * c.speed * 3.f;
    for (auto& kv : trails) {
      const Trail& t = kv.second;
      int n = int(t.pts.size());
      if (n < 2) continue;
      for (int k = 0; k < s.strands; ++k) {
        float strandOff = s.strands > 1 ? (float(k) / (s.strands - 1) - 0.5f) : 0.f;
        Vec2 prev;
        for (int i = 0; i < n; ++i) {
          // direction of the path at this point, to offset strands sideways
          Vec2 a = t.pts[std::max(0, i - 1)], b = t.pts[std::min(n - 1, i + 1)];
          Vec2 d = a - b;
          float dl = length(d);
          Vec2 nrm = dl > 0.001f ? Vec2(-d.y / dl, d.x / dl) : Vec2(0, 1);
          float u = float(i) / (n - 1);  // 0 head .. 1 tail
          float off = strandOff * wave * 2.f + std::sin(tphase + i * 0.25f + k * 2.1f) * wave * (0.3f + u);
          Vec2 p = t.pts[i] + nrm * off;
          if (i > 0) {
            float taper = 1.f - u;
            Vec3 col = c.color(c.colorT(nullptr, u, hash01(uint32_t(k) * 31u + uint32_t(t.touch)))) *
                       (0.4f + 0.8f * taper) * c.gain;
            float hw = width * (0.25f + taper) * (1.f - 0.15f * k / std::max(1, s.strands));
            segs.push_back({prev, p, col, hw, std::max(1.f, hw * 0.3f)});
          }
          prev = p;
        }
      }
    }
    fxDrawGlowSegs(trail, segs, 0.3f + s.glow, true);
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  std::map<int, Trail> trails;
  std::vector<GlowSeg> segs;
  Target trail;
};

}  // namespace

std::unique_ptr<Effect> makeRibbons() { return std::make_unique<Ribbons>(); }
