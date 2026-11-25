#pragma once
// UnityFsBundle.h — UnityFS AssetBundle archive reader & LZMA/LZ4 block extractor (.bundle, .unity3d — "UnityFS\0").

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailUnixAndLegacyArc {

// ============================================================================
// #22. UnityFS AssetBundle (.bundle / .unity3d — "UnityFS\0")
// ============================================================================
inline bool DecompressUnityBlock(
    const uint8_t* src,
    size_t compLen,
    size_t uncompLen,
    uint32_t compMode,
    std::vector<uint8_t>& out)
{
    if (uncompLen > 256 * 1024 * 1024) return false;
    if (compMode == 0) {
        if (compLen != uncompLen) return false;
        out.assign(src, src + compLen);
        return true;
    }
    if (compMode == 1) { // LZMA1: 5-byte props [propsByte:1][dictSize:4LE] + raw stream
        if (compLen < 5) return false;
        uint8_t propsByte = src[0];
        uint32_t dictSize = RdU32LE(src + 1);
        return StreamCodecs::DecompressLzma1(
            propsByte, dictSize, src + 5, compLen - 5, out,
            static_cast<uint64_t>(uncompLen), true, std::max<size_t>(uncompLen + 64, 1024));
    }
    if (compMode == 2 || compMode == 3) { // LZ4 / LZ4HC raw block
        return uncompLen <= 128u * 1024u * 1024u && ::Explorer2::Lz4::DecodeBlockExact(src, compLen, uncompLen, out);
    }
    return false;
}

inline bool ParseOrExtractUnityFsBundle(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    if (size < 32 || std::memcmp(data, "UnityFS\0", 8) != 0) return false;
    if (outInfo) outInfo->formatName = L"UnityFS AssetBundle";

    size_t pos = 8;
    if (pos + 4 > size) return false;
    uint32_t fmtVer = RdU32BE(data + pos); pos += 4;
    if (fmtVer == 0 || fmtVer > 32) return false;

    auto readCString = [&](bool& ok) -> std::string {
        size_t s = pos;
        while (pos < size && data[pos] != 0) ++pos;
        if (pos >= size) { ok = false; return {}; }
        std::string r(reinterpret_cast<const char*>(data + s), pos - s);
        ++pos;
        return r;
    };
    bool strOk = true;
    std::string playerVer = readCString(strOk);
    std::string engineVer = readCString(strOk);
    (void)playerVer;
    if (!strOk) return false;
    if (outInfo && !engineVer.empty()) {
        outInfo->formatName = L"UnityFS Bundle (" + PathUtils::Utf8ToWide(engineVer) + L")";
    }

    if (pos + 20 > size) return false;
    uint64_t bundleSz   = RdU64BE(data + pos); pos += 8;
    uint32_t compInfoSz = RdU32BE(data + pos); pos += 4;
    uint32_t uncompSz   = RdU32BE(data + pos); pos += 4;
    uint32_t flags      = RdU32BE(data + pos); pos += 4;

    if (fmtVer >= 7) {
        pos = (pos + 15u) & ~size_t(15u); // 16-byte alignment after 20-byte header in UnityFS v7+
    }

    if (compInfoSz == 0 || uncompSz < 24 || uncompSz > 64 * 1024 * 1024) return false;

    size_t infoPos = 0;
    size_t dataStart = 0;
    if ((flags & 0x80u) != 0) {
        size_t bundleEnd = (bundleSz > 0 && bundleSz <= size) ? static_cast<size_t>(bundleSz) : size;
        if (compInfoSz > bundleEnd || pos > bundleEnd - compInfoSz) return false;
        infoPos = bundleEnd - compInfoSz;
        dataStart = pos;
    } else {
        if (pos > size || compInfoSz > size - pos) return false;
        infoPos = pos;
        dataStart = pos + compInfoSz;
    }
    if ((flags & 0x200u) != 0) {
        dataStart = (dataStart + 15u) & ~size_t(15u);
    }
    if (infoPos + compInfoSz > size || dataStart > size) return false;

    std::vector<uint8_t> blocksInfo;
    uint32_t compMode = flags & 0x3Fu;
    if (!DecompressUnityBlock(data + infoPos, compInfoSz, uncompSz, compMode, blocksInfo) ||
        blocksInfo.size() != uncompSz || blocksInfo.size() < 24)
    {
        return false;
    }

    struct UnityStorageBlock {
        uint32_t uncompSize = 0;
        uint32_t compSize = 0;
        uint16_t blkFlags = 0;
        uint64_t uncompStart = 0;
        size_t   compFileOff = 0;
    };

    size_t bp = 16; // skip 16-byte uncompressedDataHash
    uint32_t blockCount = RdU32BE(blocksInfo.data() + bp); bp += 4;
    if (blockCount == 0 || blockCount > 500000 ||
        bp + static_cast<size_t>(blockCount) * 10u + 4u > blocksInfo.size())
    {
        return false;
    }

    std::vector<UnityStorageBlock> blocks;
    blocks.reserve(std::min<uint32_t>(blockCount, 16384u));
    uint64_t totalUncomp = 0;
    size_t curCompOff = dataStart;
    for (uint32_t b = 0; b < blockCount; ++b) {
        UnityStorageBlock sb;
        sb.uncompSize  = RdU32BE(blocksInfo.data() + bp + 0);
        sb.compSize    = RdU32BE(blocksInfo.data() + bp + 4);
        sb.blkFlags    = RdU16BE(blocksInfo.data() + bp + 8);
        bp += 10;
        if (curCompOff > size || sb.compSize > size - curCompOff) return false;
        sb.uncompStart = totalUncomp;
        sb.compFileOff = curCompOff;
        totalUncomp += sb.uncompSize;
        curCompOff += sb.compSize;
        blocks.push_back(sb);
    }

    uint32_t nodeCount = RdU32BE(blocksInfo.data() + bp); bp += 4;
    if (nodeCount == 0 || nodeCount > 100000) return false;

    std::wstring wantNorm = extractTargetPath ? ArchivePath::Normalize(*extractTargetPath) : std::wstring{};

    for (uint32_t n = 0; n < nodeCount && n < 32768u; ++n) {
        if (bp + 21 > blocksInfo.size()) return false;
        uint64_t nOff   = RdU64BE(blocksInfo.data() + bp + 0);
        uint64_t nSize  = RdU64BE(blocksInfo.data() + bp + 8);
        uint32_t nFlags = RdU32BE(blocksInfo.data() + bp + 16);
        (void)nFlags;
        bp += 20;

        size_t ns = bp;
        while (bp < blocksInfo.size() && blocksInfo[bp] != 0) ++bp;
        if (bp >= blocksInfo.size()) return false;
        std::string nodeName(reinterpret_cast<const char*>(blocksInfo.data() + ns), bp - ns);
        ++bp;

        if (nodeName.empty()) continue;
        if (nOff > totalUncomp || nSize > totalUncomp - nOff) return false;

        std::wstring normP = ArchivePath::Normalize(PathUtils::Utf8ToWide(nodeName));
        if (normP.empty()) continue;

        if (extractTargetPath && outExtractedBytes) {
            if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                if (nSize == 0) {
                    outExtractedBytes->clear();
                    return true;
                }
                if (nSize > 256 * 1024 * 1024) return false;
                outExtractedBytes->resize(static_cast<size_t>(nSize));
                uint64_t nEnd = nOff + nSize;
                uint64_t written = 0;
                std::vector<uint8_t> blkBuf;
                for (const auto& sb : blocks) {
                    if (sb.uncompSize == 0) continue;
                    uint64_t bStart = sb.uncompStart;
                    uint64_t bEnd   = bStart + sb.uncompSize;
                    if (bEnd <= nOff) continue;
                    if (bStart >= nEnd) break;

                    uint64_t ovStart = std::max(nOff, bStart);
                    uint64_t ovEnd   = std::min(nEnd, bEnd);
                    size_t copyLen   = static_cast<size_t>(ovEnd - ovStart);
                    size_t dstOff    = static_cast<size_t>(ovStart - nOff);
                    size_t srcInner  = static_cast<size_t>(ovStart - bStart);

                    uint32_t blkComp = sb.blkFlags & 0x3Fu;
                    if (blkComp == 0) {
                        if (sb.compSize != sb.uncompSize) return false;
                        std::memcpy(outExtractedBytes->data() + dstOff, data + sb.compFileOff + srcInner, copyLen);
                    } else {
                        if (!DecompressUnityBlock(data + sb.compFileOff, sb.compSize, sb.uncompSize, blkComp, blkBuf) ||
                            blkBuf.size() != sb.uncompSize)
                        {
                            return false;
                        }
                        std::memcpy(outExtractedBytes->data() + dstOff, blkBuf.data() + srcInner, copyLen);
                    }
                    written += copyLen;
                }
                return (written == nSize);
            }
            continue;
        }

        if (outInfo) {
            uint64_t nEnd = nOff + nSize;
            bool allRaw = true;
            uint16_t firstComp = 0;
            size_t rawStartOff = 0;
            bool sawFirst = false;
            uint64_t estCompSz = 0;
            for (const auto& sb : blocks) {
                if (sb.uncompSize == 0) continue;
                uint64_t bStart = sb.uncompStart;
                uint64_t bEnd   = bStart + sb.uncompSize;
                if (bEnd <= nOff && nSize > 0) continue;
                if (bStart > nEnd || (bStart == nEnd && nSize > 0)) break;
                uint32_t blkComp = sb.blkFlags & 0x3Fu;
                if (!sawFirst) {
                    sawFirst = true;
                    firstComp = static_cast<uint16_t>(blkComp);
                    if (blkComp == 0 && nOff >= bStart) {
                        rawStartOff = sb.compFileOff + static_cast<size_t>(nOff - bStart);
                    }
                }
                if (blkComp != 0) allRaw = false;
                uint64_t ov = (nSize == 0) ? 0ULL : (std::min(nEnd, bEnd) - std::max(nOff, bStart));
                estCompSz += (sb.uncompSize > 0) ? ((ov * sb.compSize) / sb.uncompSize) : 0ULL;
                if (nSize == 0) break;
            }

            ArchiveItemEntry item;
            item.pathInArchive = std::move(normP);
            item.name = ArchivePath::BaseName(item.pathInArchive);
            item.uncompressedSize = nSize;
            item.compressedSize = allRaw ? nSize : (estCompSz > 0 ? estCompSz : nSize);
            if (allRaw && sawFirst && rawStartOff + nSize <= size) {
                item.rawDataOffset = rawStartOff;
                item.compressionMethod = 0;
            } else {
                item.compressionMethod = (firstComp != 0) ? firstComp : 2;
            }
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseUnityFsBundle(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractUnityFsBundle(data, size, &outInfo, nullptr, nullptr);
}

} // namespace DetailUnixAndLegacyArc

inline bool InspectUnityFsBundle(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return DetailUnixAndLegacyArc::ParseUnityFsBundle(data, size, outInfo);
}

inline bool ExtractUnityFsBundleEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& want,
    std::vector<uint8_t>& outBytes)
{
    return DetailUnixAndLegacyArc::ParseOrExtractUnityFsBundle(data, size, nullptr, &want, &outBytes);
}

} // namespace Explorer2::Formats
