#include "media_lib.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "hap.hpp"
#include "log.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path toPath(const std::string& s) { return fs::u8path(s); }

std::string lowerExt(const std::string& path) {
  std::string e = toPath(path).extension().u8string();
  for (char& c : e) c = char(std::tolower((unsigned char)c));
  return e;
}

std::string newId() {
  static std::mt19937_64 rng(uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ std::random_device{}());
  char b[24];
  std::snprintf(b, sizeof(b), "%012llx", (unsigned long long)(rng() & 0xffffffffffffull));
  return b;
}

std::string readFile(const std::string& p) {
  std::ifstream f(toPath(p), std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// the last meaningful line FFmpeg wrote (its error message)
std::string lastLine(const std::string& log) {
  std::string best;
  std::istringstream in(log);
  for (std::string l; std::getline(in, l);) {
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    if (!l.empty()) best = l;
  }
  if (best.size() > 160) best = best.substr(0, 160) + "...";
  return best;
}
}  // namespace

bool isStillImage(const std::string& path) {
  std::string e = lowerExt(path);
  for (const char* x : {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp", ".tga"})
    if (e == x) return true;
  return false;
}

bool isMediaFile(const std::string& path) {
  if (isStillImage(path)) return true;
  std::string e = lowerExt(path);
  for (const char* x : {".mov", ".mp4", ".m4v", ".avi", ".mkv", ".webm", ".wmv", ".mpg", ".mpeg", ".mts", ".m2ts",
                        ".mxf", ".gif", ".flv", ".ts", ".dxv"})
    if (e == x) return true;
  return false;
}

// ---------------------------------------------------------------------------
struct MediaImporter::Job {
  enum Step { PROBE, CONVERT, COPY, THUMB };
  std::string id, src;
  int step = PROBE;
  Process proc;
  std::string probeFile, logFile, progFile, tmpOut;
  double duration = 0;
  bool alpha = false, still = false;
  float progress = 0;
  double lastRead = 0;
  // plain copy of a file that is already HAP (no FFmpeg needed)
  std::shared_ptr<std::atomic<int>> copyState;  // 0 running, 1 ok, 2 failed
};

MediaImporter::MediaImporter() = default;

MediaImporter::~MediaImporter() {
  if (job_) job_->proc.kill();
}

std::string MediaImporter::clipPath(const std::string& id) const { return (toPath(root_) / toPath(id + ".mov")).u8string(); }
std::string MediaImporter::thumbPath(const std::string& id) const { return (toPath(root_) / toPath(id + ".ppm")).u8string(); }

void MediaImporter::setRoot(const std::string& dir) {
  if (dir == root_) return;
  if (job_) {
    job_->proc.kill();
    job_.reset();
  }
  queue_.clear();
  root_ = dir;
  std::error_code ec;
  if (!root_.empty()) fs::create_directories(toPath(root_), ec);
}

int MediaImporter::import(MediaLibrary& lib, int group, const std::vector<std::string>& files) {
  if (lib.groups.empty()) lib.groups.push_back(MediaGroup{"Group 1", {}});
  group = std::clamp(group, 0, int(lib.groups.size()) - 1);
  int n = 0;
  for (const std::string& f : files) {
    if (!isMediaFile(f)) continue;
    MediaClip c;
    c.id = newId();
    c.name = toPath(f).stem().u8string();
    c.source = f;
    c.status = CLIP_CONVERTING;
    c.still = isStillImage(f);
    lib.groups[size_t(group)].clips.push_back(c);
    queue_.push_back(c.id);
    logLine("Media: importing \"%s\"", f.c_str());
    ++n;
  }
  return n;
}

void MediaImporter::resume(MediaLibrary& lib) {
  std::error_code ec;
  for (MediaGroup& g : lib.groups)
    for (MediaClip& c : g.clips) {
      if (c.status == CLIP_CONVERTING) {
        if (std::find(queue_.begin(), queue_.end(), c.id) == queue_.end() && !(job_ && job_->id == c.id))
          queue_.push_back(c.id);
      } else if (c.status == CLIP_READY && !fs::exists(toPath(clipPath(c.id)), ec)) {
        c.status = CLIP_FAILED;
        c.error = "The clip's file is missing from the project folder.";
      }
    }
}

void MediaImporter::remove(MediaLibrary& lib, const std::string& id) {
  if (job_ && job_->id == id) {
    job_->proc.kill();
    std::error_code ec;
    fs::remove(toPath(job_->tmpOut), ec);
    job_.reset();
  }
  queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
  for (MediaGroup& g : lib.groups)
    g.clips.erase(std::remove_if(g.clips.begin(), g.clips.end(), [&](const MediaClip& c) { return c.id == id; }),
                  g.clips.end());
  std::error_code ec;
  fs::remove(toPath(clipPath(id)), ec);
  fs::remove(toPath(thumbPath(id)), ec);
}

void MediaImporter::retry(MediaLibrary& lib, const std::string& id) {
  MediaClip* c = findClip(lib, id);
  if (!c || c->status == CLIP_READY) return;
  c->status = CLIP_CONVERTING;
  c->error.clear();
  if (std::find(queue_.begin(), queue_.end(), id) == queue_.end()) queue_.push_back(id);
}

float MediaImporter::progress(const std::string& id) const {
  if (job_ && job_->id == id) return job_->progress;
  return -1.f;
}

void MediaImporter::startNext(MediaLibrary& lib) {
  while (!queue_.empty() && !job_) {
    std::string id = queue_.front();
    queue_.pop_front();
    MediaClip* c = findClip(lib, id);
    if (!c || c->status != CLIP_CONVERTING) continue;
    auto j = std::make_unique<Job>();
    j->id = id;
    j->src = c->source;
    j->still = c->still;
    fs::path base = toPath(root_) / toPath(id);
    j->probeFile = base.u8string() + ".probe.json";
    j->logFile = base.u8string() + ".log";
    j->progFile = base.u8string() + ".progress";
    j->tmpOut = base.u8string() + ".part.mov";
    std::error_code ec;
    if (root_.empty() || !fs::exists(toPath(j->src), ec)) {
      job_ = std::move(j);
      finish(lib, false, root_.empty() ? "No project is open." : "The original file is not there any more: " + c->source);
      continue;
    }
    // already HAP? then it is copied as it is
    HapIndex idx;
    std::string err;
    if (lowerExt(j->src) == ".mov" && movReadIndex(j->src, idx, err)) {
      j->step = Job::COPY;
      j->copyState = std::make_shared<std::atomic<int>>(0);
      auto st = j->copyState;
      std::string from = j->src, to = j->tmpOut;
      std::thread([st, from, to] {
        std::error_code e2;
        fs::copy_file(toPath(from), toPath(to), fs::copy_options::overwrite_existing, e2);
        *st = e2 ? 2 : 1;
      }).detach();
      job_ = std::move(j);
      return;
    }
    if (!ffmpegAvailable()) {
      job_ = std::move(j);
      finish(lib, false, "FFmpeg is missing (ffmpeg.exe and ffprobe.exe go next to TouchFX.exe), so this file "
                         "cannot be converted.");
      continue;
    }
    std::vector<std::string> args = {ffprobePath(), "-v", "error", "-select_streams", "v:0", "-show_entries",
                                     "stream=width,height,pix_fmt,codec_name:format=duration", "-of", "json", j->src};
    if (!j->proc.start(args, j->probeFile)) {
      job_ = std::move(j);
      finish(lib, false, "Could not start FFprobe.");
      continue;
    }
    job_ = std::move(j);
  }
}

void MediaImporter::finish(MediaLibrary& lib, bool ok, const std::string& err) {
  std::unique_ptr<Job> j = std::move(job_);
  std::error_code ec;
  fs::remove(toPath(j->probeFile), ec);
  fs::remove(toPath(j->progFile), ec);
  MediaClip* c = findClip(lib, j->id);
  std::string log = readFile(j->logFile);
  fs::remove(toPath(j->logFile), ec);
  if (!c) {
    fs::remove(toPath(j->tmpOut), ec);
    return;
  }
  if (ok) {
    HapIndex idx;
    std::string e2;
    if (!movReadIndex(j->tmpOut, idx, e2)) {
      ok = false;
      fs::remove(toPath(j->tmpOut), ec);
      c->status = CLIP_FAILED;
      c->error = "The converted clip could not be read (" + e2 + ").";
    } else {
      fs::remove(toPath(clipPath(j->id)), ec);
      fs::rename(toPath(j->tmpOut), toPath(clipPath(j->id)), ec);
      c->width = idx.width;
      c->height = idx.height;
      c->frames = idx.frames();
      c->fps = float(idx.fps);
      c->alpha = idx.alpha();
      c->still = c->still || idx.frames() == 1;
      c->status = CLIP_READY;
      c->error.clear();
      logLine("Media: \"%s\" ready (%dx%d, %d frames, %.2f fps%s)", c->name.c_str(), c->width, c->height, c->frames,
              c->fps, c->alpha ? ", alpha" : "");
    }
  } else {
    fs::remove(toPath(j->tmpOut), ec);
    c->status = CLIP_FAILED;
    std::string l = lastLine(log);
    c->error = err + (l.empty() ? "" : " (" + l + ")");
    logLine("Media: \"%s\" failed: %s", c->name.c_str(), c->error.c_str());
  }
  ++thumbVersion_;
}

void MediaImporter::update(MediaLibrary& lib) {
  if (!job_) startNext(lib);
  if (!job_) return;
  Job& j = *job_;
  if (!findClip(lib, j.id)) {  // deleted meanwhile
    j.proc.kill();
    std::error_code ec;
    fs::remove(toPath(j.tmpOut), ec);
    job_.reset();
    return;
  }
  if (j.step == Job::COPY) {
    int st = *j.copyState;
    if (st == 0) return;
    if (st == 2) {
      finish(lib, false, "Could not copy the file.");
      return;
    }
    // thumbnail from the HAP file itself (FFmpeg reads HAP), if FFmpeg is there
    if (ffmpegAvailable()) {
      j.step = Job::THUMB;
      std::vector<std::string> args = {ffmpegPath(), "-nostdin", "-y", "-v", "error", "-i", j.tmpOut, "-frames:v", "1",
                                       "-vf",
                                       "scale=192:108:force_original_aspect_ratio=decrease,pad=192:108:(ow-iw)/2:(oh-ih)/2,format=rgb24",
                                       "-f", "image2", "-c:v", "ppm", thumbPath(j.id)};
      if (j.proc.start(args, "")) return;
    }
    finish(lib, true, "");
    return;
  }
  int code = j.proc.poll();
  if (j.step == Job::CONVERT) {
    // progress from FFmpeg's -progress file (a few times a second)
    using clk = std::chrono::steady_clock;
    double now = std::chrono::duration<double>(clk::now().time_since_epoch()).count();
    if (now - j.lastRead > 0.3 && j.duration > 0) {
      j.lastRead = now;
      std::string p = readFile(j.progFile);
      size_t k = p.rfind("out_time_us=");
      if (k != std::string::npos) {
        double us = std::atof(p.c_str() + k + 12);
        j.progress = float(std::clamp(us / 1e6 / j.duration, 0.0, 0.99));
      }
    }
  }
  if (code == -1) return;
  switch (j.step) {
    case Job::PROBE: {
      json pj;
      try {
        pj = json::parse(readFile(j.probeFile));
      } catch (...) {
      }
      bool hasVideo = pj.contains("streams") && pj["streams"].is_array() && !pj["streams"].empty();
      if (code != 0 || !hasVideo) {
        finish(lib, false, "This file has no picture TouchFX can read.");
        return;
      }
      const json& s = pj["streams"][0];
      std::string pix = s.value("pix_fmt", "");
      // formats with an alpha channel (yuva420p, rgba, bgra, argb, ya8, gbrap, pal8 pictures...)
      j.alpha = false;
      for (const char* a : {"yuva", "rgba", "bgra", "argb", "abgr", "ya8", "ya16", "gbrap"})
        if (pix.rfind(a, 0) == 0) j.alpha = true;
      if (pix == "pal8" && j.still) j.alpha = true;  // PNG with a palette can be transparent
      if (pj.contains("format") && pj["format"].contains("duration")) {
        try {
          j.duration = std::stod(pj["format"]["duration"].get<std::string>());
        } catch (...) {
        }
      }
      // HAP needs sizes in multiples of 4; anything over 4K comes down to 4K
      std::string vf = "scale='min(3840,iw)':'min(2160,ih)':force_original_aspect_ratio=decrease,"
                       "scale=trunc(iw/4)*4:trunc(ih/4)*4";
      std::vector<std::string> args = {ffmpegPath(), "-nostdin", "-y", "-v", "error", "-i", j.src, "-map", "0:v:0",
                                       "-an", "-sn", "-dn", "-vf", vf};
      if (j.still) {
        args.insert(args.end(), {"-frames:v", "1"});
      } else {
        args.insert(args.end(), {"-fps_mode", "cfr"});
      }
      args.insert(args.end(), {"-c:v", "hap", "-format", j.alpha ? "hap_alpha" : "hap_q", "-progress", j.progFile,
                               "-nostats", "-f", "mov", j.tmpOut});
      j.step = Job::CONVERT;
      if (!j.proc.start(args, j.logFile)) finish(lib, false, "Could not start FFmpeg.");
      break;
    }
    case Job::CONVERT: {
      if (code != 0) {
        finish(lib, false, "FFmpeg could not convert this file");
        return;
      }
      j.progress = 1.f;
      j.step = Job::THUMB;
      std::string ss = j.still || j.duration <= 0 ? "0" : std::to_string(std::min(1.0, j.duration * 0.3));
      // transparent parts come out black in the thumbnail
      std::string tvf = std::string(j.alpha ? "format=rgba,premultiply=inplace=1," : "") +
                        "scale=192:108:force_original_aspect_ratio=decrease,pad=192:108:(ow-iw)/2:(oh-ih)/2,format=rgb24";
      std::vector<std::string> args = {ffmpegPath(), "-nostdin", "-y", "-v", "error", "-ss", ss, "-i", j.src,
                                       "-frames:v", "1", "-vf", tvf,
                                       "-f", "image2", "-c:v", "ppm", thumbPath(j.id)};
      if (!j.proc.start(args, "")) finish(lib, true, "");
      break;
    }
    case Job::THUMB:
      finish(lib, true, "");  // a missing thumbnail is not a failure
      break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// File browser thumbnails

FileThumbs::~FileThumbs() { proc_.kill(); }

void FileThumbs::setDir(const std::string& cacheDir) {
  if (cacheDir == dir_) return;
  dir_ = cacheDir;
  std::error_code ec;
  fs::create_directories(toPath(dir_), ec);
}

const FileThumbs::Info& FileThumbs::get(const std::string& path, uint64_t size, long long mtime) {
  // the cache key: the file and its size / date (a changed file gets a new thumbnail)
  uint64_t h = 1469598103934665603ull;
  for (unsigned char ch : path) h = (h ^ ch) * 1099511628211ull;
  h = (h ^ size) * 1099511628211ull;
  h = (h ^ uint64_t(mtime)) * 1099511628211ull;
  char key[24];
  std::snprintf(key, sizeof(key), "%016llx", (unsigned long long)h);
  auto it = items_.find(key);
  if (it == items_.end()) {
    Item item;
    item.path = path;
    item.key = key;
    fs::path base = toPath(dir_) / key;
    std::error_code ec;
    // made before: read the info file next to the strip
    std::string info = readFile(base.u8string() + ".json");
    if (!info.empty() && fs::exists(toPath(base.u8string() + ".ppm"), ec)) {
      try {
        json j = json::parse(info);
        item.info.ready = !j.value("failed", false);
        item.info.failed = j.value("failed", false);
        item.info.frames = j.value("frames", 1);
        item.info.duration = j.value("duration", -1.f);
        item.info.width = j.value("width", 0);
        item.info.height = j.value("height", 0);
        item.info.codec = j.value("codec", "");
        item.info.strip = base.u8string() + ".ppm";
        item.info.version = 1;
      } catch (...) {
      }
    } else if (!info.empty() && info.find("\"failed\":true") != std::string::npos) {
      item.info.failed = true;
    }
    it = items_.emplace(key, item).first;
    if (!it->second.info.ready && !it->second.info.failed && !dir_.empty()) queue_.push_back(key);
  } else if (!it->second.info.ready && !it->second.info.failed && cur_ != it->first) {
    // asked again (it is on screen): move it to the front
    auto q = std::find(queue_.begin(), queue_.end(), it->first);
    if (q != queue_.end() && q != queue_.end() - 1) {
      queue_.erase(q);
      queue_.push_back(it->first);
    }
  }
  if (queue_.size() > 400) queue_.pop_front();
  return it->second.info;
}

void FileThumbs::update() {
  if (!busy_) {
    if (queue_.empty() || !ffmpegAvailable()) return;
    cur_ = queue_.back();  // the newest request first: what is on screen now
    queue_.pop_back();
    Item& it = items_[cur_];
    fs::path base = toPath(dir_) / cur_;
    if (isStillImage(it.path)) {
      step_ = 1;
      it.info.frames = 1;
      std::string vf = "format=rgba,premultiply=inplace=1,scale=" + std::to_string(kW) + ":" + std::to_string(kH) +
                       ":force_original_aspect_ratio=decrease,pad=" + std::to_string(kW) + ":" + std::to_string(kH) +
                       ":(ow-iw)/2:(oh-ih)/2,format=rgb24";
      busy_ = proc_.start({ffmpegPath(), "-nostdin", "-y", "-v", "error", "-i", it.path, "-frames:v", "1", "-vf", vf,
                           "-f", "image2", "-c:v", "ppm", base.u8string() + ".ppm"},
                          "");
      // the picture size comes from a quick probe afterwards (step 2)
    } else {
      step_ = 0;
      busy_ = proc_.start({ffprobePath(), "-v", "error", "-select_streams", "v:0", "-show_entries",
                           "stream=width,height,codec_name:format=duration", "-of", "json", it.path},
                          base.u8string() + ".probe");
    }
    if (!busy_) it.info.failed = true;
    return;
  }
  int code = proc_.poll();
  if (code == -1) return;
  Item& it = items_[cur_];
  fs::path base = toPath(dir_) / cur_;
  std::error_code ec;
  auto finish = [&](bool ok) {
    json j;
    j["failed"] = !ok;
    j["frames"] = it.info.frames;
    j["duration"] = it.info.duration;
    j["width"] = it.info.width;
    j["height"] = it.info.height;
    j["codec"] = it.info.codec;
    std::ofstream(toPath(base.u8string() + ".json")) << j.dump();
    it.info.ready = ok;
    it.info.failed = !ok;
    it.info.strip = base.u8string() + ".ppm";
    ++it.info.version;
    fs::remove(toPath(base.u8string() + ".probe"), ec);
    busy_ = false;
    cur_.clear();
  };
  if (step_ == 0) {  // probed: now the strip of frames across the clip
    json pj;
    try {
      pj = json::parse(readFile(base.u8string() + ".probe"));
    } catch (...) {
    }
    if (code != 0 || !pj.contains("streams") || pj["streams"].empty()) {
      finish(false);
      return;
    }
    const json& st = pj["streams"][0];
    it.info.width = st.value("width", 0);
    it.info.height = st.value("height", 0);
    it.info.codec = st.value("codec_name", "");
    try {
      if (pj.contains("format") && pj["format"].contains("duration"))
        it.info.duration = std::stof(pj["format"]["duration"].get<std::string>());
    } catch (...) {
    }
    int n = it.info.duration > 1.f ? kFrames : 1;
    it.info.frames = n;
    std::vector<std::string> args = {ffmpegPath(), "-nostdin", "-y", "-v", "error"};
    for (int k = 0; k < n; ++k) {  // one quick seek per frame (no decoding the whole file)
      float t = n == 1 ? 0.f : it.info.duration * (0.04f + 0.92f * float(k) / float(n - 1));
      args.insert(args.end(), {"-ss", std::to_string(std::max(0.f, t - 0.05f)), "-i", it.path});
    }
    std::string fc;
    for (int k = 0; k < n; ++k)
      fc += "[" + std::to_string(k) + ":v]scale=" + std::to_string(kW) + ":" + std::to_string(kH) +
            ":force_original_aspect_ratio=decrease,pad=" + std::to_string(kW) + ":" + std::to_string(kH) +
            ":(ow-iw)/2:(oh-ih)/2,setsar=1[v" + std::to_string(k) + "];";
    if (n > 1) {
      for (int k = 0; k < n; ++k) fc += "[v" + std::to_string(k) + "]";
      fc += "hstack=inputs=" + std::to_string(n) + ",format=rgb24[out]";
    } else {
      fc += "[v0]format=rgb24[out]";
    }
    args.insert(args.end(), {"-filter_complex", fc, "-map", "[out]", "-frames:v", "1", "-f", "image2", "-c:v", "ppm",
                             base.u8string() + ".ppm"});
    step_ = 1;
    busy_ = proc_.start(args, "");
    if (!busy_) finish(false);
    return;
  }
  if (step_ == 1) {
    bool ok = code == 0 && fs::exists(toPath(base.u8string() + ".ppm"), ec);
    if (ok && isStillImage(it.path) && it.info.width == 0) {  // picture: its size, quickly
      step_ = 2;
      busy_ = proc_.start({ffprobePath(), "-v", "error", "-select_streams", "v:0", "-show_entries",
                           "stream=width,height,codec_name", "-of", "json", it.path},
                          base.u8string() + ".probe");
      if (busy_) return;
    }
    finish(ok);
    return;
  }
  // step 2: picture size
  try {
    json pj = json::parse(readFile(base.u8string() + ".probe"));
    if (pj.contains("streams") && !pj["streams"].empty()) {
      it.info.width = pj["streams"][0].value("width", 0);
      it.info.height = pj["streams"][0].value("height", 0);
      it.info.codec = pj["streams"][0].value("codec_name", "");
    }
  } catch (...) {
  }
  finish(true);
}
