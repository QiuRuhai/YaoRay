#include "yr_test.hpp"

#include <atomic>
#include <cstddef>
#include <vector>

#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>

YR_TEST(cpu_tile_scheduler_builds_row_major_tiles) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(35, 18, 1, 16);

    YR_EXPECT_EQ(schedule.requested_threads, 1);
    YR_EXPECT_EQ(schedule.worker_count, 1);
    YR_EXPECT_EQ(schedule.tile_size, 16);
    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{6});
    YR_EXPECT_EQ(schedule.tiles[0].x0, 0);
    YR_EXPECT_EQ(schedule.tiles[0].y0, 0);
    YR_EXPECT_EQ(schedule.tiles[0].x1, 16);
    YR_EXPECT_EQ(schedule.tiles[0].y1, 16);
    YR_EXPECT_EQ(schedule.tiles[1].x0, 16);
    YR_EXPECT_EQ(schedule.tiles[1].y0, 0);
    YR_EXPECT_EQ(schedule.tiles[2].x0, 32);
    YR_EXPECT_EQ(schedule.tiles[2].x1, 35);
    YR_EXPECT_EQ(schedule.tiles[3].x0, 0);
    YR_EXPECT_EQ(schedule.tiles[3].y0, 16);
    YR_EXPECT_EQ(schedule.tiles[5].x1, 35);
    YR_EXPECT_EQ(schedule.tiles[5].y1, 18);
}

YR_TEST(cpu_tile_scheduler_clamps_requested_threads_to_tile_count) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(10, 10, 8, 16);

    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{1});
    YR_EXPECT_EQ(schedule.requested_threads, 8);
    YR_EXPECT_EQ(schedule.worker_count, 1);
}

YR_TEST(cpu_tile_scheduler_auto_threads_are_positive_and_capped) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(64, 64, 0, 16);

    YR_EXPECT_EQ(schedule.requested_threads, 0);
    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{16});
    YR_EXPECT_TRUE(schedule.worker_count >= 1);
    YR_EXPECT_TRUE(schedule.worker_count <= static_cast<int>(schedule.tiles.size()));
}

YR_TEST(cpu_tile_scheduler_forces_minimum_tile_size) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(2, 2, 1, 0);

    YR_EXPECT_EQ(schedule.tile_size, 1);
    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{4});
}

YR_TEST(cpu_tile_scheduler_empty_image_has_no_tiles_and_single_worker) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(0, 10, 4, 16);

    YR_EXPECT_TRUE(schedule.tiles.empty());
    YR_EXPECT_EQ(schedule.requested_threads, 4);
    YR_EXPECT_EQ(schedule.worker_count, 1);
}

YR_TEST(cpu_tile_scheduler_single_worker_visits_tiles_in_order) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(20, 8, 1, 8);
    std::vector<yr::CpuTile> visited;
    std::vector<int> workers;

    yr::ForEachCpuTile(schedule, [&](const yr::CpuTile& tile, int worker_index) {
        visited.push_back(tile);
        workers.push_back(worker_index);
    });

    YR_EXPECT_EQ(visited.size(), schedule.tiles.size());
    for (std::size_t i = 0; i < schedule.tiles.size(); ++i) {
        YR_EXPECT_EQ(visited[i].x0, schedule.tiles[i].x0);
        YR_EXPECT_EQ(visited[i].y0, schedule.tiles[i].y0);
        YR_EXPECT_EQ(workers[i], 0);
    }
}

YR_TEST(cpu_tile_scheduler_multi_worker_visits_each_tile_once) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(64, 64, 4, 8);
    std::vector<std::atomic<int>> visits(schedule.tiles.size());
    for (std::atomic<int>& visit : visits) {
        visit.store(0);
    }
    std::atomic<bool> worker_indexes_valid{true};
    std::atomic<bool> tile_indexes_valid{true};

    yr::ForEachCpuTile(schedule, [&](const yr::CpuTile& tile, int worker_index) {
        if (worker_index < 0 || worker_index >= schedule.worker_count) {
            worker_indexes_valid.store(false);
        }
        if (tile.x0 % 8 != 0 || tile.y0 % 8 != 0) {
            tile_indexes_valid.store(false);
            return;
        }
        const int tiles_x = 8;
        const std::size_t index = static_cast<std::size_t>((tile.y0 / 8) * tiles_x + (tile.x0 / 8));
        if (index >= visits.size()) {
            tile_indexes_valid.store(false);
            return;
        }
        visits[index].fetch_add(1);
    });

    YR_EXPECT_EQ(schedule.worker_count, 4);
    YR_EXPECT_TRUE(worker_indexes_valid.load());
    YR_EXPECT_TRUE(tile_indexes_valid.load());
    for (const std::atomic<int>& visit : visits) {
        YR_EXPECT_EQ(visit.load(), 1);
    }
}
