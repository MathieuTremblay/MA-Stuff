#pragma once
#include <SDL.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// What the effects get from the audio analysis. All levels are 0..1 (auto-gained).
struct AudioFeatures {
  bool active = false;  // signal above the noise gate
  float level = 0, bass = 0, mid = 0, high = 0;
  float pulse = 0;      // 1 on a detected hit, then decays
  bool onset = false;   // a hit was detected this frame
  float bpm = 0;        // estimated tempo (0 = not sure yet)
};

// Special device names
constexpr const char* kAudioLoopback = "@loopback";  // what this PC is playing (Windows)
constexpr const char* kAudioTest = "@test";          // built-in test beat

class AudioInput {
 public:
  ~AudioInput();
  static std::vector<std::string> captureDevices();  // microphones / line inputs
  static bool loopbackSupported();

  bool open(const std::string& device);  // "" = default input
  void close();
  bool isOpen() const { return open_; }
  const std::string& device() const { return device_; }
  const std::string& error() const { return error_; }
  int sampleRate() const { return rate_.load(); }

  // Analyse the newest audio. sensitivity 0..1 (higher = reacts to quieter sound).
  void update(float dt, float sensitivity);
  const AudioFeatures& features() const { return f_; }

  void push(const float* mono, int n);  // called from the audio thread

 private:
  void loopbackThread();
  void generateTest(float dt);

  bool open_ = false;
  std::string device_, error_;
  SDL_AudioDeviceID dev_ = 0;
  std::atomic<int> rate_{48000};

  std::thread loop_;
  std::atomic<bool> quit_{false};
  std::mutex errMtx_;
  std::string threadError_;

  std::mutex mtx_;
  std::vector<float> ring_ = std::vector<float>(16384, 0.f);
  size_t write_ = 0;
  uint64_t received_ = 0;

  // test signal state
  double testTime_ = 0, testCarry_ = 0;
  uint32_t testNoise_ = 1;

  // analysis state
  AudioFeatures f_;
  float peak_[4] = {0.05f, 0.05f, 0.05f, 0.05f};
  float bassAvg_ = 0;
  double now_ = 0, lastOnset_ = -10, gateHold_ = 0;
  std::vector<float> intervals_;
  std::vector<float> window_, re_, im_;
};
