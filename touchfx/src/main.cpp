// TouchFX - touch-driven live visuals with NDI output.
#include <SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "platform.hpp"

void App::clearEffect() {
  if (curFx >= 0) fx[curFx]->clear();
  fading = false;
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

void App::recallScene(int i) {
  if (i < 0 || i >= kNumScenes || !st.scenes[i].used) return;
  st.look = st.scenes[i].look;
  lastScene = i;
}

namespace {

constexpr int64_t kMouseTouchId = -1000;
std::string g_captureExternal;  // selftest: save the external window on its next frame
bool writePpm(const std::string& path, int w, int h);

Vec2 toUv(const App& a, float x, float y) {
  return Vec2(saturate((x - a.canvas.x) / a.canvas.w), saturate(1.f - (y - a.canvas.y) / a.canvas.h));
}

void touchDown(App& a, int64_t id, float x, float y) {
  if (!a.canvas.contains(x, y)) return;
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
    case SDL_QUIT: running = false; break;
    case SDL_WINDOWEVENT:
      if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
        if (ev.window.windowID == mainId) running = false;
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
      switch (ev.key.keysym.sym) {
        case SDLK_SPACE: a.tempo.tap(a.time, a.st.look.bpm); break;
        case SDLK_F11: setFullscreen(a, !a.isFullscreen); break;
        case SDLK_ESCAPE:
          if (a.isFullscreen) setFullscreen(a, false);
          break;
        case SDLK_c: a.wantClear = true; break;
        case SDLK_b: a.blackout = !a.blackout; break;
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
  presentTexture(a.out.tex, u0, v0, u1, v1, a.st.output.alpha && !a.st.output.premultiplied);
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

void addEmitters(App& a, const std::vector<Mat2>& mats, float aspect, Vec2 pos, Vec2 vel, int touchIndex) {
  Vec2 q((pos.x - 0.5f) * aspect, pos.y - 0.5f);
  for (size_t k = 0; k < mats.size() && int(a.live.size()) < kMaxSplats; ++k) {
    Emitter e;
    Vec2 mq = mats[k] * q;
    e.pos = Vec2(mq.x / aspect + 0.5f, mq.y + 0.5f);
    e.vel = mats[k] * vel;
    e.speed = length(vel);
    e.touch = touchIndex;
    e.mirror = int(k);
    e.m = mats[k];
    a.live.push_back(e);
  }
}

// Audio auto-play: virtual fingers that wander over the canvas, pushed along by the music.
void updateVirtualPoints(App& a, float dt, float aspect, const std::vector<Mat2>& mats) {
  const AudioSettings& as = a.st.audio;
  a.vActive = as.enabled && as.autoPlay && a.af.active;
  if (!a.vActive) return;
  float energy = 0.35f + a.af.level * 1.1f + a.af.pulse * 0.6f * as.reactivity;
  for (int k = 0; k < as.points; ++k) {
    App::VirtualPoint& v = a.vpts[k];
    v.phase += dt * (0.2f + as.motion * 1.6f) * energy;
    if (a.af.onset) v.phase += 0.25f * as.reactivity * as.motion;
    float ph = v.phase + k * 2.1f;
    Vec2 p(0.5f + 0.36f * std::cos(ph * 1.0f) * (0.65f + 0.35f * std::sin(ph * 0.37f + k)),
           0.5f + 0.32f * std::sin(ph * 1.37f + k * 1.3f));
    Vec2 d = p - v.pos;
    Vec2 inst(d.x * aspect / std::max(dt, 1e-3f), d.y / std::max(dt, 1e-3f));
    if (length(d) > 0.2f) inst = Vec2();  // first frame: no jump velocity
    v.vel = v.vel + (inst - v.vel) * 0.35f;
    v.pos = p;
    // louder music = faster smoke / particle bursts
    addEmitters(a, mats, aspect, v.pos, v.vel * (0.6f + a.af.level * 1.5f), kMaxTouches + k);
  }
}

void buildEmitters(App& a, float dt) {
  float aspect = float(a.st.output.width) / a.st.output.height;
  std::vector<Mat2> mats = mirrorMatrices(a.st.look.mirror);
  a.live.clear();
  updateVirtualPoints(a, dt, aspect, mats);
  // DMX cursors (Advanced mode): the console moves them with Pan/Tilt.
  for (int k = 0; k < 2; ++k) {
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
  if (!as.enabled) {
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
  a.audio.update(dt, as.sensitivity);
  a.af = a.audio.features();
  // Follow the music's tempo and line the beat up with the hits.
  if (as.autoBpm && a.af.onset && !a.dmxBpm) {
    Look& l = a.st.look;
    if (a.af.bpm > 0) l.bpm = std::round((l.bpm + (a.af.bpm - l.bpm) * 0.3f) * 10.f) / 10.f;
    double b = a.tempo.beat, nearest = std::floor(b + 0.5);
    a.tempo.beat = b + (nearest - b) * 0.5;
  }
}

// ---- developer self-test: fake fingers, every effect, screenshots ----
struct SelftestStep {
  int effect, laserMode, mirror, palette, fingers;
  bool audio = false;
  int dmx = 0;  // 1: sACN Advanced @1 (universe 100), 2: Art-Net Basic @101 (universe 99) - see tools/dmx_test_sender.py
};
const SelftestStep kSteps[] = {
    {FX_FLUID, 0, MIRROR_LR, 0, 2},          {FX_PARTICLES, 0, MIRROR_LR, 2, 2},
    {FX_SPARKLES, 0, MIRROR_QUAD, 7, 2},     {FX_KALEIDO, 0, MIRROR_OFF, 4, 2},
    {FX_LASERS, LASER_FAN, MIRROR_LR, 6, 2}, {FX_LASERS, LASER_AIM, MIRROR_OFF, 1, 2},
    {FX_LASERS, LASER_TUNNEL, MIRROR_LR, 3, 2}, {FX_LASERS, LASER_WAVE, MIRROR_OFF, 6, 3},
    {FX_STROBE, 0, MIRROR_LR, 1, 6},
    {FX_RIPPLES, 0, MIRROR_LR, 0, 2},          {FX_RIBBONS, 0, MIRROR_LR, 1, 2},
    {FX_GEOMETRY, 0, MIRROR_QUAD, 6, 2},       {FX_TUNNEL, 0, MIRROR_OFF, 4, 1},
    {FX_LIGHTNING, 0, MIRROR_OFF, 5, 3},       {FX_PLEXUS, 0, MIRROR_LR, 5, 2},
    {FX_FIREWORKS, 0, MIRROR_OFF, 2, 3},       {FX_STARFIELD, 0, MIRROR_OFF, 6, 1},
    {FX_SPECTRUM, 0, MIRROR_LR, 1, 0, true},   {FX_GRID, 0, MIRROR_LR, 7, 2},
    {FX_STROBE, STROBE_SHOCKWAVE, MIRROR_LR, 3, 2},
    {FX_PARTICLES, 0, MIRROR_LR, 4, 0, true},    {FX_LASERS, LASER_TUNNEL, MIRROR_QUAD, 6, 0, true},
    {FX_FLUID, 0, MIRROR_LR, 0, 0, false, 1},    {FX_FLUID, 0, MIRROR_LR, 0, 2, false, 2},
};
constexpr int kStepFrames = 120;
constexpr int kNumSteps = int(sizeof(kSteps) / sizeof(kSteps[0]));
constexpr int kUiShots = 6;  // live (no strip), settings x4, live with panel left

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

int main(int argc, char** argv) {
  App a;
  std::string selftestDir;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--selftest" && i + 1 < argc) selftestDir = argv[++i];
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
  bool loaded = !a.selftest && !a.settingsPath.empty() && loadState(a.settingsPath, a.st);
  a.lastSaved = serializeState(a.st);
  if (!loaded && !a.selftest && !a.settingsPath.empty()) {
    // Keep an unreadable settings file aside instead of silently overwriting it.
    if (FILE* f = std::fopen(a.settingsPath.c_str(), "rb")) {
      std::fclose(f);
      std::string bad = a.settingsPath + ".bad";
      std::remove(bad.c_str());
      std::rename(a.settingsPath.c_str(), bad.c_str());
    }
    saveStateText(a.settingsPath, a.lastSaved);
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
    if (!a.settingsPath.empty()) {
      a.lastSaved = serializeState(a.st);
      saveStateText(a.settingsPath, a.lastSaved);  // save now so a crash can't lose the original value
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
  bool shadersOk = fxSharedInit() && a.post.init() && presentInit();
  a.fx[FX_FLUID] = makeFluid();
  a.fx[FX_PARTICLES] = makeParticles();
  a.fx[FX_KALEIDO] = makeKaleido();
  a.fx[FX_LASERS] = makeLasers();
  a.fx[FX_STROBE] = makeStrobe();
  a.fx[FX_SPARKLES] = makeSparkles();
  a.fx[FX_RIPPLES] = makeRipples();
  a.fx[FX_RIBBONS] = makeRibbons();
  a.fx[FX_GEOMETRY] = makeGeometry();
  a.fx[FX_TUNNEL] = makeTunnel();
  a.fx[FX_LIGHTNING] = makeLightning();
  a.fx[FX_PLEXUS] = makePlexus();
  a.fx[FX_FIREWORKS] = makeFireworks();
  a.fx[FX_STARFIELD] = makeStarfield();
  a.fx[FX_SPECTRUM] = makeSpectrum();
  a.fx[FX_GRID] = makeGrid();
  for (auto& f : a.fx) shadersOk &= f->init();
  if (!shadersOk) SDL_Log("Some shaders failed to compile; see the log above.");

  Uint64 freq = SDL_GetPerformanceFrequency();
  Uint64 last = SDL_GetPerformanceCounter();
  bool running = true;
  int frame = 0;
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
        a.st.audio.enabled = sp.audio;
        a.st.audio.device = kAudioTest;
        a.st.audio.points = 2;
        a.st.output.alpha = frame >= kStepFrames * 7;  // test the alpha path on the last steps
        a.st.tab = 0;
        selftestTouches(a, frame, sp.fingers);
      } else {
        int k = (frame - fxFrames) / 20;
        a.st.audio.enabled = true;  // so the UI shots show the audio controls
        a.st.dmx.protocol = 1;
        a.st.layout.sceneStrip = k != 0;
        a.st.tab = (k >= 1 && k <= 4) ? 1 : 0;
        static const int tabsShown[4] = {1, 3, 4, 5};  // output, audio, dmx, system
        a.st.settingsTab = tabsShown[std::clamp(k - 1, 0, 3)];
        a.st.layout.panelLeft = k == 5;
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

    // ---- effect switching / clearing ----
    Look& look = a.st.look;
    if (look.effect != a.curFx) {
      a.curFx = look.effect;
      a.fx[a.curFx]->clear();
      a.fading = false;
      a.held.clear();
    }
    if (a.wantClear) {
      a.clearEffect();
      a.wantClear = false;
    }

    // ---- tempo, fingers, release ----
    a.tempo.advance(dt, look.bpm);
    if (!a.selftest && a.st.blockGestures != a.gesturesBlocked) setGestureBlock(a.st.blockGestures);
    updateAudio(a, dt);
    applyDmx(a, dt);
    if (look.effect != a.curFx) {  // DMX may have changed the effect
      a.curFx = look.effect;
      a.fx[a.curFx]->clear();
      a.fading = false;
      a.held.clear();
    }
    buildEmitters(a, dt);
    bool touching = !a.live.empty();
    float fadeMul = 1.f;
    if (touching) {
      a.gain = 1;
      a.fading = false;
      a.held = a.live;
    } else if (a.wasTouching) {
      if (look.release == RELEASE_SNAP) {
        a.clearEffect();
      } else {
        a.fading = true;
        a.fadeElapsed = 0;
      }
    }
    a.wasTouching = touching;
    if (a.fading) {
      float ft = std::max(0.05f, look.fadeTime);
      fadeMul = std::pow(0.001f, dt / ft);  // -60 dB over the fade time
      a.gain *= fadeMul;
      a.fadeElapsed += dt;
      if (a.fadeElapsed >= ft) a.clearEffect();
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
    c.pal = (a.dmxActive && a.dmxColors && a.st.dmx.mode == 1) ? &a.dmxPalette : &a.st.palettes[look.palette];
    c.live = a.live;
    c.held = a.held;
    c.touching = touching;
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
      c.drive = mixf(1.f, 0.2f + 1.5f * f.level + 0.8f * f.pulse, r);
      c.speed *= 1.f + r * f.bass * 0.6f;          // bass pushes the motion
      post.bloom = std::min(1.f, post.bloom + r * f.pulse * 0.35f);  // glow kicks on hits
    }

    a.fx[a.curFx]->frame(c, a.scene);
    float master = 1.f;
    if (a.dmxActive) {
      master = a.dmxDim;
      if (a.dmxStrobeHz > 0 && a.dmxStrobePhase > 0.3) master = 0.f;  // shutter strobe
    }
    a.post.run(a.scene, a.out, post, a.st.output.alpha, a.st.output.premultiplied, master);
    if (a.blackout || a.dmxBlackout) a.out.clear(0, 0, 0, a.st.output.alpha ? 0.f : 1.f);

    updateNdi(a, dt);

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
    if (a.saveTimer > 1.0 && !a.selftest && !a.settingsPath.empty()) {
      a.saveTimer = 0;
      std::string text = serializeState(a.st);
      if (text != a.lastSaved && saveStateText(a.settingsPath, text)) a.lastSaved = text;
    }
  }

  if (a.gesturesBlocked) {
    platformRestoreSystemGestures(a.st.gestureRestore);
    a.st.gestureRestore = -1;
  }
  if (!a.selftest && !a.settingsPath.empty()) saveStateText(a.settingsPath, serializeState(a.st));
  a.ndi.stop();
  a.audio.close();
  destroyExternal(a);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(a.gl);
  SDL_DestroyWindow(a.window);
  SDL_Quit();
  return 0;
}
