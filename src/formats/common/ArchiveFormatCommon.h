#pragma once
// ArchiveFormatCommon.h — Shared stream codec declarations, virtual disk reader interface,
// partition/filesystem mount helpers (DetailArc100), and shared decompression/bitstream helpers (DetailArc200).

#include "../../Base64.h"
#include "../../ByteIO.h"
#include "../../Crc32.h"
#include "../../JsonDom.h"
#include "../../Lz4Block.h"
#include "../../ArchiveTypes.h"
#include "../../ZipDeflate.h"
#include "../../Ole2Compound.h"
#include "../../PathUtils.h"
#include "../../StreamCodecs.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer2::StreamCodecs {

bool ParsePosixTarFromMemory(
    const uint8_t* buf,
    size_t len,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr);

bool ExtractCabEntryFromFile(
    const std::wstring& cabFilePath,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes);

bool ExtractCabEntryFromMemory(
    const uint8_t* cabBytes,
    size_t cabSize,
    const std::wstring& targetNorm,
    std::vector<uint8_t>& outBytes);

} // namespace Explorer2::StreamCodecs

namespace Explorer2::Formats {

namespace DetailArc100 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;
inline void WrU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

inline bool PathEqualsCI(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

inline void FileTimeU64ToDos(uint64_t ftTicks, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (ftTicks == 0) return;
    FILETIME utcFt = {}, locFt = {};
    utcFt.dwLowDateTime = static_cast<DWORD>(ftTicks & 0xFFFFFFFFu);
    utcFt.dwHighDateTime = static_cast<DWORD>(ftTicks >> 32);
    if (FileTimeToLocalFileTime(&utcFt, &locFt)) {
        FileTimeToDosDateTime(&locFt, &outDate, &outTime);
    }
}

// ============================================================================
// Abstract Virtual Disk / Raw Reader Interface
// ============================================================================
struct VirtualDiskReader {
    uint64_t virtualSize = 0;
    std::wstring formatName;
    bool directRawBuffer = false; // true when diskOff maps 1:1 to host file offset
    bool allowRawSliceFallback = true;
    std::function<bool(uint64_t diskOff, uint8_t* dst, size_t len)> readBytes;
};

// ============================================================================
// 19. ISO 9660 Optical Disc Image (.iso) — PVD + Joliet UTF-16BE + Rock Ridge NM
// ============================================================================
inline std::wstring DecodeIsoEntryName(const uint8_t* rec, uint8_t recLen, bool isJoliet) {
    uint8_t nameLen = rec[32];
    if (33u + nameLen > recLen || nameLen == 0) return L"";
    const uint8_t* namePtr = rec + 33;
    if (nameLen == 1 && (namePtr[0] == 0x00 || namePtr[0] == 0x01)) return L"";

    // Check Rock Ridge System Use Area for POSIX "NM" alternate name
    size_t suaOff = 33u + nameLen + ((nameLen & 1u) == 0 ? 1u : 0u);
    std::string rrName;
    while (suaOff + 4 <= recLen) {
        uint8_t suLen = rec[suaOff + 2];
        if (suLen < 4 || suaOff + suLen > recLen) break;
        if (rec[suaOff] == 'N' && rec[suaOff + 1] == 'M' && suLen > 5) {
            uint8_t nmFlags = rec[suaOff + 4];
            if ((nmFlags & 0x06u) == 0) {
                rrName.append(reinterpret_cast<const char*>(rec + suaOff + 5), suLen - 5);
            }
        }
        suaOff += suLen;
    }
    if (!rrName.empty()) {
        return PathUtils::Utf8ToWide(rrName);
    }

    std::wstring wname;
    if (isJoliet && (nameLen & 1u) == 0) {
        for (uint8_t i = 0; i + 1 < nameLen; i += 2) {
            wchar_t wc = static_cast<wchar_t>(RdU16BE(namePtr + i));
            if (wc == 0) break;
            wname.push_back(wc);
        }
    } else {
        std::string rawName(reinterpret_cast<const char*>(namePtr), nameLen);
        wname = PathUtils::Utf8ToWide(rawName);
    }

    size_t semi = wname.find(L';');
    if (semi != std::wstring::npos) wname.resize(semi);
    if (!wname.empty() && wname.back() == L'.') wname.pop_back();
    return wname;
}

inline void WalkIsoDirectoryReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint32_t extentLba,
    uint32_t extentBytes,
    bool isJoliet,
    const std::wstring& parentDir,
    int depth,
    std::set<uint32_t>& visitedExtents,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr,
    bool* outFoundExtract = nullptr)
{
    if (depth > 12 || extentLba == 0 || extentBytes == 0 || outInfo.entries.size() >= 25000) return;
    if (outFoundExtract && *outFoundExtract) return;
    if (!visitedExtents.insert(extentLba).second) return;

    uint32_t clampedBytes = std::min<uint32_t>(extentBytes, 4 * 1024 * 1024);
    std::vector<uint8_t> dirBuf(clampedBytes);
    uint64_t startOff = partBase + static_cast<uint64_t>(extentLba) * 2048ULL;
    if (startOff + clampedBytes > reader.virtualSize) {
        if (startOff >= reader.virtualSize) return;
        clampedBytes = static_cast<uint32_t>(reader.virtualSize - startOff);
        dirBuf.resize(clampedBytes);
    }
    if (!reader.readBytes(startOff, dirBuf.data(), clampedBytes)) return;

    size_t pos = 0;
    while (pos < clampedBytes && outInfo.entries.size() < 25000) {
        uint8_t recLen = dirBuf[pos];
        if (recLen == 0) {
            pos = ((pos / 2048) + 1) * 2048;
            continue;
        }
        if (recLen < 34 || pos + recLen > clampedBytes) break;

        const uint8_t* rec = dirBuf.data() + pos;
        uint32_t childLba = RdU32LE(rec + 2);
        uint32_t dataLen  = RdU32LE(rec + 10);
        uint8_t  flags    = rec[25];
        std::wstring wname = DecodeIsoEntryName(rec, recLen, isJoliet);
        if (!wname.empty()) {
            std::wstring fullRel = ArchivePath::Normalize(parentDir.empty() ? wname : (parentDir + L"/" + wname));
            bool isDir = (flags & 0x02u) != 0;

            if (extractTarget && outExtracted && outFoundExtract) {
                if (!isDir && PathEqualsCI(fullRel, *extractTarget)) {
                    if (dataLen <= 128 * 1024 * 1024) {
                        outExtracted->resize(dataLen);
                        if (dataLen == 0 || reader.readBytes(partBase + static_cast<uint64_t>(childLba) * 2048ULL, outExtracted->data(), dataLen)) {
                            *outFoundExtract = true;
                            return;
                        }
                        outExtracted->clear();
                    }
                    return;
                }
            } else {
                ArchiveItemEntry item;
                item.pathInArchive = fullRel;
                item.name = ArchivePath::BaseName(fullRel);
                item.isDirectory = isDir;
                item.uncompressedSize = isDir ? 0 : dataLen;
                item.compressedSize   = isDir ? 0 : dataLen;
                item.rawDataOffset    = partBase + static_cast<uint64_t>(childLba) * 2048ULL;
                item.compressionMethod = reader.directRawBuffer ? 0 : 1;
                outInfo.entries.push_back(std::move(item));
            }

            if (isDir && childLba != extentLba) {
                WalkIsoDirectoryReader(reader, partBase, childLba, dataLen, isJoliet, fullRel,
                                       depth + 1, visitedExtents, outInfo, extractTarget, outExtracted, outFoundExtract);
                if (outFoundExtract && *outFoundExtract) return;
            }
        }
        pos += recLen;
    }
}

inline bool InspectOrExtractIsoOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBase + 0x8800ULL > reader.virtualSize) return false;
    uint8_t vd[2048] = {};
    if (!reader.readBytes(partBase + 0x8000ULL, vd, 2048)) return false;
    if (vd[0] != 1 || std::memcmp(vd + 1, "CD001", 5) != 0) return false;

    uint32_t rootLba = RdU32LE(vd + 156 + 2);
    uint32_t rootLen = RdU32LE(vd + 156 + 10);
    bool useJoliet = false;

    // Scan subsequent volume descriptors for Joliet UTF-16BE SVD (type 2)
    for (uint32_t sec = 17; sec < 32; ++sec) {
        uint64_t off = partBase + static_cast<uint64_t>(sec) * 2048ULL;
        if (off + 2048 > reader.virtualSize) break;
        uint8_t svd[2048] = {};
        if (!reader.readBytes(off, svd, 2048) || std::memcmp(svd + 1, "CD001", 5) != 0) break;
        if (svd[0] == 255) break; // Volume Descriptor Set Terminator
        if (svd[0] == 2 && svd[88] == 0x25 && svd[89] == 0x2F &&
            (svd[90] == 0x40 || svd[90] == 0x43 || svd[90] == 0x45))
        {
            uint32_t jLba = RdU32LE(svd + 156 + 2);
            uint32_t jLen = RdU32LE(svd + 156 + 10);
            if (jLba > 0 && jLen > 0) {
                rootLba = jLba;
                rootLen = jLen;
                useJoliet = true;
                break;
            }
        }
    }

    std::set<uint32_t> visited;
    bool foundExtract = false;
    WalkIsoDirectoryReader(reader, partBase, rootLba, rootLen, useJoliet, prefixDir, 0,
                           visited, outInfo, extractTarget, outExtracted, &foundExtract);
    if (extractTarget) return foundExtract;
    return true;
}

inline bool Base64DecodeBlock(std::string_view in, std::vector<uint8_t>& out) {
    ::Explorer2::Base64::Decode(in, out);
    return !out.empty();
}

// ----------------------------------------------------------------------------
// FAT12 / FAT16 / FAT32 Filesystem Inspector & Cluster-Chain Extractor
// ----------------------------------------------------------------------------
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
            if (off + 2 > fatTable.size()) return 0x0FFFu;
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
    if (partBytes < 2048 || partBase + 512 > reader.virtualSize) return false;
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
    uint32_t firstDataSec = static_cast<uint32_t>(rsvd) + static_cast<uint32_t>(nfats) * fatSz + rootDirSectors;
    if (firstDataSec >= totSec) return false;
    if (partBase + static_cast<uint64_t>(firstDataSec) * bps > reader.virtualSize) return false;

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

    uint64_t fatByteOff = partBase + static_cast<uint64_t>(rsvd) * bps;
    size_t fatByteLen = static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(fatSz) * bps, 16ULL * 1024 * 1024));
    if (fatByteOff + fatByteLen > reader.virtualSize) return false;
    out.fatTable.resize(fatByteLen);
    return reader.readBytes(fatByteOff, out.fatTable.data(), fatByteLen);
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
        if (off + take > reader.virtualSize) return false;
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
        if (firstByte == 0x00) break; // End of directory
        if (firstByte == 0xE5) { resetLfn(); continue; } // Deleted entry

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
                size_t oldSz = subDirBuf.size();
                subDirBuf.resize(oldSz + bpc);
                if (!reader.readBytes(vol.ClusterOffset(c), subDirBuf.data() + oldSz, bpc)) {
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
            size_t oldSz = rootBuf.size();
            rootBuf.resize(oldSz + bpc);
            if (!reader.readBytes(vol.ClusterOffset(c), rootBuf.data() + oldSz, bpc)) {
                rootBuf.resize(oldSz);
                break;
            }
            c = vol.NextCluster(c);
        }
    } else {
        size_t rootBytes = static_cast<size_t>(vol.rootEntCnt) * 32u;
        uint64_t rootOff = vol.partBase + (static_cast<uint64_t>(vol.rsvdSecCnt) + static_cast<uint64_t>(vol.numFats) * vol.fatSz) * vol.bytesPerSec;
        if (rootOff + rootBytes > reader.virtualSize) return false;
        rootBuf.resize(rootBytes);
        if (!reader.readBytes(rootOff, rootBuf.data(), rootBytes)) return false;
    }

    bool found = false;
    WalkFatDirectoryBuffer(reader, vol, rootBuf.data(), rootBuf.size(), prefixDir, 0,
                           visitedDirs, outInfo, extractTarget, outExtracted, &found);
    return extractTarget ? found : true;
}

// ----------------------------------------------------------------------------
// NTFS $MFT Inspector & Resident / Non-Resident File Extractor
// ----------------------------------------------------------------------------
inline bool InspectOrExtractNtfsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTarget = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBytes < 4096 || partBase + 512 > reader.virtualSize) return false;
    uint8_t bpb[512] = {};
    if (!reader.readBytes(partBase, bpb, 512) || std::memcmp(bpb + 3, "NTFS    ", 8) != 0) return false;

    uint16_t bps = RdU16LE(bpb + 11);
    uint8_t  spc = bpb[13];
    uint64_t mftLcn = RdU64LE(bpb + 48);
    int8_t   recSpec = static_cast<int8_t>(bpb[64]);
    if (bps < 512 || spc == 0) return false;
    uint32_t clusterBytes = static_cast<uint32_t>(bps) * spc;
    uint32_t recSize = (recSpec < 0) ? (1u << (-recSpec)) : (static_cast<uint32_t>(recSpec) * clusterBytes);
    if (recSize < 512 || recSize > 8192) return false;

    uint64_t mftBase = partBase + mftLcn * clusterBytes;
    if (mftBase + recSize > reader.virtualSize) return false;

    struct NtfsRec {
        uint64_t parentIdx = 5;
        std::wstring name;
        uint8_t nameSpace = 255;
        bool isDir = false;
        bool valid = false;
        uint64_t fileSize = 0;
        bool resident = false;
        std::vector<uint8_t> residentData;
        std::vector<std::pair<uint64_t, uint64_t>> runs; // (lcnByteOff, byteLen)
    };

    const uint32_t maxRecs = static_cast<uint32_t>(std::min<uint64_t>((reader.virtualSize - mftBase) / recSize, 2048));
    std::unordered_map<uint64_t, NtfsRec> records;
    std::vector<uint8_t> recBuf(recSize);

    for (uint32_t idx = 0; idx < maxRecs; ++idx) {
        if (!reader.readBytes(mftBase + static_cast<uint64_t>(idx) * recSize, recBuf.data(), recSize)) break;
        if (std::memcmp(recBuf.data(), "FILE", 4) != 0) {
            if (idx > 24 && records.empty()) break;
            continue;
        }
        // Apply Update Sequence Array (USA) fixups
        uint16_t usaOff   = RdU16LE(recBuf.data() + 4);
        uint16_t usaCount = RdU16LE(recBuf.data() + 6);
        if (usaCount >= 2u && static_cast<size_t>(usaOff) + static_cast<size_t>(usaCount) * 2u <= recSize) {
            for (uint16_t s = 1; s < usaCount && static_cast<size_t>(s) * 512u <= recSize; ++s) {
                recBuf[s * 512 - 2] = recBuf[usaOff + s * 2];
                recBuf[s * 512 - 1] = recBuf[usaOff + s * 2 + 1];
            }
        }

        uint16_t flags = RdU16LE(recBuf.data() + 22);
        if ((flags & 0x01u) == 0) continue; // Not in use

        uint16_t attrOff = RdU16LE(recBuf.data() + 20);
        NtfsRec nr;
        nr.isDir = (flags & 0x02u) != 0;

        size_t ap = attrOff;
        while (ap + 16 <= recSize) {
            uint32_t attrType = RdU32LE(recBuf.data() + ap);
            if (attrType == 0xFFFFFFFFu) break;
            uint32_t attrLen = RdU32LE(recBuf.data() + ap + 4);
            if (attrLen < 16 || ap + attrLen > recSize) break;
            uint8_t nonRes  = recBuf[ap + 8];
            uint8_t nameLen = recBuf[ap + 9];

            if (attrType == 0x30u && nonRes == 0 && attrLen >= 24) { // $FILE_NAME
                uint32_t valLen = RdU32LE(recBuf.data() + ap + 16);
                uint16_t valOff = RdU16LE(recBuf.data() + ap + 20);
                if (valOff + valLen <= attrLen && valLen >= 66) {
                    const uint8_t* fn = recBuf.data() + ap + valOff;
                    uint64_t pRef = RdU64LE(fn) & 0x0000FFFFFFFFFFFFULL;
                    uint8_t fnChars = fn[64];
                    uint8_t fnSpace = fn[65];
                    if (66u + static_cast<size_t>(fnChars) * 2 <= valLen && fnChars > 0) {
                        if (nr.name.empty() || (nr.nameSpace == 2 && fnSpace != 2)) {
                            nr.parentIdx = pRef;
                            nr.nameSpace = fnSpace;
                            nr.name.resize(fnChars);
                            for (uint8_t c = 0; c < fnChars; ++c) {
                                nr.name[c] = static_cast<wchar_t>(RdU16LE(fn + 66 + c * 2));
                            }
                            nr.valid = true;
                        }
                    }
                }
            } else if (attrType == 0x80u && nameLen == 0) { // Unnamed $DATA
                if (nonRes == 0 && attrLen >= 24) {
                    uint32_t valLen = RdU32LE(recBuf.data() + ap + 16);
                    uint16_t valOff = RdU16LE(recBuf.data() + ap + 20);
                    if (valOff + valLen <= attrLen) {
                        nr.fileSize = valLen;
                        nr.resident = true;
                        nr.residentData.assign(recBuf.data() + ap + valOff, recBuf.data() + ap + valOff + valLen);
                    }
                } else if (nonRes == 1 && attrLen >= 64) {
                    nr.fileSize = RdU64LE(recBuf.data() + ap + 48);
                    uint16_t runOff = RdU16LE(recBuf.data() + ap + 32);
                    int64_t curLcn = 0;
                    size_t rp = ap + runOff;
                    while (rp < ap + attrLen && recBuf[rp] != 0) {
                        uint8_t hdr = recBuf[rp++];
                        uint8_t lenB = hdr & 0x0Fu;
                        uint8_t offB = (hdr >> 4) & 0x0Fu;
                        if (lenB == 0 || lenB > 8 || offB > 8 || rp + lenB + offB > ap + attrLen) break;
                        uint64_t rLen = 0;
                        for (uint8_t b = 0; b < lenB; ++b) rLen |= static_cast<uint64_t>(recBuf[rp++]) << (b * 8);
                        int64_t rDelta = 0;
                        for (uint8_t b = 0; b < offB; ++b) rDelta |= static_cast<int64_t>(recBuf[rp++]) << (b * 8);
                        if (offB > 0 && (recBuf[rp - 1] & 0x80u) != 0) {
                            for (uint8_t b = offB; b < 8; ++b) rDelta |= (-1LL << (b * 8));
                        }
                        if (offB == 0) { // Sparse run
                            nr.runs.push_back({ UINT64_MAX, rLen * clusterBytes });
                        } else {
                            curLcn += rDelta;
                            if (curLcn > 0) nr.runs.push_back({ partBase + static_cast<uint64_t>(curLcn) * clusterBytes, rLen * clusterBytes });
                        }
                    }
                }
            }
            ap += attrLen;
        }
        if (nr.valid) records[idx] = std::move(nr);
    }

    auto buildFullNtfsPath = [&](uint64_t idx) -> std::wstring {
        std::vector<std::wstring> parts;
        uint64_t cur = idx;
        for (int depth = 0; depth < 16 && cur != 5; ++depth) {
            auto it = records.find(cur);
            if (it == records.end() || it->second.name.empty() || it->second.name == L".") break;
            parts.push_back(it->second.name);
            if (it->second.parentIdx == cur) break;
            cur = it->second.parentIdx;
        }
        std::wstring out = prefixDir;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            out = out.empty() ? *it : (out + L"/" + *it);
        }
        return ArchivePath::Normalize(out);
    };

    // Prefer user files first; if disk only has NTFS system files ($MFT, etc.), include them so volume isn't blank
    bool hasUserFiles = false;
    for (const auto& kv : records) {
        if (kv.first >= 24 && !kv.second.name.empty() && kv.second.name[0] != L'$') {
            hasUserFiles = true;
            break;
        }
    }

    for (const auto& kv : records) {
        if (kv.first == 5) continue; // Root "."
        if (hasUserFiles && (!kv.second.name.empty() && kv.second.name[0] == L'$')) continue;
        std::wstring fullRel = buildFullNtfsPath(kv.first);
        if (fullRel.empty()) continue;

        if (extractTarget && outExtracted) {
            if (!kv.second.isDir && PathEqualsCI(fullRel, *extractTarget)) {
                if (kv.second.resident) {
                    *outExtracted = kv.second.residentData;
                    return true;
                }
                if (kv.second.fileSize <= 128 * 1024 * 1024) {
                    outExtracted->clear();
                    outExtracted->reserve(static_cast<size_t>(kv.second.fileSize));
                    for (const auto& run : kv.second.runs) {
                        if (outExtracted->size() >= kv.second.fileSize) break;
                        size_t take = static_cast<size_t>(std::min<uint64_t>(run.second, kv.second.fileSize - outExtracted->size()));
                        size_t prev = outExtracted->size();
                        outExtracted->resize(prev + take, 0);
                        if (run.first != UINT64_MAX) {
                            if (!reader.readBytes(run.first, outExtracted->data() + prev, take)) return false;
                        }
                    }
                    return outExtracted->size() == kv.second.fileSize;
                }
                return false;
            }
        } else {
            ArchiveItemEntry item;
            item.pathInArchive = fullRel;
            item.name = ArchivePath::BaseName(fullRel);
            item.isDirectory = kv.second.isDir;
            item.uncompressedSize = kv.second.isDir ? 0 : kv.second.fileSize;
            item.compressedSize   = item.uncompressedSize;
            item.compressionMethod = 1;
            outInfo.entries.push_back(std::move(item));
        }
    }
    return extractTarget ? false : !outInfo.entries.empty();
}

// ----------------------------------------------------------------------------
// Linux ext2/ext3/ext4 Full Inode, Extent-Tree & Recursive Directory Inspector + Extractor
// ----------------------------------------------------------------------------
inline void UnixTimeU32ToDos(uint32_t unixSecs, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    if (unixSecs == 0) return;
    uint64_t ticks = (static_cast<uint64_t>(unixSecs) + 11644473600ULL) * 10000000ULL;
    FileTimeU64ToDos(ticks, outDate, outTime);
}

inline bool InspectOrExtractExtFsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBytes < 2048 || partBase > reader.virtualSize || partBytes > reader.virtualSize - partBase) return false;
    uint8_t sb[1024] = {};
    if (!reader.readBytes(partBase + 1024, sb, 1024)) return false;
    if (RdU16LE(sb + 56) != 0xEF53u) return false;

    uint32_t inodesCnt    = RdU32LE(sb + 0);
    uint32_t firstDataBlk = RdU32LE(sb + 20);
    uint32_t logBlkSz     = RdU32LE(sb + 24);
    uint32_t blocksPerGrp = RdU32LE(sb + 32);
    uint32_t inodesPerGrp = RdU32LE(sb + 40);
    uint32_t revLevel     = RdU32LE(sb + 76);
    uint16_t inodeSz      = (revLevel >= 1) ? RdU16LE(sb + 88) : 128;
    uint32_t incompat     = RdU32LE(sb + 96);
    uint16_t rawDescSz    = RdU16LE(sb + 254);

    if (logBlkSz > 6) return false;
    uint32_t blockSize = 1024u << logBlkSz;
    if (inodeSz < 128 || inodeSz > blockSize || inodesCnt < 2 || inodesPerGrp == 0 || blocksPerGrp == 0) return false;
    (void)firstDataBlk;

    bool has64Bit    = (incompat & 0x80u) != 0;
    bool hasFileType = (incompat & 0x02u) != 0;
    uint32_t descSize = (has64Bit && rawDescSz >= 64) ? rawDescSz : 32u;
    if (descSize > 1024) return false;

    uint64_t bgdtBase = partBase + ((blockSize == 1024u) ? 2048ULL : static_cast<uint64_t>(blockSize));

    auto readExtInode = [&](uint32_t inoNum, std::vector<uint8_t>& outIno) -> bool {
        if (inoNum == 0 || inoNum > inodesCnt) return false;
        uint32_t bg  = (inoNum - 1u) / inodesPerGrp;
        uint32_t idx = (inoNum - 1u) % inodesPerGrp;
        uint64_t gdOff = bgdtBase + static_cast<uint64_t>(bg) * descSize;
        size_t gdNeed = (descSize >= 64) ? 64u : 32u;
        if (gdOff + gdNeed > partBase + partBytes || gdOff + gdNeed > reader.virtualSize) return false;
        uint8_t gd[64] = {};
        if (!reader.readBytes(gdOff, gd, gdNeed)) return false;

        uint64_t inoTblBlk = RdU32LE(gd + 8);
        if (descSize >= 64) {
            inoTblBlk |= (static_cast<uint64_t>(RdU32LE(gd + 40)) << 32);
        }
        if (inoTblBlk == 0) return false;
        uint64_t inoOff = partBase + inoTblBlk * blockSize + static_cast<uint64_t>(idx) * inodeSz;
        if (inoOff + inodeSz > partBase + partBytes || inoOff + inodeSz > reader.virtualSize) return false;
        outIno.resize(inodeSz);
        return reader.readBytes(inoOff, outIno.data(), inodeSz);
    };

    auto getInodeSize = [&](const std::vector<uint8_t>& ino) -> uint64_t {
        uint16_t mode = RdU16LE(ino.data() + 0);
        uint64_t sz   = RdU32LE(ino.data() + 4);
        if (((mode & 0xF000u) == 0x8000u) && revLevel >= 1) {
            sz |= (static_cast<uint64_t>(RdU32LE(ino.data() + 108)) << 32);
        }
        return sz;
    };

    auto readInodeData = [&](const std::vector<uint8_t>& ino, uint64_t maxBytes, std::vector<uint8_t>& outData) -> bool {
        uint16_t mode   = RdU16LE(ino.data() + 0);
        uint32_t iFlags = RdU32LE(ino.data() + 32);
        uint64_t fSize  = getInodeSize(ino);
        if (fSize > maxBytes) return false;
        outData.assign(static_cast<size_t>(fSize), 0);
        if (fSize == 0) return true;

        // Fast symlink or inline data stored directly inside i_block[0..14] (60 bytes)
        if (((iFlags & 0x10000000u) != 0 || ((mode & 0xF000u) == 0xA000u && RdU32LE(ino.data() + 28) == 0)) && fSize <= 60) {
            std::memcpy(outData.data(), ino.data() + 40, static_cast<size_t>(fSize));
            return true;
        }

        // ext4 extent tree (EXT4_EXTENTS_FL = 0x00080000, eh_magic = 0xF30A)
        if ((iFlags & 0x00080000u) != 0) {
            auto walkExtentNode = [&](auto& self, const uint8_t* ehBuf, size_t bufLen, int depthGuard) -> bool {
                if (depthGuard > 8 || bufLen < 12) return false;
                if (RdU16LE(ehBuf) != 0xF30Au) return false;
                uint16_t ehEntries = RdU16LE(ehBuf + 2);
                uint16_t ehDepth   = RdU16LE(ehBuf + 6);
                if (12u + static_cast<size_t>(ehEntries) * 12u > bufLen) return false;

                if (ehDepth == 0) {
                    for (uint16_t e = 0; e < ehEntries; ++e) {
                        const uint8_t* ex = ehBuf + 12 + static_cast<size_t>(e) * 12;
                        uint32_t eeBlock  = RdU32LE(ex + 0);
                        uint16_t eeRawLen = RdU16LE(ex + 4);
                        bool uninit       = (eeRawLen > 32768u);
                        uint32_t eeLen    = uninit ? (eeRawLen - 32768u) : eeRawLen;
                        uint64_t eeStart  = (static_cast<uint64_t>(RdU16LE(ex + 6)) << 32) | RdU32LE(ex + 8);
                        if (eeLen == 0 || uninit) continue; // Sparse/uninitialized run already zero-filled
                        uint64_t logOff = static_cast<uint64_t>(eeBlock) * blockSize;
                        if (logOff >= fSize) continue;
                        uint64_t runBytes = std::min<uint64_t>(static_cast<uint64_t>(eeLen) * blockSize, fSize - logOff);
                        uint64_t physOff  = partBase + eeStart * blockSize;
                        if (eeStart == 0 || physOff + runBytes > partBase + partBytes || physOff + runBytes > reader.virtualSize) {
                            return false;
                        }
                        if (!reader.readBytes(physOff, outData.data() + static_cast<size_t>(logOff), static_cast<size_t>(runBytes))) {
                            return false;
                        }
                    }
                    return true;
                } else {
                    for (uint16_t e = 0; e < ehEntries; ++e) {
                        const uint8_t* ix = ehBuf + 12 + static_cast<size_t>(e) * 12;
                        uint64_t leafBlk  = (static_cast<uint64_t>(RdU16LE(ix + 8)) << 32) | RdU32LE(ix + 4);
                        uint64_t physOff  = partBase + leafBlk * blockSize;
                        if (leafBlk == 0 || physOff + blockSize > partBase + partBytes || physOff + blockSize > reader.virtualSize) {
                            return false;
                        }
                        std::vector<uint8_t> nodeBlk(blockSize);
                        if (!reader.readBytes(physOff, nodeBlk.data(), blockSize)) return false;
                        if (!self(self, nodeBlk.data(), blockSize, depthGuard + 1)) return false;
                    }
                    return true;
                }
            };
            return walkExtentNode(walkExtentNode, ino.data() + 40, 60, 0);
        }

        // Legacy ext2/ext3 direct + single/double/triple indirect block pointers
        uint64_t curLogOff = 0;
        uint32_t ptrsPerBlk = blockSize / 4u;
        auto readPhysBlock = [&](uint32_t blkId) -> bool {
            if (curLogOff >= fSize) return true;
            size_t toCopy = static_cast<size_t>(std::min<uint64_t>(blockSize, fSize - curLogOff));
            if (blkId != 0) {
                uint64_t physOff = partBase + static_cast<uint64_t>(blkId) * blockSize;
                if (physOff + toCopy > partBase + partBytes || physOff + toCopy > reader.virtualSize) return false;
                if (!reader.readBytes(physOff, outData.data() + static_cast<size_t>(curLogOff), toCopy)) return false;
            }
            curLogOff += toCopy;
            return true;
        };

        auto readIndirect = [&](auto& self, uint32_t indBlk, int level) -> bool {
            if (curLogOff >= fSize) return true;
            if (indBlk == 0) {
                uint64_t skipBlks = 1;
                for (int l = 0; l < level; ++l) skipBlks *= ptrsPerBlk;
                curLogOff = std::min<uint64_t>(fSize, curLogOff + skipBlks * blockSize);
                return true;
            }
            uint64_t physOff = partBase + static_cast<uint64_t>(indBlk) * blockSize;
            if (physOff + blockSize > partBase + partBytes || physOff + blockSize > reader.virtualSize) return false;
            std::vector<uint8_t> ibuf(blockSize);
            if (!reader.readBytes(physOff, ibuf.data(), blockSize)) return false;
            for (uint32_t i = 0; i < ptrsPerBlk && curLogOff < fSize; ++i) {
                uint32_t childBlk = RdU32LE(ibuf.data() + i * 4u);
                if (level == 1) {
                    if (!readPhysBlock(childBlk)) return false;
                } else {
                    if (!self(self, childBlk, level - 1)) return false;
                }
            }
            return true;
        };

        for (int b = 0; b < 12 && curLogOff < fSize; ++b) {
            if (!readPhysBlock(RdU32LE(ino.data() + 40 + b * 4))) return false;
        }
        if (curLogOff < fSize && !readIndirect(readIndirect, RdU32LE(ino.data() + 40 + 12 * 4), 1)) return false;
        if (curLogOff < fSize && !readIndirect(readIndirect, RdU32LE(ino.data() + 40 + 13 * 4), 2)) return false;
        if (curLogOff < fSize && !readIndirect(readIndirect, RdU32LE(ino.data() + 40 + 14 * 4), 3)) return false;
        return true;
    };

    auto isDirEmptyLostAndFound = [&](const std::vector<uint8_t>& dirIno) -> bool {
        std::vector<uint8_t> dBytes;
        if (!readInodeData(dirIno, 256 * 1024, dBytes)) return true;
        size_t dp = 0;
        while (dp + 8 <= dBytes.size()) {
            uint32_t dIno = RdU32LE(dBytes.data() + dp);
            uint16_t rLen = RdU16LE(dBytes.data() + dp + 4);
            uint8_t  nLen = dBytes[dp + 6];
            if (rLen < 8 || dp + rLen > dBytes.size()) break;
            if (dIno != 0 && nLen > 0 && dp + 8 + nLen <= dBytes.size()) {
                std::string_view nm(reinterpret_cast<const char*>(dBytes.data() + dp + 8), nLen);
                if (nm != "." && nm != "..") return false;
            }
            dp += rLen;
        }
        return true;
    };

    std::vector<uint8_t> rootIno;
    if (!readExtInode(2u, rootIno) || (RdU16LE(rootIno.data()) & 0xF000u) != 0x4000u) return false;

    std::set<uint32_t> visitedDirs;
    size_t addedEntries = 0;
    bool extractedFound = false;

    auto walkDir = [&](auto& self, uint32_t dirInoNum, const std::vector<uint8_t>& dirIno, const std::wstring& curPrefix, int depth) -> bool {
        if (depth > 16 || outInfo.entries.size() >= 50000) return true;
        if (!visitedDirs.insert(dirInoNum).second) return true;

        std::vector<uint8_t> dirBytes;
        if (!readInodeData(dirIno, 16 * 1024 * 1024, dirBytes)) return (depth > 0);

        size_t dp = 0;
        while (dp + 8 <= dirBytes.size() && outInfo.entries.size() < 50000) {
            uint32_t dIno = RdU32LE(dirBytes.data() + dp);
            uint16_t rLen = RdU16LE(dirBytes.data() + dp + 4);
            uint8_t  nLen = dirBytes[dp + 6];
            uint8_t  fTyp = hasFileType ? dirBytes[dp + 7] : 0u;
            if (rLen < 8 || dp + rLen > dirBytes.size()) break;

            if (dIno != 0 && nLen > 0 && dp + 8 + nLen <= dirBytes.size()) {
                std::string nm(reinterpret_cast<const char*>(dirBytes.data() + dp + 8), nLen);
                if (nm != "." && nm != ".." && nm.find('/') == std::string::npos && nm.find('\\') == std::string::npos) {
                    std::vector<uint8_t> childIno;
                    if (readExtInode(dIno, childIno)) {
                        uint16_t cMode = RdU16LE(childIno.data() + 0);
                        uint16_t typeBits = cMode & 0xF000u;
                        bool isDir = (typeBits == 0x4000u) || (typeBits == 0 && fTyp == 2u);
                        bool isReg = (typeBits == 0x8000u) || (typeBits == 0xA000u) || (typeBits == 0 && (fTyp == 1u || fTyp == 7u));

                        if (isDir && depth == 0 && nm == "lost+found" && isDirEmptyLostAndFound(childIno)) {
                            dp += rLen;
                            continue;
                        }

                        if (isDir || isReg) {
                            std::wstring wnm = PathUtils::Utf8ToWide(nm);
                            std::wstring full = ArchivePath::Normalize(curPrefix.empty() ? wnm : (curPrefix + L"/" + wnm));

                            if (extractTargetNorm && outExtracted) {
                                if (!isDir && PathEqualsCI(full, *extractTargetNorm)) {
                                    if (!readInodeData(childIno, 256ULL * 1024 * 1024, *outExtracted)) return false;
                                    extractedFound = true;
                                    return true;
                                }
                            } else {
                                ArchiveItemEntry item;
                                item.pathInArchive = full;
                                item.name = ArchivePath::BaseName(full);
                                item.isDirectory = isDir;
                                item.uncompressedSize = isDir ? 0 : getInodeSize(childIno);
                                item.compressedSize = item.uncompressedSize;
                                item.compressionMethod = 1;
                                UnixTimeU32ToDos(RdU32LE(childIno.data() + 16), item.dosDate, item.dosTime);
                                outInfo.entries.push_back(std::move(item));
                                ++addedEntries;
                            }

                            if (isDir) {
                                if (!self(self, dIno, childIno, full, depth + 1)) return false;
                                if (extractedFound) return true;
                            }
                        }
                    }
                }
            }
            dp += rLen;
        }
        return true;
    };

    if (!walkDir(walkDir, 2u, rootIno, prefixDir, 0)) return false;
    if (extractTargetNorm) return extractedFound;
    return addedEntries > 0;
}

// ----------------------------------------------------------------------------
// Apple HFS+ / HFSX Catalog B-Tree Inspector & Data-Fork Extent Extractor
// ----------------------------------------------------------------------------
struct HfsExtentDesc {
    uint32_t startBlock = 0;
    uint32_t blockCount = 0;
};

struct HfsForkData {
    uint64_t logicalSize = 0;
    uint32_t clumpSize   = 0;
    uint32_t totalBlocks = 0;
    HfsExtentDesc extents[8] = {};
};

inline HfsForkData ParseHfsForkData(const uint8_t* p) {
    HfsForkData f;
    f.logicalSize = RdU64BE(p + 0);
    f.clumpSize   = RdU32BE(p + 8);
    f.totalBlocks = RdU32BE(p + 12);
    for (int i = 0; i < 8; ++i) {
        f.extents[i].startBlock = RdU32BE(p + 16 + i * 8);
        f.extents[i].blockCount = RdU32BE(p + 20 + i * 8);
    }
    return f;
}

inline bool ReadHfsForkBytes(
    const VirtualDiskReader& reader,
    uint64_t volBase,
    uint64_t volBytes,
    uint32_t blockSize,
    const HfsForkData& forkData,
    uint64_t logicalOff,
    uint8_t* dst,
    size_t len)
{
    if (len == 0) return true;
    if (logicalOff > forkData.logicalSize || len > forkData.logicalSize - logicalOff) return false;

    uint64_t curLogBase = 0;
    size_t written = 0;
    for (int i = 0; i < 8 && written < len; ++i) {
        const auto& ex = forkData.extents[i];
        if (ex.blockCount == 0) continue;
        uint64_t exBytes = static_cast<uint64_t>(ex.blockCount) * blockSize;
        uint64_t reqOff  = logicalOff + written;
        if (reqOff >= curLogBase && reqOff < curLogBase + exBytes) {
            uint64_t relInEx = reqOff - curLogBase;
            size_t chunk = static_cast<size_t>(std::min<uint64_t>(len - written, exBytes - relInEx));
            uint64_t physOff = volBase + static_cast<uint64_t>(ex.startBlock) * blockSize + relInEx;
            if (physOff + chunk > volBase + volBytes || physOff + chunk > reader.virtualSize) return false;
            if (!reader.readBytes(physOff, dst + written, chunk)) return false;
            written += chunk;
        }
        curLogBase += exBytes;
    }
    return written == len;
}

inline void HfsTimeU32ToDos(uint32_t macSecs1904, uint16_t& outDate, uint16_t& outTime) {
    outDate = 0;
    outTime = 0;
    // Mac epoch 1904-01-01 -> Unix epoch 1970-01-01 delta is 2082844800 seconds
    if (macSecs1904 <= 2082844800u) return;
    UnixTimeU32ToDos(macSecs1904 - 2082844800u, outDate, outTime);
}

inline bool InspectOrExtractHfsPlusOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (partBytes < 2048 || partBase > reader.virtualSize || partBytes > reader.virtualSize - partBase) return false;

    uint64_t volBase  = partBase;
    uint64_t volBytes = partBytes;
    uint8_t vh[512]   = {};
    if (!reader.readBytes(volBase + 1024, vh, 512)) return false;

    // Optional HFS Standard ("BD") wrapper embedding an HFS+ ("H+"/"HX") volume
    if (vh[0] == 'B' && vh[1] == 'D' && vh[124] == 'H' && (vh[125] == '+' || vh[125] == 'X')) {
        uint32_t alBlkSiz  = RdU32BE(vh + 20);
        uint16_t alBlSt    = RdU16BE(vh + 28);
        uint16_t embStart  = RdU16BE(vh + 126);
        uint16_t embCount  = RdU16BE(vh + 128);
        uint64_t embOffset = static_cast<uint64_t>(alBlSt) * 512ULL + static_cast<uint64_t>(embStart) * alBlkSiz;
        uint64_t embLength = static_cast<uint64_t>(embCount) * alBlkSiz;
        if (alBlkSiz >= 512 && embOffset + 2048 <= partBytes && embLength >= 2048) {
            volBase  = partBase + embOffset;
            volBytes = std::min<uint64_t>(embLength, partBytes - embOffset);
            if (!reader.readBytes(volBase + 1024, vh, 512)) return false;
        }
    }

    if (vh[0] != 'H' || (vh[1] != '+' && vh[1] != 'X')) return false;

    uint32_t blockSize   = RdU32BE(vh + 40);
    uint32_t totalBlocks = RdU32BE(vh + 44);
    if (blockSize < 512 || blockSize > 1048576 || (blockSize & (blockSize - 1u)) != 0 || totalBlocks == 0) {
        return false;
    }

    // Probe Catalog File ForkData at Apple TN1150 offset +272 (and fallback +208)
    HfsForkData catalogFork;
    uint16_t nodeSize      = 0;
    uint32_t firstLeafNode = 0;
    uint32_t totalNodes    = 0;
    auto probeCatalogFork = [&](size_t vhOff) -> bool {
        if (vhOff + 80 > sizeof(vh)) return false;
        HfsForkData cand = ParseHfsForkData(vh + vhOff);
        if (cand.logicalSize < 512) return false;
        uint8_t node0[120] = {};
        if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, cand, 0, node0, sizeof(node0))) return false;
        // BTNodeDescriptor: kind == 1 (kBTHeaderNode) at +8
        if (static_cast<int8_t>(node0[8]) != 1) return false;
        // BTHeaderRec starts at node0 + 14
        uint16_t ns  = RdU16BE(node0 + 14 + 18);
        uint32_t tn  = RdU32BE(node0 + 14 + 22);
        uint32_t fln = RdU32BE(node0 + 14 + 10);
        if (ns < 512 || ns > 32768 || (ns & (ns - 1u)) != 0) return false;
        if (tn == 0 || static_cast<uint64_t>(tn) * ns > cand.logicalSize) return false;
        if (fln >= tn) return false;
        catalogFork   = cand;
        nodeSize      = ns;
        firstLeafNode = fln;
        totalNodes    = tn;
        return true;
    };

    if (!probeCatalogFork(272) && !probeCatalogFork(208)) return false;
    if (firstLeafNode == 0) return false;

    struct HfsCatFolder {
        uint32_t folderId = 0;
        uint32_t parentId = 0;
        std::wstring name;
        uint32_t modDate  = 0;
    };
    struct HfsCatFile {
        uint32_t fileId   = 0;
        uint32_t parentId = 0;
        std::wstring name;
        uint32_t modDate  = 0;
        HfsForkData dataFork;
    };

    std::unordered_map<uint32_t, HfsCatFolder> foldersMap;
    std::vector<HfsCatFolder> foldersOrder;
    std::vector<HfsCatFile>   filesList;

    std::set<uint32_t> visitedNodes;
    std::vector<uint8_t> nodeBuf(nodeSize);
    uint32_t curNode = firstLeafNode;

    while (curNode != 0 && curNode < totalNodes && visitedNodes.size() < 16384) {
        if (!visitedNodes.insert(curNode).second) return false; // Cycle in fLink
        if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, catalogFork,
                              static_cast<uint64_t>(curNode) * nodeSize, nodeBuf.data(), nodeSize))
        {
            return false;
        }
        if (static_cast<int8_t>(nodeBuf[8]) != -1) return false; // Must be kBTLeafNode (-1 / 0xFF)

        uint16_t numRecs = RdU16BE(nodeBuf.data() + 10);
        if (numRecs == 0 && nodeBuf[9] == 0) {
            // Tolerance if height byte was omitted in non-standard writer (numRecords at +8..9 would conflict with kind, so keep strict +10)
        }
        if (14u + (static_cast<uint32_t>(numRecs) + 1u) * 2u > nodeSize) return false;

        size_t offTblFloor = nodeSize - static_cast<size_t>(numRecs + 1u) * 2u;
        for (uint16_t r = 0; r < numRecs; ++r) {
            uint16_t recOff  = RdU16BE(nodeBuf.data() + nodeSize - 2u * (r + 1u));
            uint16_t nextOff = RdU16BE(nodeBuf.data() + nodeSize - 2u * (r + 2u));
            if (recOff < 14u || nextOff < recOff || nextOff > offTblFloor + 2u || recOff + 8u > nodeSize) continue;

            uint16_t keyLen = RdU16BE(nodeBuf.data() + recOff);
            if (keyLen < 6u || static_cast<size_t>(recOff) + 2u + keyLen > nextOff) continue;

            uint32_t parentCnid = RdU32BE(nodeBuf.data() + recOff + 2);
            uint16_t uniLen     = RdU16BE(nodeBuf.data() + recOff + 6);
            if (6u + static_cast<uint32_t>(uniLen) * 2u > keyLen) continue;

            size_t valOff = static_cast<size_t>(recOff) + 2u + keyLen + (keyLen & 1u);
            if (valOff + 2u > nextOff) continue;
            int16_t recType = static_cast<int16_t>(RdU16BE(nodeBuf.data() + valOff));
            if (recType != 0x0001 && recType != 0x0002) continue; // Skip thread records 0x0003 / 0x0004

            // Decode HFSUniStr255 UTF-16BE nodeName
            if (uniLen > 0 && RdU16BE(nodeBuf.data() + recOff + 8) == 0) continue; // Skip \0\0\0\0HFS+ Private Data
            std::wstring wname;
            wname.reserve(uniLen);
            for (uint16_t c = 0; c < uniLen; ++c) {
                wchar_t wc = static_cast<wchar_t>(RdU16BE(nodeBuf.data() + recOff + 8 + c * 2u));
                if (wc == 0) break;
                if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
                wname.push_back(wc);
            }

            if (recType == 0x0001) {
                // kHFSPlusFolderRecord (88 bytes; folderID at +8, contentModDate at +16)
                if (valOff + 20u > nextOff) continue;
                uint32_t folderId = RdU32BE(nodeBuf.data() + valOff + 8);
                uint32_t modDate  = RdU32BE(nodeBuf.data() + valOff + 16);
                HfsCatFolder fld{ folderId, parentCnid, wname, modDate };
                foldersMap[folderId] = fld;
                if (folderId != 2u && !wname.empty()) {
                    foldersOrder.push_back(std::move(fld));
                }
            } else if (recType == 0x0002) {
                // kHFSPlusFileRecord (248 bytes; fileID at +8, contentModDate at +16, dataFork at +88..167)
                if (valOff + 168u > nextOff || wname.empty()) continue;
                // Skip HFS+ hard-link private inode metadata files under private dir
                HfsCatFile fl;
                fl.fileId   = RdU32BE(nodeBuf.data() + valOff + 8);
                fl.parentId = parentCnid;
                fl.name     = std::move(wname);
                fl.modDate  = RdU32BE(nodeBuf.data() + valOff + 16);
                fl.dataFork = ParseHfsForkData(nodeBuf.data() + valOff + 88);
                filesList.push_back(std::move(fl));
            }
        }

        curNode = RdU32BE(nodeBuf.data() + 0); // fLink
    }

    auto buildFullPath = [&](uint32_t parentId, const std::wstring& leafName, std::wstring& outFull) -> bool {
        if (leafName.empty() || leafName == L"." || leafName == L"..") return false;
        std::vector<std::wstring> parts;
        parts.push_back(leafName);
        uint32_t cur = parentId;
        int guard = 0;
        while (cur != 2u && cur != 1u) {
            if (++guard > 32) return false;
            auto it = foldersMap.find(cur);
            if (it == foldersMap.end() || it->second.name.empty()) return false;
            parts.push_back(it->second.name);
            if (it->second.parentId == cur) return false;
            cur = it->second.parentId;
        }
        if (cur != 2u) return false; // Must descend from kHFSRootFolderID == 2

        std::wstring joined = prefixDir;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!joined.empty()) joined.push_back(L'/');
            joined += *it;
        }
        outFull = ArchivePath::Normalize(std::move(joined));
        return !outFull.empty();
    };

    if (extractTargetNorm && outExtracted) {
        for (const auto& fl : filesList) {
            std::wstring full;
            if (!buildFullPath(fl.parentId, fl.name, full)) continue;
            if (PathEqualsCI(full, *extractTargetNorm)) {
                if (fl.dataFork.logicalSize > 256ULL * 1024 * 1024) return false;
                outExtracted->resize(static_cast<size_t>(fl.dataFork.logicalSize));
                if (!ReadHfsForkBytes(reader, volBase, volBytes, blockSize, fl.dataFork, 0,
                                      outExtracted->data(), outExtracted->size()))
                {
                    outExtracted->clear();
                    return false;
                }
                return true;
            }
        }
        return false;
    }

    size_t added = 0;
    for (const auto& fld : foldersOrder) {
        if (outInfo.entries.size() >= 50000) break;
        std::wstring full;
        if (!buildFullPath(fld.parentId, fld.name, full)) continue;
        ArchiveItemEntry item;
        item.pathInArchive = full;
        item.name = ArchivePath::BaseName(full);
        item.isDirectory = true;
        HfsTimeU32ToDos(fld.modDate, item.dosDate, item.dosTime);
        outInfo.entries.push_back(std::move(item));
        ++added;
    }

    for (const auto& fl : filesList) {
        if (outInfo.entries.size() >= 50000) break;
        std::wstring full;
        if (!buildFullPath(fl.parentId, fl.name, full)) continue;
        ArchiveItemEntry item;
        item.pathInArchive = full;
        item.name = ArchivePath::BaseName(full);
        item.isDirectory = false;
        item.uncompressedSize = fl.dataFork.logicalSize;
        uint64_t allocBytes = static_cast<uint64_t>(fl.dataFork.totalBlocks) * blockSize;
        item.compressedSize = (allocBytes > 0) ? std::min<uint64_t>(allocBytes, fl.dataFork.logicalSize ? allocBytes : 0) : fl.dataFork.logicalSize;
        item.compressionMethod = 1;
        HfsTimeU32ToDos(fl.modDate, item.dosDate, item.dosTime);
        outInfo.entries.push_back(std::move(item));
        ++added;
    }

    return added > 0;
}

struct DiscoveredPartition {
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    uint8_t  mbrType = 0;
    std::wstring label;
};

inline void DiscoverDiskPartitions(const VirtualDiskReader& reader, std::vector<DiscoveredPartition>& outParts) {
    outParts.clear();
    if (reader.virtualSize < 512) return;

    // 1. Check if LBA 0 is already a superfloppy FAT / NTFS / HFS+ / ext2-4 filesystem
    FatVolumeParams lba0Fat;
    if (ProbeFatVolume(reader, 0, reader.virtualSize, lba0Fat)) {
        outParts.push_back({ 0, reader.virtualSize, 0x06, L"" });
        return;
    }
    uint8_t sec0[512] = {};
    if (!reader.readBytes(0, sec0, 512)) return;
    if (std::memcmp(sec0 + 3, "NTFS    ", 8) == 0) {
        outParts.push_back({ 0, reader.virtualSize, 0x07, L"" });
        return;
    }
    if (reader.virtualSize >= 2048) {
        uint8_t sbProbe[128] = {};
        if (reader.readBytes(1024, sbProbe, sizeof(sbProbe))) {
            bool isRawHfs = (sbProbe[0] == 'H' && (sbProbe[1] == '+' || sbProbe[1] == 'X')) ||
                            (sbProbe[0] == 'B' && sbProbe[1] == 'D' && sbProbe[124] == 'H' && (sbProbe[125] == '+' || sbProbe[125] == 'X'));
            bool isRawExt = (RdU16LE(sbProbe + 56) == 0xEF53u);
            if (isRawHfs || isRawExt) {
                outParts.push_back({ 0, reader.virtualSize, static_cast<uint8_t>(isRawHfs ? 0xAFu : 0x83u), L"" });
                return;
            }
        }
    }

    // 2. Check GPT at LBA 1 ("EFI PART")
    if (reader.virtualSize >= 1024) {
        uint8_t gpt[512] = {};
        if (reader.readBytes(512, gpt, 512) && std::memcmp(gpt, "EFI PART", 8) == 0) {
            uint64_t peLba  = RdU64LE(gpt + 72);
            uint32_t numPe  = std::min<uint32_t>(RdU32LE(gpt + 80), 128);
            uint32_t peSize = RdU32LE(gpt + 84);
            if (peSize >= 128 && peSize <= 512 && peLba * 512ULL + static_cast<uint64_t>(numPe) * peSize <= reader.virtualSize) {
                std::vector<uint8_t> peBuf(static_cast<size_t>(numPe) * peSize);
                if (reader.readBytes(peLba * 512ULL, peBuf.data(), peBuf.size())) {
                    for (uint32_t i = 0; i < numPe; ++i) {
                        const uint8_t* pe = peBuf.data() + i * peSize;
                        bool allZero = true;
                        for (int b = 0; b < 16; ++b) if (pe[b] != 0) { allZero = false; break; }
                        if (allZero) continue;
                        uint64_t firstLba = RdU64LE(pe + 32);
                        uint64_t lastLba  = RdU64LE(pe + 40);
                        if (firstLba == 0 || lastLba < firstLba || firstLba * 512ULL >= reader.virtualSize) continue;
                        std::wstring pname;
                        for (size_t c = 0; c < 36; ++c) {
                            wchar_t wc = static_cast<wchar_t>(RdU16LE(pe + 56 + c * 2));
                            if (wc == 0) break;
                            if (wc == L'/' || wc == L'\\') wc = L'_';
                            pname.push_back(wc);
                        }
                        uint64_t pOff = firstLba * 512ULL;
                        uint64_t pLen = std::min<uint64_t>((lastLba - firstLba + 1ULL) * 512ULL, reader.virtualSize - pOff);
                        outParts.push_back({ pOff, pLen, 0xEE, pname });
                    }
                }
            }
            if (!outParts.empty()) return;
        }
    }

    // 3. Check Apple Partition Map (APM — "PM" signature at sector 1)
    {
        uint32_t apmBlk = 512u;
        if (sec0[0] == 'E' && sec0[1] == 'R') {
            uint16_t sbBlk = RdU16BE(sec0 + 2);
            if (sbBlk >= 512u && sbBlk <= 4096u && (sbBlk & (sbBlk - 1u)) == 0) apmBlk = sbBlk;
        }
        uint8_t apm1[512] = {};
        if (reader.virtualSize >= apmBlk + 512ULL && reader.readBytes(apmBlk, apm1, 512) &&
            apm1[0] == 'P' && apm1[1] == 'M')
        {
            uint32_t mapEntries = std::min<uint32_t>(RdU32BE(apm1 + 4), 64u);
            for (uint32_t m = 1; m <= mapEntries; ++m) {
                uint64_t entOff = static_cast<uint64_t>(m) * apmBlk;
                if (entOff + 512ULL > reader.virtualSize) break;
                uint8_t pm[512] = {};
                if (!reader.readBytes(entOff, pm, 512) || pm[0] != 'P' || pm[1] != 'M') break;
                uint32_t pBlkStart = RdU32BE(pm + 8);
                uint32_t pBlkCnt   = RdU32BE(pm + 12);
                if (pBlkCnt == 0) continue;

                std::string pName(reinterpret_cast<const char*>(pm + 16), strnlen(reinterpret_cast<const char*>(pm + 16), 32));
                std::string pType(reinterpret_cast<const char*>(pm + 48), strnlen(reinterpret_cast<const char*>(pm + 48), 32));
                if (_stricmp(pType.c_str(), "Apple_partition_map") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Free") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Extra") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Patches") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Scratch") == 0 ||
                    _stricmp(pType.c_str(), "Apple_FWDriver") == 0 ||
                    _stricmp(pType.c_str(), "Apple_Loader") == 0 ||
                    _strnicmp(pType.c_str(), "Apple_Driver", 12) == 0)
                {
                    continue;
                }

                uint64_t pOff = static_cast<uint64_t>(pBlkStart) * apmBlk;
                if (pOff >= reader.virtualSize) continue;
                uint64_t pLen = std::min<uint64_t>(static_cast<uint64_t>(pBlkCnt) * apmBlk, reader.virtualSize - pOff);
                std::wstring lbl = PathUtils::Utf8ToWide(pName);
                for (wchar_t& ch : lbl) if (ch == L'/' || ch == L'\\' || ch == L':') ch = L'_';
                outParts.push_back({ pOff, pLen, 0xAF, lbl });
            }
            if (!outParts.empty()) return;
        }
    }

    // 4. Check MBR at LBA 0 (bytes 446..509 + 0x55AA)
    if (sec0[510] == 0x55 && sec0[511] == 0xAA) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t* pe = sec0 + 446 + i * 16;
            uint8_t status = pe[0];
            uint8_t ptype  = pe[4];
            uint32_t firstLba = RdU32LE(pe + 8);
            uint32_t numSecs  = RdU32LE(pe + 12);
            if ((status != 0x00 && status != 0x80) || ptype == 0 || ptype == 0xEE || firstLba == 0 || numSecs == 0) continue;
            uint64_t pOff = static_cast<uint64_t>(firstLba) * 512ULL;
            if (pOff >= reader.virtualSize) continue;
            uint64_t pLen = std::min<uint64_t>(static_cast<uint64_t>(numSecs) * 512ULL, reader.virtualSize - pOff);
            wchar_t lbl[64] = {};
            swprintf_s(lbl, L"Partition%d_Type%02X", i, ptype);
            outParts.push_back({ pOff, pLen, ptype, lbl });
        }
        if (!outParts.empty()) return;
    }

    // 5. Superfloppy fallback (ISO9660 / ext4 / HFS+ at offset 0)
    outParts.push_back({ 0, reader.virtualSize, 0, L"" });
}

inline bool InspectOrExtractVirtualDiskReader(
    const VirtualDiskReader& reader,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    std::vector<DiscoveredPartition> parts;
    DiscoverDiskPartitions(reader, parts);
    bool multiPart = (parts.size() > 1);

    if (outInfo) {
        outInfo->formatName = reader.formatName;
    }

    for (size_t idx = 0; idx < parts.size(); ++idx) {
        const auto& p = parts[idx];
        std::wstring prefix = multiPart
            ? (p.label.empty() ? (L"Partition" + std::to_wstring(idx)) : p.label)
            : L"";

        auto matchTargetForPart = [&](std::wstring& outRelTarget) -> bool {
            if (!extractTargetNorm) return false;
            outRelTarget = *extractTargetNorm;
            if (!prefix.empty()) return true;
            if (_wcsnicmp(outRelTarget.c_str(), L"Partition", 9) == 0) {
                size_t sl = outRelTarget.find(L'/');
                if (sl != std::wstring::npos) outRelTarget = outRelTarget.substr(sl + 1);
            }
            return true;
        };

        std::wstring targetRel;
        const std::wstring* targetPtr = matchTargetForPart(targetRel) ? &targetRel : nullptr;

        // A. Try FAT12 / FAT16 / FAT32
        FatVolumeParams fatVol;
        if (ProbeFatVolume(reader, p.byteOffset, p.byteLength, fatVol)) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractFatOnReader(reader, fatVol, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // B. Try ISO-9660
        {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractIsoOnReader(reader, p.byteOffset, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // C. Try NTFS
        {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractNtfsOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
                continue;
            }
        }

        // Check +1024 signatures for Linux ext2/3/4 and Apple HFS+/HFSX
        bool hasExtMagic = false;
        bool hasHfsMagic = false;
        if (p.byteLength >= 2048 && p.byteOffset + 1152 <= reader.virtualSize) {
            uint8_t sigBuf[128] = {};
            if (reader.readBytes(p.byteOffset + 1024, sigBuf, sizeof(sigBuf))) {
                hasExtMagic = (RdU16LE(sigBuf + 56) == 0xEF53u);
                hasHfsMagic = (sigBuf[0] == 'H' && (sigBuf[1] == '+' || sigBuf[1] == 'X')) ||
                              (sigBuf[0] == 'B' && sigBuf[1] == 'D' && sigBuf[124] == 'H' && (sigBuf[125] == '+' || sigBuf[125] == 'X'));
            }
        }

        // D. Try Linux ext2/ext3/ext4
        if (hasExtMagic) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractExtFsOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
            }
            continue; // Never emit fake fallback items for corrupt ext partitions
        }

        // E. Try Apple HFS+ / HFSX Catalog B-Tree
        if (hasHfsMagic) {
            ArchiveSummaryInfo tmp;
            ArchiveSummaryInfo& dstInfo = outInfo ? *outInfo : tmp;
            if (InspectOrExtractHfsPlusOnReader(reader, p.byteOffset, p.byteLength, prefix, dstInfo, targetPtr, outExtracted)) {
                if (extractTargetNorm) return true;
            }
            continue; // Never emit fake HFSPlus_Volume_ fallback items
        }

        // F. Raw partition slice (only for real virtual disk containers like fresh VHD/VHDX/VMDK/VDI/QCOW2)
        if (!reader.allowRawSliceFallback) continue;

        std::wstring rawEntryName = !p.label.empty()
            ? (p.label + L"_" + std::to_wstring(p.byteLength / 512ULL) + L"sec.img")
            : (L"RawDisk_" + std::to_wstring(reader.virtualSize / 512ULL) + L"sec.img");

        if (extractTargetNorm && outExtracted) {
            if (PathEqualsCI(rawEntryName, *extractTargetNorm) && p.byteLength <= 128 * 1024 * 1024) {
                outExtracted->resize(static_cast<size_t>(p.byteLength));
                return reader.readBytes(p.byteOffset, outExtracted->data(), outExtracted->size());
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = rawEntryName;
            item.name = ArchivePath::BaseName(rawEntryName);
            item.uncompressedSize = p.byteLength;
            item.compressedSize   = std::min<uint64_t>(p.byteLength, size);
            item.rawDataOffset    = p.byteOffset;
            item.compressionMethod = reader.directRawBuffer ? 0 : 1;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (outInfo) {
        outInfo->valid = !outInfo->entries.empty();
        return outInfo->valid;
    }
    return false;
}

} // namespace DetailArc100

namespace DetailArc200 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool DecompressSqfsChunk(
    const uint8_t* src,
    size_t compLen,
    size_t maxOut,
    uint16_t compId,
    std::vector<uint8_t>& out)
{
    if (compLen == 0 || maxOut > 256 * 1024 * 1024) return false;
    if (compId == 1) { // Zlib / Deflate
        return ZipDeflate::InflateZlib(src, compLen, out, maxOut + 64) ||
               ZipDeflate::InflateRaw(src, compLen, out, maxOut + 64);
    }
    if (compId == 2) { // LZMA1
        if (compLen >= 13) {
            uint64_t hdrUncomp = RdU64LE(src + 5);
            bool hasSz = (hdrUncomp != UINT64_MAX);
            if (!hasSz || hdrUncomp <= maxOut) {
                if (StreamCodecs::DecompressLzma1(src[0], RdU32LE(src + 1), src + 13, compLen - 13, out, hdrUncomp, hasSz, maxOut + 64)) {
                    return true;
                }
            }
        }
        if (compLen >= 5) {
            return StreamCodecs::DecompressLzma1(src[0], RdU32LE(src + 1), src + 5, compLen - 5, out, maxOut, false, maxOut + 64);
        }
        return false;
    }
    if (compId == 4) { // XZ
        return StreamCodecs::DecompressXzStream(src, compLen, out, maxOut + 64);
    }
    if (compId == 5) { // LZ4
        if (compLen >= 4 && RdU32LE(src) == 0x184D2204u) {
            return StreamCodecs::DecompressLz4Frame(src, compLen, out, maxOut + 64);
        }
        out.clear(); // no stored decoded size (e.g. SquashFS LZ4): bounded by maxOut
        return ::Explorer2::Lz4::DecodeBlockAppend(src, compLen, out, 0, maxOut);
    }
    if (compId == 6) { // Zstd
        return StreamCodecs::DecompressZstdFrame(src, compLen, out, maxOut + 64);
    }
    return false;
}

struct LhaMsbBitReader {
    const uint8_t* src = nullptr;
    size_t srcLen = 0;
    size_t bytePos = 0;
    uint64_t bitBuf = 0;
    int bitCount = 0;
    size_t totalBitsDropped = 0;
    bool overread = false;

    LhaMsbBitReader(const uint8_t* s, size_t n) : src(s), srcLen(n) {}

    void Fill() {
        while (bitCount <= 56) {
            uint8_t b = (bytePos < srcLen) ? src[bytePos++] : 0u;
            bitBuf |= static_cast<uint64_t>(b) << (56 - bitCount);
            bitCount += 8;
        }
    }

    uint32_t PeekBits(uint32_t n) {
        if (n == 0u) return 0u;
        if (bitCount < static_cast<int>(n)) Fill();
        return static_cast<uint32_t>(bitBuf >> (64u - n));
    }

    void DropBits(uint32_t n) {
        if (n == 0u) return;
        if (bitCount < static_cast<int>(n)) Fill();
        bitBuf <<= n;
        bitCount -= static_cast<int>(n);
        totalBitsDropped += n;
        if (totalBitsDropped > srcLen * 8u) {
            overread = true;
        }
    }

    uint32_t ReadBits(uint32_t n) {
        if (n == 0u) return 0u;
        uint32_t v = PeekBits(n);
        DropBits(n);
        return v;
    }
};

struct LhaMsbHuffTable {
    bool isSingle = false;
    uint16_t singleSym = 0;
    uint32_t maxLen = 0;
    std::vector<uint32_t> table;

    void SetSingle(uint16_t sym) {
        isSingle = true;
        singleSym = sym;
        maxLen = 0;
        table.clear();
    }

    bool Build(const uint8_t* lengths, size_t alphaSz) {
        uint32_t cnt[17] = {};
        uint32_t maxL = 0;
        for (size_t s = 0; s < alphaSz; ++s) {
            uint8_t l = lengths[s];
            if (l > 16u) return false;
            if (l > 0u) {
                ++cnt[l];
                if (l > maxL) maxL = l;
            }
        }
        if (maxL == 0u) return false;

        uint32_t weightSum = 0;
        for (uint32_t l = 1u; l <= 16u; ++l) {
            weightSum += cnt[l] << (16u - l);
        }
        if (weightSum == 0u || weightSum > 65536u) return false;

        isSingle = false;
        maxLen = maxL;
        table.assign(size_t(1) << maxL, 0xFFFFFFFFu);

        uint32_t firstCode[17] = {};
        uint32_t code = 0;
        for (uint32_t l = 1u; l <= maxL; ++l) {
            code = (code + cnt[l - 1u]) << 1u;
            firstCode[l] = code;
        }

        for (size_t s = 0; s < alphaSz; ++s) {
            uint32_t l = lengths[s];
            if (l == 0u) continue;
            uint32_t c = firstCode[l]++;
            uint32_t shift = maxL - l;
            size_t startIdx = static_cast<size_t>(c) << shift;
            size_t span = size_t(1) << shift;
            if (startIdx + span > table.size()) return false;
            uint32_t packed = static_cast<uint32_t>(s) | (l << 16u);
            for (size_t k = 0; k < span; ++k) {
                table[startIdx + k] = packed;
            }
        }
        return true;
    }

    bool Decode(LhaMsbBitReader& br, uint16_t& outSym) const {
        if (isSingle) {
            if (br.overread) return false;
            outSym = singleSym;
            return true;
        }
        if (maxLen == 0u || table.empty()) return false;
        uint32_t entry = table[br.PeekBits(maxLen)];
        if (entry == 0xFFFFFFFFu) return false;
        br.DropBits(entry >> 16u);
        if (br.overread) return false;
        outSym = static_cast<uint16_t>(entry & 0xFFFFu);
        return true;
    }
};

inline bool ReadLhaPtLen(LhaMsbBitReader& br, uint32_t nn, uint32_t nbit, int iSpecial, LhaMsbHuffTable& outTbl) {
    uint32_t n = br.ReadBits(nbit);
    if (br.overread) return false;
    if (n == 0u) {
        uint32_t c = br.ReadBits(nbit);
        if (br.overread || c >= nn) return false;
        outTbl.SetSingle(static_cast<uint16_t>(c));
        return true;
    }
    if (n > nn || nn > 32u) return false;

    uint8_t ptLen[32] = {};
    uint32_t i = 0;
    while (i < n) {
        uint32_t c = br.PeekBits(3u);
        if (c == 7u) {
            uint32_t ext = br.PeekBits(16u);
            uint32_t mask = 1u << 12u;
            while ((ext & mask) != 0u) {
                ++c;
                mask >>= 1u;
                if (c > 16u) return false;
            }
            br.DropBits(c - 3u);
        } else {
            br.DropBits(3u);
        }
        if (br.overread) return false;
        ptLen[i++] = static_cast<uint8_t>(c);
        if (static_cast<int>(i) == iSpecial) {
            uint32_t skip = br.ReadBits(2u);
            if (br.overread) return false;
            while (skip-- > 0u && i < nn) {
                ptLen[i++] = 0u;
            }
        }
    }
    return outTbl.Build(ptLen, nn);
}

inline bool ReadLhaCLen(LhaMsbBitReader& br, const LhaMsbHuffTable& ptTbl, LhaMsbHuffTable& outCTbl) {
    constexpr uint32_t kNC = 510u;
    uint32_t n = br.ReadBits(9u);
    if (br.overread) return false;
    if (n == 0u) {
        uint32_t c = br.ReadBits(9u);
        if (br.overread || c >= kNC) return false;
        outCTbl.SetSingle(static_cast<uint16_t>(c));
        return true;
    }
    if (n > kNC) return false;

    uint8_t cLen[kNC] = {};
    uint32_t i = 0;
    while (i < n) {
        uint16_t c = 0;
        if (!ptTbl.Decode(br, c)) return false;
        if (c <= 2u) {
            uint32_t run = 1u;
            if (c == 1u) run = br.ReadBits(4u) + 3u;
            else if (c == 2u) run = br.ReadBits(9u) + 20u;
            if (br.overread) return false;
            while (run-- > 0u && i < kNC) {
                cLen[i++] = 0u;
            }
        } else {
            cLen[i++] = static_cast<uint8_t>(c - 2u);
        }
    }
    return outCTbl.Build(cLen, kNC);
}

inline bool DecodeLhaArjBlockHuff(
    const uint8_t* src,
    size_t compSize,
    uint64_t uncompSize,
    uint32_t np,
    uint32_t pbit,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (uncompSize == 0) return true;
    if (!src || compSize == 0 || uncompSize > 256ULL * 1024 * 1024) return false;

    outBytes.reserve(static_cast<size_t>(std::min<uint64_t>(uncompSize, static_cast<uint64_t>(compSize) * 16ULL + 8192ULL)));
    LhaMsbBitReader br(src, compSize);
    LhaMsbHuffTable ptTbl, cTbl, pTbl;
    uint32_t blockRemaining = 0;

    while (outBytes.size() < uncompSize) {
        if (blockRemaining == 0u) {
            blockRemaining = br.ReadBits(16u);
            if (br.overread || blockRemaining == 0u) {
                outBytes.clear();
                return false;
            }
            if (!ReadLhaPtLen(br, 19u, 5u, 3, ptTbl) ||
                !ReadLhaCLen(br, ptTbl, cTbl) ||
                !ReadLhaPtLen(br, np, pbit, -1, pTbl))
            {
                outBytes.clear();
                return false;
            }
        }
        --blockRemaining;

        uint16_t sym = 0;
        if (!cTbl.Decode(br, sym)) {
            outBytes.clear();
            return false;
        }
        if (sym < 256u) {
            outBytes.push_back(static_cast<uint8_t>(sym));
        } else {
            const size_t matchLen = static_cast<size_t>(sym - 256u) + 3u;
            uint16_t p = 0;
            if (!pTbl.Decode(br, p) || p > 16u) {
                outBytes.clear();
                return false;
            }
            uint32_t dist0 = 0;
            if (p > 0u) {
                dist0 = (1u << (p - 1u)) | br.ReadBits(p - 1u);
                if (br.overread) {
                    outBytes.clear();
                    return false;
                }
            }
            const size_t dist = static_cast<size_t>(dist0) + 1u;
            for (size_t k = 0; k < matchLen && outBytes.size() < uncompSize; ++k) {
                const uint8_t ch = (outBytes.size() < dist) ? 0x20u : outBytes[outBytes.size() - dist];
                outBytes.push_back(ch);
            }
        }
    }
    return outBytes.size() == uncompSize;
}

} // namespace DetailArc200

} // namespace Explorer2::Formats
