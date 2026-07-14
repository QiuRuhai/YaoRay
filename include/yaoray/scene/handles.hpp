#pragma once

namespace yr {

template <typename Tag>
class SceneHandle {
public:
    constexpr SceneHandle() = default;
    explicit constexpr SceneHandle(int value) : value_(value) {}

    constexpr bool IsValid() const { return value_ >= 0; }
    constexpr int Value() const { return value_; }

    friend constexpr bool operator==(SceneHandle, SceneHandle) = default;

private:
    int value_ = -1;
};

struct MeshPrimitiveTag;
struct InstanceTag;
struct SphereTag;
struct MaterialTag;
struct TextureTag;
struct EmissiveLightTag;
struct AnalyticLightTag;

using MeshPrimitiveHandle = SceneHandle<MeshPrimitiveTag>;
using InstanceHandle = SceneHandle<InstanceTag>;
using SphereHandle = SceneHandle<SphereTag>;
using MaterialHandle = SceneHandle<MaterialTag>;
using TextureHandle = SceneHandle<TextureTag>;
using EmissiveLightHandle = SceneHandle<EmissiveLightTag>;
using AnalyticLightHandle = SceneHandle<AnalyticLightTag>;

} // namespace yr
