#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cloud::server {

class TaskExecutor {
public:
    TaskExecutor();
    explicit TaskExecutor(std::size_t worker_count);
    ~TaskExecutor();

    void start(std::size_t worker_count);
    void submit(std::function<void()> task);
    void shutdown();

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_{false};
};

}  // namespace cloud::server


