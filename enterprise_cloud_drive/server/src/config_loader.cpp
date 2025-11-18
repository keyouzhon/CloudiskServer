#include "config_loader.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace cloud::server {

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}

ServerConfig load_config(const std::string& path) {
    ServerConfig config;
    std::ifstream stream(path);
    if (!stream.is_open()) {
        std::cerr << "[WARN] Unable to open config file " << path
                  << ", falling back to defaults" << std::endl;
        return config;
    }

    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals_pos));
        const std::string value = trim(line.substr(equals_pos + 1));

        if (key == "listen_address") {
            config.listen_address = value;
        } else if (key == "listen_port") {
            config.listen_port = static_cast<uint16_t>(std::stoi(value));
        } else if (key == "max_clients") {
            config.max_clients = static_cast<std::size_t>(std::stoul(value));
        } else if (key == "storage_root") {
            config.storage_root = value;
        } else if (key == "thread_pool_size") {
            config.thread_pool_size = static_cast<std::size_t>(std::stoul(value));
        } else if (key == "database_file") {
            config.database_file = value;
        } else if (key == "log_file") {
            config.log_file = value;
        } else if (key == "jwt_secret") {
            config.jwt_secret = value;
        } else if (key == "jwt_issuer") {
            config.jwt_issuer = value;
        } else if (key == "token_ttl_seconds") {
            config.token_ttl_seconds = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "max_chunk_bytes") {
            config.max_chunk_bytes = static_cast<std::size_t>(std::stoul(value));
        } else if (key == "long_task_threads") {
            config.long_task_threads = static_cast<std::size_t>(std::stoul(value));
        }
    }

    return config;
}

}  // namespace cloud::server
