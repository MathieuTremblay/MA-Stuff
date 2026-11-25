#pragma once
#include "glutil.hpp"
#include "state.hpp"

// Video colours: tints the image with the video input (fit = scale from output UV to video UV).
struct VideoTint {
  GLuint tex = 0;
  float fitX = 1, fitY = 1;
  float amount = 1;
};

// Final look: bloom (glow), saturation, tonemapping and the optional alpha channel.
// Effects render linear HDR colour into `scene`; this turns it into the 8-bit output.
class Post {
 public:
  bool init();
  void resize(int W, int H);
  void run(const Target& scene, const Target& out, const PostSettings& p, bool alpha, bool premultiplied,
           float master = 1.f, const VideoTint* tint = nullptr, bool original = false);
  // original: the layer is the video input - it goes out exactly as it came in (the LOOK is not applied)

 private:
  static constexpr int kLevels = 6;
  Program pDown, pUp, pFinal;
  Target mips[kLevels];
  int levels = 0;
};

// Draws a texture region into the current viewport (used for the external display).
// multiplyAlpha shows straight-alpha output correctly on a black screen.
bool presentInit();
// sharpW / sharpH: the texture size, for a sharp bicubic upscale (0 = plain bilinear)
void presentTexture(GLuint tex, float u0, float v0, float u1, float v1, bool multiplyAlpha, int sharpW = 0, int sharpH = 0);

// Background layer: combines the front and background layers (outputs of Post, premultiplied) into out.
bool compositeInit();
void compositeLayers(const Target& front, const Target& back, const Target& out, int blend, bool alpha,
                     bool premultiplied, float master);
