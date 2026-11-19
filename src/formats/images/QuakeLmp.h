#pragma once
// QuakeLmp.h — Quake Picture / Palette Lump (.lmp) decoder.
#include "../common/RetroQuakeCommon.h"

namespace Formats {
// 4. Quake Picture / Palette Lump (.lmp)
inline bool DecodeQuakeLmp(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 12) return false;

    // Case A: Exact 768-byte Quake palette.lmp (256 RGB triplets -> 16x16 swatch LUT)
    if (size == 768u) {
        outInfo.width = 16u;
        outInfo.height = 16u;
        outInfo.origWidth = 16u;
        outInfo.origHeight = 16u;
        outInfo.channels = 3u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = L"Quake 256-Color Palette Lump (.lmp)";
        outInfo.bgraPixels.resize(256u * 4u);
        for (size_t i = 0; i < 256u; ++i) {
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], data[i * 3u + 0u], data[i * 3u + 1u], data[i * 3u + 2u], 255u);
        }
        outInfo.valid = true;
        return true;
    }

    // Case B: qpic_t (uint32 width, uint32 height, width*height indices, optional trailing uint16_t 256 + 768B palette)
    uint32_t w = RetroTexDetail::RdU32LE(data + 0);
    uint32_t h = RetroTexDetail::RdU32LE(data + 4);
    if (w > 0 && w <= 4096 && h > 0 && h <= 4096) {
        uint64_t pxCnt = static_cast<uint64_t>(w) * h;
        bool exactQpic = (8ull + pxCnt == size);
        bool wad3Qpic  = (8ull + pxCnt + 2ull <= size &&
                          RetroTexDetail::RdU16LE(data + 8u + static_cast<size_t>(pxCnt)) == 256u &&
                          8ull + pxCnt + 2ull + 768ull <= size &&
                          8ull + pxCnt + 2ull + 772ull >= size);
        if (exactQpic || wad3Qpic) {
            uint8_t pal[256][3];
            RetroTexDetail::BuildQuakeDefaultPalette(pal, false);
            if (wad3Qpic) {
                const uint8_t* pp = data + 8u + static_cast<size_t>(pxCnt) + 2u;
                for (int c = 0; c < 256; ++c) {
                    pal[c][0] = pp[c * 3 + 0];
                    pal[c][1] = pp[c * 3 + 1];
                    pal[c][2] = pp[c * 3 + 2];
                }
            }
            outInfo.width = w;
            outInfo.height = h;
            outInfo.origWidth = w;
            outInfo.origHeight = h;
            outInfo.channels = 4u;
            outInfo.bitsPerChannel = 8u;
            outInfo.formatName = L"Quake qpic_t Graphic Lump (.lmp)";
            outInfo.bgraPixels.resize(static_cast<size_t>(pxCnt) * 4u);
            for (size_t i = 0; i < static_cast<size_t>(pxCnt); ++i) {
                uint8_t idx = data[8u + i];
                uint8_t a = (idx == 255u) ? 0u : 255u;
                WritePremulBGRA(&outInfo.bgraPixels[i * 4u], pal[idx][0], pal[idx][1], pal[idx][2], a);
            }
            outInfo.valid = true;
            return true;
        }
    }

    // Case C: Exact 16384-byte conchars.lmp (128x128)
    if (size == 16384u) {
        uint8_t pal[256][3];
        RetroTexDetail::BuildQuakeDefaultPalette(pal, false);
        outInfo.width = 128u;
        outInfo.height = 128u;
        outInfo.origWidth = 128u;
        outInfo.origHeight = 128u;
        outInfo.channels = 4u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = L"Quake Console Font / Colormap Lump (.lmp)";
        outInfo.bgraPixels.resize(16384u * 4u);
        for (size_t i = 0; i < 16384u; ++i) {
            uint8_t idx = data[i];
            uint8_t a = (idx == 0u || idx == 255u) ? 0u : 255u;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], pal[idx][0], pal[idx][1], pal[idx][2], a);
        }
        outInfo.valid = true;
        return true;
    }

    return false;
}

} // namespace Formats
