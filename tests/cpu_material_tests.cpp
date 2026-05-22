#include "yr_test.hpp"

#include <cstddef>

#include <yaoray/backends/cpu/cpu_material.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderTriangle MakeTestTriangle() {
    yr::RenderTriangle triangle;
    triangle.p0 = yr::Point3f{0.0f, 0.0f, 0.0f};
    triangle.p1 = yr::Point3f{1.0f, 0.0f, 0.0f};
    triangle.p2 = yr::Point3f{0.0f, 1.0f, 0.0f};
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.uv0 = yr::Vec2f{0.0f, 0.0f};
    triangle.uv1 = yr::Vec2f{1.0f, 0.0f};
    triangle.uv2 = yr::Vec2f{0.0f, 1.0f};
    triangle.has_uv = true;
    triangle.n0 = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n1 = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n2 = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.has_vertex_normals = true;
    triangle.t0 = yr::Vec3f{1.0f, 0.0f, 0.0f};
    triangle.t1 = yr::Vec3f{1.0f, 0.0f, 0.0f};
    triangle.t2 = yr::Vec3f{1.0f, 0.0f, 0.0f};
    triangle.has_tangents = true;
    return triangle;
}

yr::RenderTexture OnePixelTexture(yr::Color4f texel) {
    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.texels = {texel};
    return texture;
}

} // namespace

YR_TEST(cpu_material_combines_base_color_texture_alpha) {
    yr::RenderSceneIR scene;
    scene.textures.push_back(OnePixelTexture(yr::Color4f{0.2f, 0.4f, 0.6f, 0.25f}));

    yr::RenderMaterial material;
    material.albedo = yr::Color3f{0.5f, 0.5f, 0.5f};
    material.albedo_alpha = 0.8f;
    material.albedo_texture = 0;

    const yr::ResolvedMaterialSample sample = yr::ResolveCpuMaterialSample(
        scene,
        MakeTestTriangle(),
        material,
        yr::Vec3f{1.0f, 0.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}
    );

    YR_EXPECT_NEAR(sample.material.albedo.x, 0.1, 1e-6);
    YR_EXPECT_NEAR(sample.material.albedo.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(sample.material.albedo.z, 0.3, 1e-6);
    YR_EXPECT_NEAR(sample.alpha, 0.2, 1e-6);
}

YR_TEST(cpu_material_samples_metallic_roughness_texture) {
    yr::RenderSceneIR scene;
    scene.textures.push_back(OnePixelTexture(yr::Color4f{0.0f, 0.2f, 0.7f, 1.0f}));

    yr::RenderMaterial material;
    material.type = yr::MaterialKind::Diffuse;
    material.roughness = 1.0f;
    material.metallic = 0.0f;
    material.metallic_roughness_texture = 0;

    const yr::ResolvedMaterialSample sample = yr::ResolveCpuMaterialSample(
        scene,
        MakeTestTriangle(),
        material,
        yr::Vec3f{1.0f, 0.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}
    );

    YR_EXPECT_NEAR(sample.material.roughness, 0.2, 1e-6);
    YR_EXPECT_NEAR(sample.material.metallic, 0.7, 1e-6);
    YR_EXPECT_EQ(sample.material.type, yr::MaterialKind::Metal);
}

YR_TEST(cpu_material_samples_emissive_texture) {
    yr::RenderSceneIR scene;
    scene.textures.push_back(OnePixelTexture(yr::Color4f{0.2f, 0.4f, 0.6f, 1.0f}));

    yr::RenderMaterial material;
    material.emission = yr::Color3f{0.5f, 0.5f, 0.5f};
    material.emissive_texture = 0;

    const yr::ResolvedMaterialSample sample = yr::ResolveCpuMaterialSample(
        scene,
        MakeTestTriangle(),
        material,
        yr::Vec3f{1.0f, 0.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}
    );

    YR_EXPECT_NEAR(sample.material.emission.x, 0.1, 1e-6);
    YR_EXPECT_NEAR(sample.material.emission.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(sample.material.emission.z, 0.3, 1e-6);
}

YR_TEST(cpu_material_resolves_normal_map_from_tangent_space) {
    yr::RenderSceneIR scene;
    scene.textures.push_back(OnePixelTexture(yr::Color4f{1.0f, 0.5f, 0.5f, 1.0f}));

    yr::RenderMaterial material;
    material.normal_texture = 0;
    material.normal_scale = 1.0f;

    const yr::ResolvedMaterialSample sample = yr::ResolveCpuMaterialSample(
        scene,
        MakeTestTriangle(),
        material,
        yr::Vec3f{1.0f, 0.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}
    );

    YR_EXPECT_TRUE(sample.shading_normal.x > 0.99f);
    YR_EXPECT_NEAR(sample.shading_normal.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.shading_normal.z, 0.0, 1e-6);
}
