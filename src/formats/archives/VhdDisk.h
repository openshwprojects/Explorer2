#pragma once
// VhdDisk.h — Hyper-V / VirtualPC Fixed & Dynamic VHD virtual disk reader (.vhd — "conectix").

#include "../common/ArchiveFormatCommon.h"
#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool BuildVhdDiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // Hyper-V / VirtualPC VHD ("conectix" footer at size - 512 or 0)
    if (size >= 1024 && (std::memcmp(data + size - 512, "conectix", 8) == 0 || std::memcmp(data, "conectix", 8) == 0)) {
        const uint8_t* ftr = (std::memcmp(data + size - 512, "conectix", 8) == 0) ? (data + size - 512) : data;
        uint64_t dataOffset = RdU64BE(ftr + 16);
        uint64_t virtSize   = RdU64BE(ftr + 48);
        uint32_t diskType   = RdU32BE(ftr + 60);
        if (virtSize == 0) return false;

        if (diskType == 2) { // Fixed VHD (raw disk sectors followed by 512-byte footer)
            size_t payloadEnd = (ftr == data + size - 512) ? (size - 512) : size;
            outReader.virtualSize = virtSize;
            outReader.formatName  = L"Hyper-V Fixed VHD Disk";
            outReader.directRawBuffer = (ftr == data + size - 512);
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff > virtSize || len > virtSize - diskOff) return false;
                if (diskOff >= payloadEnd) {
                    std::memset(dst, 0, len);
                    return true;
                }
                size_t avail = static_cast<size_t>(std::min<uint64_t>(len, payloadEnd - diskOff));
                std::memcpy(dst, data + static_cast<size_t>(diskOff), avail);
                if (avail < len) std::memset(dst + avail, 0, len - avail);
                return true;
            };
            return true;
        } else if (diskType == 3 && dataOffset <= size - 1024 && std::memcmp(data + static_cast<size_t>(dataOffset), "cxsparse", 8) == 0) {
            const uint8_t* dyn = data + static_cast<size_t>(dataOffset);
            uint64_t batOff    = RdU64BE(dyn + 16);
            uint32_t maxBat    = RdU32BE(dyn + 28);
            uint32_t blockSize = RdU32BE(dyn + 32);
            if (blockSize < 512 || (blockSize & (blockSize - 1)) != 0 || batOff >= size) return false;
            uint64_t bitmapBytes = (((blockSize / 512u + 7u) / 8u) + 511u) & ~511ULL;

            outReader.virtualSize = virtSize;
            outReader.formatName  = L"Hyper-V Dynamic VHD Disk";
            outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
                if (diskOff > virtSize || len > virtSize - diskOff) return false;
                size_t done = 0;
                while (done < len) {
                    uint64_t cur = diskOff + done;
                    uint64_t blkIdx = cur / blockSize;
                    uint64_t offInBlk = cur % blockSize;
                    size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, blockSize - offInBlk));
                    bool filled = false;
                    if (blkIdx < maxBat && (blkIdx + 1ULL) * 4ULL <= size - batOff) {
                        uint32_t secOff = RdU32BE(data + static_cast<size_t>(batOff + blkIdx * 4ULL));
                        if (secOff != 0xFFFFFFFFu) {
                            uint64_t secBytes = static_cast<uint64_t>(secOff) * 512ULL;
                            if (secBytes <= size && bitmapBytes <= size - secBytes &&
                                offInBlk <= size - secBytes - bitmapBytes)
                            {
                                uint64_t phys = secBytes + bitmapBytes + offInBlk;
                                if (chunk <= size - static_cast<size_t>(phys)) {
                                    std::memcpy(dst + done, data + static_cast<size_t>(phys), chunk);
                                    filled = true;
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

    return false;
}

} // namespace DetailDiskAndGameArc

inline bool ParseOrExtractVhdDisk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailDiskAndGameArc::VirtualDiskReader reader;
    if (!DetailDiskAndGameArc::BuildVhdDiskReader(data, size, reader)) return false;
    return DetailDiskAndGameArc::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectVhdDisk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractVhdDisk(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractVhdDiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractVhdDisk(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
