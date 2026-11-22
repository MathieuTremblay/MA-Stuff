#include "platform.hpp"

#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>

// System.EdgeGesture.DisableTouchWhenFullscreen
static const PROPERTYKEY kEdgeGestureDisable = {
    {0x32CE38B2, 0x2C9A, 0x41B1, {0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44}}, 2};

void platformSetupWindow(SDL_Window* window) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info)) return;
  HWND hwnd = info.info.win.window;

  // No press-and-hold = right click circle, tap ripples or flicks.
  const wchar_t* prop = L"MicrosoftTabletPenServiceProperty";
  ATOM atom = GlobalAddAtomW(prop);
  DWORD flags = 0x00000001 /*no press-and-hold*/ | 0x00000008 /*no pen tap feedback*/ |
                0x00000010 /*no barrel feedback*/ | 0x00000100 /*no touch UI forcing*/ |
                0x00010000 /*no flicks*/;
  SetPropW(hwnd, prop, (HANDLE)(DWORD_PTR)flags);
  GlobalDeleteAtom(atom);

  // Windows 8+: no contact visualisation / gesture feedback.
  using SetFeedbackFn = BOOL(WINAPI*)(HWND, int, DWORD, UINT32, const void*);
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    auto fn = (SetFeedbackFn)(void*)GetProcAddress(user32, "SetWindowFeedbackSetting");
    if (fn) {
      BOOL off = FALSE;
      for (int type = 1; type <= 11; ++type) fn(hwnd, type, 0, sizeof(off), &off);
    }
  }

  // No edge swipes (Action Center, widgets, task view) while TouchFX is fullscreen.
  HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IPropertyStore* store = nullptr;
  if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) && store) {
    PROPVARIANT v;
    PropVariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = VARIANT_TRUE;
    store->SetValue(kEdgeGestureDisable, v);
    store->Release();
  }
  if (SUCCEEDED(co)) CoUninitialize();
}

static const wchar_t* kDesktopKey = L"Control Panel\\Desktop";
static const wchar_t* kGestureValue = L"TouchGestureSetting";

static void notifyGestureChange() {
  DWORD_PTR res;
  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)kDesktopKey, SMTO_ABORTIFHUNG, 200, &res);
}

int platformBlockSystemGestures() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS) return -1;
  DWORD value = 1, size = sizeof(value), type = 0;
  if (RegQueryValueExW(k, kGestureValue, nullptr, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
    value = 1;  // not set = gestures on (Windows default)
  int original = -1;
  if (value != 0) {
    DWORD off = 0;
    if (RegSetValueExW(k, kGestureValue, 0, REG_DWORD, (const BYTE*)&off, sizeof(off)) == ERROR_SUCCESS)
      original = int(value);
  }
  RegCloseKey(k);
  if (original >= 0) notifyGestureChange();
  return original;
}

void platformRestoreSystemGestures(int original) {
  if (original < 0) return;
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kDesktopKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
  DWORD v = DWORD(original);
  RegSetValueExW(k, kGestureValue, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
  RegCloseKey(k);
  notifyGestureChange();
}

void platformOpenTouchSettings() {
  ShellExecuteW(nullptr, L"open", L"ms-settings:devices-touch", nullptr, nullptr, SW_SHOWNORMAL);
}

bool platformHasSystemGestures() { return true; }

#else

void platformSetupWindow(SDL_Window*) {}
int platformBlockSystemGestures() { return -1; }
void platformRestoreSystemGestures(int) {}
void platformOpenTouchSettings() {}
bool platformHasSystemGestures() { return false; }

#endif
