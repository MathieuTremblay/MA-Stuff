#pragma once
#include <array>
#include <string>

using Color3 = std::array<float, 3>;

enum EffectId { FX_FLUID, FX_PARTICLES, FX_KALEIDO, FX_LASERS, FX_STROBE, FX_COUNT };
enum MirrorMode { MIRROR_OFF, MIRROR_LR, MIRROR_UD, MIRROR_QUAD, MIRROR_RADIAL6, MIRROR_RADIAL8, MIRROR_COUNT };
enum ReleaseMode { RELEASE_FADE, RELEASE_SNAP };
enum ColorMode { COLOR_GRADIENT, COLOR_SPEED, COLOR_POSITION, COLOR_BEAT, COLOR_RANDOM, COLOR_COUNT };
enum LaserMode { LASER_FAN, LASER_BURST, LASER_WEB, LASER_SCAN, LASER_COUNT };
enum StrobeShape { STROBE_FULL, STROBE_SPOT, STROBE_HBAR, STROBE_VBAR, STROBE_SPARKLE, STROBE_COUNT };
enum ParticleStrobe { PSTROBE_OFF, PSTROBE_SYNC, PSTROBE_SPARKLE, PSTROBE_COUNT };

// Flashes per beat for the strobe "rate" buttons.
constexpr int kNumDivisions = 6;
extern const float kDivisions[kNumDivisions];
extern const char* const kDivisionLabels[kNumDivisions];
// Effect speed multipliers.
constexpr int kNumSpeedMults = 5;
extern const float kSpeedMults[kNumSpeedMults];
extern const char* const kSpeedMultLabels[kNumSpeedMults];

struct Palette {
  std::string name;
  Color3 low{}, mid{}, high{};
};

// All sliders are 0..1 unless noted. "Centered" sliders use 0.5 as zero.
struct FluidSettings {
  float size = 0.4f, force = 0.5f, swirl = 0.4f, trail = 0.6f, intensity = 0.5f;
};
struct ParticleSettings {
  float amount = 0.6f, size = 0.5f, life = 0.5f, speed = 0.4f, spread = 0.6f;
  float gravity = 0.5f;  // centered
  float trail = 0.3f;
  int strobe = PSTROBE_OFF, strobeRate = 3;
};
struct KaleidoSettings {
  int segments = 8;
  float rotation = 0.6f, zoom = 0.6f;  // centered
  float warp = 0.4f, brush = 0.4f, trail = 0.6f;
};
struct LaserSettings {
  int mode = LASER_FAN, beams = 6;
  float rotation = 0.65f;  // centered
  float width = 0.3f, glow = 0.5f, length = 1.0f, trail = 0.2f;
};
struct StrobeSettings {
  int shape = STROBE_FULL, rate = 3;
  bool freeHz = false;
  float hz = 8.0f;  // 1..25
  float duty = 0.3f, size = 0.4f;
  bool colorCycle = true;
};

// Everything a scene stores.
struct Look {
  int effect = FX_FLUID;
  FluidSettings fluid;
  ParticleSettings particles;
  KaleidoSettings kaleido;
  LaserSettings lasers;
  StrobeSettings strobe;
  int mirror = MIRROR_LR;
  int release = RELEASE_FADE;
  float fadeTime = 1.5f;  // seconds
  int colorMode = COLOR_GRADIENT;
  int palette = 0;
  float brightness = 1.0f;
  float bpm = 120.0f;
  int speedMult = 2;
};

struct Scene {
  bool used = false;
  Look look;
};

struct OutputSettings {
  int width = 1920, height = 1080, fps = 60;
  bool ndiEnabled = true;
  std::string ndiName = "TouchFX";
};

constexpr int kNumPalettes = 10;
constexpr int kNumScenes = 12;
constexpr int kMaxOutW = 1920, kMaxOutH = 1080;

struct AppState {
  Look look;
  std::array<Palette, kNumPalettes> palettes;
  std::array<Scene, kNumScenes> scenes;
  OutputSettings output;
  float uiScale = 1.0f;  // multiplier on top of the monitor DPI scale
  bool fullscreen = false;
  int tab = 0;
};

Palette defaultPalette(int i);
AppState defaultState();
void sanitize(AppState& s);
std::string serializeState(const AppState& s);
bool loadState(const std::string& path, AppState& s);
bool saveStateText(const std::string& path, const std::string& text);
