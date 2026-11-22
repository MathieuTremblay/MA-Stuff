#pragma once
#include "glutil.hpp"
#include "state.hpp"

// Final look: bloom (glow), saturation, tonemapping and the optional alpha channel.
// Effects render linear HDR colour into `scene`; this turns it into the 8-bit output.
class Post {
 public:
  bool init();
  void resize(int W, int H);
  void run(const Target& scene, const Target& out, const PostSettings& p, bool alpha, bool premultiplied);

 private:
  static constexpr int kLevels = 6;
  Program pDown, pUp, pFinal;
  Target mips[kLevels];
  int levels = 0;
};

// Draws a texture region into the current viewport (used for the external display).
// multiplyAlpha shows straight-alpha output correctly on a black screen.
bool presentInit();
void presentTexture(GLuint tex, float u0, float v0, float u1, float v1, bool multiplyAlpha);
