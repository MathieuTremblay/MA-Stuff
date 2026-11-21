#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Sends video frames over NDI. The NDI runtime library is loaded at runtime
// (installed with NDI Tools / NDI Runtime), so TouchFX starts fine without it.
class NdiSender {
 public:
  ~NdiSender();
  bool start(const std::string& name);  // false if the runtime is missing or creation failed
  void stop();
  bool running() const { return instance_ != nullptr; }
  const std::string& name() const { return name_; }
  const std::string& error() const { return error_; }
  int connections() const { return connections_.load(); }
  // Copies a BGRA frame (top row first) and queues it for sending.
  void submit(const uint8_t* bgra, int w, int h, int fps);

 private:
  bool loadRuntime();
  void threadMain();

  void* lib_ = nullptr;
  void* instance_ = nullptr;
  std::string name_, error_;
  std::atomic<int> connections_{0};

  std::thread thread_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool quit_ = false, hasPending_ = false;
  std::vector<uint8_t> pending_, sending_;
  int pw_ = 0, ph_ = 0, pfps_ = 60;
};
