#pragma once
// RawFilesystemDisk.h — Superfloppy & partitioned raw filesystem disk image reader (.ext2, .ext3, .ext4, .hfs, raw .dmg).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool BuildRawFilesystemDiskReader(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;
    if (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) return false;
    if (size >= 65536 && std::memcmp(data, "vhdxfile", 8) == 0) return false;
    if (size >= 1024 && (std::memcmp(data + size - 512, "conectix", 8) == 0 || std::memcmp(data, "conectix", 8) == 0)) return false;
    if (size >= 512 && std::memcmp(data, "KDMV", 4) == 0) return false;
    if (size >= 400 && RdU32LE(data + 64) == 0xBEDA107Fu) return false;
    if (size >= 512 && std::memcmp(data + size - 512, "koly", 4) == 0) return false;

    bool hasRawHfs  = (size >= 2048 && data[1024] == 'H' && (data[1025] == '+' || data[1025] == 'X'));
    bool hasRawApm  = (size >= 1024 && data[512] == 'P' && data[513] == 'M');
    bool hasRawExt  = (size >= 2048 && RdU16LE(data + 1024 + 56) == 0xEF53u);
    if (hasRawHfs || hasRawApm || hasRawExt ||
        ((extLower == L".dmg" || extLower == L".hfs" || extLower == L".ext2" ||
          extLower == L".ext3" || extLower == L".ext4") && size >= 1024))
    {
        outReader.virtualSize = size;
        outReader.formatName  = (hasRawExt || extLower == L".ext2" || extLower == L".ext3" || extLower == L".ext4")
                              ? L"Linux ext2/ext3/ext4 Image"
                              : ((hasRawHfs || extLower == L".hfs") ? L"Apple HFS+ Volume" : L"Apple Disk Image (DMG)");
        outReader.directRawBuffer = true;
        outReader.allowRawSliceFallback = false;
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > size) return false;
            std::memcpy(dst, data + static_cast<size_t>(diskOff), len);
            return true;
        };
        return true;
    }

    return false;
}

} // namespace DetailArc100

inline bool ParseOrExtractRawFilesystemDisk(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailArc100::VirtualDiskReader reader;
    if (!DetailArc100::BuildRawFilesystemDiskReader(data, size, extLower, reader)) return false;
    return DetailArc100::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectRawFilesystemDisk(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return ParseOrExtractRawFilesystemDisk(data, size, extLower, &outInfo, nullptr, nullptr);
}

inline bool ExtractRawFilesystemDiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractRawFilesystemDisk(data, size, extLower, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
