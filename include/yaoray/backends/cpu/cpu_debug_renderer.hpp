#pragma once

#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/backends/cpu/cpu_render_stats.hpp>
#include <yaoray/film/film.hpp>

namespace yr {

struct CpuDebugRenderResult {
    Film film;
    CpuRenderStats stats;
};

CpuDebugRenderResult RenderCpuDebug(const CpuPreparedScene& prepared_scene);

} // namespace yr
