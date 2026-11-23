#pragma once
#include <array>
#include <string>
#include <vector>

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
  bool scrollToNext = false;      // the CUES list scrolls to the NEXT cue (after arrow keys)
};

// Undo history for cue / song / scene edits (Ctrl+Z, Ctrl+Y, UNDO button). Whole snapshots: simple and safe.
struct UndoStep {
  std::string label;
  std::vector<CueList> lists;
  int activeList = 0;
  std::array<Scene, kNumScenes> scenes;
  int current = -1, next = 0;
};
struct UndoHistory {
  std::vector<UndoStep> undo, redo;
  bool hasPending = false;  // an edit sheet is open: its changes become one undo step when it closes
  UndoStep pending;
};

// Cue commands: the keyboard uses these, DMX / OSC / MIDI will use the same ones later.
enum CueCommand { CUE_GO, CUE_BACK, CUE_SELECT_PREV, CUE_SELECT_NEXT, CUE_SONG_PREV, CUE_SONG_NEXT };
void cueCommand(App& a, CueCommand cmd);

void cueGo(App& a);
void cueBack(App& a);
void cueFire(App& a, int index);  // fire a specific cue of the active list
void cueSelectList(App& a, int list);
void cueRecord(App& a);  // store the live look as a new cue after the current one
void cueUpdate(App& a);  // overwrite the current cue with the live look
void cueTick(App& a, float dt);
bool cueCanGo(App& a);                     // GO does something (next cue, loop or next song)
std::string cueGoText(App& a, bool full);  // what GO will do, e.g. "3  Chorus" / "LOOP > 1  Intro"
std::string cueNumText(float number);      // 2 -> "2", 2.5 -> "2.5"
float cueNumberAfter(const CueList& cl, int index);  // free number for a cue inserted after index (-1 = at the start)
void cueRenumber(CueList& cl);             // 1, 2, 3...
void cueSort(App& a, int* track = nullptr);                    // keep the active list sorted by number (keeps the player on its cues)
int cueFindNumber(const CueList& cl, float number);

void undoPush(App& a, const std::string& label);  // call BEFORE changing cues, songs or scenes
bool undoApply(App& a, bool redo);
std::string undoLabel(App& a, bool redo);         // "" = nothing to undo
void undoClear(App& a);
void undoBegin(App& a, const std::string& label);  // start a grouped edit (edit sheet)
void undoEnd(App& a);                              // keep it as one step if something changed
const char* effectName(int effect);
