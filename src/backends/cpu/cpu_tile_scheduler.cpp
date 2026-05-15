#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

namespace yr {
namespace {

int ResolveAutoWorkerCount() {
    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads <= 1) {
        return 1;
    }
    return static_cast<int>(std::min<unsigned int>(
        hardware_threads - 1,
        static_cast<unsigned int>(std::numeric_limits<int>::max())
    ));
}

int ResolveWorkerCount(int requested_threads, std::size_t tile_count) {
    if (tile_count == 0) {
        return 1;
    }

    const int max_workers = static_cast<int>(std::min<std::size_t>(
        tile_count,
        static_cast<std::size_t>(std::numeric_limits<int>::max())
    ));
    const int resolved = requested_threads == 0 ? ResolveAutoWorkerCount() : requested_threads;
    return std::clamp(resolved, 1, max_workers);
}

int ClampedWorkerCount(const CpuTileSchedule& schedule) {
    if (schedule.tiles.empty()) {
        return 1;
    }
    return std::clamp(schedule.worker_count, 1, static_cast<int>(schedule.tiles.size()));
}

} // namespace

CpuTileSchedule BuildCpuTileSchedule(int width, int height, int requested_threads, int tile_size) {
    CpuTileSchedule schedule;
    schedule.requested_threads = requested_threads;
    schedule.tile_size = std::max(1, tile_size);

    if (width <= 0 || height <= 0) {
        schedule.worker_count = 1;
        return schedule;
    }

    for (int y = 0; y < height; y += schedule.tile_size) {
        for (int x = 0; x < width; x += schedule.tile_size) {
            schedule.tiles.push_back(CpuTile{
                x,
                y,
                std::min(x + schedule.tile_size, width),
                std::min(y + schedule.tile_size, height)
            });
        }
    }

    schedule.worker_count = ResolveWorkerCount(requested_threads, schedule.tiles.size());
    return schedule;
}

void ForEachCpuTile(const CpuTileSchedule& schedule, const CpuTileCallback& callback) {
    if (schedule.tiles.empty()) {
        return;
    }

    const int worker_count = ClampedWorkerCount(schedule);
    if (worker_count == 1) {
        for (const CpuTile& tile : schedule.tiles) {
            callback(tile, 0);
        }
        return;
    }

    std::atomic<std::size_t> next_tile{0};
    std::atomic<bool> cancelled{false};
    std::exception_ptr first_exception;
    std::mutex exception_mutex;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));

    for (int worker_index = 0; worker_index < worker_count; ++worker_index) {
        workers.emplace_back([&, worker_index]() {
            while (!cancelled.load()) {
                const std::size_t tile_index = next_tile.fetch_add(1);
                if (tile_index >= schedule.tiles.size()) {
                    break;
                }

                try {
                    callback(schedule.tiles[tile_index], worker_index);
                } catch (...) {
                    {
                        std::lock_guard<std::mutex> lock{exception_mutex};
                        if (first_exception == nullptr) {
                            first_exception = std::current_exception();
                        }
                    }
                    cancelled.store(true);
                    break;
                }
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    if (first_exception != nullptr) {
        std::rethrow_exception(first_exception);
    }
}

} // namespace yr
