// Touch-friendly control panel (Dear ImGui).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>

#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "platform.hpp"
#include "log.hpp"
#include "show.hpp"
#include "ui_widgets.hpp"

extern const unsigned char kFontData[];
extern const std::size_t kFontSize;

namespace ui {

float S() { return ImGui::GetFontSize() / 19.f; }  // current UI scale

ImU32 col32(const Color3& c, float a) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], a));
}

// A big button that lights up when "on".
bool Toggle(const char* label, bool on, ImVec2 size, ImVec4 onColor) {
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, onColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, onColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, onColor);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.f));
  }
  bool r = ImGui::Button(label, size);
  if (on) ImGui::PopStyleColor(4);
  return r;
}

// Row/grid of mutually exclusive buttons. Returns true when the value changed.
bool Choice(const char* id, const char* const* labels, int n, int* value, int cols, float h) {
  ImGui::PushID(id);
  if (cols <= 0) cols = n;
  if (h <= 0) h = 46.f * S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = (ImGui::GetContentRegionAvail().x - sp * (cols - 1)) / cols;
  bool changed = false;
  for (int i = 0; i < n; ++i) {
    if (i % cols) ImGui::SameLine();
    ImGui::PushID(i);
    if (Toggle(labels[i], *value == i, ImVec2(w, h))) {
      changed = *value != i;
      *value = i;
    }
    ImGui::PopID();
  }
  ImGui::PopID();
  return changed;
}

bool OnOff(const char* label, bool* v, float h) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s: %s###%s", label, *v ? "ON" : "OFF", label);
  if (Toggle(buf, *v, ImVec2(-1, h > 0 ? h : 46 * S()))) {
    *v = !*v;
    return true;
  }
  return false;
}

void SectionLabel(const char* text) {
  ImGui::Dummy(ImVec2(0, 3.f * S()));
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

void Hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 2 ? ImGui::GetIO().Fonts->Fonts[2] : nullptr);
  ImGui::TextWrapped("%s", text);
  ImGui::PopFont();
  ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Touch slider: drag sideways to change the value (relative, no jump), drag up/down to scroll
// the panel, tap to jump to a value.
struct SliderDrag {
  ImGuiID id = 0;
  ImVec2 start;
  float startV = 0;
  int mode = 0;  // 0 undecided, 1 sliding, 2 scrolling
};
SliderDrag g_slider;
bool g_sliding = false;  // a slider is being dragged sideways this frame

bool TouchSlider(const char* label, float* v, float lo, float hi, const char* valueText) {
  ImGuiIO& io = ImGui::GetIO();
  float s = S();
  ImVec2 size(ImGui::GetContentRegionAvail().x, 44.f * s);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGuiID id = ImGui::GetID(label);
  ImGui::InvisibleButton(label, size);
  bool changed = false;
  if (ImGui::IsItemActivated()) g_slider = SliderDrag{id, io.MousePos, *v, 0};
  if (ImGui::IsItemActive() && g_slider.id == id) {
    ImVec2 d(io.MousePos.x - g_slider.start.x, io.MousePos.y - g_slider.start.y);
    float th = 8.f * s;
    if (g_slider.mode == 0) {
      if (std::fabs(d.x) > th && std::fabs(d.x) > std::fabs(d.y)) {
        g_slider.mode = 1;
        g_slider.start = io.MousePos;
        g_slider.startV = *v;
      } else if (std::fabs(d.y) > th) {
        g_slider.mode = 2;
      }
    }
    if (g_slider.mode == 1) {
      g_sliding = true;
      float nv = std::clamp(g_slider.startV + (io.MousePos.x - g_slider.start.x) / size.x * (hi - lo), lo, hi);
      if (nv != *v) {
        *v = nv;
        changed = true;
      }
    }
  }
  if (ImGui::IsItemDeactivated() && g_slider.id == id) {
    if (g_slider.mode == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
      float t = std::clamp((io.MousePos.x - p.x) / size.x, 0.f, 1.f);
      *v = lo + t * (hi - lo);
      changed = true;
    }
    g_slider.id = 0;
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  float r = 6.f * s;
  ImVec2 p1(p.x + size.x, p.y + size.y);
  dl->AddRectFilled(p, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), r);
  float t = (hi > lo) ? std::clamp((*v - lo) / (hi - lo), 0.f, 1.f) : 0.f;
  bool active = ImGui::IsItemActive() && g_slider.mode == 1;
  ImVec4 fill = active ? ImVec4(1.f, 0.55f, 0.2f, 0.85f) : ImVec4(1.f, 0.47f, 0.1f, 0.62f);
  if (t > 0.f) dl->AddRectFilled(p, ImVec2(p.x + size.x * t, p1.y), ImGui::GetColorU32(fill), r);
  dl->AddLine(ImVec2(p.x + size.x * t, p.y + 4 * s), ImVec2(p.x + size.x * t, p1.y - 4 * s), IM_COL32(255, 255, 255, 200),
              2.f * s);
  const char* end = ImGui::FindRenderedTextEnd(label);
  float ty = p.y + (size.y - ImGui::GetFontSize()) * 0.5f;
  dl->AddText(ImVec2(p.x + 12 * s, ty), ImGui::GetColorU32(kText), label, end);
  ImVec2 vs = ImGui::CalcTextSize(valueText);
  dl->AddText(ImVec2(p1.x - vs.x - 12 * s, ty), ImGui::GetColorU32(kText), valueText);
  return changed;
}

bool Pct(const char* label, float* v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.0f %%", *v * 100.f);
  return TouchSlider(label, v, 0.f, 1.f, buf);
}

bool Centered(const char* label, float* v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%+.0f", (*v - 0.5f) * 200.f);
  return TouchSlider(label, v, 0.f, 1.f, buf);
}

bool IntSlider(const char* label, int* v, int lo, int hi) {
  float f = float(*v);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d", *v);
  if (TouchSlider(label, &f, float(lo), float(hi) + 0.999f, buf)) {
    int nv = std::clamp(int(std::floor(f)), lo, hi);
    bool ch = nv != *v;
    *v = nv;
    return ch;
  }
  return false;
}

// Swipe scrolling with momentum for the current window, even when the swipe starts on a button
// or a slider (a vertical swipe on a slider scrolls instead of changing it).
struct ScrollState {
  bool pressed = false, scrolling = false;
  float startScroll = 0, vel = 0;
};
std::unordered_map<ImGuiID, ScrollState> g_scroll;

void TouchScroll() {
  ImGuiIO& io = ImGui::GetIO();
  ScrollState& st = g_scroll[ImGui::GetID("##touchscroll")];
  bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    st.pressed = hovered && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    st.scrolling = false;
    st.startScroll = ImGui::GetScrollY();
    st.vel = 0;
  }
  if (st.pressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
    if (!st.scrolling && !g_sliding && std::fabs(d.y) > 10.f * S() && std::fabs(d.y) > std::fabs(d.x) * 1.2f)
      st.scrolling = true;
    if (st.scrolling) {
      ImGui::SetScrollY(st.startScroll - d.y);
      float dtv = std::max(io.DeltaTime, 1e-3f);
      st.vel = st.vel * 0.5f + (-io.MouseDelta.y / dtv) * 0.5f;
      ImGui::ClearActiveID();  // the button / slider under the finger must not fire
    }
  } else {
    st.pressed = false;
    st.scrolling = false;
    if (std::fabs(st.vel) > 20.f) {  // momentum
      ImGui::SetScrollY(ImGui::GetScrollY() + st.vel * io.DeltaTime);
      st.vel *= std::pow(0.04f, io.DeltaTime);
    } else {
      st.vel = 0;
    }
  }
}

const char* kEffectNames[FX_COUNT] = {"FLUID",   "PARTICLES", "KALEIDO",   "LASERS",    "STROBE",  "SPARKLES",
                                     "RIPPLES", "RIBBONS",   "SHAPES",    "TUNNEL",    "LIGHTNING", "PLEXUS",
                                     "FIREWORKS", "STARFIELD", "SPECTRUM", "LED GRID"};
// Display order of the effect buttons (two rows of eight).
const int kEffectOrder[FX_COUNT] = {FX_FLUID,  FX_PARTICLES, FX_SPARKLES,  FX_RIBBONS,  FX_FIREWORKS, FX_STARFIELD,
                                    FX_PLEXUS, FX_KALEIDO,   FX_LASERS,    FX_LIGHTNING, FX_TUNNEL,   FX_GEOMETRY,
                                    FX_RIPPLES, FX_SPECTRUM, FX_GRID,      FX_STROBE};

// ---------------------------------------------------------------------------
void EffectParams(App& a) {
  Look& l = a.st.look;
  float s = S();
  switch (l.effect) {
    case FX_FLUID: {
      FluidSettings& f = l.fluid;
      SectionLabel("FLUID / SMOKE");
      Pct("Size", &f.size);
      Pct("Force", &f.force);
      Pct("Swirl", &f.swirl);
      Pct("Trail", &f.trail);
      Pct("Intensity", &f.intensity);
      if (ImGui::Button("RESET FLUID", ImVec2(-1, 40 * s))) f = FluidSettings();
      break;
    }
    case FX_PARTICLES: {
      ParticleSettings& p = l.particles;
      SectionLabel("PARTICLES");
      Pct("Amount", &p.amount);
      Pct("Size", &p.size);
      Pct("Life", &p.life);
      Pct("Speed", &p.speed);
      Pct("Spread", &p.spread);
      Centered("Gravity", &p.gravity);
      Pct("Trail", &p.trail);
      SectionLabel("PARTICLE STROBE");
      static const char* ps[] = {"OFF", "SYNC", "SPARKLE"};
      Choice("pstrobe", ps, 3, &p.strobe);
      if (p.strobe != PSTROBE_OFF) {
        SectionLabel("FLASHES PER BEAT");
        Choice("prate", kDivisionLabels, kNumDivisions, &p.strobeRate);
      }
      if (ImGui::Button("RESET PARTICLES", ImVec2(-1, 40 * s))) p = ParticleSettings();
      break;
    }
    case FX_SPARKLES: {
      SparkleSettings& p = l.sparkles;
      SectionLabel("SPARKLES");
      Pct("Amount", &p.amount);
      Pct("Size", &p.size);
      Pct("Spread", &p.spread);
      Pct("Twinkle", &p.twinkle);
      Pct("Life", &p.life);
      Pct("Drift", &p.drift);
      Pct("Rays", &p.rays);
      Pct("Trail", &p.trail);
      if (ImGui::Button("RESET SPARKLES", ImVec2(-1, 40 * s))) p = SparkleSettings();
      break;
    }
    case FX_KALEIDO: {
      KaleidoSettings& k = l.kaleido;
      SectionLabel("KALEIDOSCOPE");
      IntSlider("Segments", &k.segments, 2, 16);
      Centered("Rotation", &k.rotation);
      Centered("Zoom", &k.zoom);
      Pct("Warp", &k.warp);
      Pct("Brush", &k.brush);
      Pct("Trail", &k.trail);
      if (ImGui::Button("RESET KALEIDO", ImVec2(-1, 40 * s))) k = KaleidoSettings();
      break;
    }
    case FX_LASERS: {
      LaserSettings& z = l.lasers;
      SectionLabel("LASER SHOW");
      static const char* modes[] = {"FAN", "AIM", "WEB", "SCAN", "TUNNEL", "WAVE"};
      static const int order[] = {LASER_FAN, LASER_AIM, LASER_TUNNEL, LASER_WAVE, LASER_SCAN, LASER_WEB};
      static const char* labels[] = {"FAN", "AIM", "TUNNEL", "WAVE", "SCAN", "WEB"};
      (void)modes;
      int idx = 0;
      for (int i = 0; i < 6; ++i)
        if (order[i] == z.mode) idx = i;
      if (Choice("lmode", labels, 6, &idx, 3)) z.mode = order[idx];
      IntSlider(z.mode == LASER_TUNNEL ? "Rings" : (z.mode == LASER_WAVE ? "Lines" : "Beams"), &z.beams, 1, 32);
      Centered(z.mode == LASER_FAN || z.mode == LASER_WEB ? "Rotation" : "Motion", &z.rotation);
      Pct(z.mode == LASER_FAN ? "Fan width" : (z.mode == LASER_AIM ? "Rig width" : "Spread"), &z.spread);
      Pct("Swing", &z.swing);
      Pct("Width", &z.width);
      Pct("Glow", &z.glow);
      Pct("Length", &z.length);
      Pct("Trail", &z.trail);
      SectionLabel("CHASE (beams blink on the beat)");
      static const char* ch[] = {"OFF", "STEP", "ALT", "RANDOM"};
      Choice("lchase", ch, 4, &z.chase);
      if (ImGui::Button("RESET LASERS", ImVec2(-1, 40 * s))) z = LaserSettings();
      break;
    }
    case FX_RIPPLES: {
      RippleSettings& p = l.ripples;
      SectionLabel("RIPPLES");
      Pct("Drop size", &p.size);
      Pct("Strength", &p.strength);
      Pct("Ring time", &p.decay);
      Pct("Shine", &p.shine);
      OnOff("DROP ON EVERY BEAT", &p.beatDrops, 44 * s);
      if (ImGui::Button("RESET RIPPLES", ImVec2(-1, 40 * s))) p = RippleSettings();
      break;
    }
    case FX_RIBBONS: {
      RibbonSettings& p = l.ribbons;
      SectionLabel("RIBBONS");
      IntSlider("Ribbons per finger", &p.strands, 1, 6);
      Pct("Length", &p.length);
      Pct("Width", &p.width);
      Pct("Wind", &p.wind);
      Pct("Flutter", &p.flutter);
      Pct("Stiffness", &p.stiffness);
      Pct("Sheen", &p.sheen);
      if (ImGui::Button("RESET RIBBONS", ImVec2(-1, 40 * s))) p = RibbonSettings();
      break;
    }
    case FX_GEOMETRY: {
      GeometrySettings& p = l.geometry;
      SectionLabel("SHAPES");
      static const char* shapes[] = {"TRIANGLE", "SQUARE", "PENTAGON", "HEXAGON", "CIRCLE", "STAR", "MIX"};
      Choice("gshape", shapes, SHAPE_COUNT, &p.shape, 4);
      SectionLabel("SHAPES PER BEAT");
      Choice("grate", kDivisionLabels, kNumDivisions, &p.rate);
      Pct("Size", &p.size);
      Centered("Spin", &p.spin);
      Pct("Line width", &p.width);
      Pct("Life", &p.life);
      Pct("Glow", &p.glow);
      if (ImGui::Button("RESET SHAPES", ImVec2(-1, 40 * s))) p = GeometrySettings();
      break;
    }
    case FX_TUNNEL: {
      TunnelSettings& p = l.tunnel;
      SectionLabel("TUNNEL");
      static const char* sides[] = {"ROUND", "TRIANGLE", "SQUARE", "HEXAGON", "OCTAGON"};
      static const int sideVals[] = {0, 3, 4, 6, 8};
      int idx = 0;
      for (int i = 0; i < 5; ++i)
        if (sideVals[i] == p.sides) idx = i;
      if (Choice("tsides", sides, 5, &idx, 3)) p.sides = sideVals[idx];
      OnOff("MERGE (one tunnel for all fingers)", &p.merge, 44 * s);
      Pct("Speed", &p.speed);
      Pct("Ring density", &p.density);
      Centered("Spin", &p.twist);
      Pct("Line width", &p.width);
      Pct("Edge fade", &p.depth);
      if (ImGui::Button("RESET TUNNEL", ImVec2(-1, 40 * s))) p = TunnelSettings();
      break;
    }
    case FX_LIGHTNING: {
      LightningSettings& p = l.lightning;
      SectionLabel("LIGHTNING");
      Pct("Branches", &p.branches);
      Pct("Chaos", &p.chaos);
      Pct("Flicker rate", &p.rate);
      Pct("Reach", &p.reach);
      Pct("Width", &p.width);
      Pct("Glow", &p.glow);
      Pct("Afterglow", &p.trail);
      if (ImGui::Button("RESET LIGHTNING", ImVec2(-1, 40 * s))) p = LightningSettings();
      break;
    }
    case FX_PLEXUS: {
      PlexusSettings& p = l.plexus;
      SectionLabel("PLEXUS");
      Pct("Points", &p.amount);
      Pct("Link distance", &p.range);
      Pct("Dot size", &p.size);
      Pct("Drift", &p.drift);
      Pct("Spread", &p.spread);
      Pct("Trail", &p.trail);
      if (ImGui::Button("RESET PLEXUS", ImVec2(-1, 40 * s))) p = PlexusSettings();
      break;
    }
    case FX_FIREWORKS: {
      FireworkSettings& p = l.fireworks;
      SectionLabel("FIREWORKS");
      SectionLabel("BURSTS PER BEAT (while holding)");
      Choice("frate", kDivisionLabels, kNumDivisions, &p.rate);
      OnOff("ROCKETS FROM THE BOTTOM", &p.rockets, 44 * s);
      Pct("Burst size", &p.size);
      Pct("Embers", &p.amount);
      Pct("Gravity", &p.gravity);
      Pct("Trail", &p.trail);
      Pct("Sparkle", &p.sparkle);
      if (ImGui::Button("RESET FIREWORKS", ImVec2(-1, 40 * s))) p = FireworkSettings();
      break;
    }
    case FX_STARFIELD: {
      StarfieldSettings& p = l.starfield;
      SectionLabel("STARFIELD");
      Pct("Stars", &p.amount);
      Pct("Speed", &p.speed);
      Pct("Streaks", &p.streak);
      Pct("Size", &p.size);
      Pct("Twinkle", &p.twinkle);
      Pct("Brightness", &p.brightness);
      if (ImGui::Button("RESET STARFIELD", ImVec2(-1, 40 * s))) p = StarfieldSettings();
      break;
    }
    case FX_SPECTRUM: {
      SpectrumSettings& p = l.spectrum;
      SectionLabel("SPECTRUM");
      if (!a.st.audio.enabled) Hint("Turn AUDIO on to follow the music (a demo spectrum plays meanwhile).");
      static const char* modes[] = {"CIRCLE", "BARS", "WAVE"};
      Choice("smode", modes, SPEC_COUNT, &p.mode);
      IntSlider("Bars", &p.bars, 8, 128);
      OnOff("SYMMETRIC", &p.symmetric, 44 * s);
      Pct("Size", &p.size);
      Pct("Gain", &p.gain);
      Pct("Bar width", &p.width);
      Pct("Glow", &p.glow);
      if (ImGui::Button("RESET SPECTRUM", ImVec2(-1, 40 * s))) p = SpectrumSettings();
      break;
    }
    case FX_GRID: {
      GridSettings& p = l.grid;
      SectionLabel("LED GRID");
      static const char* modes[] = {"TRAIL", "RINGS", "SPARKLE"};
      Choice("gmode", modes, GRID_COUNT, &p.mode);
      Pct("Tile size", &p.tile);
      Pct("Gap", &p.gap);
      Pct("Rounded", &p.round);
      Pct("Brush", &p.radius);
      Pct("Trail", &p.decay);
      if (ImGui::Button("RESET LED GRID", ImVec2(-1, 40 * s))) p = GridSettings();
      break;
    }
    case FX_STROBE: {
      StrobeSettings& t = l.strobe;
      SectionLabel("STROBE / FLASH");
      static const char* shapes[] = {"FULL", "SPOT", "H-BAR", "V-BAR", "SPARKLE", "SHOCKWAVE", "WIPE", "BLINDER", "BLOCKS"};
      Choice("sshape", shapes, STROBE_COUNT, &t.shape, 3);
      SectionLabel("RATE");
      int mode = t.freeHz ? 1 : 0;
      static const char* rm[] = {"BPM SYNC", "FREE (Hz)"};
      if (Choice("srm", rm, 2, &mode)) t.freeHz = mode == 1;
      if (t.freeHz) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f Hz", t.hz);
        TouchSlider("Hz", &t.hz, 0.5f, 25.f, buf);
      } else {
        Choice("srate", kDivisionLabels, kNumDivisions, &t.rate);
      }
      Pct("Flash length", &t.duty);
      Pct("Size", &t.size);
      OnOff("COLOUR CYCLE", &t.colorCycle, 44 * s);
      OnOff("FLASH ON AUDIO HITS", &t.audioHits, 44 * s);
      if (t.audioHits && !a.st.audio.enabled) Hint("Turn AUDIO on to flash on the hits of the music.");
      if (ImGui::Button("RESET STROBE", ImVec2(-1, 40 * s))) t = StrobeSettings();
      break;
    }
  }
}

void PaletteGrid(App& a) {
  Look& l = a.st.look;
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = (ImGui::GetContentRegionAvail().x - sp * 4) / 5;
  float h = 58 * s;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < kNumPalettes; ++i) {
    if (i % 5) ImGui::SameLine();
    ImGui::PushID(i);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("pal", ImVec2(w, h))) {
      l.palette = i;
      l.useCustom = false;
      a.dmxColors = false;
    }
    const Palette& pal = a.st.palettes[i];
    float th = h - 18 * s;
    float bw = w / 3;
    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + th), col32(pal.low), 4 * s, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRectFilled(ImVec2(p.x + bw, p.y), ImVec2(p.x + 2 * bw, p.y + th), col32(pal.mid));
    dl->AddRectFilled(ImVec2(p.x + 2 * bw, p.y), ImVec2(p.x + w, p.y + th), col32(pal.high), 4 * s,
                      ImDrawFlags_RoundCornersTopRight);
    dl->AddRectFilled(ImVec2(p.x, p.y + th), ImVec2(p.x + w, p.y + h), IM_COL32(35, 35, 40, 255), 4 * s,
                      ImDrawFlags_RoundCornersBottom);
    ImGui::PushFont(a.fontSmall);
    char name[32];
    std::snprintf(name, sizeof(name), "%.10s", pal.name.c_str());
    ImVec2 ts = ImGui::CalcTextSize(name);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + th + (18 * s - ts.y) * 0.5f),
                l.palette == i ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(kText), name);
    ImGui::PopFont();
    if (l.palette == i)
      dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + w + 2, p.y + h + 2), ImGui::GetColorU32(kAccent), 5 * s, 0,
                  3 * s);
    ImGui::PopID();
  }
}

void ColorModeChoice(App& a) {
  static const char* modes[] = {"GRADIENT", "SPEED", "POSITION", "BEAT", "RANDOM"};
  Choice("cmode", modes, 5, &a.st.look.colorMode, 3);
  static const char* help[] = {
      "Low > Mid > High over the life of the effect.", "Slow finger = Low, fast finger = High.",
      "Left of the canvas = Low, right = High.", "Steps Low > Mid > High on every beat.",
      "A random mix of the three colours."};
  Hint(help[a.st.look.colorMode]);
}

void ColorEdit(const char* label, Color3& c, float w) {
  ImGui::PushID(label);
  ImGui::BeginGroup();
  ImVec4 v(c[0], c[1], c[2], 1);
  if (ImGui::ColorButton("##btn", v, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
                         ImVec2(w, 64 * S())))
    ImGui::OpenPopup("picker");
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  float tw = ImGui::CalcTextSize(label).x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - tw) * 0.5f);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  ImGui::EndGroup();
  if (ImGui::BeginPopup("picker")) {
    ImGui::SetNextItemWidth(340 * S());
    ImGui::ColorPicker3("##pick", c.data(),
                        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_DisplayHex);
    if (ImGui::Button("DONE", ImVec2(-1, 48 * S()))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

int g_renameScene = -1;

// One scene slot. Shows the scene name (or effect) and a strip of the scene's palette.
void SceneButton(App& a, int i, ImVec2 size) {
  Scene& sc = a.st.scenes[i];
  char label[96];
  if (sc.used)
    std::snprintf(label, sizeof(label), "%d\n%.14s##scene%d", i + 1,
                  sc.name.empty() ? kEffectNames[sc.look.effect] : sc.name.c_str(), i);
  else std::snprintf(label, sizeof(label), "%d##scene%d", i + 1, i);
  if (!sc.used) ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool pressed = Toggle(label, sc.used && a.lastScene == i, size);
  if (!sc.used) ImGui::PopStyleColor();
  // long press (not in show mode): rename the scene
  if (sc.used && !ShowModeLocked(a) && ImGui::IsItemActive() && ImGui::GetIO().MouseDownDuration[0] > 0.8f) {
    g_renameScene = i;
    OpenKeyboard("scenename", sc.name.empty() ? kEffectNames[sc.look.effect] : sc.name, "Scene name");
    ImGui::ClearActiveID();
    pressed = false;
  }
  if (sc.used) {
    const Palette& pal = a.st.palettes[sc.look.palette];
    float s = S(), bw = (size.x - 12 * s) / 3, y0 = p.y + size.y - 7 * s, y1 = p.y + size.y - 3 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x + 6 * s, y0), ImVec2(p.x + 6 * s + bw, y1), col32(pal.low));
    dl->AddRectFilled(ImVec2(p.x + 6 * s + bw, y0), ImVec2(p.x + 6 * s + 2 * bw, y1), col32(pal.mid));
    dl->AddRectFilled(ImVec2(p.x + 6 * s + 2 * bw, y0), ImVec2(p.x + 6 * s + 3 * bw, y1), col32(pal.high));
  }
  if (pressed) {
    if (a.storeArmed) {
      undoPush(a, "store scene " + std::to_string(i + 1));
      sc.used = true;
      sc.look = a.st.look;
      a.storeArmed = false;
      a.lastScene = i;
      g_renameScene = i;  // give it a name right away
      OpenKeyboard("scenename", sc.name.empty() ? std::string(kEffectNames[sc.look.effect]) : sc.name, "Scene name");
    } else if (a.deleteArmed) {
      undoPush(a, "delete scene " + std::to_string(i + 1));
      sc = Scene();
      a.deleteArmed = false;
      if (a.lastScene == i) a.lastScene = -1;
    } else if (sc.used) {
      a.recallScene(i);
    }
  }
}

void StoreDeleteButtons(App& a, ImVec2 size, bool sameLine) {
  if (ShowModeLocked(a)) {  // show mode: recall only
    a.storeArmed = a.deleteArmed = false;
    ImGui::BeginDisabled();
  }
  if (Toggle(a.storeArmed ? "STORE..." : "STORE", a.storeArmed, size)) {
    a.storeArmed = !a.storeArmed;
    a.deleteArmed = false;
  }
  if (sameLine) ImGui::SameLine();
  if (Toggle(a.deleteArmed ? "DELETE..." : "DELETE", a.deleteArmed, size, kDanger)) {
    a.deleteArmed = !a.deleteArmed;
    a.storeArmed = false;
  }
  if (ShowModeLocked(a)) ImGui::EndDisabled();
}

void ScenesGrid(App& a) {
  SectionLabel("SCENES");
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = (ImGui::GetContentRegionAvail().x - sp) / 2;
  StoreDeleteButtons(a, ImVec2(w, 46 * S()), true);
  float cw = (ImGui::GetContentRegionAvail().x - sp * 3) / 4;
  for (int i = 0; i < kNumScenes; ++i) {
    if (i % 4) ImGui::SameLine();
    SceneButton(a, i, ImVec2(cw, 64 * S()));
  }
  Hint("STORE then tap a slot to save the whole look. Tap a slot to recall it.");
}

// Small level meter: BASS / MID / HIGH bars + a hit light.
void AudioMeter(App& a, float h) {
  float s = S();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float w = ImGui::GetContentRegionAvail().x;
  ImGui::Dummy(ImVec2(w, h));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const char* names[3] = {"BASS", "MID", "HIGH"};
  float vals[3] = {a.af.bass, a.af.mid, a.af.high};
  ImU32 cols[3] = {IM_COL32(255, 110, 40, 255), IM_COL32(255, 210, 60, 255), IM_COL32(80, 200, 255, 255)};
  float lightW = h;
  float bw = (w - lightW - 8 * s) / 3 - 6 * s;
  ImGui::PushFont(a.fontSmall);
  for (int i = 0; i < 3; ++i) {
    float x = p.x + i * (bw + 6 * s);
    dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + bw, p.y + h), IM_COL32(30, 30, 36, 255), 5 * s);
    dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + bw * std::clamp(vals[i], 0.f, 1.f), p.y + h), cols[i], 5 * s);
    dl->AddText(ImVec2(x + 8 * s, p.y + (h - ImGui::GetFontSize()) * 0.5f), IM_COL32(255, 255, 255, 230), names[i]);
  }
  ImGui::PopFont();
  float cx = p.x + w - lightW * 0.5f, cy = p.y + h * 0.5f;
  float pl = std::clamp(a.af.pulse, 0.f, 1.f);
  dl->AddCircleFilled(ImVec2(cx, cy), h * 0.42f, IM_COL32(60 + int(195 * pl), 60 + int(140 * pl), 70, 255));
}

const char* AudioSourceName(const std::string& dev) {
  if (dev.empty()) return "Default input";
  if (dev == kAudioLoopback) return "System audio (this PC)";
  if (dev == kAudioTest) return "Test beat (128 BPM)";
  return dev.c_str();
}

void AudioStatus(App& a) {
  if (!a.audio.error().empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
    ImGui::TextWrapped("%s", a.audio.error().c_str());
    ImGui::PopStyleColor();
  } else if (a.audio.isOpen()) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Listening: %s  (%d Hz)%s", AudioSourceName(a.st.audio.device),
                  a.audio.sampleRate(), a.af.active ? "" : "  -  no signal");
    Hint(buf);
  }
}

void AudioLive(App& a) {
  AudioSettings& as = a.st.audio;
  float s = S();
  SectionLabel("AUDIO REACTIVE");
  AudioMeter(a, 30 * s);
  AudioStatus(a);
  float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  if (Toggle(as.autoBpm ? "AUTO BPM: ON" : "AUTO BPM: OFF", as.autoBpm, ImVec2(w, 44 * s))) as.autoBpm = !as.autoBpm;
  ImGui::SameLine();
  if (Toggle(as.autoPlay ? "AUTO PLAY: ON" : "AUTO PLAY: OFF", as.autoPlay, ImVec2(w, 44 * s))) as.autoPlay = !as.autoPlay;
  Pct("Reactivity", &as.reactivity);
  if (as.autoPlay) {
    IntSlider("Auto points", &as.points, 1, 4);
    Pct("Auto motion", &as.motion);
  }
  Hint("AUTO PLAY draws by itself with the music (no touch needed, it stops when the music stops). "
       "Your fingers still work on top.");
}

void AudioSettingsTab(App& a) {
  AudioSettings& as = a.st.audio;
  float s = S();
  SectionLabel("AUDIO REACTIVE");
  OnOff("AUDIO", &as.enabled, 50 * s);
  SectionLabel("SOURCE");
  static std::vector<std::string> devices;
  static double listedAt = -100;
  if (a.time - listedAt > 3.0) {  // refresh the list now and then (USB interfaces come and go)
    devices = AudioInput::captureDevices();
    listedAt = a.time;
  }
  auto sourceButton = [&](const std::string& dev, const char* label) {
    if (Toggle(label, as.device == dev, ImVec2(-1, 44 * s))) as.device = dev;
  };
  sourceButton("", "Default input");
  for (size_t i = 0; i < devices.size(); ++i) {
    char label[160];
    std::snprintf(label, sizeof(label), "%s##dev%d", devices[i].c_str(), int(i));
    sourceButton(devices[i], label);
  }
  if (AudioInput::loopbackSupported()) sourceButton(kAudioLoopback, "System audio (what this PC is playing)");
  sourceButton(kAudioTest, "Test beat (128 BPM, no music needed)");
  Hint("Use a line input from the sound desk (or a microphone) for the live show. System audio follows "
       "music playing on this computer.");
  SectionLabel("INPUT LEVEL");
  AudioMeter(a, 30 * s);
  AudioStatus(a);
  if (!as.enabled) Hint("Listening while this tab is open, to check the input. Turn AUDIO on to drive the visuals.");
  SectionLabel("ANALYSIS");
  Pct("Sensitivity", &as.sensitivity);
  Hint("Raise it if quiet music doesn't trigger anything; lower it if room noise triggers the effects.");
  OnOff("AUTO BPM (follow the music)", &as.autoBpm);
  if (as.enabled && a.af.bpm > 0) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Detected tempo: %.0f BPM", a.af.bpm);
    Hint(buf);
  }
  SectionLabel("AUTO PLAY");
  OnOff("AUTO PLAY (draw without touch)", &as.autoPlay);
  IntSlider("Auto points", &as.points, 1, 4);
  Pct("Auto motion", &as.motion);
  Pct("Reactivity", &as.reactivity);
}

// A number with big - / + buttons (hold to repeat) and a typed value.
bool Stepper(const char* label, int* v, int lo, int hi) {
  float s = S();
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  float bw = 54 * s, h = 44 * s;
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f);
  bool ch = false;
  ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
  if (ImGui::Button("-", ImVec2(bw, h)) && *v > lo) { --*v; ch = true; }
  ImGui::SameLine();
  float w = ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * s, (h - ImGui::GetFontSize()) * 0.5f));
  ImGui::SetNextItemWidth(w);
  int t = *v;
  ImGui::InputInt("##v", &t, 0, 0);
  if (ImGui::IsItemDeactivatedAfterEdit()) { *v = std::clamp(t, lo, hi); ch = true; }
  ImGui::PopStyleVar();
  ImGui::SameLine();
  if (ImGui::Button("+", ImVec2(bw, h)) && *v < hi) { ++*v; ch = true; }
  ImGui::PopItemFlag();
  ImGui::PopID();
  return ch;
}

// Network card choice: a drop-down with AUTOMATIC or one adapter. A chosen card that is gone stays
// selected, in red.
bool AdapterPicker(App& a, const char* id, std::string& adapterId, std::string& label) {
  float s = S();
  bool changed = false;
  ImGui::PushID(id);
  bool missing = !adapterId.empty() && !findAdapter(a.adapters, adapterId);
  std::string cur = "AUTOMATIC (all network cards)";
  if (const NetAdapter* n = adapterId.empty() ? nullptr : findAdapter(a.adapters, adapterId)) cur = adapterLabel(*n);
  else if (missing) cur = (label.empty() ? std::string("Chosen card") : label) + "  -  NOT CONNECTED";
  ImVec2 p = ImGui::GetCursorScreenPos();
  float w = ImGui::GetContentRegionAvail().x;
  std::string btn = cur + "##pick";
  if (missing) ImGui::PushStyleColor(ImGuiCol_Button, kDanger);
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.f, 0.5f));
  if (ImGui::Button(btn.c_str(), ImVec2(w, 48 * s))) ImGui::OpenPopup("##cards");
  ImGui::PopStyleVar();
  if (missing) ImGui::PopStyleColor();
  // drop-down arrow
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float ax = p.x + w - 26 * s, ay = p.y + 24 * s;
  dl->AddTriangleFilled(ImVec2(ax - 8 * s, ay - 4 * s), ImVec2(ax + 8 * s, ay - 4 * s), ImVec2(ax, ay + 5 * s),
                        ImGui::GetColorU32(kText));
  ImGui::SetNextWindowPos(ImVec2(p.x, p.y + 50 * s));
  ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, 420 * s));
  if (ImGui::BeginPopup("##cards")) {
    if (Toggle("AUTOMATIC (all network cards)", adapterId.empty(), ImVec2(-1, 48 * s))) {
      changed = !adapterId.empty();
      adapterId.clear();
      label.clear();
      ImGui::CloseCurrentPopup();
    }
    for (const NetAdapter& n : a.adapters) {
      std::string l = adapterLabel(n) + "##" + n.id;
      if (Toggle(l.c_str(), n.id == adapterId, ImVec2(-1, 48 * s))) {
        changed = n.id != adapterId;
        adapterId = n.id;
        label = n.name + " (" + ipToString(n.ip) + ")";
        ImGui::CloseCurrentPopup();
      }
    }
    if (missing) {
      std::string l = cur + "##missing";
      Toggle(l.c_str(), true, ImVec2(-1, 48 * s), kDanger);
    }
    TouchScroll();
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return changed;
}

void DmxTab(App& a) {
  DmxSettings& d = a.st.dmx;
  float s = S();
  SectionLabel("DMX INPUT (control TouchFX from a lighting console)");
  static const char* protos[] = {"OFF", "sACN", "ART-NET"};
  Choice("proto", protos, 3, &d.protocol);
  if (d.protocol != DMXP_OFF) {
    SectionLabel("NETWORK CARD (this PC)");
    AdapterPicker(a, "dmxnet", a.st.dmxAdapter, a.st.dmxAdapterLabel);
    if (a.dmxAdapterMissing)
      Hint("The chosen network card is not connected: DMX is paused until it comes back (cable, adapter enabled).");
    else
      Hint("Choose the card on the lighting network so TouchFX only listens to the console there.");
  }
  if (d.protocol == DMXP_SACN) {
    Stepper("Universe", &d.sacnUniverse, 1, 63999);
  } else if (d.protocol == DMXP_ARTNET) {
    Stepper("Art-Net univ.", &d.artnetUniverse, 0, 32767);
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Net %d / Sub-Net %d / Universe %d. Art-Net counts from 0: grandMA3 universe %d is usually "
                  "Art-Net %d.", (d.artnetUniverse >> 8) & 0x7F, (d.artnetUniverse >> 4) & 0xF, d.artnetUniverse & 0xF,
                  d.artnetUniverse + 1, d.artnetUniverse);
    Hint(buf);
  }
  Stepper("Start address", &d.address, 1, 512);
  SectionLabel("PROFILE (GDTF mode)");
  static const char* modes[] = {"BASIC (12 ch)", "ADVANCED (36 ch)"};
  Choice("dmode", modes, 2, &d.mode);
  int foot = dmxFootprint(d.mode);
  int last = d.address + foot - 1;
  char buf[200];
  if (last > 512) {
    std::snprintf(buf, sizeof(buf), "Does not fit: %d channels from address %d would end at %d. Use address %d or lower.",
                  foot, d.address, last, 512 - foot + 1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
    ImGui::TextWrapped("%s", buf);
    ImGui::PopStyleColor();
  } else {
    std::snprintf(buf, sizeof(buf), "Uses channels %d-%d. Patch the \"TouchFX\" fixture (gdtf folder next to TouchFX.exe) "
                  "in mode %s.", d.address, last, d.mode == 0 ? "Basic" : "Advanced");
    Hint(buf);
  }
  OnOff("DMX LOCK (console always wins)", &d.lock);
  Hint("Without lock, the last change wins: the panel stays usable and a DMX channel only takes over when its value "
       "changes. If the signal stops, TouchFX keeps the last look.");

  SectionLabel("STATUS");
  if (d.protocol == DMXP_ARTNET) {
    std::string by = a.dmx.polledBy();
    double ago = a.dmx.secondsSincePoll();
    if (!by.empty()) {
      std::snprintf(buf, sizeof(buf), "Seen by %s (polled %.0f s ago): TouchFX is listed as an Art-Net node there.",
                    by.c_str(), ago);
      ImGui::PushStyleColor(ImGuiCol_Text, ago < 10 ? ImVec4(0.3f, 0.9f, 0.4f, 1) : kTextDim);
      ImGui::TextWrapped("%s", buf);
      ImGui::PopStyleColor();
    } else {
      Hint("No console has polled TouchFX yet (Art-Net discovery).");
    }
  }
  if (d.protocol == DMXP_OFF) {
    Hint("DMX input is off.");
  } else {
    double age = a.dmx.secondsSinceData();
    if (!a.dmx.error().empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
      ImGui::TextWrapped("%s", a.dmx.error().c_str());
      ImGui::PopStyleColor();
    }
    if (age > 1e8) {
      Hint("Waiting for data on this universe...");
    } else {
      std::snprintf(buf, sizeof(buf), "%s from %s - %d packets/s%s", age < 1.0 ? "Receiving" : "Signal lost (holding)",
                    a.dmx.sourceInfo().c_str(), a.dmx.packetsPerSecond(), age < 1.0 ? "" : "");
      ImGui::PushStyleColor(ImGuiCol_Text, age < 1.0 ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(1, 0.8f, 0.3f, 1));
      ImGui::TextWrapped("%s", buf);
      ImGui::PopStyleColor();
      // channel monitor for the footprint
      ImGui::PushFont(a.fontSmall);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      int cols = 9;
      float cw = (ImGui::GetContentRegionAvail().x - 4 * s * (cols - 1)) / cols, chh = 34 * s;
      for (int i = 0; i < foot && d.address - 1 + i < 512; ++i) {
        if (i % cols) ImGui::SameLine(0, 4 * s);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cw, chh));
        int v = a.dmxFrame[d.address - 1 + i];
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + chh), IM_COL32(30, 30, 36, 255), 4 * s);
        dl->AddRectFilled(ImVec2(p.x, p.y + chh * (1.f - v / 255.f)), ImVec2(p.x + cw, p.y + chh),
                          IM_COL32(255, 120, 30, 120), 4 * s);
        char t[24];
        std::snprintf(t, sizeof(t), "%d", d.address + i);
        dl->AddText(ImVec2(p.x + 3 * s, p.y + 1 * s), IM_COL32(150, 150, 160, 255), t);
        std::snprintf(t, sizeof(t), "%d", v);
        ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(p.x + cw - ts.x - 3 * s, p.y + chh - ts.y - 1 * s), IM_COL32(255, 255, 255, 255), t);
      }
      ImGui::PopFont();
      std::snprintf(buf, sizeof(buf), "Master dimmer %.0f%%%s%s", a.dmxDim * 100.f, a.dmxStrobeHz > 0 ? ", shutter strobe" : "",
                    a.dmxColors ? ", DMX colours" : "");
      Hint(buf);
    }
  }
  (void)s;
}

void ClearBlackout(App& a, ImVec2 size, bool sameLine) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.12f, 0.12f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.12f, 0.12f, 1));
  if (ImGui::Button("CLEAR", size)) a.wantClear = true;
  ImGui::PopStyleColor(2);
  if (sameLine) ImGui::SameLine();
  if (Toggle("BLACKOUT", a.blackout, size, kDanger)) a.blackout = !a.blackout;
}

// The effect buttons, in kEffectOrder. size.x = 0 means "fill the row".
void EffectButtons(App& a, int cols, ImVec2 size) {
  Look& l = a.st.look;
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = size.x > 0 ? size.x : (ImGui::GetContentRegionAvail().x - sp * (cols - 1)) / cols;
  for (int k = 0; k < FX_COUNT; ++k) {
    int i = kEffectOrder[k];
    if (k % cols) ImGui::SameLine();
    if (Toggle(kEffectNames[i], l.effect == i, ImVec2(w, size.y))) l.effect = i;
  }
}

// Text on a colour swatch: black on light colours, white on dark ones, with a soft shadow.
void SwatchLabel(ImVec2 p, ImVec4 col, const char* text) {
  float s = S();
  float lum = 0.2126f * col.x + 0.7152f * col.y + 0.0722f * col.z;
  bool light = lum > 0.55f;
  ImU32 fg = light ? IM_COL32(10, 10, 12, 255) : IM_COL32(255, 255, 255, 255);
  ImU32 shadow = light ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 160);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 at(p.x + 7 * s, p.y + 5 * s);
  dl->AddText(ImVec2(at.x + 1, at.y + 1), shadow, text);
  dl->AddText(at, fg, text);
}

// ---- BPM keypad (tap the BPM number) ----
char g_bpmEntry[8] = {};

void BpmKeypad(App& a) {
  float s = S();
  ImGui::SetNextWindowSize(ImVec2(330 * s, 0));
  if (!ImGui::BeginPopup("bpmpad")) return;
  ImGui::PushFont(a.fontBig);
  if (g_bpmEntry[0]) ImGui::Text("%s", g_bpmEntry);
  else ImGui::TextColored(kTextDim, "BPM");
  ImGui::PopFont();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextUnformatted("Type a tempo between 20 and 300");
  ImGui::PopStyleColor();
  static const char* keys[] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "<"};
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = (ImGui::GetContentRegionAvail().x - sp * 2) / 3, h = 58 * s;
  for (int i = 0; i < 12; ++i) {
    if (i % 3) ImGui::SameLine();
    if (ImGui::Button(keys[i], ImVec2(w, h))) {
      size_t n = std::strlen(g_bpmEntry);
      if (keys[i][0] == '<') {
        if (n) g_bpmEntry[n - 1] = 0;
      } else if (n < 5 && !(keys[i][0] == '.' && std::strchr(g_bpmEntry, '.'))) {
        g_bpmEntry[n] = keys[i][0];
        g_bpmEntry[n + 1] = 0;
      }
    }
  }
  float half = (ImGui::GetContentRegionAvail().x - sp) / 2;
  if (ImGui::Button("CANCEL", ImVec2(half, h))) ImGui::CloseCurrentPopup();
  ImGui::SameLine();
  float v = float(std::atof(g_bpmEntry));
  bool valid = v >= 20.f && v <= 300.f;
  ImGui::BeginDisabled(!valid);  // greyed out until the number is a valid tempo
  if (Toggle("OK", valid, ImVec2(half, h))) {
    a.st.look.bpm = std::round(v * 10.f) / 10.f;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

// ---- quick colours: override the palette's Low / Mid / High live ----
void QuickColours(App& a) {
  Look& l = a.st.look;
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  const Palette& shown = l.useCustom ? l.custom : a.st.palettes[l.palette];
  SectionLabel(l.useCustom ? "QUICK COLOURS (override active)" : "QUICK COLOURS (tap to override)");
  float w = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
  const char* names[3] = {"LOW", "MID", "HIGH"};
  const Color3* cols[3] = {&shown.low, &shown.mid, &shown.high};
  static int editing = 0;
  for (int i = 0; i < 3; ++i) {
    if (i) ImGui::SameLine();
    ImGui::PushID(i);
    ImVec4 v((*cols[i])[0], (*cols[i])[1], (*cols[i])[2], 1);
    bool clicked = ImGui::ColorButton(names[i], v, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
                                      ImVec2(w, 48 * s));
    ImVec2 p = ImGui::GetItemRectMin();
    ImGui::PushFont(a.fontSmall);
    SwatchLabel(p, v, names[i]);
    ImGui::PopFont();
    ImGui::PopID();
    if (clicked) {  // opened outside PushID so BeginPopup below finds the same popup
      if (!l.useCustom) {  // start from the current palette
        l.custom = a.st.palettes[l.palette];
        l.useCustom = true;
      }
      editing = i;
      ImGui::OpenPopup("quickpick");
    }
  }
  if (ImGui::BeginPopup("quickpick")) {
    Color3* target[3] = {&l.custom.low, &l.custom.mid, &l.custom.high};
    ImGui::TextUnformatted(names[editing]);
    ImGui::SetNextItemWidth(340 * s);
    ImGui::ColorPicker3("##qp", target[editing]->data(),
                        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_DisplayHex);
    if (ImGui::Button("DONE", ImVec2(-1, 48 * s))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  if (l.useCustom && !ShowModeLocked(a)) {
    float half = (ImGui::GetContentRegionAvail().x - sp) / 2;
    char save[64];
    std::snprintf(save, sizeof(save), "SAVE TO %.12s", a.st.palettes[l.palette].name.c_str());
    if (ImGui::Button(save, ImVec2(half, 40 * s))) {
      std::string name = a.st.palettes[l.palette].name;
      a.st.palettes[l.palette] = l.custom;
      a.st.palettes[l.palette].name = name;
      l.useCustom = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("BACK TO PALETTE", ImVec2(half, 40 * s))) l.useCustom = false;
  }
}

// ---- SETTINGS > LOOK ----
void LookTab(App& a) {
  Look& l = a.st.look;
  float s = S();
  SectionLabel("LOOK (applies to every effect, saved in scenes)");
  PostSettings& p = l.post;
  Pct("Glow", &p.bloom);
  Pct("Glow size", &p.bloomSize);
  Centered("Saturation", &p.saturation);
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f %%", l.brightness * 100.f);
    TouchSlider("Brightness", &l.brightness, 0.f, 2.f, buf);
  }
  if (ImGui::Button("RESET LOOK", ImVec2(-1, 40 * s))) {
    p = PostSettings();
    l.brightness = 1.f;
  }
  SectionLabel("EFFECT CHANGE");
  {
    char buf[32];
    if (a.st.effectFade < 0.02f) std::snprintf(buf, sizeof(buf), "cut");
    else std::snprintf(buf, sizeof(buf), "%.2f s", a.st.effectFade);
    TouchSlider("Crossfade", &a.st.effectFade, 0.f, 3.f, buf);
  }
  Hint("When you switch effect, the old one fades out over this time instead of disappearing at once.");
}

// ---------------------------------------------------------------------------
void LivePanel(App& a) {
  Look& l = a.st.look;
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;

  // ---- BPM ----
  {
    ImGui::PushFont(a.fontBig);
    ImGui::Text("%5.1f", l.bpm);
    ImGui::PopFont();
    if (ImGui::IsItemClicked()) {  // tap the number to type a tempo
      g_bpmEntry[0] = 0;
      ImGui::OpenPopup("bpmpad");
    }
    BpmKeypad(a);
    ImGui::SameLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float bigH = a.fontBig->FontSize;
    int beatIdx = int(std::floor(a.tempo.beat)) & 3;
    float ph = float(a.tempo.beat - std::floor(a.tempo.beat));
    for (int i = 0; i < 4; ++i) {
      ImU32 c = i == beatIdx ? ImGui::GetColorU32(ImVec4(1, 0.47f + 0.3f * (1 - ph), 0.1f, 1.f - ph * 0.6f))
                             : IM_COL32(60, 60, 70, 255);
      ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 10 * s + i * 20 * s, p.y + bigH * 0.55f), 7 * s, c);
    }
    ImGui::Dummy(ImVec2(84 * s, bigH));
    ImGui::SameLine();
    float rest = ImGui::GetContentRegionAvail().x;
    float bh = bigH + 6 * s;
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (ImGui::Button("-", ImVec2(bh * 0.9f, bh))) l.bpm = std::max(20.f, std::round(l.bpm) - 1.f);
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(bh * 0.9f, bh))) l.bpm = std::min(300.f, std::round(l.bpm) + 1.f);
    ImGui::PopItemFlag();
    ImGui::SameLine();
    float tapW = rest - (bh * 0.9f + sp) * 2;
    ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
    if (ImGui::Button("TAP", ImVec2(std::max(60.f * s, tapW), bh))) a.tempo.tap(a.time, l.bpm);
    ImGui::PopStyleColor(3);
    // speed multipliers + AUDIO toggle (with a live level bar)
    float audioW = 92 * s;
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);
    {
      float full = ImGui::GetContentRegionAvail().x;
      ImGui::BeginChild("##speedrow", ImVec2(full - audioW - sp, 38 * s), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * s, 2 * s));
      Choice("speed", kSpeedMultLabels, kNumSpeedMults, &l.speedMult, 0, 38 * s);
      ImGui::PopStyleVar();
      ImGui::EndChild();
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::SameLine();
    ImVec2 ap = ImGui::GetCursorScreenPos();
    AudioSettings& as = a.st.audio;
    if (Toggle("AUDIO", as.enabled, ImVec2(audioW, 38 * s), ImVec4(0.2f, 0.75f, 1.f, 1.f))) as.enabled = !as.enabled;
    if (as.enabled) {
      float lv = std::clamp(a.af.level, 0.f, 1.f);
      ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ap.x + 4 * s, ap.y + 32 * s),
                                                ImVec2(ap.x + 4 * s + (audioW - 8 * s) * lv, ap.y + 35 * s),
                                                IM_COL32(20, 20, 30, 255));
    }
  }

  // ---- effects in the panel (when they are not under the canvas) ----
  if (!a.st.layout.effectStrip) {
    ImGui::PushFont(a.fontSmall);
    EffectButtons(a, 4, ImVec2(0, 42 * s));
    ImGui::PopFont();
    float w = (ImGui::GetContentRegionAvail().x - sp) / 2;
    ClearBlackout(a, ImVec2(w, 42 * s), true);
  }

  // ---- everything below scrolls ----
  // mirror stays at the top with the tempo
  {
    static const char* mirrors[] = {"OFF", "L | R", "U | D", "QUAD", "RAD 6", "RAD 8"};
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::PushFont(a.fontSmall);
    ImGui::TextUnformatted("MIRROR");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    Choice("mirror", mirrors, MIRROR_COUNT, &l.mirror, 0, 40 * s);
  }

  ImGui::BeginChild("##live", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  if (a.st.audio.enabled) AudioLive(a);

  {
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "ON RELEASE (%s)", kEffectNames[l.effect]);
    SectionLabel(lbl);
    int& rel = l.releaseFx[l.effect];
    float& ft = l.fadeFx[l.effect];
    float w = (ImGui::GetContentRegionAvail().x - sp) / 2;
    if (Toggle("FADE", rel == RELEASE_FADE, ImVec2(w, 44 * s))) rel = RELEASE_FADE;
    ImGui::SameLine();
    if (Toggle("SNAP", rel == RELEASE_SNAP, ImVec2(w, 44 * s))) rel = RELEASE_SNAP;
    if (rel == RELEASE_FADE) {
      // logarithmic 0.1 .. 10 s
      float t = std::log10(std::clamp(ft, 0.1f, 10.f) / 0.1f) / 2.f;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.1f s", ft);
      if (TouchSlider("Fade time", &t, 0.f, 1.f, buf)) ft = 0.1f * std::pow(10.f, t * 2.f);
    }
  }

  EffectParams(a);

  SectionLabel("PALETTE");
  PaletteGrid(a);
  QuickColours(a);
  SectionLabel("COLOUR MODE");
  ColorModeChoice(a);

  if (!a.st.layout.sceneStrip) ScenesGrid(a);
  ImGui::Dummy(ImVec2(0, 24 * s));
  TouchScroll();
  ImGui::EndChild();
}

void SettingsPanel(App& a) {
  float s = S();
  static const char* tabs[ST_COUNT] = {"COLOURS", "LOOK", "OUTPUT", "SCREEN", "AUDIO", "DMX", "SYSTEM"};
  Choice("stabs", tabs, ST_COUNT, &a.st.settingsTab, 4, 40 * s);
  ImGui::BeginChild("##settings", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  float sp = ImGui::GetStyle().ItemSpacing.x;
  switch (a.st.settingsTab) {
    case ST_LOOK: LookTab(a); break;
    case ST_COLOURS: {
      Look& l = a.st.look;
      SectionLabel("PALETTES (tap to select, then edit below)");
      PaletteGrid(a);
      Palette& pal = a.st.palettes[l.palette];
      SectionLabel("NAME");
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%s", pal.name.c_str());
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputText("##name", buf, sizeof(buf))) pal.name = buf;
      SectionLabel("COLOURS (tap to edit)");
      float cw = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
      ColorEdit("LOW", pal.low, cw);
      ImGui::SameLine();
      ColorEdit("MID", pal.mid, cw);
      ImGui::SameLine();
      ColorEdit("HIGH", pal.high, cw);
      if (ImGui::Button("RESET THIS PALETTE", ImVec2(-1, 42 * s))) pal = defaultPalette(l.palette);
      SectionLabel("COLOUR MODE");
      ColorModeChoice(a);
      break;
    }
    case ST_OUTPUT: {
      OutputSettings& o = a.st.output;
      SectionLabel("RESOLUTION (max 1920 x 1080)");
      static const int presets[3][2] = {{1920, 1080}, {1280, 720}, {960, 540}};
      int sel = 3;
      for (int i = 0; i < 3; ++i)
        if (o.width == presets[i][0] && o.height == presets[i][1]) sel = i;
      if (a.customRes) sel = 3;
      static const char* res[] = {"1920x1080", "1280x720", "960x540", "CUSTOM"};
      if (Choice("res", res, 4, &sel, 2)) {
        a.customRes = sel == 3;
        if (sel < 3) {
          o.width = presets[sel][0];
          o.height = presets[sel][1];
        }
      }
      if (sel == 3) {
        static int cw = 0, chh = 0;
        if (!ImGui::IsAnyItemActive()) {
          cw = o.width;
          chh = o.height;
        }
        float half = (ImGui::GetContentRegionAvail().x - sp) / 2;
        ImGui::SetNextItemWidth(half);
        ImGui::InputInt("##w", &cw, 0, 0);
        if (ImGui::IsItemDeactivatedAfterEdit()) o.width = cw;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half);
        ImGui::InputInt("##h", &chh, 0, 0);
        if (ImGui::IsItemDeactivatedAfterEdit()) o.height = chh;
        sanitize(a.st);
      }
      SectionLabel("FRAME RATE");
      int f = o.fps == 30 ? 0 : 1;
      static const char* fps[] = {"30 FPS", "60 FPS"};
      if (Choice("fps", fps, 2, &f)) o.fps = f == 0 ? 30 : 60;

      SectionLabel("NDI OUTPUT");
      OnOff("NDI", &o.ndiEnabled, 50 * s);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Source name");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputText("##ndiname", a.ndiNameEdit, sizeof(a.ndiNameEdit));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string n = a.ndiNameEdit;
        if (n.empty()) n = "TouchFX";
        o.ndiName = n;
        a.ndiRetry = 0;
      }
      if (!o.ndiEnabled) {
        ImGui::TextColored(kTextDim, "NDI output is off.");
      } else if (a.ndiAdapterMissing) {
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "Paused: the chosen network card is not connected.");
      } else if (a.ndi.running()) {
        int n = a.ndi.connections();
        ImGui::PushStyleColor(ImGuiCol_Text, n > 0 ? ImVec4(0.3f, 0.9f, 0.4f, 1) : kText);
        ImGui::TextWrapped("Sending \"%s\" %dx%d @ %d fps%s - %d receiver%s", a.ndi.name().c_str(), a.out.w, a.out.h,
                           o.fps, o.alpha ? " with alpha" : "", n, n == 1 ? "" : "s");
        ImGui::PopStyleColor();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
        ImGui::TextWrapped("%s", a.ndi.error().empty() ? "Starting NDI..." : a.ndi.error().c_str());
        ImGui::PopStyleColor();
      }
      if (o.ndiEnabled) {
        SectionLabel("NDI NETWORK CARD (this PC)");
        AdapterPicker(a, "ndinet", o.ndiAdapter, o.ndiAdapterLabel);
        if (a.ndiAdapterMissing)
          Hint("The chosen network card is not connected: NDI is paused until it comes back.");
        else
          Hint("NDI is sent only on the chosen card (NDI restarts for a moment when you change it). AUTOMATIC "
               "keeps NDI's own settings. If the feed still shows on another network, restart TouchFX.");
      }
      SectionLabel("SPOUT OUTPUT (same PC: Resolume, TouchDesigner, OBS...)");
      if (SpoutOut::available()) {
        OnOff("SPOUT", &o.spout, 50 * s);
        {
          static char spName[64] = {};
          if (!ImGui::IsAnyItemActive() || spName[0] == 0) std::snprintf(spName, sizeof(spName), "%s", o.spoutName.c_str());
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted("Sender name");
          ImGui::SameLine();
          ImGui::SetNextItemWidth(-1);
          ImGui::InputText("##spoutname", spName, sizeof(spName));
          if (ImGui::IsItemDeactivatedAfterEdit()) o.spoutName = spName[0] ? spName : "TouchFX";
        }
        if (o.spout) {
          if (!a.spout.error().empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
            ImGui::TextWrapped("%s", a.spout.error().c_str());
            ImGui::PopStyleColor();
          } else if (a.spout.running()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.9f, 0.4f, 1));
            ImGui::TextWrapped("Sending \"%s\" %dx%d", o.spoutName.c_str(), a.out.w, a.out.h);
            ImGui::PopStyleColor();
          }
        }
        Hint("Spout shares the output GPU to GPU with apps on this computer, with no network and no extra "
             "latency. In Resolume: Sources > Spout > TouchFX. Use NDI for another computer.");
      } else {
        Hint("Spout is only available in the Windows version.");
      }
      SectionLabel("ALPHA CHANNEL");
      OnOff("ALPHA", &o.alpha);
      if (o.alpha) {
        int pm = o.premultiplied ? 1 : 0;
        static const char* pms[] = {"STRAIGHT", "PREMULTIPLIED"};
        if (Choice("pm", pms, 2, &pm)) o.premultiplied = pm == 1;
      }
      Hint("With alpha on, black becomes transparent so the visuals can be layered over other content. "
           "Use STRAIGHT for Resolume; switch to PREMULTIPLIED if edges look dark or haloed in your receiver.");
      break;
    }
    case ST_SCREEN: {
      ExternalSettings& e = a.st.external;
      SectionLabel("EXTERNAL DISPLAY (shows the output on another screen)");
      OnOff("EXTERNAL OUTPUT", &e.enabled, 50 * s);
      int nd = std::max(1, SDL_GetNumVideoDisplays());
      int mainDisp = SDL_GetWindowDisplayIndex(a.window);
      for (int i = 0; i < nd; ++i) {
        SDL_Rect r{};
        SDL_GetDisplayBounds(i, &r);
        const char* nm = SDL_GetDisplayName(i);
        char label[128];
        std::snprintf(label, sizeof(label), "Display %d: %s  %dx%d%s##d%d", i + 1, nm ? nm : "?", r.w, r.h,
                      i == mainDisp ? "  (control screen)" : "", i);
        if (Toggle(label, std::clamp(e.display, 0, nd - 1) == i, ImVec2(-1, 44 * s))) e.display = i;
      }
      if (std::clamp(e.display, 0, nd - 1) == mainDisp)
        Hint("This is the screen TouchFX runs on, so the output opens as a window there.");
      SectionLabel("FIT");
      static const char* fits[] = {"FIT", "FILL", "STRETCH"};
      Choice("fit", fits, 3, &e.fit);
      OnOff("WINDOWED", &e.windowed);
      Hint("Windowed opens a movable window instead of going fullscreen (handy for testing on one screen).");

      SectionLabel("LAYOUT");
      int side = a.st.layout.panelLeft ? 0 : 1;
      static const char* sides[] = {"PANEL LEFT", "PANEL RIGHT"};
      if (Choice("side", sides, 2, &side)) a.st.layout.panelLeft = side == 0;
      {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f %%", a.st.layout.panelWidth * 100.f);
        TouchSlider("Panel width", &a.st.layout.panelWidth, 0.6f, 1.8f, buf);
      }
      OnOff("EFFECTS UNDER CANVAS", &a.st.layout.effectStrip);
      OnOff("SCENES UNDER CANVAS", &a.st.layout.sceneStrip);
      {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2fx", a.uiScaleEdit);
        TouchSlider("UI size", &a.uiScaleEdit, 0.6f, 2.f, buf);
        if (!ImGui::IsItemActive() && a.uiScaleEdit != a.st.uiScale) a.st.uiScale = a.uiScaleEdit;
      }
      {
        bool fs = a.isFullscreen;
        if (OnOff("FULLSCREEN (F11 / Alt+Enter)", &fs)) setFullscreen(a, fs);
      }
      break;
    }
    case ST_AUDIO: AudioSettingsTab(a); break;
    case ST_DMX: DmxTab(a); break;
    default: {
      if (platformHasSystemGestures()) {
        SectionLabel("WINDOWS TOUCH GESTURES");
        OnOff("BLOCK 3/4-FINGER GESTURES", &a.st.blockGestures, 50 * s);
        Hint("While TouchFX runs, Windows' 3- and 4-finger swipes (switch apps, show desktop) are turned off "
             "and put back when you quit. Edge swipes are blocked while TouchFX is fullscreen (F11). "
             "If 3/4-finger swipes still work, turn them off once in Windows touch settings:");
        if (ImGui::Button("OPEN WINDOWS TOUCH SETTINGS", ImVec2(-1, 44 * s))) platformOpenTouchSettings();
      }
      SectionLabel("SHOW");
      {
        char b[160];
        std::snprintf(b, sizeof(b), "Current show: %s", a.showName.c_str());
        ImGui::TextUnformatted(b);
        float w3 = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
        if (Toggle("SWITCH SHOW", false, ImVec2(w3, 46 * s))) closeShow(a);
        ImGui::SameLine();
        if (ImGui::Button("SHOW FOLDER", ImVec2(w3, 46 * s))) platformOpenFolder(showsDir());
        ImGui::SameLine();
        if (ImGui::Button("LOGS FOLDER", ImVec2(w3, 46 * s))) platformOpenFolder(logDir());
        Hint("The show (looks, scenes, cue lists, palettes, DMX patch) saves automatically. Outputs, screens, "
             "audio input and touch settings belong to this PC.");
      }
      SectionLabel("TOUCH");
      IntSlider("Touch points", &a.st.maxTouches, 1, kMaxTouches);
      Hint("How many fingers can draw at once (up to 10, if your screen supports it). Each finger is "
           "multiplied by the mirror copies.");
      OnOff("SHOW TOUCHES ON PREVIEW", &a.st.showTouches);
      SectionLabel("ABOUT");
      ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
      ImGui::PushFont(a.fontSmall);
      ImGui::TextWrapped("TouchFX %s  -  %.0f fps", TOUCHFX_VERSION, a.fps);
      ImGui::TextWrapped("Settings save automatically to: %s", a.settingsPath.c_str());
      ImGui::TextWrapped("Keys: SPACE tap tempo, C clear, B blackout, F11 or Alt+Enter fullscreen, Esc leave "
                         "fullscreen. Last key pressed: %s", a.lastKey);
      ImGui::TextWrapped("NDI(R) is a registered trademark of Vizrt NDI AB.");
      ImGui::PopFont();
      ImGui::PopStyleColor();
      SectionLabel("RESET");
      static double armedAt = -10;
      bool armed = a.time - armedAt < 3.0;
      if (Toggle(armed ? "TAP AGAIN TO RESET EVERYTHING" : "RESET ALL SETTINGS", armed, ImVec2(-1, 46 * s), kDanger)) {
        if (armed) {
          AppState d = defaultState();
          d.fullscreen = a.st.fullscreen;
          a.st = d;
          a.uiScaleEdit = d.uiScale;
          std::snprintf(a.ndiNameEdit, sizeof(a.ndiNameEdit), "%s", d.output.ndiName.c_str());
          armedAt = -10;
        } else {
          armedAt = a.time;
        }
      }
      break;
    }
  }
  ImGui::Dummy(ImVec2(0, 24 * s));
  TouchScroll();
  ImGui::EndChild();
}

// Scenes bar under the canvas.
void SceneStrip(App& a) {
  if (!a.st.layout.sceneStrip || a.strip.w <= 0) return;
  float s = S();
  ImGui::SetNextWindowPos(ImVec2(a.strip.x, a.strip.y));
  ImGui::SetNextWindowSize(ImVec2(a.strip.w, a.strip.h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 8 * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 6 * s));
  ImGui::Begin("##strip", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
  float sp = 6 * s;
  float avail = ImGui::GetContentRegionAvail().x;
  float rowsH = ImGui::GetContentRegionAvail().y;
  int rows = rowsH > 100 * s ? 2 : 1;
  int cols = kNumScenes / rows;
  float ctlW = ImGui::CalcTextSize("DELETE...").x + 28 * s;
  float bw = (avail - (ctlW + sp) * 2 - sp * (cols - 1)) / cols;
  float bh = (rowsH - sp * (rows - 1)) / rows;
  // STORE / DELETE side by side, full height, on the left
  StoreDeleteButtons(a, ImVec2(ctlW, rowsH), true);
  ImGui::SameLine();
  ImGui::BeginGroup();
  for (int i = 0; i < kNumScenes; ++i) {
    if (i % cols) ImGui::SameLine();
    SceneButton(a, i, ImVec2(bw, bh));
  }
  ImGui::EndGroup();
  ImGui::End();
  ImGui::PopStyleVar(3);
}

// Effect bar under the canvas: CLEAR / BLACKOUT on the left, then the 16 effects.
void EffectStrip(App& a) {
  if (!a.st.layout.effectStrip || a.fxStrip.w <= 0) return;
  float s = S();
  ImGui::SetNextWindowPos(ImVec2(a.fxStrip.x, a.fxStrip.y));
  ImGui::SetNextWindowSize(ImVec2(a.fxStrip.w, a.fxStrip.h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 8 * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 6 * s));
  ImGui::Begin("##fxstrip", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
  float sp = 6 * s;
  float avail = ImGui::GetContentRegionAvail().x;
  float rowsH = ImGui::GetContentRegionAvail().y;
  int rows = rowsH > 90 * s ? 2 : 1;
  int cols = FX_COUNT / rows;
  float ctlW = ImGui::CalcTextSize("BLACKOUT").x + 28 * s;
  float bw = (avail - ctlW - sp - sp * (cols - 1)) / cols;
  float bh = (rowsH - sp * (rows - 1)) / rows;
  ImGui::BeginGroup();
  ClearBlackout(a, ImVec2(ctlW, rows == 2 ? bh : (rowsH - sp) / 2), false);
  ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::PushFont(bw < 96 * s ? a.fontSmall : a.fontMain);
  EffectButtons(a, cols, ImVec2(bw, bh));
  ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::End();
  ImGui::PopStyleVar(3);
}

void Canvas(App& a) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImVec2 p0(a.canvas.x, a.canvas.y), p1(a.canvas.x + a.canvas.w, a.canvas.y + a.canvas.h);
  if (a.st.output.alpha) {  // checkerboard shows transparency
    float c = 16.f * S();
    dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 44, 255));
    dl->PushClipRect(p0, p1, true);
    for (float y = p0.y; y < p1.y; y += c)
      for (float x = p0.x + (int((y - p0.y) / c) & 1) * c; x < p1.x; x += 2 * c)
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + c, y + c), IM_COL32(64, 64, 70, 255));
    dl->PopClipRect();
  }
  dl->AddImage((ImTextureID)(intptr_t)a.out.tex, p0, p1, ImVec2(0, 1), ImVec2(1, 0));
  dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), IM_COL32(80, 80, 90, 255));

  // mirror guides (preview only)
  ImU32 guide = IM_COL32(255, 255, 255, 28);
  float mx = a.canvas.x + a.canvas.w * 0.5f, my = a.canvas.y + a.canvas.h * 0.5f;
  int m = a.st.look.mirror;
  if (m == MIRROR_LR || m == MIRROR_QUAD) dl->AddLine(ImVec2(mx, p0.y), ImVec2(mx, p1.y), guide, 1.5f);
  if (m == MIRROR_UD || m == MIRROR_QUAD) dl->AddLine(ImVec2(p0.x, my), ImVec2(p1.x, my), guide, 1.5f);
  if (m == MIRROR_RADIAL6 || m == MIRROR_RADIAL8) {
    int n = m == MIRROR_RADIAL6 ? 6 : 8;
    float r = std::max(a.canvas.w, a.canvas.h);
    dl->PushClipRect(p0, p1, true);
    for (int i = 0; i < n; ++i) {
      float ang = kTau * (i + 0.5f) / n;
      dl->AddLine(ImVec2(mx, my), ImVec2(mx + std::cos(ang) * r, my - std::sin(ang) * r), guide, 1.5f);
    }
    dl->PopClipRect();
  }
  if (a.st.showTouches) {
    for (const Emitter& e : a.live) {
      ImVec2 c(a.canvas.x + e.pos.x * a.canvas.w, a.canvas.y + (1.f - e.pos.y) * a.canvas.h);
      bool virt = e.touch >= kMaxTouches;
      bool cursor = e.touch >= kMaxTouches + 4;
      ImU32 col = cursor ? IM_COL32(120, 255, 140, 170) : virt ? IM_COL32(80, 200, 255, 150) : IM_COL32(255, 255, 255, 150);
      if (e.mirror == 0)
        dl->AddCircle(c, 26 * S(), col, 32, 2.f * S());
      else
        dl->AddCircle(c, 12 * S(), IM_COL32(255, 255, 255, 60), 24, 1.5f * S());
    }
  }
  if (a.dmxActive && (a.dmxDim < 0.02f || a.dmxBlackout) && !a.blackout) {
    const char* t = a.dmxBlackout ? "DMX BLACKOUT" : "DMX DIMMER AT 0";
    ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(mx - ts.x * 0.5f, my - ts.y * 0.5f), IM_COL32(255, 180, 60, 230), t);
  }
  if (a.blackout) {
    const char* t = "BLACKOUT";
    ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(mx - ts.x * 0.5f, my - ts.y * 0.5f), IM_COL32(255, 60, 60, 220), t);
  }
}

}  // namespace ui

using namespace ui;

void rebuildFonts(App& a, float scale) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;
  a.fontMain = io.Fonts->AddFontFromMemoryTTF((void*)kFontData, int(kFontSize), 19.f * scale, &cfg);
  a.fontBig = io.Fonts->AddFontFromMemoryTTF((void*)kFontData, int(kFontSize), 46.f * scale, &cfg);
  a.fontSmall = io.Fonts->AddFontFromMemoryTTF((void*)kFontData, int(kFontSize), 15.f * scale, &cfg);
  io.FontDefault = a.fontMain;
  if (a.fontsBuilt) {
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
  }
  a.fontsBuilt = true;
  a.fontScale = scale;

  ImGuiStyle st;
  ImGui::StyleColorsDark(&st);
  st.WindowRounding = 0;
  st.FrameRounding = 6;
  st.GrabRounding = 6;
  st.ChildRounding = 6;
  st.PopupRounding = 8;
  st.FramePadding = ImVec2(10, 10);
  st.ItemSpacing = ImVec2(7, 7);
  st.WindowPadding = ImVec2(12, 12);
  st.ScrollbarSize = 12;
  st.ScrollbarRounding = 6;
  st.GrabMinSize = 34;
  st.WindowBorderSize = 0;
  st.TouchExtraPadding = ImVec2(2, 2);
  ImVec4* c = st.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.085f, 0.085f, 0.10f, 1);
  c[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.06f, 0.075f, 1);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_Button] = ImVec4(0.19f, 0.19f, 0.22f, 1);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.21f, 0.25f, 1);
  c[ImGuiCol_ButtonActive] = kAccentDim;
  c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.19f, 1);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.23f, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.26f, 1);
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.6f, 0.3f, 1);
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.3f, 1);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.075f, 1);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.3f, 0.3f, 0.35f, 1);
  c[ImGuiCol_TextSelectedBg] = kAccentDim;
  c[ImGuiCol_NavCursor] = ImVec4(0, 0, 0, 0);
  st.ScaleAllSizes(scale);
  ImGui::GetStyle() = st;
}

void drawUI(App& a) {
  if (!a.showLoaded) {
    Launcher(a);
    KeyboardFrame(a);
    QuitConfirmFrame(a);
    return;
  }
  float s = S();
  g_sliding = false;

  Canvas(a);
  EffectStrip(a);
  SceneStrip(a);

  ImGui::SetNextWindowPos(ImVec2(a.panel.x, a.panel.y));
  ImGui::SetNextWindowSize(ImVec2(a.panel.w, a.panel.h));
  ImGui::Begin("##panel", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);

  // ---- header: title + clock, then NDI / DMX status lights and system stats ----
  ImGui::TextColored(kAccent, "TOUCHFX");
  ImGui::SameLine();
  ImGui::PushFont(a.fontSmall);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(kTextDim, "%.24s", a.showName.c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  {
    std::time_t now = std::time(nullptr);
    char clk[16];
    std::strftime(clk, sizeof(clk), "%H:%M:%S", std::localtime(&now));
    float tw = ImGui::CalcTextSize(clk).x;
    const char* fsLabel = a.isFullscreen ? "EXIT FULL" : "FULL";
    ImGui::PushFont(a.fontSmall);
    float bw = ImGui::CalcTextSize("EXIT FULL").x + 16 * s;
    ImGui::PopFont();
    float sp2 = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - tw - bw - sp2));
    ImGui::PushFont(a.fontSmall);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * s, 2 * s));
    if (Toggle(fsLabel, a.isFullscreen, ImVec2(bw, ImGui::GetFontSize() * 1.25f + 4 * s))) setFullscreen(a, !a.isFullscreen);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted(clk);
  }
  ImGui::PushFont(a.fontSmall);
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto light = [&](ImU32 col, const char* text) {
      ImVec2 p = ImGui::GetCursorScreenPos();
      dl->AddCircleFilled(ImVec2(p.x + 6 * s, p.y + ImGui::GetFontSize() * 0.5f), 5.5f * s, col);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16 * s);
      ImGui::TextUnformatted(text);
    };
    const ImU32 grey = IM_COL32(110, 110, 120, 255), red = IM_COL32(230, 70, 60, 255),
                green = IM_COL32(60, 220, 100, 255), yellow = IM_COL32(240, 190, 40, 255);
    const OutputSettings& o = a.st.output;
    bool ok = o.ndiEnabled && a.ndi.running();
    int n = a.ndi.connections();
    char status[96];
    if (!o.ndiEnabled) std::snprintf(status, sizeof(status), "NDI OFF");
    else if (a.ndiAdapterMissing) std::snprintf(status, sizeof(status), "NDI NO NETWORK");
    else if (!ok) std::snprintf(status, sizeof(status), "NDI ERROR");
    else std::snprintf(status, sizeof(status), "NDI %dx%d%s %d rx", a.out.w, a.out.h, o.alpha ? " A" : "", n);
    light(!o.ndiEnabled ? grey : !ok ? red : n > 0 ? green : yellow, status);
    ImGui::SameLine(0, 14 * s);
    // DMX: green = receiving, yellow = signal stopped (last look held), red = error / nothing yet
    const DmxSettings& d = a.st.dmx;
    double age = a.dmx.secondsSinceData();
    char dmxs[96];
    ImU32 dcol;
    if (d.protocol == DMXP_OFF) {
      std::snprintf(dmxs, sizeof(dmxs), "DMX OFF");
      dcol = grey;
    } else {
      const char* proto = d.protocol == DMXP_SACN ? "sACN" : "ART-NET";
      int u = d.protocol == DMXP_SACN ? d.sacnUniverse : d.artnetUniverse;
      std::snprintf(dmxs, sizeof(dmxs), "%s U%d.%d", proto, u, d.address);
      dcol = !a.dmx.error().empty() && age > 1e8 ? red : age < 1.0 ? green : age < 1e8 ? yellow : red;
      if (a.dmxAdapterMissing) {
        std::snprintf(dmxs, sizeof(dmxs), "DMX NO NETWORK");
        dcol = red;
      }
    }
    light(dcol, dmxs);
    // system stats
    char st[128];
    float cpu = a.stats.cpu(), gpu = a.stats.gpu();
    char cpuS[16], gpuS[16];
    if (cpu >= 0) std::snprintf(cpuS, sizeof(cpuS), "%.0f%%", cpu); else std::snprintf(cpuS, sizeof(cpuS), "--");
    if (gpu >= 0) std::snprintf(gpuS, sizeof(gpuS), "%.0f%%", gpu); else std::snprintf(gpuS, sizeof(gpuS), "--");
    std::snprintf(st, sizeof(st), "CPU %s   GPU %s   RAM %.1f/%.0f GB   %.0f FPS", cpuS, gpuS, a.stats.ramUsed(),
                  a.stats.ramTotal(), a.fps);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(st);
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  {
    // LIVE | CUES | SETTINGS | SHOW MODE   (tab values: 0 live, 2 cues, 1 settings)
    float sp4 = ImGui::GetStyle().ItemSpacing.x;
    float w = (ImGui::GetContentRegionAvail().x - sp4 * 3) / 4, h = 46 * s;
    bool locked = ShowModeLocked(a);
    if (locked && a.st.tab == 1) a.st.tab = 0;
    if (Toggle("LIVE", a.st.tab == 0, ImVec2(w, h))) a.st.tab = 0;
    ImGui::SameLine();
    if (Toggle("CUES", a.st.tab == 2, ImVec2(w, h))) a.st.tab = 2;
    ImGui::SameLine();
    ImGui::BeginDisabled(locked);
    if (Toggle(locked ? "LOCKED" : "SETTINGS", a.st.tab == 1, ImVec2(w, h))) a.st.tab = 1;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ShowModeButton(a, ImVec2(w, h));
  }
  ImGui::Separator();

  if (a.st.tab == 0) LivePanel(a);
  else if (a.st.tab == 2) CuesPanel(a);
  else SettingsPanel(a);
  ImGui::End();

  std::string txt;
  if (KeyboardResult("scenename", txt) && g_renameScene >= 0 && g_renameScene < kNumScenes &&
      a.st.scenes[g_renameScene].name != txt) {
    // naming right after STORE belongs to the STORE undo step
    bool afterStore = !a.undo.undo.empty() && a.undo.undo.back().label.rfind("store scene", 0) == 0 &&
                      a.st.scenes[g_renameScene].name.empty();
    if (!afterStore) undoPush(a, "rename scene " + std::to_string(g_renameScene + 1));
    a.st.scenes[g_renameScene].name = txt;
  }
  KeyboardFrame(a);
  QuitConfirmFrame(a);
}
