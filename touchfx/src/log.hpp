#pragma once
#include <string>

// Session log (touchfx.log next to settings.json; the previous run is kept as touchfx-previous.log).
// A "running" marker detects when the last run ended unexpectedly.
void logInit(const std::string& dir);
void logLine(const char* fmt, ...);
void logFlush();
void logCleanExit();             // call on a normal quit
bool logPreviousRunCrashed();    // true if the last run didn't quit normally
const std::string& logDir();
