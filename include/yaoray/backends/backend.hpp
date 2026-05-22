#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <yaoray/film/film.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct RenderRequest {
};

struct RenderStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t shadow_rays = 0;
    std::uint64_t occluded_shadow_rays = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t bvh_node_tests = 0;
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    int threads = 1;
    double elapsed_seconds = 0.0;
};

struct RenderResult {
    bool ok = false;
    std::string error;
    std::optional<Film> film;
    RenderStats stats;
};

class PreparedScene {
public:
    virtual ~PreparedScene() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual const RenderSceneIR& SourceScene() const = 0;
};

struct BackendPrepareResult {
    bool ok = false;
    std::string error;
    std::unique_ptr<PreparedScene> scene;
    double elapsed_seconds = 0.0;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual BackendPrepareResult Prepare(const RenderSceneIR& scene) = 0;
    virtual RenderResult Render(const PreparedScene& scene, const RenderRequest& request) = 0;
};

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind);

} // namespace yr
