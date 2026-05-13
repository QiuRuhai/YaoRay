#pragma once

#include <algorithm>
#include <limits>

#include <yaoray/core/ray.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

struct Bounds3f {
    Point3f min;
    Point3f max;

    Bounds3f()
        : min{std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::infinity()},
          max{-std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity()} {}

    constexpr Bounds3f(Point3f p_min, Point3f p_max)
        : min(p_min), max(p_max) {}

    bool Intersects(const Ray3f& ray, float t_min, float t_max) const {
        const float origins[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
        const float dirs[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
        const float mins[3] = {min.x, min.y, min.z};
        const float maxs[3] = {max.x, max.y, max.z};

        for (int axis = 0; axis < 3; ++axis) {
            const float inv_d = 1.0f / dirs[axis];
            float t0 = (mins[axis] - origins[axis]) * inv_d;
            float t1 = (maxs[axis] - origins[axis]) * inv_d;
            if (inv_d < 0.0f) {
                std::swap(t0, t1);
            }
            t_min = std::max(t_min, t0);
            t_max = std::min(t_max, t1);
            if (t_max < t_min) {
                return false;
            }
        }
        return true;
    }
};

inline Bounds3f Union(const Bounds3f& bounds, Point3f p) {
    return Bounds3f{
        Point3f{
            std::min(bounds.min.x, p.x),
            std::min(bounds.min.y, p.y),
            std::min(bounds.min.z, p.z),
        },
        Point3f{
            std::max(bounds.max.x, p.x),
            std::max(bounds.max.y, p.y),
            std::max(bounds.max.z, p.z),
        },
    };
}

} // namespace yr
