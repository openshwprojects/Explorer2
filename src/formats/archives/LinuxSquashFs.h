#pragma once
// LinuxSquashFs.h — Linux SquashFS 4.0 / Snap / AppImage filesystem reader & extractor (.squashfs, .snap, .appimage — "hsqs").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailArc200 {

inline bool ParseOrExtractLinuxSquashFs(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    std::wstring wantNorm = extractTargetPath ? ArchivePath::Normalize(*extractTargetPath) : std::wstring{};

    // Search offset 0 (or ELF trailer up to 4 MB for .appimage/.snap/.squashfs) for SquashFS 4.0 "hsqs"
    size_t sqOff = SIZE_MAX;
    if (size >= 96 && std::memcmp(data, "hsqs", 4) == 0 && RdU16LE(data + 28) == 4) {
        sqOff = 0;
    } else if (extLower == L".appimage" || extLower == L".snap" || extLower == L".squashfs") {
        size_t scanLim = std::min<size_t>(size, 4 * 1024 * 1024);
        for (size_t i = 0; i + 96 <= scanLim; i += 4) {
            if (data[i] == 'h' && data[i + 1] == 's' && data[i + 2] == 'q' && data[i + 3] == 's') {
                uint16_t maj = RdU16LE(data + i + 28);
                uint32_t bs  = RdU32LE(data + i + 12);
                if (maj == 4 && bs >= 4096 && bs <= 1048576 && (bs & (bs - 1u)) == 0) {
                    sqOff = i;
                    break;
                }
            }
        }
    }

    if (sqOff != SIZE_MAX && sqOff + 96 <= size) {
        const uint8_t* sb       = data + sqOff;
        uint32_t inodes         = RdU32LE(sb + 4);
        uint32_t blkSize        = RdU32LE(sb + 12);
        uint32_t fragCount      = RdU32LE(sb + 16);
        uint16_t compId         = RdU16LE(sb + 20);
        uint16_t sMajor         = RdU16LE(sb + 28);
        uint64_t rootInodeRef   = RdU64LE(sb + 32);
        uint64_t inodeTblStart  = RdU64LE(sb + 64);
        uint64_t dirTblStart    = RdU64LE(sb + 72);
        uint64_t fragTblStart   = RdU64LE(sb + 80);

        if (sMajor != 4 || inodes == 0 || blkSize < 4096 || blkSize > 1048576 || (blkSize & (blkSize - 1u)) != 0) {
            return false;
        }
        if (inodeTblStart < 96 || dirTblStart < 96 ||
            sqOff + inodeTblStart >= size || sqOff + dirTblStart >= size)
        {
            return false;
        }

        if (outInfo) {
            const wchar_t* compName =
                (compId == 1) ? L"gzip" :
                (compId == 2) ? L"lzma" :
                (compId == 4) ? L"xz" :
                (compId == 5) ? L"lz4" :
                (compId == 6) ? L"zstd" : L"raw";
            outInfo->formatName = std::wstring(
                extLower == L".appimage" ? L"Linux AppImage (SquashFS/" :
                extLower == L".snap"     ? L"Ubuntu Snap (SquashFS/" : L"Linux SquashFS (") + compName + L")";
        }

        struct SqfsMetaCacheEntry {
            std::vector<uint8_t> bytes;
            size_t nextAbsPos = 0;
        };
        std::unordered_map<uint64_t, SqfsMetaCacheEntry> metaCache;

        auto getMetaBlock = [&](uint64_t absPos) -> const SqfsMetaCacheEntry* {
            auto it = metaCache.find(absPos);
            if (it != metaCache.end()) return &it->second;
            if (metaCache.size() >= 512) metaCache.clear();
            if (absPos + 2 > size) return nullptr;
            uint16_t hdr = RdU16LE(data + static_cast<size_t>(absPos));
            bool isUncomp = (hdr & 0x8000u) != 0;
            size_t cLen = hdr & 0x7FFFu;
            if (cLen == 0 || absPos + 2 + cLen > size) return nullptr;

            SqfsMetaCacheEntry entry;
            if (isUncomp) {
                if (cLen > 8192) return nullptr;
                entry.bytes.assign(
                    data + static_cast<size_t>(absPos) + 2,
                    data + static_cast<size_t>(absPos) + 2 + cLen);
            } else {
                if (!DecompressSqfsChunk(data + static_cast<size_t>(absPos) + 2, cLen, 8192, compId, entry.bytes) ||
                    entry.bytes.empty() || entry.bytes.size() > 8192)
                {
                    return nullptr;
                }
            }
            entry.nextAbsPos = static_cast<size_t>(absPos) + 2 + cLen;
            auto res = metaCache.emplace(absPos, std::move(entry));
            return &res.first->second;
        };

        auto readSqfsMetaBytes = [&](uint64_t tableRelStart, uint64_t metaBlkRelOff, size_t innerOff, size_t wantLen, std::vector<uint8_t>& outBuf) -> bool {
            outBuf.clear();
            if (wantLen == 0) return true;
            if (wantLen > 16 * 1024 * 1024) return false;
            uint64_t curAbs = sqOff + tableRelStart + metaBlkRelOff;
            if (curAbs < sqOff || curAbs >= size) return false;

            outBuf.reserve(wantLen);
            size_t skip = innerOff;
            size_t hops = 0;
            while (outBuf.size() < wantLen && hops++ < 4096) {
                const SqfsMetaCacheEntry* blk = getMetaBlock(curAbs);
                if (!blk || blk->bytes.empty()) return false;
                if (skip >= blk->bytes.size()) {
                    skip -= blk->bytes.size();
                    curAbs = blk->nextAbsPos;
                    continue;
                }
                size_t avail = blk->bytes.size() - skip;
                size_t take  = std::min<size_t>(avail, wantLen - outBuf.size());
                outBuf.insert(outBuf.end(), blk->bytes.data() + skip, blk->bytes.data() + skip + take);
                skip = 0;
                curAbs = blk->nextAbsPos;
            }
            return outBuf.size() == wantLen;
        };

        auto extractSqfsFile = [&](uint16_t itype, uint64_t blkOff, size_t inOff, uint64_t blocksStart,
                                   uint64_t fileSize, uint32_t fragIdx, uint32_t fragOff, std::vector<uint8_t>& fileOut) -> bool
        {
            if (fileSize == 0) {
                fileOut.clear();
                return true;
            }
            if (fileSize > 256 * 1024 * 1024) return false;

            bool hasFrag = (fragIdx != 0xFFFFFFFFu);
            uint64_t numBlocks64 = hasFrag ? (fileSize / blkSize) : ((fileSize + blkSize - 1ULL) / blkSize);
            if (numBlocks64 > 262144ULL) return false;
            size_t numBlocks = static_cast<size_t>(numBlocks64);
            size_t hdrLen = (itype == 2) ? 32u : 56u;

            std::vector<uint8_t> ibuf;
            if (!readSqfsMetaBytes(inodeTblStart, blkOff, inOff, hdrLen + numBlocks * 4u, ibuf)) {
                return false;
            }

            fileOut.clear();
            fileOut.reserve(static_cast<size_t>(fileSize));
            uint64_t curDataAbs = sqOff + blocksStart;
            std::vector<uint8_t> chunkBuf;

            for (size_t b = 0; b < numBlocks; ++b) {
                uint32_t rawBSz = RdU32LE(ibuf.data() + hdrLen + b * 4u);
                size_t expectLen = std::min<size_t>(blkSize, static_cast<size_t>(fileSize) - fileOut.size());
                if (rawBSz == 0) { // Sparse zero block
                    fileOut.insert(fileOut.end(), expectLen, 0);
                } else {
                    bool blkUncomp = (rawBSz & (1u << 24)) != 0;
                    uint32_t cSz = rawBSz & 0x00FFFFFFu;
                    if (cSz == 0 || curDataAbs + cSz > size) return false;
                    if (blkUncomp) {
                        if (cSz < expectLen) return false;
                        fileOut.insert(fileOut.end(),
                            data + static_cast<size_t>(curDataAbs),
                            data + static_cast<size_t>(curDataAbs) + expectLen);
                    } else {
                        if (!DecompressSqfsChunk(data + static_cast<size_t>(curDataAbs), cSz, blkSize, compId, chunkBuf) ||
                            chunkBuf.size() < expectLen)
                        {
                            return false;
                        }
                        fileOut.insert(fileOut.end(), chunkBuf.begin(), chunkBuf.begin() + static_cast<ptrdiff_t>(expectLen));
                    }
                    curDataAbs += cSz;
                }
            }

            if (fileOut.size() < fileSize) {
                if (!hasFrag || fragIdx >= fragCount || fragTblStart == UINT64_MAX) return false;
                size_t tailLen = static_cast<size_t>(fileSize) - fileOut.size();
                uint32_t ptrIdx = fragIdx / 512u;
                uint32_t subIdx = fragIdx % 512u;
                uint64_t ptrAbs = sqOff + fragTblStart + static_cast<uint64_t>(ptrIdx) * 8ULL;
                if (ptrAbs + 8 > size) return false;
                uint64_t fragMetaRelOff = RdU64LE(data + static_cast<size_t>(ptrAbs));

                std::vector<uint8_t> fragEnt;
                if (!readSqfsMetaBytes(0, fragMetaRelOff, static_cast<size_t>(subIdx) * 16u, 16u, fragEnt)) {
                    return false;
                }
                uint64_t fragStartBlk = RdU64LE(fragEnt.data() + 0);
                uint32_t fragRawSz    = RdU32LE(fragEnt.data() + 8);
                bool fragUncomp       = (fragRawSz & (1u << 24)) != 0;
                uint32_t fragCSz      = fragRawSz & 0x00FFFFFFu;
                uint64_t fragDataAbs  = sqOff + fragStartBlk;
                if (fragCSz == 0 || fragDataAbs + fragCSz > size) return false;

                if (fragUncomp) {
                    if (static_cast<uint64_t>(fragOff) + tailLen > fragCSz) return false;
                    const uint8_t* fp = data + static_cast<size_t>(fragDataAbs) + fragOff;
                    fileOut.insert(fileOut.end(), fp, fp + tailLen);
                } else {
                    if (!DecompressSqfsChunk(data + static_cast<size_t>(fragDataAbs), fragCSz, blkSize, compId, chunkBuf) ||
                        static_cast<uint64_t>(fragOff) + tailLen > chunkBuf.size())
                    {
                        return false;
                    }
                    fileOut.insert(fileOut.end(),
                        chunkBuf.begin() + static_cast<ptrdiff_t>(fragOff),
                        chunkBuf.begin() + static_cast<ptrdiff_t>(fragOff + tailLen));
                }
            }
            return fileOut.size() == fileSize;
        };

        std::unordered_set<uint64_t> visitedDirs;
        size_t totalListed = 0;
        bool extractedOk = false;

        auto walkSqfsDir = [&](auto& self, uint64_t dirRef, const std::wstring& prefix, int depth) -> bool {
            if (depth > 32 || totalListed >= 50000) return true;
            if (!visitedDirs.insert(dirRef).second) return true;

            uint64_t dBlkOff = dirRef >> 16;
            size_t   dInOff  = static_cast<size_t>(dirRef & 0xFFFFu);
            std::vector<uint8_t> dHdr;
            if (!readSqfsMetaBytes(inodeTblStart, dBlkOff, dInOff, 32u, dHdr)) return false;

            uint16_t dType = RdU16LE(dHdr.data() + 0);
            uint32_t dirBlkStart = 0;
            uint32_t dirRawSz = 0;
            uint16_t dirInnerOff = 0;
            if (dType == 1) {
                dirBlkStart = RdU32LE(dHdr.data() + 16);
                dirRawSz    = RdU16LE(dHdr.data() + 24);
                dirInnerOff = RdU16LE(dHdr.data() + 26);
            } else if (dType == 8) {
                if (!readSqfsMetaBytes(inodeTblStart, dBlkOff, dInOff, 40u, dHdr)) return false;
                dirRawSz    = RdU32LE(dHdr.data() + 20);
                dirBlkStart = RdU32LE(dHdr.data() + 24);
                dirInnerOff = RdU16LE(dHdr.data() + 34);
            } else {
                return false;
            }

            if (dirRawSz <= 3) return true; // Empty directory
            size_t dirByteLen = dirRawSz - 3u;
            if (dirByteLen > 8 * 1024 * 1024) return false;

            std::vector<uint8_t> dirData;
            if (!readSqfsMetaBytes(dirTblStart, dirBlkStart, dirInnerOff, dirByteLen, dirData)) return false;

            size_t dp = 0;
            while (dp + 12 <= dirData.size() && totalListed < 50000) {
                uint32_t countMinus1 = RdU32LE(dirData.data() + dp + 0);
                uint32_t hdrStartBlk = RdU32LE(dirData.data() + dp + 4);
                dp += 12;
                if (countMinus1 >= 8192u) return false;

                for (uint32_t idx = 0; idx <= countMinus1; ++idx) {
                    if (dp + 8 > dirData.size()) return false;
                    uint16_t entOff      = RdU16LE(dirData.data() + dp + 0);
                    uint16_t entType     = RdU16LE(dirData.data() + dp + 4);
                    uint16_t nameMinus1  = RdU16LE(dirData.data() + dp + 6);
                    dp += 8;
                    size_t nameLen = static_cast<size_t>(nameMinus1) + 1u;
                    if (dp + nameLen > dirData.size()) return false;
                    std::string childUtf8(reinterpret_cast<const char*>(dirData.data() + dp), nameLen);
                    dp += nameLen;
                    if (childUtf8.empty() || childUtf8 == "." || childUtf8 == "..") continue;

                    std::wstring childW = PathUtils::Utf8ToWide(childUtf8);
                    std::wstring fullP  = ArchivePath::Normalize(prefix.empty() ? childW : (prefix + L"/" + childW));
                    if (fullP.empty()) continue;

                    uint64_t childRef = (static_cast<uint64_t>(hdrStartBlk) << 16) | entOff;
                    uint64_t cBlkOff  = childRef >> 16;
                    size_t   cInOff   = static_cast<size_t>(childRef & 0xFFFFu);

                    std::vector<uint8_t> cBase;
                    if (!readSqfsMetaBytes(inodeTblStart, cBlkOff, cInOff, 16u, cBase)) continue;
                    uint16_t cType = RdU16LE(cBase.data() + 0);
                    bool isDir = (cType == 1 || cType == 8 || entType == 1 || entType == 8);

                    if (isDir) {
                        if (outInfo && !extractTargetPath) {
                            ArchiveItemEntry item;
                            item.pathInArchive = fullP;
                            item.name = ArchivePath::BaseName(fullP);
                            item.isDirectory = true;
                            item.compressionMethod = 0;
                            outInfo->entries.push_back(std::move(item));
                            ++totalListed;
                        }
                        if (cType == 1 || cType == 8) {
                            self(self, childRef, fullP, depth + 1);
                            if (extractedOk) return true;
                        }
                        continue;
                    }

                    if (cType == 2 || cType == 9) {
                        size_t needHdr = (cType == 2) ? 32u : 56u;
                        std::vector<uint8_t> fHdr;
                        if (!readSqfsMetaBytes(inodeTblStart, cBlkOff, cInOff, needHdr, fHdr)) continue;

                        uint64_t blocksStart = 0, fileSize = 0;
                        uint32_t fragIdx = 0xFFFFFFFFu, fragOff = 0;
                        if (cType == 2) {
                            blocksStart = RdU32LE(fHdr.data() + 16);
                            fragIdx     = RdU32LE(fHdr.data() + 20);
                            fragOff     = RdU32LE(fHdr.data() + 24);
                            fileSize    = RdU32LE(fHdr.data() + 28);
                        } else {
                            blocksStart = RdU64LE(fHdr.data() + 16);
                            fileSize    = RdU64LE(fHdr.data() + 24);
                            fragIdx     = RdU32LE(fHdr.data() + 44);
                            fragOff     = RdU32LE(fHdr.data() + 48);
                        }

                        if (extractTargetPath && outExtractedBytes) {
                            if (_wcsicmp(fullP.c_str(), wantNorm.c_str()) == 0) {
                                extractedOk = extractSqfsFile(cType, cBlkOff, cInOff, blocksStart, fileSize, fragIdx, fragOff, *outExtractedBytes);
                                return extractedOk;
                            }
                        } else if (outInfo) {
                            ArchiveItemEntry item;
                            item.pathInArchive = fullP;
                            item.name = ArchivePath::BaseName(fullP);
                            item.isDirectory = false;
                            item.uncompressedSize = fileSize;
                            item.compressedSize = fileSize;
                            item.compressionMethod = (compId != 0) ? compId : 1;
                            outInfo->entries.push_back(std::move(item));
                            ++totalListed;
                        }
                    }
                }
            }
            return true;
        };

        if (!walkSqfsDir(walkSqfsDir, rootInodeRef, L"", 0)) {
            if (extractTargetPath) return false;
        }
        if (extractTargetPath) return extractedOk;
        if (!outInfo || outInfo->entries.empty()) return false;
        outInfo->valid = true;
        return true;
    }
    return false;
}

} // namespace DetailArc200

inline bool InspectLinuxSquashFs(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    return DetailArc200::ParseOrExtractLinuxSquashFs(data, size, extLower, &outInfo, nullptr, nullptr);
}

inline bool ExtractLinuxSquashFsEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailArc200::ParseOrExtractLinuxSquashFs(data, size, extLower, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
