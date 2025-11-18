#pragma once

#include "Protocol.hpp"
#include "Session.hpp"

#include <string>

namespace netdisk {

struct LoginContext {
    std::string name;
    std::string passwordHash;
    std::string token;
    int currentCode{0};
};

class ClientApp {
public:
    ClientApp(const std::string& ip, uint16_t port);
    int run();

private:
    bool handleLogin();
    bool handleRegister();
    bool processCommandLoop();
    void handleLs();

    void sendSimpleCommand(MsgCode code, const void* payload, int payloadLen, Train& response);

    Session session_;
    LoginContext ctx_;
};

}  // namespace netdisk

