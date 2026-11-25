#pragma once
// LinuxExtFilesystem.h — Linux ext2 / ext3 / ext4 filesystem reader & extent/indirect extractor
// (.ext2, .ext3, .ext4, raw .img, and virtual disk / Android sparse partitions).

#include "../common/VirtualDiskCommon.h"

namespace Explorer2::Formats {

namespace DetailDiskAndGameArc {

inline bool InspectOrExtractExtFsOnReader(
    const VirtualDiskReader& reader,
    uint64_t partBase,
    uint64_t partBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (partBytes < 2048 || partBase > reader.virtualSize || partBytes > reader.virtualSize - partBase) return false;
    uint8_t sb[1024] = {};
    if (!reader.readBytes(partBase + 1024, sb, 1024)) return false;
    if (RdU16LE(sb + 56) != 0xEF53u) return false;

    uint32_t inodesCnt    = RdU32LE(sb + 0);
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

    bool has64Bit    = (incompat & 0x80u) != 0;
    bool hasFileType = (incompat & 0x02u) != 0;
    uint32_t descSize = (has64Bit && rawDescSz >= 64) ? rawDescSz : 32u;
    if (descSize > 1024) return false;

    const uint64_t partEnd  = partBase + partBytes;
    const uint64_t bgdtRel  = (blockSize == 1024u) ? 2048ULL : static_cast<uint64_t>(blockSize);
    if (bgdtRel >= partBytes) return false;
    const uint64_t bgdtBase = partBase + bgdtRel;

    auto readExtInode = [&](uint32_t inoNum, std::vector<uint8_t>& outIno) -> bool {
        if (inoNum == 0 || inoNum > inodesCnt) return false;
        uint32_t bg  = (inoNum - 1u) / inodesPerGrp;
        uint32_t idx = (inoNum - 1u) % inodesPerGrp;
        uint64_t gdRel = static_cast<uint64_t>(bg) * descSize;
        size_t gdNeed = (descSize >= 64) ? 64u : 32u;
        if (gdRel > partEnd - bgdtBase || gdNeed > partEnd - bgdtBase - gdRel) return false;
        uint8_t gd[64] = {};
        if (!reader.readBytes(bgdtBase + gdRel, gd, gdNeed)) return false;

        uint64_t inoTblBlk = RdU32LE(gd + 8);
        if (descSize >= 64) {
            inoTblBlk |= (static_cast<uint64_t>(RdU32LE(gd + 40)) << 32);
        }
        if (inoTblBlk == 0 || inoTblBlk > partBytes / blockSize) return false;
        uint64_t inoRel = inoTblBlk * blockSize + static_cast<uint64_t>(idx) * inodeSz;
        if (inoRel > partBytes || inodeSz > partBytes - inoRel) return false;
        outIno.resize(inodeSz);
        return reader.readBytes(partBase + inoRel, outIno.data(), inodeSz);
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

        if (((iFlags & 0x10000000u) != 0 || ((mode & 0xF000u) == 0xA000u && RdU32LE(ino.data() + 28) == 0)) && fSize <= 60) {
            std::memcpy(outData.data(), ino.data() + 40, static_cast<size_t>(fSize));
            return true;
        }

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
                        if (eeLen == 0 || uninit) continue;
                        uint64_t logOff = static_cast<uint64_t>(eeBlock) * blockSize;
                        if (logOff >= fSize) continue;
                        uint64_t runBytes = std::min<uint64_t>(static_cast<uint64_t>(eeLen) * blockSize, fSize - logOff);
                        if (eeStart == 0 || eeStart > partBytes / blockSize) return false;
                        uint64_t physRel = eeStart * blockSize;
                        if (runBytes > partBytes - physRel) return false;
                        if (!reader.readBytes(partBase + physRel, outData.data() + static_cast<size_t>(logOff), static_cast<size_t>(runBytes))) {
                            return false;
                        }
                    }
                    return true;
                } else {
                    for (uint16_t e = 0; e < ehEntries; ++e) {
                        const uint8_t* ix = ehBuf + 12 + static_cast<size_t>(e) * 12;
                        uint64_t leafBlk  = (static_cast<uint64_t>(RdU16LE(ix + 8)) << 32) | RdU32LE(ix + 4);
                        if (leafBlk == 0 || leafBlk > partBytes / blockSize) return false;
                        uint64_t physRel = leafBlk * blockSize;
                        if (blockSize > partBytes - physRel) return false;
                        std::vector<uint8_t> nodeBlk(blockSize);
                        if (!reader.readBytes(partBase + physRel, nodeBlk.data(), blockSize)) return false;
                        if (!self(self, nodeBlk.data(), blockSize, depthGuard + 1)) return false;
                    }
                    return true;
                }
            };
            return walkExtentNode(walkExtentNode, ino.data() + 40, 60, 0);
        }

        uint64_t curLogOff = 0;
        uint32_t ptrsPerBlk = blockSize / 4u;
        auto readPhysBlock = [&](uint32_t blkId) -> bool {
            if (curLogOff >= fSize) return true;
            size_t toCopy = static_cast<size_t>(std::min<uint64_t>(blockSize, fSize - curLogOff));
            if (blkId != 0) {
                uint64_t physRel = static_cast<uint64_t>(blkId) * blockSize;
                if (physRel > partBytes || toCopy > partBytes - physRel) return false;
                if (!reader.readBytes(partBase + physRel, outData.data() + static_cast<size_t>(curLogOff), toCopy)) return false;
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
            uint64_t physRel = static_cast<uint64_t>(indBlk) * blockSize;
            if (physRel > partBytes || blockSize > partBytes - physRel) return false;
            std::vector<uint8_t> ibuf(blockSize);
            if (!reader.readBytes(partBase + physRel, ibuf.data(), blockSize)) return false;
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
            if (rLen < 8 || rLen > dBytes.size() - dp) break;
            if (dIno != 0 && nLen > 0 && 8u + nLen <= rLen) {
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
            if (rLen < 8 || rLen > dirBytes.size() - dp) break;

            if (dIno != 0 && nLen > 0 && 8u + nLen <= rLen) {
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

} // namespace DetailDiskAndGameArc

inline bool InspectLinuxExtFilesystem(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 2048 || DetailDiskAndGameArc::RdU16LE(data + 1024 + 56) != 0xEF53u) return false;
    if (DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"Linux ext2/ext3/ext4 Image", false);
    outInfo = {};
    outInfo.formatName = L"Linux ext2/ext3/ext4 Image";
    if (!DetailDiskAndGameArc::InspectOrExtractExtFsOnReader(raw, 0, size, L"", outInfo, nullptr, nullptr)) {
        outInfo = {};
        return false;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractLinuxExtFilesystemEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!data || size < 2048 || normTarget.empty() || DetailDiskAndGameArc::RdU16LE(data + 1024 + 56) != 0xEF53u) return false;
    if (DetailDiskAndGameArc::IsKnownVirtualDiskContainerMagic(data, size)) return false;
    DetailDiskAndGameArc::VirtualDiskReader raw = DetailDiskAndGameArc::MakeRawMemoryDiskReader(data, size, L"Linux ext2/ext3/ext4 Image", false);
    ArchiveSummaryInfo dummy;
    return DetailDiskAndGameArc::InspectOrExtractExtFsOnReader(raw, 0, size, L"", dummy, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
