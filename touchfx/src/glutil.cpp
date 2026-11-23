#include "glutil.hpp"

#include <SDL.h>

#include <string>

const char* kGlslHeader = R"(#version 330 core
// Soft clip: colours stay exact below 0.75, brighter values roll off smoothly to 1.
vec3 tonemap(vec3 c) {
  c = max(c, vec3(0.0));
  const float k = 0.75;
  vec3 hi = k + (1.0 - k) * (1.0 - exp(-(c - k) / (1.0 - k)));
  return mix(c, hi, step(vec3(k), c));
}
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),
             mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
)";

const char* kFullscreenVS = R"(
out vec2 vUv;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUv = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static GLuint g_vao = 0;

void glutilInit() {
  if (!g_vao) glGenVertexArrays(1, &g_vao);
}

GLuint emptyVao() { return g_vao; }

void drawFullscreen() {
  glBindVertexArray(g_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void bindTex(int unit, GLuint tex) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, tex);
}

static GLuint compile(GLenum type, const std::string& src, const char* name) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    SDL_Log("Shader compile error (%s, %s):\n%s", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

bool Program::build(const char* name, const char* vs, const char* fs) {
  destroy();
  GLuint v = compile(GL_VERTEX_SHADER, std::string(kGlslHeader) + vs, name);
  GLuint f = compile(GL_FRAGMENT_SHADER, std::string(kGlslHeader) + fs, name);
  if (!v || !f) return false;
  id_ = glCreateProgram();
  glAttachShader(id_, v);
  glAttachShader(id_, f);
  glLinkProgram(id_);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(id_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
    SDL_Log("Program link error (%s):\n%s", name, log);
    glDeleteProgram(id_);
    id_ = 0;
    return false;
  }
  return true;
}

void Program::destroy() {
  if (id_) glDeleteProgram(id_);
  id_ = 0;
  cache_.clear();
}

GLint Program::loc(const char* n) {
  auto it = cache_.find(n);
  if (it != cache_.end()) return it->second;
  GLint l = glGetUniformLocation(id_, n);
  cache_[n] = l;
  return l;
}

static int g_liveTargets = 0;
int glLiveTargets() { return g_liveTargets; }

void Target::create(int w_, int h_, GLenum internalFmt, GLenum fmt, GLenum type, GLint filter) {
  destroy();
  w = w_;
  h = h_;
  ++g_liveTargets;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE) SDL_Log("Framebuffer incomplete: 0x%x (%dx%d)", st, w, h);
  clear();
}

void Target::destroy() {
  if (tex) --g_liveTargets;
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (tex) glDeleteTextures(1, &tex);
  fbo = tex = 0;
  w = h = 0;
}

void Target::clear(float r, float g, float b, float a) const {
  bind();
  glDisable(GL_BLEND);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
}
