#include <yaoray/render/render_scene.hpp>

namespace yr {

std::string_view RenderBackendName(RenderBackendKind backend) {
    switch (backend) {
        case RenderBackendKind::Cpu: return "cpu";
        case RenderBackendKind::Cuda: return "cuda";
    }
    return "unknown";
}

std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name) {
    if (name == "cpu") return RenderBackendKind::Cpu;
    if (name == "cuda") return RenderBackendKind::Cuda;
    return std::nullopt;
}

std::string_view RenderIntegratorName(RenderIntegratorKind integrator) {
    switch (integrator) {
        case RenderIntegratorKind::DebugDirect: return "debug_direct";
        case RenderIntegratorKind::Path: return "path";
    }
    return "unknown";
}

std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name) {
    if (name == "debug_direct") return RenderIntegratorKind::DebugDirect;
    if (name == "path") return RenderIntegratorKind::Path;
    return std::nullopt;
}

std::string_view RenderSamplerName(RenderSamplerKind sampler) {
    switch (sampler) {
        case RenderSamplerKind::Independent: return "independent";
        case RenderSamplerKind::Stratified: return "stratified";
    }
    return "unknown";
}

std::optional<RenderSamplerKind> ParseRenderSamplerName(std::string_view name) {
    if (name == "independent") return RenderSamplerKind::Independent;
    if (name == "stratified") return RenderSamplerKind::Stratified;
    return std::nullopt;
}

std::string_view ToneMapperName(ToneMapperKind mapper) {
    switch (mapper) {
        case ToneMapperKind::None: return "none";
        case ToneMapperKind::Reinhard: return "reinhard";
        case ToneMapperKind::Aces: return "aces";
    }
    return "unknown";
}

std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name) {
    if (name == "none") return ToneMapperKind::None;
    if (name == "reinhard") return ToneMapperKind::Reinhard;
    if (name == "aces") return ToneMapperKind::Aces;
    return std::nullopt;
}

} // namespace yr
