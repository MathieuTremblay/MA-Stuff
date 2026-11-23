// VIDEO: the video input played with the fingers.
//   REVEAL  - the fingers paint the video into view (it fades back out over the trail time)
//   LIQUID  - the picture is pushed around like a liquid
//   RIPPLE  - water ripples bend the picture
//   KALEIDO - a kaleidoscope of the video around the first finger
//   PIXEL   - big pixels where the fingers go
//   WARP    - the picture swirls around each finger
// Without a video input, a gradient in the palette colours stands in, so the effect never goes blank.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

// r = value, clamped so a resting finger does not pile up forever
const char* kDecayFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform float uMul; uniform float uMax; uniform float uDiffuse; uniform vec2 uTexel;
void main() {
  vec4 c = texture(uSrc, vUv);
  if (uDiffuse > 0.0) {
    vec4 n = texture(uSrc, vUv + vec2(uTexel.x, 0.0)) + texture(uSrc, vUv - vec2(uTexel.x, 0.0)) +
             texture(uSrc, vUv + vec2(0.0, uTexel.y)) + texture(uSrc, vUv - vec2(0.0, uTexel.y));
    c = mix(c, n * 0.25, uDiffuse);
  }
  o = clamp(c * uMul, vec4(-uMax), vec4(uMax));
}
)";

const char* kWaveFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uDamp;
void main() {
  vec2 c = texture(uSrc, vUv).rg;
  float L = texture(uSrc, vUv - vec2(uTexel.x, 0.0)).r;
  float R = texture(uSrc, vUv + vec2(uTexel.x, 0.0)).r;
  float T = texture(uSrc, vUv + vec2(0.0, uTexel.y)).r;
  float B = texture(uSrc, vUv - vec2(0.0, uTexel.y)).r;
  float h = ((L + R + T + B) * 0.5 - c.g) * uDamp;
  o = vec4(clamp(h, -8.0, 8.0), c.r, 0.0, 1.0);
}
)";

const char* kVideoFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uVideo; uniform sampler2D uField;
uniform int uHasVideo, uMode, uCount, uSegs;
uniform vec2 uFit, uTexel, uRes, uPts[8];
uniform float uAspect, uLevel, uGain, uStrength, uSize, uGlow, uRot;
uniform vec3 uLow, uMid, uHigh;

vec3 vid(vec2 uv) {
  vec2 v = (uv - 0.5) * uFit + 0.5;
  if (v.x < 0.0 || v.y < 0.0 || v.x > 1.0 || v.y > 1.0) return vec3(0.0);
  if (uHasVideo == 0) {  // no input: a soft gradient in the palette colours
    float t = clamp(v.y * 0.7 + 0.3 * sin(v.x * 6.2832), 0.0, 1.0);
    return mix(mix(uLow, uMid, t * 2.0), uHigh, max(t * 2.0 - 1.0, 0.0)) * 0.6;
  }
  return texture(uVideo, v).rgb;
}

// soft halo around the fingers, in the palette colour
float halo(vec2 uv) {
  float g = 0.0;
  float s = 0.004 + uSize * uSize * 0.05;
  for (int i = 0; i < uCount; ++i) {
    vec2 d = uv - uPts[i];
    d.x *= uAspect;
    g += exp(-dot(d, d) / s);
  }
  return g;
}

void main() {
  vec2 uv = vUv;
  vec3 col;
  vec3 extra = vec3(0.0);
  if (uMode == 0) {  // REVEAL
    float m = clamp(texture(uField, uv).r, 0.0, 1.0);
    col = vid(uv) * smoothstep(0.0, 1.0, m);
    extra = uMid * (m * (1.0 - m) * 4.0) * uGlow * 1.2;
  } else if (uMode == 1) {  // LIQUID
    vec2 d = texture(uField, uv).rg;
    col = vid(uv - d * (0.02 + uStrength * 0.12));
    extra = uHigh * clamp(length(d) * 2.0, 0.0, 1.0) * uGlow * 0.5;
  } else if (uMode == 2) {  // RIPPLE
    float L = texture(uField, uv - vec2(uTexel.x, 0.0)).r;
    float R = texture(uField, uv + vec2(uTexel.x, 0.0)).r;
    float T = texture(uField, uv + vec2(0.0, uTexel.y)).r;
    float B = texture(uField, uv - vec2(0.0, uTexel.y)).r;
    vec2 g = vec2(R - L, T - B);
    col = vid(uv + g * (0.01 + uStrength * 0.08));
    vec3 n = normalize(vec3(-g * 6.0, 1.0));
    float spec = pow(max(dot(n, normalize(vec3(0.4, 0.5, 1.0))), 0.0), 40.0) * clamp(length(g) * 8.0, 0.0, 1.0);
    extra = uHigh * spec * uGlow * 2.0;
  } else if (uMode == 3) {  // KALEIDO
    vec2 c = uCount > 0 ? uPts[0] : vec2(0.5);
    vec2 p = (uv - c) * vec2(uAspect, 1.0);
    float r = length(p) * (0.4 + (1.0 - uSize) * 1.6);
    float a = atan(p.y, p.x) + uRot;
    float seg = 6.2831853 / float(uSegs);
    a = mod(a, seg);
    if (a > seg * 0.5) a = seg - a;
    vec2 q = vec2(cos(a), sin(a)) * r;
    col = vid(vec2(0.5) + q / vec2(uAspect, 1.0) * 0.9);
  } else if (uMode == 4) {  // PIXEL
    float m = clamp(texture(uField, uv).r, 0.0, 1.0);
    float cell = mix(1.0, 6.0 + uSize * 90.0, m * (0.3 + uStrength * 0.7));
    vec2 px = uv * uRes;
    vec2 q = (floor(px / cell) + 0.5) * cell / uRes;
    col = vid(cell > 1.5 ? q : uv);
  } else {  // WARP
    vec2 w = uv;
    float s = 0.01 + uSize * uSize * 0.12;
    for (int i = 0; i < uCount; ++i) {
      vec2 d = (w - uPts[i]) * vec2(uAspect, 1.0);
      float f = exp(-dot(d, d) / s);
      float ang = (uStrength * 2.0 - 0.3) * 3.0 * f;  // left of centre: slight pinch, right: strong swirl
      float cs = cos(ang), sn = sin(ang);
      d = vec2(d.x * cs - d.y * sn, d.x * sn + d.y * cs) * (1.0 - 0.25 * f * uStrength);
      w = uPts[i] + d / vec2(uAspect, 1.0);
    }
    col = vid(w);
  }
  if (uMode != 0) extra += uMid * halo(uv) * uGlow * 0.35;
  o = vec4((col * uLevel + extra) * uGain, 1.0);
}
)";

class Video : public Effect {
 public:
  bool init() override {
    return pDecay.build("video.decay", kFullscreenVS, kDecayFS) && pWave.build("video.wave", kFullscreenVS, kWaveFS) &&
           pDraw.build("video.draw", kFullscreenVS, kVideoFS);
  }

  void resize(int W, int H) override {
    aspect = float(W) / H;
    outW = W;
    outH = H;
    simH = 360;
    simW = int(std::lround(simH * aspect));
    field.create(simW, simH, GL_RG16F, GL_RG, GL_HALF_FLOAT, GL_LINEAR);
  }

  void clear() override { field.clear(); }

  void frame(const FxCtx& c, const Target& out) override {
    const VideoSettings& s = c.look->video;
    float dt = std::min(c.dt, 0.05f);
    if (s.mode != lastMode) {  // each mode uses the field differently
      field.clear();
      lastMode = s.mode;
    }
    const std::vector<Emitter>& em = c.live;
    glDisable(GL_BLEND);

    // ---- fields ----
    if (s.mode == VID_REVEAL || s.mode == VID_PIXEL) {
      // the mask fades over the trail time (0.3 s .. 12 s)
      float life = mixf(0.3f, 12.f, s.trail * s.trail);
      decay(std::exp(-dt * 3.f / life), 1.f, 0.f);
      splat(em, [&](const Emitter& e) {
        float k = (0.25f + std::min(e.speed, 2.f) * 0.4f) * (dt * 60.f) * mixf(0.3f, 1.5f, s.strength) * c.drive;
        return Vec3(k, 0, 0);
      }, s.size);
    } else if (s.mode == VID_LIQUID) {
      float life = mixf(0.4f, 6.f, s.trail);
      decay(std::exp(-dt * 2.f / life), 3.f, 0.35f);
      splat(em, [&](const Emitter& e) {
        Vec2 v = e.vel * (0.25f * (dt * 60.f) * c.drive);
        return Vec3(v.x / aspect, v.y, 0);
      }, s.size);
    } else if (s.mode == VID_RIPPLE) {
      bool beatHit = std::floor(c.beat) != std::floor(lastBeat);
      splat(em, [&](const Emitter& e) {
        float k = std::min(e.speed, 2.f) * 0.6f * (dt * 60.f) + 0.12f * std::sin(c.time * 9.f + e.touch) * (dt * 60.f);
        if (beatHit) k += 2.5f;
        if (c.onset) k += 2.f * c.pulse;
        return Vec3(k * mixf(0.05f, 0.6f, s.strength) * c.drive, 0, 0);
      }, s.size * 0.6f);
      stepAcc += dt * 120.f * std::clamp(c.speed, 0.25f, 3.f);
      int steps = std::min(8, int(stepAcc));
      stepAcc -= steps;
      float damp = 1.f - mixf(0.03f, 0.002f, s.trail);
      for (int i = 0; i < steps; ++i) {
        field.write().bind();
        pWave.use();
        bindTex(0, field.read().tex);
        pWave.seti("uSrc", 0);
        pWave.set2f("uTexel", 1.f / simW, 1.f / simH);
        pWave.setf("uDamp", damp);
        drawFullscreen();
        field.swap();
      }
    }
    lastBeat = c.beat;
    rot += dt * c.speed * 0.4f;

    // ---- draw ----
    out.bind();
    pDraw.use();
    bindTex(0, c.videoTex);
    bindTex(1, field.read().tex);
    pDraw.seti("uVideo", 0);
    pDraw.seti("uField", 1);
    pDraw.seti("uHasVideo", c.videoTex ? 1 : 0);
    pDraw.seti("uMode", s.mode);
    pDraw.seti("uSegs", std::max(2, s.segments));
    // fit the video to the output
    float va = c.videoW > 0 && c.videoH > 0 ? float(c.videoW) / c.videoH : aspect;
    float fx = 1, fy = 1;
    if (s.fit == VFIT_FILL) {
      if (va > aspect) fx = aspect / va;
      else fy = va / aspect;
    } else if (s.fit == VFIT_FIT) {
      if (va > aspect) fy = va / aspect;
      else fx = aspect / va;
    }
    pDraw.set2f("uFit", fx, fy);
    pDraw.set2f("uTexel", 1.f / simW, 1.f / simH);
    pDraw.set2f("uRes", float(outW), float(outH));
    float pts[16] = {};
    int n = 0;
    for (const Emitter& e : em) {
      if (n >= 8) break;
      pts[n * 2] = e.pos.x;
      pts[n * 2 + 1] = e.pos.y;
      ++n;
    }
    pDraw.set2fv("uPts", pts, 8);
    pDraw.seti("uCount", n);
    pDraw.setf("uAspect", aspect);
    // a lost input fades to black (hold, then fade, in VideoInput); no input at all shows the palette gradient
    pDraw.setf("uLevel", s.level * 1.25f * c.look->brightness * (c.videoTex ? c.videoLevel : 1.f));
    pDraw.setf("uGain", c.gain);
    pDraw.setf("uStrength", s.strength);
    pDraw.setf("uSize", s.size);
    pDraw.setf("uGlow", s.glow);
    pDraw.setf("uRot", rot);
    Vec3 lo = c.color(0.f), mi = c.color(0.5f), hi = c.color(1.f);
    pDraw.set3f("uLow", lo.x, lo.y, lo.z);
    pDraw.set3f("uMid", mi.x, mi.y, mi.z);
    pDraw.set3f("uHigh", hi.x, hi.y, hi.z);
    drawFullscreen();
  }

 private:
  void decay(float mul, float maxV, float diffuse) {
    field.write().bind();
    pDecay.use();
    bindTex(0, field.read().tex);
    pDecay.seti("uSrc", 0);
    pDecay.setf("uMul", mul);
    pDecay.setf("uMax", maxV);
    pDecay.setf("uDiffuse", diffuse);
    pDecay.set2f("uTexel", 1.f / simW, 1.f / simH);
    drawFullscreen();
    field.swap();
  }

  template <class F>
  void splat(const std::vector<Emitter>& em, F value, float size) {
    if (em.empty()) return;
    std::vector<Vec2> pts;
    std::vector<Vec3> vals;
    for (const Emitter& e : em) {
      if (int(pts.size()) >= kMaxSplats) break;
      pts.push_back(e.pos);
      vals.push_back(value(e));
    }
    fxSplat(field, aspect, pts, vals, mixf(0.0002f, 0.02f, size * size));
  }

  Program pDecay, pWave, pDraw;
  DoubleTarget field;
  int simW = 0, simH = 0, outW = 1, outH = 1, lastMode = -1;
  float aspect = 1, stepAcc = 0, rot = 0;
  double lastBeat = 0;
};

}  // namespace

std::unique_ptr<Effect> makeVideo() { return std::make_unique<Video>(); }
