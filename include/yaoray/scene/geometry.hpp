#pragma once

#include <cstdint>
#include <span>

#include <yaoray/core/transform.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/handles.hpp>

namespace yr {

struct RenderVertex {
    Point3f position;
    Vec3f normal;
    Vec2f uv;
    Vec3f tangent;
    float tangent_handedness = 1.0f;
};

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material_index = 0;
    bool has_normals = false;
    bool has_uvs = false;
    bool has_tangents = false;
};

struct RenderSphere {
    Point3f center{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    int material_index = 0;
    int area_light_index = -1;
    bool flip_normals = false;
};

// Mesh instances reference immutable primitive geometry. An empty instance
// table means one identity instance per primitive for backward compatibility.
struct RenderInstance {
    MeshPrimitiveHandle primitive;
    Mat4f object_to_world;
};

struct GeometryView {
    std::span<const RenderVertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const RenderPrimitive> primitives;
    std::span<const RenderSphere> spheres;
    std::span<const RenderInstance> instances{};

    const RenderPrimitive* Find(MeshPrimitiveHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < primitives.size()
            ? &primitives[static_cast<std::size_t>(index)]
            : nullptr;
    }

    const RenderSphere* Find(SphereHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < spheres.size()
            ? &spheres[static_cast<std::size_t>(index)]
            : nullptr;
    }

    const RenderInstance* Find(InstanceHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < instances.size()
            ? &instances[static_cast<std::size_t>(index)]
            : nullptr;
    }
};

} // namespace yr
