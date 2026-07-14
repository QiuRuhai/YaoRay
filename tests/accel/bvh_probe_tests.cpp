#include "yr_test.hpp"
#include <yaoray/accel/bvh.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <cstdint>

namespace {

int AddQuad(yr::RenderSceneIR& scene, float z0) {
    const auto base = static_cast<std::uint32_t>(scene.vertices.size());
    auto V = [&](float x, float y) {
        scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x, y, z0}, yr::Vec3f{0, 0, 1}, {}, {}, 1.0f});
    };
    V(-1, -1); V(1, -1); V(1, 1); V(-1, 1);
    scene.indices.push_back(base + 0); scene.indices.push_back(base + 1); scene.indices.push_back(base + 2);
    scene.indices.push_back(base + 0); scene.indices.push_back(base + 2); scene.indices.push_back(base + 3);
    int prim = (int)scene.primitives.size();
    scene.primitives.push_back(yr::RenderPrimitive{base, 6, 0, true, false, false});
    return prim;
}

}  // namespace

YR_TEST(bvh_probe_single_quad_one_hit) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    int prim = AddQuad(scene, 0.0f);
    auto built = yr::BuildBvh(scene.Geometry());
    YR_EXPECT_TRUE(built.errors.empty());

    yr::Ray3f ray{yr::Point3f{0.2f, -0.1f, -1.0f}, yr::Vec3f{0, 0, 1}};
    yr::BvhProbeHits hits = yr::IntersectBvhProbe(
        scene.Geometry(), built.bvh, ray,
        yr::MeshPrimitiveHandle{prim}, yr::SphereHandle{}, 1e-5f, 10.0f);
    YR_EXPECT_EQ(hits.count, 1);
    YR_EXPECT_EQ(hits.hits[0].mesh_primitive.Value(), prim);
}

YR_TEST(bvh_probe_two_layers_two_hits) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    auto V = [&](float x, float y, float z) {
        scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x, y, z}, yr::Vec3f{0, 0, 1}, {}, {}, 1.0f});
    };
    V(-1, -1, 0); V(1, -1, 0); V(1, 1, 0); V(-1, 1, 0);
    V(-1, -1, 2); V(1, -1, 2); V(1, 1, 2); V(-1, 1, 2);
    auto Tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        scene.indices.push_back(a); scene.indices.push_back(b); scene.indices.push_back(c);
    };
    Tri(0, 1, 2); Tri(0, 2, 3); Tri(4, 5, 6); Tri(4, 6, 7);
    scene.primitives.push_back(yr::RenderPrimitive{0, 12, 0, true, false, false});
    scene.materials.push_back(yr::RenderMaterial{});
    auto built = yr::BuildBvh(scene.Geometry());

    yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, -1.0f}, yr::Vec3f{0, 0, 1}};
    yr::BvhProbeHits hits = yr::IntersectBvhProbe(
        scene.Geometry(), built.bvh, ray,
        yr::MeshPrimitiveHandle{0}, yr::SphereHandle{}, 1e-5f, 10.0f);
    YR_EXPECT_EQ(hits.count, 2);
    YR_EXPECT_TRUE(hits.hits[0].t < hits.hits[1].t);
}

YR_TEST(bvh_probe_filters_other_primitives) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    int target = AddQuad(scene, 0.0f);  // primitive 0
    AddQuad(scene, 1.0f);               // primitive 1 (decoy)
    auto built = yr::BuildBvh(scene.Geometry());

    yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, -1.0f}, yr::Vec3f{0, 0, 1}};
    yr::BvhProbeHits hits = yr::IntersectBvhProbe(
        scene.Geometry(), built.bvh, ray,
        yr::MeshPrimitiveHandle{target}, yr::SphereHandle{}, 1e-5f, 10.0f);
    YR_EXPECT_EQ(hits.count, 1);
    YR_EXPECT_EQ(hits.hits[0].mesh_primitive.Value(), target);
}
