#include "bsdf_internal.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace yr {

bool IsAboveSurface(Vec3f direction, Vec3f normal) {
    return Dot(direction, normal) > 0.0f;
}

Vec3f Reflect(Vec3f direction, Vec3f normal) {
    return Normalize(direction - normal * (2.0f * Dot(direction, normal)));
}

float FresnelDielectric(float cos_theta_i, float eta_i, float eta_t) {
    cos_theta_i = std::clamp(cos_theta_i, -1.0f, 1.0f);
    if (cos_theta_i <= 0.0f) {
        std::swap(eta_i, eta_t);
        cos_theta_i = std::fabs(cos_theta_i);
    }

    const float sin_theta_i = std::sqrt(std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    const float sin_theta_t = eta_i / eta_t * sin_theta_i;
    if (sin_theta_t >= 1.0f) return 1.0f;

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    const float r_parallel =
        ((eta_t * cos_theta_i) - (eta_i * cos_theta_t)) /
        ((eta_t * cos_theta_i) + (eta_i * cos_theta_t));
    const float r_perpendicular =
        ((eta_i * cos_theta_i) - (eta_t * cos_theta_t)) /
        ((eta_i * cos_theta_i) + (eta_t * cos_theta_t));
    return 0.5f * (r_parallel * r_parallel + r_perpendicular * r_perpendicular);
}

bool Refract(Vec3f wo, Vec3f normal, float eta, Vec3f& wi) {
    const float cos_theta_i = Dot(normal, wo);
    const float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    const float sin2_theta_t = eta * eta * sin2_theta_i;
    if (sin2_theta_t >= 1.0f) return false;

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin2_theta_t));
    wi = Normalize(-wo * eta + normal * (eta * cos_theta_i - cos_theta_t));
    return true;
}

bool IsBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

Color3f Lerp(Color3f a, Color3f b, float t) {
    return a * (1.0f - t) + b * t;
}

} // namespace yr
