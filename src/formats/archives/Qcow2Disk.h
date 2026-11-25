#pragma once
// Qcow2Disk.h — QEMU QCOW2 v2/v3 virtual disk reader (.qcow2 — "QFI\xFB", deflate + Zstd clusters).

#include "../common/ArchiveFormatCommon.h"
#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool BuildQcow2DiskReader(
    const uint8_t* data,
    size_t size,
    VirtualDiskReader& outReader)
{
    outReader = {};
    if (!data || size < 512) return false;

    // QEMU QCOW2 ("QFI\xFB" v2/v3)
    if (size >= 72 && std::memcmp(data, "QFI\xFB", 4) == 0) {
        uint32_t ver         = RdU32BE(data + 4);
        uint32_t clusterBits = RdU32BE(data + 20);
        uint64_t virtSize    = RdU64BE(data + 24);
        uint32_t l1Size      = RdU32BE(data + 36);
        uint64_t l1Offset    = RdU64BE(data + 40);
        if ((ver != 2 && ver != 3) || clusterBits < 9 || clusterBits > 21 || virtSize == 0) return false;
        if (l1Offset > size || static_cast<uint64_t>(l1Size) * 8ULL > size - l1Offset) return false;

        uint8_t compType = 0; // 0 = deflate, 1 = zstd
        if (ver >= 3 && size >= 105) {
            const uint64_t incompFeat = RdU64BE(data + 72);
            const uint32_t hdrLen     = RdU32BE(data + 100);
            if (hdrLen >= 105u || (incompFeat & 0x08ULL) != 0ULL) {
                compType = data[104];
                if (compType > 1u) return false;
            }
        }

        const uint64_t clusterSize = 1ULL << clusterBits;
        const uint64_t l2Entries   = clusterSize / 8ULL;

        struct Qcow2ClusterCache {
            uint64_t l2Ent = UINT64_MAX;
            std::vector<uint8_t> dec;
        };
        auto clusCache = std::make_shared<Qcow2ClusterCache>();

        outReader.virtualSize = virtSize;
        outReader.formatName  = L"QEMU QCOW2 Disk";
        outReader.readBytes = [=](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
            if (diskOff > virtSize || len > virtSize - diskOff) return false;
            size_t done = 0;
            while (done < len) {
                uint64_t cur = diskOff + done;
                uint64_t clusIdx = cur / clusterSize;
                uint64_t offInClus = cur % clusterSize;
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - done, clusterSize - offInClus));

                uint64_t l1Idx = clusIdx / l2Entries;
                uint64_t l2Idx = clusIdx % l2Entries;
                bool filled = false;
                if (l1Idx < l1Size && (l1Idx + 1ULL) * 8ULL <= size - l1Offset) {
                    uint64_t l1Ent = RdU64BE(data + static_cast<size_t>(l1Offset + l1Idx * 8ULL));
                    uint64_t l2TblOff = l1Ent & 0x00FFFFFFFFFFFE00ULL;
                    if (l2TblOff > 0 && l2TblOff <= size && (l2Idx + 1ULL) * 8ULL <= size - l2TblOff) {
                        uint64_t l2Ent = RdU64BE(data + static_cast<size_t>(l2TblOff + l2Idx * 8ULL));
                        bool compressed = (l2Ent & (1ULL << 62)) != 0;
                        bool zeroFlag = (ver >= 3) && ((l2Ent & 1ULL) != 0);
                        if (!compressed) {
                            uint64_t hostOff = l2Ent & 0x00FFFFFFFFFFFE00ULL;
                            if (!zeroFlag && hostOff > 0) {
                                if (hostOff > size || offInClus > size - hostOff || chunk > size - hostOff - offInClus) {
                                    return false;
                                }
                                std::memcpy(dst + done, data + static_cast<size_t>(hostOff + offInClus), chunk);
                                filled = true;
                            }
                        } else {
                            if (clusCache->l2Ent != l2Ent) {
                                uint32_t csizeShift = 62u - (clusterBits - 8u);
                                uint64_t hostOff = l2Ent & ((1ULL << csizeShift) - 1ULL);
                                uint64_t nbSectors = ((l2Ent >> csizeShift) & ((1ULL << (clusterBits - 8u)) - 1ULL)) + 1ULL;
                                uint64_t compBytes = nbSectors * 512ULL - (hostOff & 511ULL);
                                if (hostOff > size || compBytes > size - hostOff) return false;
                                std::vector<uint8_t> dec;
                                bool ok = false;
                                const uint8_t* cPtr = data + static_cast<size_t>(hostOff);
                                const size_t cLen   = static_cast<size_t>(compBytes);
                                const size_t maxCl  = static_cast<size_t>(clusterSize);
                                if (compType == 1u) {
                                    ok = StreamCodecs::DecompressZstdFrame(cPtr, cLen, dec, maxCl);
                                } else {
                                    ok = ZipDeflate::InflateRaw(cPtr, cLen, dec, maxCl) ||
                                         ZipDeflate::InflateZlib(cPtr, cLen, dec, maxCl);
                                }
                                if (!ok || dec.size() < clusterSize) return false;
                                clusCache->l2Ent = l2Ent;
                                clusCache->dec = std::move(dec);
                            }
                            if (offInClus > clusCache->dec.size() || chunk > clusCache->dec.size() - static_cast<size_t>(offInClus)) {
                                return false;
                            }
                            std::memcpy(dst + done, clusCache->dec.data() + static_cast<size_t>(offInClus), chunk);
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

inline bool ParseOrExtractQcow2Disk(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    DetailDiskAndGameArc::VirtualDiskReader reader;
    if (!DetailDiskAndGameArc::BuildQcow2DiskReader(data, size, reader)) return false;
    return DetailDiskAndGameArc::InspectOrExtractVirtualDiskReader(reader, size, outInfo, extractTargetNorm, outExtracted);
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
