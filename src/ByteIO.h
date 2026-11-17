#pragma once
// ByteIO.h — unaligned little/big-endian loads shared by the binary format readers.
// The caller guarantees the bytes are in bounds; float/double loads return the raw IEEE bit pattern
// (NaN/Inf included), the RdFinite* ones 0 for a non-finite value.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Explorer2::ByteIO {

// Unsigned LEB128 (protobuf, AV1, Draco varints): 7 bits per byte, the lowest group first, at most maxBytes
// bytes (10 hold 64 bits). False when the bytes run out or the limit is reached without an ending byte.
inline bool RdLeb128(const uint8_t* data, size_t size, size_t& pos, uint64_t& out, int maxBytes = 10) {
    out = 0;
    for (int i = 0; i < maxBytes && i < 10; ++i) {
        if (pos >= size) return false;
        const uint8_t b = data[pos++];
        out |= static_cast<uint64_t>(b & 0x7Fu) << (7 * i);
        if ((b & 0x80u) == 0) return true;
    }
    return false;
}

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}
inline int16_t RdI16LE(const uint8_t* p) { return static_cast<int16_t>(RdU16LE(p)); }
inline int16_t RdI16BE(const uint8_t* p) { return static_cast<int16_t>(RdU16BE(p)); }

inline uint32_t RdU24LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
}
inline uint32_t RdU24BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[2]);
}

inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline int32_t RdI32LE(const uint8_t* p) { return static_cast<int32_t>(RdU32LE(p)); }
inline int32_t RdI32BE(const uint8_t* p) { return static_cast<int32_t>(RdU32BE(p)); }

inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline int64_t RdI64LE(const uint8_t* p) { return static_cast<int64_t>(RdU64LE(p)); }
inline int64_t RdI64BE(const uint8_t* p) { return static_cast<int64_t>(RdU64BE(p)); }

inline float RdF32LE(const uint8_t* p) {
    uint32_t u = RdU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
inline float RdF32BE(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
inline double RdF64LE(const uint8_t* p) {
    uint64_t u = RdU64LE(p);
    double d = 0.0;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}
inline double RdF64BE(const uint8_t* p) {
    uint64_t u = RdU64BE(p);
    double d = 0.0;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

// IEEE 754 binary16 -> float, exact for every input (subnormals, Inf and NaN payloads included)
inline float HalfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
    uint32_t exp = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    uint32_t mant = static_cast<uint32_t>(h) & 0x3FFu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            // Subnormal half: normalize the mantissa into a normal float
            exp = 1;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3FFu;
            bits = sign | ((exp + 112u) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Endianness decided at run time (TIFF, DICOM and Godot resources say in the file which one they use)
inline uint16_t RdU16(const uint8_t* p, bool bigEndian) { return bigEndian ? RdU16BE(p) : RdU16LE(p); }
inline uint32_t RdU32(const uint8_t* p, bool bigEndian) { return bigEndian ? RdU32BE(p) : RdU32LE(p); }
inline uint64_t RdU64(const uint8_t* p, bool bigEndian) { return bigEndian ? RdU64BE(p) : RdU64LE(p); }
inline float RdF32(const uint8_t* p, bool bigEndian) { return bigEndian ? RdF32BE(p) : RdF32LE(p); }
inline double RdF64(const uint8_t* p, bool bigEndian) { return bigEndian ? RdF64BE(p) : RdF64LE(p); }

// For readers that take a non-finite value (a corrupt coordinate) as 0, so it cannot poison bounds or normals
inline float RdFiniteF32LE(const uint8_t* p) { const float f = RdF32LE(p); return std::isfinite(f) ? f : 0.0f; }
inline float RdFiniteF32BE(const uint8_t* p) { const float f = RdF32BE(p); return std::isfinite(f) ? f : 0.0f; }
inline double RdFiniteF64LE(const uint8_t* p) { const double d = RdF64LE(p); return std::isfinite(d) ? d : 0.0; }

// bfloat16 (upper half of a binary32) -> float
inline float BFloat16ToFloat(uint16_t bf) {
    uint32_t bits = static_cast<uint32_t>(bf) << 16;
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

} // namespace Explorer2::ByteIO
