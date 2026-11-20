#pragma once
// QuakePak.h — Quake Engine PACK archive reader (.pak — "PACK").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool ParseQuakePak(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    // SiN (Ritual, 1998): "SPAK", the same directory with 128-byte entries (a name of 120 bytes, offset, size)
    if (size >= 12 && std::memcmp(data, "SPAK", 4) == 0) {
        const uint32_t dirOff = RdU32LE(data + 4);
        const uint32_t dirLen = RdU32LE(data + 8);
        if (static_cast<uint64_t>(dirOff) + dirLen > size || dirLen == 0 || (dirLen % 128) != 0) return false;
        outInfo.formatName = L"SiN PAK (SPAK)";
        for (uint32_t i = 0; i < dirLen / 128 && i < 65536; ++i) {
            const uint8_t* rec = data + dirOff + static_cast<size_t>(i) * 128;
            std::string rawName(reinterpret_cast<const char*>(rec), strnlen(reinterpret_cast<const char*>(rec), 120));
            const uint32_t fOff = RdU32LE(rec + 120);
            const uint32_t fLen = RdU32LE(rec + 124);
            if (rawName.empty() || static_cast<uint64_t>(fOff) + fLen > size) continue;
            const std::wstring wp = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawName));
            if (wp.empty()) continue;
            ArchiveItemEntry item;
            item.pathInArchive = wp;
            item.name = ArchivePath::BaseName(wp);
            item.uncompressedSize = fLen;
            item.compressedSize = fLen;
            item.rawDataOffset = fOff;
            item.compressionMethod = 0;
            outInfo.entries.push_back(std::move(item));
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }
    if (size >= 12 && std::memcmp(data, "PACK", 4) == 0) {
        uint32_t dirOff = RdU32LE(data + 4);
        uint32_t dirLen = RdU32LE(data + 8);
        if (dirOff + dirLen > size || (dirLen % 64) != 0) return false;
        outInfo.formatName = L"Quake PAK";
        uint32_t count = dirLen / 64;
        for (uint32_t i = 0; i < count && i < 16384; ++i) {
            const uint8_t* rec = data + dirOff + i * 64;
            std::string rawName(reinterpret_cast<const char*>(rec), strnlen(reinterpret_cast<const char*>(rec), 56));
            uint32_t fOff = RdU32LE(rec + 56);
            uint32_t fLen = RdU32LE(rec + 60);
            if (rawName.empty() || fOff + fLen > size) continue;
            std::wstring wp = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawName));
            ArchiveItemEntry item;
            item.pathInArchive = wp;
            item.name = ArchivePath::BaseName(wp);
            item.uncompressedSize = fLen;
            item.compressedSize = fLen;
            item.rawDataOffset = fOff;
            item.compressionMethod = 0;
            outInfo.entries.push_back(std::move(item));
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }
    return false;
}

} // namespace DetailArc100

inline bool InspectQuakePak(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc100::ParseQuakePak(data, size, outInfo);
}

} // namespace Explorer2::Formats
