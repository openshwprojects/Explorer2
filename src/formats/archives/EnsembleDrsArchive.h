#pragma once
// EnsembleDrsArchive.h — Ensemble Studios Age of Empires I & II Data Resource Archive (.drs)
// Parses 64-byte Genie Engine header (`"Copyright (c) 1997 Ensemble Studios."`), reversed 4-char type
// tables (`"anib"` -> `.bina`, `" vaw"` -> `.wav`, `" pls"` -> `.slp`), and numeric resource IDs.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectEnsembleDrsArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 76u) return false;
    if (std::memcmp(data, "Copyright (c) 1997 Ensemble Studios.", 36) != 0) return false;

    // AoE2 header is 64 bytes (40B copyright + 4B ver + 12B ftype + 4B numTables + 4B firstFileOff);
    // Star Wars Galactic Battlegrounds uses 60B copyright -> header size 84B. Check "1.00" at 40 or 60.
    size_t verOff = 0u;
    if (std::memcmp(data + 40u, "1.00", 4) == 0) {
        verOff = 40u;
    } else if (size >= 96u && std::memcmp(data + 60u, "1.00", 4) == 0) {
        verOff = 60u;
    } else {
        return false;
    }

    const size_t metaOff       = verOff + 16u; // after 4B ver + 12B "tribe\0..."
    const uint32_t numTables   = ByteIO::RdU32LE(data + metaOff + 0u);
    const uint32_t firstDatOff = ByteIO::RdU32LE(data + metaOff + 4u);
    const size_t tableHdrStart = metaOff + 8u;

    if (numTables == 0u || numTables > 64u) return false;
    const uint64_t tableHdrEnd = static_cast<uint64_t>(tableHdrStart) + static_cast<uint64_t>(numTables) * 12ULL;
    if (tableHdrEnd > size || firstDatOff < tableHdrEnd || firstDatOff > size) return false;

    outInfo = {};
    outInfo.formatName = L"Ensemble Studios Genie DRS Archive (.drs)";

    for (uint32_t ti = 0u; ti < numTables; ++ti) {
        const uint8_t* th = data + tableHdrStart + static_cast<size_t>(ti) * 12u;
        // Reversed 4-byte ASCII extension (e.g. 'a','n','i','b' -> "bina", ' ','v','a','w' -> "wav")
        std::string ext;
        for (int k = 3; k >= 0; --k) {
            const unsigned char ch = th[k];
            if (ch == ' ' || ch == 0u) continue;
            if (!std::isalnum(ch) && ch != '_') { outInfo = {}; return false; }
            ext.push_back(static_cast<char>(std::tolower(ch)));
        }
        if (ext.empty()) ext = "bin";

        const uint32_t tblOff   = ByteIO::RdU32LE(th + 4u);
        const uint32_t numFiles = ByteIO::RdU32LE(th + 8u);
        if (numFiles > 65536u) { outInfo = {}; return false; }
        const uint64_t tblEnd = static_cast<uint64_t>(tblOff) + static_cast<uint64_t>(numFiles) * 12ULL;
        if (tblOff < tableHdrEnd || tblEnd > size) { outInfo = {}; return false; }

        for (uint32_t fi = 0u; fi < numFiles; ++fi) {
            const uint8_t* fe = data + tblOff + static_cast<size_t>(fi) * 12u;
            const uint32_t resId = ByteIO::RdU32LE(fe + 0u);
            const uint32_t fOff  = ByteIO::RdU32LE(fe + 4u);
            const uint32_t fSize = ByteIO::RdU32LE(fe + 8u);
            if (fOff < firstDatOff) { outInfo = {}; return false; }

            std::string relPath = ext + "/" + std::to_string(resId) + "." + ext;
            if (!GamePakCommon::AppendRawFileEntry(outInfo, relPath, fOff, fSize, size)) {
                outInfo = {};
                return false;
            }
        }
    }

    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractEnsembleDrsEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectEnsembleDrsArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
