#pragma once

#include <functional>
#include <vector>

namespace yr {

struct CpuTile {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

struct CpuTileSchedule {
    std::vector<CpuTile> tiles;
    int requested_threads = 0;
    int worker_count = 1;
    int tile_size = 16;
};

CpuTileSchedule BuildCpuTileSchedule(int width, int height, int requested_threads, int tile_size = 16);

using CpuTileCallback = std::function<void(const CpuTile& tile, int worker_index)>;

void ForEachCpuTile(const CpuTileSchedule& schedule, const CpuTileCallback& callback);

} // namespace yr
