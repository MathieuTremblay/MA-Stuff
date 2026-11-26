// Maps the received DMX channels onto TouchFX (see docs/DMX.md for the channel chart).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "app.hpp"
#include "cues.hpp"
#include "dmx_profile.inc"
#include "log.hpp"

static_assert(DMX_FN_COUNT <= 64, "App::dmxLast is too small");
static_assert(D2_COUNT <= 128, "App::dmxLast2 is too small");

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

int dmxFootprint(int mode) {
  return mode == 0 ? kDmxBasicFootprint : mode == 1 ? kDmxAdvancedFootprint : kDmxFullFootprint;
}

namespace {

// ---- Full v2 ---------------------------------------------------------------------------------------------
// Trigger channels: ranges of 40 from 10; fires once when the value enters a range.
int trigRange(int v, int count) {
  if (v < 10) return -1;
  int r = (v - 10) / 40;
  return r < count ? r : -1;
}

void applyV2(App& a, float dt) {
  const DmxSettings& d = a.st.dmx;
  int vals[D2_COUNT];
  bool present[D2_COUNT] = {};
  for (const DmxChannelDef2& c : kDmxFull) {
    int addr = d.address - 1 + c.offset;
    if (addr + c.bytes > 512) continue;
    vals[c.fn] = c.bytes == 2 ? (a.dmxFrame[addr] << 8) | a.dmxFrame[addr + 1] : a.dmxFrame[addr];
    present[c.fn] = true;
  }
  auto changed = [&](DmxFn2 f) { return present[f] && (d.lock || !a.dmxPrimed || vals[f] != a.dmxLast2[f]); };
  auto slot = [&](DmxFn2 f, int n) { return present[f] ? dmx2Slot(vals[f], n) : -1; };
  Look& l = a.st.look;
  BgLayer& bg = l.bg;

  // ---- master: continuous ----
  a.dmxDim = present[D2_M_DIMMER] ? vals[D2_M_DIMMER] / 255.f : 1.f;
  a.dmxStrobeHz = 0;
  if (present[D2_M_SHUTTER]) {
    int v = vals[D2_M_SHUTTER];
    if (v >= 32 && v <= 223) a.dmxStrobeHz = 1.f + (v - 32) / 191.f * 24.f;
  }
  a.dmxBlackout = false;
  a.dmxBpm = present[D2_M_BPM] && vals[D2_M_BPM] >= 20;  // 0-19: TouchFX keeps its own tempo
  if (a.dmxBpm && changed(D2_M_BPM)) l.bpm = float(vals[D2_M_BPM]);
  if (present[D2_LOW_R]) {
    auto rgb = [&](DmxFn2 r, DmxFn2 g, DmxFn2 b) { return Color3{vals[r] / 255.f, vals[g] / 255.f, vals[b] / 255.f}; };
    a.dmxPalette.name = "DMX";
    a.dmxPalette.low = rgb(D2_LOW_R, D2_LOW_G, D2_LOW_B);
    a.dmxPalette.mid = rgb(D2_MID_R, D2_MID_G, D2_MID_B);
    a.dmxPalette.high = rgb(D2_HIGH_R, D2_HIGH_G, D2_HIGH_B);
  }
  const DmxFn2 cur[4][3] = {{D2_C1_DIM, D2_C1_X, D2_C1_Y}, {D2_C2_DIM, D2_C2_X, D2_C2_Y},
                            {D2_C3_DIM, D2_C3_X, D2_C3_Y}, {D2_C4_DIM, D2_C4_X, D2_C4_Y}};
  for (int k = 0; k < 4; ++k) {
    App::Cursor& c = a.dmxCursor[k];
    bool on = present[cur[k][0]] && vals[cur[k][0]] >= 3;
    Vec2 p = present[cur[k][1]] ? Vec2(vals[cur[k][1]] / 65535.f, vals[cur[k][2]] / 65535.f) : Vec2(0.5f, 0.5f);
    if (on && !c.active) c.lastPos = c.pos = p;
    c.active = on;
    c.pos = p;
  }

  // ---- master: songs, cues, scenes (fire when the value changes) ----
  if (changed(D2_M_SONG) && vals[D2_M_SONG] >= 1 && vals[D2_M_SONG] <= int(a.st.cueLists.size()) && a.dmxPrimed)
    cueSelectList(a, vals[D2_M_SONG] - 1);
  if (changed(D2_M_CUE) && vals[D2_M_CUE] > 0 && a.dmxPrimed && !a.st.cueLists.empty()) {
    float num = vals[D2_M_CUE] / 10.f;
    int i = cueFindNumber(a.st.cueLists[std::clamp(a.st.activeList, 0, int(a.st.cueLists.size()) - 1)], num);
    if (i >= 0) cueFire(a, i);
    else logLine("DMX: cue %.1f is not in the current song", num);
  }
  if (changed(D2_M_SCENE) && a.dmxPrimed) {
    int sc = vals[D2_M_SCENE] - 1;
    bool valid = sc >= 0 && sc < kNumScenes && a.st.scenes[sc].used;
    // a FLASH scene stays on while the channel stays on it; leaving it puts things back
    if (a.flashFromDmx && (!valid || sc != a.flashScene)) a.endFlash();
    if (valid) {
      a.recallScene(sc);
      if (a.st.scenes[sc].flash) a.flashFromDmx = true;
    }
  }
  int cr = present[D2_M_CUECMD] ? trigRange(vals[D2_M_CUECMD], 5) : -1;
  if (cr != a.dmxTrig[0] && cr >= 0 && a.dmxPrimed) {
    switch (cr) {
      case 0: cueCommand(a, CUE_GO); break;
      case 1: cueCommand(a, CUE_BACK); break;
      case 2: cueCommand(a, CUE_SONG_NEXT); break;
      case 3: cueCommand(a, CUE_SONG_PREV); break;
      default: cueSelectList(a, a.st.activeList); cueCommand(a, CUE_GO); break;  // restart: cue 1
    }
  }
  a.dmxTrig[0] = cr;
  int ar = present[D2_M_ACTION] ? trigRange(vals[D2_M_ACTION], 3) : -1;
  if (ar != a.dmxTrig[1] && ar >= 0 && a.dmxPrimed) {
    if (ar == 0) a.wantClear = true;
    else if (ar == 1) {
      a.wantClear = true;
      a.wantClearBg = true;
    } else {
      a.tempo.tap(a.time, l.bpm);
    }
  }
  a.dmxTrig[1] = ar;
  int s;
  if (changed(D2_M_AUDIO) && (s = slot(D2_M_AUDIO, 2)) >= 0) a.st.audio.enabled = s == 1;
  if (changed(D2_M_AUTOPLAY) && (s = slot(D2_M_AUTOPLAY, 2)) >= 0) a.st.audio.autoPlay = s == 1;
  if (changed(D2_M_FREEZE) && (s = slot(D2_M_FREEZE, 2)) >= 0) a.video.setFrozen(s == 1);
  if (changed(D2_M_GLOW)) l.post.bloom = vals[D2_M_GLOW] / 255.f;
  if (changed(D2_M_GLOWSIZE)) l.post.bloomSize = vals[D2_M_GLOWSIZE] / 255.f;
  if (changed(D2_M_SAT)) l.post.saturation = vals[D2_M_SAT] / 255.f;
  if (changed(D2_M_BRIGHT)) l.brightness = vals[D2_M_BRIGHT] / 255.f * 2.f;

  // ---- front layer ----
  if (changed(D2_F_EFFECT) && (s = slot(D2_F_EFFECT, kDmx2FxCount)) >= 0 && kDmx2Effects[s] != l.effect) {
    l.effect = kDmx2Effects[s];
  }
  const DmxFn2 fch[3] = {D2_F_MODE, D2_F_OPT1, D2_F_OPT2};
  for (int w = 0; w < 3; ++w)
    if (changed(fch[w]))  // a new effect keeps its own settings until a value changes
      dmx2SetChoice(l, l.effect, w, slot(fch[w], dmx2ChoiceCount(l.effect, w)));
  const DmxFn2 fp[10] = {D2_F_P1, D2_F_P2, D2_F_P3, D2_F_P4, D2_F_P5, D2_F_P6, D2_F_P7, D2_F_P8, D2_F_P9, D2_F_P10};
  for (int i = 0; i < 10; ++i)
    if (changed(fp[i])) dmx2SetParam(l, l.effect, i, vals[fp[i]]);
  if (changed(D2_F_RELEASE) && (s = slot(D2_F_RELEASE, 3)) >= 0) l.releaseFx[l.effect] = s;
  if (changed(D2_F_FADETIME))
    l.fadeFx[l.effect] = std::max(0.05f, vals[D2_F_FADETIME] / 255.f * 10.f);
  if (changed(D2_F_SPEED) && (s = slot(D2_F_SPEED, kNumSpeedMults)) >= 0) l.speedMult = s;
  if (changed(D2_F_MIRROR) && (s = slot(D2_F_MIRROR, MIRROR_COUNT)) >= 0) l.mirror = s;
  if (changed(D2_F_PALETTE)) {
    int v = vals[D2_F_PALETTE];
    if ((s = dmx2Slot(v, kNumPalettes)) >= 0) {
      l.palette = s;
      l.useCustom = false;
      a.dmxColors = false;
    } else if (v >= 250) {
      a.dmxColors = true;
    }
  }
  if (changed(D2_F_CMODE) && (s = slot(D2_F_CMODE, COLOR_COUNT)) >= 0) l.colorMode = s;

  // ---- back layer (its own copy of the effect settings) ----
  if (changed(D2_B_LEVEL)) {
    int v = vals[D2_B_LEVEL];
    bg.on = v > 0;
    if (v > 0) bg.level = v / 255.f;
  }
  {
    Look t;
    paramsToLook(bg.fx, t);
    t.effect = bg.effect;
    if (changed(D2_B_EFFECT) && (s = slot(D2_B_EFFECT, kDmx2FxCount)) >= 0 && kDmx2Effects[s] != t.effect) {
      t.effect = kDmx2Effects[s];
    }
    const DmxFn2 bch[3] = {D2_B_MODE, D2_B_OPT1, D2_B_OPT2};
    for (int w = 0; w < 3; ++w)
      if (changed(bch[w]))
        dmx2SetChoice(t, t.effect, w, slot(bch[w], dmx2ChoiceCount(t.effect, w)));
    const DmxFn2 bp[10] = {D2_B_P1, D2_B_P2, D2_B_P3, D2_B_P4, D2_B_P5, D2_B_P6, D2_B_P7, D2_B_P8, D2_B_P9, D2_B_P10};
    for (int i = 0; i < 10; ++i)
      if (changed(bp[i])) dmx2SetParam(t, t.effect, i, vals[bp[i]]);
    lookToParams(t, bg.fx);
    bg.effect = t.effect;
  }
  if (changed(D2_B_BLEND) && (s = slot(D2_B_BLEND, BLEND_COUNT)) >= 0) bg.blend = s;
  if (changed(D2_B_MOTION)) bg.motion = vals[D2_B_MOTION] / 255.f;
  if (changed(D2_B_FOLLOW) && (s = slot(D2_B_FOLLOW, BGSRC_COUNT)) >= 0) {
    bg.source = s;
    bg.followTouch = s != BGSRC_AUTO;
  }
  if (changed(D2_B_SPEED) && (s = slot(D2_B_SPEED, kNumSpeedMults)) >= 0) bg.speedMult = s;
  if (changed(D2_B_MIRROR) && (s = slot(D2_B_MIRROR, MIRROR_COUNT)) >= 0) bg.mirror = s;
  if (changed(D2_B_PALETTE) || changed(D2_B_CMODE)) {
    // palette: same as front / palette 1-10 / DMX colours; colour mode: same as front / a mode
    int pv = present[D2_B_PALETTE] ? vals[D2_B_PALETTE] : 0, cv = present[D2_B_CMODE] ? vals[D2_B_CMODE] : 0;
    int ps = dmx2Slot(pv, 1 + kNumPalettes), cs = dmx2Slot(cv, 1 + COLOR_COUNT);
    bool dmxCol = pv >= 250;
    if (ps >= 0 || cs >= 0 || dmxCol) {
      bool own = dmxCol || ps >= 1 || cs >= 1;
      bg.ownColors = own;
      a.dmxColorsBack = dmxCol;
      if (own) {
        bg.palette = ps >= 1 ? ps - 1 : l.palette;
        bg.colorMode = cs >= 1 ? cs - 1 : l.colorMode;
      }
    }
  }

  for (int f = 0; f < D2_COUNT; ++f)
    if (present[f]) a.dmxLast2[f] = vals[f];
  a.dmxPrimed = true;
  if (a.dmxStrobeHz > 0) a.dmxStrobePhase = std::fmod(a.dmxStrobePhase + dt * a.dmxStrobeHz, 1.0);
  else a.dmxStrobePhase = 0;
}

}  // namespace

void applyDmx(App& a, float dt) {
  const DmxSettings& d = a.st.dmx;
  int universe = d.protocol == DMXP_ARTNET ? d.artnetUniverse : d.sacnUniverse;
  // network card (a PC setting): all cards, or only the chosen one
  uint32_t adIp = 0, adMask = 0;
  a.dmxAdapterMissing = false;
  int protocol = d.protocol;
  if (!a.st.dmxAdapter.empty()) {
    if (const NetAdapter* n = findAdapter(a.adapters, a.st.dmxAdapter)) {
      adIp = n->ip;
      adMask = n->mask;
    } else {
      a.dmxAdapterMissing = true;  // keep waiting for that card rather than listening elsewhere
      protocol = DMXP_OFF;
    }
  }
  if (a.dmxRunProtocol != protocol || a.dmxRunUniverse != universe || a.dmxRunAdapterIp != adIp) {
    a.dmx.start(protocol, universe, adIp, adMask);
    a.dmxRunProtocol = protocol;
    a.dmxRunUniverse = universe;
    a.dmxRunAdapterIp = adIp;
    a.dmxPrimed = false;
  }
  auto reset = [&] {
    a.dmxActive = false;
    a.dmxDim = 1;
    a.dmxStrobeHz = 0;
    a.dmxBlackout = false;
    a.dmxColors = false;
    a.dmxBpm = false;
    for (auto& c : a.dmxCursor) c.active = false;
    a.dmxTrig[0] = a.dmxTrig[1] = -1;
  };
  if (protocol == DMXP_OFF || !a.dmx.snapshot(a.dmxFrame)) {
    reset();
    a.dmxPrimed = false;
    return;
  }
  a.dmxActive = true;  // keeps the last received values if the console goes quiet
  if (d.mode == 2) {
    applyV2(a, dt);
    return;
  }
  a.dmxColorsBack = false;

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
  if (changed(DMX_EFFECT)) {  // the new effect keeps its own settings until a param value changes
    int s = slotOf(vals[DMX_EFFECT], kDmxEffects);
    if (s >= 0 && s != l.effect) l.effect = s;
  }
  if (changed(DMX_SCENE)) {
    int s = slotOf(vals[DMX_SCENE], kNumScenes);
    bool valid = s >= 0 && a.st.scenes[s].used;
    if (a.flashFromDmx && (!valid || s != a.flashScene)) a.endFlash();
    if (valid) {
      a.recallScene(s);
      if (a.st.scenes[s].flash) a.flashFromDmx = true;
    }
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
  if (changed(DMX_FXMODE)) {
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
    if (changed(params[i])) setParam(l, i, vals[params[i]]);
  if (changed(DMX_GLOW)) l.post.bloom = vals[DMX_GLOW] / 255.f;
  if (changed(DMX_SATURATION)) l.post.saturation = vals[DMX_SATURATION] / 255.f;
  if (changed(DMX_CONTROL) && a.dmxPrimed) {  // commands fire when the value enters their range
    int v = vals[DMX_CONTROL];
    if (v >= 10 && v <= 29) a.wantClear = true;
    else if (v >= 50 && v <= 69) a.st.audio.enabled = true;
    else if (v >= 70 && v <= 89) a.st.audio.enabled = false;
    else if (v >= 90 && v <= 109) a.st.audio.autoPlay = true;
    else if (v >= 110 && v <= 129) a.st.audio.autoPlay = false;
    else if (v >= 130 && v <= 149) l.releaseFx[l.effect] = RELEASE_FADE;
    else if (v >= 150 && v <= 169) l.releaseFx[l.effect] = RELEASE_SNAP;
  }

  for (int f = 0; f < DMX_FN_COUNT; ++f)
    if (present[f]) a.dmxLast[f] = vals[f];
  a.dmxPrimed = true;

  // shutter strobe phase
  if (a.dmxStrobeHz > 0) a.dmxStrobePhase = std::fmod(a.dmxStrobePhase + dt * a.dmxStrobeHz, 1.0);
  else a.dmxStrobePhase = 0;
}
