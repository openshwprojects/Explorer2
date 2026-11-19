#pragma once
// BuildEngineGrp.h — Build Engine GRP archive reader (.grp — "KenSilverman").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

// ============================================================================
// #28. Build Engine GRP Archive (.grp — "KenSilverman")
// ============================================================================
inline bool ParseBuildGrp(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 16 || std::memcmp(data, "KenSilverman", 12) != 0) return false;
    outInfo.formatName = L"Build Engine GRP Archive (.grp)";

    uint32_t count = RdU32LE(data + 12);
    if (count > 32768 || 16ULL + static_cast<uint64_t>(count) * 16 > size) return false;

    uint64_t dataOff = 16ULL + static_cast<uint64_t>(count) * 16;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* ent = data + 16 + static_cast<size_t>(i) * 16;
        std::string fname(reinterpret_cast<const char*>(ent), strnlen(reinterpret_cast<const char*>(ent), 12));
        uint32_t fSize = RdU32LE(ent + 12);
        if (!fname.empty() && dataOff + fSize <= size) {
            ArchiveItemEntry item;
            item.pathInArchive = ArchivePath::Normalize(PathUtils::Utf8ToWide(fname));
            item.name = ArchivePath::BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = dataOff;
            item.compressionMethod = 0; // Stored raw!
            outInfo.entries.push_back(std::move(item));
        }
        dataOff += fSize;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

} // namespace DetailArc200

inline bool InspectBuildEngineGrp(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailArc200::ParseBuildGrp(data, size, outInfo);
}

} // namespace Explorer2::Formats
