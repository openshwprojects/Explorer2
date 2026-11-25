#pragma once
// LinuxErofs.h — Linux EROFS (Enhanced Read-Only File System) image reader & z_erofs extractor (.erofs — 0xE0F5E1E2).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

inline bool ParseOrExtractErofsOnReader(
    uint64_t partBase,
    uint64_t totalSize,
    bool directRawBuffer,
    const std::function<bool(uint64_t, uint8_t*, size_t)>& readBytes,
    const std::wstring& prefixDir,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    if (!readBytes || totalSize < 1152 || partBase > totalSize || totalSize - partBase < 1152) return false;
    const uint64_t partEnd = totalSize;
    const uint64_t partSize = totalSize - partBase;

    uint8_t sb[128] = {};
    if (!readBytes(partBase + 1024ULL, sb, sizeof(sb))) return false;
    if (RdU32LE(sb + 0) != 0xE0F5E1E2u) return false;

    const uint8_t  blkBits      = sb[12];
    const uint16_t rootNid      = RdU16LE(sb + 14);
    const uint32_t featIncompat = RdU32LE(sb + 20);
    const uint16_t sbComprAlgs  = RdU16LE(sb + 24);
    const uint32_t blocks       = RdU32LE(sb + 36);
    const uint32_t metaBlk      = RdU32LE(sb + 40);
    (void)sbComprAlgs;

    if (blkBits < 9 || blkBits > 16 || blocks == 0) return false;
    const size_t blkSz = size_t(1) << blkBits;

    std::wstring wantNorm = extractTargetPath ? ArchivePath::Normalize(*extractTargetPath) : std::wstring{};

    struct ErofsInodeInfo {
        uint64_t inodeOff = 0;   // relative to partBase
        size_t   inodeHdrSz = 32;
        size_t   xattrBytes = 0;
        uint16_t dataLayout = 0;
        uint16_t iMode = 0;
        uint64_t iSize = 0;
        uint32_t rawBlkAddr = 0;
    };

    auto readRel = [&](uint64_t relOff, uint8_t* dst, size_t len) -> bool {
        if (relOff > partSize || len > partSize - relOff) return false;
        if (partBase + relOff > partEnd || len > partEnd - (partBase + relOff)) return false;
        return readBytes(partBase + relOff, dst, len);
    };

    auto readErofsInode = [&](uint64_t nid, ErofsInodeInfo& inf) -> bool {
        uint64_t off64 = static_cast<uint64_t>(metaBlk) * blkSz + nid * 32ULL;
        if (off64 > partSize || 32ULL > partSize - off64) return false;
        uint8_t inoBuf[64] = {};
        if (!readRel(off64, inoBuf, 32)) return false;

        inf.inodeOff = off64;
        uint16_t iFormat    = RdU16LE(inoBuf + 0);
        uint16_t xattrCount = RdU16LE(inoBuf + 2);
        inf.iMode           = RdU16LE(inoBuf + 4);
        if ((inf.iMode & 0170000u) == 0) return false;

        bool isExt = (iFormat & 1u) != 0;
        inf.inodeHdrSz = isExt ? 64u : 32u;
        if (isExt) {
            if (64ULL > partSize - off64 || !readRel(off64, inoBuf, 64)) return false;
        }

        inf.dataLayout = (iFormat >> 1) & 0x07u;
        inf.xattrBytes = (xattrCount > 0) ? (12u + static_cast<size_t>(xattrCount - 1u) * 4u) : 0u;
        if (!isExt) {
            inf.iSize      = RdU32LE(inoBuf + 8);
            inf.rawBlkAddr = RdU32LE(inoBuf + 16);
        } else {
            inf.iSize      = RdU64LE(inoBuf + 8);
            uint32_t u16Addr = RdU32LE(inoBuf + 16);
            uint32_t u20Addr = RdU32LE(inoBuf + 20);
            inf.rawBlkAddr = (u16Addr != 0 && u16Addr != 0xFFFFFFFFu) ? u16Addr : u20Addr;
        }
        return true;
    };

    auto readErofsData = [&](const ErofsInodeInfo& inf, std::vector<uint8_t>& outBuf) -> bool {
        outBuf.clear();
        if (inf.iSize == 0) return true;
        if (inf.iSize > 256ULL * 1024 * 1024) return false;

        if (inf.dataLayout == 0) { // EROFS_INODE_FLAT_PLAIN
            uint64_t startOff = static_cast<uint64_t>(inf.rawBlkAddr) * blkSz;
            outBuf.resize(static_cast<size_t>(inf.iSize));
            if (!readRel(startOff, outBuf.data(), outBuf.size())) {
                outBuf.clear();
                return false;
            }
            return true;
        }

        if (inf.dataLayout == 2) { // EROFS_INODE_FLAT_INLINE
            size_t fullBytes = static_cast<size_t>((inf.iSize / blkSz) * blkSz);
            size_t tailBytes = static_cast<size_t>(inf.iSize % blkSz);
            outBuf.resize(static_cast<size_t>(inf.iSize));
            if (fullBytes > 0) {
                uint64_t startOff = static_cast<uint64_t>(inf.rawBlkAddr) * blkSz;
                if (!readRel(startOff, outBuf.data(), fullBytes)) {
                    outBuf.clear();
                    return false;
                }
            }
            if (tailBytes > 0) {
                uint64_t inlineOff = inf.inodeOff + inf.inodeHdrSz + inf.xattrBytes;
                if (!readRel(inlineOff, outBuf.data() + fullBytes, tailBytes)) {
                    outBuf.clear();
                    return false;
                }
            }
            return true;
        }

        // Compressed z_erofs layouts:
        //   1 = EROFS_INODE_FLAT_COMPRESSION_LEGACY / EROFS_INODE_COMPRESSED_FULL
        //   3 = EROFS_INODE_COMPRESSED_FULL (or mainline EROFS_INODE_COMPRESSED_COMPACT)
        //   4 = EROFS_INODE_COMPRESSED_COMPACT
        if (inf.dataLayout == 1 || inf.dataLayout == 3 || inf.dataLayout == 4) {
            uint64_t rawMetaEnd = inf.inodeOff + inf.inodeHdrSz + inf.xattrBytes;
            uint64_t mapHdrOff = (rawMetaEnd + 7ULL) & ~7ULL;
            uint8_t mh[8] = {};
            if (!readRel(mapHdrOff, mh, 8)) return false;

            uint16_t hAdvise      = RdU16LE(mh + 4);
            uint8_t  hAlgType     = mh[6];
            uint8_t  hClusterBits = mh[7];
            uint64_t idxBase      = mapHdrOff + 8ULL;

            // Check true legacy v1 16-byte zero dummy header before 8-byte VLE descriptors
            if (inf.dataLayout == 1 && featIncompat == 0 &&
                hAdvise == 0 && hAlgType == 0 && hClusterBits == 0)
            {
                uint8_t p8[8] = {}, p16[8] = {};
                if (readRel(mapHdrOff + 8ULL, p8, 8) && readRel(mapHdrOff + 16ULL, p16, 8)) {
                    bool p8Zero = true;
                    for (uint8_t b : p8) if (b != 0) { p8Zero = false; break; }
                    uint16_t adv16 = RdU16LE(p16 + 0);
                    uint16_t ofs16 = RdU16LE(p16 + 2);
                    uint32_t blk16 = RdU32LE(p16 + 4);
                    if (p8Zero && (adv16 & 3u) != 2u && ofs16 == 0u && blk16 > 0u && blk16 < blocks) {
                        idxBase = mapHdrOff + 16ULL;
                    }
                }
            }

            const uint32_t lclusterBits = static_cast<uint32_t>(blkBits) + static_cast<uint32_t>(hClusterBits & 0x07u);
            if (lclusterBits < 9u || lclusterBits > 24u) return false;
            const size_t lclusterSz = size_t(1) << lclusterBits;
            const uint64_t totalLc64 = (inf.iSize + lclusterSz - 1ULL) >> lclusterBits;
            if (totalLc64 == 0 || totalLc64 > 524288ULL) return false;
            const size_t totalLclusters = static_cast<size_t>(totalLc64);

            bool useCompact = false;
            if (inf.dataLayout == 4) {
                useCompact = true;
            } else if (inf.dataLayout == 3) {
                if ((hAdvise & 0x0001u) != 0) {
                    useCompact = true;
                } else {
                    const uint32_t lobitsProbe = std::max<uint32_t>(lclusterBits, 12u);
                    uint8_t probe[4] = {};
                    if (!readRel(idxBase, probe, 4)) return false;
                    uint16_t w0 = RdU16LE(probe + 0);
                    if (lobitsProbe <= 14u && (w0 >> lobitsProbe) != 0u && (w0 & ((1u << lobitsProbe) - 1u)) == 0u) {
                        useCompact = true;
                    }
                }
            }

            struct LclusterDesc {
                uint8_t  type = 0;       // 0=PLAIN, 1=HEAD1, 2=NONHEAD, 3=HEAD2
                uint16_t clusterOfs = 0;
                uint32_t blkAddr = 0;
                uint16_t delta0 = 0;
            };
            std::vector<LclusterDesc> lc(totalLclusters);
            uint64_t afterIdxOff = idxBase;

            if (!useCompact) {
                const size_t idxBytes = totalLclusters * 8u;
                std::vector<uint8_t> idxBuf(idxBytes);
                if (!readRel(idxBase, idxBuf.data(), idxBytes)) return false;
                for (size_t i = 0; i < totalLclusters; ++i) {
                    const uint8_t* p = idxBuf.data() + i * 8u;
                    uint16_t adv = RdU16LE(p + 0);
                    lc[i].type       = static_cast<uint8_t>(adv & 0x03u);
                    lc[i].clusterOfs = RdU16LE(p + 2);
                    if (lc[i].type == 2u) {
                        lc[i].delta0 = RdU16LE(p + 4);
                    } else {
                        lc[i].blkAddr = RdU32LE(p + 4);
                    }
                }
                afterIdxOff = idxBase + idxBytes;
            } else {
                const uint32_t lobits = std::max<uint32_t>(lclusterBits, 12u);
                if (lobits > 14u) return false;
                const uint32_t lomask = (1u << lobits) - 1u;
                const bool bigPcluster = (hAdvise & 0x0002u) != 0;
                const bool compacted2B = ((hAdvise & 0x0001u) != 0) && (lobits == 12u);

                size_t initial4BCount = 0;
                if (compacted2B) {
                    size_t rem32 = static_cast<size_t>(idxBase & 31ULL);
                    if (rem32 != 0) initial4BCount = (32u - rem32) / 4u;
                    if (initial4BCount > totalLclusters) initial4BCount = 0;
                }
                const size_t num2BPacks  = compacted2B ? ((totalLclusters - initial4BCount) / 16u) : 0u;
                const size_t middle2BEnd = initial4BCount + num2BPacks * 16u;

                auto decodePack = [&](uint64_t packOff, size_t vcnt, size_t amortizedShift, size_t firstLcn) -> bool {
                    const size_t packBytes = vcnt << amortizedShift;
                    uint8_t packBuf[32] = {};
                    if (packBytes > sizeof(packBuf) || !readRel(packOff, packBuf, packBytes)) return false;

                    const uint32_t baseBlk = RdU32LE(packBuf + packBytes - 4u);
                    const uint32_t encodeBits = static_cast<uint32_t>(((packBytes - 4u) * 8u) / vcnt);
                    const uint32_t entryMask = (encodeBits >= 32u) ? 0xFFFFFFFFu : ((1u << encodeBits) - 1u);

                    uint8_t rawType[16] = {};
                    uint16_t rawLo[16]  = {};
                    for (size_t k = 0; k < vcnt; ++k) {
                        const size_t bitPos  = k * encodeBits;
                        const size_t byteIdx = bitPos >> 3;
                        const uint32_t bitSh = static_cast<uint32_t>(bitPos & 7u);
                        uint32_t acc = 0;
                        for (size_t b = 0; b < 4 && byteIdx + b < packBytes - 4u; ++b) {
                            acc |= static_cast<uint32_t>(packBuf[byteIdx + b]) << (b * 8u);
                        }
                        uint32_t v = (acc >> bitSh) & entryMask;
                        rawType[k] = static_cast<uint8_t>((v >> lobits) & 0x03u);
                        rawLo[k]   = static_cast<uint16_t>(v & lomask);
                    }

                    uint32_t curBlk = baseBlk;
                    for (size_t k = 0; k < vcnt; ++k) {
                        const size_t dstIdx = firstLcn + k;
                        if (rawType[k] != 2u) {
                            if (dstIdx < totalLclusters) {
                                lc[dstIdx].type       = rawType[k];
                                lc[dstIdx].clusterOfs = rawLo[k];
                                lc[dstIdx].blkAddr    = curBlk;
                            }
                            bool nextHasCblkCnt = (k + 1u < vcnt &&
                                                   rawType[k + 1u] == 2u &&
                                                   bigPcluster &&
                                                   (rawLo[k + 1u] & 0x0800u) != 0u);
                            if (!nextHasCblkCnt) {
                                ++curBlk;
                            }
                        } else {
                            if (dstIdx < totalLclusters) {
                                lc[dstIdx].type   = 2u;
                                lc[dstIdx].delta0 = rawLo[k];
                            }
                            if (bigPcluster && (rawLo[k] & 0x0800u) != 0u) {
                                curBlk += (rawLo[k] & 0x07FFu);
                            }
                        }
                    }
                    return true;
                };

                uint64_t curPackOff = idxBase;
                size_t lcnPos = 0;
                while (lcnPos < initial4BCount) {
                    if (!decodePack(curPackOff, 2u, 2u, lcnPos)) return false;
                    curPackOff += 8ULL;
                    lcnPos += 2u;
                }
                while (lcnPos < middle2BEnd) {
                    if (!decodePack(curPackOff, 16u, 1u, lcnPos)) return false;
                    curPackOff += 32ULL;
                    lcnPos += 16u;
                }
                while (lcnPos < totalLclusters) {
                    if (!decodePack(curPackOff, 2u, 2u, lcnPos)) return false;
                    curPackOff += 8ULL;
                    lcnPos += 2u;
                }
                afterIdxOff = curPackOff;
            }

            struct PclusterSpan {
                size_t   headLcn = 0;
                size_t   endLcn = 0;
                uint8_t  type = 0;
                uint64_t logStart = 0;
                uint64_t logEnd = 0;
                uint32_t blkAddr = 0;
                uint32_t physBlks = 1;
                bool     isInline = false;
            };
            std::vector<PclusterSpan> spans;
            spans.reserve(totalLclusters);

            for (size_t i = 0; i < totalLclusters; ++i) {
                if (lc[i].type == 2u) continue;
                if (lc[i].clusterOfs >= lclusterSz) return false;
                uint64_t ls = (static_cast<uint64_t>(i) << lclusterBits) + lc[i].clusterOfs;
                if (ls >= inf.iSize) break;
                if (!spans.empty() && ls < spans.back().logStart) return false;

                uint32_t pBlks = 1u;
                if (i + 1u < totalLclusters && lc[i + 1u].type == 2u && (lc[i + 1u].delta0 & 0x0800u) != 0u) {
                    pBlks = lc[i + 1u].delta0 & 0x07FFu;
                    if (pBlks == 0u) return false;
                }

                PclusterSpan sp;
                sp.headLcn  = i;
                sp.endLcn   = totalLclusters - 1u;
                sp.type     = lc[i].type;
                sp.logStart = ls;
                sp.blkAddr  = lc[i].blkAddr;
                sp.physBlks = pBlks;
                if (!spans.empty()) {
                    spans.back().endLcn = i;
                }
                spans.push_back(sp);
            }

            if (spans.empty() || spans[0].logStart != 0ULL) return false;
            for (size_t s = 0; s < spans.size(); ++s) {
                spans[s].logEnd = (s + 1u < spans.size()) ? spans[s + 1u].logStart : inf.iSize;
                if (s + 1u == spans.size() && (hAdvise & 0x0008u) != 0u &&
                    (spans[s].blkAddr == 0u || spans[s].blkAddr == 0xFFFFFFFFu))
                {
                    spans[s].isInline = true;
                }
            }

            outBuf.reserve(static_cast<size_t>(inf.iSize));
            std::vector<uint8_t> pBuf;
            std::vector<uint8_t> dec;

            for (const auto& sp : spans) {
                if (sp.logEnd <= sp.logStart) continue;
                const size_t wantLen = static_cast<size_t>(sp.logEnd - sp.logStart);
                const size_t maxClusterOut = std::max<size_t>(
                    wantLen,
                    (sp.endLcn - sp.headLcn + 1u) * lclusterSz + lclusterSz);
                if (maxClusterOut > 64u * 1024u * 1024u) return false;

                if (sp.isInline) {
                    if (afterIdxOff >= partSize) return false;
                    size_t avail = static_cast<size_t>(std::min<uint64_t>(blkSz, partSize - afterIdxOff));
                    pBuf.resize(avail);
                    if (!readRel(afterIdxOff, pBuf.data(), avail)) return false;
                } else {
                    if (sp.blkAddr >= blocks) return false;
                    uint64_t pOff = static_cast<uint64_t>(sp.blkAddr) * blkSz;
                    uint64_t pLen64 = static_cast<uint64_t>(sp.physBlks) * blkSz;
                    if (pLen64 == 0 || pLen64 > 16ULL * 1024 * 1024) return false;
                    pBuf.resize(static_cast<size_t>(pLen64));
                    if (!readRel(pOff, pBuf.data(), pBuf.size())) return false;
                }

                if (sp.type == 0u) { // Z_EROFS_LCLUSTER_TYPE_PLAIN
                    if (wantLen > pBuf.size()) return false;
                    outBuf.insert(outBuf.end(), pBuf.data(), pBuf.data() + wantLen);
                } else { // HEAD1 (1) or HEAD2 (3)
                    const uint8_t alg = (sp.type == 3u) ? static_cast<uint8_t>((hAlgType >> 4) & 0x0Fu)
                                                        : static_cast<uint8_t>(hAlgType & 0x0Fu);
                    dec.clear();
                    if (alg == 0u) { // Z_EROFS_COMPRESSION_LZ4
                        size_t pad = 0;
                        while (pad < pBuf.size() && pBuf[pad] == 0u) ++pad;
                        if (pad >= pBuf.size()) return false;
                        const uint8_t* cSrc = pBuf.data() + pad;
                        const size_t cLen = pBuf.size() - pad;

                        bool okLz4 = ::Explorer2::Lz4::DecodeBlockAppend(cSrc, cLen, dec, 0, maxClusterOut, false) &&
                                     dec.size() >= wantLen;
                        if (!okLz4) {
                            okLz4 = ::Explorer2::Lz4::DecodeBlockExact(cSrc, cLen, wantLen, dec);
                        }
                        if (!okLz4) {
                            size_t alignedWant = (wantLen + lclusterSz - 1u) & ~(lclusterSz - 1u);
                            if (alignedWant <= maxClusterOut) {
                                okLz4 = ::Explorer2::Lz4::DecodeBlockExact(cSrc, cLen, alignedWant, dec) &&
                                        dec.size() >= wantLen;
                            }
                        }
                        if (!okLz4 || dec.size() < wantLen) {
                            outBuf.clear();
                            return false;
                        }
                        outBuf.insert(outBuf.end(), dec.data(), dec.data() + wantLen);
                    } else if (alg == 2u) { // Z_EROFS_COMPRESSION_DEFLATE
                        bool okDef = ZipDeflate::InflateRaw(pBuf.data(), pBuf.size(), dec, maxClusterOut) &&
                                     dec.size() >= wantLen;
                        if (!okDef) {
                            okDef = ZipDeflate::InflateZlib(pBuf.data(), pBuf.size(), dec, maxClusterOut) &&
                                    dec.size() >= wantLen;
                        }
                        if (!okDef && (featIncompat & 0x01u) != 0u) {
                            size_t pad = 0;
                            while (pad < pBuf.size() && pBuf[pad] == 0u) ++pad;
                            if (pad > 0 && pad < pBuf.size()) {
                                okDef = ZipDeflate::InflateRaw(pBuf.data() + pad, pBuf.size() - pad, dec, maxClusterOut) &&
                                        dec.size() >= wantLen;
                            }
                        }
                        if (!okDef || dec.size() < wantLen) {
                            outBuf.clear();
                            return false;
                        }
                        outBuf.insert(outBuf.end(), dec.data(), dec.data() + wantLen);
                    } else if (alg == 3u) { // Z_EROFS_COMPRESSION_ZSTD
                        size_t pad = 0;
                        while (pad + 4 <= pBuf.size() && pBuf[pad] == 0u && RdU32LE(pBuf.data() + pad) != 0xFD2FB528u) {
                            ++pad;
                        }
                        if (pad >= pBuf.size() ||
                            !StreamCodecs::DecompressZstdFrame(pBuf.data() + pad, pBuf.size() - pad, dec, maxClusterOut) ||
                            dec.size() < wantLen)
                        {
                            outBuf.clear();
                            return false;
                        }
                        outBuf.insert(outBuf.end(), dec.data(), dec.data() + wantLen);
                    } else if (alg == 1u) { // Z_EROFS_COMPRESSION_LZMA (MicroLZMA / LZMA1)
                        size_t pad = 0;
                        while (pad < pBuf.size() && pBuf[pad] == 0u) ++pad;
                        const uint8_t* ms = pBuf.data() + pad;
                        const size_t ml = pBuf.size() - pad;
                        bool okLzma = false;
                        if (ml >= 14) {
                            // Standard 13-byte LZMA header or EROFS MicroLZMA header
                            okLzma = StreamCodecs::DecompressLzma1(ms[0], RdU32LE(ms + 1), ms + 13, ml - 13, dec, wantLen, true, maxClusterOut) &&
                                     dec.size() >= wantLen;
                        }
                        if (!okLzma || dec.size() < wantLen) {
                            outBuf.clear();
                            return false;
                        }
                        outBuf.insert(outBuf.end(), dec.data(), dec.data() + wantLen);
                    } else {
                        outBuf.clear();
                        return false;
                    }
                }
            }
            return outBuf.size() == inf.iSize;
        }

        return false;
    };

    std::unordered_set<uint64_t> visitedNids;
    size_t totalListed = 0;
    bool extractedOk = false;

    auto walkErofsDir = [&](auto& self, uint64_t dirNid, const std::wstring& prefix, int depth) -> bool {
        if (depth > 32 || totalListed >= 50000) return true;
        if (!visitedNids.insert(dirNid).second) return true;

        ErofsInodeInfo dInf;
        if (!readErofsInode(dirNid, dInf)) return false;
        if ((dInf.iMode & 0170000u) != 0040000u || dInf.iSize < 12) return false;

        std::vector<uint8_t> dirBytes;
        if (!readErofsData(dInf, dirBytes) || dirBytes.size() < 12) return false;

        for (size_t blkOff = 0; blkOff < dirBytes.size(); blkOff += blkSz) {
            size_t chunkLen = std::min<size_t>(blkSz, dirBytes.size() - blkOff);
            if (chunkLen < 12) break;
            const uint8_t* blk = dirBytes.data() + blkOff;
            uint16_t firstNameOff = RdU16LE(blk + 8);
            if (firstNameOff < 12 || firstNameOff > chunkLen || (firstNameOff % 12u) != 0) break;
            size_t numDirents = firstNameOff / 12u;

            for (size_t d = 0; d < numDirents && totalListed < 50000; ++d) {
                uint64_t childNid = RdU64LE(blk + d * 12u + 0u);
                uint16_t nOff     = RdU16LE(blk + d * 12u + 8u);
                uint8_t  fType    = blk[d * 12u + 10u];
                uint16_t nextOff  = (d + 1 < numDirents)
                    ? RdU16LE(blk + (d + 1) * 12u + 8u)
                    : static_cast<uint16_t>(chunkLen);
                if (nOff >= nextOff || nextOff > chunkLen) continue;

                std::string dn(reinterpret_cast<const char*>(blk + nOff),
                               strnlen(reinterpret_cast<const char*>(blk + nOff), nextOff - nOff));
                if (dn.empty() || dn == "." || dn == "..") continue;

                std::wstring wName = PathUtils::Utf8ToWide(dn);
                std::wstring fullP = ArchivePath::Normalize(prefix.empty() ? wName : (prefix + L"/" + wName));
                if (fullP.empty()) continue;

                ErofsInodeInfo cInf;
                bool hasInode = readErofsInode(childNid, cInf);
                bool isDir = hasInode ? ((cInf.iMode & 0170000u) == 0040000u) : (fType == 2);

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
                    if (hasInode) {
                        self(self, childNid, fullP, depth + 1);
                        if (extractedOk) return true;
                    }
                    continue;
                }

                if (!hasInode) continue;

                if (extractTargetPath && outExtractedBytes) {
                    if (_wcsicmp(fullP.c_str(), wantNorm.c_str()) == 0) {
                        extractedOk = readErofsData(cInf, *outExtractedBytes);
                        return extractedOk;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = fullP;
                    item.name = ArchivePath::BaseName(fullP);
                    item.isDirectory = false;
                    item.uncompressedSize = cInf.iSize;
                    item.compressedSize = cInf.iSize;
                    if (directRawBuffer && cInf.dataLayout == 0 &&
                        static_cast<uint64_t>(cInf.rawBlkAddr) * blkSz <= partSize &&
                        cInf.iSize <= partSize - static_cast<uint64_t>(cInf.rawBlkAddr) * blkSz)
                    {
                        item.rawDataOffset = partBase + static_cast<uint64_t>(cInf.rawBlkAddr) * blkSz;
                        item.compressionMethod = 0;
                    } else if (directRawBuffer && cInf.dataLayout == 2 && cInf.iSize < blkSz &&
                               cInf.inodeOff + cInf.inodeHdrSz + cInf.xattrBytes <= partSize &&
                               cInf.iSize <= partSize - (cInf.inodeOff + cInf.inodeHdrSz + cInf.xattrBytes))
                    {
                        item.rawDataOffset = partBase + cInf.inodeOff + cInf.inodeHdrSz + cInf.xattrBytes;
                        item.compressionMethod = 0;
                    } else {
                        item.compressionMethod = 2; // Compressed / multi-block / virtual-disk EROFS
                    }
                    outInfo->entries.push_back(std::move(item));
                    ++totalListed;
                }
            }
        }
        return true;
    };

    const std::wstring startPrefix = ArchivePath::Normalize(prefixDir);
    if (!walkErofsDir(walkErofsDir, rootNid, startPrefix, 0)) {
        if (extractTargetPath) return false;
    }
    if (extractTargetPath) return extractedOk;
    if (!outInfo || outInfo->entries.empty()) return false;

    auto readPrintableVol = [&](size_t off) -> std::string {
        size_t len = strnlen(reinterpret_cast<const char*>(sb + off), 16);
        if (len == 0) return {};
        for (size_t i = 0; i < len; ++i) {
            uint8_t ch = sb[off + i];
            if (ch < 32 || ch > 126) return {};
        }
        return std::string(reinterpret_cast<const char*>(sb + off), len);
    };
    std::string volName = readPrintableVol(64);
    if (volName.empty()) volName = readPrintableVol(48);
    if (outInfo->formatName.empty()) {
        outInfo->formatName = L"Linux EROFS Image" + (volName.empty() ? L"" : (L" (" + PathUtils::Utf8ToWide(volName) + L")"));
    }
    outInfo->valid = true;
    return true;
}

inline bool ParseOrExtractLinuxErofs(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    // #32. Linux EROFS — Superblock magic 0xE0F5E1E2 at offset 1024
    if (size >= 1152 && RdU32LE(data + 1024) == 0xE0F5E1E2u) {
        auto rawRead = [data, size](uint64_t off, uint8_t* dst, size_t len) -> bool {
            if (off > size || len > size - static_cast<size_t>(off)) return false;
            std::memcpy(dst, data + static_cast<size_t>(off), len);
            return true;
        };
        return ParseOrExtractErofsOnReader(0, size, true, rawRead, L"", outInfo, extractTargetPath, outExtractedBytes);
    }

    return false;
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectLinuxErofs(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParseOrExtractLinuxErofs(data, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractLinuxErofsEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailUnixAndLegacyArc::ParseOrExtractLinuxErofs(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
