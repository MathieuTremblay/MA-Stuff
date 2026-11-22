// Maps the received DMX channels onto TouchFX (see docs/DMX.md for the channel chart).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app.hpp"
#include "dmx_profile.inc"

static_assert(DMX_FN_COUNT <= 64, "App::dmxLast is too small");

namespace {

// 0-9 / 250-255 = keep (-1), otherwise the slot index.
int slotOf(int v, int count) {
  if (v < kDmxSlotStart || v > kDmxSlotEnd) return -1;
  int size = (kDmxSlotEnd - kDmxSlotStart + 1) / count;
  int i = (v - kDmxSlotStart) / size;
  return i < count ? i : -1;
}

// Param 1-4 of each effect (same table as tools/dmx_profile.py EFFECT_PARAMS).
void setParam(Look& l, int idx, int v) {
  float t = v / 255.f;
  switch (l.effect) {
    case FX_FLUID: {
      float* p[4] = {&l.fluid.size, &l.fluid.force, &l.fluid.swirl, &l.fluid.trail};
      *p[idx] = t;
      break;
    }
    case FX_PARTICLES: {
      float* p[4] = {&l.particles.amount, &l.particles.size, &l.particles.speed, &l.particles.trail};
      *p[idx] = t;
      break;
    }
    case FX_SPARKLES: {
      float* p[4] = {&l.sparkles.amount, &l.sparkles.size, &l.sparkles.twinkle, &l.sparkles.spread};
      *p[idx] = t;
      break;
    }
    case FX_KALEIDO: {
      float* p[4] = {&l.kaleido.brush, &l.kaleido.rotation, &l.kaleido.zoom, &l.kaleido.trail};
      *p[idx] = t;
      break;
    }
    case FX_LASERS: {
      float* p[4] = {&l.lasers.spread, &l.lasers.rotation, &l.lasers.swing, &l.lasers.width};
      *p[idx] = t;
      break;
    }
    case FX_STROBE:
      if (idx == 0) l.strobe.duty = t;
      else if (idx == 1) l.strobe.size = t;
      else if (idx == 2) l.strobe.rate = std::min(kNumDivisions - 1, int(t * kNumDivisions));
      break;
  }
}

void setMode(Look& l, int slot) {
  switch (l.effect) {
    case FX_LASERS:
      if (slot < LASER_COUNT) l.lasers.mode = slot;
      break;
    case FX_STROBE:
      if (slot < STROBE_COUNT) l.strobe.shape = slot;
      break;
    case FX_PARTICLES:
      if (slot < PSTROBE_COUNT) l.particles.strobe = slot;
      break;
    default: break;
  }
}

}  // namespace

int dmxFootprint(int mode) { return mode == 0 ? kDmxBasicFootprint : kDmxAdvancedFootprint; }

void applyDmx(App& a, float dt) {
  const DmxSettings& d = a.st.dmx;
  int universe = d.protocol == DMXP_ARTNET ? d.artnetUniverse : d.sacnUniverse;
  if (a.dmxRunProtocol != d.protocol || a.dmxRunUniverse != universe) {
    a.dmx.start(d.protocol, universe);
    a.dmxRunProtocol = d.protocol;
    a.dmxRunUniverse = universe;
    a.dmxPrimed = false;
  }
  auto reset = [&] {
    a.dmxActive = false;
    a.dmxDim = 1;
    a.dmxStrobeHz = 0;
    a.dmxBlackout = false;
    a.dmxColors = false;
    a.dmxBpm = false;
    a.dmxCursor[0].active = a.dmxCursor[1].active = false;
  };
  if (d.protocol == DMXP_OFF || !a.dmx.snapshot(a.dmxFrame)) {
    reset();
    a.dmxPrimed = false;
    return;
  }
  a.dmxActive = true;  // keeps the last received values if the console goes quiet

  const DmxChannelDef* defs = d.mode == 0 ? kDmxBasic : kDmxAdvanced;
  int n = d.mode == 0 ? int(sizeof(kDmxBasic) / sizeof(kDmxBasic[0]))
                      : int(sizeof(kDmxAdvanced) / sizeof(kDmxAdvanced[0]));
  int vals[DMX_FN_COUNT];
  bool present[DMX_FN_COUNT] = {};
  for (int i = 0; i < n; ++i) {
    int addr = d.address - 1 + defs[i].offset;
    if (addr + defs[i].bytes > 512) continue;  // footprint runs past the end of the universe
    int v = defs[i].bytes == 2 ? (a.dmxFrame[addr] << 8) | a.dmxFrame[addr + 1] : a.dmxFrame[addr];
    vals[defs[i].fn] = v;
    present[defs[i].fn] = true;
  }
  auto changed = [&](DmxFn f) { return present[f] && (d.lock || !a.dmxPrimed || vals[f] != a.dmxLast[f]); };
  Look& l = a.st.look;

  // ---- continuous: always follow DMX ----
  a.dmxDim = present[DMX_DIMMER] ? vals[DMX_DIMMER] / 255.f : 1.f;
  a.dmxStrobeHz = 0;
  if (present[DMX_SHUTTER]) {
    int v = vals[DMX_SHUTTER];
    if (v >= 32 && v <= 223) a.dmxStrobeHz = 1.f + (v - 32) / 191.f * 24.f;
  }
  a.dmxBlackout = present[DMX_CONTROL] && vals[DMX_CONTROL] >= 30 && vals[DMX_CONTROL] <= 49;
  if (present[DMX_LOW_R]) {
    auto rgb = [&](DmxFn r, DmxFn g, DmxFn b) { return Color3{vals[r] / 255.f, vals[g] / 255.f, vals[b] / 255.f}; };
    a.dmxPalette.name = "DMX";
    a.dmxPalette.low = rgb(DMX_LOW_R, DMX_LOW_G, DMX_LOW_B);
    a.dmxPalette.mid = rgb(DMX_MID_R, DMX_MID_G, DMX_MID_B);
    a.dmxPalette.high = rgb(DMX_HIGH_R, DMX_HIGH_G, DMX_HIGH_B);
  }
  const DmxFn cur[2][3] = {{DMX_CUR1_DIM, DMX_CUR1_X, DMX_CUR1_Y}, {DMX_CUR2_DIM, DMX_CUR2_X, DMX_CUR2_Y}};
  for (int k = 0; k < 2; ++k) {
    App::Cursor& c = a.dmxCursor[k];
    bool on = present[cur[k][0]] && vals[cur[k][0]] >= 3;
    Vec2 p = present[cur[k][1]] ? Vec2(vals[cur[k][1]] / 65535.f, vals[cur[k][2]] / 65535.f) : Vec2(0.5f, 0.5f);
    if (on && !c.active) c.lastPos = c.pos = p;
    c.active = on;
    c.pos = p;
  }

  // ---- discrete: last change wins (the panel keeps control until the console changes the value) ----
  bool effectChanged = false;
  if (changed(DMX_EFFECT)) {
    int s = slotOf(vals[DMX_EFFECT], FX_COUNT);
    if (s >= 0 && s != l.effect) {
      l.effect = s;
      effectChanged = true;
    }
  }
  if (changed(DMX_SCENE)) {
    int s = slotOf(vals[DMX_SCENE], kNumScenes);
    if (s >= 0) a.recallScene(s);
  }
  for (DmxFn f : {DMX_PALETTE, DMX_PALETTE_ADV}) {
    if (!changed(f)) continue;
    int v = vals[f];
    int s = slotOf(v, kNumPalettes);
    if (s >= 0) {
      l.palette = s;
      a.dmxColors = false;
    } else if (f == DMX_PALETTE_ADV && v >= 250) {
      a.dmxColors = true;
    } else if (f == DMX_PALETTE_ADV) {
      a.dmxColors = false;
    }
  }
  if (changed(DMX_FXMODE) || (effectChanged && present[DMX_FXMODE])) {
    int s = slotOf(vals[DMX_FXMODE], 8);
    if (s >= 0) setMode(l, s);
  }
  if (changed(DMX_MIRROR)) {
    int s = slotOf(vals[DMX_MIRROR], MIRROR_COUNT);
    if (s >= 0) l.mirror = s;
  }
  a.dmxBpm = present[DMX_BPM] && vals[DMX_BPM] >= 256;
  if (changed(DMX_BPM) && a.dmxBpm)
    l.bpm = std::round((20.f + (vals[DMX_BPM] - 256) / float(65535 - 256) * 280.f) * 10.f) / 10.f;
  if (changed(DMX_SPEED)) {
    int s = slotOf(vals[DMX_SPEED], kNumSpeedMults);
    if (s >= 0) l.speedMult = s;
  }
  const DmxFn params[4] = {DMX_PARAM1, DMX_PARAM2, DMX_PARAM3, DMX_PARAM4};
  for (int i = 0; i < 4; ++i)
    if (changed(params[i]) || (effectChanged && present[params[i]])) setParam(l, i, vals[params[i]]);
  if (changed(DMX_GLOW)) l.post.bloom = vals[DMX_GLOW] / 255.f;
  if (changed(DMX_SATURATION)) l.post.saturation = vals[DMX_SATURATION] / 255.f;
  if (changed(DMX_CONTROL) && a.dmxPrimed) {  // commands fire when the value enters their range
    int v = vals[DMX_CONTROL];
    if (v >= 10 && v <= 29) a.wantClear = true;
    else if (v >= 50 && v <= 69) a.st.audio.enabled = true;
    else if (v >= 70 && v <= 89) a.st.audio.enabled = false;
    else if (v >= 90 && v <= 109) a.st.audio.autoPlay = true;
    else if (v >= 110 && v <= 129) a.st.audio.autoPlay = false;
    else if (v >= 130 && v <= 149) l.release = RELEASE_FADE;
    else if (v >= 150 && v <= 169) l.release = RELEASE_SNAP;
  }

  for (int f = 0; f < DMX_FN_COUNT; ++f)
    if (present[f]) a.dmxLast[f] = vals[f];
  a.dmxPrimed = true;

  // shutter strobe phase
  if (a.dmxStrobeHz > 0) a.dmxStrobePhase = std::fmod(a.dmxStrobePhase + dt * a.dmxStrobeHz, 1.0);
  else a.dmxStrobePhase = 0;
}
