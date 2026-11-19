#pragma once
// GodotPck.h — Godot Engine Resource Pack reader (.pck — "GDPC").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #23. Godot Engine Resource Pack (.pck — "GDPC")
// ============================================================================
inline bool ParseGodotPck(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    // Magic "GDPC" can be at offset 0 or embedded at EOF of self-contained Godot binary
    if (size < 96 || std::memcmp(data, "GDPC", 4) != 0) return false;
    uint32_t packVer = RdU32LE(data + 4);
    uint32_t maj = RdU32LE(data + 8), minV = RdU32LE(data + 12), patch = RdU32LE(data + 16);
    outInfo.formatName = L"Godot Engine PCK (v" + std::to_wstring(maj) + L"." +
                         std::to_wstring(minV) + L"." + std::to_wstring(patch) + L")";

    size_t pos = 20;
    uint64_t fileBase = 0;
    if (packVer >= 2) {
        if (pos + 12 + 64 + 4 > size) return false;
        uint32_t packFlags = RdU32LE(data + pos); pos += 4;
        (void)packFlags;
        fileBase = RdU64LE(data + pos); pos += 8;
        pos += 64; // 16 reserved uint32s
    } else {
        pos += 64; // 16 reserved uint32s in PCK v0/v1
    }
    if (pos + 4 > size) return false;
    uint32_t fileCount = RdU32LE(data + pos); pos += 4;

    for (uint32_t i = 0; i < fileCount && pos + 4 <= size && i < 50000; ++i) {
        uint32_t pathLen = RdU32LE(data + pos); pos += 4;
        if (pathLen > 4096 || pos + pathLen + 32 > size) break;
        std::string rawP(reinterpret_cast<const char*>(data + pos), strnlen(reinterpret_cast<const char*>(data + pos), pathLen));
        pos += pathLen;
        if (rawP.rfind("res://", 0) == 0) rawP.erase(0, 6);

        uint64_t fOff  = RdU64LE(data + pos); pos += 8;
        uint64_t fSize = RdU64LE(data + pos); pos += 8;
        pos += 16; // MD5[16]
        uint32_t entryFlags = 0;
        if (packVer >= 2) {
            if (pos + 4 > size) break;
            entryFlags = RdU32LE(data + pos); pos += 4;
        }

        if (!rawP.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawP));
            item.name = ArchivePath::BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = fileBase + fOff;
            item.compressionMethod = (entryFlags & 1u) ? 99 : 0; // 0 = stored raw!
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectGodotPck(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseGodotPck(data, size, outInfo);
}

} // namespace Explorer2::Formats
