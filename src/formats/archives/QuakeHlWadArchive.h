#pragma once
// QuakeHlWadArchive.h — Quake WAD2 & Half-Life WAD3 Lump Archive (.wad) reader.
#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailQuakeHlWad {

inline bool IsPrintableWadLumpName(const uint8_t* p, size_t maxLen, std::string& outName) {
    outName.clear();
    if (!p || maxLen == 0) return false;
    for (size_t i = 0; i < maxLen; ++i) {
        const uint8_t c = p[i];
        if (c == 0) break;
        if (c < 0x20u || c >= 0x7Fu) return false;
        char ch = static_cast<char>(c);
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }
        outName.push_back(ch);
    }
    return !outName.empty();
}

} // namespace DetailQuakeHlWad

inline bool ParseOrExtractWad2Wad3Archive(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!data || size < 12) return false;
    const bool isWad2 = (std::memcmp(data, "WAD2", 4) == 0);
    const bool isWad3 = (std::memcmp(data, "WAD3", 4) == 0);
    if (!isWad2 && !isWad3) return false;

    const uint32_t numLumps = ByteIO::RdU32LE(data + 4);
    const uint32_t dirOff   = ByteIO::RdU32LE(data + 8);
    if (numLumps == 0 || numLumps > 65536u || dirOff < 12u || dirOff > size ||
        static_cast<uint64_t>(numLumps) * 32ULL > size - dirOff) {
        return false;
    }

    if (outInfo) {
        outInfo->formatName = isWad3 ? L"Half-Life WAD3" : L"Quake WAD2";
    }

    const std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};

    for (uint32_t i = 0; i < numLumps; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 32u;
        const uint32_t fPos = ByteIO::RdU32LE(ent + 0);
        const uint32_t dSz  = ByteIO::RdU32LE(ent + 4);
        const uint32_t uSz  = ByteIO::RdU32LE(ent + 8);
        const uint8_t  type = ent[12];
        const uint8_t  comp = ent[13];
        if (fPos > size || dSz > size - fPos) return false;

        std::string rawName;
        if (!DetailQuakeHlWad::IsPrintableWadLumpName(ent + 16, 16, rawName)) continue;

        const wchar_t* extSuf = L".lmp";
        if (type == 0x40u) extSuf = L".pal";
        else if (type == 0x42u) extSuf = L".lmp";
        else if (type == 0x43u || type == 0x44u) extSuf = L".miptex";
        else if (type == 0x46u) extSuf = L".fnt";

        const std::wstring rawWide = PathUtils::Utf8ToWide(rawName);
        const std::wstring entryName = rawWide + extSuf;
        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(entryName.c_str(), wantNorm.c_str()) == 0 ||
                _wcsicmp(rawWide.c_str(), wantNorm.c_str()) == 0)
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

inline bool InspectQuakeHlWadArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractWad2Wad3Archive(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractQuakeHlWadEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractWad2Wad3Archive(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
