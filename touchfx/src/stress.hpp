#pragma once
// Stress test: TouchFX.exe --stress <minutes>
// A robot plays TouchFX for the given time: up to 10 fingers, effect / background / scene / cue / undo changes,
// random settings, resolution changes, video sources dropping out, DMX floods, damaged show files.
// Nothing is saved to your shows or settings. A report (and a CSV of the measurements) is written next to
// the log (%APPDATA%\TouchFX\TouchFX\stress-report.txt).
#include <string>

struct App;

void stressStart(App& a, double minutes, const std::string& reportDir);
void stressTick(App& a, float dt, float frameSeconds);  // once per frame, before the effects run
bool stressFinished(const App& a);
void stressFinish(App& a);  // writes the report
