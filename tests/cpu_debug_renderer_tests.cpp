#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Expand debug renderer tests for table-geometry + emissive primitives.

namespace {

yr::CpuPreparedScene PrepareDebugScene(yr::RenderSceneIR scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(std::move(scene));
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

yr::RenderSceneIR MakeDebugTriangleScene(int width = 5, int height = 5) {
    yr::RenderSceneIR scene;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;
    scene.environment.active = false;
    scene.environment.radiance = yr::Color3f{0.05f, 0.10f, 0.15f};
    scene.environment.strength = 1.0f;
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Diffuse;
    mat.reflectance = yr::TexParam3f{{1.0f, 0.2f, 0.1f}};
    scene.materials.push_back(mat);
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-0.5f, -0.5f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 0.5f, -0.5f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 0.0f,  0.5f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    return scene;
}

} // namespace

YR_TEST(cpu_debug_renderer_traces_one_ray_per_pixel) {
    yr::RenderSceneIR scene = MakeDebugTriangleScene(4, 3);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(PrepareDebugScene(std::move(scene)));

    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
}

YR_TEST(cpu_debug_renderer_records_hits_and_misses) {
    yr::RenderSceneIR scene = MakeDebugTriangleScene(5, 5);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(PrepareDebugScene(std::move(scene)));

    YR_EXPECT_TRUE(result.stats.hits > 0);
    YR_EXPECT_TRUE(result.stats.misses > 0);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
}

YR_TEST(cpu_debug_renderer_shades_environment_misses) {
    yr::RenderSceneIR scene = MakeDebugTriangleScene(2, 2);
    scene.vertices.clear();
    scene.indices.clear();
    scene.primitives.clear();
    scene.materials.clear();
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(PrepareDebugScene(std::move(scene)));
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_EQ(result.stats.hits, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{4});
    YR_EXPECT_NEAR(pixel.x, 0.4, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 0.6, 1e-6);
    YR_EXPECT_NEAR(pixel.z, 0.8, 1e-6);
}
