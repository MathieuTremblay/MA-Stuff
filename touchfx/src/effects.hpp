#pragma once
#include <memory>
#include <vector>

#include "glutil.hpp"
#include "mathutil.hpp"
#include "state.hpp"

// One point where an effect is injected. Each finger produces one emitter per mirror copy.
struct Emitter {
  Vec2 pos;          // output UV, 0..1, y up
  Vec2 vel;          // velocity in "aspect space" (x scaled by aspect, y = 0..1) per second
  float speed = 0;   // |vel|
  int touch = 0;     // which finger (0..9)
  int mirror = 0;    // which mirror copy (0 = the real finger)
  Mat2 m;            // mirror transform (aspect space, around the canvas centre)
};

struct FxCtx {
  float dt = 0;       // real seconds since last frame
  float time = 0;     // seconds since start
  float speed = 1;    // BPM speed factor (1.0 at 120 BPM, x1)
  double beat = 0;    // beats elapsed (continuous)
  int W = 0, H = 0;
  float aspect = 1;
  const Look* look = nullptr;
  const Palette* pal = nullptr;
  std::vector<Emitter> live;  // emitters from fingers currently down
  std::vector<Emitter> held;  // last emitters, kept while fading out after release
  bool touching = false;
  float gain = 1;     // 1 while touching, falls to 0 while fading out
  float fadeMul = 1;  // per-frame attenuation while fading out (1 = not fading)
  // audio reactive
  bool audioOn = false;
  float level = 0, bass = 0, mid = 0, high = 0, pulse = 0;
  bool onset = false;
  float drive = 1;    // multiplies how much each effect emits (1 = no audio influence)
  const float* spectrum = nullptr;  // 32 log bands 0..1 when audio is on, else nullptr

  // Emitters to draw "shape" effects from: live ones, or the held ones while fading.
  const std::vector<Emitter>& shapeEmitters() const { return touching ? live : held; }
  Vec3 color(float t) const;
  // Picks a 0..1 position in the palette according to the colour mode.
  // gradient: what "gradient" means for this effect (age, beam index...), rnd: a random value.
  float colorT(const Emitter* e, float gradient, float rnd) const;
};

class Effect {
 public:
  virtual ~Effect() = default;
  virtual bool init() = 0;               // build shaders
  virtual void resize(int W, int H) = 0;  // (re)create buffers for the output size
  virtual void clear() = 0;               // remove everything on screen
  virtual void frame(const FxCtx& c, const Target& out) = 0;
};

std::unique_ptr<Effect> makeFluid();
std::unique_ptr<Effect> makeParticles();
std::unique_ptr<Effect> makeKaleido();
std::unique_ptr<Effect> makeLasers();
std::unique_ptr<Effect> makeStrobe();
std::unique_ptr<Effect> makeSparkles();
std::unique_ptr<Effect> makeRipples();
std::unique_ptr<Effect> makeRibbons();
std::unique_ptr<Effect> makeGeometry();
std::unique_ptr<Effect> makeTunnel();
std::unique_ptr<Effect> makeLightning();
std::unique_ptr<Effect> makePlexus();
std::unique_ptr<Effect> makeFireworks();
std::unique_ptr<Effect> makeStarfield();
std::unique_ptr<Effect> makeSpectrum();
std::unique_ptr<Effect> makeGrid();

std::vector<Mat2> mirrorMatrices(int mode);
Vec3 paletteAt(const Palette& p, float t);

// ---- shared GPU helpers ----
bool fxSharedInit();
// Copies src into out (HDR, no tonemapping) multiplied by gain.
void fxCopy(const Target& out, GLuint srcTex, float gain);
// Multiplies every pixel of t by mul (used for trails and fade-outs).
void fxFade(const Target& t, float mul);
// Adds gaussian blobs into a ping-pong target. aspect corrects the blob shape. Max kMaxSplats points.
void fxSplat(DoubleTarget& t, float aspect, const std::vector<Vec2>& pts, const std::vector<Vec3>& vals,
             float radius);

// Soft round points drawn additively (particles, sparkles).
struct PointVertex {
  float x, y;        // pixels, origin bottom-left
  float r, g, b, a;
  float size;        // diameter in pixels
};
enum PointStyle { POINT_SOFT, POINT_STAR };
void fxDrawPoints(const Target& out, const std::vector<PointVertex>& pts, PointStyle style = POINT_SOFT);

// Glowing line segments (pixels, origin bottom-left): a hot core plus a soft glow.
// maxBlend keeps the brightest value where lines overlap (no bright beads at joints).
struct GlowSeg {
  Vec2 a, b;
  Vec3 col;
  float halfW, coreW;  // total half width and core width, pixels
};
void fxDrawGlowSegs(const Target& out, const std::vector<GlowSeg>& segs, float glow, bool maxBlend);

constexpr int kMaxSplats = 64;   // also the max number of emitters (fingers x mirror copies)
