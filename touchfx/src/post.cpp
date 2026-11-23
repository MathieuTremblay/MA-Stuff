#include "post.hpp"

#include <algorithm>

namespace {

const char* kDownFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uKnee; uniform int uFirst;
void main() {
  vec3 c = texture(uSrc, vUv + uTexel * vec2(-1.0, -1.0)).rgb
         + texture(uSrc, vUv + uTexel * vec2( 1.0, -1.0)).rgb
         + texture(uSrc, vUv + uTexel * vec2(-1.0,  1.0)).rgb
         + texture(uSrc, vUv + uTexel * vec2( 1.0,  1.0)).rgb;
  c *= 0.25;
  if (uFirst == 1) {
    c = min(c, vec3(16.0));                       // no single-pixel fireflies
    float l = max(c.r, max(c.g, c.b));
    c *= smoothstep(0.0, uKnee, l) * 0.8 + 0.2;   // bright parts glow most
  }
  o = vec4(c, 1.0);
}
)";

const char* kUpFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec2 uTexel; uniform float uWeight;
void main() {
  vec3 c = texture(uSrc, vUv).rgb * 4.0;
  c += (texture(uSrc, vUv + uTexel * vec2(-1, 0)).rgb + texture(uSrc, vUv + uTexel * vec2(1, 0)).rgb +
        texture(uSrc, vUv + uTexel * vec2(0, -1)).rgb + texture(uSrc, vUv + uTexel * vec2(0, 1)).rgb) * 2.0;
  c += texture(uSrc, vUv + uTexel * vec2(-1, -1)).rgb + texture(uSrc, vUv + uTexel * vec2(1, -1)).rgb +
       texture(uSrc, vUv + uTexel * vec2(-1, 1)).rgb + texture(uSrc, vUv + uTexel * vec2(1, 1)).rgb;
  o = vec4(c / 16.0 * uWeight, 1.0);
}
)";

const char* kFinalFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uScene; uniform sampler2D uBloom;
uniform float uBloomAmt, uSat, uMaster;
uniform int uAlpha, uPremult;
void main() {
  vec3 c = max(texture(uScene, vUv).rgb, vec3(0.0)) + texture(uBloom, vUv).rgb * uBloomAmt;
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = max(mix(vec3(l), c, uSat), vec3(0.0));
  c = tonemap(c) * uMaster;  // master after tone mapping: 50% looks like 50%
  if (uAlpha == 1) {
    float a = clamp(max(c.r, max(c.g, c.b)), 0.0, 1.0);
    if (uPremult == 0) c = a > 0.0005 ? c / a : vec3(0.0);
    o = vec4(c, a);
  } else {
    o = vec4(c, 1.0);
  }
}
)";

}  // namespace

bool Post::init() {
  bool ok = pDown.build("post.down", kFullscreenVS, kDownFS);
  ok &= pUp.build("post.up", kFullscreenVS, kUpFS);
  ok &= pFinal.build("post.final", kFullscreenVS, kFinalFS);
  return ok;
}

void Post::resize(int W, int H) {
  levels = 0;
  int w = W / 2, h = H / 2;
  for (int i = 0; i < kLevels && w >= 8 && h >= 8; ++i) {
    mips[i].create(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    ++levels;
    w /= 2;
    h /= 2;
  }
}

void Post::run(const Target& scene, const Target& out, const PostSettings& p, bool alpha, bool premultiplied,
               float master) {
  glDisable(GL_BLEND);
  bool bloom = p.bloom > 0.001f && levels > 0;
  if (bloom) {
    // Downsample chain.
    pDown.use();
    pDown.seti("uSrc", 0);
    pDown.setf("uKnee", 1.2f);
    const Target* src = &scene;
    for (int i = 0; i < levels; ++i) {
      mips[i].bind();
      bindTex(0, src->tex);
      pDown.set2f("uTexel", 0.5f / src->w, 0.5f / src->h);
      pDown.seti("uFirst", i == 0 ? 1 : 0);
      drawFullscreen();
      src = &mips[i];
    }
    // Upsample and accumulate: each level adds a blurred copy of the smaller one.
    pUp.use();
    pUp.seti("uSrc", 0);
    pUp.setf("uWeight", 0.35f + p.bloomSize * 0.75f);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = levels - 2; i >= 0; --i) {
      mips[i].bind();
      bindTex(0, mips[i + 1].tex);
      pUp.set2f("uTexel", 1.f / mips[i + 1].w, 1.f / mips[i + 1].h);
      drawFullscreen();
    }
    glDisable(GL_BLEND);
  }

  out.bind();
  pFinal.use();
  bindTex(0, scene.tex);
  bindTex(1, bloom ? mips[0].tex : scene.tex);
  pFinal.seti("uScene", 0);
  pFinal.seti("uBloom", 1);
  pFinal.setf("uBloomAmt", bloom ? p.bloom * 0.9f : 0.f);
  pFinal.setf("uSat", p.saturation * 2.f);
  pFinal.setf("uMaster", master);
  pFinal.seti("uAlpha", alpha ? 1 : 0);
  pFinal.seti("uPremult", premultiplied ? 1 : 0);
  drawFullscreen();
}

namespace {
Program g_present;
const char* kPresentFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec4 uRect; uniform int uMulAlpha;
void main() {
  vec4 c = texture(uSrc, mix(uRect.xy, uRect.zw, vUv));
  o = vec4(uMulAlpha == 1 ? c.rgb * c.a : c.rgb, 1.0);
}
)";
}  // namespace

bool presentInit() { return g_present.build("present", kFullscreenVS, kPresentFS); }

void presentTexture(GLuint tex, float u0, float v0, float u1, float v1, bool multiplyAlpha) {
  glDisable(GL_BLEND);
  g_present.use();
  bindTex(0, tex);
  g_present.seti("uSrc", 0);
  glUniform4f(g_present.loc("uRect"), u0, v0, u1, v1);
  g_present.seti("uMulAlpha", multiplyAlpha ? 1 : 0);
  drawFullscreen();
}
