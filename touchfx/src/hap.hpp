#pragma once
// HAP video (Vidvox): frames are GPU-compressed textures (DXT1 / DXT5 / YCoCg-DXT5 / + RGTC1 alpha), usually
// with a Snappy layer on top. The graphics card decodes them, so playback is cheap and frame-accurate.
// The clips TouchFX makes on import are QuickTime .mov files with one HAP video track and no audio.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Index of a .mov file's HAP video track: where each frame is in the file.
struct HapIndex {
  int width = 0, height = 0;   // picture size (the texture is padded up to multiples of 4)
  uint32_t fourcc = 0;         // 'Hap1' 'Hap5' 'HapY' 'HapM' 'HapA'
  double fps = 0;
  std::vector<uint64_t> offset;
  std::vector<uint32_t> size;
  int frames() const { return int(offset.size()); }
  bool alpha() const;          // the clip has transparency
};
bool movReadIndex(const std::string& path, HapIndex& out, std::string& err);

enum HapTex { HAPTEX_DXT1, HAPTEX_DXT5, HAPTEX_YCOCG, HAPTEX_ALPHA_RGTC1 };

// One decoded frame: one texture (or two for HAP Q Alpha: YCoCg colour + RGTC1 alpha), ready for
// glCompressedTexImage2D.
struct HapFrame {
  int count = 0;
  int format[2] = {0, 0};
  std::vector<uint8_t> data[2];
};
bool hapDecode(const uint8_t* src, size_t len, HapFrame& out, std::string& err);

// Snappy (Google) decompression of a whole block. Returns false on damaged data.
bool snappyUncompress(const uint8_t* src, size_t len, std::vector<uint8_t>& out);

// Reads frames from an open .mov (one reader per thread).
class HapReader {
 public:
  ~HapReader() { close(); }
  bool open(const std::string& path, std::string& err);
  void close();
  const HapIndex& index() const { return idx_; }
  bool readFrame(int i, HapFrame& out, std::string& err);

 private:
  HapIndex idx_;
  FILE* f_ = nullptr;
  std::vector<uint8_t> buf_;
};
