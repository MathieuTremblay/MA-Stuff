#pragma once
// The media player: plays the look's clip (Look::mediaClip) with its own settings, and changes clip with the
// media layer's transition (CUT / CROSSFADE / FADE THROUGH BLACK). Its picture (output size, premultiplied
// RGBA) is the MEDIA source of the VIDEO effect - on the BACK layer that is the media layer.
// Each clip being played has a decoder thread reading HAP frames; the GPU decompresses them.
#include <map>
#include <memory>
#include <string>

#include "glutil.hpp"
#include "state.hpp"

class MediaImporter;

struct MediaEnv {
  const MediaLibrary* lib = nullptr;
  const MediaImporter* imp = nullptr;
  double beat = 0;
  float bpm = 120;
  GLuint liveTex = 0;  // the video input (for the LIVE slot)
  int liveW = 0, liveH = 0;
  float liveLevel = 0;
};

class MediaPlayer {
 public:
  MediaPlayer();
  ~MediaPlayer();
  bool init();
  void resize(int W, int H);
  // Every frame on the GL thread. Follows `clip` (from the look). render = somebody shows the picture.
  void update(const MediaEnv& env, const std::string& clip, float dt, bool render);
  GLuint output() const { return out_.tex; }
  int width() const { return out_.w; }
  int height() const { return out_.h; }
  // MEDIA window: a clip shown in the preview monitor (with its settings), not on the output
  void updatePreview(const MediaEnv& env, const std::string& clip, float dt, int w, int h);
  GLuint preview() const { return prev_.tex; }
  void restart();         // the playing clip starts again from its In point
  void snapNext();        // the next clip change cuts (FLASH scenes)
  void resumeNext();      // the next clip carries on where it was (FLASH released: back as it was)
  const std::string& liveClip() const;
  float livePosition() const;   // 0..1 within the clip, for the MEDIA window
  float previewPosition() const;
  std::string status() const;   // e.g. a clip that cannot be read

  struct Deck;

 private:
  void renderDeck(const MediaEnv& env, Deck& d, const Target& dst, float weight);
  void startDeck(const MediaEnv& env, std::unique_ptr<Deck>& d, const std::string& id, bool resume);
  Program prog_, blur_, copy_;
  Target out_, tmpA_, tmpB_, prev_;
  std::unique_ptr<Deck> cur_, old_, pre_;
  float trans_ = 1.f, transDur_ = 0.f;
  int transType_ = MTRANS_CUT;
  bool snap_ = false, resume_ = false;
  std::string liveId_;
  struct Saved {
    double pos;
    int dir;
    double when;
  };
  std::map<std::string, Saved> saved_;  // where clips were when they left (FLASH return)
  double clock_ = 0;
  int W_ = 0, H_ = 0;
};
