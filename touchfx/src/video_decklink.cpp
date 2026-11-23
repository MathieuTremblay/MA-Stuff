// Blackmagic DeckLink / UltraStudio / Intensity input through the DeckLink API (COM, provided by the
// Blackmagic Desktop Video driver). The input format is detected automatically.
#include "video_in.hpp"

#ifdef _WIN32
#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "DeckLinkAPI.h"

namespace {

std::string fromBstr(BSTR b) {
  if (!b) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, b, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, b, -1, &s[0], n, nullptr, nullptr);
  SysFreeString(b);
  return s;
}

struct ComScope {
  bool ok;
  ComScope() : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
  ~ComScope() {
    if (ok) CoUninitialize();
  }
};

// All DeckLink devices (name, device). Caller releases.
std::vector<std::pair<std::string, IDeckLink*>> enumDeckLinks(std::string* err) {
  std::vector<std::pair<std::string, IDeckLink*>> out;
  IDeckLinkIterator* it = nullptr;
  if (FAILED(CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&it)) || !it) {
    if (err) *err = "Blackmagic Desktop Video is not installed (needed for DeckLink / UltraStudio).";
    return out;
  }
  IDeckLink* d = nullptr;
  while (it->Next(&d) == S_OK) {
    BSTR name = nullptr;
    std::string s = "DeckLink " + std::to_string(out.size() + 1);
    if (SUCCEEDED(d->GetDisplayName(&name))) s = fromBstr(name);
    // only devices that can capture
    IDeckLinkInput* in = nullptr;
    if (SUCCEEDED(d->QueryInterface(IID_IDeckLinkInput, (void**)&in)) && in) {
      in->Release();
      out.push_back({s, d});
    } else {
      d->Release();
    }
  }
  it->Release();
  return out;
}

class DeckLinkBackend : public VideoBackend, public IDeckLinkInputCallback {
 public:
  // ---- VideoBackend ----
  bool open(const std::string& source, std::string& err) override {
    com_ = new ComScope();
    auto devs = enumDeckLinks(&err);
    if (devs.empty()) {
      if (err.empty()) err = "No Blackmagic capture device found.";
      return false;
    }
    IDeckLink* dev = nullptr;
    for (auto& d : devs) {
      if (!dev && (source.empty() || d.first == source)) {
        dev = d.second;
        name_ = d.first;
      } else {
        d.second->Release();
      }
    }
    if (!dev) {
      err = "\"" + source + "\" is not connected.";
      return false;
    }
    HRESULT hr = dev->QueryInterface(IID_IDeckLinkInput, (void**)&input_);
    dev->Release();
    if (FAILED(hr) || !input_) {
      err = "\"" + name_ + "\" cannot capture.";
      return false;
    }
    input_->SetCallback(this);
    // start in 1080p25 with format detection: the driver tells us the real format right away
    pixel_ = bmdFormat8BitYUV;
    hr = input_->EnableVideoInput(bmdModeHD1080p25, pixel_, bmdVideoInputEnableFormatDetection);
    if (FAILED(hr)) hr = input_->EnableVideoInput(bmdModeHD1080i50, pixel_, bmdVideoInputEnableFormatDetection);
    if (FAILED(hr)) {
      err = "\"" + name_ + "\" is busy (used by another program?) or does not detect formats.";
      return false;
    }
    if (FAILED(input_->StartStreams())) {
      err = "Could not start \"" + name_ + "\".";
      return false;
    }
    return true;
  }

  int read(RawFrame& f, int timeoutMs) override {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return fresh_; });
    if (!fresh_) return 0;
    fresh_ = false;
    std::swap(f, frame_);
    return 1;
  }

  void close() override {
    if (input_) {
      input_->StopStreams();
      input_->DisableVideoInput();
      input_->SetCallback(nullptr);
      input_->Release();
      input_ = nullptr;
    }
    delete com_;
    com_ = nullptr;
  }

  std::string status() override {
    std::lock_guard<std::mutex> lk(m_);
    return noSignal_ ? "No signal on \"" + name_ + "\"" : "";
  }

  // ---- IUnknown (the object lives as long as the backend; the driver only borrows it) ----
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
    if (iid == IID_IUnknown || iid == IID_IDeckLinkInputCallback) {
      *ppv = static_cast<IDeckLinkInputCallback*>(this);
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }

  // ---- IDeckLinkInputCallback ----
  HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode* mode,
                                                     BMDDetectedVideoInputFormatFlags flags) override {
    if (!input_ || !mode) return S_OK;
    BMDPixelFormat px = (flags & bmdDetectedVideoInputRGB444) ? bmdFormat8BitBGRA : bmdFormat8BitYUV;
    // restart the input in the detected mode
    input_->PauseStreams();
    input_->EnableVideoInput(mode->GetDisplayMode(), px, bmdVideoInputEnableFormatDetection);
    input_->FlushStreams();
    input_->StartStreams();
    BSTR nm = nullptr;
    std::string modeName = SUCCEEDED(mode->GetName(&nm)) ? fromBstr(nm) : "";
    std::lock_guard<std::mutex> lk(m_);
    pixel_ = px;
    info_ = modeName + (px == bmdFormat8BitBGRA ? " RGB  " : " YUV  ") + name_;
    (void)events;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame* vf, IDeckLinkAudioInputPacket*) override {
    if (!vf) return S_OK;
    bool none = (vf->GetFlags() & bmdFrameHasNoInputSource) != 0;
    {
      std::lock_guard<std::mutex> lk(m_);
      noSignal_ = none;
    }
    if (none) return S_OK;
    void* bytes = nullptr;
    if (FAILED(vf->GetBytes(&bytes)) || !bytes) return S_OK;
    long w = vf->GetWidth(), h = vf->GetHeight(), rb = vf->GetRowBytes();
    BMDPixelFormat pf = vf->GetPixelFormat();
    if (w <= 0 || h <= 0 || rb <= 0) return S_OK;
    if (pf != bmdFormat8BitYUV && pf != bmdFormat8BitBGRA) return S_OK;
    std::lock_guard<std::mutex> lk(m_);
    frame_.w = int(w);
    frame_.h = int(h);
    frame_.fmt = pf == bmdFormat8BitYUV ? PIX_UYVY : PIX_BGRA;
    frame_.stride = int(rb);
    frame_.data.assign((const uint8_t*)bytes, (const uint8_t*)bytes + size_t(rb) * h);
    frame_.info = info_.empty() ? std::to_string(w) + "x" + std::to_string(h) + "  " + name_ : info_;
    fresh_ = true;
    cv_.notify_one();
    return S_OK;
  }

 private:
  ComScope* com_ = nullptr;
  IDeckLinkInput* input_ = nullptr;
  BMDPixelFormat pixel_ = bmdFormat8BitYUV;
  std::string name_, info_;
  std::mutex m_;
  std::condition_variable cv_;
  RawFrame frame_;
  bool fresh_ = false, noSignal_ = false;
};

}  // namespace

std::unique_ptr<VideoBackend> makeDeckLinkBackend() { return std::make_unique<DeckLinkBackend>(); }

std::vector<std::string> listDeckLinkDevices() {
  ComScope com;
  std::vector<std::string> v;
  for (auto& d : enumDeckLinks(nullptr)) {
    v.push_back(d.first);
    d.second->Release();
  }
  return v;
}

#else

namespace {
class NoDeckLink : public VideoBackend {
 public:
  bool open(const std::string&, std::string& err) override {
    err = "Blackmagic input is only supported on Windows.";
    return false;
  }
  int read(RawFrame&, int) override { return -1; }
  void close() override {}
};
}  // namespace

std::unique_ptr<VideoBackend> makeDeckLinkBackend() { return std::make_unique<NoDeckLink>(); }
std::vector<std::string> listDeckLinkDevices() { return {}; }

#endif
