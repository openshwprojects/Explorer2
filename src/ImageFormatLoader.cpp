#include "ImageFormatLoader.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#ifdef _WIN32
// Windows codecs: WIC (JPEG / PNG inside other formats, HEIF), DirectWrite and GDI (fonts, metafiles), Media
// Foundation (HEVC, H.264). The Linux build of the readers has none of them: the few decoders that need them
// return false there (#ifndef _WIN32 at the top of each).
#include "OsCompat.h"
#include <wrl/client.h>
#include <wincodec.h>
#include <dwrite_3.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>
#include <unordered_map>
#include <functional>

namespace Explorer2 {

namespace {

constexpr uint32_t kMaxRasterDimension = 16384;

// Byte loads under this file's names (ByteIO holds the one implementation)
inline uint16_t ReadU16LE(const uint8_t* p) { return ByteIO::RdU16LE(p); }
inline uint32_t ReadU32LE(const uint8_t* p) { return ByteIO::RdU32LE(p); }
inline uint64_t ReadU64LE(const uint8_t* p) { return ByteIO::RdU64LE(p); }
inline uint16_t ReadU16BE(const uint8_t* p) { return ByteIO::RdU16BE(p); }
inline int16_t ReadI16BE(const uint8_t* p) { return ByteIO::RdI16BE(p); }
inline uint32_t ReadU32BE(const uint8_t* p) { return ByteIO::RdU32BE(p); }
inline int32_t ReadI32BE(const uint8_t* p) { return ByteIO::RdI32BE(p); }
inline uint64_t ReadU64BE(const uint8_t* p) { return ByteIO::RdU64BE(p); }

inline void WritePremulBGRA(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 255) {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = 255;
    } else if (a == 0) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
    } else {
        dst[0] = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
        dst[1] = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
        dst[2] = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
        dst[3] = a;
    }
}

void DownscaleIfRequested(uint32_t maxDimension, ExtendedImageInfo& info) {
    info.origWidth = info.width;
    info.origHeight = info.height;
    if (maxDimension == 0 || (info.width <= maxDimension && info.height <= maxDimension)) {
        return;
    }
    if (info.width == 0 || info.height == 0 || info.bgraPixels.size() < static_cast<size_t>(info.width) * info.height * 4) {
        return;
    }

    uint32_t dstW = info.width;
    uint32_t dstH = info.height;
    if (info.width >= info.height) {
        dstW = maxDimension;
        dstH = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(info.height) * maxDimension) / info.width));
    } else {
        dstH = maxDimension;
        dstW = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(info.width) * maxDimension) / info.height));
    }

    std::vector<uint8_t> scaled(static_cast<size_t>(dstW) * dstH * 4);
    for (uint32_t y = 0; y < dstH; ++y) {
        uint32_t srcY = std::min(info.height - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * info.height) / dstH));
        const uint8_t* srcRow = info.bgraPixels.data() + static_cast<size_t>(srcY) * info.width * 4;
        uint8_t* dstRow = scaled.data() + static_cast<size_t>(y) * dstW * 4;
        for (uint32_t x = 0; x < dstW; ++x) {
            uint32_t srcX = std::min(info.width - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * info.width) / dstW));
            const uint8_t* sp = srcRow + srcX * 4;
            uint8_t* dp = dstRow + x * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    info.width = dstW;
    info.height = dstH;
    info.bgraPixels = std::move(scaled);
}

namespace GpuTex {

using ::Explorer2::ByteIO::HalfToFloat;

inline float ReadF32LE(const uint8_t* p) {
    uint32_t u = ReadU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline void ConvertFloatRgbaToBgra(
    const float* rgba,
    uint32_t w,
    uint32_t h,
    bool forceHdrToneMap,
    std::vector<uint8_t>& outBgra)
{
    size_t count = static_cast<size_t>(w) * h;
    outBgra.resize(count * 4);
    bool isHdr = forceHdrToneMap;
    if (!isHdr) {
        for (size_t i = 0; i < count; ++i) {
            for (int c = 0; c < 3; ++c) {
                float v = rgba[i * 4 + c];
                if (std::isfinite(v) && v > 1.02f) {
                    isHdr = true;
                    break;
                }
            }
            if (isHdr) break;
        }
    }

    auto mapCh = [isHdr](float v) -> uint8_t {
        if (!std::isfinite(v)) return (v > 0.0f) ? 255 : 0;
        if (v <= 0.0f) return 0;
        if (isHdr) {
            float mapped = v / (1.0f + v);
            return static_cast<uint8_t>(std::clamp(std::pow(mapped, 1.0f / 2.2f) * 255.0f + 0.5f, 0.0f, 255.0f));
        }
        return static_cast<uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
    };

    for (size_t i = 0; i < count; ++i) {
        uint8_t r = mapCh(rgba[i * 4 + 0]);
        uint8_t g = mapCh(rgba[i * 4 + 1]);
        uint8_t b = mapCh(rgba[i * 4 + 2]);
        float af = rgba[i * 4 + 3];
        uint8_t a = (!std::isfinite(af)) ? 255 : static_cast<uint8_t>(std::clamp(af * 255.0f + 0.5f, 0.0f, 255.0f));
        WritePremulBGRA(&outBgra[i * 4], r, g, b, a);
    }
}

inline void UnpackRgb565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((c >> 11) & 31u) * 255u / 31u);
    g = static_cast<uint8_t>(((c >> 5)  & 63u) * 255u / 63u);
    b = static_cast<uint8_t>((c         & 31u) * 255u / 31u);
}

// Decodes an 8-byte BC1 / DXT1 block into 16 RGBA8 pixels (row-major 4x4).
// If allowDegenerate3Color is true (standard BC1/DXT1), c0 <= c1 uses 3-color + transparent/black mode;
// if opaqueAlpha is true (RGB DXT1), alpha is forced to 255 even in c0 <= c1 mode.
inline void DecodeBC1Block(const uint8_t* block, uint8_t outRgba[64], bool allowDegenerate3Color = true, bool opaqueAlpha = false) {
    uint16_t c0 = ReadU16LE(block);
    uint16_t c1 = ReadU16LE(block + 2);
    uint32_t bits = ReadU32LE(block + 4);

    uint8_t r[4], g[4], b[4], a[4] = { 255, 255, 255, 255 };
    UnpackRgb565(c0, r[0], g[0], b[0]);
    UnpackRgb565(c1, r[1], g[1], b[1]);
    if (c0 > c1 || !allowDegenerate3Color) {
        r[2] = static_cast<uint8_t>((2u * r[0] + r[1] + 1u) / 3u);
        g[2] = static_cast<uint8_t>((2u * g[0] + g[1] + 1u) / 3u);
        b[2] = static_cast<uint8_t>((2u * b[0] + b[1] + 1u) / 3u);
        r[3] = static_cast<uint8_t>((r[0] + 2u * r[1] + 1u) / 3u);
        g[3] = static_cast<uint8_t>((g[0] + 2u * g[1] + 1u) / 3u);
        b[3] = static_cast<uint8_t>((b[0] + 2u * b[1] + 1u) / 3u);
    } else {
        r[2] = static_cast<uint8_t>((static_cast<uint32_t>(r[0]) + r[1]) / 2u);
        g[2] = static_cast<uint8_t>((static_cast<uint32_t>(g[0]) + g[1]) / 2u);
        b[2] = static_cast<uint8_t>((static_cast<uint32_t>(b[0]) + b[1]) / 2u);
        r[3] = g[3] = b[3] = 0;
        a[3] = opaqueAlpha ? 255 : 0;
    }

    for (int i = 0; i < 16; ++i) {
        uint32_t code = (bits >> (2 * i)) & 3u;
        outRgba[i * 4 + 0] = r[code];
        outRgba[i * 4 + 1] = g[code];
        outRgba[i * 4 + 2] = b[code];
        outRgba[i * 4 + 3] = a[code];
    }
}

// Decodes an 8-byte BC4 (RGTC1) single-channel block into 16 8-bit values.
inline void DecodeBC4ChannelBlock(const uint8_t* block, uint8_t outCh[16], bool isSNorm = false) {
    uint8_t lut[8] = {};
    if (!isSNorm) {
        uint8_t e0 = block[0];
        uint8_t e1 = block[1];
        lut[0] = e0;
        lut[1] = e1;
        if (e0 > e1) {
            for (int i = 1; i <= 6; ++i) {
                lut[i + 1] = static_cast<uint8_t>(((7 - i) * e0 + i * e1 + 3) / 7);
            }
        } else {
            for (int i = 1; i <= 4; ++i) {
                lut[i + 1] = static_cast<uint8_t>(((5 - i) * e0 + i * e1 + 2) / 5);
            }
            lut[6] = 0;
            lut[7] = 255;
        }
    } else {
        int e0 = static_cast<int8_t>(block[0]);
        int e1 = static_cast<int8_t>(block[1]);
        auto toU8 = [](int s) -> uint8_t {
            int clamped = std::clamp(s, -127, 127);
            return static_cast<uint8_t>(((clamped + 127) * 255 + 127) / 254);
        };
        lut[0] = toU8(e0);
        lut[1] = toU8(e1);
        if (e0 > e1) {
            for (int i = 1; i <= 6; ++i) {
                lut[i + 1] = toU8(((7 - i) * e0 + i * e1) / 7);
            }
        } else {
            for (int i = 1; i <= 4; ++i) {
                lut[i + 1] = toU8(((5 - i) * e0 + i * e1) / 5);
            }
            lut[6] = 0;
            lut[7] = 255;
        }
    }

    uint64_t idxBits = 0;
    for (int i = 0; i < 6; ++i) {
        idxBits |= (static_cast<uint64_t>(block[2 + i]) << (8 * i));
    }
    for (int i = 0; i < 16; ++i) {
        outCh[i] = lut[(idxBits >> (3 * i)) & 7u];
    }
}

inline void DecodeBC2Block(const uint8_t* block, uint8_t outRgba[64]) {
    DecodeBC1Block(block + 8, outRgba, false, false);
    for (int i = 0; i < 8; ++i) {
        uint8_t v = block[i];
        outRgba[(i * 2 + 0) * 4 + 3] = static_cast<uint8_t>((v & 0x0Fu) * 17u);
        outRgba[(i * 2 + 1) * 4 + 3] = static_cast<uint8_t>((v >> 4) * 17u);
    }
}

inline void DecodeBC3Block(const uint8_t* block, uint8_t outRgba[64]) {
    uint8_t alphas[16];
    DecodeBC4ChannelBlock(block, alphas, false);
    DecodeBC1Block(block + 8, outRgba, false, false);
    for (int i = 0; i < 16; ++i) {
        outRgba[i * 4 + 3] = alphas[i];
    }
}

inline void DecodeBC4Block(const uint8_t* block, uint8_t outRgba[64], bool isSNorm = false) {
    uint8_t ch[16];
    DecodeBC4ChannelBlock(block, ch, isSNorm);
    for (int i = 0; i < 16; ++i) {
        outRgba[i * 4 + 0] = ch[i];
        outRgba[i * 4 + 1] = ch[i];
        outRgba[i * 4 + 2] = ch[i];
        outRgba[i * 4 + 3] = 255;
    }
}

inline void DecodeBC5Block(const uint8_t* block, uint8_t outRgba[64], bool isSNorm = false) {
    uint8_t rCh[16], gCh[16];
    DecodeBC4ChannelBlock(block, rCh, isSNorm);
    DecodeBC4ChannelBlock(block + 8, gCh, isSNorm);
    for (int i = 0; i < 16; ++i) {
        float nx = (static_cast<float>(rCh[i]) / 255.0f) * 2.0f - 1.0f;
        float ny = (static_cast<float>(gCh[i]) / 255.0f) * 2.0f - 1.0f;
        float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
        uint8_t b = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
        outRgba[i * 4 + 0] = rCh[i];
        outRgba[i * 4 + 1] = gCh[i];
        outRgba[i * 4 + 2] = b;
        outRgba[i * 4 + 3] = 255;
    }
}

// Khronos / Direct3D BC7 (BPTC UNORM) 16-byte block decoder supporting all 8 modes (0..7).
inline void DecodeBC7Block(const uint8_t* block, uint8_t outRgba[64]) {
    if (block[0] == 0) {
        std::memset(outRgba, 0, 64);
        return;
    }
    int mode = 0;
    while (mode < 8 && ((block[0] >> mode) & 1) == 0) ++mode;
    if (mode >= 8) {
        std::memset(outRgba, 0, 64);
        return;
    }

    struct ModeDesc {
        uint8_t subsets;
        uint8_t partBits;
        uint8_t rotBits;
        uint8_t idxModeBits;
        uint8_t colorBits;
        uint8_t alphaBits;
        uint8_t epPbits;     // 1 = per-endpoint P-bit, 2 = per-subset shared P-bit
        uint8_t idxBits0;
        uint8_t idxBits1;
    };
    static const ModeDesc kModes[8] = {
        { 3, 4, 0, 0, 4, 0, 1, 3, 0 }, // Mode 0
        { 2, 6, 0, 0, 6, 0, 2, 3, 0 }, // Mode 1
        { 3, 6, 0, 0, 5, 0, 0, 2, 0 }, // Mode 2
        { 2, 6, 0, 0, 7, 0, 1, 2, 0 }, // Mode 3
        { 1, 0, 2, 1, 5, 6, 0, 2, 3 }, // Mode 4
        { 1, 0, 2, 0, 7, 8, 0, 2, 2 }, // Mode 5
        { 1, 0, 0, 0, 7, 7, 1, 4, 0 }, // Mode 6
        { 2, 6, 0, 0, 5, 5, 1, 2, 0 }  // Mode 7
    };
    static const uint16_t kPart2[64] = {
        0xCCCC,0x8888,0xEEEE,0xECC8,0xC880,0xFEEC,0xFEC8,0xEC80,
        0xC800,0xFFEC,0xFE80,0xE800,0xFFE8,0xFF00,0xFFF0,0xF000,
        0xF710,0x008E,0x7100,0x08CE,0x008C,0x7310,0x3100,0x8CCE,
        0x088C,0x3110,0x6666,0x366C,0x17E8,0x0FF0,0x718E,0x399C,
        0xAAAA,0xF0F0,0x5A5A,0x33CC,0x3C3C,0x55AA,0x9696,0xA55A,
        0x73CE,0x13C8,0x324C,0x3BDC,0x6996,0xC33C,0x9966,0x0660,
        0x0272,0x04E4,0x4E40,0x2720,0xC936,0x936C,0x39C6,0x639C,
        0x9336,0x9CC6,0x817E,0xE718,0xCCF0,0x0FCC,0x7744,0xEE22
    };
    static const uint8_t kAnc2[64] = {
        15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15,
        15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2,
        15,15, 6, 8, 2, 8,15,15,  2, 8, 2, 2, 2,15,15, 6,
         6, 2, 6, 8,15,15, 2, 2, 15,15,15,15,15, 2, 2,15
    };
    static const uint32_t kPart3[64] = {
        0xAA685050,0x6A5A5040,0x5A5A4200,0x5450A0A8,0xA5A50000,0xA0A05050,0x5555A0A0,0x5A5A5050,
        0xAA550000,0xAA555500,0xAAAA5500,0x90909090,0x94949494,0xA4A4A4A4,0xA9A59450,0x2A0A4250,
        0xA5945040,0x0A425054,0xA5A5A500,0x55A0A0A0,0xA8A85454,0x6A6A4040,0xA4A45000,0x1A1A0500,
        0x0050A4A4,0xAAA59090,0x14696914,0x69691400,0xA08585A0,0xAA821414,0x50A4A450,0x6A5A0200,
        0xA9A58000,0x5090A0A8,0xA8A09050,0x24242424,0x00AA5500,0x24924924,0x24499224,0x50A50A50,
        0x500AA550,0xAAAA4444,0x66660000,0xA5A0A5A0,0x50A050A0,0x69286928,0x44AAAA44,0x66666600,
        0xAA444444,0x54A854A8,0x95809580,0x96969600,0xA85454A8,0x80959580,0xAA141414,0x96960000,
        0xAAAA1414,0xA05050A0,0xA0A5A5A0,0x96000000,0x40804080,0xA9A8A9A8,0xAAAAAA44,0x2A4A5254
    };
    static const uint8_t kAnc3a[64] = {
         3, 3,15,15, 8, 3,15,15,  8, 8, 6, 6, 6, 5, 3, 3,
         3, 3, 8,15, 3, 3, 6,10,  5, 8, 8, 6, 8, 5,15,15,
         8,15, 3, 5, 6,10, 8,15, 15, 3,15, 5,15,15,15,15,
         3,15, 5, 5, 5, 8, 5,10,  5,10, 8,13,15,12, 3, 3
    };
    static const uint8_t kAnc3b[64] = {
        15, 8, 8, 3,15,15, 3, 8, 15,15,15,15,15,15,15, 8,
        15, 8,15, 3,15, 8,15, 8,  3,15, 6,10,15,15,10, 8,
        15, 3,15,10,10, 8, 9,10,  6,15, 8,15, 3, 6, 6, 8,
        15, 3,15,15,15,15,15,15, 15,15,15,15, 3,15,15, 8
    };
    static const uint8_t kW2[4]  = { 0, 21, 43, 64 };
    static const uint8_t kW3[8]  = { 0, 9, 18, 27, 37, 46, 55, 64 };
    static const uint8_t kW4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

    uint64_t lo = ReadU64LE(block);
    uint64_t hi = ReadU64LE(block + 8);
    int bitPos = mode + 1;
    auto rdBits = [&](int n) -> uint32_t {
        if (n <= 0 || bitPos >= 128) return 0;
        uint32_t out = 0;
        for (int i = 0; i < n && bitPos < 128; ++i, ++bitPos) {
            uint32_t bit = (bitPos < 64) ? static_cast<uint32_t>((lo >> bitPos) & 1u)
                                         : static_cast<uint32_t>((hi >> (bitPos - 64)) & 1u);
            out |= (bit << i);
        }
        return out;
    };

    const ModeDesc& md = kModes[mode];
    uint32_t part = rdBits(md.partBits);
    uint32_t rot  = rdBits(md.rotBits);
    uint32_t idxMode = rdBits(md.idxModeBits);

    int numEp = md.subsets * 2;
    uint8_t ep[6][4] = {};
    for (int c = 0; c < 3; ++c) {
        for (int e = 0; e < numEp; ++e) ep[e][c] = static_cast<uint8_t>(rdBits(md.colorBits));
    }
    if (md.alphaBits > 0) {
        for (int e = 0; e < numEp; ++e) ep[e][3] = static_cast<uint8_t>(rdBits(md.alphaBits));
    }
    uint8_t pbits[6] = {};
    if (md.epPbits == 1) {
        for (int e = 0; e < numEp; ++e) pbits[e] = static_cast<uint8_t>(rdBits(1));
    } else if (md.epPbits == 2) {
        for (int s = 0; s < md.subsets; ++s) {
            uint8_t pb = static_cast<uint8_t>(rdBits(1));
            pbits[s * 2 + 0] = pb;
            pbits[s * 2 + 1] = pb;
        }
    }

    int cPrec = md.colorBits + (md.epPbits ? 1 : 0);
    int aPrec = md.alphaBits + ((md.alphaBits > 0 && md.epPbits) ? 1 : 0);
    auto expandBits = [](uint32_t v, int prec) -> uint8_t {
        v <<= (8 - prec);
        return static_cast<uint8_t>(v | (v >> prec));
    };
    for (int e = 0; e < numEp; ++e) {
        for (int c = 0; c < 3; ++c) {
            uint32_t v = md.epPbits ? ((static_cast<uint32_t>(ep[e][c]) << 1) | pbits[e]) : ep[e][c];
            ep[e][c] = expandBits(v, cPrec);
        }
        if (md.alphaBits > 0) {
            uint32_t v = md.epPbits ? ((static_cast<uint32_t>(ep[e][3]) << 1) | pbits[e]) : ep[e][3];
            ep[e][3] = expandBits(v, aPrec);
        } else {
            ep[e][3] = 255;
        }
    }

    uint8_t subsetOf[16] = {};
    int anc1 = -1, anc2 = -1;
    if (md.subsets == 2) {
        for (int i = 0; i < 16; ++i) subsetOf[i] = static_cast<uint8_t>((kPart2[part & 63] >> i) & 1u);
        anc1 = kAnc2[part & 63];
    } else if (md.subsets == 3) {
        for (int i = 0; i < 16; ++i) subsetOf[i] = static_cast<uint8_t>((kPart3[part & 63] >> (2 * i)) & 3u);
        anc1 = kAnc3a[part & 63];
        anc2 = kAnc3b[part & 63];
    }

    uint8_t idx0[16] = {}, idx1[16] = {};
    for (int i = 0; i < 16; ++i) {
        bool isAnchor = (i == 0 || i == anc1 || i == anc2);
        idx0[i] = static_cast<uint8_t>(rdBits(md.idxBits0 - (isAnchor ? 1 : 0)));
    }
    if (md.idxBits1 > 0) {
        for (int i = 0; i < 16; ++i) {
            idx1[i] = static_cast<uint8_t>(rdBits(md.idxBits1 - (i == 0 ? 1 : 0)));
        }
    }

    auto weightOf = [&](uint8_t idx, uint8_t bits) -> uint8_t {
        if (bits == 2) return kW2[idx & 3];
        if (bits == 3) return kW3[idx & 7];
        return kW4[idx & 15];
    };

    for (int i = 0; i < 16; ++i) {
        int s = subsetOf[i];
        const uint8_t* e0 = ep[s * 2 + 0];
        const uint8_t* e1 = ep[s * 2 + 1];
        uint8_t wc = 0, wa = 0;
        if (md.idxBits1 == 0) {
            wc = wa = weightOf(idx0[i], md.idxBits0);
        } else if (idxMode == 0) {
            wc = weightOf(idx0[i], md.idxBits0);
            wa = weightOf(idx1[i], md.idxBits1);
        } else {
            wc = weightOf(idx1[i], md.idxBits1);
            wa = weightOf(idx0[i], md.idxBits0);
        }
        uint8_t ch[4];
        for (int c = 0; c < 3; ++c) {
            ch[c] = static_cast<uint8_t>(((64 - wc) * e0[c] + wc * e1[c] + 32) >> 6);
        }
        ch[3] = static_cast<uint8_t>(((64 - wa) * e0[3] + wa * e1[3] + 32) >> 6);
        if (rot == 1) std::swap(ch[3], ch[0]);
        else if (rot == 2) std::swap(ch[3], ch[1]);
        else if (rot == 3) std::swap(ch[3], ch[2]);
        outRgba[i * 4 + 0] = ch[0];
        outRgba[i * 4 + 1] = ch[1];
        outRgba[i * 4 + 2] = ch[2];
        outRgba[i * 4 + 3] = ch[3];
    }
}

inline void UnpackR11G11B10Float(uint32_t packed, float& r, float& g, float& b) {
    auto decFloat = [](uint32_t exp, uint32_t mant, int mantBits) -> float {
        if (exp == 0) {
            if (mant == 0) return 0.0f;
            return std::ldexp(static_cast<float>(mant) / static_cast<float>(1u << mantBits), -14);
        }
        if (exp == 31) {
            return (mant == 0) ? std::numeric_limits<float>::infinity() : 0.0f;
        }
        return std::ldexp(1.0f + static_cast<float>(mant) / static_cast<float>(1u << mantBits), static_cast<int>(exp) - 15);
    };
    r = decFloat((packed >> 6)  & 0x1Fu, packed & 0x3Fu, 6);
    g = decFloat((packed >> 17) & 0x1Fu, (packed >> 11) & 0x3Fu, 6);
    b = decFloat((packed >> 27) & 0x1Fu, (packed >> 22) & 0x1Fu, 5);
}

inline void UnpackRgb9E5(uint32_t packed, float& r, float& g, float& b) {
    int exp = static_cast<int>((packed >> 27) & 0x1Fu) - 15 - 9;
    float scale = std::ldexp(1.0f, exp);
    r = static_cast<float>(packed & 0x1FFu) * scale;
    g = static_cast<float>((packed >> 9) & 0x1FFu) * scale;
    b = static_cast<float>((packed >> 18) & 0x1FFu) * scale;
}

// Khronos / Direct3D 11 BC6H (BPTC_FLOAT UF16 & SF16) 16-byte block decoder supporting all 14 modes.
inline void DecodeBC6HBlock(const uint8_t* block, float outRgbaFloat[64], bool isSigned) {
    auto getBit = [&](int bitIdx) -> uint32_t {
        return (block[bitIdx >> 3] >> (bitIdx & 7)) & 1u;
    };
    auto getBits = [&](int startBit, int count) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) v |= (getBit(startBit + i) << i);
        return v;
    };
    auto setBit = [](int& dst, int bitPos, uint32_t bitVal) {
        dst |= (static_cast<int>(bitVal & 1u) << bitPos);
    };
    auto setRange = [&](int& dst, int dstLo, int srcBit, int count) {
        for (int i = 0; i < count; ++i) setBit(dst, dstLo + i, getBit(srcBit + i));
    };
    auto signExtend = [](int v, int bits) -> int {
        int mask = 1 << (bits - 1);
        return ((v & ((1 << bits) - 1)) ^ mask) - mask;
    };

    uint32_t modeId = ((block[0] & 0x02u) == 0) ? (block[0] & 0x03u) : (block[0] & 0x1Fu);
    int rw = 0, gw = 0, bw = 0, rx = 0, gx = 0, bx = 0;
    int ry = 0, gy = 0, by = 0, rz = 0, gz = 0, bz = 0;
    int subsets = 2;
    bool transformed = true;
    int baseBitsR = 10, baseBitsG = 10, baseBitsB = 10;
    int dBitsR = 5, dBitsG = 5, dBitsB = 5;

    switch (modeId) {
        case 0x00: // Mode 1 (2-subset): 10 base + 5,5,5 delta
            baseBitsR = baseBitsG = baseBitsB = 10; dBitsR = dBitsG = dBitsB = 5;
            setBit(gy, 4, getBit(2)); setBit(by, 4, getBit(3)); setBit(bz, 4, getBit(4));
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 5); setBit(gz, 4, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 5); setBit(bz, 0, getBit(50)); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 5); setBit(bz, 1, getBit(60)); setRange(by, 0, 61, 4);
            setRange(ry, 0, 65, 5); setBit(bz, 2, getBit(70)); setRange(rz, 0, 71, 5); setBit(bz, 3, getBit(76));
            break;
        case 0x01: // Mode 2 (2-subset): 7 base + 6,6,6 delta
            baseBitsR = baseBitsG = baseBitsB = 7; dBitsR = dBitsG = dBitsB = 6;
            setBit(gy, 5, getBit(2)); setBit(gz, 4, getBit(3)); setBit(gz, 5, getBit(4));
            setRange(rw, 0, 5, 7); setBit(bz, 0, getBit(12)); setBit(bz, 1, getBit(13)); setBit(by, 4, getBit(14));
            setRange(gw, 0, 15, 7); setBit(by, 5, getBit(22)); setBit(bz, 2, getBit(23)); setBit(gy, 4, getBit(24));
            setRange(bw, 0, 25, 7); setBit(bz, 3, getBit(32)); setBit(bz, 5, getBit(33)); setBit(bz, 4, getBit(34));
            setRange(rx, 0, 35, 6); setRange(gy, 0, 41, 4); setRange(gx, 0, 45, 6); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 6); setRange(by, 0, 61, 4); setRange(ry, 0, 65, 6); setRange(rz, 0, 71, 6);
            break;
        case 0x02: // Mode 3 (2-subset): 11 base + 5,4,4 delta
            baseBitsR = baseBitsG = baseBitsB = 11; dBitsR = 5; dBitsG = 4; dBitsB = 4;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 5); setBit(rw, 10, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 4); setBit(gw, 10, getBit(49)); setBit(bz, 0, getBit(50)); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 4); setBit(bw, 10, getBit(59)); setBit(bz, 1, getBit(60)); setRange(by, 0, 61, 4);
            setRange(ry, 0, 65, 5); setBit(bz, 2, getBit(70)); setRange(rz, 0, 71, 5); setBit(bz, 3, getBit(76));
            break;
        case 0x06: // Mode 4 (2-subset): 11 base + 4,5,4 delta
            baseBitsR = baseBitsG = baseBitsB = 11; dBitsR = 4; dBitsG = 5; dBitsB = 4;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 4); setBit(rw, 10, getBit(39)); setBit(gz, 4, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 5); setBit(gw, 10, getBit(50)); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 4); setBit(bw, 10, getBit(59)); setBit(bz, 1, getBit(60)); setRange(by, 0, 61, 4);
            setRange(ry, 0, 65, 4); setBit(bz, 0, getBit(69)); setBit(bz, 2, getBit(70));
            setRange(rz, 0, 71, 4); setBit(gy, 4, getBit(75)); setBit(bz, 3, getBit(76));
            break;
        case 0x0A: // Mode 5 (2-subset): 11 base + 4,4,5 delta
            baseBitsR = baseBitsG = baseBitsB = 11; dBitsR = 4; dBitsG = 4; dBitsB = 5;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 4); setBit(rw, 10, getBit(39)); setBit(by, 4, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 4); setBit(gw, 10, getBit(49)); setBit(bz, 0, getBit(50)); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 5); setBit(bw, 10, getBit(60)); setRange(by, 0, 61, 4);
            setRange(ry, 0, 65, 4); setBit(bz, 1, getBit(69)); setBit(bz, 2, getBit(70));
            setRange(rz, 0, 71, 4); setBit(bz, 4, getBit(75)); setBit(bz, 3, getBit(76));
            break;
        case 0x0E: // Mode 6 (2-subset): 9 base + 5,5,5 delta
            baseBitsR = baseBitsG = baseBitsB = 9; dBitsR = dBitsG = dBitsB = 5;
            setRange(rw, 0, 5, 9); setBit(by, 4, getBit(14));
            setRange(gw, 0, 15, 9); setBit(gy, 4, getBit(24));
            setRange(bw, 0, 25, 9); setBit(bz, 4, getBit(34));
            setRange(rx, 0, 35, 5); setBit(gz, 4, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 5); setBit(bz, 0, getBit(50)); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 5); setBit(bz, 1, getBit(60)); setRange(by, 0, 61, 4);
            setRange(ry, 0, 65, 5); setBit(bz, 2, getBit(70)); setRange(rz, 0, 71, 5); setBit(bz, 3, getBit(76));
            break;
        case 0x12: // Mode 7 (2-subset): 8 base + 6,5,5 delta
            baseBitsR = baseBitsG = baseBitsB = 8; dBitsR = 6; dBitsG = 5; dBitsB = 5;
            setRange(rw, 0, 5, 8); setBit(gz, 4, getBit(13)); setBit(by, 4, getBit(14));
            setRange(gw, 0, 15, 8); setBit(bz, 2, getBit(23)); setBit(gy, 4, getBit(24));
            setRange(bw, 0, 25, 8); setBit(bz, 3, getBit(33)); setBit(bz, 4, getBit(34));
            setRange(rx, 0, 35, 6); setRange(gy, 0, 41, 4); setRange(gx, 0, 45, 5); setBit(bz, 0, getBit(50));
            setRange(gz, 0, 51, 4); setRange(bx, 0, 55, 5); setBit(bz, 1, getBit(60)); setRange(by, 0, 61, 4);
            setRange(ry, 0, 65, 6); setRange(rz, 0, 71, 6);
            break;
        case 0x16: // Mode 8 (2-subset): 8 base + 5,6,5 delta
            baseBitsR = baseBitsG = baseBitsB = 8; dBitsR = 5; dBitsG = 6; dBitsB = 5;
            setRange(rw, 0, 5, 8); setBit(bz, 0, getBit(13)); setBit(by, 4, getBit(14));
            setRange(gw, 0, 15, 8); setBit(gy, 5, getBit(23)); setBit(gy, 4, getBit(24));
            setRange(bw, 0, 25, 8); setBit(gz, 5, getBit(33)); setBit(bz, 4, getBit(34));
            setRange(rx, 0, 35, 5); setBit(gz, 4, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 6); setRange(gz, 0, 51, 4); setRange(bx, 0, 55, 5); setBit(bz, 1, getBit(60));
            setRange(by, 0, 61, 4); setRange(ry, 0, 65, 5); setBit(bz, 2, getBit(70)); setRange(rz, 0, 71, 5); setBit(bz, 3, getBit(76));
            break;
        case 0x1A: // Mode 9 (2-subset): 8 base + 5,5,6 delta
            baseBitsR = baseBitsG = baseBitsB = 8; dBitsR = 5; dBitsG = 5; dBitsB = 6;
            setRange(rw, 0, 5, 8); setBit(bz, 1, getBit(13)); setBit(by, 4, getBit(14));
            setRange(gw, 0, 15, 8); setBit(by, 5, getBit(23)); setBit(gy, 4, getBit(24));
            setRange(bw, 0, 25, 8); setBit(bz, 5, getBit(33)); setBit(bz, 4, getBit(34));
            setRange(rx, 0, 35, 5); setBit(gz, 4, getBit(40)); setRange(gy, 0, 41, 4);
            setRange(gx, 0, 45, 5); setBit(bz, 0, getBit(50)); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 6); setRange(by, 0, 61, 4); setRange(ry, 0, 65, 5); setBit(bz, 2, getBit(70)); setRange(rz, 0, 71, 5); setBit(bz, 3, getBit(76));
            break;
        case 0x1E: // Mode 10 (2-subset): 6,6,6 untransformed
            baseBitsR = baseBitsG = baseBitsB = 6; transformed = false;
            setRange(rw, 0, 5, 6); setBit(gz, 4, getBit(11)); setBit(bz, 0, getBit(12)); setBit(bz, 1, getBit(13)); setBit(by, 4, getBit(14));
            setRange(gw, 0, 15, 6); setBit(gy, 5, getBit(21)); setBit(by, 5, getBit(22)); setBit(bz, 2, getBit(23)); setBit(gy, 4, getBit(24));
            setRange(bw, 0, 25, 6); setBit(gz, 5, getBit(31)); setBit(bz, 3, getBit(32)); setBit(bz, 5, getBit(33)); setBit(bz, 4, getBit(34));
            setRange(rx, 0, 35, 6); setRange(gy, 0, 41, 4); setRange(gx, 0, 45, 6); setRange(gz, 0, 51, 4);
            setRange(bx, 0, 55, 6); setRange(by, 0, 61, 4); setRange(ry, 0, 65, 6); setRange(rz, 0, 71, 6);
            break;
        case 0x03: // Mode 11 (1-subset): 10,10,10 untransformed
            subsets = 1; baseBitsR = baseBitsG = baseBitsB = 10; transformed = false;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 10); setRange(gx, 0, 45, 10); setRange(bx, 0, 55, 10);
            break;
        case 0x07: // Mode 12 (1-subset): 11 base + 9,9,9 delta
            subsets = 1; baseBitsR = baseBitsG = baseBitsB = 11; dBitsR = dBitsG = dBitsB = 9;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 9); setBit(rw, 10, getBit(44));
            setRange(gx, 0, 45, 9); setBit(gw, 10, getBit(54));
            setRange(bx, 0, 55, 9); setBit(bw, 10, getBit(64));
            break;
        case 0x0B: // Mode 13 (1-subset): 12 base + 8,8,8 delta
            subsets = 1; baseBitsR = baseBitsG = baseBitsB = 12; dBitsR = dBitsG = dBitsB = 8;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 8); setBit(rw, 11, getBit(43)); setBit(rw, 10, getBit(44));
            setRange(gx, 0, 45, 8); setBit(gw, 11, getBit(53)); setBit(gw, 10, getBit(54));
            setRange(bx, 0, 55, 8); setBit(bw, 11, getBit(63)); setBit(bw, 10, getBit(64));
            break;
        case 0x0F: // Mode 14 (1-subset): 16 base + 4,4,4 delta
            subsets = 1; baseBitsR = baseBitsG = baseBitsB = 16; dBitsR = dBitsG = dBitsB = 4;
            setRange(rw, 0, 5, 10); setRange(gw, 0, 15, 10); setRange(bw, 0, 25, 10);
            setRange(rx, 0, 35, 4); for (int k = 0; k < 6; ++k) setBit(rw, 15 - k, getBit(39 + k));
            setRange(gx, 0, 45, 4); for (int k = 0; k < 6; ++k) setBit(gw, 15 - k, getBit(49 + k));
            setRange(bx, 0, 55, 4); for (int k = 0; k < 6; ++k) setBit(bw, 15 - k, getBit(59 + k));
            break;
        default: // Reserved BC6H mode (0x13, 0x17, 0x1B, 0x1F)
            for (int i = 0; i < 16; ++i) {
                outRgbaFloat[i * 4 + 0] = 0.0f;
                outRgbaFloat[i * 4 + 1] = 0.0f;
                outRgbaFloat[i * 4 + 2] = 0.0f;
                outRgbaFloat[i * 4 + 3] = 1.0f;
            }
            return;
    }

    if (isSigned) {
        rw = signExtend(rw, baseBitsR); gw = signExtend(gw, baseBitsG); bw = signExtend(bw, baseBitsB);
    }
    if (transformed) {
        rx = signExtend(rx, dBitsR); gx = signExtend(gx, dBitsG); bx = signExtend(bx, dBitsB);
        int mR = (1 << baseBitsR) - 1, mG = (1 << baseBitsG) - 1, mB = (1 << baseBitsB) - 1;
        rx = (rw + rx) & mR; gx = (gw + gx) & mG; bx = (bw + bx) & mB;
        if (isSigned) { rx = signExtend(rx, baseBitsR); gx = signExtend(gx, baseBitsG); bx = signExtend(bx, baseBitsB); }
        if (subsets == 2) {
            ry = signExtend(ry, dBitsR); gy = signExtend(gy, dBitsG); by = signExtend(by, dBitsB);
            rz = signExtend(rz, dBitsR); gz = signExtend(gz, dBitsG); bz = signExtend(bz, dBitsB);
            ry = (rw + ry) & mR; gy = (gw + gy) & mG; by = (bw + by) & mB;
            rz = (rw + rz) & mR; gz = (gw + gz) & mG; bz = (bw + bz) & mB;
            if (isSigned) {
                ry = signExtend(ry, baseBitsR); gy = signExtend(gy, baseBitsG); by = signExtend(by, baseBitsB);
                rz = signExtend(rz, baseBitsR); gz = signExtend(gz, baseBitsG); bz = signExtend(bz, baseBitsB);
            }
        }
    } else if (isSigned) {
        rx = signExtend(rx, baseBitsR); gx = signExtend(gx, baseBitsG); bx = signExtend(bx, baseBitsB);
        if (subsets == 2) {
            ry = signExtend(ry, baseBitsR); gy = signExtend(gy, baseBitsG); by = signExtend(by, baseBitsB);
            rz = signExtend(rz, baseBitsR); gz = signExtend(gz, baseBitsG); bz = signExtend(bz, baseBitsB);
        }
    }

    auto unquant = [&](int val, int bits) -> int {
        if (!isSigned) {
            if (bits >= 15) return val;
            if (val <= 0) return 0;
            if (val >= ((1 << bits) - 1)) return 0xFFFF;
            return ((val << 16) + 0x8000) >> bits;
        } else {
            if (bits >= 16) return val;
            bool neg = (val < 0);
            int av = neg ? -val : val;
            int u = (av == 0) ? 0 : (av >= ((1 << (bits - 1)) - 1)) ? 0x7FFF : (((av << 15) + 0x4000) >> (bits - 1));
            return neg ? -u : u;
        }
    };

    int ep[4][3] = {
        { unquant(rw, baseBitsR), unquant(gw, baseBitsG), unquant(bw, baseBitsB) },
        { unquant(rx, baseBitsR), unquant(gx, baseBitsG), unquant(bx, baseBitsB) },
        { unquant(ry, baseBitsR), unquant(gy, baseBitsG), unquant(by, baseBitsB) },
        { unquant(rz, baseBitsR), unquant(gz, baseBitsG), unquant(bz, baseBitsB) }
    };

    static const uint16_t kPart2[32] = {
        0xCCCC,0x8888,0xEEEE,0xECC8,0xC880,0xFEEC,0xFEC8,0xEC80,
        0xC800,0xFFEC,0xFE80,0xE800,0xFFE8,0xFF00,0xFFF0,0xF000,
        0xF710,0x008E,0x7100,0x08CE,0x008C,0x7310,0x3100,0x8CCE,
        0x088C,0x3110,0x6666,0x366C,0x17E8,0x0FF0,0x718E,0x399C
    };
    static const uint8_t kAnc2[32] = {
        15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15,
        15, 2, 8, 2, 2, 8, 8,15,  2, 8, 2, 2, 8, 8, 2, 2
    };
    static const int kW3[8]  = { 0, 9, 18, 27, 37, 46, 55, 64 };
    static const int kW4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

    uint32_t part = (subsets == 2) ? (getBits(77, 5) & 31u) : 0u;
    int idxCursor = (subsets == 2) ? 82 : 65;
    int anc1 = (subsets == 2) ? kAnc2[part] : -1;

    for (int i = 0; i < 16; ++i) {
        int sub = (subsets == 2) ? static_cast<int>((kPart2[part] >> i) & 1u) : 0;
        bool isAnc = (i == 0 || i == anc1);
        int ibits = ((subsets == 2) ? 3 : 4) - (isAnc ? 1 : 0);
        uint32_t idx = getBits(idxCursor, ibits);
        idxCursor += ibits;
        int w = (subsets == 2) ? kW3[idx & 7u] : kW4[idx & 15u];

        for (int c = 0; c < 3; ++c) {
            int comp = ((64 - w) * ep[sub * 2 + 0][c] + w * ep[sub * 2 + 1][c] + 32) >> 6;
            uint16_t h16 = 0;
            if (!isSigned) {
                uint32_t u = static_cast<uint32_t>(std::max(0, comp));
                h16 = static_cast<uint16_t>(std::min<uint32_t>(0x7BFFu, (u * 31u) >> 6));
            } else {
                bool neg = (comp < 0);
                uint32_t u = static_cast<uint32_t>(neg ? -comp : comp);
                uint32_t mag = std::min<uint32_t>(0x7BFFu, (u * 31u) >> 5);
                h16 = static_cast<uint16_t>((neg ? 0x8000u : 0u) | mag);
            }
            outRgbaFloat[i * 4 + c] = HalfToFloat(h16);
        }
        outRgbaFloat[i * 4 + 3] = 1.0f;
    }
}

// Decodes an 8-byte ETC1 or ETC2 RGB8 block into 16 RGBA8 pixels (row-major 4x4).
inline void DecodeETC2RgbBlock(const uint8_t* ptr, uint8_t outRgba[64], bool allowEtc2Modes) {
    static const int kEtcMod[8][4] = {
        { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
        { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
    };
    static const int kEtc2Dist[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

    bool diffBit = (ptr[3] & 2) != 0;
    bool flipBit = (ptr[3] & 1) != 0;
    uint16_t msb = ReadU16BE(ptr + 4);
    uint16_t lsb = ReadU16BE(ptr + 6);

    if (diffBit && allowEtc2Modes) {
        auto s3 = [](int v) { return (v >= 4) ? (v - 8) : v; };
        int sumR = ((ptr[0] >> 3) & 31) + s3(ptr[0] & 7);
        int sumG = ((ptr[1] >> 3) & 31) + s3(ptr[1] & 7);
        int sumB = ((ptr[2] >> 3) & 31) + s3(ptr[2] & 7);

        if (sumR < 0 || sumR > 31) {
            // ETC2 T-mode
            int r1 = (((ptr[0] >> 1) & 0xC) | (ptr[0] & 0x3)) * 17;
            int g1 = ((ptr[1] >> 4) & 0xF) * 17;
            int b1 = (ptr[1] & 0xF) * 17;
            int r2 = ((ptr[2] >> 4) & 0xF) * 17;
            int g2 = (ptr[2] & 0xF) * 17;
            int b2 = ((ptr[3] >> 4) & 0xF) * 17;
            int d  = kEtc2Dist[((ptr[3] >> 1) & 0x6) | (ptr[3] & 0x1)];
            int pr[4] = { r1, std::clamp(r2 + d, 0, 255), r2, std::clamp(r2 - d, 0, 255) };
            int pg[4] = { g1, std::clamp(g2 + d, 0, 255), g2, std::clamp(g2 - d, 0, 255) };
            int pb[4] = { b1, std::clamp(b2 + d, 0, 255), b2, std::clamp(b2 - d, 0, 255) };
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    int bitIdx = px * 4 + py;
                    int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
                    int di = (py * 4 + px) * 4;
                    outRgba[di + 0] = static_cast<uint8_t>(pr[idx]);
                    outRgba[di + 1] = static_cast<uint8_t>(pg[idx]);
                    outRgba[di + 2] = static_cast<uint8_t>(pb[idx]);
                    outRgba[di + 3] = 255;
                }
            }
            return;
        }
        if (sumG < 0 || sumG > 31) {
            // ETC2 H-mode
            int r1 = ((ptr[0] >> 3) & 0xF) * 17;
            int g1 = (((ptr[0] & 0x7) << 1) | ((ptr[1] >> 4) & 1)) * 17;
            int b1 = ((ptr[1] & 0x8) | ((ptr[1] & 0x3) << 1) | ((ptr[2] >> 7) & 1)) * 17;
            int r2 = ((ptr[2] >> 3) & 0xF) * 17;
            int g2 = (((ptr[2] & 0x7) << 1) | ((ptr[3] >> 7) & 1)) * 17;
            int b2 = ((ptr[3] >> 3) & 0xF) * 17;
            uint32_t v1 = (static_cast<uint32_t>(r1) << 16) | (static_cast<uint32_t>(g1) << 8) | static_cast<uint32_t>(b1);
            uint32_t v2 = (static_cast<uint32_t>(r2) << 16) | (static_cast<uint32_t>(g2) << 8) | static_cast<uint32_t>(b2);
            int d = kEtc2Dist[(ptr[3] & 4) | ((ptr[3] & 1) << 1) | (v1 >= v2 ? 1 : 0)];
            int pr[4] = { std::clamp(r1 + d, 0, 255), std::clamp(r1 - d, 0, 255), std::clamp(r2 + d, 0, 255), std::clamp(r2 - d, 0, 255) };
            int pg[4] = { std::clamp(g1 + d, 0, 255), std::clamp(g1 - d, 0, 255), std::clamp(g2 + d, 0, 255), std::clamp(g2 - d, 0, 255) };
            int pb[4] = { std::clamp(b1 + d, 0, 255), std::clamp(b1 - d, 0, 255), std::clamp(b2 + d, 0, 255), std::clamp(b2 - d, 0, 255) };
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    int bitIdx = px * 4 + py;
                    int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
                    int di = (py * 4 + px) * 4;
                    outRgba[di + 0] = static_cast<uint8_t>(pr[idx]);
                    outRgba[di + 1] = static_cast<uint8_t>(pg[idx]);
                    outRgba[di + 2] = static_cast<uint8_t>(pb[idx]);
                    outRgba[di + 3] = 255;
                }
            }
            return;
        }
        if (sumB < 0 || sumB > 31) {
            // ETC2 Planar mode
            uint64_t u = ReadU64BE(ptr);
            auto exp6 = [](uint64_t v) { int x = static_cast<int>(v & 0x3Fu); return (x << 2) | (x >> 4); };
            auto exp7 = [](uint64_t v) { int x = static_cast<int>(v & 0x7Fu); return (x << 1) | (x >> 6); };
            int ro = exp6(u >> 57);
            int go = exp7((((u >> 56) & 1u) << 6) | ((u >> 49) & 0x3Fu));
            int bo = exp6((((u >> 48) & 1u) << 5) | (((u >> 43) & 3u) << 3) | ((u >> 39) & 7u));
            int rh = exp6((((u >> 34) & 0x1Fu) << 1) | ((u >> 32) & 1u));
            int gh = exp7(u >> 25);
            int bh = exp6(u >> 19);
            int rv = exp6(u >> 13);
            int gv = exp7(u >> 6);
            int bv = exp6(u);
            for (int py = 0; py < 4; ++py) {
                for (int px = 0; px < 4; ++px) {
                    int di = (py * 4 + px) * 4;
                    outRgba[di + 0] = static_cast<uint8_t>(std::clamp((px * (rh - ro) + py * (rv - ro) + 4 * ro + 2) >> 2, 0, 255));
                    outRgba[di + 1] = static_cast<uint8_t>(std::clamp((px * (gh - go) + py * (gv - go) + 4 * go + 2) >> 2, 0, 255));
                    outRgba[di + 2] = static_cast<uint8_t>(std::clamp((px * (bh - bo) + py * (bv - bo) + 4 * bo + 2) >> 2, 0, 255));
                    outRgba[di + 3] = 255;
                }
            }
            return;
        }
    }

    int r1, g1, b1, r2, g2, b2;
    if (diffBit) {
        auto s3 = [](int v) { return (v >= 4) ? (v - 8) : v; };
        int br1 = (ptr[0] >> 3) & 31, bg1 = (ptr[1] >> 3) & 31, bb1 = (ptr[2] >> 3) & 31;
        int br2 = std::clamp(br1 + s3(ptr[0] & 7), 0, 31);
        int bg2 = std::clamp(bg1 + s3(ptr[1] & 7), 0, 31);
        int bb2 = std::clamp(bb1 + s3(ptr[2] & 7), 0, 31);
        r1 = (br1 << 3) | (br1 >> 2); g1 = (bg1 << 3) | (bg1 >> 2); b1 = (bb1 << 3) | (bb1 >> 2);
        r2 = (br2 << 3) | (br2 >> 2); g2 = (bg2 << 3) | (bg2 >> 2); b2 = (bb2 << 3) | (bb2 >> 2);
    } else {
        r1 = ((ptr[0] & 0xF0) | (ptr[0] >> 4)); r2 = ((ptr[0] & 0x0F) | ((ptr[0] & 0x0F) << 4));
        g1 = ((ptr[1] & 0xF0) | (ptr[1] >> 4)); g2 = ((ptr[1] & 0x0F) | ((ptr[1] & 0x0F) << 4));
        b1 = ((ptr[2] & 0xF0) | (ptr[2] >> 4)); b2 = ((ptr[2] & 0x0F) | ((ptr[2] & 0x0F) << 4));
    }
    int cw1 = (ptr[3] >> 5) & 7, cw2 = (ptr[3] >> 2) & 7;
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            int bitIdx = px * 4 + py;
            int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
            bool sub2 = flipBit ? (py >= 2) : (px >= 2);
            int mod = kEtcMod[sub2 ? cw2 : cw1][idx];
            int di = (py * 4 + px) * 4;
            outRgba[di + 0] = static_cast<uint8_t>(std::clamp((sub2 ? r2 : r1) + mod, 0, 255));
            outRgba[di + 1] = static_cast<uint8_t>(std::clamp((sub2 ? g2 : g1) + mod, 0, 255));
            outRgba[di + 2] = static_cast<uint8_t>(std::clamp((sub2 ? b2 : b1) + mod, 0, 255));
            outRgba[di + 3] = 255;
        }
    }
}

inline void DecodeETC1Block(const uint8_t* block, uint8_t outRgba[64]) {
    DecodeETC2RgbBlock(block, outRgba, false);
}

inline void DecodeETC2Block(const uint8_t* block, uint8_t outRgba[64], bool hasEacAlpha) {
    if (!hasEacAlpha) {
        DecodeETC2RgbBlock(block, outRgba, true);
        return;
    }
    static const int kEacMod[16][8] = {
        {-3,-6,-9,-15,2,5,8,14}, {-3,-7,-10,-13,2,6,9,12}, {-2,-5,-8,-13,1,4,7,12}, {-2,-4,-6,-13,1,3,5,12},
        {-3,-6,-8,-12,2,5,7,11}, {-3,-7,-9,-11,2,6,8,10}, {-4,-7,-8,-11,3,6,7,10}, {-3,-5,-8,-11,2,4,7,10},
        {-2,-6,-8,-10,1,5,7,9},  {-2,-5,-8,-10,1,4,7,9},  {-2,-4,-8,-10,1,3,7,9},  {-2,-5,-7,-10,1,4,6,9},
        {-3,-4,-7,-10,2,3,6,9},  {-1,-2,-3,-10,0,1,2,9},  {-4,-6,-8,-9,3,5,7,8},   {-3,-5,-7,-9,2,4,6,8}
    };
    DecodeETC2RgbBlock(block + 8, outRgba, true);
    int base = block[0];
    int mult = (block[1] >> 4) & 0xF;
    int tbl  = block[1] & 0xF;
    uint64_t bits48 = 0;
    for (int i = 0; i < 6; ++i) bits48 = (bits48 << 8) | block[2 + i];
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            int pixIdx = px * 4 + py;
            int code = static_cast<int>((bits48 >> (45 - 3 * pixIdx)) & 7u);
            int a = std::clamp(base + kEacMod[tbl][code] * mult, 0, 255);
            outRgba[(py * 4 + px) * 4 + 3] = static_cast<uint8_t>(a);
        }
    }
}

// Decodes an 8-byte ETC2 RGB8_PUNCHTHROUGH_ALPHA1 block (OpenGL ES 3.0 Appendix C.2.3).
inline void DecodeETC2RgbA1Block(const uint8_t* ptr, uint8_t outRgba[64]) {
    static const int kEtcMod[8][4] = {
        { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
        { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
    };
    static const int kEtc2Dist[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

    const bool opaque  = (ptr[3] & 2) != 0;
    const bool flipBit = (ptr[3] & 1) != 0;
    const uint16_t msb = ReadU16BE(ptr + 4);
    const uint16_t lsb = ReadU16BE(ptr + 6);

    auto s3 = [](int v) { return (v >= 4) ? (v - 8) : v; };
    const int sumR = ((ptr[0] >> 3) & 31) + s3(ptr[0] & 7);
    const int sumG = ((ptr[1] >> 3) & 31) + s3(ptr[1] & 7);
    const int sumB = ((ptr[2] >> 3) & 31) + s3(ptr[2] & 7);

    if (sumR < 0 || sumR > 31) {
        // T-mode with punchthrough alpha
        int r1 = (((ptr[0] >> 1) & 0xC) | (ptr[0] & 0x3)) * 17;
        int g1 = ((ptr[1] >> 4) & 0xF) * 17;
        int b1 = (ptr[1] & 0xF) * 17;
        int r2 = ((ptr[2] >> 4) & 0xF) * 17;
        int g2 = (ptr[2] & 0xF) * 17;
        int b2 = ((ptr[3] >> 4) & 0xF) * 17;
        int d  = kEtc2Dist[((ptr[3] >> 1) & 0x6) | (ptr[3] & 0x1)];
        int pr[4] = { r1, std::clamp(r2 + d, 0, 255), r2, std::clamp(r2 - d, 0, 255) };
        int pg[4] = { g1, std::clamp(g2 + d, 0, 255), g2, std::clamp(g2 - d, 0, 255) };
        int pb[4] = { b1, std::clamp(b2 + d, 0, 255), b2, std::clamp(b2 - d, 0, 255) };
        for (int py = 0; py < 4; ++py) {
            for (int px = 0; px < 4; ++px) {
                int bitIdx = px * 4 + py;
                int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
                int di = (py * 4 + px) * 4;
                if (!opaque && idx == 2) {
                    outRgba[di + 0] = outRgba[di + 1] = outRgba[di + 2] = outRgba[di + 3] = 0;
                } else {
                    outRgba[di + 0] = static_cast<uint8_t>(pr[idx]);
                    outRgba[di + 1] = static_cast<uint8_t>(pg[idx]);
                    outRgba[di + 2] = static_cast<uint8_t>(pb[idx]);
                    outRgba[di + 3] = 255;
                }
            }
        }
        return;
    }
    if (sumG < 0 || sumG > 31) {
        // H-mode with punchthrough alpha
        int r1 = ((ptr[0] >> 3) & 0xF) * 17;
        int g1 = (((ptr[0] & 0x7) << 1) | ((ptr[1] >> 4) & 1)) * 17;
        int b1 = ((ptr[1] & 0x8) | ((ptr[1] & 0x3) << 1) | ((ptr[2] >> 7) & 1)) * 17;
        int r2 = ((ptr[2] >> 3) & 0xF) * 17;
        int g2 = (((ptr[2] & 0x7) << 1) | ((ptr[3] >> 7) & 1)) * 17;
        int b2 = ((ptr[3] >> 3) & 0xF) * 17;
        uint32_t v1 = (static_cast<uint32_t>(r1) << 16) | (static_cast<uint32_t>(g1) << 8) | static_cast<uint32_t>(b1);
        uint32_t v2 = (static_cast<uint32_t>(r2) << 16) | (static_cast<uint32_t>(g2) << 8) | static_cast<uint32_t>(b2);
        int d = kEtc2Dist[(ptr[3] & 4) | ((ptr[3] & 1) << 1) | (v1 >= v2 ? 1 : 0)];
        int pr[4] = { std::clamp(r1 + d, 0, 255), std::clamp(r1 - d, 0, 255), std::clamp(r2 + d, 0, 255), std::clamp(r2 - d, 0, 255) };
        int pg[4] = { std::clamp(g1 + d, 0, 255), std::clamp(g1 - d, 0, 255), std::clamp(g2 + d, 0, 255), std::clamp(g2 - d, 0, 255) };
        int pb[4] = { std::clamp(b1 + d, 0, 255), std::clamp(b1 - d, 0, 255), std::clamp(b2 + d, 0, 255), std::clamp(b2 - d, 0, 255) };
        for (int py = 0; py < 4; ++py) {
            for (int px = 0; px < 4; ++px) {
                int bitIdx = px * 4 + py;
                int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
                int di = (py * 4 + px) * 4;
                if (!opaque && idx == 2) {
                    outRgba[di + 0] = outRgba[di + 1] = outRgba[di + 2] = outRgba[di + 3] = 0;
                } else {
                    outRgba[di + 0] = static_cast<uint8_t>(pr[idx]);
                    outRgba[di + 1] = static_cast<uint8_t>(pg[idx]);
                    outRgba[di + 2] = static_cast<uint8_t>(pb[idx]);
                    outRgba[di + 3] = 255;
                }
            }
        }
        return;
    }
    if (sumB < 0 || sumB > 31) {
        // Planar mode (always opaque per OpenGL ES 3.0 Appendix C.2.3)
        uint64_t u = ReadU64BE(ptr);
        auto exp6 = [](uint64_t v) { int x = static_cast<int>(v & 0x3Fu); return (x << 2) | (x >> 4); };
        auto exp7 = [](uint64_t v) { int x = static_cast<int>(v & 0x7Fu); return (x << 1) | (x >> 6); };
        int ro = exp6(u >> 57);
        int go = exp7((((u >> 56) & 1u) << 6) | ((u >> 49) & 0x3Fu));
        int bo = exp6((((u >> 48) & 1u) << 5) | (((u >> 43) & 3u) << 3) | ((u >> 39) & 7u));
        int rh = exp6((((u >> 34) & 0x1Fu) << 1) | ((u >> 32) & 1u));
        int gh = exp7(u >> 25);
        int bh = exp6(u >> 19);
        int rv = exp6(u >> 13);
        int gv = exp7(u >> 6);
        int bv = exp6(u);
        for (int py = 0; py < 4; ++py) {
            for (int px = 0; px < 4; ++px) {
                int di = (py * 4 + px) * 4;
                outRgba[di + 0] = static_cast<uint8_t>(std::clamp((px * (rh - ro) + py * (rv - ro) + 4 * ro + 2) >> 2, 0, 255));
                outRgba[di + 1] = static_cast<uint8_t>(std::clamp((px * (gh - go) + py * (gv - go) + 4 * go + 2) >> 2, 0, 255));
                outRgba[di + 2] = static_cast<uint8_t>(std::clamp((px * (bh - bo) + py * (bv - bo) + 4 * bo + 2) >> 2, 0, 255));
                outRgba[di + 3] = 255;
            }
        }
        return;
    }

    int br1 = (ptr[0] >> 3) & 31, bg1 = (ptr[1] >> 3) & 31, bb1 = (ptr[2] >> 3) & 31;
    int br2 = std::clamp(br1 + s3(ptr[0] & 7), 0, 31);
    int bg2 = std::clamp(bg1 + s3(ptr[1] & 7), 0, 31);
    int bb2 = std::clamp(bb1 + s3(ptr[2] & 7), 0, 31);
    int r1 = (br1 << 3) | (br1 >> 2), g1 = (bg1 << 3) | (bg1 >> 2), b1 = (bb1 << 3) | (bb1 >> 2);
    int r2 = (br2 << 3) | (br2 >> 2), g2 = (bg2 << 3) | (bg2 >> 2), b2 = (bb2 << 3) | (bb2 >> 2);
    int cw1 = (ptr[3] >> 5) & 7, cw2 = (ptr[3] >> 2) & 7;
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            int bitIdx = px * 4 + py;
            int idx = (((msb >> bitIdx) & 1) << 1) | ((lsb >> bitIdx) & 1);
            bool sub2 = flipBit ? (py >= 2) : (px >= 2);
            int cw = sub2 ? cw2 : cw1;
            int di = (py * 4 + px) * 4;
            if (!opaque && idx == 2) {
                outRgba[di + 0] = outRgba[di + 1] = outRgba[di + 2] = outRgba[di + 3] = 0;
            } else {
                int mod = (!opaque && idx == 0) ? 0 : kEtcMod[cw][idx];
                outRgba[di + 0] = static_cast<uint8_t>(std::clamp((sub2 ? r2 : r1) + mod, 0, 255));
                outRgba[di + 1] = static_cast<uint8_t>(std::clamp((sub2 ? g2 : g1) + mod, 0, 255));
                outRgba[di + 2] = static_cast<uint8_t>(std::clamp((sub2 ? b2 : b1) + mod, 0, 255));
                outRgba[di + 3] = 255;
            }
        }
    }
}

// Decodes an 8-byte 11-bit EAC single-channel block (OpenGL ES 3.0 Appendix C.2.4 / C.2.5) into 16 8-bit values.
inline void DecodeEacChannel11(const uint8_t* block, bool isSigned, uint8_t outCh8[16]) {
    static const int kEacMod[16][8] = {
        {-3,-6,-9,-15,2,5,8,14}, {-3,-7,-10,-13,2,6,9,12}, {-2,-5,-8,-13,1,4,7,12}, {-2,-4,-6,-13,1,3,5,12},
        {-3,-6,-8,-12,2,5,7,11}, {-3,-7,-9,-11,2,6,8,10}, {-4,-7,-8,-11,3,6,7,10}, {-3,-5,-8,-11,2,4,7,10},
        {-2,-6,-8,-10,1,5,7,9},  {-2,-5,-8,-10,1,4,7,9},  {-2,-4,-8,-10,1,3,7,9},  {-2,-5,-7,-10,1,4,6,9},
        {-3,-4,-7,-10,2,3,6,9},  {-1,-2,-3,-10,0,1,2,9},  {-4,-6,-8,-9,3,5,7,8},   {-3,-5,-7,-9,2,4,6,8}
    };
    const int mult = (block[1] >> 4) & 0xF;
    const int tbl  = block[1] & 0xF;
    uint64_t bits48 = 0;
    for (int i = 0; i < 6; ++i) bits48 = (bits48 << 8) | block[2 + i];

    if (!isSigned) {
        const int base = block[0];
        for (int py = 0; py < 4; ++py) {
            for (int px = 0; px < 4; ++px) {
                int pixIdx = px * 4 + py;
                int code = static_cast<int>((bits48 >> (45 - 3 * pixIdx)) & 7u);
                int mod = kEacMod[tbl][code];
                int val11 = (mult == 0)
                    ? std::clamp(base * 8 + 4 + mod, 0, 2047)
                    : std::clamp(base * 8 + 4 + mod * mult * 8, 0, 2047);
                outCh8[py * 4 + px] = static_cast<uint8_t>((val11 * 255 + 1023) / 2047);
            }
        }
    } else {
        int base = static_cast<int>(static_cast<int8_t>(block[0]));
        if (base == -128) base = -127;
        for (int py = 0; py < 4; ++py) {
            for (int px = 0; px < 4; ++px) {
                int pixIdx = px * 4 + py;
                int code = static_cast<int>((bits48 >> (45 - 3 * pixIdx)) & 7u);
                int mod = kEacMod[tbl][code];
                int val11 = (mult == 0)
                    ? std::clamp(base * 8 + mod, -1023, 1023)
                    : std::clamp(base * 8 + mod * mult * 8, -1023, 1023);
                outCh8[py * 4 + px] = static_cast<uint8_t>(((val11 + 1023) * 255 + 1023) / 2046);
            }
        }
    }
}

inline void DecodeEacR11Block(const uint8_t* block, bool isSigned, uint8_t outRgba[64]) {
    uint8_t r8[16];
    DecodeEacChannel11(block, isSigned, r8);
    for (int i = 0; i < 16; ++i) {
        outRgba[i * 4 + 0] = r8[i];
        outRgba[i * 4 + 1] = r8[i];
        outRgba[i * 4 + 2] = r8[i];
        outRgba[i * 4 + 3] = 255;
    }
}

inline void DecodeEacRG11Block(const uint8_t* block, bool isSigned, uint8_t outRgba[64]) {
    uint8_t r8[16], g8[16];
    DecodeEacChannel11(block, isSigned, r8);
    DecodeEacChannel11(block + 8, isSigned, g8);
    for (int i = 0; i < 16; ++i) {
        outRgba[i * 4 + 0] = r8[i];
        outRgba[i * 4 + 1] = g8[i];
        outRgba[i * 4 + 2] = 0;
        outRgba[i * 4 + 3] = 255;
    }
}

inline void BlitBlock4x4RgbaToPremulBgra(
    const uint8_t blockRgba[64],
    uint32_t bx,
    uint32_t by,
    uint32_t imgW,
    uint32_t imgH,
    std::vector<uint8_t>& dstBgra)
{
    for (uint32_t py = 0; py < 4; ++py) {
        uint32_t dy = by * 4 + py;
        if (dy >= imgH) continue;
        for (uint32_t px = 0; px < 4; ++px) {
            uint32_t dx = bx * 4 + px;
            if (dx >= imgW) continue;
            const uint8_t* s = &blockRgba[(py * 4 + px) * 4];
            WritePremulBGRA(&dstBgra[(static_cast<size_t>(dy) * imgW + dx) * 4], s[0], s[1], s[2], s[3]);
        }
    }
}

} // namespace GpuTex

#include "formats/Tga.h"
#include "formats/Psd.h"
#include "formats/Xcf.h"
#include "formats/Hdr.h"
#include "formats/Netpbm.h"
#include "formats/Pcx.h"
#include "formats/Qoi.h"
#include "formats/Dds.h"
#include "formats/Xbm.h"
#include "formats/Xpm.h"
#include "formats/Svg.h"

} // namespace

#include "formats/Pam.h"
#include "formats/Pfm.h"
#include "formats/Sgi.h"
#include "formats/Ras.h"
#include "formats/Ktx.h"
#include "formats/Vtf.h"
#include "formats/Farbfeld.h"
#include "formats/Fits.h"
#include "formats/Pkm.h"
#include "formats/Astc.h"
#include "formats/Pvr.h"
#include "formats/Ilbm.h"
#include "formats/Tim.h"
#include "formats/Wbmp.h"
#include "formats/Bdf.h"
#include "formats/Psf.h"
#include "formats/Y4m.h"
#include "formats/Ani.h"
#include "formats/PaletteImg.h"
#include "formats/Dicom.h"
#include "formats/ExtendedImages100.h"
#include "formats/ExtendedImages200.h"
#define E2_RW_ENABLE_TXD 1
#include "formats/RenderWare3D.h"
#undef E2_RW_ENABLE_TXD
#define E2_UE_ENABLE_TEX 1
#include "formats/UnrealAsset3DTex.h"
#undef E2_UE_ENABLE_TEX
#include "formats/AvifHeifImage.h"
#include "formats/Jpeg2000.h"
#include "formats/TiffWebpNative.h"
#define E2_RETROTEX_ENABLE_IMAGE 1
#include "formats/RetroGameTex.h"
#undef E2_RETROTEX_ENABLE_IMAGE
#include "formats/ConsoleGameTex.h"

bool ImageFormatLoader::IsExtendedImageExtension(const std::wstring& extLower) {
    return extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst" ||
           extLower == L".psd" || extLower == L".psb" ||
           extLower == L".xcf" ||
           extLower == L".hdr" || extLower == L".pic" ||
           extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm" ||
           extLower == L".pcx" ||
           extLower == L".qoi" ||
           extLower == L".dds" ||
           extLower == L".svg" ||
           extLower == L".xbm" ||
           extLower == L".xpm" ||
           extLower == L".pam" ||
           extLower == L".pfm" ||
           extLower == L".sgi" || extLower == L".rgb" || extLower == L".bw"  || extLower == L".rgba" ||
           extLower == L".ras" || extLower == L".sun" ||
           extLower == L".ktx" ||
           extLower == L".vtf" ||
           extLower == L".ff"  || extLower == L".farbfeld" ||
           extLower == L".fits" || extLower == L".fit" || extLower == L".fts" ||
           extLower == L".pkm" ||
           extLower == L".astc" ||
           extLower == L".pvr" ||
           extLower == L".iff" || extLower == L".ilbm" || extLower == L".lbm" ||
           extLower == L".tim" ||
           extLower == L".wbmp" ||
           extLower == L".bdf" ||
           extLower == L".psf" ||
           extLower == L".y4m" ||
           extLower == L".ani" ||
           extLower == L".gpl" || extLower == L".pal" || extLower == L".act" || extLower == L".aco" ||
           extLower == L".dcm" || extLower == L".dicom" ||
           extLower == L".exr" || extLower == L".ktx2" || extLower == L".basis" ||
           extLower == L".kra" || extLower == L".ora" || extLower == L".procreate" ||
           extLower == L".clip" || extLower == L".aseprite" || extLower == L".icns" ||
           extLower == L".jxl" || extLower == L".bpg" || extLower == L".blp" || extLower == L".utx" ||
           extLower == L".dpx" || extLower == L".cin" || extLower == L".cube" || extLower == L".3dl" ||
           extLower == L".ies" || extLower == L".ttf" || extLower == L".otf" || extLower == L".woff" || extLower == L".woff2" ||
           extLower == L".cr2" || extLower == L".cr3" || extLower == L".nef" || extLower == L".arw" ||
           extLower == L".dng" || extLower == L".raf" || extLower == L".orf" || extLower == L".rw2" ||
           extLower == L".pef" || extLower == L".eps" || extLower == L".ai"  || extLower == L".cdr" ||
           extLower == L".afphoto" || extLower == L".afdesign" || extLower == L".afpub" ||
           extLower == L".emf" || extLower == L".wmf" || extLower == L".cgm" ||
           extLower == L".abr" || extLower == L".pat" || extLower == L".dst" || extLower == L".pes" ||
           extLower == L".ans" || extLower == L".sixel" || extLower == L".txd" || extLower == L".uasset" ||
           extLower == L".avif" || extLower == L".heic" || extLower == L".heif" || extLower == L".avci" ||
           extLower == L".jp2"  || extLower == L".j2k"  || extLower == L".jpf"  || extLower == L".jpx"  || extLower == L".j2c" ||
           extLower == L".tif"  || extLower == L".tiff" || extLower == L".btf"  || extLower == L".tf8"  || extLower == L".webp" ||
           extLower == L".cur"  || extLower == L".wal"  || extLower == L".wad"  || extLower == L".spr"  || extLower == L".lmp"  || extLower == L".art" ||
           extLower == L".bntx" || extLower == L".gxt"  || extLower == L".gtf"  || extLower == L".nut"  || extLower == L".tex"  || extLower == L".vtex_c";
}

bool ImageFormatLoader::DecodeImageMemory(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
try {
    outInfo = {};
    if (!data || size < 6) return false;

    bool ok = false;
    // Check magic signatures first so even misnamed files decode reliably
    if (size >= 4 && std::memcmp(data, "8BPS", 4) == 0) {
        ok = DecodePSD(data, size, outInfo);
    } else if (extLower == L".avif" || extLower == L".heic" || extLower == L".heif" || extLower == L".avci" ||
               (extLower != L".cr3" && Formats::IsLikelyAvifOrHeifSignature(data, size))) {
        ok = Formats::DecodeAvifOrHeifImage(data, size, extLower, outInfo);
    } else if (extLower == L".jp2" || extLower == L".j2k" || extLower == L".jpf" || extLower == L".jpx" || extLower == L".j2c" ||
               Formats::IsLikelyJpeg2000Signature(data, size)) {
        ok = Formats::DecodeJpeg2000(data, size, extLower, outInfo);
    } else if (extLower == L".webp" ||
               (size >= 16 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0)) {
        ok = Formats::DecodeWebP(data, size, outInfo);
    } else if (extLower == L".tif" || extLower == L".tiff" || extLower == L".btf" || extLower == L".tf8" ||
               (size >= 16 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 43 && data[3] == 0) ||
                               (data[0] == 'M' && data[1] == 'M' && data[2] == 0  && data[3] == 43)))) {
        ok = Formats::DecodeTiffOrBigTiff(data, size, extLower, outInfo);
    } else if (size >= 4 && std::memcmp(data, "DDS ", 4) == 0) {
        ok = DecodeDDS(data, size, outInfo);
    } else if (size >= 9 && std::memcmp(data, "gimp xcf ", 9) == 0) {
        ok = DecodeXCF(data, size, outInfo);
    } else if (size >= 4 && std::memcmp(data, "qoif", 4) == 0) {
        ok = DecodeQOI(data, size, outInfo);
    } else if (size >= 10 && (std::memcmp(data, "#?RADIANCE", 10) == 0 || std::memcmp(data, "#?RGBE", 6) == 0)) {
        ok = DecodeHDR(data, size, outInfo);
    } else if (size >= 16 && std::memcmp(data, "farbfeld", 8) == 0) {
        ok = Formats::DecodeFarbfeld(data, size, outInfo);
    } else if (size >= 64 && std::memcmp(data, "VTF\0", 4) == 0) {
        ok = Formats::DecodeVTF(data, size, outInfo);
    } else if (size >= 12 && data[0] == 0xAB && std::memcmp(data + 1, "KTX 11", 6) == 0) {
        ok = Formats::DecodeKTX(data, size, outInfo);
    } else if (size >= 12 && data[0] == 0xAB && std::memcmp(data + 1, "KTX 20", 6) == 0) {
        ok = Formats::DecodeKTX2OrBasis(data, size, extLower, outInfo);
    } else if (size >= 77 && data[0] == 's' && data[1] == 'B' && ReadU16LE(data + 4) >= 77) {
        ok = Formats::DecodeKTX2OrBasis(data, size, L".basis", outInfo);
    } else if (size >= 32 && data[0] == 0x76 && data[1] == 0x2F && data[2] == 0x31 && data[3] == 0x01) {
        ok = Formats::DecodeOpenEXR(data, size, outInfo);
    } else if (size >= 24 && std::memcmp(data, "CSFCHUNK", 8) == 0) {
        ok = Formats::DecodeClipStudio(data, size, outInfo);
    } else if (size >= 128 && data[4] == 0xE0 && data[5] == 0xA5) {
        ok = Formats::DecodeAseprite(data, size, outInfo);
    } else if (size >= 16 && std::memcmp(data, "icns", 4) == 0) {
        ok = Formats::DecodeAppleICNS(data, size, outInfo);
    } else if (size >= 12 && std::memcmp(data, "ASEF", 4) == 0) {
        ok = Formats::DecodePaletteSwatch(data, size, extLower, outInfo);
    } else if (size >= 32 && data[0] == 0x59 && data[1] == 0xA6 && data[2] == 0x6A && data[3] == 0x95) {
        ok = Formats::DecodeSunRaster(data, size, outInfo);
    } else if (size >= 512 && data[0] == 0x01 && data[1] == 0xDA) {
        ok = Formats::DecodeSGI(data, size, outInfo);
    } else if (size >= 2880 && std::memcmp(data, "SIMPLE  =", 9) == 0) {
        ok = Formats::DecodeFITS(data, size, outInfo);
    } else if (extLower == L".pam" || (size >= 16 && data[0] == 'P' && data[1] == '7')) {
        ok = Formats::DecodePAM(data, size, outInfo);
    } else if (extLower == L".pfm" || (size >= 16 && data[0] == 'P' && (data[1] == 'F' || data[1] == 'f'))) {
        ok = Formats::DecodePFM(data, size, outInfo);
    } else if (extLower == L".sgi" || extLower == L".rgb" || extLower == L".bw" || extLower == L".rgba") {
        ok = Formats::DecodeSGI(data, size, outInfo);
    } else if (extLower == L".ras" || extLower == L".sun") {
        ok = Formats::DecodeSunRaster(data, size, outInfo);
    } else if (extLower == L".ktx") {
        ok = Formats::DecodeKTX(data, size, outInfo);
    } else if (extLower == L".vtf") {
        ok = Formats::DecodeVTF(data, size, outInfo);
    } else if (extLower == L".ff" || extLower == L".farbfeld") {
        ok = Formats::DecodeFarbfeld(data, size, outInfo);
    } else if (extLower == L".fits" || (extLower == L".fit" && size >= 80 && std::memcmp(data, "SIMPLE", 6) == 0) || extLower == L".fts") {
        ok = Formats::DecodeFITS(data, size, outInfo);
    } else if (extLower == L".svg") {
        ok = DecodeSVG(data, size, maxDimension, outInfo);
    } else if (extLower == L".dds") {
        ok = DecodeDDS(data, size, outInfo);
    } else if (extLower == L".xbm") {
        ok = DecodeXBM(data, size, outInfo);
    } else if (extLower == L".xpm") {
        ok = DecodeXPM(data, size, outInfo);
    } else if (extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm") {
        ok = DecodeNetpbm(data, size, outInfo);
    } else if (extLower == L".pcx") {
        ok = DecodePCX(data, size, outInfo);
    } else if (extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst") {
        ok = DecodeTGA(data, size, outInfo);
    } else if (extLower == L".hdr" || extLower == L".pic") {
        ok = DecodeHDR(data, size, outInfo);
    } else if (extLower == L".psd" || extLower == L".psb") {
        ok = DecodePSD(data, size, outInfo);
    } else if (extLower == L".xcf") {
        ok = DecodeXCF(data, size, outInfo);
    } else if (extLower == L".qoi") {
        ok = DecodeQOI(data, size, outInfo);
    } else if (extLower == L".pkm" || (size >= 16 && std::memcmp(data, "PKM ", 4) == 0)) {
        ok = Formats::DecodePKM(data, size, outInfo);
    } else if (extLower == L".astc" || (size >= 16 && data[0] == 0x13 && data[1] == 0xAB && data[2] == 0xA1 && data[3] == 0x5C)) {
        ok = Formats::DecodeASTC(data, size, outInfo);
    } else if (extLower == L".pvr" || (size >= 52 && (std::memcmp(data, "PVR\x03", 4) == 0 || std::memcmp(data + 44, "PVR!", 4) == 0))) {
        ok = Formats::DecodePVR(data, size, outInfo);
    } else if (extLower == L".iff" || extLower == L".ilbm" || extLower == L".lbm" || (size >= 12 && std::memcmp(data, "FORM", 4) == 0)) {
        ok = Formats::DecodeILBM(data, size, outInfo);
    } else if (extLower == L".tim") {
        ok = Formats::DecodeTIM(data, size, outInfo);
    } else if (extLower == L".wbmp") {
        ok = Formats::DecodeWBMP(data, size, outInfo);
    } else if (extLower == L".bdf") {
        ok = Formats::DecodeBDF(data, size, outInfo);
    } else if (extLower == L".psf") {
        ok = Formats::DecodePSF(data, size, outInfo);
    } else if (extLower == L".y4m" || (size >= 10 && std::memcmp(data, "YUV4MPEG2 ", 10) == 0)) {
        ok = Formats::DecodeY4M(data, size, outInfo);
    } else if (extLower == L".cur") {
        ok = Formats::DecodeCur(data, size, outInfo);
    } else if (extLower == L".spr" || (size >= 44 && std::memcmp(data, "IDSP", 4) == 0)) {
        ok = Formats::DecodeQuakeOrHlSprite(data, size, outInfo);
    } else if ((size >= 12 && (std::memcmp(data, "WAD2", 4) == 0 || std::memcmp(data, "WAD3", 4) == 0)) || extLower == L".wad") {
        ok = Formats::DecodeWad2OrWad3Atlas(data, size, outInfo);
    } else if (extLower == L".wal") {
        ok = Formats::DecodeQuake2Wal(data, size, outInfo);
    } else if (extLower == L".lmp") {
        ok = Formats::DecodeQuakeLmp(data, size, outInfo);
    } else if (extLower == L".art") {
        ok = Formats::DecodeBuildEngineArt(data, size, L"", outInfo);
    } else if (extLower == L".bntx" || (size >= 144 && std::memcmp(data, "BNTX", 4) == 0)) {
        ok = Formats::DecodeSwitchBntx(data, size, outInfo);
    } else if (extLower == L".gxt" || (size >= 64 && std::memcmp(data, "GXT\0", 4) == 0)) {
        ok = Formats::DecodeVitaGxt(data, size, outInfo);
    } else if (extLower == L".gtf") {
        ok = Formats::DecodePs3Gtf(data, size, outInfo);
    } else if (extLower == L".nut" || (size >= 64 && (std::memcmp(data, "NTP3", 4) == 0 || std::memcmp(data, "NTWU", 4) == 0 || std::memcmp(data, "NTWD", 4) == 0))) {
        ok = Formats::DecodeNamcoNut(data, size, outInfo);
    } else if (extLower == L".tex" || Formats::IsLikelyCapcomTexBinary(data, size)) {
        ok = Formats::DecodeCapcomTex(data, size, outInfo);
    } else if (extLower == L".vtex_c") {
        ok = Formats::DecodeSource2VtexC(data, size, outInfo);
    } else if (extLower == L".ani" || (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "ACON", 4) == 0)) {
        ok = Formats::DecodeANI(data, size, outInfo);
    } else if (extLower == L".gpl" || extLower == L".pal" || extLower == L".act" || extLower == L".aco") {
        ok = Formats::DecodePaletteSwatch(data, size, extLower, outInfo);
    } else if (extLower == L".dcm" || extLower == L".dicom" || (size >= 132 && std::memcmp(data + 128, "DICM", 4) == 0)) {
        ok = Formats::DecodeDICOM(data, size, outInfo);
    } else if (extLower == L".exr") {
        ok = Formats::DecodeOpenEXR(data, size, outInfo);
    } else if (extLower == L".ktx2" || extLower == L".basis") {
        ok = Formats::DecodeKTX2OrBasis(data, size, extLower, outInfo);
    } else if (extLower == L".kra" || extLower == L".ora" || extLower == L".procreate") {
        ok = Formats::DecodeZipArtworkContainer(data, size, extLower, outInfo);
    } else if (extLower == L".clip") {
        ok = Formats::DecodeClipStudio(data, size, outInfo);
    } else if (extLower == L".aseprite") {
        ok = Formats::DecodeAseprite(data, size, outInfo);
    } else if (extLower == L".icns") {
        ok = Formats::DecodeAppleICNS(data, size, outInfo);
    } else if (extLower == L".jxl" || extLower == L".bpg" || (size >= 8 && std::memcmp(data, "BPG\xFB", 4) == 0) ||
               (size >= 12 && std::memcmp(data, "\x00\x00\x00\x0CJXL \x0D\x0A\x87\x0A", 12) == 0)) {
        ok = Formats::DecodeJxlOrBpg(data, size, extLower, outInfo);
    } else if (extLower == L".uasset" ||
               (size >= 64 && ReadU32LE(data) == 0x9E2A83C1u && static_cast<int32_t>(ReadU32LE(data + 4)) < 0)) {
        ok = Formats::DecodeUnrealTexture2D(data, size, L"", outInfo);
    } else if (extLower == L".blp" || extLower == L".utx" || (size >= 36 && ReadU32LE(data) == 0x9E2A83C1u)) {
        ok = Formats::DecodeBlpOrUtx(data, size, extLower, outInfo);
    } else if (extLower == L".txd" || (size >= 36 && ReadU32LE(data) == 0x16u && Formats::RwStreamDetail::IsPlausibleRwVersion(ReadU32LE(data + 8)))) {
        ok = Formats::DecodeRenderWareTxd(data, size, outInfo);
    } else if (extLower == L".dpx" || extLower == L".cin") {
        ok = Formats::DecodeDpxOrCineon(data, size, extLower, outInfo);
    } else if (extLower == L".cube" || extLower == L".3dl") {
        ok = Formats::DecodeColorLutCube(data, size, extLower, outInfo);
    } else if (extLower == L".ies") {
        ok = Formats::DecodePhotometricIES(data, size, extLower, outInfo);
    } else if (extLower == L".ttf" || extLower == L".otf" || extLower == L".woff" || extLower == L".woff2" ||
               (size >= 44 && std::memcmp(data, "wOFF", 4) == 0) || (size >= 48 && std::memcmp(data, "wOF2", 4) == 0)) {
        ok = Formats::DecodeFontSpecimen(data, size, extLower, outInfo);
    } else if (size >= 12 && std::memcmp(data, "8BPT", 4) == 0) {
        ok = Formats::DecodeAbrOrPat(data, size, L".pat", outInfo);
    } else {
        ok = Formats::DecodeExtendedImages200(data, size, extLower, outInfo);
    }

    if (ok && outInfo.valid) {
        DownscaleIfRequested(maxDimension, outInfo);
        return true;
    }
    outInfo = {};
    return false;
} E2_READER_CATCH(L"Image reader", extLower, { outInfo = ExtendedImageInfo(); return false; })

bool ImageFormatLoader::DecodeImageFile(
    const std::wstring& fullPath,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
try {
    outInfo = {};
    std::vector<uint8_t> bytes;
    if (!PathUtils::ReadFileBytes(fullPath, 256ULL * 1024 * 1024, bytes) || bytes.empty()) {
        return false;
    }
    std::wstring ext = PathUtils::GetExtension(fullPath);
    if (ext == L".art") {
        if (Formats::DecodeBuildEngineArt(bytes.data(), bytes.size(), fullPath, outInfo) && outInfo.valid) {
            DownscaleIfRequested(maxDimension, outInfo);
            return true;
        }
        outInfo = {};
        return false;
    }
    if (ext == L".uasset" ||
        (bytes.size() >= 64 && ReadU32LE(bytes.data()) == 0x9E2A83C1u && static_cast<int32_t>(ReadU32LE(bytes.data() + 4)) < 0))
    {
        if (Formats::DecodeUnrealTexture2D(bytes.data(), bytes.size(), fullPath, outInfo) && outInfo.valid) {
            DownscaleIfRequested(maxDimension, outInfo);
            return true;
        }
        outInfo = {};
        return false;
    }
    return DecodeImageMemory(bytes.data(), bytes.size(), ext, maxDimension, outInfo);
} E2_READER_CATCH(L"Image reader", fullPath, { outInfo = ExtendedImageInfo(); return false; })

} // namespace Explorer2
