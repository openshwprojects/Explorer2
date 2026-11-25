#pragma once
// GamePakArchiveCommon.h — Shared DRY path sanitization, DOS timestamp, TOC builder, and raw-slice
// extractor for uncompressed game engine resource containers.

#include "ArchiveFormatCommon.h"

namespace Explorer2::Formats::GamePakCommon {

inline void UnixSecToDosDateTime(uint32_t unixSec, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (unixSec == 0u) return;
    int y = 0, m = 0, d = 0;
    ReaderText::CivilFromDays1970(static_cast<int64_t>(unixSec / 86400u), y, m, d);
    if (y < 1980 || y > 2107) return;
    const uint32_t rem = unixSec % 86400u;
    const uint32_t hh  = rem / 3600u;
    const uint32_t mm  = (rem % 3600u) / 60u;
    const uint32_t ss  = rem % 60u;
    outDate = static_cast<uint16_t>(((y - 1980) << 9) | (m << 5) | d);
    outTime = static_cast<uint16_t>((hh << 11) | (mm << 5) | (ss >> 1));
}

inline bool IsSafeSegmentAscii(std::string_view seg) {
    if (seg.empty() || seg.size() > 240u) return false;
    if (seg == "." || seg == "..") return false;
    for (unsigned char c : seg) {
        if (c < 0x20u || c > 0x7Eu) return false;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<'  || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

inline bool SanitizeRelativeArchivePath(std::string_view rawPath, std::string& outNorm) {
    outNorm.clear();
    size_t i = 0;
    while (i < rawPath.size() && (rawPath[i] == '/' || rawPath[i] == '\\')) ++i;
    if (i >= rawPath.size()) return false;

    while (i < rawPath.size()) {
        size_t e = i;
        while (e < rawPath.size() && rawPath[e] != '/' && rawPath[e] != '\\') ++e;
        std::string_view seg = rawPath.substr(i, e - i);
        if (!IsSafeSegmentAscii(seg)) return false;
        if (!outNorm.empty()) outNorm.push_back('/');
        outNorm.append(seg.data(), seg.size());
        i = e;
        while (i < rawPath.size() && (rawPath[i] == '/' || rawPath[i] == '\\')) ++i;
    }
    return !outNorm.empty() && ReaderText::IsSafeRelativeSubFile(outNorm);
}

inline bool AppendRawFileEntry(
    ArchiveSummaryInfo& outInfo,
    std::string_view relPathAscii,
    uint64_t rawOff,
    uint64_t rawLen,
    size_t archiveSize,
    uint32_t unixTs = 0u)
{
    if (rawOff > archiveSize || rawLen > archiveSize - rawOff) return false;
    std::string cleanRel;
    if (!SanitizeRelativeArchivePath(relPathAscii, cleanRel)) return false;

    ArchiveItemEntry item{};
    item.pathInArchive     = ArchivePath::Normalize(PathUtils::Utf8ToWide(cleanRel));
    item.name              = ArchivePath::BaseName(item.pathInArchive);
    item.isDirectory       = false;
    item.uncompressedSize  = rawLen;
    item.compressedSize    = rawLen;
    item.rawDataOffset     = rawOff;
    item.compressionMethod = 0u;
    UnixSecToDosDateTime(unixTs, item.dosDate, item.dosTime);
    outInfo.entries.push_back(std::move(item));
    return true;
}

template <typename ParseFn>
inline bool ExtractByParsedSummary(
    ParseFn&& parseFn,
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || normTarget.empty()) return false;
    ArchiveSummaryInfo info{};
    if (!parseFn(data, size, info) || !info.valid) return false;
    for (const auto& e : info.entries) {
        if (!e.isDirectory && DetailDiskAndGameArc::PathEqualsCI(e.pathInArchive, normTarget)) {
            if (e.rawDataOffset > size || e.uncompressedSize > size - e.rawDataOffset) return false;
            if (e.uncompressedSize > (256ULL << 20)) return false;
            outBytes.assign(
                data + static_cast<size_t>(e.rawDataOffset),
                data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
            return true;
        }
    }
    return false;
}

} // namespace Explorer2::Formats::GamePakCommon
