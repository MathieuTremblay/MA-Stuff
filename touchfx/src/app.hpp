#pragma once
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "audio.hpp"
#include "cues.hpp"
#include "dmx.hpp"
#include "netif.hpp"
#include "sysstats.hpp"
#include "effects.hpp"
#include "ndi.hpp"
#include "post.hpp"
#include "spout_out.hpp"
#include "state.hpp"
#include "video_in.hpp"

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

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

struct App {
  AppState st;
  std::string settingsPath;
  std::string lastSaved;
  double saveTimer = 0;

  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  int winW = 0, winH = 0;  // window size in pixels
  float dpiScale = 1;

  Tempo tempo;
  std::unique_ptr<Effect> fx[FX_COUNT];
  int curFx = -1;
  Target scene;  // HDR render of the current effect
  Target scenePrev;  // the previous effect while it fades out after a switch
  int prevFx = -1;
  float xfadeOverride = -1;  // crossfade time for the current switch (cue fade), -1 = settings value
  float xfade = 0, prevGain = 1;
  std::vector<Emitter> prevHeld;
  SpoutOut spout;
  Target out;    // final 8-bit output (preview, NDI, external display)
  Post post;

  // NDI readback
  NdiSender ndi;
  Target flip;
  GLuint pbo[2] = {0, 0};
  bool pboValid[2] = {false, false};
  int pboIdx = 0;
  double ndiAccum = 0, ndiRetry = 0;

  // Background layer (LIVE: FRONT / BACK)
  std::unique_ptr<Effect> bgFx[FX_COUNT];  // own instances (an effect can run on both layers)
  bool bgSized[FX_COUNT] = {};             // its buffers exist (made the first time it is used)
  int bgCurFx = -1;
  Target bgScene, layerFront, layerBack;
  Post postBg;
  Look bgLook;              // the look the background effect sees (its own settings / mirror / colours)
  float bgShown = 0;        // 0..1, fades in / out (and dips through black when its effect changes)
  float bgFadeOverride = -1;  // fade time from a cue, -1 = the effect-change setting
  struct BgPoint {
    Vec2 pos, vel;
    float phase = 0;
  } bgPts[3];
  std::vector<Emitter> bgLive;
  int layerEdit = 0;        // which layer the LIVE tab edits: 0 FRONT, 1 BACK
  bool wantClearBg = false;
  double lastClear = -10;   // CLEAR twice within a second clears the background too
  bool uiBlocksCanvas = false;  // a pop-up is open: canvas taps are ignored
  // CUES keys
  bool cueEditOpen = false, cueEditClose = false, cueEditRequest = false, cueDeleteRequest = false;
  double cueDelArmed = -10;

  // External display
  SDL_Window* extWindow = nullptr;
  int extDisplay = -1;
  bool extWindowed = false;

  // Audio reactive
  AudioInput audio;
  AudioFeatures af;
  double audioRetry = 0;
  std::string audioDeviceOpen;
  struct VirtualPoint {
    Vec2 pos, lastPos, vel;
    float phase = 0;
  } vpts[4];
  bool vActive = false;
  bool gesturesBlocked = false;

  // DMX control
  DmxInput dmx;
  int dmxRunProtocol = -1, dmxRunUniverse = -1;
  bool dmxActive = false;     // data has been received (TouchFX holds the last look if it stops)
  bool dmxPrimed = false;
  int dmxLast[64] = {};
  float dmxDim = 1;           // master dimmer
  float dmxStrobeHz = 0;      // shutter strobe (0 = open)
  double dmxStrobePhase = 0;
  bool dmxBlackout = false;
  bool dmxColors = false;     // use the Low/Mid/High colours from DMX
  bool dmxBpm = false;        // tempo comes from DMX
  Palette dmxPalette;
  struct Cursor {
    bool active = false;
    Vec2 pos, lastPos, vel;
  } dmxCursor[2];
  uint8_t dmxFrame[512] = {};

  SysStats stats;
  VideoInput video;  // after ndi: destroyed first (its NDI receiver uses the NDI runtime)

  // Network cards
  std::vector<NetAdapter> adapters;
  double adapterTimer = 0;
  std::string ndiConfigDir;      // where TouchFX writes its NDI config copy
  bool ndiAdapterMissing = false, dmxAdapterMissing = false;
  uint32_t dmxRunAdapterIp = 0;

  TouchSlot touches[kMaxTouches];
  std::vector<Emitter> live, held;
  bool wasTouching = false, fading = false;
  bool holding = false;  // release HOLD: the effect keeps running at the last finger positions until CLEAR
  float fadeElapsed = 0, gain = 1;
  bool blackout = false;

  // Layout (window pixels)
  Rect canvas, panel, strip, fxStrip;

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
  bool isFullscreen = false, wasMaximized = false;
  SDL_Rect savedRect{0, 0, 0, 0};
  char lastKey[32] = "-";
  bool selftest = false;
  bool stress = false;  // --stress: a robot plays; nothing is saved

  // Show files
  bool showLoaded = false;
  std::string showName;
  std::string lastSavedShow, lastSavedMachine;
  double thumbTimer = 0;
  CuePlayer cue;
  UndoHistory undo;  // cue / song / scene edits of the open show
  float releaseLevel = 1;  // release fade: output level while the effect fades after the fingers lift
  bool confirmQuit = false, quitNow = false;
  float unlockHold = 0;  // seconds the SHOW MODE button has been held
  std::string launcherMessage;

  void clearEffect();
  bool touching() const { return !live.empty(); }
  void recallScene(int i);
};

enum SettingsTab { ST_COLOURS, ST_LOOK, ST_OUTPUT, ST_SCREEN, ST_AUDIO, ST_DMX, ST_SYSTEM, ST_VIDEO, ST_COUNT };

void drawUI(App& a);
void setFullscreen(App& a, bool on);
bool openShow(App& a, const std::string& name);
bool newShow(App& a, const std::string& name);
void closeShow(App& a);  // save and go back to the launcher
void saveShowNow(App& a);
void applyDmx(App& a, float dt);
void requestClear(App& a);  // CLEAR button / key
int dmxFootprint(int mode);
void rebuildFonts(App& a, float scale);
