// CUES view: songs (cue lists), cue rows, GO / BACK, recording and editing.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.hpp"
#include "cues.hpp"
#include "imgui_internal.h"
#include "ui_widgets.hpp"

namespace ui {

namespace {

CueList& active(App& a) {
  a.st.activeList = std::clamp(a.st.activeList, 0, int(a.st.cueLists.size()) - 1);
  return a.st.cueLists[a.st.activeList];
}

std::string followText(const Cue& c) {
  char b[32] = "";
  if (c.follow == FOLLOW_SECONDS) std::snprintf(b, sizeof(b), "-> %.0fs", c.followValue);
  if (c.follow == FOLLOW_BARS) std::snprintf(b, sizeof(b), "-> %.0f bars", c.followValue);
  return b;
}

int g_edit = -1;          // cue being edited in the edit sheet
double g_delArmed = -10;  // two-tap delete
double g_songDelArmed = -10;

// Edit sheet for one cue: name, fade, follow, move, copy, delete.
void CueEditSheet(App& a) {
  float s = S();
  CueList& cl = active(a);
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(560 * s, io.DisplaySize.x - 20 * s), 0));
  if (!ImGui::BeginPopup("##cueedit")) return;
  if (g_edit < 0 || g_edit >= int(cl.cues.size())) {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  Cue& c = cl.cues[g_edit];
  float sp = ImGui::GetStyle().ItemSpacing.x;
  ImGui::PushFont(a.fontBig);
  ImGui::Text("Cue %d", g_edit + 1);
  ImGui::PopFont();
  char nm[96];
  std::snprintf(nm, sizeof(nm), "%s##cuename", c.name.empty() ? "(no name)" : c.name.c_str());
  if (ImGui::Button(nm, ImVec2(-1, 48 * s))) OpenKeyboard("cuename", c.name, "Cue name");
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f s", c.fade);
  TouchSlider("Fade", &c.fade, 0.f, 10.f, buf);
  SectionLabel("FOLLOW (go to the next cue automatically)");
  static const char* follows[] = {"OFF", "AFTER SECONDS", "AFTER BARS"};
  Choice("follow", follows, FOLLOW_COUNT, &c.follow);
  if (c.follow == FOLLOW_SECONDS) {
    std::snprintf(buf, sizeof(buf), "%.0f s", c.followValue);
    TouchSlider("Wait", &c.followValue, 1.f, 120.f, buf);
    c.followValue = std::round(c.followValue);
  } else if (c.follow == FOLLOW_BARS) {
    std::snprintf(buf, sizeof(buf), "%.0f bars", c.followValue);
    TouchSlider("Wait", &c.followValue, 1.f, 64.f, buf);
    c.followValue = std::round(c.followValue);
  }
  ImGui::Dummy(ImVec2(0, 6 * s));
  float w4 = (ImGui::GetContentRegionAvail().x - sp * 3) / 4;
  CuePlayer& p = a.cue;
  auto fixIndex = [](int& idx, int from, int to) {
    if (idx == from) idx = to;
    else if (idx == to) idx = from;
  };
  ImGui::BeginDisabled(g_edit == 0);
  if (ImGui::Button("MOVE UP", ImVec2(w4, 48 * s))) {
    std::swap(cl.cues[g_edit], cl.cues[g_edit - 1]);
    fixIndex(p.current, g_edit, g_edit - 1);
    fixIndex(p.next, g_edit, g_edit - 1);
    --g_edit;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_edit + 1 >= int(cl.cues.size()));
  if (ImGui::Button("MOVE DOWN", ImVec2(w4, 48 * s))) {
    std::swap(cl.cues[g_edit], cl.cues[g_edit + 1]);
    fixIndex(p.current, g_edit, g_edit + 1);
    fixIndex(p.next, g_edit, g_edit + 1);
    ++g_edit;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("COPY", ImVec2(w4, 48 * s))) {
    Cue copy = c;
    copy.name += " copy";
    cl.cues.insert(cl.cues.begin() + g_edit + 1, copy);
    if (p.current > g_edit) ++p.current;
    if (p.next > g_edit) ++p.next;
    ++g_edit;
  }
  ImGui::SameLine();
  bool armed = ImGui::GetTime() - g_delArmed < 3.0;
  if (Toggle(armed ? "TAP AGAIN" : "DELETE", armed, ImVec2(w4, 48 * s), kDanger)) {
    if (armed) {
      cl.cues.erase(cl.cues.begin() + g_edit);
      if (p.current == g_edit) p.current = -1;
      else if (p.current > g_edit) --p.current;
      if (p.next == g_edit) p.next = g_edit < int(cl.cues.size()) ? g_edit : -1;
      else if (p.next > g_edit) --p.next;
      g_delArmed = -10;
      g_edit = -1;
      ImGui::CloseCurrentPopup();
    } else {
      g_delArmed = ImGui::GetTime();
    }
  }
  if (Toggle("DONE", true, ImVec2(-1, 52 * s), kGo)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// Song menu: pick a song; manage songs when unlocked.
void SongMenu(App& a) {
  float s = S();
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(560 * s, io.DisplaySize.x - 20 * s), std::min(io.DisplaySize.y - 40 * s, 760 * s)));
  if (!ImGui::BeginPopup("##songs")) return;
  bool locked = ShowModeLocked(a);
  ImGui::PushFont(a.fontBig);
  ImGui::TextUnformatted("Songs");
  ImGui::PopFont();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float listH = ImGui::GetContentRegionAvail().y - (locked ? 60 * s : 170 * s);
  ImGui::BeginChild("##songlist", ImVec2(0, std::max(100.f * s, listH)));
  for (int i = 0; i < int(a.st.cueLists.size()); ++i) {
    char lbl[128];
    std::snprintf(lbl, sizeof(lbl), "%d.  %s   (%d cues)##song%d", i + 1, a.st.cueLists[i].name.c_str(),
                  int(a.st.cueLists[i].cues.size()), i);
    if (Toggle(lbl, i == a.st.activeList, ImVec2(-1, 52 * s))) cueSelectList(a, i);
  }
  TouchScroll();
  ImGui::EndChild();
  if (!locked) {
    float w3 = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
    if (ImGui::Button("NEW SONG", ImVec2(w3, 48 * s))) {
      char def[32];
      std::snprintf(def, sizeof(def), "Song %d", int(a.st.cueLists.size()) + 1);
      OpenKeyboard("newsong", def, "Song name");
    }
    ImGui::SameLine();
    if (ImGui::Button("RENAME", ImVec2(w3, 48 * s))) OpenKeyboard("rensong", active(a).name, "Song name");
    ImGui::SameLine();
    if (ImGui::Button("DUPLICATE", ImVec2(w3, 48 * s))) {
      CueList copy = active(a);
      copy.name += " copy";
      a.st.cueLists.insert(a.st.cueLists.begin() + a.st.activeList + 1, copy);
      cueSelectList(a, a.st.activeList + 1);
    }
    int al = a.st.activeList;
    ImGui::BeginDisabled(al == 0);
    if (ImGui::Button("MOVE UP", ImVec2(w3, 48 * s))) {
      std::swap(a.st.cueLists[al], a.st.cueLists[al - 1]);
      a.st.activeList = al - 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(al + 1 >= int(a.st.cueLists.size()));
    if (ImGui::Button("MOVE DOWN", ImVec2(w3, 48 * s))) {
      std::swap(a.st.cueLists[al], a.st.cueLists[al + 1]);
      a.st.activeList = al + 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool armed = ImGui::GetTime() - g_songDelArmed < 3.0;
    ImGui::BeginDisabled(a.st.cueLists.size() <= 1);
    if (Toggle(armed ? "TAP AGAIN" : "DELETE", armed, ImVec2(w3, 48 * s), kDanger)) {
      if (armed) {
        a.st.cueLists.erase(a.st.cueLists.begin() + al);
        cueSelectList(a, std::max(0, al - 1));
        g_songDelArmed = -10;
      } else {
        g_songDelArmed = ImGui::GetTime();
      }
    }
    ImGui::EndDisabled();
  }
  if (Toggle("DONE", true, ImVec2(-1, 48 * s), kGo)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

}  // namespace

// Compact GO for the LIVE tab (only when the song has cues).
void LiveGoButton(App& a, float h) {
  CueList& cl = active(a);
  if (cl.cues.empty()) return;
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float backW = 90 * s;
  ImGui::BeginDisabled(a.cue.current <= 0);
  if (ImGui::Button("BACK##live", ImVec2(backW, h))) cueBack(a);
  ImGui::EndDisabled();
  ImGui::SameLine();
  char lbl[128];
  if (a.cue.next >= 0)
    std::snprintf(lbl, sizeof(lbl), "GO  >  %d  %s##livego", a.cue.next + 1, cl.cues[a.cue.next].name.c_str());
  else
    std::snprintf(lbl, sizeof(lbl), "END OF %s##livego", cl.name.c_str());
  ImGui::BeginDisabled(a.cue.next < 0);
  if (Toggle(lbl, a.cue.next >= 0, ImVec2(ImGui::GetContentRegionAvail().x, h), kGo)) cueGo(a);
  ImGui::EndDisabled();
  (void)sp;
}

void CuesPanel(App& a) {
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  bool locked = ShowModeLocked(a);
  CueList& cl = active(a);
  CuePlayer& p = a.cue;

  // ---- song selector ----
  float arrowW = 60 * s, h = 52 * s;
  ImGui::BeginDisabled(a.st.activeList == 0);
  if (ImGui::Button("<##prevsong", ImVec2(arrowW, h))) cueSelectList(a, a.st.activeList - 1);
  ImGui::EndDisabled();
  ImGui::SameLine();
  char song[160];
  std::snprintf(song, sizeof(song), "%s   (%d/%d)##songbtn", cl.name.c_str(), a.st.activeList + 1,
                int(a.st.cueLists.size()));
  ImGui::PushFont(a.fontMain);
  if (ImGui::Button(song, ImVec2(ImGui::GetContentRegionAvail().x - arrowW - sp, h))) ImGui::OpenPopup("##songs");
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::BeginDisabled(a.st.activeList + 1 >= int(a.st.cueLists.size()));
  if (ImGui::Button(">##nextsong", ImVec2(arrowW, h))) cueSelectList(a, a.st.activeList + 1);
  ImGui::EndDisabled();
  SongMenu(a);

  // ---- cue rows ----
  float bottomH = (locked ? 0.f : 56 * s + sp) + 30 * s + 96 * s + sp * 3;
  ImGui::BeginChild("##cuelist", ImVec2(0, ImGui::GetContentRegionAvail().y - bottomH),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  if (cl.cues.empty()) {
    ImGui::Dummy(ImVec2(0, 20 * s));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped(locked ? "This song has no cues." :
                                "No cues in this song yet. Set up a look on the canvas, then tap RECORD to store it "
                                "as a cue. Repeat for each look, then use GO during the show.");
    ImGui::PopStyleColor();
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float rowH = 58 * s;
  for (int i = 0; i < int(cl.cues.size()); ++i) {
    const Cue& c = cl.cues[i];
    ImGui::PushID(i);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("row", ImVec2(w, rowH))) p.next = i;  // tap = make it the next cue
    bool cur = p.current == i, nxt = p.next == i;
    ImU32 bg = cur ? IM_COL32(25, 90, 45, 255) : IM_COL32(30, 30, 36, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + rowH), bg, 6 * s);
    if (nxt) dl->AddRect(pos, ImVec2(pos.x + w, pos.y + rowH), ImGui::GetColorU32(kAccent), 6 * s, 0, 3 * s);
    // number, name, effect, palette, fade / follow
    char num[16];
    std::snprintf(num, sizeof(num), "%d", i + 1);
    ImGui::PushFont(a.fontBig);
    float numY = pos.y + (rowH - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(pos.x + 12 * s, numY), cur ? IM_COL32(150, 255, 170, 255) : IM_COL32(200, 200, 210, 255), num);
    ImGui::PopFont();
    float x = pos.x + 70 * s;
    dl->AddText(ImVec2(x, pos.y + 7 * s), IM_COL32(240, 240, 245, 255), c.name.empty() ? "(no name)" : c.name.c_str());
    const Palette& pal = c.look.useCustom ? c.look.custom : a.st.palettes[c.look.palette];
    ImGui::PushFont(a.fontSmall);
    float y2 = pos.y + rowH - ImGui::GetFontSize() - 8 * s;
    const Color3* cols[3] = {&pal.low, &pal.mid, &pal.high};
    for (int k = 0; k < 3; ++k)
      dl->AddRectFilled(ImVec2(x + k * 14 * s, y2 + 2 * s), ImVec2(x + k * 14 * s + 12 * s, y2 + ImGui::GetFontSize() - 2 * s),
                        col32(*cols[k]), 2 * s);
    dl->AddText(ImVec2(x + 48 * s, y2), ImGui::GetColorU32(kTextDim), effectName(c.look.effect));
    char right[64];
    std::snprintf(right, sizeof(right), "%.1fs %s", c.fade, followText(c).c_str());
    ImVec2 rs = ImGui::CalcTextSize(right);
    dl->AddText(ImVec2(pos.x + w - rs.x - 12 * s, y2), ImGui::GetColorU32(kTextDim), right);
    const char* tag = cur ? (p.modified ? "PLAYING (modified)" : "PLAYING") : (nxt ? "NEXT" : nullptr);
    if (tag) {
      ImVec2 ts = ImGui::CalcTextSize(tag);
      dl->AddText(ImVec2(pos.x + w - ts.x - 12 * s, pos.y + 8 * s),
                  cur ? IM_COL32(150, 255, 170, 255) : ImGui::GetColorU32(kAccent), tag);
    }
    ImGui::PopFont();
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0, 10 * s));
  TouchScroll();
  ImGui::EndChild();

  // ---- editing (unlocked only) ----
  if (!locked) {
    float w3 = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
    if (ImGui::Button("RECORD", ImVec2(w3, 56 * s))) cueRecord(a);
    ImGui::SameLine();
    ImGui::BeginDisabled(p.current < 0);
    if (Toggle("UPDATE", p.modified, ImVec2(w3, 56 * s))) cueUpdate(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    int target = p.next >= 0 ? p.next : p.current;
    ImGui::BeginDisabled(target < 0);
    if (ImGui::Button("EDIT", ImVec2(w3, 56 * s))) {
      g_edit = target;
      ImGui::OpenPopup("##cueedit");
    }
    ImGui::EndDisabled();
    CueEditSheet(a);
  }

  // ---- status + GO / BACK ----
  {
    char st[160];
    if (p.current >= 0 && p.current < int(cl.cues.size()))
      std::snprintf(st, sizeof(st), "On stage: %d  %s%s", p.current + 1, cl.cues[p.current].name.c_str(),
                    p.modified ? "  (modified - UPDATE to keep)" : "");
    else
      std::snprintf(st, sizeof(st), "Ready: GO starts %s", cl.name.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(st);
    ImGui::PopStyleColor();
    // progress: fade, then follow countdown
    ImVec2 pp = ImGui::GetCursorScreenPos();
    float pw = ImGui::GetContentRegionAvail().x;
    float prog = 0;
    if (p.fading && p.fadeDur > 0) prog = std::min(1.f, p.fadeT / p.fadeDur);
    else if (p.current >= 0 && p.current < int(cl.cues.size())) {
      const Cue& c = cl.cues[p.current];
      if (c.follow == FOLLOW_SECONDS) prog = float((a.time - p.goTime) / c.followValue);
      if (c.follow == FOLLOW_BARS) prog = float((a.tempo.beat - p.goBeat) / (c.followValue * 4.0));
    }
    ImGui::Dummy(ImVec2(pw, 8 * s));
    ImGui::GetWindowDrawList()->AddRectFilled(pp, ImVec2(pp.x + pw, pp.y + 6 * s), IM_COL32(40, 40, 48, 255), 3 * s);
    if (prog > 0)
      ImGui::GetWindowDrawList()->AddRectFilled(pp, ImVec2(pp.x + pw * std::clamp(prog, 0.f, 1.f), pp.y + 6 * s),
                                                ImGui::GetColorU32(kGo), 3 * s);
  }
  float backW = 130 * s, goH = 96 * s;
  ImGui::BeginDisabled(p.current <= 0);
  if (ImGui::Button("BACK", ImVec2(backW, goH))) cueBack(a);
  ImGui::EndDisabled();
  ImGui::SameLine();
  char go[160];
  if (p.next >= 0 && p.next < int(cl.cues.size()))
    std::snprintf(go, sizeof(go), "GO\n%d  %s##bigGo", p.next + 1, cl.cues[p.next].name.c_str());
  else
    std::snprintf(go, sizeof(go), "END OF SONG##bigGo");
  ImGui::BeginDisabled(p.next < 0);
  ImGui::PushFont(a.fontMain);
  if (Toggle(go, p.next >= 0, ImVec2(ImGui::GetContentRegionAvail().x, goH), kGo)) cueGo(a);
  ImGui::PopFont();
  ImGui::EndDisabled();

  // keyboard results
  std::string txt;
  if (KeyboardResult("cuename", txt) && g_edit >= 0 && g_edit < int(cl.cues.size())) cl.cues[g_edit].name = txt;
  if (KeyboardResult("newsong", txt)) {
    a.st.cueLists.push_back(CueList{txt.empty() ? "Song" : txt, {}});
    cueSelectList(a, int(a.st.cueLists.size()) - 1);
  }
  if (KeyboardResult("rensong", txt) && !txt.empty()) active(a).name = txt;
}

}  // namespace ui
