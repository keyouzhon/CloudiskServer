#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cloud::server {

struct JwtConfig {
    std::string issuer = "enterprise-cloud-drive";
    std::string secret;
    uint32_t ttl_seconds = 3600;
};

struct JwtClaims {
    std::string subject;
    std::uint64_t expires_at{};
    std::uint64_t issued_at{};
};

class JwtService {
public:
    explicit JwtService(JwtConfig config);

    std::string issue(const std::string& username) const;
    std::optional<JwtClaims> verify(const std::string& token) const;

private:
    static std::string base64url_encode(std::string_view input);
    static std::string base64url_decode(const std::string& input);
    static std::string escape_json(const std::string& raw);
    static std::string random_jti();

    JwtConfig config_;
};

}  // namespace cloud::server


