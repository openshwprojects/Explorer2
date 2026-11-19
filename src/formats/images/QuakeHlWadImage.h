#pragma once
// QuakeHlWadImage.h — Quake WAD2 & Half-Life WAD3 Texture Atlas preview decoder (.wad).
#include "../common/RetroQuakeCommon.h"

namespace Formats {
// 2. Quake WAD2 & Half-Life WAD3 Texture Sheet Preview (.wad)
inline bool DecodeWad2OrWad3Atlas(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 12) return false;
    bool isWad2 = (std::memcmp(data, "WAD2", 4) == 0);
    bool isWad3 = (std::memcmp(data, "WAD3", 4) == 0);
    if (!isWad2 && !isWad3) return false;

    uint32_t numLumps = RetroTexDetail::RdU32LE(data + 4);
    uint32_t dirOff   = RetroTexDetail::RdU32LE(data + 8);
    if (numLumps == 0 || numLumps > 65536u || dirOff < 12u || dirOff > size ||
        static_cast<uint64_t>(numLumps) * 32ull > size - dirOff) {
        return false;
    }

    uint8_t globalPal[256][3];
    RetroTexDetail::BuildQuakeDefaultPalette(globalPal, false);

    // First pass: check if WAD2 contains an explicit PALETTE lump (type 0x40, 768 bytes)
    for (uint32_t i = 0; i < numLumps; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 32u;
        uint32_t fPos = RetroTexDetail::RdU32LE(ent + 0);
        uint32_t dSz  = RetroTexDetail::RdU32LE(ent + 4);
        uint8_t  type = ent[12];
        uint8_t  comp = ent[13];
        if (comp == 0u && fPos <= size && dSz <= size - fPos && dSz >= 768u && type == 0x40u) {
            for (int c = 0; c < 256; ++c) {
                globalPal[c][0] = data[fPos + c * 3 + 0];
                globalPal[c][1] = data[fPos + c * 3 + 1];
                globalPal[c][2] = data[fPos + c * 3 + 2];
            }
            break;
        }
    }

    struct DecodedTile {
        std::wstring name;
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> bgra;
        bool hasAlpha = false;
    };
    std::vector<DecodedTile> tiles;

    for (uint32_t i = 0; i < numLumps; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 32u;
        uint32_t fPos = RetroTexDetail::RdU32LE(ent + 0);
        uint32_t dSz  = RetroTexDetail::RdU32LE(ent + 4);
        uint8_t  type = ent[12];
        uint8_t  comp = ent[13];
        if (comp != 0u || fPos > size || dSz > size - fPos || dSz < 8u) continue;

        std::string lumpName;
        RetroTexDetail::IsPrintableLumpName(ent + 16, 16, lumpName);
        std::wstring wName = PathUtils::Utf8ToWide(lumpName);
        if (!wName.empty() && outInfo.layerNames.size() < 128) {
            outInfo.layerNames.push_back(wName);
        }
        if (tiles.size() >= 16) continue;

        const uint8_t* lp = data + fPos;
        if ((type == 0x43u || type == 0x44u) && dSz >= 40u) { // WAD3 / WAD2 miptex (40-byte header)
            uint32_t tw = RetroTexDetail::RdU32LE(lp + 16);
            uint32_t th = RetroTexDetail::RdU32LE(lp + 20);
            uint32_t o0 = RetroTexDetail::RdU32LE(lp + 24);
            uint32_t o3 = RetroTexDetail::RdU32LE(lp + 36);
            if (tw == 0 || th == 0 || tw > 2048 || th > 2048) continue;
            size_t pxCnt = static_cast<size_t>(tw) * th;
            if (o0 < 40u || o0 > dSz || pxCnt > dSz - o0) continue;

            uint8_t localPal[256][3];
            std::memcpy(localPal, globalPal, sizeof(localPal));

            // Half-Life WAD3 miptex (0x43) stores uint16_t palColors + RGB palette immediately after mip3!
            if ((isWad3 || type == 0x43u) && o3 >= 40u && (tw >= 8u && th >= 8u)) {
                size_t mip3End = static_cast<size_t>(o3) + (static_cast<size_t>(tw >> 3) * (th >> 3));
                if (mip3End + 2u <= dSz) {
                    uint16_t nCols = RetroTexDetail::RdU16LE(lp + mip3End);
                    if (nCols > 0 && nCols <= 256u && mip3End + 2u + static_cast<size_t>(nCols) * 3u <= dSz) {
                        const uint8_t* pp = lp + mip3End + 2u;
                        for (uint16_t c = 0; c < nCols; ++c) {
                            localPal[c][0] = pp[c * 3 + 0];
                            localPal[c][1] = pp[c * 3 + 1];
                            localPal[c][2] = pp[c * 3 + 2];
                        }
                    }
                }
            }

            bool alphaKey = (!lumpName.empty() && lumpName[0] == '{');
            DecodedTile dt;
            dt.name = wName;
            dt.w = tw;
            dt.h = th;
            dt.hasAlpha = alphaKey;
            dt.bgra.resize(pxCnt * 4u);
            for (size_t p = 0; p < pxCnt; ++p) {
                uint8_t idx = lp[o0 + p];
                uint8_t a = (alphaKey && idx == 255u) ? 0u : 255u;
                WritePremulBGRA(&dt.bgra[p * 4u], localPal[idx][0], localPal[idx][1], localPal[idx][2], a);
            }
            tiles.push_back(std::move(dt));
        } else if (type == 0x42u && dSz >= 8u) { // qpic_t (width, height, indices, optional WAD3 palette)
            uint32_t tw = RetroTexDetail::RdU32LE(lp + 0);
            uint32_t th = RetroTexDetail::RdU32LE(lp + 4);
            size_t pxCnt = static_cast<size_t>(tw) * th;
            if (tw == 0 || th == 0 || tw > 2048 || th > 2048 || pxCnt > dSz - 8u) continue;

            uint8_t localPal[256][3];
            std::memcpy(localPal, globalPal, sizeof(localPal));
            if (isWad3 && 8u + pxCnt + 2u <= dSz) {
                uint16_t nCols = RetroTexDetail::RdU16LE(lp + 8u + pxCnt);
                if (nCols > 0 && nCols <= 256u && 8u + pxCnt + 2u + static_cast<size_t>(nCols) * 3u <= dSz) {
                    const uint8_t* pp = lp + 8u + pxCnt + 2u;
                    for (uint16_t c = 0; c < nCols; ++c) {
                        localPal[c][0] = pp[c * 3 + 0];
                        localPal[c][1] = pp[c * 3 + 1];
                        localPal[c][2] = pp[c * 3 + 2];
                    }
                }
            }
            DecodedTile dt;
            dt.name = wName;
            dt.w = tw;
            dt.h = th;
            dt.hasAlpha = true;
            dt.bgra.resize(pxCnt * 4u);
            for (size_t p = 0; p < pxCnt; ++p) {
                uint8_t idx = lp[8u + p];
                uint8_t a = (idx == 255u) ? 0u : 255u;
                WritePremulBGRA(&dt.bgra[p * 4u], localPal[idx][0], localPal[idx][1], localPal[idx][2], a);
            }
            tiles.push_back(std::move(dt));
        }
    }

    if (tiles.empty()) return false;

    if (tiles.size() == 1u) {
        outInfo.width = tiles[0].w;
        outInfo.height = tiles[0].h;
        outInfo.origWidth = tiles[0].w;
        outInfo.origHeight = tiles[0].h;
        outInfo.channels = tiles[0].hasAlpha ? 4u : 3u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = isWad3 ? L"Half-Life WAD3 Texture (.wad)" : L"Quake WAD2 Texture (.wad)";
        outInfo.bgraPixels = std::move(tiles[0].bgra);
        outInfo.valid = true;
        return true;
    }

    // Multi-texture contact sheet grid (packed cleanly with 4px gutter)
    uint32_t cols = (tiles.size() <= 2u) ? 2u : (tiles.size() <= 4u) ? 2u : (tiles.size() <= 9u) ? 3u : 4u;
    uint32_t rows = static_cast<uint32_t>((tiles.size() + cols - 1u) / cols);
    uint32_t cellW = 0, cellH = 0;
    for (const auto& t : tiles) {
        cellW = std::max(cellW, std::min<uint32_t>(t.w, 256u));
        cellH = std::max(cellH, std::min<uint32_t>(t.h, 256u));
    }
    const uint32_t pad = 4u;
    uint32_t atlasW = cols * cellW + (cols - 1u) * pad;
    uint32_t atlasH = rows * cellH + (rows - 1u) * pad;
    outInfo.bgraPixels.resize(static_cast<size_t>(atlasW) * atlasH * 4u);
    for (size_t i = 0; i < static_cast<size_t>(atlasW) * atlasH; ++i) {
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], 24, 28, 36, 255);
    }

    for (size_t ti = 0; ti < tiles.size(); ++ti) {
        const auto& t = tiles[ti];
        uint32_t gx = static_cast<uint32_t>(ti % cols) * (cellW + pad);
        uint32_t gy = static_cast<uint32_t>(ti / cols) * (cellH + pad);
        uint32_t copyW = std::min(t.w, cellW);
        uint32_t copyH = std::min(t.h, cellH);
        for (uint32_t y = 0; y < copyH; ++y) {
            for (uint32_t x = 0; x < copyW; ++x) {
                const uint8_t* sp = &t.bgra[(static_cast<size_t>(y) * t.w + x) * 4u];
                uint8_t* dp = &outInfo.bgraPixels[(static_cast<size_t>(gy + y) * atlasW + (gx + x)) * 4u];
                if (sp[3] == 255u) {
                    std::memcpy(dp, sp, 4u);
                } else if (sp[3] > 0u) {
                    uint8_t inv = static_cast<uint8_t>(255u - sp[3]);
                    dp[0] = static_cast<uint8_t>(sp[0] + (dp[0] * inv) / 255u);
                    dp[1] = static_cast<uint8_t>(sp[1] + (dp[1] * inv) / 255u);
                    dp[2] = static_cast<uint8_t>(sp[2] + (dp[2] * inv) / 255u);
                    dp[3] = 255u;
                }
            }
        }
    }

    outInfo.width = atlasW;
    outInfo.height = atlasH;
    outInfo.origWidth = atlasW;
    outInfo.origHeight = atlasH;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = (isWad3 ? L"Half-Life WAD3 Texture Sheet (" : L"Quake WAD2 Texture Sheet (") +
                         std::to_wstring(outInfo.layerNames.size()) + L" lumps)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
