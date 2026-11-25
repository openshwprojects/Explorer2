#pragma once
#include "../../ReaderTextUtils.h"
// AndroidSparseImg.h — Zero-dependency C++20 Android Sparse Filesystem Image (.simg -> ext2/3/4 & EROFS) reader.

#include "../../ByteIO.h"
#include "../../ArchiveTypes.h"
#include "../../ZipDeflate.h"
#include "../../Crc32.h"
#include "../../StreamCodecs.h"
#include "../../PathUtils.h"
#include "../common/ArchiveFormatCommon.h"
#include "../common/VirtualDiskCommon.h"
#include "LinuxErofs.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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


namespace Explorer2::Formats {

namespace DetailAndroidSimg {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool EndsWithCI(const std::wstring& s, const wchar_t* suffix) {
    return ::Explorer2::ReaderText::EndsWithWideCI(s, suffix);
}
inline std::string AsciiLower(std::string_view sv) { return ::Explorer2::ReaderText::ToLowerAscii(sv); }
inline std::string_view TrimAsciiSV(std::string_view sv) { return ::Explorer2::ReaderText::TrimCrTabSpace(sv); }
using ::Explorer2::ReaderText::CalendarToDosDateTime;

struct SimgExtent {
    uint64_t vStart = 0;
    uint64_t vEnd   = 0;
    uint16_t kind   = 0;   // 0xCAC1 = RAW, 0xCAC2 = FILL, 0xCAC3 = DONT_CARE
    uint32_t fillLe = 0;
    size_t   rawOff = 0;
};

inline bool BuildAndroidSimgReader(
    const uint8_t* data,
    size_t size,
    DetailDiskAndGameArc::VirtualDiskReader& outReader)
{
    if (!data || size < 40u) return false;
    if (RdU32LE(data + 0) != 0xED26FF3Au) return false;

    const uint16_t majorVer    = RdU16LE(data + 4);
    const uint16_t fileHdrSz   = RdU16LE(data + 8);
    const uint16_t chunkHdrSz  = RdU16LE(data + 10);
    const uint32_t blkSz       = RdU32LE(data + 12);
    const uint32_t totalBlks   = RdU32LE(data + 16);
    const uint32_t totalChunks = RdU32LE(data + 20);

    if (majorVer != 1u || fileHdrSz < 28u || fileHdrSz > size || chunkHdrSz < 12u || chunkHdrSz > 256u) {
        return false;
    }
    if (blkSz < 1024u || blkSz > 1048576u || (blkSz & (blkSz - 1u)) != 0u) return false;
    if (totalBlks == 0u || totalChunks == 0u || totalChunks > 1000000u) return false;

    const uint64_t virtualSize = static_cast<uint64_t>(totalBlks) * blkSz;
    if (virtualSize == 0ULL || virtualSize > 64ULL * 1024 * 1024 * 1024) return false;

    auto extents = std::make_shared<std::vector<SimgExtent>>();
    extents->reserve(std::min<size_t>(totalChunks, 65536u));

    size_t pos = fileHdrSz;
    uint64_t curBlk = 0;
    uint64_t vCur   = 0;

    for (uint32_t c = 0; c < totalChunks; ++c) {
        if (chunkHdrSz > size - pos) return false;
        const uint16_t chunkType = RdU16LE(data + pos + 0);
        const uint32_t chunkBlks = RdU32LE(data + pos + 4);
        const uint32_t totalSz   = RdU32LE(data + pos + 8);
        if (totalSz < chunkHdrSz || totalSz > size - pos) return false;

        const size_t payloadOff = pos + chunkHdrSz;
        const size_t payloadLen = totalSz - chunkHdrSz;
        const uint64_t vLen     = static_cast<uint64_t>(chunkBlks) * blkSz;

        if (chunkType == 0xCAC1u) { // CHUNK_TYPE_RAW
            if (chunkBlks > totalBlks - curBlk || vLen != payloadLen) return false;
            if (chunkBlks > 0u) {
                extents->push_back({ vCur, vCur + vLen, 0xCAC1u, 0u, payloadOff });
                curBlk += chunkBlks;
                vCur   += vLen;
            }
        } else if (chunkType == 0xCAC2u) { // CHUNK_TYPE_FILL
            if (chunkBlks > totalBlks - curBlk || payloadLen != 4u) return false;
            uint32_t pat = RdU32LE(data + payloadOff);
            if (chunkBlks > 0u) {
                extents->push_back({ vCur, vCur + vLen, 0xCAC2u, pat, 0u });
                curBlk += chunkBlks;
                vCur   += vLen;
            }
        } else if (chunkType == 0xCAC3u) { // CHUNK_TYPE_DONT_CARE
            if (chunkBlks > totalBlks - curBlk || payloadLen != 0u) return false;
            if (chunkBlks > 0u) {
                extents->push_back({ vCur, vCur + vLen, 0xCAC3u, 0u, 0u });
                curBlk += chunkBlks;
                vCur   += vLen;
            }
        } else if (chunkType == 0xCAC4u) { // CHUNK_TYPE_CRC32
            if (payloadLen != 4u) return false;
        } else {
            return false;
        }

        pos += totalSz;
    }

    if (curBlk != totalBlks || vCur != virtualSize || extents->empty()) return false;

    outReader.virtualSize = virtualSize;
    outReader.formatName  = L"Android Sparse Image (.simg)";
    outReader.directRawBuffer = false;
    outReader.allowRawSliceFallback = false;
    outReader.readBytes = [data, size, virtualSize, extents](uint64_t diskOff, uint8_t* dst, size_t len) -> bool {
        if (len == 0u) return diskOff <= virtualSize;
        if (diskOff > virtualSize || len > virtualSize - diskOff) return false;

        auto it = std::upper_bound(
            extents->begin(),
            extents->end(),
            diskOff,
            [](uint64_t off, const SimgExtent& ex) { return off < ex.vEnd; });

        uint64_t curOff = diskOff;
        size_t rem = len;
        while (rem > 0u && it != extents->end()) {
            if (curOff < it->vStart || curOff >= it->vEnd) return false;
            const size_t take = static_cast<size_t>(std::min<uint64_t>(rem, it->vEnd - curOff));
            if (it->kind == 0xCAC1u) {
                const size_t rel = static_cast<size_t>(curOff - it->vStart);
                if (it->rawOff > size || rel > size - it->rawOff || take > size - it->rawOff - rel) return false;
                std::memcpy(dst, data + it->rawOff + rel, take);
            } else if (it->kind == 0xCAC3u) {
                std::memset(dst, 0, take);
            } else {
                const uint8_t pat[4] = {
                    static_cast<uint8_t>(it->fillLe & 0xFFu),
                    static_cast<uint8_t>((it->fillLe >> 8u) & 0xFFu),
                    static_cast<uint8_t>((it->fillLe >> 16u) & 0xFFu),
                    static_cast<uint8_t>((it->fillLe >> 24u) & 0xFFu)
                };
                if (pat[0] == pat[1] && pat[1] == pat[2] && pat[2] == pat[3]) {
                    std::memset(dst, pat[0], take);
                } else {
                    for (size_t k = 0; k < take; ++k) {
                        dst[k] = pat[static_cast<size_t>((curOff + k) & 3ULL)];
                    }
                }
            }
            dst    += take;
            curOff += take;
            rem    -= take;
            ++it;
        }
        return rem == 0u;
    };
    return true;
}

inline std::wstring DeriveSimgRawImageName(const std::wstring& archivePath) {
    std::wstring fn = PathUtils::GetFileName(archivePath);
    if (EndsWithCI(fn, L".img.simg") && fn.size() > 5u) {
        return fn.substr(0, fn.size() - 5u);
    }
    if (EndsWithCI(fn, L".simg") && fn.size() > 5u) {
        return fn.substr(0, fn.size() - 5u) + L".img";
    }
    return L"partition.img";
}

} // namespace DetailAndroidSimg

inline bool ParseOrExtractAndroidSimg(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    DetailDiskAndGameArc::VirtualDiskReader simgReader;
    if (!DetailAndroidSimg::BuildAndroidSimgReader(data, size, simgReader)) return false;

    // 1. Check direct LBA 0 filesystem superblock (ext2/ext3/ext4 or EROFS)
    if (simgReader.virtualSize >= 2048ULL) {
        uint8_t sbProbe[64] = {};
        if (simgReader.readBytes(1024ULL, sbProbe, sizeof(sbProbe))) {
            if (DetailAndroidSimg::RdU16LE(sbProbe + 56) == 0xEF53u) { // ext2 / ext3 / ext4
                ArchiveSummaryInfo dummy;
                ArchiveSummaryInfo& target = outInfo ? *outInfo : dummy;
                if (!DetailDiskAndGameArc::InspectOrExtractExtFsOnReader(
                        simgReader, 0, simgReader.virtualSize, L"", target, extractTargetNorm, outExtracted))
                {
                    return false;
                }
                if (outInfo) {
                    outInfo->formatName = L"Android Sparse Image (.simg / ext4)";
                    outInfo->valid = !outInfo->entries.empty();
                    return outInfo->valid;
                }
                return true;
            }
            if (DetailAndroidSimg::RdU32LE(sbProbe + 0) == 0xE0F5E1E2u) { // Linux EROFS
                if (!DetailUnixAndLegacyArc::ParseOrExtractErofsOnReader(
                        0, simgReader.virtualSize, false, simgReader.readBytes, L"",
                        outInfo, extractTargetNorm, outExtracted))
                {
                    return false;
                }
                if (outInfo) {
                    outInfo->formatName = L"Android Sparse Image (.simg / EROFS)";
                    outInfo->valid = !outInfo->entries.empty();
                    return outInfo->valid;
                }
                return true;
            }
        }
    }

    // 2. Check GPT / MBR partitions inside sparse disk image
    std::vector<DetailDiskAndGameArc::DiscoveredPartition> parts;
    DetailDiskAndGameArc::DiscoverDiskPartitions(simgReader, parts);
    if (!parts.empty() && !(parts.size() == 1u && parts[0].byteOffset == 0u)) {
        bool mountedAny = false;
        ArchiveSummaryInfo dummy;
        ArchiveSummaryInfo& target = outInfo ? *outInfo : dummy;
        for (size_t i = 0; i < parts.size(); ++i) {
            const auto& pt = parts[i];
            std::wstring pfx = (parts.size() > 1u)
                ? (pt.label.empty() ? (L"Partition_" + std::to_wstring(i + 1u)) : pt.label)
                : L"";
            if (DetailDiskAndGameArc::InspectOrExtractExtFsOnReader(
                    simgReader, pt.byteOffset, pt.byteLength, pfx, target, extractTargetNorm, outExtracted))
            {
                if (extractTargetNorm) return true;
                mountedAny = true;
                continue;
            }
            if (DetailUnixAndLegacyArc::ParseOrExtractErofsOnReader(
                    pt.byteOffset, pt.byteOffset + pt.byteLength, false, simgReader.readBytes, pfx,
                    outInfo, extractTargetNorm, outExtracted))
            {
                if (extractTargetNorm) return true;
                mountedAny = true;
                continue;
            }
        }
        if (mountedAny && outInfo) {
            outInfo->formatName = L"Android Sparse Image (.simg)";
            outInfo->valid = !outInfo->entries.empty();
            return outInfo->valid;
        }
    }

    // 3. Fallback: expose reconstructed raw binary partition (<stem>.img)
    const std::wstring rawName = ArchivePath::BaseName(ArchivePath::Normalize(DetailAndroidSimg::DeriveSimgRawImageName(archivePath)));
    if (extractTargetNorm && outExtracted) {
        std::wstring want = ArchivePath::Normalize(*extractTargetNorm);
        if (_wcsicmp(want.c_str(), rawName.c_str()) != 0) return false;
        if (simgReader.virtualSize > 128ULL * 1024 * 1024) return false;
        outExtracted->resize(static_cast<size_t>(simgReader.virtualSize));
        if (!simgReader.readBytes(0, outExtracted->data(), outExtracted->size())) {
            outExtracted->clear();
            return false;
        }
        return true;
    }

    if (!outInfo) return false;
    outInfo->formatName = L"Android Sparse Image (.simg)";
    ArchiveItemEntry item;
    item.pathInArchive = rawName;
    item.name = rawName;
    item.isDirectory = false;
    item.uncompressedSize = simgReader.virtualSize;
    item.compressedSize   = size;
    item.compressionMethod = 1;
    outInfo->entries.push_back(std::move(item));
    outInfo->valid = true;
    return true;
}

} // namespace Explorer2::Formats
