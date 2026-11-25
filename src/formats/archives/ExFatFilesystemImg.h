#pragma once
// ExFatFilesystemImg.h — Microsoft exFAT filesystem reader & extractor (.exfat, raw/partitioned .img).
// Validates "EXFAT   " boot sector + 11-sector BootChecksum, walks 32-bit FAT cluster chains AND
// contiguous NoFatChain (GeneralSecondaryFlags & 0x02) streams, and parses 0x85 File + 0xC0 Stream
// Extension + 0xC1 UTF-16LE FileName directory entry sets recursively.

#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

struct ExFatVolumeParams {
    uint64_t partBase = 0;
    uint64_t partBytes = 0;
    uint32_t bytesPerSec = 512;
    uint32_t secPerClus = 1;
    uint32_t fatOffsetSec = 0;
    uint32_t fatLengthSec = 0;
    uint32_t clusterHeapOffsetSec = 0;
    uint32_t clusterCount = 0;
    uint32_t rootDirCluster = 2;
    std::vector<uint8_t> fatTable;

    uint32_t BytesPerCluster() const noexcept { return bytesPerSec * secPerClus; }

    uint64_t ClusterOffset(uint32_t clus) const noexcept {
        return partBase +
               (static_cast<uint64_t>(clusterHeapOffsetSec) + static_cast<uint64_t>(clus - 2u) * secPerClus) * bytesPerSec;
    }

    uint32_t NextCluster(uint32_t clus) const noexcept {
        if (clus < 2u || clus >= clusterCount + 2u) return 0xFFFFFFFFu;
        const size_t off = static_cast<size_t>(clus) * 4u;
        if (off + 4u > fatTable.size()) return 0xFFFFFFFFu;
        const uint32_t v = RdU32LE(fatTable.data() + off);
        if (v < 2u || v >= 0xFFFFFFF7u || v >= clusterCount + 2u) return 0xFFFFFFFFu;
        return v;
    }
};

inline uint32_t ComputeExFatBootChecksum(const uint8_t* bootRegion, size_t regionBytes) noexcept {
    uint32_t sum = 0u;
    for (size_t i = 0; i < regionBytes; ++i) {
        if (i == 106u || i == 107u || i == 112u) continue; // VolumeFlags & PercentInUse excluded per exFAT §3.4
        sum = ((sum & 1u) ? 0x80000000u : 0u) + (sum >> 1u) + bootRegion[i];
    }
    return sum;
}

inline bool ProbeExFatVolume(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    ExFatVolumeParams& out)
{
    if (partBytes < 12u * 512u || partBase > reader.virtualSize || 512ULL > reader.virtualSize - partBase) return false;
    uint8_t sec0[512] = {};
    if (!reader.readBytes(partBase, sec0, 512)) return false;
    if (std::memcmp(sec0 + 3, "EXFAT   ", 8) != 0) return false;
    if (sec0[510] != 0x55u || sec0[511] != 0xAAu) return false;

    // MustBeZero bytes 11..63
    for (size_t z = 11u; z < 64u; ++z) {
        if (sec0[z] != 0u) return false;
    }

    const uint8_t bpsShift = sec0[108];
    const uint8_t spcShift = sec0[109];
    const uint8_t numFats  = sec0[110];
    if (bpsShift < 9u || bpsShift > 12u || spcShift > (25u - bpsShift) || (numFats != 1u && numFats != 2u)) {
        return false;
    }

    const uint32_t bps = 1u << bpsShift;
    const uint32_t spc = 1u << spcShift;
    const size_t bootRegionBytes = static_cast<size_t>(11u) * bps;
    if (bootRegionBytes + bps > partBytes || bootRegionBytes + bps > reader.virtualSize - partBase) return false;

    // Verify 11-sector Main Boot Region Checksum against Sector 11
    std::vector<uint8_t> boot12(bootRegionBytes + 4u);
    if (!reader.readBytes(partBase, boot12.data(), boot12.size())) return false;
    const uint32_t calcSum   = ComputeExFatBootChecksum(boot12.data(), bootRegionBytes);
    const uint32_t storedSum = RdU32LE(boot12.data() + bootRegionBytes);
    if (calcSum != storedSum) return false;

    const uint32_t fatOffSec   = RdU32LE(sec0 + 80);
    const uint32_t fatLenSec   = RdU32LE(sec0 + 84);
    const uint32_t heapOffSec  = RdU32LE(sec0 + 88);
    const uint32_t clusterCnt  = RdU32LE(sec0 + 92);
    const uint32_t rootCluster = RdU32LE(sec0 + 96);

    if (fatOffSec < 12u || fatLenSec == 0u || heapOffSec < fatOffSec + fatLenSec * numFats) return false;
    if (clusterCnt == 0u || rootCluster < 2u || rootCluster >= clusterCnt + 2u) return false;
    if (static_cast<uint64_t>(heapOffSec) * bps >= reader.virtualSize - partBase) return false;

    out = {};
    out.partBase             = partBase;
    out.partBytes            = partBytes;
    out.bytesPerSec          = bps;
    out.secPerClus           = spc;
    out.fatOffsetSec         = fatOffSec;
    out.fatLengthSec         = fatLenSec;
    out.clusterHeapOffsetSec = heapOffSec;
    out.clusterCount         = clusterCnt;
    out.rootDirCluster       = rootCluster;

    const uint64_t fatRelOff = static_cast<uint64_t>(fatOffSec) * bps;
    const size_t fatBytes = static_cast<size_t>(std::min<uint64_t>(
        static_cast<uint64_t>(fatLenSec) * bps,
        std::min<uint64_t>(static_cast<uint64_t>(clusterCnt + 2u) * 4ULL, 32ULL * 1024 * 1024)));
    if (fatRelOff > reader.virtualSize - partBase || fatBytes > reader.virtualSize - partBase - fatRelOff) return false;
    out.fatTable.resize(fatBytes);
    return reader.readBytes(partBase + fatRelOff, out.fatTable.data(), fatBytes);
}

inline bool HasExFatBootSignatureOnReader(const VirtualDiskReader& reader, uint64_t partBase, uint64_t partBytes) {
    if (partBytes < 12u * 512u || partBase > reader.virtualSize || 512ULL > reader.virtualSize - partBase) return false;
    uint8_t sec0[16] = {};
    if (!reader.readBytes(partBase, sec0, sizeof(sec0))) return false;
    return std::memcmp(sec0 + 3, "EXFAT   ", 8) == 0;
}

inline bool ReadExFatStreamBytes(
    const VirtualDiskReader& reader,
    const ExFatVolumeParams& vol,
    uint32_t firstCluster,
    uint64_t wantedBytes,
    bool noFatChain,
    size_t maxBytes,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (wantedBytes == 0) return true;
    if (firstCluster < 2u || firstCluster >= vol.clusterCount + 2u || wantedBytes > maxBytes) return false;

    const size_t target = static_cast<size_t>(wantedBytes);
    if (noFatChain) {
        const uint64_t startOff = vol.ClusterOffset(firstCluster);
        if (startOff > reader.virtualSize || target > reader.virtualSize - startOff) return false;
        outBytes.resize(target);
        return reader.readBytes(startOff, outBytes.data(), target);
    }

    outBytes.reserve(std::min<size_t>(target, 1024 * 1024));
    const uint32_t bpc = vol.BytesPerCluster();
    std::vector<uint8_t> clusBuf(bpc);

    uint32_t clus = firstCluster;
    size_t steps = 0;
    const size_t maxSteps = static_cast<size_t>(vol.clusterCount) + 4u;
    while (clus >= 2u && clus < vol.clusterCount + 2u && outBytes.size() < target && steps++ < maxSteps) {
        const uint64_t off = vol.ClusterOffset(clus);
        const size_t take = std::min<size_t>(bpc, target - outBytes.size());
        if (off > reader.virtualSize || take > reader.virtualSize - off) return false;
        if (!reader.readBytes(off, clusBuf.data(), take)) return false;
        outBytes.insert(outBytes.end(), clusBuf.data(), clusBuf.data() + take);
        clus = vol.NextCluster(clus);
    }
    return outBytes.size() == target;
}

inline bool ReadExFatDirBuffer(
    const VirtualDiskReader& reader,
    const ExFatVolumeParams& vol,
    uint32_t startCluster,
    uint64_t dataLengthHint,
    bool noFatChain,
    std::vector<uint8_t>& outDir)
{
    outDir.clear();
    if (startCluster < 2u || startCluster >= vol.clusterCount + 2u) return false;
    constexpr size_t kMaxDirBytes = 4 * 1024 * 1024;
    if (noFatChain) {
        const size_t dirLen = static_cast<size_t>(std::min<uint64_t>(
            dataLengthHint > 0 ? dataLengthHint : vol.BytesPerCluster(),
            kMaxDirBytes));
        return ReadExFatStreamBytes(reader, vol, startCluster, dirLen, true, kMaxDirBytes, outDir);
    }

    const uint32_t bpc = vol.BytesPerCluster();
    uint32_t c = startCluster;
    size_t steps = 0;
    const size_t maxBytes = (dataLengthHint > 0 && dataLengthHint < kMaxDirBytes)
        ? static_cast<size_t>(dataLengthHint)
        : kMaxDirBytes;
    while (c >= 2u && c < vol.clusterCount + 2u && outDir.size() < maxBytes && steps++ < 4096u) {
        const uint64_t off = vol.ClusterOffset(c);
        if (off > reader.virtualSize || bpc > reader.virtualSize - off) break;
        const size_t prev = outDir.size();
        outDir.resize(prev + bpc);
        if (!reader.readBytes(off, outDir.data() + prev, bpc)) {
            outDir.resize(prev);
            break;
        }
        c = vol.NextCluster(c);
    }
    return !outDir.empty();
}

inline void WalkExFatDirectory(
    const VirtualDiskReader& reader,
    const ExFatVolumeParams& vol,
    const uint8_t* dirData,
    size_t dirLen,
    const std::wstring& parentDir,
    int depth,
    std::set<uint32_t>& visitedDirs,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted,
    bool* outFound)
{
    if (depth > 20 || outInfo.entries.size() >= 30000) return;
    if (outFound && *outFound) return;

    size_t pos = 0;
    while (pos + 32u <= dirLen && outInfo.entries.size() < 30000) {
        const uint8_t etype = dirData[pos];
        if (etype == 0x00u) break; // End of directory

        if (etype != 0x85u) { // Skip Allocation Bitmap (0x81), Up-Case Table (0x82), Volume Label (0x83), deleted (<0x80)
            pos += 32u;
            continue;
        }

        const uint8_t secondaryCount = dirData[pos + 1u];
        if (secondaryCount < 2u || secondaryCount > 18u || (static_cast<size_t>(secondaryCount) + 1u) * 32u > dirLen - pos) {
            pos += 32u;
            continue;
        }

        const uint8_t* fileEnt   = dirData + pos;
        const uint8_t* streamEnt = dirData + pos + 32u;
        if (streamEnt[0] != 0xC0u) {
            pos += 32u;
            continue;
        }

        const uint16_t fileAttrs     = RdU16LE(fileEnt + 4u);
        const uint32_t modTimestamp  = RdU32LE(fileEnt + 12u);
        const uint8_t  secFlags      = streamEnt[1u];
        const uint8_t  nameChars     = streamEnt[3u];
        const uint64_t validDataLen  = RdU64LE(streamEnt + 8u);
        const uint32_t firstCluster  = RdU32LE(streamEnt + 20u);
        const uint64_t allocDataLen  = RdU64LE(streamEnt + 24u);
        const bool     noFatChain    = (secFlags & 0x02u) != 0u;
        const bool     isDir         = (fileAttrs & 0x10u) != 0u;

        std::wstring wname;
        wname.reserve(nameChars);
        for (uint8_t s = 2u; s <= secondaryCount && wname.size() < nameChars; ++s) {
            const uint8_t* fnEnt = dirData + pos + static_cast<size_t>(s) * 32u;
            if (fnEnt[0] != 0xC1u) break;
            for (size_t c = 0; c < 15u && wname.size() < nameChars; ++c) {
                const uint16_t cu = RdU16LE(fnEnt + 2u + c * 2u);
                if (cu == 0u) break;
                wchar_t wc = static_cast<wchar_t>(cu);
                if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
                wname.push_back(wc);
            }
        }
        pos += (static_cast<size_t>(secondaryCount) + 1u) * 32u;

        if (wname.empty() || wname == L"." || wname == L"..") continue;
        const std::wstring fullRel = ArchivePath::Normalize(parentDir.empty() ? wname : (parentDir + L"/" + wname));
        if (fullRel.empty()) continue;

        const uint64_t effectiveSize = std::min<uint64_t>(validDataLen, allocDataLen > 0 ? allocDataLen : validDataLen);

        if (extractTargetNorm && outExtracted && outFound) {
            if (!isDir && PathEqualsCI(fullRel, *extractTargetNorm)) {
                if (ReadExFatStreamBytes(reader, vol, firstCluster, effectiveSize, noFatChain, 128 * 1024 * 1024, *outExtracted)) {
                    *outFound = true;
                }
                return;
            }
        } else {
            ArchiveItemEntry item;
            item.pathInArchive    = fullRel;
            item.name             = ArchivePath::BaseName(fullRel);
            item.isDirectory      = isDir;
            item.uncompressedSize = isDir ? 0ULL : effectiveSize;
            item.compressedSize   = isDir ? 0ULL : allocDataLen;
            item.dosTime          = static_cast<uint16_t>(modTimestamp & 0xFFFFu);
            item.dosDate          = static_cast<uint16_t>((modTimestamp >> 16u) & 0xFFFFu);
            if (!isDir && noFatChain && reader.directRawBuffer && firstCluster >= 2u) {
                item.rawDataOffset    = vol.ClusterOffset(firstCluster);
                item.compressionMethod = 0u;
            } else {
                item.compressionMethod = 1u;
            }
            outInfo.entries.push_back(std::move(item));
        }

        if (isDir && firstCluster >= 2u && visitedDirs.insert(firstCluster).second) {
            std::vector<uint8_t> subBuf;
            if (ReadExFatDirBuffer(reader, vol, firstCluster, allocDataLen, noFatChain, subBuf)) {
                WalkExFatDirectory(reader, vol, subBuf.data(), subBuf.size(), fullRel,
                                   depth + 1, visitedDirs, outInfo, extractTargetNorm, outExtracted, outFound);
                if (outFound && *outFound) return;
            }
        }
    }
}

inline bool InspectOrExtractExFatOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    ExFatVolumeParams vol;
    if (!ProbeExFatVolume(reader, partBase, partBytes, vol)) return false;

    std::vector<uint8_t> rootBuf;
    if (!ReadExFatDirBuffer(reader, vol, vol.rootDirCluster, 0, false, rootBuf)) return false;

    std::set<uint32_t> visitedDirs;
    visitedDirs.insert(vol.rootDirCluster);
    bool found = false;
    WalkExFatDirectory(reader, vol, rootBuf.data(), rootBuf.size(), prefixDir, 0,
                       visitedDirs, outInfo, extractTargetNorm, outExtracted, &found);
    return extractTargetNorm ? found : !outInfo.entries.empty();
}

} // namespace DetailDiskAndGameArc

inline bool InspectExFatFilesystemImg(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 12u * 512u || DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"Microsoft exFAT Image (.exfat)", false);
    outInfo = {};
    outInfo.formatName = L"Microsoft exFAT Image (.exfat)";
    if (!DetailDiskAndGameArc::InspectOrExtractExFatOnReader(raw, 0, size, L"", outInfo, nullptr, nullptr)) {
        outInfo = {};
        return false;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractExFatFilesystemImgEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || size < 12u * 512u || normTarget.empty() || DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"Microsoft exFAT Image (.exfat)", false);
    ArchiveSummaryInfo dummy;
    return DetailDiskAndGameArc::InspectOrExtractExFatOnReader(raw, 0, size, L"", dummy, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
