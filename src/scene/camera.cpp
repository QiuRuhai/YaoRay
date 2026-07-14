#include <yaoray/scene/camera.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

Vec3f PerspectiveDirection(const RenderCamera& camera, float width, float height,
    float film_x, float film_y) {
    const float aspect = width / height;
    const float half_height = std::tan(camera.fov_y_radians * 0.5f);
    const float screen_x = (2.0f * film_x / width - 1.0f) * aspect * half_height;
    const float screen_y = (1.0f - 2.0f * film_y / height) * half_height;
    return Normalize(camera.forward + camera.right * screen_x + camera.up * screen_y);
}

} // namespace

Ray3f GeneratePerspectiveCameraRay(
    const RenderCamera& camera,
    int film_width,
    int film_height,
    int pixel_x,
    int pixel_y,
    Vec2f pixel_sample
) {
    const float width = static_cast<float>(std::max(1, film_width));
    const float height = static_cast<float>(std::max(1, film_height));
    const float film_x = static_cast<float>(pixel_x) + pixel_sample.x;
    const float film_y = static_cast<float>(pixel_y) + pixel_sample.y;
    Ray3f ray{camera.origin, PerspectiveDirection(camera, width, height, film_x, film_y)};
    ray.has_differentials = true;
    ray.rx_origin = camera.origin;
    ray.ry_origin = camera.origin;
    ray.rx_direction = PerspectiveDirection(camera, width, height, film_x + 1.0f, film_y);
    ray.ry_direction = PerspectiveDirection(camera, width, height, film_x, film_y + 1.0f);
    return ray;
}

} // namespace yr
