#pragma once

#include <string>

#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/backends/cpu/cpu_render_stats.hpp>
#include <yaoray/film/film.hpp>

namespace yr {

struct CpuPathTraceResult {
    Film film;
    CpuRenderStats stats;
    bool ok = true;
    std::string error;
};

CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene, const RenderRequest& request = {});

} // namespace yr
