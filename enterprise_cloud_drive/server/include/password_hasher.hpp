#pragma once

#include <string>

namespace cloud::server {

class PasswordHasher {
public:
    static std::string generate_salt();
    static std::string hash_password(const std::string& password, const std::string& salt);
};

}  // namespace cloud::server
