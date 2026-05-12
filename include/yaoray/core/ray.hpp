#pragma once

#include <yaoray/core/vec.hpp>

namespace yr {

struct Ray3f {
    Point3f origin;
    Vec3f direction;
    float time = 0.0f;

    constexpr Ray3f() = default;
    constexpr Ray3f(Point3f ray_origin, Vec3f ray_direction, float ray_time = 0.0f)
        : origin(ray_origin), direction(ray_direction), time(ray_time) {}

    constexpr Point3f At(float t) const {
        return origin + direction * t;
    }
};

} // namespace yr
