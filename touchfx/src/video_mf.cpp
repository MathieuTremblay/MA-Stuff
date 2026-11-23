// Capture cards and cameras through Windows Media Foundation (USB capture devices such as AV Matrix,
// Elgato, Magewell USB; webcams; and cards whose driver shows up as a Windows camera).
#include "video_in.hpp"

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

template <class T>
void release(T*& p) {
  if (p) p->Release();
  p = nullptr;
}

std::string narrow(const wchar_t* w) {
  if (!w) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

// RAII for COM + Media Foundation on the calling thread
struct MfScope {
  bool com = false, mf = false;
  MfScope() {
    com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
  }
  ~MfScope() {
    if (mf) MFShutdown();
    if (com) CoUninitialize();
  }
};

// All video capture devices: (name, activate object). Caller releases the activates.
std::vector<std::pair<std::string, IMFActivate*>> enumDevices() {
  std::vector<std::pair<std::string, IMFActivate*>> out;
  IMFAttributes* attr = nullptr;
  if (FAILED(MFCreateAttributes(&attr, 1))) return out;
  attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate** devs = nullptr;
  UINT32 n = 0;
  if (SUCCEEDED(MFEnumDeviceSources(attr, &devs, &n))) {
    for (UINT32 i = 0; i < n; ++i) {
      WCHAR* name = nullptr;
      UINT32 len = 0;
      std::string s = "Device " + std::to_string(i + 1);
      if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) {
        s = narrow(name);
        CoTaskMemFree(name);
      }
      out.push_back({s, devs[i]});
    }
    CoTaskMemFree(devs);
  }
  attr->Release();
  return out;
}

int pixFmtOf(const GUID& g) {
  if (g == MFVideoFormat_NV12) return PIX_NV12;
  if (g == MFVideoFormat_YUY2) return PIX_YUY2;
  if (g == MFVideoFormat_UYVY) return PIX_UYVY;
  if (g == MFVideoFormat_RGB32 || g == MFVideoFormat_ARGB32) return PIX_BGRA;
  return -1;
}

const char* fmtName(int f) {
  switch (f) {
    case PIX_NV12: return "NV12";
    case PIX_YUY2: return "YUY2";
    case PIX_UYVY: return "UYVY";
    default: return "RGB";
  }
}

class MfBackend : public VideoBackend {
 public:
  bool open(const std::string& source, std::string& err) override {
    scope_ = new MfScope();
    if (!scope_->mf) {
      err = "Windows Media Foundation is not available (Windows N edition without the Media Feature Pack?).";
      return false;
    }
    auto devs = enumDevices();
    IMFActivate* chosen = nullptr;
    for (auto& d : devs)
      if (!chosen && (source.empty() || d.first == source)) {
        chosen = d.second;
        name_ = d.first;
      }
    if (!chosen) {
      err = devs.empty() ? "No capture card or camera found." : "\"" + source + "\" is not connected.";
      for (auto& d : devs) d.second->Release();
      return false;
    }
    IMFMediaSource* src = nullptr;
    HRESULT hr = chosen->ActivateObject(IID_PPV_ARGS(&src));
    for (auto& d : devs) d.second->Release();
    if (FAILED(hr)) {
      err = "Could not open \"" + name_ + "\" (in use by another program?).";
      return false;
    }
    IMFAttributes* ra = nullptr;
    MFCreateAttributes(&ra, 2);
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);  // lets the reader convert if needed
    ra->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    hr = MFCreateSourceReaderFromMediaSource(src, ra, &reader_);
    ra->Release();
    src->Release();  // the reader keeps it
    if (FAILED(hr)) {
      err = "Could not start \"" + name_ + "\".";
      return false;
    }
    if (!chooseFormat()) {
      err = "\"" + name_ + "\" has no video format TouchFX can use.";
      return false;
    }
    return true;
  }

  int read(RawFrame& f, int) override {
    DWORD stream = 0, flags = 0;
    LONGLONG ts = 0;
    IMFSample* sample = nullptr;
    HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample);
    if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
      release(sample);
      status_ = "\"" + name_ + "\" stopped (unplugged?)";
      return -1;
    }
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) readCurrentType();
    if (!sample) return 0;
    IMFMediaBuffer* buf = nullptr;
    int r = 0;
    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && w_ > 0 && h_ > 0 && fmt_ >= 0) {
      int rowBytes = fmt_ == PIX_BGRA ? w_ * 4 : (fmt_ == PIX_NV12 ? w_ : w_ * 2);
      int rows = fmt_ == PIX_NV12 ? h_ * 3 / 2 : h_;
      f.w = w_;
      f.h = h_;
      f.fmt = fmt_;
      f.stride = rowBytes;
      f.data.resize(size_t(rowBytes) * rows);
      IMF2DBuffer* b2 = nullptr;
      BYTE* p = nullptr;
      LONG pitch = 0;
      if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&p, &pitch))) {
        copyRows(f, p, pitch, rows, rowBytes);  // pitch < 0: bottom-up, Lock2D gives the top row
        b2->Unlock2D();
        r = 1;
      } else {
        DWORD len = 0;
        if (SUCCEEDED(buf->Lock(&p, nullptr, &len))) {
          LONG stride = stride_ ? stride_ : rowBytes;
          const BYTE* top = stride < 0 ? p + size_t(-stride) * (h_ - 1) : p;
          if (size_t(len) >= size_t(std::abs(stride)) * rows) {
            copyRows(f, top, stride, rows, rowBytes);
            r = 1;
          }
          buf->Unlock();
        }
      }
      release(b2);
      f.info = info_;
    }
    release(buf);
    release(sample);
    status_.clear();
    return r;
  }

  void close() override {
    release(reader_);
    delete scope_;
    scope_ = nullptr;
  }
  std::string status() override { return status_; }

 private:
  void copyRows(RawFrame& f, const BYTE* top, LONG pitch, int rows, int rowBytes) {
    for (int y = 0; y < rows; ++y) std::memcpy(f.data.data() + size_t(y) * rowBytes, top + ptrdiff_t(pitch) * y, rowBytes);
  }

  bool chooseFormat() {
    // best native format: largest picture up to 1080p, then highest frame rate (up to 60), uncompressed first
    IMFMediaType* best = nullptr;
    double bestScore = -1;
    for (DWORD i = 0;; ++i) {
      IMFMediaType* t = nullptr;
      if (FAILED(reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t))) break;
      UINT32 w = 0, h = 0, num = 0, den = 1;
      MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
      MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
      GUID sub{};
      t->GetGUID(MF_MT_SUBTYPE, &sub);
      double fps = den ? double(num) / den : 0;
      double px = double(w) * h;
      double score = (px <= 1920.0 * 1080.0 ? px : 1920.0 * 1080.0 - px * 0.01) * 1000.0 + std::min(fps, 60.0) * 10.0 +
                     (pixFmtOf(sub) >= 0 ? 5.0 : 0.0);
      if (score > bestScore) {
        release(best);
        best = t;
        bestScore = score;
      } else {
        t->Release();
      }
    }
    if (!best) return false;
    reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, best);
    GUID sub{};
    best->GetGUID(MF_MT_SUBTYPE, &sub);
    best->Release();
    if (pixFmtOf(sub) < 0) {
      // compressed (MJPEG, H.264...): ask the reader to decode to NV12, else to RGB
      for (const GUID& g : {MFVideoFormat_NV12, MFVideoFormat_YUY2, MFVideoFormat_RGB32}) {
        IMFMediaType* o = nullptr;
        MFCreateMediaType(&o);
        o->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        o->SetGUID(MF_MT_SUBTYPE, g);
        HRESULT hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, o);
        o->Release();
        if (SUCCEEDED(hr)) break;
      }
    }
    reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    return readCurrentType();
  }

  bool readCurrentType() {
    IMFMediaType* t = nullptr;
    if (FAILED(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t))) return false;
    UINT32 w = 0, h = 0, num = 0, den = 1;
    MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
    MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
    GUID sub{};
    t->GetGUID(MF_MT_SUBTYPE, &sub);
    stride_ = LONG(MFGetAttributeUINT32(t, MF_MT_DEFAULT_STRIDE, 0));
    t->Release();
    w_ = int(w);
    h_ = int(h);
    fmt_ = pixFmtOf(sub);
    if (fmt_ == PIX_NV12 || fmt_ == PIX_YUY2 || fmt_ == PIX_UYVY) w_ &= ~1;  // even width for 4:2:x
    if (fmt_ == PIX_NV12) h_ &= ~1;
    char b[200];
    std::snprintf(b, sizeof(b), "%dx%d %.2f fps %s  %s", w_, h_, den ? double(num) / den : 0.0,
                  fmt_ >= 0 ? fmtName(fmt_) : "?", name_.c_str());
    info_ = b;
    return fmt_ >= 0 && w_ > 0 && h_ > 0;
  }

  MfScope* scope_ = nullptr;
  IMFSourceReader* reader_ = nullptr;
  std::string name_, status_, info_;
  int w_ = 0, h_ = 0, fmt_ = -1;
  LONG stride_ = 0;
};

}  // namespace

std::unique_ptr<VideoBackend> makeCaptureBackend() { return std::make_unique<MfBackend>(); }

std::vector<std::string> listCaptureDevices() {
  MfScope scope;
  std::vector<std::string> v;
  if (!scope.mf) return v;
  for (auto& d : enumDevices()) {
    v.push_back(d.first);
    d.second->Release();
  }
  return v;
}

#else  // not Windows: no capture backend (the test source covers development)

namespace {
class NoCapture : public VideoBackend {
 public:
  bool open(const std::string&, std::string& err) override {
    err = "Capture cards are only supported on Windows.";
    return false;
  }
  int read(RawFrame&, int) override { return -1; }
  void close() override {}
};
}  // namespace

std::unique_ptr<VideoBackend> makeCaptureBackend() { return std::make_unique<NoCapture>(); }
std::vector<std::string> listCaptureDevices() { return {}; }

#endif
