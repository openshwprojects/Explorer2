#pragma once
// BioWareErfArchive.h — BioWare Aurora Encapsulated Resource File (.erf, .mod, .hak, .nwm, .sav).
#include "../common/ArchiveFormatCommon.h"
#define E2_BIOWARE_TOP_LEVEL 1
#include "../common/BioWareCommon.h"
#undef E2_BIOWARE_TOP_LEVEL

namespace Explorer2::Formats {

inline bool InspectBioWareErfArchive(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    using namespace BioWareDetail;
    if (!data || size < 160) return false;
    // ERF V1.0 / V1.1 (.erf, .mod, .hak, .nwm, .sav)
    if ((std::memcmp(data, "ERF ", 4) == 0 || std::memcmp(data, "MOD ", 4) == 0 ||
         std::memcmp(data, "HAK ", 4) == 0 || std::memcmp(data, "NWM ", 4) == 0 ||
         std::memcmp(data, "SAV ", 4) == 0) &&
        (std::memcmp(data + 4, "V1.0", 4) == 0 || std::memcmp(data + 4, "V1.1", 4) == 0))
    {
        const bool isV11 = (std::memcmp(data + 4, "V1.1", 4) == 0);
        const size_t keyStride = isV11 ? 40u : 24u; // V1.0 = 16B resRef, V1.1 = 32B resRef
        const size_t resRefMax = isV11 ? 32u : 16u;
        uint32_t entryCount = BwU32(data + 16);
        uint32_t offKeyList = BwU32(data + 24);
        uint32_t offResList = BwU32(data + 28);
        if (entryCount == 0 || entryCount > 250000) return false;
        if (offKeyList < 160 || offResList < 160) return false;
        if (offKeyList > size || static_cast<uint64_t>(entryCount) * keyStride > size - offKeyList) return false;
        if (offResList > size || static_cast<uint64_t>(entryCount) * 8ULL > size - offResList) return false;

        outInfo.entries.reserve(entryCount);
        for (uint32_t i = 0; i < entryCount; ++i) {
            const uint8_t* kp = data + offKeyList + static_cast<size_t>(i) * keyStride;
            const uint8_t* rp = data + offResList + static_cast<size_t>(i) * 8;
            uint32_t dOff = BwU32(rp + 0);
            uint32_t dSz  = BwU32(rp + 4);
            if (dSz > 0 && (dOff < 160 || dOff > size || dSz > size - dOff)) return false;

            std::wstring stem = ReadFixedResRef(kp, resRefMax);
            if (stem.empty()) {
                wchar_t fb[32] = {};
                swprintf_s(fb, L"res_%04u", i);
                stem = fb;
            }
            uint16_t resType = BwU16(kp + (isV11 ? 36 : 20));
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
        outInfo.formatName = (extLower == L".mod") ? L"BioWare Aurora Module (.mod)" :
                             (extLower == L".hak") ? L"BioWare Aurora HakPak (.hak)" :
                             (extLower == L".nwm") ? L"Neverwinter Nights Campaign (.nwm)" :
                                                     L"BioWare Aurora Encapsulated Resource (.erf)";
        outInfo.valid = true;
        return true;
    }

    return false;
}

} // namespace Explorer2::Formats
