#include "client_app.hpp"

#include "socket_utils.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/md5.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace cloud::client {

namespace {
constexpr std::size_t kChunkBytes = 1 * 1024 * 1024;
constexpr std::size_t kMmapThreshold = 100ULL * 1024 * 1024;

std::string compute_md5(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open file for hashing");
    }
    MD5_CTX ctx;
    MD5_Init(&ctx);
    std::vector<char> buffer(kChunkBytes);
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = stream.gcount();
        if (read > 0) {
            MD5_Update(&ctx, buffer.data(), static_cast<size_t>(read));
        }
    }
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    std::ostringstream oss;
    oss << std::hex;
    for (unsigned char byte : digest) {
        oss << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<std::byte> slice_from_mmap(const std::byte* base, std::size_t offset, std::size_t length) {
    return std::vector<std::byte>(base + offset, base + offset + length);
}

std::string bytes_to_string(const std::vector<std::byte>& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

bool write_chunk(const std::filesystem::path& path, std::uint64_t offset, std::span<const std::byte> data) {
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        return false;
    }
    const ssize_t written =
        ::pwrite(fd, data.data(), static_cast<size_t>(data.size()), static_cast<off_t>(offset));
    ::close(fd);
    return written == static_cast<ssize_t>(data.size());
}

}  // namespace

ClientApp::ClientApp() = default;

ClientApp::~ClientApp() {
    close_connection();
}

bool ClientApp::connect_to_server(const std::string& host, uint16_t port) {
    close_connection();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        std::cerr << "Unable to resolve host" << std::endl;
        return false;
    }

    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        socket_fd_ = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (socket_fd_ < 0) {
            continue;
        }
        if (::connect(socket_fd_, ptr->ai_addr, ptr->ai_addrlen) == 0) {
            break;
        }
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    freeaddrinfo(result);

    if (socket_fd_ < 0) {
        std::cerr << "Unable to connect to server" << std::endl;
        return false;
    }

    inbound_.clear();
    inbound_offset_ = 0;
    token_.clear();
    remote_cwd_ = ".";
    return true;
}

void ClientApp::close_connection() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool ClientApp::send_message(const protocol::Message& message) {
    if (socket_fd_ < 0) {
        return false;
    }
    const auto buffer = protocol::encode(message);
    return cloud::net::send_all(socket_fd_, buffer.data(), buffer.size());
}

bool ClientApp::read_message(protocol::Message& message) {
    std::array<std::byte, 64 * 1024> buffer{};
    while (true) {
        if (protocol::try_decode(inbound_, inbound_offset_, message)) {
            return true;
        }
        const ssize_t received = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            return false;
        }
        inbound_.insert(inbound_.end(), buffer.begin(), buffer.begin() + received);
    }
}

std::optional<protocol::Message> ClientApp::call(protocol::Message message) {
    if (socket_fd_ < 0) {
        std::cerr << "Not connected" << std::endl;
        return std::nullopt;
    }
    if (!token_.empty()) {
        message.headers.emplace("token", token_);
    }
    if (!send_message(message)) {
        return std::nullopt;
    }
    protocol::Message response;
    if (!read_message(response)) {
        return std::nullopt;
    }
    return response;
}

bool ClientApp::ensure_logged_in() {
    if (!token_.empty()) {
        return true;
    }
    std::cout << "Please login first." << std::endl;
    return false;
}

bool ClientApp::handle_upload(const std::filesystem::path& local_path, const std::filesystem::path& remote_path) {
    if (!ensure_logged_in()) {
        return false;
    }
    if (!std::filesystem::exists(local_path)) {
        std::cerr << "Local file not found: " << local_path << std::endl;
        return false;
    }
    const auto size = std::filesystem::file_size(local_path);
    std::cout << "Computing MD5..." << std::endl;
    const auto md5 = compute_md5(local_path);

    protocol::Message init;
    init.headers.emplace("cmd", "FILE_UPLOAD_INIT");
    init.headers.emplace("path", remote_path.generic_string());
    init.headers.emplace("size", std::to_string(size));
    init.headers.emplace("md5", md5);

    auto init_resp = call(std::move(init));
    if (!init_resp) {
        std::cerr << "Failed to initialize upload" << std::endl;
        return false;
    }
    const auto status = protocol::header_value(*init_resp, "status");
    if (status == "instant") {
        std::cout << "Instant transfer succeeded (server already has the file)." << std::endl;
        return true;
    }
    if (status != "ready") {
        std::cerr << "Upload init failed: " << bytes_to_string(init_resp->body) << std::endl;
        return false;
    }
    std::uint64_t offset = 0;
    const auto offset_view = protocol::header_value(*init_resp, "offset");
    if (!offset_view.empty()) {
        offset = std::stoull(std::string(offset_view));
    }

    const bool use_mmap = size >= kMmapThreshold;
    const int fd = use_mmap ? ::open(local_path.c_str(), O_RDONLY) : -1;
    void* mapped = MAP_FAILED;
    if (use_mmap && fd >= 0) {
        mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    std::ifstream stream;
    if (!use_mmap) {
        stream.open(local_path, std::ios::binary);
    }

    while (offset < size) {
        const auto chunk_size = std::min<std::uint64_t>(kChunkBytes, size - offset);
        protocol::Message chunk_msg;
        chunk_msg.headers.emplace("cmd", "FILE_UPLOAD_CHUNK");
        chunk_msg.headers.emplace("offset", std::to_string(offset));

        if (use_mmap && mapped != MAP_FAILED) {
            chunk_msg.body = slice_from_mmap(static_cast<std::byte*>(mapped), offset, chunk_size);
        } else {
            std::vector<std::byte> buffer(chunk_size);
            stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(chunk_size));
            chunk_msg.body = std::move(buffer);
        }

        auto resp = call(std::move(chunk_msg));
        if (!resp || protocol::header_value(*resp, "status") != "ok") {
            std::cerr << "Failed to upload chunk at offset " << offset << std::endl;
            if (use_mmap && mapped != MAP_FAILED) {
                ::munmap(mapped, size);
                ::close(fd);
            }
            return false;
        }
        offset += chunk_size;
        std::cout << "\rUploaded " << offset << "/" << size << std::flush;
    }
    std::cout << std::endl;

    if (use_mmap && mapped != MAP_FAILED) {
        ::munmap(mapped, size);
        ::close(fd);
    }

    protocol::Message commit;
    commit.headers.emplace("cmd", "FILE_UPLOAD_COMMIT");
    auto final_resp = call(std::move(commit));
    if (!final_resp || protocol::header_value(*final_resp, "status") != "ok") {
        std::cerr << "Upload commit failed" << std::endl;
        return false;
    }
    std::cout << "Upload completed server path: "
              << protocol::header_value(*final_resp, "path", remote_path.generic_string()) << std::endl;
    return true;
}

bool ClientApp::handle_download(const std::filesystem::path& remote_path, const std::filesystem::path& local_path) {
    if (!ensure_logged_in()) {
        return false;
    }
    protocol::Message init;
    init.headers.emplace("cmd", "FILE_DOWNLOAD_INIT");
    init.headers.emplace("path", remote_path.generic_string());

    auto resp = call(std::move(init));
    if (!resp || protocol::header_value(*resp, "status") != "ok") {
        std::cerr << "Download init failed" << std::endl;
        return false;
    }
    const auto total_size = std::stoull(std::string(protocol::header_value(*resp, "size", "0")));

    std::uint64_t local_offset = 0;
    if (std::filesystem::exists(local_path)) {
        local_offset = std::filesystem::file_size(local_path);
        if (local_offset > total_size) {
            local_offset = 0;
        }
    }
    const int fd = ::open(local_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        std::cerr << "Unable to prepare local file: " << local_path << std::endl;
        return false;
    }
    ::ftruncate(fd, static_cast<off_t>(total_size));

    while (local_offset < total_size) {
        protocol::Message chunk_req;
        chunk_req.headers.emplace("cmd", "FILE_DOWNLOAD_FETCH");
        chunk_req.headers.emplace("path", remote_path.generic_string());
        chunk_req.headers.emplace("offset", std::to_string(local_offset));
        chunk_req.headers.emplace(
            "length",
            std::to_string(std::min<std::uint64_t>(kChunkBytes, total_size - local_offset)));

        auto chunk_resp = call(std::move(chunk_req));
        if (!chunk_resp) {
            ::close(fd);
            return false;
        }
        if (protocol::header_value(*chunk_resp, "status") == "done") {
            break;
        }
        if (!write_chunk(local_path, local_offset, chunk_resp->body)) {
            std::cerr << "Failed to write downloaded chunk" << std::endl;
            ::close(fd);
            return false;
        }
        local_offset += chunk_resp->body.size();
        std::cout << "\rDownloaded " << local_offset << "/" << total_size << std::flush;
    }
    std::cout << std::endl;
    ::close(fd);
    return true;
}

void ClientApp::run_shell() {
    if (socket_fd_ < 0) {
        std::cerr << "Connect to server first" << std::endl;
        return;
    }

    std::cout << "Type 'help' for available commands." << std::endl;
    std::string input;
    while (true) {
        std::cout << "(" << remote_cwd_ << ")> " << std::flush;
        if (!std::getline(std::cin, input)) {
            break;
        }
        if (input.empty()) {
            continue;
        }
        std::istringstream iss(input);
        std::string command;
        iss >> command;
        std::string cmd_lower = command;
        std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (cmd_lower == "help") {
            std::cout << "Commands:\n"
                      << "  register <username> <password>\n"
                      << "  login <username> <password>\n"
                      << "  ls [path]\n"
                      << "  pwd\n"
                      << "  cd <path>\n"
                      << "  mkdir <path>\n"
                      << "  upload <local> [remote]\n"
                      << "  download <remote> <local>\n"
                      << "  delete <remote>\n"
                      << "  logout\n"
                      << "  quit" << std::endl;
            continue;
        }

        if (cmd_lower == "quit") {
            close_connection();
            break;
        }

        if (cmd_lower == "register") {
            std::string username, password;
            iss >> username >> password;
            if (username.empty() || password.empty()) {
                std::cout << "Usage: register <username> <password>" << std::endl;
                continue;
            }
            protocol::Message msg;
            msg.headers.emplace("cmd", "REGISTER");
            msg.headers.emplace("username", username);
            msg.headers.emplace("password", password);
            auto resp = call(std::move(msg));
            if (!resp) {
                std::cout << "Connection lost." << std::endl;
                break;
            }
            std::cout << "register: " << protocol::header_value(*resp, "status", "error") << std::endl;
            continue;
        }

        if (cmd_lower == "login") {
            std::string username, password;
            iss >> username >> password;
            if (username.empty() || password.empty()) {
                std::cout << "Usage: login <username> <password>" << std::endl;
                continue;
            }
            protocol::Message msg;
            msg.headers.emplace("cmd", "LOGIN");
            msg.headers.emplace("username", username);
            msg.headers.emplace("password", password);
            auto resp = call(std::move(msg));
            if (!resp) {
                std::cout << "Connection lost." << std::endl;
                break;
            }
            if (protocol::header_value(*resp, "status") == "ok") {
                token_ = std::string(protocol::header_value(*resp, "token"));
                remote_cwd_ = ".";
                std::cout << "Login successful. Token issued." << std::endl;
            } else {
                std::cout << "Login failed." << std::endl;
            }
            continue;
        }

        if (!ensure_logged_in()) {
            continue;
        }

        if (cmd_lower == "pwd") {
            protocol::Message msg;
            msg.headers.emplace("cmd", "DIR_PWD");
            auto resp = call(std::move(msg));
            if (!resp) {
                std::cout << "Connection lost." << std::endl;
                break;
            }
            std::cout << protocol::header_value(*resp, "path", remote_cwd_) << std::endl;
            continue;
        }

        if (cmd_lower == "ls") {
            std::string path;
            iss >> path;
            protocol::Message msg;
            msg.headers.emplace("cmd", "DIR_LIST");
            if (!path.empty()) {
                msg.headers.emplace("path", path);
            }
            auto resp = call(std::move(msg));
            if (!resp || protocol::header_value(*resp, "status") != "ok") {
                std::cout << "List failed" << std::endl;
                continue;
            }
            std::cout << bytes_to_string(resp->body);
            continue;
        }

        if (cmd_lower == "cd") {
            std::string path;
            iss >> path;
            if (path.empty()) {
                std::cout << "Usage: cd <path>" << std::endl;
                continue;
            }
            protocol::Message msg;
            msg.headers.emplace("cmd", "DIR_CHANGE");
            msg.headers.emplace("path", path);
            auto resp = call(std::move(msg));
            if (!resp) {
                std::cout << "Connection lost." << std::endl;
                break;
            }
            if (protocol::header_value(*resp, "status") == "ok") {
                remote_cwd_ = std::string(protocol::header_value(*resp, "path"));
            } else {
                std::cout << "Failed to change directory" << std::endl;
            }
            continue;
        }

        if (cmd_lower == "mkdir") {
            std::string path;
            iss >> path;
            if (path.empty()) {
                std::cout << "Usage: mkdir <path>" << std::endl;
                continue;
            }
            protocol::Message msg;
            msg.headers.emplace("cmd", "DIR_MKDIR");
            msg.headers.emplace("path", path);
            auto resp = call(std::move(msg));
            if (!resp) {
                std::cout << "Connection lost." << std::endl;
                break;
            }
            std::cout << "mkdir: " << protocol::header_value(*resp, "status", "error") << std::endl;
            continue;
        }

        if (cmd_lower == "upload") {
            std::string local, remote;
            iss >> local >> remote;
            if (local.empty()) {
                std::cout << "Usage: upload <local> [remote]" << std::endl;
                continue;
            }
            if (remote.empty()) {
                remote = std::filesystem::path(local).filename().string();
            }
            handle_upload(local, remote);
            continue;
        }

        if (cmd_lower == "download") {
            std::string remote, local;
            iss >> remote >> local;
            if (remote.empty() || local.empty()) {
                std::cout << "Usage: download <remote> <local>" << std::endl;
                continue;
            }
            handle_download(remote, local);
            continue;
        }

        if (cmd_lower == "delete") {
            std::string path;
            iss >> path;
            if (path.empty()) {
                std::cout << "Usage: delete <path>" << std::endl;
                continue;
            }
            protocol::Message msg;
            msg.headers.emplace("cmd", "FILE_DELETE");
            msg.headers.emplace("path", path);
            auto resp = call(std::move(msg));
            if (!resp) {
                std::cout << "Connection lost." << std::endl;
                break;
            }
            std::cout << "delete: " << protocol::header_value(*resp, "status", "error") << std::endl;
            continue;
        }

        if (cmd_lower == "logout") {
            token_.clear();
            remote_cwd_ = ".";
            std::cout << "Cleared local token." << std::endl;
            continue;
        }

        std::cout << "Unknown command. Type 'help'." << std::endl;
    }
}

}  // namespace cloud::client
