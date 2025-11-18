#pragma once

#include "Protocol.hpp"

#include <netinet/in.h>
#include <string>

namespace netdisk {

class Session {
public:
    Session(const std::string& ip, uint16_t port);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void reconnect();

    void sendTrain(const Train& train);
    void receiveTrain(Train& train);

    const std::string& ip() const { return ip_; }
    uint16_t port() const { return port_; }

private:
    void connectSocket();
    void closeSocket();
    static void sendAll(int fd, const void* buf, std::size_t len);
    static void recvAll(int fd, void* buf, std::size_t len);

    std::string ip_;
    uint16_t port_;
    int sockfd_{-1};
};

}  // namespace netdisk

