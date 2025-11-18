#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct sqlite3;

namespace cloud::server {

struct FileMetadata {
    std::string owner;
    std::string logical_path;
    std::string md5;
    std::string storage_path;
    std::uint64_t size;
};

class FileIndex {
public:
    explicit FileIndex(const std::string& database_path);
    ~FileIndex();

    void initialize_schema();
    std::optional<FileMetadata> find_by_path(const std::string& owner, const std::string& logical_path);
    std::optional<FileMetadata> find_by_md5(const std::string& md5);
    void upsert(const FileMetadata& metadata);
    void remove(const std::string& owner, const std::string& logical_path);

private:
    sqlite3* db_{};
};

}  // namespace cloud::server


