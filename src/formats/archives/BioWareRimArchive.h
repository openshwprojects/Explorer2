#pragma once
// BioWareRimArchive.h — BioWare Odyssey Resource Image (.rim).
#include "../common/ArchiveFormatCommon.h"
#define E2_BIOWARE_TOP_LEVEL 1
#include "../common/BioWareCommon.h"
#undef E2_BIOWARE_TOP_LEVEL

namespace Explorer2::Formats {

inline bool InspectBioWareRimArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    using namespace BioWareDetail;
    if (!data || size < 120) return false;
    // RIM V1.0 (.rim — KotOR / KotOR II Resource Image)
    if (std::memcmp(data, "RIM V1.0", 8) == 0) {
        uint32_t entryCount = BwU32(data + 12);
        uint32_t offList    = BwU32(data + 16);
        if (offList == 0) offList = 120;
        if (entryCount == 0 || entryCount > 250000 || offList < 20) return false;
        if (offList > size || static_cast<uint64_t>(entryCount) * 32ULL > size - offList) return false;

        outInfo.entries.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; ++i) {
            const uint8_t* ep = data + offList + static_cast<size_t>(i) * 32;
            uint16_t resType = BwU16(ep + 16);
            uint32_t dOff    = BwU32(ep + 24);
            uint32_t dSz     = BwU32(ep + 28);
            if (dSz > 0 && (dOff < 120 || dOff > size || dSz > size - dOff)) return false;

            std::wstring stem = ReadFixedResRef16(ep);
            if (stem.empty()) {
                wchar_t fb[32] = {};
                swprintf_s(fb, L"rim_%04u", i);
                stem = fb;
            }
            std::wstring fileName = stem + ResTypeToExtension(resType);

            ArchiveItemEntry e;
            e.pathInArchive = fileName;
            e.name = fileName;
            e.uncompressedSize = dSz;
            e.compressedSize = dSz;
            e.rawDataOffset = dOff;
            e.compressionMethod = 0;
            outInfo.entries.push_back(std::move(e));
        }

        if (outInfo.entries.empty()) return false;
        outInfo.formatName = L"BioWare Odyssey Resource Image (.rim)";
        outInfo.valid = true;
        return true;
    }

    return false;
}

} // namespace Explorer2::Formats
