#pragma once
// ConsolePixelFormats.h — Shared DRY 16-bit console pixel unpackers (ABGR1555, ARGB1555, BGR565,
// ABGR4444, ARGB4444) and Morton 2D Z-order twiddle helpers for PS1/PS2/PSP/Dreamcast/3DS/Xbox textures.

namespace Formats::ConsolePx {

inline void UnpackAbgr1555(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a, bool zeroIsTransparentOnly = false) {
    r = static_cast<uint8_t>(((px >>  0) & 0x1Fu) * 255u / 31u);
    g = static_cast<uint8_t>(((px >>  5) & 0x1Fu) * 255u / 31u);
    b = static_cast<uint8_t>(((px >> 10) & 0x1Fu) * 255u / 31u);
    if (zeroIsTransparentOnly) {
        a = (px == 0u) ? 0u : 255u;
    } else {
        a = ((px & 0x8000u) != 0u) ? 255u : 0u;
    }
}

inline void UnpackArgb1555(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    r = static_cast<uint8_t>(((px >> 10) & 0x1Fu) * 255u / 31u);
    g = static_cast<uint8_t>(((px >>  5) & 0x1Fu) * 255u / 31u);
    b = static_cast<uint8_t>(((px >>  0) & 0x1Fu) * 255u / 31u);
    a = ((px & 0x8000u) != 0u) ? 255u : 0u;
}

inline void UnpackBgr565(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((px >>  0) & 0x1Fu) * 255u / 31u);
    g = static_cast<uint8_t>(((px >>  5) & 0x3Fu) * 255u / 63u);
    b = static_cast<uint8_t>(((px >> 11) & 0x1Fu) * 255u / 31u);
}

inline void UnpackAbgr4444(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    r = static_cast<uint8_t>(((px >>  0) & 0x0Fu) * 17u);
    g = static_cast<uint8_t>(((px >>  4) & 0x0Fu) * 17u);
    b = static_cast<uint8_t>(((px >>  8) & 0x0Fu) * 17u);
    a = static_cast<uint8_t>(((px >> 12) & 0x0Fu) * 17u);
}

inline void UnpackArgb4444(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    a = static_cast<uint8_t>(((px >> 12) & 0x0Fu) * 17u);
    r = static_cast<uint8_t>(((px >>  8) & 0x0Fu) * 17u);
    g = static_cast<uint8_t>(((px >>  4) & 0x0Fu) * 17u);
    b = static_cast<uint8_t>(((px >>  0) & 0x0Fu) * 17u);
}

// Interleaves low 16 bits of x and y for Morton Z-order swizzled console/GPU textures
inline uint32_t Part1By1(uint32_t v) {
    v &= 0x0000FFFFu;
    v = (v ^ (v << 8)) & 0x00FF00FFu;
    v = (v ^ (v << 4)) & 0x0F0F0F0Fu;
    v = (v ^ (v << 2)) & 0x33333333u;
    v = (v ^ (v << 1)) & 0x55555555u;
    return v;
}

inline uint32_t MortonIndex2D(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    // Supports rectangular power-of-2 textures via min(width, height) square Morton blocks
    const uint32_t minDim = std::min(width, height);
    if (minDim == 0u || (minDim & (minDim - 1u)) != 0u) {
        return y * width + x;
    }
    const uint32_t mask = minDim - 1u;
    const uint32_t blockPix = minDim * minDim;
    // Standard Z-order: x in even bits, y in odd bits
    const uint32_t inBlock = Part1By1(x & mask) | (Part1By1(y & mask) << 1);
    if (width >= height) {
        return (x / minDim) * blockPix + inBlock;
    }
    return (y / minDim) * blockPix + inBlock;
}

// Shared linear S3TC (1 = BC1/DXT1, 2 = BC2/DXT3, 3 = BC3/DXT5) surface -> premultiplied BGRA8 helper
inline bool DecodeLinearS3tcToPbgra(
    const uint8_t* src,
    size_t srcBytes,
    uint32_t width,
    uint32_t height,
    int bcKind,
    std::vector<uint8_t>& outBgra)
{
    if (!src || width == 0u || height == 0u || width > kMaxRasterDimension || height > kMaxRasterDimension) {
        return false;
    }
    if (bcKind < 1 || bcKind > 3) return false;
    const size_t blkBytes = (bcKind == 1) ? 8u : 16u;
    const uint64_t bxCount = (static_cast<uint64_t>(width)  + 3ULL) / 4ULL;
    const uint64_t byCount = (static_cast<uint64_t>(height) + 3ULL) / 4ULL;
    const uint64_t needed  = bxCount * byCount * blkBytes;
    if (needed > srcBytes || static_cast<uint64_t>(width) * height > 64ULL * 1024ULL * 1024ULL) {
        return false;
    }

    outBgra.assign(static_cast<size_t>(width) * height * 4u, 0u);
    uint8_t blkRgba[64]{};
    size_t off = 0u;
    for (uint32_t by = 0u; by < static_cast<uint32_t>(byCount); ++by) {
        for (uint32_t bx = 0u; bx < static_cast<uint32_t>(bxCount); ++bx) {
            if      (bcKind == 1) GpuTex::DecodeBC1Block(src + off, blkRgba, true, false);
            else if (bcKind == 2) GpuTex::DecodeBC2Block(src + off, blkRgba);
            else                  GpuTex::DecodeBC3Block(src + off, blkRgba);
            off += blkBytes;

            for (uint32_t py = 0u; py < 4u; ++py) {
                const uint32_t dy = by * 4u + py;
                if (dy >= height) continue;
                for (uint32_t px = 0u; px < 4u; ++px) {
                    const uint32_t dx = bx * 4u + px;
                    if (dx >= width) continue;
                    const uint8_t* s = &blkRgba[(py * 4u + px) * 4u];
                    WritePremulBGRA(&outBgra[(static_cast<size_t>(dy) * width + dx) * 4u], s[0], s[1], s[2], s[3]);
                }
            }
        }
    }
    return true;
}

inline bool FinalizeConsoleImageInfo(
    ExtendedImageInfo& outInfo,
    uint32_t width,
    uint32_t height,
    std::vector<uint8_t>&& bgraPixels,
    std::wstring formatName,
    uint32_t maxDimension = 0u)
{
    if (width == 0u || height == 0u || bgraPixels.size() != static_cast<size_t>(width) * height * 4u) {
        return false;
    }
    outInfo = {};
    outInfo.width          = width;
    outInfo.height         = height;
    outInfo.origWidth      = width;
    outInfo.origHeight     = height;
    outInfo.channels       = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels     = std::move(bgraPixels);
    outInfo.formatName     = std::move(formatName);
    outInfo.valid          = true;
    DownscaleIfRequested(maxDimension, outInfo);
    return true;
}

} // namespace Formats::ConsolePx
