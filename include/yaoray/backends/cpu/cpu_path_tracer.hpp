#pragma once

#include <cstdint>
#include <yaoray/film/film.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct CpuPathTraceStats {
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

struct CpuPathTraceResult {
    Film film;
    CpuPathTraceStats stats;
};

CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene);

} // namespace yr
