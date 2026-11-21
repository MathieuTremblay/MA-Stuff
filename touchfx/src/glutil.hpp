#pragma once
#include <glad/gl.h>
#include <string>
#include <unordered_map>

// Shared GLSL pieces. Every fragment shader body is prefixed with kGlslHeader.
extern const char* kGlslHeader;
extern const char* kFullscreenVS;

class Program {
 public:
  // vs / fs are bodies; the version + common functions header is prepended automatically.
  bool build(const char* name, const char* vs, const char* fs);
  void destroy();
  void use() const { glUseProgram(id_); }
  GLuint id() const { return id_; }
  GLint loc(const char* n);
  void setf(const char* n, float v) { glUniform1f(loc(n), v); }
  void seti(const char* n, int v) { glUniform1i(loc(n), v); }
  void set2f(const char* n, float a, float b) { glUniform2f(loc(n), a, b); }
  void set3f(const char* n, float a, float b, float c) { glUniform3f(loc(n), a, b, c); }
  void set2fv(const char* n, const float* v, int count) { glUniform2fv(loc(n), count, v); }
  void set3fv(const char* n, const float* v, int count) { glUniform3fv(loc(n), count, v); }

 private:
  GLuint id_ = 0;
  std::unordered_map<std::string, GLint> cache_;
};

// A texture with a framebuffer attached.
struct Target {
  GLuint fbo = 0, tex = 0;
  int w = 0, h = 0;
  void create(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type, GLint filter);
  void destroy();
  void bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
  }
  void clear(float r = 0, float g = 0, float b = 0, float a = 0) const;
};

// Two targets used for ping-pong rendering.
struct DoubleTarget {
  Target t[2];
  int cur = 0;
  void create(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type, GLint filter) {
    t[0].create(w, h, internalFmt, fmt, type, filter);
    t[1].create(w, h, internalFmt, fmt, type, filter);
    cur = 0;
  }
  void destroy() { t[0].destroy(); t[1].destroy(); }
  Target& read() { return t[cur]; }
  Target& write() { return t[cur ^ 1]; }
  void swap() { cur ^= 1; }
  void clear() { t[0].clear(); t[1].clear(); }
};

void bindTex(int unit, GLuint tex);
void drawFullscreen();     // draws a fullscreen triangle with the shared empty VAO
GLuint emptyVao();
void glutilInit();
