#include "cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "app.hpp"
#include "log.hpp"

namespace {

CueList* activeList(App& a) {
  if (a.st.cueLists.empty()) return nullptr;
  a.st.activeList = std::clamp(a.st.activeList, 0, int(a.st.cueLists.size()) - 1);
  return &a.st.cueLists[a.st.activeList];
}

// First song after the active one that has cues (-1 = none: the show ends here).
int nextSongWithCues(App& a) {
  for (int i = a.st.activeList + 1; i < int(a.st.cueLists.size()); ++i)
    if (!a.st.cueLists[i].cues.empty()) return i;
  return -1;
}

}  // namespace

std::string cueNumText(float number) {
  char b[32];
  std::snprintf(b, sizeof(b), "%.3f", double(number));
  std::string t = b;
  while (!t.empty() && t.back() == '0') t.pop_back();
  if (!t.empty() && t.back() == '.') t.pop_back();
  return t;
}

int cueFindNumber(const CueList& cl, float number) {
  for (int i = 0; i < int(cl.cues.size()); ++i)
    if (std::fabs(cl.cues[i].number - number) < 0.0005f) return i;
  return -1;
}

float cueNumberAfter(const CueList& cl, int index) {
  double prev = index >= 0 && index < int(cl.cues.size()) ? cl.cues[index].number : 0.0;
  if (index + 1 >= int(cl.cues.size())) return float(std::floor(prev + 1e-4) + 1.0);
  double next = cl.cues[index + 1].number;
  for (double step : {1.0, 0.5, 0.1, 0.01, 0.001}) {  // 3 -> 4, else 3.5, else 3.6, 3.61...
    double x = std::floor(prev / step + 1e-4) * step + step;
    x = std::round(x * 1000.0) / 1000.0;
    if (x > prev + 1e-4 && x < next - 1e-4) return float(x);
  }
  return -1;  // no room left: renumber first
}

void cueRenumber(CueList& cl) {
  for (int i = 0; i < int(cl.cues.size()); ++i) cl.cues[i].number = float(i + 1);
}

void cueSort(App& a, int* track) {
  CueList* cl = activeList(a);
  if (!cl) return;
  int n = int(cl->cues.size());
  std::vector<int> order(n), pos(n);
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(),
                   [&](int x, int y) { return cl->cues[x].number < cl->cues[y].number; });
  std::vector<Cue> sorted;
  sorted.reserve(n);
  for (int k = 0; k < n; ++k) {
    sorted.push_back(std::move(cl->cues[order[k]]));
    pos[order[k]] = k;
  }
  cl->cues = std::move(sorted);
  auto remap = [&](int& i) {
    if (i >= 0 && i < n) i = pos[i];
  };
  remap(a.cue.current);
  remap(a.cue.next);
  if (track) remap(*track);
}

// ---- undo ----
namespace {
UndoStep capture(App& a, const std::string& label) {
  UndoStep u;
  u.label = label;
  u.lists = a.st.cueLists;
  u.activeList = a.st.activeList;
  u.scenes = a.st.scenes;
  u.current = a.cue.current;
  u.next = a.cue.next;
  return u;
}
}  // namespace

void undoPush(App& a, const std::string& label) {
  a.undo.undo.push_back(capture(a, label));
  if (a.undo.undo.size() > 50) a.undo.undo.erase(a.undo.undo.begin());
  a.undo.redo.clear();
}

bool undoApply(App& a, bool redo) {
  undoEnd(a);  // an edit sheet still open: its changes so far become a step first
  std::vector<UndoStep>& from = redo ? a.undo.redo : a.undo.undo;
  std::vector<UndoStep>& to = redo ? a.undo.undo : a.undo.redo;
  if (from.empty()) return false;
  UndoStep step = std::move(from.back());
  from.pop_back();
  to.push_back(capture(a, step.label));
  a.st.cueLists = std::move(step.lists);
  if (a.st.cueLists.empty()) a.st.cueLists.push_back(CueList{"Song 1", END_STOP, {}});
  a.st.activeList = std::clamp(step.activeList, 0, int(a.st.cueLists.size()) - 1);
  a.st.scenes = step.scenes;
  CuePlayer& p = a.cue;
  int n = int(a.st.cueLists[a.st.activeList].cues.size());
  p.current = step.current < n ? step.current : -1;
  p.next = step.next < n ? step.next : -1;
  p.fading = false;
  if (p.current >= 0) {
    p.snapshot = lookFingerprint(a.st.cueLists[a.st.activeList].cues[p.current].look);
    p.modified = lookFingerprint(a.st.look) != p.snapshot;
  }
  logLine("%s: %s", redo ? "Redo" : "Undo", step.label.c_str());
  return true;
}

std::string undoLabel(App& a, bool redo) {
  const std::vector<UndoStep>& v = redo ? a.undo.redo : a.undo.undo;
  return v.empty() ? std::string() : v.back().label;
}

void undoClear(App& a) {
  a.undo.undo.clear();
  a.undo.redo.clear();
  a.undo.hasPending = false;
}

void undoBegin(App& a, const std::string& label) {
  a.undo.pending = capture(a, label);
  a.undo.hasPending = true;
}

void undoEnd(App& a) {
  if (!a.undo.hasPending) return;
  a.undo.hasPending = false;
  const UndoStep& u = a.undo.pending;
  auto sameCue = [](const Cue& x, const Cue& y) {
    return x.number == y.number && x.name == y.name && x.fade == y.fade && x.follow == y.follow &&
           x.followValue == y.followValue && lookToText(x.look) == lookToText(y.look);
  };
  bool same = u.lists.size() == a.st.cueLists.size();
  for (size_t i = 0; same && i < u.lists.size(); ++i) {
    const CueList &x = u.lists[i], &y = a.st.cueLists[i];
    same = x.name == y.name && x.endAction == y.endAction && x.cues.size() == y.cues.size();
    for (size_t k = 0; same && k < x.cues.size(); ++k) same = sameCue(x.cues[k], y.cues[k]);
  }
  if (same) return;
  a.undo.undo.push_back(std::move(a.undo.pending));
  if (a.undo.undo.size() > 50) a.undo.undo.erase(a.undo.undo.begin());
  a.undo.redo.clear();
}

const char* effectName(int e) {
  static const char* n[FX_COUNT] = {"FLUID",   "PARTICLES", "KALEIDO",   "LASERS",    "STROBE",   "SPARKLES",
                                    "RIPPLES", "RIBBONS",   "SHAPES",    "TUNNEL",    "VIDEO",     "PLEXUS",
                                    "FIREWORKS", "STARFIELD", "SPECTRUM", "LED GRID"};
  return (e >= 0 && e < FX_COUNT) ? n[e] : "?";
}

void cueFire(App& a, int index) {
  CueList* cl = activeList(a);
  if (!cl || index < 0 || index >= int(cl->cues.size())) return;
  const Cue& cue = cl->cues[index];
  CuePlayer& p = a.cue;
  float bpm = a.st.look.bpm;  // the tempo is never recalled
  Look target = cue.look;
  target.bpm = bpm;
  if (target.effect != a.st.look.effect || cue.fade < 0.02f) {
    // different effect: switch, the old one fades out over the cue's fade time
    a.xfadeOverride = cue.fade;
    a.st.look = target;
    p.fading = false;
  } else {
    // same effect: every value glides to the new look
    p.fromJson = lookToText(a.st.look);
    p.toJson = lookToText(target);
    p.fadeT = 0;
    p.fadeDur = cue.fade;
    p.fading = true;
  }
  a.bgFadeOverride = cue.fade;  // the background layer fades in / out / changes over the cue's fade too
  p.current = index;
  p.next = index + 1 < int(cl->cues.size()) ? index + 1 : -1;
  p.goTime = a.time;
  p.goBeat = a.tempo.beat;
  p.snapshot = lookFingerprint(target);
  p.modified = false;
  a.lastScene = -1;
  logLine("Cue %s \"%s\" (%s, %s)", cueNumText(cue.number).c_str(), cue.name.c_str(), cl->name.c_str(), effectName(target.effect));
}

void cueGo(App& a) {
  CueList* cl = activeList(a);
  if (!cl) return;
  if (a.cue.next >= 0) {
    cueFire(a, a.cue.next);
    return;
  }
  if (cl->endAction == END_LOOP && !cl->cues.empty()) {
    cueFire(a, 0);
  } else if (cl->endAction == END_NEXT_SONG) {
    int song = nextSongWithCues(a);
    if (song < 0) return;
    cueSelectList(a, song);
    cueFire(a, 0);
  }
}

bool cueCanGo(App& a) {
  CueList* cl = activeList(a);
  if (!cl) return false;
  if (a.cue.next >= 0) return true;
  if (cl->endAction == END_LOOP) return !cl->cues.empty();
  if (cl->endAction == END_NEXT_SONG) return nextSongWithCues(a) >= 0;
  return false;
}

std::string cueGoText(App& a, bool full) {
  CueList* cl = activeList(a);
  if (!cl) return "";
  auto cueText = [&](const CueList& l, int i) {
    return cueNumText(l.cues[i].number) + "  " + l.cues[i].name;
  };
  if (a.cue.next >= 0 && a.cue.next < int(cl->cues.size())) return cueText(*cl, a.cue.next);
  if (cl->endAction == END_LOOP && !cl->cues.empty()) return "LOOP > " + cueText(*cl, 0);
  if (cl->endAction == END_NEXT_SONG) {
    int song = nextSongWithCues(a);
    if (song >= 0) return "NEXT SONG: " + a.st.cueLists[song].name;
    return full ? "END OF SHOW (last song)" : "END OF SHOW";
  }
  return "END OF SONG";
}

void cueCommand(App& a, CueCommand cmd) {
  CueList* cl = activeList(a);
  if (!cl) return;
  CuePlayer& p = a.cue;
  int n = int(cl->cues.size());
  switch (cmd) {
    case CUE_GO: cueGo(a); break;
    case CUE_BACK: cueBack(a); break;
    case CUE_SELECT_PREV:
      if (n == 0) break;
      p.next = p.next < 0 ? n - 1 : std::max(0, p.next - 1);
      p.scrollToNext = true;
      break;
    case CUE_SELECT_NEXT:
      if (n == 0) break;
      p.next = p.next < 0 ? n - 1 : std::min(n - 1, p.next + 1);
      p.scrollToNext = true;
      break;
    case CUE_SONG_PREV: cueSelectList(a, a.st.activeList - 1); break;
    case CUE_SONG_NEXT: cueSelectList(a, a.st.activeList + 1); break;
  }
}

void cueBack(App& a) {
  if (a.cue.current > 0) cueFire(a, a.cue.current - 1);
}

void cueSelectList(App& a, int list) {
  if (list < 0 || list >= int(a.st.cueLists.size())) return;
  a.st.activeList = list;
  a.cue.current = -1;
  a.cue.next = a.st.cueLists[list].cues.empty() ? -1 : 0;
  a.cue.fading = false;
  a.cue.modified = false;
}

void cueRecord(App& a) {
  CueList* cl = activeList(a);
  if (!cl) return;
  Cue c;
  c.look = a.st.look;
  c.name = std::string(effectName(c.look.effect));
  c.name = c.name.substr(0, 1) + [&] {  // "Lasers" rather than "LASERS"
    std::string r = c.name.substr(1);
    for (char& ch : r) ch = char(std::tolower((unsigned char)ch));
    return r;
  }();
  c.fade = 1.0f;
  int at = a.cue.current >= 0 ? a.cue.current + 1 : int(cl->cues.size());
  c.number = cueNumberAfter(*cl, at - 1);
  if (c.number < 0) {  // no free number between the two cues: renumber the song first
    cueRenumber(*cl);
    c.number = cueNumberAfter(*cl, at - 1);
  }
  cl->cues.insert(cl->cues.begin() + at, c);
  a.cue.current = at;
  a.cue.next = at + 1 < int(cl->cues.size()) ? at + 1 : -1;
  a.cue.snapshot = lookFingerprint(c.look);
  a.cue.modified = false;
}

void cueUpdate(App& a) {
  CueList* cl = activeList(a);
  if (!cl || a.cue.current < 0 || a.cue.current >= int(cl->cues.size())) return;
  cl->cues[a.cue.current].look = a.st.look;
  a.cue.snapshot = lookFingerprint(a.st.look);
  a.cue.modified = false;
}

void cueTick(App& a, float dt) {
  CuePlayer& p = a.cue;
  CueList* cl = activeList(a);
  if (!cl) return;
  int n = int(cl->cues.size());
  if (p.current >= n) p.current = n - 1;
  if (p.next >= n) p.next = -1;
  if (p.next < 0 && p.current + 1 < n && p.current >= -1) p.next = p.current + 1;

  if (p.fading) {
    p.fadeT += dt;
    float t = p.fadeDur > 0 ? std::min(1.f, p.fadeT / p.fadeDur) : 1.f;
    float e = t * t * (3.f - 2.f * t);  // smooth start and end
    float bpm = a.st.look.bpm;
    if (!lookFromText(t >= 1.f ? p.toJson : lookBlendText(p.fromJson, p.toJson, e), a.st.look)) p.fading = false;
    a.st.look.bpm = bpm;
    if (t >= 1.f) p.fading = false;
  }

  // follow: auto-GO after N seconds or N bars
  if (p.current >= 0 && p.current < n && cueCanGo(a)) {
    const Cue& c = cl->cues[p.current];
    bool fire = (c.follow == FOLLOW_SECONDS && a.time - p.goTime >= c.followValue) ||
                (c.follow == FOLLOW_BARS && a.tempo.beat - p.goBeat >= c.followValue * 4.0);
    if (fire) cueGo(a);
  }

  // live tweaks since the cue fired?
  p.modCheck += dt;
  if (p.modCheck > 0.5 && p.current >= 0 && !p.fading) {
    p.modCheck = 0;
    p.modified = lookFingerprint(a.st.look) != p.snapshot;
  }
}
