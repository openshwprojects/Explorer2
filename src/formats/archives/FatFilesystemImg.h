#pragma once
// FatFilesystemImg.h — FAT12 / FAT16 / FAT32 + VFAT Long Filename filesystem reader & cluster-chain
// extractor (.img, .ima, .flp, and partitions inside virtual disks).

#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

struct FatVolumeParams {
    uint64_t partBase = 0;
    uint64_t partBytes = 0;
    uint32_t bytesPerSec = 512;
    uint32_t secPerClus = 1;
    uint32_t rsvdSecCnt = 1;
    uint32_t numFats = 2;
    uint32_t rootEntCnt = 0;
    uint32_t fatSz = 0;
    uint32_t firstDataSector = 0;
    uint32_t countOfClusters = 0;
    uint32_t rootCluster = 2;
    int fatBits = 16; // 12, 16, or 32
    std::vector<uint8_t> fatTable;

    uint32_t BytesPerCluster() const { return bytesPerSec * secPerClus; }

    uint64_t ClusterOffset(uint32_t clus) const {
        return partBase + (static_cast<uint64_t>(firstDataSector) + static_cast<uint64_t>(clus - 2) * secPerClus) * bytesPerSec;
    }

    uint32_t NextCluster(uint32_t clus) const {
        if (clus < 2) return 0x0FFFFFFFu;
        if (fatBits == 12) {
            size_t off = clus + (clus / 2);
            if (off + 2 > fatTable.size()) return 0x0FFFFFFFu;
            uint16_t v = RdU16LE(fatTable.data() + off);
            v = (clus & 1u) ? (v >> 4) : (v & 0x0FFFu);
            return (v >= 0x0FF8u || v < 2u) ? 0x0FFFFFFFu : v;
        } else if (fatBits == 16) {
            size_t off = static_cast<size_t>(clus) * 2;
            if (off + 2 > fatTable.size()) return 0x0FFFFFFFu;
            uint16_t v = RdU16LE(fatTable.data() + off);
            return (v >= 0xFFF8u || v < 2u) ? 0x0FFFFFFFu : v;
        } else {
            size_t off = static_cast<size_t>(clus) * 4;
            if (off + 4 > fatTable.size()) return 0x0FFFFFFFu;
            uint32_t v = RdU32LE(fatTable.data() + off) & 0x0FFFFFFFu;
            return (v >= 0x0FFFFFF8u || v < 2u) ? 0x0FFFFFFFu : v;
        }
    }
};

inline bool ProbeFatVolume(const VirtualDiskReader& reader, uint64_t partBase, uint64_t partBytes, FatVolumeParams& out) {
    if (partBytes < 2048 || partBase > reader.virtualSize || 512ULL > reader.virtualSize - partBase) return false;
    uint8_t bpb[512] = {};
    if (!reader.readBytes(partBase, bpb, 512)) return false;
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) return false;
    if (std::memcmp(bpb + 3, "NTFS    ", 8) == 0 || std::memcmp(bpb + 3, "EXFAT   ", 8) == 0) return false;
    if (!((bpb[0] == 0xEB && bpb[2] == 0x90) || bpb[0] == 0xE9 || bpb[0] == 0xEB)) return false;

    uint16_t bps = RdU16LE(bpb + 11);
    uint8_t  spc = bpb[13];
    uint16_t rsvd = RdU16LE(bpb + 14);
    uint8_t  nfats = bpb[16];
    uint16_t rootCnt = RdU16LE(bpb + 17);
    if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) ||
        spc == 0 || (spc & (spc - 1)) != 0 || rsvd == 0 || nfats == 0 || nfats > 4)
    {
        return false;
    }

    uint32_t totSec = RdU16LE(bpb + 19) != 0 ? RdU16LE(bpb + 19) : RdU32LE(bpb + 32);
    uint32_t fatSz  = RdU16LE(bpb + 22) != 0 ? RdU16LE(bpb + 22) : RdU32LE(bpb + 36);
    if (totSec == 0 || fatSz == 0) return false;

    uint32_t rootDirSectors = ((static_cast<uint32_t>(rootCnt) * 32u) + (bps - 1u)) / bps;
    uint64_t firstDataSec64 = static_cast<uint64_t>(rsvd) + static_cast<uint64_t>(nfats) * fatSz + rootDirSectors;
    if (firstDataSec64 >= totSec || firstDataSec64 > UINT32_MAX) return false;
    uint32_t firstDataSec = static_cast<uint32_t>(firstDataSec64);
    if (static_cast<uint64_t>(firstDataSec) * bps > reader.virtualSize - partBase) return false;

    uint32_t dataSec = totSec - firstDataSec;
    uint32_t countClusters = dataSec / spc;
    if (countClusters == 0) return false;

    out = {};
    out.partBase = partBase;
    out.partBytes = partBytes;
    out.bytesPerSec = bps;
    out.secPerClus = spc;
    out.rsvdSecCnt = rsvd;
    out.numFats = nfats;
    out.rootEntCnt = rootCnt;
    out.fatSz = fatSz;
    out.firstDataSector = firstDataSec;
    out.countOfClusters = countClusters;

    if (rootCnt == 0 || std::memcmp(bpb + 82, "FAT32", 5) == 0) {
        out.fatBits = 32;
        out.rootCluster = RdU32LE(bpb + 44);
        if (out.rootCluster < 2) out.rootCluster = 2;
    } else if (std::memcmp(bpb + 54, "FAT16", 5) == 0) {
        out.fatBits = 16;
    } else if (std::memcmp(bpb + 54, "FAT12", 5) == 0) {
        out.fatBits = 12;
    } else {
        out.fatBits = (countClusters < 4085) ? 12 : ((countClusters < 65525) ? 16 : 32);
    }

    uint64_t fatRelOff = static_cast<uint64_t>(rsvd) * bps;
    size_t fatByteLen = static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(fatSz) * bps, 16ULL * 1024 * 1024));
    if (fatRelOff > reader.virtualSize - partBase || fatByteLen > reader.virtualSize - partBase - fatRelOff) return false;
    out.fatTable.resize(fatByteLen);
    return reader.readBytes(partBase + fatRelOff, out.fatTable.data(), fatByteLen);
}

inline bool HasFatBootSignatureOnReader(const VirtualDiskReader& reader, uint64_t partBase, uint64_t partBytes) {
    FatVolumeParams dummy;
    return ProbeFatVolume(reader, partBase, partBytes, dummy);
}

inline bool ReadFatClusterChain(
    const VirtualDiskReader& reader,
    const FatVolumeParams& vol,
    uint32_t startCluster,
    uint64_t wantedBytes,
    size_t maxBytes,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (wantedBytes == 0) return true;
    if (startCluster < 2 || wantedBytes > maxBytes) return false;

    size_t target = static_cast<size_t>(wantedBytes);
    outBytes.reserve(std::min<size_t>(target, 1024 * 1024));
    uint32_t bpc = vol.BytesPerCluster();
    std::vector<uint8_t> clusBuf(bpc);

    uint32_t clus = startCluster;
    size_t steps = 0;
    size_t maxSteps = static_cast<size_t>(vol.countOfClusters) + 4;
    while (clus >= 2 && clus < 0x0FFFFFF8u && clus < vol.countOfClusters + 2 &&
           outBytes.size() < target && steps++ < maxSteps)
    {
        uint64_t off = vol.ClusterOffset(clus);
        size_t take = std::min<size_t>(bpc, target - outBytes.size());
        if (off > reader.virtualSize || take > reader.virtualSize - off) return false;
        if (!reader.readBytes(off, clusBuf.data(), take)) return false;
        outBytes.insert(outBytes.end(), clusBuf.data(), clusBuf.data() + take);
        clus = vol.NextCluster(clus);
    }
    return outBytes.size() == target;
}

inline void WalkFatDirectoryBuffer(
    const VirtualDiskReader& reader,
    const FatVolumeParams& vol,
    const uint8_t* dirData,
    size_t dirLen,
    const std::wstring& parentDir,
    int depth,
    std::set<uint32_t>& visitedDirs,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget,
    std::vector<uint8_t>* outExtracted,
    bool* outFound)
{
    if (depth > 16 || outInfo.entries.size() >= 25000) return;
    if (outFound && *outFound) return;

    std::wstring lfnParts[21];
    int lfnMaxOrd = 0;
    auto resetLfn = [&]() {
        for (int k = 0; k < 21; ++k) lfnParts[k].clear();
        lfnMaxOrd = 0;
    };

    for (size_t pos = 0; pos + 32 <= dirLen; pos += 32) {
        const uint8_t* ent = dirData + pos;
        uint8_t firstByte = ent[0];
        if (firstByte == 0x00) break;
        if (firstByte == 0xE5) { resetLfn(); continue; }

        uint8_t attr = ent[11];
        if (attr == 0x0Fu) { // VFAT Long File Name entry
            uint8_t ord = firstByte & 0x3Fu;
            if ((firstByte & 0x40u) != 0) resetLfn();
            if (ord >= 1 && ord <= 20) {
                static const int kOffs[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };
                std::wstring seg;
                for (int c = 0; c < 13; ++c) {
                    uint16_t u = RdU16LE(ent + kOffs[c]);
                    if (u == 0x0000 || u == 0xFFFF) break;
                    seg.push_back(static_cast<wchar_t>(u));
                }
                lfnParts[ord] = std::move(seg);
                if (static_cast<int>(ord) > lfnMaxOrd) lfnMaxOrd = static_cast<int>(ord);
            }
            continue;
        }

        if ((attr & 0x08u) != 0) { // Volume ID
            resetLfn();
            continue;
        }

        std::wstring entryName;
        if (lfnMaxOrd > 0) {
            for (int k = 1; k <= lfnMaxOrd; ++k) entryName += lfnParts[k];
        }
        resetLfn();

        if (entryName.empty()) {
            std::string base(reinterpret_cast<const char*>(ent), 8);
            if (!base.empty() && static_cast<uint8_t>(base[0]) == 0x05) base[0] = static_cast<char>(0xE5);
            while (!base.empty() && base.back() == ' ') base.pop_back();
            std::string ext(reinterpret_cast<const char*>(ent + 8), 3);
            while (!ext.empty() && ext.back() == ' ') ext.pop_back();
            if ((ent[12] & 0x08u) != 0) base = PathUtils::ToLowerAscii(base);
            if ((ent[12] & 0x10u) != 0) ext  = PathUtils::ToLowerAscii(ext);
            std::string full83 = ext.empty() ? base : (base + "." + ext);
            entryName = PathUtils::Utf8ToWide(full83);
        }

        if (entryName.empty() || entryName == L"." || entryName == L"..") continue;

        bool isDir = (attr & 0x10u) != 0;
        uint32_t firstClus = (vol.fatBits == 32 ? (static_cast<uint32_t>(RdU16LE(ent + 20)) << 16) : 0u) | RdU16LE(ent + 26);
        uint32_t fSize     = RdU32LE(ent + 28);
        uint16_t wrtTime   = RdU16LE(ent + 22);
        uint16_t wrtDate   = RdU16LE(ent + 24);
        std::wstring fullRel = ArchivePath::Normalize(parentDir.empty() ? entryName : (parentDir + L"/" + entryName));

        if (extractTarget && outExtracted && outFound) {
            if (!isDir && PathEqualsCI(fullRel, *extractTarget)) {
                if (ReadFatClusterChain(reader, vol, firstClus, fSize, 128 * 1024 * 1024, *outExtracted)) {
                    *outFound = true;
                }
                return;
            }
        } else {
            ArchiveItemEntry item;
            item.pathInArchive = fullRel;
            item.name = ArchivePath::BaseName(fullRel);
            item.isDirectory = isDir;
            item.uncompressedSize = isDir ? 0 : fSize;
            item.compressedSize   = isDir ? 0 : fSize;
            item.dosTime = wrtTime;
            item.dosDate = wrtDate;
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
        }

        if (isDir && firstClus >= 2 && visitedDirs.insert(firstClus).second) {
            std::vector<uint8_t> subDirBuf;
            uint32_t bpc = vol.BytesPerCluster();
            uint32_t c = firstClus;
            size_t steps = 0;
            while (c >= 2 && c < 0x0FFFFFF8u && c < vol.countOfClusters + 2 &&
                   subDirBuf.size() < 2 * 1024 * 1024 && steps++ < 4096)
            {
                uint64_t cOff = vol.ClusterOffset(c);
                if (cOff > reader.virtualSize || bpc > reader.virtualSize - cOff) break;
                size_t oldSz = subDirBuf.size();
                subDirBuf.resize(oldSz + bpc);
                if (!reader.readBytes(cOff, subDirBuf.data() + oldSz, bpc)) {
                    subDirBuf.resize(oldSz);
                    break;
                }
                c = vol.NextCluster(c);
            }
            if (!subDirBuf.empty()) {
                WalkFatDirectoryBuffer(reader, vol, subDirBuf.data(), subDirBuf.size(), fullRel,
                                       depth + 1, visitedDirs, outInfo, extractTarget, outExtracted, outFound);
                if (outFound && *outFound) return;
            }
        }
    }
}

inline bool InspectOrExtractFatOnReader(
    const VirtualDiskReader& reader,
    const FatVolumeParams& vol,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    std::vector<uint8_t> rootBuf;
    std::set<uint32_t> visitedDirs;
    if (vol.fatBits == 32) {
        visitedDirs.insert(vol.rootCluster);
        uint32_t bpc = vol.BytesPerCluster();
        uint32_t c = vol.rootCluster;
        size_t steps = 0;
        while (c >= 2 && c < 0x0FFFFFF8u && c < vol.countOfClusters + 2 &&
               rootBuf.size() < 2 * 1024 * 1024 && steps++ < 4096)
        {
            uint64_t cOff = vol.ClusterOffset(c);
            if (cOff > reader.virtualSize || bpc > reader.virtualSize - cOff) break;
            size_t oldSz = rootBuf.size();
            rootBuf.resize(oldSz + bpc);
            if (!reader.readBytes(cOff, rootBuf.data() + oldSz, bpc)) {
                rootBuf.resize(oldSz);
                break;
            }
            c = vol.NextCluster(c);
        }
    } else {
        size_t rootBytes = static_cast<size_t>(vol.rootEntCnt) * 32u;
        uint64_t rootRel = (static_cast<uint64_t>(vol.rsvdSecCnt) + static_cast<uint64_t>(vol.numFats) * vol.fatSz) * vol.bytesPerSec;
        if (rootRel > reader.virtualSize - vol.partBase || rootBytes > reader.virtualSize - vol.partBase - rootRel) return false;
        rootBuf.resize(rootBytes);
        if (!reader.readBytes(vol.partBase + rootRel, rootBuf.data(), rootBytes)) return false;
    }

    bool found = false;
    WalkFatDirectoryBuffer(reader, vol, rootBuf.data(), rootBuf.size(), prefixDir, 0,
                           visitedDirs, outInfo, extractTarget, outExtracted, &found);
    return extractTarget ? found : !outInfo.entries.empty();
}

inline bool InspectOrExtractFatPartitionOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    FatVolumeParams vol;
    if (!ProbeFatVolume(reader, partBase, partBytes, vol)) return false;
    return InspectOrExtractFatOnReader(reader, vol, prefixDir, outInfo, extractTargetNorm, outExtracted);
}

} // namespace DetailDiskAndGameArc

inline bool InspectFatFilesystemImg(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 2048 || DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"FAT Filesystem Image", false);
    DetailDiskAndGameArc::FatVolumeParams vol;
    if (!DetailDiskAndGameArc::ProbeFatVolume(raw, 0, size, vol)) return false;
    outInfo = {};
    outInfo.formatName = (vol.fatBits == 12) ? L"FAT12 Floppy / Disk Image" :
                         (vol.fatBits == 16) ? L"FAT16 Filesystem Image" :
                                               L"FAT32 Filesystem Image";
    if (!DetailDiskAndGameArc::InspectOrExtractFatOnReader(raw, vol, L"", outInfo, nullptr, nullptr)) {
        outInfo = {};
        return false;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractFatFilesystemImgEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || size < 2048 || normTarget.empty() || DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"FAT Filesystem Image", false);
    DetailDiskAndGameArc::FatVolumeParams vol;
    if (!DetailDiskAndGameArc::ProbeFatVolume(raw, 0, size, vol)) return false;
    ArchiveSummaryInfo dummy;
    return DetailDiskAndGameArc::InspectOrExtractFatOnReader(raw, vol, L"", dummy, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
