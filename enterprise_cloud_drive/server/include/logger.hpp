#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

enum class LogLevel {
    kInfo,
    kWarn,
    kError,
};

namespace cloud::server {

class Logger {
public:
    explicit Logger(const std::string& file_path);
    void log(LogLevel level, std::string_view message);

    void info(std::string_view message) { log(LogLevel::kInfo, message); }
    void warn(std::string_view message) { log(LogLevel::kWarn, message); }
    void error(std::string_view message) { log(LogLevel::kError, message); }

private:
    std::string level_to_string(LogLevel level) const;
    void ensure_stream();

    std::mutex mutex_;
    std::ofstream stream_;
    std::string file_path_;
};

}  // namespace cloud::server
