#include "yr_test.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <yaoray/backends/backend.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderScene MakeBackendTriangleScene(int width = 4, int height = 3) {
    yr::RenderScene scene;
    scene.backend = yr::RenderBackendKind::Cpu;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.05f, 0.10f, 0.15f};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::Color3f{1.0f, 0.2f, 0.1f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

} // namespace

YR_TEST(create_render_backend_returns_cpu_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cpu);
}

YR_TEST(cpu_backend_renders_film_and_stats) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderScene scene = MakeBackendTriangleScene(4, 3);

    const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->Width(), 4);
    YR_EXPECT_EQ(result.film->Height(), 3);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.triangle_tests, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
}

YR_TEST(create_render_backend_returns_cuda_stub_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cuda);
}

YR_TEST(cuda_backend_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    yr::RenderScene scene = MakeBackendTriangleScene(1, 1);
    scene.backend = yr::RenderBackendKind::Cuda;

    const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CUDA backend not implemented yet") != std::string::npos);
}
