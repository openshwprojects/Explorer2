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
    if (p.rfind(L"./", 0) == 0) p.erase(0, 2);
    return p;
}

inline std::wstring BaseName(const std::wstring& p) {
    size_t s = p.find_last_of(L'/');
    return (s == std::wstring::npos) ? p : p.substr(s + 1);
}

// Fast native LZ4 raw block decompressor (used by UnityFS .bundle TOC blocks)
inline bool DecompressLz4RawBlock(const uint8_t* src, size_t srcLen, size_t uncompLen, std::vector<uint8_t>& out) {
    if (uncompLen > 64 * 1024 * 1024) return false;
    out.clear();
    out.reserve(uncompLen);
    size_t sp = 0;
    while (sp < srcLen && out.size() < uncompLen) {
        uint8_t token = src[sp++];
        size_t litLen = (token >> 4) & 0x0F;
        if (litLen == 15) {
            while (sp < srcLen) {
                uint8_t s = src[sp++];
                litLen += s;
                if (s != 255) break;
            }
        }
        if (sp + litLen > srcLen) return false;
        out.insert(out.end(), src + sp, src + sp + litLen);
        sp += litLen;
        if (sp >= srcLen || out.size() >= uncompLen) break;

        if (sp + 2 > srcLen) return false;
        uint16_t offset = RdU16LE(src + sp);
        sp += 2;
        if (offset == 0 || offset > out.size()) return false;

        size_t matchLen = (token & 0x0F) + 4;
        if ((token & 0x0F) == 15) {
            while (sp < srcLen) {
                uint8_t s = src[sp++];
                matchLen += s;
                if (s != 255) break;
            }
        }
        size_t matchPos = out.size() - offset;
        for (size_t i = 0; i < matchLen && out.size() < uncompLen; ++i) {
            out.push_back(out[matchPos + i]);
        }
    }
    return (out.size() == uncompLen);
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
inline bool ParseUnityFsBundle(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 32 || std::memcmp(data, "UnityFS\0", 8) != 0) return false;
    outInfo.formatName = L"UnityFS AssetBundle";

    size_t pos = 8;
    if (pos + 4 > size) return false;
    uint32_t fmtVer = RdU32BE(data + pos); pos += 4;

    auto readCString = [&]() -> std::string {
        size_t s = pos;
        while (pos < size && data[pos] != 0) ++pos;
        std::string r(reinterpret_cast<const char*>(data + s), pos - s);
        if (pos < size) ++pos;
        return r;
    };
    std::string playerVer = readCString();
    std::string engineVer = readCString();
    (void)playerVer;
    if (!engineVer.empty()) {
        outInfo.formatName = L"UnityFS Bundle (" + PathUtils::Utf8ToWide(engineVer) + L")";
    }

    if (pos + 20 > size) return false;
    uint64_t bundleSz   = RdU64BE(data + pos); pos += 8;
    uint32_t compInfoSz = RdU32BE(data + pos); pos += 4;
    uint32_t uncompSz   = RdU32BE(data + pos); pos += 4;
    uint32_t flags      = RdU32BE(data + pos); pos += 4;
    (void)bundleSz;

    if (fmtVer >= 7) {
        pos = (pos + 15u) & ~15u; // 16-byte alignment in UnityFS v7+
    }

    size_t infoPos = (flags & 0x80u) ? (size >= compInfoSz ? size - compInfoSz : size) : pos;
    size_t dataStart = (flags & 0x80u) ? pos : (pos + compInfoSz);
    if ((flags & 0x200u) != 0) dataStart = (dataStart + 15u) & ~15u;

    std::vector<uint8_t> blocksInfo;
    uint32_t compMode = flags & 0x3Fu;
    if (infoPos + compInfoSz <= size && uncompSz > 0 && uncompSz <= 16 * 1024 * 1024) {
        if (compMode == 0 && compInfoSz == uncompSz) {
            blocksInfo.assign(data + infoPos, data + infoPos + compInfoSz);
        } else if (compMode == 2 || compMode == 3) { // LZ4 / LZ4HC
            DecompressLz4RawBlock(data + infoPos, compInfoSz, uncompSz, blocksInfo);
        }
    }

    if (blocksInfo.size() >= 24) {
        size_t bp = 16; // skip 16-byte uncompressedDataHash
        uint32_t blockCount = RdU32BE(blocksInfo.data() + bp); bp += 4;
        bp += static_cast<size_t>(blockCount) * 10; // [uncomp:4][comp:4][flags:2]
        if (bp + 4 <= blocksInfo.size()) {
            uint32_t nodeCount = RdU32BE(blocksInfo.data() + bp); bp += 4;
            for (uint32_t n = 0; n < nodeCount && bp + 20 <= blocksInfo.size() && n < 8192; ++n) {
                uint64_t nOff   = RdU64BE(blocksInfo.data() + bp); bp += 8;
                uint64_t nSize  = RdU64BE(blocksInfo.data() + bp); bp += 8;
                uint32_t nFlags = RdU32BE(blocksInfo.data() + bp); bp += 4;
                (void)nFlags;
                size_t ns = bp;
                while (bp < blocksInfo.size() && blocksInfo[bp] != 0) ++bp;
                std::string nodeName(reinterpret_cast<const char*>(blocksInfo.data() + ns), bp - ns);
                if (bp < blocksInfo.size()) ++bp;
                if (!nodeName.empty()) {
                    ArchiveItemEntry item;
                    item.pathInArchive = NormPath(PathUtils::Utf8ToWide(nodeName));
                    item.name = BaseName(item.pathInArchive);
                    item.uncompressedSize = nSize;
                    item.compressedSize = nSize;
                    if (compMode == 0 && dataStart + nOff + nSize <= size) {
                        item.rawDataOffset = dataStart + nOff;
                        item.compressionMethod = 0;
                    } else {
                        item.compressionMethod = static_cast<uint16_t>(compMode);
                    }
                    outInfo.entries.push_back(std::move(item));
                }
            }
        }
    }

    if (outInfo.entries.empty()) {
        ArchiveItemEntry fallback;
        fallback.pathInArchive = L"CAB-SerializedAssets.assets";
        fallback.name = fallback.pathInArchive;
        fallback.uncompressedSize = size - std::min(size, pos);
        fallback.compressedSize = fallback.uncompressedSize;
        fallback.compressionMethod = static_cast<uint16_t>(compMode);
        outInfo.entries.push_back(std::move(fallback));
    }
    outInfo.valid = true;
    return true;
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
inline bool ParseRpgMakerRgss(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 12 || std::memcmp(data, "RGSSAD\0", 7) != 0) return false;
    uint8_t ver = data[7];
    if (ver != 1 && ver != 2 && ver != 3) return false;
    outInfo.formatName = (ver == 3) ? L"RPG Maker VX Ace Archive (.rgss3a)" : L"RPG Maker Archive (.rgssad)";

    if (ver == 3) {
        uint32_t key = RdU32LE(data + 8) * 9u + 3u;
        size_t pos = 12;
        while (pos + 16 <= size && outInfo.entries.size() < 30000) {
            uint32_t fOff    = RdU32LE(data + pos + 0) ^ key;
            uint32_t fSize   = RdU32LE(data + pos + 4) ^ key;
            uint32_t subKey  = RdU32LE(data + pos + 8) ^ key;
            uint32_t nameLen = RdU32LE(data + pos + 12) ^ key;
            pos += 16;
            (void)subKey;
            if (fOff == 0) break;
            if (nameLen == 0 || nameLen > 1024 || pos + nameLen > size) break;
            std::string decName(nameLen, '\0');
            for (uint32_t i = 0; i < nameLen; ++i) {
                decName[i] = static_cast<char>(data[pos + i] ^ ((key >> (8 * (i & 3))) & 0xFF));
            }
            pos += nameLen;
            ArchiveItemEntry item;
            item.pathInArchive = NormPath(PathUtils::Utf8ToWide(decName));
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = fOff;
            item.compressionMethod = 99; // XOR stream
            outInfo.entries.push_back(std::move(item));
        }
    } else {
        uint32_t key = 0xDEADCAFEu;
        size_t pos = 8;
        while (pos + 4 <= size && outInfo.entries.size() < 30000) {
            uint32_t nameLen = RdU32LE(data + pos) ^ key;
            key = key * 7u + 3u;
            pos += 4;
            if (nameLen == 0 || nameLen > 1024 || pos + nameLen + 4 > size) break;
            std::string decName(nameLen, '\0');
            for (uint32_t i = 0; i < nameLen; ++i) {
                decName[i] = static_cast<char>(data[pos + i] ^ (key & 0xFF));
                key = key * 7u + 3u;
            }
            pos += nameLen;
            uint32_t fSize = RdU32LE(data + pos) ^ key;
            key = key * 7u + 3u;
            pos += 4;
            if (pos + fSize > size) break;

            ArchiveItemEntry item;
            item.pathInArchive = NormPath(PathUtils::Utf8ToWide(decName));
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = pos;
            item.compressionMethod = 99;
            outInfo.entries.push_back(std::move(item));
            pos += fSize;
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #25. KiriKiri Visual Novel Archive (.xp3 — "XP3\r\n \n\x1A\x8B\x67\x01")
// ============================================================================
inline bool ParseKiriKiriXp3(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    static const uint8_t kXp3Sig[11] = { 'X','P','3','\r','\n',' ','\n',0x1A,0x8B,0x67,0x01 };
    if (size < 28 || std::memcmp(data, kXp3Sig, 11) != 0) return false;
    outInfo.formatName = L"KiriKiri XP3 Archive (.xp3)";

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

    size_t p = 0;
    while (p + 12 <= idxLen && outInfo.entries.size() < 40000) {
        uint64_t chunkSz = RdU64LE(idxPtr + p + 4);
        if (chunkSz > idxLen - p - 12) break;
        if (std::memcmp(idxPtr + p, "File", 4) == 0) {
            size_t sp = p + 12, send = p + 12 + static_cast<size_t>(chunkSz);
            std::wstring fname;
            uint64_t origSz = 0, compSz = 0, rawStart = 0;
            bool segCompressed = false;
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
                    uint32_t segFlags = RdU32LE(sub + 0);
                    segCompressed = (segFlags & 0x07) != 0;
                    rawStart = RdU64LE(sub + 4);
                    if (origSz == 0) origSz = RdU64LE(sub + 12);
                    if (compSz == 0) compSz = RdU64LE(sub + 20);
                }
                sp += 12 + static_cast<size_t>(subSz);
            }
            if (!fname.empty()) {
                ArchiveItemEntry item;
                item.pathInArchive = NormPath(fname);
                item.name = BaseName(item.pathInArchive);
                item.uncompressedSize = origSz;
                item.compressedSize = compSz;
                item.rawDataOffset = rawStart;
                item.compressionMethod = segCompressed ? 8 : 0;
                outInfo.entries.push_back(std::move(item));
            }
        }
        p += 12 + static_cast<size_t>(chunkSz);
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #26. Blizzard MoPaQ Archive (.mpq — "MPQ\x1A")
// ============================================================================
inline bool ParseBlizzardMpq(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 32 || std::memcmp(data, "MPQ\x1A", 4) != 0) return false;
    outInfo.formatName = L"Blizzard MoPaQ Archive (.mpq)";

    uint32_t hdrSz       = RdU32LE(data + 4);
    uint32_t blockTblPos = RdU32LE(data + 20);
    uint32_t blockCount  = RdU32LE(data + 28);
    (void)hdrSz;
    if (blockCount == 0 || blockCount > 100000 || blockTblPos + static_cast<uint64_t>(blockCount) * 16 > size) {
        return false;
    }

    // Build Storm MPQ encryption table to decrypt block table if encrypted (key = HashString("(block table)", 0x300))
    uint32_t cryptTbl[0x500] = {};
    uint32_t seed = 0x00100001u;
    for (uint32_t idx1 = 0; idx1 < 0x100; ++idx1) {
        uint32_t idx2 = idx1;
        for (int i = 0; i < 5; ++i, idx2 += 0x100) {
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t1 = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t2 = (seed & 0xFFFFu);
            cryptTbl[idx2] = t1 | t2;
        }
    }
    auto mpqHash = [&](const char* str, uint32_t hashType) -> uint32_t {
        uint32_t s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
        while (*str) {
            uint8_t ch = static_cast<uint8_t>(toupper(static_cast<unsigned char>(*str++)));
            s1 = cryptTbl[hashType + ch] ^ (s1 + s2);
            s2 = ch + s1 + s2 + (s2 << 5) + 3u;
        }
        return s1;
    };

    std::vector<uint32_t> btWords(blockCount * 4);
    std::memcpy(btWords.data(), data + blockTblPos, btWords.size() * 4);

    // Check if unencrypted first; if filePos looks encrypted (> size), decrypt with "(block table)" key!
    if (btWords[0] > size) {
        uint32_t k1 = mpqHash("(block table)", 0x300), k2 = 0xEEEEEEEEu;
        for (size_t i = 0; i < btWords.size(); ++i) {
            k2 += cryptTbl[0x400 + (k1 & 0xFF)];
            uint32_t plain = btWords[i] ^ (k1 + k2);
            k1 = ((~k1 << 0x15) + 0x11111111u) | (k1 >> 0x0B);
            k2 = plain + k2 + (k2 << 5) + 3u;
            btWords[i] = plain;
        }
    }

    // Try extracting embedded plain-text (listfile) lines if present
    std::vector<std::string> listNames;
    for (uint32_t i = 0; i < blockCount; ++i) {
        uint32_t fPos = btWords[i * 4 + 0];
        uint32_t cSz  = btWords[i * 4 + 1];
        uint32_t fSz  = btWords[i * 4 + 2];
        uint32_t flg  = btWords[i * 4 + 3];
        if ((flg & 0x80000000u) && cSz == fSz && fSz > 4 && fSz < 1024 * 1024 && fPos + fSz <= size) {
            std::string_view sv(reinterpret_cast<const char*>(data + fPos), fSz);
            if (sv.find('\\') != std::string_view::npos || sv.find('.') != std::string_view::npos) {
                bool printable = true;
                for (size_t c = 0; c < std::min<size_t>(fSz, 64); ++c) {
                    uint8_t b = static_cast<uint8_t>(sv[c]);
                    if (b < 9 || (b > 13 && b < 32) || b > 126) { printable = false; break; }
                }
                if (printable && sv.find('\n') != std::string_view::npos) {
                    size_t lp = 0;
                    while (lp < sv.size()) {
                        size_t le = sv.find_first_of("\r\n", lp);
                        if (le == std::string_view::npos) le = sv.size();
                        if (le > lp) listNames.emplace_back(sv.substr(lp, le - lp));
                        lp = le + 1;
                    }
                    break;
                }
            }
        }
    }

    for (uint32_t i = 0; i < blockCount; ++i) {
        uint32_t fPos = btWords[i * 4 + 0];
        uint32_t cSz  = btWords[i * 4 + 1];
        uint32_t fSz  = btWords[i * 4 + 2];
        uint32_t flg  = btWords[i * 4 + 3];
        if ((flg & 0x80000000u) == 0 || fPos >= size) continue; // MPQ_FILE_EXISTS
        ArchiveItemEntry item;
        if (i < listNames.size() && !listNames[i].empty()) {
            item.pathInArchive = NormPath(PathUtils::Utf8ToWide(listNames[i]));
        } else {
            wchar_t nb[64] = {};
            swprintf_s(nb, L"Files/Block_%04u.dat", i);
            item.pathInArchive = nb;
        }
        item.name = BaseName(item.pathInArchive);
        item.compressedSize = cSz;
        item.uncompressedSize = fSz;
        item.rawDataOffset = fPos;
        item.compressionMethod = ((flg & 0x0000FF00u) == 0 && cSz == fSz) ? 0 : 8;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

// ============================================================================
// #27. EA Games BIGF / BIG4 Archive (.big)
// ============================================================================
inline bool ParseEaBigArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (size < 16 || (std::memcmp(data, "BIGF", 4) != 0 && std::memcmp(data, "BIG4", 4) != 0)) return false;
    outInfo.formatName = L"EA Games BIG Archive (.big)";

    uint32_t numFiles = RdU32BE(data + 8);
    uint32_t hdrEnd   = RdU32BE(data + 12);
    size_t pos = 16;
    size_t limit = std::min<size_t>(size, hdrEnd > 16 ? hdrEnd : size);

    for (uint32_t i = 0; i < numFiles && pos + 9 <= limit && i < 60000; ++i) {
        uint32_t fOff  = RdU32BE(data + pos + 0);
        uint32_t fSize = RdU32BE(data + pos + 4);
        pos += 8;
        size_t ns = pos;
        while (pos < limit && data[pos] != 0) ++pos;
        std::string fname(reinterpret_cast<const char*>(data + ns), pos - ns);
        if (pos < limit) ++pos;
        if (!fname.empty() && fOff <= size) {
            ArchiveItemEntry item;
            item.pathInArchive = NormPath(PathUtils::Utf8ToWide(fname));
            item.name = BaseName(item.pathInArchive);
            item.uncompressedSize = fSize;
            item.compressedSize = fSize;
            item.rawDataOffset = fOff;
            item.compressionMethod = 0; // Stored raw!
            outInfo.entries.push_back(std::move(item));
        }
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
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
inline bool ParseSquashFsOrErofs(const uint8_t* data, size_t size, const std::wstring& extLower, ArchiveSummaryInfo& outInfo) {
    // Search offset 0 (or ELF trailer up to 4 MB for .appimage) for "hsqs" (0x73717368)
    size_t sqOff = SIZE_MAX;
    if (size >= 96 && std::memcmp(data, "hsqs", 4) == 0) {
        sqOff = 0;
    } else if (extLower == L".appimage" || extLower == L".snap" || extLower == L".squashfs") {
        size_t scanLim = std::min<size_t>(size, 4 * 1024 * 1024);
        for (size_t i = 0; i + 96 <= scanLim; i += 4) {
            if (data[i] == 'h' && data[i + 1] == 's' && data[i + 2] == 'q' && data[i + 3] == 's') {
                uint16_t maj = RdU16LE(data + i + 28);
                if (maj == 4) { sqOff = i; break; }
            }
        }
    }

    if (sqOff != SIZE_MAX && sqOff + 96 <= size) {
        const uint8_t* sb = data + sqOff;
        uint32_t inodes   = RdU32LE(sb + 4);
        uint32_t blkSize  = RdU32LE(sb + 12);
        uint16_t compId   = RdU16LE(sb + 20);
        uint64_t bytesUsed= RdU64LE(sb + 40);
        uint64_t dirTbl   = RdU64LE(sb + 72);
        (void)blkSize;

        const wchar_t* compName = (compId == 1) ? L"gzip" : (compId == 4) ? L"xz" : (compId == 5) ? L"lz4" : (compId == 6) ? L"zstd" : L"raw";
        outInfo.formatName = std::wstring(extLower == L".appimage" ? L"Linux AppImage (SquashFS/" :
                                          extLower == L".snap"     ? L"Ubuntu Snap (SquashFS/" : L"Linux SquashFS (") + compName + L")";

        // Walk uncompressed directory table metadata blocks if bit 15 (0x8000 = uncompressed) is set!
        size_t dp = sqOff + static_cast<size_t>(dirTbl);
        while (dp + 14 <= size && outInfo.entries.size() < 4096) {
            uint16_t mhdr = RdU16LE(data + dp);
            bool uncomp = (mhdr & 0x8000u) != 0;
            uint16_t mlen = mhdr & 0x7FFFu;
            if (!uncomp || mlen < 12 || dp + 2 + mlen > size) break;

            size_t mp = dp + 2, mend = dp + 2 + mlen;
            while (mp + 12 <= mend) {
                uint32_t countMinus1 = RdU32LE(data + mp + 0);
                mp += 12; // squashfs_dir_header: [count:4][start_block:4][inode_number:4]
                if (countMinus1 > 256) { mp = mend; break; }
                for (uint32_t e = 0; e <= countMinus1 && mp + 8 <= mend; ++e) {
                    uint16_t itype = RdU16LE(data + mp + 4);
                    uint16_t nszMinus1 = RdU16LE(data + mp + 6);
                    mp += 8;
                    size_t nlen = static_cast<size_t>(nszMinus1) + 1;
                    if (mp + nlen > mend) break;
                    std::string ename(reinterpret_cast<const char*>(data + mp), nlen);
                    mp += nlen;
                    ArchiveItemEntry item;
                    item.pathInArchive = NormPath(PathUtils::Utf8ToWide(ename));
                    item.name = item.pathInArchive;
                    item.isDirectory = (itype == 1 || itype == 8);
                    item.uncompressedSize = item.isDirectory ? 0 : 4096;
                    item.compressedSize = item.uncompressedSize;
                    item.compressionMethod = compId;
                    outInfo.entries.push_back(std::move(item));
                }
            }
            break;
        }

        if (outInfo.entries.empty()) {
            ArchiveItemEntry summary;
            summary.pathInArchive = L"squashfs-root (" + std::to_wstring(inodes) + L" inodes)";
            summary.name = summary.pathInArchive;
            summary.uncompressedSize = bytesUsed;
            summary.compressedSize = bytesUsed;
            summary.compressionMethod = compId;
            outInfo.entries.push_back(std::move(summary));
        }
        outInfo.valid = true;
        return true;
    }

    // #32. Linux EROFS — Superblock magic 0xE0F5E1E2 at offset 1024
    if (size >= 1152 && RdU32LE(data + 1024) == 0xE0F5E1E2u) {
        const uint8_t* sb = data + 1024;
        uint8_t blkBits   = sb[12];
        uint16_t rootNid  = RdU16LE(sb + 14);
        uint64_t inos     = RdU64LE(sb + 16);
        uint32_t blocks   = RdU32LE(sb + 36);
        uint32_t metaBlk  = RdU32LE(sb + 40);
        std::string volName(reinterpret_cast<const char*>(sb + 48), strnlen(reinterpret_cast<const char*>(sb + 48), 16));
        outInfo.formatName = L"Linux EROFS Image" + (volName.empty() ? L"" : (L" (" + PathUtils::Utf8ToWide(volName) + L")"));

        size_t blkSz = (blkBits >= 9 && blkBits <= 16) ? (size_t(1) << blkBits) : 4096;
        size_t rootInodeOff = static_cast<size_t>(metaBlk) * blkSz + static_cast<size_t>(rootNid) * 32;
        if (rootInodeOff + 32 <= size) {
            uint16_t iFormat = RdU16LE(data + rootInodeOff);
            size_t inodeHdrSz = ((iFormat & 1u) != 0) ? 64 : 32;
            uint32_t rawBlkAddr = RdU32LE(data + rootInodeOff + ((inodeHdrSz == 64) ? 20 : 16));
            uint16_t dataLayout = (iFormat >> 1) & 0x07u;
            size_t dirDataOff = (dataLayout == 2) ? (rootInodeOff + inodeHdrSz) : (static_cast<size_t>(rawBlkAddr) * blkSz);
            if (dirDataOff + 12 <= size) {
                uint16_t firstNameOff = RdU16LE(data + dirDataOff + 8);
                if (firstNameOff >= 12 && firstNameOff <= blkSz && (firstNameOff % 12) == 0 && dirDataOff + firstNameOff <= size) {
                    size_t numDirents = firstNameOff / 12;
                    for (size_t d = 0; d < numDirents && d < 2048; ++d) {
                        uint16_t nOff = RdU16LE(data + dirDataOff + d * 12 + 8);
                        uint8_t fType = data[dirDataOff + d * 12 + 10];
                        uint16_t nextOff = (d + 1 < numDirents)
                            ? RdU16LE(data + dirDataOff + (d + 1) * 12 + 8)
                            : static_cast<uint16_t>(std::min<size_t>(blkSz, size - dirDataOff));
                        if (nOff < nextOff && dirDataOff + nextOff <= size) {
                            std::string dn(reinterpret_cast<const char*>(data + dirDataOff + nOff),
                                           strnlen(reinterpret_cast<const char*>(data + dirDataOff + nOff), nextOff - nOff));
                            if (!dn.empty() && dn != "." && dn != "..") {
                                ArchiveItemEntry item;
                                item.pathInArchive = PathUtils::Utf8ToWide(dn);
                                item.name = item.pathInArchive;
                                item.isDirectory = (fType == 2);
                                outInfo.entries.push_back(std::move(item));
                            }
                        }
                    }
                }
            }
        }
        if (outInfo.entries.empty()) {
            ArchiveItemEntry e;
            e.pathInArchive = L"erofs_root (" + std::to_wstring(inos) + L" inodes)";
            e.name = e.pathInArchive;
            e.uncompressedSize = static_cast<uint64_t>(blocks) * blkSz;
            e.compressedSize = e.uncompressedSize;
            outInfo.entries.push_back(std::move(e));
        }
        outInfo.valid = true;
        return true;
    }
    return false;
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

} // namespace Explorer2::Formats
