// The MEDIA window: a file browser (with search) on the left, the clip grid by group in the middle, the
// preview and output monitors on the right, and the selected clip's settings underneath.
// Like Resolume: tap a clip's NAME to preview it and edit its settings, tap its PICTURE to play it.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "app.hpp"
#include "imgui.h"
#include "log.hpp"
#include "platform.hpp"
#include "show.hpp"
#include "ui_widgets.hpp"

namespace fs = std::filesystem;

namespace ui {
namespace {

struct Entry {
  std::string name, path;
  bool dir = false;
};

struct MediaUi {
  std::string path;            // folder being browsed ("" = the list of places)
  std::string search, lastSearch, lastPath;
  std::vector<Entry> entries;
  bool dirty = true;
  std::set<std::string> picked;  // files ticked for IMPORT
  std::map<std::string, GLuint> thumbs;
  int thumbVersion = -1;
  int tab = 0;                 // settings: PLAY / TRANSFORM / COLOUR
  double deleteArmed = -10, groupDeleteArmed = -10;
  std::string deleteId;
} g_m;

std::string u8(const fs::path& p) { return p.u8string(); }

std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

// Places to start browsing: the user's folders and the drives.
std::vector<Entry> places() {
  std::vector<Entry> out;
  std::error_code ec;
#ifdef _WIN32
  const wchar_t* home = _wgetenv(L"USERPROFILE");
  fs::path h = home ? fs::path(home) : fs::path();
#else
  const char* home = std::getenv("HOME");
  fs::path h = home ? fs::path(home) : fs::path();
#endif
  if (!h.empty())
    for (const char* sub : {"Desktop", "Videos", "Pictures", "Documents", "Downloads"})
      if (fs::is_directory(h / sub, ec)) out.push_back({sub, u8(h / sub), true});
  for (const DriveInfo& d : listDrives(true)) out.push_back({d.label, d.root, true});
  return out;
}

void listFolder(const std::string& dir, const std::string& search, std::vector<Entry>& out) {
  out.clear();
  std::error_code ec;
  std::string q = lower(search);
  if (q.size() >= 2) {
    // search: this folder and the folders inside it (a few levels, a few thousand entries)
    int seen = 0;
    auto opts = fs::directory_options::skip_permission_denied;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(dir), opts, ec);
         it != fs::recursive_directory_iterator() && seen < 20000 && out.size() < 400; it.increment(ec)) {
      if (ec) break;
      ++seen;
      if (it.depth() > 4) {
        it.disable_recursion_pending();
        continue;
      }
      std::string name = u8(it->path().filename());
      if (!name.empty() && name[0] == '.') {
        if (it->is_directory(ec)) it.disable_recursion_pending();
        continue;
      }
      bool d = it->is_directory(ec);
      if (lower(name).find(q) == std::string::npos) continue;
      if (!d && !isMediaFile(u8(it->path()))) continue;
      out.push_back({u8(fs::relative(it->path(), fs::u8path(dir), ec)), u8(it->path()), d});
    }
  } else {
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), fs::directory_options::skip_permission_denied, ec)) {
      std::string name = u8(e.path().filename());
      if (name.empty() || name[0] == '.' || name[0] == '$') continue;
      bool d = e.is_directory(ec);
      if (!d && !isMediaFile(u8(e.path()))) continue;
      if (!q.empty() && lower(name).find(q) == std::string::npos) continue;
      out.push_back({name, u8(e.path()), d});
    }
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    if (a.dir != b.dir) return a.dir;
    return lower(a.name) < lower(b.name);
  });
}

GLuint loadPpm(const std::string& path) {
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) return 0;
  std::string magic;
  int w = 0, h = 0, mx = 0;
  f >> magic >> w >> h >> mx;
  f.get();
  if (magic != "P6" || w <= 0 || h <= 0 || w > 4096 || h > 4096 || mx != 255) return 0;
  std::vector<unsigned char> px(size_t(w) * h * 3);
  f.read(reinterpret_cast<char*>(px.data()), std::streamsize(px.size()));
  if (!f) return 0;
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return t;
}

GLuint thumbOf(App& a, const std::string& id) {
  if (g_m.thumbVersion != a.importer.thumbVersion()) {  // a conversion finished: reload
    for (auto& kv : g_m.thumbs)
      if (kv.second) glDeleteTextures(1, &kv.second);
    g_m.thumbs.clear();
    g_m.thumbVersion = a.importer.thumbVersion();
  }
  auto it = g_m.thumbs.find(id);
  if (it != g_m.thumbs.end()) return it->second;
  GLuint t = loadPpm(a.importer.thumbPath(id));
  g_m.thumbs[id] = t;
  return t;
}

const ImVec4 kLabelCols[kNumClipLabels] = {
    {0.35f, 0.35f, 0.4f, 1}, {0.93f, 0.26f, 0.26f, 1}, {1.f, 0.55f, 0.1f, 1},  {0.98f, 0.85f, 0.15f, 1},
    {0.3f, 0.85f, 0.35f, 1},  {0.2f, 0.85f, 0.9f, 1},  {0.25f, 0.5f, 1.f, 1}, {0.65f, 0.4f, 1.f, 1},
    {1.f, 0.45f, 0.75f, 1}};

// Plays a clip: on the media layer (the BACK layer becomes VIDEO on MEDIA if nothing shows the media yet).
void goLive(App& a, const std::string& id) {
  Look& l = a.st.look;
  if (l.mediaClip == id) {
    a.media.restart();
    return;
  }
  l.mediaClip = id;
  bool front = l.effect == FX_VIDEO && l.video.source == VIDSRC_MEDIA;
  BgLayer& bg = l.bg;
  bool back = bg.on && bg.effect == FX_VIDEO && bg.fx.video.source == VIDSRC_MEDIA;
  if (!front && !back) {
    bg.on = true;
    bg.effect = FX_VIDEO;
    bg.fx.video.mode = VID_CLEAN;
    bg.fx.video.alwaysOn = true;
    bg.fx.video.source = VIDSRC_MEDIA;
    bg.fx.video.level = 0.8f;  // 1:1
    bg.level = 1.f;
    bg.blend = BLEND_BEHIND;  // the effects you play go over the clip
  }
}

std::string timeText(float sec) {
  char b[32];
  int m = int(sec) / 60;
  std::snprintf(b, sizeof(b), "%d:%05.2f", m, sec - m * 60.f);
  return b;
}

// ---- left: files ----
void Browser(App& a, float h) {
  float s = S();
  ImGui::BeginChild("##browser", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::TextColored(kAccent, "FILES");
  float w = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
  // where: PLACES / UP, then the folder
  if (ImGui::Button("PLACES", ImVec2((w - sp) * 0.5f, 40 * s))) {
    g_m.path.clear();
    g_m.dirty = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(g_m.path.empty());
  if (ImGui::Button("UP", ImVec2((w - sp) * 0.5f, 40 * s))) {
    fs::path p = fs::u8path(g_m.path);
    g_m.path = p.has_relative_path() && p.parent_path() != p ? u8(p.parent_path()) : "";
    if (fs::u8path(g_m.path) == p) g_m.path.clear();
    g_m.dirty = true;
  }
  ImGui::EndDisabled();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("%s", g_m.path.empty() ? "Choose a place" : g_m.path.c_str());
  ImGui::PopStyleColor();
  // search: type, or tap KEYS for the on-screen keyboard
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s", g_m.search.c_str());
  ImGui::SetNextItemWidth(w - 70 * s - sp);
  if (ImGui::InputTextWithHint("##search", "Search files and folders", buf, sizeof(buf))) g_m.search = buf;
  ImGui::SameLine();
  if (ImGui::Button("KEYS", ImVec2(70 * s, 0))) OpenKeyboard("mediasearch", g_m.search, "Search files and folders");
  std::string txt;
  if (KeyboardResult("mediasearch", txt)) g_m.search = txt;
  if (g_m.search != g_m.lastSearch || g_m.path != g_m.lastPath) g_m.dirty = true;
  if (g_m.dirty) {
    if (g_m.path.empty()) {
      g_m.entries = places();
      if (!g_m.search.empty()) {
        std::string q = lower(g_m.search);
        g_m.entries.erase(std::remove_if(g_m.entries.begin(), g_m.entries.end(),
                                         [&](const Entry& e) { return lower(e.name).find(q) == std::string::npos; }),
                          g_m.entries.end());
      }
    } else {
      listFolder(g_m.path, g_m.search, g_m.entries);
    }
    g_m.lastSearch = g_m.search;
    g_m.lastPath = g_m.path;
    g_m.dirty = false;
  }
  // the list
  float listH = ImGui::GetContentRegionAvail().y - 100 * s;
  ImGui::BeginChild("##files", ImVec2(0, std::max(80.f * s, listH)));
  std::string enter;
  for (size_t i = 0; i < g_m.entries.size(); ++i) {
    const Entry& e = g_m.entries[i];
    ImGui::PushID(int(i));
    bool picked = g_m.picked.count(e.path) > 0;
    std::string lbl = (e.dir ? "[ ] " : (picked ? "(x) " : "    ")) + e.name;
    if (e.dir) lbl = "> " + e.name;
    if (Toggle(lbl.c_str(), picked, ImVec2(ImGui::GetContentRegionAvail().x, 40 * s))) {
      if (e.dir) enter = e.path;
      else if (picked) g_m.picked.erase(e.path);
      else g_m.picked.insert(e.path);
    }
    ImGui::PopID();
  }
  if (g_m.entries.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped(g_m.path.empty() ? "No places found." : "No videos or pictures here.");
    ImGui::PopStyleColor();
  }
  TouchScroll();
  ImGui::EndChild();
  if (!enter.empty()) {
    g_m.path = enter;
    g_m.search.clear();
    g_m.dirty = true;
  }
  // import
  int group = a.st.media.activeGroup;
  char ib[64];
  std::snprintf(ib, sizeof(ib), "IMPORT %d", int(g_m.picked.size()));
  ImGui::BeginDisabled(g_m.picked.empty());
  if (Toggle(ib, !g_m.picked.empty(), ImVec2((w - sp) * 0.5f, 46 * s), kGo)) {
    std::vector<std::string> files(g_m.picked.begin(), g_m.picked.end());
    a.importer.import(a.st.media, group, files);
    g_m.picked.clear();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  bool anyFiles = false;
  for (const Entry& e : g_m.entries) anyFiles = anyFiles || !e.dir;
  ImGui::BeginDisabled(!anyFiles);
  if (ImGui::Button("IMPORT ALL", ImVec2((w - sp) * 0.5f, 46 * s))) {
    std::vector<std::string> files;
    for (const Entry& e : g_m.entries)
      if (!e.dir) files.push_back(e.path);
    a.importer.import(a.st.media, group, files);
    g_m.picked.clear();
  }
  ImGui::EndDisabled();
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("Imported into \"%s\", converted to HAP in the project folder.",
                     a.st.media.groups.empty() ? "" : a.st.media.groups[size_t(group)].name.c_str());
  ImGui::PopStyleColor();
  ImGui::EndChild();
}

// ---- middle: groups and clips ----
void ClipCell(App& a, const std::string& id, const MediaClip* c, float cw, float th) {
  float s = S();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  bool live = a.st.look.mediaClip == id;
  bool sel = a.mediaSelected == id;
  ImGui::BeginGroup();
  ImVec2 p = ImGui::GetCursorScreenPos();
  // the picture: tap to play
  if (ImGui::InvisibleButton("thumb", ImVec2(cw, th))) {
    if (!c || c->status == CLIP_READY) goLive(a, id);
    a.mediaSelected = id;
  }
  ImVec2 p1(p.x + cw, p.y + th);
  dl->AddRectFilled(p, p1, IM_COL32(12, 12, 15, 255), 4 * s);
  if (!c) {  // LIVE (the video input)
    if (a.video.hasPicture()) dl->AddImage((ImTextureID)(intptr_t)a.video.texture(), p, p1, ImVec2(0, 1), ImVec2(1, 0));
    ImVec2 ts = ImGui::CalcTextSize("LIVE INPUT");
    dl->AddRectFilled(ImVec2(p.x + 4 * s, p.y + 4 * s), ImVec2(p.x + 12 * s + ts.x, p.y + 8 * s + ts.y), IM_COL32(0, 0, 0, 170), 3 * s);
    dl->AddText(ImVec2(p.x + 8 * s, p.y + 6 * s), ImGui::GetColorU32(kBack), "LIVE INPUT");
  } else {
    GLuint t = thumbOf(a, id);
    if (t) dl->AddImage((ImTextureID)(intptr_t)t, p, p1);
    if (c->status == CLIP_CONVERTING) {
      float pr = a.importer.progress(id);
      dl->AddRectFilled(p, p1, IM_COL32(0, 0, 0, 150), 4 * s);
      const char* msg = pr < 0 ? "WAITING..." : "CONVERTING";
      dl->AddText(ImVec2(p.x + 8 * s, p.y + th * 0.35f), IM_COL32(255, 255, 255, 230), msg);
      if (pr >= 0) {
        dl->AddRectFilled(ImVec2(p.x + 6 * s, p1.y - 14 * s), ImVec2(p1.x - 6 * s, p1.y - 6 * s), IM_COL32(60, 60, 70, 255), 3 * s);
        dl->AddRectFilled(ImVec2(p.x + 6 * s, p1.y - 14 * s), ImVec2(p.x + 6 * s + (cw - 12 * s) * pr, p1.y - 6 * s),
                          ImGui::GetColorU32(kAccent), 3 * s);
      }
    } else if (c->status == CLIP_FAILED) {
      dl->AddRectFilled(p, p1, IM_COL32(60, 0, 0, 180), 4 * s);
      dl->AddText(ImVec2(p.x + 8 * s, p.y + th * 0.35f), IM_COL32(255, 120, 110, 255), "FAILED (see settings)");
    }
    if (c->label > 0) dl->AddRectFilled(p, ImVec2(p1.x, p.y + 6 * s), ImGui::GetColorU32(kLabelCols[c->label]), 4 * s);
    if (c->alpha) dl->AddText(ImVec2(p1.x - 22 * s, p.y + 8 * s), IM_COL32(255, 255, 255, 200), "A");
  }
  if (live) {
    dl->AddRect(p, p1, ImGui::GetColorU32(kAccent), 4 * s, 0, 4 * s);
    float pos = a.media.livePosition();
    dl->AddRectFilled(ImVec2(p.x, p1.y - 4 * s), ImVec2(p.x + cw * pos, p1.y), ImGui::GetColorU32(kAccent));
  }
  if (sel) dl->AddRect(ImVec2(p.x - 2 * s, p.y - 2 * s), ImVec2(p1.x + 2 * s, p1.y + 2 * s), ImGui::GetColorU32(kBack), 5 * s, 0, 2 * s);
  // the name: tap to preview and edit
  std::string name = c ? c->name : std::string("Live input");
  if (Toggle((name + "##nm").c_str(), sel, ImVec2(cw, 34 * s), kBack)) a.mediaSelected = id;
  ImGui::EndGroup();
}

void ClipGrid(App& a, float h) {
  float s = S();
  MediaLibrary& m = a.st.media;
  ImGui::BeginChild("##grid", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  // group tabs
  for (size_t gi = 0; gi < m.groups.size(); ++gi) {
    ImGui::PushID(int(gi));
    if (gi) ImGui::SameLine();
    if (Toggle(m.groups[gi].name.c_str(), m.activeGroup == int(gi), ImVec2(0, 40 * s))) m.activeGroup = int(gi);
    ImGui::PopID();
  }
  ImGui::SameLine();
  if (ImGui::Button("+ GROUP", ImVec2(0, 40 * s))) OpenKeyboard("mgroupnew", "Group " + std::to_string(m.groups.size() + 1), "New group");
  ImGui::SameLine();
  if (ImGui::Button("RENAME", ImVec2(0, 40 * s)) && !m.groups.empty())
    OpenKeyboard("mgroupren", m.groups[size_t(m.activeGroup)].name, "Group name");
  ImGui::SameLine();
  bool armed = ImGui::GetTime() - g_m.groupDeleteArmed < 3.0;
  ImGui::BeginDisabled(m.groups.size() <= 1);
  if (Toggle(armed ? "TAP AGAIN" : "DELETE GROUP", armed, ImVec2(0, 40 * s), kDanger)) {
    if (armed) {
      MediaGroup& g = m.groups[size_t(m.activeGroup)];
      std::vector<std::string> ids;
      for (const MediaClip& c : g.clips) ids.push_back(c.id);
      for (const std::string& id : ids) a.importer.remove(m, id);
      m.groups.erase(m.groups.begin() + m.activeGroup);
      m.activeGroup = std::max(0, m.activeGroup - 1);
      g_m.groupDeleteArmed = -10;
    } else {
      g_m.groupDeleteArmed = ImGui::GetTime();
    }
  }
  ImGui::EndDisabled();
  std::string txt;
  if (KeyboardResult("mgroupnew", txt) && !txt.empty()) {
    m.groups.push_back(MediaGroup{txt, {}});
    m.activeGroup = int(m.groups.size()) - 1;
  }
  if (KeyboardResult("mgroupren", txt) && !txt.empty() && !m.groups.empty()) m.groups[size_t(m.activeGroup)].name = txt;

  // clips
  ImGui::BeginChild("##cells", ImVec2(0, 0));
  float avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
  int cols = std::max(2, int((avail + sp) / (190 * s + sp)));
  float cw = (avail - sp * (cols - 1)) / cols, th = cw * 9.f / 16.f;
  int k = 0;
  ImGui::PushID("live");
  ClipCell(a, kMediaLive, nullptr, cw, th);
  ImGui::PopID();
  ++k;
  if (!m.groups.empty()) {
    MediaGroup& g = m.groups[size_t(std::clamp(m.activeGroup, 0, int(m.groups.size()) - 1))];
    for (size_t i = 0; i < g.clips.size(); ++i, ++k) {
      if (k % cols) ImGui::SameLine();
      ImGui::PushID(g.clips[i].id.c_str());
      ClipCell(a, g.clips[i].id, &g.clips[i], cw, th);
      ImGui::PopID();
    }
    if (g.clips.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
      ImGui::TextWrapped("No clips in this group yet: pick videos or pictures in FILES and tap IMPORT. "
                         "Tap a clip's picture to play it, its name to preview it and change its settings.");
      ImGui::PopStyleColor();
    }
  }
  TouchScroll();
  ImGui::EndChild();
  ImGui::EndChild();
}

// ---- right: monitors ----
void Monitors(App& a, float h) {
  float s = S();
  ImGui::BeginChild("##monitors", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  float w = ImGui::GetContentRegionAvail().x;
  float aspect = float(a.out.w) / std::max(1, a.out.h);
  // both monitors fit in the column (labels and buttons take about 190 px)
  float mh = std::min(w / aspect, std::max(60.f * s, (h - 200.f * s) * 0.5f));
  w = std::min(w, mh * aspect);
  const MediaClip* sc = findClip(a.st.media, a.mediaSelected);
  std::string pname = a.mediaSelected == kMediaLive ? "Live input" : (sc ? sc->name : "");
  ImGui::TextColored(kBack, "PREVIEW  %s", pname.c_str());
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + mh), IM_COL32(0, 0, 0, 255));
  if (!a.mediaSelected.empty() && a.media.preview())
    dl->AddImage((ImTextureID)(intptr_t)a.media.preview(), p, ImVec2(p.x + w, p.y + mh), ImVec2(0, 1), ImVec2(1, 0));
  dl->AddRect(p, ImVec2(p.x + w, p.y + mh), ImGui::GetColorU32(kBack), 3 * s);
  ImGui::Dummy(ImVec2(w, mh));
  if (!a.mediaSelected.empty()) {
    float pos = a.media.previewPosition();
    ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(q, ImVec2(q.x + w, q.y + 4 * s), IM_COL32(50, 50, 60, 255));
    dl->AddRectFilled(q, ImVec2(q.x + w * pos, q.y + 4 * s), ImGui::GetColorU32(kBack));
    ImGui::Dummy(ImVec2(w, 6 * s));
    if (Toggle("PLAY IT", true, ImVec2(w, 40 * s), kGo)) {
      if (!sc || sc->status == CLIP_READY) goLive(a, a.mediaSelected);
    }
  }
  ImGui::Dummy(ImVec2(0, 6 * s));
  const MediaClip* lc = findClip(a.st.media, a.st.look.mediaClip);
  std::string lname = a.st.look.mediaClip == kMediaLive ? "Live input" : (lc ? lc->name : "(nothing)");
  ImGui::TextColored(kAccent, "PLAYING  %s", lname.c_str());
  p = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + mh), IM_COL32(0, 0, 0, 255));
  if (a.media.output()) dl->AddImage((ImTextureID)(intptr_t)a.media.output(), p, ImVec2(p.x + w, p.y + mh), ImVec2(0, 1), ImVec2(1, 0));
  dl->AddRect(p, ImVec2(p.x + w, p.y + mh), ImGui::GetColorU32(kAccent), 3 * s);
  ImGui::Dummy(ImVec2(w, mh));
  std::string st = a.media.status();
  if (!st.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
    ImGui::TextWrapped("%s", st.c_str());
    ImGui::PopStyleColor();
  }
  float sp = ImGui::GetStyle().ItemSpacing.x;
  if (ImGui::Button("RESTART", ImVec2((w - sp) * 0.5f, 40 * s))) a.media.restart();
  ImGui::SameLine();
  if (Toggle("STOP", false, ImVec2((w - sp) * 0.5f, 40 * s), kDanger)) a.st.look.mediaClip.clear();
  const Look& l = a.st.look;
  bool shown = (l.effect == FX_VIDEO && l.video.source == VIDSRC_MEDIA) ||
               (l.bg.on && l.bg.effect == FX_VIDEO && l.bg.fx.video.source == VIDSRC_MEDIA);
  if (!shown && !l.mediaClip.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.3f, 1));
    ImGui::TextWrapped("Not on the output: the BACK layer is not on MEDIA (tap a clip's picture to put it there).");
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
}

// ---- bottom: the selected clip's settings ----
bool Slider(const char* label, float* v, float lo, float hi, const char* fmt, float mul = 1.f) {
  char vt[32];
  std::snprintf(vt, sizeof(vt), fmt, *v * mul);
  return TouchSlider(label, v, lo, hi, vt);
}

void Settings(App& a) {
  float s = S();
  MediaLibrary& m = a.st.media;
  ImGui::BeginChild("##clipset", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
  bool liveSel = a.mediaSelected == kMediaLive;
  MediaClip* c = findClip(m, a.mediaSelected);
  if (!c && !liveSel) {
    ImGui::TextColored(kTextDim, "Tap a clip's name to preview it and change its settings here.");
    // the media layer's transition
    ImGui::Dummy(ImVec2(0, 6 * s));
    ImGui::TextColored(kAccent, "WHEN THE CLIP CHANGES");
    static const char* tr[] = {"CUT", "CROSSFADE", "FADE THROUGH BLACK"};
    Choice("mtrans", tr, MTRANS_COUNT, &m.transition, 3, 44 * s);
    if (m.transition != MTRANS_CUT) Slider("Time", &m.transTime, 0.f, 5.f, "%.1f s");
    ImGui::EndChild();
    return;
  }
  ClipSettings& k = liveSel ? m.liveSet : c->set;
  float sp = ImGui::GetStyle().ItemSpacing.x;
  // header: name, info, label colours, actions
  ImGui::PushFont(a.fontBig);
  ImGui::TextUnformatted(liveSel ? "Live input" : c->name.c_str());
  ImGui::PopFont();
  if (c) {
    ImGui::SameLine();
    if (ImGui::Button("RENAME", ImVec2(0, 40 * s))) OpenKeyboard("mcliprename", c->name, "Clip name");
    std::string txt;
    if (KeyboardResult("mcliprename", txt) && !txt.empty()) c->name = txt;
    ImGui::SameLine();
    char info[160];
    if (c->status == CLIP_READY)
      std::snprintf(info, sizeof(info), "%dx%d   %s   %s%s", c->width, c->height,
                    c->still ? "picture" : (timeText(c->frames / std::max(1.f, c->fps)) + "  " + std::to_string(int(std::lround(c->fps))) + " fps").c_str(),
                    c->alpha ? "HAP Alpha" : "HAP Q", "");
    else std::snprintf(info, sizeof(info), "%s", c->status == CLIP_CONVERTING ? "converting..." : "failed");
    ImGui::TextColored(kTextDim, "%s", info);
    if (c->status == CLIP_FAILED) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
      ImGui::TextWrapped("%s", c->error.c_str());
      ImGui::PopStyleColor();
      if (ImGui::Button("TRY AGAIN", ImVec2(160 * s, 40 * s))) a.importer.retry(m, c->id);
    }
    // label colours
    for (int i = 0; i < kNumClipLabels; ++i) {
      if (i) ImGui::SameLine();
      ImGui::PushID(i);
      ImVec2 q = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("lbl", ImVec2(34 * s, 30 * s))) c->label = i;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(q, ImVec2(q.x + 34 * s, q.y + 30 * s), ImGui::GetColorU32(kLabelCols[i]), 4 * s);
      if (c->label == i) dl->AddRect(q, ImVec2(q.x + 34 * s, q.y + 30 * s), IM_COL32(255, 255, 255, 255), 4 * s, 0, 3 * s);
      if (i == 0) dl->AddText(ImVec2(q.x + 6 * s, q.y + 5 * s), IM_COL32(220, 220, 220, 255), "-");
      ImGui::PopID();
    }
    // move / order / delete
    ImGui::SameLine(0, 20 * s);
    int gi = -1, ci = -1;
    for (size_t g = 0; g < m.groups.size(); ++g)
      for (size_t i = 0; i < m.groups[g].clips.size(); ++i)
        if (&m.groups[g].clips[i] == c) gi = int(g), ci = int(i);
    if (ImGui::Button("<", ImVec2(44 * s, 30 * s)) && gi >= 0 && ci > 0) {
      std::swap(m.groups[size_t(gi)].clips[size_t(ci)], m.groups[size_t(gi)].clips[size_t(ci - 1)]);
      c = findClip(m, a.mediaSelected);
    }
    ImGui::SameLine();
    if (gi >= 0 && ImGui::Button(">", ImVec2(44 * s, 30 * s)) && ci + 1 < int(m.groups[size_t(gi)].clips.size())) {
      std::swap(m.groups[size_t(gi)].clips[size_t(ci)], m.groups[size_t(gi)].clips[size_t(ci + 1)]);
      c = findClip(m, a.mediaSelected);
    }
    ImGui::SameLine();
    if (ImGui::Button("MOVE TO GROUP", ImVec2(0, 30 * s))) ImGui::OpenPopup("##movegroup");
    if (ImGui::BeginPopup("##movegroup")) {
      for (size_t g = 0; g < m.groups.size(); ++g) {
        ImGui::PushID(int(g));
        if (int(g) != gi && ImGui::Button(m.groups[g].name.c_str(), ImVec2(260 * s, 44 * s)) && gi >= 0) {
          MediaClip moved = *c;
          m.groups[size_t(gi)].clips.erase(m.groups[size_t(gi)].clips.begin() + ci);
          m.groups[g].clips.push_back(moved);
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
      }
      ImGui::EndPopup();
    }
    c = findClip(m, a.mediaSelected);
    if (!c) {
      ImGui::EndChild();
      return;
    }
    ImGui::SameLine();
    bool armed = g_m.deleteId == c->id && ImGui::GetTime() - g_m.deleteArmed < 3.0;
    if (Toggle(armed ? "TAP AGAIN" : "DELETE CLIP", armed, ImVec2(0, 30 * s), kDanger)) {
      if (armed) {
        std::string id = c->id;
        if (a.st.look.mediaClip == id) a.st.look.mediaClip.clear();
        a.importer.remove(m, id);
        a.mediaSelected.clear();
        g_m.deleteId.clear();
        ImGui::EndChild();
        return;
      }
      g_m.deleteId = c->id;
      g_m.deleteArmed = ImGui::GetTime();
    }
  }
  // tabs
  static const char* tabs[] = {"PLAY", "TRANSFORM", "COLOUR"};
  Choice("msettab", tabs, 3, &g_m.tab, 3, 40 * s);
  float colW = (ImGui::GetContentRegionAvail().x - sp * 2) / 3;
  if (ImGui::BeginTable("##mset", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    if (g_m.tab == 0) {
      static const char* plays[] = {"LOOP", "ONCE + HOLD", "ONCE + BLACK", "BOUNCE"};
      Choice("mplay", plays, PLAY_COUNT, &k.play, 2, 40 * s);
      float dur = c ? c->frames / std::max(1.f, c->fps) : 0.f;
      char vt[48];
      std::snprintf(vt, sizeof(vt), "%s", timeText(k.in * dur).c_str());
      if (TouchSlider("In", &k.in, 0.f, 1.f, vt)) k.out = std::max(k.out, k.in);
      std::snprintf(vt, sizeof(vt), "%s", timeText(k.out * dur).c_str());
      if (TouchSlider("Out", &k.out, 0.f, 1.f, vt)) k.in = std::min(k.in, k.out);
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(k.beatSync);
      Slider("Speed", &k.speed, 0.f, 4.f, "%.0f %%", 100.f);
      ImGui::EndDisabled();
      if (Toggle(k.reverse ? "REVERSE: ON" : "REVERSE: OFF", k.reverse, ImVec2(ImGui::GetContentRegionAvail().x, 40 * s)))
        k.reverse = !k.reverse;
      if (Toggle(k.beatSync ? "BEAT SYNC: ON" : "BEAT SYNC: OFF", k.beatSync, ImVec2(ImGui::GetContentRegionAvail().x, 40 * s)))
        k.beatSync = !k.beatSync;
      if (k.beatSync) {
        static const char* beats[] = {"1", "2", "4", "8", "16", "32", "64"};
        int bi = 4;
        for (int i = 0; i < 7; ++i)
          if (kBeatChoices[i] == k.beats) bi = i;
        if (Choice("mbeats", beats, 7, &bi, 7, 38 * s)) k.beats = kBeatChoices[bi];
        ImGui::TextColored(kTextDim, "The clip (In to Out) lasts %d beats at the BPM.", k.beats);
      }
      ImGui::TableNextColumn();
      static const char* tr[] = {"LAYER", "CUT", "CROSSFADE", "BLACK"};
      int t = k.transition + 1;
      ImGui::TextColored(kTextDim, "Transition into this clip");
      if (Choice("mctrans", tr, 4, &t, 4, 38 * s)) k.transition = t - 1;
      if (k.transition > MTRANS_CUT) Slider("Time", &k.transTime, 0.f, 5.f, "%.1f s");
      ImGui::TextColored(kTextDim, "LAYER = the media layer's own setting:");
      static const char* tr2[] = {"CUT", "XFADE", "BLACK"};
      Choice("mtrans2", tr2, MTRANS_COUNT, &m.transition, 3, 38 * s);
      if (m.transition != MTRANS_CUT) Slider("Layer time", &m.transTime, 0.f, 5.f, "%.1f s");
    } else if (g_m.tab == 1) {
      Slider("Scale", &k.scale, 0.05f, 4.f, "%.0f %%", 100.f);
      Slider("Scale X", &k.scaleX, 0.05f, 4.f, "%.0f %%", 100.f);
      Slider("Scale Y", &k.scaleY, 0.05f, 4.f, "%.0f %%", 100.f);
      ImGui::TableNextColumn();
      Slider("Position X", &k.posX, -1.f, 1.f, "%+.0f %%", 100.f);
      Slider("Position Y", &k.posY, -1.f, 1.f, "%+.0f %%", 100.f);
      Slider("Rotation", &k.rotation, -180.f, 180.f, "%.0f deg");
      float hw = (ImGui::GetContentRegionAvail().x - sp) / 2;
      if (Toggle("FLIP H", k.flipH, ImVec2(hw, 40 * s))) k.flipH = !k.flipH;
      ImGui::SameLine();
      if (Toggle("FLIP V", k.flipV, ImVec2(hw, 40 * s))) k.flipV = !k.flipV;
      ImGui::TableNextColumn();
      Slider("Crop left", &k.cropL, 0.f, 0.9f, "%.0f %%", 100.f);
      Slider("Crop right", &k.cropR, 0.f, 0.9f, "%.0f %%", 100.f);
      Slider("Crop top", &k.cropT, 0.f, 0.9f, "%.0f %%", 100.f);
      Slider("Crop bottom", &k.cropB, 0.f, 0.9f, "%.0f %%", 100.f);
      if (ImGui::Button("RESET TRANSFORM", ImVec2(-1, 40 * s))) {
        ClipSettings d;
        k.scale = d.scale, k.scaleX = d.scaleX, k.scaleY = d.scaleY, k.posX = d.posX, k.posY = d.posY;
        k.rotation = d.rotation, k.flipH = d.flipH, k.flipV = d.flipV;
        k.cropL = k.cropR = k.cropT = k.cropB = 0.f;
      }
    } else {
      Slider("Gamma", &k.gamma, 0.2f, 3.f, "%.2f");
      Slider("Hue", &k.hue, -180.f, 180.f, "%+.0f deg");
      ImGui::TableNextColumn();
      Slider("Saturation", &k.saturation, 0.f, 2.f, "%.0f %%", 100.f);
      Slider("Blur", &k.blur, 0.f, 1.f, "%.0f %%", 100.f);
      ImGui::TableNextColumn();
      if (ImGui::Button("RESET COLOUR", ImVec2(-1, 40 * s))) {
        ClipSettings d;
        k.gamma = d.gamma, k.hue = d.hue, k.saturation = d.saturation, k.blur = d.blur;
      }
    }
    ImGui::EndTable();
  }
  (void)colW;
  TouchScroll();
  ImGui::EndChild();
}

}  // namespace

void MediaWindowUI(App& a) {
  float s = S();
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##media", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  // top bar
  float sp = ImGui::GetStyle().ItemSpacing.x;
  if (ImGui::Button("< TOUCHFX", ImVec2(150 * s, 46 * s))) SDL_RaiseWindow(a.window);
  ImGui::SameLine();
  ImGui::PushFont(a.fontBig);
  ImGui::TextColored(kBack, "MEDIA");
  ImGui::PopFont();
  ImGui::SameLine(0, 20 * s);
  int pending = a.importer.pending();
  if (pending > 0) ImGui::TextColored(kAccent, "Converting %d clip%s...", pending, pending > 1 ? "s" : "");
  else if (!a.showLoaded) ImGui::TextColored(kTextDim, "Open a show to use the media server.");
  float right = 2 * (150 * s) + sp;
  ImGui::SameLine(ImGui::GetWindowWidth() - right - ImGui::GetStyle().WindowPadding.x);
  bool full = a.st.layout.mediaFull;
  if (Toggle("FULLSCREEN", full, ImVec2(150 * s, 46 * s))) {
    a.st.layout.mediaFull = !full;
    platformSetFullscreen(a.mediaWin, a.st.layout.mediaFull);
  }
  ImGui::SameLine();
  if (ImGui::Button("CLOSE", ImVec2(150 * s, 46 * s))) a.wantCloseMediaWin = true;

  if (!a.showLoaded) {
    ImGui::End();
    return;
  }
  float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
  float topH = H * 0.6f;
  float leftW = std::max(260 * s, W * 0.24f), rightW = std::max(280 * s, W * 0.24f);
  float midW = W - leftW - rightW - sp * 2;
  ImGui::BeginChild("##left", ImVec2(leftW, topH));
  Browser(a, topH);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##mid", ImVec2(midW, topH));
  ClipGrid(a, topH);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##right", ImVec2(rightW, topH));
  Monitors(a, topH);
  ImGui::EndChild();
  Settings(a);
  ImGui::End();
  KeyboardFrame(a);
}

}  // namespace ui
