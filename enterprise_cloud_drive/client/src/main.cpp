#include "client_app.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: cloud_drive_client <host> <port>" << std::endl;
        return 1;
    }

    const std::string host = argv[1];
    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));

    cloud::client::ClientApp app;
    if (!app.connect_to_server(host, port)) {
        return 1;
    }

    app.run_shell();
    return 0;
}
