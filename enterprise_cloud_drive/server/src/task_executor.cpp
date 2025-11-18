#include "task_executor.hpp"

#include <stdexcept>

namespace cloud::server {

TaskExecutor::TaskExecutor() = default;

TaskExecutor::TaskExecutor(std::size_t worker_count) {
    start(worker_count);
}

TaskExecutor::~TaskExecutor() {
    shutdown();
}

void TaskExecutor::start(std::size_t worker_count) {
    if (!workers_.empty()) {
        return;
    }
    if (worker_count == 0) {
        throw std::invalid_argument("worker_count must be > 0");
    }
    stopping_ = false;
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&TaskExecutor::worker_loop, this);
    }
}

void TaskExecutor::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void TaskExecutor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<std::function<void()>> empty;
        std::swap(tasks_, empty);
    }
}

void TaskExecutor::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (...) {
            // Swallow exceptions to keep workers alive.
        }
    }
}

}  // namespace cloud::server


