#include "log.hpp"

#include <SDL.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace {
FILE* g_file = nullptr;
std::string g_dir;
bool g_crashed = false;
SDL_LogOutputFunction g_prevOut = nullptr;
void* g_prevData = nullptr;

void sdlLogToFile(void* data, int category, SDL_LogPriority priority, const char* message) {
  logLine("[sdl] %s", message);
  if (g_prevOut) g_prevOut(g_prevData, category, priority, message);
  (void)data;
}
}  // namespace

void logInit(const std::string& dir) {
  g_dir = dir;
  std::string marker = dir + "running.marker";
  if (FILE* m = std::fopen(marker.c_str(), "rb")) {
    std::fclose(m);
    g_crashed = true;
  }
  std::string path = dir + "touchfx.log", prev = dir + "touchfx-previous.log";
  std::remove(prev.c_str());
  std::rename(path.c_str(), prev.c_str());
  g_file = std::fopen(path.c_str(), "wb");
  if (FILE* m = std::fopen(marker.c_str(), "wb")) {
    std::fputs("TouchFX is running\n", m);
    std::fclose(m);
  }
  SDL_LogGetOutputFunction(&g_prevOut, &g_prevData);
  SDL_LogSetOutputFunction(sdlLogToFile, nullptr);
}

void logLine(const char* fmt, ...) {
  if (!g_file) return;
  std::time_t t = std::time(nullptr);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  std::fprintf(g_file, "%s  ", ts);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(g_file, fmt, ap);
  va_end(ap);
  std::fputc('\n', g_file);
  std::fflush(g_file);
}

void logFlush() {
  if (g_file) std::fflush(g_file);
}

void logCleanExit() {
  logLine("Normal exit");
  if (g_file) std::fclose(g_file);
  g_file = nullptr;
  std::remove((g_dir + "running.marker").c_str());
}

bool logPreviousRunCrashed() { return g_crashed; }
const std::string& logDir() { return g_dir; }
