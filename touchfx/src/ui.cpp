// Touch-friendly control panel (Dear ImGui).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

extern const unsigned char kFontData[];
extern const std::size_t kFontSize;

namespace {

const ImVec4 kAccent(1.00f, 0.47f, 0.10f, 1.f);
const ImVec4 kAccentDim(0.55f, 0.25f, 0.05f, 1.f);
const ImVec4 kDanger(0.85f, 0.12f, 0.15f, 1.f);
const ImVec4 kText(0.92f, 0.92f, 0.94f, 1.f);
const ImVec4 kTextDim(0.55f, 0.55f, 0.60f, 1.f);

float S() { return ImGui::GetFontSize() / 19.f; }  // current UI scale

ImU32 col32(const Color3& c, float a = 1.f) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], a));
}

// A big button that lights up when "on".
bool Toggle(const char* label, bool on, ImVec2 size, ImVec4 onColor = kAccent) {
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
bool Choice(const char* id, const char* const* labels, int n, int* value, int cols = 0, float h = 0) {
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

void SectionLabel(const char* text) {
  ImGui::Dummy(ImVec2(0, 2.f * S()));
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

// Label on the left, wide slider on the right showing 0-100 %.
bool SliderPct(const char* label, float* v, bool centered = false) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f + ImGui::GetCursorPosX() * 0.f);
  ImGui::SetNextItemWidth(-1);
  bool ch;
  if (centered) {
    float t = (*v - 0.5f) * 200.f;
    ch = ImGui::SliderFloat("##s", &t, -100.f, 100.f, "%+.0f", ImGuiSliderFlags_AlwaysClamp);
    if (ch) *v = t / 200.f + 0.5f;
  } else {
    float t = *v * 100.f;
    ch = ImGui::SliderFloat("##s", &t, 0.f, 100.f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
    if (ch) *v = t / 100.f;
  }
  ImGui::PopID();
  return ch;
}

bool SliderIntL(const char* label, int* v, int lo, int hi) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f);
  ImGui::SetNextItemWidth(-1);
  bool ch = ImGui::SliderInt("##s", v, lo, hi, "%d", ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopID();
  return ch;
}

bool SliderFloatL(const char* label, float* v, float lo, float hi, const char* fmt) {
  ImGui::PushID(label);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.34f);
  ImGui::SetNextItemWidth(-1);
  bool ch = ImGui::SliderFloat("##s", v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
  ImGui::PopID();
  return ch;
}

// Drag on empty space inside the current window scrolls it (touch friendly).
void TouchScroll() {
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.f) &&
      !ImGui::IsAnyItemActive())
    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
}

const char* kEffectNames[FX_COUNT] = {"FLUID", "PARTICLES", "KALEIDO", "LASERS", "STROBE"};

// ---------------------------------------------------------------------------
void EffectTab(App& a) {
  Look& l = a.st.look;
  switch (l.effect) {
    case FX_FLUID: {
      FluidSettings& s = l.fluid;
      SectionLabel("FLUID / SMOKE");
      SliderPct("Size", &s.size);
      SliderPct("Force", &s.force);
      SliderPct("Swirl", &s.swirl);
      SliderPct("Trail", &s.trail);
      SliderPct("Intensity", &s.intensity);
      if (ImGui::Button("RESET FLUID", ImVec2(-1, 40 * S()))) s = FluidSettings();
      break;
    }
    case FX_PARTICLES: {
      ParticleSettings& s = l.particles;
      SectionLabel("PARTICLES");
      SliderPct("Amount", &s.amount);
      SliderPct("Size", &s.size);
      SliderPct("Life", &s.life);
      SliderPct("Speed", &s.speed);
      SliderPct("Spread", &s.spread);
      SliderPct("Gravity", &s.gravity, true);
      SliderPct("Trail", &s.trail);
      SectionLabel("PARTICLE STROBE");
      static const char* ps[] = {"OFF", "SYNC", "SPARKLE"};
      Choice("pstrobe", ps, 3, &s.strobe);
      if (s.strobe != PSTROBE_OFF) {
        SectionLabel("FLASHES PER BEAT");
        Choice("prate", kDivisionLabels, kNumDivisions, &s.strobeRate);
      }
      if (ImGui::Button("RESET PARTICLES", ImVec2(-1, 40 * S()))) s = ParticleSettings();
      break;
    }
    case FX_KALEIDO: {
      KaleidoSettings& s = l.kaleido;
      SectionLabel("KALEIDOSCOPE");
      SliderIntL("Segments", &s.segments, 2, 16);
      SliderPct("Rotation", &s.rotation, true);
      SliderPct("Zoom", &s.zoom, true);
      SliderPct("Warp", &s.warp);
      SliderPct("Brush", &s.brush);
      SliderPct("Trail", &s.trail);
      if (ImGui::Button("RESET KALEIDO", ImVec2(-1, 40 * S()))) s = KaleidoSettings();
      break;
    }
    case FX_LASERS: {
      LaserSettings& s = l.lasers;
      SectionLabel("LINES / LASERS");
      static const char* modes[] = {"FAN", "BURST", "WEB", "SCAN"};
      Choice("lmode", modes, 4, &s.mode);
      SliderIntL("Beams", &s.beams, 1, 32);
      SliderPct("Rotation", &s.rotation, true);
      SliderPct("Width", &s.width);
      SliderPct("Glow", &s.glow);
      SliderPct("Length", &s.length);
      SliderPct("Trail", &s.trail);
      if (ImGui::Button("RESET LASERS", ImVec2(-1, 40 * S()))) s = LaserSettings();
      break;
    }
    case FX_STROBE: {
      StrobeSettings& s = l.strobe;
      SectionLabel("STROBE / FLASH");
      static const char* shapes[] = {"FULL", "SPOT", "H-BAR", "V-BAR", "SPARKLE"};
      Choice("sshape", shapes, 5, &s.shape, 3);
      SectionLabel("RATE");
      int mode = s.freeHz ? 1 : 0;
      static const char* rm[] = {"BPM SYNC", "FREE (Hz)"};
      if (Choice("srm", rm, 2, &mode)) s.freeHz = mode == 1;
      if (s.freeHz) SliderFloatL("Hz", &s.hz, 0.5f, 25.f, "%.1f Hz");
      else Choice("srate", kDivisionLabels, kNumDivisions, &s.rate);
      SliderPct("Flash length", &s.duty);
      SliderPct("Size", &s.size);
      if (Toggle(s.colorCycle ? "COLOUR CYCLE: ON" : "COLOUR CYCLE: OFF", s.colorCycle, ImVec2(-1, 44 * S())))
        s.colorCycle = !s.colorCycle;
      if (ImGui::Button("RESET STROBE", ImVec2(-1, 40 * S()))) s = StrobeSettings();
      break;
    }
  }
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

void ColorTab(App& a) {
  Look& l = a.st.look;
  SectionLabel("PALETTES");
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = (ImGui::GetContentRegionAvail().x - sp * 4) / 5;
  float h = 58 * S();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < kNumPalettes; ++i) {
    if (i % 5) ImGui::SameLine();
    ImGui::PushID(i);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("pal", ImVec2(w, h))) l.palette = i;
    const Palette& pal = a.st.palettes[i];
    float th = h - 18 * S();
    float bw = w / 3;
    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + th), col32(pal.low), 4 * S(), ImDrawFlags_RoundCornersTopLeft);
    dl->AddRectFilled(ImVec2(p.x + bw, p.y), ImVec2(p.x + 2 * bw, p.y + th), col32(pal.mid));
    dl->AddRectFilled(ImVec2(p.x + 2 * bw, p.y), ImVec2(p.x + w, p.y + th), col32(pal.high), 4 * S(),
                      ImDrawFlags_RoundCornersTopRight);
    dl->AddRectFilled(ImVec2(p.x, p.y + th), ImVec2(p.x + w, p.y + h), IM_COL32(35, 35, 40, 255), 4 * S(),
                      ImDrawFlags_RoundCornersBottom);
    ImGui::PushFont(a.fontSmall);
    char name[32];
    std::snprintf(name, sizeof(name), "%.10s", pal.name.c_str());
    ImVec2 ts = ImGui::CalcTextSize(name);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + th + (18 * S() - ts.y) * 0.5f),
                l.palette == i ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(kText), name);
    ImGui::PopFont();
    if (l.palette == i)
      dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + w + 2, p.y + h + 2), ImGui::GetColorU32(kAccent), 5 * S(), 0,
                  3 * S());
    ImGui::PopID();
  }

  Palette& pal = a.st.palettes[l.palette];
  SectionLabel("EDIT PALETTE");
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%s", pal.name.c_str());
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##name", buf, sizeof(buf))) pal.name = buf;
  float cw = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
  ColorEdit("LOW", pal.low, cw);
  ImGui::SameLine();
  ColorEdit("MID", pal.mid, cw);
  ImGui::SameLine();
  ColorEdit("HIGH", pal.high, cw);
  if (ImGui::Button("RESET THIS PALETTE", ImVec2(-1, 40 * S()))) pal = defaultPalette(l.palette);

  SectionLabel("COLOUR MODE");
  static const char* modes[] = {"GRADIENT", "SPEED", "POSITION", "BEAT", "RANDOM"};
  Choice("cmode", modes, 5, &l.colorMode, 3);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  static const char* help[] = {
      "Low > Mid > High over the life of the effect.", "Slow finger = Low, fast finger = High.",
      "Left of the canvas = Low, right = High.", "Steps Low > Mid > High on every beat.",
      "A random mix of the three colours."};
  ImGui::TextWrapped("%s", help[l.colorMode]);
  ImGui::PopStyleColor();
  SectionLabel("OUTPUT LEVEL");
  float b = l.brightness * 100.f;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat("##bright", &b, 0.f, 200.f, "Brightness %.0f %%", ImGuiSliderFlags_AlwaysClamp))
    l.brightness = b / 100.f;
}

void ScenesTab(App& a) {
  SectionLabel("SCENES");
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("A scene saves the full look: effect, its settings, mirror, release, palette and BPM. "
                     "Tap STORE then a slot to save; tap a slot to recall.");
  ImGui::PopStyleColor();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float w = (ImGui::GetContentRegionAvail().x - sp) / 2;
  if (Toggle(a.storeArmed ? "TAP A SLOT TO STORE" : "STORE", a.storeArmed, ImVec2(w, 50 * S()))) {
    a.storeArmed = !a.storeArmed;
    a.deleteArmed = false;
  }
  ImGui::SameLine();
  if (Toggle(a.deleteArmed ? "TAP A SLOT TO DELETE" : "DELETE", a.deleteArmed, ImVec2(w, 50 * S()), kDanger)) {
    a.deleteArmed = !a.deleteArmed;
    a.storeArmed = false;
  }
  ImGui::Dummy(ImVec2(0, 4 * S()));
  float cw = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
  for (int i = 0; i < kNumScenes; ++i) {
    if (i % 3) ImGui::SameLine();
    Scene& sc = a.st.scenes[i];
    char label[64];
    if (sc.used)
      std::snprintf(label, sizeof(label), "%d\n%s\n%s##s%d", i + 1, kEffectNames[sc.look.effect],
                    a.st.palettes[sc.look.palette].name.c_str(), i);
    else
      std::snprintf(label, sizeof(label), "%d\n-##s%d", i + 1, i);
    if (!sc.used) ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    bool pressed = Toggle(label, sc.used && a.lastScene == i, ImVec2(cw, 84 * S()));
    if (!sc.used) ImGui::PopStyleColor();
    if (pressed) {
      if (a.storeArmed) {
        sc.used = true;
        sc.look = a.st.look;
        a.storeArmed = false;
        a.lastScene = i;
      } else if (a.deleteArmed) {
        sc = Scene();
        a.deleteArmed = false;
        if (a.lastScene == i) a.lastScene = -1;
      } else if (sc.used) {
        a.st.look = sc.look;
        a.lastScene = i;
      }
    }
  }
}

void OutputTab(App& a) {
  OutputSettings& o = a.st.output;
  SectionLabel("RESOLUTION (max 1920 x 1080)");
  static const int presets[3][2] = {{1920, 1080}, {1280, 720}, {960, 540}};
  int sel = 3;
  for (int i = 0; i < 3; ++i)
    if (o.width == presets[i][0] && o.height == presets[i][1]) sel = i;
  if (a.customRes) sel = 3;
  static const char* res[] = {"1920x1080", "1280x720", "960x540", "CUSTOM"};
  if (Choice("res", res, 4, &sel)) {
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
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
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
  if (Toggle(o.ndiEnabled ? "NDI: ON" : "NDI: OFF", o.ndiEnabled, ImVec2(-1, 50 * S()))) o.ndiEnabled = !o.ndiEnabled;
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
  } else if (a.ndi.running()) {
    int n = a.ndi.connections();
    ImGui::TextColored(n > 0 ? ImVec4(0.3f, 0.9f, 0.4f, 1) : kText, "Sending \"%s\" %dx%d @ %d fps - %d receiver%s",
                       a.ndi.name().c_str(), a.out.w, a.out.h, o.fps, n, n == 1 ? "" : "s");
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
    ImGui::TextWrapped("%s", a.ndi.error().empty() ? "Starting NDI..." : a.ndi.error().c_str());
    ImGui::PopStyleColor();
  }

  SectionLabel("DISPLAY");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("UI size");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  ImGui::SliderFloat("##uiscale", &a.uiScaleEdit, 0.6f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::IsItemDeactivatedAfterEdit()) a.st.uiScale = a.uiScaleEdit;
  if (Toggle(a.st.fullscreen ? "FULLSCREEN: ON  (F11)" : "FULLSCREEN: OFF  (F11)", a.st.fullscreen,
             ImVec2(-1, 48 * S()))) {
    a.st.fullscreen = !a.st.fullscreen;
    SDL_SetWindowFullscreen(a.window, a.st.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  }

  SectionLabel("ABOUT");
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::PushFont(a.fontSmall);
  ImGui::TextWrapped("TouchFX %s  -  %.0f fps", TOUCHFX_VERSION, a.fps);
  ImGui::TextWrapped("Settings save automatically to: %s", a.settingsPath.c_str());
  ImGui::TextWrapped("Keys: SPACE tap tempo, C clear, B blackout, F11 fullscreen.");
  ImGui::TextWrapped("NDI(R) is a registered trademark of Vizrt NDI AB.");
  ImGui::PopFont();
  ImGui::PopStyleColor();
}

void CanvasOverlay(App& a) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  ImVec2 p0(a.cx, a.cy), p1(a.cx + a.cw, a.cy + a.ch);
  dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), IM_COL32(80, 80, 90, 255));
  // mirror guides
  ImU32 guide = IM_COL32(255, 255, 255, 28);
  float mx = a.cx + a.cw * 0.5f, my = a.cy + a.ch * 0.5f;
  int m = a.st.look.mirror;
  if (m == MIRROR_LR || m == MIRROR_QUAD) dl->AddLine(ImVec2(mx, p0.y), ImVec2(mx, p1.y), guide, 1.5f);
  if (m == MIRROR_UD || m == MIRROR_QUAD) dl->AddLine(ImVec2(p0.x, my), ImVec2(p1.x, my), guide, 1.5f);
  if (m == MIRROR_RADIAL6 || m == MIRROR_RADIAL8) {
    int n = m == MIRROR_RADIAL6 ? 6 : 8;
    float r = std::max(a.cw, a.ch);
    for (int i = 0; i < n; ++i) {
      float ang = kTau * (i + 0.5f) / n;
      dl->AddLine(ImVec2(mx, my), ImVec2(mx + std::cos(ang) * r, my - std::sin(ang) * r), guide, 1.5f);
    }
    dl->PushClipRect(p0, p1);
    dl->PopClipRect();
  }
  // finger markers (preview only, never in the output)
  float aspect = float(a.st.output.width) / a.st.output.height;
  for (const Emitter& e : a.live) {
    ImVec2 c(a.cx + e.pos.x * a.cw, a.cy + (1.f - e.pos.y) * a.ch);
    if (e.mirror == 0)
      dl->AddCircle(c, 26 * S(), IM_COL32(255, 255, 255, 150), 32, 2.f * S());
    else
      dl->AddCircle(c, 12 * S(), IM_COL32(255, 255, 255, 60), 24, 1.5f * S());
  }
  (void)aspect;
  if (a.blackout) {
    const char* t = "BLACKOUT";
    ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(mx - ts.x * 0.5f, my - ts.y * 0.5f), IM_COL32(255, 60, 60, 220), t);
  }
}

}  // namespace

void rebuildFonts(App& a, float scale) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;
  a.fontMain = io.Fonts->AddFontFromMemoryTTF((void*)kFontData, int(kFontSize), 19.f * scale, &cfg);
  a.fontBig = io.Fonts->AddFontFromMemoryTTF((void*)kFontData, int(kFontSize), 46.f * scale, &cfg);
  a.fontSmall = io.Fonts->AddFontFromMemoryTTF((void*)kFontData, int(kFontSize), 14.f * scale, &cfg);
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
  st.ScrollbarSize = 22;
  st.GrabMinSize = 34;
  st.WindowBorderSize = 0;
  st.TouchExtraPadding = ImVec2(2, 2);
  ImVec4* c = st.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.085f, 0.085f, 0.10f, 1);
  c[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.06f, 0.075f, 1);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_Button] = ImVec4(0.19f, 0.19f, 0.22f, 1);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.23f, 0.23f, 0.27f, 1);
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
  ImGuiIO& io = ImGui::GetIO();
  Look& l = a.st.look;
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;

  ImGui::SetNextWindowPos(ImVec2(a.winW - a.panelW, 0));
  ImGui::SetNextWindowSize(ImVec2(a.panelW, float(a.winH)));
  ImGui::Begin("##panel", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);

  // ---- header ----
  ImGui::TextColored(kAccent, "TOUCHFX");
  ImGui::SameLine();
  {
    const OutputSettings& o = a.st.output;
    bool ok = o.ndiEnabled && a.ndi.running();
    int n = a.ndi.connections();
    char status[96];
    if (!o.ndiEnabled) std::snprintf(status, sizeof(status), "NDI OFF");
    else if (!ok) std::snprintf(status, sizeof(status), "NDI NOT RUNNING");
    else std::snprintf(status, sizeof(status), "NDI  %dx%d  %d rx", a.out.w, a.out.h, n);
    float tw = ImGui::CalcTextSize(status).x + 22 * s;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - tw));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImU32 dot = !o.ndiEnabled ? IM_COL32(110, 110, 120, 255)
                : !ok        ? IM_COL32(230, 70, 60, 255)
                : n > 0      ? IM_COL32(60, 220, 100, 255)
                             : IM_COL32(240, 190, 40, 255);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 7 * s, p.y + ImGui::GetFontSize() * 0.5f), 6 * s, dot);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20 * s);
    ImGui::TextUnformatted(status);
  }

  // ---- BPM ----
  {
    ImGui::PushFont(a.fontBig);
    ImGui::Text("%5.1f", l.bpm);
    ImGui::PopFont();
    ImGui::SameLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float bigH = a.fontBig->FontSize;
    // beat lights
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
    Choice("speed", kSpeedMultLabels, kNumSpeedMults, &l.speedMult, 0, 38 * s);
  }

  // ---- effects ----
  {
    SectionLabel("EFFECT");
    float w = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
    float h = 54 * s;
    for (int i = 0; i < FX_COUNT; ++i) {
      if (i % 3) ImGui::SameLine();
      if (Toggle(kEffectNames[i], l.effect == i, ImVec2(w, h))) l.effect = i;
    }
    ImGui::SameLine();
    if (Toggle("BLACKOUT", a.blackout, ImVec2(w, h), kDanger)) a.blackout = !a.blackout;
  }

  // ---- mirror ----
  SectionLabel("MIRROR");
  static const char* mirrors[] = {"OFF", "L | R", "U | D", "QUAD", "RAD 6", "RAD 8"};
  Choice("mirror", mirrors, MIRROR_COUNT, &l.mirror, 0, 44 * s);

  // ---- release ----
  SectionLabel("ON RELEASE");
  {
    float w = (ImGui::GetContentRegionAvail().x - sp * 3) / 4;
    if (Toggle("FADE", l.release == RELEASE_FADE, ImVec2(w, 44 * s))) l.release = RELEASE_FADE;
    ImGui::SameLine();
    if (Toggle("SNAP", l.release == RELEASE_SNAP, ImVec2(w, 44 * s))) l.release = RELEASE_SNAP;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 2 + sp);
    if (l.release == RELEASE_FADE) {
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * s, (44 * s - ImGui::GetFontSize()) * 0.5f));
      ImGui::SliderFloat("##fade", &l.fadeTime, 0.1f, 10.f, "%.1f s", ImGuiSliderFlags_Logarithmic);
      ImGui::PopStyleVar();
    } else {
      ImGui::Dummy(ImVec2(w * 2 + sp, 44 * s));
    }
  }
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.12f, 0.12f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.12f, 0.12f, 1));
  if (ImGui::Button("CLEAR CANVAS", ImVec2(-1, 40 * s))) a.wantClear = true;
  ImGui::PopStyleColor(2);

  // ---- tabs ----
  ImGui::Dummy(ImVec2(0, 4 * s));
  static const char* tabs[] = {"EFFECT", "COLOUR", "SCENES", "OUTPUT"};
  Choice("tabs", tabs, 4, &a.st.tab, 0, 44 * s);

  ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  switch (a.st.tab) {
    case 0: EffectTab(a); break;
    case 1: ColorTab(a); break;
    case 2: ScenesTab(a); break;
    default: OutputTab(a); break;
  }
  ImGui::Dummy(ImVec2(0, 20 * s));
  TouchScroll();
  ImGui::EndChild();
  ImGui::End();

  CanvasOverlay(a);
  (void)io;
}
