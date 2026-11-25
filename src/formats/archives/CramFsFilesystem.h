#pragma once
// CramFsFilesystem.h — Linux CramFS ("Compressed ROMFS", 0x28CD3D45 LE & BE) 4KB paged zlib/LZMA filesystem reader & extractor (.cramfs).

#include "../common/ArchiveFormatCommon.h"
#include <unordered_set>

namespace Explorer2::Formats {

namespace DetailCramFs {

struct CramFsInode {
    uint16_t mode = 0;
    uint16_t uid = 0;
    uint8_t  gid = 0;
    uint32_t fileSize = 0;
    uint32_t nameLenBytes = 0;
    uint32_t offsetBytes = 0;
};

inline uint32_t RdCramU32(const uint8_t* p, bool bigEndian) noexcept {
    return bigEndian ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p);
}

inline CramFsInode DecodeCramFsInode(const uint8_t* in, bool bigEndian) noexcept {
    CramFsInode node{};
    const uint32_t w0 = RdCramU32(in + 0u, bigEndian);
    const uint32_t w1 = RdCramU32(in + 4u, bigEndian);
    const uint32_t w2 = RdCramU32(in + 8u, bigEndian);
    if (!bigEndian) {
        node.mode         = static_cast<uint16_t>(w0 & 0xFFFFu);
        node.uid          = static_cast<uint16_t>((w0 >> 16u) & 0xFFFFu);
        node.fileSize     = w1 & 0x00FFFFFFu;
        node.gid          = static_cast<uint8_t>((w1 >> 24u) & 0xFFu);
        node.nameLenBytes = (w2 & 0x3Fu) * 4u;
        node.offsetBytes  = ((w2 >> 6u) & 0x03FFFFFFu) * 4u;
    } else {
        node.mode         = static_cast<uint16_t>((w0 >> 16u) & 0xFFFFu);
        node.uid          = static_cast<uint16_t>(w0 & 0xFFFFu);
        node.fileSize     = (w1 >> 8u) & 0x00FFFFFFu;
        node.gid          = static_cast<uint8_t>(w1 & 0xFFu);
        node.nameLenBytes = ((w2 >> 26u) & 0x3Fu) * 4u;
        node.offsetBytes  = (w2 & 0x03FFFFFFu) * 4u;
    }
    return node;
}

inline bool DecompressCramFsPage(const uint8_t* cPtr, size_t cLen, size_t expectLen, std::vector<uint8_t>& outPage) {
    outPage.clear();
    if (cLen == 0u) return false;
    if (ZipDeflate::InflateZlib(cPtr, cLen, outPage, 4096u) && outPage.size() >= expectLen) {
        return true;
    }
    if (ZipDeflate::InflateRaw(cPtr, cLen, outPage, 4096u) && outPage.size() >= expectLen) {
        return true;
    }
    if (cLen >= 6u && cPtr[0] == 0xFDu && std::memcmp(cPtr + 1u, "7zXZ\0", 5) == 0) {
        if (StreamCodecs::DecompressXzStream(cPtr, cLen, outPage, 4096u) && outPage.size() >= expectLen) {
            return true;
        }
    }
    // Embedded Linux cramfs-lzma variants: [props:1][dictLE32:4][optional u64 size:8][lzma1...]
    if (cLen >= 13u && cPtr[0] < 225u) {
        const uint8_t props  = cPtr[0];
        const uint32_t dict  = ByteIO::RdU32LE(cPtr + 1u);
        const uint64_t hdrSz = ByteIO::RdU64LE(cPtr + 5u);
        if (hdrSz == UINT64_MAX || hdrSz == expectLen || hdrSz <= 4096u) {
            if (StreamCodecs::DecompressLzma1(props, dict, cPtr + 13u, cLen - 13u, outPage, expectLen, true, 4096u) &&
                outPage.size() >= expectLen)
            {
                return true;
            }
        }
    }
    if (cLen >= 5u && cPtr[0] < 225u) {
        const uint8_t props = cPtr[0];
        const uint32_t dict = ByteIO::RdU32LE(cPtr + 1u);
        if (StreamCodecs::DecompressLzma1(props, dict, cPtr + 5u, cLen - 5u, outPage, expectLen, true, 4096u) &&
            outPage.size() >= expectLen)
        {
            return true;
        }
    }
    // OpenWrt / MontaVista cramfs-lzma 9-byte header: [lc, lp, pb, pad][dictLE32? or props at +4]
    if (cLen >= 9u && cPtr[0] <= 8u && cPtr[1] <= 4u && cPtr[2] <= 4u) {
        const uint8_t props = static_cast<uint8_t>((cPtr[2] * 5u + cPtr[1]) * 9u + cPtr[0]);
        if (StreamCodecs::DecompressLzma1(props, 65536u, cPtr + 4u, cLen - 4u, outPage, expectLen, true, 4096u) &&
            outPage.size() >= expectLen)
        {
            return true;
        }
    }
    return false;
}

inline bool ExtractCramFsInodeData(
    const uint8_t* imgBase,
    size_t imgSize,
    bool bigEndian,
    uint32_t sbFlags,
    const CramFsInode& inode,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (inode.fileSize == 0u) return true;
    if (inode.fileSize > 64u * 1024u * 1024u) return false;

    constexpr uint32_t kPageSize = 4096u;
    const uint32_t numPages = (inode.fileSize + kPageSize - 1u) / kPageSize;
    if (inode.offsetBytes == 0u || inode.offsetBytes > imgSize ||
        static_cast<uint64_t>(numPages) * 4ULL > imgSize - inode.offsetBytes)
    {
        return false;
    }

    const bool extPtrs = (sbFlags & 0x00000800u) != 0u; // CRAMFS_FLAG_EXT_BLOCK_POINTERS
    outBytes.reserve(inode.fileSize);

    uint32_t curDataStart = inode.offsetBytes + numPages * 4u;
    std::vector<uint8_t> pageBuf;

    for (uint32_t p = 0; p < numPages; ++p) {
        const size_t expectLen = std::min<size_t>(kPageSize, inode.fileSize - outBytes.size());
        const uint32_t rawPtr  = RdCramU32(imgBase + inode.offsetBytes + p * 4u, bigEndian);
        const bool isUncompExt = extPtrs && ((rawPtr & 0x80000000u) != 0u);
        const bool isDirectExt = extPtrs && ((rawPtr & 0x40000000u) != 0u);
        const uint32_t endOff  = extPtrs ? (rawPtr & 0x3FFFFFFFu) : rawPtr;

        if (isDirectExt) {
            const uint64_t directOff = static_cast<uint64_t>(endOff) << 2u;
            if (directOff > imgSize || expectLen > imgSize - static_cast<size_t>(directOff)) return false;
            const uint8_t* dp = imgBase + static_cast<size_t>(directOff);
            outBytes.insert(outBytes.end(), dp, dp + expectLen);
            continue;
        }

        if (endOff < curDataStart || endOff > imgSize) return false;
        const size_t cLen = endOff - curDataStart;
        if (cLen == 0u) {
            // Zero-filled hole page
            outBytes.insert(outBytes.end(), expectLen, 0u);
        } else if (isUncompExt) {
            if (cLen < expectLen) return false;
            const uint8_t* sp = imgBase + curDataStart;
            outBytes.insert(outBytes.end(), sp, sp + expectLen);
        } else {
            if (!DecompressCramFsPage(imgBase + curDataStart, cLen, expectLen, pageBuf)) return false;
            outBytes.insert(outBytes.end(), pageBuf.begin(), pageBuf.begin() + static_cast<ptrdiff_t>(expectLen));
        }
        curDataStart = endOff;
    }

    return outBytes.size() == inode.fileSize;
}

inline bool ParseOrExtractCramFs(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!data || size < 76u) return false;

    // CramFS superblock sits at offset 0, or offset 512 when preceded by a boot sector pad
    size_t sbOff = SIZE_MAX;
    bool bigEndian = false;
    for (size_t cand : { size_t(0), size_t(512) }) {
        if (size < cand + 76u) continue;
        const uint8_t* sb = data + cand;
        if (std::memcmp(sb + 16u, "Compressed ROMFS", 16) != 0) continue;
        if (ByteIO::RdU32LE(sb + 0u) == 0x28CD3D45u) {
            sbOff = cand;
            bigEndian = false;
            break;
        }
        if (ByteIO::RdU32BE(sb + 0u) == 0x28CD3D45u) {
            sbOff = cand;
            bigEndian = true;
            break;
        }
    }
    if (sbOff == SIZE_MAX) return false;

    const uint8_t* sb = data + sbOff;
    const uint32_t fsSize  = RdCramU32(sb + 4u, bigEndian);
    const uint32_t sbFlags = RdCramU32(sb + 8u, bigEndian);

    // CramFS inode offsets are relative to start of image (offset 0, or sbOff if standalone)
    const uint8_t* imgBase = data;
    size_t imgSize = size;
    if ((sbFlags & 0x00000001u) != 0u && fsSize >= 76u) {
        if (fsSize <= size) {
            imgSize = fsSize;
        } else {
            return false;
        }
    }

    const CramFsInode rootInode = DecodeCramFsInode(sb + 64u, bigEndian);
    if ((rootInode.mode & 0xF000u) != 0x4000u) return false; // Root must be S_IFDIR

    if (outInfo) {
        std::string volName(reinterpret_cast<const char*>(sb + 48u), strnlen(reinterpret_cast<const char*>(sb + 48u), 16u));
        std::wstring fmt = bigEndian ? L"Linux CramFS (BE)" : L"Linux CramFS (LE)";
        if (!volName.empty() && volName != "Compressed") {
            fmt += L" (" + PathUtils::Utf8ToWide(volName) + L")";
        }
        outInfo->formatName = std::move(fmt);
    }

    const std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};
    std::unordered_set<uint32_t> visitedDirs;
    size_t totalListed = 0;
    bool extractedOk = false;

    auto walkDir = [&](auto& self, const CramFsInode& dirNode, const std::wstring& prefix, int depth) -> bool {
        if (depth > 32 || totalListed >= 50000u || dirNode.fileSize == 0u || dirNode.offsetBytes == 0u) {
            return true;
        }
        if (!visitedDirs.insert(dirNode.offsetBytes).second) return true;

        size_t baseOff = dirNode.offsetBytes;
        if (baseOff > imgSize || dirNode.fileSize > imgSize - baseOff) {
            // Try relative to sbOff if boot-padded image stored sbOff-relative pointers
            if (sbOff > 0u && sbOff <= imgSize && baseOff <= imgSize - sbOff && dirNode.fileSize <= imgSize - sbOff - baseOff) {
                baseOff += sbOff;
            } else {
                return false;
            }
        }

        const size_t dirEnd = baseOff + dirNode.fileSize;
        size_t pos = baseOff;
        while (dirEnd - pos >= 12u && totalListed < 50000u) {
            const CramFsInode child = DecodeCramFsInode(imgBase + pos, bigEndian);
            pos += 12u;
            if (child.nameLenBytes > dirEnd - pos) return false;

            const size_t actualNameLen = strnlen(reinterpret_cast<const char*>(imgBase + pos), child.nameLenBytes);
            std::string childUtf8(reinterpret_cast<const char*>(imgBase + pos), actualNameLen);
            pos += child.nameLenBytes;

            if (childUtf8.empty() || childUtf8 == "." || childUtf8 == "..") continue;
            const std::wstring childW = PathUtils::Utf8ToWide(childUtf8);
            const std::wstring fullRel = ArchivePath::Normalize(prefix.empty() ? childW : (prefix + L"/" + childW));
            if (fullRel.empty()) continue;

            const uint16_t ftype = child.mode & 0xF000u;
            if (ftype == 0x4000u) { // S_IFDIR
                if (outInfo && !extractTargetNorm) {
                    ArchiveItemEntry item;
                    item.pathInArchive     = fullRel;
                    item.name              = ArchivePath::BaseName(fullRel);
                    item.isDirectory       = true;
                    item.compressionMethod = 0;
                    outInfo->entries.push_back(std::move(item));
                    ++totalListed;
                }
                if (!self(self, child, fullRel, depth + 1)) return false;
                if (extractedOk) return true;
            } else if (ftype == 0x8000u || ftype == 0xA000u) { // S_IFREG / S_IFLNK
                if (extractTargetNorm && outExtracted) {
                    if (_wcsicmp(fullRel.c_str(), wantNorm.c_str()) == 0) {
                        CramFsInode adjusted = child;
                        if (adjusted.fileSize > 0u && adjusted.offsetBytes > imgSize && sbOff > 0u) {
                            return false;
                        }
                        extractedOk = ExtractCramFsInodeData(imgBase, imgSize, bigEndian, sbFlags, adjusted, *outExtracted);
                        return extractedOk;
                    }
                } else if (outInfo) {
                    // Validate pointer table fits inside image
                    if (child.fileSize > 0u) {
                        const uint32_t nPages = (child.fileSize + 4095u) / 4096u;
                        if (child.offsetBytes == 0u || child.offsetBytes > imgSize ||
                            static_cast<uint64_t>(nPages) * 4ULL > imgSize - child.offsetBytes)
                        {
                            return false;
                        }
                    }
                    uint64_t estComp = 0;
                    if (child.fileSize > 0u) {
                        const uint32_t nPages = (child.fileSize + 4095u) / 4096u;
                        const uint32_t lastEnd = RdCramU32(imgBase + child.offsetBytes + (nPages - 1u) * 4u, bigEndian) &
                                                 (((sbFlags & 0x800u) != 0u) ? 0x3FFFFFFFu : 0xFFFFFFFFu);
                        const uint32_t dataStart = child.offsetBytes + nPages * 4u;
                        estComp = (lastEnd >= dataStart) ? (lastEnd - dataStart) : child.fileSize;
                    }

                    ArchiveItemEntry item;
                    item.pathInArchive     = fullRel;
                    item.name              = ArchivePath::BaseName(fullRel);
                    item.isDirectory       = false;
                    item.uncompressedSize  = child.fileSize;
                    item.compressedSize    = estComp;
                    item.rawDataOffset     = child.offsetBytes;
                    item.compressionMethod = 8;
                    outInfo->entries.push_back(std::move(item));
                    ++totalListed;
                }
            }
        }
        return true;
    };

    if (!walkDir(walkDir, rootInode, L"", 0)) {
        if (outInfo) *outInfo = {};
        return false;
    }

    if (extractTargetNorm) return extractedOk;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

} // namespace DetailCramFs

inline bool InspectCramFsFilesystem(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailCramFs::ParseOrExtractCramFs(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractCramFsFilesystemEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    return DetailCramFs::ParseOrExtractCramFs(data, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
