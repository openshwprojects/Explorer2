#pragma once
// VhdxDisk.h — Hyper-V VHDX virtual disk reader (.vhdx — "vhdxfile").

#include "../common/ArchiveFormatCommon.h"
#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool BuildVhdxDiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // Hyper-V VHDX ("vhdxfile")
    if (size >= 65536 && std::memcmp(data, "vhdxfile", 8) == 0) {
        static const uint8_t kBatGuid[16]  = { 0x66,0x77,0xC2,0x2D,0x23,0xF6,0x00,0x42,0x9D,0x64,0x11,0x5E,0x9B,0xFD,0x4A,0x08 };
        static const uint8_t kMetaGuid[16] = { 0x06,0xA2,0x7C,0x8B,0x90,0x47,0x9A,0x4B,0xB8,0xFE,0x57,0x5F,0x05,0x0F,0x88,0x6E };
        static const uint8_t kFileParamGuid[16] = { 0x37,0x67,0xA1,0xCA,0x36,0xFA,0x43,0x4D,0xB3,0xB6,0x33,0xF0,0xAA,0x44,0xE7,0x6B };
        static const uint8_t kVirtSizeGuid[16]  = { 0x24,0x42,0xA5,0x2F,0x1B,0xCD,0x76,0x48,0xB2,0x11,0x5D,0xBE,0xD8,0x3B,0xF4,0xB8 };
        static const uint8_t kLogSecGuid[16]    = { 0x1D,0xBF,0x41,0x81,0x6F,0xA9,0x09,0x47,0xBA,0x47,0xF2,0x33,0xA8,0xFA,0xAB,0x5F };

        const size_t regCandidates[3] = { 0x30000, 0x40000, 0x10000 };
        size_t regPos = SIZE_MAX;
        for (size_t cand : regCandidates) {
            if (cand <= size - 16 && std::memcmp(data + cand, "regi", 4) == 0) {
                regPos = cand;
                break;
            }
        }
        if (regPos == SIZE_MAX) return false;

        uint32_t regCount = std::min<uint32_t>(RdU32LE(data + regPos + 8), 2047);
        uint64_t batOff = 0, batLen = 0, metaOff = 0, metaLen = 0;
        for (uint32_t i = 0; i < regCount && static_cast<uint64_t>(i + 1ULL) * 32ULL <= size - regPos - 16; ++i) {
            const uint8_t* re = data + regPos + 16 + static_cast<size_t>(i) * 32;
            if (std::memcmp(re, kBatGuid, 16) == 0) {
                batOff = RdU64LE(re + 16);
                batLen = RdU32LE(re + 24);
            } else if (std::memcmp(re, kMetaGuid, 16) == 0) {
                metaOff = RdU64LE(re + 16);
                metaLen = RdU32LE(re + 24);
            }
        }
        if (metaOff == 0 || metaOff > size - 32 || std::memcmp(data + static_cast<size_t>(metaOff), "metadata", 8) != 0) {
            return false;
        }

        const size_t metaBase = static_cast<size_t>(metaOff);
        uint16_t metaCount = std::min<uint16_t>(RdU16LE(data + metaBase + 10), 2047);
        uint32_t blockSize = 0, logicalSector = 512;
        uint64_t virtSize = 0;
        for (uint16_t i = 0; i < metaCount && static_cast<uint64_t>(i + 1ULL) * 32ULL <= size - metaBase - 32; ++i) {
            const uint8_t* me = data + metaBase + 32 + static_cast<size_t>(i) * 32;
            uint32_t itemOff = RdU32LE(me + 16);
            uint32_t itemLen = RdU32LE(me + 20);
            if (itemOff > metaLen || itemLen > metaLen - itemOff) continue;
            if (itemOff > size - metaBase || itemLen > size - metaBase - itemOff) continue;
            const uint8_t* val = data + metaBase + itemOff;
            if (std::memcmp(me, kFileParamGuid, 16) == 0 && itemLen >= 4) {
                blockSize = RdU32LE(val);
            } else if (std::memcmp(me, kVirtSizeGuid, 16) == 0 && itemLen >= 8) {
                virtSize = RdU64LE(val);
            } else if (std::memcmp(me, kLogSecGuid, 16) == 0 && itemLen >= 4) {
                uint32_t ls = RdU32LE(val);
                if (ls >= 512 && ls <= 4096) logicalSector = ls;
            }
        }
        if (blockSize < 65536 || (blockSize & (blockSize - 1)) != 0 || virtSize == 0 || batOff == 0 || batOff >= size) {
            return false;
        }
        uint64_t chunkRatio = ((1ULL << 23) * logicalSector) / blockSize;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"Hyper-V VHDX Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff > virtSize || len > virtSize - diskOff) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t pBlk = cur / blockSize;
                uint64_t offInBlk = cur % blockSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, blockSize - offInBlk));

                uint64_t batIdx = pBlk + (chunkRatio > 0 ? (pBlk / chunkRatio) : 0);
                bool filled = false;
                if (batIdx < batLen / 8ULL && (batIdx + 1ULL) * 8ULL <= size - batOff) {
                    uint64_t entry = RdU64LE(data + static_cast<size_t>(batOff + batIdx * 8ULL));
                    if ((entry & 0x7ULL) == 6ULL) { // PAYLOAD_BLOCK_FULLY_PRESENT
                        uint64_t physBase = entry & 0xFFFFFFFFFFF00000ULL;
                        if (physBase > 0 && physBase <= size && offInBlk <= size - physBase &&
                            chunk <= size - physBase - offInBlk)
                        {
                            std::memcpy(dst + done, data + static_cast<size_t>(physBase + offInBlk), chunk);
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

} // namespace DetailDiskAndGameArc

inline bool ParseOrExtractVhdxDisk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailDiskAndGameArc::VirtualDiskReader reader;
    if (!DetailDiskAndGameArc::BuildVhdxDiskReader(data, size, reader)) return false;
    return DetailDiskAndGameArc::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectVhdxDisk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractVhdxDisk(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractVhdxDiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractVhdxDisk(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
