#include "auth_service.hpp"

#include "password_hasher.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace cloud::server {

AuthService::AuthService(const std::string& database_path) {
    const auto parent = std::filesystem::path(database_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    if (sqlite3_open(database_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database: " + std::string(sqlite3_errmsg(db_)));
    }
}

AuthService::~AuthService() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void AuthService::initialize_schema() {
    const char* ddl = R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )SQL";

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, ddl, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string error(err_msg ? err_msg : "unknown error");
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to initialize schema: " + error);
    }
}

bool AuthService::register_user(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        return false;
    }
    if (find_user(username).has_value()) {
        return false;
    }
    const std::string salt = PasswordHasher::generate_salt();
    const std::string hash = PasswordHasher::hash_password(password, salt);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO users(username, password_hash, salt) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);

    const bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool AuthService::validate_user(const std::string& username, const std::string& password) {
    auto record = find_user(username);
    if (!record.has_value()) {
        return false;
    }
    const std::string attempted = PasswordHasher::hash_password(password, record->salt);
    return attempted == record->password_hash;
}

std::optional<UserRecord> AuthService::find_user(const std::string& username) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT username,password_hash,salt FROM users WHERE username=?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<UserRecord> record;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        record = UserRecord{
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))};
    }
    sqlite3_finalize(stmt);
    return record;
}

}  // namespace cloud::server
