#include <yaoray/scene/scene.hpp>

namespace yr {

std::string_view RenderBackendName(RenderBackendKind backend) {
    switch (backend) {
        case RenderBackendKind::Cpu:
            return "cpu";
        case RenderBackendKind::Cuda:
            return "cuda";
    }
    return "unknown";
}

std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name) {
    if (name == "cpu") {
        return RenderBackendKind::Cpu;
    }
    if (name == "cuda") {
        return RenderBackendKind::Cuda;
    }
    return std::nullopt;
}

std::string_view RenderIntegratorName(RenderIntegratorKind integrator) {
    switch (integrator) {
        case RenderIntegratorKind::DebugDirect:
            return "debug_direct";
        case RenderIntegratorKind::Path:
            return "path";
    }
    return "unknown";
}

std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name) {
    if (name == "debug_direct") {
        return RenderIntegratorKind::DebugDirect;
    }
    if (name == "path") {
        return RenderIntegratorKind::Path;
    }
    return std::nullopt;
}

std::string_view RenderSamplerName(RenderSamplerKind sampler) {
    switch (sampler) {
        case RenderSamplerKind::Independent:
            return "independent";
        case RenderSamplerKind::Stratified:
            return "stratified";
    }
    return "unknown";
}

std::optional<RenderSamplerKind> ParseRenderSamplerName(std::string_view name) {
    if (name == "independent") {
        return RenderSamplerKind::Independent;
    }
    if (name == "stratified") {
        return RenderSamplerKind::Stratified;
    }
    return std::nullopt;
}

std::string_view ToneMapperName(ToneMapperKind mapper) {
    switch (mapper) {
        case ToneMapperKind::None:
            return "none";
        case ToneMapperKind::Reinhard:
            return "reinhard";
        case ToneMapperKind::Aces:
            return "aces";
    }
    return "unknown";
}

std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name) {
    if (name == "none") {
        return ToneMapperKind::None;
    }
    if (name == "reinhard") {
        return ToneMapperKind::Reinhard;
    }
    if (name == "aces") {
        return ToneMapperKind::Aces;
    }
    return std::nullopt;
}

std::string_view CameraKindName(CameraKind kind) {
    switch (kind) {
        case CameraKind::Perspective:
            return "perspective";
    }
    return "unknown";
}

std::optional<CameraKind> ParseCameraKindName(std::string_view name) {
    if (name == "perspective") {
        return CameraKind::Perspective;
    }
    return std::nullopt;
}

std::string_view LightKindName(LightKind kind) {
    switch (kind) {
        case LightKind::Area:
            return "area";
    }
    return "unknown";
}

std::optional<LightKind> ParseLightKindName(std::string_view name) {
    if (name == "area") {
        return LightKind::Area;
    }
    return std::nullopt;
}

std::string_view EnvironmentKindName(EnvironmentKind kind) {
    switch (kind) {
        case EnvironmentKind::None:
            return "none";
        case EnvironmentKind::Constant:
            return "constant";
        case EnvironmentKind::Hdri:
            return "hdri";
    }
    return "unknown";
}

std::optional<EnvironmentKind> ParseEnvironmentKindName(std::string_view name) {
    if (name == "none") {
        return EnvironmentKind::None;
    }
    if (name == "constant") {
        return EnvironmentKind::Constant;
    }
    if (name == "hdri") {
        return EnvironmentKind::Hdri;
    }
    return std::nullopt;
}

} // namespace yr
