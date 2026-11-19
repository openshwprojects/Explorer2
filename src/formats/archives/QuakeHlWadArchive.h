#pragma once
// QuakeHlWadArchive.h — Quake WAD2 & Half-Life WAD3 Lump Archive (.wad) reader.
#include "../common/RetroQuakeCommon.h"

namespace Formats {

inline bool ParseOrExtractWad2Wad3Archive(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!data || size < 12) return false;
    bool isWad2 = (std::memcmp(data, "WAD2", 4) == 0);
    bool isWad3 = (std::memcmp(data, "WAD3", 4) == 0);
    if (!isWad2 && !isWad3) return false;

    uint32_t numLumps = RetroTexDetail::RdU32LE(data + 4);
    uint32_t dirOff   = RetroTexDetail::RdU32LE(data + 8);
    if (numLumps == 0 || numLumps > 65536u || dirOff < 12u || dirOff > size ||
        static_cast<uint64_t>(numLumps) * 32ull > size - dirOff) {
        return false;
    }

    if (outInfo) {
        outInfo->formatName = isWad3 ? L"Half-Life WAD3" : L"Quake WAD2";
    }

    for (uint32_t i = 0; i < numLumps; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 32u;
        uint32_t fPos = RetroTexDetail::RdU32LE(ent + 0);
        uint32_t dSz  = RetroTexDetail::RdU32LE(ent + 4);
        uint32_t uSz  = RetroTexDetail::RdU32LE(ent + 8);
        uint8_t  type = ent[12];
        uint8_t  comp = ent[13];
        if (fPos > size || dSz > size - fPos) return false;

        std::string rawName;
        if (!RetroTexDetail::IsPrintableLumpName(ent + 16, 16, rawName)) continue;

        const wchar_t* extSuf = L".lmp";
        if (type == 0x40u) extSuf = L".pal";
        else if (type == 0x42u) extSuf = L".lmp";
        else if (type == 0x43u || type == 0x44u) extSuf = L".miptex";
        else if (type == 0x46u) extSuf = L".fnt";

        std::wstring entryName = PathUtils::Utf8ToWide(rawName) + extSuf;
        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(entryName.c_str(), extractTargetNorm->c_str()) == 0 ||
                _wcsicmp(PathUtils::Utf8ToWide(rawName).c_str(), extractTargetNorm->c_str()) == 0)
            {
                if (comp != 0u) return false;
                outExtracted->assign(data + fPos, data + fPos + dSz);
                return true;
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive     = entryName;
            item.name              = entryName;
            item.uncompressedSize  = (uSz > 0) ? uSz : dSz;
            item.compressedSize    = dSz;
            item.rawDataOffset     = fPos;
            item.compressionMethod = comp;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

} // namespace Formats
