#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace yr {

enum class RenderBackendKind { Cpu, Cuda };
enum class RenderIntegratorKind { DebugDirect, Path };
enum class RenderSamplerKind { Independent, Stratified, OwenSobol, ZSobol };
enum class ToneMapperKind { None, Reinhard, Aces };

std::string_view                    RenderBackendName(RenderBackendKind backend);
std::optional<RenderBackendKind>    ParseRenderBackendName(std::string_view name);
std::string_view                    RenderIntegratorName(RenderIntegratorKind integrator);
std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name);
std::string_view                    RenderSamplerName(RenderSamplerKind sampler);
std::optional<RenderSamplerKind>    ParseRenderSamplerName(std::string_view name);
std::string_view                    ToneMapperName(ToneMapperKind mapper);
std::optional<ToneMapperKind>       ParseToneMapperName(std::string_view name);

struct FilmSettings {
    std::filesystem::path output;
    ToneMapperKind        tone_mapper = ToneMapperKind::Aces;
    float                 exposure    = 0.0f;
};

struct AdaptiveSamplingSettings {
    bool enabled = false;
    int min_spp = 16;
    float relative_error = 0.05f;
    float absolute_error = 0.001f;
    float confidence = 1.96f;
};

struct RenderSettings {
    RenderBackendKind    requested_backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator        = RenderIntegratorKind::Path;
    RenderSamplerKind    sampler           = RenderSamplerKind::Independent;
    int                  width             = 0;
    int                  height            = 0;
    int                  spp               = 1;
    int                  max_depth         = 5;
    std::uint64_t        seed              = 0;
    int                  threads           = 0;
    float                radiance_clamp    = 0.0f;
    AdaptiveSamplingSettings adaptive_sampling;
    FilmSettings         film;
};

}  // namespace yr
