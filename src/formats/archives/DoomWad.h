#pragma once
// DoomWad.h — DOOM Engine IWAD / PWAD lump archive reader (.wad — "IWAD" / "PWAD").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool ParseDoomWad(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size >= 12 && (std::memcmp(data, "IWAD", 4) == 0 || std::memcmp(data, "PWAD", 4) == 0)) {
        uint32_t numLumps = RdU32LE(data + 4);
        uint32_t dirOff   = RdU32LE(data + 8);
        if (dirOff + static_cast<size_t>(numLumps) * 16 > size) return false;
        outInfo.formatName = (data[0] == 'I') ? L"DOOM IWAD" : L"DOOM PWAD";
        for (uint32_t i = 0; i < numLumps && i < 16384; ++i) {
            const uint8_t* rec = data + dirOff + i * 16;
            uint32_t fOff = RdU32LE(rec + 0);
            uint32_t fLen = RdU32LE(rec + 4);
            std::string rawName(reinterpret_cast<const char*>(rec + 8), strnlen(reinterpret_cast<const char*>(rec + 8), 8));
            if (rawName.empty()) continue;
            ArchiveItemEntry item;
            item.pathInArchive = PathUtils::Utf8ToWide(rawName) + L".lmp";
            item.name = item.pathInArchive;
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

inline bool InspectDoomWad(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc100::ParseDoomWad(data, size, outInfo);
}

} // namespace Explorer2::Formats
