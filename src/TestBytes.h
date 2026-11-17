#pragma once
// TestBytes.h — little/big-endian byte writers the format self-tests build their sample files with
// (Append* grow a vector, Put* store into bytes already there).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace Explorer2::TestBytes {

inline void AppendU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
inline void AppendBytes(std::vector<uint8_t>& b, const void* p, size_t n) {
    const auto* u = static_cast<const uint8_t*>(p);
    b.insert(b.end(), u, u + n);
}
inline void AppendStr(std::vector<uint8_t>& b, std::string_view s) { AppendBytes(b, s.data(), s.size()); }

inline void AppendU16LE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
inline void AppendU32LE(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline void AppendU64LE(std::vector<uint8_t>& b, uint64_t v) {
    AppendU32LE(b, static_cast<uint32_t>(v));
    AppendU32LE(b, static_cast<uint32_t>(v >> 32));
}
inline void AppendI32LE(std::vector<uint8_t>& b, int32_t v) { AppendU32LE(b, static_cast<uint32_t>(v)); }
inline void AppendI64LE(std::vector<uint8_t>& b, int64_t v) { AppendU64LE(b, static_cast<uint64_t>(v)); }
inline void AppendF32LE(std::vector<uint8_t>& b, float f) { uint32_t u = 0; std::memcpy(&u, &f, 4); AppendU32LE(b, u); }
inline void AppendF64LE(std::vector<uint8_t>& b, double d) { uint64_t u = 0; std::memcpy(&u, &d, 8); AppendU64LE(b, u); }

inline void AppendU16BE(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
inline void AppendU32BE(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline void AppendU64BE(std::vector<uint8_t>& b, uint64_t v) {
    AppendU32BE(b, static_cast<uint32_t>(v >> 32));
    AppendU32BE(b, static_cast<uint32_t>(v));
}
inline void AppendI32BE(std::vector<uint8_t>& b, int32_t v) { AppendU32BE(b, static_cast<uint32_t>(v)); }
inline void AppendF32BE(std::vector<uint8_t>& b, float f) { uint32_t u = 0; std::memcpy(&u, &f, 4); AppendU32BE(b, u); }
inline void AppendF64BE(std::vector<uint8_t>& b, double d) { uint64_t u = 0; std::memcpy(&u, &d, 8); AppendU64BE(b, u); }

inline void PutU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
inline void PutU32LE(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline void PutU64LE(uint8_t* p, uint64_t v) {
    PutU32LE(p, static_cast<uint32_t>(v));
    PutU32LE(p + 4, static_cast<uint32_t>(v >> 32));
}
inline void PutI32LE(uint8_t* p, int32_t v) { PutU32LE(p, static_cast<uint32_t>(v)); }
inline void PutF32LE(uint8_t* p, float f) { uint32_t u = 0; std::memcpy(&u, &f, 4); PutU32LE(p, u); }
inline void PutF64LE(uint8_t* p, double d) { uint64_t u = 0; std::memcpy(&u, &d, 8); PutU64LE(p, u); }

inline void PutU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
inline void PutU32BE(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * (3 - i)));
}
inline void PutU64BE(uint8_t* p, uint64_t v) {
    PutU32BE(p, static_cast<uint32_t>(v >> 32));
    PutU32BE(p + 4, static_cast<uint32_t>(v));
}

} // namespace Explorer2::TestBytes
