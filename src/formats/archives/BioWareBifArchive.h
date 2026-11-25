#pragma once
// BioWareBifArchive.h — BioWare Infinity/Aurora/Odyssey BIFF Archive (.bif + companion .key).
#include "../common/ArchiveFormatCommon.h"
#define E2_BIOWARE_TOP_LEVEL 1
#include "../common/BioWareCommon.h"
#undef E2_BIOWARE_TOP_LEVEL

namespace Explorer2::Formats {

inline bool InspectBioWareBifArchive(const std::wstring& archivePath, const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    using namespace BioWareDetail;
    if (!data || size < 20) return false;
    // BIFF V1 / V1.1 (.bif — Infinity / Aurora / Odyssey BIF + optional companion .key resRef table)
    if (std::memcmp(data, "BIFF", 4) == 0 &&
        (std::memcmp(data + 4, "V1  ", 4) == 0 || std::memcmp(data + 4, "V1.1", 4) == 0))
    {
        const bool isV11 = (std::memcmp(data + 4, "V1.1", 4) == 0);
        const size_t varStride = isV11 ? 20u : 16u;
        uint32_t varCount = BwU32(data + 8);
        uint32_t varOff   = BwU32(data + 16);
        if (varCount == 0 || varCount > 500000 || varOff < 20) return false;
        if (varOff > size || static_cast<uint64_t>(varCount) * varStride > size - varOff) return false;

        // Optional companion KEY V1 lookup (same stem .key or chitin.key in same/parent folder)
        std::unordered_map<uint32_t, std::wstring> keyNames;
        if (!archivePath.empty()) {
            const wchar_t kSep =
#ifdef _WIN32
                L'\\';
#else
                L'/';
#endif
            std::wstring dir = PathUtils::GetParentPath(archivePath);
            std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(archivePath), false);
            std::vector<uint8_t> kb;
            if (!dir.empty()) {
                if (!PathUtils::ReadFileBytes(dir + kSep + stem + L".key", 16 * 1024 * 1024, kb)) {
                    if (!PathUtils::ReadFileBytes(dir + kSep + L"chitin.key", 16 * 1024 * 1024, kb)) {
                        std::wstring parentDir = PathUtils::GetParentPath(dir);
                        if (!parentDir.empty()) {
                            PathUtils::ReadFileBytes(parentDir + kSep + L"chitin.key", 16 * 1024 * 1024, kb);
                        }
                    }
                }
            }
            if (kb.size() >= 64 && std::memcmp(kb.data(), "KEY V1  ", 8) == 0) {
                uint32_t keyCount  = BwU32(kb.data() + 12);
                uint32_t offKeyTbl = BwU32(kb.data() + 20);
                if (keyCount > 0 && keyCount <= 500000 && offKeyTbl <= kb.size() &&
                    static_cast<uint64_t>(keyCount) * 22ULL <= kb.size() - offKeyTbl)
                {
                    for (uint32_t ki = 0; ki < keyCount; ++ki) {
                        const uint8_t* ke = kb.data() + offKeyTbl + static_cast<size_t>(ki) * 22;
                        std::wstring rname = ReadFixedResRef16(ke);
                        uint32_t resId = BwU32(ke + 18);
                        if (!rname.empty()) {
                            keyNames[resId & 0xFFFFFu] = rname;
                        }
                    }
                }
            }
        }

        outInfo.entries.reserve(varCount);
        for (uint32_t i = 0; i < varCount; ++i) {
            const uint8_t* vp = data + varOff + static_cast<size_t>(i) * varStride;
            uint32_t resId   = BwU32(vp + 0);
            uint32_t dOff    = BwU32(vp + (isV11 ? 8 : 4));
            uint32_t dSz     = BwU32(vp + (isV11 ? 12 : 8));
            uint16_t resType = BwU16(vp + (isV11 ? 16 : 12));
            if (dSz > 0 && (dOff < 20 || dOff > size || dSz > size - dOff)) return false;

            std::wstring stem;
            auto it = keyNames.find(resId & 0xFFFFFu);
            if (it != keyNames.end()) {
                stem = it->second;
            } else {
                wchar_t fb[32] = {};
                swprintf_s(fb, L"bif_%04u", i);
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
        outInfo.formatName = L"BioWare Aurora BIFF Archive (.bif)";
        outInfo.valid = true;
        return true;
    }

    return false;
}

} // namespace Explorer2::Formats
