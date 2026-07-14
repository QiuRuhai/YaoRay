#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace yr {

class CpuWorkerPool {
public:
    using Task = std::function<void(std::size_t task_index, int worker_index)>;

    explicit CpuWorkerPool(int worker_count);
    ~CpuWorkerPool();

    CpuWorkerPool(const CpuWorkerPool&)            = delete;
    CpuWorkerPool& operator=(const CpuWorkerPool&) = delete;

    int WorkerCount() const { return static_cast<int>(workers_.size()); }

    bool Run(std::size_t task_count, std::stop_token stop_token, const Task& task);

private:
    void WorkerLoop(std::stop_token lifetime_stop, int worker_index);

    std::vector<std::jthread>   workers_;
    std::mutex                  mutex_;
    std::condition_variable_any work_available_;
    std::condition_variable     finished_;
    Task                        task_;
    std::stop_token             dispatch_stop_;
    std::exception_ptr          first_exception_;
    std::size_t                 task_count_         = 0;
    std::size_t                 next_task_          = 0;
    std::size_t                 completed_workers_  = 0;
    std::size_t                 generation_         = 0;
    bool                        dispatch_active_    = false;
    bool                        dispatch_cancelled_ = false;
    bool                        shutting_down_      = false;
};

}  // namespace yr
