#include "yr_test.hpp"

#include <stdexcept>
#include <utility>

#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/backends/cpu/cpu_surface.hpp>
#include <yaoray/core/ray.hpp>

// TODO(Task 11): Rewrite CPU surface tests for table-geometry + PBRT material model.

namespace {

yr::CpuPreparedScene PrepareSurfaceScene(yr::RenderSceneIR scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(std::move(scene));
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

yr::RenderSceneIR MakeSimpleScene() {
    yr::RenderSceneIR scene;
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Diffuse;
    mat.reflectance = yr::TexParam3f{{0.8f, 0.2f, 0.1f}};
    scene.materials.push_back(mat);
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-1.0f, -1.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, -1.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 0.0f,  1.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    return scene;
}

} // namespace

YR_TEST(cpu_surface_hits_single_triangle) {
    const yr::CpuPreparedScene prepared = PrepareSurfaceScene(MakeSimpleScene());
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
