#pragma once

#include <yaoray/core/ray.hpp>

namespace yr {

struct RenderCamera {
    Point3f origin;
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fov_y_radians = 0.785398185f;
};

Ray3f GeneratePerspectiveCameraRay(
    const RenderCamera& camera,
    int film_width,
    int film_height,
    int pixel_x,
    int pixel_y,
    Vec2f pixel_sample
);

} // namespace yr
