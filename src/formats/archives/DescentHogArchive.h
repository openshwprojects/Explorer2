#pragma once
// DescentHogArchive.h — Parallax Software Descent 1/2 ("DHF") & Outrage Descent 3 ("HOG2") Archive Reader
// Reuses shared GamePakArchiveCommon.h for safe filename validation, DOS timestamps, and slice extraction.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

inline bool InspectDescentHogArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 20u) return false;

    // 1. Outrage Descent 3 "HOG2"
    if (size >= 68u && std::memcmp(data, "HOG2", 4) == 0) {
        const uint32_t nFiles    = ByteIO::RdU32LE(data + 4u);
        const uint32_t dataStart = ByteIO::RdU32LE(data + 8u);
        if (nFiles == 0u || nFiles > 32768u) return false;
        const uint64_t dirEnd = 68ULL + static_cast<uint64_t>(nFiles) * 48ULL;
        if (dirEnd > size || dataStart < dirEnd || dataStart > size) return false;

        uint64_t curOff = dataStart;
        outInfo = {};
        outInfo.formatName = L"Descent 3 HOG2 Archive (.hog)";
        outInfo.entries.reserve(nFiles);

        for (uint32_t i = 0u; i < nFiles; ++i) {
            const uint8_t* ent  = data + 68u + static_cast<size_t>(i) * 48u;
            const size_t   nlen = strnlen(reinterpret_cast<const char*>(ent), 36u);
            if (nlen == 0u || nlen == 36u) { outInfo = {}; return false; }
            std::string_view rawName(reinterpret_cast<const char*>(ent), nlen);
            if (!GamePakCommon::IsSafeSegmentAscii(rawName)) { outInfo = {}; return false; }

            const uint32_t fLen = ByteIO::RdU32LE(ent + 40u);
            const uint32_t fTs  = ByteIO::RdU32LE(ent + 44u);
            if (!GamePakCommon::AppendRawFileEntry(outInfo, rawName, curOff, fLen, size, fTs)) {
                outInfo = {};
                return false;
            }
            curOff += fLen;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    // 2. Parallax Descent 1 / 2 "DHF"
    if (std::memcmp(data, "DHF", 3) == 0) {
        size_t pos = 3u;
        outInfo = {};
        outInfo.formatName = L"Descent DHF Archive (.hog)";
        while (pos < size) {
            if (size - pos < 17u) { outInfo = {}; return false; }
            const char*  namePtr = reinterpret_cast<const char*>(data + pos);
            const size_t nlen    = strnlen(namePtr, 13u);
            if (nlen == 0u || nlen == 13u) { outInfo = {}; return false; }
            std::string_view rawName(namePtr, nlen);
            if (!GamePakCommon::IsSafeSegmentAscii(rawName)) { outInfo = {}; return false; }

            const uint32_t fLen = ByteIO::RdU32LE(data + pos + 13u);
            pos += 17u;
            if (!GamePakCommon::AppendRawFileEntry(outInfo, rawName, pos, fLen, size) ||
                outInfo.entries.size() > 32768u) {
                outInfo = {};
                return false;
            }
            pos += fLen;
        }
        if (pos != size || outInfo.entries.empty()) {
            outInfo = {};
            return false;
        }
        outInfo.valid = true;
        return true;
    }

    return false;
}

inline bool ExtractDescentHogEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectDescentHogArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
