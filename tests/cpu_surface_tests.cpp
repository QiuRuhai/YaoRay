#include "yr_test.hpp"

#include <stdexcept>
#include <utility>

#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/backends/cpu/cpu_surface.hpp>
#include <yaoray/core/ray.hpp>

namespace {

yr::RenderTriangle MakeUvTriangle(float z, int material_index) {
    yr::RenderTriangle triangle;
    triangle.p0 = yr::Point3f{-1.0f, -1.0f, z};
    triangle.p1 = yr::Point3f{1.0f, -1.0f, z};
    triangle.p2 = yr::Point3f{0.0f, 1.0f, z};
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.material_index = material_index;
    triangle.uv0 = yr::Vec2f{0.0f, 0.0f};
    triangle.uv1 = yr::Vec2f{1.0f, 0.0f};
    triangle.uv2 = yr::Vec2f{0.0f, 1.0f};
    triangle.has_uv = true;
    return triangle;
}

yr::RenderTexture AlphaTexture(float alpha) {
    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.texels = {yr::Color4f{1.0f, 1.0f, 1.0f, alpha}};
    return texture;
}

yr::CpuPreparedScene PrepareSurfaceScene(const yr::RenderSceneIR& scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(scene);
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

yr::RenderSceneIR MakeMaskedFrontScene(float front_alpha) {
    yr::RenderSceneIR scene;
    yr::RenderMaterial front;
    front.alpha_mode = yr::RenderAlphaMode::Mask;
    front.alpha_cutoff = 0.5f;
    front.albedo_texture = 0;
    yr::RenderMaterial back;
    back.albedo = yr::Color3f{0.2f, 0.7f, 0.3f};
    scene.materials = {front, back};
    scene.textures.push_back(AlphaTexture(front_alpha));
    scene.triangles.push_back(MakeUvTriangle(0.5f, 0));
    scene.triangles.push_back(MakeUvTriangle(0.0f, 1));
    return scene;
}

} // namespace

YR_TEST(cpu_surface_skips_masked_front_triangle_and_returns_back_triangle) {
    const yr::RenderSceneIR scene = MakeMaskedFrontScene(0.0f);
    const yr::CpuPreparedScene prepared = PrepareSurfaceScene(scene);
    yr::BvhTraceStats stats;

    const yr::CpuSurfaceHit hit = yr::TraceVisibleSurface(
        prepared,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        1.0e-5f,
        10.0f,
        &stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.geometry_hit.triangle_index, 1);
    YR_EXPECT_NEAR(hit.sample.material.albedo.y, 0.7, 1e-6);
}

YR_TEST(cpu_surface_returns_masked_triangle_when_alpha_passes_cutoff) {
    const yr::RenderSceneIR scene = MakeMaskedFrontScene(1.0f);
    const yr::CpuPreparedScene prepared = PrepareSurfaceScene(scene);
    yr::BvhTraceStats stats;

    const yr::CpuSurfaceHit hit = yr::TraceVisibleSurface(
        prepared,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        1.0e-5f,
        10.0f,
        &stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.geometry_hit.triangle_index, 0);
    YR_EXPECT_TRUE(yr::IsAlphaVisible(hit.sample));
}
