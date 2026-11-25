#pragma once
// Base64.h — RFC 4648 base64 (standard alphabet) decoding shared by the readers: MIME parts, PEM / armored blocks,
// data URIs, Godot resources, VTK XML data arrays.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer2::Base64 {

enum class Mode {
    Lenient,      // characters outside the alphabet are skipped; '=' ends the data
    Strict,       // only the alphabet and whitespace may appear; '=' ends the data
    Concatenated, // like Lenient, but '=' only ends one block and decoding goes on with the next (VTK)
};

// Decodes `in` into `out` (cleared first); whitespace is always skipped. False when a strict input holds another
// character, or when more than maxBytes would come out.
inline bool Decode(std::string_view in, std::vector<uint8_t>& out, Mode mode = Mode::Lenient, size_t maxBytes = SIZE_MAX) {
    out.clear();
    const size_t estimate = in.size() / 4 * 3 + 3;
    out.reserve(estimate < maxBytes ? estimate : maxBytes);
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        const unsigned char c = static_cast<unsigned char>(ch);
        uint32_t v = 0;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') {
            if (mode != Mode::Concatenated) break;
            acc = 0;
            bits = 0;
            continue;
        } else if (c <= 0x20 || mode != Mode::Strict) {
            continue;
        } else {
            return false;
        }
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
            if (out.size() > maxBytes) return false;
        }
    }
    return true;
}

inline std::string Encode(const uint8_t* data, size_t len) {
    static constexpr char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!data || len == 0) return {};
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0u;
        const uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0u;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlpha[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlpha[(triple >> 12) & 0x3Fu]);
        out.push_back((i + 1 < len) ? kAlpha[(triple >> 6) & 0x3Fu] : '=');
        out.push_back((i + 2 < len) ? kAlpha[triple & 0x3Fu] : '=');
    }
    return out;
}

} // namespace Explorer2::Base64
