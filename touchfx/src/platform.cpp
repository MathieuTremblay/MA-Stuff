#include "platform.hpp"

#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <cstdio>
#include <ctime>
#include <string>
#include "log.hpp"

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

static std::string toUtf8(const wchar_t* w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}
static std::wstring toWide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

std::string platformDocumentsDir() {
  wchar_t path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, nullptr, 0, path))) return toUtf8(path);
  return ".";
}

void platformOpenFolder(const std::string& path) {
  ShellExecuteW(nullptr, L"open", toWide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void platformPreventSleep(bool on) {
  SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) : ES_CONTINUOUS);
}

// ---- crash dumps ----
static std::wstring g_crashDir;
typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
struct MiniDumpExceptionInfo {
  DWORD ThreadId;
  EXCEPTION_POINTERS* ExceptionPointers;
  BOOL ClientPointers;
};

static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
  logLine("CRASH: exception 0x%08lx at %p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
  logFlush();
  if (HMODULE dbg = LoadLibraryW(L"dbghelp.dll")) {
    auto write = (MiniDumpWriteDumpFn)(void*)GetProcAddress(dbg, "MiniDumpWriteDump");
    if (write) {
      wchar_t name[64];
      std::time_t t = std::time(nullptr);
      std::tm tm = *std::localtime(&t);
      swprintf(name, 64, L"\\crash-%04d%02d%02d-%02d%02d%02d.dmp", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
               tm.tm_min, tm.tm_sec);
      std::wstring path = g_crashDir + name;
      HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (f != INVALID_HANDLE_VALUE) {
        MiniDumpExceptionInfo info{GetCurrentThreadId(), ep, FALSE};
        const int kMiniDumpWithIndirectlyReferencedMemory = 0x40;
        write(GetCurrentProcess(), GetCurrentProcessId(), f, kMiniDumpWithIndirectlyReferencedMemory, &info, nullptr,
              nullptr);
        CloseHandle(f);
      }
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void platformInstallCrashHandler(const std::string& dir) {
  g_crashDir = toWide(dir);
  while (!g_crashDir.empty() && (g_crashDir.back() == L'\\' || g_crashDir.back() == L'/')) g_crashDir.pop_back();
  SetUnhandledExceptionFilter(crashFilter);
}

// The classic Win32 way: drop the frame and cover the monitor; restore the saved placement on exit.
static WINDOWPLACEMENT g_placement = {sizeof(WINDOWPLACEMENT)};
static LONG g_style = 0;

bool platformSetFullscreen(SDL_Window* window, bool on) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo(window, &info)) return false;
  HWND hwnd = info.info.win.window;
  if (on) {
    g_style = GetWindowLongW(hwnd, GWL_STYLE);
    g_placement.length = sizeof(g_placement);
    GetWindowPlacement(hwnd, &g_placement);
    if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);  // a maximized window would stay clamped to the work area
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongW(hwnd, GWL_STYLE, (g_style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  } else {
    SetWindowLongW(hwnd, GWL_STYLE, g_style ? g_style : (WS_OVERLAPPEDWINDOW | WS_VISIBLE));
    SetWindowPlacement(hwnd, &g_placement);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  }
  return true;
}

#else

void platformSetupWindow(SDL_Window*) {}
int platformBlockSystemGestures() { return -1; }
void platformRestoreSystemGestures(int) {}
void platformOpenTouchSettings() {}
bool platformHasSystemGestures() { return false; }
bool platformSetFullscreen(SDL_Window*, bool) { return false; }

#include <csignal>
#include <cstdlib>
#include "log.hpp"
std::string platformDocumentsDir() {
  const char* h = std::getenv("HOME");
  return h ? std::string(h) + "/Documents" : std::string(".");
}
void platformOpenFolder(const std::string& path) {
  std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
  int r = std::system(cmd.c_str());
  (void)r;
}
void platformPreventSleep(bool) {}
static void crashSignal(int sig) {
  logLine("CRASH: signal %d", sig);
  logFlush();
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
void platformInstallCrashHandler(const std::string&) {
  std::signal(SIGSEGV, crashSignal);
  std::signal(SIGABRT, crashSignal);
}

#endif
