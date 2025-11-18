#include "auth_service.hpp"
#include "cloud_server.hpp"
#include "config_loader.hpp"
#include "file_index.hpp"
#include "jwt_service.hpp"
#include "logger.hpp"
#include "storage_manager.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_should_run{true};

void handle_signal(int) {
    g_should_run = false;
}
}  // namespace

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : "server/config/server.conf";

    try {
        auto config = cloud::server::load_config(config_path);
        cloud::server::Logger logger(config.log_file);
        cloud::server::AuthService auth(config.database_file);
        auth.initialize_schema();
        cloud::server::FileIndex file_index(config.database_file);
        file_index.initialize_schema();
        cloud::server::JwtService jwt({.issuer = config.jwt_issuer,
                                       .secret = config.jwt_secret,
                                       .ttl_seconds = config.token_ttl_seconds});
        cloud::server::StorageManager storage(config.storage_root);

        cloud::server::CloudServer server(config, auth, storage, file_index, jwt, logger);
        server.start();

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        std::cout << "Cloud drive server started. Press Ctrl+C to stop." << std::endl;
        while (g_should_run.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "Stopping server..." << std::endl;
        server.stop();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
