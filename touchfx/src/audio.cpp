#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#endif

namespace {

constexpr int kFft = 2048;
constexpr float kPiF = 3.14159265358979f;

void fft(std::vector<float>& re, std::vector<float>& im) {
  const int n = int(re.size());
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -2.f * kPiF / len;
    float wr = std::cos(ang), wi = std::sin(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; ++k) {
        int a = i + k, b = a + len / 2;
        float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

void sdlCallback(void* user, Uint8* stream, int len) {
  auto* self = static_cast<AudioInput*>(user);
  self->push(reinterpret_cast<const float*>(stream), len / int(sizeof(float)));
}

}  // namespace

AudioInput::~AudioInput() { close(); }

std::vector<std::string> AudioInput::captureDevices() {
  if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
  std::vector<std::string> out;
  int n = SDL_GetNumAudioDevices(1);
  for (int i = 0; i < n; ++i)
    if (const char* name = SDL_GetAudioDeviceName(i, 1)) out.push_back(name);
  return out;
}

bool AudioInput::loopbackSupported() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

bool AudioInput::open(const std::string& device) {
  close();
  device_ = device;
  error_.clear();
  {
    std::lock_guard<std::mutex> lk(mtx_);
    std::fill(ring_.begin(), ring_.end(), 0.f);
    write_ = 0;
    received_ = 0;
  }
  f_ = AudioFeatures();
  intervals_.clear();

  if (device == kAudioTest) {
    rate_ = 48000;
    open_ = true;
    return true;
  }
  if (device == kAudioLoopback) {
#ifdef _WIN32
    quit_ = false;
    {
      std::lock_guard<std::mutex> lk(errMtx_);
      threadError_.clear();
    }
    loop_ = std::thread(&AudioInput::loopbackThread, this);
    open_ = true;
    return true;
#else
    error_ = "System audio capture is only available on Windows.";
    return false;
#endif
  }

  if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    error_ = std::string("Audio unavailable: ") + SDL_GetError();
    return false;
  }
  SDL_AudioSpec want{}, have{};
  want.freq = 48000;
  want.format = AUDIO_F32SYS;
  want.channels = 1;  // SDL mixes the input down to mono for us
  want.samples = 512;
  want.callback = sdlCallback;
  want.userdata = this;
  dev_ = SDL_OpenAudioDevice(device.empty() ? nullptr : device.c_str(), 1, &want, &have,
                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (!dev_) {
    error_ = std::string("Could not open the audio input: ") + SDL_GetError();
    return false;
  }
  rate_ = have.freq;
  SDL_PauseAudioDevice(dev_, 0);
  open_ = true;
  return true;
}

void AudioInput::close() {
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
  if (loop_.joinable()) {
    quit_ = true;
    loop_.join();
  }
  open_ = false;
}

void AudioInput::push(const float* mono, int n) {
  std::lock_guard<std::mutex> lk(mtx_);
  const size_t sz = ring_.size();
  for (int i = 0; i < n; ++i) {
    ring_[write_] = mono[i];
    write_ = (write_ + 1) % sz;
  }
  received_ += uint64_t(n);
}

// A 128 BPM kick + hi-hat + bass line, so audio mode can be tried without music.
void AudioInput::generateTest(float dt) {
  const int rate = 48000;
  testCarry_ += dt * rate;
  int n = int(testCarry_);
  testCarry_ -= n;
  n = std::min(n, 8192);
  std::vector<float> buf(static_cast<size_t>(n));
  const double beatLen = 60.0 / 128.0;
  for (int i = 0; i < n; ++i) {
    double t = testTime_ + double(i) / rate;
    double bt = std::fmod(t, beatLen);
    double half = std::fmod(t + beatLen * 0.5, beatLen);
    float kick = float(std::sin(2 * kPiF * (50.0 * bt + 60.0 * (1 - std::exp(-bt * 30)) / 30.0)) * std::exp(-bt * 9.0));
    testNoise_ = testNoise_ * 1664525u + 1013904223u;
    float noise = (float(testNoise_ >> 8) / float(1u << 24)) * 2.f - 1.f;
    float hat = noise * float(std::exp(-half * 60.0)) * 0.25f;
    int bar = int(t / (beatLen * 4)) % 4;
    static const float notes[4] = {55.f, 55.f, 65.4f, 49.f};
    float bass = float(std::sin(2 * kPiF * notes[bar] * t)) * 0.18f;
    buf[size_t(i)] = kick * 0.8f + hat + bass;
  }
  testTime_ += double(n) / rate;
  push(buf.data(), n);
}

void AudioInput::update(float dt, float sensitivity) {
  f_.onset = false;
  if (!open_) {
    f_ = AudioFeatures();
    return;
  }
  if (device_ == kAudioTest) generateTest(dt);
#ifdef _WIN32
  {
    std::lock_guard<std::mutex> lk(errMtx_);
    if (!threadError_.empty()) error_ = threadError_;
  }
#endif
  now_ += dt;

  // newest kFft samples
  if (window_.empty()) {
    window_.resize(kFft);
    for (int i = 0; i < kFft; ++i) window_[i] = 0.5f - 0.5f * std::cos(2.f * kPiF * i / (kFft - 1));
    re_.resize(kFft);
    im_.resize(kFft);
  }
  float rms = 0;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    const size_t sz = ring_.size();
    size_t start = (write_ + sz - kFft) % sz;
    for (int i = 0; i < kFft; ++i) {
      float v = ring_[(start + i) % sz];
      rms += v * v;
      re_[i] = v * window_[i];
      im_[i] = 0;
    }
  }
  rms = std::sqrt(rms / kFft);
  fft(re_, im_);

  // band energies
  const float binHz = float(rate_.load()) / kFft;
  auto band = [&](float lo, float hi) {
    int a = std::max(1, int(lo / binHz)), b = std::min(kFft / 2 - 1, int(hi / binHz));
    float e = 0;
    for (int i = a; i <= b; ++i) e += re_[i] * re_[i] + im_[i] * im_[i];
    return std::sqrt(e / float(std::max(1, b - a + 1))) / kFft;
  };
  float raw[4] = {rms, band(30, 150), band(150, 2000), band(2000, 12000)};

  // noise gate with a short hold so the virtual fingers don't flicker
  float gate = 0.02f * std::pow(0.05f, sensitivity);  // 0.02 (deaf) .. 0.001 (very sensitive)
  if (rms > gate) gateHold_ = 0.6;
  else gateHold_ -= dt;
  f_.active = gateHold_ > 0;

  // auto gain: each band is divided by its own slowly decaying peak
  float decay = std::exp(-dt / 5.f);
  float norm[4];
  for (int i = 0; i < 4; ++i) {
    float floorV = (i == 0 ? gate : gate * 0.02f);
    peak_[i] = std::max(raw[i], std::max(peak_[i] * decay, floorV));
    norm[i] = std::clamp(raw[i] / peak_[i], 0.f, 1.f);
    if (!f_.active) norm[i] = 0;
  }
  auto smooth = [&](float& v, float target) { v += (target - v) * (target > v ? 0.6f : 0.12f); };
  smooth(f_.level, norm[0]);
  smooth(f_.bass, norm[1]);
  smooth(f_.mid, norm[2]);
  smooth(f_.high, norm[3]);

  // 32-band log spectrum (for the Spectrum effect), one shared auto gain so band heights stay comparable
  {
    float bandsRaw[32];
    float mx = 0;
    for (int b = 0; b < 32; ++b) {
      float lo = 40.f * std::pow(400.f, b / 32.f), hi = 40.f * std::pow(400.f, (b + 1) / 32.f);
      bandsRaw[b] = band(lo, std::max(hi, lo + binHz)) * (1.f + b * 0.08f);  // tilt: highs are naturally weaker
      mx = std::max(mx, bandsRaw[b]);
    }
    specPeak_ = std::max(mx, std::max(specPeak_ * decay, gate * 0.02f));
    for (int b = 0; b < 32; ++b) {
      float t = f_.active ? std::clamp(bandsRaw[b] / specPeak_, 0.f, 1.f) : 0.f;
      f_.spectrum[b] += (t - f_.spectrum[b]) * (t > f_.spectrum[b] ? 0.7f : 0.18f);
    }
  }

  // hits: bass jumps above its running average
  float b = norm[1];
  bassAvg_ += (b - bassAvg_) * std::min(1.f, dt * 3.f);
  if (f_.active && b > bassAvg_ * 1.3f + 0.12f && now_ - lastOnset_ > 0.22) {
    double interval = now_ - lastOnset_;
    lastOnset_ = now_;
    f_.onset = true;
    f_.pulse = 1.f;
    if (interval > 0.25 && interval < 2.0) {
      float iv = float(interval);
      while (iv < 60.f / 170.f) iv *= 2.f;  // fold into 75..170 BPM
      while (iv > 60.f / 75.f) iv *= 0.5f;
      intervals_.push_back(iv);
      if (intervals_.size() > 16) intervals_.erase(intervals_.begin());
      if (intervals_.size() >= 4) {
        std::vector<float> s = intervals_;
        std::nth_element(s.begin(), s.begin() + s.size() / 2, s.end());
        f_.bpm = 60.f / s[s.size() / 2];
      }
    }
  }
  f_.pulse *= std::exp(-dt * 7.f);
}

#ifdef _WIN32
// Captures what the PC is playing (WASAPI loopback of the default output device).
void AudioInput::loopbackThread() {
  auto fail = [&](const char* msg) {
    std::lock_guard<std::mutex> lk(errMtx_);
    threadError_ = msg;
  };
  HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* en = nullptr;
  IMMDevice* dev = nullptr;
  IAudioClient* client = nullptr;
  IAudioCaptureClient* cap = nullptr;
  WAVEFORMATEX* fmt = nullptr;
  do {
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&en))) {
      fail("System audio: could not list audio devices.");
      break;
    }
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
      fail("System audio: no playback device found.");
      break;
    }
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) {
      fail("System audio: could not open the playback device.");
      break;
    }
    if (FAILED(client->GetMixFormat(&fmt))) {
      fail("System audio: unknown audio format.");
      break;
    }
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr))) {
      fail("System audio: loopback capture refused.");
      break;
    }
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap))) {
      fail("System audio: capture service unavailable.");
      break;
    }
    // Work out the sample format (float32 or 16-bit PCM, possibly wrapped in WAVE_FORMAT_EXTENSIBLE).
    WORD tag = fmt->wFormatTag;
    if (tag == 0xFFFE && fmt->cbSize >= 22) {
      DWORD sub = 0;  // first DWORD of the SubFormat GUID is the real format tag
      std::memcpy(&sub, reinterpret_cast<const BYTE*>(fmt) + 18 + 2 + 4, sizeof(sub));
      tag = WORD(sub);
    }
    const bool isFloat = tag == 3 && fmt->wBitsPerSample == 32;
    const bool isPcm16 = tag == 1 && fmt->wBitsPerSample == 16;
    if (!isFloat && !isPcm16) {
      fail("System audio: unsupported audio format.");
      break;
    }
    const int ch = std::max<int>(1, fmt->nChannels);
    rate_ = int(fmt->nSamplesPerSec);
    client->Start();
    std::vector<float> mono;
    while (!quit_) {
      Sleep(5);
      UINT32 packet = 0;
      if (FAILED(cap->GetNextPacketSize(&packet))) break;
      while (packet > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        mono.assign(frames, 0.f);
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
          for (UINT32 i = 0; i < frames; ++i) {
            float s = 0;
            for (int c = 0; c < ch; ++c) {
              if (isFloat) s += reinterpret_cast<const float*>(data)[i * ch + c];
              else s += reinterpret_cast<const int16_t*>(data)[i * ch + c] / 32768.f;
            }
            mono[i] = s / ch;
          }
        }
        push(mono.data(), int(frames));
        cap->ReleaseBuffer(frames);
        if (FAILED(cap->GetNextPacketSize(&packet))) break;
      }
    }
    client->Stop();
  } while (false);
  if (cap) cap->Release();
  if (client) client->Release();
  if (dev) dev->Release();
  if (en) en->Release();
  if (fmt) CoTaskMemFree(fmt);
  if (SUCCEEDED(hrInit)) CoUninitialize();
}
#else
void AudioInput::loopbackThread() {}
#endif
