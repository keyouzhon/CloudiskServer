#pragma once

#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cloud::util {

static constexpr std::string_view kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode(std::string_view input) {
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    uint32_t accumulator = 0;
    int bits_collected = 0;

    for (unsigned char c : input) {
        accumulator = (accumulator << 8) | c;
        bits_collected += 8;
        while (bits_collected >= 6) {
            bits_collected -= 6;
            output.push_back(kBase64Chars[(accumulator >> bits_collected) & 0x3F]);
        }
    }

    if (bits_collected > 0) {
        accumulator <<= 6 - bits_collected;
        output.push_back(kBase64Chars[accumulator & 0x3F]);
    }

    while (output.size() % 4 != 0) {
        output.push_back('=');
    }

    return output;
}

inline std::string base64_decode(std::string_view input) {
    if (input.size() % 4 != 0) {
        throw std::invalid_argument("Invalid Base64 input length");
    }

    std::array<int, 256> decoding_table{};
    decoding_table.fill(-1);
    for (size_t i = 0; i < kBase64Chars.size(); ++i) {
        decoding_table[static_cast<unsigned char>(kBase64Chars[i])] = static_cast<int>(i);
    }

    std::string output;
    output.reserve((input.size() / 4) * 3);

    uint32_t accumulator = 0;
    int bits_collected = 0;

    for (char ch : input) {
        if (ch == '=') {
            break;
        }
        const int value = decoding_table[static_cast<unsigned char>(ch)];
        if (value < 0) {
            throw std::invalid_argument("Invalid Base64 character");
        }
        accumulator = (accumulator << 6) | value;
        bits_collected += 6;
        if (bits_collected >= 8) {
            bits_collected -= 8;
            output.push_back(static_cast<char>((accumulator >> bits_collected) & 0xFF));
        }
    }

    return output;
}

}  // namespace cloud::util
