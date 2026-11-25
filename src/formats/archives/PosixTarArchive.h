#pragma once
// PosixTarArchive.h — Unified POSIX USTAR / V7 / GNU @LongLink / PAX TAR & TAR.GZ / OVA reader & extractor
// wrapping StreamCodecs::ParsePosixTarFromMemory.

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

inline bool InspectPosixTarOrTarGz(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    if (!data || size < 18) return false;
    const bool isGzip = (data[0] == 0x1Fu && data[1] == 0x8Bu && data[2] == 8u);
    std::vector<uint8_t> decomp;
    const uint8_t* tarPtr = data;
    size_t tarLen = size;

    if (isGzip) {
        if (!ZipDeflate::InflateGzip(data, size, decomp, 256 * 1024 * 1024) || decomp.size() < 512) {
            return false;
        }
        tarPtr = decomp.data();
        tarLen = decomp.size();
    } else if (tarLen < 512) {
        return false;
    }

    ArchiveSummaryInfo parsed;
    if (!StreamCodecs::ParsePosixTarFromMemory(tarPtr, tarLen, parsed) || parsed.entries.empty()) {
        return false;
    }

    outInfo = std::move(parsed);
    outInfo.formatName = (extLower == L".ova") ? L"OVA Virtual Appliance (.ova)" :
                         (isGzip ? L"TAR.GZ" : L"TAR");
    if (isGzip) {
        uint64_t totalUnp = 0;
        for (const auto& e : outInfo.entries) {
            if (!e.isDirectory) totalUnp += e.uncompressedSize;
        }
        for (auto& e : outInfo.entries) {
            if (!e.isDirectory) {
                e.compressedSize = (totalUnp > 0)
                    ? ((static_cast<uint64_t>(size) * e.uncompressedSize) / totalUnp)
                    : size;
                e.compressionMethod = 8;
            }
        }
    }
    outInfo.valid = true;
    return true;
}

inline bool ExtractPosixTarOrTarGzEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || size < 18 || normTarget.empty()) return false;
    const bool isGzip = (data[0] == 0x1Fu && data[1] == 0x8Bu && data[2] == 8u);
    std::vector<uint8_t> decomp;
    const uint8_t* tarPtr = data;
    size_t tarLen = size;

    if (isGzip) {
        if (!ZipDeflate::InflateGzip(data, size, decomp, 256 * 1024 * 1024) || decomp.size() < 512) {
            return false;
        }
        tarPtr = decomp.data();
        tarLen = decomp.size();
    } else if (tarLen < 512) {
        return false;
    }

    ArchiveSummaryInfo dummy;
    return StreamCodecs::ParsePosixTarFromMemory(tarPtr, tarLen, dummy, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
