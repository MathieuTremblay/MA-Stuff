// Ribbons: each finger paints a wide, flat brush stroke made of parallel colour bands (2 to 6, colours from
// the palette: low, mid, high, repeating) - a retro striped look. The stroke follows the finger in smooth
// curves; turns too tight for its width are rounded off so the bands never fold. STAY: the paint stays where
// it was drawn and only ripples in place. FLOAT: the tail also sways and drifts away as it fades.
// It fades out after the Length time. Matte by default, Glow adds a soft halo.
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
    float w = 1;  // width here (a fast stroke is thinner, like a real brush)
  };
  Vec2 tip, target;  // the brush tip trails the finger a little (lazy brush): smooth, no jitter
  float wf = 1;      // current width from the brush speed
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
    float step = std::max(3.f * px, width * 0.06f);  // a new point every few pixels of brush travel
    // lazy brush: how quickly the tip catches up with the finger (Smoothing 0 = right under the finger)
    float follow = 1.f - std::exp(-dt * mixf(45.f, 3.5f, std::sqrt(s.smooth)));

    // ---- follow the fingers ----
    for (auto& kv : strokes) kv.second.seen = false;
    for (const Emitter& e : c.live) {
      int key = e.touch * 64 + e.mirror;
      Stroke& st = strokes[key];
      Vec2 p(e.pos.x * c.W, e.pos.y * c.H);
      if (st.pts.empty()) {
        st.phase = hash01(uint32_t(key) * 2654435761u + uint32_t(clock * 10.f)) * kTau;
        st.order = ++orderCounter;
        st.tip = p;
        st.wf = 1.f;
        st.pts.push_back({p, clock, 1.f});
        st.pts.push_back({p, clock, 1.f});
      }
      st.target = p;
      st.seen = true;
      // band colours: palette low, mid, high, repeating (the colour mode applies)
      static const float kT[3] = {0.f, 0.5f, 1.f};
      for (int b = 0; b < bands; ++b) st.cols[b] = c.color(c.colorT(&e, kT[b % 3], hash01(uint32_t(key) * 31u + uint32_t(b))));
    }
    // ---- move the brush tips (a lifted finger's brush still glides to where the finger was) ----
    for (auto& kv : strokes) {
      Stroke& st = kv.second;
      if (st.pts.size() < 2) continue;
      Vec2 old = st.tip;
      st.tip = st.tip + (st.target - st.tip) * follow;
      float moved = length(st.tip - old);
      if (!st.seen && moved < 0.3f * px) continue;
      // faster = thinner (Brush), eased so the width breathes rather than jumps
      float speed = moved / std::max(dt, 1e-3f) / float(c.H);  // screen heights per second
      float wantW = std::clamp(1.f / (1.f + s.brush * 2.2f * speed), 0.3f, 1.f);
      st.wf += (wantW - st.wf) * std::min(1.f, dt * 6.f);
      Stroke::Pt& head = st.pts.back();
      Vec2 last = st.pts[st.pts.size() - 2].p;
      head.p = st.tip;
      head.born = clock;
      head.w = st.wf;
      if (length(st.tip - last) >= step && st.pts.size() < 2000) st.pts.push_back({st.tip, clock, st.wf});
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
    float flow = mixf(0.25f, 2.5f, s.flow) * std::clamp(c.speed, 0.3f, 3.f) * std::clamp(c.drive, 0.5f, 1.8f);
    for (const Stroke* st : order) {
      if (st->pts.size() < 2) continue;
      if (!build(*st, width, waveAmp, flow, life, c.gain, s.motion == RIBBON_FLOAT, s.taper)) continue;
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
  bool build(const Stroke& st, float width, float waveAmp, float flow, float life, float gain, bool floating, float taper) {
    // 1. the painted path, evenly spaced (oldest first, the finger last). The head is left out while it sits
    // on the last point (no direction yet).
    size_t n = st.pts.size();
    if (n > 2 && length(st.pts[n - 1].p - st.pts[n - 2].p) < 1.f) --n;
    if (n < 2) return false;
    const float ds = std::max(4.f, width * 0.25f);  // coarse first: rounding the turns settles fast
    path.clear();
    ages.clear();
    wfs.clear();
    path.push_back(st.pts[0].p);
    ages.push_back(clock - st.pts[0].born);
    wfs.push_back(st.pts[0].w);
    float carry = 0;
    for (size_t i = 0; i + 1 < n; ++i) {
      Vec2 a = st.pts[i].p, b = st.pts[i + 1].p;
      float segL = length(b - a);
      float ageA = clock - st.pts[i].born, ageB = clock - st.pts[i + 1].born;
      float t = ds - carry;
      while (t < segL && path.size() < 4000) {
        float k = t / segL;
        path.push_back(a + (b - a) * k);
        ages.push_back(ageA + (ageB - ageA) * k);
        wfs.push_back(st.pts[i].w + (st.pts[i + 1].w - st.pts[i].w) * k);
        t += ds;
      }
      carry = segL - (t - ds);
    }
    if (length(st.pts[n - 1].p - path.back()) > 0.5f) {
      path.push_back(st.pts[n - 1].p);
      ages.push_back(clock - st.pts[n - 1].born);
      wfs.push_back(st.pts[n - 1].w);
    }
    const size_t m = path.size();
    if (m < 2) return false;
    float hw = width * 0.5f;
    // 2. round off turns tighter than the stroke can follow (the inner edge would fold over itself)
    limitCurvature(hw * 1.15f, ds);

    // 3. motion. STAY: the paint stays where it was drawn and only ripples in place (a small wave running down
    // the tail; the head stays on the finger). FLOAT: the tail also sways wider with age and drifts away softly.
    base = path;
    float distFromHead = 0;
    Vec2 drift = Vec2(std::sin((st.phase - kPi) * 0.45f), std::cos((st.phase - kPi) * 0.45f));  // mostly up
    for (size_t j = m; j-- > 0;) {
      if (j + 1 < m) distFromHead += length(base[j + 1] - base[j]);
      Vec2 tan = base[std::min(m - 1, j + 1)] - base[j ? j - 1 : 0];
      float tl = length(tan);
      Vec2 nrm = tl > 1e-3f ? Vec2(-tan.y / tl, tan.x / tl) : Vec2(0, 1);
      float age = std::clamp(ages[j] / life, 0.f, 1.f);
      float ramp = std::clamp(distFromHead / (width * 1.5f), 0.f, 1.f);
      ramp = ramp * ramp * (3.f - 2.f * ramp);
      // several soft waves of different lengths and speeds, slowly swelling: more like cloth than a sine
      float d = distFromHead / width;
      float w3 = std::sin(st.phase + clock * flow * 1.7f - d / 2.2f) * 0.6f +
                 std::sin(st.phase * 1.7f + clock * flow * 1.1f - d / 3.9f) * 0.3f +
                 std::sin(st.phase * 2.3f - clock * flow * 0.6f - d / 1.3f) * 0.15f;
      float swell = 0.75f + 0.25f * std::sin(clock * 0.37f + st.phase * 3.1f);
      float ripple = w3 * swell * waveAmp * 0.3f * ramp;
      Vec2 off = nrm * ripple;
      if (floating) {
        off += nrm * (std::sin(st.phase + clock * flow - age * 5.f) * waveAmp * std::sqrt(age));
        off += drift * (std::pow(age, 1.5f) * width * 1.2f);
      }
      path[j] = base[j] + off;
    }
    if (waveAmp > 0.f || floating) limitCurvature(hw * 1.15f, ds);  // the motion can bend it again
    // then smooth and fine (Chaikin, twice: the corners are cut, never sharpened), keeping both ends
    for (int it = 0; it < 2; ++it) {
      base.clear();
      tmpA.clear();
      tmpW.clear();
      base.push_back(path.front());
      tmpA.push_back(ages.front());
      tmpW.push_back(wfs.front());
      for (size_t i = 0; i + 1 < path.size(); ++i) {
        base.push_back(path[i] * 0.75f + path[i + 1] * 0.25f);
        base.push_back(path[i] * 0.25f + path[i + 1] * 0.75f);
        tmpA.push_back(ages[i] * 0.75f + ages[i + 1] * 0.25f);
        tmpA.push_back(ages[i] * 0.25f + ages[i + 1] * 0.75f);
        tmpW.push_back(wfs[i] * 0.75f + wfs[i + 1] * 0.25f);
        tmpW.push_back(wfs[i] * 0.25f + wfs[i + 1] * 0.75f);
      }
      base.push_back(path.back());
      tmpA.push_back(ages.back());
      tmpW.push_back(wfs.back());
      path.swap(base);
      ages.swap(tmpA);
      wfs.swap(tmpW);
    }
    const size_t m2 = path.size();
    alphas.resize(m2);
    for (size_t j = 0; j < m2; ++j) alphas[j] = gain * std::clamp((1.f - ages[j] / life) * 3.f, 0.f, 1.f);
    // taper: the tail end narrows to a point over the last part of the stroke (Taper sets how long)
    if (taper > 0.001f && m2 > 2) {
      float total = 0;
      for (size_t j = 1; j < m2; ++j) total += length(path[j] - path[j - 1]);
      float len = std::max(1.f, std::min(total * 0.6f, width * mixf(0.5f, 6.f, taper)));
      float acc = 0;
      for (size_t j = 0; j < m2; ++j) {
        if (j) acc += length(path[j] - path[j - 1]);
        if (acc >= len) break;
        float u = acc / len;
        wfs[j] *= mixf(1.f, 0.08f + 0.92f * u * (2.f - u), std::min(1.f, taper * 1.5f));
      }
    }

    // 4. a strip of quads plus round ends
    verts.clear();
    halo.clear();
    float hh = hw * 1.9f;
    Vec2 lastL, lastR, lastHL, lastHR;
    float lastA = 0;
    bool any = false;
    Vec2 firstT, lastT, firstN, lastN;
    for (size_t i = 0; i < m2; ++i) {
      Vec2 tan = path[std::min(m2 - 1, i + 1)] - path[i ? i - 1 : 0];
      float tl = length(tan);
      if (tl < 1e-4f) {
        tan = Vec2(1, 0);
        tl = 1;
      }
      tan = tan * (1.f / tl);
      Vec2 nrm(-tan.y, tan.x);
      if (i == 0) firstT = tan, firstN = nrm;
      lastT = tan;
      lastN = nrm;
      float wk = wfs[i];
      Vec2 L = path[i] + nrm * (hw * wk), R = path[i] - nrm * (hw * wk);
      Vec2 HL = path[i] + nrm * (hh * wk), HR = path[i] - nrm * (hh * wk);
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
    // round ends: the bands carry on straight across a half disc, like the end of a wide brush
    if (alphas.back() > 0.002f) {
      cap(verts, path.back(), lastT, lastN, hw * wfs.back(), alphas.back());
      cap(halo, path.back(), lastT, lastN, hh * wfs.back(), alphas.back());
    }
    if (alphas.front() > 0.002f) {
      cap(verts, path.front(), firstT * -1.f, firstN, hw * wfs.front(), alphas.front());
      cap(halo, path.front(), firstT * -1.f, firstN, hh * wfs.front(), alphas.front());
    }
    return any;
  }

  // Half disc at an end: out = the direction away from the stroke. across = position across the stroke (-1..1).
  static void cap(std::vector<float>& v, Vec2 c, Vec2 out, Vec2 nrm, float r, float a) {
    const int seg = 12;
    for (int k = 0; k < seg; ++k) {
      float p0 = kPi * k / seg, p1 = kPi * (k + 1) / seg;
      Vec2 d0 = nrm * std::cos(p0) + out * std::sin(p0), d1 = nrm * std::cos(p1) + out * std::sin(p1);
      Vec2 q0 = c + d0 * r, q1 = c + d1 * r;
      float t[3][4] = {{c.x, c.y, 0, a}, {q0.x, q0.y, std::cos(p0), a}, {q1.x, q1.y, std::cos(p1), a}};
      for (auto& x : t) v.insert(v.end(), x, x + 4);
    }
  }

  // Smooths `path` where it turns tighter than radius rmin (points ds apart), keeping both ends in place.
  void limitCurvature(float rmin, float ds) {
    const size_t m = path.size();
    if (m < 3) return;
    for (int it = 0; it < 60; ++it) {
      bool changed = false;
      for (size_t i = 1; i + 1 < m; ++i) {
        Vec2 a = path[i] - path[i - 1], b = path[i + 1] - path[i];
        float la = length(a), lb = length(b);
        if (la < 1e-4f || lb < 1e-4f) continue;
        float cosT = std::clamp((a.x * b.x + a.y * b.y) / (la * lb), -1.f, 1.f);
        float turn = std::acos(cosT);
        float span = 0.5f * (la + lb);
        if (turn * rmin > span * 1.02f + 1e-3f) {  // radius = span / turn is below rmin
          path[i] = path[i] * 0.4f + (path[i - 1] + path[i + 1]) * 0.3f;
          changed = true;
        }
      }
      if (!changed) break;
    }
    (void)ds;
  }

  Program prog;
  GLuint vao = 0, vbo = 0;
  std::map<int, Stroke> strokes;
  std::vector<float> verts, halo;
  std::vector<Vec2> path, base;
  std::vector<float> alphas, ages, tmpA, wfs, tmpW;
  Target canvas;
  float clock = 0;
  int orderCounter = 0;
};

}  // namespace

std::unique_ptr<Effect> makeRibbons() { return std::make_unique<Ribbons>(); }
