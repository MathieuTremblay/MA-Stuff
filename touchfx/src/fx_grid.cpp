// LED grid: the canvas as a coarse grid of LED tiles. Tiles light up under the fingers and fade (trail),
// send rings out on the beat, or sparkle randomly around the fingers.
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

const char* kGridFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uTiles; uniform vec2 uGrid; uniform float uGap, uRound, uGain;
void main() {
  vec2 g = vUv * uGrid;
  vec2 cell = floor(g);
  vec2 f = fract(g) - 0.5;
  vec3 c = texture(uTiles, (cell + 0.5) / uGrid).rgb;
  // rounded square with a gap around it
  float hs = 0.5 - uGap * 0.5;
  float r = uRound * hs;
  vec2 q = abs(f) - vec2(hs - r);
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
  float px = fwidth(g.x);
  float m = 1.0 - smoothstep(-px, px, d);
  o = vec4(c * m * uGain, 1.0);
}
)";

class Grid : public Effect {
 public:
  bool init() override {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return prog.build("grid", kFullscreenVS, kGridFS);
  }
  void resize(int W, int H) override { aspect = float(W) / H; }
  void clear() override {
    std::fill(level.begin(), level.end(), 0.f);
    rings.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const GridSettings& s = c.look->grid;
    float dt = std::min(c.dt, 0.05f);
    rows = int(mixf(48.f, 6.f, s.tile));
    cols = std::max(1, int(std::lround(rows * aspect)));
    size_t n = size_t(rows) * cols;
    if (level.size() != n) {
      level.assign(n, 0.f);
      tint.assign(n, 0.f);
    }

    // ---- decay ----
    float decay = std::exp(-dt * mixf(12.f, 0.6f, s.decay));
    for (float& v : level) v *= decay * c.fadeMul;

    // ---- light tiles ----
    float radius = mixf(0.4f, 5.f, s.radius);  // in tiles
    auto lightAt = [&](float cx, float cy, float rad, float amount, float t) {
      int x0 = std::max(0, int(cx - rad - 1)), x1 = std::min(cols - 1, int(cx + rad + 1));
      int y0 = std::max(0, int(cy - rad - 1)), y1 = std::min(rows - 1, int(cy + rad + 1));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
          if (d > rad) continue;
          size_t i = size_t(y) * cols + x;
          float k = amount * (1.f - d / (rad + 0.001f) * 0.5f);
          if (k > level[i]) {
            level[i] = k;
            tint[i] = t;
          }
        }
    };
    double cycles = c.beat * 2.0;
    bool tick = std::floor(cycles) != std::floor(lastCycles) || (c.audioOn && c.onset);
    lastCycles = cycles;
    for (const Emitter& e : c.live) {
      float cx = e.pos.x * cols, cy = e.pos.y * rows;
      float t = c.colorT(&e, 0.f, hash01(uint32_t(e.touch) * 13u + uint32_t(c.beat)));
      switch (s.mode) {
        case GRID_TRAIL: lightAt(cx, cy, radius, std::min(1.5f, c.drive), t); break;
        case GRID_RINGS:
          lightAt(cx, cy, 0.8f, 1.f, t);
          if (tick && rings.size() < 64) rings.push_back({cx, cy, 0.f, t});
          break;
        default:  // sparkle
          for (int k = 0; k < 3; ++k) {
            float a = rng.range(0, kTau), r = rng.next() * radius * 2.f;
            lightAt(cx + std::cos(a) * r, cy + std::sin(a) * r, 0.6f, rng.range(0.5f, 1.2f) * c.drive, rng.next());
          }
          break;
      }
    }
    // expanding rings (RINGS mode)
    for (Ring& r : rings) {
      r.r += dt * c.speed * 14.f;
      for (int k = 0; k < 96; ++k) {
        float a = kTau * k / 96;
        lightAt(r.x + std::cos(a) * r.r, r.y + std::sin(a) * r.r, 0.7f, std::max(0.f, 1.f - r.r / 40.f), r.t);
      }
    }
    rings.erase(std::remove_if(rings.begin(), rings.end(), [](const Ring& r) { return r.r > 40.f; }), rings.end());

    // ---- upload tile colours and draw ----
    pixels.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
      float v = std::min(level[i], 1.5f);
      float t = c.look->colorMode == COLOR_GRADIENT ? 1.f - std::min(1.f, level[i]) : tint[i];
      Vec3 col = c.color(t) * v;
      auto to8 = [](float x) { return uint8_t(std::clamp(x / 1.5f, 0.f, 1.f) * 255.f); };
      pixels[i * 4] = to8(col.x);
      pixels[i * 4 + 1] = to8(col.y);
      pixels[i * 4 + 2] = to8(col.z);
      pixels[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    out.bind();
    glDisable(GL_BLEND);
    prog.use();
    bindTex(0, tex);
    prog.seti("uTiles", 0);
    prog.set2f("uGrid", float(cols), float(rows));
    prog.setf("uGap", mixf(0.02f, 0.5f, s.gap));
    prog.setf("uRound", s.round);
    prog.setf("uGain", 1.5f * 1.3f * c.look->brightness);
    drawFullscreen();
  }

 private:
  struct Ring {
    float x, y, r, t;
  };
  Program prog;
  GLuint tex = 0;
  std::vector<float> level, tint;
  std::vector<uint8_t> pixels;
  std::vector<Ring> rings;
  Rng rng;
  int rows = 0, cols = 0;
  float aspect = 1;
  double lastCycles = 0;
};

}  // namespace

std::unique_ptr<Effect> makeGrid() { return std::make_unique<Grid>(); }
