#pragma once
// Psf.h — Linux PC Screen Font (.psf, PSF1 & PSF2) Arbitrary-Dimension 256/512-Glyph Atlas Renderer

namespace Formats {

inline bool DecodePSF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 4) return false;
    uint32_t numGlyphs = 256u, bytesPerGlyph = 16u, charH = 16u, charW = 8u, hdrSize = 4u;

    if (size >= 4 && data[0] == 0x36 && data[1] == 0x04) {
        numGlyphs = (data[2] & 1u) ? 512u : 256u;
        charW = 8u;
        bytesPerGlyph = charH = data[3];
        hdrSize = 4u;
    } else if (size >= 32 && data[0] == 0x72 && data[1] == 0xB5 && data[2] == 0x4A && data[3] == 0x86) {
        const uint32_t ver = ReadU32LE(data + 4);
        if (ver > 0u) return false;
        hdrSize       = ReadU32LE(data + 8);
        numGlyphs     = ReadU32LE(data + 16);
        bytesPerGlyph = ReadU32LE(data + 20);
        charH         = ReadU32LE(data + 24);
        charW         = ReadU32LE(data + 28);
    } else {
        return false;
    }

    if (hdrSize >= size || numGlyphs == 0 || charW == 0 || charW > 128u || charH == 0 || charH > 128u) return false;
    const uint32_t bytesPerRow = (charW + 7u) / 8u;
    if (bytesPerGlyph < bytesPerRow * charH) return false;
    if (hdrSize + bytesPerGlyph > size) return false;

    const uint32_t availGlyphs = static_cast<uint32_t>((size - hdrSize) / bytesPerGlyph);
    const uint32_t maxG = std::min({ numGlyphs, availGlyphs, 512u });
    if (maxG == 0u) return false;

    const uint32_t cols = 16u;
    const uint32_t rows = (maxG + cols - 1u) / cols;
    const uint32_t cellW = charW + 4u;
    const uint32_t cellH = charH + 4u;
    const uint32_t imgW = cols * cellW;
    const uint32_t imgH = rows * cellH;

    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = imgW;
    outInfo.origHeight = imgH;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Linux PC Screen Font (.psf)";
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4u, 248u);
    for (size_t i = 0; i < static_cast<size_t>(imgW) * imgH; ++i) outInfo.bgraPixels[i * 4u + 3u] = 255u;

    for (uint32_t g = 0; g < maxG; ++g) {
        const size_t gOff = hdrSize + static_cast<size_t>(g) * bytesPerGlyph;
        if (gOff + bytesPerGlyph > size) break;
        const uint32_t gx = (g % cols) * cellW + 2u;
        const uint32_t gy = (g / cols) * cellH + 2u;
        for (uint32_t r = 0; r < charH; ++r) {
            const uint8_t* rowPtr = data + gOff + static_cast<size_t>(r) * bytesPerRow;
            for (uint32_t b = 0; b < charW; ++b) {
                if ((rowPtr[b >> 3] & (0x80u >> (b & 7u))) != 0u) {
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(gy + r) * imgW + (gx + b)) * 4u];
                    dst[0] = 235u; dst[1] = 99u; dst[2] = 37u; dst[3] = 255u;
                }
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
