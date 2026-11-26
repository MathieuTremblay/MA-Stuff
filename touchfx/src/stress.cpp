#include "stress.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

#include "app.hpp"
#include "cues.hpp"
#include "log.hpp"
#include "platform.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// from main.cpp
void robotTouchDown(App& a, int64_t id, float x, float y);
void robotTouchMove(App& a, int64_t id, float x, float y);
void robotTouchUp(App& a, int64_t id);

namespace {

using nlohmann::json;

struct Finger {
  bool down = false;
  int64_t id = 0;
  float x = 0.5f, y = 0.5f, vx = 0, vy = 0;  // canvas 0..1
  double until = 0;
};

struct Stress {
  bool on = false;
  double duration = 60, elapsed = 0;
  std::string dir;
  std::mt19937 rng{12345};
  Finger fingers[kMaxTouches];
  int64_t nextId = 5000;
  double nextAction = 1, nextRes = 40, nextVideo = 15, nextRoundTrip = 10, nextSample = 10, dmxUntil = 0, nextDmx = 20;
  double dmxAccum = 0;
  // measurements
  uint64_t frames = 0, hitches = 0;
  double worstFrame = 0;
  uint64_t winFrames = 0;
  double winTime = 0, winWorst = 0;
  double memStart = -1, memMax = 0, memEnd = 0;
  int texStart = -1, texMax = 0, texEnd = 0;
  double fpsMin = 1e9, fpsSum = 0;
  int fpsSamples = 0;
  std::map<std::string, int> actions;
  std::vector<double> memSamples;  // after the first minute
  int fuzzFiles = 0, fuzzLoaded = 0, fuzzRejected = 0;
  int roundTrips = 0, roundTripMismatch = 0;
  std::vector<std::string> notes;
  std::FILE* csv = nullptr;
  // the PC's own video input, restored between drop-out tests
  int userVideoType = 0;
  std::string userVideoSource;
  bool videoDropped = false;
  // DMX flood
#ifdef _WIN32
  SOCKET udp = INVALID_SOCKET;
#else
  int udp = -1;
#endif
  uint8_t seq = 0;
  std::array<uint8_t, 512> dmx{};
} g;

float rnd(float lo = 0.f, float hi = 1.f) { return std::uniform_real_distribution<float>(lo, hi)(g.rng); }
int irnd(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g.rng); }  // inclusive
bool chance(float p) { return rnd() < p; }

double processMemoryMB() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
    return double(pmc.PrivateUsage) / (1024.0 * 1024.0);
  return 0;
#else
  std::ifstream f("/proc/self/statm");
  long pages = 0, resident = 0;
  f >> pages >> resident;
  return double(resident) * double(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
}

int gpuFreeMB() {
  // NVIDIA (GL_NVX_gpu_memory_info) or AMD (GL_ATI_meminfo); -1 if neither
  GLint kb[4] = {-1, -1, -1, -1};
  while (glGetError() != GL_NO_ERROR) {
  }
  glGetIntegerv(0x9049, kb);  // GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
  if (glGetError() == GL_NO_ERROR && kb[0] > 0) return kb[0] / 1024;
  glGetIntegerv(0x87FC, kb);  // TEXTURE_FREE_MEMORY_ATI
  if (glGetError() == GL_NO_ERROR && kb[0] > 0) return kb[0] / 1024;
  return -1;
}

void count(const char* what) { ++g.actions[what]; }

// ---- damaged files: TouchFX must refuse them cleanly, never crash ----
void fuzzFiles(App& a) {
  namespace fs = std::filesystem;
  std::string fdir = g.dir + "stress-fuzz";
  std::error_code ec;
  fs::create_directories(fs::u8path(fdir), ec);
  AppState base = a.st;
  base.look.bg.on = true;
  base.cueLists[0].cues.push_back(Cue{1.f, "Cue", base.look, 1.f, FOLLOW_BARS, 4.f});
  std::string good = serializeShow(base);
  std::string machine = serializeMachine(base);
  const char* junk[] = {"null", "-1e308", "1e308", "\"x\"", "true", "[]", "{}", "-2147483649", "NaN", "\"\\u0000\""};
  for (int i = 0; i < 400; ++i) {
    std::string t = i % 5 == 4 ? machine : good;
    switch (i % 4) {
      case 0: t.resize(size_t(rnd() * t.size())); break;  // truncated (power cut while saving)
      case 1:
        for (int k = 0; k < 8; ++k) t[size_t(rnd() * (t.size() - 1))] = char(irnd(0, 255));  // random bytes
        break;
      case 2: {  // a number replaced by something odd
        for (int k = 0; k < 6; ++k) {
          size_t p = t.find_first_of("0123456789", size_t(rnd() * (t.size() - 1)));
          if (p == std::string::npos) break;
          size_t e = t.find_first_not_of("0123456789.e-", p);
          t.replace(p, e - p, junk[irnd(0, 9)]);
        }
        break;
      }
      case 3: {  // a chunk removed
        size_t p = size_t(rnd() * (t.size() - 1)), n = size_t(rnd() * 200);
        t.erase(p, std::min(n, t.size() - p));
        break;
      }
    }
    std::string path = fdir + "/f" + std::to_string(i) + ".tfxshow";
    {
      std::ofstream f(fs::u8path(path), std::ios::binary);
      f << t;
    }
    AppState s = a.st;
    bool ok = i % 5 == 4 ? loadMachine(path, s) : loadShow(path, s);
    ++g.fuzzFiles;
    (ok ? g.fuzzLoaded : g.fuzzRejected)++;
    if (ok) serializeShow(s);  // whatever was accepted must be usable
  }
  fs::remove_all(fs::u8path(fdir), ec);
}

// ---- random settings: every number / switch of the look, including extremes ----
void randomizeJson(json& j, int depth) {
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (it.key() == "bpm") continue;
    json& v = it.value();
    if (v.is_object() && depth < 4) randomizeJson(v, depth + 1);
    else if (v.is_number_float()) v = chance(0.15f) ? (chance(0.5f) ? 0.0 : 1.0) : double(rnd());
    else if (v.is_number_integer()) v = irnd(-1, 20);  // out of range on purpose: must be clamped
    else if (v.is_boolean()) v = chance(0.5f);
  }
}

void randomLook(App& a) {
  json j = json::parse(lookToText(a.st.look));
  randomizeJson(j, 0);
  Look l;
  if (lookFromText(j.dump(), l)) {
    l.bpm = a.st.look.bpm;
    a.st.look = l;
  }
}

// ---- DMX flood (sACN or Art-Net to this PC) ----
void dmxSend(App& a) {
  const DmxSettings& d = a.st.dmx;
  if (d.protocol == DMXP_OFF) return;
#ifdef _WIN32
  if (g.udp == INVALID_SOCKET) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    g.udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g.udp == INVALID_SOCKET) return;
  }
#else
  if (g.udp < 0) {
    g.udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (g.udp < 0) return;
  }
#endif
  // mostly random, sometimes all 0 / all 255 frames
  int kind = irnd(0, 9);
  for (int i = 0; i < 512; ++i) g.dmx[i] = kind == 0 ? 0 : kind == 1 ? 255 : (chance(0.3f) ? uint8_t(irnd(0, 255)) : g.dmx[i]);
  std::vector<uint8_t> p;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = htonl(0x7F000001);
  auto be16 = [&](int v) {
    p.push_back(uint8_t(v >> 8));
    p.push_back(uint8_t(v));
  };
  auto be32 = [&](uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) p.push_back(uint8_t(v >> s));
  };
  if (d.protocol == DMXP_SACN) {
    const int n = 512;
    be16(0x0010);
    be16(0x0000);
    const char id[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
    p.insert(p.end(), id, id + 12);
    be16(0x7000 | (110 + n));
    be32(0x00000004);
    for (int i = 0; i < 16; ++i) p.push_back(uint8_t(0xA0 + i));  // CID
    be16(0x7000 | (88 + n));
    be32(0x00000002);
    const char nm[] = "TouchFX stress test";
    for (int i = 0; i < 64; ++i) p.push_back(i < int(sizeof(nm)) ? uint8_t(nm[i]) : 0);
    p.push_back(100);  // priority
    be16(0);
    p.push_back(g.seq++);
    p.push_back(0);
    be16(d.sacnUniverse);
    be16(0x7000 | (11 + n));
    p.push_back(0x02);
    p.push_back(0xA1);
    be16(0x0000);
    be16(0x0001);
    be16(n + 1);
    p.push_back(0);
    p.insert(p.end(), g.dmx.begin(), g.dmx.end());
    to.sin_port = htons(5568);
  } else {
    const char id[8] = {'A', 'r', 't', '-', 'N', 'e', 't', 0};
    p.insert(p.end(), id, id + 8);
    p.push_back(0x00);
    p.push_back(0x50);  // OpDmx (little endian)
    be16(14);           // protocol version
    p.push_back(g.seq++);
    p.push_back(0);
    p.push_back(uint8_t(d.artnetUniverse & 0xFF));
    p.push_back(uint8_t((d.artnetUniverse >> 8) & 0x7F));
    be16(512);
    p.insert(p.end(), g.dmx.begin(), g.dmx.end());
    to.sin_port = htons(6454);
  }
  sendto(g.udp, (const char*)p.data(), int(p.size()), 0, (const sockaddr*)&to, sizeof(to));
}

// ---- the media server: play / switch clips, change their settings, move the media between layers, the window ----
void mediaAction(App& a) {
  Look& l = a.st.look;
  MediaLibrary& m = a.st.media;
  std::vector<std::string> ids = {"", kMediaLive};
  for (const MediaGroup& g : m.groups)
    for (const MediaClip& c : g.clips) ids.push_back(c.id);
  switch (irnd(0, 9)) {
    case 0: case 1: case 2:
      l.mediaClip = ids[size_t(irnd(0, int(ids.size()) - 1))];
      count("media clip");
      break;
    case 3:
      if (chance(0.5f)) {  // the media layer on BACK
        l.bg.on = true;
        l.bg.effect = FX_VIDEO;
        l.bg.fx.video.source = VIDSRC_MEDIA;
        l.bg.fx.video.mode = chance(0.7f) ? VID_CLEAN : irnd(0, VID_COUNT - 1);
        l.bg.fx.video.alwaysOn = chance(0.8f);
      } else {  // the front VIDEO effect on MEDIA
        l.effect = FX_VIDEO;
        l.video.source = VIDSRC_MEDIA;
        l.video.mode = irnd(0, VID_COUNT - 1);
      }
      count("media layer");
      break;
    case 4: case 5: case 6: {  // any clip's settings, while it plays
      ClipSettings* k = &m.liveSet;
      if (ids.size() > 2) {
        MediaClip* c = findClip(m, ids[size_t(irnd(2, int(ids.size()) - 1))]);
        if (c) k = &c->set;
      }
      switch (irnd(0, 9)) {
        case 0: k->play = irnd(0, PLAY_COUNT - 1); break;
        case 1: k->in = rnd() * 0.6f; k->out = k->in + rnd() * (1.f - k->in); break;
        case 2: k->speed = rnd() * 4.f; k->reverse = chance(0.3f); break;
        case 3: k->beatSync = chance(0.5f); k->beats = kBeatChoices[irnd(0, 6)]; break;
        case 4: k->scale = 0.05f + rnd() * 3.f; k->scaleX = 0.2f + rnd() * 2.f; k->scaleY = 0.2f + rnd() * 2.f; break;
        case 5: k->posX = rnd() * 2.f - 1.f; k->posY = rnd() * 2.f - 1.f; k->rotation = rnd() * 360.f - 180.f; break;
        case 6: k->cropL = rnd() * 0.5f; k->cropR = rnd() * 0.4f; k->cropT = rnd() * 0.4f; k->cropB = rnd() * 0.4f; break;
        case 7: k->gamma = 0.2f + rnd() * 2.8f; k->hue = rnd() * 360.f - 180.f; k->saturation = rnd() * 2.f; break;
        case 8: k->blur = chance(0.5f) ? rnd() : 0.f; break;
        default: k->transition = irnd(-1, MTRANS_COUNT - 1); k->transTime = rnd() * 2.f; break;
      }
      count("media settings");
      break;
    }
    case 7:
      m.transition = irnd(0, MTRANS_COUNT - 1);
      m.transTime = rnd() * 2.f;
      if (chance(0.3f)) a.media.restart();
      count("media transition");
      break;
    case 8:  // the MEDIA window opens / closes, with a clip in its preview
      if (a.mediaWin) a.wantCloseMediaWin = true;
      else a.wantMediaWin = true;
      a.mediaSelected = ids[size_t(irnd(0, int(ids.size()) - 1))];
      count("media window");
      break;
    default:  // a flash-style snap back
      a.media.snapNext();
      if (chance(0.5f)) a.media.resumeNext();
      l.mediaClip = ids[size_t(irnd(0, int(ids.size()) - 1))];
      count("media snap");
      break;
  }
}

// ---- one random action ----
void action(App& a) {
  Look& l = a.st.look;
  if (chance(0.12f)) {
    mediaAction(a);
    return;
  }
  int r = irnd(0, 99);
  if (r < 14) {
    l.effect = irnd(0, FX_COUNT - 1);
    if (chance(0.3f)) l.effect = FX_VIDEO;
    count("effect");
  } else if (r < 24) {
    BgLayer& bg = l.bg;
    switch (irnd(0, 7)) {
      case 0: bg.on = !bg.on; break;
      case 1: bg.effect = irnd(0, FX_COUNT - 1); break;
      case 2: bg.blend = irnd(0, BLEND_COUNT - 1); break;
      case 3: bg.level = rnd(); break;
      case 4:
        bg.source = irnd(0, BGSRC_COUNT - 1);
        bg.followTouch = bg.source != BGSRC_AUTO;
        break;
      case 5: bg.ownColors = !bg.ownColors; bg.colorMode = irnd(0, COLOR_COUNT - 1); break;
      case 6: bg.mirror = irnd(0, MIRROR_COUNT - 1); break;
      default: bg.fx.video.mode = irnd(0, VID_COUNT - 1); bg.effect = FX_VIDEO; bg.on = true; break;
    }
    count("background");
  } else if (r < 30) {
    randomLook(a);
    count("random settings");
  } else if (r < 36) {
    l.mirror = irnd(0, MIRROR_COUNT - 1);
    l.grid.mode = irnd(0, GRID_COUNT - 1);
    l.spectrum.mode = irnd(0, SPEC_COUNT - 1);
    l.ribbons.bands = irnd(2, 6);
    if (chance(0.3f)) l.effect = chance(0.5f) ? FX_SPECTRUM : (chance(0.5f) ? FX_RIBBONS : FX_GRID);
    l.grid.bulb = chance(0.5f) ? rnd() : 0.f;
    l.lasers.swingMode = irnd(0, SWING_COUNT - 1);
    if (chance(0.3f)) l.colorMode = COLOR_VIDEO;
    l.palette = irnd(0, kNumPalettes - 1);
    l.colorMode = irnd(0, COLOR_COUNT - 1);
    l.video.mode = irnd(0, VID_COUNT - 1);
    count("mirror / colours");
  } else if (r < 44) {
    int i = irnd(0, kNumScenes - 1);
    int k = irnd(0, 2);
    if (k == 0) {
      undoPush(a, "store scene");
      a.st.scenes[i].used = true;
      a.st.scenes[i].look = l;
      a.st.scenes[i].flash = chance(0.35f);
      count("scene store");
    } else if (k == 1) {
      a.recallScene(i);  // a FLASH scene starts a flash (ended below, like a released button)
      count(a.flashScene >= 0 ? "flash scene" : "scene recall");
    } else {
      undoPush(a, "delete scene");
      a.st.scenes[i] = Scene();
      count("scene delete");
    }
  } else if (r < 62) {
    switch (irnd(0, 11)) {
      case 0: case 1:
        undoPush(a, "record cue");
        cueRecord(a);
        a.st.cueLists[a.st.activeList].cues.back().fade = rnd(0.f, 3.f);
        count("cue record");
        break;
      case 2: case 3: cueCommand(a, CUE_GO); count("cue GO"); break;
      case 4:
        undoPush(a, "update cue");
        cueUpdate(a, cueUpdateTarget(a));
        count("cue update");
        break;
      case 5: cueCommand(a, CUE_BACK); count("cue BACK"); break;
      case 6: cueCommand(a, chance(0.5f) ? CUE_SELECT_NEXT : CUE_SELECT_PREV); count("cue select"); break;
      case 7: undoApply(a, chance(0.3f)); count("undo / redo"); break;
      case 8:
        undoPush(a, "song");
        if (chance(0.6f) || a.st.cueLists.size() < 2) a.st.cueLists.push_back(CueList{"Song", irnd(0, END_COUNT - 1), {}});
        else a.st.cueLists.erase(a.st.cueLists.begin() + irnd(0, int(a.st.cueLists.size()) - 1));
        cueSelectList(a, irnd(0, int(a.st.cueLists.size()) - 1));
        count("song add / delete");
        break;
      case 9: {
        CueList& cl = a.st.cueLists[a.st.activeList];
        cl.endAction = irnd(0, END_COUNT - 1);
        if (!cl.cues.empty()) {
          Cue& c = cl.cues[irnd(0, int(cl.cues.size()) - 1)];
          c.follow = irnd(0, FOLLOW_COUNT - 1);
          c.followValue = float(irnd(1, 3));
        }
        count("cue follow / end");
        break;
      }
      case 10:
        undoPush(a, "renumber");
        cueRenumber(a.st.cueLists[a.st.activeList]);
        count("renumber");
        break;
      default:
        cueCommand(a, chance(0.5f) ? CUE_SONG_NEXT : CUE_SONG_PREV);
        count("song select");
        break;
    }
  } else if (r < 72) {
    a.st.tab = irnd(0, 2);
    a.st.settingsTab = irnd(0, ST_COUNT - 1);
    a.layerEdit = irnd(0, 1);
    count("tabs");
  } else if (r < 78) {
    if (chance(0.5f)) a.wantClear = true;
    else a.blackout = chance(0.2f);
    if (chance(0.3f)) requestClear(a);
    count("clear / blackout");
  } else if (r < 84) {
    l.releaseFx[l.effect] = irnd(0, 2);
    if (chance(0.1f)) a.video.setFrozen(!a.video.frozen());
    l.fadeFx[l.effect] = rnd(0.1f, 6.f);
    a.st.effectFade = rnd(0.f, 2.f);
    count("release / crossfade");
  } else if (r < 88) {
    a.st.output.alpha = chance(0.3f);
    a.st.output.premultiplied = chance(0.5f);
    count("alpha");
  } else if (r < 92) {
    l.speedMult = irnd(0, kNumSpeedMults - 1);
    l.bpm = float(irnd(60, 180));
    count("tempo");
  } else if (r < 95) {
    a.st.audio.enabled = chance(0.5f);
    a.st.audio.device = "@test";
    a.st.audio.autoPlay = chance(0.5f);
    a.st.audio.points = irnd(1, kMaxAutoPoints);
    a.st.audio.range = irnd(0, RANGE_COUNT - 1);
    a.st.audio.rangeLo = float(irnd(20, 8000));
    a.st.audio.rangeHi = a.st.audio.rangeLo + float(irnd(10, 8000));
    if (chance(0.2f)) a.audio.shiftOctave(chance(0.5f) ? 0.5f : 2.f);
    count("audio");
  } else {
    a.st.maxTouches = irnd(1, kMaxTouches);
    count("max fingers");
  }
}

void fingersTick(App& a, float dt) {
  int want = int(rnd() < 0.02f ? irnd(0, kMaxTouches) : -1);
  for (int i = 0; i < kMaxTouches; ++i) {
    Finger& f = g.fingers[i];
    float px = a.canvas.x + f.x * a.canvas.w, py = a.canvas.y + (1.f - f.y) * a.canvas.h;
    if (!f.down) {
      if (g.elapsed >= f.until && (want < 0 || i < want)) {
        f.down = true;
        f.id = g.nextId++;
        f.x = rnd(0.05f, 0.95f);
        f.y = rnd(0.05f, 0.95f);
        f.until = g.elapsed + rnd(0.2f, 6.f);
        robotTouchDown(a, f.id, a.canvas.x + f.x * a.canvas.w, a.canvas.y + (1.f - f.y) * a.canvas.h);
      }
      continue;
    }
    if (g.elapsed >= f.until || (want >= 0 && i >= want)) {
      robotTouchUp(a, f.id);
      f.down = false;
      f.until = g.elapsed + rnd(0.1f, 3.f);
      continue;
    }
    // wander, sometimes flick fast
    f.vx += rnd(-1.f, 1.f) * dt * (chance(0.01f) ? 60.f : 4.f);
    f.vy += rnd(-1.f, 1.f) * dt * (chance(0.01f) ? 60.f : 4.f);
    f.vx *= 0.97f;
    f.vy *= 0.97f;
    f.x += f.vx * dt;
    f.y += f.vy * dt;
    if (f.x < 0.01f || f.x > 0.99f) f.vx = -f.vx;
    if (f.y < 0.01f || f.y > 0.99f) f.vy = -f.vy;
    f.x = std::clamp(f.x, 0.01f, 0.99f);
    f.y = std::clamp(f.y, 0.01f, 0.99f);
    (void)px;
    (void)py;
    robotTouchMove(a, f.id, a.canvas.x + f.x * a.canvas.w, a.canvas.y + (1.f - f.y) * a.canvas.h);
  }
}

void sample(App& a) {
  double mem = processMemoryMB();
  int tex = glLiveTargets();
  double fps = g.winTime > 0 ? g.winFrames / g.winTime : 0;
  // measurements start after the first minute (everything is created by then)
  if (g.elapsed >= std::min(60.0, g.duration * 0.25)) {
    if (g.memStart < 0) {
      g.memStart = mem;
      g.texStart = tex;
    }
    g.memMax = std::max(g.memMax, mem);
    g.texMax = std::max(g.texMax, tex);
    g.fpsMin = std::min(g.fpsMin, fps);
    g.fpsSum += fps;
    ++g.fpsSamples;
    g.memSamples.push_back(mem);
  }
  g.memEnd = mem;
  g.texEnd = tex;
  if (g.csv) {
    std::fprintf(g.csv, "%.0f,%.1f,%.1f,%llu,%.1f,%d,%d,%d,%.1f,%d,%s,%s,%dx%d\n", g.elapsed, fps, g.winWorst * 1000.0,
                 (unsigned long long)g.hitches, mem, tex, gpuFreeMB(), a.video.state(), a.video.framesPerSecond(),
                 a.st.look.bg.on ? 1 : 0, effectName(a.st.look.effect), effectName(a.st.look.bg.effect), a.out.w, a.out.h);
    std::fflush(g.csv);
  }
  logLine("Stress %.0f s: %.1f fps (worst frame %.0f ms), memory %.0f MB, %d GPU buffers, video %d", g.elapsed, fps,
          g.winWorst * 1000.0, mem, tex, a.video.state());
  g.winFrames = 0;
  g.winTime = 0;
  g.winWorst = 0;
}

}  // namespace

void stressStart(App& a, double minutes, const std::string& reportDir) {
  g = Stress();
  g.on = true;
  g.duration = std::max(0.5, minutes) * 60.0;
  g.dir = reportDir;
  g.rng.seed(uint32_t(std::time(nullptr)));
  g.userVideoType = a.st.videoIn.type;
  g.userVideoSource = a.st.videoIn.source;
  a.st.maxTouches = kMaxTouches;
  g.csv = std::fopen((reportDir + "stress-data.csv").c_str(), "w");
  if (g.csv)
    std::fprintf(g.csv, "seconds,fps,worst_frame_ms,hitches_total,memory_mb,gpu_buffers,gpu_free_mb,video_state,"
                        "video_fps,background,effect,background_effect,resolution\n");
  logLine("Stress test started: %.1f minutes", minutes);
  // media: clips from TOUCHFX_TEST_MEDIA (files separated by ';') are converted into a scratch folder
  a.importer.setRoot(reportDir + "stress-media");
  a.st.media = MediaLibrary();
  a.st.media.groups.push_back(MediaGroup{"Stress", {}});
  if (const char* mv = std::getenv("TOUCHFX_TEST_MEDIA")) {
    std::vector<std::string> files;
    std::string v = mv;
    size_t p = 0;
    while (p <= v.size()) {
      size_t q = v.find(';', p);
      if (q == std::string::npos) q = v.size();
      if (q > p) files.push_back(v.substr(p, q - p));
      p = q + 1;
    }
    a.importer.import(a.st.media, 0, files);
  }
  fuzzFiles(a);
  logLine("Stress: %d damaged files tried, %d refused, %d loaded and cleaned up", g.fuzzFiles, g.fuzzRejected,
          g.fuzzLoaded);
}

void stressTick(App& a, float dt, float frameSeconds) {
  if (!g.on) return;
  g.elapsed += dt;
  ++g.frames;
  ++g.winFrames;
  g.winTime += frameSeconds;
  g.winWorst = std::max(g.winWorst, double(frameSeconds));
  if (g.elapsed > 30) {  // after start-up
    g.worstFrame = std::max(g.worstFrame, double(frameSeconds));
    if (frameSeconds > 0.1f) ++g.hitches;
  }

  fingersTick(a, dt);
  if (a.flashScene >= 0 && !a.flashFromDmx && rnd() < dt * 0.8f) a.endFlash();  // the robot lets go of a FLASH
  if (g.elapsed >= g.nextAction) {
    action(a);
    g.nextAction = g.elapsed + (chance(0.1f) ? rnd(0.02f, 0.2f) : rnd(0.3f, 2.5f));  // sometimes a burst
  }
  // output resolution / frame rate changes
  if (g.elapsed >= g.nextRes) {
    static const int res[][2] = {{1920, 1080}, {1280, 720}, {960, 540}, {1366, 768}, {800, 600}, {1080, 1080}, {640, 360}};
    int i = irnd(0, 6);
    a.st.output.width = res[i][0];
    a.st.output.height = res[i][1];
    a.st.output.fps = chance(0.5f) ? 60 : 30;
    g.nextRes = g.elapsed + rnd(30.f, 90.f);
    count("resolution");
  }
  // video: the PC's own input drops out now and then; without one, the test sources take turns
  if (g.elapsed >= g.nextVideo) {
    VideoInSettings& v = a.st.videoIn;
    if (g.userVideoType != VSRC_OFF && g.userVideoType != VSRC_TEST) {
      if (!g.videoDropped) {
        v.type = VSRC_OFF;
        g.videoDropped = true;
        g.nextVideo = g.elapsed + rnd(3.f, 8.f);
      } else {
        v.type = g.userVideoType;
        v.source = g.userVideoSource;
        g.videoDropped = false;
        g.nextVideo = g.elapsed + rnd(40.f, 120.f);
      }
    } else {
      // test pictures, a source that hangs like a stuck driver, NDI, a capture card (none here: an error)
      static const char* srcs[] = {"Colour bars", "Colour bars UYVY", "Flaky (drops out)", "Hang (test)"};
      int k = irnd(0, 6);
      v.type = k < 4 ? VSRC_TEST : k == 4 ? VSRC_NDI : k == 5 ? VSRC_CAPTURE : VSRC_OFF;
      v.source = k < 4 ? srcs[k] : "";
      if (chance(0.5f)) a.video.requestSourceList(v.type);
      g.nextVideo = g.elapsed + rnd(4.f, 20.f);
    }
    count("video source");
  }
  // DMX floods: 10-20 s of random frames at 40 Hz every minute or two (only when DMX listens on all cards)
  if (g.elapsed >= g.nextDmx && a.st.dmx.protocol != DMXP_OFF && a.st.dmxAdapter.empty()) {
    g.dmxUntil = g.elapsed + rnd(10.f, 20.f);
    a.st.dmx.mode = chance(0.6f) ? 2 : irnd(0, 1);  // mostly the Full v2 profile
    g.nextDmx = g.elapsed + rnd(25.f, 60.f);
    count("DMX flood");
  }
  if (g.elapsed < g.dmxUntil) {
    g.dmxAccum += dt;
    while (g.dmxAccum > 0.025) {
      g.dmxAccum -= 0.025;
      dmxSend(a);
    }
  }
  // show file round trip: what is saved must load back identical
  if (g.elapsed >= g.nextRoundTrip) {
    std::string text = serializeShow(a.st);
    std::string path = g.dir + "stress-roundtrip.tfxshow";
    saveStateText(path, text);
    AppState s = a.st;
    ++g.roundTrips;
    if (!loadShow(path, s) || serializeShow(s) != text) {
      ++g.roundTripMismatch;
      // name the first setting that changed
      std::string where = "?";
      std::function<bool(const json&, const json&, const std::string&)> diff = [&](const json& x, const json& y,
                                                                                    const std::string& at) {
        if (x.type() != y.type()) { where = at; return true; }
        if (x.is_object()) {
          for (auto it = x.begin(); it != x.end(); ++it)
            if (!y.contains(it.key()) || diff(it.value(), y[it.key()], at + "/" + it.key())) return true;
          return false;
        }
        if (x.is_array()) {
          for (size_t i = 0; i < x.size() && i < y.size(); ++i)
            if (diff(x[i], y[i], at + "/" + std::to_string(i))) return true;
          if (x.size() != y.size()) { where = at; return true; }
          return false;
        }
        if (x != y) { where = at + " " + x.dump() + " -> " + y.dump(); return true; }
        return false;
      };
      try {
        diff(json::parse(text), json::parse(serializeShow(s)), "");
      } catch (...) {
      }
      if (g.notes.size() < 20)
        g.notes.push_back("Show file round trip changed " + where + " at " + std::to_string(int(g.elapsed)) + " s");
    }
    std::remove(path.c_str());
    g.nextRoundTrip = g.elapsed + 15;
  }
  if (g.elapsed >= g.nextSample) {
    sample(a);
    g.nextSample = g.elapsed + 10;
  }
}

bool stressFinished(const App&) { return g.on && g.elapsed >= g.duration; }

void stressFinish(App& a) {
  if (!g.on) return;
  sample(a);
  g.on = false;
  if (g.csv) std::fclose(g.csv);
  g.csv = nullptr;
#ifdef _WIN32
  if (g.udp != INVALID_SOCKET) closesocket(g.udp);
#else
  if (g.udp >= 0) close(g.udp);
#endif
  // typical memory (median) in the middle third of the run vs the last third: peaks from resolution changes
  // come and go, a leak moves the whole band up
  auto median = [](std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  double growth = 0, midMed = 0, endMed = 0;
  size_t n = g.memSamples.size();
  if (n >= 6) {
    midMed = median(std::vector<double>(g.memSamples.begin() + n / 3, g.memSamples.begin() + 2 * n / 3));
    endMed = median(std::vector<double>(g.memSamples.begin() + 2 * n / 3, g.memSamples.end()));
    growth = endMed - midMed;
  }
  int texGrowth = g.texStart >= 0 ? g.texEnd - g.texStart : 0;
  bool warnMem = growth > 100.0, warnTex = texGrowth > 8, fail = g.roundTripMismatch > 0;
  std::ostringstream o;
  char b[512];
  std::time_t now = std::time(nullptr);
  char when[64];
  std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&now));
  o << "TouchFX " << TOUCHFX_VERSION << " stress test - " << when << "\n\n";
  o << "RESULT: " << (fail ? "PROBLEM FOUND" : (warnMem || warnTex) ? "CHECK (see warnings)" : "PASS") << "\n\n";
  std::snprintf(b, sizeof(b), "Duration            %.1f minutes, %llu frames\n", g.elapsed / 60.0, (unsigned long long)g.frames);
  o << b;
  std::snprintf(b, sizeof(b), "Frame rate          average %.1f fps, lowest 10 s average %.1f fps\n",
                g.fpsSamples ? g.fpsSum / g.fpsSamples : 0.0, g.fpsSamples ? g.fpsMin : 0.0);
  o << b;
  std::snprintf(b, sizeof(b), "Slowest frame       %.0f ms (frames over 100 ms: %llu)\n", g.worstFrame * 1000.0,
                (unsigned long long)g.hitches);
  o << b;
  std::snprintf(b, sizeof(b),
                "Memory              typical %.0f MB mid-run, %.0f MB at the end (%+.0f MB), %.0f MB peak%s\n",
                midMed, endMed, growth, g.memMax, warnMem ? "  <- WARNING: keeps growing" : "");
  o << b;
  std::snprintf(b, sizeof(b), "GPU buffers         %d after the first minute, %d at the end, %d max%s\n", g.texStart, g.texEnd,
                g.texMax, warnTex ? "  <- WARNING: keeps growing" : "");
  o << b;
  std::snprintf(b, sizeof(b), "Damaged show files  %d tried: %d refused, %d loaded and cleaned up, no crash\n", g.fuzzFiles,
                g.fuzzRejected, g.fuzzLoaded);
  o << b;
  std::snprintf(b, sizeof(b), "Save / load checks  %d, %d mismatches\n", g.roundTrips, g.roundTripMismatch);
  o << b;
  o << "\nActions performed:\n";
  for (auto& kv : g.actions) {
    std::snprintf(b, sizeof(b), "  %-22s %d\n", kv.first.c_str(), kv.second);
    o << b;
  }
  for (const std::string& n : g.notes) o << "NOTE: " << n << "\n";
  o << "\nMeasurements every 10 s: stress-data.csv (open with Excel). Log: touchfx.log.\n";
  std::string path = g.dir + "stress-report.txt";
  std::ofstream(std::filesystem::u8path(path)) << o.str();
  logLine("Stress test finished: %s", fail ? "PROBLEM FOUND" : (warnMem || warnTex) ? "CHECK" : "PASS");
  std::fprintf(stderr, "%s", o.str().c_str());
  (void)a;
}
