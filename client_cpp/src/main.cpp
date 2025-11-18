#include "ClientApp.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port>\n";
        return EXIT_FAILURE;
    }
    const std::string ip = argv[1];
    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    try {
        netdisk::ClientApp app(ip, port);
        return app.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}

