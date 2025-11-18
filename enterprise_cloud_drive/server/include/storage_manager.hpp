#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cloud::server {

struct DirEntry {
    std::string name;
    bool is_directory;
    std::uint64_t size;
    std::uint64_t modified;
};

struct UploadCheckpoint {
    bool active = false;
    std::filesystem::path temp_path;
    std::filesystem::path meta_path;
    std::filesystem::path final_path;
    std::uint64_t total = 0;
    std::uint64_t received = 0;
};

class StorageManager {
public:
    explicit StorageManager(std::filesystem::path root);

    std::filesystem::path user_root(const std::string& username) const;
    std::filesystem::path resolve(const std::string& username, const std::filesystem::path& relative) const;

    std::vector<DirEntry> list(const std::string& username, const std::filesystem::path& relative_path);
    bool ensure_directory(const std::string& username, const std::filesystem::path& relative_path);
    bool remove(const std::string& username, const std::filesystem::path& relative_path);

    UploadCheckpoint prepare_upload(const std::string& username,
                                    const std::string& md5,
                                    const std::filesystem::path& logical_path,
                                    std::uint64_t total_bytes);
    bool write_chunk(const UploadCheckpoint& checkpoint, std::uint64_t offset, std::span<const std::byte> data);
    void update_progress(const UploadCheckpoint& checkpoint, std::uint64_t received_bytes);
    std::filesystem::path finalize_upload(const UploadCheckpoint& checkpoint);
    void discard_checkpoint(const UploadCheckpoint& checkpoint);

    std::vector<std::byte> read_chunk(const std::filesystem::path& absolute_path,
                                      std::uint64_t offset,
                                      std::size_t length) const;
    std::string compute_md5(const std::filesystem::path& absolute_path) const;
    std::uint64_t file_size(const std::filesystem::path& absolute_path) const;

private:
    std::filesystem::path sanitize_path(const std::filesystem::path& base, const std::filesystem::path& relative) const;

    std::filesystem::path checkpoint_dir(const std::string& username) const;
    std::filesystem::path meta_file(const std::string& username, const std::string& md5) const;
    std::filesystem::path temp_file(const std::string& username, const std::string& md5) const;

    std::filesystem::path root_;
};

}  // namespace cloud::server
