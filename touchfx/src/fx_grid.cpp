// LED grid: the canvas as a coarse grid of LED tiles.
//   TRAIL / RINGS / SPARKLE  - tiles light under, around and out from the fingers
//   SCROLL     - bands of colour sweep across the wall on the beat (finger movement sets the direction)
//   EQUALIZER  - each column is a VU bar (audio spectrum, or a bounce on the beat without audio)
//   PLASMA     - smooth flowing colour over the wall
//   RAIN       - drops fall down the columns, more where you touch
//   CHECKER    - checker / stripes / blocks flipping on the beat
//   RADAR      - a sweep turning around each finger
//   VIDEO MAP  - the video input on the tiles
// Bulb: incandescent lamps (warm when dim, slow to heat up and cool down, a round glowing bulb).
#include <algorithm>
#include <cmath>

#include "effects.hpp"

namespace {

Vec2 unit(Vec2 v) {
  float l = length(v);
  return l > 1e-6f ? v * (1.f / l) : Vec2(1, 0);
}

const char* kGridFS = R"(
in vec2 vUv; out vec4 o;
uniform sampler2D uTiles; uniform sampler2D uVideo;
uniform vec2 uGrid, uVFit; uniform float uGap, uRound, uGain, uBulb, uVLevel; uniform int uVideoMap;
void main() {
  vec2 g = vUv * uGrid;
  vec2 cell = floor(g);
  vec2 f = fract(g) - 0.5;
  vec2 cuv = (cell + 0.5) / uGrid;
  vec4 t = texture(uTiles, cuv);
  vec3 c = t.rgb;
  if (uVideoMap == 1) {  // the video at the tile's centre, brightness from the tile
    vec2 v = (cuv - 0.5) * uVFit + 0.5;
    vec3 vc = (v.x < 0.0 || v.y < 0.0 || v.x > 1.0 || v.y > 1.0) ? vec3(0.0) : texture(uVideo, v).rgb;
    c = vc * t.a * 1.5 * uVLevel;
  }
  // rounded square with a gap around it
  float hs = 0.5 - uGap * 0.5;
  float r = uRound * hs;
  vec2 q = abs(f) - vec2(hs - r);
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
  float px = fwidth(g.x);
  float m = 1.0 - smoothstep(-px, px, d);
  if (uBulb > 0.0) {
    // incandescent bulb: a hot filament in the middle, glass glowing around it
    float rd = length(f) / max(hs, 0.05);
    float glass = clamp(1.0 - rd, 0.0, 1.0);
    float bulb = pow(glass, 0.7) * 0.75 + exp(-rd * rd * 9.0) * 0.9;
    m = mix(m, bulb, uBulb);
  }
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
    std::fill(shown.begin(), shown.end(), 0.f);
    std::fill(peaks.begin(), peaks.end(), 0.f);
    rings.clear();
    drops.clear();
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
      shown.assign(n, 0.f);
      peaks.assign(size_t(cols), 0.f);
      drops.clear();
    }
    if (s.mode != lastMode) {
      lastMode = s.mode;
      drops.clear();
      rings.clear();
    }

    // ---- decay ----
    float decay = std::exp(-dt * mixf(12.f, 0.6f, s.decay));
    for (float& v : level) v *= decay * c.fadeMul;

    // ---- light tiles ----
    auto setTile = [&](int x, int y, float v, float t) {
      if (x < 0 || y < 0 || x >= cols || y >= rows) return;
      size_t i = size_t(y) * cols + x;
      if (v > level[i]) {
        level[i] = v;
        tint[i] = t;
      }
    };
    float radius = mixf(0.4f, 5.f, s.radius);  // in tiles
    auto lightAt = [&](float cx, float cy, float rad, float amount, float t) {
      int x0 = std::max(0, int(cx - rad - 1)), x1 = std::min(cols - 1, int(cx + rad + 1));
      int y0 = std::max(0, int(cy - rad - 1)), y1 = std::min(rows - 1, int(cy + rad + 1));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
          if (d > rad) continue;
          setTile(x, y, amount * (1.f - d / (rad + 0.001f) * 0.5f), t);
        }
    };
    double cycles = c.beat * 2.0;
    bool tick = std::floor(cycles) != std::floor(lastCycles) || (c.audioOn && c.onset);
    lastCycles = cycles;
    bool active = !c.live.empty();
    float beat = float(c.beat);
    float pattern = mixf(1.f, 0.15f, s.radius);  // whole-wall modes: bigger = fewer, wider shapes

    for (const Emitter& e : c.live) {
      float cx = e.pos.x * cols, cy = e.pos.y * rows;
      float t = c.colorT(&e, 0.f, hash01(uint32_t(e.touch) * 13u + uint32_t(c.beat)));
      switch (s.mode) {
        case GRID_TRAIL:
        case GRID_VIDEO: lightAt(cx, cy, radius, std::min(1.5f, c.drive), t); break;
        case GRID_RINGS:
          lightAt(cx, cy, 0.8f, 1.f, t);
          if (tick && rings.size() < 64) rings.push_back({cx, cy, 0.f, t});
          break;
        case GRID_SPARKLE:
          for (int k = 0; k < 3; ++k) {
            float a = rng.range(0, kTau), r = rng.next() * radius * 2.f;
            lightAt(cx + std::cos(a) * r, cy + std::sin(a) * r, 0.6f, rng.range(0.5f, 1.2f) * c.drive, rng.next());
          }
          break;
        case GRID_RADAR: {
          float ang = c.time * c.speed * 2.5f + e.touch * 1.3f;
          float R = radius * 4.f;
          int x0 = std::max(0, int(cx - R)), x1 = std::min(cols - 1, int(cx + R));
          int y0 = std::max(0, int(cy - R)), y1 = std::min(rows - 1, int(cy + R));
          for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
              float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
              float d = std::sqrt(dx * dx + dy * dy);
              if (d > R) continue;
              float da = std::remainder(std::atan2(dy, dx) - ang, kTau);
              if (std::fabs(da) < 0.12f + 0.6f / std::max(d, 1.f)) setTile(x, y, 1.f - d / R * 0.4f, d / R);
            }
          break;
        }
        default: break;
      }
    }
    if (s.mode == GRID_RINGS) {  // expanding rings
      for (Ring& r : rings) {
        r.r += dt * c.speed * 14.f;
        for (int k = 0; k < 96; ++k) {
          float a = kTau * k / 96;
          lightAt(r.x + std::cos(a) * r.r, r.y + std::sin(a) * r.r, 0.7f, std::max(0.f, 1.f - r.r / 40.f), r.t);
        }
      }
      rings.erase(std::remove_if(rings.begin(), rings.end(), [](const Ring& r) { return r.r > 40.f; }), rings.end());
    }

    // ---- whole-wall modes (while the effect is played) ----
    if (active) {
      switch (s.mode) {
        case GRID_SCROLL: {
          Vec2 v;
          for (const Emitter& e : c.live) v += e.vel;
          if (length(v) > 0.3f) scrollDir = scrollDir + (unit(v) - scrollDir) * std::min(1.f, dt * 4.f);
          Vec2 d = length(scrollDir) > 0.01f ? unit(scrollDir) : Vec2(1, 0);
          float bands = mixf(1.f, 6.f, pattern);
          float span = float(std::max(cols, rows));
          for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x) {
              float p = (x * d.x + y * d.y) / span * bands - beat * 0.5f;
              float v2 = std::pow(std::max(0.f, std::cos(kTau * p)), 3.f);
              setTile(x, y, v2, fractf(std::floor(p) * 0.37f));
            }
          break;
        }
        case GRID_EQ: {
          for (int x = 0; x < cols; ++x) {
            // symmetric: low frequencies in the middle
            float u = std::fabs((x + 0.5f) / cols - 0.5f) * 2.f;
            float val;
            if (c.spectrum) val = c.spectrum[std::min(31, int(u * 31.f))];
            else val = (0.35f + 0.3f * std::sin(beat * kPi * 2.f + x * 0.7f)) * (1.f - u * 0.5f) *
                       (0.6f + 0.4f * std::pow(1.f - fractf(beat), 2.f));
            val = std::clamp(val * mixf(0.6f, 1.6f, pattern), 0.f, 1.f);
            int h = int(val * rows + 0.5f);
            for (int y = 0; y < h; ++y) setTile(x, y, 1.f, float(y) / std::max(1, rows - 1));
            float& pk = peaks[size_t(x)];
            pk = std::max(pk - dt * 0.6f * rows, float(h));
            setTile(x, std::min(rows - 1, int(pk)), 1.f, 1.f);  // falling peak
          }
          break;
        }
        case GRID_PLASMA: {
          Vec2 ctr(cols * 0.5f, rows * 0.5f);
          if (!c.live.empty()) ctr = Vec2(c.live[0].pos.x * cols, c.live[0].pos.y * rows);
          float tt = c.time * c.speed * 0.8f;
          float k = mixf(0.35f, 0.07f, pattern);
          for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x) {
              float dx = x - ctr.x, dy = y - ctr.y;
              float v2 = std::sin(x * k + tt) + std::sin(y * k * 1.3f - tt * 1.2f) + std::sin((x + y) * k * 0.7f + tt * 0.6f) +
                         std::sin(std::sqrt(dx * dx + dy * dy) * k * 1.5f - tt * 2.f);
              float nrm = 0.5f + 0.125f * v2;
              setTile(x, y, 0.35f + 0.65f * nrm, nrm);
            }
          break;
        }
        case GRID_RAIN: {
          dropAcc += dt * mixf(4.f, 70.f, s.radius) * c.drive;
          while (dropAcc >= 1.f && drops.size() < 400) {
            dropAcc -= 1.f;
            int x = rng.next() < 0.3f ? int(rng.next() * cols) : -1;
            if (x < 0) {  // most drops near a finger's column
              const Emitter& e = c.live[size_t(rng.next() * c.live.size()) % c.live.size()];
              x = int(e.pos.x * cols) + int(rng.range(-2.5f, 2.5f));
            }
            drops.push_back({float(std::clamp(x, 0, cols - 1)), float(rows), rng.range(0.6f, 1.4f), rng.next()});
          }
          break;
        }
        case GRID_CHECKER: {
          if (tick) ++flips;
          int block = 1 + int((1.f - pattern) * 5.f);
          int kind = (flips / 8) % 4, parity = flips & 1;
          int cx = !c.live.empty() ? int(c.live[0].pos.x * cols) : cols / 2;
          int cy = !c.live.empty() ? int(c.live[0].pos.y * rows) : rows / 2;
          for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x) {
              int on;
              switch (kind) {
                case 0: on = (x / block + y / block) & 1; break;   // checker
                case 1: on = (y / block) & 1; break;               // horizontal stripes
                case 2: on = (x / block) & 1; break;               // vertical stripes
                default: on = (x < cx) != (y < cy); break;         // quarters around the finger
              }
              if (on == parity) setTile(x, y, 1.f, parity ? 0.15f : 0.85f);
            }
          break;
        }
        case GRID_VIDEO:
          for (float& v : level) v = std::max(v, 0.55f);
          break;
        default: break;
      }
    }
    if (s.mode == GRID_RAIN) {  // drops keep falling after release
      for (Drop& d : drops) {
        d.y -= dt * d.speed * rows * 0.6f * std::clamp(c.speed, 0.25f, 3.f);
        setTile(int(d.x), int(d.y), 1.f, d.t);
      }
      drops.erase(std::remove_if(drops.begin(), drops.end(), [](const Drop& d) { return d.y < -1.f; }), drops.end());
    }

    // ---- incandescent: slow to heat up / cool down ----
    float up = s.bulb > 0.001f ? 1.f - std::exp(-dt / (0.02f + 0.18f * s.bulb)) : 1.f;
    float down = s.bulb > 0.001f ? 1.f - std::exp(-dt / (0.03f + 0.45f * s.bulb)) : 1.f;
    for (size_t i = 0; i < n; ++i) shown[i] += (level[i] - shown[i]) * (level[i] > shown[i] ? up : down);

    // ---- upload tile colours and draw ----
    pixels.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
      float v = std::min(shown[i], 1.5f);
      float t = c.look->colorMode == COLOR_GRADIENT && s.mode <= GRID_SPARKLE ? 1.f - std::min(1.f, shown[i]) : tint[i];
      Vec3 col = c.color(t) * v;
      if (s.bulb > 0.001f) {
        // incandescent colour: deep amber when dim, warm white at full
        float u = std::min(1.f, v);
        Vec3 warm = Vec3(1.f, 0.28f + 0.5f * u, 0.04f + 0.42f * u * u) * (v * 1.35f);
        col = col + (warm - col) * (s.bulb * 0.8f);
      }
      auto to8 = [](float x) { return uint8_t(std::clamp(x / 1.5f, 0.f, 1.f) * 255.f); };
      pixels[i * 4] = to8(col.x);
      pixels[i * 4 + 1] = to8(col.y);
      pixels[i * 4 + 2] = to8(col.z);
      pixels[i * 4 + 3] = to8(v);  // brightness, for VIDEO MAP
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    out.bind();
    glDisable(GL_BLEND);
    prog.use();
    bindTex(0, tex);
    bindTex(1, c.videoTex);
    prog.seti("uTiles", 0);
    prog.seti("uVideo", 1);
    bool vmap = s.mode == GRID_VIDEO && c.videoTex;
    prog.seti("uVideoMap", vmap ? 1 : 0);
    float va = c.videoW > 0 && c.videoH > 0 ? float(c.videoW) / c.videoH : aspect;
    prog.set2f("uVFit", va > aspect ? aspect / va : 1.f, va > aspect ? 1.f : va / aspect);  // fill
    prog.setf("uVLevel", c.videoLevel);
    prog.set2f("uGrid", float(cols), float(rows));
    prog.setf("uGap", mixf(0.02f, 0.5f, s.gap));
    prog.setf("uRound", s.round);
    prog.setf("uBulb", s.bulb);
    prog.setf("uGain", 1.5f * 1.3f * c.look->brightness);
    drawFullscreen();
  }

 private:
  struct Ring {
    float x, y, r, t;
  };
  struct Drop {
    float x, y, speed, t;
  };
  Program prog;
  GLuint tex = 0;
  std::vector<float> level, tint, shown, peaks;
  std::vector<uint8_t> pixels;
  std::vector<Ring> rings;
  std::vector<Drop> drops;
  Rng rng;
  Vec2 scrollDir{1, 0};
  int rows = 0, cols = 0, lastMode = -1, flips = 0;
  float aspect = 1, dropAcc = 0;
  double lastCycles = 0;
};

}  // namespace

std::unique_ptr<Effect> makeGrid() { return std::make_unique<Grid>(); }
