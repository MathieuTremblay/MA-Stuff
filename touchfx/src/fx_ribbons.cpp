// Ribbons: flat bands of cloth floating in the wind. Each ribbon is a chain of points (Verlet physics) pinned to
// a finger; the rest trails behind with inertia, pushed by wind and flutter. The band twists along its length:
// the projected width shrinks and flips, front and back faces get different colours, and a silky sheen
// catches the light where the band faces the viewer. Released ribbons drift away on the wind and fade.
#include <algorithm>
#include <cmath>
#include <map>

#include "effects.hpp"

namespace {

const char* kRibbonVS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in float aAcross;
layout(location = 2) in vec3 aCol;
uniform vec2 uRes;
out float vAcross; out vec3 vCol;
void main() {
  gl_Position = vec4(aPos / uRes * 2.0 - 1.0, 0.0, 1.0);
  vAcross = aAcross; vCol = aCol;
}
)";

const char* kRibbonFS = R"(
in float vAcross; in vec3 vCol;
out vec4 o;
uniform float uSheen;
void main() {
  float x = abs(vAcross);
  float body = 1.0 - smoothstep(0.82, 1.0, x);            // soft edges
  float rim = smoothstep(0.55, 0.92, x) * body * uSheen;   // brighter hem, like satin
  o = vec4(vCol * (body * 0.85 + rim * 0.9), 1.0);
}
)";

struct Ribbon {
  std::vector<Vec2> p, prev;  // pixels
  Mat2 m;                     // mirror transform (mirrored ribbons blow the mirrored way)
  float alpha = 1, phase = 0;
  bool seen = false, anchored = true;
  int touch = 0;
};

class Ribbons : public Effect {
 public:
  bool init() override {
    bool ok = prog.build("ribbons", kRibbonVS, kRibbonFS);
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei st = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, st, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    return ok;
  }
  void resize(int W, int H) override { trail.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override {
    ribbons.clear();
    trail.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const RibbonSettings& s = c.look->ribbons;
    float dt = std::min(c.dt, 1.f / 30.f);
    float px = c.H / 1080.f;
    int nodes = int(mixf(10.f, 56.f, s.length));
    float seg = 16.f * px;
    float width = mixf(6.f, 90.f, s.width * s.width) * px;
    float spacing = width * 1.3f;
    float t = c.time;

    // ---- attach ribbons to fingers (one per strand) ----
    for (auto& kv : ribbons) kv.second.seen = false;
    for (const Emitter& e : c.live) {
      for (int k = 0; k < s.strands; ++k) {
        int key = ((e.touch * 64 + e.mirror) << 3) | k;
        Ribbon& r = ribbons[key];
        Vec2 windDir = e.m * Vec2(-0.85f, -0.35f);  // default breeze: to the left and a bit down (trails behind)
        Vec2 perp(-windDir.y, windDir.x);
        float off = (k - (s.strands - 1) * 0.5f) * spacing;
        Vec2 anchor = Vec2(e.pos.x * c.W, e.pos.y * c.H) + perp * off;
        if (int(r.p.size()) != nodes) {
          // new ribbon (or new length): lay it out downwind
          Vec2 start = r.p.empty() ? anchor : r.p[0];
          r.p.resize(size_t(nodes));
          r.prev.resize(size_t(nodes));
          for (int i = 0; i < nodes; ++i) r.p[i] = r.prev[i] = start + windDir * (seg * i);
          r.phase = hash01(uint32_t(key) * 2654435761u) * kTau;
        }
        r.p[0] = anchor;
        r.m = e.m;
        r.seen = true;
        r.anchored = true;
        r.alpha = 1.f;
        r.touch = e.touch;
      }
    }
    for (auto it = ribbons.begin(); it != ribbons.end();) {
      Ribbon& r = it->second;
      if (!r.seen) {
        r.anchored = false;  // let go: it floats away
        r.alpha *= std::exp(-dt * 1.8f);
      }
      if (r.alpha < 0.01f) it = ribbons.erase(it);
      else ++it;
    }

    // ---- physics ----
    float windStrength = mixf(60.f, 900.f, s.wind) * px * std::max(0.5f, std::min(c.drive, 1.8f));
    float flutterAmp = mixf(0.f, 1600.f, s.flutter) * px;
    float damping = mixf(0.90f, 0.985f, 1.f - s.stiffness * 0.5f);
    int iterations = 2 + int(s.stiffness * 6.f);
    const int sub = 2;
    float h = dt / sub;
    for (auto& kv : ribbons) {
      Ribbon& r = kv.second;
      const int n = int(r.p.size());  // each ribbon keeps its own length (released ones keep the old one)
      Vec2 windDir = r.m * Vec2(-0.85f, -0.35f);
      Vec2 perp(-windDir.y, windDir.x);
      for (int st = 0; st < sub && n >= 2; ++st) {
        int first = r.anchored ? 1 : 0;
        for (int i = first; i < n; ++i) {
          float u = float(i) / (n - 1);
          // gusts: the wind breathes; flutter: a travelling wave down the ribbon, stronger at the tail
          float gust = 0.7f + 0.3f * std::sin(t * 0.7f + r.phase);
          float wave = std::sin(t * mixf(3.f, 9.f, s.flutter) * c.speed - i * 0.45f + r.phase);
          Vec2 acc = windDir * (windStrength * gust) + perp * (wave * flutterAmp * (0.15f + u));
          if (!r.anchored) acc += Vec2(0, 120.f * px);  // released ribbons drift up a little
          Vec2 vel = (r.p[i] - r.prev[i]) * damping;
          r.prev[i] = r.p[i];
          r.p[i] += vel + acc * (h * h);
        }
        for (int it = 0; it < iterations; ++it) {
          for (int i = 0; i + 1 < n; ++i) {
            Vec2 d = r.p[i + 1] - r.p[i];
            float l = std::max(0.001f, length(d));
            Vec2 corr = d * ((l - seg) / l);
            if (i == 0 && r.anchored) {
              r.p[i + 1] = r.p[i + 1] - corr;
            } else {
              r.p[i] += corr * 0.5f;
              r.p[i + 1] = r.p[i + 1] - corr * 0.5f;
            }
          }
        }
      }
    }

    // ---- build the twisted band ----
    verts.clear();
    for (auto& kv : ribbons) {
      const Ribbon& r = kv.second;
      const int n = int(r.p.size());
      float a = r.alpha * c.gain;
      if (a < 0.005f || n < 2) continue;
      Vec2 lastL, lastR;
      Vec3 lastC;
      for (int i = 0; i < n; ++i) {
        float u = float(i) / (n - 1);
        Vec2 tan = r.p[std::min(n - 1, i + 1)] - r.p[std::max(0, i - 1)];
        float tl = std::max(0.001f, length(tan));
        Vec2 n(-tan.y / tl, tan.x / tl);
        float twist = t * mixf(0.6f, 2.5f, s.flutter) + i * 0.16f + r.phase;
        float facing = std::cos(twist);  // +1 front facing us, -1 back facing us
        float hw = width * 0.5f * (1.f - 0.45f * u) * facing;
        float colorPos = c.look->colorMode == COLOR_GRADIENT ? u : c.colorT(nullptr, u, hash01(uint32_t(r.touch) * 7u));
        Vec3 front = c.color(colorPos);
        Vec3 back = c.color(1.f - colorPos) * 0.55f;
        float f = std::fabs(facing);
        Vec3 col = (facing >= 0 ? front : back) * (0.3f + 0.9f * f);
        col = col + Vec3(1, 1, 1) * (s.sheen * 0.7f * std::pow(f, 14.f));  // silk highlight when flat to us
        col = col * (a * (1.f - 0.35f * u));
        Vec2 L = r.p[i] + n * hw, R = r.p[i] - n * hw;
        if (i > 0) {
          float q[6][6] = {{lastL.x, lastL.y, 1, lastC.x, lastC.y, lastC.z}, {lastR.x, lastR.y, -1, lastC.x, lastC.y, lastC.z},
                           {L.x, L.y, 1, col.x, col.y, col.z},             {L.x, L.y, 1, col.x, col.y, col.z},
                           {lastR.x, lastR.y, -1, lastC.x, lastC.y, lastC.z}, {R.x, R.y, -1, col.x, col.y, col.z}};
          for (auto& v : q) verts.insert(verts.end(), v, v + 6);
        }
        lastL = L;
        lastR = R;
        lastC = col;
      }
    }

    fxFade(trail, std::exp(-dt * 45.f) * c.fadeMul);
    if (!verts.empty()) {
      trail.bind();
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE, GL_ONE);
      prog.use();
      prog.set2f("uRes", float(c.W), float(c.H));
      prog.setf("uSheen", s.sheen);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size() / 6));
      glBindVertexArray(0);
      glDisable(GL_BLEND);
    }
    fxCopy(out, trail.tex, c.look->brightness);
  }

 private:
  Program prog;
  GLuint vao = 0, vbo = 0;
  std::map<int, Ribbon> ribbons;
  std::vector<float> verts;
  Target trail;
};

}  // namespace

std::unique_ptr<Effect> makeRibbons() { return std::make_unique<Ribbons>(); }
