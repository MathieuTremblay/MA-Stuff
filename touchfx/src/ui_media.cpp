// The MEDIA window, laid out like this (every border can be dragged; RESET LAYOUT puts it back):
//
//   FILES (full height)  |  CLIPS: groups like folders, large icons / details  |  CLIP SETTINGS
//   places | folder view  |---------------------------------------------------- |  (full height)
//   + preview of the file |  PREVIEW                |  PLAYING                  |
//
// FILES works like the Windows file explorer: Back / Forward / Up, a clickable address bar, Quick access and the
// drives on the left, large icons (thumbnails of the videos) or details, and a preview of the selected file.
// CLIPS: like Resolume, tap a clip's NAME to preview it and edit its settings, tap its PICTURE to play it.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "app.hpp"
#include "imgui.h"
#include "imgui_internal.h"
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
  uint64_t size = 0;
  long long mtime = 0;  // seconds
};

struct Strip {
  GLuint tex = 0;
  int frames = 1, version = -1;
};

struct MediaUi {
  // files
  std::string path;  // "" = This PC
  std::vector<std::string> back, fwd;
  std::string search, lastSearch, lastPath = "\x01";
  std::vector<Entry> entries;
  bool dirty = true;
  double listedAt = 0;
  std::string selFile;           // the file shown in the file preview
  std::set<std::string> picked;  // ticked for IMPORT
  int sortCol = 0;
  bool sortAsc = true;
  std::map<std::string, Strip> strips;  // by file path
  // clips
  int groupView = -2;  // -1 = all groups (folders), >= 0 a group; -2 = follow the active group
  std::map<std::string, GLuint> thumbs;
  int thumbVersion = -1;
  int tab = 0;  // settings: PLAY / TRANSFORM / COLOUR
  double deleteArmed = -10, groupDeleteArmed = -10;
  std::string deleteId;
} g_m;

std::string u8(const fs::path& p) { return p.u8string(); }

std::string lower(std::string s) {
  for (char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

long long fileTimeSec(const fs::directory_entry& e) {
  std::error_code ec;
  auto t = e.last_write_time(ec);
  if (ec) return 0;
  auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  return (long long)std::chrono::system_clock::to_time_t(sys);
}

std::string sizeText(uint64_t b) {
  char t[32];
  if (b >= 1ull << 30) std::snprintf(t, sizeof(t), "%.1f GB", double(b) / double(1ull << 30));
  else if (b >= 1ull << 20) std::snprintf(t, sizeof(t), "%.1f MB", double(b) / double(1ull << 20));
  else std::snprintf(t, sizeof(t), "%.0f KB", double(b) / 1024.0);
  return t;
}

std::string dateText(long long t) {
  if (t <= 0) return "";
  std::time_t tt = std::time_t(t);
  char b[32];
  std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M", std::localtime(&tt));
  return b;
}

std::string timeText(float sec) {
  if (sec < 0) return "";
  char b[32];
  int m = int(sec) / 60;
  std::snprintf(b, sizeof(b), "%d:%05.2f", m, sec - m * 60.f);
  return b;
}

// ---- places (Quick access + This PC) ----
struct Place {
  std::string label, path;
};
std::vector<Place> quickAccess() {
  std::vector<Place> out;
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
      if (fs::is_directory(h / sub, ec)) out.push_back({sub, u8(h / sub)});
  return out;
}
std::vector<Place> thisPc() {
  std::vector<Place> out;
  for (const DriveInfo& d : listDrives(true)) out.push_back({d.label, d.root});
  return out;
}

void listFolder(const std::string& dir, const std::string& search, std::vector<Entry>& out) {
  out.clear();
  std::error_code ec;
  std::string q = lower(search);
  auto opts = fs::directory_options::skip_permission_denied;
  if (q.size() >= 2) {  // search: this folder and the folders inside it
    int seen = 0;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(dir), opts, ec);
         it != fs::recursive_directory_iterator() && seen < 20000 && out.size() < 400; it.increment(ec)) {
      if (ec) break;
      ++seen;
      if (it.depth() > 4) {
        it.disable_recursion_pending();
        continue;
      }
      std::string name = u8(it->path().filename());
      bool d = it->is_directory(ec);
      if (!name.empty() && (name[0] == '.' || name[0] == '$')) {
        if (d) it.disable_recursion_pending();
        continue;
      }
      if (lower(name).find(q) == std::string::npos) continue;
      if (!d && !isMediaFile(u8(it->path()))) continue;
      Entry e{u8(fs::relative(it->path(), fs::u8path(dir), ec)), u8(it->path()), d, 0, fileTimeSec(*it)};
      if (!d) e.size = it->file_size(ec);
      out.push_back(e);
    }
  } else {
    for (const auto& de : fs::directory_iterator(fs::u8path(dir), opts, ec)) {
      std::string name = u8(de.path().filename());
      if (name.empty() || name[0] == '.' || name[0] == '$') continue;
      bool d = de.is_directory(ec);
      if (!d && !isMediaFile(u8(de.path()))) continue;
      if (!q.empty() && lower(name).find(q) == std::string::npos) continue;
      Entry e{name, u8(de.path()), d, 0, fileTimeSec(de)};
      if (!d) e.size = de.file_size(ec);
      out.push_back(e);
    }
  }
}

void sortEntries() {
  auto ext = [](const Entry& e) { return lower(fs::u8path(e.name).extension().u8string()); };
  std::stable_sort(g_m.entries.begin(), g_m.entries.end(), [&](const Entry& a, const Entry& b) {
    if (a.dir != b.dir) return a.dir;  // folders first, like Windows
    bool less;
    switch (g_m.sortCol) {
      case 1: less = a.mtime < b.mtime; break;
      case 2: less = ext(a) < ext(b); break;
      case 3: less = a.size < b.size; break;
      default: less = lower(a.name) < lower(b.name); break;
    }
    return g_m.sortAsc ? less : !less && lower(a.name) != lower(b.name);
  });
}

void navigate(const std::string& p, bool remember = true) {
  if (p == g_m.path) return;
  if (remember) {
    g_m.back.push_back(g_m.path);
    g_m.fwd.clear();
  }
  g_m.path = p;
  g_m.search.clear();
  g_m.dirty = true;
}

GLuint loadPpm(const std::string& path, int* wOut = nullptr) {
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) return 0;
  std::string magic;
  int w = 0, h = 0, mx = 0;
  f >> magic >> w >> h >> mx;
  f.get();
  if (magic != "P6" || w <= 0 || h <= 0 || w > 8192 || h > 4096 || mx != 255) return 0;
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
  if (wOut) *wOut = w;
  return t;
}

// The strip of frames for a file in the browser (made in the background).
const Strip* stripOf(App& a, const Entry& e, const FileThumbs::Info** infoOut = nullptr) {
  const FileThumbs::Info& in = a.fileThumbs.get(e.path, e.size, e.mtime);
  if (infoOut) *infoOut = &in;
  Strip& s = g_m.strips[e.path];
  if (in.ready && s.version != in.version) {
    if (s.tex) glDeleteTextures(1, &s.tex);
    s.tex = loadPpm(in.strip);
    s.frames = std::max(1, in.frames);
    s.version = in.version;
  }
  return s.tex ? &s : nullptr;
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

void folderIcon(ImDrawList* dl, ImVec2 p, ImVec2 sz, float s) {
  ImU32 c1 = IM_COL32(232, 186, 70, 255), c2 = IM_COL32(250, 210, 100, 255);
  float tabW = sz.x * 0.4f, tabH = sz.y * 0.14f;
  dl->AddRectFilled(ImVec2(p.x, p.y + tabH * 0.2f), ImVec2(p.x + tabW, p.y + tabH * 1.6f), c1, 3 * s);
  dl->AddRectFilled(ImVec2(p.x, p.y + tabH), ImVec2(p.x + sz.x, p.y + sz.y), c1, 4 * s);
  dl->AddRectFilled(ImVec2(p.x, p.y + tabH * 2.2f), ImVec2(p.x + sz.x, p.y + sz.y), c2, 4 * s);
}

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

// ---- dragged borders ----
// A vertical bar between two side-by-side areas: drag sideways. frac = left area / total.
void VSplit(const char* id, float* frac, float total, float h, float lo, float hi, bool invert = false) {
  float s = S();
  float bw = 12 * s;
  ImGui::SameLine(0, 0);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(bw, h));
  if (ImGui::IsItemActive() && total > 1) {
    float d = ImGui::GetIO().MouseDelta.x / total;
    *frac = std::clamp(*frac + (invert ? -d : d), lo, hi);
  }
  bool hot = ImGui::IsItemActive() || ImGui::IsItemHovered();
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + bw * 0.5f - 1.5f * s, p.y + h * 0.5f - 24 * s),
                                            ImVec2(p.x + bw * 0.5f + 1.5f * s, p.y + h * 0.5f + 24 * s),
                                            hot ? ImGui::GetColorU32(kAccent) : IM_COL32(90, 90, 105, 255), 2 * s);
  ImGui::SameLine(0, 0);
}
// A horizontal bar between two stacked areas: drag up / down. frac = BOTTOM area / total.
void HSplit(const char* id, float* frac, float total, float w, float lo, float hi) {
  float s = S();
  float bh = 12 * s;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, ImVec2(w, bh));
  if (ImGui::IsItemActive() && total > 1) *frac = std::clamp(*frac - ImGui::GetIO().MouseDelta.y / total, lo, hi);
  bool hot = ImGui::IsItemActive() || ImGui::IsItemHovered();
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + w * 0.5f - 24 * s, p.y + bh * 0.5f - 1.5f * s),
                                            ImVec2(p.x + w * 0.5f + 24 * s, p.y + bh * 0.5f + 1.5f * s),
                                            hot ? ImGui::GetColorU32(kAccent) : IM_COL32(90, 90, 105, 255), 2 * s);
}

// =====================================================================================================
// FILES
void FilePreview(App& a, float h);

void Files(App& a, float h) {
  float s = S();
  LayoutSettings& L = a.st.layout;
  ImGui::BeginChild("##files", ImVec2(ImGui::GetContentRegionAvail().x, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  float w = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
  float bh = 40 * s;
  // toolbar: back, forward, up | view
  ImGui::BeginDisabled(g_m.back.empty());
  if (ImGui::Button("<##back", ImVec2(44 * s, bh))) {
    g_m.fwd.push_back(g_m.path);
    std::string p = g_m.back.back();
    g_m.back.pop_back();
    navigate(p, false);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_m.fwd.empty());
  if (ImGui::Button(">##fwd", ImVec2(44 * s, bh))) {
    g_m.back.push_back(g_m.path);
    std::string p = g_m.fwd.back();
    g_m.fwd.pop_back();
    navigate(p, false);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_m.path.empty());
  if (ImGui::Button("UP", ImVec2(54 * s, bh))) {
    fs::path p = fs::u8path(g_m.path);
    fs::path par = p.parent_path();
    navigate(par == p || u8(par).empty() ? std::string() : u8(par));
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  float vw = (w - (44 * 2 + 54) * s - sp * 3 - sp) * 0.5f;
  if (Toggle("ICONS", L.mFilesView == 0, ImVec2(vw, bh))) L.mFilesView = 0;
  ImGui::SameLine();
  if (Toggle("DETAILS", L.mFilesView == 1, ImVec2(vw, bh))) L.mFilesView = 1;

  // address bar: This PC > drive > folders (tap a part to go there)
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3 * s, 4 * s));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.16f, 1));
    std::vector<std::pair<std::string, std::string>> crumbs = {{"This PC", ""}};
    if (!g_m.path.empty()) {
      fs::path p = fs::u8path(g_m.path), acc;
      for (const fs::path& part : p) {
        acc /= part;
        std::string n = u8(part);
        if (n == "/" || n == "\\") continue;
        if (n.empty()) continue;
        crumbs.push_back({n, u8(acc)});
      }
#ifndef _WIN32
      for (auto& c : crumbs)
        if (!c.second.empty() && c.second[0] != '/') c.second = "/" + c.second;
#endif
    }
    float x0 = ImGui::GetCursorPosX(), maxX = x0 + w;
    for (size_t i = 0; i < crumbs.size(); ++i) {
      ImGui::PushID(int(i));
      std::string lbl = crumbs[i].first + (i + 1 < crumbs.size() ? "  >" : "");
      float bwid = ImGui::CalcTextSize(lbl.c_str()).x + 16 * s;
      if (i && ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + bwid < maxX) ImGui::SameLine();
      if (ImGui::Button(lbl.c_str(), ImVec2(bwid, 34 * s))) navigate(crumbs[i].second);
      ImGui::PopID();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
  }
  // search
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%s", g_m.search.c_str());
  ImGui::SetNextItemWidth(w - 70 * s - sp);
  if (ImGui::InputTextWithHint("##search", g_m.path.empty() ? "Search places" : "Search this folder and below", buf,
                               sizeof(buf)))
    g_m.search = buf;
  ImGui::SameLine();
  if (ImGui::Button("KEYS", ImVec2(70 * s, 0))) OpenKeyboard("mediasearch", g_m.search, "Search files and folders");
  std::string txt;
  if (KeyboardResult("mediasearch", txt)) g_m.search = txt;

  // refresh the listing (also every few seconds, for files copied meanwhile)
  double now = ImGui::GetTime();
  if (g_m.search != g_m.lastSearch || g_m.path != g_m.lastPath || now - g_m.listedAt > 5.0) g_m.dirty = true;
  if (g_m.dirty) {
    if (!g_m.path.empty()) listFolder(g_m.path, g_m.search, g_m.entries);
    else g_m.entries.clear();
    sortEntries();
    g_m.lastSearch = g_m.search;
    g_m.lastPath = g_m.path;
    g_m.listedAt = now;
    g_m.dirty = false;
  }

  // body: places | folder, then the preview of the selected file
  float previewH = std::min(230 * s, ImGui::GetContentRegionAvail().y * 0.42f);
  float bodyH = ImGui::GetContentRegionAvail().y - previewH - sp;
  float bodyW = ImGui::GetContentRegionAvail().x;
  float navW = std::max(140 * s, bodyW * L.mNavW);
  ImGui::BeginChild("##nav", ImVec2(navW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
  {
    auto placeBtn = [&](const Place& p) {
      bool cur = !g_m.path.empty() && fs::u8path(g_m.path) == fs::u8path(p.path);
      if (Toggle(p.label.c_str(), cur, ImVec2(ImGui::GetContentRegionAvail().x, 38 * s))) navigate(p.path);
    };
    ImGui::TextColored(kTextDim, "QUICK ACCESS");
    for (const Place& p : quickAccess()) {
      ImGui::PushID(p.path.c_str());
      placeBtn(p);
      ImGui::PopID();
    }
    ImGui::TextColored(kTextDim, "THIS PC");
    for (const Place& p : thisPc()) {
      ImGui::PushID(p.path.c_str());
      placeBtn(p);
      ImGui::PopID();
    }
    TouchScroll();
  }
  ImGui::EndChild();
  VSplit("##navsplit", &L.mNavW, bodyW, bodyH, 0.15f, 0.6f);
  ImGui::BeginChild("##folder", ImVec2(ImGui::GetContentRegionAvail().x, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
  std::string enter;
  if (g_m.path.empty()) {  // This PC: the places as big tiles
    ImGui::TextColored(kTextDim, "Choose a place on the left, or a drive:");
    for (const Place& p : thisPc()) {
      ImGui::PushID(p.path.c_str());
      if (ImGui::Button(p.label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 50 * s))) enter = p.path;
      ImGui::PopID();
    }
  } else if (L.mFilesView == 0) {
    // large icons
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, int((avail + sp) / (150 * s + sp)));
    float cw = (avail - sp * (cols - 1)) / cols, th = cw * 9.f / 16.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < g_m.entries.size(); ++i) {
      const Entry& e = g_m.entries[i];
      ImGui::PushID(int(i));
      if (i % cols) ImGui::SameLine();
      ImGui::BeginGroup();
      ImVec2 p = ImGui::GetCursorScreenPos();
      bool sel = g_m.selFile == e.path, picked = g_m.picked.count(e.path) > 0;
      if (ImGui::InvisibleButton("tile", ImVec2(cw, th + 40 * s))) {
        if (e.dir) enter = e.path;  // folders open with one tap (touch)
        else g_m.selFile = e.path;
      }
      bool dbl = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
      if (dbl && !e.dir) {
        if (picked) g_m.picked.erase(e.path);
        else g_m.picked.insert(e.path);
      }
      ImVec2 p1(p.x + cw, p.y + th);
      if (sel) dl->AddRectFilled(ImVec2(p.x - 3 * s, p.y - 3 * s), ImVec2(p1.x + 3 * s, p1.y + 40 * s), IM_COL32(40, 80, 140, 255), 5 * s);
      if (e.dir) {
        folderIcon(dl, ImVec2(p.x + cw * 0.15f, p.y + th * 0.12f), ImVec2(cw * 0.7f, th * 0.8f), s);
      } else {
        dl->AddRectFilled(p, p1, IM_COL32(12, 12, 15, 255), 4 * s);
        const Strip* st = stripOf(a, e);
        if (st) {
          // the selected file plays through its frames; the others show the first one
          int f = sel ? int(ImGui::GetTime() * 1.6) % st->frames : 0;
          dl->AddImage((ImTextureID)(intptr_t)st->tex, p, p1, ImVec2(float(f) / st->frames, 0), ImVec2(float(f + 1) / st->frames, 1));
        } else {
          std::string x = lower(fs::u8path(e.name).extension().u8string());
          ImVec2 ts = ImGui::CalcTextSize(x.c_str());
          dl->AddText(ImVec2(p.x + (cw - ts.x) * 0.5f, p.y + (th - ts.y) * 0.5f), IM_COL32(150, 150, 160, 255), x.c_str());
        }
        // tick box (top left): tap it to pick the file for IMPORT
        ImVec2 cb(p.x + 4 * s, p.y + 4 * s), cb1(cb.x + 26 * s, cb.y + 26 * s);
        dl->AddRectFilled(cb, cb1, picked ? ImGui::GetColorU32(kGo) : IM_COL32(0, 0, 0, 150), 4 * s);
        dl->AddRect(cb, cb1, IM_COL32(255, 255, 255, 200), 4 * s, 0, 1.5f * s);
        if (picked) {
          dl->AddLine(ImVec2(cb.x + 6 * s, cb.y + 13 * s), ImVec2(cb.x + 11 * s, cb.y + 19 * s), IM_COL32(255, 255, 255, 255), 3 * s);
          dl->AddLine(ImVec2(cb.x + 11 * s, cb.y + 19 * s), ImVec2(cb.x + 20 * s, cb.y + 7 * s), IM_COL32(255, 255, 255, 255), 3 * s);
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsMouseHoveringRect(ImVec2(cb.x - 6 * s, cb.y - 6 * s), ImVec2(cb1.x + 8 * s, cb1.y + 8 * s))) {
          if (picked) g_m.picked.erase(e.path);
          else g_m.picked.insert(e.path);
        }
      }
      // name (two lines at most)
      ImGui::PushClipRect(ImVec2(p.x, p1.y), ImVec2(p1.x, p1.y + 40 * s), true);
      ImFont* f = a.fontSmall ? a.fontSmall : ImGui::GetFont();
      dl->AddText(f, f->FontSize, ImVec2(p.x + 2 * s, p1.y + 3 * s), ImGui::GetColorU32(kText), e.name.c_str(), nullptr, cw - 4 * s);
      ImGui::PopClipRect();
      ImGui::EndGroup();
      ImGui::PopID();
    }
  } else {
    // details: Name | Date modified | Type | Size  (tap a heading to sort)
    static const char* heads[] = {"Name", "Date modified", "Type", "Size"};
    if (ImGui::BeginTable("##details", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.f);
      ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthStretch, 1.6f);
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.7f);
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.9f);
      ImGui::TableNextRow();
      for (int c = 0; c < 4; ++c) {
        ImGui::TableNextColumn();
        std::string h = std::string(heads[c]) + (g_m.sortCol == c ? (g_m.sortAsc ? " ^" : " v") : "");
        ImGui::PushID(c);
        if (ImGui::Button(h.c_str(), ImVec2(-1, 34 * s))) {
          if (g_m.sortCol == c) g_m.sortAsc = !g_m.sortAsc;
          else g_m.sortCol = c, g_m.sortAsc = true;
          sortEntries();
        }
        ImGui::PopID();
      }
      for (size_t i = 0; i < g_m.entries.size(); ++i) {
        const Entry& e = g_m.entries[i];
        ImGui::TableNextRow(0, 40 * s);
        ImGui::TableNextColumn();
        ImGui::PushID(int(i));
        bool picked = g_m.picked.count(e.path) > 0;
        std::string lbl = (e.dir ? "[folder]  " : (picked ? "[x]  " : "[ ]  ")) + e.name;
        if (ImGui::Selectable(lbl.c_str(), g_m.selFile == e.path || picked, ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0, 34 * s))) {
          if (e.dir) enter = e.path;
          else {
            // tap the [ ] part to pick, the rest to preview
            if (ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x < 46 * s) {
              if (picked) g_m.picked.erase(e.path);
              else g_m.picked.insert(e.path);
            }
            g_m.selFile = e.path;
          }
        }
        ImGui::TableNextColumn();
        ImGui::TextColored(kTextDim, "%s", dateText(e.mtime).c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(kTextDim, "%s", e.dir ? "Folder" : lower(fs::u8path(e.name).extension().u8string()).c_str());
        ImGui::TableNextColumn();
        if (!e.dir) ImGui::TextColored(kTextDim, "%s", sizeText(e.size).c_str());
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }
  if (!g_m.path.empty() && g_m.entries.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped(g_m.search.empty() ? "No videos, pictures or folders here." : "Nothing matches the search.");
    ImGui::PopStyleColor();
  }
  TouchScroll();
  ImGui::EndChild();
  if (!enter.empty()) navigate(enter);

  FilePreview(a, previewH);
  ImGui::EndChild();
}

// The selected file: its frames playing through, its details, and IMPORT.
void FilePreview(App& a, float h) {
  float s = S();
  ImGui::BeginChild("##fprev", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  float w = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
  float pw = std::min(w * 0.5f, (h - 70 * s) * 16.f / 9.f), ph = pw * 9.f / 16.f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(0, 0, 0, 255), 4 * s);
  const Entry* sel = nullptr;
  for (const Entry& e : g_m.entries)
    if (e.path == g_m.selFile) sel = &e;
  const FileThumbs::Info* info = nullptr;
  if (sel) {
    const Strip* st = stripOf(a, *sel, &info);
    if (st) {
      int f = int(ImGui::GetTime() * 1.6) % st->frames;
      dl->AddImage((ImTextureID)(intptr_t)st->tex, p, ImVec2(p.x + pw, p.y + ph), ImVec2(float(f) / st->frames, 0),
                   ImVec2(float(f + 1) / st->frames, 1));
    } else if (info && info->failed) {
      dl->AddText(ImVec2(p.x + 8 * s, p.y + 8 * s), IM_COL32(255, 120, 110, 255), "No preview");
    } else {
      dl->AddText(ImVec2(p.x + 8 * s, p.y + 8 * s), IM_COL32(180, 180, 190, 255), ffmpegAvailable() ? "Making a preview..." : "No preview (FFmpeg missing)");
    }
  }
  ImGui::Dummy(ImVec2(pw, ph));
  ImGui::SameLine();
  ImGui::BeginGroup();
  if (sel) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (w - pw - sp));
    ImGui::TextUnformatted(sel->name.c_str());
    ImGui::PopTextWrapPos();
    if (info && info->width > 0) ImGui::TextColored(kTextDim, "%dx%d  %s", info->width, info->height, info->codec.c_str());
    if (info && info->duration > 0) ImGui::TextColored(kTextDim, "%s", timeText(info->duration).c_str());
    ImGui::TextColored(kTextDim, "%s", sizeText(sel->size).c_str());
  } else {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (w - pw - sp));
    ImGui::TextColored(kTextDim, "Tap a file to preview it. Tick its box (or double tap it) to pick several.");
    ImGui::PopTextWrapPos();
  }
  ImGui::EndGroup();
  // import
  MediaLibrary& m = a.st.media;
  int group = std::clamp(m.activeGroup, 0, std::max(0, int(m.groups.size()) - 1));
  std::vector<std::string> files(g_m.picked.begin(), g_m.picked.end());
  if (files.empty() && sel) files.push_back(sel->path);
  char ib[64];
  std::snprintf(ib, sizeof(ib), g_m.picked.empty() ? "IMPORT" : "IMPORT %d", int(files.size()));
  float bw = (w - sp * 2) / 3;
  ImGui::BeginDisabled(files.empty());
  if (Toggle(ib, !files.empty(), ImVec2(bw, 44 * s), kGo)) {
    a.importer.import(m, group, files);
    g_m.picked.clear();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  bool anyFiles = false;
  for (const Entry& e : g_m.entries) anyFiles = anyFiles || !e.dir;
  ImGui::BeginDisabled(!anyFiles);
  if (ImGui::Button("IMPORT ALL", ImVec2(bw, 44 * s))) {
    std::vector<std::string> all;
    for (const Entry& e : g_m.entries)
      if (!e.dir) all.push_back(e.path);
    a.importer.import(m, group, all);
    g_m.picked.clear();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_m.picked.empty());
  if (ImGui::Button("UNTICK", ImVec2(bw, 44 * s))) g_m.picked.clear();
  ImGui::EndDisabled();
  ImGui::TextColored(kTextDim, "Into \"%s\"", m.groups.empty() ? "" : m.groups[size_t(group)].name.c_str());
  ImGui::EndChild();
}

// =====================================================================================================
// CLIPS
void ClipCell(App& a, const std::string& id, const MediaClip* c, float cw, float th) {
  float s = S();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  bool live = a.st.look.mediaClip == id;
  bool sel = a.mediaSelected == id;
  ImGui::BeginGroup();
  ImVec2 p = ImGui::GetCursorScreenPos();
  if (ImGui::InvisibleButton("thumb", ImVec2(cw, th))) {  // the picture: play it
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
      dl->AddText(ImVec2(p.x + 8 * s, p.y + th * 0.35f), IM_COL32(255, 255, 255, 230), pr < 0 ? "WAITING..." : "CONVERTING");
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
  std::string name = c ? c->name : std::string("Live input");
  if (Toggle((name + "##nm").c_str(), sel, ImVec2(cw, 34 * s), kBack)) a.mediaSelected = id;  // the name: preview / edit
  ImGui::EndGroup();
}

void Clips(App& a, float h) {
  float s = S();
  LayoutSettings& L = a.st.layout;
  MediaLibrary& m = a.st.media;
  if (g_m.groupView == -2 || g_m.groupView >= int(m.groups.size())) g_m.groupView = std::clamp(m.activeGroup, 0, int(m.groups.size()) - 1);
  ImGui::BeginChild("##clips", ImVec2(ImGui::GetContentRegionAvail().x, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  float bodyW = ImGui::GetContentRegionAvail().x, bodyH = ImGui::GetContentRegionAvail().y;
  float sp = ImGui::GetStyle().ItemSpacing.x;
  // the groups, like folders
  float treeW = std::max(110 * s, bodyW * L.mTreeW);
  ImGui::BeginChild("##tree", ImVec2(treeW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
  {
    float tw = ImGui::GetContentRegionAvail().x;
    if (Toggle("ALL GROUPS", g_m.groupView == -1, ImVec2(tw, 38 * s))) g_m.groupView = -1;
    for (size_t gi = 0; gi < m.groups.size(); ++gi) {
      ImGui::PushID(int(gi));
      char lbl[96];
      std::snprintf(lbl, sizeof(lbl), "%s  (%d)", m.groups[gi].name.c_str(), int(m.groups[gi].clips.size()));
      if (Toggle(lbl, g_m.groupView == int(gi), ImVec2(tw, 38 * s))) {
        g_m.groupView = int(gi);
        m.activeGroup = int(gi);
      }
      ImGui::PopID();
    }
    if (ImGui::Button("+ GROUP", ImVec2(tw, 38 * s))) OpenKeyboard("mgroupnew", "Group " + std::to_string(m.groups.size() + 1), "New group");
    TouchScroll();
  }
  ImGui::EndChild();
  VSplit("##treesplit", &L.mTreeW, bodyW, bodyH, 0.1f, 0.5f);
  ImGui::BeginChild("##content", ImVec2(ImGui::GetContentRegionAvail().x, bodyH));
  float cwAvail = ImGui::GetContentRegionAvail().x;
  // toolbar: view, the group's name and actions
  if (Toggle("ICONS##c", L.mClipsView == 0, ImVec2(90 * s, 38 * s))) L.mClipsView = 0;
  ImGui::SameLine();
  if (Toggle("DETAILS##c", L.mClipsView == 1, ImVec2(100 * s, 38 * s))) L.mClipsView = 1;
  if (g_m.groupView >= 0 && g_m.groupView < int(m.groups.size())) {
    ImGui::SameLine();
    if (ImGui::Button("RENAME", ImVec2(0, 38 * s))) OpenKeyboard("mgroupren", m.groups[size_t(g_m.groupView)].name, "Group name");
    ImGui::SameLine();
    bool armed = ImGui::GetTime() - g_m.groupDeleteArmed < 3.0;
    ImGui::BeginDisabled(m.groups.size() <= 1);
    if (Toggle(armed ? "TAP AGAIN" : "DELETE GROUP", armed, ImVec2(0, 38 * s), kDanger)) {
      if (armed) {
        MediaGroup& g = m.groups[size_t(g_m.groupView)];
        std::vector<std::string> ids;
        for (const MediaClip& c : g.clips) ids.push_back(c.id);
        for (const std::string& id : ids) {
          if (a.st.look.mediaClip == id) a.st.look.mediaClip.clear();
          a.importer.remove(m, id);
        }
        m.groups.erase(m.groups.begin() + g_m.groupView);
        m.activeGroup = std::max(0, std::min(m.activeGroup, int(m.groups.size()) - 1));
        g_m.groupView = m.activeGroup;
        g_m.groupDeleteArmed = -10;
      } else {
        g_m.groupDeleteArmed = ImGui::GetTime();
      }
    }
    ImGui::EndDisabled();
  }
  std::string txt;
  if (KeyboardResult("mgroupnew", txt) && !txt.empty()) {
    m.groups.push_back(MediaGroup{txt, {}});
    m.activeGroup = int(m.groups.size()) - 1;
    g_m.groupView = m.activeGroup;
  }
  if (KeyboardResult("mgroupren", txt) && !txt.empty() && g_m.groupView >= 0 && g_m.groupView < int(m.groups.size()))
    m.groups[size_t(g_m.groupView)].name = txt;

  ImGui::BeginChild("##cells", ImVec2(0, 0));
  float avail = ImGui::GetContentRegionAvail().x;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (g_m.groupView == -1) {
    // all groups as folders: a collage of their first clips, the name and how many clips
    int cols = std::max(1, int((avail + sp) / (200 * s + sp)));
    float cw = (avail - sp * (cols - 1)) / cols, th = cw * 9.f / 16.f;
    for (size_t gi = 0; gi < m.groups.size(); ++gi) {
      const MediaGroup& g = m.groups[gi];
      ImGui::PushID(int(gi));
      if (gi % cols) ImGui::SameLine();
      ImGui::BeginGroup();
      ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("grp", ImVec2(cw, th + 30 * s))) {
        g_m.groupView = int(gi);
        m.activeGroup = int(gi);
      }
      folderIcon(dl, p, ImVec2(cw, th), s);
      for (int k = 0; k < 4 && k < int(g.clips.size()); ++k) {
        GLuint t = thumbOf(a, g.clips[size_t(k)].id);
        float qw = cw * 0.42f, qh = th * 0.34f;
        ImVec2 q(p.x + cw * 0.06f + (k % 2) * (qw + cw * 0.04f), p.y + th * 0.26f + (k / 2) * (qh + th * 0.04f));
        if (t) dl->AddImage((ImTextureID)(intptr_t)t, q, ImVec2(q.x + qw, q.y + qh));
      }
      char lbl[96];
      std::snprintf(lbl, sizeof(lbl), "%s  (%d)", g.name.c_str(), int(g.clips.size()));
      dl->AddText(ImVec2(p.x + 2 * s, p.y + th + 4 * s), ImGui::GetColorU32(kText), lbl);
      ImGui::EndGroup();
      ImGui::PopID();
    }
  } else if (!m.groups.empty()) {
    MediaGroup& g = m.groups[size_t(g_m.groupView)];
    if (L.mClipsView == 0) {
      int cols = std::max(2, int((avail + sp) / (180 * s + sp)));
      float cw = (avail - sp * (cols - 1)) / cols, th = cw * 9.f / 16.f;
      ImGui::PushID("live");
      ClipCell(a, kMediaLive, nullptr, cw, th);
      ImGui::PopID();
      for (size_t i = 0; i < g.clips.size(); ++i) {
        if ((i + 1) % cols) ImGui::SameLine();
        ImGui::PushID(g.clips[i].id.c_str());
        ClipCell(a, g.clips[i].id, &g.clips[i], cw, th);
        ImGui::PopID();
      }
    } else if (ImGui::BeginTable("##clipdetails", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("pic", ImGuiTableColumnFlags_WidthFixed, 80 * s);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.f);
      ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthStretch, 1.f);
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.1f);
      ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch, 1.f);
      ImGui::TableSetupColumn("play", ImGuiTableColumnFlags_WidthFixed, 80 * s);
      ImGui::TableHeadersRow();
      auto row = [&](const std::string& id, const MediaClip* c) {
        ImGui::TableNextRow(0, 48 * s);
        ImGui::PushID(id.c_str());
        ImGui::TableNextColumn();
        ImVec2 p = ImGui::GetCursorScreenPos();
        GLuint t = c ? thumbOf(a, id) : (a.video.hasPicture() ? a.video.texture() : 0);
        if (t) {
          if (c) ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)t, p, ImVec2(p.x + 76 * s, p.y + 42 * s));
          else ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)t, p, ImVec2(p.x + 76 * s, p.y + 42 * s), ImVec2(0, 1), ImVec2(1, 0));
        }
        if (c && c->label > 0) ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 76 * s, p.y + 5 * s), ImGui::GetColorU32(kLabelCols[c->label]));
        ImGui::Dummy(ImVec2(76 * s, 42 * s));
        ImGui::TableNextColumn();
        std::string name = c ? c->name : std::string("Live input");
        if (ImGui::Selectable(name.c_str(), a.mediaSelected == id, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 42 * s))) {
          a.mediaSelected = id;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && (!c || c->status == CLIP_READY)) goLive(a, id);
        }
        ImGui::TableNextColumn();
        if (c) ImGui::TextColored(kTextDim, "%s", c->status != CLIP_READY ? (c->status == CLIP_FAILED ? "failed" : "converting") : c->still ? "picture" : timeText(c->frames / std::max(1.f, c->fps)).c_str());
        ImGui::TableNextColumn();
        if (c && c->width) ImGui::TextColored(kTextDim, "%dx%d", c->width, c->height);
        ImGui::TableNextColumn();
        if (c && c->status == CLIP_READY) ImGui::TextColored(kTextDim, "%s", c->alpha ? "HAP Alpha" : "HAP Q");
        ImGui::TableNextColumn();
        bool live = a.st.look.mediaClip == id;
        if (Toggle(live ? "ON" : "PLAY", live, ImVec2(-1, 40 * s), kAccent) && (!c || c->status == CLIP_READY)) goLive(a, id);
        ImGui::PopID();
      };
      row(kMediaLive, nullptr);
      for (const MediaClip& c : g.clips) row(c.id, &c);
      ImGui::EndTable();
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
  (void)cwAvail;
  ImGui::EndChild();
}

// =====================================================================================================
// MONITORS (bottom): preview | playing
void Monitors(App& a, float h) {
  float s = S();
  ImGui::BeginChild("##monitors", ImVec2(ImGui::GetContentRegionAvail().x, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  float W = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
  float colW = (W - sp) * 0.5f;
  float aspect = float(a.out.w) / std::max(1, a.out.h);
  float avH = ImGui::GetContentRegionAvail().y - 100 * s;
  float mh = std::max(40 * s, std::min(colW / aspect, avH)), mw = mh * aspect;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const MediaClip* sc = findClip(a.st.media, a.mediaSelected);
  std::string pname = a.mediaSelected == kMediaLive ? "Live input" : (sc ? sc->name : "");
  const MediaClip* lc = findClip(a.st.media, a.st.look.mediaClip);
  std::string lname = a.st.look.mediaClip == kMediaLive ? "Live input" : (lc ? lc->name : "(nothing)");
  for (int k = 0; k < 2; ++k) {
    if (k) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushID(k);
    if (k == 0) ImGui::TextColored(kBack, "PREVIEW  %s", pname.c_str());
    else ImGui::TextColored(kAccent, "PLAYING  %s", lname.c_str());
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.x += (colW - mw) * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + mw, p.y + mh), IM_COL32(0, 0, 0, 255));
    GLuint t = k == 0 ? (a.mediaSelected.empty() ? 0 : a.media.preview()) : a.media.output();
    if (t) dl->AddImage((ImTextureID)(intptr_t)t, p, ImVec2(p.x + mw, p.y + mh), ImVec2(0, 1), ImVec2(1, 0));
    dl->AddRect(p, ImVec2(p.x + mw, p.y + mh), ImGui::GetColorU32(k == 0 ? kBack : kAccent), 3 * s);
    float pos = k == 0 ? a.media.previewPosition() : a.media.livePosition();
    dl->AddRectFilled(ImVec2(p.x, p.y + mh + 2 * s), ImVec2(p.x + mw * pos, p.y + mh + 6 * s), ImGui::GetColorU32(k == 0 ? kBack : kAccent));
    ImGui::Dummy(ImVec2(colW, mh + 8 * s));
    if (k == 0) {
      ImGui::BeginDisabled(a.mediaSelected.empty());
      if (Toggle("PLAY IT", !a.mediaSelected.empty(), ImVec2(colW, 42 * s), kGo) && (!sc || sc->status == CLIP_READY))
        goLive(a, a.mediaSelected);
      ImGui::EndDisabled();
    } else {
      float bw = (colW - sp) * 0.5f;
      if (ImGui::Button("RESTART", ImVec2(bw, 42 * s))) a.media.restart();
      ImGui::SameLine();
      if (Toggle("STOP", false, ImVec2(bw, 42 * s), kDanger)) a.st.look.mediaClip.clear();
    }
    ImGui::PopID();
    ImGui::EndGroup();
  }
  std::string st = a.media.status();
  const Look& l = a.st.look;
  bool shown = (l.effect == FX_VIDEO && l.video.source == VIDSRC_MEDIA) ||
               (l.bg.on && l.bg.effect == FX_VIDEO && l.bg.fx.video.source == VIDSRC_MEDIA);
  if (!st.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
    ImGui::TextWrapped("%s", st.c_str());
    ImGui::PopStyleColor();
  } else if (!shown && !l.mediaClip.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.3f, 1));
    ImGui::TextWrapped("Not on the output: the BACK layer is not on MEDIA (tap a clip's picture to put it there).");
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
}

// =====================================================================================================
// SETTINGS (right)
bool Slider(const char* label, float* v, float lo, float hi, const char* fmt, float def, float mul = 1.f) {
  char vt[32];
  std::snprintf(vt, sizeof(vt), fmt, *v * mul);
  return TouchSliderDef(label, v, lo, hi, vt, &def);
}

void Settings(App& a, float h) {
  float s = S();
  MediaLibrary& m = a.st.media;
  ImGui::BeginChild("##clipset", ImVec2(ImGui::GetContentRegionAvail().x, h), ImGuiChildFlags_AlwaysUseWindowPadding);
  float w = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
  bool liveSel = a.mediaSelected == kMediaLive;
  MediaClip* c = findClip(m, a.mediaSelected);
  const ClipSettings D;  // the defaults (double tap a slider to go back to its default)
  if (!c && !liveSel) {
    ImGui::TextColored(kAccent, "CLIP SETTINGS");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped("Tap a clip's name to preview it and change its settings here.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8 * s));
    ImGui::TextColored(kAccent, "WHEN THE CLIP CHANGES");
    static const char* tr[] = {"CUT", "CROSSFADE", "THROUGH BLACK"};
    Choice("mtrans", tr, MTRANS_COUNT, &m.transition, 1, 42 * s);
    if (m.transition != MTRANS_CUT) Slider("Time", &m.transTime, 0.f, 5.f, "%.1f s", 1.f);
    TouchScroll();
    ImGui::EndChild();
    return;
  }
  ClipSettings& k = liveSel ? m.liveSet : c->set;
  ImGui::PushFont(a.fontBig);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(liveSel ? "Live input" : c->name.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  if (c) {
    char info[160];
    if (c->status == CLIP_READY)
      std::snprintf(info, sizeof(info), "%dx%d   %s   %s", c->width, c->height,
                    c->still ? "picture" : (timeText(c->frames / std::max(1.f, c->fps)) + "  " + std::to_string(int(std::lround(c->fps))) + " fps").c_str(),
                    c->alpha ? "HAP Alpha" : "HAP Q");
    else std::snprintf(info, sizeof(info), "%s", c->status == CLIP_CONVERTING ? "converting..." : "failed");
    ImGui::TextColored(kTextDim, "%s", info);
    if (c->status == CLIP_FAILED) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.4f, 1));
      ImGui::TextWrapped("%s", c->error.c_str());
      ImGui::PopStyleColor();
      if (ImGui::Button("TRY AGAIN", ImVec2(w, 40 * s))) a.importer.retry(m, c->id);
    }
    // label colours
    float lw = (w - sp * (kNumClipLabels - 1)) / kNumClipLabels;
    for (int i = 0; i < kNumClipLabels; ++i) {
      if (i) ImGui::SameLine();
      ImGui::PushID(i);
      ImVec2 q = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("lbl", ImVec2(lw, 28 * s))) c->label = i;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(q, ImVec2(q.x + lw, q.y + 28 * s), ImGui::GetColorU32(kLabelCols[i]), 4 * s);
      if (c->label == i) dl->AddRect(q, ImVec2(q.x + lw, q.y + 28 * s), IM_COL32(255, 255, 255, 255), 4 * s, 0, 3 * s);
      ImGui::PopID();
    }
    // rename / order / move / delete
    int gi = -1, ci = -1;
    for (size_t g = 0; g < m.groups.size(); ++g)
      for (size_t i = 0; i < m.groups[g].clips.size(); ++i)
        if (&m.groups[g].clips[i] == c) gi = int(g), ci = int(i);
    float b4 = (w - sp * 3) / 4;
    if (ImGui::Button("RENAME", ImVec2(b4, 38 * s))) OpenKeyboard("mcliprename", c->name, "Clip name");
    ImGui::SameLine();
    if (ImGui::Button("<", ImVec2(b4, 38 * s)) && gi >= 0 && ci > 0)
      std::swap(m.groups[size_t(gi)].clips[size_t(ci)], m.groups[size_t(gi)].clips[size_t(ci - 1)]);
    ImGui::SameLine();
    if (ImGui::Button(">", ImVec2(b4, 38 * s)) && gi >= 0 && ci + 1 < int(m.groups[size_t(gi)].clips.size()))
      std::swap(m.groups[size_t(gi)].clips[size_t(ci)], m.groups[size_t(gi)].clips[size_t(ci + 1)]);
    ImGui::SameLine();
    if (ImGui::Button("MOVE", ImVec2(b4, 38 * s))) ImGui::OpenPopup("##movegroup");
    c = findClip(m, a.mediaSelected);
    std::string txt;
    if (c && KeyboardResult("mcliprename", txt) && !txt.empty()) c->name = txt;
    if (ImGui::BeginPopup("##movegroup")) {
      ImGui::TextColored(kTextDim, "Move the clip to:");
      for (size_t g = 0; g < m.groups.size(); ++g) {
        ImGui::PushID(int(g));
        int cg = -1, cc = -1;
        for (size_t g2 = 0; g2 < m.groups.size(); ++g2)
          for (size_t i = 0; i < m.groups[g2].clips.size(); ++i)
            if (m.groups[g2].clips[i].id == a.mediaSelected) cg = int(g2), cc = int(i);
        if (int(g) != cg && ImGui::Button(m.groups[g].name.c_str(), ImVec2(260 * s, 44 * s)) && cg >= 0) {
          MediaClip moved = m.groups[size_t(cg)].clips[size_t(cc)];
          m.groups[size_t(cg)].clips.erase(m.groups[size_t(cg)].clips.begin() + cc);
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
    bool armed = g_m.deleteId == c->id && ImGui::GetTime() - g_m.deleteArmed < 3.0;
    if (Toggle(armed ? "TAP AGAIN TO DELETE" : "DELETE CLIP", armed, ImVec2(w, 36 * s), kDanger)) {
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
  static const char* tabs[] = {"PLAY", "TRANSFORM", "COLOUR"};
  Choice("msettab", tabs, 3, &g_m.tab, 3, 40 * s);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("Double tap a slider: back to its default.");
  ImGui::PopStyleColor();
  ImGui::BeginChild("##setsliders", ImVec2(0, 0));
  if (g_m.tab == 0) {
    static const char* plays[] = {"LOOP", "ONCE + HOLD", "ONCE + BLACK", "BOUNCE"};
    Choice("mplay", plays, PLAY_COUNT, &k.play, 2, 40 * s);
    float dur = c ? c->frames / std::max(1.f, c->fps) : 0.f;
    char vt[48];
    std::snprintf(vt, sizeof(vt), "%s", timeText(k.in * dur).c_str());
    if (TouchSliderDef("In", &k.in, 0.f, 1.f, vt, &D.in)) k.out = std::max(k.out, k.in);
    std::snprintf(vt, sizeof(vt), "%s", timeText(k.out * dur).c_str());
    if (TouchSliderDef("Out", &k.out, 0.f, 1.f, vt, &D.out)) k.in = std::min(k.in, k.out);
    ImGui::BeginDisabled(k.beatSync);
    Slider("Speed", &k.speed, 0.f, 4.f, "%.0f %%", D.speed, 100.f);
    ImGui::EndDisabled();
    float hw = (ImGui::GetContentRegionAvail().x - sp) / 2;
    if (Toggle("REVERSE", k.reverse, ImVec2(hw, 40 * s))) k.reverse = !k.reverse;
    ImGui::SameLine();
    if (Toggle("BEAT SYNC", k.beatSync, ImVec2(hw, 40 * s))) k.beatSync = !k.beatSync;
    if (k.beatSync) {
      static const char* beats[] = {"1", "2", "4", "8", "16", "32", "64"};
      int bi = 4;
      for (int i = 0; i < 7; ++i)
        if (kBeatChoices[i] == k.beats) bi = i;
      if (Choice("mbeats", beats, 7, &bi, 7, 38 * s)) k.beats = kBeatChoices[bi];
      ImGui::TextColored(kTextDim, "The clip (In to Out) lasts %d beats.", k.beats);
    }
    ImGui::TextColored(kTextDim, "Transition into this clip");
    static const char* tr[] = {"LAYER", "CUT", "XFADE", "BLACK"};
    int t = k.transition + 1;
    if (Choice("mctrans", tr, 4, &t, 4, 38 * s)) k.transition = t - 1;
    if (k.transition > MTRANS_CUT) Slider("Time", &k.transTime, 0.f, 5.f, "%.1f s", D.transTime);
    ImGui::TextColored(kTextDim, "LAYER = the media layer's own:");
    static const char* tr2[] = {"CUT", "XFADE", "BLACK"};
    Choice("mtrans2", tr2, MTRANS_COUNT, &m.transition, 3, 38 * s);
    if (m.transition != MTRANS_CUT) Slider("Layer time", &m.transTime, 0.f, 5.f, "%.1f s", 1.f);
  } else if (g_m.tab == 1) {
    Slider("Scale", &k.scale, 0.05f, 4.f, "%.0f %%", D.scale, 100.f);
    Slider("Scale X", &k.scaleX, 0.05f, 4.f, "%.0f %%", D.scaleX, 100.f);
    Slider("Scale Y", &k.scaleY, 0.05f, 4.f, "%.0f %%", D.scaleY, 100.f);
    Slider("Position X", &k.posX, -1.f, 1.f, "%+.0f %%", D.posX, 100.f);
    Slider("Position Y", &k.posY, -1.f, 1.f, "%+.0f %%", D.posY, 100.f);
    Slider("Rotation", &k.rotation, -180.f, 180.f, "%.0f deg", D.rotation);
    float hw = (ImGui::GetContentRegionAvail().x - sp) / 2;
    if (Toggle("FLIP H", k.flipH, ImVec2(hw, 40 * s))) k.flipH = !k.flipH;
    ImGui::SameLine();
    if (Toggle("FLIP V", k.flipV, ImVec2(hw, 40 * s))) k.flipV = !k.flipV;
    Slider("Crop left", &k.cropL, 0.f, 0.9f, "%.0f %%", D.cropL, 100.f);
    Slider("Crop right", &k.cropR, 0.f, 0.9f, "%.0f %%", D.cropR, 100.f);
    Slider("Crop top", &k.cropT, 0.f, 0.9f, "%.0f %%", D.cropT, 100.f);
    Slider("Crop bottom", &k.cropB, 0.f, 0.9f, "%.0f %%", D.cropB, 100.f);
    if (ImGui::Button("RESET ALL TRANSFORM", ImVec2(-1, 40 * s))) {
      k.scale = D.scale, k.scaleX = D.scaleX, k.scaleY = D.scaleY, k.posX = D.posX, k.posY = D.posY;
      k.rotation = D.rotation, k.flipH = D.flipH, k.flipV = D.flipV;
      k.cropL = k.cropR = k.cropT = k.cropB = 0.f;
    }
  } else {
    Slider("Gamma", &k.gamma, 0.2f, 3.f, "%.2f", D.gamma);
    Slider("Hue", &k.hue, -180.f, 180.f, "%+.0f deg", D.hue);
    Slider("Saturation", &k.saturation, 0.f, 2.f, "%.0f %%", D.saturation, 100.f);
    Slider("Blur", &k.blur, 0.f, 1.f, "%.0f %%", D.blur, 100.f);
    if (ImGui::Button("RESET ALL COLOUR", ImVec2(-1, 40 * s))) {
      k.gamma = D.gamma, k.hue = D.hue, k.saturation = D.saturation, k.blur = D.blur;
    }
  }
  TouchScroll();
  ImGui::EndChild();
  ImGui::EndChild();
}

}  // namespace

// self-test: open the file browser in a folder with a file selected
void MediaBrowseTo(const std::string& folder, const std::string& file) {
  navigate(folder);
  g_m.selFile = file;
}

void MediaWindowUI(App& a) {
  float s = S();
  ImGuiIO& io = ImGui::GetIO();
  LayoutSettings& L = a.st.layout;
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##media", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
  // top bar: CLOSE and < TOUCHFX on the left (always on screen), then the status, then layout / fullscreen
  float sp = ImGui::GetStyle().ItemSpacing.x;
  if (Toggle("CLOSE", true, ImVec2(110 * s, 46 * s), kDanger)) a.wantCloseMediaWin = true;
  ImGui::SameLine();
  if (ImGui::Button("< TOUCHFX", ImVec2(140 * s, 46 * s))) SDL_RaiseWindow(a.window);
  ImGui::SameLine();
  ImGui::PushFont(a.fontBig);
  ImGui::TextColored(kBack, "MEDIA");
  ImGui::PopFont();
  ImGui::SameLine(0, 16 * s);
  int pending = a.importer.pending();
  ImGui::BeginGroup();
  if (pending > 0) ImGui::TextColored(kAccent, "Converting %d clip%s...", pending, pending > 1 ? "s" : "");
  else if (!a.showLoaded) ImGui::TextColored(kTextDim, "Open a show to use the media server.");
  if (!ffmpegAvailable()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "FFmpeg missing: clips cannot be imported.");
  ImGui::EndGroup();
  float right = 2 * (170 * s) + sp;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - right - ImGui::GetStyle().WindowPadding.x));
  if (ImGui::Button("RESET LAYOUT", ImVec2(170 * s, 46 * s))) {
    LayoutSettings d;
    L.mFilesW = d.mFilesW, L.mSetW = d.mSetW, L.mMonH = d.mMonH, L.mNavW = d.mNavW, L.mTreeW = d.mTreeW;
  }
  ImGui::SameLine();
  bool full = L.mediaFull;
  if (Toggle("FULLSCREEN", full, ImVec2(170 * s, 46 * s))) {
    L.mediaFull = !full;
    platformSetFullscreen(a.mediaWin, L.mediaFull);
  }
  if (!a.showLoaded) {
    ImGui::End();
    KeyboardFrame(a);
    return;
  }
  // FILES | splitter | (CLIPS / splitter / MONITORS) | splitter | SETTINGS
  float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
  float bar = 12 * s;
  float filesW = std::max(260 * s, W * L.mFilesW), setW = std::max(260 * s, W * L.mSetW);
  float midW = std::max(200 * s, W - filesW - setW - bar * 2);
  ImGui::BeginChild("##colfiles", ImVec2(filesW, H));
  Files(a, H);
  ImGui::EndChild();
  VSplit("##split1", &L.mFilesW, W, H, 0.12f, 0.5f);
  ImGui::BeginChild("##colmid", ImVec2(midW, H));
  {
    float monH = std::max(140 * s, H * L.mMonH);
    float clipsH = std::max(120 * s, H - monH - bar);
    Clips(a, clipsH);
    HSplit("##split3", &L.mMonH, H, midW, 0.15f, 0.7f);
    Monitors(a, ImGui::GetContentRegionAvail().y);
  }
  ImGui::EndChild();
  VSplit("##split2", &L.mSetW, W, H, 0.12f, 0.45f, true);
  ImGui::BeginChild("##colset", ImVec2(ImGui::GetContentRegionAvail().x, H));
  Settings(a, H);
  ImGui::EndChild();
  ImGui::End();
  KeyboardFrame(a);
}

}  // namespace ui
