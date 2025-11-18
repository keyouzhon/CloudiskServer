#include "storage_manager.hpp"

#include <fcntl.h>
#include <openssl/md5.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cloud::server {

namespace {
constexpr std::uint64_t kMmapThreshold = 100ULL * 1024 * 1024;
constexpr std::size_t kReadChunk = 1024 * 1024;

std::uint64_t last_write_offset(const UploadCheckpoint& checkpoint) {
    if (!std::filesystem::exists(checkpoint.temp_path)) {
        return 0;
    }
    return std::filesystem::file_size(checkpoint.temp_path);
}

void write_meta(const UploadCheckpoint& checkpoint) {
    std::ofstream meta(checkpoint.meta_path, std::ios::trunc);
    meta << "path=" << checkpoint.final_path.string() << "\n";
    meta << "total=" << checkpoint.total << "\n";
    meta << "received=" << checkpoint.received << "\n";
    meta.flush();
}

UploadCheckpoint read_meta(const std::filesystem::path& meta_path, const std::filesystem::path& final_path) {
    UploadCheckpoint cp;
    cp.meta_path = meta_path;
    cp.final_path = final_path;
    cp.active = std::filesystem::exists(meta_path);
    if (!cp.active) {
        return cp;
    }

    std::ifstream meta(meta_path);
    std::string line;
    while (std::getline(meta, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const auto key = line.substr(0, pos);
        const auto value = line.substr(pos + 1);
        if (key == "total") {
            cp.total = std::stoull(value);
        } else if (key == "received") {
            cp.received = std::stoull(value);
        } else if (key == "path") {
            cp.final_path = value;
        }
    }
    return cp;
}

}  // namespace

StorageManager::StorageManager(std::filesystem::path root) : root_(std::move(root)) {
    std::filesystem::create_directories(root_);
}

std::filesystem::path StorageManager::user_root(const std::string& username) const {
    auto path = root_ / username;
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path StorageManager::checkpoint_dir(const std::string& username) const {
    auto dir = user_root(username) / ".resume";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path StorageManager::meta_file(const std::string& username, const std::string& md5) const {
    return checkpoint_dir(username) / (md5 + ".meta");
}

std::filesystem::path StorageManager::temp_file(const std::string& username, const std::string& md5) const {
    return checkpoint_dir(username) / (md5 + ".part");
}

std::filesystem::path StorageManager::sanitize_path(const std::filesystem::path& base,
                                                    const std::filesystem::path& relative) const {
    auto target = base / relative;
    auto canonical_base = std::filesystem::weakly_canonical(base);
    auto canonical_target = std::filesystem::weakly_canonical(target);
    if (canonical_target.string().rfind(canonical_base.string(), 0) != 0) {
        throw std::runtime_error("Path traversal detected");
    }
    return canonical_target;
}

std::filesystem::path StorageManager::resolve(const std::string& username,
                                              const std::filesystem::path& relative) const {
    return sanitize_path(user_root(username), relative);
}

std::vector<DirEntry> StorageManager::list(const std::string& username, const std::filesystem::path& relative_path) {
    std::vector<DirEntry> entries;
    const auto target = resolve(username, relative_path);
    if (!std::filesystem::exists(target)) {
        return entries;
    }
    for (const auto& entry : std::filesystem::directory_iterator(target)) {
        DirEntry item;
        item.name = entry.path().filename().string();
        item.is_directory = entry.is_directory();
        item.size = entry.is_regular_file() ? entry.file_size() : 0;
        const auto ftime = entry.last_write_time();
        const auto sys_time = decltype(ftime)::clock::to_sys(ftime);
        item.modified =
            std::chrono::duration_cast<std::chrono::seconds>(sys_time.time_since_epoch()).count();
        entries.push_back(std::move(item));
    }
    return entries;
}

bool StorageManager::ensure_directory(const std::string& username, const std::filesystem::path& relative_path) {
    const auto path = resolve(username, relative_path);
    std::filesystem::create_directories(path);
    return std::filesystem::exists(path);
}

bool StorageManager::remove(const std::string& username, const std::filesystem::path& relative_path) {
    const auto target = resolve(username, relative_path);
    if (!std::filesystem::exists(target)) {
        return false;
    }
    if (std::filesystem::is_directory(target)) {
        std::filesystem::remove_all(target);
        return true;
    }
    return std::filesystem::remove(target);
}

UploadCheckpoint StorageManager::prepare_upload(const std::string& username,
                                                const std::string& md5,
                                                const std::filesystem::path& logical_path,
                                                std::uint64_t total_bytes) {
    UploadCheckpoint checkpoint;
    checkpoint.active = true;
    checkpoint.total = total_bytes;
    checkpoint.final_path = resolve(username, logical_path);
    checkpoint.meta_path = meta_file(username, md5);
    checkpoint.temp_path = temp_file(username, md5);

    std::filesystem::create_directories(checkpoint.final_path.parent_path());

    if (std::filesystem::exists(checkpoint.meta_path)) {
        auto existing = read_meta(checkpoint.meta_path, checkpoint.final_path);
        checkpoint.received = std::min(existing.received, total_bytes);
    } else if (std::filesystem::exists(checkpoint.temp_path)) {
        checkpoint.received = std::min<std::uint64_t>(last_write_offset(checkpoint), total_bytes);
        write_meta(checkpoint);
    } else {
        checkpoint.received = 0;
        write_meta(checkpoint);
    }
    return checkpoint;
}

bool StorageManager::write_chunk(const UploadCheckpoint& checkpoint,
                                 std::uint64_t offset,
                                 std::span<const std::byte> data) {
    const int fd = ::open(checkpoint.temp_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        return false;
    }
    const ssize_t written =
        ::pwrite(fd, data.data(), static_cast<size_t>(data.size()), static_cast<off_t>(offset));
    ::close(fd);
    return written == static_cast<ssize_t>(data.size());
}

void StorageManager::update_progress(const UploadCheckpoint& checkpoint, std::uint64_t received_bytes) {
    UploadCheckpoint updated = checkpoint;
    updated.received = received_bytes;
    write_meta(updated);
}

std::filesystem::path StorageManager::finalize_upload(const UploadCheckpoint& checkpoint) {
    std::filesystem::create_directories(checkpoint.final_path.parent_path());
    std::filesystem::rename(checkpoint.temp_path, checkpoint.final_path);
    if (std::filesystem::exists(checkpoint.meta_path)) {
        std::filesystem::remove(checkpoint.meta_path);
    }
    return checkpoint.final_path;
}

void StorageManager::discard_checkpoint(const UploadCheckpoint& checkpoint) {
    if (std::filesystem::exists(checkpoint.temp_path)) {
        std::filesystem::remove(checkpoint.temp_path);
    }
    if (std::filesystem::exists(checkpoint.meta_path)) {
        std::filesystem::remove(checkpoint.meta_path);
    }
}

std::vector<std::byte> StorageManager::read_chunk(const std::filesystem::path& absolute_path,
                                                  std::uint64_t offset,
                                                  std::size_t length) const {
    const auto size = file_size(absolute_path);
    if (offset >= size) {
        return {};
    }
    const std::size_t to_read = static_cast<std::size_t>(std::min<std::uint64_t>(length, size - offset));
    std::vector<std::byte> buffer(to_read);

    if (size >= kMmapThreshold) {
        const int fd = ::open(absolute_path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("Unable to open file for mmap");
        }
        const long page_size = sysconf(_SC_PAGESIZE);
        const std::uint64_t page_offset = offset % static_cast<std::uint64_t>(page_size);
        const std::uint64_t map_offset = offset - page_offset;
        const std::size_t map_length = static_cast<std::size_t>(page_offset + to_read);
        void* mapped =
            ::mmap(nullptr, map_length, PROT_READ, MAP_PRIVATE, fd, static_cast<off_t>(map_offset));
        if (mapped == MAP_FAILED) {
            ::close(fd);
            throw std::runtime_error("mmap failed");
        }
        std::memcpy(buffer.data(), static_cast<char*>(mapped) + page_offset, to_read);
        ::munmap(mapped, map_length);
        ::close(fd);
    } else {
        std::ifstream stream(absolute_path, std::ios::binary);
        stream.seekg(static_cast<std::streamoff>(offset));
        stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(to_read));
    }
    return buffer;
}

std::string StorageManager::compute_md5(const std::filesystem::path& absolute_path) const {
    MD5_CTX ctx;
    MD5_Init(&ctx);
    std::ifstream stream(absolute_path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open file for MD5");
    }
    std::vector<char> buf(kReadChunk);
    while (stream) {
        stream.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto read = stream.gcount();
        if (read > 0) {
            MD5_Update(&ctx, buf.data(), static_cast<size_t>(read));
        }
    }
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);
    std::ostringstream oss;
    oss << std::hex;
    for (unsigned char byte : digest) {
        oss << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::uint64_t StorageManager::file_size(const std::filesystem::path& absolute_path) const {
    return std::filesystem::exists(absolute_path) ? std::filesystem::file_size(absolute_path) : 0;
}

}  // namespace cloud::server
