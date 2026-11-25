#pragma once
// ActivisionZfsArchive.h — Activision Interstate '76 / Battlezone / Heavy Gear "ZFSF" Archive (.zfs)
// Parses chained directory pages (with cycle protection) and optional 4-byte XOR payload decryption.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectActivisionZfsArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 68u) return false;
    if (std::memcmp(data, "ZFSF", 4) != 0) return false;

    const uint32_t version       = ByteIO::RdU32LE(data + 4u);
    const uint32_t filesPerBlock = ByteIO::RdU32LE(data + 12u);
    const uint32_t totalFiles    = ByteIO::RdU32LE(data + 16u);
    const uint32_t firstDirOff   = ByteIO::RdU32LE(data + 24u);

    if (version != 1u || filesPerBlock == 0u || filesPerBlock > 4096u) return false;
    if (totalFiles == 0u || totalFiles > 65536u || firstDirOff < 28u || firstDirOff >= size) return false;

    outInfo = {};
    outInfo.formatName = L"Activision ZFSF Archive (.zfs)";
    outInfo.entries.reserve(totalFiles);

    std::unordered_set<uint32_t> visitedPages;
    uint32_t curPageOff = firstDirOff;
    uint32_t remaining  = totalFiles;

    while (remaining > 0u) {
        if (curPageOff < 28u || curPageOff >= size || !visitedPages.insert(curPageOff).second) {
            outInfo = {};
            return false; // Invalid or cyclic directory page pointer!
        }
        const uint32_t inThisPage = std::min(remaining, filesPerBlock);
        const uint64_t pageNeed   = 4ULL + static_cast<uint64_t>(inThisPage) * 36ULL;
        if (pageNeed > size - curPageOff) {
            outInfo = {};
            return false;
        }

        const uint32_t nextPageOff = ByteIO::RdU32LE(data + curPageOff);
        for (uint32_t i = 0u; i < inThisPage; ++i) {
            const uint8_t* ent = data + curPageOff + 4u + static_cast<size_t>(i) * 36u;
            std::string rawName;
            if (!FormatPrologue::ReadPrintableFixedAscii(ent + 0u, 16u, rawName, true) || rawName.empty()) {
                outInfo = {};
                return false;
            }
            const uint32_t fOff  = ByteIO::RdU32LE(ent + 16u);
            const uint32_t fSize = ByteIO::RdU32LE(ent + 24u);
            const uint32_t fTime = ByteIO::RdU32LE(ent + 28u);

            if (fOff < 28u || !GamePakCommon::AppendRawFileEntry(outInfo, rawName, fOff, fSize, size, fTime)) {
                outInfo = {};
                return false;
            }
        }

        remaining -= inThisPage;
        curPageOff = nextPageOff;
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractActivisionZfsEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (!GamePakCommon::ExtractByParsedSummary(InspectActivisionZfsArchive, data, size, normTarget, outBytes)) {
        return false;
    }
    // Apply 4-byte ZFSF XOR key (`header[20..24]`) if nonzero
    const uint32_t xorKey = ByteIO::RdU32LE(data + 20u);
    if (xorKey != 0u) {
        const uint8_t k[4] = {
            static_cast<uint8_t>(xorKey & 0xFFu),
            static_cast<uint8_t>((xorKey >> 8u) & 0xFFu),
            static_cast<uint8_t>((xorKey >> 16u) & 0xFFu),
            static_cast<uint8_t>((xorKey >> 24u) & 0xFFu)
        };
        for (size_t i = 0u; i < outBytes.size(); ++i) {
            outBytes[i] ^= k[i & 3u];
        }
    }
    return true;
}

} // namespace Explorer2::Formats
