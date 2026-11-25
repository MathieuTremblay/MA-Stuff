// Ribbons: each finger paints a wide, flat brush stroke made of parallel colour bands (2 to 6, colours from
// the palette: low, mid, high, repeating) - a retro striped look. The stroke follows the finger in smooth
// curves; its tail flows gently in a wave, then fades out after the Length time. Matte by default, Glow adds
// a soft halo.
#include <algorithm>
#include <cmath>
#include <map>

#include "effects.hpp"

namespace {

const char* kRibbonVS = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in float aAcross;
layout(location = 2) in float aAlpha;
uniform vec2 uRes;
out float vAcross; out float vAlpha;
void main() {
  gl_Position = vec4(aPos / uRes * 2.0 - 1.0, 0.0, 1.0);
  vAcross = aAcross; vAlpha = aAlpha;
}
)";

// Bands: vAcross runs -1..1 across the stroke. Halo pass (uHalo = 1): a soft wide glow under the stroke.
const char* kRibbonFS = R"(
in float vAcross; in float vAlpha;
out vec4 o;
uniform vec3 uCols[6]; uniform int uBands; uniform float uGap; uniform int uHalo; uniform float uGlow;
uniform float uAA;
void main() {
  if (uHalo == 1) {
    float g = 1.0 - abs(vAcross);
    vec3 avg = vec3(0.0);
    for (int i = 0; i < 6; ++i) if (i < uBands) avg += uCols[i];
    avg /= float(uBands);
    o = vec4(avg * g * g * uGlow * vAlpha, 0.0);
    return;
  }
  float x = (vAcross * 0.5 + 0.5) * float(uBands);
  int b = clamp(int(floor(x)), 0, uBands - 1);
  float f = fract(x);
  // a thin dark line between the bands, anti-aliased edges
  float aa = uAA * float(uBands);
  float inBand = smoothstep(uGap * 0.5, uGap * 0.5 + aa, f) * (1.0 - smoothstep(1.0 - uGap * 0.5 - aa, 1.0 - uGap * 0.5, f));
  float edge = 1.0 - smoothstep(1.0 - uAA * 2.0, 1.0, abs(vAcross));
  vec3 c = uCols[0];
  for (int i = 1; i < 6; ++i) if (i == b) c = uCols[i];
  float a = vAlpha * edge;
  o = vec4(c * inBand * a, a);  // premultiplied: later strokes cover earlier ones
}
)";

struct Stroke {
  struct Pt {
    Vec2 p;  // pixels
    float born;
  };
  std::vector<Pt> pts;  // oldest first, the last one is the finger
  Vec3 cols[6];
  float phase = 0;
  bool seen = false;
  int order = 0;  // creation order, newer strokes are drawn on top
};

class Ribbons : public Effect {
 public:
  bool init() override {
    bool ok = prog.build("ribbons", kRibbonVS, kRibbonFS);
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei st = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, st, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    return ok;
  }
  void resize(int W, int H) override { canvas.create(W, H, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR); }
  void clear() override {
    strokes.clear();
    canvas.clear();
  }

  void frame(const FxCtx& c, const Target& out) override {
    const RibbonSettings& s = c.look->ribbons;
    float dt = std::min(c.dt, 0.05f);
    float px = c.H / 1080.f;
    clock += dt;
    float life = mixf(0.4f, 6.f, s.length * s.length);  // seconds a piece of the stroke stays
    float width = mixf(30.f, 320.f, s.width) * px;
    int bands = std::clamp(s.bands, 2, 6);
    float step = std::max(3.f * px, width * 0.06f);  // a new point every few pixels of finger travel

    // ---- follow the fingers ----
    for (auto& kv : strokes) kv.second.seen = false;
    for (const Emitter& e : c.live) {
      int key = e.touch * 64 + e.mirror;
      Stroke& st = strokes[key];
      Vec2 p(e.pos.x * c.W, e.pos.y * c.H);
      if (st.pts.empty()) {
        st.phase = hash01(uint32_t(key) * 2654435761u + uint32_t(clock * 10.f)) * kTau;
        st.order = ++orderCounter;
        st.pts.push_back({p, clock});
        st.pts.push_back({p, clock});
      }
      // the head follows the finger; a point is left behind every `step` pixels
      Stroke::Pt& head = st.pts.back();
      Vec2 last = st.pts[st.pts.size() - 2].p;
      head.p = p;
      head.born = clock;
      if (length(p - last) >= step && st.pts.size() < 2000) st.pts.push_back({p, clock});
      // band colours: palette low, mid, high, repeating (the colour mode applies)
      static const float kT[3] = {0.f, 0.5f, 1.f};
      for (int b = 0; b < bands; ++b) st.cols[b] = c.color(c.colorT(&e, kT[b % 3], hash01(uint32_t(key) * 31u + uint32_t(b))));
      st.seen = true;
    }
    // age out the tail; a released stroke stays until its whole length has faded
    for (auto it = strokes.begin(); it != strokes.end();) {
      Stroke& st = it->second;
      size_t drop = 0;
      while (drop + 2 < st.pts.size() && clock - st.pts[drop + 1].born > life) ++drop;
      if (drop) st.pts.erase(st.pts.begin(), st.pts.begin() + long(drop));
      bool gone = !st.seen && (st.pts.empty() || clock - st.pts.back().born > life);
      if (gone) it = strokes.erase(it);
      else ++it;
    }

    // ---- draw: oldest strokes first ----
    std::vector<const Stroke*> order;
    for (auto& kv : strokes) order.push_back(&kv.second);
    std::sort(order.begin(), order.end(), [](const Stroke* a, const Stroke* b) { return a->order < b->order; });

    canvas.bind();
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    prog.use();
    prog.set2f("uRes", float(c.W), float(c.H));
    prog.seti("uBands", bands);
    prog.setf("uGap", 0.06f);
    prog.setf("uAA", std::min(0.2f, 1.5f * px / std::max(1.f, width * 0.5f)));
    prog.setf("uGlow", s.glow * 0.6f);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    float waveAmp = mixf(0.f, 0.9f, s.wave) * width;
    float flow = mixf(0.4f, 3.f, s.flow) * std::clamp(c.speed, 0.3f, 3.f) * std::clamp(c.drive, 0.5f, 1.8f);
    for (const Stroke* st : order) {
      if (st->pts.size() < 2) continue;
      if (!build(*st, width, waveAmp, flow, life, c.gain)) continue;
      float cols[18];
      for (int b = 0; b < 6; ++b) {
        Vec3 v = st->cols[std::min(b, bands - 1)];
        cols[b * 3] = v.x;
        cols[b * 3 + 1] = v.y;
        cols[b * 3 + 2] = v.z;
      }
      prog.set3fv("uCols", cols, 6);
      glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
      if (s.glow > 0.01f) {  // soft halo, added under / around
        glBlendFunc(GL_ONE, GL_ONE);
        prog.seti("uHalo", 1);
        glBufferData(GL_ARRAY_BUFFER, halo.size() * sizeof(float), halo.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(halo.size() / 4));
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
      }
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      prog.seti("uHalo", 0);
      glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size() / 4));
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    fxCopy(out, canvas.tex, c.look->brightness * 1.1f);
  }

 private:
  // Makes the band's triangles (and the halo's) for one stroke into verts / halo.
  bool build(const Stroke& st, float width, float waveAmp, float flow, float life, float gain) {
    // 1. the path with the tail waving: sideways offset grows with age, so the head stays on the finger.
    // The head is left out while it sits on the last point (no direction yet: it would fold the end).
    size_t n = st.pts.size();
    if (n > 2 && length(st.pts[n - 1].p - st.pts[n - 2].p) < 1.f) --n;
    path.resize(n);
    alphas.resize(n);
    for (size_t i = 0; i < n; ++i) {
      Vec2 tan = st.pts[std::min(n - 1, i + 1)].p - st.pts[i ? i - 1 : 0].p;
      float tl = length(tan);
      Vec2 nrm = tl > 1e-3f ? Vec2(-tan.y / tl, tan.x / tl) : Vec2(0, 1);
      float age = std::clamp((clock - st.pts[i].born) / life, 0.f, 1.f);
      float sway = std::sin(st.phase + clock * flow - age * 5.f) * waveAmp * std::sqrt(age);
      path[i] = st.pts[i].p + nrm * sway;
      // fade: the oldest third melts away
      alphas[i] = gain * std::clamp((1.f - age) * 3.f, 0.f, 1.f);
    }
    // 2. smooth curves (Chaikin, twice), keeping both ends
    for (int it = 0; it < 2 && path.size() >= 3; ++it) {
      tmpP.clear();
      tmpA.clear();
      tmpP.push_back(path.front());
      tmpA.push_back(alphas.front());
      for (size_t i = 0; i + 1 < path.size(); ++i) {
        tmpP.push_back(path[i] * 0.75f + path[i + 1] * 0.25f);
        tmpP.push_back(path[i] * 0.25f + path[i + 1] * 0.75f);
        tmpA.push_back(alphas[i] * 0.75f + alphas[i + 1] * 0.25f);
        tmpA.push_back(alphas[i] * 0.25f + alphas[i + 1] * 0.75f);
      }
      tmpP.push_back(path.back());
      tmpA.push_back(alphas.back());
      path.swap(tmpP);
      alphas.swap(tmpA);
    }
    // 3. a strip of quads, constant width (miter-free: the normal is averaged along the curve)
    verts.clear();
    halo.clear();
    const size_t m = path.size();
    if (m < 2) return false;
    float hw = width * 0.5f, hh = width * 0.5f * 1.9f;
    Vec2 lastL, lastR, lastHL, lastHR;
    float lastA = 0;
    bool any = false;
    for (size_t i = 0; i < m; ++i) {
      Vec2 tan = path[std::min(m - 1, i + 1)] - path[i ? i - 1 : 0];
      float tl = length(tan);
      if (tl < 1e-4f) {
        tan = Vec2(1, 0);
        tl = 1;
      }
      Vec2 nrm(-tan.y / tl, tan.x / tl);
      Vec2 L = path[i] + nrm * hw, R = path[i] - nrm * hw;
      Vec2 HL = path[i] + nrm * hh, HR = path[i] - nrm * hh;
      float a = alphas[i];
      if (i > 0 && (a > 0.002f || lastA > 0.002f)) {
        any = true;
        float q[6][4] = {{lastL.x, lastL.y, 1, lastA}, {lastR.x, lastR.y, -1, lastA}, {L.x, L.y, 1, a},
                         {L.x, L.y, 1, a},             {lastR.x, lastR.y, -1, lastA}, {R.x, R.y, -1, a}};
        for (auto& v : q) verts.insert(verts.end(), v, v + 4);
        float h[6][4] = {{lastHL.x, lastHL.y, 1, lastA}, {lastHR.x, lastHR.y, -1, lastA}, {HL.x, HL.y, 1, a},
                         {HL.x, HL.y, 1, a},             {lastHR.x, lastHR.y, -1, lastA}, {HR.x, HR.y, -1, a}};
        for (auto& v : h) halo.insert(halo.end(), v, v + 4);
      }
      lastL = L;
      lastR = R;
      lastHL = HL;
      lastHR = HR;
      lastA = a;
    }
    return any;
  }

  Program prog;
  GLuint vao = 0, vbo = 0;
  std::map<int, Stroke> strokes;
  std::vector<float> verts, halo;
  std::vector<Vec2> path, tmpP;
  std::vector<float> alphas, tmpA;
  Target canvas;
  float clock = 0;
  int orderCounter = 0;
};

}  // namespace

std::unique_ptr<Effect> makeRibbons() { return std::make_unique<Ribbons>(); }
