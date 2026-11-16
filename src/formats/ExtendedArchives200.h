#pragma once
// ExtendedArchives200.h — 20 Game Bundles, Linux/Container FS, Mobile Packages & Retro Archives (#21–#40)
// Covers:
//   Game Bundles (#21–#30): .unitypackage, .bundle/.unity3d, .pck, .rgss3a/.rgssad, .xp3, .mpq, .big, .grp, .pbo, .scs
//   FS & Containers (#31–#35): .squashfs/.snap/.appimage, .erofs, .cpio, .ar/.a/.lib, .xar/.pkg
//   Retro, Mobile & Appliances (#36–#40): .lzh/.lha, .arj, .ace, .ipa/.aab/.xapk, .ova

#include "../ArchiveExplorer.h"
#include "../ZipDeflate.h"
#include "../PathUtils.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace Explorer2::Formats {

namespace DetailArc200 {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) |
           (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | RdU32BE(p + 4);
}

inline std::wstring NormPath(std::wstring p) {
    for (wchar_t& ch : p) {
        if (ch == L'\\') ch = L'/';
    }
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    while (!p.empty() && p.back() == L'/') p.pop_back();
    while (p.rfind(L"./", 0) == 0) p.erase(0, 2);
    return p;
}

inline std::wstring BaseName(const std::wstring& p) {
    size_t s = p.find_last_of(L'/');
    return (s == std::wstring::npos) ? p : p.substr(s + 1);
}

// Fast native LZ4 raw block decompressor (exact known uncompressed length)
inline bool DecompressLz4RawBlock(const uint8_t* src, size_t srcLen, size_t uncompLen, std::vector<uint8_t>& out) {
    if (uncompLen > 128 * 1024 * 1024) return false;
    out.clear();
    out.reserve(uncompLen);
    size_t sp = 0;
    while (sp < srcLen && out.size() < uncompLen) {
        uint8_t token = src[sp++];
        size_t litLen = (token >> 4) & 0x0Fu;
        if (litLen == 15) {
            while (sp < srcLen) {
                uint8_t s = src[sp++];
                litLen += s;
                if (litLen > uncompLen) return false;
                if (s != 255) break;
            }
        }
        if (sp + litLen > srcLen || out.size() + litLen > uncompLen) return false;
        out.insert(out.end(), src + sp, src + sp + litLen);
        sp += litLen;
        if (sp >= srcLen || out.size() >= uncompLen) break;

        if (sp + 2 > srcLen) return false;
        uint16_t offset = RdU16LE(src + sp);
        sp += 2;
        if (offset == 0 || offset > out.size()) return false;

        size_t matchLen = (token & 0x0Fu) + 4;
        if ((token & 0x0Fu) == 15) {
            while (sp < srcLen) {
                uint8_t s = src[sp++];
                matchLen += s;
                if (matchLen > uncompLen) return false;
                if (s != 255) break;
            }
        }
        if (out.size() + matchLen > uncompLen) return false;
        size_t matchPos = out.size() - offset;
        for (size_t i = 0; i < matchLen; ++i) {
            out.push_back(out[matchPos + i]);
        }
    }
    return (out.size() == uncompLen);
}

// LZ4 raw block decompressor bounded by maxOut when exact uncompressed size is not stored in header (e.g. SquashFS LZ4)
inline bool DecompressLz4RawBlockBounded(const uint8_t* src, size_t srcLen, size_t maxOut, std::vector<uint8_t>& out) {
    out.clear();
    size_t sp = 0;
    while (sp < srcLen) {
        uint8_t token = src[sp++];
        size_t litLen = (token >> 4) & 0x0Fu;
        if (litLen == 15) {
            uint8_t s = 0;
            do {
                if (sp >= srcLen) return false;
                s = src[sp++];
                litLen += s;
                if (litLen > maxOut) return false;
            } while (s == 255);
        }
        if (sp + litLen > srcLen || out.size() + litLen > maxOut) return false;
        out.insert(out.end(), src + sp, src + sp + litLen);
        sp += litLen;
        if (sp == srcLen) return true;

        if (sp + 2 > srcLen) return false;
        uint16_t offset = RdU16LE(src + sp);
        sp += 2;
        if (offset == 0 || offset > out.size()) return false;

        size_t matchLen = (token & 0x0Fu) + 4;
        if ((token & 0x0Fu) == 15) {
            uint8_t s = 0;
            do {
                if (sp >= srcLen) return false;
                s = src[sp++];
                matchLen += s;
                if (matchLen > maxOut) return false;
            } while (s == 255);
        }
        if (out.size() + matchLen > maxOut) return false;
        for (size_t i = 0; i < matchLen; ++i) {
            out.push_back(out[out.size() - offset]);
        }
    }
    return true;
}

// ============================================================================
// #21. Unity Asset Package (.unitypackage — TAR/TAR.GZ with <GUID>/pathname -> <GUID>/asset)
// ============================================================================
inline bool ParseUnityPackageFromMemory(
    const uint8_t* data, size_t size,
    ArchiveSummaryInfo& outInfo,
    const std::wstring* extractTargetPath = nullptr,
    std::vector<uint8_t>* outExtractedBytes = nullptr)
{
    std::vector<uint8_t> tarStorage;
    const uint8_t* tarBuf = data;
    size_t tarLen = size;
    if (size >= 18 && data[0] == 0x1F && data[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(data, size, tarStorage, 256 * 1024 * 1024) || tarStorage.size() < 512) {
            return false;
        }
        tarBuf = tarStorage.data();
        tarLen = tarStorage.size();
    }
    if (tarLen < 512) return false;

    struct GuidSlot {
        std::wstring realPath;
        size_t assetOff = 0;
        uint64_t assetSize = 0;
        bool hasAsset = false;
        bool isDir = false;
    };
    std::unordered_map<std::string, GuidSlot> guidMap;

    size_t pos = 0;
    while (pos + 512 <= tarLen) {
        if (tarBuf[pos] == 0) break;
        std::string name(reinterpret_cast<const char*>(tarBuf + pos), strnlen(reinterpret_cast<const char*>(tarBuf + pos), 100));
        uint64_t fsize = std::strtoull(std::string(reinterpret_cast<const char*>(tarBuf + pos + 124), 12).c_str(), nullptr, 8);
        char typeFlag = static_cast<char>(tarBuf[pos + 156]);
        pos += 512;
        if (pos + fsize > tarLen) break;

        // Normalize leading "./"
        if (name.rfind("./", 0) == 0) name.erase(0, 2);
        size_t slash = name.find('/');
        if (slash != std::string::npos && typeFlag != '5') {
            std::string guid = name.substr(0, slash);
            std::string leaf = name.substr(slash + 1);
            if (leaf == "pathname" && fsize > 0 && fsize < 2048) {
                std::string rawP(reinterpret_cast<const char*>(tarBuf + pos), static_cast<size_t>(fsize));
                size_t nl = rawP.find_first_of("\r\n\0", 0, 3);
                if (nl != std::string::npos) rawP.resize(nl);
                guidMap[guid].realPath = NormPath(PathUtils::Utf8ToWide(rawP));
            } else if (leaf == "asset") {
                auto& slot = guidMap[guid];
                slot.assetOff = pos;
                slot.assetSize = fsize;
                slot.hasAsset = true;
            }
        }
        pos += static_cast<size_t>((fsize + 511ULL) & ~511ULL);
    }

    if (guidMap.empty()) return false;

    if (extractTargetPath && outExtractedBytes) {
        std::wstring want = NormPath(*extractTargetPath);
        for (const auto& kv : guidMap) {
            if (kv.second.hasAsset && _wcsicmp(kv.second.realPath.c_str(), want.c_str()) == 0) {
                outExtractedBytes->assign(
                    tarBuf + kv.second.assetOff,
                    tarBuf + kv.second.assetOff + static_cast<size_t>(kv.second.assetSize));
                return true;
            }
        }
        return false;
    }

    outInfo.formatName = L"Unity Asset Package (.unitypackage)";
    for (auto& kv : guidMap) {
        if (kv.second.realPath.empty()) continue;
        ArchiveItemEntry item;
        item.pathInArchive = kv.second.realPath;
        item.name = BaseName(item.pathInArchive);
        item.isDirectory = !kv.second.hasAsset;
        item.uncompressedSize = kv.second.assetSize;
        item.compressedSize = kv.second.assetSize;
        if (tarBuf == data && kv.second.hasAsset) {
            item.rawDataOffset = kv.second.assetOff;
            item.compressionMethod = 0;
        } else {
            item.compressionMethod = 8;
        }
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

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
        return DecompressLz4RawBlock(src, compLen, uncompLen, out);
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

    std::wstring wantNorm = extractTargetPath ? NormPath(*extractTargetPath) : std::wstring{};

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

        std::wstring normP = NormPath(PathUtils::Utf8ToWide(nodeName));
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
            item.name = BaseName(item.pathInArchive);
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

// ============================================================================
// #23. Godot Engine Resource Pack (.pck — "GDPC")
// ============================================================================
inline bool ParseGodotPck(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    // Magic "GDPC" can be at offset 0 or embedded at EOF of self-contained Godot binary
    if (size < 96 || std::memcmp(data, "GDPC", 4) != 0) return false;
    uint32_t packVer = RdU32LE(data + 4);
    uint32_t maj = RdU32LE(data + 8), minV = RdU32LE(data + 12), patch = RdU32LE(data + 16);
    outInfo.formatName = L"Godot Engine PCK (v" + std::to_wstring(maj) + L"." +
                         std::to_wstring(minV) + L"." + std::to_wstring(patch) + L")";

    size_t pos = 20;
    uint64_t fileBase = 0;
    if (packVer >= 2) {
        if (pos + 12 + 64 + 4 > size) return false;
        uint32_t packFlags = RdU32LE(data + pos); pos += 4;
        (void)packFlags;
        fileBase = RdU64LE(data + pos); pos += 8;
        pos += 64; // 16 reserved uint32s
    } else {
        pos += 64; // 16 reserved uint32s in PCK v0/v1
    }
    if (pos + 4 > size) return false;
    uint32_t fileCount = RdU32LE(data + pos); pos += 4;

    for (uint32_t i = 0; i < fileCount && pos + 4 <= size && i < 50000; ++i) {
        uint32_t pathLen = RdU32LE(data + pos); pos += 4;
        if (pathLen > 4096 || pos + pathLen + 32 > size) break;
        std::string rawP(reinterpret_cast<const char*>(data + pos), strnlen(reinterpret_cast<const char*>(data + pos), pathLen));
        pos += pathLen;
        if (rawP.rfind("res://", 0) == 0) rawP.erase(0, 6);

        uint64_t fOff  = RdU64LE(data + pos); pos += 8;
        uint64_t fSize = RdU64LE(data + pos); pos += 8;
        pos += 16; // MD5[16]
        uint32_t entryFlags = 0;
        if (packVer >= 2) {
            if (pos + 4 > size) break;
            entryFlags = RdU32LE(data + pos); pos += 4;
        }

        if (!rawP.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = NormPath(PathUtils::Utf8ToWide(rawP));
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = fileBase + fOff;
            item.compressionMethod = (entryFlags & 1u) ? 99 : 0; // 0 = stored raw!
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #24. RPG Maker Encrypted Archive (.rgss3a / .rgss2a / .rgssad — "RGSSAD\0")
// ============================================================================
inline void DecryptRgssStream(const uint8_t* src, size_t len, uint32_t subKey, std::vector<uint8_t>& outBytes) {
    outBytes.resize(len);
    uint32_t curKey = subKey;
    for (size_t i = 0; i < len; ++i) {
        outBytes[i] = static_cast<uint8_t>(src[i] ^ ((curKey >> (8u * (i & 3u))) & 0xFFu));
        if ((i & 3u) == 3u) {
            curKey = curKey * 7u + 3u;
        }
    }
}

inline bool ParseOrExtractRpgMakerRgss(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    if (size < 12 || std::memcmp(data, "RGSSAD\0", 7) != 0) return false;
    uint8_t ver = data[7];
    if (ver != 1 && ver != 2 && ver != 3) return false;
    if (outInfo) {
        outInfo->formatName = (ver == 3) ? L"RPG Maker VX Ace Archive (.rgss3a)" : L"RPG Maker Archive (.rgssad)";
    }

    std::wstring wantNorm = extractTargetPath ? NormPath(*extractTargetPath) : std::wstring{};

    if (ver == 3) {
        uint32_t masterKey = RdU32LE(data + 8) * 9u + 3u;
        size_t pos = 12;
        size_t count = 0;
        while (pos + 16 <= size && count < 30000) {
            uint32_t fOff    = RdU32LE(data + pos + 0) ^ masterKey;
            uint32_t fSize   = RdU32LE(data + pos + 4) ^ masterKey;
            uint32_t subKey  = RdU32LE(data + pos + 8) ^ masterKey;
            uint32_t nameLen = RdU32LE(data + pos + 12) ^ masterKey;
            pos += 16;
            if (fOff == 0) break;
            if (nameLen == 0 || nameLen > 1024 || pos + nameLen > size) break;
            if (static_cast<uint64_t>(fOff) + static_cast<uint64_t>(fSize) > size) break;

            std::string decName(nameLen, '\0');
            for (uint32_t i = 0; i < nameLen; ++i) {
                decName[i] = static_cast<char>(data[pos + i] ^ ((masterKey >> (8u * (i & 3u))) & 0xFFu));
            }
            pos += nameLen;

            std::wstring normP = NormPath(PathUtils::Utf8ToWide(decName));
            if (normP.empty()) continue;
            ++count;

            if (extractTargetPath && outExtractedBytes) {
                if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                    if (fSize > 256 * 1024 * 1024) return false;
                    DecryptRgssStream(data + fOff, fSize, subKey, *outExtractedBytes);
                    return true;
                }
                continue;
            }

            if (outInfo) {
                ArchiveItemEntry item;
                item.pathInArchive = std::move(normP);
                item.name = BaseName(item.pathInArchive);
                item.uncompressedSize = fSize;
                item.compressedSize = fSize;
                item.rawDataOffset = fOff;
                item.compressionMethod = 99; // XOR stream cipher
                outInfo->entries.push_back(std::move(item));
            }
        }
    } else {
        uint32_t key = 0xDEADCAFEu;
        size_t pos = 8;
        size_t count = 0;
        while (pos + 4 <= size && count < 30000) {
            uint32_t nameLen = RdU32LE(data + pos) ^ key;
            key = key * 7u + 3u;
            pos += 4;
            if (nameLen == 0 || nameLen > 1024 || pos + nameLen + 4 > size) break;
            std::string decName(nameLen, '\0');
            for (uint32_t i = 0; i < nameLen; ++i) {
                decName[i] = static_cast<char>(data[pos + i] ^ (key & 0xFFu));
                key = key * 7u + 3u;
            }
            pos += nameLen;
            uint32_t fSize = RdU32LE(data + pos) ^ key;
            key = key * 7u + 3u;
            pos += 4;
            if (pos + fSize > size) break;

            uint32_t subKey = key; // Initial per-file stream key in RGSS v1/v2
            std::wstring normP = NormPath(PathUtils::Utf8ToWide(decName));
            if (!normP.empty()) {
                ++count;
                if (extractTargetPath && outExtractedBytes) {
                    if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                        if (fSize > 256 * 1024 * 1024) return false;
                        DecryptRgssStream(data + pos, fSize, subKey, *outExtractedBytes);
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = std::move(normP);
                    item.name = BaseName(item.pathInArchive);
                    item.uncompressedSize = fSize;
                    item.compressedSize = fSize;
                    item.rawDataOffset = pos;
                    item.compressionMethod = 99;
                    outInfo->entries.push_back(std::move(item));
                }
            }
            pos += fSize;
        }
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseRpgMakerRgss(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractRpgMakerRgss(data, size, &outInfo, nullptr, nullptr);
}

// ============================================================================
// #25. KiriKiri Visual Novel Archive (.xp3 — "XP3\r\n \n\x1A\x8B\x67\x01")
// ============================================================================
inline bool ParseOrExtractKiriKiriXp3(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    static const uint8_t kXp3Sig[11] = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
    if (size < 28 || std::memcmp(data, kXp3Sig, 11) != 0) return false;
    if (outInfo) outInfo->formatName = L"KiriKiri XP3 Archive (.xp3)";

    uint64_t idxOff = RdU64LE(data + 11);
    if (idxOff + 9 <= size && data[idxOff] == 0x80) {
        // v2 extended index header: [0x80:1][hdrSize:8][minorVer:4][realIdxOff:8]
        if (idxOff + 21 <= size) idxOff = RdU64LE(data + idxOff + 13);
    }
    if (idxOff + 9 > size) return false;

    uint8_t idxFlags = data[idxOff];
    std::vector<uint8_t> idxBuf;
    const uint8_t* idxPtr = nullptr;
    size_t idxLen = 0;

    if ((idxFlags & 0x07) == 1) {
        if (idxOff + 17 > size) return false;
        uint64_t compSz   = RdU64LE(data + idxOff + 1);
        uint64_t uncompSz = RdU64LE(data + idxOff + 9);
        if (idxOff + 17 + compSz > size || uncompSz > 64 * 1024 * 1024) return false;
        if (!ZipDeflate::InflateZlib(data + idxOff + 17, static_cast<size_t>(compSz), idxBuf, static_cast<size_t>(uncompSz) + 64)) {
            return false;
        }
        idxPtr = idxBuf.data();
        idxLen = idxBuf.size();
    } else {
        uint64_t rawSz = RdU64LE(data + idxOff + 1);
        if (idxOff + 9 + rawSz > size) return false;
        idxPtr = data + idxOff + 9;
        idxLen = static_cast<size_t>(rawSz);
    }

    struct Xp3Seg {
        uint32_t flags = 0;
        uint64_t start = 0;
        uint64_t origSz = 0;
        uint64_t compSz = 0;
    };

    std::wstring wantNorm = extractTargetPath ? NormPath(*extractTargetPath) : std::wstring{};
    size_t p = 0;
    size_t count = 0;
    while (p + 12 <= idxLen && count < 40000) {
        uint64_t chunkSz = RdU64LE(idxPtr + p + 4);
        if (chunkSz > idxLen - p - 12) break;
        if (std::memcmp(idxPtr + p, "File", 4) == 0) {
            size_t sp = p + 12, send = p + 12 + static_cast<size_t>(chunkSz);
            std::wstring fname;
            uint64_t origSz = 0, compSz = 0, rawStart = 0;
            bool segCompressed = false;
            std::vector<Xp3Seg> segs;
            while (sp + 12 <= send) {
                uint64_t subSz = RdU64LE(idxPtr + sp + 4);
                if (subSz > send - sp - 12) break;
                const uint8_t* sub = idxPtr + sp + 12;
                if (std::memcmp(idxPtr + sp, "info", 4) == 0 && subSz >= 22) {
                    origSz = RdU64LE(sub + 4);
                    compSz = RdU64LE(sub + 12);
                    uint16_t nChars = RdU16LE(sub + 20);
                    if (22 + static_cast<size_t>(nChars) * 2 <= subSz) {
                        fname.resize(nChars);
                        for (uint16_t c = 0; c < nChars; ++c) {
                            fname[c] = static_cast<wchar_t>(RdU16LE(sub + 22 + c * 2));
                        }
                    }
                } else if (std::memcmp(idxPtr + sp, "segm", 4) == 0 && subSz >= 28) {
                    size_t numSegs = static_cast<size_t>(subSz / 28);
                    for (size_t s = 0; s < numSegs; ++s) {
                        const uint8_t* se = sub + s * 28;
                        Xp3Seg seg;
                        seg.flags  = RdU32LE(se + 0);
                        seg.start  = RdU64LE(se + 4);
                        seg.origSz = RdU64LE(se + 12);
                        seg.compSz = RdU64LE(se + 20);
                        if (s == 0) {
                            rawStart = seg.start;
                            if (origSz == 0) origSz = seg.origSz;
                            if (compSz == 0) compSz = seg.compSz;
                        }
                        if ((seg.flags & 0x07u) != 0 || segs.size() > 0) {
                            if ((seg.flags & 0x07u) != 0) segCompressed = true;
                        }
                        segs.push_back(seg);
                    }
                    if (segs.size() > 1) segCompressed = true;
                }
                sp += 12 + static_cast<size_t>(subSz);
            }
            std::wstring normP = NormPath(fname);
            if (!normP.empty()) {
                ++count;
                if (extractTargetPath && outExtractedBytes) {
                    if (_wcsicmp(normP.c_str(), wantNorm.c_str()) == 0) {
                        if (segs.empty() || origSz > 256 * 1024 * 1024) return false;
                        outExtractedBytes->clear();
                        outExtractedBytes->reserve(static_cast<size_t>(origSz));
                        std::vector<uint8_t> segOut;
                        for (const auto& seg : segs) {
                            if ((seg.flags & 0x07u) != 0) {
                                if (seg.start + seg.compSz > size || seg.origSz > 256 * 1024 * 1024) return false;
                                if (!ZipDeflate::InflateZlib(data + seg.start, static_cast<size_t>(seg.compSz), segOut, static_cast<size_t>(seg.origSz) + 64) &&
                                    !ZipDeflate::InflateRaw(data + seg.start, static_cast<size_t>(seg.compSz), segOut, static_cast<size_t>(seg.origSz) + 64))
                                {
                                    return false;
                                }
                                outExtractedBytes->insert(outExtractedBytes->end(), segOut.begin(), segOut.end());
                            } else {
                                if (seg.start + seg.origSz > size) return false;
                                outExtractedBytes->insert(outExtractedBytes->end(), data + seg.start, data + seg.start + static_cast<size_t>(seg.origSz));
                            }
                        }
                        return true;
                    }
                } else if (outInfo) {
                    ArchiveItemEntry item;
                    item.pathInArchive = std::move(normP);
                    item.name = BaseName(item.pathInArchive);
                    item.uncompressedSize = origSz;
                    item.compressedSize = compSz;
                    item.rawDataOffset = rawStart;
                    item.compressionMethod = segCompressed ? 8 : 0;
                    outInfo->entries.push_back(std::move(item));
                }
            }
        }
        p += 12 + static_cast<size_t>(chunkSz);
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseKiriKiriXp3(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractKiriKiriXp3(data, size, &outInfo, nullptr, nullptr);
}

// ============================================================================
// #26. Blizzard MoPaQ Archive (.mpq — "MPQ\x1A" / "MPQ\x1B")
// ============================================================================
inline bool ParseOrExtractBlizzardMpq(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    if (size < 32) return false;
    size_t mpqOff = SIZE_MAX;
    if (std::memcmp(data, "MPQ\x1A", 4) == 0) {
        mpqOff = 0;
    } else if (std::memcmp(data, "MPQ\x1B", 4) == 0 && size >= 16) {
        uint32_t hdrOff = RdU32LE(data + 8);
        if (static_cast<uint64_t>(hdrOff) + 32ULL <= size && std::memcmp(data + hdrOff, "MPQ\x1A", 4) == 0) {
            mpqOff = hdrOff;
        }
    }
    if (mpqOff == SIZE_MAX || mpqOff + 32 > size) return false;
    if (outInfo) outInfo->formatName = L"Blizzard MoPaQ Archive (.mpq)";

    const uint8_t* mpqHdr   = data + mpqOff;
    uint32_t hdrSz          = RdU32LE(mpqHdr + 4);
    uint16_t secShift       = RdU16LE(mpqHdr + 14);
    uint32_t hashTblRelPos  = RdU32LE(mpqHdr + 16);
    uint32_t blockTblRelPos = RdU32LE(mpqHdr + 20);
    uint32_t hashTblCount   = RdU32LE(mpqHdr + 24);
    uint32_t blockCount     = RdU32LE(mpqHdr + 28);

    if (hdrSz < 32 || secShift > 15 || blockCount == 0 || blockCount > 100000) return false;
    uint64_t absBlockTblPos = mpqOff + static_cast<uint64_t>(blockTblRelPos);
    if (absBlockTblPos + static_cast<uint64_t>(blockCount) * 16ULL > size) return false;
    const uint32_t sectorSize = 512u << secShift;

    // Build Storm MPQ encryption & hash lookup table
    uint32_t cryptTbl[0x500] = {};
    uint32_t seed = 0x00100001u;
    for (uint32_t idx1 = 0; idx1 < 0x100u; ++idx1) {
        uint32_t idx2 = idx1;
        for (int i = 0; i < 5; ++i, idx2 += 0x100u) {
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t1 = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t2 = (seed & 0xFFFFu);
            cryptTbl[idx2] = t1 | t2;
        }
    }

    auto mpqHashEx = [&](std::string_view sv, uint32_t hashType, bool slashToBackslash) -> uint32_t {
        uint32_t s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
        for (char rawCh : sv) {
            char c = (slashToBackslash && rawCh == '/') ? '\\' : rawCh;
            uint8_t ch = static_cast<uint8_t>(toupper(static_cast<unsigned char>(c)));
            s1 = cryptTbl[hashType + ch] ^ (s1 + s2);
            s2 = ch + s1 + s2 + (s2 << 5) + 3u;
        }
        return s1;
    };

    auto mpqDecryptWords = [&](uint32_t* words, size_t dwordCount, uint32_t k1) {
        uint32_t k2 = 0xEEEEEEEEu;
        for (size_t i = 0; i < dwordCount; ++i) {
            k2 += cryptTbl[0x400u + (k1 & 0xFFu)];
            uint32_t plain = words[i] ^ (k1 + k2);
            k1 = ((~k1 << 0x15) + 0x11111111u) | (k1 >> 0x0B);
            k2 = plain + k2 + (k2 << 5) + 3u;
            words[i] = plain;
        }
    };

    auto mpqDecryptBytes = [&](uint8_t* buf, size_t byteLen, uint32_t k1) {
        uint32_t k2 = 0xEEEEEEEEu;
        size_t dwords = byteLen / 4u;
        for (size_t i = 0; i < dwords; ++i) {
            k2 += cryptTbl[0x400u + (k1 & 0xFFu)];
            uint32_t enc = RdU32LE(buf + i * 4u);
            uint32_t plain = enc ^ (k1 + k2);
            k1 = ((~k1 << 0x15) + 0x11111111u) | (k1 >> 0x0B);
            k2 = plain + k2 + (k2 << 5) + 3u;
            buf[i * 4u + 0] = static_cast<uint8_t>(plain & 0xFFu);
            buf[i * 4u + 1] = static_cast<uint8_t>((plain >> 8) & 0xFFu);
            buf[i * 4u + 2] = static_cast<uint8_t>((plain >> 16) & 0xFFu);
            buf[i * 4u + 3] = static_cast<uint8_t>((plain >> 24) & 0xFFu);
        }
    };

    // Read & validate/decrypt Block Table (detecting unencrypted synthetic vs Storm-encrypted "(block table)")
    std::vector<uint32_t> rawBt(static_cast<size_t>(blockCount) * 4u);
    for (size_t i = 0; i < rawBt.size(); ++i) {
        rawBt[i] = RdU32LE(data + static_cast<size_t>(absBlockTblPos) + i * 4u);
    }
    std::vector<uint32_t> decBt = rawBt;
    mpqDecryptWords(decBt.data(), decBt.size(), mpqHashEx("(block table)", 0x300u, false));

    auto isBlockTableValid = [&](const std::vector<uint32_t>& bt) -> bool {
        uint32_t validExisting = 0;
        for (uint32_t i = 0; i < blockCount; ++i) {
            uint32_t fRel = bt[i * 4u + 0];
            uint32_t cSz  = bt[i * 4u + 1];
            uint32_t fSz  = bt[i * 4u + 2];
            uint32_t flg  = bt[i * 4u + 3];
            if ((flg & 0x80000000u) == 0) continue;
            if (mpqOff + static_cast<uint64_t>(fRel) + static_cast<uint64_t>(cSz) > size) return false;
            if (fSz > 1024ULL * 1024ULL * 1024ULL) return false;
            ++validExisting;
        }
        return validExisting > 0;
    };

    bool decBtOk = isBlockTableValid(decBt);
    bool rawBtOk = isBlockTableValid(rawBt);
    if (!decBtOk && !rawBtOk) return false;
    const std::vector<uint32_t>& btWords = decBtOk ? decBt : rawBt;

    // Read & validate/decrypt Hash Table if present
    uint64_t absHashTblPos = mpqOff + static_cast<uint64_t>(hashTblRelPos);
    bool hasHashTable = (hashTblCount > 0 && hashTblCount <= 262144u &&
                         absHashTblPos + static_cast<uint64_t>(hashTblCount) * 16ULL <= size);
    std::vector<uint32_t> htWords;
    if (hasHashTable) {
        std::vector<uint32_t> rawHt(static_cast<size_t>(hashTblCount) * 4u);
        for (size_t i = 0; i < rawHt.size(); ++i) {
            rawHt[i] = RdU32LE(data + static_cast<size_t>(absHashTblPos) + i * 4u);
        }
        std::vector<uint32_t> decHt = rawHt;
        mpqDecryptWords(decHt.data(), decHt.size(), mpqHashEx("(hash table)", 0x300u, false));

        auto isHashTableValid = [&](const std::vector<uint32_t>& ht, uint32_t& outUsed) -> bool {
            outUsed = 0;
            for (uint32_t i = 0; i < hashTblCount; ++i) {
                uint32_t bIdx = ht[i * 4u + 3u];
                if (bIdx == 0xFFFFFFFFu || bIdx == 0xFFFFFFFEu) continue;
                if (bIdx >= blockCount) return false;
                ++outUsed;
            }
            return true;
        };
        uint32_t decUsed = 0, rawUsed = 0;
        bool decHtOk = isHashTableValid(decHt, decUsed);
        bool rawHtOk = isHashTableValid(rawHt, rawUsed);
        if (decHtOk && (decUsed > 0 || !rawHtOk)) {
            htWords = std::move(decHt);
        } else if (rawHtOk) {
            htWords = std::move(rawHt);
        } else {
            hasHashTable = false;
        }
    }

    auto lookupMpqHash = [&](std::string_view filePath) -> int32_t {
        if (!hasHashTable || htWords.empty()) return -1;
        for (int pass = 0; pass < 2; ++pass) {
            bool normSlash = (pass == 0);
            if (pass == 1 && filePath.find('/') == std::string_view::npos) break;
            uint32_t startIdx = mpqHashEx(filePath, 0x000u, normSlash) % hashTblCount;
            uint32_t nameA    = mpqHashEx(filePath, 0x100u, normSlash);
            uint32_t nameB    = mpqHashEx(filePath, 0x200u, normSlash);
            for (uint32_t step = 0; step < hashTblCount; ++step) {
                uint32_t idx = (startIdx + step) % hashTblCount;
                uint32_t bIdx = htWords[idx * 4u + 3u];
                if (bIdx == 0xFFFFFFFFu) break;
                if (bIdx == 0xFFFFFFFEu) continue;
                if (htWords[idx * 4u + 0u] == nameA && htWords[idx * 4u + 1u] == nameB && bIdx < blockCount) {
                    return static_cast<int32_t>(bIdx);
                }
            }
        }
        return -1;
    };

    constexpr uint32_t kMpqImplode    = 0x00000100u;
    constexpr uint32_t kMpqCompress   = 0x00000200u;
    constexpr uint32_t kMpqEncrypted  = 0x00010000u;
    constexpr uint32_t kMpqFixKey     = 0x00020000u;
    constexpr uint32_t kMpqSingleUnit = 0x01000000u;
    constexpr uint32_t kMpqDeleteMark = 0x02000000u;
    constexpr uint32_t kMpqSectorCrc  = 0x04000000u;
    constexpr uint32_t kMpqExists     = 0x80000000u;

    auto decompressMpqSector = [&](const uint8_t* secPtr, size_t secCompLen, size_t secUncompLen, uint32_t flg, std::vector<uint8_t>& secOut) -> bool {
        if (secCompLen == secUncompLen || (flg & (kMpqCompress | kMpqImplode)) == 0) {
            if (secCompLen < secUncompLen) return false;
            secOut.assign(secPtr, secPtr + secUncompLen);
            return true;
        }
        if ((flg & kMpqCompress) == 0 || secCompLen < 2) return false;
        uint8_t compMask = secPtr[0];
        const uint8_t* payload = secPtr + 1;
        size_t payloadLen = secCompLen - 1;
        if (compMask == 0x00u) {
            if (payloadLen != secUncompLen) return false;
            secOut.assign(payload, payload + payloadLen);
            return true;
        }
        if ((compMask & 0x02u) != 0) { // Zlib
            if (!ZipDeflate::InflateZlib(payload, payloadLen, secOut, secUncompLen + 64) &&
                !ZipDeflate::InflateRaw(payload, payloadLen, secOut, secUncompLen + 64))
            {
                return false;
            }
            return secOut.size() == secUncompLen;
        }
        if ((compMask & 0x10u) != 0) { // BZip2
            if (!StreamCodecs::DecompressBzip2Stream(payload, payloadLen, secOut, secUncompLen + 64)) return false;
            return secOut.size() == secUncompLen;
        }
        if (compMask == 0x12u) { // LZMA
            if (payloadLen >= 5 &&
                StreamCodecs::DecompressLzma1(payload[0], RdU32LE(payload + 1), payload + 5, payloadLen - 5, secOut, secUncompLen, true, secUncompLen + 64) &&
                secOut.size() == secUncompLen)
            {
                return true;
            }
            if (payloadLen >= 6 && payload[0] == 0 &&
                StreamCodecs::DecompressLzma1(payload[1], RdU32LE(payload + 2), payload + 6, payloadLen - 6, secOut, secUncompLen, true, secUncompLen + 64) &&
                secOut.size() == secUncompLen)
            {
                return true;
            }
            return false;
        }
        return false;
    };

    auto extractMpqBlock = [&](uint32_t blockIdx, std::string_view fileNameForKey, std::vector<uint8_t>& outBuf) -> bool {
        if (blockIdx >= blockCount) return false;
        uint32_t fRelPos = btWords[blockIdx * 4u + 0u];
        uint32_t cSz     = btWords[blockIdx * 4u + 1u];
        uint32_t fSz     = btWords[blockIdx * 4u + 2u];
        uint32_t flg     = btWords[blockIdx * 4u + 3u];
        if ((flg & kMpqExists) == 0 || (flg & kMpqDeleteMark) != 0) return false;
        uint64_t absFilePos = mpqOff + static_cast<uint64_t>(fRelPos);
        if (absFilePos + static_cast<uint64_t>(cSz) > size || fSz > 256 * 1024 * 1024) return false;
        if (fSz == 0) {
            outBuf.clear();
            return true;
        }

        uint32_t fileKey = 0;
        if ((flg & kMpqEncrypted) != 0) {
            if (fileNameForKey.empty()) return false;
            size_t lastSep = fileNameForKey.find_last_of("\\/");
            std::string_view plainBase = (lastSep == std::string_view::npos) ? fileNameForKey : fileNameForKey.substr(lastSep + 1);
            fileKey = mpqHashEx(plainBase, 0x300u, false);
            if ((flg & kMpqFixKey) != 0) {
                fileKey = (fileKey + fRelPos) ^ fSz;
            }
        }

        const uint8_t* fData = data + static_cast<size_t>(absFilePos);
        if ((flg & kMpqSingleUnit) != 0) {
            std::vector<uint8_t> rawUnit(fData, fData + cSz);
            if ((flg & kMpqEncrypted) != 0) {
                mpqDecryptBytes(rawUnit.data(), rawUnit.size(), fileKey);
            }
            return decompressMpqSector(rawUnit.data(), rawUnit.size(), fSz, flg, outBuf);
        }

        if ((flg & (kMpqCompress | kMpqImplode)) == 0) {
            if (cSz < fSz) return false;
            outBuf.assign(fData, fData + fSz);
            if ((flg & kMpqEncrypted) != 0) {
                uint32_t numSecs = (fSz + sectorSize - 1u) / sectorSize;
                for (uint32_t s = 0; s < numSecs; ++s) {
                    size_t sOff = static_cast<size_t>(s) * sectorSize;
                    size_t sLen = std::min<size_t>(sectorSize, fSz - sOff);
                    mpqDecryptBytes(outBuf.data() + sOff, sLen, fileKey + s);
                }
            }
            return true;
        }

        uint32_t numSectors = (fSz + sectorSize - 1u) / sectorSize;
        uint32_t numOffsets = numSectors + 1u + ((flg & kMpqSectorCrc) ? 1u : 0u);
        size_t tblBytes = static_cast<size_t>(numOffsets) * 4u;
        if (tblBytes > cSz) return false;

        std::vector<uint8_t> tblBuf(fData, fData + tblBytes);
        if ((flg & kMpqEncrypted) != 0) {
            mpqDecryptBytes(tblBuf.data(), tblBytes, fileKey - 1u);
        }
        std::vector<uint32_t> secOffsets(numSectors + 1u);
        for (uint32_t s = 0; s <= numSectors; ++s) {
            secOffsets[s] = RdU32LE(tblBuf.data() + static_cast<size_t>(s) * 4u);
        }
        size_t minExpectedOff0 = static_cast<size_t>(numSectors + 1u) * 4u;
        if (secOffsets[0] != tblBytes && secOffsets[0] != minExpectedOff0) return false;

        outBuf.clear();
        outBuf.reserve(fSz);
        std::vector<uint8_t> secRaw, secDec;
        for (uint32_t s = 0; s < numSectors; ++s) {
            uint32_t sStart = secOffsets[s];
            uint32_t sEnd   = secOffsets[s + 1u];
            if (sStart > sEnd || sEnd > cSz) return false;
            size_t sCompLen   = sEnd - sStart;
            size_t sUncompLen = std::min<size_t>(sectorSize, fSz - outBuf.size());
            secRaw.assign(fData + sStart, fData + sEnd);
            if ((flg & kMpqEncrypted) != 0) {
                mpqDecryptBytes(secRaw.data(), secRaw.size(), fileKey + s);
            }
            if (!decompressMpqSector(secRaw.data(), sCompLen, sUncompLen, flg, secDec)) return false;
            outBuf.insert(outBuf.end(), secDec.begin(), secDec.end());
        }
        return outBuf.size() == fSz;
    };

    int32_t lfBlock   = lookupMpqHash("(listfile)");
    int32_t attrBlock = lookupMpqHash("(attributes)");
    int32_t sigBlock  = lookupMpqHash("(signature)");
    int32_t udatBlock = lookupMpqHash("(user data)");

    std::vector<std::string> listNames;
    auto parseListfileBytes = [&](std::string_view sv) {
        size_t lp = 0;
        while (lp < sv.size() && listNames.size() < 100000) {
            size_t le = sv.find_first_of("\r\n;", lp);
            if (le == std::string_view::npos) le = sv.size();
            std::string_view line = sv.substr(lp, le - lp);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
            if (!line.empty()) listNames.emplace_back(line);
            lp = le + 1;
        }
    };

    if (lfBlock >= 0) {
        std::vector<uint8_t> lfBytes;
        if (extractMpqBlock(static_cast<uint32_t>(lfBlock), "(listfile)", lfBytes) && !lfBytes.empty()) {
            parseListfileBytes(std::string_view(reinterpret_cast<const char*>(lfBytes.data()), lfBytes.size()));
        }
    } else if (!hasHashTable) {
        for (uint32_t i = 0; i < blockCount; ++i) {
            uint32_t fRel = btWords[i * 4u + 0u];
            uint32_t cSz  = btWords[i * 4u + 1u];
            uint32_t fSz  = btWords[i * 4u + 2u];
            uint32_t flg  = btWords[i * 4u + 3u];
            uint64_t absP = mpqOff + static_cast<uint64_t>(fRel);
            if ((flg & kMpqExists) && cSz == fSz && fSz > 4 && fSz < 1024 * 1024 && absP + fSz <= size) {
                std::string_view sv(reinterpret_cast<const char*>(data + static_cast<size_t>(absP)), fSz);
                if ((sv.find('\\') != std::string_view::npos || sv.find('.') != std::string_view::npos) &&
                    sv.find('\n') != std::string_view::npos)
                {
                    bool printable = true;
                    for (size_t c = 0; c < std::min<size_t>(fSz, 64); ++c) {
                        uint8_t b = static_cast<uint8_t>(sv[c]);
                        if (b < 9 || (b > 13 && b < 32) || b > 126) { printable = false; break; }
                    }
                    if (printable) {
                        parseListfileBytes(sv);
                        lfBlock = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
        }
    }

    std::vector<std::string> blockToName(blockCount);
    if (hasHashTable) {
        for (const std::string& nm : listNames) {
            if (_stricmp(nm.c_str(), "(listfile)") == 0 ||
                _stricmp(nm.c_str(), "(attributes)") == 0 ||
                _stricmp(nm.c_str(), "(signature)") == 0 ||
                _stricmp(nm.c_str(), "(user data)") == 0)
            {
                continue;
            }
            int32_t bIdx = lookupMpqHash(nm);
            if (bIdx >= 0 && static_cast<uint32_t>(bIdx) < blockCount && blockToName[static_cast<size_t>(bIdx)].empty()) {
                blockToName[static_cast<size_t>(bIdx)] = nm;
            }
        }
    } else {
        size_t nameCursor = 0;
        for (uint32_t i = 0; i < blockCount && nameCursor < listNames.size(); ++i) {
            if (static_cast<int32_t>(i) == lfBlock) continue;
            blockToName[i] = listNames[nameCursor++];
        }
    }

    std::wstring wantNorm = extractTargetPath ? NormPath(*extractTargetPath) : std::wstring{};

    for (uint32_t i = 0; i < blockCount; ++i) {
        if (static_cast<int32_t>(i) == lfBlock ||
            static_cast<int32_t>(i) == attrBlock ||
            static_cast<int32_t>(i) == sigBlock ||
            static_cast<int32_t>(i) == udatBlock)
        {
            continue;
        }
        uint32_t fRelPos = btWords[i * 4u + 0u];
        uint32_t cSz     = btWords[i * 4u + 1u];
        uint32_t fSz     = btWords[i * 4u + 2u];
        uint32_t flg     = btWords[i * 4u + 3u];
        uint64_t absP    = mpqOff + static_cast<uint64_t>(fRelPos);
        if ((flg & kMpqExists) == 0 || (flg & kMpqDeleteMark) != 0 || absP + cSz > size) continue;

        std::wstring displayPath;
        if (!blockToName[i].empty()) {
            displayPath = NormPath(PathUtils::Utf8ToWide(blockToName[i]));
        } else {
            wchar_t nb[64] = {};
            swprintf_s(nb, L"Files/Block_%04u.dat", i);
            displayPath = nb;
        }

        if (extractTargetPath && outExtractedBytes) {
            if (_wcsicmp(displayPath.c_str(), wantNorm.c_str()) == 0) {
                return extractMpqBlock(i, blockToName[i], *outExtractedBytes);
            }
            continue;
        }

        if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive = std::move(displayPath);
            item.name = BaseName(item.pathInArchive);
            item.compressedSize = cSz;
            item.uncompressedSize = fSz;
            item.rawDataOffset = absP;
            item.compressionMethod = (((flg & (kMpqCompress | kMpqImplode | kMpqEncrypted)) == 0) && cSz == fSz) ? 0 : 8;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetPath) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

inline bool ParseBlizzardMpq(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractBlizzardMpq(data, size, &outInfo, nullptr, nullptr);
}

// ============================================================================
// #27. EA Games BIGF / BIG4 Archive (.big — Raw + EA RefPack 0x10FB Decompression)
// ============================================================================
inline bool PeekEaRefPackHeader(
    const uint8_t* src,
    size_t srcLen,
    uint64_t& outUncompressedSize,
    size_t& outHeaderBytes)
{
    if (srcLen < 6) return false;
    uint16_t sig = RdU16BE(src);
    if ((sig & 0xFEFFu) != 0x10FBu) return false; // 0x10FB / 0x11FB / 0x90FB / 0x91FB

    size_t szBytes    = ((sig & 0x8000u) != 0) ? 4u : 3u;
    bool hasCompField = ((sig & 0x0100u) != 0);
    size_t hdrLen     = 2u + (hasCompField ? szBytes : 0u) + szBytes;
    if (srcLen < hdrLen + 1u) return false;

    auto rdBeN = [](const uint8_t* p, size_t n) -> uint64_t {
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v = (v << 8) | p[i];
        return v;
    };

    uint64_t uncompSz = rdBeN(src + 2u + (hasCompField ? szBytes : 0u), szBytes);
    if (uncompSz > 512ULL * 1024 * 1024) return false;
    outUncompressedSize = uncompSz;
    outHeaderBytes      = hdrLen;
    return true;
}

inline bool DecompressEaRefPack(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut = 256 * 1024 * 1024)
{
    out.clear();
    uint64_t uncompSz = 0;
    size_t pos = 0;
    if (!PeekEaRefPackHeader(src, srcLen, uncompSz, pos) || uncompSz > maxOut) {
        return false;
    }

    out.reserve(static_cast<size_t>(uncompSz));

    auto copyLits = [&](size_t litLen) -> bool {
        if (litLen > srcLen - pos || litLen > uncompSz - out.size()) return false;
        out.insert(out.end(), src + pos, src + pos + litLen);
        pos += litLen;
        return true;
    };

    auto copyMatch = [&](size_t dist, size_t copyLen) -> bool {
        if (dist == 0 || dist > out.size() || copyLen > uncompSz - out.size()) return false;
        for (size_t i = 0; i < copyLen; ++i) {
            out.push_back(out[out.size() - dist]);
        }
        return true;
    };

    bool sawStop = false;
    while (pos < srcLen) {
        uint8_t b0 = src[pos++];
        if ((b0 & 0x80u) == 0) {
            // 2-byte command (0x00..0x7F): short LZ77 back-reference
            if (pos >= srcLen) return false;
            uint8_t b1     = src[pos++];
            size_t procLen = b0 & 0x03u;
            size_t refLen  = ((b0 & 0x1Cu) >> 2) + 3u;
            size_t dist    = ((static_cast<size_t>(b0 & 0x60u) << 3) | b1) + 1u;
            if (!copyLits(procLen) || !copyMatch(dist, refLen)) return false;
        } else if ((b0 & 0xC0u) == 0x80u) {
            // 3-byte command (0x80..0xBF): medium LZ77 back-reference
            if (pos + 2 > srcLen) return false;
            uint8_t b1     = src[pos++];
            uint8_t b2     = src[pos++];
            size_t procLen = (b1 >> 6) & 0x03u;
            size_t refLen  = (b0 & 0x3Fu) + 4u;
            size_t dist    = ((static_cast<size_t>(b1 & 0x3Fu) << 8) | b2) + 1u;
            if (!copyLits(procLen) || !copyMatch(dist, refLen)) return false;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            // 4-byte command (0xC0..0xDF): long LZ77 back-reference
            if (pos + 3 > srcLen) return false;
            uint8_t b1     = src[pos++];
            uint8_t b2     = src[pos++];
            uint8_t b3     = src[pos++];
            size_t procLen = b0 & 0x03u;
            size_t refLen  = ((static_cast<size_t>(b0 & 0x0Cu) << 6) | b3) + 5u;
            size_t dist    = ((static_cast<size_t>(b0 & 0x10u) << 12) |
                              (static_cast<size_t>(b1) << 8) |
                              static_cast<size_t>(b2)) + 1u;
            if (!copyLits(procLen) || !copyMatch(dist, refLen)) return false;
        } else if (b0 <= 0xFBu) {
            // 1-byte literal run (0xE0..0xFB): 4..112 literal bytes
            size_t procLen = (static_cast<size_t>(b0 & 0x1Fu) + 1u) * 4u;
            if (!copyLits(procLen)) return false;
        } else {
            // 1-byte stop command (0xFC..0xFF): 0..3 trailing literal bytes + EOF
            size_t procLen = b0 & 0x03u;
            if (!copyLits(procLen)) return false;
            sawStop = true;
            break;
        }
    }

    if (!sawStop || out.size() != uncompSz) {
        out.clear();
        return false;
    }
    return true;
}

inline bool ParseOrExtractEaBigArchive(
    const uint8_t* data,
    size_t size,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm = nullptr,
    std::vector<uint8_t>* outExtracted = nullptr)
{
    if (size < 16 || (std::memcmp(data, "BIGF", 4) != 0 && std::memcmp(data, "BIG4", 4) != 0)) return false;
    uint32_t numFiles = RdU32BE(data + 8);
    uint32_t hdrEnd   = RdU32BE(data + 12);
    if (numFiles == 0 || numFiles > 60000) return false;

    if (outInfo) {
        outInfo->formatName = L"EA Games BIG Archive (.big)";
    }

    size_t pos   = 16;
    size_t limit = std::min<size_t>(size, hdrEnd > 16 ? hdrEnd : size);

    for (uint32_t i = 0; i < numFiles && pos + 9 <= limit; ++i) {
        uint32_t fOff  = RdU32BE(data + pos + 0);
        uint32_t fSize = RdU32BE(data + pos + 4);
        pos += 8;

        size_t ns = pos;
        while (pos < limit && data[pos] != 0) ++pos;
        if (pos >= limit) return false;
        std::string fname(reinterpret_cast<const char*>(data + ns), pos - ns);
        ++pos;

        if (fname.empty()) continue;
        if (fOff > size || fSize > size - fOff) {
            if (outInfo) { outInfo->entries.clear(); outInfo->valid = false; }
            return false;
        }

        std::wstring fullPath = NormPath(PathUtils::Utf8ToWide(fname));
        if (fullPath.empty()) continue;

        const uint8_t* payload = data + fOff;
        uint64_t refUncompSz   = 0;
        size_t refHdrLen       = 0;
        bool isRefPack         = PeekEaRefPackHeader(payload, fSize, refUncompSz, refHdrLen);

        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(fullPath.c_str(), extractTargetNorm->c_str()) == 0) {
                if (isRefPack) {
                    return DecompressEaRefPack(payload, fSize, *outExtracted);
                }
                if (fSize > 256u * 1024u * 1024u) return false;
                outExtracted->assign(payload, payload + fSize);
                return true;
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive     = fullPath;
            item.name              = BaseName(fullPath);
            item.uncompressedSize  = isRefPack ? refUncompSz : fSize;
            item.compressedSize    = fSize;
            item.rawDataOffset     = fOff;
            item.compressionMethod = isRefPack ? 1 : 0;
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

inline bool ParseEaBigArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractEaBigArchive(data, size, &outInfo, nullptr, nullptr);
}

// ============================================================================
// #28. Build Engine GRP Archive (.grp — "KenSilverman")
// ============================================================================
inline bool ParseBuildGrp(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 16 || std::memcmp(data, "KenSilverman", 12) != 0) return false;
    outInfo.formatName = L"Build Engine GRP Archive (.grp)";

    uint32_t count = RdU32LE(data + 12);
    if (count > 32768 || 16ULL + static_cast<uint64_t>(count) * 16 > size) return false;

    uint64_t dataOff = 16ULL + static_cast<uint64_t>(count) * 16;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* ent = data + 16 + static_cast<size_t>(i) * 16;
        std::string fname(reinterpret_cast<const char*>(ent), strnlen(reinterpret_cast<const char*>(ent), 12));
        uint32_t fSize = RdU32LE(ent + 12);
        if (!fname.empty() && dataOff + fSize <= size) {
            ArchiveItemEntry item;
            item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fname));
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = dataOff;
            item.compressionMethod = 0; // Stored raw!
            outInfo.entries.push_back(std::move(item));
        }
        dataOff += fSize;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #29. Bohemia Interactive Packed Bank (.pbo — Arma / DayZ)
// ============================================================================
inline bool ParseBohemiaPbo(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 22) return false;
    outInfo.formatName = L"Bohemia Interactive PBO (.pbo)";

    struct RawPboRec { std::wstring path; uint32_t method; uint32_t origSz; uint32_t dataSz; };
    std::vector<RawPboRec> recs;
    std::wstring prefixDir;

    size_t pos = 0;
    bool sawHeaderOrEntry = false;
    while (pos < size && recs.size() < 50000) {
        size_t ns = pos;
        while (pos < size && data[pos] != 0) ++pos;
        if (pos + 21 > size) break;
        std::string fname(reinterpret_cast<const char*>(data + ns), pos - ns);
        ++pos; // skip NUL

        uint32_t mime   = RdU32LE(data + pos + 0);
        uint32_t origSz = RdU32LE(data + pos + 4);
        uint32_t ts     = RdU32LE(data + pos + 12);
        uint32_t dataSz = RdU32LE(data + pos + 16);
        (void)ts;
        pos += 20;

        if (fname.empty() && mime == 0x56657273u) { // "sreV" version/prefix properties block
            sawHeaderOrEntry = true;
            while (pos < size) {
                size_t ks = pos;
                while (pos < size && data[pos] != 0) ++pos;
                std::string k(reinterpret_cast<const char*>(data + ks), pos - ks);
                if (pos < size) ++pos;
                if (k.empty()) break;
                size_t vs = pos;
                while (pos < size && data[pos] != 0) ++pos;
                std::string v(reinterpret_cast<const char*>(data + vs), pos - vs);
                if (pos < size) ++pos;
                if (_stricmp(k.c_str(), "prefix") == 0) prefixDir = NormPath(PathUtils::Utf8ToWide(v));
            }
            continue;
        }
        if (fname.empty() && mime == 0 && dataSz == 0) {
            break; // End of PBO header table! Data stream immediately follows!
        }
        if (fname.empty()) return false;
        sawHeaderOrEntry = true;
        recs.push_back({ NormPath(PathUtils::Utf8ToWide(fname)), mime, origSz, dataSz });
    }

    if (!sawHeaderOrEntry || recs.empty()) return false;
    uint64_t curDataOff = pos;
    for (const auto& r : recs) {
        ArchiveItemEntry item;
        item.pathInArchive = prefixDir.empty() ? r.path : (prefixDir + L"/" + r.path);
        item.name = BaseName(item.pathInArchive);
        item.compressedSize = r.dataSz;
        item.uncompressedSize = (r.origSz > 0) ? r.origSz : r.dataSz;
        item.rawDataOffset = curDataOff;
        item.compressionMethod = (r.method == 0x43707273u) ? 99 : 0; // "srpC" = compressed, 0 = stored!
        outInfo.entries.push_back(std::move(item));
        curDataOff += r.dataSz;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #30. SCS Software Prism3D HashFS Archive (.scs — "SCS#")
// ============================================================================
inline bool ParseScsPrismArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 20 || std::memcmp(data, "SCS#", 4) != 0) return false;
    uint16_t ver = RdU16LE(data + 4);
    outInfo.formatName = L"SCS Prism3D HashFS v" + std::to_wstring(ver) + L" (.scs)";

    uint32_t numEntries = RdU32LE(data + 12);
    uint64_t startOff   = RdU32LE(data + 16);
    if (ver == 1 && startOff + static_cast<uint64_t>(numEntries) * 32 <= size) {
        for (uint32_t i = 0; i < numEntries && i < 30000; ++i) {
            const uint8_t* e = data + startOff + static_cast<size_t>(i) * 32;
            uint64_t hash   = RdU64LE(e + 0);
            uint64_t fOff   = RdU64LE(e + 8);
            uint32_t flags  = RdU32LE(e + 16);
            uint32_t crc    = RdU32LE(e + 20);
            uint32_t origSz = RdU32LE(e + 24);
            uint32_t compSz = RdU32LE(e + 28);

            wchar_t nameBuf[64] = {};
            swprintf_s(nameBuf, (flags & 1u) ? L"dir_%016llx" : L"asset_%016llx.bin", static_cast<unsigned long long>(hash));
            ArchiveItemEntry item;
            item.pathInArchive = nameBuf;
            item.name = nameBuf;
            item.isDirectory = (flags & 1u) != 0;
            item.crc32 = crc;
            item.uncompressedSize = origSz;
            item.compressedSize = compSz;
            item.rawDataOffset = fOff;
            item.compressionMethod = (flags & 2u) ? 8 : 0;
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #31 & #32. Linux SquashFS (.squashfs / .snap / .appimage) & EROFS (.erofs)
// ============================================================================
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
        return DecompressLz4RawBlockBounded(src, compLen, maxOut, out);
    }
    if (compId == 6) { // Zstd
        return StreamCodecs::DecompressZstdFrame(src, compLen, out, maxOut + 64);
    }
    return false;
}

inline bool ParseOrExtractSquashFsOrErofs(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetPath,
    std::vector<uint8_t>* outExtractedBytes)
{
    std::wstring wantNorm = extractTargetPath ? NormPath(*extractTargetPath) : std::wstring{};

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
                    std::wstring fullP  = NormPath(prefix.empty() ? childW : (prefix + L"/" + childW));
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
                            item.name = BaseName(fullP);
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
                            item.name = BaseName(fullP);
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

    // #32. Linux EROFS — Superblock magic 0xE0F5E1E2 at offset 1024
    if (size >= 1152 && RdU32LE(data + 1024) == 0xE0F5E1E2u) {
        const uint8_t* sb = data + 1024;
        uint8_t  blkBits  = sb[12];
        uint16_t rootNid  = RdU16LE(sb + 14);
        uint32_t blocks   = RdU32LE(sb + 36);
        uint32_t metaBlk  = RdU32LE(sb + 40);

        if (blkBits < 9 || blkBits > 16 || blocks == 0) return false;
        const size_t blkSz = size_t(1) << blkBits;

        if (outInfo) {
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
            outInfo->formatName = L"Linux EROFS Image" + (volName.empty() ? L"" : (L" (" + PathUtils::Utf8ToWide(volName) + L")"));
        }

        struct ErofsInodeInfo {
            size_t   inodeOff = 0;
            size_t   inodeHdrSz = 32;
            size_t   xattrBytes = 0;
            uint16_t dataLayout = 0;
            uint16_t iMode = 0;
            uint64_t iSize = 0;
            uint32_t rawBlkAddr = 0;
        };

        auto readErofsInode = [&](uint64_t nid, ErofsInodeInfo& inf) -> bool {
            uint64_t off64 = static_cast<uint64_t>(metaBlk) * blkSz + nid * 32ULL;
            if (off64 + 32ULL > size) return false;
            inf.inodeOff = static_cast<size_t>(off64);
            uint16_t iFormat    = RdU16LE(data + inf.inodeOff + 0);
            uint16_t xattrCount = RdU16LE(data + inf.inodeOff + 2);
            inf.iMode           = RdU16LE(data + inf.inodeOff + 4);
            if ((inf.iMode & 0170000u) == 0) return false;

            bool isExt = (iFormat & 1u) != 0;
            inf.inodeHdrSz = isExt ? 64u : 32u;
            if (inf.inodeOff + inf.inodeHdrSz > size) return false;

            inf.dataLayout = (iFormat >> 1) & 0x07u;
            inf.xattrBytes = (xattrCount > 0) ? (12u + static_cast<size_t>(xattrCount - 1u) * 4u) : 0u;
            if (!isExt) {
                inf.iSize      = RdU32LE(data + inf.inodeOff + 8);
                inf.rawBlkAddr = RdU32LE(data + inf.inodeOff + 16);
            } else {
                inf.iSize      = RdU64LE(data + inf.inodeOff + 8);
                uint32_t u16Addr = RdU32LE(data + inf.inodeOff + 16);
                uint32_t u20Addr = RdU32LE(data + inf.inodeOff + 20);
                inf.rawBlkAddr = (u16Addr != 0 && u16Addr != 0xFFFFFFFFu) ? u16Addr : u20Addr;
            }
            return true;
        };

        auto readErofsData = [&](const ErofsInodeInfo& inf, std::vector<uint8_t>& outBuf) -> bool {
            if (inf.iSize == 0) {
                outBuf.clear();
                return true;
            }
            if (inf.iSize > 256 * 1024 * 1024) return false;
            if (inf.dataLayout == 0) { // EROFS_INODE_FLAT_PLAIN
                uint64_t startOff = static_cast<uint64_t>(inf.rawBlkAddr) * blkSz;
                if (startOff + inf.iSize > size) return false;
                outBuf.assign(
                    data + static_cast<size_t>(startOff),
                    data + static_cast<size_t>(startOff + inf.iSize));
                return true;
            }
            if (inf.dataLayout == 2) { // EROFS_INODE_FLAT_INLINE
                size_t fullBytes = static_cast<size_t>((inf.iSize / blkSz) * blkSz);
                size_t tailBytes = static_cast<size_t>(inf.iSize % blkSz);
                outBuf.clear();
                outBuf.reserve(static_cast<size_t>(inf.iSize));
                if (fullBytes > 0) {
                    uint64_t startOff = static_cast<uint64_t>(inf.rawBlkAddr) * blkSz;
                    if (startOff + fullBytes > size) return false;
                    outBuf.insert(outBuf.end(),
                        data + static_cast<size_t>(startOff),
                        data + static_cast<size_t>(startOff) + fullBytes);
                }
                if (tailBytes > 0) {
                    uint64_t inlineOff = static_cast<uint64_t>(inf.inodeOff) + inf.inodeHdrSz + inf.xattrBytes;
                    if (inlineOff + tailBytes > size) return false;
                    outBuf.insert(outBuf.end(),
                        data + static_cast<size_t>(inlineOff),
                        data + static_cast<size_t>(inlineOff) + tailBytes);
                }
                return true;
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
                    std::wstring fullP = NormPath(prefix.empty() ? wName : (prefix + L"/" + wName));
                    if (fullP.empty()) continue;

                    ErofsInodeInfo cInf;
                    bool hasInode = readErofsInode(childNid, cInf);
                    bool isDir = hasInode ? ((cInf.iMode & 0170000u) == 0040000u) : (fType == 2);

                    if (isDir) {
                        if (outInfo && !extractTargetPath) {
                            ArchiveItemEntry item;
                            item.pathInArchive = fullP;
                            item.name = BaseName(fullP);
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
                        item.name = BaseName(fullP);
                        item.isDirectory = false;
                        item.uncompressedSize = cInf.iSize;
                        item.compressedSize = cInf.iSize;
                        if (cInf.dataLayout == 0 && static_cast<uint64_t>(cInf.rawBlkAddr) * blkSz + cInf.iSize <= size) {
                            item.rawDataOffset = static_cast<uint64_t>(cInf.rawBlkAddr) * blkSz;
                            item.compressionMethod = 0;
                        } else if (cInf.dataLayout == 2 && cInf.iSize < blkSz &&
                                   cInf.inodeOff + cInf.inodeHdrSz + cInf.xattrBytes + cInf.iSize <= size)
                        {
                            item.rawDataOffset = cInf.inodeOff + cInf.inodeHdrSz + cInf.xattrBytes;
                            item.compressionMethod = 0;
                        } else {
                            item.compressionMethod = 2; // Multi-block + inline tail handled by ExtractExtendedArchive200Entry
                        }
                        outInfo->entries.push_back(std::move(item));
                        ++totalListed;
                    }
                }
            }
            return true;
        };

        if (!walkErofsDir(walkErofsDir, rootNid, L"", 0)) {
            if (extractTargetPath) return false;
        }
        if (extractTargetPath) return extractedOk;
        if (!outInfo || outInfo->entries.empty()) return false;
        outInfo->valid = true;
        return true;
    }

    return false;
}

inline bool ParseSquashFsOrErofs(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    return ParseOrExtractSquashFsOrErofs(data, size, extLower, &outInfo, nullptr, nullptr);
}

// ============================================================================
// #33. POSIX CPIO Archive (.cpio — "070701" newc / "070702" crc / "070707" odc)
// ============================================================================
inline bool ParsePosixCpio(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 76) return false;
    bool isNewc = (size >= 110 && (std::memcmp(data, "070701", 6) == 0 || std::memcmp(data, "070702", 6) == 0));
    bool isOdc  = (!isNewc && std::memcmp(data, "070707", 6) == 0);
    if (!isNewc && !isOdc) return false;

    outInfo.formatName = isNewc ? L"POSIX SVR4 CPIO Archive (.cpio)" : L"POSIX Octal CPIO Archive (.cpio)";
    size_t pos = 0;

    if (isNewc) {
        while (pos + 110 <= size && outInfo.entries.size() < 50000) {
            if (std::memcmp(data + pos, "070701", 6) != 0 && std::memcmp(data + pos, "070702", 6) != 0) break;
            auto hex8 = [&](size_t off) -> uint32_t {
                char b[9] = {};
                std::memcpy(b, data + pos + off, 8);
                return static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
            };
            uint32_t mode     = hex8(14);
            uint32_t fSize    = hex8(54);
            uint32_t nameSize = hex8(94);
            uint32_t check    = hex8(102);
            if (nameSize == 0 || nameSize > 4096 || pos + 110 + nameSize > size) break;

            std::string fname(reinterpret_cast<const char*>(data + pos + 110), strnlen(reinterpret_cast<const char*>(data + pos + 110), nameSize));
            size_t dataStart = (pos + 110 + nameSize + 3u) & ~size_t(3u);
            if (fname == "TRAILER!!!") break;
            if (dataStart + fSize > size) break;

            if (!fname.empty() && fname != ".") {
                ArchiveItemEntry item;
                item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fname));
                item.name = BaseName(item.pathInArchive);
                item.isDirectory = ((mode & 0170000u) == 0040000u);
                item.uncompressedSize = item.isDirectory ? 0 : fSize;
                item.compressedSize = item.uncompressedSize;
                item.crc32 = check;
                item.rawDataOffset = dataStart;
                item.compressionMethod = 0; // Stored raw!
                outInfo.entries.push_back(std::move(item));
            }
            pos = (dataStart + fSize + 3u) & ~size_t(3u);
        }
    } else {
        while (pos + 76 <= size && outInfo.entries.size() < 50000) {
            if (std::memcmp(data + pos, "070707", 6) != 0) break;
            auto oct = [&](size_t off, size_t len) -> uint64_t {
                char b[16] = {};
                std::memcpy(b, data + pos + off, len);
                return static_cast<uint64_t>(std::strtoull(b, nullptr, 8));
            };
            uint64_t mode     = oct(18, 6);
            uint64_t nameSize = oct(59, 6);
            uint64_t fSize    = oct(65, 11);
            if (nameSize == 0 || nameSize > 4096 || pos + 76 + nameSize > size) break;
            std::string fname(reinterpret_cast<const char*>(data + pos + 76), strnlen(reinterpret_cast<const char*>(data + pos + 76), static_cast<size_t>(nameSize)));
            if (fname == "TRAILER!!!") break;
            size_t dataStart = pos + 76 + static_cast<size_t>(nameSize);
            if (dataStart + fSize > size) break;
            if (!fname.empty() && fname != ".") {
                ArchiveItemEntry item;
                item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fname));
                item.name = BaseName(item.pathInArchive);
                item.isDirectory = ((mode & 0170000u) == 0040000u);
                item.uncompressedSize = item.isDirectory ? 0 : fSize;
                item.compressedSize = item.uncompressedSize;
                item.rawDataOffset = dataStart;
                item.compressionMethod = 0;
                outInfo.entries.push_back(std::move(item));
            }
            pos = dataStart + static_cast<size_t>(fSize);
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #34. Unix / COFF Static Library Archive (.ar / .a / .lib — "!<arch>\n")
// ============================================================================
inline bool ParseUnixCoffAr(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    if (size < 68 || std::memcmp(data, "!<arch>\n", 8) != 0) return false;
    outInfo.formatName = (extLower == L".lib") ? L"COFF Static Library (.lib)" : L"Unix Static Archive (.ar/.a)";

    std::string gnuLongNames;
    size_t pos = 8;
    while (pos + 60 <= size && outInfo.entries.size() < 50000) {
        if (data[pos + 58] != 0x60 || data[pos + 59] != 0x0A) break;
        std::string rawName(reinterpret_cast<const char*>(data + pos), 16);
        while (!rawName.empty() && rawName.back() == ' ') rawName.pop_back();
        std::string sizeStr(reinterpret_cast<const char*>(data + pos + 48), 10);
        uint64_t memSz = static_cast<uint64_t>(std::strtoull(sizeStr.c_str(), nullptr, 10));
        pos += 60;
        if (pos + memSz > size) break;

        size_t payloadOff = pos;
        uint64_t payloadSz = memSz;

        if (rawName == "//") {
            gnuLongNames.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(memSz));
        } else if (rawName != "/" && rawName != "/SYM64/" && rawName != "__.SYMDEF" && rawName != "__.SYMDEF SORTED") {
            std::string cleanName = rawName;
            if (rawName.size() > 1 && rawName[0] == '/' && rawName[1] >= '0' && rawName[1] <= '9') {
                size_t strOff = std::strtoul(rawName.c_str() + 1, nullptr, 10);
                if (strOff < gnuLongNames.size()) {
                    size_t endN = gnuLongNames.find_first_of("/\r\n\0", strOff, 4);
                    cleanName = gnuLongNames.substr(strOff, (endN == std::string::npos ? gnuLongNames.size() : endN) - strOff);
                }
            } else if (rawName.rfind("#1/", 0) == 0) { // BSD extended name inline at start of member payload
                size_t nLen = std::strtoul(rawName.c_str() + 3, nullptr, 10);
                if (nLen <= memSz) {
                    cleanName.assign(reinterpret_cast<const char*>(data + pos), strnlen(reinterpret_cast<const char*>(data + pos), nLen));
                    payloadOff += nLen;
                    payloadSz -= nLen;
                }
            } else if (!cleanName.empty() && cleanName.back() == '/') {
                cleanName.pop_back();
            }

            if (!cleanName.empty()) {
                ArchiveItemEntry item;
                item.pathInArchive = NormPath(PathUtils::Utf8ToWide(cleanName));
                item.name = BaseName(item.pathInArchive);
                item.uncompressedSize = payloadSz;
                item.compressedSize = payloadSz;
                item.rawDataOffset = payloadOff;
                item.compressionMethod = 0; // Stored raw object!
                outInfo.entries.push_back(std::move(item));
            }
        }
        pos += static_cast<size_t>(memSz + (memSz & 1ULL));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #35. macOS eXtensible Archive (.xar / .pkg — "xar!")
// ============================================================================
inline bool ParseXarPkg(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    if (size < 28 || std::memcmp(data, "xar!", 4) != 0) return false;
    uint16_t hdrSize   = RdU16BE(data + 4);
    uint64_t tocCompSz = RdU64BE(data + 8);
    uint64_t tocUncSz  = RdU64BE(data + 16);
    if (hdrSize < 28 || hdrSize + tocCompSz > size || tocUncSz > 64 * 1024 * 1024) return false;

    outInfo.formatName = (extLower == L".pkg") ? L"macOS Installer Package (.pkg / XAR)" : L"eXtensible Archive (.xar)";

    std::vector<uint8_t> xmlBytes;
    if (tocCompSz == tocUncSz && tocCompSz > 8 && data[hdrSize] == '<') {
        xmlBytes.assign(data + hdrSize, data + hdrSize + static_cast<size_t>(tocCompSz));
    } else if (!ZipDeflate::InflateZlib(data + hdrSize, static_cast<size_t>(tocCompSz), xmlBytes, static_cast<size_t>(tocUncSz) + 64)) {
        return false;
    }

    uint64_t heapBase = hdrSize + tocCompSz;
    std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
    std::vector<std::wstring> dirStack;

    size_t pos = 0;
    while (pos < sv.size() && outInfo.entries.size() < 20000) {
        size_t fOpen = sv.find("<file", pos);
        size_t fClose = sv.find("</file>", pos);
        if (fOpen == std::string_view::npos && fClose == std::string_view::npos) break;
        if (fClose != std::string_view::npos && (fOpen == std::string_view::npos || fClose < fOpen)) {
            if (!dirStack.empty()) dirStack.pop_back();
            pos = fClose + 7;
            continue;
        }
        size_t nextBoundary = std::min(
            sv.find("<file", fOpen + 5) == std::string_view::npos ? sv.size() : sv.find("<file", fOpen + 5),
            sv.find("</file>", fOpen + 5) == std::string_view::npos ? sv.size() : sv.find("</file>", fOpen + 5));
        std::string_view blk = sv.substr(fOpen, nextBoundary - fOpen);

        auto extractTag = [&](const char* openT, const char* closeT) -> std::string_view {
            size_t a = blk.find(openT);
            if (a == std::string_view::npos) return {};
            a += std::strlen(openT);
            size_t b = blk.find(closeT, a);
            return (b == std::string_view::npos) ? std::string_view{} : blk.substr(a, b - a);
        };

        std::string_view nm = extractTag("<name>", "</name>");
        std::string_view tp = extractTag("<type>", "</type>");
        std::string_view sz = extractTag("<size>", "</size>");
        std::string_view ln = extractTag("<length>", "</length>");
        std::string_view of = extractTag("<offset>", "</offset>");

        std::wstring wName = PathUtils::Utf8ToWide(nm);
        bool isDir = (tp == "directory");
        std::wstring fullRel = dirStack.empty() ? wName : (dirStack.back() + L"/" + wName);
        if (!wName.empty()) {
            ArchiveItemEntry item;
            item.pathInArchive = NormPath(fullRel);
            item.name = wName;
            item.isDirectory = isDir;
            item.uncompressedSize = sz.empty() ? 0 : std::strtoull(std::string(sz).c_str(), nullptr, 10);
            item.compressedSize   = ln.empty() ? item.uncompressedSize : std::strtoull(std::string(ln).c_str(), nullptr, 10);
            uint64_t relOff       = of.empty() ? 0 : std::strtoull(std::string(of).c_str(), nullptr, 10);
            item.rawDataOffset    = heapBase + relOff;
            item.compressionMethod = (blk.find("octet-stream") != std::string_view::npos && item.compressedSize == item.uncompressedSize) ? 0 : 8;
            outInfo.entries.push_back(std::move(item));
        }
        dirStack.push_back(fullRel);
        pos = fOpen + 5;
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #36, #37, #38. LHArc (.lzh/.lha), ARJ (.arj), and WinACE (.ace)
// ============================================================================
inline bool ParseLzhArjAce(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    // #36. LZH / LHA: "-lh0-".."-lh7-" / "-lzs-" / "-lhd-" at offset 2
    if (size >= 24 && data[2] == '-' && (data[3] == 'l' || data[3] == 'p') && data[6] == '-') {
        outInfo.formatName = L"LHArc Archive (.lzh/.lha)";
        size_t pos = 0;
        while (pos + 22 <= size && outInfo.entries.size() < 40000) {
            if (data[pos] == 0) break;
            if (data[pos + 2] != '-' || data[pos + 6] != '-') break;
            std::string method(reinterpret_cast<const char*>(data + pos + 2), 5);
            uint32_t packSz = RdU32LE(data + pos + 7);
            uint32_t origSz = RdU32LE(data + pos + 11);
            uint8_t lvl     = data[pos + 20];

            std::string fname, dirName;
            size_t dataStart = 0;
            if (lvl == 0 || lvl == 1) {
                uint8_t hdrSz = data[pos];
                uint8_t nLen  = data[pos + 21];
                if (pos + 2 + hdrSz > size || 22 + nLen > hdrSz + 2) break;
                fname.assign(reinterpret_cast<const char*>(data + pos + 22), nLen);
                dataStart = pos + 2 + hdrSz;
                if (lvl == 1) {
                    // Skip Level-1 extended header chain before compressed stream
                    size_t extPos = pos + 25 + nLen;
                    while (extPos + 2 <= size) {
                        uint16_t nextExt = RdU16LE(data + extPos);
                        if (nextExt == 0) break;
                        if (extPos + nextExt > size) break;
                        if (data[extPos + 2] == 0x02 && nextExt > 5) {
                            dirName.assign(reinterpret_cast<const char*>(data + extPos + 3), nextExt - 5);
                            for (char& c : dirName) if (static_cast<uint8_t>(c) == 0xFF) c = '/';
                        }
                        extPos += nextExt;
                    }
                }
            } else if (lvl == 2) {
                uint16_t totalHdr = RdU16LE(data + pos);
                if (totalHdr < 26 || pos + totalHdr > size) break;
                dataStart = pos + totalHdr;
                size_t extPos = pos + 24;
                while (extPos + 2 <= dataStart) {
                    uint16_t nextExt = RdU16LE(data + extPos);
                    if (nextExt < 3 || extPos + nextExt > dataStart) break;
                    uint8_t etype = data[extPos + 2];
                    if (etype == 0x01) fname.assign(reinterpret_cast<const char*>(data + extPos + 3), nextExt - 5);
                    else if (etype == 0x02) {
                        dirName.assign(reinterpret_cast<const char*>(data + extPos + 3), nextExt - 5);
                        for (char& c : dirName) if (static_cast<uint8_t>(c) == 0xFF) c = '/';
                    }
                    extPos += nextExt;
                }
            } else {
                break;
            }

            if (dataStart + packSz > size) break;
            std::string fullP = dirName + fname;
            if (!fullP.empty()) {
                ArchiveItemEntry item;
                item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fullP));
                item.name = BaseName(item.pathInArchive);
                item.isDirectory = (method == "-lhd-");
                item.compressedSize = packSz;
                item.uncompressedSize = origSz;
                item.rawDataOffset = dataStart;
                item.compressionMethod = (method == "-lh0-") ? 0 : 5;
                outInfo.entries.push_back(std::move(item));
            }
            pos = dataStart + packSz;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    // #37. ARJ Archive — Magic 0x60 0xEA
    if (size >= 34 && data[0] == 0x60 && data[1] == 0xEA) {
        outInfo.formatName = L"ARJ Archive (.arj)";
        size_t pos = 0;
        bool isMainHdr = true;
        while (pos + 4 <= size && outInfo.entries.size() < 40000) {
            if (data[pos] != 0x60 || data[pos + 1] != 0xEA) break;
            uint16_t basicSz = RdU16LE(data + pos + 2);
            if (basicSz == 0) break; // End of ARJ archive
            if (basicSz < 30 || pos + 4 + basicSz + 4 > size) break;

            const uint8_t* hdr = data + pos + 4;
            uint8_t firstHdrSz = hdr[0];
            uint8_t method     = hdr[3];
            uint8_t fileType   = hdr[4];
            uint32_t dosTime   = RdU32LE(hdr + 8);
            uint32_t compSz    = RdU32LE(hdr + 12);
            uint32_t origSz    = RdU32LE(hdr + 16);
            uint32_t crc32     = RdU32LE(hdr + 20);

            std::string fname;
            if (firstHdrSz < basicSz) {
                fname.assign(reinterpret_cast<const char*>(hdr + firstHdrSz),
                             strnlen(reinterpret_cast<const char*>(hdr + firstHdrSz), basicSz - firstHdrSz));
            }

            // Skip basic header + 4-byte header CRC + extended header chain
            size_t extPos = pos + 4 + basicSz + 4;
            while (extPos + 2 <= size) {
                uint16_t extSz = RdU16LE(data + extPos);
                extPos += 2;
                if (extSz == 0) break;
                extPos += extSz + 4; // ext data + CRC32
            }
            if (extPos > size) break;

            if (isMainHdr || fileType == 2) {
                isMainHdr = false;
                pos = extPos;
                continue;
            }
            if (extPos + compSz > size) break;
            if (!fname.empty()) {
                ArchiveItemEntry item;
                item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fname));
                item.name = BaseName(item.pathInArchive);
                item.isDirectory = (fileType == 3);
                item.compressedSize = compSz;
                item.uncompressedSize = origSz;
                item.crc32 = crc32;
                item.dosTime = static_cast<uint16_t>(dosTime & 0xFFFF);
                item.dosDate = static_cast<uint16_t>((dosTime >> 16) & 0xFFFF);
                item.rawDataOffset = extPos;
                item.compressionMethod = (method == 0) ? 0 : method;
                outInfo.entries.push_back(std::move(item));
            }
            pos = extPos + compSz;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    // #38. WinACE Archive — Magic "**ACE**" at offset 7
    if (size >= 32 && std::memcmp(data + 7, "**ACE**", 7) == 0) {
        outInfo.formatName = L"WinACE Archive (.ace)";
        size_t pos = 0;
        while (pos + 7 <= size && outInfo.entries.size() < 40000) {
            uint16_t hdrSz   = RdU16LE(data + pos + 2);
            if (hdrSz < 3 || pos + 4 + hdrSz > size) break;
            uint8_t  hdrType = data[pos + 4];
            uint16_t hdrFlgs = RdU16LE(data + pos + 5);
            uint64_t addSz   = (hdrFlgs & 0x0001u) ? RdU32LE(data + pos + 7) : 0;

            if (hdrType == 1 && hdrSz >= 31) { // File block
                const uint8_t* fh = data + pos + 4;
                uint32_t packSz  = RdU32LE(fh + 3);
                uint32_t origSz  = RdU32LE(fh + 7);
                uint32_t dosTime = RdU32LE(fh + 11);
                uint32_t attr    = RdU32LE(fh + 15);
                uint32_t crc32   = RdU32LE(fh + 19);
                uint8_t  compTp  = fh[23];
                uint16_t fnLen   = RdU16LE(fh + 29);
                if (31 + fnLen <= hdrSz) {
                    std::string fname(reinterpret_cast<const char*>(fh + 31), fnLen);
                    ArchiveItemEntry item;
                    item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fname));
                    item.name = BaseName(item.pathInArchive);
                    item.isDirectory = (attr & 0x10u) != 0;
                    item.compressedSize = packSz;
                    item.uncompressedSize = origSz;
                    item.crc32 = crc32;
                    item.dosTime = static_cast<uint16_t>(dosTime & 0xFFFF);
                    item.dosDate = static_cast<uint16_t>((dosTime >> 16) & 0xFFFF);
                    item.rawDataOffset = pos + 4 + hdrSz;
                    item.compressionMethod = (compTp == 0) ? 0 : compTp;
                    outInfo.entries.push_back(std::move(item));
                }
            }
            size_t step = 4 + hdrSz + static_cast<size_t>(addSz);
            if (step == 0 || pos + step > size) break;
            pos += step;
        }
        outInfo.valid = !outInfo.entries.empty();
        return outInfo.valid;
    }

    (void)extLower;
    return false;
}

} // namespace DetailArc200

inline bool InspectExtendedArchive200(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    ArchiveSummaryInfo& outInfo)
{
    if (extLower == L".unitypackage") {
        if (DetailArc200::ParseUnityPackageFromMemory(data, size, outInfo)) return true;
    }
    if (DetailArc200::ParseUnityFsBundle(data, size, outInfo)) return true;
    if (DetailArc200::ParseGodotPck(data, size, outInfo)) return true;
    if (DetailArc200::ParseRpgMakerRgss(data, size, outInfo)) return true;
    if (DetailArc200::ParseKiriKiriXp3(data, size, outInfo)) return true;
    if (DetailArc200::ParseBlizzardMpq(data, size, outInfo)) return true;
    if (DetailArc200::ParseEaBigArchive(data, size, outInfo)) return true;
    if (DetailArc200::ParseBuildGrp(data, size, outInfo)) return true;
    if (extLower == L".pbo" && DetailArc200::ParseBohemiaPbo(data, size, outInfo)) return true;
    if (DetailArc200::ParseScsPrismArchive(data, size, outInfo)) return true;
    if (DetailArc200::ParseSquashFsOrErofs(data, size, extLower, outInfo)) return true;
    if (DetailArc200::ParsePosixCpio(data, size, outInfo)) return true;
    if ((extLower == L".ar" || extLower == L".a" || extLower == L".lib") &&
        DetailArc200::ParseUnixCoffAr(data, size, extLower, outInfo)) return true;
    if (DetailArc200::ParseXarPkg(data, size, extLower, outInfo)) return true;
    if (DetailArc200::ParseLzhArjAce(data, size, extLower, outInfo)) return true;
    return false;
}

inline bool ExtractExtendedArchive200Entry(
    const std::wstring& archivePath,
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    (void)archivePath;
    if (!data || size < 4 || normTarget.empty()) return false;

    std::wstring want = DetailArc200::NormPath(normTarget);
    if (want.empty()) return false;

    // 1. UnityPackage (.unitypackage)
    if (extLower == L".unitypackage") {
        ArchiveSummaryInfo dummy;
        if (DetailArc200::ParseUnityPackageFromMemory(data, size, dummy, &want, &outBytes)) {
            return true;
        }
    }

    // 2. UnityFS AssetBundle (.bundle / .unity3d)
    if (size >= 32 && std::memcmp(data, "UnityFS\0", 8) == 0) {
        return DetailArc200::ParseOrExtractUnityFsBundle(data, size, nullptr, &want, &outBytes);
    }

    // 3. RPG Maker Encrypted Archives (.rgssad / .rgss2a / .rgss3a)
    if (size >= 12 && std::memcmp(data, "RGSSAD\0", 7) == 0) {
        return DetailArc200::ParseOrExtractRpgMakerRgss(data, size, nullptr, &want, &outBytes);
    }

    // 4. KiriKiri Visual Novel Archive (.xp3 — raw & zlib multi-segment)
    static const uint8_t kXp3Sig[11] = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
    if (size >= 28 && std::memcmp(data, kXp3Sig, 11) == 0) {
        return DetailArc200::ParseOrExtractKiriKiriXp3(data, size, nullptr, &want, &outBytes);
    }

    // 5. Blizzard MoPaQ Archive (.mpq — MPQ\x1A & MPQ\x1B, Storm cipher + Zlib/BZip2/LZMA sectors)
    if (size >= 32 && (std::memcmp(data, "MPQ\x1A", 4) == 0 || std::memcmp(data, "MPQ\x1B", 4) == 0)) {
        return DetailArc200::ParseOrExtractBlizzardMpq(data, size, nullptr, &want, &outBytes);
    }

    // 6. SCS Software Prism3D HashFS (.scs — raw & zlib)
    if (size >= 20 && std::memcmp(data, "SCS#", 4) == 0) {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseScsPrismArchive(data, size, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset + e.compressedSize > size || e.uncompressedSize > 256 * 1024 * 1024) return false;
                    if (e.compressionMethod == 0) {
                        if (e.rawDataOffset + e.uncompressedSize > size) return false;
                        outBytes.assign(
                            data + static_cast<size_t>(e.rawDataOffset),
                            data + static_cast<size_t>(e.rawDataOffset + e.uncompressedSize));
                        return true;
                    }
                    if (e.compressionMethod == 8) {
                        return ZipDeflate::InflateZlib(
                                   data + static_cast<size_t>(e.rawDataOffset),
                                   static_cast<size_t>(e.compressedSize),
                                   outBytes,
                                   static_cast<size_t>(e.uncompressedSize) + 64) ||
                               ZipDeflate::InflateRaw(
                                   data + static_cast<size_t>(e.rawDataOffset),
                                   static_cast<size_t>(e.compressedSize),
                                   outBytes,
                                   static_cast<size_t>(e.uncompressedSize) + 64);
                    }
                    return false;
                }
            }
        }
        return false;
    }

    // 7. Linux SquashFS (.squashfs / .snap / .appimage) & Linux EROFS (.erofs)
    if ((size >= 96 && std::memcmp(data, "hsqs", 4) == 0) ||
        (size >= 1152 && DetailArc200::RdU32LE(data + 1024) == 0xE0F5E1E2u) ||
        extLower == L".squashfs" || extLower == L".snap" || extLower == L".appimage" || extLower == L".erofs")
    {
        if (DetailArc200::ParseOrExtractSquashFsOrErofs(data, size, extLower, nullptr, &want, &outBytes)) {
            return true;
        }
    }

    // 8. macOS eXtensible Archive (.xar / .pkg — raw, zlib, bzip2, xz)
    if (size >= 28 && std::memcmp(data, "xar!", 4) == 0) {
        ArchiveSummaryInfo info;
        if (DetailArc200::ParseXarPkg(data, size, extLower, info)) {
            for (const auto& e : info.entries) {
                if (!e.isDirectory && _wcsicmp(e.pathInArchive.c_str(), want.c_str()) == 0) {
                    if (e.rawDataOffset + e.compressedSize > size || e.uncompressedSize > 256 * 1024 * 1024) return false;
                    const uint8_t* p = data + static_cast<size_t>(e.rawDataOffset);
                    size_t cLen = static_cast<size_t>(e.compressedSize);
                    size_t uLen = static_cast<size_t>(e.uncompressedSize);
                    if (e.compressionMethod == 0) {
                        if (e.rawDataOffset + uLen > size) return false;
                        outBytes.assign(p, p + uLen);
                        return true;
                    }
                    if (cLen >= 3 && std::memcmp(p, "BZh", 3) == 0) {
                        return StreamCodecs::DecompressBzip2Stream(p, cLen, outBytes, uLen + 64);
                    }
                    if (cLen >= 6 && std::memcmp(p, "\xFD" "7zXZ\0", 6) == 0) {
                        return StreamCodecs::DecompressXzStream(p, cLen, outBytes, uLen + 64);
                    }
                    if (ZipDeflate::InflateZlib(p, cLen, outBytes, uLen + 64) ||
                        ZipDeflate::InflateRaw(p, cLen, outBytes, uLen + 64))
                    {
                        return true;
                    }
                    if (cLen == uLen) {
                        outBytes.assign(p, p + uLen);
                        return true;
                    }
                    return false;
                }
            }
        }
        return false;
    }

    // 9. EA Games BIGF / BIG4 Archive (.big — raw + EA RefPack 0x10FB decompression)
    if (size >= 16 && (std::memcmp(data, "BIGF", 4) == 0 || std::memcmp(data, "BIG4", 4) == 0)) {
        return DetailArc200::ParseOrExtractEaBigArchive(data, size, nullptr, &want, &outBytes);
    }

    return false;
}

} // namespace Explorer2::Formats
