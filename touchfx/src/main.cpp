// TouchFX - touch-driven live visuals with NDI output.
#include <SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "log.hpp"
#include "platform.hpp"
#include "show.hpp"
#include "stress.hpp"

void App::clearEffect() {
  if (curFx >= 0) fx[curFx]->clear();
  fading = false;
  holding = false;
  held.clear();
  gain = 0;
}

// Fullscreen = a borderless window covering the whole screen TouchFX is on. More reliable than SDL's
// "desktop fullscreen" (which silently did nothing on some setups) and plays well with touch and
// a second output screen.
void setFullscreen(App& a, bool on) {
  if (on == a.isFullscreen) {
    a.st.fullscreen = on;
    return;
  }
  SDL_Window* w = a.window;
  if (platformSetFullscreen(w, on)) {  // Windows: native, most reliable
    a.isFullscreen = on;
    a.st.fullscreen = on;
    return;
  }
  if (on) {
    Uint32 f = SDL_GetWindowFlags(w);
    a.wasMaximized = (f & SDL_WINDOW_MAXIMIZED) != 0;
    if (a.wasMaximized) SDL_RestoreWindow(w);
    SDL_GetWindowPosition(w, &a.savedRect.x, &a.savedRect.y);
    SDL_GetWindowSize(w, &a.savedRect.w, &a.savedRect.h);
    int disp = std::max(0, SDL_GetWindowDisplayIndex(w));
    SDL_Rect b;
    SDL_GetDisplayBounds(disp, &b);
    SDL_SetWindowBordered(w, SDL_FALSE);
    SDL_SetWindowResizable(w, SDL_FALSE);
    SDL_SetWindowPosition(w, b.x, b.y);
    SDL_SetWindowSize(w, b.w, b.h);
    SDL_RaiseWindow(w);
  } else {
    SDL_SetWindowBordered(w, SDL_TRUE);
    SDL_SetWindowResizable(w, SDL_TRUE);
    if (a.savedRect.w > 0) {
      SDL_SetWindowSize(w, a.savedRect.w, a.savedRect.h);
      SDL_SetWindowPosition(w, a.savedRect.x, a.savedRect.y);
    }
    if (a.wasMaximized) SDL_MaximizeWindow(w);
  }
  a.isFullscreen = on;
  a.st.fullscreen = on;
}

// Switches effect. The old effect keeps running and fades out over effectFade seconds (see the crossfade in
// the main loop) instead of disappearing at once.
void switchEffect(App& a, int newFx) {
  int old = a.curFx;
  if (newFx == old) return;
  bool resume = newFx == a.prevFx;  // back to the effect that is still fading out: keep what it shows
  if (a.prevFx >= 0 && !resume) a.fx[a.prevFx]->clear();
  a.prevFx = -1;
  float fadeTime = a.xfadeOverride >= 0 ? a.xfadeOverride : a.st.effectFade;
  if (old >= 0 && fadeTime > 0.01f) {
    a.prevFx = old;
    a.xfade = 0;
    a.prevHeld = a.touching() ? a.live : a.held;
    a.prevGain = a.gain * a.releaseLevel;  // switching during a release fade: carry on from that level
  } else if (old >= 0) {
    a.fx[old]->clear();
    a.xfadeOverride = -1;
  }
  a.curFx = newFx;
  if (!resume) a.fx[newFx]->clear();
  a.fading = false;
  a.holding = false;
  a.held.clear();
}

void requestClear(App& a) {
  a.wantClear = true;
  if (a.time - a.lastClear < 1.0) a.wantClearBg = true;  // twice in a row: the background too
  a.lastClear = a.time;
}

void App::recallScene(int i) {
  if (i < 0 || i >= kNumScenes || !st.scenes[i].used) return;
  float bpm = st.look.bpm;  // scenes never change the tempo
  st.look = st.scenes[i].look;
  st.look.bpm = bpm;
  lastScene = i;
}

// ---- show files ----
void saveShowNow(App& a) {
  if (!a.showLoaded || a.selftest || a.stress) return;
  a.lastSavedShow = serializeShow(a.st);
  saveStateText(showPath(a.showName), a.lastSavedShow);
}

static void resetForShow(App& a) {
  for (auto& f : a.fx)
    if (f) f->clear();
  a.prevFx = -1;
  a.xfadeOverride = -1;
  a.fading = false;
  a.held.clear();
  a.lastScene = -1;
  a.dmxPrimed = false;
  a.dmxRunProtocol = -1;  // restart DMX with the show's patch
  a.cue = CuePlayer();
  undoClear(a);
  cueSelectList(a, a.st.activeList);
}

bool openShow(App& a, const std::string& name) {
  if (!loadShow(showPath(name), a.st)) {
    a.launcherMessage = "Could not open \"" + name + "\" (file damaged?).";
    logLine("Could not open show %s", name.c_str());
    return false;
  }
  a.showName = name;
  a.st.lastShow = name;
  a.showLoaded = true;
  a.lastSavedShow = serializeShow(a.st);
  a.launcherMessage.clear();
  resetForShow(a);
  logLine("Opened show \"%s\"", name.c_str());
  return true;
}

bool newShow(App& a, const std::string& name) {
  AppState d = defaultState();
  AppState s = a.st;  // keep this PC's settings, fresh show data
  s.look = d.look;
  s.palettes = d.palettes;
  s.scenes = d.scenes;
  s.cueLists = d.cueLists;
  s.activeList = 0;
  s.dmx = d.dmx;
  std::string n = uniqueShowName(name);
  if (!saveStateText(showPath(n), serializeShow(s))) {
    a.launcherMessage = "Could not create the show (is the Documents folder writable?).";
    return false;
  }
  return openShow(a, n);
}

void closeShow(App& a) {
  if (!a.showLoaded) return;
  saveShowNow(a);
  logLine("Closed show \"%s\"", a.showName.c_str());
  a.showLoaded = false;
  a.st.showMode = false;
}

namespace {

constexpr int64_t kMouseTouchId = -1000;

// Small picture of the output for the launcher list.
void saveThumbnail(App& a) {
  if (!a.out.fbo) return;
  static Target small;
  if (!small.fbo) small.create(kThumbW, kThumbH, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, a.out.fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, small.fbo);
  glBlitFramebuffer(0, 0, a.out.w, a.out.h, 0, kThumbH, kThumbW, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);  // flipped
  glBindFramebuffer(GL_READ_FRAMEBUFFER, small.fbo);
  std::vector<uint8_t> rgba(size_t(kThumbW) * kThumbH * 4), rgb(size_t(kThumbW) * kThumbH * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, kThumbW, kThumbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  for (size_t i = 0; i < size_t(kThumbW) * kThumbH; ++i)
    for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
  saveThumb(a.showName, rgb);
}
std::string g_captureExternal;  // selftest: save the external window on its next frame
bool writePpm(const std::string& path, int w, int h);

Vec2 toUv(const App& a, float x, float y) {
  return Vec2(saturate((x - a.canvas.x) / a.canvas.w), saturate(1.f - (y - a.canvas.y) / a.canvas.h));
}

void touchDown(App& a, int64_t id, float x, float y) {
  if (!a.showLoaded || !a.canvas.contains(x, y)) return;
  if (a.uiBlocksCanvas) return;  // a window / pop-up is open over the canvas: the tap is for it
  int maxT = std::clamp(a.st.maxTouches, 1, kMaxTouches);
  for (int i = 0; i < kMaxTouches; ++i)
    if (a.touches[i].active && a.touches[i].id == id) return;
  for (int i = 0; i < maxT; ++i) {
    TouchSlot& t = a.touches[i];
    if (!t.active) {
      t.active = true;
      t.id = id;
      t.pos = t.lastPos = toUv(a, x, y);
      t.vel = Vec2();
      return;
    }
  }
}

void touchMove(App& a, int64_t id, float x, float y) {
  for (auto& t : a.touches)
    if (t.active && t.id == id) t.pos = toUv(a, x, y);
}

void touchUp(App& a, int64_t id) {
  for (auto& t : a.touches)
    if (t.active && t.id == id) t.active = false;
}

void destroyExternal(App& a) {
  if (a.extWindow) SDL_DestroyWindow(a.extWindow);
  a.extWindow = nullptr;
  a.extDisplay = -1;
  SDL_GL_MakeCurrent(a.window, a.gl);
}

void handleEvent(App& a, const SDL_Event& ev, bool& running) {
  ImGuiIO& io = ImGui::GetIO();
  Uint32 mainId = SDL_GetWindowID(a.window);
  switch (ev.type) {
    case SDL_QUIT:
      if (a.st.showMode && a.showLoaded) a.confirmQuit = true;  // show mode: ask before quitting
      else running = false;
      break;
    case SDL_WINDOWEVENT:
      if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
        if (ev.window.windowID == mainId) {
          if (a.st.showMode && a.showLoaded) a.confirmQuit = true;
          else running = false;
        }
        else if (a.extWindow && ev.window.windowID == SDL_GetWindowID(a.extWindow)) a.st.external.enabled = false;
      }
      break;
    case SDL_FINGERDOWN:
      if (ev.tfinger.windowID && ev.tfinger.windowID != mainId) break;
      touchDown(a, ev.tfinger.fingerId, ev.tfinger.x * a.winW, ev.tfinger.y * a.winH);
      break;
    case SDL_FINGERMOTION:
      if (ev.tfinger.windowID && ev.tfinger.windowID != mainId) break;
      touchMove(a, ev.tfinger.fingerId, ev.tfinger.x * a.winW, ev.tfinger.y * a.winH);
      break;
    case SDL_FINGERUP: touchUp(a, ev.tfinger.fingerId); break;
    // A real mouse also draws on the canvas (handy for testing without a touchscreen).
    case SDL_MOUSEBUTTONDOWN:
      if (ev.button.windowID == mainId && ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT)
        touchDown(a, kMouseTouchId, float(ev.button.x), float(ev.button.y));
      break;
    case SDL_MOUSEMOTION:
      if (ev.motion.windowID == mainId && ev.motion.which != SDL_TOUCH_MOUSEID)
        touchMove(a, kMouseTouchId, float(ev.motion.x), float(ev.motion.y));
      break;
    case SDL_MOUSEBUTTONUP:
      if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT) touchUp(a, kMouseTouchId);
      break;
    case SDL_KEYDOWN:
      std::snprintf(a.lastKey, sizeof(a.lastKey), "%s", SDL_GetKeyName(ev.key.keysym.sym));
      if (io.WantTextInput || ev.key.repeat) break;
      // F11 by key code or by key position (some keyboards remap the F-row); Alt+Enter as well
      if (ev.key.keysym.scancode == SDL_SCANCODE_F11 ||
          ((ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) && (ev.key.keysym.mod & KMOD_ALT))) {
        setFullscreen(a, !a.isFullscreen);
        break;
      }
      if (!a.showLoaded) {  // launcher: only fullscreen keys
        if (ev.key.keysym.sym == SDLK_ESCAPE && a.isFullscreen) setFullscreen(a, false);
        break;
      }
      if (a.cueEditOpen) {  // cue edit window: Enter = DONE, Escape = close, nothing else reaches the show
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE) a.cueEditClose = true;
        break;
      }
      if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
        cueCommand(a, CUE_GO);  // Enter = GO (every tab)
        break;
      }
      if (ev.key.keysym.mod & KMOD_CTRL) {  // Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo (not in show mode)
        SDL_Keycode k = ev.key.keysym.sym;
        if (!a.st.showMode && (k == SDLK_z || k == SDLK_y))
          undoApply(a, k == SDLK_y || (ev.key.keysym.mod & KMOD_SHIFT));
        break;
      }
      if (a.st.tab == 2) {  // CUES view: Space = GO, arrows pick the next cue / song (show mode too)
        bool done = true;
        switch (ev.key.keysym.sym) {
          case SDLK_SPACE: cueCommand(a, CUE_GO); break;
          case SDLK_BACKSPACE: cueCommand(a, CUE_BACK); break;
          case SDLK_UP: cueCommand(a, CUE_SELECT_PREV); break;
          case SDLK_DOWN: cueCommand(a, CUE_SELECT_NEXT); break;
          case SDLK_LEFT: cueCommand(a, CUE_SONG_PREV); break;
          case SDLK_RIGHT: cueCommand(a, CUE_SONG_NEXT); break;
          case SDLK_t: a.tempo.tap(a.time, a.st.look.bpm); break;  // tap tempo moves to T here
          case SDLK_e:  // edit the NEXT cue (not in show mode)
            if (!a.st.showMode) a.cueEditRequest = true;
            break;
          case SDLK_DELETE:  // delete the NEXT cue: press twice
            if (!a.st.showMode) {
              if (a.time - a.cueDelArmed < 3.0) {
                a.cueDeleteRequest = true;
                a.cueDelArmed = -10;
              } else {
                a.cueDelArmed = a.time;
              }
            }
            break;
          default: done = false; break;
        }
        if (done) break;
      }
      if (a.st.showMode) {  // show mode: only tap tempo, GO and blackout from the keyboard
        if (ev.key.keysym.sym == SDLK_SPACE) a.tempo.tap(a.time, a.st.look.bpm);
        else if (ev.key.keysym.sym == SDLK_b) a.blackout = !a.blackout;
        else if (ev.key.keysym.sym == SDLK_f) a.video.setFrozen(!a.video.frozen());
        break;
      }
      switch (ev.key.keysym.sym) {
        case SDLK_SPACE: a.tempo.tap(a.time, a.st.look.bpm); break;
        case SDLK_F11: setFullscreen(a, !a.isFullscreen); break;
        case SDLK_ESCAPE:
          if (a.isFullscreen) setFullscreen(a, false);
          break;
        case SDLK_c: requestClear(a); break;
        case SDLK_b: a.blackout = !a.blackout; break;
        case SDLK_f: a.video.setFrozen(!a.video.frozen()); break;  // freeze the video input
        default: break;
      }
      break;
    default: break;
  }
}

void ensureOutput(App& a) {
  int W = a.st.output.width, H = a.st.output.height;
  if (a.out.w == W && a.out.h == H) return;
  a.scene.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
  a.scenePrev.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
  a.prevFx = -1;
  a.out.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  a.out.clear(0, 0, 0, 1);
  a.flip.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
  a.post.resize(W, H);
  for (int i = 0; i < 2; ++i) {
    if (!a.pbo[i]) glGenBuffers(1, &a.pbo[i]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, a.pbo[i]);
    glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(W) * H * 4, nullptr, GL_STREAM_READ);
    a.pboValid[i] = false;
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  for (auto& f : a.fx) {
    f->resize(W, H);
    f->clear();
  }
  a.bgScene.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
  a.layerFront.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  a.layerBack.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  a.postBg.resize(W, H);
  for (int i = 0; i < FX_COUNT; ++i)
    if (a.bgFx[i] && a.bgSized[i]) {
      a.bgFx[i]->resize(W, H);
      a.bgFx[i]->clear();
    }
}

std::unique_ptr<Effect> makeEffect(int id) {
  switch (id) {
    case FX_FLUID: return makeFluid();
    case FX_PARTICLES: return makeParticles();
    case FX_KALEIDO: return makeKaleido();
    case FX_LASERS: return makeLasers();
    case FX_STROBE: return makeStrobe();
    case FX_SPARKLES: return makeSparkles();
    case FX_RIPPLES: return makeRipples();
    case FX_RIBBONS: return makeRibbons();
    case FX_GEOMETRY: return makeGeometry();
    case FX_TUNNEL: return makeTunnel();
    case FX_VIDEO: return makeVideo();
    case FX_PLEXUS: return makePlexus();
    case FX_FIREWORKS: return makeFireworks();
    case FX_STARFIELD: return makeStarfield();
    case FX_SPECTRUM: return makeSpectrum();
    case FX_GRID: return makeGrid();
  }
  return nullptr;
}

void buildBgEmitters(App& a, float dt);

// Renders the background layer into a.bgScene. Returns false when it is off (fully faded out).
bool renderBackground(App& a, const FxCtx& front, float dt) {
  const Look& look = a.st.look;
  const BgLayer& bg = look.bg;
  float fadeT = std::max(0.05f, a.bgFadeOverride >= 0 ? a.bgFadeOverride : std::max(0.3f, a.st.effectFade));
  bool switching = a.bgCurFx != bg.effect;
  float target = bg.on && !switching ? 1.f : 0.f;
  if (switching && a.bgShown <= 0.001f) {
    // faded out (or never shown): change effect now, then fade back in
    if (bg.on) {
      // shaders are ready from start-up; the picture buffers are made the first time the effect is used
      if (a.bgFx[bg.effect] && !a.bgSized[bg.effect]) {
        a.bgFx[bg.effect]->resize(a.out.w, a.out.h);
        a.bgSized[bg.effect] = true;
      }
      if (a.bgFx[bg.effect]) a.bgFx[bg.effect]->clear();
      a.bgCurFx = bg.effect;
      switching = false;
      target = 1.f;
    }
  }
  // a change of effect dips through black: half the time out, half back in
  float step = dt / (switching ? fadeT * 0.5f : fadeT);
  if (a.bgShown < target) a.bgShown = std::min(target, a.bgShown + step);
  else a.bgShown = std::max(target, a.bgShown - step);
  if (a.bgShown == target && !switching) a.bgFadeOverride = -1;
  if (a.bgShown <= 0.001f || a.bgCurFx < 0 || !a.bgFx[a.bgCurFx]) {
    if (!bg.on && a.bgCurFx >= 0 && a.bgFx[a.bgCurFx]) {  // off: free its picture
      a.bgFx[a.bgCurFx]->clear();
      a.bgCurFx = -1;
    }
    return false;
  }

  buildBgEmitters(a, dt);
  Look& bl = a.bgLook;
  bl = look;
  paramsToLook(bg.fx, bl);
  bl.effect = a.bgCurFx;
  bl.mirror = bg.mirror;
  bl.speedMult = bg.speedMult;
  if (bg.ownColors) {
    bl.useCustom = false;
    bl.palette = bg.palette;
    bl.colorMode = bg.colorMode;
  }
  FxCtx c = front;
  c.look = &bl;
  if (bg.ownColors) c.pal = (a.dmxActive && a.dmxColorsBack && a.st.dmx.mode == 2) ? &a.dmxPalette : &a.st.palettes[bg.palette];
  c.speed = look.bpm / 120.f * kSpeedMults[bg.speedMult];
  c.live = a.bgLive;
  c.held = a.bgLive;
  c.touching = !a.bgLive.empty();
  c.gain = 1.f;
  c.fadeMul = 1.f;
  if (a.st.audio.enabled && a.audio.isOpen()) c.speed *= 1.f + a.st.audio.reactivity * a.af.focus * 0.6f;
  a.bgFx[a.bgCurFx]->frame(c, a.bgScene);
  return true;
}

// Scale from output UV to video UV so the video fills the output (crop), for the video colours.
void videoFillFit(App& a, float& fx, float& fy) {
  float out = float(a.out.w) / std::max(1, a.out.h);
  float va = a.video.height() > 0 ? float(a.video.width()) / a.video.height() : out;
  fx = fy = 1;
  if (va > out) fx = out / va;
  else fy = va / out;
}

void ensureExternal(App& a) {
  const ExternalSettings& e = a.st.external;
  int nd = std::max(1, SDL_GetNumVideoDisplays());
  int disp = std::clamp(e.display, 0, nd - 1);
  int mainDisp = SDL_GetWindowDisplayIndex(a.window);
  // Never cover the control screen with a fullscreen output: use a window there instead.
  bool windowed = e.windowed || disp == mainDisp;
  if (!e.enabled) {
    if (a.extWindow) destroyExternal(a);
    return;
  }
  if (a.extWindow && a.extDisplay == disp && a.extWindowed == windowed) return;
  destroyExternal(a);
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
  int w = 960, h = 540;
  if (windowed) {
    flags |= SDL_WINDOW_RESIZABLE;
  } else {
    SDL_Rect r;
    SDL_GetDisplayBounds(disp, &r);
    w = r.w;
    h = r.h;
    flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN_DESKTOP;
  }
  a.extWindow = SDL_CreateWindow("TouchFX Output", SDL_WINDOWPOS_CENTERED_DISPLAY(disp),
                                 SDL_WINDOWPOS_CENTERED_DISPLAY(disp), w, h, flags);
  a.extDisplay = disp;
  a.extWindowed = windowed;
  SDL_GL_MakeCurrent(a.window, a.gl);
  SDL_RaiseWindow(a.window);  // keep the control screen focused
}

void renderExternal(App& a) {
  if (!a.extWindow) return;
  if (SDL_GL_MakeCurrent(a.extWindow, a.gl) != 0) return;
  int dw, dh;
  SDL_GL_GetDrawableSize(a.extWindow, &dw, &dh);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, dw, dh);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  float srcA = float(a.out.w) / a.out.h, dstA = float(dw) / std::max(1, dh);
  float x = 0, y = 0, w = float(dw), h = float(dh);
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  switch (a.st.external.fit) {
    case FIT_FIT:
      if (dstA > srcA) { w = dh * srcA; x = (dw - w) * 0.5f; }
      else { h = dw / srcA; y = (dh - h) * 0.5f; }
      break;
    case FIT_FILL:
      if (dstA > srcA) { float k = srcA / dstA; v0 = 0.5f - k * 0.5f; v1 = 0.5f + k * 0.5f; }
      else { float k = dstA / srcA; u0 = 0.5f - k * 0.5f; u1 = 0.5f + k * 0.5f; }
      break;
    default: break;
  }
  glViewport(int(x), int(y), int(w), int(h));
  // a screen bigger than the render (e.g. 4K from 1080p): sharp bicubic upscale instead of a soft bilinear one
  bool upscale = w > (u1 - u0) * a.out.w * 1.05f || h > (v1 - v0) * a.out.h * 1.05f;
  presentTexture(a.out.tex, u0, v0, u1, v1, a.st.output.alpha && !a.st.output.premultiplied, upscale ? a.out.w : 0,
                 upscale ? a.out.h : 0);
  if (!g_captureExternal.empty()) {
    writePpm(g_captureExternal, dw, dh);
    g_captureExternal.clear();
  }
  SDL_GL_SetSwapInterval(1);
  SDL_GL_SwapWindow(a.extWindow);
  SDL_GL_MakeCurrent(a.window, a.gl);
}

void updateNdi(App& a, float dt) {
  const OutputSettings& o = a.st.output;
  if (!o.ndiEnabled) {
    if (a.ndi.running()) a.ndi.stop();
    return;
  }
  // network card: automatic, or the chosen one (if it is missing, NDI waits instead of using another one)
  std::string adapterIp;
  a.ndiAdapterMissing = false;
  if (!o.ndiAdapter.empty()) {
    const NetAdapter* n = findAdapter(a.adapters, o.ndiAdapter);
    if (!n) {
      a.ndiAdapterMissing = true;
      if (a.ndi.running()) a.ndi.stop();
      return;
    }
    adapterIp = ipToString(n->ip);
  }
  if (!a.ndiConfigDir.empty()) {
    bool changed = a.ndi.adapterChangeNeeded(adapterIp);
    if (changed) a.video.pauseNdi(true);  // its NDI receiver must be closed while NDI restarts
    a.ndi.setAdapter(adapterIp, a.ndiConfigDir);
    if (changed) {
      a.video.pauseNdi(false);
      a.ndiRetry = 0;
    }
  }
  if (!a.ndi.running() || a.ndi.name() != o.ndiName) {
    a.ndiRetry -= dt;
    if (a.ndiRetry <= 0) {
      a.ndi.start(o.ndiName);
      a.ndiRetry = 3.0;  // if it failed, try again in a few seconds
      a.pboValid[0] = a.pboValid[1] = false;
    }
    if (!a.ndi.running()) return;
  }

  // Pace frames to the chosen output frame rate.
  double interval = 1.0 / o.fps;
  a.ndiAccum += dt;
  if (a.ndiAccum < interval * 0.92) return;
  a.ndiAccum -= interval;
  if (a.ndiAccum > interval) a.ndiAccum = 0;

  int W = a.out.w, H = a.out.h;
  // Flip vertically so row 0 is the top of the image, as NDI expects.
  glBindFramebuffer(GL_READ_FRAMEBUFFER, a.out.fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, a.flip.fbo);
  glBlitFramebuffer(0, 0, W, H, 0, H, W, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, a.flip.fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);

  // Async readback: start this frame's copy, then send the previous one.
  int cur = a.pboIdx, prev = a.pboIdx ^ 1;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, a.pbo[cur]);
  glReadPixels(0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
  a.pboValid[cur] = true;
  if (a.pboValid[prev]) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, a.pbo[prev]);
    if (void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(W) * H * 4, GL_MAP_READ_BIT)) {
      a.ndi.submit((const uint8_t*)p, W, H, o.fps, o.alpha);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  a.pboIdx = prev;
}

void layout(App& a) {
  float s = a.fontScale > 0 ? a.fontScale : 1.f;
  const LayoutSettings& L = a.st.layout;
  float pw = std::clamp(470.f * s * L.panelWidth, 300.f, a.winW * 0.65f);
  float W = float(a.winW), H = float(a.winH);
  a.panel = L.panelLeft ? Rect{0, 0, pw, H} : Rect{W - pw, 0, pw, H};
  Rect area = L.panelLeft ? Rect{pw, 0, W - pw, H} : Rect{0, 0, W - pw, H};
  float m = 12.f * s;

  // Bars under the canvas: effects, then scenes.
  float innerW = area.w - 2 * m;
  float bottom = area.y + area.h - m;
  float used = 0;
  if (L.sceneStrip) {
    int cols = innerW / kNumScenes >= 92.f * s ? kNumScenes : kNumScenes / 2;
    int rows = kNumScenes / cols;
    float h = rows * 60.f * s + (rows - 1) * 6.f * s + 16.f * s;
    a.strip = Rect{area.x + m, bottom - h, innerW, h};
    bottom -= h + m * 0.6f;
    used += h + m * 0.6f;
  } else {
    a.strip = Rect{};
  }
  if (L.effectStrip) {
    int cols = innerW / FX_COUNT >= 100.f * s ? FX_COUNT : FX_COUNT / 2;
    int rows = FX_COUNT / cols;
    float h = rows * 50.f * s + (rows - 1) * 6.f * s + 16.f * s;
    a.fxStrip = Rect{area.x + m, bottom - h, innerW, h};
    used += h + m * 0.6f;
  } else {
    a.fxStrip = Rect{};
  }
  float stripH = used;

  float areaW = std::max(50.f, area.w - m * 2);
  float areaH = std::max(50.f, area.h - m * 2 - stripH);
  float aspect = float(a.st.output.width) / a.st.output.height;
  float w = areaW, h = areaW / aspect;
  if (h > areaH) {
    h = areaH;
    w = h * aspect;
  }
  a.canvas = Rect{area.x + m + (areaW - w) * 0.5f, area.y + m + (areaH - h) * 0.5f, w, h};
}

void addEmittersTo(std::vector<Emitter>& out, const std::vector<Mat2>& mats, float aspect, Vec2 pos, Vec2 vel,
                   int touchIndex) {
  Vec2 q((pos.x - 0.5f) * aspect, pos.y - 0.5f);
  for (size_t k = 0; k < mats.size() && int(out.size()) < kMaxSplats; ++k) {
    Emitter e;
    Vec2 mq = mats[k] * q;
    e.pos = Vec2(mq.x / aspect + 0.5f, mq.y + 0.5f);
    e.vel = mats[k] * vel;
    e.speed = length(vel);
    e.touch = touchIndex;
    e.mirror = int(k);
    e.m = mats[k];
    out.push_back(e);
  }
}

void addEmitters(App& a, const std::vector<Mat2>& mats, float aspect, Vec2 pos, Vec2 vel, int touchIndex) {
  addEmittersTo(a.live, mats, aspect, pos, vel, touchIndex);
}

// Background layer points: wander by themselves (faster with the music), plus the fingers with FOLLOW TOUCH.
void buildBgEmitters(App& a, float dt) {
  const BgLayer& bg = a.st.look.bg;
  float aspect = float(a.st.output.width) / a.st.output.height;
  std::vector<Mat2> mats = mirrorMatrices(bg.mirror);
  a.bgLive.clear();
  float energy = 1.f;
  if (a.st.audio.enabled && a.af.active) energy = 0.5f + a.af.focus * 1.2f + a.af.pulse * 0.5f;
  float speed = a.st.look.bpm / 120.f * kSpeedMults[bg.speedMult];
  int src = bg.source < 0 ? (bg.followTouch ? BGSRC_BOTH : BGSRC_AUTO) : bg.source;
  for (int k = 0; k < 2 && src != BGSRC_TOUCH; ++k) {
    App::BgPoint& v = a.bgPts[k];
    v.phase += dt * (0.05f + bg.motion * 0.9f) * energy * std::clamp(speed, 0.25f, 3.f);
    float ph = v.phase + k * 2.7f;
    Vec2 p(0.5f + 0.33f * std::cos(ph * 0.83f) * (0.7f + 0.3f * std::sin(ph * 0.29f + k)),
           0.5f + 0.3f * std::sin(ph * 1.13f + k * 1.7f));
    Vec2 d = p - v.pos;
    Vec2 inst(d.x * aspect / std::max(dt, 1e-3f), d.y / std::max(dt, 1e-3f));
    if (length(d) > 0.2f) inst = Vec2();
    v.vel = v.vel + (inst - v.vel) * 0.2f;
    v.pos = p;
    addEmittersTo(a.bgLive, mats, aspect, v.pos, v.vel, kMaxTouches + 8 + k);
  }
  if (src != BGSRC_AUTO)
    for (int i = 0; i < kMaxTouches; ++i)
      if (a.touches[i].active) addEmittersTo(a.bgLive, mats, aspect, a.touches[i].pos, a.touches[i].vel, i);
}

// Audio auto-play: virtual fingers that wander over the canvas, pushed along by the music.
void updateVirtualPoints(App& a, float dt, float aspect, const std::vector<Mat2>& mats) {
  const AudioSettings& as = a.st.audio;
  a.vActive = as.enabled && as.autoPlay && a.af.active && a.showLoaded;
  // A real finger takes over: the auto points step aside while someone touches, so the effect follows the
  // hand at full speed instead of sharing its particles / slots with the music points.
  bool touched = false;
  for (int i = 0; i < kMaxTouches; ++i) touched = touched || a.touches[i].active;
  if (touched) a.vActive = false;
  if (!a.vActive) return;
  float energy = 0.35f + a.af.focus * 1.1f + a.af.pulse * 0.6f * as.reactivity;
  for (int k = 0; k < as.points; ++k) {
    App::VirtualPoint& v = a.vpts[k];
    v.phase += dt * (0.2f + as.motion * 1.6f) * energy;
    if (a.af.onset) v.phase += 0.25f * as.reactivity * as.motion;
    float ph = v.phase + k * 2.1f + (k >= 4 ? k * 0.77f : 0.f);
    Vec2 p(0.5f + 0.36f * std::cos(ph * 1.0f) * (0.65f + 0.35f * std::sin(ph * 0.37f + k)),
           0.5f + 0.32f * std::sin(ph * 1.37f + k * 1.3f));
    Vec2 d = p - v.pos;
    Vec2 inst(d.x * aspect / std::max(dt, 1e-3f), d.y / std::max(dt, 1e-3f));
    if (length(d) > 0.2f) inst = Vec2();  // first frame: no jump velocity
    v.vel = v.vel + (inst - v.vel) * 0.35f;
    v.pos = p;
    // louder music = faster smoke / particle bursts
    addEmitters(a, mats, aspect, v.pos, v.vel * (0.6f + a.af.focus * 1.5f), kMaxTouches + 20 + k);
  }
}

void buildEmitters(App& a, float dt) {
  float aspect = float(a.st.output.width) / a.st.output.height;
  std::vector<Mat2> mats = mirrorMatrices(a.st.look.mirror);
  a.live.clear();
  updateVirtualPoints(a, dt, aspect, mats);
  // DMX cursors (Advanced mode): the console moves them with Pan/Tilt.
  for (int k = 0; k < 4; ++k) {
    App::Cursor& c = a.dmxCursor[k];
    if (!c.active) continue;
    Vec2 d = c.pos - c.lastPos;
    Vec2 inst(d.x * aspect / std::max(dt, 1e-3f), d.y / std::max(dt, 1e-3f));
    c.vel = c.vel + (inst - c.vel) * 0.35f;
    c.lastPos = c.pos;
    addEmitters(a, mats, aspect, c.pos, c.vel, kMaxTouches + 4 + k);
  }
  for (int i = 0; i < kMaxTouches; ++i) {
    TouchSlot& t = a.touches[i];
    if (!t.active) continue;
    if (i >= a.st.maxTouches) {  // the limit was lowered while touching
      t.active = false;
      continue;
    }
    Vec2 d = t.pos - t.lastPos;
    Vec2 inst(d.x * aspect / std::max(dt, 1e-3f), d.y / std::max(dt, 1e-3f));
    t.vel = t.vel + (inst - t.vel) * 0.35f;
    t.lastPos = t.pos;
    addEmitters(a, mats, aspect, t.pos, t.vel, i);
  }
}

void updateAudio(App& a, float dt) {
  AudioSettings& as = a.st.audio;
  // The AUDIO settings tab listens even when audio reactive is off, to check the input.
  bool monitor = a.showLoaded && a.st.tab == 1 && a.st.settingsTab == ST_AUDIO;
  if (!as.enabled && !monitor) {
    if (a.audio.isOpen()) a.audio.close();
    a.af = AudioFeatures();
    return;
  }
  if (!a.audio.isOpen() || a.audioDeviceOpen != as.device) {
    a.audioRetry -= dt;
    if (a.audioRetry <= 0 || a.audioDeviceOpen != as.device) {
      a.audioDeviceOpen = as.device;
      a.audio.open(as.device);
      a.audioRetry = 3.0;
    }
  }
  float lo = 30, hi = 16000;
  audioRange(as, lo, hi);
  a.audio.update(dt, as.sensitivity, lo, hi);
  a.af = a.audio.features();
  // Follow the music's tempo and line the beat up with the hits.
  if (as.enabled && as.autoBpm && a.af.onset && !a.dmxBpm) {
    Look& l = a.st.look;
    if (a.af.bpm > 0) {
      float target = a.af.bpm;
      // a big change (new song) moves quickly; small ones settle gently
      float k = std::fabs(target - l.bpm) > 8.f ? 0.5f : 0.25f;
      l.bpm = std::clamp(std::round((l.bpm + (target - l.bpm) * k) * 10.f) / 10.f, 20.f, 300.f);
    }
    double b = a.tempo.beat, nearest = std::floor(b + 0.5);
    a.tempo.beat = b + (nearest - b) * 0.5;
  }
}

// ---- developer self-test: fake fingers, every effect, screenshots ----
struct SelftestStep {
  int effect, laserMode, mirror, palette, fingers;  // laserMode: also the strobe shape / video mode
  bool audio = false;
  int dmx = 0;  // 1: sACN Advanced @1 (universe 100), 2: Art-Net Basic @101 (universe 99) - see tools/dmx_test_sender.py
  int bg = -1;  // background effect (-1 = off)
  int blend = BLEND_ADD;
  int colorMode = COLOR_GRADIENT;
  int extra = 0;  // LED grid: incandescent bulbs; lasers: random swing + scatter
};
const SelftestStep kSteps[] = {
    {FX_FLUID, 0, MIRROR_LR, 0, 2},          {FX_PARTICLES, 0, MIRROR_LR, 2, 2},
    {FX_SPARKLES, 0, MIRROR_QUAD, 7, 2},     {FX_KALEIDO, 0, MIRROR_OFF, 4, 2},
    {FX_LASERS, LASER_FAN, MIRROR_LR, 6, 2}, {FX_LASERS, LASER_AIM, MIRROR_OFF, 1, 2},
    {FX_LASERS, LASER_TUNNEL, MIRROR_LR, 3, 2}, {FX_LASERS, LASER_WAVE, MIRROR_OFF, 6, 3},
    {FX_STROBE, 0, MIRROR_LR, 1, 6},
    {FX_RIPPLES, 0, MIRROR_LR, 0, 2},          {FX_RIBBONS, 0, MIRROR_LR, 1, 2},
    {FX_GEOMETRY, 0, MIRROR_QUAD, 6, 2},       {FX_TUNNEL, 0, MIRROR_OFF, 4, 3},
    {FX_VIDEO, 0, MIRROR_OFF, 5, 3},       {FX_PLEXUS, 0, MIRROR_LR, 5, 2},
    {FX_FIREWORKS, 0, MIRROR_OFF, 2, 3},       {FX_STARFIELD, 0, MIRROR_OFF, 6, 1},
    {FX_SPECTRUM, 0, MIRROR_LR, 1, 0, true},   {FX_GRID, 0, MIRROR_LR, 7, 2},
    {FX_STROBE, STROBE_SHOCKWAVE, MIRROR_LR, 3, 2},
    {FX_PARTICLES, 0, MIRROR_LR, 4, 0, true},    {FX_LASERS, LASER_TUNNEL, MIRROR_QUAD, 6, 0, true},
    {FX_FLUID, 0, MIRROR_LR, 0, 0, false, 1},    {FX_FLUID, 0, MIRROR_LR, 0, 2, false, 2},
    // video input (built-in test source) and the background layer
    {FX_VIDEO, VID_REVEAL, MIRROR_OFF, 0, 2},     {FX_VIDEO, VID_LIQUID, MIRROR_OFF, 0, 2},
    {FX_VIDEO, VID_RIPPLE, MIRROR_OFF, 0, 2},     {FX_VIDEO, VID_KALEIDO, MIRROR_OFF, 0, 1},
    {FX_VIDEO, VID_PIXEL, MIRROR_LR, 0, 2},       {FX_VIDEO, VID_WARP, MIRROR_OFF, 0, 2},
    {FX_PARTICLES, 0, MIRROR_LR, 1, 2, false, 0, FX_STARFIELD, BLEND_ADD},
    {FX_FLUID, 0, MIRROR_LR, 0, 2, false, 0, FX_VIDEO, BLEND_REVEAL},
    {FX_LASERS, LASER_FAN, MIRROR_LR, 6, 2, false, 0, FX_TUNNEL, BLEND_BEHIND},
    {FX_FLUID, 0, MIRROR_LR, 0, 2, false, 0, -1, BLEND_ADD, COLOR_VIDEO},
    {FX_FLUID, 0, MIRROR_LR, 0, 2, false, 0, -1, BLEND_ADD, COLOR_VIDEO_MASK},
    {FX_VIDEO, VID_CLEAN, MIRROR_OFF, 0, 0},  // no fingers: ALWAYS ON
    {FX_VIDEO, VID_REVEAL, MIRROR_OFF, 0, 1},  // ALWAYS ON, base level, one finger painting
    {FX_GRID, GRID_SCROLL, MIRROR_OFF, 4, 1},  {FX_GRID, GRID_EQ, MIRROR_OFF, 1, 1, true},
    {FX_GRID, GRID_PLASMA, MIRROR_OFF, 6, 1},  {FX_GRID, GRID_RAIN, MIRROR_OFF, 0, 2},
    {FX_GRID, GRID_CHECKER, MIRROR_OFF, 2, 1}, {FX_GRID, GRID_RADAR, MIRROR_OFF, 7, 2},
    {FX_GRID, GRID_VIDEO, MIRROR_OFF, 0, 1},   {FX_GRID, GRID_TRAIL, MIRROR_OFF, 2, 3, false, 0, -1, 0, 0, 1},
    {FX_LASERS, LASER_FAN, MIRROR_OFF, 6, 2, false, 0, -1, 0, 0, 1},
    // 0.9.2: striped ribbons, the new audio visuals, mirror-aware equalisers
    {FX_RIBBONS, 0, MIRROR_OFF, 0, 3, false, 0, -1, 0, 0, 1}, {FX_RIBBONS, 0, MIRROR_LR, 3, 2, false, 0, -1, 0, 0, 1},
    {FX_SPECTRUM, SPEC_WATERFALL, MIRROR_OFF, 1, 2, true}, {FX_SPECTRUM, SPEC_SCOPE, MIRROR_OFF, 4, 2, true},
    {FX_SPECTRUM, SPEC_RINGS, MIRROR_LR, 6, 2, true},      {FX_SPECTRUM, SPEC_BURST, MIRROR_OFF, 2, 2, true},
    {FX_SPECTRUM, SPEC_BARS, MIRROR_QUAD, 1, 1, true},     {FX_SPECTRUM, SPEC_BARS, MIRROR_OFF, 7, 1, true},
    {FX_GRID, GRID_VU, MIRROR_OFF, 1, 1, true},            {FX_GRID, GRID_SPECTRO, MIRROR_LR, 6, 1, true},
    {FX_GRID, GRID_EQ, MIRROR_QUAD, 1, 1, true},
};
constexpr int kStepFrames = 120;
constexpr int kNumSteps = int(sizeof(kSteps) / sizeof(kSteps[0]));
constexpr int kUiShots = 10;  // live (no strip), settings x4, panel left, cues, show mode, BACK, video in

void selftestTouches(App& a, int frame, int fingers) {
  float t = frame / 60.f;
  int local = frame % kStepFrames;
  for (int i = 0; i < kMaxTouches; ++i) {
    TouchSlot& s = a.touches[i];
    s.active = i < fingers && !(i == 0 && local >= 70 && local < 82);  // exercise release / fade
    s.id = i + 1;
    float ph = i * 1.9f;
    s.pos = Vec2(0.5f + 0.3f * std::cos(t * (1.3f + i * 0.2f) + ph) * (i ? 1.f : 0.7f),
                 0.5f + 0.28f * std::sin(t * (1.7f + i * 0.15f) + ph));
  }
}

bool writePpm(const std::string& path, int w, int h) {
  std::vector<unsigned char> px(size_t(w) * h * 3);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * w * 3], 1, size_t(w) * 3, f);
  std::fclose(f);
  return true;
}

}  // namespace

// Touches for the stress-test robot (same path as real fingers).
void robotTouchDown(App& a, int64_t id, float x, float y) { touchDown(a, id, x, y); }
void robotTouchMove(App& a, int64_t id, float x, float y) { touchMove(a, id, x, y); }
void robotTouchUp(App& a, int64_t id) { touchUp(a, id); }

int main(int argc, char** argv) {
  App a;
  std::string selftestDir;
  int selftestFrom = 0;  // start the self-test at this step (debugging)
  double stressMinutes = 0;  // --stress <minutes>
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--selftest" && i + 1 < argc) selftestDir = argv[++i];
    if (std::string(argv[i]) == "--selftest-from" && i + 1 < argc) selftestFrom = std::atoi(argv[++i]);
    if (std::string(argv[i]) == "--stress" && i + 1 < argc) stressMinutes = std::atof(argv[++i]);
  }
  a.selftest = !selftestDir.empty();

  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");   // fingers also drive the UI
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");   // but the mouse does not fake fingers
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TouchFX", SDL_GetError(), nullptr);
    return 1;
  }

  // Settings live in %APPDATA%\TouchFX\TouchFX\settings.json
  if (char* pref = SDL_GetPrefPath("TouchFX", "TouchFX")) {
    a.settingsPath = std::string(pref) + "settings.json";
    SDL_free(pref);
  }
  a.st = defaultState();
  if (!a.selftest && !a.settingsPath.empty()) {
    std::string dir = a.settingsPath.substr(0, a.settingsPath.size() - std::string("settings.json").size());
    logInit(dir);
    a.ndiConfigDir = dir + "ndi";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(a.ndiConfigDir), ec);
    platformInstallCrashHandler(dir);
    logLine("TouchFX %s starting", TOUCHFX_VERSION);
    // Before show files, settings.json held everything: turn it into a first show.
    if (listShows().empty() && fileHasShowData(a.settingsPath)) {
      AppState old = defaultState();
      if (loadState(a.settingsPath, old)) {
        saveStateText(showPath("My Show"), serializeShow(old));
        logLine("Converted the previous settings into the show \"My Show\"");
      }
    }
    bool loaded = loadMachine(a.settingsPath, a.st);
    if (!loaded) {
      // Keep an unreadable settings file aside instead of silently overwriting it.
      if (FILE* f = std::fopen(a.settingsPath.c_str(), "rb")) {
        std::fclose(f);
        std::string bad = a.settingsPath + ".bad";
        std::remove(bad.c_str());
        std::rename(a.settingsPath.c_str(), bad.c_str());
      }
    }
    a.lastSavedMachine = serializeMachine(a.st);
    saveStateText(a.settingsPath, a.lastSavedMachine);
    if (logPreviousRunCrashed()) {
      a.launcherMessage = "TouchFX closed unexpectedly last time. The log and crash report are in the logs folder.";
      if (a.st.videoIn.type != VSRC_OFF) {  // safe start: the video input could be the cause
        a.st.videoIn.type = VSRC_OFF;
        a.launcherMessage += " The video input was turned off as a precaution: choose it again in SETTINGS > VIDEO IN.";
        logLine("Safe start: video input turned off after an unexpected close");
      }
    }
  }
  platformPreventSleep(true);
  if (a.selftest) a.showLoaded = true;  // the self-test runs without a show file
  if (stressMinutes > 0 && !a.selftest) {
    // stress test: a throw-away show, nothing is saved
    a.stress = true;
    a.showLoaded = true;
    a.showName = "STRESS TEST (nothing is saved)";
    a.st.showMode = false;
  }
  std::snprintf(a.ndiNameEdit, sizeof(a.ndiNameEdit), "%s", a.st.output.ndiName.c_str());
  a.uiScaleEdit = a.st.uiScale;
  if (a.selftest) {
    a.st.output.ndiEnabled = true;
    a.st.maxTouches = kMaxTouches;
    a.st.external.enabled = true;
    a.st.external.windowed = true;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (!a.selftest) flags |= SDL_WINDOW_MAXIMIZED;
  a.window = SDL_CreateWindow("TouchFX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, a.selftest ? 1600 : 1280,
                              a.selftest ? 900 : 760, flags);
  if (!a.window) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TouchFX", SDL_GetError(), nullptr);
    return 1;
  }
  a.gl = SDL_GL_CreateContext(a.window);
  if (!a.gl || !gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TouchFX",
                             "OpenGL 3.3 is required. Please update your graphics driver.", a.window);
    return 1;
  }
  SDL_GL_MakeCurrent(a.window, a.gl);
  SDL_GL_SetSwapInterval(a.selftest ? 0 : 1);
  platformSetupWindow(a.window);
  auto setGestureBlock = [&](bool on) {
    if (on && !a.gesturesBlocked) {
      int orig = platformBlockSystemGestures();
      if (orig >= 0 && a.st.gestureRestore < 0) a.st.gestureRestore = orig;  // keep the value from a crashed run
      a.gesturesBlocked = true;
    } else if (!on && a.gesturesBlocked) {
      platformRestoreSystemGestures(a.st.gestureRestore);
      a.st.gestureRestore = -1;
      a.gesturesBlocked = false;
    }
    if (!a.settingsPath.empty() && !a.selftest && !a.stress) {
      a.lastSavedMachine = serializeMachine(a.st);
      saveStateText(a.settingsPath, a.lastSavedMachine);  // save now so a crash can't lose the original value
    }
  };
  if (!a.selftest && a.st.blockGestures) setGestureBlock(true);
  if (a.st.fullscreen && !a.selftest) setFullscreen(a, true);

  float ddpi = 0;
  if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(a.window), &ddpi, nullptr, nullptr) == 0 && ddpi > 0)
    a.dpiScale = std::min(3.f, std::max(1.f, ddpi / 96.f));

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui_ImplSDL2_InitForOpenGL(a.window, a.gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  glutilInit();
  bool shadersOk = fxSharedInit() && a.post.init() && a.postBg.init() && presentInit() && compositeInit() &&
                   a.video.init();
  for (int i = 0; i < FX_COUNT; ++i) a.fx[i] = makeEffect(i);
  for (auto& f : a.fx) shadersOk &= f->init();
  for (int i = 0; i < FX_COUNT; ++i) {  // background layer: its own effects (shaders now, buffers when used)
    a.bgFx[i] = makeEffect(i);
    shadersOk &= a.bgFx[i]->init();
  }
  if (!shadersOk) SDL_Log("Some shaders failed to compile; see the log above.");

  Uint64 freq = SDL_GetPerformanceFrequency();
  Uint64 last = SDL_GetPerformanceCounter();
  bool running = true;
  int frame = selftestFrom * kStepFrames;
  int shot = 0;
  const int fxFrames = kNumSteps * kStepFrames;

  while (running) {
    SDL_GetWindowSize(a.window, &a.winW, &a.winH);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      handleEvent(a, ev, running);
    }

    Uint64 now = SDL_GetPerformanceCounter();
    float dt = float(double(now - last) / double(freq));
    last = now;
    float rawFrame = dt;
    if (a.selftest) dt = 1.f / 60.f;
    dt = std::min(dt, 0.1f);
    a.time += dt;
    if (dt > 0) a.fps = a.fps * 0.95f + (1.f / std::max(dt, 1e-4f)) * 0.05f;

    if (a.selftest) {
      if (frame < fxFrames) {
        const SelftestStep& sp = kSteps[frame / kStepFrames];
        a.st.dmx.protocol = sp.dmx == 1 ? 1 : (sp.dmx == 2 ? 2 : 0);
        a.st.dmx.mode = sp.dmx == 2 ? 0 : 1;
        a.st.dmx.address = sp.dmx == 2 ? 101 : 1;
        if (frame % kStepFrames == 0 && sp.dmx) a.st.look = Look();  // start from defaults, DMX drives the rest
        if (!sp.dmx) {
          a.st.look.effect = sp.effect;
          a.st.look.lasers.mode = sp.laserMode;
          a.st.look.mirror = sp.mirror;
          a.st.look.palette = sp.palette;
        }
        if (sp.effect == FX_STROBE) a.st.look.strobe.shape = sp.laserMode ? sp.laserMode : STROBE_SPARKLE;
        if (sp.effect == FX_VIDEO) a.st.look.video.mode = sp.laserMode;
        if (sp.effect == FX_SPECTRUM) a.st.look.spectrum.mode = sp.laserMode;
        if (sp.effect == FX_RIBBONS && sp.extra) a.st.look.ribbons.length = 0.6f;
        if (sp.effect == FX_GRID) {
          a.st.look.grid.mode = sp.laserMode;
          a.st.look.grid.bulb = sp.extra ? 1.f : 0.f;
          a.st.look.grid.round = sp.extra ? 1.f : 0.3f;
        }
        if (sp.effect == FX_LASERS) {
          a.st.look.lasers.swingMode = sp.extra ? SWING_RANDOM : SWING_TOGETHER;
          a.st.look.lasers.scatter = sp.extra ? 0.7f : 0.f;
          a.st.look.lasers.swing = sp.extra ? 0.6f : 0.3f;
        }
        a.st.look.video.alwaysOn = sp.effect == FX_VIDEO && (sp.fingers == 0 || sp.laserMode == VID_REVEAL && sp.fingers == 1);
        a.st.videoIn.type = VSRC_TEST;
        a.st.videoIn.source = "Colour bars";
        a.st.look.colorMode = sp.colorMode;
        a.st.look.bg.on = sp.bg >= 0;
        if (sp.bg >= 0) {
          a.st.look.bg.effect = sp.bg;
          a.st.look.bg.blend = sp.blend;
          a.st.look.bg.level = 0.8f;
          a.st.look.bg.fx.video.mode = VID_LIQUID;
        }
        a.st.audio.enabled = sp.audio;
        a.st.audio.device = kAudioTest;
        a.st.audio.points = 2;
        a.st.output.alpha = frame >= kStepFrames * 7;  // test the alpha path on the last steps
        a.st.tab = 0;
        selftestTouches(a, frame, sp.fingers);
        if (sp.effect == FX_RIBBONS && !sp.extra) {  // change the length while a released ribbon is still drifting
          int local = frame % kStepFrames;
          a.st.look.ribbons.length = local < 75 ? 0.5f : (local < 100 ? 0.9f : 0.15f);
        }
      } else {
        int k = (frame - fxFrames) / 20;
        a.st.audio.enabled = true;  // so the UI shots show the audio controls
        a.st.dmx.protocol = 1;
        a.st.layout.sceneStrip = k != 0;
        a.st.tab = (k >= 1 && k <= 4) ? 1 : 0;
        static const int tabsShown[4] = {ST_LOOK, ST_AUDIO, ST_DMX, ST_SYSTEM};
        a.st.settingsTab = tabsShown[std::clamp(k - 1, 0, 3)];
        a.st.layout.panelLeft = k == 5;
        if (k == 6) {  // cue list with cues, GO through them (including a same-effect fade)
          a.st.tab = 2;
          if (frame == fxFrames + 6 * 20) {
            CueList& cl = a.st.cueLists[0];
            cl.name = "01 Intro";
            cl.cues.clear();
            const int fxs[4] = {FX_FLUID, FX_LASERS, FX_LASERS, FX_SPARKLES};
            const char* names[4] = {"Smoke intro", "First drop", "Wide fan", "Outro sparkle"};
            for (int i = 0; i < 4; ++i) {
              Cue c;
              c.name = names[i];
              c.look = a.st.look;
              c.look.effect = fxs[i];
              c.look.lasers.spread = i == 2 ? 1.f : 0.3f;
              c.fade = 0.2f;
              c.follow = i == 0 ? FOLLOW_BARS : FOLLOW_NONE;
              c.number = i == 2 ? 2.5f : float(i < 2 ? i + 1 : 3);
              cl.cues.push_back(c);
            }
            cl.endAction = END_LOOP;
            a.st.cueLists.push_back(CueList{"02 Main", END_STOP, {}});
            cueSelectList(a, 0);
            cueGo(a);
          }
          if (frame == fxFrames + 6 * 20 + 8) cueGo(a);
          if (frame == fxFrames + 6 * 20 + 12) cueGo(a);  // same effect: values glide
          if (frame == fxFrames + 6 * 20 + 14) cueGo(a);  // last cue: GO now shows LOOP
        }
        if (k == 7) {  // show mode
          a.st.tab = 0;
          a.st.showMode = true;
        }
        if (k == 8) {  // LIVE, editing the background layer
          a.st.showMode = false;
          a.st.tab = 0;
          a.layerEdit = 1;
          a.st.look.bg.on = true;
          a.st.look.bg.effect = FX_STARFIELD;
        }
        if (k == 9) {  // SETTINGS > VIDEO IN
          a.layerEdit = 0;
          a.st.tab = 1;
          a.st.settingsTab = ST_VIDEO;
        }
        if (k == 0) {
          a.st.scenes[0].used = a.st.scenes[3].used = true;
          a.st.scenes[3].look.effect = FX_LASERS;
        }
      }
    }

    float wantScale = a.dpiScale * a.st.uiScale;
    if (std::fabs(wantScale - a.fontScale) > 0.01f) rebuildFonts(a, wantScale);
    layout(a);
    ensureOutput(a);
    ensureExternal(a);

    if (a.quitNow) running = false;
    a.adapterTimer -= dt;
    if (a.adapterTimer <= 0) {  // network cards can come and go (cable, USB adapter)
      a.adapters = listAdapters();
      a.adapterTimer = 3.0;
    }
    if (a.showLoaded) cueTick(a, dt);

    // ---- video input (a PC setting) ----
    if (a.stress) {
      if (frame == 2) {
        std::string dir = a.settingsPath.empty()
                              ? std::string("./")
                              : a.settingsPath.substr(0, a.settingsPath.size() - std::string("settings.json").size());
        // every background effect gets its buffers now, so the leak check compares like with like
        for (int i = 0; i < FX_COUNT; ++i)
          if (a.bgFx[i] && !a.bgSized[i]) {
            a.bgFx[i]->resize(a.out.w, a.out.h);
            a.bgSized[i] = true;
          }
        stressStart(a, stressMinutes, dir);
      }
      if (frame >= 2) stressTick(a, std::min(rawFrame, 1.f), rawFrame);  // real time
      if (stressFinished(a)) running = false;
    }
    a.video.configure(a.showLoaded || a.selftest ? a.st.videoIn.type : VSRC_OFF, a.st.videoIn.source);
    a.video.update(dt);

    // ---- effect switching / clearing ----
    Look& look = a.st.look;
    if (look.effect != a.curFx) switchEffect(a, look.effect);
    if (a.wantClear) {
      a.clearEffect();
      a.wantClear = false;
    }
    if (a.wantClearBg) {  // CLEAR twice: the background layer goes out (0.3 s) and turns OFF
      if (look.bg.on) {
        look.bg.on = false;
        a.bgFadeOverride = 0.3f;
      }
      a.wantClearBg = false;
    }

    // ---- tempo, fingers, release ----
    a.tempo.advance(dt, look.bpm);
    if (!a.selftest && a.st.blockGestures != a.gesturesBlocked) setGestureBlock(a.st.blockGestures);
    updateAudio(a, dt);
    if (a.showLoaded) applyDmx(a, dt);
    if (look.effect != a.curFx) switchEffect(a, look.effect);  // DMX may have changed the effect
    buildEmitters(a, dt);
    bool touching = !a.live.empty();
    if (look.effect == FX_VIDEO && look.video.alwaysOn) {  // the picture stays up without fingers
      touching = true;
      a.fading = false;
    }
    float fadeMul = 1.f;
    if (touching) {
      a.gain = 1;
      a.fading = false;
      a.holding = false;
      a.held = a.live;
    } else if (a.wasTouching) {
      if (look.releaseFx[a.curFx] == RELEASE_SNAP) {
        a.clearEffect();
      } else if (look.releaseFx[a.curFx] == RELEASE_HOLD) {
        a.holding = true;  // keeps running where the fingers were lifted, until CLEAR
      } else {
        a.fading = true;
        a.fadeElapsed = 0;
      }
    }
    a.wasTouching = touching;
    // Release fade: the effect keeps running as if the fingers were still there and the final image is
    // dimmed after tone mapping, so the fade lasts exactly the chosen time on every effect.
    a.releaseLevel = 1.f;
    if (a.fading) {
      float ft = std::max(0.05f, look.fadeFx[a.curFx]);
      a.fadeElapsed += dt;
      float t = std::min(1.f, a.fadeElapsed / ft);
      a.releaseLevel = 1.f - t * t * (3.f - 2.f * t) * 0.15f - t * 0.85f;  // even fade, soft start
      if (a.fadeElapsed >= ft) {
        a.clearEffect();
        a.releaseLevel = 1.f;
      }
    }

    FxCtx c;
    c.dt = dt;
    c.time = float(a.time);
    c.speed = look.bpm / 120.f * kSpeedMults[look.speedMult];
    c.beat = a.tempo.beat;
    c.W = a.out.w;
    c.H = a.out.h;
    c.aspect = float(c.W) / c.H;
    c.look = &look;
    c.pal = (a.dmxActive && a.dmxColors && a.st.dmx.mode >= 1) ? &a.dmxPalette
            : look.useCustom                                     ? &look.custom
                                                                 : &a.st.palettes[look.palette];
    c.live = a.live;
    c.held = a.held;
    c.touching = touching;
    if ((a.fading || a.holding) && !a.held.empty()) {
      // Release fade: the fingers stay "down" where they were lifted (their motion slowing to a stop), so
      // effects that only draw while touched keep their image alive and the fade lasts the full time.
      float k = std::exp(-dt * 2.5f);
      for (Emitter& e : a.held) {
        e.vel = e.vel * k;
        e.speed *= k;
      }
      c.held = a.held;
      c.live = a.held;
      c.touching = true;
      c.releasing = a.fading;
    }
    c.gain = a.gain;
    c.fadeMul = fadeMul;
    PostSettings post = look.post;
    if (a.st.audio.enabled && a.audio.isOpen()) {
      const AudioFeatures& f = a.af;
      float r = a.st.audio.reactivity;
      c.audioOn = true;
      c.level = f.level;
      c.bass = f.bass;
      c.mid = f.mid;
      c.high = f.high;
      c.pulse = f.pulse;
      c.onset = f.onset;
      c.spectrum = a.af.spectrum;
      c.wave = a.af.wave;
      c.drive = mixf(1.f, 0.2f + 1.5f * f.focus + 0.8f * f.pulse, r);
      // touch always gets at least the normal amount: a quiet passage must not slow down the fingers
      bool fingers = false;
      for (int i = 0; i < kMaxTouches; ++i) fingers = fingers || a.touches[i].active;
      if (fingers) c.drive = std::max(c.drive, 1.f);
      c.speed *= 1.f + r * f.focus * 0.6f;         // the chosen range pushes the motion
      post.bloom = std::min(1.f, post.bloom + r * f.pulse * 0.35f);  // glow kicks on hits
    }

    c.videoTex = a.video.hasPicture() || a.video.state() != VS_OFF ? a.video.texture() : 0;
    c.videoW = a.video.width();
    c.videoH = a.video.height();
    c.videoLevel = a.video.level();
    a.video.setWantThumb(look.colorMode == COLOR_VIDEO || (look.bg.on && look.bg.ownColors && look.bg.colorMode == COLOR_VIDEO));
    if (a.video.thumb() && a.video.state() != VS_OFF) {
      c.vidThumb = a.video.thumb();
      c.vidThumbW = a.video.thumbW();
      c.vidThumbH = a.video.thumbH();
      videoFillFit(a, c.vidFitX, c.vidFitY);
    }
    a.fx[a.curFx]->frame(c, a.scene);
    // ---- crossfade: the previous effect fades out on top of the new one ----
    if (a.prevFx >= 0) {
      float T = std::max(0.05f, a.xfadeOverride >= 0 ? a.xfadeOverride : a.st.effectFade);
      a.xfade += dt;
      float k = 1.f - a.xfade / T;
      if (k <= 0.f) {
        a.fx[a.prevFx]->clear();
        a.prevFx = -1;
        a.xfadeOverride = -1;
      } else {
        FxCtx pc = c;
        pc.live.clear();
        pc.held = a.prevHeld;
        pc.touching = false;
        pc.gain = a.prevGain * k;
        pc.fadeMul = std::pow(0.01f, dt / T);
        a.fx[a.prevFx]->frame(pc, a.scenePrev);
        fxAdd(a.scene, a.scenePrev.tex, k * k);
      }
    }
    float master = 1.f;
    if (a.dmxActive) {
      master *= a.dmxDim;
      if (a.dmxStrobeHz > 0 && a.dmxStrobePhase > 0.3) master = 0.f;  // shutter strobe
    }
    // video colours (front layer)
    VideoTint frontTint;
    if (look.colorMode == COLOR_VIDEO_MASK && a.video.texture() && a.video.state() != VS_OFF) {
      frontTint.tex = a.video.texture();
      frontTint.amount = a.video.level();
      videoFillFit(a, frontTint.fitX, frontTint.fitY);
    }
    // ---- background layer ----
    bool bgActive = renderBackground(a, c, dt);
    // a layer playing VIDEO goes out exactly like the source (the LOOK is for the effects only)
    bool frontOriginal = a.curFx == FX_VIDEO;
    bool backOriginal = a.bgCurFx == FX_VIDEO;
    // RIBBONS are flat and matte: the glow only comes in with their own Glow setting
    PostSettings postBack = post;
    if (a.curFx == FX_RIBBONS) post.bloom *= look.ribbons.glow;
    if (a.bgCurFx == FX_RIBBONS) postBack.bloom *= a.bgLook.ribbons.glow;
    if (!bgActive) {
      a.post.run(a.scene, a.out, post, a.st.output.alpha, a.st.output.premultiplied, master * a.releaseLevel,
                 &frontTint, frontOriginal);
    } else {
      const BgLayer& bg = look.bg;
      a.post.run(a.scene, a.layerFront, post, true, true, a.releaseLevel, &frontTint, frontOriginal);
      VideoTint backTint;
      int bgColorMode = bg.ownColors ? bg.colorMode : look.colorMode;
      if (bgColorMode == COLOR_VIDEO_MASK && a.video.texture() && a.video.state() != VS_OFF) {
        backTint.tex = a.video.texture();
        backTint.amount = a.video.level();
        videoFillFit(a, backTint.fitX, backTint.fitY);
      }
      a.postBg.run(a.bgScene, a.layerBack, postBack, true, true, a.bgShown * bg.level, &backTint, backOriginal);
      compositeLayers(a.layerFront, a.layerBack, a.out, bg.blend, a.st.output.alpha, a.st.output.premultiplied,
                      master);
    }
    if (a.blackout || a.dmxBlackout || !a.showLoaded) a.out.clear(0, 0, 0, a.st.output.alpha ? 0.f : 1.f);

    updateNdi(a, dt);
    if (a.st.output.spout) a.spout.send(a.st.output.spoutName, a.out.tex, a.out.w, a.out.h);
    else if (a.spout.running()) a.spout.stop();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ---- UI (the preview image is drawn by the UI) ----
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    drawUI(a);
    ImGui::Render();

    int dw, dh;
    SDL_GL_GetDrawableSize(a.window, &dw, &dh);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.055f, 0.055f, 0.065f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (a.selftest) {
      bool capture = (frame < fxFrames && frame % kStepFrames == 110) ||  // 110: inside a strobe flash at 120 BPM
                     (frame >= fxFrames && (frame - fxFrames) % 20 == 19);
      if (capture) writePpm(selftestDir + "/shot" + std::to_string(shot++) + ".ppm", dw, dh);
      if (frame >= fxFrames + kUiShots * 20 - 1) running = false;
    }
    // With an external display, its vsync paces the loop; the control screen swaps without waiting.
    SDL_GL_SetSwapInterval(a.extWindow || a.selftest ? 0 : 1);
    SDL_GL_SwapWindow(a.window);
    if (a.selftest && frame == kStepFrames * 5 - 1) g_captureExternal = selftestDir + "/external.ppm";
    renderExternal(a);
    ++frame;

    // ---- autosave (once a second, only when something changed) ----
    a.saveTimer += dt;
    if (a.saveTimer > 1.0 && !a.selftest && !a.stress && !a.settingsPath.empty()) {
      a.saveTimer = 0;
      std::string m = serializeMachine(a.st);
      if (m != a.lastSavedMachine && saveStateText(a.settingsPath, m)) a.lastSavedMachine = m;
      if (a.showLoaded) {
        std::string s = serializeShow(a.st);
        if (s != a.lastSavedShow && saveStateText(showPath(a.showName), s)) a.lastSavedShow = s;
      }
    }
    // thumbnail of the look for the launcher
    a.thumbTimer += dt;
    if (a.showLoaded && !a.selftest && !a.stress && a.thumbTimer > 15.0) {
      a.thumbTimer = 0;
      saveThumbnail(a);
    }
  }

  if (a.gesturesBlocked) {
    platformRestoreSystemGestures(a.st.gestureRestore);
    a.st.gestureRestore = -1;
  }
  if (a.stress) {
    stressFinish(a);
    if (!a.settingsPath.empty()) {
      std::string dir = a.settingsPath.substr(0, a.settingsPath.size() - std::string("settings.json").size());
      platformOpenFolder(dir);  // show the report
    }
  }
  if (!a.selftest && !a.stress && !a.settingsPath.empty()) {
    saveStateText(a.settingsPath, serializeMachine(a.st));
    if (a.showLoaded) {
      saveStateText(showPath(a.showName), serializeShow(a.st));
      saveThumbnail(a);
    }
  }
  platformPreventSleep(false);
  a.ndi.stop();
  a.spout.stop();
  a.audio.close();
  destroyExternal(a);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(a.gl);
  SDL_DestroyWindow(a.window);
  if (!a.selftest) logCleanExit();
  SDL_Quit();
  return 0;
}
