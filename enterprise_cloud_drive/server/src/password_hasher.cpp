#include "password_hasher.hpp"

#include <array>
#include <crypt.h>
#include <random>
#include <stdexcept>

namespace cloud::server {

namespace {
constexpr char kSaltAlphabet[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr std::size_t kSaltLength = 16;
}  // namespace

std::string PasswordHasher::generate_salt() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(sizeof(kSaltAlphabet) - 2));

    std::string salt;
    salt.reserve(kSaltLength);
    for (std::size_t i = 0; i < kSaltLength; ++i) {
        salt.push_back(kSaltAlphabet[dist(gen)]);
    }
    return "$6$" + salt;  // SHA-512 crypt identifier
}

std::string PasswordHasher::hash_password(const std::string& password, const std::string& salt) {
    crypt_data data;
    data.initialized = 0;
    const std::string salt_spec = salt.rfind("$6$", 0) == 0 ? salt : "$6$" + salt;
    char* hashed = crypt_r(password.c_str(), salt_spec.c_str(), &data);
    if (hashed == nullptr) {
        throw std::runtime_error("crypt_r failed");
    }
    return std::string(hashed);
}

}  // namespace cloud::server
