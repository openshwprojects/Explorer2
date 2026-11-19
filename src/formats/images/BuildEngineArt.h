#pragma once
// BuildEngineArt.h — Build Engine Tile Sheet (.art) decoder.
#include "../common/RetroQuakeCommon.h"

namespace Formats {
// 5. Build Engine Tile Archive (.art — Duke Nukem 3D / Blood / Shadow Warrior)
inline bool DecodeBuildEngineArt(const uint8_t* data, size_t size, const std::wstring& diskPath, ExtendedImageInfo& outInfo) {
    if (!data || size < 24) return false;
    uint32_t artVersion = RetroTexDetail::RdU32LE(data + 0);
    uint32_t tileStart  = RetroTexDetail::RdU32LE(data + 8);
    uint32_t tileEnd    = RetroTexDetail::RdU32LE(data + 12);
    if (artVersion != 1u || tileEnd < tileStart) return false;
    uint64_t numLocal = static_cast<uint64_t>(tileEnd) - tileStart + 1ull;
    if (numLocal == 0ull || numLocal > 4096ull) return false;

    size_t hdrBytes = 16u + static_cast<size_t>(numLocal) * 8u; // sizx[2*N] + sizy[2*N] + picanm[4*N]
    if (hdrBytes > size) return false;

    const uint8_t* sizXPtr = data + 16u;
    const uint8_t* sizYPtr = sizXPtr + static_cast<size_t>(numLocal) * 2u;

    uint8_t pal[256][3];
    RetroTexDetail::BuildQuakeDefaultPalette(pal, true);
    pal[255][0] = 255; pal[255][1] = 0; pal[255][2] = 255; // Build Engine key index 255

    // Check for companion PALETTE.DAT in the same directory when loaded from disk
    if (!diskPath.empty()) {
        size_t slash = diskPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring palPath = diskPath.substr(0, slash + 1) + L"PALETTE.DAT";
            std::vector<uint8_t> palBytes;
            if (PathUtils::ReadFileBytes(palPath, 65536, palBytes) && palBytes.size() >= 768u) {
                bool isVga6Bit = true;
                for (size_t k = 0; k < 768u; ++k) if (palBytes[k] > 63u) { isVga6Bit = false; break; }
                for (int c = 0; c < 256; ++c) {
                    for (int ch = 0; ch < 3; ++ch) {
                        uint8_t v = palBytes[c * 3 + ch];
                        pal[c][ch] = isVga6Bit ? static_cast<uint8_t>((v << 2) | (v >> 4)) : v;
                    }
                }
            }
        }
    }

    struct ArtTile {
        uint32_t tileId = 0;
        uint16_t w = 0, h = 0;
        const uint8_t* colMajorPx = nullptr;
    };
    std::vector<ArtTile> validTiles;
    size_t pOff = hdrBytes;

    for (size_t i = 0; i < static_cast<size_t>(numLocal); ++i) {
        uint16_t sx = RetroTexDetail::RdU16LE(sizXPtr + i * 2u);
        uint16_t sy = RetroTexDetail::RdU16LE(sizYPtr + i * 2u);
        if (sx == 0 || sy == 0) continue;
        if (sx > 2048 || sy > 2048) return false;
        size_t pxBytes = static_cast<size_t>(sx) * sy;
        if (pxBytes > size - pOff) return false; // Truncated tile payload -> reject strictly!
        if (outInfo.layerNames.size() < 128) {
            outInfo.layerNames.push_back(L"Tile_" + std::to_wstring(tileStart + static_cast<uint32_t>(i)));
        }
        if (validTiles.size() < 16u) {
            validTiles.push_back({ tileStart + static_cast<uint32_t>(i), sx, sy, data + pOff });
        }
        pOff += pxBytes;
    }

    if (validTiles.empty()) return false;

    auto decodeArtTileBgra = [&](const ArtTile& t, std::vector<uint8_t>& outBgra) {
        outBgra.resize(static_cast<size_t>(t.w) * t.h * 4u);
        // Build Engine .ART stores pixels COLUMN-MAJOR: x outer, y inner!
        for (uint16_t x = 0; x < t.w; ++x) {
            const uint8_t* col = t.colMajorPx + static_cast<size_t>(x) * t.h;
            for (uint16_t y = 0; y < t.h; ++y) {
                uint8_t idx = col[y];
                uint8_t a = (idx == 255u) ? 0u : 255u;
                WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * t.w + x) * 4u],
                                pal[idx][0], pal[idx][1], pal[idx][2], a);
            }
        }
    };

    if (validTiles.size() == 1u) {
        outInfo.width = validTiles[0].w;
        outInfo.height = validTiles[0].h;
        outInfo.origWidth = outInfo.width;
        outInfo.origHeight = outInfo.height;
        outInfo.channels = 4u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = L"Build Engine ART Tile (.art)";
        decodeArtTileBgra(validTiles[0], outInfo.bgraPixels);
        outInfo.valid = true;
        return true;
    }

    uint32_t cols = (validTiles.size() <= 4u) ? 2u : 4u;
    uint32_t rows = static_cast<uint32_t>((validTiles.size() + cols - 1u) / cols);
    uint32_t cellW = 0, cellH = 0;
    for (const auto& t : validTiles) {
        cellW = std::max<uint32_t>(cellW, std::min<uint32_t>(t.w, 256u));
        cellH = std::max<uint32_t>(cellH, std::min<uint32_t>(t.h, 256u));
    }
    const uint32_t pad = 4u;
    uint32_t atlasW = cols * cellW + (cols - 1u) * pad;
    uint32_t atlasH = rows * cellH + (rows - 1u) * pad;
    outInfo.bgraPixels.assign(static_cast<size_t>(atlasW) * atlasH * 4u, 0u);

    std::vector<uint8_t> tileBgra;
    for (size_t ti = 0; ti < validTiles.size(); ++ti) {
        const auto& t = validTiles[ti];
        decodeArtTileBgra(t, tileBgra);
        uint32_t gx = static_cast<uint32_t>(ti % cols) * (cellW + pad);
        uint32_t gy = static_cast<uint32_t>(ti / cols) * (cellH + pad);
        uint32_t copyW = std::min<uint32_t>(t.w, cellW);
        uint32_t copyH = std::min<uint32_t>(t.h, cellH);
        for (uint32_t y = 0; y < copyH; ++y) {
            for (uint32_t x = 0; x < copyW; ++x) {
                std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(gy + y) * atlasW + (gx + x)) * 4u],
                            &tileBgra[(static_cast<size_t>(y) * t.w + x) * 4u], 4u);
            }
        }
    }

    outInfo.width = atlasW;
    outInfo.height = atlasH;
    outInfo.origWidth = atlasW;
    outInfo.origHeight = atlasH;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Build Engine Tile Atlas (.art, " + std::to_wstring(outInfo.layerNames.size()) + L" tiles)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
