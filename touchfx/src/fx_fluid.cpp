// Fluid / smoke: a small GPU Navier-Stokes solver. Fingers push the fluid and inject coloured dye.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

const char* kCurlFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uVel; uniform vec2 uTexel;
void main() {
  float L = texture(uVel, vUv - vec2(uTexel.x, 0.0)).y;
  float R = texture(uVel, vUv + vec2(uTexel.x, 0.0)).y;
  float T = texture(uVel, vUv + vec2(0.0, uTexel.y)).x;
  float B = texture(uVel, vUv - vec2(0.0, uTexel.y)).x;
  o = vec4(0.5 * (R - L - T + B), 0.0, 0.0, 1.0);
}
)";

const char* kVorticityFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uVel; uniform sampler2D uCurl; uniform vec2 uTexel;
uniform float uCurlStr; uniform float uDt;
void main() {
  float L = texture(uCurl, vUv - vec2(uTexel.x, 0.0)).x;
  float R = texture(uCurl, vUv + vec2(uTexel.x, 0.0)).x;
  float T = texture(uCurl, vUv + vec2(0.0, uTexel.y)).x;
  float B = texture(uCurl, vUv - vec2(0.0, uTexel.y)).x;
  float C = texture(uCurl, vUv).x;
  vec2 force = 0.5 * vec2(abs(T) - abs(B), abs(R) - abs(L));
  force /= length(force) + 0.0001;
  force *= uCurlStr * C;
  force.y *= -1.0;
  vec2 v = texture(uVel, vUv).xy + force * uDt;
  o = vec4(clamp(v, -1000.0, 1000.0), 0.0, 1.0);
}
)";

const char* kDivergenceFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uVel; uniform vec2 uTexel;
void main() {
  float L = texture(uVel, vUv - vec2(uTexel.x, 0.0)).x;
  float R = texture(uVel, vUv + vec2(uTexel.x, 0.0)).x;
  float T = texture(uVel, vUv + vec2(0.0, uTexel.y)).y;
  float B = texture(uVel, vUv - vec2(0.0, uTexel.y)).y;
  vec2 C = texture(uVel, vUv).xy;
  if (vUv.x - uTexel.x < 0.0) L = -C.x;
  if (vUv.x + uTexel.x > 1.0) R = -C.x;
  if (vUv.y + uTexel.y > 1.0) T = -C.y;
  if (vUv.y - uTexel.y < 0.0) B = -C.y;
  o = vec4(0.5 * (R - L + T - B), 0.0, 0.0, 1.0);
}
)";

const char* kPressureFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uPressure; uniform sampler2D uDivergence; uniform vec2 uTexel;
void main() {
  float L = texture(uPressure, vUv - vec2(uTexel.x, 0.0)).x;
  float R = texture(uPressure, vUv + vec2(uTexel.x, 0.0)).x;
  float T = texture(uPressure, vUv + vec2(0.0, uTexel.y)).x;
  float B = texture(uPressure, vUv - vec2(0.0, uTexel.y)).x;
  float div = texture(uDivergence, vUv).x;
  o = vec4((L + R + B + T - div) * 0.25, 0.0, 0.0, 1.0);
}
)";

const char* kGradientFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uPressure; uniform sampler2D uVel; uniform vec2 uTexel;
void main() {
  float L = texture(uPressure, vUv - vec2(uTexel.x, 0.0)).x;
  float R = texture(uPressure, vUv + vec2(uTexel.x, 0.0)).x;
  float T = texture(uPressure, vUv + vec2(0.0, uTexel.y)).x;
  float B = texture(uPressure, vUv - vec2(0.0, uTexel.y)).x;
  vec2 v = texture(uVel, vUv).xy - vec2(R - L, T - B);
  o = vec4(v, 0.0, 1.0);
}
)";

const char* kAdvectFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uVel; uniform sampler2D uSrc; uniform vec2 uTexel;
uniform float uDt; uniform float uDiss;
void main() {
  vec2 coord = vUv - uDt * texture(uVel, vUv).xy * uTexel;
  o = texture(uSrc, coord) / (1.0 + uDiss * uDt);
}
)";

const char* kScaleFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform float uValue;
void main() { o = texture(uSrc, vUv) * uValue; }
)";

class Fluid : public Effect {
 public:
  bool init() override {
    bool ok = true;
    ok &= pCurl.build("fluid.curl", kFullscreenVS, kCurlFS);
    ok &= pVort.build("fluid.vorticity", kFullscreenVS, kVorticityFS);
    ok &= pDiv.build("fluid.divergence", kFullscreenVS, kDivergenceFS);
    ok &= pPress.build("fluid.pressure", kFullscreenVS, kPressureFS);
    ok &= pGrad.build("fluid.gradient", kFullscreenVS, kGradientFS);
    ok &= pAdvect.build("fluid.advect", kFullscreenVS, kAdvectFS);
    ok &= pScale.build("fluid.scale", kFullscreenVS, kScaleFS);
    return ok;
  }

  void resize(int W, int H) override {
    aspect = float(W) / float(H);
    simH = 160;
    simW = std::max(16, int(std::lround(simH * aspect)));
    dyeH = std::min(H, 720);
    dyeW = std::max(16, int(std::lround(dyeH * aspect)));
    vel.create(simW, simH, GL_RG16F, GL_RG, GL_HALF_FLOAT, GL_LINEAR);
    pressure.create(simW, simH, GL_R16F, GL_RED, GL_HALF_FLOAT, GL_NEAREST);
    divergence.create(simW, simH, GL_R16F, GL_RED, GL_HALF_FLOAT, GL_NEAREST);
    curl.create(simW, simH, GL_R16F, GL_RED, GL_HALF_FLOAT, GL_NEAREST);
    dye.create(dyeW, dyeH, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
  }

  void clear() override {
    vel.clear();
    pressure.clear();
    dye.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const FluidSettings& s = c.look->fluid;
    float realDt = std::min(c.dt, 1.f / 20.f);
    float dt = realDt * c.speed;
    float tx = 1.f / simW, ty = 1.f / simH;

    if (c.fadeMul < 1.f) {
      scale(dye, c.fadeMul);
      scale(vel, std::sqrt(c.fadeMul));
    }

    // ---- inject from fingers ----
    if (!c.live.empty()) {
      std::vector<Vec2> pts;
      std::vector<Vec3> vels, cols;
      float force = mixf(0.6f, 4.0f, s.force);
      float cyc = c.time * 0.12f * c.speed;  // gradient = colours cycle over time, offset per finger
      for (const Emitter& e : c.live) {
        if (int(pts.size()) >= kMaxSplats) break;
        pts.push_back(e.pos);
        // A slowly turning push keeps the smoke alive when the finger holds still.
        Vec2 idle = e.m * Vec2(std::cos(c.time * 2.3f + e.touch * 2.f), std::sin(c.time * 2.3f + e.touch * 2.f));
        Vec2 v = e.vel * (simH * force) + idle * (simH * 0.25f * force);
        vels.push_back(Vec3(v.x, v.y, 0));
        float g = fractf(cyc + e.touch * 0.37f);
        g = g < 0.5f ? g * 2.f : 2.f - g * 2.f;
        float ct = c.colorT(&e, g, hash01(uint32_t(c.time * 4.f) * 31u + e.touch));
        float amt = mixf(0.02f, 0.28f, s.intensity) * (0.6f + std::min(e.speed, 2.f)) * (realDt * 60.f);
        cols.push_back(c.color(ct) * amt);
      }
      float radius = mixf(0.0002f, 0.006f, s.size * s.size);
      fxSplat(vel, aspect, pts, vels, radius * 1.5f);
      fxSplat(dye, aspect, pts, cols, radius);
    }

    // ---- simulate ----
    glDisable(GL_BLEND);
    curl.bind();
    pCurl.use();
    bindTex(0, vel.read().tex);
    pCurl.seti("uVel", 0);
    pCurl.set2f("uTexel", tx, ty);
    drawFullscreen();

    vel.write().bind();
    pVort.use();
    bindTex(0, vel.read().tex);
    bindTex(1, curl.tex);
    pVort.seti("uVel", 0);
    pVort.seti("uCurl", 1);
    pVort.set2f("uTexel", tx, ty);
    pVort.setf("uCurlStr", mixf(0.f, 45.f, s.swirl));
    pVort.setf("uDt", dt);
    drawFullscreen();
    vel.swap();

    divergence.bind();
    pDiv.use();
    bindTex(0, vel.read().tex);
    pDiv.seti("uVel", 0);
    pDiv.set2f("uTexel", tx, ty);
    drawFullscreen();

    scale(pressure, 0.8f);
    pPress.use();
    pPress.seti("uPressure", 0);
    pPress.seti("uDivergence", 1);
    pPress.set2f("uTexel", tx, ty);
    bindTex(1, divergence.tex);
    for (int i = 0; i < 20; ++i) {
      pressure.write().bind();
      bindTex(0, pressure.read().tex);
      drawFullscreen();
      pressure.swap();
    }

    vel.write().bind();
    pGrad.use();
    bindTex(0, pressure.read().tex);
    bindTex(1, vel.read().tex);
    pGrad.seti("uPressure", 0);
    pGrad.seti("uVel", 1);
    pGrad.set2f("uTexel", tx, ty);
    drawFullscreen();
    vel.swap();

    advect(vel, vel.read().tex, dt, 0.25f, tx, ty);
    advect(dye, vel.read().tex, dt, mixf(2.5f, 0.04f, s.trail), tx, ty);

    // ---- display ----
    fxCopy(out, dye.read().tex, c.look->brightness * 1.05f);
  }

 private:
  void scale(DoubleTarget& t, float v) {
    t.write().bind();
    glDisable(GL_BLEND);
    pScale.use();
    bindTex(0, t.read().tex);
    pScale.seti("uSrc", 0);
    pScale.setf("uValue", v);
    drawFullscreen();
    t.swap();
  }

  void advect(DoubleTarget& t, GLuint velTex, float dt, float diss, float tx, float ty) {
    t.write().bind();
    pAdvect.use();
    bindTex(0, velTex);
    bindTex(1, t.read().tex);
    pAdvect.seti("uVel", 0);
    pAdvect.seti("uSrc", 1);
    pAdvect.set2f("uTexel", tx, ty);
    pAdvect.setf("uDt", dt);
    pAdvect.setf("uDiss", diss);
    drawFullscreen();
    t.swap();
  }

  Program pCurl, pVort, pDiv, pPress, pGrad, pAdvect, pScale;
  DoubleTarget vel, pressure, dye;
  Target divergence, curl;
  int simW = 0, simH = 0, dyeW = 0, dyeH = 0;
  float aspect = 1;
};

}  // namespace

std::unique_ptr<Effect> makeFluid() { return std::make_unique<Fluid>(); }
