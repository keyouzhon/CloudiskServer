#pragma once

#include "protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cloud::client {

class ClientApp {
public:
    ClientApp();
    ~ClientApp();

    bool connect_to_server(const std::string& host, uint16_t port);
    void run_shell();

private:
    void close_connection();
    bool send_message(const cloud::protocol::Message& message);
    bool read_message(cloud::protocol::Message& message);
    std::optional<cloud::protocol::Message> call(cloud::protocol::Message message);

    bool handle_upload(const std::filesystem::path& local_path, const std::filesystem::path& remote_path);
    bool handle_download(const std::filesystem::path& remote_path, const std::filesystem::path& local_path);
    bool ensure_logged_in();

    int socket_fd_ = -1;
    std::vector<std::byte> inbound_;
    std::size_t inbound_offset_ = 0;

    std::string token_;
    std::string remote_cwd_ = ".";
};

}  // namespace cloud::client
