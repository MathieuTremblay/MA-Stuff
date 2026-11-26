#pragma once
// Runs a helper program (FFmpeg) in the background without a console window.
#include <memory>
#include <string>
#include <vector>

class Process {
 public:
  ~Process();
  // args[0] is the program. Its output (stdout + stderr) goes to logFile ("" = discarded).
  bool start(const std::vector<std::string>& args, const std::string& logFile);
  // -1 while running, else the exit code
  int poll();
  void kill();
  bool running() { return poll() == -1; }

 private:
#ifdef _WIN32
  void* proc_ = nullptr;
#else
  int pid_ = -1;
  int status_ = -1;
#endif
  int exit_ = -1;
};

// The folder TouchFX.exe is in (with a trailing separator).
std::string exeDir();
// FFmpeg / FFprobe: next to TouchFX.exe (or in an "ffmpeg" folder there); on Linux TOUCHFX_FFMPEG / PATH.
std::string ffmpegPath();
std::string ffprobePath();
bool ffmpegAvailable();
