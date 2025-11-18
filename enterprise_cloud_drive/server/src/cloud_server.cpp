#include "cloud_server.hpp"

#include "auth_service.hpp"
#include "socket_utils.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cloud::server {

namespace {

constexpr int kMaxEvents = 128;

int set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

std::vector<std::byte> to_bytes(const std::string& text) {
    return std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                  reinterpret_cast<const std::byte*>(text.data() + text.size()));
}

std::string bytes_to_string(const std::vector<std::byte>& buffer) {
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

std::string normalize_relative(const std::filesystem::path& path) {
    auto normalized = std::filesystem::path{};
    for (const auto& part : path) {
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            if (!normalized.empty()) {
                normalized = normalized.parent_path();
            }
            continue;
        }
        normalized /= part;
    }
    return normalized.empty() ? std::string(".") : normalized.generic_string();
}

}  // namespace

struct CloudServer::ConnectionContext {
    int fd;
    std::string peer;
    std::vector<std::byte> inbound;
    std::size_t inbound_offset = 0;
    std::vector<std::byte> outbound;

    std::string username;
    std::string token;
    std::filesystem::path cwd{"."};

    bool upload_active = false;
    UploadCheckpoint upload_checkpoint;
    std::uint64_t upload_expected = 0;
    std::string upload_md5;
    std::filesystem::path upload_logical;
};

CloudServer::CloudServer(ServerConfig config,
                         AuthService& auth_service,
                         StorageManager& storage_manager,
                         FileIndex& file_index,
                         JwtService& jwt_service,
                         Logger& logger)
    : config_(std::move(config)),
      auth_service_(auth_service),
      storage_manager_(storage_manager),
      file_index_(file_index),
      jwt_service_(jwt_service),
      logger_(logger) {}

CloudServer::~CloudServer() {
    stop();
}

void CloudServer::start() {
    if (running_) {
        return;
    }

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.listen_port);
    addr.sin_addr.s_addr = inet_addr(config_.listen_address.c_str());
    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("Failed to bind server socket");
    }
    if (::listen(server_fd_, static_cast<int>(config_.max_clients)) < 0) {
        throw std::runtime_error("Failed to listen on server socket");
    }
    set_non_blocking(server_fd_);

    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create epoll");
    }
    notify_fd_ = ::eventfd(0, EFD_NONBLOCK);
    if (notify_fd_ < 0) {
        throw std::runtime_error("Failed to create eventfd");
    }

    epoll_event server_event{};
    server_event.data.fd = server_fd_;
    server_event.events = EPOLLIN;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &server_event);

    epoll_event notify_event{};
    notify_event.data.fd = notify_fd_;
    notify_event.events = EPOLLIN;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, notify_fd_, &notify_event);

    task_executor_.start(config_.long_task_threads);

    running_ = true;
    reactor_thread_ = std::thread(&CloudServer::reactor_loop, this);
    logger_.info("Reactor listening on " + config_.listen_address + ":" + std::to_string(config_.listen_port));
}

void CloudServer::stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    if (notify_fd_ >= 0) {
        uint64_t value = 1;
        ::write(notify_fd_, &value, sizeof(value));
    }

    if (reactor_thread_.joinable()) {
        reactor_thread_.join();
    }

    for (auto& [fd, ctx] : connections_) {
        ::close(fd);
    }
    connections_.clear();
    ready_queue_.clear();

    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (notify_fd_ >= 0) {
        ::close(notify_fd_);
        notify_fd_ = -1;
    }

    task_executor_.shutdown();
}

void CloudServer::reactor_loop() {
    std::array<epoll_event, kMaxEvents> events{};

    while (running_) {
        int ready = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), 500);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            logger_.error("epoll_wait failed");
            break;
        }
        for (int i = 0; i < ready; ++i) {
            const auto& event = events[i];
            if (event.data.fd == server_fd_) {
                handle_accept();
                continue;
            }
            if (event.data.fd == notify_fd_) {
                drain_async_queue();
                uint64_t tmp;
                ::read(notify_fd_, &tmp, sizeof(tmp));
                continue;
            }
            ready_queue_.emplace_back(event.data.fd, event.events);
        }

        while (!ready_queue_.empty()) {
            auto [fd, mask] = ready_queue_.front();
            ready_queue_.pop_front();
            handle_fd_event(fd, mask);
        }
    }
}

void CloudServer::handle_accept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            logger_.warn("accept failed: " + std::string(std::strerror(errno)));
            break;
        }
        set_non_blocking(client_fd);
        cloud::net::set_socket_keepalive(client_fd);

        epoll_event event{};
        event.data.fd = client_fd;
        event.events = EPOLLIN | EPOLLRDHUP;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0) {
            ::close(client_fd);
            continue;
        }

        std::ostringstream peer;
        peer << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port);

        auto ctx = std::make_unique<ConnectionContext>();
        ctx->fd = client_fd;
        ctx->peer = peer.str();
        connections_.emplace(client_fd, std::move(ctx));
        logger_.info("Accepted connection from " + peer.str());
    }
}

void CloudServer::handle_fd_event(int fd, uint32_t events) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    auto& ctx = *it->second;

    if (events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
        close_connection(fd);
        return;
    }

    if (events & EPOLLIN) {
        std::array<std::byte, 64 * 1024> buf{};
        while (true) {
            const ssize_t received = ::recv(fd, buf.data(), buf.size(), 0);
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                close_connection(fd);
                return;
            }
            if (received == 0) {
                close_connection(fd);
                return;
            }
            ctx.inbound.insert(ctx.inbound.end(), buf.begin(), buf.begin() + received);
        }

        protocol::Message message;
        while (protocol::try_decode(ctx.inbound, ctx.inbound_offset, message)) {
            const auto cmd = protocol::header_value(message, "cmd");
            if (cmd.empty()) {
                schedule_response(fd, protocol::make_message({{"cmd", "ERROR"}, {"reason", "MissingCommand"}}));
                continue;
            }
            const std::string command(cmd);
            try {
                if (command == "REGISTER") {
                    auto username = protocol::header_value(message, "username");
                    auto password = protocol::header_value(message, "password");
                    if (username.empty() || password.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "REGISTER"}, {"status", "invalid"}}));
                        continue;
                    }
                    if (auth_service_.register_user(std::string(username), std::string(password))) {
                        schedule_response(fd, protocol::make_message({{"cmd", "REGISTER"}, {"status", "ok"}}));
                    } else {
                        schedule_response(fd, protocol::make_message({{"cmd", "REGISTER"}, {"status", "exists"}}));
                    }
                    continue;
                }
                if (command == "LOGIN") {
                    auto username = protocol::header_value(message, "username");
                    auto password = protocol::header_value(message, "password");
                    if (username.empty() || password.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "LOGIN"}, {"status", "invalid"}}));
                        continue;
                    }
                    if (auth_service_.validate_user(std::string(username), std::string(password))) {
                        auto token = jwt_service_.issue(std::string(username));
                        ctx.username = username;
                        ctx.token = token;
                        ctx.cwd = ".";
                        schedule_response(fd, protocol::make_message({{"cmd", "LOGIN"},
                                                                      {"status", "ok"},
                                                                      {"token", token},
                                                                      {"home", "."}}));
                        logger_.info("User " + std::string(username) + " logged in from " + ctx.peer);
                    } else {
                        schedule_response(fd, protocol::make_message({{"cmd", "LOGIN"}, {"status", "denied"}}));
                    }
                    continue;
                }
                if (command == "TOKEN_AUTH") {
                    auto token = protocol::header_value(message, "token");
                    if (token.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "TOKEN_AUTH"}, {"status", "missing"}}));
                        continue;
                    }
                    auto claims = jwt_service_.verify(std::string(token));
                    if (!claims) {
                        schedule_response(fd, protocol::make_message({{"cmd", "TOKEN_AUTH"}, {"status", "invalid"}}));
                        continue;
                    }
                    ctx.username = claims->subject;
                    ctx.token = std::string(token);
                    schedule_response(fd, protocol::make_message({{"cmd", "TOKEN_AUTH"}, {"status", "ok"}}));
                    continue;
                }

                auto token = protocol::header_value(message, "token");
                if (token.empty()) {
                    schedule_response(fd, protocol::make_message({{"cmd", command}, {"status", "auth_required"}}));
                    continue;
                }
                auto claims = jwt_service_.verify(std::string(token));
                if (!claims) {
                    schedule_response(fd, protocol::make_message({{"cmd", command}, {"status", "token_invalid"}}));
                    continue;
                }
                ctx.username = claims->subject;
                ctx.token = std::string(token);

                if (command == "DIR_PWD") {
                    schedule_response(fd, protocol::make_message(
                                            {{"cmd", "DIR_PWD"}, {"status", "ok"}, {"path", ctx.cwd.generic_string()}}));
                    continue;
                }
                if (command == "DIR_CHANGE") {
                    auto path = protocol::header_value(message, "path");
                    if (path.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "DIR_CHANGE"}, {"status", "invalid"}}));
                        continue;
                    }
                    try {
                        auto resolved = storage_manager_.resolve(ctx.username, ctx.cwd / std::string(path));
                        if (!std::filesystem::is_directory(resolved)) {
                            schedule_response(fd, protocol::make_message({{"cmd", "DIR_CHANGE"}, {"status", "notfound"}}));
                            continue;
                        }
                        ctx.cwd =
                            std::filesystem::relative(resolved, storage_manager_.user_root(ctx.username)).lexically_normal();
                        if (ctx.cwd.empty()) {
                            ctx.cwd = ".";
                        }
                        schedule_response(fd, protocol::make_message(
                                                {{"cmd", "DIR_CHANGE"}, {"status", "ok"}, {"path", ctx.cwd.string()}}));
                    } catch (const std::exception& ex) {
                        schedule_response(fd, protocol::make_message({{"cmd", "DIR_CHANGE"}, {"status", ex.what()}}));
                    }
                    continue;
                }
                if (command == "DIR_MKDIR") {
                    auto path = protocol::header_value(message, "path");
                    if (path.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "DIR_MKDIR"}, {"status", "invalid"}}));
                        continue;
                    }
                    if (storage_manager_.ensure_directory(ctx.username, ctx.cwd / std::string(path))) {
                        schedule_response(fd, protocol::make_message({{"cmd", "DIR_MKDIR"}, {"status", "ok"}}));
                    } else {
                        schedule_response(fd, protocol::make_message({{"cmd", "DIR_MKDIR"}, {"status", "failed"}}));
                    }
                    continue;
                }
                if (command == "DIR_LIST") {
                    auto path = protocol::header_value(message, "path");
                    auto target = ctx.cwd;
                    if (!path.empty()) {
                        target /= std::string(path);
                    }
                    try {
                        auto entries = storage_manager_.list(ctx.username, target);
                        std::ostringstream body;
                        for (const auto& entry : entries) {
                            body << entry.name << "|" << (entry.is_directory ? "dir" : "file") << "|" << entry.size << "|"
                                 << entry.modified << "\n";
                        }
                        protocol::Message resp;
                        resp.headers.emplace("cmd", "DIR_LIST");
                        resp.headers.emplace("status", "ok");
                        resp.headers.emplace("count", std::to_string(entries.size()));
                        resp.body = to_bytes(body.str());
                        schedule_response(fd, std::move(resp));
                    } catch (const std::exception& ex) {
                        schedule_response(fd, protocol::make_message({{"cmd", "DIR_LIST"}, {"status", ex.what()}}));
                    }
                    continue;
                }
                if (command == "FILE_DELETE") {
                    auto path = protocol::header_value(message, "path");
                    if (path.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DELETE"}, {"status", "invalid"}}));
                        continue;
                    }
                    if (storage_manager_.remove(ctx.username, ctx.cwd / std::string(path))) {
                        file_index_.remove(ctx.username, normalize_relative(ctx.cwd / std::string(path)));
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DELETE"}, {"status", "ok"}}));
                    } else {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DELETE"}, {"status", "notfound"}}));
                    }
                    continue;
                }
                if (command == "FILE_UPLOAD_INIT") {
                    auto path = protocol::header_value(message, "path");
                    auto md5 = protocol::header_value(message, "md5");
                    auto size = protocol::header_value(message, "size");
                    if (path.empty() || md5.empty() || size.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_INIT"}, {"status", "invalid"}}));
                        continue;
                    }
                    const auto logical = normalize_relative(ctx.cwd / std::string(path));
                    const auto absolute = storage_manager_.resolve(ctx.username, std::filesystem::path(logical));

                    auto instant = file_index_.find_by_md5(std::string(md5));
                    if (instant && std::filesystem::exists(instant->storage_path)) {
                        std::filesystem::create_directories(absolute.parent_path());
                        std::filesystem::copy_file(instant->storage_path, absolute,
                                                   std::filesystem::copy_options::overwrite_existing);
                        file_index_.upsert(FileMetadata{ctx.username, logical, std::string(md5),
                                                        absolute.string(), instant->size});
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_INIT"},
                                                                      {"status", "instant"},
                                                                      {"path", logical}}));
                        continue;
                    }

                    auto checkpoint =
                        storage_manager_.prepare_upload(ctx.username, std::string(md5), std::filesystem::path(logical),
                                                        static_cast<std::uint64_t>(std::stoull(std::string(size))));
                    ctx.upload_active = true;
                    ctx.upload_checkpoint = checkpoint;
                    ctx.upload_expected = checkpoint.total;
                    ctx.upload_md5 = std::string(md5);
                    ctx.upload_logical = std::filesystem::path(logical);

                    schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_INIT"},
                                                                  {"status", "ready"},
                                                                  {"offset", std::to_string(checkpoint.received)}}));
                    continue;
                }
                if (command == "FILE_UPLOAD_CHUNK") {
                    if (!ctx.upload_active) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_CHUNK"}, {"status", "no_session"}}));
                        continue;
                    }
                    auto offset = protocol::header_value(message, "offset");
                    if (offset.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_CHUNK"}, {"status", "invalid"}}));
                        continue;
                    }
                    const std::uint64_t off = std::stoull(std::string(offset));
                    if (off != ctx.upload_checkpoint.received) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_CHUNK"}, {"status", "offset"}}));
                        continue;
                    }
                    if (!storage_manager_.write_chunk(ctx.upload_checkpoint, off, message.body)) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_CHUNK"}, {"status", "io_error"}}));
                        continue;
                    }
                    ctx.upload_checkpoint.received += message.body.size();
                    storage_manager_.update_progress(ctx.upload_checkpoint, ctx.upload_checkpoint.received);
                    schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_CHUNK"},
                                                                  {"status", "ok"},
                                                                  {"received", std::to_string(ctx.upload_checkpoint.received)}}));
                    continue;
                }
                if (command == "FILE_UPLOAD_COMMIT") {
                    if (!ctx.upload_active || ctx.upload_checkpoint.received != ctx.upload_expected) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_UPLOAD_COMMIT"},
                                                                      {"status", "incomplete"}}));
                        continue;
                    }
                    ctx.upload_active = false;
                    auto checkpoint = ctx.upload_checkpoint;
                    auto md5 = ctx.upload_md5;
                    auto logical = ctx.upload_logical;
                    auto username = ctx.username;
                    auto fd_copy = fd;

                    task_executor_.submit([this, checkpoint, md5, logical, username, fd_copy]() {
                        protocol::Message response;
                        response.headers.emplace("cmd", "FILE_UPLOAD_COMMIT");
                        try {
                            auto final_path = storage_manager_.finalize_upload(checkpoint);
                            auto actual_md5 = storage_manager_.compute_md5(final_path);
                            if (actual_md5 != md5) {
                                storage_manager_.discard_checkpoint(checkpoint);
                                response.headers.emplace("status", "md5_mismatch");
                            } else {
                                file_index_.upsert(FileMetadata{username, logical, actual_md5,
                                                                final_path.string(),
                                                                checkpoint.total});
                                response.headers.emplace("status", "ok");
                                response.headers.emplace("path", logical.string());
                            }
                        } catch (const std::exception& ex) {
                            response.headers.emplace("status", ex.what());
                        }
                        schedule_response(fd_copy, std::move(response));
                    });
                    continue;
                }
                if (command == "FILE_DOWNLOAD_INIT") {
                    auto path = protocol::header_value(message, "path");
                    if (path.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DOWNLOAD_INIT"},
                                                                      {"status", "invalid"}}));
                        continue;
                    }
                    auto logical = normalize_relative(ctx.cwd / std::string(path));
                    const auto absolute = storage_manager_.resolve(ctx.username, std::filesystem::path(logical));
                    if (!std::filesystem::exists(absolute)) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DOWNLOAD_INIT"},
                                                                      {"status", "notfound"}}));
                        continue;
                    }
                    auto meta = file_index_.find_by_path(ctx.username, logical);
                    std::string md5 = meta ? meta->md5 : storage_manager_.compute_md5(absolute);
                    protocol::Message resp;
                    resp.headers.emplace("cmd", "FILE_DOWNLOAD_INIT");
                    resp.headers.emplace("status", "ok");
                    resp.headers.emplace("size", std::to_string(storage_manager_.file_size(absolute)));
                    resp.headers.emplace("md5", md5);
                    resp.headers.emplace("path", logical);
                    schedule_response(fd, std::move(resp));
                    continue;
                }
                if (command == "FILE_DOWNLOAD_FETCH") {
                    auto path = protocol::header_value(message, "path");
                    auto offset = protocol::header_value(message, "offset");
                    auto length = protocol::header_value(message, "length");
                    if (path.empty() || offset.empty() || length.empty()) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DOWNLOAD_FETCH"},
                                                                      {"status", "invalid"}}));
                        continue;
                    }
                    auto logical = normalize_relative(ctx.cwd / std::string(path));
                    const auto absolute = storage_manager_.resolve(ctx.username, std::filesystem::path(logical));
                    if (!std::filesystem::exists(absolute)) {
                        schedule_response(fd, protocol::make_message({{"cmd", "FILE_DOWNLOAD_FETCH"},
                                                                      {"status", "notfound"}}));
                        continue;
                    }
                    const auto requested = static_cast<std::size_t>(std::stoul(std::string(length)));
                    const auto chunk_size = std::min<std::size_t>(requested, config_.max_chunk_bytes);
                    auto chunk = storage_manager_.read_chunk(absolute,
                                                             std::stoull(std::string(offset)),
                                                             chunk_size);
                    protocol::Message resp;
                    resp.headers.emplace("cmd", "FILE_DOWNLOAD_FETCH");
                    resp.headers.emplace("status", chunk.empty() ? "done" : "ok");
                    resp.headers.emplace("chunk", std::to_string(chunk.size()));
                    resp.body = std::move(chunk);
                    schedule_response(fd, std::move(resp));
                    continue;
                }

                schedule_response(fd, protocol::make_message({{"cmd", command}, {"status", "unknown"}}));
            } catch (const std::exception& ex) {
                schedule_response(fd, protocol::make_message({{"cmd", std::string(cmd)},
                                                              {"status", "error"},
                                                              {"reason", ex.what()}}));
            }
        }
    }

    if (events & EPOLLOUT) {
        while (!ctx.outbound.empty()) {
            const ssize_t sent = ::send(fd, ctx.outbound.data(), ctx.outbound.size(), 0);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                close_connection(fd);
                return;
            }
            ctx.outbound.erase(ctx.outbound.begin(), ctx.outbound.begin() + sent);
        }
        if (ctx.outbound.empty()) {
            epoll_event ev{};
            ev.data.fd = fd;
            ev.events = EPOLLIN | EPOLLRDHUP;
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
        }
    }
}

void CloudServer::schedule_response(int fd, protocol::Message message) {
    std::lock_guard<std::mutex> lock(async_mutex_);
    async_responses_.push_back(PendingResponse{fd, std::move(message)});
    uint64_t value = 1;
    ::write(notify_fd_, &value, sizeof(value));
}

void CloudServer::drain_async_queue() {
    std::vector<PendingResponse> pending;
    {
        std::lock_guard<std::mutex> lock(async_mutex_);
        pending.swap(async_responses_);
    }
    for (auto& resp : pending) {
        auto it = connections_.find(resp.fd);
        if (it == connections_.end()) {
            continue;
        }
        auto encoded = protocol::encode(resp.message);
        auto& buffer = it->second->outbound;
        buffer.insert(buffer.end(), encoded.begin(), encoded.end());

        epoll_event ev{};
        ev.data.fd = resp.fd;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, resp.fd, &ev);
    }
}

void CloudServer::close_connection(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    connections_.erase(fd);
}

}  // namespace cloud::server
