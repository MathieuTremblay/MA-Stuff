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
uniform sampler2D uVid; uniform int uTint; uniform vec2 uVFit; uniform float uTintAmt;
uniform int uOriginal;  // video layer: pass the picture through untouched (no glow, saturation or roll-off)
void main() {
  if (uOriginal == 1) {
    vec4 s = texture(uScene, vUv);
    vec3 c = clamp(s.rgb, 0.0, 1.0) * uMaster;
    if (uAlpha == 1) {
      // opaque where the video is (its coverage is in the scene's alpha), brightness for anything else
      float a = clamp(max(s.a * uMaster, max(c.r, max(c.g, c.b))), 0.0, 1.0);
      if (uPremult == 0) c = a > 0.0005 ? c / a : vec3(0.0);
      o = vec4(c, a);
    } else {
      o = vec4(c, 1.0);
    }
    return;
  }
  vec3 c = max(texture(uScene, vUv).rgb, vec3(0.0)) + texture(uBloom, vUv).rgb * uBloomAmt;
  if (uTint == 1) {  // video colours: every pixel takes the colour of the video under it
    vec2 v = (vUv - 0.5) * uVFit + 0.5;
    vec3 vc = texture(uVid, clamp(v, 0.0, 1.0)).rgb;
    float vb = max(vc.r, max(vc.g, vc.b));
    vec3 hue = vc / max(vb, 0.04);
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c, l * hue * (0.35 + 0.65 * vb) * 1.7, uTintAmt);
  }
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
               float master, const VideoTint* tint, bool original) {
  glDisable(GL_BLEND);
  bool bloom = !original && p.bloom > 0.001f && levels > 0;
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
  pFinal.seti("uOriginal", original ? 1 : 0);
  bool t = tint && tint->tex && tint->amount > 0.001f;
  pFinal.seti("uTint", t ? 1 : 0);
  if (t) {
    bindTex(2, tint->tex);
    pFinal.seti("uVid", 2);
    pFinal.set2f("uVFit", tint->fitX, tint->fitY);
    pFinal.setf("uTintAmt", tint->amount);
  }
  drawFullscreen();
}

// ---- layers: front (played) + background, both already through Post (premultiplied, display values) ----
namespace {
Program g_comp;
const char* kCompositeFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uFront; uniform sampler2D uBack;
uniform int uBlend, uAlpha, uPremult; uniform float uMaster;
void main() {
  vec4 f = texture(uFront, vUv), b = texture(uBack, vUv);
  vec3 c;
  float cov;  // how much of the pixel the layers cover (a video / clip layer is solid where its picture is)
  if (uBlend == 0) {  // both glow together
    c = vec3(1.0) - (vec3(1.0) - f.rgb) * (vec3(1.0) - b.rgb);
    cov = 1.0 - (1.0 - f.a) * (1.0 - b.a);
  } else if (uBlend == 1) {  // front covers the background
    c = f.rgb + b.rgb * (1.0 - f.a);
    cov = f.a + b.a * (1.0 - f.a);
  } else {  // background shows through the front
    float k = clamp(f.a * 1.5, 0.0, 1.0);
    c = b.rgb * k;
    cov = b.a * k;
  }
  c *= uMaster;
  if (uAlpha == 1) {
    float a = clamp(max(cov * uMaster, max(c.r, max(c.g, c.b))), 0.0, 1.0);
    if (uPremult == 0) c = a > 0.0005 ? c / a : vec3(0.0);
    o = vec4(c, a);
  } else {
    o = vec4(c, 1.0);
  }
}
)";
}  // namespace

bool compositeInit() { return g_comp.build("composite", kFullscreenVS, kCompositeFS); }

void compositeLayers(const Target& front, const Target& back, const Target& out, int blend, bool alpha,
                     bool premultiplied, float master) {
  glDisable(GL_BLEND);
  out.bind();
  g_comp.use();
  bindTex(0, front.tex);
  bindTex(1, back.tex);
  g_comp.seti("uFront", 0);
  g_comp.seti("uBack", 1);
  g_comp.seti("uBlend", blend);
  g_comp.seti("uAlpha", alpha ? 1 : 0);
  g_comp.seti("uPremult", premultiplied ? 1 : 0);
  g_comp.setf("uMaster", master);
  drawFullscreen();
}

namespace {
Program g_present;
const char* kPresentFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uSrc; uniform vec4 uRect; uniform int uMulAlpha; uniform int uSharp; uniform vec2 uSrcSize;
// Catmull-Rom bicubic (16 texels) for a sharp upscale, e.g. 1080p to a 4K screen. Clamped to the 4 nearest
// texels so edges do not ring (no halos around lasers / LED tiles).
vec4 bicubic(vec2 uv) {
  vec2 p = uv * uSrcSize - 0.5;
  vec2 f = fract(p);
  vec2 i0 = floor(p) - 1.0;
  vec2 f2 = f * f, f3 = f2 * f;
  vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
  vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
  vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
  vec2 w3 = 0.5 * f3 - 0.5 * f2;
  float wxs[4] = float[4](w0.x, w1.x, w2.x, w3.x);
  float wys[4] = float[4](w0.y, w1.y, w2.y, w3.y);
  vec4 sum = vec4(0.0), lo = vec4(1e9), hi = vec4(-1e9);
  ivec2 maxc = ivec2(uSrcSize) - 1;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) {
      ivec2 c = clamp(ivec2(i0) + ivec2(x, y), ivec2(0), maxc);
      vec4 t = texelFetch(uSrc, c, 0);
      sum += t * (wxs[x] * wys[y]);
      if ((x == 1 || x == 2) && (y == 1 || y == 2)) { lo = min(lo, t); hi = max(hi, t); }
    }
  return clamp(sum, lo, hi);
}
void main() {
  vec2 uv = mix(uRect.xy, uRect.zw, vUv);
  vec4 c = uSharp == 1 ? bicubic(uv) : texture(uSrc, uv);
  o = vec4(uMulAlpha == 1 ? c.rgb * c.a : c.rgb, 1.0);
}
)";
}  // namespace

bool presentInit() { return g_present.build("present", kFullscreenVS, kPresentFS); }

void presentTexture(GLuint tex, float u0, float v0, float u1, float v1, bool multiplyAlpha, int sharpW, int sharpH) {
  glDisable(GL_BLEND);
  g_present.use();
  bindTex(0, tex);
  g_present.seti("uSrc", 0);
  glUniform4f(g_present.loc("uRect"), u0, v0, u1, v1);
  g_present.seti("uMulAlpha", multiplyAlpha ? 1 : 0);
  g_present.seti("uSharp", sharpW > 0 && sharpH > 0 ? 1 : 0);
  g_present.set2f("uSrcSize", float(std::max(1, sharpW)), float(std::max(1, sharpH)));
  drawFullscreen();
}
