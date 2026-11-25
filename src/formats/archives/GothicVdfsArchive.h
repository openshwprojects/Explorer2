#pragma once
// GothicVdfsArchive.h — Piranha Bytes Gothic I & II ZenGin "PSVDSC_V2.00" VDFS Volume (.vdf)
// Parses 256B comment + 40B VDFS header + 80B hierarchical catalog entries (space-padded 64B names,
// 0x80000000 DIR child-index pointers, 0x40000000 LAST-sibling flags) with visited-bitmask cycle safety.

#include "../common/GamePakArchiveCommon.h"

namespace Explorer2::Formats {

namespace DetailGothicVdfs {

inline std::string TrimVdfsName64(const uint8_t* raw64) {
    size_t len = 0u;
    while (len < 64u && raw64[len] != 0u) ++len;
    while (len > 0u && raw64[len - 1u] == ' ') --len;
    return std::string(reinterpret_cast<const char*>(raw64), len);
}

inline bool WalkVdfsDir(
    const uint8_t* data,
    size_t archiveSize,
    size_t catOff,
    uint32_t entryCount,
    uint32_t startIdx,
    int depth,
    const std::string& prefix,
    uint16_t volDosDate,
    uint16_t volDosTime,
    std::vector<uint8_t>& visited,
    ArchiveSummaryInfo& outInfo)
{
    if (depth > 32 || startIdx >= entryCount) return false;

    uint32_t idx = startIdx;
    while (true) {
        if (idx >= entryCount || visited[idx] != 0u) return false; // Bounds or cycle bomb!
        visited[idx] = 1u;

        const uint8_t* ent       = data + catOff + static_cast<size_t>(idx) * 80u;
        const std::string name   = TrimVdfsName64(ent);
        const uint32_t offOrChld = ByteIO::RdU32LE(ent + 64u);
        const uint32_t fSize     = ByteIO::RdU32LE(ent + 68u);
        const uint32_t flags     = ByteIO::RdU32LE(ent + 72u);

        if (!GamePakCommon::IsSafeSegmentAscii(name)) return false;

        const bool isDir  = (flags & 0x80000000u) != 0u;
        const bool isLast = (flags & 0x40000000u) != 0u;

        if (isDir) {
            if (offOrChld != 0u) {
                if (!WalkVdfsDir(
                        data, archiveSize, catOff, entryCount, offOrChld,
                        depth + 1, prefix + name + "/", volDosDate, volDosTime, visited, outInfo)) {
                    return false;
                }
            }
        } else {
            if (offOrChld < 296u) return false;
            if (!GamePakCommon::AppendRawFileEntry(
                    outInfo, prefix + name, offOrChld, fSize, archiveSize)) {
                return false;
            }
            outInfo.entries.back().dosDate = volDosDate;
            outInfo.entries.back().dosTime = volDosTime;
        }

        if (isLast) break;
        ++idx;
    }
    return true;
}

} // namespace DetailGothicVdfs

inline bool InspectGothicVdfsArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 296u + 80u) return false;
    if (std::memcmp(data + 0x100u, "PSVDSC_V2.00", 12) != 0) return false;

    const uint32_t entryCount = ByteIO::RdU32LE(data + 0x110u);
    const uint32_t dosPacked  = ByteIO::RdU32LE(data + 0x118u);
    const uint32_t catOffset  = ByteIO::RdU32LE(data + 0x120u);
    const uint32_t verField   = ByteIO::RdU32LE(data + 0x124u);

    if (entryCount == 0u || entryCount > 262144u || catOffset < 296u) return false;
    if (verField != 0x50u && verField != 0u) return false;
    if (static_cast<uint64_t>(catOffset) + static_cast<uint64_t>(entryCount) * 80ULL > size) {
        return false;
    }

    const uint16_t volDosDate = static_cast<uint16_t>(dosPacked >> 16);
    const uint16_t volDosTime = static_cast<uint16_t>(dosPacked & 0xFFFFu);

    outInfo = {};
    outInfo.formatName = (data[0x10Cu] == '\n')
        ? L"Gothic II ZenGin VDFS Volume (.vdf)"
        : L"Gothic ZenGin VDFS Volume (.vdf)";

    std::vector<uint8_t> visited(entryCount, 0u);
    if (!DetailGothicVdfs::WalkVdfsDir(
            data, size, catOffset, entryCount, 0u, 0, "", volDosDate, volDosTime, visited, outInfo) ||
        outInfo.entries.empty()) {
        outInfo = {};
        return false;
    }

    outInfo.valid = true;
    return true;
}

inline bool ExtractGothicVdfsEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return GamePakCommon::ExtractByParsedSummary(InspectGothicVdfsArchive, data, size, normTarget, outBytes);
}

} // namespace Explorer2::Formats
