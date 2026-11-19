#pragma once
// VmdkDisk.h — VMware Sparse VMDK virtual disk reader (.vmdk — "KDMV").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool BuildVmdkDiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // 4. VMware Sparse VMDK ("KDMV")
    if (size >= 512 && std::memcmp(data, "KDMV", 4) == 0) {
        uint32_t flags        = RdU32LE(data + 8);
        uint64_t capacitySec  = RdU64LE(data + 12);
        uint64_t grainSec     = RdU64LE(data + 20);
        uint32_t numGtesPerGt = RdU32LE(data + 44);
        uint64_t rgdSec       = RdU64LE(data + 48);
        uint64_t gdSec        = RdU64LE(data + 56);
        if (gdSec == 0xFFFFFFFFFFFFFFFFULL) gdSec = rgdSec;
        if (capacitySec == 0 || grainSec == 0 || numGtesPerGt == 0 || gdSec * 512ULL >= size) return false;

        uint64_t virtSize   = capacitySec * 512ULL;
        uint64_t grainBytes = grainSec * 512ULL;
        uint64_t gdByteOff  = gdSec * 512ULL;
        bool compressedGrains = (flags & 0x10000u) != 0;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"VMware VMDK Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t grainIdx = cur / grainBytes;
                uint64_t offInGrain = cur % grainBytes;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, grainBytes - offInGrain));
                uint64_t gtIdx  = grainIdx / numGtesPerGt;
                uint64_t gteIdx = grainIdx % numGtesPerGt;
                bool filled = false;
                if (gdByteOff + (gtIdx + 1) * 4 <= size) {
                    uint32_t gtSec = RdU32LE(data + static_cast<size_t>(gdByteOff + gtIdx * 4));
                    uint64_t gtByteOff = static_cast<uint64_t>(gtSec) * 512ULL;
                    if (gtSec > 0 && gtByteOff + (gteIdx + 1) * 4 <= size) {
                        uint32_t gSec = RdU32LE(data + static_cast<size_t>(gtByteOff + gteIdx * 4));
                        uint64_t gByteOff = static_cast<uint64_t>(gSec) * 512ULL;
                        if (gSec > 0 && gByteOff < size) {
                            if (!compressedGrains) {
                                if (gByteOff + offInGrain + chunk <= size) {
                                    std::memcpy(dst + done, data + static_cast<size_t>(gByteOff + offInGrain), chunk);
                                    filled = true;
                                }
                            } else if (gByteOff + 12 <= size) {
                                uint32_t cmpSz = RdU32LE(data + static_cast<size_t>(gByteOff + 8));
                                if (gByteOff + 12 + cmpSz <= size) {
                                    std::vector<uint8_t> dec;
                                    if (ZipDeflate::InflateZlib(data + static_cast<size_t>(gByteOff + 12), cmpSz, dec, static_cast<size_t>(grainBytes)) &&
                                        offInGrain + chunk <= dec.size())
                                    {
                                        std::memcpy(dst + done, dec.data() + static_cast<size_t>(offInGrain), chunk);
                                        filled = true;
                                    }
                                }
                            }
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

inline bool ParseOrExtractVmdkDisk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailArc100::VirtualDiskReader reader;
    if (!DetailArc100::BuildVmdkDiskReader(data, size, reader)) return false;
    return DetailArc100::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectVmdkDisk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractVmdkDisk(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractVmdkDiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractVmdkDisk(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
