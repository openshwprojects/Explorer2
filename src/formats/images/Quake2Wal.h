#pragma once
// Quake2Wal.h — Quake II Mipmapped Texture (.wal) decoder.
#include "../common/RetroQuakeCommon.h"

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

} // namespace Formats
