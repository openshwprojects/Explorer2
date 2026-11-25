#pragma once
// LucasArtsLabArchive.h — LucasArts Grim Fandango / Outlaws / Escape from Monkey Island "LABN" (.lab)
// Parses 16-byte "LABN" header + 16-byte entry table + NUL-terminated string pool (supporting both
// plain ASCII in Grim Fandango/Outlaws and 0x96-XORed string tables in Escape from Monkey Island).

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectLucasArtsLabArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 34u) return false;
    if (std::memcmp(data, "LABN", 4) != 0) return false;

    const uint32_t version     = ByteIO::RdU32LE(data + 0x04);
    const uint32_t numFiles    = ByteIO::RdU32LE(data + 0x08);
    const uint32_t strPoolSize = ByteIO::RdU32LE(data + 0x0C);
    if (version == 0u || version > 0x00100000u ||
        numFiles == 0u || numFiles > 131072u ||
        strPoolSize < 2u || strPoolSize > (16u << 20)) {
        return false;
    }

    const uint64_t strPoolOff = 16ULL + static_cast<uint64_t>(numFiles) * 16ULL;
    const uint64_t strPoolEnd = strPoolOff + strPoolSize;
    if (strPoolEnd > size) return false;

    const uint8_t* rawPool = data + static_cast<size_t>(strPoolOff);
    // Escape from Monkey Island XORs the entire string pool (including NULs) with 0x96
    const uint8_t xorKey = (rawPool[strPoolSize - 1u] == 0x96u && rawPool[strPoolSize - 1u] != 0x00u) ? 0x96u : 0x00u;
    if ((rawPool[strPoolSize - 1u] ^ xorKey) != 0x00u) return false;

    outInfo = {};
    outInfo.formatName = (xorKey != 0u)
        ? L"LucasArts LABN Archive (.lab, EMI XOR)"
        : L"LucasArts LABN Archive (.lab)";
    outInfo.entries.reserve(numFiles);

    for (uint32_t i = 0; i < numFiles; ++i) {
        const uint8_t* ent      = data + 16u + static_cast<size_t>(i) * 16u;
        const uint32_t nameOff  = ByteIO::RdU32LE(ent + 0x00);
        const uint32_t dataOff  = ByteIO::RdU32LE(ent + 0x04);
        const uint32_t dataLen  = ByteIO::RdU32LE(ent + 0x08);
        if (nameOff >= strPoolSize || dataOff < strPoolEnd) {
            outInfo = {};
            return false;
        }

        std::string entryPath;
        size_t p = nameOff;
        while (p < strPoolSize) {
            const uint8_t ch = rawPool[p++] ^ xorKey;
            if (ch == 0u) break;
            entryPath.push_back(static_cast<char>(ch));
            if (entryPath.size() > 260u) {
                outInfo = {};
                return false;
            }
        }
        if (entryPath.empty() ||
            !GamePakCommon::AppendRawFileEntry(outInfo, entryPath, dataOff, dataLen, size)) {
            outInfo = {};
            return false;
        }
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractLucasArtsLabEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectLucasArtsLabArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
