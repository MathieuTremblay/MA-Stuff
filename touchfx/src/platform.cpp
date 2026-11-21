#include "platform.hpp"

#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>

// Turn off the Windows touch "press and hold = right click" circle, tap ripples and flicks,
// which get in the way when drawing with fingers.
void platformSetupWindow(SDL_Window* window) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info)) return;
  HWND hwnd = info.info.win.window;

  const wchar_t* prop = L"MicrosoftTabletPenServiceProperty";
  ATOM atom = GlobalAddAtomW(prop);
  DWORD flags = 0x00000001 /*no press-and-hold*/ | 0x00000008 /*no pen tap feedback*/ |
                0x00000010 /*no barrel feedback*/ | 0x00000100 /*no touch UI forcing*/ |
                0x00010000 /*no flicks*/;
  SetPropW(hwnd, prop, (HANDLE)(DWORD_PTR)flags);
  GlobalDeleteAtom(atom);

  // Windows 8+: disable the contact visualisation and gesture feedback.
  using SetFeedbackFn = BOOL(WINAPI*)(HWND, int, DWORD, UINT32, const void*);
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    auto fn = (SetFeedbackFn)(void*)GetProcAddress(user32, "SetWindowFeedbackSetting");
    if (fn) {
      BOOL off = FALSE;
      for (int type = 1; type <= 11; ++type) fn(hwnd, type, 0, sizeof(off), &off);
    }
  }
}

#else

void platformSetupWindow(SDL_Window*) {}

#endif
