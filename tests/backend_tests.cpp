#include "yr_test.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <yaoray/backends/backend.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderSceneIR MakeBackendTriangleScene(int width = 4, int height = 3) {
    yr::RenderSceneIR scene;
    scene.requested_backend = yr::RenderBackendKind::Cpu;
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
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 0.2f, 0.1f},
        yr::Color3f{}
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

class ForeignPreparedScene final : public yr::PreparedScene {
public:
    explicit ForeignPreparedScene(const yr::RenderSceneIR& scene)
        : scene_(&scene) {
    }

    yr::RenderBackendKind Kind() const override {
        return yr::RenderBackendKind::Cuda;
    }

    const yr::RenderSceneIR& SourceScene() const override {
        return *scene_;
    }

private:
    const yr::RenderSceneIR* scene_ = nullptr;
};

} // namespace

YR_TEST(create_render_backend_returns_cpu_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cpu);
}

YR_TEST(cpu_prepare_scene_builds_bvh_from_render_scene_ir) {
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);

    const yr::CpuPrepareResult prepared = yr::PrepareCpuScene(scene);

    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.error.empty());
    YR_EXPECT_TRUE(prepared.scene.has_value());
    YR_EXPECT_TRUE(prepared.scene->render_scene == &scene);
    YR_EXPECT_EQ(&prepared.scene->SourceScene(), &scene);
    YR_EXPECT_EQ(&prepared.scene->Scene(), &scene);
    YR_EXPECT_EQ(prepared.scene->Kind(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(prepared.scene->bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(prepared.scene->bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(prepared.scene->bvh.max_depth, 1);
}

YR_TEST(cpu_backend_prepare_builds_cpu_prepared_scene) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);

    yr::BackendPrepareResult prepared = backend->Prepare(scene);

    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.error.empty());
    YR_EXPECT_TRUE(prepared.scene != nullptr);
    YR_EXPECT_EQ(prepared.scene->Kind(), yr::RenderBackendKind::Cpu);

    const auto* cpu_scene = dynamic_cast<const yr::CpuPreparedScene*>(prepared.scene.get());
    YR_EXPECT_TRUE(cpu_scene != nullptr);
    YR_EXPECT_EQ(&cpu_scene->SourceScene(), &scene);
    YR_EXPECT_EQ(cpu_scene->bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(cpu_scene->bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(cpu_scene->bvh.max_depth, 1);
}

YR_TEST(cpu_backend_renders_film_and_stats) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);

    yr::BackendPrepareResult prepared = backend->Prepare(scene);
    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.scene != nullptr);

    const yr::RenderResult result = backend->Render(*prepared.scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->Width(), 4);
    YR_EXPECT_EQ(result.film->Height(), 3);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
    YR_EXPECT_TRUE(result.stats.triangle_tests <= result.stats.rays_traced);
    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_backend_dispatches_path_integrator) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderSceneIR scene = MakeBackendTriangleScene(20, 17);
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.spp = 4;
    scene.threads = 2;

    yr::BackendPrepareResult prepared = backend->Prepare(scene);
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

YR_TEST(cpu_backend_keeps_debug_direct_as_default_integrator) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);
    scene.integrator = yr::RenderIntegratorKind::DebugDirect;
    scene.spp = 4;
    scene.threads = 4;

    yr::BackendPrepareResult prepared = backend->Prepare(scene);
    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.scene != nullptr);

    const yr::RenderResult result = backend->Render(*prepared.scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_backend_rejects_non_cpu_prepared_scene) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(1, 1);
    const ForeignPreparedScene foreign_scene(scene);

    const yr::RenderResult result = backend->Render(foreign_scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CPU backend received a non-CPU prepared scene") != std::string::npos);
}

YR_TEST(create_render_backend_returns_cuda_stub_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cuda);
}

YR_TEST(cuda_backend_prepare_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    yr::RenderSceneIR scene = MakeBackendTriangleScene(1, 1);
    scene.requested_backend = yr::RenderBackendKind::Cuda;

    yr::BackendPrepareResult prepared = backend->Prepare(scene);

    YR_EXPECT_TRUE(!prepared.ok);
    YR_EXPECT_TRUE(prepared.scene == nullptr);
    YR_EXPECT_TRUE(prepared.error.find("CUDA backend preparation is not implemented yet") != std::string::npos);
}

YR_TEST(cuda_backend_render_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(1, 1);
    const ForeignPreparedScene prepared(scene);

    const yr::RenderResult result = backend->Render(prepared, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CUDA backend rendering is not implemented yet") != std::string::npos);
}
