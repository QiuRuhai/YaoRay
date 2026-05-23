#include <yaoray/render/render_scene_hash.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace yr {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ull;
constexpr std::uint64_t FnvPrime = 1099511628211ull;

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= FnvPrime;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) {
    HashBytes(hash, &value, sizeof(T));
}

void HashString(std::uint64_t& hash, std::string_view value) {
    HashBytes(hash, value.data(), value.size());
}

void HashColor(std::uint64_t& hash, Color3f value) {
    HashValue(hash, value.x);
    HashValue(hash, value.y);
    HashValue(hash, value.z);
}

void HashVec3(std::uint64_t& hash, Vec3f value) {
    HashValue(hash, value.x);
    HashValue(hash, value.y);
    HashValue(hash, value.z);
}

} // namespace

std::uint64_t ComputeRenderSettingsHash(const RenderSceneIR& scene) {
    std::uint64_t hash = FnvOffset;
    HashString(hash, "YaoRayRenderSettingsHashV1");
    HashValue(hash, static_cast<int>(scene.requested_backend));
    HashValue(hash, static_cast<int>(scene.integrator));
    HashValue(hash, static_cast<int>(scene.sampler));
    HashValue(hash, scene.width);
    HashValue(hash, scene.height);
    HashValue(hash, scene.spp);
    HashValue(hash, scene.max_depth);
    HashValue(hash, scene.seed);
    HashValue(hash, scene.light_samples);
    HashValue(hash, scene.radiance_clamp);
    HashVec3(hash, scene.camera.origin);
    HashVec3(hash, scene.camera.forward);
    HashVec3(hash, scene.camera.right);
    HashVec3(hash, scene.camera.up);
    HashValue(hash, scene.camera.fov_y_radians);
    HashValue(hash, scene.camera.aperture);
    HashValue(hash, scene.camera.focus_distance);
    HashValue(hash, static_cast<int>(scene.environment.type));
    HashColor(hash, scene.environment.radiance);
    HashValue(hash, scene.environment.strength);
    HashValue(hash, scene.environment.rotation_radians);
    HashValue(hash, scene.vertices.size());
    HashValue(hash, scene.indices.size());
    HashValue(hash, scene.primitives.size());
    HashValue(hash, scene.triangles.size());
    HashValue(hash, scene.materials.size());
    HashValue(hash, scene.textures.size());
    HashValue(hash, scene.area_lights.size());
    return hash;
}

} // namespace yr
