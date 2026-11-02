#pragma once

#include <cmath>

namespace ofs {

// Table frame of table_geometry.hpp: +x towards the black, +y to the left seen from baulk, +z up
// through the cloth. Ball centres keep z = 0 (motion is planar, spec 7.1); spins use all three.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
};

constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }
constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr double norm2(const Vec3& v) { return dot(v, v); }
inline double norm(const Vec3& v) { return std::sqrt(norm2(v)); }
// The planar part (x, y) only.
inline double norm_xy(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
// Zero for a zero vector rather than NaN.
inline Vec3 unit_or_zero(const Vec3& v) {
    const double n = norm(v);
    return n > 0.0 ? v / n : Vec3{};
}

inline constexpr Vec3 kUp{0.0, 0.0, 1.0};

}  // namespace ofs
