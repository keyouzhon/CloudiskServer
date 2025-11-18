#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace cloud::server {

namespace {
std::string now_string() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&now_c, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
}

Logger::Logger(const std::string& file_path) : file_path_(file_path) {
    ensure_stream();
}

void Logger::ensure_stream() {
    if (stream_.is_open()) {
        return;
    }
    const auto parent = std::filesystem::path(file_path_).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    stream_.open(file_path_, std::ios::app);
    if (!stream_) {
        throw std::runtime_error("Failed to open log file: " + file_path_);
    }
}

std::string Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        default:
            return "INFO";
    }
}

void Logger::log(LogLevel level, std::string_view message) {
    const std::string stamp = now_string();
    std::lock_guard<std::mutex> lock(mutex_);
    ensure_stream();
    stream_ << stamp << " [" << level_to_string(level) << "] " << message << '\n';
    stream_.flush();
    std::clog << stamp << " [" << level_to_string(level) << "] " << message << '\n';
}

}  // namespace cloud::server
