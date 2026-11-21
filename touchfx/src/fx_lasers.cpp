// Lines / lasers: glowing beams from the fingers (fan, burst, web and scan modes).
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
  float d = abs(vSideAlong.x) * vWidth.x;               // pixel distance from centre line
  float core = 1.0 - smoothstep(vWidth.y * 0.5, vWidth.y * 0.5 + 1.2, d);
  float glowW = max(vWidth.x - vWidth.y, 0.001);
  float glow = exp(-d / glowW * 3.0) * uGlow;
  float fall = 1.0 - 0.55 * vSideAlong.y;                // beams dim with distance
  vec3 c = vCol * (core * 1.6 + glow) * fall + vec3(core * 0.35 * fall);  // white-hot core
  o = vec4(c, 1.0);
}
)";

struct Seg {
  Vec2 a, b;  // pixels
  Vec3 col;
};

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

    fxFade(trail, std::exp(-dt * mixf(80.f, 1.5f, s.trail)) * c.fadeMul);

    const std::vector<Emitter>& em = c.shapeEmitters();
    segs.clear();
    if (!em.empty() && c.gain > 0.001f) build(c, s, em);

    float px = c.H / 1080.f;
    float coreW = mixf(1.f, 9.f, s.width) * px;
    float halfW = coreW * 0.5f + mixf(1.f, 46.f, s.glow) * px;
    verts.clear();
    for (const Seg& sg : segs) addQuad(sg, halfW, coreW, c.gain);
    if (!verts.empty()) {
      trail.bind();
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE, GL_ONE);
      prog.use();
      prog.set2f("uRes", float(c.W), float(c.H));
      prog.setf("uGlow", 0.25f + s.glow * 0.6f);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(BeamVertex), verts.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
      glBindVertexArray(0);
      glDisable(GL_BLEND);
    }
    fxComposite(out, trail.tex, c.look->brightness);
  }

 private:
  void build(const FxCtx& c, const LaserSettings& s, const std::vector<Emitter>& em) {
    const float W = float(c.W), H = float(c.H);
    const float diag = std::sqrt(W * W + H * H);
    const float len = mixf(0.08f, 1.2f, s.length) * diag;
    const int n = s.beams;
    auto toPx = [&](Vec2 uv) { return Vec2(uv.x * W, uv.y * H); };
    auto beamColor = [&](const Emitter* e, int i, int count, uint32_t salt) {
      float g = count > 1 ? float(i) / float(count - 1) : 0.5f;
      return c.color(c.colorT(e, g, hash01(salt * 977u + i * 131u + uint32_t(std::floor(c.beat)))));
    };

    switch (s.mode) {
      case LASER_FAN:
      case LASER_BURST: {
        uint32_t step = uint32_t(std::floor(c.beat));
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          for (int i = 0; i < n; ++i) {
            float ang;
            if (s.mode == LASER_FAN) {
              ang = rot + kTau * i / n;
            } else {
              // new random directions on every beat, eased in
              float ph = fractf(float(c.beat));
              ang = hash01(step * 7919u + i * 104729u) * kTau + rot * 0.3f;
              float l = len * std::min(1.f, ph * 6.f);
              Vec2 d = e.m * Vec2(std::cos(ang), std::sin(ang));
              segs.push_back({o, o + d * l, beamColor(&e, i, n, e.touch)});
              continue;
            }
            Vec2 d = e.m * Vec2(std::cos(ang), std::sin(ang));
            segs.push_back({o, o + d * len, beamColor(&e, i, n, e.touch)});
          }
        }
        break;
      }
      case LASER_WEB: {
        // Connect every emitter to every other one; a single point shoots to the corners.
        std::vector<Vec2> pts;
        for (const Emitter& e : em) pts.push_back(toPx(e.pos));
        if (pts.size() == 1) {
          Vec2 corners[4] = {{0, 0}, {W, 0}, {W, H}, {0, H}};
          for (int i = 0; i < 4; ++i) segs.push_back({pts[0], corners[i], beamColor(&em[0], i, 4, 1)});
        } else {
          int k = 0, total = int(pts.size() * (pts.size() - 1) / 2);
          for (size_t i = 0; i < pts.size(); ++i)
            for (size_t j = i + 1; j < pts.size(); ++j) segs.push_back({pts[i], pts[j], beamColor(&em[i], k++, total, 2)});
        }
        // plus a small fan around each point
        int extra = std::max(0, n - 1);
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          for (int i = 0; i < extra; ++i) {
            float ang = rot + kTau * i / extra;
            Vec2 d = e.m * Vec2(std::cos(ang), std::sin(ang));
            segs.push_back({o, o + d * (len * 0.25f), beamColor(&e, i, extra, 3)});
          }
        }
        break;
      }
      case LASER_SCAN: {
        // Parallel lines through each finger that breathe open and closed on the beat.
        float open = 0.5f + 0.5f * std::sin(float(c.beat) * kPi);
        float spacing = mixf(0.01f, 0.08f, open) * H;
        for (const Emitter& e : em) {
          Vec2 o = toPx(e.pos);
          Vec2 d = e.m * Vec2(std::cos(rot * 0.25f), std::sin(rot * 0.25f));
          Vec2 nrm(-d.y, d.x);
          for (int i = 0; i < n; ++i) {
            float off = (i - (n - 1) * 0.5f) * spacing;
            Vec2 base = o + nrm * off;
            segs.push_back({base - d * len, base + d * len, beamColor(&e, i, n, e.touch)});
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
        {a.x + nn.x * halfW, a.y + nn.y * halfW, 1, 0, c.x, c.y, c.z, halfW, coreW},
        {a.x - nn.x * halfW, a.y - nn.y * halfW, -1, 0, c.x, c.y, c.z, halfW, coreW},
        {b.x + nn.x * halfW, b.y + nn.y * halfW, 1, 1, c.x, c.y, c.z, halfW, coreW},
        {b.x - nn.x * halfW, b.y - nn.y * halfW, -1, 1, c.x, c.y, c.z, halfW, coreW},
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
