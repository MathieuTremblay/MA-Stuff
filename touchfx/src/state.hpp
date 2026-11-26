#pragma once
#include <array>
#include <string>
#include <vector>

using Color3 = std::array<float, 3>;

// Values are stored in settings files: only ever append.
enum EffectId {
  FX_FLUID, FX_PARTICLES, FX_KALEIDO, FX_LASERS, FX_STROBE, FX_SPARKLES,
  FX_RIPPLES, FX_RIBBONS, FX_GEOMETRY, FX_TUNNEL, FX_VIDEO /* Lightning before 0.8 */, FX_PLEXUS, FX_FIREWORKS, FX_STARFIELD,
  FX_SPECTRUM, FX_GRID,
  FX_COUNT
};
constexpr int kDmxEffects = 6;  // effects reachable from the DMX effect channel (profile unchanged for now)
enum MirrorMode { MIRROR_OFF, MIRROR_LR, MIRROR_UD, MIRROR_QUAD, MIRROR_RADIAL6, MIRROR_RADIAL8, MIRROR_COUNT };
enum ReleaseMode { RELEASE_FADE, RELEASE_SNAP, RELEASE_HOLD };
// COLOR_VIDEO: picks up the video's colour where the fingers are (the effect carries it);
// COLOR_VIDEO_MASK: every pixel takes the colour of the video under it.
enum ColorMode {
  COLOR_GRADIENT, COLOR_SPEED, COLOR_POSITION, COLOR_BEAT, COLOR_RANDOM, COLOR_VIDEO, COLOR_VIDEO_MASK, COLOR_COUNT
};
enum LaserMode { LASER_FAN, LASER_AIM, LASER_WEB, LASER_SCAN, LASER_TUNNEL, LASER_WAVE, LASER_COUNT };
enum LaserChase { CHASE_OFF, CHASE_STEP, CHASE_ALTERNATE, CHASE_RANDOM, CHASE_COUNT };
enum SwingMode { SWING_TOGETHER, SWING_MIRROR, SWING_WAVE, SWING_RANDOM, SWING_COUNT };
enum StrobeShape {
  STROBE_FULL, STROBE_SPOT, STROBE_HBAR, STROBE_VBAR, STROBE_SPARKLE,
  STROBE_SHOCKWAVE, STROBE_WIPE, STROBE_BLINDER, STROBE_BLOCKS, STROBE_COUNT
};
enum ShapeType { SHAPE_TRI, SHAPE_SQUARE, SHAPE_PENT, SHAPE_HEX, SHAPE_CIRCLE, SHAPE_STAR, SHAPE_MIX, SHAPE_COUNT };
enum RibbonMotion { RIBBON_STAY, RIBBON_FLOAT, RIBBON_MOTION_COUNT };
enum SpectrumMode { SPEC_CIRCLE, SPEC_BARS, SPEC_WAVE, SPEC_WATERFALL, SPEC_SCOPE, SPEC_RINGS, SPEC_BURST, SPEC_COUNT };
enum GridMode { GRID_TRAIL, GRID_RINGS, GRID_SPARKLE, GRID_SCROLL, GRID_EQ, GRID_PLASMA, GRID_RAIN, GRID_CHECKER,
                GRID_RADAR, GRID_VIDEO, GRID_VU, GRID_SPECTRO, GRID_COUNT };
enum ParticleStrobe { PSTROBE_OFF, PSTROBE_SYNC, PSTROBE_SPARKLE, PSTROBE_COUNT };
enum VideoMode { VID_REVEAL, VID_LIQUID, VID_RIPPLE, VID_KALEIDO, VID_PIXEL, VID_WARP, VID_CLEAN, VID_COUNT };
enum VideoFit { VFIT_FILL, VFIT_FIT, VFIT_STRETCH, VFIT_COUNT };
enum BlendMode { BLEND_ADD, BLEND_BEHIND, BLEND_REVEAL, BLEND_COUNT };

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
  int swingMode = 0;      // SwingMode
  float scatter = 0.f;    // random offset of each beam's angle
};
struct SparkleSettings {
  float amount = 0.5f, size = 0.5f, spread = 0.35f, twinkle = 0.5f, life = 0.5f, drift = 0.25f, rays = 0.6f,
        trail = 0.1f;
};
struct RippleSettings {
  float size = 0.4f, strength = 0.55f, decay = 0.5f, shine = 0.6f;
  bool beatDrops = true;
};
struct RibbonSettings {
  int bands = 4;  // parallel colour bands in each stroke, 2..6
  int motion = 0;  // RibbonMotion: STAY (paint stays where drawn, ripples) or FLOAT (drifts away as it fades)
  float length = 0.5f, width = 0.5f, wave = 0.4f, flow = 0.4f, glow = 0.f;
};
struct GeometrySettings {
  int shape = SHAPE_MIX, rate = 3;
  float size = 0.5f, spin = 0.65f /*centered*/, width = 0.3f, life = 0.5f, glow = 0.5f;
};
struct TunnelSettings {
  int sides = 0;  // 0 = round, 3..8 = polygon
  bool merge = false;  // one tunnel for all fingers instead of one per finger
  float speed = 0.5f, density = 0.5f, twist = 0.5f /*centered*/, width = 0.35f, depth = 0.5f;
};
// VIDEO effect: the video input, played with the fingers.
struct VideoSettings {
  int mode = VID_REVEAL;
  int fit = VFIT_FILL;
  int segments = 6;       // KALEIDO
  float size = 0.5f;      // brush / cell / lens size
  float strength = 0.5f;  // how strongly the fingers act
  float trail = 0.6f;     // how long the touched areas stay (REVEAL, LIQUID, RIPPLE)
  float level = 0.8f;     // video brightness
  float glow = 0.3f;      // edge glow in the effect's colours
  bool alwaysOn = false;  // the picture stays up without fingers (and does not fade on release)
  float base = 0.3f;      // REVEAL + ALWAYS ON: level of the unrevealed video
};
struct PlexusSettings {
  float amount = 0.5f, range = 0.5f, size = 0.4f, drift = 0.4f, spread = 0.5f, trail = 0.1f;
};
struct FireworkSettings {
  int rate = 2;
  bool rockets = true;
  float size = 0.5f, amount = 0.5f, gravity = 0.5f, trail = 0.5f, sparkle = 0.5f;
};
struct StarfieldSettings {
  float amount = 0.3f, speed = 0.4f, streak = 0.35f, size = 0.3f, twinkle = 0.3f, brightness = 0.5f;
};
struct SpectrumSettings {
  int mode = SPEC_CIRCLE, bars = 48;
  bool symmetric = true;
  float size = 0.5f, gain = 0.5f, width = 0.4f, glow = 0.5f;
};
struct GridSettings {
  int mode = GRID_TRAIL;
  float tile = 0.4f, gap = 0.3f, decay = 0.5f, round = 0.3f, radius = 0.4f;
  float bulb = 0.f;  // incandescent bulbs: warm colour when dim, slow to heat / cool, round glowing bulb
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

// The settings of every effect (used by the background layer, which has its own copy).
#define TOUCHFX_FX_PARAMS(X) \
  X(fluid) X(particles) X(kaleido) X(lasers) X(strobe) X(sparkles) X(ripples) X(ribbons) X(geometry) X(tunnel) \
  X(video) X(plexus) X(fireworks) X(starfield) X(spectrum) X(grid)
struct FxParams {
  FluidSettings fluid;
  ParticleSettings particles;
  KaleidoSettings kaleido;
  LaserSettings lasers;
  StrobeSettings strobe;
  SparkleSettings sparkles;
  RippleSettings ripples;
  RibbonSettings ribbons;
  GeometrySettings geometry;
  TunnelSettings tunnel;
  VideoSettings video;
  PlexusSettings plexus;
  FireworkSettings fireworks;
  StarfieldSettings starfield;
  SpectrumSettings spectrum;
  GridSettings grid;
};

// Background layer: a second effect behind the one played with the fingers. It moves by itself (and with
// the music); it never fades on release.
// What moves the background layer
enum BgSource { BGSRC_AUTO, BGSRC_TOUCH, BGSRC_BOTH, BGSRC_COUNT };

struct BgLayer {
  bool on = false;
  int effect = FX_STARFIELD;
  float level = 0.6f;
  bool followTouch = false;  // old files (0.9.1 and before); source replaces it
  int source = BGSRC_AUTO;   // BgSource; followTouch is kept in step (on for TOUCH / BOTH)
  int blend = BLEND_ADD;
  int mirror = MIRROR_OFF;
  bool ownColors = false;    // own palette / colour mode instead of the front layer's
  int palette = 0;
  int colorMode = COLOR_GRADIENT;
  int speedMult = 2;
  float motion = 0.4f;       // how fast its automatic points move
  FxParams fx;
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
  RippleSettings ripples;
  RibbonSettings ribbons;
  GeometrySettings geometry;
  TunnelSettings tunnel;
  VideoSettings video;
  PlexusSettings plexus;
  FireworkSettings fireworks;
  StarfieldSettings starfield;
  SpectrumSettings spectrum;
  GridSettings grid;
  PostSettings post;
  int mirror = MIRROR_LR;
  int release = RELEASE_FADE;    // v0.5 and older (migrated into releaseFx / fadeFx)
  float fadeTime = 1.5f;
  std::vector<int> releaseFx = std::vector<int>(FX_COUNT, RELEASE_FADE);  // per effect: fade / snap
  std::vector<float> fadeFx = std::vector<float>(FX_COUNT, 1.5f);         // per effect: fade time (s)
  bool useCustom = false;        // quick colours from the LIVE tab override the palette
  Palette custom;
  int colorMode = COLOR_GRADIENT;
  int palette = 0;
  float brightness = 1.0f;
  float bpm = 120.0f;
  int speedMult = 2;
  BgLayer bg;
};

// Copies the effect settings between a look and a background layer.
void paramsToLook(const FxParams& p, Look& l);
void lookToParams(const Look& l, FxParams& p);

struct Scene {
  bool used = false;
  bool flash = false;  // FLASH: on only while its button is held, then everything goes back as it was
  std::string name;  // shown on the scene button ("" = effect name)
  Look look;
};

enum CueFollow { FOLLOW_NONE, FOLLOW_SECONDS, FOLLOW_BARS, FOLLOW_COUNT };
struct Cue {
  float number = 0;     // cue number shown and used to sort (1, 2, 2.5...), like a console cue list
  std::string name;
  Look look;            // the full look (BPM is never recalled)
  float fade = 1.0f;    // seconds
  int follow = FOLLOW_NONE;
  float followValue = 4.f;  // seconds or bars
};
enum CueEnd { END_STOP, END_LOOP, END_NEXT_SONG, END_COUNT };  // GO on the last cue
struct CueList {  // one per song
  std::string name = "Song";
  int endAction = END_STOP;
  std::vector<Cue> cues;
};

struct OutputSettings {
  int width = 1920, height = 1080, fps = 60;
  bool ndiEnabled = true;
  std::string ndiName = "TouchFX";
  std::string ndiAdapter;       // network card for NDI ("" = automatic, all cards)
  std::string ndiAdapterLabel;  // its name / IP, shown if it is missing
  bool spout = false;          // Spout output (Windows, same PC)
  std::string spoutName = "TouchFX";
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
  // Frequency range the hits, pulse and motion listen to
  int range = 1;             // AudioRange; RANGE_CUSTOM uses rangeLo / rangeHi
  float rangeLo = 40.f, rangeHi = 250.f;  // Hz
};
enum AudioRange { RANGE_KICK, RANGE_BASS, RANGE_LOWMID, RANGE_VOCALS, RANGE_HATS, RANGE_FULL, RANGE_CUSTOM, RANGE_COUNT };
extern const char* const kAudioRangeNames[RANGE_COUNT];
void audioRange(const AudioSettings& as, float& lo, float& hi);

struct DmxSettings {
  int protocol = 1;          // 0 off, 1 sACN, 2 Art-Net
  int sacnUniverse = 100;    // 1..63999
  int artnetUniverse = 99;   // Art-Net port-address 0..32767 (counts from 0: MA universe 100 = Art-Net 99)
  int address = 1;           // 1..512
  int mode = 1;              // 0 Basic, 1 Advanced
  bool lock = false;         // DMX always wins over the panel
};

enum VideoSourceType { VSRC_OFF, VSRC_NDI, VSRC_CAPTURE, VSRC_SPOUT, VSRC_DECKLINK, VSRC_AJA, VSRC_TEST, VSRC_COUNT };
struct VideoInSettings {  // belongs to the PC
  int type = VSRC_OFF;
  std::string source;      // NDI source name / device name ("" = first one found)
};

struct LayoutSettings {
  bool panelLeft = false;
  float panelWidth = 1.0f;  // multiplier
  bool sceneStrip = true;   // scenes bar under the canvas (otherwise in the LIVE tab)
  bool effectStrip = true;  // effect buttons under the canvas (otherwise in the LIVE tab)
};

constexpr int kNumPalettes = 10;
constexpr int kNumScenes = 12;
constexpr int kMaxOutW = 1920, kMaxOutH = 1080;
constexpr int kMaxTouches = 10;
constexpr int kMaxAutoPoints = 10;  // audio auto-play points

struct AppState {
  Look look;
  std::array<Palette, kNumPalettes> palettes;
  std::array<Scene, kNumScenes> scenes;
  std::vector<CueList> cueLists;  // songs, each with its own cues
  int activeList = 0;
  OutputSettings output;
  ExternalSettings external;
  LayoutSettings layout;
  AudioSettings audio;
  VideoInSettings videoIn;
  DmxSettings dmx;
  std::string dmxAdapter;       // network card DMX listens on ("" = all) - belongs to the PC, not the show
  std::string dmxAdapterLabel;
  float effectFade = 0.5f;  // seconds to fade out the old effect when switching (0 = cut)
  int maxTouches = 4;  // 1..10
  bool blockGestures = true;  // turn off Windows 3/4-finger gestures while running
  int gestureRestore = -1;    // original Windows value to put back (-1 = nothing to restore)
  bool showTouches = true;
  int settingsTab = 0;
  bool showMode = false;  // locks settings and editing during a show
  std::string lastShow;   // name of the last opened show (highlighted in the launcher)
  float uiScale = 1.0f;  // multiplier on top of the monitor DPI scale
  bool fullscreen = false;
  int tab = 0;
};

Palette defaultPalette(int i);
AppState defaultState();
void sanitize(AppState& s);
// The show file holds the creative data; settings.json holds what belongs to this PC.
std::string serializeShow(const AppState& s);
std::string serializeMachine(const AppState& s);
bool loadShow(const std::string& path, AppState& s);     // replaces the show part of s
bool loadMachine(const std::string& path, AppState& s);  // replaces the PC part of s
bool fileHasShowData(const std::string& path);           // settings.json from before show files
bool loadState(const std::string& path, AppState& s);    // everything (old settings.json)
bool saveStateText(const std::string& path, const std::string& text);

// Look helpers for the cue engine
std::string lookToText(const Look& l);
bool lookFromText(const std::string& text, Look& l);
std::string lookBlendText(const std::string& from, const std::string& to, float t);  // floats glide, rest = 'to'
std::string lookFingerprint(const Look& l);  // look without BPM, for comparisons
