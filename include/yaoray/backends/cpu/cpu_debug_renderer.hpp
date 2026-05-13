#pragma once

#include <cstdint>

#include <yaoray/film/film.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct CpuDebugRenderStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t bvh_node_tests = 0;
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    double elapsed_seconds = 0.0;
};

struct CpuDebugRenderResult {
    Film film;
    CpuDebugRenderStats stats;
};

CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene);

} // namespace yr
