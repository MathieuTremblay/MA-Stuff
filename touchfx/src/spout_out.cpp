#include "spout_out.hpp"

#if defined(_WIN32) && defined(TOUCHFX_SPOUT)
#include "SpoutSender.h"

SpoutOut::~SpoutOut() { stop(); }
bool SpoutOut::available() { return true; }

bool SpoutOut::send(const std::string& name, unsigned int texture, int width, int height) {
  auto* s = static_cast<SpoutSender*>(sender_);
  if (s && name != name_) {
    stop();
    s = nullptr;
  }
  if (!s) {
    s = new SpoutSender();
    s->SetSenderName(name.c_str());
    sender_ = s;
    name_ = name;
    error_.clear();
  }
  // bInvert: OpenGL textures are bottom-up, Spout shares top-down like DirectX.
  bool ok = s->SendTexture(texture, 0x0DE1 /*GL_TEXTURE_2D*/, unsigned(width), unsigned(height), true, 0);
  if (!ok) error_ = "Spout could not send (graphics driver without DirectX/OpenGL sharing?).";
  else error_.clear();
  return ok;
}

void SpoutOut::stop() {
  if (auto* s = static_cast<SpoutSender*>(sender_)) {
    s->ReleaseSender();
    delete s;
  }
  sender_ = nullptr;
}

#else

SpoutOut::~SpoutOut() {}
bool SpoutOut::available() { return false; }
bool SpoutOut::send(const std::string&, unsigned int, int, int) {
  error_ = "Spout is only available on Windows.";
  return false;
}
void SpoutOut::stop() {}

#endif
