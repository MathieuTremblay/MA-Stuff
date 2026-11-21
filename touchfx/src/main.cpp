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

namespace {

constexpr int64_t kMouseTouchId = -1000;

bool inCanvas(const App& a, float x, float y) {
  return x >= a.cx && x <= a.cx + a.cw && y >= a.cy && y <= a.cy + a.ch;
}

Vec2 toUv(const App& a, float x, float y) {
  return Vec2(saturate((x - a.cx) / a.cw), saturate(1.f - (y - a.cy) / a.ch));
}

void touchDown(App& a, int64_t id, float x, float y) {
  if (!inCanvas(a, x, y)) return;
  for (auto& t : a.touches)
    if (t.active && t.id == id) return;
  for (auto& t : a.touches) {
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

void setFullscreen(App& a, bool on) {
  SDL_SetWindowFullscreen(a.window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  a.st.fullscreen = on;
}

void handleEvent(App& a, const SDL_Event& ev, bool& running) {
  ImGuiIO& io = ImGui::GetIO();
  switch (ev.type) {
    case SDL_QUIT: running = false; break;
    case SDL_WINDOWEVENT:
      if (ev.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
      break;
    case SDL_FINGERDOWN:
      touchDown(a, ev.tfinger.fingerId, ev.tfinger.x * a.winW, ev.tfinger.y * a.winH);
      break;
    case SDL_FINGERMOTION:
      touchMove(a, ev.tfinger.fingerId, ev.tfinger.x * a.winW, ev.tfinger.y * a.winH);
      break;
    case SDL_FINGERUP: touchUp(a, ev.tfinger.fingerId); break;
    // A real mouse also draws on the canvas (handy for testing without a touchscreen).
    case SDL_MOUSEBUTTONDOWN:
      if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT)
        touchDown(a, kMouseTouchId, float(ev.button.x), float(ev.button.y));
      break;
    case SDL_MOUSEMOTION:
      if (ev.motion.which != SDL_TOUCH_MOUSEID) touchMove(a, kMouseTouchId, float(ev.motion.x), float(ev.motion.y));
      break;
    case SDL_MOUSEBUTTONUP:
      if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT) touchUp(a, kMouseTouchId);
      break;
    case SDL_KEYDOWN:
      if (io.WantTextInput || ev.key.repeat) break;
      switch (ev.key.keysym.sym) {
        case SDLK_SPACE: a.tempo.tap(a.time, a.st.look.bpm); break;
        case SDLK_F11: setFullscreen(a, !a.st.fullscreen); break;
        case SDLK_ESCAPE:
          if (a.st.fullscreen) setFullscreen(a, false);
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
  a.out.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
  a.out.clear(0, 0, 0, 1);
  a.flip.create(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
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
      a.ndi.submit((const uint8_t*)p, W, H, o.fps);
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  a.pboIdx = prev;
}

void layoutCanvas(App& a) {
  float s = a.fontScale > 0 ? a.fontScale : 1.f;
  a.panelW = std::min(std::max(470.f * s, 340.f), a.winW * 0.6f);
  float m = 12.f * s;
  float areaW = std::max(50.f, a.winW - a.panelW - m * 2), areaH = std::max(50.f, a.winH - m * 2);
  float aspect = float(a.st.output.width) / a.st.output.height;
  float w = areaW, h = areaW / aspect;
  if (h > areaH) {
    h = areaH;
    w = h * aspect;
  }
  a.cw = w;
  a.ch = h;
  a.cx = m + (areaW - w) * 0.5f;
  a.cy = m + (areaH - h) * 0.5f;
}

void buildEmitters(App& a, float dt) {
  float aspect = float(a.st.output.width) / a.st.output.height;
  std::vector<Mat2> mats = mirrorMatrices(a.st.look.mirror);
  a.live.clear();
  for (int i = 0; i < 2; ++i) {
    TouchSlot& t = a.touches[i];
    if (!t.active) continue;
    Vec2 d = t.pos - t.lastPos;
    Vec2 inst(d.x * aspect / std::max(dt, 1e-3f), d.y / std::max(dt, 1e-3f));
    t.vel = t.vel + (inst - t.vel) * 0.35f;
    t.lastPos = t.pos;
    Vec2 q((t.pos.x - 0.5f) * aspect, t.pos.y - 0.5f);
    for (size_t k = 0; k < mats.size(); ++k) {
      Emitter e;
      Vec2 mq = mats[k] * q;
      e.pos = Vec2(mq.x / aspect + 0.5f, mq.y + 0.5f);
      e.vel = mats[k] * t.vel;
      e.speed = length(t.vel);
      e.touch = i;
      e.mirror = int(k);
      e.m = mats[k];
      a.live.push_back(e);
    }
  }
}

// Synthetic fingers for --selftest
void selftestTouches(App& a, int frame) {
  float t = frame / 60.f;
  int local = frame % 150;
  a.touches[0].active = !(local >= 90 && local < 105);  // exercise the release / fade path
  a.touches[0].id = 1;
  a.touches[0].pos = Vec2(0.3f + 0.12f * std::cos(t * 2.1f), 0.55f + 0.2f * std::sin(t * 2.7f));
  a.touches[1].active = frame % 240 > 40;
  a.touches[1].id = 2;
  a.touches[1].pos = Vec2(0.62f + 0.1f * std::sin(t * 1.7f), 0.35f + 0.15f * std::cos(t * 2.3f));
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
  if (a.selftest) a.st.output.ndiEnabled = true;

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
  SDL_GLContext gl = SDL_GL_CreateContext(a.window);
  if (!gl || !gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TouchFX",
                             "OpenGL 3.3 is required. Please update your graphics driver.", a.window);
    return 1;
  }
  SDL_GL_MakeCurrent(a.window, gl);
  SDL_GL_SetSwapInterval(a.selftest ? 0 : 1);
  platformSetupWindow(a.window);
  if (a.st.fullscreen && !a.selftest) setFullscreen(a, true);

  float ddpi = 0;
  if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(a.window), &ddpi, nullptr, nullptr) == 0 && ddpi > 0)
    a.dpiScale = std::min(3.f, std::max(1.f, ddpi / 96.f));

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui_ImplSDL2_InitForOpenGL(a.window, gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  glutilInit();
  bool shadersOk = fxSharedInit();
  a.fx[FX_FLUID] = makeFluid();
  a.fx[FX_PARTICLES] = makeParticles();
  a.fx[FX_KALEIDO] = makeKaleido();
  a.fx[FX_LASERS] = makeLasers();
  a.fx[FX_STROBE] = makeStrobe();
  for (auto& f : a.fx) shadersOk &= f->init();
  if (!shadersOk) SDL_Log("Some shaders failed to compile; see the log above.");

  Uint64 freq = SDL_GetPerformanceFrequency();
  Uint64 last = SDL_GetPerformanceCounter();
  bool running = true;
  int frame = 0;
  int shot = 0;

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

    float wantScale = a.dpiScale * a.st.uiScale;
    if (std::fabs(wantScale - a.fontScale) > 0.01f) rebuildFonts(a, wantScale);
    layoutCanvas(a);
    ensureOutput(a);

    if (a.selftest) {
      // 5 effects x 150 frames, then the colour / scenes / output tabs.
      int fxIndex = std::min(frame / 150, FX_COUNT - 1);
      a.st.look.effect = fxIndex;
      a.st.look.mirror = (fxIndex == FX_KALEIDO) ? MIRROR_OFF : (fxIndex == FX_LASERS ? MIRROR_QUAD : MIRROR_LR);
      a.st.look.palette = fxIndex * 2 % kNumPalettes;
      a.st.tab = frame < 750 ? 0 : 1 + (frame - 750) / 20;
      if (fxIndex == FX_STROBE && frame >= 690) a.st.look.strobe.shape = STROBE_SPARKLE;
      selftestTouches(a, frame);
    }

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
    c.pal = &a.st.palettes[look.palette];
    c.live = a.live;
    c.held = a.held;
    c.touching = touching;
    c.gain = a.gain;
    c.fadeMul = fadeMul;

    a.fx[a.curFx]->frame(c, a.out);
    if (a.blackout) a.out.clear(0, 0, 0, 1);

    updateNdi(a, dt);

    // ---- UI ----
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    drawUI(a);
    ImGui::Render();

    int dw, dh;
    SDL_GL_GetDrawableSize(a.window, &dw, &dh);
    float sx = a.winW > 0 ? float(dw) / a.winW : 1.f, sy = a.winH > 0 ? float(dh) / a.winH : 1.f;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.055f, 0.055f, 0.065f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Preview: draw the output into the canvas rectangle.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, a.out.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    int x0 = int(a.cx * sx), x1 = int((a.cx + a.cw) * sx);
    int y1 = dh - int(a.cy * sy), y0 = dh - int((a.cy + a.ch) * sy);
    glBlitFramebuffer(0, 0, a.out.w, a.out.h, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (a.selftest) {
      bool capture = frame % 150 == 149 || frame == 675 || (frame >= 750 && (frame - 750) % 20 == 19);
      if (capture) writePpm(selftestDir + "/shot" + std::to_string(shot++) + ".ppm", dw, dh);
      if (frame >= 750 + 3 * 20 - 1) running = false;
    }
    SDL_GL_SwapWindow(a.window);
    ++frame;

    // ---- autosave (once a second, only when something changed) ----
    a.saveTimer += dt;
    if (a.saveTimer > 1.0 && !a.selftest && !a.settingsPath.empty()) {
      a.saveTimer = 0;
      std::string text = serializeState(a.st);
      if (text != a.lastSaved && saveStateText(a.settingsPath, text)) a.lastSaved = text;
    }
  }

  if (!a.selftest && !a.settingsPath.empty()) saveStateText(a.settingsPath, serializeState(a.st));
  a.ndi.stop();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(a.window);
  SDL_Quit();
  return 0;
}
