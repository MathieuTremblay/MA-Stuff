// Kaleidoscope: fingers paint into a swirling feedback buffer that is folded into mirrored segments.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

// Paint buffer covers a square world region [-D, D]^2 around the canvas centre (D = half diagonal),
// so every on-screen pixel maps inside it whatever the rotation.
const char* kFeedbackFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc;
uniform float uTime, uWarp, uZoom, uDecay, uDt, uD;
void main() {
  vec2 q = (vUv - 0.5) * 2.0 * uD;            // world coords
  q *= 1.0 - uZoom * uDt;                      // zoom in/out (content flows out from / into centre)
  vec2 n = vec2(vnoise(q * 3.0 + vec2(uTime * 0.31, 0.0)),
                vnoise(q * 3.0 + vec2(5.2, uTime * 0.27 + 1.3))) - 0.5;
  q += n * uWarp * uDt;
  vec2 uv = q / (2.0 * uD) + 0.5;
  o = vec4(texture(uSrc, uv).rgb * uDecay, 1.0);
}
)";

const char* kDisplayFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc;
uniform float uAspect, uSeg, uRot, uGain, uD;
void main() {
  vec2 q = vUv - 0.5;
  q.x *= uAspect;
  float r = length(q);
  float a = atan(q.y, q.x) - uRot;
  a = mod(a, uSeg);
  if (a > uSeg * 0.5) a = uSeg - a;
  vec2 p = r * vec2(cos(a), sin(a));
  vec3 c = texture(uSrc, p / (2.0 * uD) + 0.5).rgb;
  // soft vignette at the very centre hides the fold seam
  c *= smoothstep(0.0, 0.02, r) * 0.15 + 0.85;
  o = vec4(c * uGain, 1.0);
}
)";

class Kaleido : public Effect {
 public:
  bool init() override {
    bool ok = pFeedback.build("kaleido.feedback", kFullscreenVS, kFeedbackFS);
    ok &= pDisplay.build("kaleido.display", kFullscreenVS, kDisplayFS);
    return ok;
  }

  void resize(int W, int H) override {
    aspect = float(W) / H;
    D = 0.5f * std::sqrt(aspect * aspect + 1.f);
    int res = std::min(1024, std::max(256, int(H * 0.8f)));
    paint.create(res, res, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
  }

  void clear() override { paint.clear(); }

  void frame(const FxCtx& c, const Target& out) override {
    const KaleidoSettings& s = c.look->kaleido;
    float dt = std::min(c.dt, 0.05f);
    float sdt = dt * c.speed;
    float seg = kTau / std::max(2, s.segments);
    rot += sdt * (s.rotation - 0.5f) * 2.f * 1.2f;
    rot = std::fmod(rot, kTau * 64.f);

    // ---- feedback (swirl, zoom, decay) ----
    paint.write().bind();
    glDisable(GL_BLEND);
    pFeedback.use();
    bindTex(0, paint.read().tex);
    pFeedback.seti("uSrc", 0);
    pFeedback.setf("uTime", c.time * c.speed);
    pFeedback.setf("uWarp", s.warp * 0.6f * c.speed);
    pFeedback.setf("uZoom", (s.zoom - 0.5f) * 2.f * 0.6f * c.speed);
    pFeedback.setf("uDecay", std::exp(-dt * mixf(5.f, 0.12f, s.trail)) * c.fadeMul);
    pFeedback.setf("uDt", dt);
    pFeedback.setf("uD", D);
    drawFullscreen();
    paint.swap();

    // ---- brush: paint at the folded position of every emitter ----
    if (!c.live.empty()) {
      std::vector<Vec2> pts;
      std::vector<Vec3> cols;
      float t = 0.5f - 0.5f * std::cos(c.time * 0.5f * c.speed);
      for (const Emitter& e : c.live) {
        if (int(pts.size()) >= kMaxSplats) break;
        Vec2 q((e.pos.x - 0.5f) * aspect, e.pos.y - 0.5f);
        float r = length(q);
        float a = std::atan2(q.y, q.x) - rot;
        a = a - seg * std::floor(a / seg);
        if (a > seg * 0.5f) a = seg - a;
        Vec2 p(r * std::cos(a), r * std::sin(a));
        pts.push_back(Vec2(p.x / (2.f * D) + 0.5f, p.y / (2.f * D) + 0.5f));
        float ct = c.colorT(&e, t, hash01(uint32_t(c.beat * 2.0) + e.touch * 7u));
        float amt = 0.18f * (0.5f + std::min(e.speed, 1.5f)) * (dt * 60.f) * c.drive;
        cols.push_back(c.color(ct) * amt);
      }
      float radius = mixf(0.00004f, 0.0025f, s.brush * s.brush) / (4.f * D * D);
      fxSplat(paint, 1.f, pts, cols, radius);
    }

    // ---- fold onto the output ----
    out.bind();
    glDisable(GL_BLEND);
    pDisplay.use();
    bindTex(0, paint.read().tex);
    pDisplay.seti("uSrc", 0);
    pDisplay.setf("uAspect", aspect);
    pDisplay.setf("uSeg", seg);
    pDisplay.setf("uRot", rot);
    pDisplay.setf("uGain", c.look->brightness * 0.95f);
    pDisplay.setf("uD", D);
    drawFullscreen();
  }

 private:
  Program pFeedback, pDisplay;
  DoubleTarget paint;
  float aspect = 1, D = 1, rot = 0;
};

}  // namespace

std::unique_ptr<Effect> makeKaleido() { return std::make_unique<Kaleido>(); }
