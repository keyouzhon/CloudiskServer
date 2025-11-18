#pragma once

#include "auth_service.hpp"
#include "config_loader.hpp"
#include "file_index.hpp"
#include "jwt_service.hpp"
#include "logger.hpp"
#include "protocol.hpp"
#include "storage_manager.hpp"
#include "task_executor.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <thread>

namespace cloud::server {

class CloudServer {
public:
    CloudServer(ServerConfig config,
                AuthService& auth_service,
                StorageManager& storage_manager,
                FileIndex& file_index,
                JwtService& jwt_service,
                Logger& logger);
    ~CloudServer();

    void start();
    void stop();

private:
    struct ConnectionContext;
    struct PendingResponse {
        int fd;
        protocol::Message message;
    };

    void reactor_loop();
    void handle_accept();
    void handle_fd_event(int fd, uint32_t events);
    void drain_async_queue();
    void schedule_response(int fd, protocol::Message message);
    void close_connection(int fd);

    ServerConfig config_;
    AuthService& auth_service_;
    StorageManager& storage_manager_;
    FileIndex& file_index_;
    JwtService& jwt_service_;
    Logger& logger_;

    int server_fd_ = -1;
    int epoll_fd_ = -1;
    int notify_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread reactor_thread_;
    TaskExecutor task_executor_;

    std::unordered_map<int, std::unique_ptr<ConnectionContext>> connections_;
    std::deque<std::pair<int, uint32_t>> ready_queue_;
    std::mutex async_mutex_;
    std::vector<PendingResponse> async_responses_;
};

}  // namespace cloud::server
