#pragma once
#include <atomic>
#include <thread>

// CPU / GPU / RAM usage sampled once a second on a background thread.
class SysStats {
 public:
  SysStats();
  ~SysStats();
  float cpu() const { return cpu_.load(); }          // 0..100, -1 = unknown
  float gpu() const { return gpu_.load(); }          // 0..100 (3D engine, whole system), -1 = unknown
  float ramUsed() const { return ramUsed_.load(); }  // GB
  float ramTotal() const { return ramTotal_.load(); }

 private:
  void run();
  std::thread t_;
  std::atomic<bool> quit_{false};
  std::atomic<float> cpu_{-1}, gpu_{-1}, ramUsed_{0}, ramTotal_{0};
};
