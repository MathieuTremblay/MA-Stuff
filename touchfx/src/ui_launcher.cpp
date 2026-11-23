// Startup launcher (choose / create / manage shows), the on-screen keyboard, show mode and quit confirmation.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>

#include "app.hpp"
#include "glutil.hpp"
#include "imgui_internal.h"
#include "log.hpp"
#include "platform.hpp"
#include "show.hpp"
#include "ui_widgets.hpp"

namespace ui {

// ---------------------------------------------------------------------------
// On-screen keyboard
namespace {
struct KbState {
  bool requested = false, open = false;
  std::string id, title, text;
  bool ready = false;
  std::string resultId, result;
  bool shift = true;
  bool numeric = false;  // number pad (0-9, dot) instead of the full keyboard
  bool fresh = false;    // number pad: the first key replaces the shown value
} g_kb;
}  // namespace

void OpenKeyboard(const char* id, const std::string& initial, const char* title, bool numeric) {
  g_kb.numeric = numeric;
  g_kb.fresh = numeric;
  g_kb.requested = true;
  g_kb.id = id;
  g_kb.title = title;
  g_kb.text = initial;
  g_kb.shift = initial.empty();
  g_kb.ready = false;
}

bool KeyboardResult(const char* id, std::string& out) {
  if (!g_kb.ready || g_kb.resultId != id) return false;
  g_kb.ready = false;
  out = g_kb.result;
  return true;
}

bool KeyboardIsOpen() { return g_kb.requested || g_kb.open; }

// Number pad: 7 8 9 / 4 5 6 / 1 2 3 / . 0 ERASE, then CANCEL / DONE.
static void KeypadBody(App& a, bool enter) {
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float kw = (ImGui::GetContentRegionAvail().x - sp * 2) / 3, kh = 64 * s;
  static const char* keys[12] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "ERASE"};
  ImGui::PushFont(a.fontBig);
  for (int i = 0; i < 12; ++i) {
    if (i % 3) ImGui::SameLine();
    if (i == 11) {
      ImGui::PopFont();
      ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
      if (ImGui::Button("ERASE##kp", ImVec2(kw, kh)) && !g_kb.text.empty()) g_kb.text.pop_back();
      ImGui::PopItemFlag();
      ImGui::PushFont(a.fontBig);
      if (ImGui::IsItemActivated()) g_kb.fresh = false;
      continue;
    }
    char lbl[16];
    std::snprintf(lbl, sizeof(lbl), "%s##kp%d", keys[i], i);
    if (ImGui::Button(lbl, ImVec2(kw, kh))) {
      if (g_kb.fresh) g_kb.text.clear();
      g_kb.fresh = false;
      bool dot = keys[i][0] == '.';
      if (g_kb.text.size() < 12 && !(dot && g_kb.text.find('.') != std::string::npos)) {
        if (dot && g_kb.text.empty()) g_kb.text = "0";
        g_kb.text += keys[i];
      }
    }
  }
  ImGui::PopFont();
  float half = (ImGui::GetContentRegionAvail().x - sp) / 2;
  if (ImGui::Button("CANCEL", ImVec2(half, kh))) {
    g_kb.open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (Toggle("DONE", true, ImVec2(half, kh), kGo) || enter) {
    g_kb.result = g_kb.text;
    g_kb.resultId = g_kb.id;
    g_kb.ready = true;
    g_kb.open = false;
    ImGui::CloseCurrentPopup();
  }
}

void KeyboardFrame(App& a) {
  float s = S();
  if (g_kb.requested) {
    ImGui::OpenPopup("##keyboard");
    g_kb.requested = false;
    g_kb.open = true;
  }
  ImGuiIO& io = ImGui::GetIO();
  float w = std::min(io.DisplaySize.x - 20 * s, (g_kb.numeric ? 440 : 900) * s);
  ImGui::SetNextWindowSize(ImVec2(w, 0));
  if (g_kb.numeric)
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  else
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 10 * s), ImGuiCond_Always, ImVec2(0.5f, 1.f));
  if (!ImGui::BeginPopupModal("##keyboard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) {
    g_kb.open = false;
    return;
  }
  ImGui::TextColored(kTextDim, "%s", g_kb.title.c_str());
  // the text, editable with a real keyboard too
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s", g_kb.text.c_str());
  ImGui::SetNextItemWidth(-1);
  ImGui::PushFont(a.fontBig);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  bool enter = ImGui::InputText("##kbtext", buf, sizeof(buf),
                                ImGuiInputTextFlags_EnterReturnsTrue |
                                    (g_kb.numeric ? ImGuiInputTextFlags_CharsDecimal : 0));
  ImGui::PopFont();
  if (g_kb.text != buf) g_kb.fresh = false;
  g_kb.text = buf;
  if (g_kb.numeric) {
    KeypadBody(a, enter);
    ImGui::EndPopup();
    return;
  }

  static const char* rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.'"};
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float kw = (ImGui::GetContentRegionAvail().x - sp * 9) / 10, kh = 56 * s;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 10; ++c) {
      if (c) ImGui::SameLine();
      char ch = rows[r][c];
      if (!g_kb.shift && ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
      char lbl[8];
      std::snprintf(lbl, sizeof(lbl), "%c##k%d%d", ch, r, c);
      if (ImGui::Button(lbl, ImVec2(kw, kh)) && g_kb.text.size() < 60) {
        g_kb.text += ch;
        if (g_kb.shift && ch >= 'A' && ch <= 'Z') g_kb.shift = false;  // capital only for the first letter
      }
    }
  }
  float wide = (ImGui::GetContentRegionAvail().x - sp * 4) / 6;
  if (Toggle("SHIFT", g_kb.shift, ImVec2(wide, kh))) g_kb.shift = !g_kb.shift;
  ImGui::SameLine();
  if (ImGui::Button("SPACE", ImVec2(wide * 2 + sp, kh)) && g_kb.text.size() < 60) g_kb.text += ' ';
  ImGui::SameLine();
  ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
  if (ImGui::Button("DELETE", ImVec2(wide, kh)) && !g_kb.text.empty()) {
    // remove one UTF-8 character
    size_t n = g_kb.text.size() - 1;
    while (n > 0 && (static_cast<unsigned char>(g_kb.text[n]) & 0xC0) == 0x80) --n;
    g_kb.text.erase(n);
  }
  ImGui::PopItemFlag();
  ImGui::SameLine();
  if (ImGui::Button("CLEAR", ImVec2(wide, kh))) g_kb.text.clear();
  float half = (ImGui::GetContentRegionAvail().x - sp) / 2;
  if (ImGui::Button("CANCEL", ImVec2(half, kh))) {
    g_kb.open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  bool ok = Toggle("OK", true, ImVec2(half, kh), kGo) || enter;
  if (ok) {
    g_kb.result = g_kb.text;
    g_kb.resultId = g_kb.id;
    g_kb.ready = true;
    g_kb.open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Show mode
bool ShowModeLocked(App& a) { return a.st.showMode; }

void ShowModeButton(App& a, ImVec2 size) {
  float s = S();
  if (!a.st.showMode) {
    if (Toggle("SHOW MODE", false, size)) {
      a.st.showMode = true;
      if (a.st.tab == 1) a.st.tab = 0;  // leave SETTINGS
      logLine("Show mode on");
    }
    return;
  }
  // Locked: hold 3 seconds to unlock (a brush of the hand can't do it).
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##unlock", size);
  bool held = ImGui::IsItemActive();
  a.unlockHold = held ? a.unlockHold + ImGui::GetIO().DeltaTime : 0.f;
  float t = std::min(1.f, a.unlockHold / 3.f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p1(p.x + size.x, p.y + size.y);
  float r = 6 * s;
  dl->AddRectFilled(p, p1, ImGui::GetColorU32(kDanger), r);
  if (t > 0) dl->AddRectFilled(p, ImVec2(p.x + size.x * t, p1.y), IM_COL32(255, 255, 255, 90), r);
  const char* lbl = held ? "KEEP HOLDING..." : "SHOW (HOLD 3s)";
  ImGui::PushFont(a.fontSmall);
  ImVec2 ts = ImGui::CalcTextSize(lbl);
  dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), lbl);
  ImGui::PopFont();
  if (a.unlockHold >= 3.f) {
    a.st.showMode = false;
    a.unlockHold = 0;
    logLine("Show mode off");
  }
}

void QuitConfirmFrame(App& a) {
  float s = S();
  if (a.confirmQuit) {
    ImGui::OpenPopup("##quit");
    a.confirmQuit = false;
  }
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(460 * s, 0));
  if (!ImGui::BeginPopupModal("##quit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)) return;
  ImGui::PushFont(a.fontBig);
  ImGui::TextUnformatted("Quit TouchFX?");
  ImGui::PopFont();
  ImGui::TextWrapped("Show mode is on. The output stops when TouchFX closes.");
  float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
  if (Toggle("KEEP RUNNING", true, ImVec2(half, 60 * s), kGo)) ImGui::CloseCurrentPopup();
  ImGui::SameLine();
  if (Toggle("QUIT", true, ImVec2(half, 60 * s), kDanger)) {
    a.quitNow = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Launcher
namespace {
struct LauncherState {
  std::vector<ShowInfo> shows;
  bool dirty = true;
  int selected = -1;
  std::map<std::string, GLuint> thumbs;
  double deleteArmed = -10;
} g_l;

GLuint thumbTexture(const std::string& name) {
  auto it = g_l.thumbs.find(name);
  if (it != g_l.thumbs.end()) return it->second;
  GLuint tex = 0;
  std::vector<uint8_t> rgb;
  if (loadThumb(name, rgb)) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kThumbW, kThumbH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  g_l.thumbs[name] = tex;
  return tex;
}

void refresh(App& a) {
  for (auto& kv : g_l.thumbs)
    if (kv.second) glDeleteTextures(1, &kv.second);
  g_l.thumbs.clear();
  g_l.shows = listShows();
  g_l.selected = -1;
  for (size_t i = 0; i < g_l.shows.size(); ++i)
    if (g_l.shows[i].name == a.st.lastShow) g_l.selected = int(i);
  if (g_l.selected < 0 && !g_l.shows.empty()) g_l.selected = 0;
  g_l.dirty = false;
}
}  // namespace

void Launcher(App& a) {
  float s = S();
  ImGuiIO& io = ImGui::GetIO();
  if (g_l.dirty) refresh(a);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  float colW = std::min(io.DisplaySize.x - 40 * s, 1000 * s);
  ImGui::SetCursorPosX((io.DisplaySize.x - colW) * 0.5f);
  ImGui::BeginGroup();
  ImGui::Dummy(ImVec2(colW, 10 * s));
  ImGui::PushFont(a.fontBig);
  ImGui::TextColored(kAccent, "TOUCHFX");
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(kTextDim, "  %s  -  choose a show", TOUCHFX_VERSION);

  if (!a.launcherMessage.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.3f, 1));
    ImGui::TextWrapped("%s", a.launcherMessage.c_str());
    ImGui::PopStyleColor();
    if (logPreviousRunCrashed() && ImGui::Button("OPEN LOGS FOLDER", ImVec2(260 * s, 40 * s)))
      platformOpenFolder(logDir());
  }

  // ---- show list ----
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float btnH = 64 * s;
  float listH = io.DisplaySize.y - ImGui::GetCursorPosY() - btnH * 2 - 60 * s;
  ImGui::BeginChild("##shows", ImVec2(colW, std::max(150.f * s, listH)), ImGuiChildFlags_AlwaysUseWindowPadding);
  if (g_l.shows.empty()) {
    ImGui::Dummy(ImVec2(0, 30 * s));
    ImGui::TextColored(kTextDim, "No shows yet. Tap NEW SHOW to create your first one.");
  }
  float rowH = 118 * s;
  float thW = rowH * 16.f / 9.f - 8 * s;
  for (size_t i = 0; i < g_l.shows.size(); ++i) {
    const ShowInfo& sh = g_l.shows[i];
    ImGui::PushID(int(i));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    bool sel = g_l.selected == int(i);
    if (ImGui::InvisibleButton("row", ImVec2(w, rowH))) {
      if (sel && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openShow(a, sh.name);
      g_l.selected = int(i);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), sel ? IM_COL32(60, 38, 20, 255) : IM_COL32(30, 30, 36, 255), 8 * s);
    if (sel) dl->AddRect(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(kAccent), 8 * s, 0, 2.5f * s);
    ImVec2 t0(p.x + 6 * s, p.y + 6 * s), t1(t0.x + thW, p.y + rowH - 6 * s);
    if (GLuint tex = thumbTexture(sh.name)) dl->AddImage((ImTextureID)(intptr_t)tex, t0, t1);
    else dl->AddRectFilled(t0, t1, IM_COL32(10, 10, 12, 255), 4 * s);
    ImGui::PushFont(a.fontBig);
    dl->AddText(ImVec2(t1.x + 18 * s, p.y + 16 * s), IM_COL32(240, 240, 245, 255), sh.name.c_str());
    ImGui::PopFont();
    char when[64];
    std::strftime(when, sizeof(when), "Last used %d %b %Y, %H:%M", std::localtime(&sh.modified));
    dl->AddText(ImVec2(t1.x + 18 * s, p.y + rowH - 34 * s), ImGui::GetColorU32(kTextDim), when);
    ImGui::PopID();
  }
  TouchScroll();
  ImGui::EndChild();

  // ---- actions ----
  bool has = g_l.selected >= 0 && g_l.selected < int(g_l.shows.size());
  std::string selName = has ? g_l.shows[g_l.selected].name : "";
  float bw = (colW - sp * 2) / 3;
  if (Toggle("NEW SHOW", true, ImVec2(bw, btnH))) {
    char def[64];
    std::time_t now = std::time(nullptr);
    std::strftime(def, sizeof(def), "Show %d %b", std::localtime(&now));
    OpenKeyboard("newshow", def, "Name of the new show");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!has);
  if (Toggle(has ? "OPEN" : "OPEN", has, ImVec2(bw * 2 + sp, btnH), kGo) && has) openShow(a, selName);
  ImGui::EndDisabled();

  float bw4 = (colW - sp * 4) / 5;
  ImGui::BeginDisabled(!has);
  if (ImGui::Button("DUPLICATE", ImVec2(bw4, btnH * 0.75f)))
    OpenKeyboard("dupshow", selName + " copy", "Name of the copy");
  ImGui::SameLine();
  if (ImGui::Button("RENAME", ImVec2(bw4, btnH * 0.75f))) OpenKeyboard("renshow", selName, "New name");
  ImGui::SameLine();
  bool armed = io.DeltaTime > 0 && ImGui::GetTime() - g_l.deleteArmed < 3.0;
  if (Toggle(armed ? "TAP AGAIN" : "DELETE", armed, ImVec2(bw4, btnH * 0.75f), kDanger)) {
    if (armed) {
      deleteShow(selName);
      logLine("Deleted show \"%s\" (moved to the Deleted folder)", selName.c_str());
      g_l.dirty = true;
      g_l.deleteArmed = -10;
    } else {
      g_l.deleteArmed = ImGui::GetTime();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("SHOW FOLDER", ImVec2(bw4, btnH * 0.75f))) platformOpenFolder(showsDir());
  ImGui::SameLine();
  if (ImGui::Button("QUIT", ImVec2(bw4, btnH * 0.75f))) a.quitNow = true;
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("Shows are saved in %s. Copy .tfxshow files there to import them, or out of it to back them up.",
                     showsDir().c_str());
  ImGui::PopStyleColor();
  ImGui::EndGroup();
  ImGui::End();

  // keyboard results
  std::string txt;
  if (KeyboardResult("newshow", txt)) {
    newShow(a, txt);
    g_l.dirty = true;
  }
  if (KeyboardResult("dupshow", txt) && has) {
    std::string n = uniqueShowName(txt);
    if (!duplicateShow(selName, n)) a.launcherMessage = "Could not duplicate the show.";
    g_l.dirty = true;
  }
  if (KeyboardResult("renshow", txt) && has) {
    std::string n = safeShowName(txt);
    if (n != selName && !renameShow(selName, n)) a.launcherMessage = "A show called \"" + n + "\" already exists.";
    else if (a.st.lastShow == selName) a.st.lastShow = n;
    g_l.dirty = true;
  }
  if (a.showLoaded) g_l.dirty = true;  // refresh the list next time the launcher opens
}

}  // namespace ui
