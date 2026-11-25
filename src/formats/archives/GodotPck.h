#pragma once
// GodotPck.h — Godot Engine Resource Pack reader (.pck — "GDPC").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

inline bool ParseGodotPck(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 96 || std::memcmp(data, "GDPC", 4) != 0) return false;
    uint32_t packVer = RdU32LE(data + 4);
    uint32_t maj = RdU32LE(data + 8), minV = RdU32LE(data + 12), patch = RdU32LE(data + 16);
    outInfo.formatName = L"Godot Engine PCK (v" + std::to_wstring(maj) + L"." +
                         std::to_wstring(minV) + L"." + std::to_wstring(patch) + L")";

    size_t pos = 20;
    uint64_t fileBase = 0;
    if (packVer >= 2) {
        if (size - pos < 80u) return false;
        uint32_t packFlags = RdU32LE(data + pos); pos += 4;
        (void)packFlags;
        fileBase = RdU64LE(data + pos); pos += 8;
        pos += 64; // 16 reserved uint32s
    } else {
        if (size - pos < 68u) return false;
        pos += 64; // 16 reserved uint32s in PCK v0/v1
    }
    if (size - pos < 4u) return false;
    uint32_t fileCount = RdU32LE(data + pos); pos += 4;

    for (uint32_t i = 0; i < fileCount && size - pos >= 4u && i < 50000; ++i) {
        uint32_t pathLen = RdU32LE(data + pos); pos += 4;
        if (pathLen > 4096u || pathLen > size - pos || size - pos - pathLen < 32u) return false;
        std::string rawP(reinterpret_cast<const char*>(data + pos), strnlen(reinterpret_cast<const char*>(data + pos), pathLen));
        pos += pathLen;
        if (rawP.rfind("res://", 0) == 0) rawP.erase(0, 6);

        uint64_t fOff  = RdU64LE(data + pos); pos += 8;
        uint64_t fSize = RdU64LE(data + pos); pos += 8;
        pos += 16; // MD5[16]
        uint32_t entryFlags = 0;
        if (packVer >= 2) {
            if (size - pos < 4u) return false;
            entryFlags = RdU32LE(data + pos); pos += 4;
        }

        if (fOff > UINT64_MAX - fileBase) return false;
        const uint64_t rawOff = fileBase + fOff;
        if ((entryFlags & 1u) == 0u) {
            if (rawOff > size || fSize > size - rawOff) return false;
        }

        if (!rawP.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawP));
            item.name = ArchivePath::BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = rawOff;
            item.compressionMethod = (entryFlags & 1u) ? 99 : 0; // 0 = stored raw
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectGodotPck(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParseGodotPck(data, size, outInfo);
}

} // namespace Explorer2::Formats
