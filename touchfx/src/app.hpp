#pragma once
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "effects.hpp"
#include "ndi.hpp"
#include "state.hpp"

struct ImFont;

struct TouchSlot {
  bool active = false;
  int64_t id = 0;
  Vec2 pos;      // output UV, y up
  Vec2 lastPos;  // previous frame
  Vec2 vel;      // aspect space per second (smoothed)
};

struct Tempo {
  double beat = 0;
  std::vector<double> taps;
  void advance(double dt, float bpm) { beat += dt * bpm / 60.0; }
  void tap(double now, float& bpm) {
    if (!taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back(now);
    if (taps.size() > 8) taps.erase(taps.begin());
    if (taps.size() >= 2) {
      double avg = (taps.back() - taps.front()) / double(taps.size() - 1);
      if (avg > 0.0) bpm = float(std::min(300.0, std::max(20.0, 60.0 / avg)));
    }
    beat = std::floor(beat + 0.5);  // tapping also re-aligns the downbeat
  }
};

struct App {
  AppState st;
  std::string settingsPath;
  std::string lastSaved;
  double saveTimer = 0;

  SDL_Window* window = nullptr;
  int winW = 0, winH = 0;  // window size in pixels
  float dpiScale = 1;

  Tempo tempo;
  std::unique_ptr<Effect> fx[FX_COUNT];
  int curFx = -1;
  Target out;  // the rendered output (also shown in the preview)

  // NDI readback
  NdiSender ndi;
  Target flip;
  GLuint pbo[2] = {0, 0};
  bool pboValid[2] = {false, false};
  int pboIdx = 0;
  double ndiAccum = 0, ndiRetry = 0;

  TouchSlot touches[2];
  std::vector<Emitter> live, held;
  bool wasTouching = false, fading = false;
  float fadeElapsed = 0, gain = 1;
  bool blackout = false;

  // canvas rectangle in window pixels
  float cx = 0, cy = 0, cw = 1, ch = 1;
  float panelW = 500;

  // UI
  float fontScale = 0;
  ImFont* fontMain = nullptr;
  ImFont* fontBig = nullptr;
  ImFont* fontSmall = nullptr;
  bool fontsBuilt = false;
  bool storeArmed = false, deleteArmed = false, customRes = false;
  int lastScene = -1;
  float uiScaleEdit = 1;
  char ndiNameEdit[64] = {};
  float fps = 60;
  double time = 0;
  bool wantClear = false;
  bool selftest = false;

  void clearEffect();
};

void drawUI(App& a);
void rebuildFonts(App& a, float scale);
