#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace netdisk {

constexpr std::size_t kBufferSize = 10240;

// 需与服务器 MSG_code 枚举保持一致
enum class MsgCode : int32_t {
    LOGIN_PRE = 1,
    LOGIN_NO,
    LOGIN_POK,
    LOGIN_Q,
    LOGIN_OK,
    REGISTER_PRE,
    REGISTER_NO,
    REGISTER_POK,
    REGISTER_Q,
    REGISTER_OK,
    TOKEN_PLESE,
    OPERATE_Q,
    OPERATE_NO,
    OPERATE_OK,
    LS_Q,
    LS_OK,
    DOWNLOAD_PRE,
    DOWNLOAD_POK,
    DOWNLOAD_Q,
    UPLOAD_PRE,
    UPLOAD_POK,
    UPLOAD_OK,
    UPLOAD_Q,
    DOWN_MORE_PRE,
    DOWN_MORE_POK
};

#pragma pack(push, 1)
struct Train {
    int32_t Len = 0;
    int32_t ctl_code = 0;
    std::array<char, kBufferSize> buf{};
};

struct Zhuce {
    char name[30]{};
    char passward[100]{};
    char token[50]{};
};

struct QURMsg {
    char buf1[100]{};
    char buf[200]{};
};
#pragma pack(pop)

inline void copyString(char* dst, std::size_t dstSize, std::string_view src) {
    if (dstSize == 0) {
        return;
    }
    const std::size_t len = std::min(dstSize - 1, src.size());
    std::memcpy(dst, src.data(), len);
    dst[len] = '\0';
}

inline void copyString(char* dst, std::size_t dstSize, const std::string& src) {
    copyString(dst, dstSize, std::string_view(src));
}

inline void copyString(char* dst, std::size_t dstSize, const char* src) {
    copyString(dst, dstSize, std::string_view(src ? src : ""));
}

}  // namespace netdisk

