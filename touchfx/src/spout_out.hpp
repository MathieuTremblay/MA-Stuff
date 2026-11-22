#pragma once
#include <string>

// Spout output (Windows only): shares the output texture with Resolume, TouchDesigner, OBS... on the same PC,
// GPU to GPU, without network or extra latency. Kept in its own file because Spout uses its own OpenGL headers.
class SpoutOut {
 public:
  ~SpoutOut();
  static bool available();  // false on non-Windows builds
  // Sends an OpenGL texture (call with the GL context current). Starts the sender on first use.
  bool send(const std::string& name, unsigned int texture, int width, int height);
  void stop();
  bool running() const { return sender_ != nullptr; }
  const std::string& error() const { return error_; }

 private:
  void* sender_ = nullptr;
  std::string name_, error_;
};
