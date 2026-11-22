// Lines / lasers: a laser-show style effect. Thin, very bright beams (the global bloom adds the haze),
// drawn with MAX blending so crossings and polyline joints never bead up.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

struct BeamVertex {
  float x, y;      // pixels
  float side;      // -1..1 across the beam
  float along;     // 0..1 along the beam
  float r, g, b;
  float halfW, coreW;  // pixels
};

const char* kBeamVS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aSideAlong;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec2 aWidth;
uniform vec2 uRes;
out vec2 vSideAlong; out vec3 vCol; out vec2 vWidth;
void main() {
  gl_Position = vec4(aPos / uRes * 2.0 - 1.0, 0.0, 1.0);
  vSideAlong = aSideAlong; vCol = aCol; vWidth = aWidth;
}
)";

const char* kBeamFS = R"(
in vec2 vSideAlong; in vec3 vCol; in vec2 vWidth;
out vec4 o;
uniform float uGlow;
void main() {
  float d = abs(vSideAlong.x) * vWidth.x;               // pixel distance from the centre line
  float halfCore = vWidth.y * 0.5;
  float core = 1.0 - smoothstep(halfCore, halfCore + 1.0, d);
  float glowW = max(vWidth.x - halfCore, 0.001);
  float glow = exp(-max(d - halfCore, 0.0) / glowW * 4.0) * uGlow;
  float fall = 1.0 - 0.35 * vSideAlong.y;                // beams dim slightly with distance
  vec3 c = vCol * (core * 3.2 + glow * 1.3) * fall + vec3(core * 1.1 * fall);  // white-hot core
  o = vec4(c, 1.0);
}
)";

struct Seg {
  Vec2 a, b;  // pixels
  Vec3 col;
  float alongA = 0, alongB = 1;
};

constexpr int kMaxSegs = 8000;

class Lasers : public Effect {
 public:
  bool init() override {
    bool ok = prog.build("lasers", kBeamVS, kBeamFS);
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei st = sizeof(BeamVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, st, (void*)(7 * sizeof(float)));
    glBindVertexArray(0);
    return ok;
  }

  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }

  void clear() override { trail.clear(); }

  void frame(const FxCtx& c, const Target& out) override {
    const LaserSettings& s = c.look->lasers;
    float dt = std::min(c.dt, 0.05f);
    rot += dt * c.speed * (s.rotation - 0.5f) * 2.f * kPi * 0.5f;
    rot = std::fmod(rot, kTau * 64.f);

    fxFade(trail, std::exp(-dt * mixf(90.f, 1.5f, s.trail)) * c.fadeMul);

    const std::vector<Emitter>& em = c.shapeEmitters();
    segs.clear();
    if (!em.empty() && c.gain > 0.001f) build(c, s, em);

    float px = c.H / 1080.f;
    float coreW = mixf(1.2f, 7.f, s.width) * px;
    float halfW = coreW * 0.5f + mixf(2.f, 34.f, s.glow) * px;
    verts.clear();
    for (const Seg& sg : segs) addQuad(sg, halfW, coreW, c.gain * std::min(c.drive, 1.2f));
    if (!verts.empty()) {
      trail.bind();
      glEnable(GL_BLEND);
      glBlendEquation(GL_MAX);  // overlapping beams keep the brightest value: no beads at joints
      prog.use();
      prog.set2f("uRes", float(c.W), float(c.H));
      prog.setf("uGlow", 0.2f + s.glow * 0.8f);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(BeamVertex), verts.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
      glBindVertexArray(0);
      glBlendEquation(GL_FUNC_ADD);
      glDisable(GL_BLEND);
    }
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  bool visible(const LaserSettings& s, const FxCtx& c, int i, int n) const {
    switch (s.chase) {
      case CHASE_STEP: return i == int(std::floor(c.beat * 2.0)) % std::max(1, n);
      case CHASE_ALTERNATE: return ((i + int(std::floor(c.beat))) & 1) == 0;
      case CHASE_RANDOM: return hash01(uint32_t(std::floor(c.beat * 4.0)) * 7919u + uint32_t(i) * 104729u) < 0.5f;
      default: return true;
    }
  }

  void line(Vec2 a, Vec2 b, Vec3 col, float alongA = 0, float alongB = 1) {
    if (int(segs.size()) < kMaxSegs) segs.push_back({a, b, col, alongA, alongB});
  }

  void build(const FxCtx& c, const LaserSettings& s, const std::vector<Emitter>& em) {
    const float W = float(c.W), H = float(c.H);
    const float diag = std::sqrt(W * W + H * H);
    const float len = mixf(0.08f, 1.3f, s.length) * diag;
    const int n = s.beams;
    const float swingAng = s.swing * std::sin(float(c.beat) * kPi * 0.5f) * kPi * 0.5f;
    auto toPx = [&](Vec2 uv) { return Vec2(uv.x * W, uv.y * H); };
    auto beamColor = [&](const Emitter* e, int i, int count, uint32_t salt) {
      float g = count > 1 ? float(i) / float(count - 1) : 0.5f;
      return c.color(c.colorT(e, g, hash01(salt * 977u + i * 131u + uint32_t(std::floor(c.beat)))));
    };
    auto dirOf = [](const Emitter& e, float ang) { return e.m * Vec2(std::cos(ang), std::sin(ang)); };

    switch (s.mode) {
      case LASER_FAN: {
        // A fan of beams from each finger. Spread = fan width, swing = sweep on the beat.
        float fanA = mixf(0.15f, kTau, s.spread);
        bool full = s.spread > 0.97f;
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          for (int i = 0; i < n; ++i) {
            if (!visible(s, c, i, n)) continue;
            float k = full ? float(i) / n : (n > 1 ? float(i) / (n - 1) - 0.5f : 0.f);
            float ang = kPi * 0.5f + rot + swingAng + k * fanA;
            line(o, o + dirOf(e, ang) * len, beamColor(&e, i, n, e.touch));
          }
        }
        break;
      }
      case LASER_AIM: {
        // Projectors along the bottom edge all aim at the fingers, like a stage laser rig.
        int srcN = std::max(1, n);
        float width = mixf(0.1f, 1.0f, s.spread) * W;
        int targets = std::min<int>(int(em.size()), 8);
        for (int i = 0; i < srcN; ++i) {
          if (!visible(s, c, i, srcN)) continue;
          float k = srcN > 1 ? float(i) / (srcN - 1) - 0.5f : 0.f;
          Vec2 src(W * 0.5f + k * width, -2.f);
          for (int t = 0; t < targets; ++t) {
            const Emitter& e = em[t];
            Vec2 tgt = toPx(e.pos);
            Vec2 d = tgt - src;
            float dl = std::max(1.f, length(d));
            d = d * (1.f / dl);
            Vec2 nrm(-d.y, d.x);
            tgt += nrm * (std::sin(float(c.beat) * kPi * 0.5f + i * 1.7f) * s.swing * 0.12f * H);
            d = tgt - src;
            dl = std::max(1.f, length(d));
            d = d * (1.f / dl);
            line(src, src + d * std::max(dl, len), beamColor(&e, i, srcN, 5));
          }
        }
        break;
      }
      case LASER_WEB: {
        // Connect the fingers to each other; a single point shoots to the corners.
        std::vector<Vec2> pts;
        for (const Emitter& e : em) {
          if (pts.size() >= 24) break;
          pts.push_back(toPx(e.pos));
        }
        if (pts.size() == 1) {
          Vec2 corners[4] = {{0, 0}, {W, 0}, {W, H}, {0, H}};
          for (int i = 0; i < 4; ++i)
            if (visible(s, c, i, 4)) line(pts[0], corners[i], beamColor(&em[0], i, 4, 1));
        } else {
          int k = 0, total = int(pts.size() * (pts.size() - 1) / 2);
          for (size_t i = 0; i < pts.size(); ++i)
            for (size_t j = i + 1; j < pts.size(); ++j, ++k)
              if (visible(s, c, k, total)) line(pts[i], pts[j], beamColor(&em[i], k, total, 2));
        }
        int extra = std::max(0, n - 1);
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          for (int i = 0; i < extra; ++i) {
            float ang = rot + swingAng + kTau * i / extra;
            line(o, o + dirOf(e, ang) * (len * 0.25f), beamColor(&e, i, extra, 3));
          }
        }
        break;
      }
      case LASER_SCAN: {
        // Parallel lines through each finger that open and close on the beat.
        float open = 0.5f + 0.5f * std::sin(float(c.beat) * kPi);
        float spacing = mixf(0.005f, 0.1f, s.spread) * mixf(0.4f, 1.f, open) * H;
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          Vec2 d = dirOf(e, rot * 0.25f + swingAng);
          Vec2 nrm(-d.y, d.x);
          for (int i = 0; i < n; ++i) {
            if (!visible(s, c, i, n)) continue;
            Vec2 base = o + nrm * ((i - (n - 1) * 0.5f) * spacing);
            line(base - d * len, base + d * len, beamColor(&e, i, n, e.touch));
          }
        }
        break;
      }
      case LASER_TUNNEL: {
        // Rings expanding from each finger, a new ring every half beat.
        int rings = std::clamp(n, 1, 12);
        float maxR = mixf(0.08f, 0.9f, s.spread) * H;
        const int sides = 72;
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          for (int j = 0; j < rings; ++j) {
            if (!visible(s, c, j, rings)) continue;
            float ph = fractf(float(c.beat) * 0.5f / rings * 2.f + float(j) / rings);
            float r = ph * maxR;
            float fade = std::sin(ph * kPi);  // fade in from the centre, out at the edge
            if (r < 2.f) continue;
            Vec3 col = beamColor(&e, j, rings, 4) * fade;
            float wob = s.swing * 0.25f;
            Vec2 prev;
            for (int k = 0; k <= sides; ++k) {
              float a = kTau * k / sides + rot;
              float rr = r * (1.f + wob * std::sin(a * 3.f + float(c.beat) * kPi));
              Vec2 p = o + Vec2(std::cos(a), std::sin(a)) * rr;
              if (k) line(prev, p, col, 0.f, 0.f);
              prev = p;
            }
          }
        }
        break;
      }
      case LASER_WAVE: {
        // Sine waves across the whole screen through each finger.
        int lines = std::clamp(n, 1, 8);
        float amp = mixf(0.01f, 0.35f, s.spread) * H;
        const float step = 10.f * (H / 1080.f);
        for (const Emitter& e : em) {
          float ey = e.pos.y * H;
          float sign = e.m.d < 0 ? -1.f : 1.f;
          for (int j = 0; j < lines; ++j) {
            if (!visible(s, c, j, lines)) continue;
            float freq = kTau / W * (1.5f + j * 0.5f + s.swing * 3.f);
            float phase = rot * 2.f + j * 0.9f + float(c.beat) * kPi * 0.5f;
            Vec3 col = beamColor(&e, j, lines, 6);
            Vec2 prev;
            for (float x = 0; x <= W + step; x += step) {
              Vec2 p(x, ey + sign * amp * std::sin(x * freq + phase) * (1.f - 0.12f * j));
              if (x > 0) line(prev, p, col, 0.f, 0.f);
              prev = p;
            }
          }
        }
        break;
      }
    }
  }

  void addQuad(const Seg& s, float halfW, float coreW, float gain) {
    Vec2 d = s.b - s.a;
    float l = length(d);
    if (l < 0.5f) return;
    d = d * (1.f / l);
    Vec2 nn(-d.y, d.x);
    Vec2 a = s.a - d * halfW, b = s.b + d * halfW;
    Vec3 c = s.col * gain;
    BeamVertex v[4] = {
        {a.x + nn.x * halfW, a.y + nn.y * halfW, 1, s.alongA, c.x, c.y, c.z, halfW, coreW},
        {a.x - nn.x * halfW, a.y - nn.y * halfW, -1, s.alongA, c.x, c.y, c.z, halfW, coreW},
        {b.x + nn.x * halfW, b.y + nn.y * halfW, 1, s.alongB, c.x, c.y, c.z, halfW, coreW},
        {b.x - nn.x * halfW, b.y - nn.y * halfW, -1, s.alongB, c.x, c.y, c.z, halfW, coreW},
    };
    verts.push_back(v[0]);
    verts.push_back(v[1]);
    verts.push_back(v[2]);
    verts.push_back(v[2]);
    verts.push_back(v[1]);
    verts.push_back(v[3]);
  }

  Program prog;
  GLuint vao = 0, vbo = 0;
  Target trail;
  std::vector<Seg> segs;
  std::vector<BeamVertex> verts;
  float rot = 0;
};

}  // namespace

std::unique_ptr<Effect> makeLasers() { return std::make_unique<Lasers>(); }
