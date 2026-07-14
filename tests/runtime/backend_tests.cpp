#include "yr_test.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <yaoray/runtime/backend.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/runtime/cuda_stub_prepared_scene.hpp>
#include <yaoray/runtime/render_job.hpp>

namespace {

yr::RenderJob MakeBackendTriangleJob(int width = 4, int height = 3) {
    yr::RenderJob job;
    job.settings.requested_backend = yr::RenderBackendKind::Cpu;
    job.settings.width = width;
    job.settings.height = height;
    job.settings.spp = 1;
    yr::RenderSceneIR& scene = job.scene;
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
    return job;
}

} // namespace

YR_TEST(create_render_backend_returns_cpu_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cpu);
}

YR_TEST(cpu_backend_capabilities_describe_current_support) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);

    const yr::RenderBackendCapabilities capabilities = backend->Capabilities();

    YR_EXPECT_EQ(capabilities.kind, yr::RenderBackendKind::Cpu);
    YR_EXPECT_TRUE(capabilities.runnable);
    YR_EXPECT_TRUE(capabilities.supports_debug_direct);
    YR_EXPECT_TRUE(capabilities.supports_path);
    YR_EXPECT_TRUE(capabilities.supports_offline_progress);
    YR_EXPECT_TRUE(capabilities.supports_texture_materials);
}

YR_TEST(cpu_backend_prepare_builds_cpu_prepared_scene) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderJob job = MakeBackendTriangleJob(4, 3);

    yr::BackendPrepareResult prepared = backend->Prepare(std::move(job));

    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.error.empty());
    YR_EXPECT_TRUE(prepared.scene != nullptr);
    YR_EXPECT_TRUE(prepared.elapsed_seconds >= 0.0);
    YR_EXPECT_EQ(prepared.scene->Kind(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(prepared.scene->Settings().width, 4);
    YR_EXPECT_EQ(prepared.scene->Settings().height, 3);
    YR_EXPECT_EQ(prepared.scene->SourceScene().primitives.size(), std::size_t{1});
}

YR_TEST(cpu_backend_prepare_rejects_scene_requested_for_cuda) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderJob job = MakeBackendTriangleJob(4, 3);
    job.settings.requested_backend = yr::RenderBackendKind::Cuda;

    yr::BackendPrepareResult prepared = backend->Prepare(std::move(job));

    YR_EXPECT_TRUE(!prepared.ok);
    YR_EXPECT_TRUE(prepared.scene == nullptr);
    YR_EXPECT_TRUE(prepared.error.find("CPU backend cannot prepare a scene requested for cuda") != std::string::npos);
}

YR_TEST(cpu_backend_renders_film_and_stats) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderJob job = MakeBackendTriangleJob(4, 3);

    yr::BackendPrepareResult prepared = backend->Prepare(std::move(job));
    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.scene != nullptr);

    const yr::RenderResult result = backend->Render(*prepared.scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->Width(), 4);
    YR_EXPECT_EQ(result.film->Height(), 3);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_backend_dispatches_path_integrator) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderJob job = MakeBackendTriangleJob(20, 17);
    job.settings.integrator = yr::RenderIntegratorKind::Path;
    job.settings.spp = 4;
    job.settings.threads = 2;

    yr::BackendPrepareResult prepared = backend->Prepare(std::move(job));
    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.scene != nullptr);

    const yr::RenderResult result = backend->Render(*prepared.scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->SampleCount(0, 0), 4);
    YR_EXPECT_TRUE(result.stats.rays_traced >= std::uint64_t{20 * 17 * 4});
    YR_EXPECT_EQ(result.stats.threads, 2);
}

YR_TEST(cuda_prepared_scene_exposes_source_scene_and_settings) {
    yr::RenderJob job = MakeBackendTriangleJob(2, 2);

    const yr::CudaPreparedScene prepared(std::move(job));

    YR_EXPECT_EQ(prepared.Kind(), yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(prepared.Settings().width, 2);
    YR_EXPECT_EQ(prepared.Settings().height, 2);
    YR_EXPECT_EQ(prepared.SourceScene().primitives.size(), std::size_t{1});
}

YR_TEST(create_render_backend_returns_cuda_stub_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cuda);
}

YR_TEST(cuda_backend_prepare_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    yr::RenderJob job = MakeBackendTriangleJob(1, 1);
    job.settings.requested_backend = yr::RenderBackendKind::Cuda;

    yr::BackendPrepareResult prepared = backend->Prepare(std::move(job));

    YR_EXPECT_TRUE(!prepared.ok);
    YR_EXPECT_TRUE(prepared.scene == nullptr);
    YR_EXPECT_TRUE(prepared.error.find("CUDA backend preparation is not implemented yet") != std::string::npos);
}

YR_TEST(cuda_backend_render_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    yr::RenderJob job = MakeBackendTriangleJob(1, 1);
    const yr::CudaPreparedScene prepared(std::move(job));

    const yr::RenderResult result = backend->Render(prepared, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CUDA backend rendering is not implemented yet") != std::string::npos);
}
