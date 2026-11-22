#pragma once
#include <array>
#include <string>

using Color3 = std::array<float, 3>;

// Values are stored in settings files: only ever append.
enum EffectId { FX_FLUID, FX_PARTICLES, FX_KALEIDO, FX_LASERS, FX_STROBE, FX_SPARKLES, FX_COUNT };
enum MirrorMode { MIRROR_OFF, MIRROR_LR, MIRROR_UD, MIRROR_QUAD, MIRROR_RADIAL6, MIRROR_RADIAL8, MIRROR_COUNT };
enum ReleaseMode { RELEASE_FADE, RELEASE_SNAP };
enum ColorMode { COLOR_GRADIENT, COLOR_SPEED, COLOR_POSITION, COLOR_BEAT, COLOR_RANDOM, COLOR_COUNT };
enum LaserMode { LASER_FAN, LASER_AIM, LASER_WEB, LASER_SCAN, LASER_TUNNEL, LASER_WAVE, LASER_COUNT };
enum LaserChase { CHASE_OFF, CHASE_STEP, CHASE_ALTERNATE, CHASE_RANDOM, CHASE_COUNT };
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
  int mode = LASER_FAN, beams = 8;
  float rotation = 0.6f;  // centered
  float spread = 0.45f;   // fan angle / wave height / tunnel size
  float swing = 0.3f;     // back-and-forth sweep on the beat
  float width = 0.25f, glow = 0.5f, length = 1.0f, trail = 0.15f;
  int chase = CHASE_OFF;
};
struct SparkleSettings {
  float amount = 0.5f, size = 0.5f, spread = 0.35f, twinkle = 0.5f, life = 0.5f, drift = 0.25f, rays = 0.6f,
        trail = 0.1f;
};
// Global look applied after every effect.
struct PostSettings {
  float bloom = 0.55f, bloomSize = 0.6f;
  float saturation = 0.62f;  // 0.5 = unchanged
};
struct StrobeSettings {
  int shape = STROBE_FULL, rate = 3;
  bool freeHz = false;
  float hz = 8.0f;  // 1..25
  float duty = 0.3f, size = 0.4f;
  bool colorCycle = true;
  bool audioHits = false;  // flash on detected audio hits instead of the rate
};

// Everything a scene stores.
struct Look {
  int effect = FX_FLUID;
  FluidSettings fluid;
  ParticleSettings particles;
  KaleidoSettings kaleido;
  LaserSettings lasers;
  StrobeSettings strobe;
  SparkleSettings sparkles;
  PostSettings post;
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
  bool alpha = false;          // send an alpha channel (transparent background)
  bool premultiplied = false;  // premultiplied instead of straight alpha
};

enum FitMode { FIT_FIT, FIT_FILL, FIT_STRETCH, FIT_COUNT };
struct ExternalSettings {
  bool enabled = false;
  int display = 1;
  int fit = FIT_FIT;
  bool windowed = false;  // a movable window instead of fullscreen (handy with one screen)
};

struct AudioSettings {
  bool enabled = false;
  std::string device;        // "" = default input, "@loopback" = system audio, "@test" = test beat
  float sensitivity = 0.6f;
  bool autoBpm = true;       // follow the detected tempo
  bool autoPlay = true;      // virtual fingers driven by the music (no touch needed)
  int points = 2;            // number of virtual fingers 1..4
  float motion = 0.5f;       // how fast they move
  float reactivity = 0.7f;   // how strongly the audio drives the effects
};

struct LayoutSettings {
  bool panelLeft = false;
  float panelWidth = 1.0f;  // multiplier
  bool sceneStrip = true;   // scenes bar under the canvas (otherwise in the LIVE tab)
};

constexpr int kNumPalettes = 10;
constexpr int kNumScenes = 12;
constexpr int kMaxOutW = 1920, kMaxOutH = 1080;
constexpr int kMaxTouches = 10;

struct AppState {
  Look look;
  std::array<Palette, kNumPalettes> palettes;
  std::array<Scene, kNumScenes> scenes;
  OutputSettings output;
  ExternalSettings external;
  LayoutSettings layout;
  AudioSettings audio;
  int maxTouches = 4;  // 1..10
  bool blockGestures = true;  // turn off Windows 3/4-finger gestures while running
  int gestureRestore = -1;    // original Windows value to put back (-1 = nothing to restore)
  bool showTouches = true;
  int settingsTab = 0;
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
