#include <yaoray/scene/render_scene_hash.hpp>

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

std::uint64_t ComputeRenderSettingsHash(
    const RenderSceneIR& scene,
    const RenderSettings& settings) {
    std::uint64_t hash = FnvOffset;
    HashString(hash, "YaoRayRenderSettingsHashV4");
    HashValue(hash, static_cast<int>(settings.requested_backend));
    HashValue(hash, static_cast<int>(settings.integrator));
    HashValue(hash, static_cast<int>(settings.sampler));
    HashValue(hash, settings.width);
    HashValue(hash, settings.height);
    HashValue(hash, settings.spp);
    HashValue(hash, settings.max_depth);
    HashValue(hash, settings.seed);
    HashValue(hash, settings.radiance_clamp);
    HashValue(hash, settings.adaptive_sampling.enabled);
    HashValue(hash, settings.adaptive_sampling.min_spp);
    HashValue(hash, settings.adaptive_sampling.relative_error);
    HashValue(hash, settings.adaptive_sampling.absolute_error);
    HashValue(hash, settings.adaptive_sampling.confidence);
    HashVec3(hash, scene.camera.origin);
    HashVec3(hash, scene.camera.forward);
    HashVec3(hash, scene.camera.right);
    HashVec3(hash, scene.camera.up);
    HashValue(hash, scene.camera.fov_y_radians);
    HashValue(hash, scene.environment.active);
    HashColor(hash, scene.environment.radiance);
    HashValue(hash, scene.environment.strength);
    HashValue(hash, scene.environment.rotation_radians);
    HashValue(hash, scene.vertices.size());
    HashValue(hash, scene.indices.size());
    HashValue(hash, scene.primitives.size());
    HashValue(hash, scene.spheres.size());
    HashValue(hash, scene.instances.size());
    for (const RenderInstance& instance : scene.instances) {
        HashValue(hash, instance.primitive.Value());
        for (float value : instance.object_to_world.m) HashValue(hash, value);
    }
    HashValue(hash, scene.materials.size());
    HashValue(hash, scene.textures.size());
    HashValue(hash, scene.emissive_primitives.size());
    return hash;
}

} // namespace yr
