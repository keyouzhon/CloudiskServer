#include "ClientApp.hpp"

#include <crypt.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace netdisk {

namespace {

MsgCode toMsgCode(int code) {
    return static_cast<MsgCode>(code);
}

int toRaw(MsgCode code) {
    return static_cast<int>(code);
}

std::string prompt(const std::string& text) {
    std::cout << text;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

}  // namespace

ClientApp::ClientApp(const std::string& ip, uint16_t port)
    : session_(ip, port) {}

int ClientApp::run() {
    while (true) {
        std::cout << "\n请选择操作:\n"
                     "1) 注册新账号\n"
                     "2) 登录\n"
                     "3) 退出\n"
                     "输入选项: ";
        int option = 0;
        if (!(std::cin >> option)) {
            std::cerr << "输入无效\n";
            return 1;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        bool success = false;
        if (option == 1) {
            success = handleRegister();
        } else if (option == 2) {
            success = handleLogin();
        } else if (option == 3) {
            return 0;
        } else {
            std::cout << "未知选项\n";
            continue;
        }

        if (success) {
            if (!processCommandLoop()) {
                return 0;
            }
        }
    }
}

bool ClientApp::handleRegister() {
    const std::string name = prompt("请输入用户名: ");
    const std::string password = prompt("请输入密码: ");
    const std::string confirm = prompt("请再次输入密码: ");
    if (password != confirm) {
        std::cout << "两次输入密码不一致\n";
        return false;
    }

    Train request{};
    request.ctl_code = toRaw(MsgCode::LOGIN_PRE);
    request.Len = static_cast<int>(name.size() + 1);
    std::memcpy(request.buf.data(), name.c_str(), request.Len);
    session_.sendTrain(request);

    Train response{};
    session_.receiveTrain(response);
    if (toMsgCode(response.ctl_code) == MsgCode::LOGIN_NO) {
        std::cout << "账号已存在\n";
        return false;
    }
    if (toMsgCode(response.ctl_code) != MsgCode::LOGIN_POK) {
        std::cout << "注册失败，服务器返回未知响应\n";
        return false;
    }

    std::string salt(response.buf.data(), static_cast<std::size_t>(response.Len));
    const char* hashed = ::crypt(password.c_str(), salt.c_str());
    if (!hashed) {
        throw std::runtime_error("crypt 失败");
    }

    Zhuce zhuce{};
    copyString(zhuce.name, sizeof(zhuce.name), name);
    copyString(zhuce.passward, sizeof(zhuce.passward), hashed);

    Train finalReq{};
    finalReq.ctl_code = toRaw(MsgCode::LOGIN_Q);
    finalReq.Len = sizeof(Zhuce);
    std::memcpy(finalReq.buf.data(), &zhuce, sizeof(Zhuce));
    session_.sendTrain(finalReq);

    std::cout << "注册成功，请重新登录\n";
    return true;
}

bool ClientApp::handleLogin() {
    const std::string name = prompt("请输入用户名: ");
    const std::string password = prompt("请输入密码: ");

    Train request{};
    request.ctl_code = toRaw(MsgCode::REGISTER_PRE);
    request.Len = static_cast<int>(name.size() + 1);
    std::memcpy(request.buf.data(), name.c_str(), request.Len);
    session_.sendTrain(request);

    Train response{};
    session_.receiveTrain(response);
    if (toMsgCode(response.ctl_code) == MsgCode::REGISTER_NO) {
        std::cout << "账号不存在或密码错误\n";
        return false;
    }
    if (toMsgCode(response.ctl_code) != MsgCode::REGISTER_POK) {
        std::cout << "登录失败，服务器返回未知响应\n";
        return false;
    }

    std::string salt(response.buf.data(), static_cast<std::size_t>(response.Len));
    const char* hashed = ::crypt(password.c_str(), salt.c_str());
    if (!hashed) {
        throw std::runtime_error("crypt 失败");
    }

    std::ostringstream tokenBuilder;
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tokenBuilder << name << " " << std::put_time(std::localtime(&now), "%F %T");

    Zhuce zhuce{};
    copyString(zhuce.name, sizeof(zhuce.name), name);
    copyString(zhuce.passward, sizeof(zhuce.passward), hashed);
    copyString(zhuce.token, sizeof(zhuce.token), tokenBuilder.str());

    Train finalReq{};
    finalReq.ctl_code = toRaw(MsgCode::REGISTER_Q);
    finalReq.Len = sizeof(Zhuce);
    std::memcpy(finalReq.buf.data(), &zhuce, sizeof(Zhuce));
    session_.sendTrain(finalReq);

    session_.receiveTrain(response);
    if (toMsgCode(response.ctl_code) != MsgCode::REGISTER_OK) {
        std::cout << "账号或密码错误\n";
        return false;
    }

    ctx_.name = name;
    ctx_.passwordHash = hashed;
    ctx_.token = tokenBuilder.str();
    ctx_.currentCode = 0;
    std::cout << "登录成功\n";
    return true;
}

bool ClientApp::processCommandLoop() {
    std::string line;
    while (true) {
        std::cout << "[" << ctx_.name << "@Netdisk]$ ";
        if (!std::getline(std::cin, line)) {
            return false;
        }
        if (line.empty()) {
            continue;
        }
        if (line == "exit") {
            return false;
        }
        if (line == "ls") {
            handleLs();
            continue;
        }
        std::cout << "暂不支持该命令: " << line << "\n";
    }
}

void ClientApp::handleLs() {
    try {
        Train response{};
        sendSimpleCommand(MsgCode::LS_Q, nullptr, 0, response);
        if (toMsgCode(response.ctl_code) == MsgCode::LS_OK) {
            if (response.Len > 0) {
                std::string listing(response.buf.data(), static_cast<std::size_t>(response.Len));
                std::cout << listing;
            } else {
                std::cout << "(空)\n";
            }
        } else {
            std::cout << "ls 执行失败\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "ls 执行异常: " << ex.what() << "\n";
    }
}

void ClientApp::sendSimpleCommand(MsgCode code, const void* payload, int payloadLen, Train& response) {
    Train request{};
    request.ctl_code = toRaw(code);
    request.Len = payloadLen;
    if (payloadLen > 0 && payload) {
        std::memcpy(request.buf.data(), payload, static_cast<std::size_t>(payloadLen));
    }
    session_.sendTrain(request);
    session_.receiveTrain(response);
}

}  // namespace netdisk

