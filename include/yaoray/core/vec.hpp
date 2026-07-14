#pragma once

#include <cmath>

namespace yr {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2f() = default;
    constexpr Vec2f(float x_value, float y_value)
        : x(x_value), y(y_value) {}
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3f() = default;
    constexpr Vec3f(float x_value, float y_value, float z_value)
        : x(x_value), y(y_value), z(z_value) {}

    constexpr Vec3f operator-() const {
        return Vec3f{-x, -y, -z};
    }
};

using Point3f = Vec3f;
using Color3f = Vec3f;

struct Color4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Color4f() = default;
    constexpr Color4f(float x_value, float y_value, float z_value, float w_value = 1.0f)
        : x(x_value), y(y_value), z(z_value), w(w_value) {}
    constexpr Color4f(Color3f rgb, float alpha = 1.0f)
        : x(rgb.x), y(rgb.y), z(rgb.z), w(alpha) {}

    constexpr Color3f rgb() const {
        return Color3f{x, y, z};
    }
};

constexpr Color4f operator+(Color4f a, Color4f b) {
    return Color4f{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

constexpr Color4f operator*(Color4f value, float scale) {
    return Color4f{value.x * scale, value.y * scale, value.z * scale, value.w * scale};
}

constexpr Color4f operator/(Color4f value, float scale) {
    return Color4f{value.x / scale, value.y / scale, value.z / scale, value.w / scale};
}

constexpr Vec3f operator+(Vec3f a, Vec3f b) {
    return Vec3f{a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr Vec3f operator-(Vec3f a, Vec3f b) {
    return Vec3f{a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr Vec3f operator*(Vec3f v, float scale) {
    return Vec3f{v.x * scale, v.y * scale, v.z * scale};
}

constexpr Vec3f operator*(float scale, Vec3f v) {
    return v * scale;
}

constexpr Vec3f operator/(Vec3f v, float scale) {
    return Vec3f{v.x / scale, v.y / scale, v.z / scale};
}

constexpr float Dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3f Cross(Vec3f a, Vec3f b) {
    return Vec3f{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline float LengthSquared(Vec3f v) {
    return Dot(v, v);
}

inline float Length(Vec3f v) {
    return std::sqrt(LengthSquared(v));
}

inline Vec3f Normalize(Vec3f v) {
    const float len = Length(v);
    return len > 0.0f ? v / len : Vec3f{};
}

} // namespace yr
