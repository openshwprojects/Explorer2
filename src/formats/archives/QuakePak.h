#pragma once
// QuakePak.h — Quake Engine PACK archive reader (.pak — "PACK").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool ParseQuakePak(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
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
