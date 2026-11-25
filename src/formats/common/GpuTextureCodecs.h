#pragma once
// GpuTextureCodecs.h — Shared PBGRA pixel helpers, downscaler, HDR float converter, and GPU block
// texture decoders (BC1/DXT1, BC2/DXT3, BC3/DXT5, BC4, BC5, BC6H, BC7, ETC1, ETC2, EAC).
// Included inside `namespace Explorer2` by ImageFormatLoader.cpp and ImageReadersConsoleRetro.cpp.

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

inline void DownscaleIfRequested(uint32_t maxDimension, ExtendedImageInfo& info) {
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

// ============================================================================
// Pure-C++ PNG Decoder (1/2/4/8/16-bit Gray, RGB, Indexed+PLTE+tRNS, GA, RGBA; non-interlaced & Adam7)
// ============================================================================
inline uint8_t PngPaethPredictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

inline bool DecodePng(
    const uint8_t* data,
    size_t size,
    uint32_t& outW,
    uint32_t& outH,
    std::vector<uint8_t>& outStraightRgba,
    uint32_t* outOrigChannels = nullptr,
    uint32_t* outOrigBitDepth = nullptr)
{
    static constexpr uint8_t kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (!data || size < 33 || std::memcmp(data, kPngSig, 8) != 0) return false;

    uint32_t w = 0, h = 0;
    uint8_t bitDepth = 0, colorType = 0, compMethod = 0, filterMethod = 0, interlace = 0;
    bool hasIhdr = false;
    uint8_t palette[256][4] = {};
    uint32_t palCount = 0;
    bool hasTrnsKey = false;
    uint16_t trnsGray = 0, trnsR = 0, trnsG = 0, trnsB = 0;
    std::vector<uint8_t> idatCompressed;

    size_t pos = 8;
    while (pos + 12 <= size) {
        uint32_t cLen = ::Explorer2::ByteIO::RdU32BE(data + pos);
        const uint8_t* cType = data + pos + 4;
        if (cLen > size - (pos + 12)) break;
        const uint8_t* cData = data + pos + 8;

        if (std::memcmp(cType, "IHDR", 4) == 0) {
            if (cLen < 13) return false;
            w = ::Explorer2::ByteIO::RdU32BE(cData);
            h = ::Explorer2::ByteIO::RdU32BE(cData + 4);
            bitDepth = cData[8];
            colorType = cData[9];
            compMethod = cData[10];
            filterMethod = cData[11];
            interlace = cData[12];
            if (w == 0 || h == 0 || w > 16384 || h > 16384) return false;
            if (compMethod != 0 || filterMethod != 0 || interlace > 1) return false;
            if (static_cast<uint64_t>(w) * h > 64ull * 1024ull * 1024ull) return false;
            hasIhdr = true;
        } else if (std::memcmp(cType, "PLTE", 4) == 0) {
            palCount = std::min<uint32_t>(256u, cLen / 3u);
            for (uint32_t i = 0; i < palCount; ++i) {
                palette[i][0] = cData[i * 3 + 0];
                palette[i][1] = cData[i * 3 + 1];
                palette[i][2] = cData[i * 3 + 2];
                palette[i][3] = 255;
            }
        } else if (std::memcmp(cType, "tRNS", 4) == 0 && hasIhdr) {
            if (colorType == 3) {
                uint32_t n = std::min<uint32_t>(256u, cLen);
                for (uint32_t i = 0; i < n; ++i) {
                    palette[i][3] = cData[i];
                    if (cData[i] < 255) hasTrnsKey = true;
                }
            } else if (colorType == 0 && cLen >= 2) {
                trnsGray = ::Explorer2::ByteIO::RdU16BE(cData);
                hasTrnsKey = true;
            } else if (colorType == 2 && cLen >= 6) {
                trnsR = ::Explorer2::ByteIO::RdU16BE(cData);
                trnsG = ::Explorer2::ByteIO::RdU16BE(cData + 2);
                trnsB = ::Explorer2::ByteIO::RdU16BE(cData + 4);
                hasTrnsKey = true;
            }
        } else if (std::memcmp(cType, "IDAT", 4) == 0) {
            if (idatCompressed.size() + cLen > 128ull * 1024ull * 1024ull) return false;
            idatCompressed.insert(idatCompressed.end(), cData, cData + cLen);
        } else if (std::memcmp(cType, "IEND", 4) == 0) {
            break;
        }
        pos += 12ull + cLen;
    }

    if (!hasIhdr || idatCompressed.empty()) return false;
    uint32_t channels = 0;
    if (colorType == 0) channels = 1;
    else if (colorType == 2) channels = 3;
    else if (colorType == 3) channels = 1;
    else if (colorType == 4) channels = 2;
    else if (colorType == 6) channels = 4;
    else return false;

    if (colorType == 3 && (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8)) return false;
    if (colorType == 0 && (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8 && bitDepth != 16)) return false;
    if ((colorType == 2 || colorType == 4 || colorType == 6) && (bitDepth != 8 && bitDepth != 16)) return false;
    if (colorType == 3 && palCount == 0) return false;

    const uint32_t bitsPerPixel = channels * bitDepth;
    const size_t filterBpp = std::max<size_t>(1u, (bitsPerPixel + 7u) / 8u);

    std::vector<uint8_t> inflated;
    size_t maxInflated = static_cast<size_t>(h) * (static_cast<size_t>(w) * 8u + 16u) + 4096u;
    if (!::Explorer2::ZipDeflate::InflateZlib(idatCompressed.data(), idatCompressed.size(), inflated, maxInflated)) {
        return false;
    }

    outW = w;
    outH = h;
    if (outOrigChannels) {
        *outOrigChannels = (colorType == 3) ? (hasTrnsKey ? 4u : 3u)
                         : ((hasTrnsKey && (colorType == 0 || colorType == 2)) ? 4u : channels);
    }
    if (outOrigBitDepth) *outOrigBitDepth = bitDepth;
    outStraightRgba.assign(static_cast<size_t>(w) * h * 4u, 0u);

    auto unpackPixelRun = [&](const uint8_t* rowBytes, uint32_t passW, uint32_t x0, uint32_t dx, uint32_t dstY) {
        for (uint32_t px = 0; px < passW; ++px) {
            uint32_t dstX = x0 + px * dx;
            if (dstX >= w || dstY >= h) continue;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (bitDepth < 8) {
                uint32_t bitIdx = px * bitDepth;
                uint8_t byte = rowBytes[bitIdx >> 3];
                uint32_t shift = 8u - bitDepth - (bitIdx & 7u);
                uint8_t val = static_cast<uint8_t>((byte >> shift) & ((1u << bitDepth) - 1u));
                if (colorType == 3) {
                    r = palette[val][0]; g = palette[val][1]; b = palette[val][2]; a = palette[val][3];
                } else {
                    uint8_t maxV = static_cast<uint8_t>((1u << bitDepth) - 1u);
                    r = g = b = static_cast<uint8_t>((val * 255u + (maxV >> 1)) / maxV);
                    if (hasTrnsKey && val == trnsGray) a = 0;
                }
            } else if (bitDepth == 8) {
                const uint8_t* p = rowBytes + static_cast<size_t>(px) * channels;
                if (colorType == 0) {
                    r = g = b = p[0];
                    if (hasTrnsKey && p[0] == (trnsGray & 0xFFu)) a = 0;
                } else if (colorType == 2) {
                    r = p[0]; g = p[1]; b = p[2];
                    if (hasTrnsKey && p[0] == (trnsR & 0xFFu) && p[1] == (trnsG & 0xFFu) && p[2] == (trnsB & 0xFFu)) a = 0;
                } else if (colorType == 3) {
                    uint8_t idx = p[0];
                    r = palette[idx][0]; g = palette[idx][1]; b = palette[idx][2]; a = palette[idx][3];
                } else if (colorType == 4) {
                    r = g = b = p[0]; a = p[1];
                } else {
                    r = p[0]; g = p[1]; b = p[2]; a = p[3];
                }
            } else {
                const uint8_t* p = rowBytes + static_cast<size_t>(px) * channels * 2u;
                if (colorType == 0) {
                    uint16_t v16 = ::Explorer2::ByteIO::RdU16BE(p);
                    r = g = b = static_cast<uint8_t>((static_cast<uint32_t>(v16) * 255u + 32767u) / 65535u);
                    if (hasTrnsKey && v16 == trnsGray) a = 0;
                } else if (colorType == 2) {
                    uint16_t r16 = ::Explorer2::ByteIO::RdU16BE(p);
                    uint16_t g16 = ::Explorer2::ByteIO::RdU16BE(p + 2);
                    uint16_t b16 = ::Explorer2::ByteIO::RdU16BE(p + 4);
                    r = static_cast<uint8_t>((static_cast<uint32_t>(r16) * 255u + 32767u) / 65535u);
                    g = static_cast<uint8_t>((static_cast<uint32_t>(g16) * 255u + 32767u) / 65535u);
                    b = static_cast<uint8_t>((static_cast<uint32_t>(b16) * 255u + 32767u) / 65535u);
                    if (hasTrnsKey && r16 == trnsR && g16 == trnsG && b16 == trnsB) a = 0;
                } else if (colorType == 4) {
                    uint16_t v16 = ::Explorer2::ByteIO::RdU16BE(p);
                    uint16_t a16 = ::Explorer2::ByteIO::RdU16BE(p + 2);
                    r = g = b = static_cast<uint8_t>((static_cast<uint32_t>(v16) * 255u + 32767u) / 65535u);
                    a = static_cast<uint8_t>((static_cast<uint32_t>(a16) * 255u + 32767u) / 65535u);
                } else {
                    uint16_t r16 = ::Explorer2::ByteIO::RdU16BE(p);
                    uint16_t g16 = ::Explorer2::ByteIO::RdU16BE(p + 2);
                    uint16_t b16 = ::Explorer2::ByteIO::RdU16BE(p + 4);
                    uint16_t a16 = ::Explorer2::ByteIO::RdU16BE(p + 6);
                    r = static_cast<uint8_t>((static_cast<uint32_t>(r16) * 255u + 32767u) / 65535u);
                    g = static_cast<uint8_t>((static_cast<uint32_t>(g16) * 255u + 32767u) / 65535u);
                    b = static_cast<uint8_t>((static_cast<uint32_t>(b16) * 255u + 32767u) / 65535u);
                    a = static_cast<uint8_t>((static_cast<uint32_t>(a16) * 255u + 32767u) / 65535u);
                }
            }
            size_t di = (static_cast<size_t>(dstY) * w + dstX) * 4u;
            outStraightRgba[di + 0] = r;
            outStraightRgba[di + 1] = g;
            outStraightRgba[di + 2] = b;
            outStraightRgba[di + 3] = a;
        }
    };

    size_t srcOff = 0;
    auto decodePass = [&](uint32_t x0, uint32_t dx, uint32_t y0, uint32_t dy) -> bool {
        if (x0 >= w || y0 >= h) return true;
        uint32_t passW = (w - x0 + dx - 1u) / dx;
        uint32_t passH = (h - y0 + dy - 1u) / dy;
        size_t rowBytes = (static_cast<size_t>(passW) * bitsPerPixel + 7u) / 8u;
        if (srcOff + static_cast<size_t>(passH) * (1u + rowBytes) > inflated.size()) return false;

        std::vector<uint8_t> prevRow(rowBytes, 0u);
        std::vector<uint8_t> curRow(rowBytes, 0u);
        for (uint32_t py = 0; py < passH; ++py) {
            uint8_t filt = inflated[srcOff++];
            const uint8_t* raw = inflated.data() + srcOff;
            srcOff += rowBytes;
            for (size_t i = 0; i < rowBytes; ++i) {
                int left = (i >= filterBpp) ? curRow[i - filterBpp] : 0;
                int up = prevRow[i];
                int upLeft = (i >= filterBpp) ? prevRow[i - filterBpp] : 0;
                uint8_t v = raw[i];
                switch (filt) {
                    case 0: curRow[i] = v; break;
                    case 1: curRow[i] = static_cast<uint8_t>(v + left); break;
                    case 2: curRow[i] = static_cast<uint8_t>(v + up); break;
                    case 3: curRow[i] = static_cast<uint8_t>(v + ((left + up) >> 1)); break;
                    case 4: curRow[i] = static_cast<uint8_t>(v + PngPaethPredictor(left, up, upLeft)); break;
                    default: return false;
                }
            }
            unpackPixelRun(curRow.data(), passW, x0, dx, y0 + py * dy);
            prevRow.swap(curRow);
        }
        return true;
    };

    if (interlace == 0) {
        return decodePass(0, 1, 0, 1);
    }
    static constexpr uint32_t kAdam7[7][4] = {
        {0, 8, 0, 8}, {4, 8, 0, 8}, {0, 4, 4, 8}, {2, 4, 0, 4},
        {0, 2, 2, 4}, {1, 2, 0, 2}, {0, 1, 1, 2}
    };
    for (int p = 0; p < 7; ++p) {
        if (!decodePass(kAdam7[p][0], kAdam7[p][1], kAdam7[p][2], kAdam7[p][3])) return false;
    }
    return true;
}

inline bool DecodePngToImageInfo(
    const uint8_t* data,
    size_t size,
    const std::wstring& fmtLabel,
    ExtendedImageInfo& outInfo)
{
    uint32_t w = 0, h = 0, ch = 4, bpc = 8;
    std::vector<uint8_t> rgba;
    if (!DecodePng(data, size, w, h, rgba, &ch, &bpc)) return false;
    size_t n = static_cast<size_t>(w) * h;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = ch;
    outInfo.bitsPerChannel = std::clamp(bpc, 8u, 16u);
    outInfo.formatName = fmtLabel;
    outInfo.bgraPixels.resize(n * 4u);
    for (size_t i = 0; i < n; ++i) {
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u],
            rgba[i * 4u + 0], rgba[i * 4u + 1], rgba[i * 4u + 2], rgba[i * 4u + 3]);
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// PowerVR PVRTC 2bpp & 4bpp Shared Surface Decoder (used by Pvr.h & VitaGxt.h)
// ============================================================================
inline uint32_t PvrtcTwiddleIndex(uint32_t x, uint32_t y, uint32_t wordsX, uint32_t wordsY) {
    bool pow2 = ((wordsX & (wordsX - 1u)) == 0) && ((wordsY & (wordsY - 1u)) == 0);
    if (!pow2) return y * wordsX + x;

    uint32_t minDim = std::min(wordsX, wordsY);
    uint32_t idx = 0;
    uint32_t shift = 0;
    for (uint32_t bit = 1; bit < minDim; bit <<= 1) {
        if (y & bit) idx |= (1u << shift);
        ++shift;
        if (x & bit) idx |= (1u << shift);
        ++shift;
    }
    if (wordsX > wordsY) {
        idx |= ((x / minDim) << shift);
    } else if (wordsY > wordsX) {
        idx |= ((y / minDim) << shift);
    }
    return idx;
}

inline bool DecodePvrtcSurface(
    const uint8_t* src,
    size_t availBytes,
    uint32_t w,
    uint32_t h,
    bool is2bpp,
    bool opaqueOnly,
    std::vector<uint8_t>& outBgra)
{
    if (!src || w == 0 || h == 0 || w > 16384u || h > 16384u) return false;
    uint32_t blockW = is2bpp ? 8u : 4u;
    uint32_t blockH = 4u;
    uint32_t rawWordsX = (w + blockW - 1u) / blockW;
    uint32_t rawWordsY = (h + blockH - 1u) / blockH;
    uint32_t stdWordsX = std::max(2u, rawWordsX);
    uint32_t stdWordsY = std::max(2u, rawWordsY);

    uint32_t wordsX = stdWordsX;
    uint32_t wordsY = stdWordsY;
    if (availBytes < static_cast<size_t>(stdWordsX) * stdWordsY * 8u) {
        if (availBytes >= static_cast<size_t>(rawWordsX) * rawWordsY * 8u) {
            wordsX = rawWordsX;
            wordsY = rawWordsY;
        } else {
            return false;
        }
    }

    struct WordInfo {
        uint32_t modData;
        bool modMode;
        int a[4];
        int b[4];
    };

    size_t numWords = static_cast<size_t>(wordsX) * wordsY;
    std::vector<WordInfo> grid(numWords);
    auto exp3 = [](uint32_t v) { v &= 7u;  return static_cast<int>((v << 5) | (v << 2) | (v >> 1)); };
    auto exp4 = [](uint32_t v) { v &= 15u; return static_cast<int>((v << 4) | v); };
    auto exp5 = [](uint32_t v) { v &= 31u; return static_cast<int>((v << 3) | (v >> 2)); };

    for (uint32_t wy = 0; wy < wordsY; ++wy) {
        for (uint32_t wx = 0; wx < wordsX; ++wx) {
            uint32_t tidx = PvrtcTwiddleIndex(wx, wy, wordsX, wordsY);
            if (tidx >= numWords) tidx = wy * wordsX + wx;
            const uint8_t* wp = src + static_cast<size_t>(tidx) * 8u;
            uint32_t mod = ::Explorer2::ByteIO::RdU32LE(wp);
            uint32_t col = ::Explorer2::ByteIO::RdU32LE(wp + 4);

            WordInfo wi = {};
            wi.modData = mod;
            wi.modMode = (col & 1u) != 0;

            if ((col >> 15) & 1u) {
                wi.a[0] = exp5(col >> 10);
                wi.a[1] = exp5(col >> 5);
                wi.a[2] = exp4(col >> 1);
                wi.a[3] = 255;
            } else {
                wi.a[3] = exp3(col >> 12);
                wi.a[0] = exp4(col >> 8);
                wi.a[1] = exp4(col >> 4);
                wi.a[2] = exp3(col >> 1);
            }

            if ((col >> 31) & 1u) {
                wi.b[0] = exp5(col >> 26);
                wi.b[1] = exp5(col >> 21);
                wi.b[2] = exp5(col >> 16);
                wi.b[3] = 255;
            } else {
                wi.b[3] = exp3(col >> 28);
                wi.b[0] = exp4(col >> 24);
                wi.b[1] = exp4(col >> 20);
                wi.b[2] = exp4(col >> 16);
            }

            if (opaqueOnly) {
                wi.a[3] = 255;
                wi.b[3] = 255;
            }
            grid[static_cast<size_t>(wy) * wordsX + wx] = wi;
        }
    }

    uint32_t wrapW = wordsX * blockW;
    uint32_t wrapH = wordsY * blockH;
    outBgra.resize(static_cast<size_t>(w) * h * 4u);

    auto modWeight2bpp = [&](uint32_t px, uint32_t py) -> int {
        uint32_t wx = (px / 8u) % wordsX;
        uint32_t wy = (py / 4u) % wordsY;
        uint32_t lx = px % 8u;
        uint32_t ly = py % 4u;
        const WordInfo& wi = grid[static_cast<size_t>(wy) * wordsX + wx];
        if (!wi.modMode) {
            return ((wi.modData >> (ly * 8u + lx)) & 1u) ? 8 : 0;
        }
        static const int kLut[4] = { 0, 3, 5, 8 };
        auto directAt = [&](uint32_t sx, uint32_t sy) -> int {
            uint32_t swx = (sx / 8u) % wordsX;
            uint32_t swy = (sy / 4u) % wordsY;
            uint32_t slx = sx % 8u;
            uint32_t sly = sy % 4u;
            const WordInfo& swi = grid[static_cast<size_t>(swy) * wordsX + swx];
            if (!swi.modMode) return ((swi.modData >> (sly * 8u + slx)) & 1u) ? 8 : 0;
            uint32_t bitPos = (sly * 4u + (slx >> 1)) * 2u;
            return kLut[(swi.modData >> bitPos) & 3u];
        };
        if (((lx ^ ly) & 1u) == 0) return directAt(px, py);
        uint32_t xm = (px + wrapW - 1u) % wrapW;
        uint32_t xp = (px + 1u) % wrapW;
        uint32_t ym = (py + wrapH - 1u) % wrapH;
        uint32_t yp = (py + 1u) % wrapH;
        return (directAt(xm, py) + directAt(xp, py) + directAt(px, ym) + directAt(px, yp) + 2) / 4;
    };

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t sy = (y + wrapH - blockH / 2u) % wrapH;
        uint32_t by0 = (sy / blockH) % wordsY;
        uint32_t by1 = (by0 + 1u) % wordsY;
        int fy = static_cast<int>(sy % blockH);
        int wy1 = fy, wy0 = static_cast<int>(blockH) - fy;

        for (uint32_t x = 0; x < w; ++x) {
            uint32_t sx = (x + wrapW - blockW / 2u) % wrapW;
            uint32_t bx0 = (sx / blockW) % wordsX;
            uint32_t bx1 = (bx0 + 1u) % wordsX;
            int fx = static_cast<int>(sx % blockW);
            int wx1 = fx, wx0 = static_cast<int>(blockW) - fx;

            const WordInfo& w00 = grid[static_cast<size_t>(by0) * wordsX + bx0];
            const WordInfo& w10 = grid[static_cast<size_t>(by0) * wordsX + bx1];
            const WordInfo& w01 = grid[static_cast<size_t>(by1) * wordsX + bx0];
            const WordInfo& w11 = grid[static_cast<size_t>(by1) * wordsX + bx1];

            int denom = static_cast<int>(blockW * blockH);
            int ca[4], cb[4];
            for (int c = 0; c < 4; ++c) {
                ca[c] = (w00.a[c] * wx0 * wy0 + w10.a[c] * wx1 * wy0 + w01.a[c] * wx0 * wy1 + w11.a[c] * wx1 * wy1 + denom / 2) / denom;
                cb[c] = (w00.b[c] * wx0 * wy0 + w10.b[c] * wx1 * wy0 + w01.b[c] * wx0 * wy1 + w11.b[c] * wx1 * wy1 + denom / 2) / denom;
            }

            int mw = 0;
            bool punchZeroAlpha = false;
            if (!is2bpp) {
                const WordInfo& cur = grid[static_cast<size_t>((y / 4u) % wordsY) * wordsX + ((x / 4u) % wordsX)];
                uint32_t code = (cur.modData >> (2u * ((y % 4u) * 4u + (x % 4u)))) & 3u;
                if (!cur.modMode) {
                    static const int kLut0[4] = { 0, 3, 5, 8 };
                    mw = kLut0[code];
                } else {
                    static const int kLut1[4] = { 0, 4, 4, 8 };
                    mw = kLut1[code];
                    if (code == 2 && !opaqueOnly) punchZeroAlpha = true;
                }
            } else {
                mw = modWeight2bpp(x, y);
            }

            uint8_t r = static_cast<uint8_t>(std::clamp((ca[0] * (8 - mw) + cb[0] * mw + 4) / 8, 0, 255));
            uint8_t g = static_cast<uint8_t>(std::clamp((ca[1] * (8 - mw) + cb[1] * mw + 4) / 8, 0, 255));
            uint8_t b = static_cast<uint8_t>(std::clamp((ca[2] * (8 - mw) + cb[2] * mw + 4) / 8, 0, 255));
            uint8_t a = punchZeroAlpha ? 0 : static_cast<uint8_t>(std::clamp((ca[3] * (8 - mw) + cb[3] * mw + 4) / 8, 0, 255));
            WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * w + x) * 4], r, g, b, a);
        }
    }
    return true;
}

// ============================================================================
// Wii U Latte / AMD R600 GX2 Surface Deswizzler (tileMode 0..7, uncompressed & BCn)
// Shared by NintendoWiiUBflim.h (.bflim) and NamcoNut.h (NTWU .nut).
// ============================================================================
inline uint32_t ComputeGx2MicroTilePixelIndex(uint32_t x, uint32_t y, uint32_t bppBits, uint32_t thickness) {
    const uint32_t x0 = x & 1u, x1 = (x >> 1) & 1u, x2 = (x >> 2) & 1u;
    const uint32_t y0 = y & 1u, y1 = (y >> 1) & 1u, y2 = (y >> 2) & 1u;
    uint32_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0;
    if (bppBits == 8u) {
        b0 = x0; b1 = x1; b2 = x2; b3 = y1; b4 = y0; b5 = y2;
    } else if (bppBits == 16u) {
        b0 = x0; b1 = x1; b2 = x2; b3 = y0; b4 = y1; b5 = y2;
    } else if (bppBits == 32u || bppBits == 96u) {
        if (thickness == 4u) {
            b0 = x0; b1 = y0; b3 = x1; b4 = y1; b6 = x2; b7 = y2;
        } else {
            b0 = x0; b1 = x1; b2 = y0; b3 = x2; b4 = y1; b5 = y2;
        }
    } else if (bppBits == 64u) {
        if (thickness == 4u) {
            b0 = x0; b1 = y0; b3 = x1; b4 = x2; b5 = y1; b7 = y2;
        } else {
            b0 = x0; b1 = y0; b2 = x1; b3 = x2; b4 = y1; b5 = y2;
        }
    } else { // 128-bit (RGBA32F, BC2/BC3/BC5/BC6H/BC7 4x4 blocks)
        if (thickness == 4u) {
            b0 = y0; b2 = x0; b3 = x1; b4 = x2; b5 = y1; b7 = y2;
        } else {
            b0 = y0; b1 = x0; b2 = x1; b3 = x2; b4 = y1; b5 = y2;
        }
    }
    return b0 | (b1 << 1) | (b2 << 2) | (b3 << 3) | (b4 << 4) | (b5 << 5) | (b6 << 6) | (b7 << 7);
}

inline bool DeswizzleWiiUGx2Surface(
    const uint8_t* src,
    size_t srcLen,
    uint32_t elemW,
    uint32_t elemH,
    uint32_t bytesPerElem,
    uint32_t tileMode,
    uint32_t pipeSwizzle,
    uint32_t bankSwizzle,
    std::vector<uint8_t>& outLinear)
{
    if (!src || elemW == 0 || elemH == 0 || elemW > 16384u || elemH > 16384u) return false;
    if (bytesPerElem != 1u && bytesPerElem != 2u && bytesPerElem != 4u && bytesPerElem != 8u && bytesPerElem != 16u) {
        return false;
    }
    const uint64_t linearBytes = static_cast<uint64_t>(elemW) * elemH * bytesPerElem;
    if (linearBytes == 0 || linearBytes > 256ull * 1024ull * 1024ull) return false;

    const uint32_t tm = tileMode & 0x1Fu;
    if (tm == 0u) {
        if (srcLen < linearBytes) return false;
        outLinear.assign(src, src + static_cast<size_t>(linearBytes));
        return true;
    }

    if (tm == 1u) { // GX2_TILE_MODE_LINEAR_ALIGNED
        const uint32_t pitchAlign = std::max<uint32_t>(8u, 64u / bytesPerElem);
        const uint32_t alignedPitch = (elemW + pitchAlign - 1u) & ~(pitchAlign - 1u);
        const uint64_t alignedBytes = static_cast<uint64_t>(alignedPitch) * elemH * bytesPerElem;
        const uint32_t rowStrideElems = (srcLen >= alignedBytes) ? alignedPitch : elemW;
        if (srcLen < static_cast<uint64_t>(rowStrideElems) * elemH * bytesPerElem) return false;
        outLinear.resize(static_cast<size_t>(linearBytes));
        for (uint32_t y = 0; y < elemH; ++y) {
            std::memcpy(
                outLinear.data() + static_cast<size_t>(y) * elemW * bytesPerElem,
                src + static_cast<size_t>(y) * rowStrideElems * bytesPerElem,
                static_cast<size_t>(elemW) * bytesPerElem);
        }
        return true;
    }

    const uint32_t bppBits = bytesPerElem * 8u;

    // 1D Micro-Tiled modes: 2 = 1D_TILED_THIN1 (thickness=1), 3 = 1D_TILED_THICK (thickness=4)
    if (tm == 2u || tm == 3u) {
        const uint32_t thickness = (tm == 3u) ? 4u : 1u;
        const uint32_t pitchAlign = std::max<uint32_t>(8u, 64u / (bytesPerElem * thickness));
        uint32_t alignedPitch = (elemW + pitchAlign - 1u) & ~(pitchAlign - 1u);
        const uint32_t alignedH = (elemH + 7u) & ~7u;
        const uint32_t microTileBytes = 64u * bytesPerElem * thickness;
        uint32_t tilesPerRow = alignedPitch / 8u;
        // Support tight-8-aligned buffers if full GPU pitch padding was stripped
        if (static_cast<uint64_t>(tilesPerRow) * (alignedH / 8u) * microTileBytes > srcLen) {
            alignedPitch = (elemW + 7u) & ~7u;
            tilesPerRow = alignedPitch / 8u;
        }
        outLinear.assign(static_cast<size_t>(linearBytes), 0u);
        for (uint32_t y = 0; y < elemH; ++y) {
            for (uint32_t x = 0; x < elemW; ++x) {
                const uint32_t tx = x >> 3;
                const uint32_t ty = y >> 3;
                const uint32_t pxIdx = ComputeGx2MicroTilePixelIndex(x & 7u, y & 7u, bppBits, thickness);
                const uint64_t srcOff = (static_cast<uint64_t>(ty) * tilesPerRow + tx) * microTileBytes +
                                        static_cast<uint64_t>(pxIdx) * bytesPerElem;
                if (srcOff + bytesPerElem > srcLen) return false;
                std::memcpy(
                    outLinear.data() + (static_cast<size_t>(y) * elemW + x) * bytesPerElem,
                    src + static_cast<size_t>(srcOff),
                    bytesPerElem);
            }
        }
        return true;
    }

    // 2D Macro-Tiled modes: 4 = 2D_TILED_THIN1, 5 = 2D_TILED_THIN2, 6 = 2D_TILED_THIN4, 7 = 2D_TILED_THICK
    if (tm >= 4u && tm <= 15u) {
        const uint32_t thickness = (tm == 7u || tm == 11u || tm == 13u || tm == 15u) ? 4u : 1u;
        uint32_t macroAspect = 1u;
        if (tm == 5u || tm == 9u) macroAspect = 2u;
        else if (tm == 6u || tm == 10u) macroAspect = 4u;

        // Wii U Latte constants: numPipes = 2, numBanks = 4, pipeInterleaveBytes = 256
        const uint32_t macroTilePitch = 32u / macroAspect;
        const uint32_t macroTileHeight = 16u * macroAspect;
        const uint32_t alignedPitch = (elemW + macroTilePitch - 1u) & ~(macroTilePitch - 1u);
        const uint32_t macroTilesPerRow = alignedPitch / macroTilePitch;
        const uint32_t microTileBytes = 64u * bytesPerElem * thickness;
        const uint32_t macroTileBytes = 8u * microTileBytes; // numPipes(2) * numBanks(4) = 8 micro-tiles

        static constexpr uint8_t kBankSwapOrder[4] = { 0, 1, 3, 2 };
        const bool swapBanks = (tm >= 8u && tm <= 11u) || (tm >= 14u && tm <= 15u);
        const uint32_t swapWidth = std::max<uint32_t>(1u, (256u * 2u * 8u) / bppBits);

        outLinear.assign(static_cast<size_t>(linearBytes), 0u);
        for (uint32_t y = 0; y < elemH; ++y) {
            for (uint32_t x = 0; x < elemW; ++x) {
                const uint32_t pxIdx = ComputeGx2MicroTilePixelIndex(x & 7u, y & 7u, bppBits, thickness);
                const uint64_t elemOffBytes = static_cast<uint64_t>(pxIdx) * bytesPerElem;
                const uint64_t macroIdx = static_cast<uint64_t>(y / macroTileHeight) * macroTilesPerRow + (x / macroTilePitch);
                const uint64_t macroOff = macroIdx * macroTileBytes;
                const uint64_t totalOff = elemOffBytes + (macroOff >> 3);

                uint32_t pipe = (((y >> 3) ^ (x >> 3)) & 1u) ^ (pipeSwizzle & 1u);
                uint32_t bank = ((((y >> 5) ^ (x >> 3)) & 1u) | ((((y >> 4) ^ (x >> 4)) & 1u) << 1)) ^ (bankSwizzle & 3u);
                if (swapBanks) {
                    bank ^= kBankSwapOrder[(x / swapWidth) & 3u];
                }

                const uint64_t srcOff = ((totalOff >> 8) << 11) |
                                        (static_cast<uint64_t>(bank & 3u) << 9) |
                                        (static_cast<uint64_t>(pipe & 1u) << 8) |
                                        (totalOff & 0xFFull);
                if (srcOff + bytesPerElem > srcLen) return false;
                std::memcpy(
                    outLinear.data() + (static_cast<size_t>(y) * elemW + x) * bytesPerElem,
                    src + static_cast<size_t>(srcOff),
                    bytesPerElem);
            }
        }
        return true;
    }

    return false;
}

} // namespace GpuTex
