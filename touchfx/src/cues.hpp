#pragma once
#include <string>

#include "state.hpp"

struct App;

// Cue playback for the active cue list (song).
struct CuePlayer {
  int current = -1;  // cue on stage (-1 = none yet)
  int next = 0;      // cue the next GO fires (-1 = end of the list)
  bool fading = false;
  float fadeT = 0, fadeDur = 0;
  std::string fromJson, toJson;  // looks being blended (same effect: every value glides)
  double goTime = 0, goBeat = 0;  // for follow timing
  bool modified = false;          // live tweaks since the cue fired
  std::string snapshot;           // the fired look, to detect tweaks
  double modCheck = 0;
};

void cueGo(App& a);
void cueBack(App& a);
void cueFire(App& a, int index);  // fire a specific cue of the active list
void cueSelectList(App& a, int list);
void cueRecord(App& a);  // store the live look as a new cue after the current one
void cueUpdate(App& a);  // overwrite the current cue with the live look
void cueTick(App& a, float dt);
const char* effectName(int effect);
