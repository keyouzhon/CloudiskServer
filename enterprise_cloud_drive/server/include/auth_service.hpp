#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>

namespace cloud::server {

struct UserRecord {
    std::string username;
    std::string password_hash;
    std::string salt;
};

class AuthService {
public:
    explicit AuthService(const std::string& database_path);
    ~AuthService();

    void initialize_schema();

    bool register_user(const std::string& username, const std::string& password);
    bool validate_user(const std::string& username, const std::string& password);

private:
    std::optional<UserRecord> find_user(const std::string& username);

    sqlite3* db_{};
};

}  // namespace cloud::server
