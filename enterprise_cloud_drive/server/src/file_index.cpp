#include "file_index.hpp"

#include <filesystem>
#include <stdexcept>

#include <sqlite3.h>

namespace cloud::server {

FileIndex::FileIndex(const std::string& database_path) {
    const auto parent = std::filesystem::path(database_path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    if (sqlite3_open(database_path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open metadata database");
    }
}

FileIndex::~FileIndex() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void FileIndex::initialize_schema() {
    const char* ddl = R"SQL(
        CREATE TABLE IF NOT EXISTS user_files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            owner TEXT NOT NULL,
            logical_path TEXT NOT NULL,
            md5 TEXT NOT NULL,
            storage_path TEXT NOT NULL,
            size INTEGER NOT NULL,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(owner, logical_path)
        );
        CREATE INDEX IF NOT EXISTS idx_user_files_md5 ON user_files(md5);
    )SQL";

    char* err = nullptr;
    if (sqlite3_exec(db_, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("Failed to initialize file index: " + msg);
    }
}

std::optional<FileMetadata> FileIndex::find_by_path(const std::string& owner, const std::string& logical_path) {
    const char* sql = R"SQL(
        SELECT owner,logical_path,md5,storage_path,size FROM user_files
        WHERE owner=? AND logical_path=?
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, owner.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, logical_path.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<FileMetadata> meta;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        meta = FileMetadata{
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
            static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 4))};
    }
    sqlite3_finalize(stmt);
    return meta;
}

std::optional<FileMetadata> FileIndex::find_by_md5(const std::string& md5) {
    const char* sql = R"SQL(
        SELECT owner,logical_path,md5,storage_path,size FROM user_files
        WHERE md5=?
        LIMIT 1
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, md5.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<FileMetadata> meta;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        meta = FileMetadata{
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
            static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 4))};
    }
    sqlite3_finalize(stmt);
    return meta;
}

void FileIndex::upsert(const FileMetadata& metadata) {
    const char* sql = R"SQL(
        INSERT INTO user_files(owner, logical_path, md5, storage_path, size)
        VALUES(?,?,?,?,?)
        ON CONFLICT(owner, logical_path)
        DO UPDATE SET md5=excluded.md5,
                      storage_path=excluded.storage_path,
                      size=excluded.size,
                      updated_at=CURRENT_TIMESTAMP
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, metadata.owner.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, metadata.logical_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, metadata.md5.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, metadata.storage_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(metadata.size));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void FileIndex::remove(const std::string& owner, const std::string& logical_path) {
    const char* sql = "DELETE FROM user_files WHERE owner=? AND logical_path=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, owner.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, logical_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

}  // namespace cloud::server


