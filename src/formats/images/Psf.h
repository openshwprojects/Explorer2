#pragma once
// Psf.h — Linux PC Screen Font (.psf, PSF1 & PSF2) Atlas Renderer

namespace Formats {

inline bool DecodePSF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    uint32_t numGlyphs = 256, bytesPerGlyph = 16, charH = 16, hdrSize = 4;
    if (size >= 4 && data[0] == 0x36 && data[1] == 0x04) {
        numGlyphs = (data[2] & 1) ? 512 : 256;
        bytesPerGlyph = charH = std::max<uint32_t>(1, data[3]);
        hdrSize = 4;
    } else if (size >= 32 && data[0] == 0x72 && data[1] == 0xB5 && data[2] == 0x4A && data[3] == 0x86) {
        std::memcpy(&hdrSize, data + 8, 4);
        std::memcpy(&numGlyphs, data + 16, 4);
        std::memcpy(&bytesPerGlyph, data + 20, 4);
        std::memcpy(&charH, data + 24, 4);
    } else {
        return false;
    }
    if (hdrSize >= size || charH == 0 || charH > 64 || bytesPerGlyph == 0) return false;

    const uint32_t cols = 16, rows = 8, cellW = 12, cellH = 18;
    uint32_t imgW = cols * cellW, imgH = rows * cellH;
    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Linux PC Screen Font (.psf)";
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4, 248);
    for (size_t i = 0; i < static_cast<size_t>(imgW) * imgH; ++i) outInfo.bgraPixels[i * 4 + 3] = 255;

    uint32_t maxG = std::min<uint32_t>(numGlyphs, cols * rows);
    for (uint32_t g = 0; g < maxG; ++g) {
        size_t gOff = hdrSize + static_cast<size_t>(g) * bytesPerGlyph;
        if (gOff + bytesPerGlyph > size) break;
        uint32_t gx = (g % cols) * cellW + 2;
        uint32_t gy = (g / cols) * cellH + 1;
        for (uint32_t r = 0; r < std::min<uint32_t>(charH, 16); ++r) {
            uint8_t bits = data[gOff + r];
            for (uint32_t b = 0; b < 8; ++b) {
                if ((bits >> (7 - b)) & 1) {
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(gy + r) * imgW + (gx + b)) * 4];
                    dst[0] = 235; dst[1] = 99; dst[2] = 37; dst[3] = 255;
                }
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
