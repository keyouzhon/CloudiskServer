#include "jwt_service.hpp"

#include "base64.hpp"

#include <openssl/hmac.h>

#include <array>
#include <chrono>
#include <cctype>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

std::string base64url_from_standard(std::string value) {
    for (char& ch : value) {
        if (ch == '+') {
            ch = '-';
        } else if (ch == '/') {
            ch = '_';
        }
    }
    while (!value.empty() && value.back() == '=') {
        value.pop_back();
    }
    return value;
}

std::string base64url_to_standard(std::string value) {
    for (char& ch : value) {
        if (ch == '-') {
            ch = '+';
        } else if (ch == '_') {
            ch = '/';
        }
    }
    while (value.size() % 4 != 0) {
        value.push_back('=');
    }
    return value;
}

std::string extract_json_string(const std::string& payload, const std::string& key) {
    const std::string pattern = "\"" + key + "\":\"";
    const auto pos = payload.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + pattern.size();
    const auto end = payload.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return payload.substr(start, end - start);
}

std::uint64_t extract_json_number(const std::string& payload, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    const auto pos = payload.find(pattern);
    if (pos == std::string::npos) {
        return 0;
    }
    std::size_t idx = pos + pattern.size();
    while (idx < payload.size() && std::isspace(static_cast<unsigned char>(payload[idx]))) {
        ++idx;
    }
    std::size_t end = idx;
    while (end < payload.size() && std::isdigit(static_cast<unsigned char>(payload[end]))) {
        ++end;
    }
    return std::stoull(payload.substr(idx, end - idx));
}

}  // namespace

namespace cloud::server {

JwtService::JwtService(JwtConfig config) : config_(std::move(config)) {}

std::string JwtService::escape_json(const std::string& raw) {
    std::string escaped;
    escaped.reserve(raw.size());
    for (char ch : raw) {
        if (ch == '"' || ch == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string JwtService::random_jti() {
    std::array<unsigned char, 16> bytes{};
    std::random_device rd;
    for (auto& byte : bytes) {
        byte = static_cast<unsigned char>(rd());
    }
    return cloud::util::base64_encode(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::string JwtService::base64url_encode(std::string_view input) {
    return base64url_from_standard(cloud::util::base64_encode(input));
}

std::string JwtService::base64url_decode(const std::string& input) {
    return cloud::util::base64_decode(base64url_to_standard(input));
}

std::string JwtService::issue(const std::string& username) const {
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto exp = now + config_.ttl_seconds;

    const std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::ostringstream payload;
    payload << "{\"iss\":\"" << escape_json(config_.issuer) << "\","
            << "\"sub\":\"" << escape_json(username) << "\","
            << "\"iat\":" << now << ","
            << "\"exp\":" << exp << ","
            << "\"jti\":\"" << escape_json(random_jti()) << "\"}";

    const std::string header_part = base64url_encode(header);
    const std::string payload_part = base64url_encode(payload.str());
    const std::string signing_input = header_part + "." + payload_part;

    unsigned int len = 0;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    HMAC(EVP_sha256(), config_.secret.data(), static_cast<int>(config_.secret.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()),
         signing_input.size(), digest.data(), &len);

    const std::string signature =
        base64url_from_standard(cloud::util::base64_encode(std::string_view(reinterpret_cast<const char*>(digest.data()), len)));

    return signing_input + "." + signature;
}

std::optional<JwtClaims> JwtService::verify(const std::string& token) const {
    const auto first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return std::nullopt;
    }
    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return std::nullopt;
    }

    const std::string header_part = token.substr(0, first_dot);
    const std::string payload_part = token.substr(first_dot + 1, second_dot - first_dot - 1);
    const std::string signature_part = token.substr(second_dot + 1);

    const std::string decoded_header = base64url_decode(header_part);
    if (decoded_header.find("\"HS256\"") == std::string::npos) {
        return std::nullopt;
    }

    const std::string signing_input = header_part + "." + payload_part;
    unsigned int len = 0;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    HMAC(EVP_sha256(), config_.secret.data(), static_cast<int>(config_.secret.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()),
         signing_input.size(), digest.data(), &len);

    const std::string expected_signature =
        base64url_from_standard(cloud::util::base64_encode(std::string_view(reinterpret_cast<const char*>(digest.data()), len)));

    if (expected_signature != signature_part) {
        return std::nullopt;
    }

    const std::string payload = base64url_decode(payload_part);
    JwtClaims claims;
    claims.subject = extract_json_string(payload, "sub");
    claims.issued_at = extract_json_number(payload, "iat");
    claims.expires_at = extract_json_number(payload, "exp");

    if (claims.subject.empty() || claims.expires_at == 0) {
        return std::nullopt;
    }

    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (claims.expires_at < now) {
        return std::nullopt;
    }
    return claims;
}

}  // namespace cloud::server


