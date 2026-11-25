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
                                                trail, chase, swingMode, scatter)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SparkleSettings, amount, size, spread, twinkle, life, drift, rays, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PostSettings, bloom, bloomSize, saturation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RippleSettings, size, strength, decay, shine, beatDrops)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RibbonSettings, strands, length, width, wind, flutter, stiffness, sheen)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GeometrySettings, shape, rate, size, spin, width, life, glow)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TunnelSettings, sides, merge, speed, density, twist, width, depth)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(VideoSettings, mode, fit, segments, size, strength, trail, level, glow,
                                                alwaysOn, base)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlexusSettings, amount, range, size, drift, spread, trail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FireworkSettings, rate, rockets, size, amount, gravity, trail, sparkle)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StarfieldSettings, amount, speed, streak, size, twinkle, brightness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpectrumSettings, mode, bars, symmetric, size, gain, width, glow)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GridSettings, mode, tile, gap, decay, round, radius, bulb)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StrobeSettings, shape, rate, freeHz, hz, duty, size, colorCycle,
                                                audioHits)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AudioSettings, enabled, device, sensitivity, autoBpm, autoPlay, points, range, rangeLo, rangeHi,
                                                motion, reactivity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FxParams, fluid, particles, kaleido, lasers, strobe, sparkles, ripples,
                                                ribbons, geometry, tunnel, video, plexus, fireworks, starfield,
                                                spectrum, grid)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BgLayer, on, effect, level, followTouch, blend, mirror, ownColors,
                                                palette, colorMode, speedMult, motion, fx)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Look, effect, fluid, particles, kaleido, lasers, strobe, sparkles, ripples,
                                                ribbons, geometry, tunnel, video, plexus, fireworks, starfield,
                                                spectrum, grid, post,
                                                mirror, releaseFx, fadeFx, useCustom, custom,
                                                release, fadeTime, colorMode, palette, brightness, bpm, speedMult, bg)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(VideoInSettings, type, source)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Scene, used, name, look)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Cue, number, name, look, fade, follow, followValue)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CueList, name, endAction, cues)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OutputSettings, width, height, fps, ndiEnabled, ndiName, ndiAdapter,
                                                ndiAdapterLabel, spout, spoutName, alpha,
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
  sanitize(s);  // fills the per-effect release lists
  return s;
}

template <class T>
static void clampRef(T& v, T lo, T hi) { v = std::clamp(v, lo, hi); }

void paramsToLook(const FxParams& p, Look& l) {
#define TFX_COPY(m) l.m = p.m;
  TOUCHFX_FX_PARAMS(TFX_COPY)
#undef TFX_COPY
}

void lookToParams(const Look& l, FxParams& p) {
#define TFX_COPY(m) p.m = l.m;
  TOUCHFX_FX_PARAMS(TFX_COPY)
#undef TFX_COPY
}

static void sanitizeParams(Look& l);

static void sanitizeLook(Look& l) {
  sanitizeParams(l);
  // background layer: same limits on its own copy of the effect settings
  BgLayer& b = l.bg;
  clampRef(b.effect, 0, FX_COUNT - 1);
  clampRef(b.level, 0.f, 1.f);
  clampRef(b.blend, 0, BLEND_COUNT - 1);
  clampRef(b.mirror, 0, MIRROR_COUNT - 1);
  clampRef(b.palette, 0, kNumPalettes - 1);
  clampRef(b.colorMode, 0, COLOR_COUNT - 1);
  clampRef(b.speedMult, 0, kNumSpeedMults - 1);
  clampRef(b.motion, 0.f, 1.f);
  {
    Look tmp;
    paramsToLook(b.fx, tmp);
    sanitizeParams(tmp);
    lookToParams(tmp, b.fx);
  }
}

static void sanitizeParams(Look& l) {
  // per-effect release: older files only had one global setting, which becomes every effect's start value
  if (int(l.releaseFx.size()) < FX_COUNT) l.releaseFx.resize(FX_COUNT, l.release);
  if (int(l.fadeFx.size()) < FX_COUNT) l.fadeFx.resize(FX_COUNT, l.fadeTime);
  l.releaseFx.resize(FX_COUNT);
  l.fadeFx.resize(FX_COUNT);
  for (int& r : l.releaseFx) r = std::clamp(r, 0, 2);
  for (float& f : l.fadeFx) f = std::clamp(f, 0.05f, 10.f);
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
  clampRef(l.lasers.swingMode, 0, SWING_COUNT - 1);
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
  clampRef(l.video.mode, 0, VID_COUNT - 1);
  clampRef(l.video.fit, 0, VFIT_COUNT - 1);
  clampRef(l.video.segments, 2, 16);
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
  clampRef(s.tab, 0, 2);  // 0 live, 1 settings, 2 cues
  clampRef(s.settingsTab, 0, 7);
  if (s.cueLists.empty()) s.cueLists.push_back(CueList{"Song 1", {}});
  for (CueList& cl : s.cueLists) {
    clampRef(cl.endAction, 0, END_COUNT - 1);
    bool numbered = true;  // shows from v0.7 have no cue numbers: number them 1, 2, 3...
    for (size_t i = 0; i < cl.cues.size(); ++i)
      if (!(cl.cues[i].number > 0) || (i > 0 && cl.cues[i].number <= cl.cues[i - 1].number)) numbered = false;
    for (size_t i = 0; i < cl.cues.size(); ++i) {
      Cue& c = cl.cues[i];
      if (!numbered) c.number = float(i + 1);
      sanitizeLook(c.look);
      clampRef(c.fade, 0.f, 30.f);
      clampRef(c.follow, 0, FOLLOW_COUNT - 1);
      clampRef(c.followValue, 0.1f, 600.f);
    }
  }
  clampRef(s.activeList, 0, int(s.cueLists.size()) - 1);
  clampRef(s.effectFade, 0.f, 3.f);
  if (s.output.spoutName.empty()) s.output.spoutName = "TouchFX";
  clampRef(s.dmx.protocol, 0, 2);
  clampRef(s.dmx.sacnUniverse, 1, 63999);
  clampRef(s.dmx.artnetUniverse, 0, 32767);
  clampRef(s.dmx.address, 1, 512);
  clampRef(s.dmx.mode, 0, 2);
  clampRef(s.audio.points, 1, kMaxAutoPoints);
  clampRef(s.audio.range, 0, RANGE_COUNT - 1);
  clampRef(s.audio.rangeLo, 20.f, 16000.f);
  clampRef(s.audio.rangeHi, 20.f, 16000.f);
  if (s.audio.rangeHi < s.audio.rangeLo + 10.f) s.audio.rangeHi = std::min(16000.f, s.audio.rangeLo + 10.f);
  clampRef(s.maxTouches, 1, kMaxTouches);
  clampRef(s.external.display, 0, 15);
  clampRef(s.external.fit, 0, FIT_COUNT - 1);
  clampRef(s.layout.panelWidth, 0.6f, 1.8f);
  clampRef(s.videoIn.type, 0, VSRC_COUNT - 1);
}

std::string serializeShow(const AppState& s) {
  json j;
  j["touchfxShow"] = 2;  // 2: effect 10 is VIDEO (it was Lightning)
  j["look"] = s.look;
  j["palettes"] = s.palettes;
  j["scenes"] = s.scenes;
  j["cueLists"] = s.cueLists;
  j["activeList"] = s.activeList;
  j["dmx"] = s.dmx;
  return j.dump(1);
}

std::string serializeMachine(const AppState& s) {
  json j;
  j["version"] = 2;
  j["output"] = s.output;
  j["external"] = s.external;
  j["layout"] = s.layout;
  j["audio"] = s.audio;
  j["videoIn"] = s.videoIn;
  j["showMode"] = s.showMode;
  j["dmxAdapter"] = s.dmxAdapter;
  j["dmxAdapterLabel"] = s.dmxAdapterLabel;
  j["lastShow"] = s.lastShow;
  j["blockGestures"] = s.blockGestures;
  j["gestureRestore"] = s.gestureRestore;
  j["maxTouches"] = s.maxTouches;
  j["effectFade"] = s.effectFade;
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

static void migrateLook(const json& lj, Look& l) {
  // Files from before per-effect release: every effect starts from the old global setting.
  if (!lj.contains("releaseFx")) l.releaseFx.assign(FX_COUNT, l.release);
  if (!lj.contains("fadeFx")) l.fadeFx.assign(FX_COUNT, l.fadeTime);
}

// Before 0.8, effect 10 was Lightning; it is VIDEO now. Old looks that used Lightning get Plexus (the closest).
static void migrateLightning(AppState& d) {
  auto fix = [](Look& l) {
    if (l.effect == FX_VIDEO) l.effect = FX_PLEXUS;
  };
  fix(d.look);
  for (Scene& sc : d.scenes) fix(sc.look);
  for (CueList& cl : d.cueLists)
    for (Cue& c : cl.cues) fix(c.look);
}

static void readShowPart(const json& j, AppState& d) {
  if (j.contains("look")) {
    d.look = j["look"].get<Look>();
    migrateLook(j["look"], d.look);
  }
  readArray(j, "palettes", d.palettes);
  readArray(j, "scenes", d.scenes);
  if (j.contains("scenes") && j["scenes"].is_array())
    for (size_t i = 0; i < d.scenes.size() && i < j["scenes"].size(); ++i)
      if (j["scenes"][i].contains("look")) migrateLook(j["scenes"][i]["look"], d.scenes[i].look);
  if (j.contains("cueLists")) d.cueLists = j["cueLists"].get<std::vector<CueList>>();
  d.activeList = j.value("activeList", 0);
  if (j.contains("dmx")) d.dmx = j["dmx"].get<DmxSettings>();
  if (j.value("touchfxShow", 1) < 2) migrateLightning(d);
}

static void readMachinePart(const json& j, AppState& d) {
  if (j.contains("output")) d.output = j["output"].get<OutputSettings>();
  if (j.contains("external")) d.external = j["external"].get<ExternalSettings>();
  if (j.contains("layout")) d.layout = j["layout"].get<LayoutSettings>();
  if (j.contains("audio")) d.audio = j["audio"].get<AudioSettings>();
  if (j.contains("videoIn")) d.videoIn = j["videoIn"].get<VideoInSettings>();
  d.showMode = j.value("showMode", d.showMode);
  d.dmxAdapter = j.value("dmxAdapter", d.dmxAdapter);
  d.dmxAdapterLabel = j.value("dmxAdapterLabel", d.dmxAdapterLabel);
  d.lastShow = j.value("lastShow", d.lastShow);
  d.blockGestures = j.value("blockGestures", d.blockGestures);
  d.gestureRestore = j.value("gestureRestore", d.gestureRestore);
  d.maxTouches = j.value("maxTouches", d.maxTouches);
  d.effectFade = j.value("effectFade", d.effectFade);
  d.showTouches = j.value("showTouches", d.showTouches);
  d.settingsTab = j.value("settingsTab", d.settingsTab);
  d.uiScale = j.value("uiScale", d.uiScale);
  d.fullscreen = j.value("fullscreen", d.fullscreen);
  d.tab = j.value("tab", d.tab);
}

static bool readJson(const std::string& path, json& j) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  try {
    j = json::parse(f);
    return true;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Could not read %s: %s\n", path.c_str(), e.what());
    return false;
  }
}

bool loadShow(const std::string& path, AppState& s) {
  json j;
  if (!readJson(path, j)) return false;
  try {
    AppState d = s;
    AppState def = defaultState();
    d.look = def.look;
    d.palettes = def.palettes;
    d.scenes = def.scenes;
    d.cueLists.clear();
    d.dmx = def.dmx;
    readShowPart(j, d);
    sanitize(d);
    s = d;
    return true;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Bad show file %s: %s\n", path.c_str(), e.what());
    return false;
  }
}

bool loadMachine(const std::string& path, AppState& s) {
  json j;
  if (!readJson(path, j)) return false;
  try {
    AppState d = s;
    readMachinePart(j, d);
    sanitize(d);
    s = d;
    return true;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Bad settings file %s: %s\n", path.c_str(), e.what());
    return false;
  }
}

bool fileHasShowData(const std::string& path) {
  json j;
  return readJson(path, j) && (j.contains("look") || j.contains("scenes"));
}

bool loadState(const std::string& path, AppState& s) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  try {
    json j = json::parse(f);
    AppState d = defaultState();
    // Files from before per-effect release: every effect starts from the old global setting.
    auto migrate = [](const json& lj, Look& l) {
      if (!lj.contains("releaseFx")) l.releaseFx.assign(FX_COUNT, l.release);
      if (!lj.contains("fadeFx")) l.fadeFx.assign(FX_COUNT, l.fadeTime);
    };
    if (j.contains("look")) {
      d.look = j["look"].get<Look>();
      migrate(j["look"], d.look);
    }
    readArray(j, "palettes", d.palettes);
    readArray(j, "scenes", d.scenes);
    if (j.contains("scenes") && j["scenes"].is_array())
      for (size_t i = 0; i < d.scenes.size() && i < j["scenes"].size(); ++i)
        if (j["scenes"][i].contains("look")) migrate(j["scenes"][i]["look"], d.scenes[i].look);
    if (j.contains("output")) d.output = j["output"].get<OutputSettings>();
    if (j.contains("external")) d.external = j["external"].get<ExternalSettings>();
    if (j.contains("layout")) d.layout = j["layout"].get<LayoutSettings>();
    if (j.contains("audio")) d.audio = j["audio"].get<AudioSettings>();
    if (j.contains("dmx")) d.dmx = j["dmx"].get<DmxSettings>();
    migrateLightning(d);  // old settings.json: always from before 0.8
    d.blockGestures = j.value("blockGestures", d.blockGestures);
    d.gestureRestore = j.value("gestureRestore", d.gestureRestore);
    d.maxTouches = j.value("maxTouches", d.maxTouches);
    d.effectFade = j.value("effectFade", d.effectFade);
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

static json lerpJson(const json& a, const json& b, float t) {
  if (a.is_object() && b.is_object()) {
    json o = b;
    for (auto it = b.begin(); it != b.end(); ++it)
      if (a.contains(it.key())) o[it.key()] = lerpJson(a[it.key()], it.value(), t);
    return o;
  }
  if (a.is_array() && b.is_array() && a.size() == b.size()) {
    json o = json::array();
    for (size_t i = 0; i < b.size(); ++i) o.push_back(lerpJson(a[i], b[i], t));
    return o;
  }
  if (a.is_number_float() && b.is_number_float()) return a.get<double>() + (b.get<double>() - a.get<double>()) * t;
  return b;
}

std::string lookToText(const Look& l) { return json(l).dump(); }

bool lookFromText(const std::string& text, Look& l) {
  try {
    Look r = json::parse(text).get<Look>();
    sanitizeLook(r);
    l = r;
    return true;
  } catch (...) {
    return false;
  }
}

std::string lookBlendText(const std::string& from, const std::string& to, float t) {
  try {
    return lerpJson(json::parse(from), json::parse(to), t).dump();
  } catch (...) {
    return to;
  }
}

std::string lookFingerprint(const Look& l) {
  json j = l;
  j.erase("bpm");
  return j.dump();
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

const char* const kAudioRangeNames[RANGE_COUNT] = {"KICK", "BASS", "LOW-MIDS", "VOCALS", "HI-HATS", "FULL", "CUSTOM"};
void audioRange(const AudioSettings& as, float& lo, float& hi) {
  static const float r[RANGE_COUNT - 1][2] = {{40, 120}, {40, 250}, {250, 800}, {300, 3400}, {6000, 16000}, {30, 16000}};
  if (as.range >= 0 && as.range < RANGE_CUSTOM) {
    lo = r[as.range][0];
    hi = r[as.range][1];
  } else {
    lo = as.rangeLo;
    hi = std::max(as.rangeHi, as.rangeLo + 10.f);
  }
}
