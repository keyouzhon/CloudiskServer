#pragma once

#include <arpa/inet.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cloud::protocol {

inline constexpr uint32_t kMagic = 0x45434452;  // "E C D R"
inline constexpr uint16_t kVersion = 1;

using HeaderMap = std::unordered_map<std::string, std::string>;

struct Message {
    HeaderMap headers;
    std::vector<std::byte> body;
};

namespace detail {
struct WireHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t body_size;
};

inline std::string serialize_headers(const HeaderMap& headers) {
    std::string encoded;
    for (const auto& entry : headers) {
        encoded.append(entry.first);
        encoded.push_back('=');
        encoded.append(entry.second);
        encoded.push_back('\n');
    }
    return encoded;
}

inline HeaderMap parse_headers(std::string_view data) {
    HeaderMap headers;
    std::size_t start = 0;
    while (start < data.size()) {
        const auto end = data.find('\n', start);
        const auto line_end = end == std::string_view::npos ? data.size() : end;
        if (line_end == start) {
            break;
        }
        const auto sep = data.find('=', start);
        if (sep != std::string_view::npos && sep < line_end) {
            headers.emplace(std::string(data.substr(start, sep - start)),
                            std::string(data.substr(sep + 1, line_end - sep - 1)));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return headers;
}

}  // namespace detail

inline Message make_message(std::initializer_list<std::pair<std::string, std::string>> headers,
                            std::vector<std::byte> body = {}) {
    Message msg;
    for (const auto& entry : headers) {
        msg.headers.emplace(entry.first, entry.second);
    }
    msg.body = std::move(body);
    return msg;
}

inline std::string_view header_value(const Message& msg, const std::string& key,
                                     std::string_view fallback = {}) {
    auto it = msg.headers.find(key);
    if (it == msg.headers.end()) {
        return fallback;
    }
    return it->second;
}

inline std::vector<std::byte> encode(const Message& message) {
    const auto header_blob = detail::serialize_headers(message.headers);
    detail::WireHeader wire{};
    wire.magic = htonl(kMagic);
    wire.version = htons(kVersion);
    wire.header_size = htons(static_cast<uint16_t>(header_blob.size()));
    wire.body_size = htonl(static_cast<uint32_t>(message.body.size()));

    std::vector<std::byte> buffer(sizeof(detail::WireHeader) + header_blob.size() + message.body.size());
    std::memcpy(buffer.data(), &wire, sizeof(detail::WireHeader));
    std::memcpy(buffer.data() + sizeof(detail::WireHeader), header_blob.data(), header_blob.size());
    if (!message.body.empty()) {
        std::memcpy(buffer.data() + sizeof(detail::WireHeader) + header_blob.size(), message.body.data(),
                    message.body.size());
    }
    return buffer;
}

inline bool try_decode(std::vector<std::byte>& buffer, std::size_t& offset, Message& out) {
    const auto available = buffer.size() - offset;
    if (available < sizeof(detail::WireHeader)) {
        return false;
    }
    detail::WireHeader wire{};
    std::memcpy(&wire, buffer.data() + offset, sizeof(detail::WireHeader));

    const uint32_t magic = ntohl(wire.magic);
    const uint16_t version = ntohs(wire.version);
    const uint16_t header_size = ntohs(wire.header_size);
    const uint32_t body_size = ntohl(wire.body_size);

    if (magic != kMagic) {
        throw std::runtime_error("Protocol magic mismatch");
    }
    if (version != kVersion) {
        throw std::runtime_error("Unsupported protocol version");
    }

    const std::size_t frame_size = sizeof(detail::WireHeader) + header_size + body_size;
    if (available < frame_size) {
        return false;
    }

    const auto* header_begin = buffer.data() + offset + sizeof(detail::WireHeader);
    std::string header_blob(reinterpret_cast<const char*>(header_begin), header_size);
    out.headers = detail::parse_headers(header_blob);

    out.body.resize(body_size);
    if (body_size > 0) {
        const auto* body_begin = header_begin + header_size;
        std::memcpy(out.body.data(), body_begin, body_size);
    }

    offset += frame_size;
    if (offset > 0 && offset > buffer.size() / 2) {
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
        offset = 0;
    }
    return true;
}

}  // namespace cloud::protocol


