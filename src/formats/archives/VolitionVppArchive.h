#pragma once
// VolitionVppArchive.h — Volition Red Faction / Summoner VPP Package (.vpp — magic 0x51890ACE)
// Reuses shared GamePakArchiveCommon.h for safe filename validation and byte-exact slice extraction.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

namespace DetailVolitionVpp {

constexpr uint32_t kVppMagic = 0x51890ACEu;

inline uint64_t AlignSector2048(uint64_t off) {
    return (off + 2047ULL) & ~2047ULL;
}

} // namespace DetailVolitionVpp

inline bool InspectVolitionVppArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 4096u) return false;
    if (ByteIO::RdU32LE(data + 0u) != DetailVolitionVpp::kVppMagic) return false;

    const uint32_t ver       = ByteIO::RdU32LE(data + 4u);
    const uint32_t numFiles  = ByteIO::RdU32LE(data + 8u);
    const uint32_t packBytes = ByteIO::RdU32LE(data + 12u);
    if (ver != 1u && ver != 2u) return false;
    if (numFiles == 0u || numFiles > 65536u) return false;
    if (packBytes < 4096u || packBytes > size) return false;

    const uint64_t dirOff = 2048ULL;
    const uint64_t dirEnd = dirOff + static_cast<uint64_t>(numFiles) * 64ULL;
    if (dirEnd > size) return false;

    uint64_t curDataOff = DetailVolitionVpp::AlignSector2048(dirEnd);
    if (curDataOff > size) return false;

    outInfo = {};
    outInfo.formatName = L"Volition VPP Package (.vpp)";
    outInfo.entries.reserve(numFiles);

    for (uint32_t i = 0u; i < numFiles; ++i) {
        const uint8_t* rec  = data + static_cast<size_t>(dirOff) + static_cast<size_t>(i) * 64u;
        const size_t   nlen = strnlen(reinterpret_cast<const char*>(rec), 60u);
        if (nlen == 0u || nlen == 60u) { outInfo = {}; return false; }
        std::string_view rawName(reinterpret_cast<const char*>(rec), nlen);
        const uint32_t fSize = ByteIO::RdU32LE(rec + 60u);
        if (!GamePakCommon::AppendRawFileEntry(outInfo, rawName, curDataOff, fSize, size)) {
            outInfo = {};
            return false;
        }
        curDataOff = DetailVolitionVpp::AlignSector2048(curDataOff + fSize);
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractVolitionVppEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectVolitionVppArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
