#pragma once
// VdiDisk.h — VirtualBox VDI virtual disk reader (.vdi — 0xBEDA107F).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool BuildVdiDiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // 5. VirtualBox VDI (0xBEDA107F at offset 64)
    if (size >= 400 && RdU32LE(data + 64) == 0xBEDA107Fu) {
        uint32_t offsetBlocks = RdU32LE(data + 340);
        uint32_t offsetData   = RdU32LE(data + 344);
        uint64_t virtSize     = RdU64LE(data + 368);
        uint32_t blockSize    = RdU32LE(data + 376);
        uint32_t blocksInHdd  = RdU32LE(data + 384);
        if (virtSize == 0 || blockSize < 4096 || offsetBlocks >= size || offsetData > size) return false;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"VirtualBox VDI Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t blkIdx = cur / blockSize;
                uint64_t offInBlk = cur % blockSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, blockSize - offInBlk));
                bool filled = false;
                if (blkIdx < blocksInHdd && offsetBlocks + (blkIdx + 1) * 4 <= size) {
                    uint32_t physBlk = RdU32LE(data + static_cast<size_t>(offsetBlocks + blkIdx * 4));
                    if (physBlk < 0xFFFFFFFEu) {
                        uint64_t phys = static_cast<uint64_t>(offsetData) + static_cast<uint64_t>(physBlk) * blockSize + offInBlk;
                        if (phys + chunk <= size) {
                            std::memcpy(dst + done, data + static_cast<size_t>(phys), chunk);
                            filled = true;
                        }
                    }
                }
                if (!filled) std::memset(dst + done, 0, chunk);
                done += chunk;
            }
            return true;
        };
        return true;
    }

    return false;
}

} // namespace DetailArc100

inline bool ParseOrExtractVdiDisk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailArc100::VirtualDiskReader reader;
    if (!DetailArc100::BuildVdiDiskReader(data, size, reader)) return false;
    return DetailArc100::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectVdiDisk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractVdiDisk(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractVdiDiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractVdiDisk(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
