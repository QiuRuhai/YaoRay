#pragma once

#include <cstdint>

#include <yaoray/scene/render_scene.hpp>

namespace yrtest {

inline void AddBvhTestTriangle(yr::RenderSceneIR& scene, float x_offset) {
    const auto first_vertex = static_cast<std::uint32_t>(scene.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(scene.indices.size());
    scene.vertices.push_back(yr::RenderVertex{
        yr::Point3f{x_offset - 0.25f, -0.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f
    });
    scene.vertices.push_back(yr::RenderVertex{
        yr::Point3f{x_offset + 0.25f, -0.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f
    });
    scene.vertices.push_back(yr::RenderVertex{
        yr::Point3f{x_offset, 0.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f
    });
    scene.indices.push_back(first_vertex);
    scene.indices.push_back(first_vertex + 1);
    scene.indices.push_back(first_vertex + 2);
    scene.primitives.push_back(yr::RenderPrimitive{
        first_index, 3, 0, true, false, false
    });
}

inline yr::RenderSceneIR MakeSingleBvhTestTriangle(float x_offset = 0.0f) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    AddBvhTestTriangle(scene, x_offset);
    return scene;
}

inline yr::RenderSceneIR MakeClusteredBvhTestScene() {
    yr::RenderSceneIR scene = MakeSingleBvhTestTriangle(0.0f);
    AddBvhTestTriangle(scene, 0.05f);
    AddBvhTestTriangle(scene, 0.10f);
    AddBvhTestTriangle(scene, 0.15f);
    AddBvhTestTriangle(scene, 5.00f);
    AddBvhTestTriangle(scene, 5.05f);
    AddBvhTestTriangle(scene, 5.10f);
    AddBvhTestTriangle(scene, 5.15f);
    AddBvhTestTriangle(scene, 10.00f);
    AddBvhTestTriangle(scene, 10.05f);
    AddBvhTestTriangle(scene, 10.10f);
    AddBvhTestTriangle(scene, 10.15f);
    return scene;
}

} // namespace yrtest
