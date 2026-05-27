#include "yr_test.hpp"

#include <yaoray/backends/cpu/cpu_material.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/shading.hpp>

// TODO(Task 11): Rewrite CPU material tests for table-geometry API (TriangleRef, TexParam).

YR_TEST(cpu_material_resolve_returns_base_material_for_untextured) {
    yr::RenderSceneIR scene;
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Diffuse;
    mat.reflectance = yr::TexParam3f{{0.8f, 0.2f, 0.1f}};
    scene.materials.push_back(mat);
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{0.0f, 0.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{1.0f, 0.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{0.0f, 1.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});

    const yr::TriangleRef tri = yr::LocateTriangle(scene, 0);
    const yr::ResolvedMaterialSample sample = yr::ResolveCpuMaterialSample(
        scene, tri, scene.materials[0],
        0.33f, 0.33f,
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_EQ(sample.material.kind, yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_TRUE(yr::IsAlphaVisible(sample));
}
