#include "cues.hpp"

#include <algorithm>
#include <cmath>

#include "app.hpp"
#include "log.hpp"

namespace {

CueList* activeList(App& a) {
  if (a.st.cueLists.empty()) return nullptr;
  a.st.activeList = std::clamp(a.st.activeList, 0, int(a.st.cueLists.size()) - 1);
  return &a.st.cueLists[a.st.activeList];
}

}  // namespace

const char* effectName(int e) {
  static const char* n[FX_COUNT] = {"FLUID",   "PARTICLES", "KALEIDO",   "LASERS",    "STROBE",   "SPARKLES",
                                    "RIPPLES", "RIBBONS",   "SHAPES",    "TUNNEL",    "LIGHTNING", "PLEXUS",
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
  p.current = index;
  p.next = index + 1 < int(cl->cues.size()) ? index + 1 : -1;
  p.goTime = a.time;
  p.goBeat = a.tempo.beat;
  p.snapshot = lookFingerprint(target);
  p.modified = false;
  a.lastScene = -1;
  logLine("Cue %d \"%s\" (%s, %s)", index + 1, cue.name.c_str(), cl->name.c_str(), effectName(target.effect));
}

void cueGo(App& a) {
  if (a.cue.next >= 0) cueFire(a, a.cue.next);
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
  if (p.current >= 0 && p.current < n && p.next >= 0) {
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
