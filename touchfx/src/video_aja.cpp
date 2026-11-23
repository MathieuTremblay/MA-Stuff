// AJA capture cards (KONA, Io, Corvid...) through the open-source AJA NTV2 SDK (libajantv2, MIT).
// Needs the AJA driver installed. Each SDI / HDMI input is listed as its own source.
#include "video_in.hpp"

#ifdef TOUCHFX_AJA
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>

#include "ntv2card.h"
#include "ntv2devicescanner.h"
#include "ntv2formatdescriptor.h"
#include "ntv2signalrouter.h"
#include "ntv2utils.h"

namespace {

struct AjaInput {
  std::string label;  // "KONA 5 - SDI 1"
  ULWord device = 0;
  NTV2InputSource source = NTV2_INPUTSOURCE_SDI1;
  NTV2Channel channel = NTV2_CHANNEL1;
};

std::vector<AjaInput> enumAja() {
  std::vector<AjaInput> out;
  for (ULWord i = 0; i < 16; ++i) {
    CNTV2Card card;
    if (!CNTV2DeviceScanner::GetDeviceAtIndex(i, card)) break;
    std::string name = card.GetDisplayName();
    UWord sdi = card.features().GetNumVideoInputs();
    UWord hdmi = card.features().GetNumHDMIVideoInputs();
    for (UWord k = 0; k < sdi && k < 4; ++k) {
      AjaInput in;
      in.label = name + " - SDI " + std::to_string(k + 1);
      in.device = i;
      in.source = ::GetNTV2InputSourceForIndex(k, NTV2_IOKINDS_SDI);
      in.channel = NTV2Channel(k);
      out.push_back(in);
    }
    for (UWord k = 0; k < hdmi && k < 2; ++k) {
      AjaInput in;
      in.label = name + " - HDMI " + std::to_string(k + 1);
      in.device = i;
      in.source = ::GetNTV2InputSourceForIndex(k, NTV2_IOKINDS_HDMI);
      in.channel = NTV2Channel(k);
      out.push_back(in);
    }
  }
  return out;
}

class AjaBackend : public VideoBackend {
 public:
  bool open(const std::string& source, std::string& err) override {
    auto inputs = enumAja();
    if (inputs.empty()) {
      err = "No AJA device found (is the AJA driver installed?).";
      return false;
    }
    const AjaInput* pick = nullptr;
    for (const AjaInput& in : inputs)
      if (!pick && (source.empty() || in.label == source)) pick = &in;
    if (!pick) {
      err = "\"" + source + "\" is not connected.";
      return false;
    }
    in_ = *pick;
    if (!CNTV2DeviceScanner::GetDeviceAtIndex(in_.device, card_)) {
      err = "Could not open \"" + in_.label + "\".";
      return false;
    }
    card_.GetTaskMode(savedTask_);
    card_.SetTaskMode(NTV2_OEM_TASKS);
    card_.EnableChannel(in_.channel);
    card_.EnableInputInterrupt(in_.channel);
    card_.SubscribeInputVerticalEvent(in_.channel);
    if (card_.features().HasBiDirectionalSDI() && NTV2_INPUT_SOURCE_IS_SDI(in_.source))
      card_.SetSDITransmitEnable(in_.channel, false);
    opened_ = true;
    return true;
  }

  int read(RawFrame& f, int timeoutMs) override {
    auto now = std::chrono::steady_clock::now();
    // (re)start when the input format changes or appears
    if (now - lastCheck_ > std::chrono::milliseconds(500)) {
      lastCheck_ = now;
      NTV2VideoFormat vf = card_.GetInputVideoFormat(in_.source);
      if (vf != format_) {
        stopCapture();
        format_ = vf;
        if (vf == NTV2_FORMAT_UNKNOWN) {
          status_ = "No signal on \"" + in_.label + "\"";
        } else if (!startCapture()) {
          status_ = "\"" + in_.label + "\" cannot capture " + ::NTV2VideoFormatToString(vf);
          format_ = NTV2_FORMAT_UNKNOWN;
        } else {
          status_.clear();
        }
      }
    }
    if (!running_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeoutMs, 50)));
      return 0;
    }
    AUTOCIRCULATE_STATUS st;
    card_.AutoCirculateGetStatus(in_.channel, st);
    if (!(st.IsRunning() && st.HasAvailableInputFrame())) {
      card_.WaitForInputVerticalInterrupt(in_.channel);
      return 0;
    }
    buf_.resize(desc_.GetTotalBytes());
    AUTOCIRCULATE_TRANSFER xfer;
    xfer.SetVideoBuffer(reinterpret_cast<ULWord*>(buf_.data()), ULWord(buf_.size()));
    if (!card_.AutoCirculateTransfer(in_.channel, xfer)) return 0;
    f.w = int(desc_.GetRasterWidth());
    f.h = int(desc_.GetRasterHeight());
    f.stride = int(desc_.GetBytesPerRow());
    f.fmt = rgb_ ? PIX_BGRA : PIX_UYVY;
    f.data.assign(buf_.begin(), buf_.begin() + size_t(f.stride) * f.h);
    f.info = std::string(::NTV2VideoFormatToString(format_)) + "  " + in_.label;
    return 1;
  }

  void close() override {
    stopCapture();
    if (opened_) {
      card_.UnsubscribeInputVerticalEvent(in_.channel);
      card_.SetTaskMode(savedTask_);
      card_.Close();
    }
    opened_ = false;
  }

  std::string status() override { return status_; }

 private:
  bool startCapture() {
    if (!card_.features().CanDoVideoFormat(format_)) return false;
    if (NTV2_IS_4K_VIDEO_FORMAT(format_) || NTV2_IS_QUAD_QUAD_FORMAT(format_)) return false;  // up to 1080p
    rgb_ = false;
    if (NTV2_INPUT_SOURCE_IS_HDMI(in_.source)) {
      NTV2LHIHDMIColorSpace cs = NTV2_LHIHDMIColorSpaceYCbCr;
      card_.GetHDMIInputColor(cs, in_.channel);
      rgb_ = cs == NTV2_LHIHDMIColorSpaceRGB;
    }
    NTV2PixelFormat pf = rgb_ ? NTV2_FBF_ARGB : NTV2_FBF_8BIT_YCBCR;  // BGRA bytes / UYVY bytes
    card_.SetReference(NTV2_REFERENCE_FREERUN);
    card_.SetVideoFormat(format_, false, false, in_.channel);
    card_.SetVANCMode(NTV2_VANCMODE_OFF, in_.channel);
    card_.SetFrameBufferFormat(in_.channel, pf);
    card_.SetMode(in_.channel, NTV2_MODE_CAPTURE);
    // route the input straight into the frame store
    card_.Connect(::GetFrameStoreInputXptFromChannel(in_.channel), ::GetInputSourceOutputXpt(in_.source, false, rgb_));
    desc_ = NTV2FormatDescriptor(format_, pf);
    card_.AutoCirculateStop(in_.channel);
    if (!card_.AutoCirculateInitForInput(in_.channel, 7)) return false;
    if (!card_.AutoCirculateStart(in_.channel)) return false;
    running_ = true;
    return true;
  }

  void stopCapture() {
    if (running_) card_.AutoCirculateStop(in_.channel);
    running_ = false;
  }

  CNTV2Card card_;
  AjaInput in_;
  NTV2TaskMode savedTask_ = NTV2_OEM_TASKS;
  NTV2VideoFormat format_ = NTV2_FORMAT_UNKNOWN;
  NTV2FormatDescriptor desc_;
  bool opened_ = false, running_ = false, rgb_ = false;
  std::vector<uint8_t> buf_;
  std::string status_;
  std::chrono::steady_clock::time_point lastCheck_{};
};

}  // namespace

std::unique_ptr<VideoBackend> makeAjaBackend() { return std::make_unique<AjaBackend>(); }

std::vector<std::string> listAjaDevices() {
  std::vector<std::string> v;
  for (const AjaInput& in : enumAja()) v.push_back(in.label);
  return v;
}

#else

namespace {
class NoAja : public VideoBackend {
 public:
  bool open(const std::string&, std::string& err) override {
    err = "This build of TouchFX has no AJA support.";
    return false;
  }
  int read(RawFrame&, int) override { return -1; }
  void close() override {}
};
}  // namespace

std::unique_ptr<VideoBackend> makeAjaBackend() { return std::make_unique<NoAja>(); }
std::vector<std::string> listAjaDevices() { return {}; }

#endif
