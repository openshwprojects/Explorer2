#pragma once
// Qcow2Disk.h — QEMU QCOW2 v2/v3 virtual disk reader (.qcow2 — "QFI\xFB").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc100 {

inline bool BuildQcow2DiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // 1. QEMU QCOW2 ("QFI\xFB" v2/v3)
    if (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) {
        uint32_t ver         = RdU32BE(data + 4);
        uint32_t clusterBits = RdU32BE(data + 20);
        uint64_t virtSize    = RdU64BE(data + 24);
        uint32_t l1Size      = RdU32BE(data + 36);
        uint64_t l1Offset    = RdU64BE(data + 40);
        if ((ver != 2 && ver != 3) || clusterBits < 9 || clusterBits > 21 || virtSize == 0) return false;
        if (l1Offset + static_cast<uint64_t>(l1Size) * 8ULL > size) return false;

        uint64_t clusterSize = 1ULL << clusterBits;
        uint64_t l2Entries   = clusterSize / 8ULL;

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"QEMU QCOW2 Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff + len > virtSize) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t clusIdx = cur / clusterSize;
                uint64_t offInClus = cur % clusterSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, clusterSize - offInClus));

                uint64_t l1Idx = clusIdx / l2Entries;
                uint64_t l2Idx = clusIdx % l2Entries;
                bool filled = false;
                if (l1Idx < l1Size && l1Offset + (l1Idx + 1) * 8 <= size) {
                    uint64_t l1Ent = RdU64BE(data + static_cast<size_t>(l1Offset + l1Idx * 8));
                    uint64_t l2TblOff = l1Ent & 0x00FFFFFFFFFFFE00ULL;
                    if (l2TblOff > 0 && l2TblOff + (l2Idx + 1) * 8 <= size) {
                        uint64_t l2Ent = RdU64BE(data + static_cast<size_t>(l2TblOff + l2Idx * 8));
                        bool compressed = (l2Ent & (1ULL << 62)) != 0;
                        bool zeroFlag = (ver >= 3) && ((l2Ent & 1ULL) != 0);
                        if (!compressed) {
                            uint64_t hostOff = l2Ent & 0x00FFFFFFFFFFFE00ULL;
                            if (!zeroFlag && hostOff > 0 && hostOff + offInClus + chunk <= size) {
                                std::memcpy(dst + done, data + static_cast<size_t>(hostOff + offInClus), chunk);
                                filled = true;
                            }
                        } else {
                            uint32_t csizeShift = 62 - (clusterBits - 8);
                            uint64_t hostOff = l2Ent & ((1ULL << csizeShift) - 1ULL);
                            uint64_t nbSectors = ((l2Ent >> csizeShift) & ((1ULL << (clusterBits - 8)) - 1ULL)) + 1ULL;
                            uint64_t compBytes = nbSectors * 512ULL - (hostOff & 511ULL);
                            if (hostOff + compBytes <= size) {
                                std::vector<uint8_t> dec;
                                if (ZipDeflate::InflateRaw(data + static_cast<size_t>(hostOff), static_cast<size_t>(compBytes), dec, static_cast<size_t>(clusterSize)) &&
                                    offInClus + chunk <= dec.size())
                                {
                                    std::memcpy(dst + done, dec.data() + static_cast<size_t>(offInClus), chunk);
                                    filled = true;
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

inline bool ParseOrExtractQcow2Disk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailArc100::VirtualDiskReader reader;
    if (!DetailArc100::BuildQcow2DiskReader(data, size, reader)) return false;
    return DetailArc100::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
}

inline bool InspectQcow2Disk(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractQcow2Disk(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractQcow2DiskEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return ParseOrExtractQcow2Disk(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
