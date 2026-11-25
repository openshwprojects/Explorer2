#pragma once
// RitualSinSpakArchive.h — Ritual Entertainment SiN "SPAK" Archive (.sin)
// Parses 12-byte `SPAK` header + 128-byte directory entries (`120B` path + `uint32` offset + `uint32` size)
// via shared GamePakArchiveCommon.h.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectRitualSinSpakArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 140u) return false;
    if (std::memcmp(data, "SPAK", 4) != 0) return false;

    const uint32_t dirOff = ByteIO::RdU32LE(data + 4u);
    const uint32_t dirLen = ByteIO::RdU32LE(data + 8u);
    if (dirOff < 12u || dirLen == 0u || (dirLen % 128u) != 0u) return false;
    if (dirOff > size || dirLen > size - dirOff) return false;

    const uint32_t numFiles = dirLen / 128u;
    if (numFiles > 65536u) return false;

    outInfo = {};
    outInfo.formatName = L"SiN PAK (SPAK)";
    outInfo.entries.reserve(numFiles);

    for (uint32_t i = 0u; i < numFiles; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 128u;
        const size_t nlen = strnlen(reinterpret_cast<const char*>(ent), 120u);
        if (nlen == 0u || nlen == 120u) { outInfo = {}; return false; }
        std::string_view rawPath(reinterpret_cast<const char*>(ent), nlen);

        const uint32_t fOff = ByteIO::RdU32LE(ent + 120u);
        const uint32_t fLen = ByteIO::RdU32LE(ent + 124u);
        if (fOff < 12u || !GamePakCommon::AppendRawFileEntry(outInfo, rawPath, fOff, fLen, size)) {
            outInfo = {};
            return false;
        }
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractRitualSinSpakEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectRitualSinSpakArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
