#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <yaoray/core/ray.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace yr {
namespace {

constexpr float MinHitT = 1.0e-5f;
constexpr float ParallelEpsilon = 1.0e-8f;

struct TriangleHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    const RenderTriangle* triangle = nullptr;
};

Ray3f MakeCameraRay(const RenderScene& scene, int x, int y) {
    const float width = static_cast<float>(scene.width);
    const float height = static_cast<float>(scene.height);
    const float aspect = width / height;
    const float half_height = std::tan(scene.camera.fov_y_radians * 0.5f);
    const float screen_x = (2.0f * (static_cast<float>(x) + 0.5f) / width - 1.0f) * aspect * half_height;
    const float screen_y = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / height) * half_height;
    const Vec3f direction = Normalize(
        scene.camera.forward +
        scene.camera.right * screen_x +
        scene.camera.up * screen_y
    );
    return Ray3f{scene.camera.origin, direction};
}

bool IntersectTriangle(const Ray3f& ray, const RenderTriangle& triangle, float& t_out) {
    const Vec3f edge1 = triangle.p1 - triangle.p0;
    const Vec3f edge2 = triangle.p2 - triangle.p0;
    const Vec3f pvec = Cross(ray.direction, edge2);
    const float det = Dot(edge1, pvec);
    if (std::fabs(det) < ParallelEpsilon) {
        return false;
    }

    const float inv_det = 1.0f / det;
    const Vec3f tvec = ray.origin - triangle.p0;
    const float u = Dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const Vec3f qvec = Cross(tvec, edge1);
    const float v = Dot(ray.direction, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float t = Dot(edge2, qvec) * inv_det;
    if (t <= MinHitT) {
        return false;
    }

    t_out = t;
    return true;
}

TriangleHit FindNearestHit(const RenderScene& scene, const Ray3f& ray, CpuDebugRenderStats& stats) {
    TriangleHit nearest;
    for (const RenderTriangle& triangle : scene.triangles) {
        ++stats.triangle_tests;
        float t = 0.0f;
        if (IntersectTriangle(ray, triangle, t) && t < nearest.t) {
            nearest.hit = true;
            nearest.t = t;
            nearest.triangle = &triangle;
        }
    }
    return nearest;
}

Color3f EnvironmentColor(const RenderScene& scene) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    return Color3f{};
}

Color3f ShadeHit(const RenderScene& scene, const Ray3f& ray, const RenderTriangle& triangle) {
    if (triangle.material_index < 0 ||
        static_cast<std::size_t>(triangle.material_index) >= scene.materials.size()) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
    const float normal_lighting = std::max(0.15f, std::fabs(Dot(triangle.normal, -ray.direction)));
    return material.albedo * normal_lighting + material.emission;
}

} // namespace

CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene) {
    CpuDebugRenderResult result{Film{scene.width, scene.height}, {}};
    const auto start = std::chrono::steady_clock::now();

    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            const Ray3f ray = MakeCameraRay(scene, x, y);
            ++result.stats.rays_traced;

            const TriangleHit hit = FindNearestHit(scene, ray, result.stats);
            if (hit.hit && hit.triangle != nullptr) {
                ++result.stats.hits;
                result.film.AddSample(x, y, ShadeHit(scene, ray, *hit.triangle));
            } else {
                ++result.stats.misses;
                result.film.AddSample(x, y, EnvironmentColor(scene));
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
