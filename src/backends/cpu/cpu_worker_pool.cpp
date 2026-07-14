#include "cpu_worker_pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace yr {

CpuWorkerPool::CpuWorkerPool(int worker_count) {
    const int resolved_count = std::max(1, worker_count);
    workers_.reserve(static_cast<std::size_t>(resolved_count));
    for (int worker_index = 0; worker_index < resolved_count; ++worker_index) {
        workers_.emplace_back([this, worker_index](std::stop_token stop) {
            WorkerLoop(stop, worker_index);
        });
    }
}

CpuWorkerPool::~CpuWorkerPool() {
    {
        std::lock_guard lock{mutex_};
        shutting_down_ = true;
    }
    for (std::jthread& worker : workers_) {
        worker.request_stop();
    }
    work_available_.notify_all();
    workers_.clear();
}

bool CpuWorkerPool::Run(
    std::size_t task_count,
    std::stop_token stop_token,
    const Task& task
) {
    if (task_count == 0 || stop_token.stop_requested()) {
        return !stop_token.stop_requested();
    }

    std::unique_lock lock{mutex_};
    if (dispatch_active_) {
        throw std::logic_error("CpuWorkerPool does not support concurrent dispatches");
    }

    task_ = task;
    dispatch_stop_ = stop_token;
    first_exception_ = nullptr;
    task_count_ = task_count;
    next_task_ = 0;
    completed_workers_ = 0;
    dispatch_cancelled_ = false;
    dispatch_active_ = true;
    ++generation_;
    work_available_.notify_all();

    finished_.wait(lock, [this] {
        return completed_workers_ == workers_.size();
    });

    dispatch_active_ = false;
    task_ = {};
    const bool completed = !dispatch_cancelled_ && !stop_token.stop_requested();
    const std::exception_ptr exception = first_exception_;
    lock.unlock();
    if (exception != nullptr) {
        std::rethrow_exception(exception);
    }
    return completed;
}

void CpuWorkerPool::WorkerLoop(std::stop_token lifetime_stop, int worker_index) {
    std::size_t observed_generation = 0;
    while (!lifetime_stop.stop_requested()) {
        std::unique_lock lock{mutex_};
        work_available_.wait(lock, lifetime_stop, [this, observed_generation] {
            return shutting_down_ || generation_ != observed_generation;
        });
        if (shutting_down_ || lifetime_stop.stop_requested()) {
            return;
        }
        observed_generation = generation_;

        while (true) {
            if (dispatch_cancelled_ || dispatch_stop_.stop_requested() ||
                next_task_ >= task_count_) {
                if (dispatch_stop_.stop_requested()) {
                    dispatch_cancelled_ = true;
                }
                break;
            }
            const std::size_t task_index = next_task_++;
            const Task task = task_;
            lock.unlock();
            try {
                task(task_index, worker_index);
            } catch (...) {
                lock.lock();
                if (first_exception_ == nullptr) {
                    first_exception_ = std::current_exception();
                }
                dispatch_cancelled_ = true;
                break;
            }
            lock.lock();
        }

        ++completed_workers_;
        if (completed_workers_ == workers_.size()) {
            finished_.notify_one();
        }
    }
}

} // namespace yr
