#include "process.hpp"

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
namespace {
// Windows command-line quoting (the rules CommandLineToArgvW / the C runtime use)
std::wstring quoteArg(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
  std::wstring q = L"\"";
  size_t bs = 0;
  for (wchar_t c : a) {
    if (c == L'\\') {
      ++bs;
    } else if (c == L'"') {
      q.append(bs * 2 + 1, L'\\');
      q += L'"';
      bs = 0;
    } else {
      q.append(bs, L'\\');
      q += c;
      bs = 0;
    }
  }
  q.append(bs * 2, L'\\');
  q += L'"';
  return q;
}
}  // namespace

Process::~Process() {
  kill();
  if (proc_) CloseHandle(proc_);
}

bool Process::start(const std::vector<std::string>& args, const std::string& logFile) {
  if (args.empty()) return false;
  std::wstring cmd;
  for (const std::string& a : args) {
    if (!cmd.empty()) cmd += L' ';
    cmd += quoteArg(fs::u8path(a).wstring());
  }
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE out = INVALID_HANDLE_VALUE;
  if (!logFile.empty())
    out = CreateFileW(fs::u8path(logFile).wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = out != INVALID_HANDLE_VALUE ? out : nul;
  si.hStdError = si.hStdOutput;
  PROCESS_INFORMATION pi{};
  std::wstring exe = fs::u8path(args[0]).wstring();
  BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                           nullptr, nullptr, &si, &pi);
  if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (!ok) return false;
  CloseHandle(pi.hThread);
  proc_ = pi.hProcess;
  exit_ = -1;
  return true;
}

int Process::poll() {
  if (!proc_) return exit_;
  if (WaitForSingleObject(proc_, 0) != WAIT_OBJECT_0) return -1;
  DWORD code = 1;
  GetExitCodeProcess(proc_, &code);
  CloseHandle(proc_);
  proc_ = nullptr;
  exit_ = int(code);
  return exit_;
}

void Process::kill() {
  if (!proc_) return;
  TerminateProcess(proc_, 1);
  WaitForSingleObject(proc_, 2000);
  CloseHandle(proc_);
  proc_ = nullptr;
  exit_ = 1;
}

std::string exeDir() {
  wchar_t buf[MAX_PATH * 2] = {0};
  GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
  fs::path p(buf);
  return p.parent_path().u8string() + "\\";
}

static std::string findTool(const char* name) {
  for (const char* sub : {"", "ffmpeg\\", "ffmpeg\\bin\\"}) {
    std::string p = exeDir() + sub + name;
    std::error_code ec;
    if (fs::exists(fs::u8path(p), ec)) return p;
  }
  return "";
}
std::string ffmpegPath() { return findTool("ffmpeg.exe"); }
std::string ffprobePath() { return findTool("ffprobe.exe"); }

#else

Process::~Process() { kill(); }

bool Process::start(const std::vector<std::string>& args, const std::string& logFile) {
  if (args.empty()) return false;
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    int out = logFile.empty() ? open("/dev/null", O_WRONLY) : open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int in = open("/dev/null", O_RDONLY);
    if (out >= 0) {
      dup2(out, 1);
      dup2(out, 2);
    }
    if (in >= 0) dup2(in, 0);
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  pid_ = pid;
  exit_ = -1;
  return true;
}

int Process::poll() {
  if (pid_ < 0) return exit_;
  int st = 0;
  pid_t r = waitpid(pid_, &st, WNOHANG);
  if (r == 0) return -1;
  pid_ = -1;
  exit_ = (r > 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : 1;
  return exit_;
}

void Process::kill() {
  if (pid_ < 0) return;
  ::kill(pid_, SIGKILL);
  int st = 0;
  waitpid(pid_, &st, 0);
  pid_ = -1;
  exit_ = 1;
}

std::string exeDir() {
  std::error_code ec;
  fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? std::string("./") : p.parent_path().u8string() + "/";
}

std::string ffmpegPath() {
  const char* e = std::getenv("TOUCHFX_FFMPEG");
  if (e && *e) return std::string(e) == "none" ? "" : e;
  return "ffmpeg";
}
std::string ffprobePath() {
  const char* e = std::getenv("TOUCHFX_FFPROBE");
  if (e && *e) return std::string(e) == "none" ? "" : e;
  return "ffprobe";
}
#endif

bool ffmpegAvailable() { return !ffmpegPath().empty() && !ffprobePath().empty(); }
