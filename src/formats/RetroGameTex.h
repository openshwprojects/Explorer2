#pragma once
// RetroGameTex.h — #62 Classic Quake / Half-Life / Build Engine Textures, Sprites & WAD2/WAD3 Archives
// Covers:
//   - Quake II Mipmapped Textures (.wal)
//   - Quake WAD2 ("WAD2") & Half-Life WAD3 ("WAD3") Texture Archives (.wad) — BOTH Image Atlas & Archive Explorer!
//   - Quake v1 & Half-Life v2 Sprites (.spr, magic "IDSP")
//   - Quake Picture & Palette Lumps (.lmp — qpic_t, 768B palette.lmp, 16384B conchars/colormap)
//   - Ken Silverman Build Engine Tile Archives (.art — column-major TILES000.ART + optional PALETTE.DAT)

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace Formats {
namespace RetroTexDetail {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(RdU32LE(p));
}

// Procedural reconstruction of id Software's classic 16-ramp Quake / Quake II 256-color palette
// (faithful 16-shade tonal ramps: grey, brown, sky-blue, olive-green, blood-red, gold, tan, violet, fire/lava, 255=key)
inline void BuildQuakeDefaultPalette(uint8_t pal[256][3], bool isQuake2) {
    static constexpr uint8_t kRampEnds[16][6] = {
        {  15,  15,  15,  235, 235, 235 }, // 0..15:   Neutral grey
        {  35,  25,  15,  165, 120,  75 }, // 16..31:  Earth brown
        {  20,  30,  55,  145, 175, 235 }, // 32..47:  Slate / sky blue
        {  20,  40,  18,  130, 195,  95 }, // 48..63:  Moss / slime green
        {  55,  12,  12,  230,  55,  45 }, // 64..79:  Crimson / blood red
        {  55,  42,  14,  240, 200,  75 }, // 80..95:  Bronze / gold
        {  65,  45,  32,  225, 175, 135 }, // 96..111: Flesh / sandstone
        {  42,  24,  58,  190, 135, 225 }, // 112..127: Runic purple
        { 185, 145, 165,   35,  22,  30 }, // 128..143: Reversed mauve ramp
        { 195, 135, 105,   42,  24,  18 }, // 144..159: Reversed rust ramp
        { 205, 185, 135,   45,  38,  24 }, // 160..175: Reversed khaki ramp
        { 145, 185, 165,   22,  42,  34 }, // 176..191: Reversed teal-metal ramp
        { 245, 225,  55,   65,  35,   8 }, // 192..207: Torch yellow -> ember
        {  25,  55, 225,  185, 215, 255 }, // 208..223: Plasma / electric blue
        {  85,  18,   8,  255, 235, 145 }, // 224..239: Lava / fireball ramp
        {  45,  95,  45,  250, 250, 220 }  // 240..254: Fullbright accents
    };
    for (int r = 0; r < 16; ++r) {
        for (int s = 0; s < 16; ++s) {
            int idx = r * 16 + s;
            int r0 = kRampEnds[r][0], g0 = kRampEnds[r][1], b0 = kRampEnds[r][2];
            int r1 = kRampEnds[r][3], g1 = kRampEnds[r][4], b1 = kRampEnds[r][5];
            if (isQuake2 && r == 3) { g1 = std::min(255, g1 + 25); }
            pal[idx][0] = static_cast<uint8_t>(r0 + ((r1 - r0) * s) / 15);
            pal[idx][1] = static_cast<uint8_t>(g0 + ((g1 - g0) * s) / 15);
            pal[idx][2] = static_cast<uint8_t>(b0 + ((b1 - b0) * s) / 15);
        }
    }
    pal[255][0] = 159; pal[255][1] = 91; pal[255][2] = 83; // Classic Quake/Build transparent key
}

inline bool IsPrintableLumpName(const uint8_t* p, size_t maxLen, std::string& outName) {
    outName.clear();
    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t c = p[i];
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) return false;
        outName.push_back(static_cast<char>(c));
    }
    return !outName.empty();
}

} // namespace RetroTexDetail
} // namespace Formats

#if defined(E2_RETROTEX_ENABLE_IMAGE)
namespace Formats {

// 1. Quake II Mipmapped Texture (.wal) — 100-byte miptex_wal_t header + 4 mip offsets
inline bool DecodeQuake2Wal(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 100) return false;
    std::string texName;
    // Note: some Quake II .wal files leave name[0] == 0, allow empty or printable ASCII name
    for (size_t i = 0; i < 32; ++i) {
        uint8_t c = data[i];
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) return false;
        texName.push_back(static_cast<char>(c));
    }

    uint32_t w    = RetroTexDetail::RdU32LE(data + 32);
    uint32_t h    = RetroTexDetail::RdU32LE(data + 36);
    uint32_t off0 = RetroTexDetail::RdU32LE(data + 40);
    uint32_t off1 = RetroTexDetail::RdU32LE(data + 44);
    uint32_t off2 = RetroTexDetail::RdU32LE(data + 48);
    uint32_t off3 = RetroTexDetail::RdU32LE(data + 52);
    uint32_t flags= RetroTexDetail::RdU32LE(data + 88);

    if (w == 0 || h == 0 || w > 4096 || h > 4096 || (w & 7u) != 0u || (h & 7u) != 0u) {
        return false;
    }
    size_t mip0Bytes = static_cast<size_t>(w) * h;
    if (off0 < 100u || off0 > size || mip0Bytes > size - off0) return false;
    // Validate sub-mip offsets if present
    if (off1 != 0u && (off1 < 100u || off1 + (mip0Bytes >> 2) > size)) return false;
    if (off2 != 0u && (off2 < 100u || off2 + (mip0Bytes >> 4) > size)) return false;
    if (off3 != 0u && (off3 < 100u || off3 + (mip0Bytes >> 6) > size)) return false;

    uint8_t pal[256][3];
    RetroTexDetail::BuildQuakeDefaultPalette(pal, true);

    bool hasKeyAlpha = (!texName.empty() && texName[0] == '{') || ((flags & 0x30u) != 0u);
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = hasKeyAlpha ? 4u : 3u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Quake II WAL Texture (.wal" +
        (texName.empty() ? std::wstring() : (L", " + PathUtils::Utf8ToWide(texName))) + L")";
    outInfo.bgraPixels.resize(mip0Bytes * 4u);

    const uint8_t* src = data + off0;
    for (size_t i = 0; i < mip0Bytes; ++i) {
        uint8_t idx = src[i];
        uint8_t a = (hasKeyAlpha && idx == 255u) ? 0u : 255u;
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], pal[idx][0], pal[idx][1], pal[idx][2], a);
    }
    outInfo.valid = true;
    return true;
}

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

// 3. Quake v1 & Half-Life v2 Sprite (.spr, magic "IDSP")
inline bool DecodeQuakeOrHlSprite(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 44 || std::memcmp(data, "IDSP", 4) != 0) return false;
    uint32_t version = RetroTexDetail::RdU32LE(data + 4);
    if (version != 1u && version != 2u) return false;

    uint8_t pal[256][3];
    RetroTexDetail::BuildQuakeDefaultPalette(pal, false);
    uint32_t texFormat = 0u; // 0=NORMAL, 1=ADDITIVE, 2=INDEXALPHA, 3=ALPHTEST
    uint32_t numFrames = 0u;
    size_t pos = 0;

    if (version == 1u) {
        numFrames = RetroTexDetail::RdU32LE(data + 24);
        pos = 36u;
    } else { // Half-Life SPR v2
        texFormat = RetroTexDetail::RdU32LE(data + 12);
        numFrames = RetroTexDetail::RdU32LE(data + 28);
        if (size < 42u) return false;
        uint16_t palColors = RetroTexDetail::RdU16LE(data + 40);
        if (palColors == 0u || palColors > 256u) return false;
        pos = 42u + static_cast<size_t>(palColors) * 3u;
        if (pos > size) return false;
        for (uint16_t c = 0; c < palColors; ++c) {
            pal[c][0] = data[42u + c * 3u + 0u];
            pal[c][1] = data[42u + c * 3u + 1u];
            pal[c][2] = data[42u + c * 3u + 2u];
        }
    }
    if (numFrames == 0u || numFrames > 4096u || pos + 4u > size) return false;

    uint32_t frameGroup = RetroTexDetail::RdU32LE(data + pos);
    pos += 4u;
    if (frameGroup != 0u) {
        // Group frame: int32 numSubFrames + float intervals[numSubFrames]
        if (pos + 4u > size) return false;
        uint32_t subCount = RetroTexDetail::RdU32LE(data + pos);
        pos += 4u;
        if (subCount == 0u || subCount > 1024u || static_cast<uint64_t>(subCount) * 4ull > size - pos) return false;
        pos += static_cast<size_t>(subCount) * 4u;
    }

    if (pos + 16u > size) return false;
    int32_t fw = RetroTexDetail::RdI32LE(data + pos + 8);
    int32_t fh = RetroTexDetail::RdI32LE(data + pos + 12);
    pos += 16u;
    if (fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096) return false;
    size_t pxCnt = static_cast<size_t>(fw) * static_cast<size_t>(fh);
    if (pxCnt > size - pos) return false;

    const uint8_t* indices = data + pos;
    outInfo.width = static_cast<uint32_t>(fw);
    outInfo.height = static_cast<uint32_t>(fh);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = (version == 2u)
        ? (L"Half-Life Sprite v2 (.spr, " + std::to_wstring(numFrames) + L" frames)")
        : (L"Quake Sprite v1 (.spr, " + std::to_wstring(numFrames) + L" frames)");
    outInfo.bgraPixels.resize(pxCnt * 4u);

    for (size_t i = 0; i < pxCnt; ++i) {
        uint8_t idx = indices[i];
        uint8_t r = pal[idx][0], g = pal[idx][1], b = pal[idx][2], a = 255u;
        if (version == 1u) {
            if (idx == 255u) a = 0u;
        } else if (texFormat == 3u) { // SPR_ALPHTEST
            if (idx == 255u) a = 0u;
        } else if (texFormat == 2u) { // SPR_INDEXALPHA (pal[255] color, idx = alpha!)
            r = pal[255][0]; g = pal[255][1]; b = pal[255][2]; a = idx;
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }
    outInfo.valid = true;
    return true;
}

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
#endif // E2_RETROTEX_ENABLE_IMAGE

#if defined(E2_RETROTEX_ENABLE_ARCHIVE)
namespace Formats {

inline bool ParseOrExtractWad2Wad3Archive(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
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

    if (outInfo) {
        outInfo->formatName = isWad3 ? L"Half-Life WAD3" : L"Quake WAD2";
    }

    for (uint32_t i = 0; i < numLumps; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 32u;
        uint32_t fPos = RetroTexDetail::RdU32LE(ent + 0);
        uint32_t dSz  = RetroTexDetail::RdU32LE(ent + 4);
        uint32_t uSz  = RetroTexDetail::RdU32LE(ent + 8);
        uint8_t  type = ent[12];
        uint8_t  comp = ent[13];
        if (fPos > size || dSz > size - fPos) return false;

        std::string rawName;
        if (!RetroTexDetail::IsPrintableLumpName(ent + 16, 16, rawName)) continue;

        const wchar_t* extSuf = L".lmp";
        if (type == 0x40u) extSuf = L".pal";
        else if (type == 0x42u) extSuf = L".lmp";
        else if (type == 0x43u || type == 0x44u) extSuf = L".miptex";
        else if (type == 0x46u) extSuf = L".fnt";

        std::wstring entryName = PathUtils::Utf8ToWide(rawName) + extSuf;
        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(entryName.c_str(), extractTargetNorm->c_str()) == 0 ||
                _wcsicmp(PathUtils::Utf8ToWide(rawName).c_str(), extractTargetNorm->c_str()) == 0)
            {
                if (comp != 0u) return false;
                outExtracted->assign(data + fPos, data + fPos + dSz);
                return true;
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive     = entryName;
            item.name              = entryName;
            item.uncompressedSize  = (uSz > 0) ? uSz : dSz;
            item.compressedSize    = dSz;
            item.rawDataOffset     = fPos;
            item.compressionMethod = comp;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

} // namespace Formats
#endif // E2_RETROTEX_ENABLE_ARCHIVE
