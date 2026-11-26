#include "hap.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

namespace {

FILE* openUtf8(const std::string& path) {
#ifdef _WIN32
  return _wfopen(std::filesystem::u8path(path).wstring().c_str(), L"rb");
#else
  return std::fopen(path.c_str(), "rb");
#endif
}

bool seek64(FILE* f, uint64_t pos) {
#ifdef _WIN32
  return _fseeki64(f, int64_t(pos), SEEK_SET) == 0;
#else
  return fseeko(f, off_t(pos), SEEK_SET) == 0;
#endif
}

uint64_t fileSize(FILE* f) {
#ifdef _WIN32
  _fseeki64(f, 0, SEEK_END);
  return uint64_t(_ftelli64(f));
#else
  fseeko(f, 0, SEEK_END);
  return uint64_t(ftello(f));
#endif
}

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
constexpr uint32_t fcc(const char* s) {
  return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}

bool isHap(uint32_t f) {
  return f == fcc("Hap1") || f == fcc("Hap5") || f == fcc("HapY") || f == fcc("HapM") || f == fcc("HapA") ||
         f == fcc("Hap7") || f == fcc("HapH");
}

// ---- QuickTime atoms ----
struct Track {
  uint32_t handler = 0, codec = 0;
  int w = 0, h = 0;
  uint32_t timescale = 0;
  std::vector<std::pair<uint32_t, uint32_t>> stts;     // count, delta
  std::vector<uint32_t> sizes;                         // per sample
  uint32_t uniformSize = 0, sampleCount = 0;
  std::vector<std::array<uint32_t, 3>> stsc;          // first chunk, samples per chunk, desc
  std::vector<uint64_t> chunks;
};

struct Parser {
  const std::vector<uint8_t>& d;
  std::vector<Track> tracks;
  bool bad = false;

  // walks the atoms in [a, b); containers are entered
  void walk(size_t a, size_t b, Track* t) {
    while (a + 8 <= b && !bad) {
      uint64_t sz = be32(&d[a]);
      uint32_t type = be32(&d[a + 4]);
      size_t hdr = 8;
      if (sz == 1) {
        if (a + 16 > b) return;
        sz = be64(&d[a + 8]);
        hdr = 16;
      } else if (sz == 0) {
        sz = b - a;
      }
      if (sz < hdr || a + sz > b) {  // damaged / truncated: stop here
        bad = sz < hdr;
        return;
      }
      size_t p = a + hdr, e = a + size_t(sz);
      if (type == fcc("trak")) {
        tracks.emplace_back();
        walk(p, e, &tracks.back());
      } else if (type == fcc("moov") || type == fcc("mdia") || type == fcc("minf") || type == fcc("stbl")) {
        walk(p, e, t);
      } else if (t) {
        leaf(type, p, e, *t);
      }
      a = e;
    }
  }

  void leaf(uint32_t type, size_t p, size_t e, Track& t) {
    auto need = [&](size_t n) { return p + n <= e; };
    if (type == fcc("hdlr") && need(12)) {
      if (t.handler == 0) t.handler = be32(&d[p + 8]);  // the media handler (minf has a second, data hdlr)
    } else if (type == fcc("mdhd") && need(24)) {
      int ver = d[p];
      if (ver == 1 && need(32)) t.timescale = be32(&d[p + 20]);
      else t.timescale = be32(&d[p + 12]);
    } else if (type == fcc("stsd") && need(8 + 36)) {
      size_t q = p + 8;  // first sample description
      t.codec = be32(&d[q + 4]);
      t.w = be16(&d[q + 32]);
      t.h = be16(&d[q + 34]);
    } else if (type == fcc("stts") && need(8)) {
      uint32_t n = be32(&d[p + 4]);
      for (uint32_t i = 0; i < n && need(8 + i * 8 + 8); ++i)
        t.stts.push_back({be32(&d[p + 8 + i * 8]), be32(&d[p + 12 + i * 8])});
    } else if (type == fcc("stsz") && need(12)) {
      t.uniformSize = be32(&d[p + 4]);
      t.sampleCount = be32(&d[p + 8]);
      if (t.uniformSize == 0)
        for (uint32_t i = 0; i < t.sampleCount && need(12 + i * 4 + 4); ++i) t.sizes.push_back(be32(&d[p + 12 + i * 4]));
    } else if (type == fcc("stsc") && need(8)) {
      uint32_t n = be32(&d[p + 4]);
      for (uint32_t i = 0; i < n && need(8 + i * 12 + 12); ++i)
        t.stsc.push_back({be32(&d[p + 8 + i * 12]), be32(&d[p + 12 + i * 12]), be32(&d[p + 16 + i * 12])});
    } else if (type == fcc("stco") && need(8)) {
      uint32_t n = be32(&d[p + 4]);
      for (uint32_t i = 0; i < n && need(8 + i * 4 + 4); ++i) t.chunks.push_back(be32(&d[p + 8 + i * 4]));
    } else if (type == fcc("co64") && need(8)) {
      uint32_t n = be32(&d[p + 4]);
      for (uint32_t i = 0; i < n && need(8 + i * 8 + 8); ++i) t.chunks.push_back(be64(&d[p + 8 + i * 8]));
    }
  }
};

}  // namespace

bool HapIndex::alpha() const { return fourcc == fcc("Hap5") || fourcc == fcc("HapM"); }

bool movReadIndex(const std::string& path, HapIndex& out, std::string& err) {
  out = HapIndex();
  FILE* f = openUtf8(path);
  if (!f) {
    err = "cannot open the file";
    return false;
  }
  uint64_t total = fileSize(f);
  // find the moov atom among the top-level atoms (it can be at the start or the end)
  uint64_t pos = 0;
  std::vector<uint8_t> moov;
  while (pos + 8 <= total) {
    uint8_t h[16];
    if (!seek64(f, pos) || std::fread(h, 1, 8, f) != 8) break;
    uint64_t sz = be32(h);
    uint32_t type = be32(h + 4);
    if (sz == 1) {
      if (std::fread(h + 8, 1, 8, f) != 8) break;
      sz = be64(h + 8);
    } else if (sz == 0) {
      sz = total - pos;
    }
    if (sz < 8) break;
    if (type == fcc("moov")) {
      if (sz > 256ull * 1024 * 1024) break;  // not a sane index
      moov.resize(size_t(sz));
      if (!seek64(f, pos) || std::fread(moov.data(), 1, moov.size(), f) != moov.size()) moov.clear();
      break;
    }
    pos += sz;
  }
  std::fclose(f);
  if (moov.empty()) {
    err = "not a QuickTime / MP4 file (no index)";
    return false;
  }
  Parser ps{moov, {}, false};
  ps.walk(0, moov.size(), nullptr);
  for (const Track& t : ps.tracks) {
    if (t.handler != fcc("vide") || !isHap(t.codec)) continue;
    uint32_t n = t.uniformSize ? t.sampleCount : uint32_t(t.sizes.size());
    if (n == 0 || t.chunks.empty() || t.stsc.empty()) continue;
    out.width = t.w;
    out.height = t.h;
    out.fourcc = t.codec;
    // frame offsets: walk the chunks with the sample-to-chunk table
    out.offset.reserve(n);
    out.size.reserve(n);
    uint32_t sample = 0;
    for (size_t e = 0; e < t.stsc.size() && sample < n; ++e) {
      uint32_t first = t.stsc[e][0], per = t.stsc[e][1];
      uint32_t last = e + 1 < t.stsc.size() ? t.stsc[e + 1][0] : uint32_t(t.chunks.size()) + 1;
      for (uint32_t ch = first; ch < last && sample < n; ++ch) {
        if (ch == 0 || ch > t.chunks.size()) break;
        uint64_t off = t.chunks[ch - 1];
        for (uint32_t k = 0; k < per && sample < n; ++k, ++sample) {
          uint32_t s = t.uniformSize ? t.uniformSize : t.sizes[sample];
          out.offset.push_back(off);
          out.size.push_back(s);
          off += s;
        }
      }
    }
    for (size_t i = 0; i < out.offset.size(); ++i)
      if (out.offset[i] + out.size[i] > total) {  // truncated file: keep the frames that are there
        out.offset.resize(i);
        out.size.resize(i);
        break;
      }
    // frame rate from the most common frame duration
    uint32_t delta = 0, best = 0;
    for (auto& s : t.stts)
      if (s.first > best) best = s.first, delta = s.second;
    out.fps = (delta && t.timescale) ? double(t.timescale) / delta : 30.0;
    if (out.fps < 1 || out.fps > 240) out.fps = 30;
    if (out.offset.empty()) {
      err = "the file has no complete frames";
      return false;
    }
    if (out.width <= 0 || out.height <= 0 || out.width > 16384 || out.height > 16384) {
      err = "bad picture size";
      return false;
    }
    return true;
  }
  err = "no HAP video in the file (it needs converting)";
  return false;
}

// ---------------------------------------------------------------------------
// Snappy (format description: github.com/google/snappy/blob/main/format_description.txt)
bool snappyUncompress(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
  size_t i = 0;
  uint64_t n = 0;
  for (int shift = 0; shift < 35; shift += 7) {
    if (i >= len) return false;
    uint8_t b = src[i++];
    n |= uint64_t(b & 0x7f) << shift;
    if (!(b & 0x80)) break;
  }
  if (n > (1ull << 30)) return false;
  out.resize(size_t(n));
  uint8_t* dst = out.data();
  size_t o = 0;
  while (i < len) {
    uint8_t tag = src[i++];
    size_t length = 0, offset = 0;
    switch (tag & 3) {
      case 0: {  // literal
        length = (tag >> 2) + 1;
        if (length > 60) {
          size_t extra = length - 60;  // 1..4 bytes of length
          if (i + extra > len) return false;
          length = 0;
          for (size_t k = 0; k < extra; ++k) length |= size_t(src[i + k]) << (8 * k);
          length += 1;
          i += extra;
        }
        if (i + length > len || o + length > n) return false;
        std::memcpy(dst + o, src + i, length);
        i += length;
        o += length;
        continue;
      }
      case 1:
        if (i >= len) return false;
        length = 4 + ((tag >> 2) & 7);
        offset = (size_t(tag >> 5) << 8) | src[i++];
        break;
      case 2:
        if (i + 2 > len) return false;
        length = (tag >> 2) + 1;
        offset = size_t(src[i]) | size_t(src[i + 1]) << 8;
        i += 2;
        break;
      default:
        if (i + 4 > len) return false;
        length = (tag >> 2) + 1;
        offset = le32(src + i);
        i += 4;
        break;
    }
    if (offset == 0 || offset > o || o + length > n) return false;
    // byte by byte: copies may overlap themselves (run-length style)
    const uint8_t* from = dst + o - offset;
    if (offset >= length) std::memcpy(dst + o, from, length);
    else
      for (size_t k = 0; k < length; ++k) dst[o + k] = from[k];
    o += length;
  }
  return o == n;
}

// ---------------------------------------------------------------------------
// HAP frames (spec: github.com/Vidvox/hap/blob/master/documentation/HapVideoDRAFT.md)
namespace {

struct Section {
  const uint8_t* data;
  size_t size;
  int type;
};

bool readSection(const uint8_t* p, size_t avail, Section& s, size_t& used) {
  if (avail < 4) return false;
  size_t size = size_t(p[0]) | size_t(p[1]) << 8 | size_t(p[2]) << 16;
  s.type = p[3];
  size_t hdr = 4;
  if (size == 0) {
    if (avail < 8) return false;
    size = le32(p + 4);
    hdr = 8;
  }
  if (hdr + size > avail) return false;
  s.data = p + hdr;
  s.size = size;
  used = hdr + size;
  return true;
}

bool texFormat(int lowNibble, int& fmt) {
  switch (lowNibble) {
    case 0xB: fmt = HAPTEX_DXT1; return true;
    case 0xE: fmt = HAPTEX_DXT5; return true;
    case 0xF: fmt = HAPTEX_YCOCG; return true;
    case 0x1: fmt = HAPTEX_ALPHA_RGTC1; return true;
    default: return false;
  }
}

// One texture section: second-stage decompression (none / Snappy / chunked)
bool decodeImage(const Section& s, int& fmt, std::vector<uint8_t>& out, std::string& err) {
  if (!texFormat(s.type & 0x0F, fmt)) {
    err = "unsupported HAP texture format";
    return false;
  }
  int comp = s.type >> 4;
  if (comp == 0xA) {
    out.assign(s.data, s.data + s.size);
    return true;
  }
  if (comp == 0xB) {
    if (!snappyUncompress(s.data, s.size, out)) {
      err = "damaged frame (snappy)";
      return false;
    }
    return true;
  }
  if (comp != 0xC) {
    err = "unknown HAP compressor";
    return false;
  }
  // chunked: a decode-instructions container, then the chunks
  Section di;
  size_t used = 0;
  if (!readSection(s.data, s.size, di, used) || di.type != 0x01) {
    err = "damaged frame (chunks)";
    return false;
  }
  std::vector<uint8_t> comps;
  std::vector<uint32_t> sizes, offsets;
  size_t q = 0, u2 = 0;
  while (q < di.size) {
    Section sub;
    if (!readSection(di.data + q, di.size - q, sub, u2)) break;
    if (sub.type == 0x02) comps.assign(sub.data, sub.data + sub.size);
    else if (sub.type == 0x03)
      for (size_t k = 0; k + 4 <= sub.size; k += 4) sizes.push_back(le32(sub.data + k));
    else if (sub.type == 0x04)
      for (size_t k = 0; k + 4 <= sub.size; k += 4) offsets.push_back(le32(sub.data + k));
    q += u2;
  }
  if (comps.empty() || comps.size() != sizes.size()) {
    err = "damaged frame (chunk table)";
    return false;
  }
  const uint8_t* base = s.data + used;
  size_t avail = s.size - used;
  out.clear();
  std::vector<uint8_t> tmp;
  size_t running = 0;
  for (size_t k = 0; k < comps.size(); ++k) {
    size_t off = offsets.size() == comps.size() ? offsets[k] : running;
    running += sizes[k];
    if (off + sizes[k] > avail) {
      err = "damaged frame (chunk size)";
      return false;
    }
    if (comps[k] == 0x0A) {
      out.insert(out.end(), base + off, base + off + sizes[k]);
    } else if (comps[k] == 0x0B) {
      if (!snappyUncompress(base + off, sizes[k], tmp)) {
        err = "damaged frame (snappy chunk)";
        return false;
      }
      out.insert(out.end(), tmp.begin(), tmp.end());
    } else {
      err = "unknown chunk compressor";
      return false;
    }
  }
  return true;
}

}  // namespace

bool hapDecode(const uint8_t* src, size_t len, HapFrame& out, std::string& err) {
  out.count = 0;
  Section top;
  size_t used = 0;
  if (!readSection(src, len, top, used)) {
    err = "damaged frame (header)";
    return false;
  }
  if (top.type == 0x0D) {  // several textures (HAP Q Alpha): colour, then alpha
    size_t q = 0;
    while (q < top.size && out.count < 2) {
      Section s;
      size_t u = 0;
      if (!readSection(top.data + q, top.size - q, s, u)) break;
      if (!decodeImage(s, out.format[out.count], out.data[out.count], err)) return false;
      ++out.count;
      q += u;
    }
    return out.count > 0;
  }
  if (!decodeImage(top, out.format[0], out.data[0], err)) return false;
  out.count = 1;
  return true;
}

// ---------------------------------------------------------------------------
bool HapReader::open(const std::string& path, std::string& err) {
  close();
  if (!movReadIndex(path, idx_, err)) return false;
  f_ = openUtf8(path);
  if (!f_) {
    err = "cannot open the file";
    return false;
  }
  return true;
}

void HapReader::close() {
  if (f_) std::fclose(f_);
  f_ = nullptr;
}

bool HapReader::readFrame(int i, HapFrame& out, std::string& err) {
  if (!f_ || i < 0 || i >= idx_.frames()) {
    err = "no such frame";
    return false;
  }
  buf_.resize(idx_.size[size_t(i)]);
  if (!seek64(f_, idx_.offset[size_t(i)]) || std::fread(buf_.data(), 1, buf_.size(), f_) != buf_.size()) {
    err = "read error (was the drive removed?)";
    return false;
  }
  return hapDecode(buf_.data(), buf_.size(), out, err);
}
