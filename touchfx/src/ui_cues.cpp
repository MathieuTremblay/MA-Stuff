// CUES view: songs (cue lists), cue rows, GO / BACK, recording, editing, numbering and undo.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

std::string cueLabel(const CueList& cl, int i) {
  return (i >= 0 && i < int(cl.cues.size())) ? "cue " + cueNumText(cl.cues[i].number) : "cue";
}

int g_edit = -1;          // cue being edited in the edit sheet
bool g_sheetOpen = false;  // the edit sheet was open last frame
bool g_sheetReturn = false;  // reopen the edit sheet when the on-screen keyboard closes
double g_delArmed = -10;  // two-tap delete
double g_songDelArmed = -10;
std::string g_numError;   // "cue 3 already exists"
double g_numErrorTime = -10;

// Removes a cue and keeps the player on the right cues.
void deleteCue(App& a, CueList& cl, int i) {
  CuePlayer& p = a.cue;
  if (i < 0 || i >= int(cl.cues.size())) return;
  cl.cues.erase(cl.cues.begin() + i);
  if (p.current == i) p.current = -1;
  else if (p.current > i) --p.current;
  if (p.next == i) p.next = i < int(cl.cues.size()) ? i : -1;
  else if (p.next > i) --p.next;
}

// Edit sheet for one cue: number, name, fade, follow, move, copy, delete.
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
  if (a.cueEditClose) {  // Enter / Escape on the keyboard
    a.cueEditClose = false;
    g_sheetReturn = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  Cue& c = cl.cues[g_edit];
  float sp = ImGui::GetStyle().ItemSpacing.x;
  ImGui::PushFont(a.fontBig);
  ImGui::Text("Cue %s", cueNumText(c.number).c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::PushFont(a.fontSmall);
  ImGui::TextUnformatted("   Enter = done, Esc = close");
  ImGui::PopFont();
  ImGui::PopStyleColor();
  // number + name (both open the on-screen keyboard)
  char nb[64];
  std::snprintf(nb, sizeof(nb), "No. %s##cuenum", cueNumText(c.number).c_str());
  float numW = 150 * s;
  if (ImGui::Button(nb, ImVec2(numW, 48 * s))) {
    OpenKeyboard("cuenum", cueNumText(c.number), "Cue number (for example 3 or 2.5)", true);
    g_sheetReturn = true;
  }
  ImGui::SameLine();
  char nm[96];
  std::snprintf(nm, sizeof(nm), "%s##cuename", c.name.empty() ? "(no name)" : c.name.c_str());
  if (ImGui::Button(nm, ImVec2(-1, 48 * s))) {
    OpenKeyboard("cuename", c.name, "Cue name");
    g_sheetReturn = true;
  }
  if (ImGui::GetTime() - g_numErrorTime < 4.0) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
    ImGui::TextUnformatted(g_numError.c_str());
    ImGui::PopStyleColor();
  }
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
  // moving swaps the cues but keeps the numbers in order
  ImGui::BeginDisabled(g_edit == 0);
  if (ImGui::Button("MOVE UP", ImVec2(w4, 48 * s))) {
    std::swap(cl.cues[g_edit], cl.cues[g_edit - 1]);
    std::swap(cl.cues[g_edit].number, cl.cues[g_edit - 1].number);
    fixIndex(p.current, g_edit, g_edit - 1);
    fixIndex(p.next, g_edit, g_edit - 1);
    --g_edit;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_edit + 1 >= int(cl.cues.size()));
  if (ImGui::Button("MOVE DOWN", ImVec2(w4, 48 * s))) {
    std::swap(cl.cues[g_edit], cl.cues[g_edit + 1]);
    std::swap(cl.cues[g_edit].number, cl.cues[g_edit + 1].number);
    fixIndex(p.current, g_edit, g_edit + 1);
    fixIndex(p.next, g_edit, g_edit + 1);
    ++g_edit;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("COPY", ImVec2(w4, 48 * s))) {
    Cue copy = cl.cues[g_edit];
    copy.name += " copy";
    copy.number = cueNumberAfter(cl, g_edit);
    if (copy.number < 0) {
      cueRenumber(cl);
      copy.number = cueNumberAfter(cl, g_edit);
    }
    cl.cues.insert(cl.cues.begin() + g_edit + 1, copy);
    if (p.current > g_edit) ++p.current;
    if (p.next > g_edit) ++p.next;
    ++g_edit;
  }
  ImGui::SameLine();
  bool armed = ImGui::GetTime() - g_delArmed < 3.0;
  if (Toggle(armed ? "TAP AGAIN" : "DELETE", armed, ImVec2(w4, 48 * s), kDanger)) {
    if (armed) {
      if (a.undo.hasPending) a.undo.pending.label = "delete " + cueLabel(cl, g_edit);
      deleteCue(a, cl, g_edit);
      g_delArmed = -10;
      g_edit = -1;
      g_sheetReturn = false;
      ImGui::CloseCurrentPopup();
    } else {
      g_delArmed = ImGui::GetTime();
    }
  }
  if (Toggle("DONE", true, ImVec2(-1, 52 * s), kGo)) {
    g_sheetReturn = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// Song menu: pick a song; manage songs when unlocked.
void SongMenu(App& a) {
  float s = S();
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(560 * s, io.DisplaySize.x - 20 * s), std::min(io.DisplaySize.y - 40 * s, 860 * s)));
  if (!ImGui::BeginPopup("##songs")) return;
  bool locked = ShowModeLocked(a);
  ImGui::PushFont(a.fontBig);
  ImGui::TextUnformatted("Songs");
  ImGui::PopFont();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  float listH = ImGui::GetContentRegionAvail().y - (locked ? 60 * s : 330 * s);
  ImGui::BeginChild("##songlist", ImVec2(0, std::max(100.f * s, listH)));
  static const char* ends[] = {"stop", "loop", "next song"};
  for (int i = 0; i < int(a.st.cueLists.size()); ++i) {
    const CueList& l = a.st.cueLists[i];
    char lbl[160];
    std::snprintf(lbl, sizeof(lbl), "%d.  %s   (%d cues, then %s)##song%d", i + 1, l.name.c_str(), int(l.cues.size()),
                  ends[std::clamp(l.endAction, 0, END_COUNT - 1)], i);
    if (Toggle(lbl, i == a.st.activeList, ImVec2(-1, 52 * s))) cueSelectList(a, i);
  }
  TouchScroll();
  ImGui::EndChild();
  if (!locked) {
    CueList& cl = active(a);
    SectionLabel("GO ON THE LAST CUE OF THIS SONG");
    static const char* endLabels[] = {"STOP", "LOOP TO CUE 1", "NEXT SONG"};
    int end = cl.endAction;
    if (Choice("endaction", endLabels, END_COUNT, &end) && end != cl.endAction) {
      undoPush(a, "end of song");
      cl.endAction = end;
    }
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
      undoPush(a, "duplicate song");
      CueList copy = active(a);
      copy.name += " copy";
      a.st.cueLists.insert(a.st.cueLists.begin() + a.st.activeList + 1, copy);
      cueSelectList(a, a.st.activeList + 1);
    }
    int al = a.st.activeList;
    ImGui::BeginDisabled(al == 0);
    if (ImGui::Button("MOVE UP", ImVec2(w3, 48 * s))) {
      undoPush(a, "move song");
      std::swap(a.st.cueLists[al], a.st.cueLists[al - 1]);
      a.st.activeList = al - 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(al + 1 >= int(a.st.cueLists.size()));
    if (ImGui::Button("MOVE DOWN", ImVec2(w3, 48 * s))) {
      undoPush(a, "move song");
      std::swap(a.st.cueLists[al], a.st.cueLists[al + 1]);
      a.st.activeList = al + 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool armed = ImGui::GetTime() - g_songDelArmed < 3.0;
    ImGui::BeginDisabled(a.st.cueLists.size() <= 1);
    if (Toggle(armed ? "TAP AGAIN" : "DELETE", armed, ImVec2(w3, 48 * s), kDanger)) {
      if (armed) {
        undoPush(a, "delete song " + a.st.cueLists[al].name);
        a.st.cueLists.erase(a.st.cueLists.begin() + al);
        cueSelectList(a, std::max(0, al - 1));
        g_songDelArmed = -10;
      } else {
        g_songDelArmed = ImGui::GetTime();
      }
    }
    ImGui::EndDisabled();
    CueList& now = active(a);  // DUPLICATE / DELETE above may have changed the song list
    ImGui::BeginDisabled(now.cues.empty());
    if (ImGui::Button("RENUMBER CUES 1, 2, 3...", ImVec2(-1, 48 * s))) {
      undoPush(a, "renumber " + now.name);
      cueRenumber(now);
    }
    ImGui::EndDisabled();
  }
  if (Toggle("DONE", true, ImVec2(-1, 48 * s), kGo)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// UNDO / REDO button: shows what it will undo.
void UndoButton(App& a, bool redo, ImVec2 size) {
  std::string what = undoLabel(a, redo);
  ImGui::PushFont(a.fontSmall);
  float maxW = size.x - ImGui::GetStyle().FramePadding.x * 2;
  if (ImGui::CalcTextSize(what.c_str()).x > maxW) {  // too long for the button: shorten with "..."
    while (!what.empty() && ImGui::CalcTextSize((what + "...").c_str()).x > maxW) what.pop_back();
    what += "...";
  }
  std::string lbl = std::string(redo ? "REDO" : "UNDO") + (what.empty() ? "" : "\n" + what) + (redo ? "##redo" : "##undo");
  ImGui::BeginDisabled(undoLabel(a, redo).empty());
  if (ImGui::Button(lbl.c_str(), size)) undoApply(a, redo);
  ImGui::PopFont();
  ImGui::EndDisabled();
}

// Results of the on-screen keyboard. Applied first in the frame, before the edit sheet can reopen and take its
// undo snapshot.
void KeyboardResults(App& a) {
  CueList& cl = active(a);
  std::string txt;
  if (KeyboardResult("cuename", txt) && g_edit >= 0 && g_edit < int(cl.cues.size()) && cl.cues[g_edit].name != txt) {
    undoPush(a, "rename " + cueLabel(cl, g_edit));
    cl.cues[g_edit].name = txt;
  }
  if (KeyboardResult("cuenum", txt) && g_edit >= 0 && g_edit < int(cl.cues.size())) {
    for (char& ch : txt)
      if (ch == ',') ch = '.';  // 2,5 works too
    char* end = nullptr;
    float v = std::strtof(txt.c_str(), &end);
    v = std::round(v * 1000.f) / 1000.f;
    int other = cueFindNumber(cl, v);
    if (txt.empty() || end == txt.c_str() || *end != 0 || !(v > 0) || v >= 10000) {
      g_numError = "\"" + txt + "\" is not a cue number. Use a number like 3 or 2.5.";
      g_numErrorTime = ImGui::GetTime();
    } else if (other >= 0 && other != g_edit) {
      g_numError = "Cue " + cueNumText(v) + " already exists in this song.";
      g_numErrorTime = ImGui::GetTime();
    } else if (other != g_edit) {
      undoPush(a, "renumber " + cueLabel(cl, g_edit));
      cl.cues[g_edit].number = v;
      cueSort(a, &g_edit);
    }
  }
  if (KeyboardResult("newsong", txt)) {
    undoPush(a, "new song");
    a.st.cueLists.push_back(CueList{txt.empty() ? "Song" : txt, END_STOP, {}});
    cueSelectList(a, int(a.st.cueLists.size()) - 1);
  }
  if (KeyboardResult("rensong", txt) && !txt.empty() && active(a).name != txt) {
    undoPush(a, "rename song");
    active(a).name = txt;
  }
}

}  // namespace

void CuesPanel(App& a) {
  KeyboardResults(a);
  float s = S();
  float sp = ImGui::GetStyle().ItemSpacing.x;
  bool locked = ShowModeLocked(a);
  CuePlayer& p = a.cue;

  // ---- song selector ----
  float arrowW = 60 * s, h = 52 * s;
  ImGui::BeginDisabled(a.st.activeList == 0);
  if (ImGui::Button("<##prevsong", ImVec2(arrowW, h))) cueSelectList(a, a.st.activeList - 1);
  ImGui::EndDisabled();
  ImGui::SameLine();
  char song[160];
  std::snprintf(song, sizeof(song), "%s   (%d/%d)##songbtn", active(a).name.c_str(), a.st.activeList + 1,
                int(a.st.cueLists.size()));
  ImGui::PushFont(a.fontMain);
  if (ImGui::Button(song, ImVec2(ImGui::GetContentRegionAvail().x - arrowW - sp, h))) ImGui::OpenPopup("##songs");
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::BeginDisabled(a.st.activeList + 1 >= int(a.st.cueLists.size()));
  if (ImGui::Button(">##nextsong", ImVec2(arrowW, h))) cueSelectList(a, a.st.activeList + 1);
  ImGui::EndDisabled();
  SongMenu(a);
  CueList* clp = &active(a);  // taken after the song menu, which can add or remove songs

  // ---- cue rows ----
  CueList& cl = *clp;
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
  float rowH = 58 * s, numW = 84 * s;
  for (int i = 0; i < int(cl.cues.size()); ++i) {
    const Cue& c = cl.cues[i];
    ImGui::PushID(i);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("row", ImVec2(w, rowH))) p.next = i;  // tap = make it the next cue
    bool cur = p.current == i, nxt = p.next == i;
    if (nxt && p.scrollToNext) {
      ImGui::SetScrollHereY(0.5f);
      p.scrollToNext = false;
    }
    ImU32 bg = cur ? IM_COL32(25, 90, 45, 255) : IM_COL32(30, 30, 36, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + rowH), bg, 6 * s);
    if (nxt) dl->AddRect(pos, ImVec2(pos.x + w, pos.y + rowH), ImGui::GetColorU32(kAccent), 6 * s, 0, 3 * s);
    // number, name, effect, palette, fade / follow
    std::string num = cueNumText(c.number);
    ImGui::PushFont(a.fontBig);
    if (ImGui::CalcTextSize(num.c_str()).x > numW - 16 * s) {
      ImGui::PopFont();
      ImGui::PushFont(a.fontMain);
    }
    float numY = pos.y + (rowH - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(pos.x + 12 * s, numY), cur ? IM_COL32(150, 255, 170, 255) : IM_COL32(200, 200, 210, 255),
                num.c_str());
    ImGui::PopFont();
    float x = pos.x + numW;
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
  if (p.next < 0) p.scrollToNext = false;
  // end of song: what GO does after the last cue
  if (!cl.cues.empty()) {
    static const char* endText[] = {"End of song: GO stops here", "End of song: GO loops back to the first cue",
                                    "End of song: GO starts the next song"};
    ImGui::Dummy(ImVec2(0, 4 * s));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::PushFont(a.fontSmall);
    ImGui::TextUnformatted(endText[std::clamp(cl.endAction, 0, END_COUNT - 1)]);
    ImGui::PopFont();
    ImGui::PopStyleColor();
  }
  ImGui::Dummy(ImVec2(0, 10 * s));
  TouchScroll();
  ImGui::EndChild();

  // ---- editing (unlocked only) ----
  if (!locked) {
    float unit = (ImGui::GetContentRegionAvail().x - sp * 4) / 5.f;  // RECORD UPDATE EDIT, then UNDO / REDO
    float bw = unit * 0.9f, uw = unit * 1.4f;  // REDO takes what is left
    if (ImGui::Button("RECORD", ImVec2(bw, 56 * s))) {
      undoPush(a, "record cue");
      cueRecord(a);
      if (!a.undo.undo.empty()) a.undo.undo.back().label = "record " + cueLabel(cl, p.current);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(p.current < 0);
    if (Toggle("UPDATE", p.modified, ImVec2(bw, 56 * s))) {
      undoPush(a, "update " + cueLabel(cl, p.current));
      cueUpdate(a);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    int target = p.next >= 0 ? p.next : p.current;
    ImGui::BeginDisabled(target < 0);
    if (ImGui::Button("EDIT", ImVec2(bw, 56 * s)) || (a.cueEditRequest && target >= 0)) {  // or the E key
      g_edit = target;
      ImGui::OpenPopup("##cueedit");
    }
    ImGui::EndDisabled();
    a.cueEditRequest = false;
    if (a.cueDeleteRequest && target >= 0) {  // Delete key pressed twice
      undoPush(a, "delete " + cueLabel(cl, target));
      deleteCue(a, cl, target);
    }
    a.cueDeleteRequest = false;
    ImGui::SameLine();
    UndoButton(a, false, ImVec2(uw, 56 * s));
    ImGui::SameLine();
    UndoButton(a, true, ImVec2(ImGui::GetContentRegionAvail().x, 56 * s));
    // back to the edit sheet after typing a number or a name
    if (g_sheetReturn && !KeyboardIsOpen() && !ImGui::IsPopupOpen("##cueedit")) {
      g_sheetReturn = false;
      if (g_edit >= 0 && g_edit < int(active(a).cues.size())) ImGui::OpenPopup("##cueedit");
    }
    bool open = ImGui::IsPopupOpen("##cueedit");
    a.cueEditOpen = open;
    if (open && !g_sheetOpen) undoBegin(a, "edit " + cueLabel(active(a), g_edit));  // all changes = one undo step
    CueEditSheet(a);
    open = ImGui::IsPopupOpen("##cueedit");
    a.cueEditOpen = open;
    if (!open && g_sheetOpen) undoEnd(a);
    g_sheetOpen = open;
  }

  // ---- status + GO / BACK ----
  CueList& cur = active(a);  // UNDO may have replaced the song list
  {
    char st[160];
    if (p.current >= 0 && p.current < int(cur.cues.size()))
      std::snprintf(st, sizeof(st), "On stage: %s  %s%s", cueNumText(cur.cues[p.current].number).c_str(),
                    cur.cues[p.current].name.c_str(), p.modified ? "  (modified - UPDATE to keep)" : "");
    else
      std::snprintf(st, sizeof(st), "Ready: GO starts %s", cur.name.c_str());
    int delTarget = p.next >= 0 ? p.next : p.current;
    if (a.time - a.cueDelArmed < 3.0 && delTarget >= 0 && delTarget < int(cur.cues.size()))
      std::snprintf(st, sizeof(st), "Press Delete again to delete cue %s", cueNumText(cur.cues[delTarget].number).c_str());
    bool armedDel = a.time - a.cueDelArmed < 3.0;
    ImGui::PushStyleColor(ImGuiCol_Text, armedDel ? ImVec4(1, 0.45f, 0.4f, 1) : kTextDim);
    ImGui::TextUnformatted(st);
    ImGui::PopStyleColor();
    // progress: fade, then follow countdown
    ImVec2 pp = ImGui::GetCursorScreenPos();
    float pw = ImGui::GetContentRegionAvail().x;
    float prog = 0;
    if (p.fading && p.fadeDur > 0) prog = std::min(1.f, p.fadeT / p.fadeDur);
    else if (p.current >= 0 && p.current < int(cur.cues.size())) {
      const Cue& c = cur.cues[p.current];
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
  if (ImGui::Button("BACK", ImVec2(backW, goH))) cueCommand(a, CUE_BACK);
  ImGui::EndDisabled();
  ImGui::SameLine();
  bool canGo = cueCanGo(a);
  std::string go = (canGo ? "GO\n" : "") + cueGoText(a, true) + "##bigGo";
  ImGui::BeginDisabled(!canGo);
  ImGui::PushFont(a.fontMain);
  if (Toggle(go.c_str(), canGo, ImVec2(ImGui::GetContentRegionAvail().x, goH), kGo)) cueCommand(a, CUE_GO);
  ImGui::PopFont();
  ImGui::EndDisabled();

}

}  // namespace ui
