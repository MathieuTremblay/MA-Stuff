#pragma once
#include <cmath>
#include <cstdint>

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  Vec2(float a, float b) : x(a), y(b) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
  Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

// 2x2 matrix (row-major): [a b; c d]
struct Mat2 {
  float a = 1, b = 0, c = 0, d = 1;
  Vec2 operator*(Vec2 v) const { return {a * v.x + b * v.y, c * v.x + d * v.y}; }
  Mat2 operator*(const Mat2& o) const {
    return {a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d};
  }
  static Mat2 rot(float t) { float c = std::cos(t), s = std::sin(t); return {c, -s, s, c}; }
  static Mat2 scale(float sx, float sy) { return {sx, 0, 0, sy}; }
};

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.f, 1.f); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 mix3(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float fractf(float v) { return v - std::floor(v); }

inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
inline float hash01(uint32_t x) { return (hash32(x) & 0xFFFFFF) / float(0x1000000); }

struct Rng {
  uint32_t s = 0x12345678u;
  float next() { s = s * 1664525u + 1013904223u; return (hash32(s) & 0xFFFFFF) / float(0x1000000); }
  float range(float a, float b) { return a + (b - a) * next(); }
};
