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

Vec3 FxCtx::color(float t) const { return paletteAt(*pal, t); }

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
  Program composite, fade, splat, points;
  GLuint pointVbo = 0, pointVao = 0;
};
Shared g;

const char* kCompositeFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform float uGain;
void main() { o = vec4(tonemap(texture(uSrc, vUv).rgb * uGain), 1.0); }
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
uniform vec2 uPoint[16];
uniform vec3 uValue[16];
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

}  // namespace

bool fxSharedInit() {
  bool ok = true;
  ok &= g.composite.build("composite", kFullscreenVS, kCompositeFS);
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

void fxComposite(const Target& out, GLuint srcTex, float gain) {
  out.bind();
  glDisable(GL_BLEND);
  g.composite.use();
  bindTex(0, srcTex);
  g.composite.seti("uSrc", 0);
  g.composite.setf("uGain", gain);
  drawFullscreen();
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

void fxDrawPoints(const Target& out, const std::vector<PointVertex>& pts) {
  if (pts.empty()) return;
  out.bind();
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE);
  glEnable(GL_PROGRAM_POINT_SIZE);
  g.points.use();
  g.points.set2f("uRes", float(out.w), float(out.h));
  glBindVertexArray(g.pointVao);
  glBindBuffer(GL_ARRAY_BUFFER, g.pointVbo);
  glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(PointVertex), pts.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_POINTS, 0, GLsizei(pts.size()));
  glBindVertexArray(0);
  glDisable(GL_PROGRAM_POINT_SIZE);
  glDisable(GL_BLEND);
}
