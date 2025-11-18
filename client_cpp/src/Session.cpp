#include "Session.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace netdisk {

namespace {

void throwSystemError(std::string_view where) {
    throw std::runtime_error(std::string(where) + ": " + std::strerror(errno));
}

}  // namespace

Session::Session(const std::string& ip, uint16_t port)
    : ip_(ip), port_(port) {
    connectSocket();
}

Session::~Session() {
    closeSocket();
}

void Session::connectSocket() {
    closeSocket();
    sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ == -1) {
        throwSystemError("socket");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("Invalid IP address: " + ip_);
    }
    if (::connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        throwSystemError("connect");
    }
}

void Session::closeSocket() {
    if (sockfd_ != -1) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

void Session::reconnect() {
    connectSocket();
}

void Session::sendTrain(const Train& train) {
    if (sockfd_ == -1) {
        throw std::runtime_error("Session not connected");
    }
    sendAll(sockfd_, &train.Len, sizeof(train.Len));
    sendAll(sockfd_, &train.ctl_code, sizeof(train.ctl_code));
    if (train.Len > 0) {
        sendAll(sockfd_, train.buf.data(), static_cast<std::size_t>(train.Len));
    }
}

void Session::receiveTrain(Train& train) {
    if (sockfd_ == -1) {
        throw std::runtime_error("Session not connected");
    }
    recvAll(sockfd_, &train.Len, sizeof(train.Len));
    recvAll(sockfd_, &train.ctl_code, sizeof(train.ctl_code));
    if (train.Len < 0 || train.Len > static_cast<int>(kBufferSize)) {
        throw std::runtime_error("Invalid payload length from server");
    }
    if (train.Len > 0) {
        recvAll(sockfd_, train.buf.data(), static_cast<std::size_t>(train.Len));
    }
}

void Session::sendAll(int fd, const void* buf, std::size_t len) {
    const std::uint8_t* ptr = static_cast<const std::uint8_t*>(buf);
    std::size_t sent = 0;
    while (sent < len) {
        ssize_t ret = ::send(fd, ptr + sent, len - sent, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("send");
        }
        if (ret == 0) {
            throw std::runtime_error("Connection closed during send");
        }
        sent += static_cast<std::size_t>(ret);
    }
}

void Session::recvAll(int fd, void* buf, std::size_t len) {
    std::uint8_t* ptr = static_cast<std::uint8_t*>(buf);
    std::size_t recvd = 0;
    while (recvd < len) {
        ssize_t ret = ::recv(fd, ptr + recvd, len - recvd, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("recv");
        }
        if (ret == 0) {
            throw std::runtime_error("Connection closed during recv");
        }
        recvd += static_cast<std::size_t>(ret);
    }
}

}  // namespace netdisk

