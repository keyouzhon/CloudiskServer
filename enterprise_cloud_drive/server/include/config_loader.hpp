#pragma once

#include <cstdint>
#include <string>

namespace cloud::server {

struct ServerConfig {
    std::string listen_address = "0.0.0.0";
    uint16_t listen_port = 6000;
    std::string storage_root = "./server/storage";
    std::string database_file = "./data/cloud_drive.db";
    std::string log_file = "./data/server.log";
    std::size_t max_clients = 512;
    std::size_t thread_pool_size = 8;
    std::size_t long_task_threads = 4;
    std::size_t max_chunk_bytes = 1 * 1024 * 1024;
    std::string jwt_secret = "change-me";
    std::string jwt_issuer = "enterprise-cloud-drive";
    uint32_t token_ttl_seconds = 3600;
};

ServerConfig load_config(const std::string& path);

}  // namespace cloud::server
