// Ripples: a water surface (wave equation on the GPU). Fingers drop ripples that spread, bounce off the edges
// and interfere; the wave slopes catch the light in the palette colours.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

// r = height now, g = height one step ago
const char* kStepFS = R"(
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

const char* kShadeFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uTexel;
uniform vec3 uLow, uMid, uHigh; uniform float uShine, uGain;
void main() {
  float h = texture(uSrc, vUv).r;
  float L = texture(uSrc, vUv - vec2(uTexel.x, 0.0)).r;
  float R = texture(uSrc, vUv + vec2(uTexel.x, 0.0)).r;
  float T = texture(uSrc, vUv + vec2(0.0, uTexel.y)).r;
  float B = texture(uSrc, vUv - vec2(0.0, uTexel.y)).r;
  vec2 grad = vec2(R - L, T - B);
  float slope = length(grad);
  vec3 n = normalize(vec3(-grad * 6.0, 1.0));
  float spec = pow(max(dot(n, normalize(vec3(0.4, 0.5, 1.0))), 0.0), 40.0);
  float body = clamp(slope * 3.0, 0.0, 1.0);
  vec3 col = mix(uLow, uMid, clamp(abs(h) * 1.5, 0.0, 1.0)) * body * 1.6;
  col += uHigh * (spec * uShine * 2.5 * clamp(slope * 8.0, 0.0, 1.0));
  o = vec4(col * uGain, 1.0);
}
)";

class Ripples : public Effect {
 public:
  bool init() override {
    return pStep.build("ripples.step", kFullscreenVS, kStepFS) && pShade.build("ripples.shade", kFullscreenVS, kShadeFS);
  }

  void resize(int W, int H) override {
    aspect = float(W) / H;
    simH = 360;
    simW = int(std::lround(simH * aspect));
    field.create(simW, simH, GL_RG16F, GL_RG, GL_HALF_FLOAT, GL_LINEAR);
  }

  void clear() override { field.clear(); }

  void frame(const FxCtx& c, const Target& out) override {
    const RippleSettings& s = c.look->ripples;
    float dt = std::min(c.dt, 0.05f);

    if (c.fadeMul < 1.f) scaleField(c.fadeMul);

    // ---- drops ----
    double beatPos = c.beat;
    bool beatHit = s.beatDrops && std::floor(beatPos) != std::floor(lastBeat);
    lastBeat = beatPos;
    if (!c.live.empty()) {
      std::vector<Vec2> pts;
      std::vector<Vec3> vals;
      float amt = mixf(0.05f, 0.6f, s.strength) * c.drive;
      for (const Emitter& e : c.live) {
        if (int(pts.size()) >= kMaxSplats) break;
        // moving fingers leave a wake, a resting finger pulses, and every beat drops a big one
        float k = std::min(e.speed, 2.f) * 0.6f * (dt * 60.f);
        k += 0.15f * std::sin(c.time * 9.f + e.touch) * (dt * 60.f);
        if (beatHit) k += 3.f;
        if (c.onset) k += 2.f * c.pulse;
        pts.push_back(e.pos);
        vals.push_back(Vec3(k * amt, 0, 0));
      }
      float radius = mixf(0.00003f, 0.0012f, s.size * s.size);
      fxSplat(field, aspect, pts, vals, radius);
    }

    // ---- simulate: 2 steps per frame at 120 BPM, more when faster ----
    stepAcc += dt * 120.f * std::clamp(c.speed, 0.25f, 3.f);
    int steps = std::min(8, int(stepAcc));
    stepAcc -= steps;
    float damp = 1.f - mixf(0.03f, 0.002f, s.decay);
    glDisable(GL_BLEND);
    for (int i = 0; i < steps; ++i) {
      field.write().bind();
      pStep.use();
      bindTex(0, field.read().tex);
      pStep.seti("uSrc", 0);
      pStep.set2f("uTexel", 1.f / simW, 1.f / simH);
      pStep.setf("uDamp", damp);
      drawFullscreen();
      field.swap();
    }

    // ---- shade ----
    out.bind();
    pShade.use();
    bindTex(0, field.read().tex);
    pShade.seti("uSrc", 0);
    pShade.set2f("uTexel", 1.f / simW, 1.f / simH);
    Vec3 lo = c.color(0.f), mi = c.color(0.5f), hi = c.color(1.f);
    pShade.set3f("uLow", lo.x, lo.y, lo.z);
    pShade.set3f("uMid", mi.x, mi.y, mi.z);
    pShade.set3f("uHigh", hi.x, hi.y, hi.z);
    pShade.setf("uShine", s.shine);
    pShade.setf("uGain", c.look->brightness);
    drawFullscreen();
  }

 private:
  void scaleField(float m) {
    // fade out: multiply both height layers
    fxFade(field.read(), m);
  }

  Program pStep, pShade;
  DoubleTarget field;
  int simW = 0, simH = 0;
  float aspect = 1, stepAcc = 0;
  double lastBeat = 0;
};

}  // namespace

std::unique_ptr<Effect> makeRipples() { return std::make_unique<Ripples>(); }
