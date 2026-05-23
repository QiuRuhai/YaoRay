#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/render/environment.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/shading.hpp>

namespace {

float Luminance(yr::Color3f color) {
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

bool ColorEqual(yr::Color3f a, yr::Color3f b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

yr::CpuPreparedScene PreparePathScene(const yr::RenderSceneIR& scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(scene);
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

yr::CpuPathTraceResult RunPathTrace(const yr::RenderSceneIR& scene) {
    return yr::RenderCpuPathTrace(PreparePathScene(scene));
}

yr::CpuPathTraceResult RunPathTrace(const yr::RenderSceneIR& scene, const yr::RenderRequest& request) {
    return yr::RenderCpuPathTrace(PreparePathScene(scene), request);
}

YR_TEST(shading_normal_interpolates_vertex_normals) {
    yr::RenderTriangle triangle;
    triangle.p0 = yr::Point3f{0.0f, 0.0f, 0.0f};
    triangle.p1 = yr::Point3f{1.0f, 0.0f, 0.0f};
    triangle.p2 = yr::Point3f{0.0f, 1.0f, 0.0f};
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n0 = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n1 = yr::Normalize(yr::Vec3f{0.0f, 1.0f, 1.0f});
    triangle.n2 = yr::Normalize(yr::Vec3f{1.0f, 0.0f, 1.0f});
    triangle.has_vertex_normals = true;

    const yr::Vec3f barycentric{0.2f, 0.3f, 0.5f};
    const yr::Vec3f normal = yr::ResolveShadingNormal(triangle, barycentric, yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_TRUE(normal.z > 0.8f);
    YR_EXPECT_TRUE(normal.x > 0.2f);
    YR_EXPECT_TRUE(normal.y > 0.1f);
}

YR_TEST(shading_normal_falls_back_to_geometric_normal) {
    yr::RenderTriangle triangle;
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};

    const yr::Vec3f normal =
        yr::ResolveShadingNormal(triangle, yr::Vec3f{0.2f, 0.3f, 0.5f}, yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_NEAR(normal.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.z, 1.0, 1e-6);
}

YR_TEST(shading_normal_is_corrected_to_geometric_hemisphere) {
    yr::RenderTriangle triangle;
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n0 = yr::Vec3f{0.0f, 0.0f, -1.0f};
    triangle.n1 = yr::Vec3f{0.0f, 0.0f, -1.0f};
    triangle.n2 = yr::Vec3f{0.0f, 0.0f, -1.0f};
    triangle.has_vertex_normals = true;

    const yr::Vec3f normal =
        yr::ResolveShadingNormal(triangle, yr::Vec3f{0.3f, 0.3f, 0.4f}, yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_NEAR(normal.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.z, 1.0, 1e-6);
}

yr::RenderSceneIR MakeTexturedTriangleScene(std::uint64_t seed = 7, int threads = 1) {
    yr::RenderSceneIR scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = seed;
    scene.threads = threads;
    scene.light_samples = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.5f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.5f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.1f, 0.1f, 0.1f},
        yr::Color3f{},
        0.0f,
        0.04f,
        0
    });
    scene.textures.push_back(yr::RenderTexture{
        2,
        2,
        std::vector<yr::Color4f>{
            yr::Color4f{1.0f, 0.0f, 0.0f, 1.0f},
            yr::Color4f{0.0f, 1.0f, 0.0f, 1.0f},
            yr::Color4f{0.0f, 0.0f, 1.0f, 1.0f},
            yr::Color4f{1.0f, 1.0f, 1.0f, 1.0f}
        }
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 0.0f, -3.0f},
        yr::Point3f{0.0f, 0.0f, 3.0f},
        yr::Point3f{3.0f, 0.0f, -3.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0,
        yr::Vec2f{0.25f, 0.25f},
        yr::Vec2f{0.25f, 0.25f},
        yr::Vec2f{0.25f, 0.25f},
        true
    });
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, 0.0f},
        1.0f,
        1.0f,
        yr::Color3f{4.0f, 4.0f, 4.0f}
    });
    return scene;
}

yr::RenderSceneIR MakeBilinearTexturedTriangleScene() {
    yr::RenderSceneIR scene = MakeTexturedTriangleScene();
    scene.textures[0].filter = yr::TextureFilter::Bilinear;
    scene.triangles[0].uv0 = yr::Vec2f{0.5f, 0.5f};
    scene.triangles[0].uv1 = yr::Vec2f{0.5f, 0.5f};
    scene.triangles[0].uv2 = yr::Vec2f{0.5f, 0.5f};
    return scene;
}

yr::RenderSceneIR MakeNearestCenterTexturedTriangleScene() {
    yr::RenderSceneIR scene = MakeBilinearTexturedTriangleScene();
    scene.textures[0].filter = yr::TextureFilter::Nearest;
    return scene;
}

yr::RenderSceneIR MakeBaseScene(int width, int height) {
    yr::RenderSceneIR scene;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = 7;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.8f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.02f, 0.03f, 0.04f};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{0.8f, 0.8f, 0.8f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-1.5f, -1.0f, 0.0f},
        yr::Point3f{1.5f, -1.0f, 0.0f},
        yr::Point3f{0.0f, 1.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

yr::RenderSceneIR MakeHdriMissScene(yr::Color3f env_color) {
    yr::RenderSceneIR scene;
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 1;
    scene.max_depth = 2;
    scene.seed = 7;
    scene.threads = 1;
    scene.light_samples = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.strength = 1.0f;
    scene.environment.texture_index = 0;
    scene.environment.distribution_index = 0;

    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.wrap_s = yr::TextureWrap::Repeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {env_color};
    scene.textures.push_back(texture);
    scene.environment_distributions.push_back(yr::BuildEnvironmentDistribution(scene.textures[0]));
    return scene;
}

void AddHorizontalQuadOccluder(yr::RenderSceneIR& scene, float y, int material_index) {
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-20.0f, y, -20.0f},
        yr::Point3f{20.0f, y, -20.0f},
        yr::Point3f{20.0f, y, 20.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        material_index
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-20.0f, y, -20.0f},
        yr::Point3f{20.0f, y, 20.0f},
        yr::Point3f{-20.0f, y, 20.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        material_index
    });
}

yr::RenderMaterial MakeShadowGlassMaterial(
    bool thin,
    yr::Color3f albedo = yr::Color3f{1.0f, 1.0f, 1.0f},
    yr::Color3f absorption_color = yr::Color3f{1.0f, 1.0f, 1.0f},
    float absorption_distance = 1.0f
) {
    yr::RenderMaterial material;
    material.type = yr::MaterialKind::Dielectric;
    material.albedo = albedo;
    material.ior = 1.5f;
    material.roughness = 0.0f;
    material.thin = thin;
    material.absorption_color = absorption_color;
    material.absorption_distance = absorption_distance;
    return material;
}

yr::RenderSceneIR MakeDiffusePlaneUnderHdriScene(bool with_occluder) {
    yr::RenderSceneIR scene = MakeHdriMissScene(yr::Color3f{2.0f, 2.0f, 2.0f});
    scene.spp = 4;
    scene.max_depth = 1;
    scene.light_samples = 16;
    scene.camera.origin = yr::Point3f{0.0f, 1.0f, 2.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.45f, -1.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Normalize(yr::Cross(scene.camera.right, scene.camera.forward));
    scene.camera.fov_y_radians = 0.01f;
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{0.8f, 0.8f, 0.8f}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-2.0f, 0.0f, -2.0f},
        yr::Point3f{2.0f, 0.0f, -2.0f},
        yr::Point3f{0.0f, 0.0f, 2.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    if (with_occluder) {
        const auto add_ceiling_quad = [&](yr::Point3f p0, yr::Point3f p1, yr::Point3f p2, yr::Point3f p3) {
            scene.triangles.push_back(yr::RenderTriangle{p0, p1, p2, yr::Vec3f{0.0f, -1.0f, 0.0f}, 0});
            scene.triangles.push_back(yr::RenderTriangle{p0, p2, p3, yr::Vec3f{0.0f, -1.0f, 0.0f}, 0});
        };
        add_ceiling_quad(
            yr::Point3f{-5.0f, 0.5f, -5.0f},
            yr::Point3f{-0.2f, 0.5f, -5.0f},
            yr::Point3f{-0.2f, 0.5f, 5.0f},
            yr::Point3f{-5.0f, 0.5f, 5.0f}
        );
        add_ceiling_quad(
            yr::Point3f{0.2f, 0.5f, -5.0f},
            yr::Point3f{5.0f, 0.5f, -5.0f},
            yr::Point3f{5.0f, 0.5f, 5.0f},
            yr::Point3f{0.2f, 0.5f, 5.0f}
        );
        add_ceiling_quad(
            yr::Point3f{-0.2f, 0.5f, -5.0f},
            yr::Point3f{0.2f, 0.5f, -5.0f},
            yr::Point3f{0.2f, 0.5f, 0.7f},
            yr::Point3f{-0.2f, 0.5f, 0.7f}
        );
        add_ceiling_quad(
            yr::Point3f{-0.2f, 0.5f, 1.1f},
            yr::Point3f{0.2f, 0.5f, 1.1f},
            yr::Point3f{0.2f, 0.5f, 5.0f},
            yr::Point3f{-0.2f, 0.5f, 5.0f}
        );
    }
    return scene;
}

yr::RenderSceneIR MakeDiffusePlaneUnderHdriWithGlassOccluder(bool thin) {
    yr::RenderSceneIR scene = MakeDiffusePlaneUnderHdriScene(false);
    scene.materials.push_back(MakeShadowGlassMaterial(thin));
    const auto add_ceiling_quad = [&](yr::Point3f p0, yr::Point3f p1, yr::Point3f p2, yr::Point3f p3) {
        scene.triangles.push_back(yr::RenderTriangle{p0, p1, p2, yr::Vec3f{0.0f, -1.0f, 0.0f}, 1});
        scene.triangles.push_back(yr::RenderTriangle{p0, p2, p3, yr::Vec3f{0.0f, -1.0f, 0.0f}, 1});
    };
    add_ceiling_quad(
        yr::Point3f{-5.0f, 0.5f, -5.0f},
        yr::Point3f{-0.2f, 0.5f, -5.0f},
        yr::Point3f{-0.2f, 0.5f, 5.0f},
        yr::Point3f{-5.0f, 0.5f, 5.0f}
    );
    add_ceiling_quad(
        yr::Point3f{0.2f, 0.5f, -5.0f},
        yr::Point3f{5.0f, 0.5f, -5.0f},
        yr::Point3f{5.0f, 0.5f, 5.0f},
        yr::Point3f{0.2f, 0.5f, 5.0f}
    );
    add_ceiling_quad(
        yr::Point3f{-0.2f, 0.5f, -5.0f},
        yr::Point3f{0.2f, 0.5f, -5.0f},
        yr::Point3f{0.2f, 0.5f, 0.7f},
        yr::Point3f{-0.2f, 0.5f, 0.7f}
    );
    add_ceiling_quad(
        yr::Point3f{-0.2f, 0.5f, 1.1f},
        yr::Point3f{0.2f, 0.5f, 1.1f},
        yr::Point3f{0.2f, 0.5f, 5.0f},
        yr::Point3f{-0.2f, 0.5f, 5.0f}
    );
    return scene;
}

yr::RenderSceneIR MakeEmissiveTriangleScene() {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    scene.environment.radiance = yr::Color3f{};
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};
    return scene;
}

yr::RenderSceneIR MakeMirrorTriangleScene() {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    scene.max_depth = 2;
    scene.environment.radiance = yr::Color3f{0.2f, 0.4f, 0.6f};
    scene.environment.strength = 1.0f;
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{0.5f, 0.5f, 0.5f},
        yr::Color3f{}
    };
    return scene;
}

yr::RenderSceneIR MakeGlassPanelScene(yr::MaterialKind type, float roughness, bool thin) {
    yr::RenderSceneIR scene = MakeBaseScene(1, 1);
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.spp = 16;
    scene.max_depth = 3;
    scene.seed = 71;
    scene.threads = 1;
    scene.light_samples = 4;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.radiance = yr::Color3f{0.25f, 0.5f, 1.0f};
    scene.materials[0].type = type;
    scene.materials[0].albedo = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials[0].roughness = roughness;
    scene.materials[0].ior = 1.5f;
    scene.materials[0].thin = thin;
    scene.triangles[0] = yr::RenderTriangle{
        yr::Point3f{-2.0f, -2.0f, 0.0f},
        yr::Point3f{2.0f, -2.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    };
    return scene;
}

yr::RenderSceneIR MakeAbsorbingGlassSlabScene(bool thin = false) {
    yr::RenderSceneIR scene = MakeBaseScene(1, 1);
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.spp = 32;
    scene.max_depth = 4;
    scene.seed = 5;
    scene.threads = 1;
    scene.area_lights.clear();
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.environment.strength = 1.0f;
    scene.materials[0].type = yr::MaterialKind::Dielectric;
    scene.materials[0].albedo = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials[0].roughness = 0.0f;
    scene.materials[0].ior = 1.5f;
    scene.materials[0].thin = thin;
    scene.materials[0].absorption_color = yr::Color3f{0.25f, 0.70f, 1.0f};
    scene.materials[0].absorption_distance = 1.0f;
    scene.triangles = {
        yr::RenderTriangle{
            yr::Point3f{-2.0f, -2.0f, 0.0f},
            yr::Point3f{2.0f, -2.0f, 0.0f},
            yr::Point3f{0.0f, 2.0f, 0.0f},
            yr::Vec3f{0.0f, 0.0f, 1.0f},
            0
        },
        yr::RenderTriangle{
            yr::Point3f{-2.0f, -2.0f, -1.0f},
            yr::Point3f{0.0f, 2.0f, -1.0f},
            yr::Point3f{2.0f, -2.0f, -1.0f},
            yr::Vec3f{0.0f, 0.0f, -1.0f},
            0
        }
    };
    return scene;
}

yr::RenderSceneIR MakeStochasticEdgeScene(std::uint64_t seed) {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    scene.spp = 8;
    scene.seed = seed;
    scene.environment.radiance = yr::Color3f{0.0f, 0.0f, 0.0f};
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{1.0f, 0.25f, 0.125f};
    scene.triangles.clear();
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.06f, -0.8f, 0.0f},
        yr::Point3f{0.9f, -0.8f, 0.0f},
        yr::Point3f{-0.06f, 0.8f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

yr::RenderSceneIR MakeDiffuseFloorScene(std::uint64_t seed = 7) {
    yr::RenderSceneIR scene;
    scene.width = 3;
    scene.height = 3;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = seed;
    scene.camera.origin = yr::Point3f{0.0f, 0.5f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.5f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 0.0f, -3.0f},
        yr::Point3f{0.0f, 0.0f, 3.0f},
        yr::Point3f{3.0f, 0.0f, -3.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, 0.0f},
        2.0f,
        2.0f,
        yr::Color3f{4.0f, 4.0f, 4.0f}
    });
    return scene;
}

yr::RenderSceneIR MakeBlockedDiffuseFloorScene() {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene();
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 1.0f, -3.0f},
        yr::Point3f{0.0f, 1.0f, 3.0f},
        yr::Point3f{3.0f, 1.0f, -3.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        0
    });
    return scene;
}

yr::RenderSceneIR MakeAreaShadowSceneWithPane(const yr::RenderMaterial& pane_material) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.max_depth = 1;
    scene.threads = 1;
    scene.light_samples = 8;
    scene.materials.push_back(pane_material);
    AddHorizontalQuadOccluder(scene, 1.0f, 1);
    return scene;
}

yr::RenderSceneIR MakeAreaShadowSceneWithSlab(const yr::RenderMaterial& slab_material) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.max_depth = 1;
    scene.threads = 1;
    scene.light_samples = 8;
    scene.materials.push_back(slab_material);
    AddHorizontalQuadOccluder(scene, 0.75f, 1);
    AddHorizontalQuadOccluder(scene, 1.25f, 1);
    return scene;
}

yr::RenderSceneIR MakeAreaShadowSceneWithPaneAndOpaqueBlocker() {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.max_depth = 1;
    scene.threads = 1;
    scene.light_samples = 8;
    scene.materials.push_back(MakeShadowGlassMaterial(true));
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    });
    AddHorizontalQuadOccluder(scene, 0.75f, 1);
    AddHorizontalQuadOccluder(scene, 1.25f, 2);
    return scene;
}

yr::RenderSceneIR MakeDiffuseToExplicitEmitterScene(bool matching_explicit_area_light) {
    yr::RenderSceneIR scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 32;
    scene.max_depth = 2;
    scene.seed = 21;
    scene.light_samples = 128;
    scene.camera.origin = yr::Point3f{0.0f, 0.5f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.5f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    });
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 0.0f, -3.0f},
        yr::Point3f{0.0f, 0.0f, 3.0f},
        yr::Point3f{3.0f, 0.0f, -3.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, 2.0f, -25.0f},
        yr::Point3f{25.0f, 2.0f, -25.0f},
        yr::Point3f{25.0f, 2.0f, 25.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, 2.0f, -25.0f},
        yr::Point3f{25.0f, 2.0f, 25.0f},
        yr::Point3f{-25.0f, 2.0f, 25.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });

    scene.area_lights.push_back(yr::RenderAreaLight{
        matching_explicit_area_light ? yr::Point3f{0.0f, 2.0f, 0.0f} : yr::Point3f{0.0f, -2.0f, 0.0f},
        50.0f,
        50.0f,
        yr::Color3f{}
    });

    return scene;
}

yr::RenderSceneIR MakeMirrorToExplicitEmitterScene() {
    yr::RenderSceneIR scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 1;
    scene.max_depth = 2;
    scene.seed = 33;
    scene.light_samples = 128;
    scene.camera.origin = yr::Point3f{0.0f, 1.0f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -1.0f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    });
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-4.0f, 0.0f, -4.0f},
        yr::Point3f{0.0f, 0.0f, 4.0f},
        yr::Point3f{4.0f, 0.0f, -4.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, 2.0f, -33.0f},
        yr::Point3f{25.0f, 2.0f, -33.0f},
        yr::Point3f{25.0f, 2.0f, 17.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, 2.0f, -33.0f},
        yr::Point3f{25.0f, 2.0f, 17.0f},
        yr::Point3f{-25.0f, 2.0f, 17.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, -8.0f},
        50.0f,
        50.0f,
        yr::Color3f{}
    });
    return scene;
}

yr::RenderSceneIR MakeMirrorHallScene(int max_depth, std::uint64_t seed = 101) {
    yr::RenderSceneIR scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 128;
    scene.max_depth = max_depth;
    scene.seed = seed;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, 1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{0.2f, 0.2f, 0.2f},
        yr::Color3f{}
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, 1.0f},
        yr::Point3f{10.0f, -10.0f, 1.0f},
        yr::Point3f{10.0f, 10.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, -1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, 1.0f},
        yr::Point3f{10.0f, 10.0f, 1.0f},
        yr::Point3f{-10.0f, 10.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, -1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, -1.0f},
        yr::Point3f{10.0f, -10.0f, -1.0f},
        yr::Point3f{10.0f, 10.0f, -1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, -1.0f},
        yr::Point3f{10.0f, 10.0f, -1.0f},
        yr::Point3f{-10.0f, 10.0f, -1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });

    return scene;
}

yr::RenderSceneIR MakeIndirectBounceScene(int max_depth) {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    scene.max_depth = max_depth;
    scene.environment.radiance = yr::Color3f{0.4f, 0.5f, 0.6f};
    scene.environment.strength = 1.0f;
    scene.materials[0].albedo = yr::Color3f{0.9f, 0.9f, 0.9f};
    scene.area_lights.clear();
    return scene;
}

yr::RenderSceneIR MakeDirectLightScene() {
    return MakeDiffuseFloorScene();
}

yr::RenderSceneIR MakeShadowedDirectLightScene() {
    return MakeBlockedDiffuseFloorScene();
}

bool AnyPixelDifferent(const yr::Film& first, const yr::Film& second) {
    for (int y = 0; y < first.Height(); ++y) {
        for (int x = 0; x < first.Width(); ++x) {
            if (!ColorEqual(first.LinearPixel(x, y), second.LinearPixel(x, y))) {
                return true;
            }
        }
    }
    return false;
}

bool FilmsEqual(const yr::Film& first, const yr::Film& second) {
    if (first.Width() != second.Width() || first.Height() != second.Height()) {
        return false;
    }
    if (first.BadSampleCount() != second.BadSampleCount()) {
        return false;
    }
    for (int y = 0; y < first.Height(); ++y) {
        for (int x = 0; x < first.Width(); ++x) {
            if (first.SampleCount(x, y) != second.SampleCount(x, y)) {
                return false;
            }
            if (!ColorEqual(first.LinearPixel(x, y), second.LinearPixel(x, y))) {
                return false;
            }
        }
    }
    return true;
}

bool CoreStatsEqual(const yr::CpuPathTraceStats& first, const yr::CpuPathTraceStats& second) {
    return first.rays_traced == second.rays_traced &&
           first.shadow_rays == second.shadow_rays &&
           first.occluded_shadow_rays == second.occluded_shadow_rays &&
           first.triangle_tests == second.triangle_tests &&
           first.bvh_node_tests == second.bvh_node_tests &&
           first.bvh_nodes == second.bvh_nodes &&
           first.bvh_max_depth == second.bvh_max_depth &&
           first.hits == second.hits &&
           first.misses == second.misses;
}

yr::RenderSceneIR MakeThreadedDeterminismScene() {
    yr::RenderSceneIR scene = MakeBaseScene(40, 24);
    scene.spp = 3;
    scene.max_depth = 2;
    scene.seed = 99;
    scene.sampler = yr::RenderSamplerKind::Stratified;
    scene.light_samples = 4;
    scene.environment.radiance = yr::Color3f{0.05f, 0.06f, 0.07f};
    scene.materials[0].albedo = yr::Color3f{0.7f, 0.6f, 0.5f};
    scene.materials[0].type = yr::MaterialKind::Mirror;
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 0.5f, 2.0f},
        1.0f,
        1.0f,
        yr::Color3f{2.0f, 2.0f, 2.0f}
    });
    return scene;
}

} // namespace

YR_TEST(cpu_path_tracer_traces_one_sample_per_pixel) {
    const yr::RenderSceneIR scene = MakeBaseScene(4, 3);

    const yr::CpuPathTraceResult result = RunPathTrace(scene);

    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_accumulates_spp_samples) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 4;

    const yr::CpuPathTraceResult result = RunPathTrace(scene);

    YR_EXPECT_EQ(result.film.Width(), 2);
    YR_EXPECT_EQ(result.film.Height(), 2);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 4);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{16});
}

YR_TEST(cpu_path_tracer_reports_progress_after_each_sample_pass) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 3;
    std::vector<int> completed;

    yr::RenderRequest request;
    request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film& film) {
        completed.push_back(progress.completed_spp);
        YR_EXPECT_EQ(progress.target_spp, 3);
        YR_EXPECT_EQ(progress.target_samples, std::uint64_t{12});
        YR_EXPECT_EQ(progress.completed_samples, static_cast<std::uint64_t>(progress.completed_spp * 4));
        YR_EXPECT_EQ(film.SampleCount(0, 0), static_cast<std::uint32_t>(progress.completed_spp));
        return yr::RenderProgressDecision{};
    };

    const yr::CpuPathTraceResult result = RunPathTrace(scene, request);

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(completed.size(), std::size_t{3});
    YR_EXPECT_EQ(completed[0], 1);
    YR_EXPECT_EQ(completed[1], 2);
    YR_EXPECT_EQ(completed[2], 3);
}

YR_TEST(cpu_path_tracer_resume_matches_uninterrupted_render) {
    yr::RenderSceneIR full_scene = MakeThreadedDeterminismScene();
    full_scene.spp = 4;
    full_scene.threads = 2;

    yr::RenderRequest partial_request;
    partial_request.progress_callback = [](const yr::RenderProgress& progress, const yr::Film&) {
        if (progress.completed_spp == 2) {
            return yr::RenderProgressDecision{true, "checkpoint after two passes"};
        }
        return yr::RenderProgressDecision{};
    };
    const yr::CpuPathTraceResult partial = RunPathTrace(full_scene, partial_request);
    yr::RenderRequest resume_request;
    resume_request.resume_film = &partial.film;
    resume_request.resume_completed_spp = 2;
    const yr::CpuPathTraceResult resumed = RunPathTrace(full_scene, resume_request);
    const yr::CpuPathTraceResult uninterrupted = RunPathTrace(full_scene);

    YR_EXPECT_TRUE(!partial.ok);
    YR_EXPECT_EQ(partial.film.SampleCount(0, 0), 2);
    YR_EXPECT_TRUE(resumed.ok);
    YR_EXPECT_TRUE(uninterrupted.ok);
    YR_EXPECT_TRUE(FilmsEqual(resumed.film, uninterrupted.film));
    YR_EXPECT_TRUE(resumed.stats.rays_traced > 0);
}

YR_TEST(cpu_path_tracer_rejects_invalid_resume_sample_count) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 4;
    yr::Film film{2, 2};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 0.0f, 0.0f}, 1});

    yr::RenderRequest request;
    request.resume_film = &film;
    request.resume_completed_spp = 2;
    const yr::CpuPathTraceResult result = RunPathTrace(scene, request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("resume") != std::string::npos);
}

YR_TEST(cpu_path_tracer_progress_callback_can_cancel_render) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 3;

    yr::RenderRequest request;
    request.progress_callback = [](const yr::RenderProgress&, const yr::Film&) {
        return yr::RenderProgressDecision{true, "stop after first pass"};
    };

    const yr::CpuPathTraceResult result = RunPathTrace(scene, request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("stop after first pass") != std::string::npos);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
}

YR_TEST(cpu_path_tracer_reports_cpu_prepared_bvh_stats) {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    for (int i = 0; i < 5; ++i) {
        const float x = 10.0f + static_cast<float>(i);
        scene.triangles.push_back(yr::RenderTriangle{
            yr::Point3f{x - 0.25f, -0.25f, 0.0f},
            yr::Point3f{x + 0.25f, -0.25f, 0.0f},
            yr::Point3f{x, 0.25f, 0.0f},
            yr::Vec3f{0.0f, 0.0f, 1.0f},
            0
        });
    }

    const yr::CpuPathTraceResult result = RunPathTrace(scene);

    YR_EXPECT_TRUE(result.stats.bvh_nodes >= 3);
    YR_EXPECT_TRUE(result.stats.bvh_max_depth >= 2);
}

YR_TEST(cpu_path_tracer_sees_emissive_surfaces) {
    const yr::RenderSceneIR scene = MakeEmissiveTriangleScene();

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
}

YR_TEST(cpu_path_tracer_radiance_clamp_limits_sample_max_component) {
    yr::RenderSceneIR scene = MakeEmissiveTriangleScene();
    scene.radiance_clamp = 10.0f;
    scene.materials[0].emission = yr::Color3f{100.0f, 50.0f, 25.0f};

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 10.0, 1e-5);
    YR_EXPECT_NEAR(center.y, 5.0, 1e-5);
    YR_EXPECT_NEAR(center.z, 2.5, 1e-5);
}

YR_TEST(cpu_path_tracer_radiance_clamp_is_disabled_by_default) {
    yr::RenderSceneIR scene = MakeEmissiveTriangleScene();
    scene.materials[0].emission = yr::Color3f{100.0f, 50.0f, 25.0f};

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 100.0, 1e-5);
    YR_EXPECT_NEAR(center.y, 50.0, 1e-5);
    YR_EXPECT_NEAR(center.z, 25.0, 1e-5);
}

YR_TEST(cpu_path_tracer_camera_miss_sees_hdri_environment) {
    yr::RenderSceneIR scene = MakeHdriMissScene(yr::Color3f{0.25f, 0.5f, 1.0f});

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_NEAR(pixel.x, 0.25, 1e-5);
    YR_EXPECT_NEAR(pixel.y, 0.5, 1e-5);
    YR_EXPECT_NEAR(pixel.z, 1.0, 1e-5);
}

YR_TEST(cpu_path_tracer_diffuse_surface_receives_direct_hdri_light) {
    yr::RenderSceneIR scene = MakeDiffusePlaneUnderHdriScene(false);

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.x > 0.05f);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_occluder_blocks_direct_hdri_light) {
    const yr::CpuPathTraceResult open_result = RunPathTrace(MakeDiffusePlaneUnderHdriScene(false));
    const yr::CpuPathTraceResult occluded_result = RunPathTrace(MakeDiffusePlaneUnderHdriScene(true));

    const float open_luminance = open_result.film.LinearPixel(0, 0).x;
    const float occluded_luminance = occluded_result.film.LinearPixel(0, 0).x;

    YR_EXPECT_TRUE(open_luminance > occluded_luminance);
    YR_EXPECT_TRUE(occluded_result.stats.occluded_shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_clear_glass_pane_transmits_hdri_direct_light_shadow) {
    const yr::CpuPathTraceResult open_result = RunPathTrace(MakeDiffusePlaneUnderHdriScene(false));
    const yr::CpuPathTraceResult glass_result =
        RunPathTrace(MakeDiffusePlaneUnderHdriWithGlassOccluder(true));

    const float open_luminance = Luminance(open_result.film.LinearPixel(0, 0));
    const float glass_luminance = Luminance(glass_result.film.LinearPixel(0, 0));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(glass_luminance > open_luminance * 0.8f);
    YR_EXPECT_EQ(glass_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_mirror_reflects_environment) {
    const yr::CpuPathTraceResult result = RunPathTrace(MakeMirrorTriangleScene());
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.1, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.3, 1e-6);
}

YR_TEST(cpu_path_tracer_mirror_skips_diffuse_direct_lighting) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.max_depth = 1;
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    };

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_black_mirror_stops_after_emission) {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    scene.max_depth = 2;
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{},
        yr::Color3f{0.25f, 0.5f, 0.75f}
    };

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
}

YR_TEST(cpu_path_tracer_smooth_glass_transmits_environment) {
    yr::RenderSceneIR scene = MakeGlassPanelScene(yr::MaterialKind::Dielectric, 0.0f, false);
    scene.spp = 1;
    scene.max_depth = 2;

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.z > 0.1f);
    YR_EXPECT_TRUE(result.stats.rays_traced > 1);
}

YR_TEST(cpu_path_tracer_thin_glass_panel_does_not_black_out_environment) {
    const yr::CpuPathTraceResult result =
        RunPathTrace(MakeGlassPanelScene(yr::MaterialKind::Dielectric, 0.0f, true));
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.z > 0.1f);
    YR_EXPECT_TRUE(result.stats.rays_traced > 1);
}

YR_TEST(cpu_path_tracer_rough_glass_is_deterministic) {
    const yr::RenderSceneIR scene = MakeGlassPanelScene(yr::MaterialKind::Dielectric, 0.35f, false);

    const yr::CpuPathTraceResult first = RunPathTrace(scene);
    const yr::CpuPathTraceResult second = RunPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
    YR_EXPECT_TRUE(Luminance(first.film.LinearPixel(0, 0)) > 0.0f);
}

YR_TEST(cpu_path_tracer_absorbing_glass_tints_transmitted_environment) {
    yr::RenderSceneIR neutral = MakeAbsorbingGlassSlabScene(false);
    neutral.materials[0].absorption_color = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::CpuPathTraceResult neutral_result = RunPathTrace(neutral);
    const yr::CpuPathTraceResult tinted_result = RunPathTrace(MakeAbsorbingGlassSlabScene(false));
    const yr::Color3f neutral_pixel = neutral_result.film.LinearPixel(0, 0);
    const yr::Color3f tinted_pixel = tinted_result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(tinted_pixel.x < neutral_pixel.x * 0.8f);
    YR_EXPECT_TRUE(tinted_pixel.y < neutral_pixel.y);
    YR_EXPECT_TRUE(tinted_pixel.z > tinted_pixel.x * 1.5f);
    YR_EXPECT_TRUE(tinted_pixel.z > tinted_pixel.y);
}

YR_TEST(cpu_path_tracer_thin_glass_ignores_thickness_absorption) {
    yr::RenderSceneIR thin_absorbing = MakeAbsorbingGlassSlabScene(true);
    yr::RenderSceneIR thin_neutral = thin_absorbing;
    thin_neutral.materials[0].absorption_color = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::CpuPathTraceResult absorbing_result = RunPathTrace(thin_absorbing);
    const yr::CpuPathTraceResult neutral_result = RunPathTrace(thin_neutral);
    const yr::Color3f absorbing_pixel = absorbing_result.film.LinearPixel(0, 0);
    const yr::Color3f neutral_pixel = neutral_result.film.LinearPixel(0, 0);

    YR_EXPECT_NEAR(absorbing_pixel.x, neutral_pixel.x, 1e-5);
    YR_EXPECT_NEAR(absorbing_pixel.y, neutral_pixel.y, 1e-5);
    YR_EXPECT_NEAR(absorbing_pixel.z, neutral_pixel.z, 1e-5);
}

YR_TEST(cpu_path_tracer_uses_diffuse_texture_albedo_on_hit) {
    const yr::CpuPathTraceResult result = RunPathTrace(MakeTexturedTriangleScene());
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.x > 0.0f);
    YR_EXPECT_TRUE(pixel.y < pixel.x * 0.1f);
    YR_EXPECT_TRUE(pixel.z < pixel.x * 0.1f);
}

YR_TEST(cpu_path_tracer_uses_bilinear_texture_sampling) {
    const yr::CpuPathTraceResult nearest = RunPathTrace(MakeNearestCenterTexturedTriangleScene());
    const yr::CpuPathTraceResult bilinear = RunPathTrace(MakeBilinearTexturedTriangleScene());
    const yr::Color3f nearest_pixel = nearest.film.LinearPixel(0, 0);
    const yr::Color3f bilinear_pixel = bilinear.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(bilinear_pixel.x > 0.0f);
    YR_EXPECT_NEAR(bilinear_pixel.x, bilinear_pixel.y, 1e-5);
    YR_EXPECT_NEAR(bilinear_pixel.y, bilinear_pixel.z, 1e-5);
    YR_EXPECT_TRUE(bilinear_pixel.x < nearest_pixel.x * 0.75f);
}

YR_TEST(cpu_path_tracer_textured_scene_is_deterministic_across_thread_counts) {
    const yr::CpuPathTraceResult single = RunPathTrace(MakeTexturedTriangleScene(91, 1));
    const yr::CpuPathTraceResult threaded = RunPathTrace(MakeTexturedTriangleScene(91, 8));

    YR_EXPECT_TRUE(FilmsEqual(single.film, threaded.film));
    YR_EXPECT_EQ(single.stats.rays_traced, threaded.stats.rays_traced);
}

YR_TEST(cpu_path_tracer_is_deterministic_for_same_seed) {
    const yr::RenderSceneIR scene = MakeStochasticEdgeScene(123);

    const yr::CpuPathTraceResult first = RunPathTrace(scene);
    const yr::CpuPathTraceResult second = RunPathTrace(scene);

    YR_EXPECT_TRUE(ColorEqual(first.film.LinearPixel(1, 1), second.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_changes_stochastic_result_for_different_seed) {
    const yr::CpuPathTraceResult first = RunPathTrace(MakeStochasticEdgeScene(123));
    const yr::CpuPathTraceResult second = RunPathTrace(MakeStochasticEdgeScene(456));

    YR_EXPECT_TRUE(AnyPixelDifferent(first.film, second.film));
}

YR_TEST(cpu_path_tracer_stratified_sampler_changes_stochastic_result) {
    yr::RenderSceneIR independent = MakeStochasticEdgeScene(123);
    independent.sampler = yr::RenderSamplerKind::Independent;

    yr::RenderSceneIR stratified = independent;
    stratified.sampler = yr::RenderSamplerKind::Stratified;

    const yr::CpuPathTraceResult independent_result = RunPathTrace(independent);
    const yr::CpuPathTraceResult stratified_result = RunPathTrace(stratified);

    YR_EXPECT_TRUE(AnyPixelDifferent(independent_result.film, stratified_result.film));
}

YR_TEST(cpu_path_tracer_adds_direct_area_light_contribution) {
    const yr::CpuPathTraceResult result = RunPathTrace(MakeDirectLightScene());
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_counts_shadow_occlusion_and_dims_direct_light) {
    const yr::RenderSceneIR unblocked = MakeDiffuseFloorScene();
    const yr::RenderSceneIR blocked = MakeShadowedDirectLightScene();

    const yr::CpuPathTraceResult unblocked_result = RunPathTrace(unblocked);
    const yr::CpuPathTraceResult blocked_result = RunPathTrace(blocked);

    YR_EXPECT_TRUE(unblocked_result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(unblocked_result.stats.occluded_shadow_rays, std::uint64_t{0});
    YR_EXPECT_TRUE(blocked_result.stats.shadow_rays > 0);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
    YR_EXPECT_TRUE(Luminance(blocked_result.film.LinearPixel(1, 1)) < Luminance(unblocked_result.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_opaque_shadow_occluder_still_blocks_direct_light) {
    const yr::CpuPathTraceResult open_result = RunPathTrace(MakeDiffuseFloorScene(7));
    const yr::CpuPathTraceResult blocked_result = RunPathTrace(MakeBlockedDiffuseFloorScene());

    const float open_luminance = Luminance(open_result.film.LinearPixel(1, 1));
    const float blocked_luminance = Luminance(blocked_result.film.LinearPixel(1, 1));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(blocked_luminance < open_luminance);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_clear_glass_pane_transmits_area_light_shadow) {
    const yr::CpuPathTraceResult open_result = RunPathTrace(MakeDiffuseFloorScene(7));
    const yr::CpuPathTraceResult glass_result =
        RunPathTrace(MakeAreaShadowSceneWithPane(MakeShadowGlassMaterial(true)));

    const float open_luminance = Luminance(open_result.film.LinearPixel(1, 1));
    const float glass_luminance = Luminance(glass_result.film.LinearPixel(1, 1));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(glass_luminance > open_luminance * 0.8f);
    YR_EXPECT_EQ(glass_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_absorbing_glass_slab_tints_area_light_shadow) {
    const yr::CpuPathTraceResult clear_result =
        RunPathTrace(MakeAreaShadowSceneWithSlab(MakeShadowGlassMaterial(false)));
    const yr::CpuPathTraceResult tinted_result = RunPathTrace(MakeAreaShadowSceneWithSlab(
        MakeShadowGlassMaterial(false, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{0.25f, 0.70f, 1.0f}, 0.5f)
    ));

    const yr::Color3f clear = clear_result.film.LinearPixel(1, 1);
    const yr::Color3f tinted = tinted_result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(clear.x > 0.0f);
    YR_EXPECT_TRUE(tinted.x < clear.x * 0.8f);
    YR_EXPECT_TRUE(tinted.y < clear.y);
    YR_EXPECT_TRUE(tinted.z > tinted.x * 1.5f);
    YR_EXPECT_TRUE(tinted.z > tinted.y);
    YR_EXPECT_EQ(tinted_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_thin_glass_shadow_ignores_absorption_distance) {
    const yr::CpuPathTraceResult neutral_result =
        RunPathTrace(MakeAreaShadowSceneWithPane(MakeShadowGlassMaterial(true)));
    const yr::CpuPathTraceResult absorbing_result = RunPathTrace(MakeAreaShadowSceneWithPane(
        MakeShadowGlassMaterial(true, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{0.1f, 0.2f, 1.0f}, 0.25f)
    ));

    const yr::Color3f neutral = neutral_result.film.LinearPixel(1, 1);
    const yr::Color3f absorbing = absorbing_result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(Luminance(neutral) > 0.0f);
    YR_EXPECT_NEAR(absorbing.x, neutral.x, 1e-5);
    YR_EXPECT_NEAR(absorbing.y, neutral.y, 1e-5);
    YR_EXPECT_NEAR(absorbing.z, neutral.z, 1e-5);
}

YR_TEST(cpu_path_tracer_glass_then_opaque_shadow_still_blocks_area_light) {
    const yr::CpuPathTraceResult open_result = RunPathTrace(MakeDiffuseFloorScene(7));
    const yr::CpuPathTraceResult blocked_result = RunPathTrace(MakeAreaShadowSceneWithPaneAndOpaqueBlocker());

    const float open_luminance = Luminance(open_result.film.LinearPixel(1, 1));
    const float blocked_luminance = Luminance(blocked_result.film.LinearPixel(1, 1));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(blocked_luminance < open_luminance);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce) {
    const yr::CpuPathTraceResult depth_one = RunPathTrace(MakeIndirectBounceScene(1));
    const yr::CpuPathTraceResult depth_two = RunPathTrace(MakeIndirectBounceScene(2));

    YR_EXPECT_TRUE(Luminance(depth_two.film.LinearPixel(1, 1)) > Luminance(depth_one.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_mis_weights_bsdf_sampled_explicit_emitter_hits) {
    const yr::CpuPathTraceResult without_matching_explicit_light =
        RunPathTrace(MakeDiffuseToExplicitEmitterScene(false));
    const yr::CpuPathTraceResult with_matching_explicit_light =
        RunPathTrace(MakeDiffuseToExplicitEmitterScene(true));

    const float without_luminance = Luminance(without_matching_explicit_light.film.LinearPixel(0, 0));
    const float with_luminance = Luminance(with_matching_explicit_light.film.LinearPixel(0, 0));

    YR_EXPECT_TRUE(without_luminance > 0.0f);
    YR_EXPECT_TRUE(with_luminance > 0.0f);
    YR_EXPECT_TRUE(with_luminance < without_luminance);
}

YR_TEST(cpu_path_tracer_delta_bsdf_sampled_emissive_hits_keep_full_weight) {
    const yr::CpuPathTraceResult result = RunPathTrace(MakeMirrorToExplicitEmitterScene());
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_NEAR(pixel.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(pixel.z, 1.0, 1e-6);
}

YR_TEST(cpu_path_tracer_russian_roulette_does_not_start_before_fixed_depth) {
    const yr::RenderSceneIR scene = MakeMirrorHallScene(4);
    const yr::CpuPathTraceResult result = RunPathTrace(scene);

    YR_EXPECT_EQ(result.stats.rays_traced, static_cast<std::uint64_t>(scene.spp * scene.max_depth));
    YR_EXPECT_EQ(result.stats.hits, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_russian_roulette_terminates_low_throughput_high_depth_paths) {
    const yr::RenderSceneIR scene = MakeMirrorHallScene(12);
    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const std::uint64_t no_roulette_rays = static_cast<std::uint64_t>(scene.spp * scene.max_depth);
    const std::uint64_t minimum_pre_roulette_rays = static_cast<std::uint64_t>(scene.spp * 4);

    YR_EXPECT_TRUE(result.stats.rays_traced >= minimum_pre_roulette_rays);
    YR_EXPECT_TRUE(result.stats.rays_traced < no_roulette_rays);
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_russian_roulette_is_deterministic_for_same_seed) {
    const yr::RenderSceneIR scene = MakeMirrorHallScene(12, 202);

    const yr::CpuPathTraceResult first = RunPathTrace(scene);
    const yr::CpuPathTraceResult second = RunPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}

YR_TEST(cpu_path_tracer_direct_light_uses_diffuse_brdf_weight) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.area_lights[0].width = 2.0f;
    scene.area_lights[0].height = 2.0f;
    scene.area_lights[0].radiance = yr::Color3f{4.0f, 4.0f, 4.0f};

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(center.x < 2.0f);
    YR_EXPECT_TRUE(center.y < 2.0f);
    YR_EXPECT_TRUE(center.z < 2.0f);
}

YR_TEST(cpu_path_tracer_direct_light_mis_downweights_large_light_against_diffuse_bsdf) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(136539);
    scene.max_depth = 1;
    scene.spp = 1;
    scene.light_samples = 1;
    scene.area_lights[0].width = 50.0f;
    scene.area_lights[0].height = 50.0f;
    scene.area_lights[0].radiance = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(center.x < 0.01f);
    YR_EXPECT_TRUE(center.y < 0.01f);
    YR_EXPECT_TRUE(center.z < 0.01f);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_area_light_sampling_changes_with_seed) {
    yr::RenderSceneIR first_scene = MakeDiffuseFloorScene(11);
    yr::RenderSceneIR second_scene = MakeDiffuseFloorScene(12);
    first_scene.spp = 1;
    second_scene.spp = 1;

    const yr::CpuPathTraceResult first = RunPathTrace(first_scene);
    const yr::CpuPathTraceResult second = RunPathTrace(second_scene);

    YR_EXPECT_TRUE(!ColorEqual(first.film.LinearPixel(1, 1), second.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_ignores_invalid_area_light_size) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene();
    scene.area_lights[0].width = 0.0f;
    scene.area_lights[0].height = 2.0f;

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_ignores_area_light_behind_surface) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene();
    scene.area_lights[0].position = yr::Point3f{0.0f, -2.0f, 0.0f};

    const yr::CpuPathTraceResult result = RunPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_light_samples_increase_shadow_rays) {
    yr::RenderSceneIR one_sample = MakeDiffuseFloorScene(7);
    one_sample.light_samples = 1;

    yr::RenderSceneIR four_samples = one_sample;
    four_samples.light_samples = 4;

    const yr::CpuPathTraceResult one_result = RunPathTrace(one_sample);
    const yr::CpuPathTraceResult four_result = RunPathTrace(four_samples);

    YR_EXPECT_TRUE(one_result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(four_result.stats.shadow_rays, one_result.stats.shadow_rays * std::uint64_t{4});
    YR_EXPECT_EQ(four_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_is_deterministic_with_multiple_light_samples) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.light_samples = 4;

    const yr::CpuPathTraceResult first = RunPathTrace(scene);
    const yr::CpuPathTraceResult second = RunPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}

YR_TEST(cpu_path_tracer_stratified_sampler_is_deterministic) {
    yr::RenderSceneIR scene = MakeDiffuseFloorScene(7);
    scene.sampler = yr::RenderSamplerKind::Stratified;
    scene.spp = 4;
    scene.light_samples = 4;

    const yr::CpuPathTraceResult first = RunPathTrace(scene);
    const yr::CpuPathTraceResult second = RunPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}

YR_TEST(cpu_path_tracer_reports_single_thread_when_requested) {
    yr::RenderSceneIR scene = MakeThreadedDeterminismScene();
    scene.threads = 1;

    const yr::CpuPathTraceResult result = RunPathTrace(scene);

    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_reports_capped_requested_threads) {
    yr::RenderSceneIR scene = MakeBaseScene(4, 4);
    scene.threads = 32;

    const yr::CpuPathTraceResult result = RunPathTrace(scene);

    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_is_bitwise_identical_across_thread_counts) {
    yr::RenderSceneIR single_thread = MakeThreadedDeterminismScene();
    single_thread.threads = 1;
    yr::RenderSceneIR two_threads = single_thread;
    two_threads.threads = 2;
    yr::RenderSceneIR four_threads = single_thread;
    four_threads.threads = 4;

    const yr::CpuPathTraceResult single_result = RunPathTrace(single_thread);
    const yr::CpuPathTraceResult two_result = RunPathTrace(two_threads);
    const yr::CpuPathTraceResult four_result = RunPathTrace(four_threads);

    YR_EXPECT_EQ(single_result.stats.threads, 1);
    YR_EXPECT_EQ(two_result.stats.threads, 2);
    YR_EXPECT_EQ(four_result.stats.threads, 4);
    YR_EXPECT_TRUE(FilmsEqual(single_result.film, two_result.film));
    YR_EXPECT_TRUE(FilmsEqual(single_result.film, four_result.film));
    YR_EXPECT_TRUE(CoreStatsEqual(single_result.stats, two_result.stats));
    YR_EXPECT_TRUE(CoreStatsEqual(single_result.stats, four_result.stats));
}
