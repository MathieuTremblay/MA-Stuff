#pragma once
#include <SDL.h>

// Window tweaks for touch: no press-and-hold right click, no tap feedback, no edge swipes when fullscreen.
void platformSetupWindow(SDL_Window* window);

// Windows 3- and 4-finger touch gestures (switch app, show desktop...) are a per-user system setting.
// Turns them off and returns the original value to put back later, or -1 if nothing was changed.
int platformBlockSystemGestures();
void platformRestoreSystemGestures(int original);
// Opens the Windows touch settings page.
void platformOpenTouchSettings();
bool platformHasSystemGestures();  // false on non-Windows builds
// Native borderless fullscreen on the window's monitor (Windows). Returns false if not handled here.
bool platformSetFullscreen(SDL_Window* window, bool on);

#include <string>
std::string platformDocumentsDir();               // user's Documents folder (UTF-8)
void platformOpenFolder(const std::string& path);  // opens it in Explorer
void platformPreventSleep(bool on);                // no sleep / screen saver / display off while running
// Writes a crash dump (Windows minidump) and a log line into dir if TouchFX crashes.
void platformInstallCrashHandler(const std::string& dir);
