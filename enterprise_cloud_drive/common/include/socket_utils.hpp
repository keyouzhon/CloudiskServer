#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace cloud::net {

inline bool send_all(int fd, const void* buffer, size_t length) {
    const auto* data = static_cast<const std::byte*>(buffer);
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = ::send(fd, data + total_sent, length - total_sent, 0);
        if (sent <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_sent += static_cast<size_t>(sent);
    }
    return true;
}

inline bool recv_all(int fd, void* buffer, size_t length) {
    auto* data = static_cast<std::byte*>(buffer);
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t received = ::recv(fd, data + total_received, length - total_received, 0);
        if (received <= 0) {
            if (received == 0) {
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_received += static_cast<size_t>(received);
    }
    return true;
}

inline bool send_line(int fd, std::string_view line) {
    std::string payload(line);
    payload.push_back('\n');
    return send_all(fd, payload.data(), payload.size());
}

inline bool recv_line(int fd, std::string& out) {
    out.clear();
    char ch;
    while (true) {
        ssize_t received = ::recv(fd, &ch, 1, 0);
        if (received <= 0) {
            if (received == 0) {
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ch == '\n') {
            while (!out.empty() && (out.back() == '\r')) {
                out.pop_back();
            }
            return true;
        }
        out.push_back(ch);
        if (out.size() > 65536) {
            throw std::runtime_error("Incoming line exceeds 64KB limit");
        }
    }
}

inline void set_socket_keepalive(int fd) {
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
}

}  // namespace cloud::net
