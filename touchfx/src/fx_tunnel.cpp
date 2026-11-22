// Tunnel: a full-screen tunnel of rings (round or polygon) rushing towards the viewer. The vanishing point
// follows the fingers; the speed follows the BPM.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

const char* kTunnelFS = R"(
in vec2 vUv; out vec4 o;
uniform vec2 uCenter; uniform float uAspect, uZ, uDensity, uTwist, uWidth, uDepth, uSides, uGain, uPulse;
uniform vec3 uLow, uMid, uHigh; uniform int uGradient;
vec3 pal(float t) {
  t = fract(t);
  float k = t < 0.5 ? t * 2.0 : 2.0 - t * 2.0;  // low -> high -> low, loops seamlessly
  return k < 0.5 ? mix(uLow, uMid, k * 2.0) : mix(uMid, uHigh, k * 2.0 - 1.0);
}
void main() {
  vec2 d = vUv - uCenter;
  d.x *= uAspect;
  float r = length(d);
  float a = atan(d.y, d.x);
  if (uSides >= 3.0) {  // polygon "radius": distance to a regular polygon instead of a circle
    float seg = 6.2831853 / uSides;
    float aa = mod(a + uZ * uTwist, seg) - seg * 0.5;
    r = r * cos(aa) / cos(seg * 0.5);
  }
  float z = 1.0 / max(r, 0.002);           // depth: far away near the vanishing point
  float rings = z * uDensity + uZ;
  float dist = min(fract(rings), 1.0 - fract(rings));      // 0 on a ring line
  float aaw = max(fwidth(rings), 1e-5);
  float distPx = dist / aaw;                                // distance to the ring line in pixels
  float wPx = uWidth * (0.6 + 1.4 * clamp(r, 0.0, 1.0));    // a bit thicker up close, never a flat band
  float line = 1.0 - smoothstep(wPx, wPx + 1.2, distPx);
  line *= clamp(1.0 - aaw * 1.5, 0.0, 1.0);                // fade rings too dense to draw (no shimmer)
  float fogNear = smoothstep(0.0, 0.18, r);                // dark core
  float edge = mix(1.0, exp(-r * 1.5), uDepth);            // optional fade towards the screen edges
  vec3 col = uGradient == 1 ? pal(floor(rings) * 0.11) : uMid;
  o = vec4(col * line * fogNear * edge * uGain * (1.0 + uPulse * 0.6), 1.0);
}
)";

class Tunnel : public Effect {
 public:
  bool init() override { return prog.build("tunnel", kFullscreenVS, kTunnelFS); }
  void resize(int W, int H) override { aspect = float(W) / H; }
  void clear() override { center = Vec2(0.5f, 0.5f); }

  void frame(const FxCtx& c, const Target& out) override {
    const TunnelSettings& s = c.look->tunnel;
    float dt = std::min(c.dt, 0.05f);
    out.clear(0, 0, 0, 1);
    const std::vector<Emitter>& em = c.shapeEmitters();
    if (em.empty() || c.gain < 0.001f) return;
    // vanishing point = the real fingers (mirror copies would cancel out), smoothed
    Vec2 target(0, 0);
    int n = 0;
    for (const Emitter& e : em)
      if (e.mirror == 0) {
        target += e.pos;
        ++n;
      }
    if (n) target = target * (1.f / n);
    center = center + (target - center) * std::min(1.f, dt * 6.f);
    z += dt * c.speed * mixf(0.2f, 4.f, s.speed) * (0.7f + 0.6f * std::min(c.drive, 1.5f));
    z = std::fmod(z, 1000.f);

    out.bind();
    prog.use();
    prog.set2f("uCenter", center.x, center.y);
    prog.setf("uAspect", aspect);
    prog.setf("uZ", z);
    prog.setf("uDensity", mixf(0.08f, 0.6f, s.density));
    prog.setf("uTwist", (s.twist - 0.5f) * 0.6f);
    prog.setf("uWidth", mixf(1.f, 14.f, s.width) * (c.H / 1080.f));
    prog.setf("uDepth", s.depth);
    prog.setf("uSides", float(s.sides));
    prog.setf("uGain", c.gain * c.look->brightness * 1.4f);
    prog.setf("uPulse", c.audioOn ? c.pulse : std::pow(1.f - fractf(float(c.beat)), 6.f) * 0.5f);
    Vec3 lo = c.color(0), mi = c.color(c.colorT(&em[0], 0.5f, 0.5f)), hi = c.color(1);
    prog.set3f("uLow", lo.x, lo.y, lo.z);
    prog.set3f("uMid", mi.x, mi.y, mi.z);
    prog.set3f("uHigh", hi.x, hi.y, hi.z);
    prog.seti("uGradient", c.look->colorMode == COLOR_GRADIENT || c.look->colorMode == COLOR_RANDOM ? 1 : 0);
    drawFullscreen();
  }

 private:
  Program prog;
  float aspect = 1, z = 0;
  Vec2 center{0.5f, 0.5f};
};

}  // namespace

std::unique_ptr<Effect> makeTunnel() { return std::make_unique<Tunnel>(); }
