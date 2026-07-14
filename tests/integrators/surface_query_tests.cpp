#include "yr_test.hpp"

#include <stdexcept>
#include <utility>

#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/integrators/surface_query.hpp>
#include <yaoray/core/ray.hpp>

namespace {

yr::CpuPreparedScene PrepareSurfaceScene(yr::RenderSceneIR scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(
        yr::RenderJob{std::move(scene), yr::RenderSettings{}}
    );
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

YR_TEST(surface_query_hits_single_triangle) {
    const yr::CpuPreparedScene prepared = PrepareSurfaceScene(MakeSimpleScene());
    yr::BvhTraceStats stats;

    const yr::SurfaceHit hit = yr::TraceVisibleSurface(
        prepared.Scene(),
        prepared.acceleration,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        1.0e-5f,
        10.0f,
        &stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.geometry_hit.triangle_index, 0);
    YR_EXPECT_TRUE(yr::IsAlphaVisible(hit.sample));
}

YR_TEST(surface_query_transforms_instanced_shading_normal) {
    yr::RenderSceneIR scene = MakeSimpleScene();
    scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0},
        yr::RotationAxisMatrix(90.0f, yr::Vec3f{0.0f, 1.0f, 0.0f})
    });
    const yr::CpuPreparedScene prepared = PrepareSurfaceScene(std::move(scene));
    yr::BvhTraceStats stats;

    const yr::SurfaceHit hit = yr::TraceVisibleSurface(
        prepared.Scene(),
        prepared.acceleration,
        yr::Ray3f{yr::Point3f{1.0f, 0.0f, 0.0f}, yr::Vec3f{-1.0f, 0.0f, 0.0f}},
        1.0e-5f,
        10.0f,
        &stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.geometry_hit.instance.Value(), 0);
    YR_EXPECT_NEAR(hit.sample.shading_normal.x, 1.0f, 1.0e-5f);
    YR_EXPECT_NEAR(hit.sample.shading_normal.y, 0.0f, 1.0e-5f);
    YR_EXPECT_NEAR(hit.sample.shading_normal.z, 0.0f, 1.0e-5f);
}
