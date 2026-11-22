#include "state.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using nlohmann::json;

const float kDivisions[kNumDivisions] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};
const char* const kDivisionLabels[kNumDivisions] = {"1/4", "1/2", "1", "2", "4", "8"};
const float kSpeedMults[kNumSpeedMults] = {0.25f, 0.5f, 1.f, 2.f, 4.f};
const char* const kSpeedMultLabels[kNumSpeedMults] = {"x1/4", "x1/2", "x1", "x2", "x4"};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Palette, name, low, mid, high)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FluidSettings, size, force, swirl, trail, intensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParticleSettings, amount, size, life, speed, spread, gravity,
                                                trail, strobe, strobeRate)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(KaleidoSettings, segments, rotation, zoom, warp, brush, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LaserSettings, mode, beams, rotation, spread, swing, width, glow, length,
                                                trail, chase)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SparkleSettings, amount, size, spread, twinkle, life, drift, rays, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PostSettings, bloom, bloomSize, saturation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RippleSettings, size, strength, decay, shine, beatDrops)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RibbonSettings, strands, length, width, wave, glow, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GeometrySettings, shape, rate, size, spin, width, life, glow)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TunnelSettings, sides, speed, density, twist, width, depth)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LightningSettings, branches, chaos, rate, width, glow, reach, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlexusSettings, amount, range, size, drift, spread, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FireworkSettings, rate, rockets, size, amount, gravity, trail, sparkle)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StarfieldSettings, amount, speed, streak, size, twinkle)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpectrumSettings, mode, bars, symmetric, size, gain, width, glow)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GridSettings, mode, tile, gap, decay, round, radius)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StrobeSettings, shape, rate, freeHz, hz, duty, size, colorCycle,
                                                audioHits)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AudioSettings, enabled, device, sensitivity, autoBpm, autoPlay, points,
                                                motion, reactivity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Look, effect, fluid, particles, kaleido, lasers, strobe, sparkles, ripples,
                                                ribbons, geometry, tunnel, lightning, plexus, fireworks, starfield,
                                                spectrum, grid, post,
                                                mirror,
                                                release, fadeTime, colorMode, palette, brightness, bpm, speedMult)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene, used, look)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OutputSettings, width, height, fps, ndiEnabled, ndiName, alpha,
                                                premultiplied)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DmxSettings, protocol, sacnUniverse, artnetUniverse, address, mode, lock)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ExternalSettings, enabled, display, fit, windowed)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LayoutSettings, panelLeft, panelWidth, sceneStrip, effectStrip)

static Color3 hex(unsigned v) {
  return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f};
}

Palette defaultPalette(int i) {
  struct D { const char* n; unsigned lo, mi, hi; };
  static const D d[kNumPalettes] = {
      {"Ocean", 0x0a1e8c, 0x1e78ff, 0xa0e6ff},  {"Neon", 0x6a00ff, 0xff2bd6, 0xffd1f5},
      {"Fire", 0x8c0a00, 0xff5a00, 0xffd24a},   {"Toxic", 0x0b6b00, 0x5aff00, 0xeaff8a},
      {"Sunset", 0x5b1a8c, 0xff3d6e, 0xffb347}, {"Ice", 0x1b2a6b, 0x6fb7ff, 0xffffff},
      {"Cyber", 0x00f0ff, 0xff00c8, 0xfff200},  {"Royal", 0x2a0a6b, 0x8a2be2, 0xffd700},
      {"Blood", 0x3a0000, 0xc00010, 0xff6060},  {"Mono", 0x303030, 0xa0a0a0, 0xffffff},
  };
  i = std::clamp(i, 0, kNumPalettes - 1);
  return {d[i].n, hex(d[i].lo), hex(d[i].mi), hex(d[i].hi)};
}

AppState defaultState() {
  AppState s;
  for (int i = 0; i < kNumPalettes; ++i) s.palettes[i] = defaultPalette(i);
  return s;
}

template <class T>
static void clampRef(T& v, T lo, T hi) { v = std::clamp(v, lo, hi); }

static void sanitizeLook(Look& l) {
  clampRef(l.effect, 0, FX_COUNT - 1);
  clampRef(l.mirror, 0, MIRROR_COUNT - 1);
  clampRef(l.release, 0, 1);
  clampRef(l.fadeTime, 0.05f, 10.f);
  clampRef(l.colorMode, 0, COLOR_COUNT - 1);
  clampRef(l.palette, 0, kNumPalettes - 1);
  clampRef(l.brightness, 0.f, 2.f);
  clampRef(l.bpm, 20.f, 300.f);
  clampRef(l.speedMult, 0, kNumSpeedMults - 1);
  clampRef(l.particles.strobe, 0, PSTROBE_COUNT - 1);
  clampRef(l.particles.strobeRate, 0, kNumDivisions - 1);
  clampRef(l.kaleido.segments, 2, 16);
  clampRef(l.lasers.mode, 0, LASER_COUNT - 1);
  clampRef(l.lasers.beams, 1, 32);
  clampRef(l.lasers.chase, 0, CHASE_COUNT - 1);
  clampRef(l.strobe.shape, 0, STROBE_COUNT - 1);
  clampRef(l.strobe.rate, 0, kNumDivisions - 1);
  clampRef(l.ribbons.strands, 1, 6);
  clampRef(l.geometry.shape, 0, SHAPE_COUNT - 1);
  clampRef(l.geometry.rate, 0, kNumDivisions - 1);
  clampRef(l.tunnel.sides, 0, 8);
  if (l.tunnel.sides > 0 && l.tunnel.sides < 3) l.tunnel.sides = 3;
  clampRef(l.fireworks.rate, 0, kNumDivisions - 1);
  clampRef(l.spectrum.mode, 0, SPEC_COUNT - 1);
  clampRef(l.spectrum.bars, 8, 128);
  clampRef(l.grid.mode, 0, GRID_COUNT - 1);
  clampRef(l.strobe.hz, 0.5f, 25.f);
}

void sanitize(AppState& s) {
  sanitizeLook(s.look);
  for (auto& sc : s.scenes) sanitizeLook(sc.look);
  clampRef(s.output.width, 64, kMaxOutW);
  clampRef(s.output.height, 64, kMaxOutH);
  s.output.width &= ~1;  // even sizes keep video receivers happy
  s.output.height &= ~1;
  if (s.output.fps != 30 && s.output.fps != 60) s.output.fps = 60;
  if (s.output.ndiName.empty()) s.output.ndiName = "TouchFX";
  clampRef(s.uiScale, 0.5f, 3.f);
  clampRef(s.tab, 0, 1);
  clampRef(s.settingsTab, 0, 5);
  clampRef(s.dmx.protocol, 0, 2);
  clampRef(s.dmx.sacnUniverse, 1, 63999);
  clampRef(s.dmx.artnetUniverse, 0, 32767);
  clampRef(s.dmx.address, 1, 512);
  clampRef(s.dmx.mode, 0, 1);
  clampRef(s.audio.points, 1, 4);
  clampRef(s.maxTouches, 1, kMaxTouches);
  clampRef(s.external.display, 0, 15);
  clampRef(s.external.fit, 0, FIT_COUNT - 1);
  clampRef(s.layout.panelWidth, 0.6f, 1.8f);
}

std::string serializeState(const AppState& s) {
  json j;
  j["version"] = 1;
  j["look"] = s.look;
  j["palettes"] = s.palettes;
  j["scenes"] = s.scenes;
  j["output"] = s.output;
  j["external"] = s.external;
  j["layout"] = s.layout;
  j["audio"] = s.audio;
  j["dmx"] = s.dmx;
  j["blockGestures"] = s.blockGestures;
  j["gestureRestore"] = s.gestureRestore;
  j["maxTouches"] = s.maxTouches;
  j["showTouches"] = s.showTouches;
  j["settingsTab"] = s.settingsTab;
  j["uiScale"] = s.uiScale;
  j["fullscreen"] = s.fullscreen;
  j["tab"] = s.tab;
  return j.dump(2);
}

template <class T, size_t N>
static void readArray(const json& j, const char* key, std::array<T, N>& out) {
  if (!j.contains(key) || !j[key].is_array()) return;
  const json& a = j[key];
  for (size_t i = 0; i < N && i < a.size(); ++i) {
    try {
      out[i] = a[i].get<T>();
    } catch (...) {
    }
  }
}

bool loadState(const std::string& path, AppState& s) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  try {
    json j = json::parse(f);
    AppState d = defaultState();
    if (j.contains("look")) d.look = j["look"].get<Look>();
    readArray(j, "palettes", d.palettes);
    readArray(j, "scenes", d.scenes);
    if (j.contains("output")) d.output = j["output"].get<OutputSettings>();
    if (j.contains("external")) d.external = j["external"].get<ExternalSettings>();
    if (j.contains("layout")) d.layout = j["layout"].get<LayoutSettings>();
    if (j.contains("audio")) d.audio = j["audio"].get<AudioSettings>();
    if (j.contains("dmx")) d.dmx = j["dmx"].get<DmxSettings>();
    d.blockGestures = j.value("blockGestures", d.blockGestures);
    d.gestureRestore = j.value("gestureRestore", d.gestureRestore);
    d.maxTouches = j.value("maxTouches", d.maxTouches);
    d.showTouches = j.value("showTouches", d.showTouches);
    d.settingsTab = j.value("settingsTab", d.settingsTab);
    d.uiScale = j.value("uiScale", d.uiScale);
    d.fullscreen = j.value("fullscreen", d.fullscreen);
    d.tab = j.value("tab", d.tab);
    sanitize(d);
    s = d;
    return true;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Could not read settings %s: %s\n", path.c_str(), e.what());
    return false;
  }
}

bool saveStateText(const std::string& path, const std::string& text) {
  // Write to a temp file then swap it in, so a crash mid-write never corrupts the settings.
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    if (!f) return false;
  }
  std::remove(path.c_str());
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}
