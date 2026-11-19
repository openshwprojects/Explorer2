#pragma once
// QuakeHlSprite.h — Quake v1 & Half-Life v2 Sprite (.spr) decoder.
#include "../common/RetroQuakeCommon.h"

namespace Formats {
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

} // namespace Formats
