#pragma once
// Pvr.h — PowerVR GPU Texture Container (.pvr, v3 "PVR\x03" & Legacy v2 "PVR!") Decoder

namespace Formats {

namespace DetailPvr {

inline uint32_t TwiddleIndex(uint32_t x, uint32_t y, uint32_t wordsX, uint32_t wordsY) {
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
        uint32_t rem = x / minDim;
        idx |= (rem << shift);
    } else if (wordsY > wordsX) {
        uint32_t rem = y / minDim;
        idx |= (rem << shift);
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
        int a[4]; // R, G, B, A
        int b[4]; // R, G, B, A
    };

    size_t numWords = static_cast<size_t>(wordsX) * wordsY;
    std::vector<WordInfo> grid(numWords);
    auto exp3 = [](uint32_t v) { v &= 7u;  return static_cast<int>((v << 5) | (v << 2) | (v >> 1)); };
    auto exp4 = [](uint32_t v) { v &= 15u; return static_cast<int>((v << 4) | v); };
    auto exp5 = [](uint32_t v) { v &= 31u; return static_cast<int>((v << 3) | (v >> 2)); };

    for (uint32_t wy = 0; wy < wordsY; ++wy) {
        for (uint32_t wx = 0; wx < wordsX; ++wx) {
            uint32_t tidx = TwiddleIndex(wx, wy, wordsX, wordsY);
            if (tidx >= numWords) tidx = wy * wordsX + wx;
            const uint8_t* wp = src + static_cast<size_t>(tidx) * 8u;
            uint32_t mod = ReadU32LE(wp);
            uint32_t col = ReadU32LE(wp + 4);

            WordInfo wi = {};
            wi.modData = mod;
            wi.modMode = (col & 1u) != 0;

            if ((col >> 15) & 1u) { // Color A Opaque (5.5.4)
                wi.a[0] = exp5(col >> 10);
                wi.a[1] = exp5(col >> 5);
                wi.a[2] = exp4(col >> 1);
                wi.a[3] = 255;
            } else { // Color A Translucent (3.4.4.3)
                wi.a[3] = exp3(col >> 12);
                wi.a[0] = exp4(col >> 8);
                wi.a[1] = exp4(col >> 4);
                wi.a[2] = exp3(col >> 1);
            }

            if ((col >> 31) & 1u) { // Color B Opaque (5.5.5)
                wi.b[0] = exp5(col >> 26);
                wi.b[1] = exp5(col >> 21);
                wi.b[2] = exp5(col >> 16);
                wi.b[3] = 255;
            } else { // Color B Translucent (3.4.4.4)
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
        if (((lx ^ ly) & 1u) == 0) {
            return directAt(px, py);
        }
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

inline uint8_t ExtractMaskedChannel(uint32_t val, uint32_t mask, uint8_t defaultVal) {
    if (mask == 0) return defaultVal;
    int shift = 0;
    while (((mask >> shift) & 1u) == 0 && shift < 32) ++shift;
    uint32_t shiftedMask = mask >> shift;
    uint32_t raw = (val >> shift) & shiftedMask;
    if (shiftedMask == 0) return defaultVal;
    return static_cast<uint8_t>((static_cast<uint64_t>(raw) * 255u + shiftedMask / 2u) / shiftedMask);
}

inline bool DecodePvrLegacyV2(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 52 || ReadU32LE(data) != 52u || std::memcmp(data + 44, "PVR!", 4) != 0) return false;
    uint32_t h      = ReadU32LE(data + 4);
    uint32_t w      = ReadU32LE(data + 8);
    uint32_t flags  = ReadU32LE(data + 16);
    uint32_t bpp    = ReadU32LE(data + 24);
    uint32_t rMask  = ReadU32LE(data + 28);
    uint32_t gMask  = ReadU32LE(data + 32);
    uint32_t bMask  = ReadU32LE(data + 36);
    uint32_t aMask  = ReadU32LE(data + 40);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    const uint8_t* src = data + 52;
    size_t avail = size - 52;
    uint32_t pf = flags & 0xFFu;
    size_t pixelCount = static_cast<size_t>(w) * h;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"PowerVR Texture v2 (.pvr)";

    if (pf == 0x0C || pf == 0x18 || pf == 0x0D || pf == 0x19) {
        bool is2bpp = (pf == 0x0C || pf == 0x18);
        if (!DecodePvrtcSurface(src, avail, w, h, is2bpp, (aMask == 0 && (flags & 0x8000u) == 0), outInfo.bgraPixels)) {
            return false;
        }
        outInfo.valid = true;
        return true;
    }

    if (pf == 0x20 || pf == 0x21 || pf == 0x22 || pf == 0x23 || pf == 0x24 || pf == 0x36) {
        uint32_t bw = (w + 3u) / 4u, bh = (h + 3u) / 4u;
        size_t stride = (pf == 0x20 || pf == 0x36) ? 8u : 16u;
        if (avail < static_cast<size_t>(bw) * bh * stride) return false;
        outInfo.bgraPixels.resize(pixelCount * 4u);
        uint8_t blk[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                if (pf == 0x20)                     GpuTex::DecodeBC1Block(bp, blk, true, false);
                else if (pf == 0x21 || pf == 0x22)  GpuTex::DecodeBC2Block(bp, blk);
                else if (pf == 0x23 || pf == 0x24)  GpuTex::DecodeBC3Block(bp, blk);
                else                                GpuTex::DecodeETC1Block(bp, blk);
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                bp += stride;
            }
        }
        outInfo.valid = true;
        return true;
    }

    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return false;
    size_t bytesPerPx = bpp / 8u;
    if (avail < pixelCount * bytesPerPx) return false;

    outInfo.bgraPixels.resize(pixelCount * 4u);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* p = src + i * bytesPerPx;
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (pf == 0x06 || pf == 0x16 || (bytesPerPx == 1 && rMask == 0)) {
            r = g = b = p[0];
        } else if (pf == 0x07 || pf == 0x17) {
            r = g = b = p[0]; a = p[1];
        } else {
            uint32_t v = 0;
            if (bytesPerPx == 1) v = p[0];
            else if (bytesPerPx == 2) v = ReadU16LE(p);
            else if (bytesPerPx == 3) v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
            else v = ReadU32LE(p);

            if (rMask == 0 && gMask == 0 && bMask == 0 && bytesPerPx >= 3) {
                r = p[0]; g = p[1]; b = p[2]; a = (bytesPerPx == 4) ? p[3] : 255;
            } else {
                r = ExtractMaskedChannel(v, rMask, 0);
                g = ExtractMaskedChannel(v, gMask, 0);
                b = ExtractMaskedChannel(v, bMask, 0);
                a = ExtractMaskedChannel(v, aMask, 255);
            }
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
    }
    outInfo.valid = true;
    return true;
}

} // namespace DetailPvr

inline bool DecodePVR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size >= 52 && ReadU32LE(data) == 52u && std::memcmp(data + 44, "PVR!", 4) == 0) {
        return DetailPvr::DecodePvrLegacyV2(data, size, outInfo);
    }

    if (size < 52) return false;
    bool le = (std::memcmp(data, "PVR\x03", 4) == 0);
    bool be = (std::memcmp(data, "\x03RVP", 4) == 0);
    if (!le && !be) return false;

    auto rd32 = [&](size_t off) -> uint32_t {
        return be ? ReadU32BE(data + off) : ReadU32LE(data + off);
    };
    uint64_t pixelFormat = be ? ReadU64BE(data + 8) : ReadU64LE(data + 8);
    uint32_t channelType = rd32(20);
    uint32_t h           = rd32(24);
    uint32_t w           = rd32(28);
    uint32_t metaLen     = rd32(48);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || 52u + static_cast<size_t>(metaLen) > size) {
        return false;
    }

    const uint8_t* src = data + 52u + static_cast<size_t>(metaLen);
    size_t avail = size - (52u + static_cast<size_t>(metaLen));
    size_t pixelCount = static_cast<size_t>(w) * h;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"PowerVR Texture v3 (.pvr)";

    // 1. Uncompressed PVR v3 format: upper 32 bits hold bit-widths of up to 4 channels
    if ((pixelFormat >> 32) != 0) {
        uint8_t chName[4] = {
            static_cast<uint8_t>(pixelFormat & 0xFFu),
            static_cast<uint8_t>((pixelFormat >> 8) & 0xFFu),
            static_cast<uint8_t>((pixelFormat >> 16) & 0xFFu),
            static_cast<uint8_t>((pixelFormat >> 24) & 0xFFu)
        };
        uint8_t chBits[4] = {
            static_cast<uint8_t>((pixelFormat >> 32) & 0xFFu),
            static_cast<uint8_t>((pixelFormat >> 40) & 0xFFu),
            static_cast<uint8_t>((pixelFormat >> 48) & 0xFFu),
            static_cast<uint8_t>((pixelFormat >> 56) & 0xFFu)
        };
        uint32_t totalBits = static_cast<uint32_t>(chBits[0]) + chBits[1] + chBits[2] + chBits[3];
        if (totalBits == 0 || (totalBits % 8u) != 0 || totalBits > 128u) return false;
        size_t bpp = totalBits / 8u;
        if (avail < pixelCount * bpp) return false;

        bool isFloat = (channelType == 9 || channelType == 10 || channelType == 11 || channelType == 12);
        if (isFloat && (chBits[0] == 16 || chBits[0] == 32)) {
            std::vector<float> rgba(pixelCount * 4u, 0.0f);
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4 + 3] = 1.0f;
                const uint8_t* px = src + i * bpp;
                size_t bitOff = 0;
                for (int c = 0; c < 4 && chBits[c] > 0; ++c) {
                    float val = 0.0f;
                    if (chBits[c] == 16) val = GpuTex::HalfToFloat(ReadU16LE(px + bitOff / 8u));
                    else if (chBits[c] == 32) val = GpuTex::ReadF32LE(px + bitOff / 8u);
                    bitOff += chBits[c];
                    char cn = static_cast<char>(chName[c]);
                    if (cn == 'r') rgba[i * 4 + 0] = val;
                    else if (cn == 'g') rgba[i * 4 + 1] = val;
                    else if (cn == 'b') rgba[i * 4 + 2] = val;
                    else if (cn == 'a') rgba[i * 4 + 3] = val;
                    else if (cn == 'l' || cn == 'i') {
                        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = val;
                        if (cn == 'i') rgba[i * 4 + 3] = val;
                    }
                }
            }
            outInfo.bitsPerChannel = chBits[0];
            GpuTex::ConvertFloatRgbaToBgra(rgba.data(), w, h, false, outInfo.bgraPixels);
            outInfo.valid = true;
            return true;
        }

        outInfo.bgraPixels.resize(pixelCount * 4u);
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t* px = src + i * bpp;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (bpp <= 8) {
                uint64_t word = 0;
                for (size_t k = 0; k < bpp; ++k) word |= (static_cast<uint64_t>(px[k]) << (8u * k));
                uint32_t bitShift = 0;
                for (int c = 0; c < 4 && chBits[c] > 0; ++c) {
                    uint32_t bits = chBits[c];
                    uint64_t mask = (bits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1u);
                    uint64_t raw = (word >> bitShift) & mask;
                    bitShift += bits;
                    uint8_t v8 = (bits == 8) ? static_cast<uint8_t>(raw)
                                             : static_cast<uint8_t>((raw * 255u + mask / 2u) / std::max<uint64_t>(1u, mask));
                    char cn = static_cast<char>(chName[c]);
                    if (cn == 'r') r = v8;
                    else if (cn == 'g') g = v8;
                    else if (cn == 'b') b = v8;
                    else if (cn == 'a') a = v8;
                    else if (cn == 'l') { r = g = b = v8; }
                    else if (cn == 'i') { r = g = b = a = v8; }
                }
            } else {
                size_t byteOff = 0;
                for (int c = 0; c < 4 && chBits[c] > 0; ++c) {
                    size_t cb = chBits[c] / 8u;
                    uint8_t v8 = (cb >= 1) ? px[byteOff + cb - 1u] : 0;
                    byteOff += cb;
                    char cn = static_cast<char>(chName[c]);
                    if (cn == 'r') r = v8;
                    else if (cn == 'g') g = v8;
                    else if (cn == 'b') b = v8;
                    else if (cn == 'a') a = v8;
                    else if (cn == 'l') { r = g = b = v8; }
                }
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        }
        outInfo.valid = true;
        return true;
    }

    // 2. Compressed PVR v3 enum IDs (< 64)
    uint32_t pf = static_cast<uint32_t>(pixelFormat);
    if (pf <= 3) {
        bool is2bpp = (pf == 0 || pf == 1);
        bool opaque = (pf == 0 || pf == 2);
        if (!DetailPvr::DecodePvrtcSurface(src, avail, w, h, is2bpp, opaque, outInfo.bgraPixels)) {
            return false;
        }
        outInfo.valid = true;
        return true;
    }

    if (pf >= 27 && pf <= 40) {
        static const uint8_t kAstcDims[14][2] = {
            {4,4}, {5,4}, {5,5}, {6,5}, {6,6}, {8,5}, {8,6},
            {8,8}, {10,5}, {10,6}, {10,8}, {10,10}, {12,10}, {12,12}
        };
        uint32_t bx = kAstcDims[pf - 27][0];
        uint32_t by = kAstcDims[pf - 27][1];
        if (!DecodeAstcSurface(src, avail, w, h, bx, by, outInfo.bgraPixels)) return false;
        outInfo.valid = true;
        return true;
    }

    uint32_t bw = (w + 3u) / 4u;
    uint32_t bh = (h + 3u) / 4u;
    size_t blockStride = 0;
    switch (pf) {
    case 6:  // ETC1
    case 7:  // DXT1
    case 12: // BC4
    case 22: // ETC2 RGB
        blockStride = 8;
        break;
    case 8: case 9:   // DXT2 / DXT3
    case 10: case 11: // DXT4 / DXT5
    case 13:          // BC5
    case 15:          // BC7
    case 23:          // ETC2 RGBA
        blockStride = 16;
        break;
    default:
        return false;
    }
    if (avail < static_cast<size_t>(bw) * bh * blockStride) return false;

    outInfo.bgraPixels.resize(pixelCount * 4u);
    uint8_t blk[64];
    const uint8_t* bp = src;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            switch (pf) {
            case 6:           GpuTex::DecodeETC1Block(bp, blk); break;
            case 7:           GpuTex::DecodeBC1Block(bp, blk, true, false); break;
            case 8: case 9:   GpuTex::DecodeBC2Block(bp, blk); break;
            case 10: case 11: GpuTex::DecodeBC3Block(bp, blk); break;
            case 12:          GpuTex::DecodeBC4Block(bp, blk, false); break;
            case 13:          GpuTex::DecodeBC5Block(bp, blk, false); break;
            case 15:          GpuTex::DecodeBC7Block(bp, blk); break;
            case 22:          GpuTex::DecodeETC2Block(bp, blk, false); break;
            case 23:          GpuTex::DecodeETC2Block(bp, blk, true); break;
            }
            GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
            bp += blockStride;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
