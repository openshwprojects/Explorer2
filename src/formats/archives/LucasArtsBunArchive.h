#pragma once
// LucasArtsBunArchive.h — LucasArts The Curse of Monkey Island "LB83" Bundle Archive (.bun)
// Parses 12-byte big-endian `LB83` header + 32-byte big-endian directory entries (`24B` filename,
// `BE32` offset, `BE32` size) via shared GamePakArchiveCommon.h.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectLucasArtsBunArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 44u) return false;
    if (std::memcmp(data, "LB83", 4) != 0) return false;

    const uint32_t dirOff   = ByteIO::RdU32BE(data + 4u);
    const uint32_t numFiles = ByteIO::RdU32BE(data + 8u);
    if (numFiles == 0u || numFiles > 65536u || dirOff < 12u || dirOff > size) return false;

    const uint64_t dirBytes = static_cast<uint64_t>(numFiles) * 32ULL;
    if (dirBytes > size - dirOff) return false;

    outInfo = {};
    outInfo.formatName = L"LucasArts SCUMM LB83 Bundle (.bun)";
    outInfo.entries.reserve(numFiles);

    for (uint32_t i = 0u; i < numFiles; ++i) {
        const uint8_t* ent = data + dirOff + static_cast<size_t>(i) * 32u;
        std::string rawName;
        if (!FormatPrologue::ReadPrintableFixedAscii(ent + 0u, 24u, rawName, true) || rawName.empty()) {
            outInfo = {};
            return false;
        }
        const uint32_t fOff  = ByteIO::RdU32BE(ent + 24u);
        const uint32_t fSize = ByteIO::RdU32BE(ent + 28u);
        if (fOff < 12u || !GamePakCommon::AppendRawFileEntry(outInfo, rawName, fOff, fSize, size)) {
            outInfo = {};
            return false;
        }
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractLucasArtsBunEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectLucasArtsBunArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
