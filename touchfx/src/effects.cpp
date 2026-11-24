#include "effects.hpp"

#include <algorithm>
#include <cmath>

Vec3 paletteAt(const Palette& p, float t) {
  t = saturate(t);
  Vec3 lo(p.low[0], p.low[1], p.low[2]);
  Vec3 mi(p.mid[0], p.mid[1], p.mid[2]);
  Vec3 hi(p.high[0], p.high[1], p.high[2]);
  return t < 0.5f ? mix3(lo, mi, t * 2.f) : mix3(mi, hi, (t - 0.5f) * 2.f);
}

// VIDEO colour mode: colorT() returns 2 + an encoded canvas position; color() looks the video up there.
static float encodePos(Vec2 p) {
  int x = std::clamp(int(p.x * 1023.f + 0.5f), 0, 1023), y = std::clamp(int(p.y * 1023.f + 0.5f), 0, 1023);
  return 2.f + float(y * 1024 + x) / (1024.f * 1024.f);
}

Vec3 FxCtx::color(float t) const {
  if (t < 2.f) return paletteAt(*pal, t);
  int code = std::clamp(int((t - 2.f) * (1024.f * 1024.f) + 0.5f), 0, 1024 * 1024 - 1);
  Vec2 p((code % 1024) / 1023.f, (code / 1024) / 1023.f);
  if (!vidThumb || vidThumbW <= 0 || vidThumbH <= 0) return paletteAt(*pal, p.x);  // no video: palette by position
  float u = (p.x - 0.5f) * vidFitX + 0.5f, v = (p.y - 0.5f) * vidFitY + 0.5f;
  float fx = std::clamp(u, 0.f, 1.f) * (vidThumbW - 1), fy = std::clamp(v, 0.f, 1.f) * (vidThumbH - 1);
  int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, vidThumbW - 1), y1 = std::min(y0 + 1, vidThumbH - 1);
  float ax = fx - x0, ay = fy - y0;
  auto px = [&](int x, int y) {
    const float* q = vidThumb + (size_t(y) * vidThumbW + x) * 3;
    return Vec3(q[0], q[1], q[2]);
  };
  Vec3 col = (px(x0, y0) * (1 - ax) + px(x1, y0) * ax) * (1 - ay) + (px(x0, y1) * (1 - ax) + px(x1, y1) * ax) * ay;
  // keep the hue, soften the darkness a little so dark parts of the video still give some light
  float m = std::max(col.x, std::max(col.y, col.z));
  return col * ((0.35f + 0.65f * m) / std::max(m, 0.12f));
}

float FxCtx::colorT(const Emitter* e, float gradient, float rnd) const {
  switch (look->colorMode) {
    case COLOR_GRADIENT: return saturate(gradient);
    case COLOR_SPEED: return e ? saturate(e->speed / 1.5f) : 0.5f;
    case COLOR_POSITION: return e ? saturate(e->pos.x) : 0.5f;
    case COLOR_BEAT: {
      int step = int(std::floor(beat)) % 3;
      return step * 0.5f;  // low -> mid -> high, one per beat
    }
    case COLOR_RANDOM: return saturate(rnd);
    case COLOR_VIDEO: {
      // the video's colour where the finger is (no finger given: near one of the fingers, picked by rnd)
      Vec2 p(saturate(gradient), 0.5f);
      if (e) p = e->pos;
      else if (!live.empty()) {
        const Emitter& f = live[size_t(rnd * live.size()) % live.size()];
        p = f.pos + Vec2(hash01(uint32_t(rnd * 9e6f)) - 0.5f, hash01(uint32_t(rnd * 7e6f) + 3u) - 0.5f) * 0.08f;
      }
      return encodePos(p);
    }
  }
  return 0.5f;
}

std::vector<Mat2> mirrorMatrices(int mode) {
  const Mat2 I;
  const Mat2 flipX = Mat2::scale(-1, 1), flipY = Mat2::scale(1, -1);
  switch (mode) {
    case MIRROR_LR: return {I, flipX};
    case MIRROR_UD: return {I, flipY};
    case MIRROR_QUAD: return {I, flipX, flipY, Mat2::scale(-1, -1)};
    case MIRROR_RADIAL6:
    case MIRROR_RADIAL8: {
      // Kaleidoscope symmetry: n/2 rotations, each with its mirror image.
      int k = mode == MIRROR_RADIAL6 ? 3 : 4;
      std::vector<Mat2> out;
      for (int i = 0; i < k; ++i) {
        Mat2 r = Mat2::rot(kTau * i / k);
        out.push_back(r);
        out.push_back(r * flipY);
      }
      return out;
    }
    default: return {I};
  }
}

// ---------------------------------------------------------------------------
namespace {

struct Shared {
  Program copy, fade, splat, points, stars, glow;
  GLuint pointVbo = 0, pointVao = 0;
  GLuint glowVbo = 0, glowVao = 0;
  std::vector<float> glowVerts;
};
Shared g;

const char* kCopyFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform float uGain;
void main() { o = vec4(max(texture(uSrc, vUv).rgb, vec3(0.0)) * uGain, 1.0); }
)";

const char* kFadeFS = R"(
out vec4 o;
void main() { o = vec4(0.0); }
)";

const char* kSplatFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uTarget;
uniform float uAspect, uRadius;
uniform int uCount;
uniform vec2 uPoint[64];
uniform vec3 uValue[64];
void main() {
  vec4 base = texture(uTarget, vUv);
  vec3 add = vec3(0.0);
  for (int i = 0; i < uCount; ++i) {
    vec2 p = vUv - uPoint[i];
    p.x *= uAspect;
    add += exp(-dot(p, p) / uRadius) * uValue[i];
  }
  o = vec4(base.rgb + add, 1.0);
}
)";

const char* kPointsVS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 2) in float aSize;
uniform vec2 uRes;
out vec4 vCol;
void main() {
  gl_Position = vec4(aPos / uRes * 2.0 - 1.0, 0.0, 1.0);
  gl_PointSize = aSize;
  vCol = aCol;
}
)";

const char* kPointsFS = R"(
in vec4 vCol; out vec4 o;
void main() {
  vec2 d = gl_PointCoord * 2.0 - 1.0;
  float r = dot(d, d);
  if (r > 1.0) discard;
  float a = exp(-r * 2.2) - 0.11;
  o = vec4(vCol.rgb * vCol.a * max(a, 0.0), 1.0);
}
)";

// Four-pointed glint with a hot core and thinner diagonal rays.
const char* kStarsFS = R"(
in vec4 vCol; out vec4 o;
void main() {
  vec2 d = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(d, d);
  if (r2 > 1.0) discard;
  float core = exp(-r2 * 60.0) * 3.0 + exp(-r2 * 12.0) * 0.6;
  float ray = exp(-abs(d.x) * 40.0) * exp(-d.y * d.y * 4.0) + exp(-abs(d.y) * 40.0) * exp(-d.x * d.x * 4.0);
  vec2 q = vec2(d.x + d.y, d.x - d.y) * 0.7071;
  float diag = exp(-abs(q.x) * 60.0) * exp(-q.y * q.y * 10.0) + exp(-abs(q.y) * 60.0) * exp(-q.x * q.x * 10.0);
  float edge = 1.0 - smoothstep(0.7, 1.0, sqrt(r2));
  float v = (core + ray * 1.2 + diag * 0.5) * edge;
  vec3 c = mix(vCol.rgb, vec3(1.0), clamp(core * 0.25, 0.0, 0.6));  // white-hot centre
  o = vec4(c * v * vCol.a, 1.0);
}
)";

const char* kGlowVS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in float aSide;
layout(location = 2) in vec3 aCol;
layout(location = 3) in vec2 aWidth;
uniform vec2 uRes;
out float vSide; out vec3 vCol; out vec2 vWidth;
void main() {
  gl_Position = vec4(aPos / uRes * 2.0 - 1.0, 0.0, 1.0);
  vSide = aSide; vCol = aCol; vWidth = aWidth;
}
)";

const char* kGlowFS = R"(
in float vSide; in vec3 vCol; in vec2 vWidth;
out vec4 o;
uniform float uGlow;
void main() {
  float d = abs(vSide) * vWidth.x;
  float hc = vWidth.y * 0.5;
  float core = 1.0 - smoothstep(hc, hc + 1.0, d);
  float gw = max(vWidth.x - hc, 0.001);
  float glow = exp(-max(d - hc, 0.0) / gw * 4.0) * uGlow;
  o = vec4(vCol * (core * 2.4 + glow) + vec3(core * 0.6) * dot(vCol, vec3(0.33)), 1.0);
}
)";

}  // namespace

void fxDrawGlowSegs(const Target& out, const std::vector<GlowSeg>& segs, float glow, bool maxBlend) {
  if (segs.empty()) return;
  std::vector<float>& v = g.glowVerts;
  v.clear();
  v.reserve(segs.size() * 6 * 8);
  for (const GlowSeg& s : segs) {
    Vec2 d = s.b - s.a;
    float l = length(d);
    if (l < 0.25f) continue;
    d = d * (1.f / l);
    Vec2 n(-d.y, d.x);
    float hw = s.halfW;
    Vec2 a = s.a - d * (hw * 0.5f), b = s.b + d * (hw * 0.5f);
    Vec2 p[4] = {a + n * hw, a - n * hw, b + n * hw, b - n * hw};
    float side[4] = {1, -1, 1, -1};
    int idx[6] = {0, 1, 2, 2, 1, 3};
    for (int k : idx) {
      float vv[8] = {p[k].x, p[k].y, side[k], s.col.x, s.col.y, s.col.z, hw, s.coreW};
      v.insert(v.end(), vv, vv + 8);
    }
  }
  if (v.empty()) return;
  out.bind();
  glEnable(GL_BLEND);
  if (maxBlend) {
    glBlendEquation(GL_MAX);
  } else {
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
  }
  g.glow.use();
  g.glow.set2f("uRes", float(out.w), float(out.h));
  g.glow.setf("uGlow", glow);
  glBindVertexArray(g.glowVao);
  glBindBuffer(GL_ARRAY_BUFFER, g.glowVbo);
  glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / 8));
  glBindVertexArray(0);
  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_BLEND);
}

bool fxSharedInit() {
  bool ok = true;
  ok &= g.copy.build("copy", kFullscreenVS, kCopyFS);
  ok &= g.stars.build("stars", kPointsVS, kStarsFS);
  ok &= g.glow.build("glowsegs", kGlowVS, kGlowFS);
  glGenVertexArrays(1, &g.glowVao);
  glGenBuffers(1, &g.glowVbo);
  glBindVertexArray(g.glowVao);
  glBindBuffer(GL_ARRAY_BUFFER, g.glowVbo);
  {
    const GLsizei st = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, st, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, st, (void*)(6 * sizeof(float)));
  }
  glBindVertexArray(0);
  ok &= g.fade.build("fade", kFullscreenVS, kFadeFS);
  ok &= g.splat.build("splat", kFullscreenVS, kSplatFS);
  ok &= g.points.build("points", kPointsVS, kPointsFS);

  glGenVertexArrays(1, &g.pointVao);
  glGenBuffers(1, &g.pointVbo);
  glBindVertexArray(g.pointVao);
  glBindBuffer(GL_ARRAY_BUFFER, g.pointVbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)(2 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)(6 * sizeof(float)));
  glBindVertexArray(0);
  return ok;
}

void fxCopy(const Target& out, GLuint srcTex, float gain) {
  out.bind();
  glDisable(GL_BLEND);
  g.copy.use();
  bindTex(0, srcTex);
  g.copy.seti("uSrc", 0);
  g.copy.setf("uGain", gain);
  drawFullscreen();
}

void fxAdd(const Target& out, GLuint srcTex, float gain) {
  out.bind();
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE);
  g.copy.use();
  bindTex(0, srcTex);
  g.copy.seti("uSrc", 0);
  g.copy.setf("uGain", gain);
  drawFullscreen();
  glDisable(GL_BLEND);
}

void fxFade(const Target& t, float mul) {
  if (mul >= 0.9999f) return;
  t.bind();
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ZERO, GL_CONSTANT_COLOR);  // dst = dst * mul
  glBlendColor(mul, mul, mul, mul);
  g.fade.use();
  drawFullscreen();
  glDisable(GL_BLEND);
}

void fxSplat(DoubleTarget& t, float aspect, const std::vector<Vec2>& pts, const std::vector<Vec3>& vals,
             float radius) {
  if (pts.empty()) return;
  int n = std::min<int>(int(pts.size()), kMaxSplats);
  float p[kMaxSplats * 2], v[kMaxSplats * 3];
  for (int i = 0; i < n; ++i) {
    p[i * 2] = pts[i].x;
    p[i * 2 + 1] = pts[i].y;
    v[i * 3] = vals[i].x;
    v[i * 3 + 1] = vals[i].y;
    v[i * 3 + 2] = vals[i].z;
  }
  t.write().bind();
  glDisable(GL_BLEND);
  g.splat.use();
  bindTex(0, t.read().tex);
  g.splat.seti("uTarget", 0);
  g.splat.setf("uAspect", aspect);
  g.splat.setf("uRadius", radius);
  g.splat.seti("uCount", n);
  g.splat.set2fv("uPoint", p, n);
  g.splat.set3fv("uValue", v, n);
  drawFullscreen();
  t.swap();
}

void fxDrawPoints(const Target& out, const std::vector<PointVertex>& pts, PointStyle style) {
  if (pts.empty()) return;
  out.bind();
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE);
  glEnable(GL_PROGRAM_POINT_SIZE);
  Program& prog = style == POINT_STAR ? g.stars : g.points;
  prog.use();
  prog.set2f("uRes", float(out.w), float(out.h));
  glBindVertexArray(g.pointVao);
  glBindBuffer(GL_ARRAY_BUFFER, g.pointVbo);
  glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(PointVertex), pts.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_POINTS, 0, GLsizei(pts.size()));
  glBindVertexArray(0);
  glDisable(GL_PROGRAM_POINT_SIZE);
  glDisable(GL_BLEND);
}
