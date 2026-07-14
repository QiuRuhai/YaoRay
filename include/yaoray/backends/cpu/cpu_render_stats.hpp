#pragma once

#include <yaoray/integrators/path_integrator.hpp>

namespace yr {

struct CpuRenderStats : PathTraceStats {
    std::uint64_t samples_rendered = 0;
    std::uint64_t converged_pixels = 0;
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
    int threads = 1;
    double elapsed_seconds = 0.0;
};

} // namespace yr
