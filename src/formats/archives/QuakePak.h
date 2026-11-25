#pragma once
// QuakePak.h — Quake Engine PACK archive reader (.pak — "PACK").
// ("SPAK" .sin archives are handled exclusively by RitualSinSpakArchive.h.)

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool ParseQuakePak(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 12 || std::memcmp(data, "PACK", 4) != 0) return false;

    const uint32_t dirOff = RdU32LE(data + 4);
    const uint32_t dirLen = RdU32LE(data + 8);
    if (dirOff < 12u || dirOff > size || dirLen == 0u || dirLen > size - dirOff || (dirLen % 64u) != 0u) {
        return false;
    }

    outInfo.formatName = L"Quake PAK";
    const uint32_t count = dirLen / 64u;
    for (uint32_t i = 0; i < count && i < 65536u; ++i) {
        const uint8_t* rec = data + dirOff + static_cast<size_t>(i) * 64u;
        std::string rawName(reinterpret_cast<const char*>(rec), strnlen(reinterpret_cast<const char*>(rec), 56u));
        const uint32_t fOff = RdU32LE(rec + 56u);
        const uint32_t fLen = RdU32LE(rec + 60u);
        if (rawName.empty() || fOff > size || fLen > size - fOff) continue;
        std::wstring wp = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawName));
        if (wp.empty()) continue;

        ArchiveItemEntry item;
        item.pathInArchive     = std::move(wp);
        item.name              = ArchivePath::BaseName(item.pathInArchive);
        item.uncompressedSize  = fLen;
        item.compressedSize    = fLen;
        item.rawDataOffset     = fOff;
        item.compressionMethod = 0;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailDiskAndGameArc

inline bool InspectQuakePak(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailDiskAndGameArc::ParseQuakePak(data, size, outInfo);
}

} // namespace Explorer2::Formats
