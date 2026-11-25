#pragma once
// ZooArchive.h — Rahul Dhesi ZOO 2.x archive reader & Stored / 13-bit LZW / LH5 extractor (.zoo — 0xFDC4A7DC).

#include "../common/ArchiveFormatCommon.h"
#include <unordered_set>

namespace Explorer2::Formats {

namespace DetailZooArc {

// ZOO Method 1: Rahul Dhesi 9..13-bit LSB-first continuous LZW with Clear=256, EOF=257, FirstFree=258 (lzd.c)
inline bool DecodeZooLzw13(
    const uint8_t* src,
    size_t srcLen,
    uint32_t expectedOut,
    std::vector<uint8_t>& outBytes,
    size_t maxOut = 128u * 1024u * 1024u)
{
    outBytes.clear();
    if (expectedOut == 0u) return true;
    if (!src || srcLen == 0u || expectedOut > maxOut) return false;
    outBytes.reserve(std::min<size_t>(expectedOut, srcLen * 8u + 256u));

    constexpr uint32_t kClear     = 256u;
    constexpr uint32_t kEof       = 257u;
    constexpr uint32_t kFirstFree = 258u;
    constexpr uint32_t kMaxCode13 = 8192u;

    std::vector<uint16_t> prefix(kMaxCode13, 0u);
    std::vector<uint8_t>  suffix(kMaxCode13, 0u);
    std::vector<uint8_t>  stack(kMaxCode13 + 16u, 0u);
    for (uint32_t c = 0; c < 256u; ++c) suffix[c] = static_cast<uint8_t>(c);

    int nBits = 9;
    uint32_t maxCode  = 512u;
    uint32_t freeCode = kFirstFree;
    size_t bitOffset  = 0;

    auto readCode = [&]() -> int32_t {
        const size_t bytePos = bitOffset >> 3u;
        const int bitShift   = static_cast<int>(bitOffset & 7u);
        if (bitOffset + static_cast<size_t>(nBits) > srcLen * 8u) return -1;
        const uint32_t b0 = (bytePos < srcLen) ? src[bytePos] : 0u;
        const uint32_t b1 = (bytePos + 1u < srcLen) ? src[bytePos + 1u] : 0u;
        const uint32_t b2 = (bytePos + 2u < srcLen) ? src[bytePos + 2u] : 0u;
        const uint32_t word = b0 | (b1 << 8u) | (b2 << 16u);
        bitOffset += static_cast<size_t>(nBits);
        return static_cast<int32_t>((word >> bitShift) & ((1u << nBits) - 1u));
    };

    uint32_t oldCode = 0;
    uint8_t  finChar = 0;
    bool havePrev = false;

    while (outBytes.size() < expectedOut) {
        const int32_t raw = readCode();
        if (raw < 0) break;
        const uint32_t inCode = static_cast<uint32_t>(raw);
        if (inCode == kEof) break;

        if (inCode == kClear) {
            nBits    = 9;
            maxCode  = 512u;
            freeCode = kFirstFree;
            havePrev = false;
            continue;
        }

        if (!havePrev) {
            if (inCode >= 256u) return false;
            finChar = static_cast<uint8_t>(inCode);
            oldCode = inCode;
            havePrev = true;
            outBytes.push_back(finChar);
            continue;
        }

        uint32_t code = inCode;
        size_t sp = 0;
        if (code >= freeCode) {
            if (code > freeCode) return false;
            stack[sp++] = finChar;
            code = oldCode;
        }

        while (code >= 256u) {
            if (code < kFirstFree || sp >= kMaxCode13) return false;
            stack[sp++] = suffix[code];
            code = prefix[code];
        }
        finChar = suffix[code];
        stack[sp++] = finChar;

        for (size_t k = sp; k > 0u && outBytes.size() < expectedOut; --k) {
            outBytes.push_back(stack[k - 1u]);
        }

        if (freeCode < kMaxCode13) {
            prefix[freeCode] = static_cast<uint16_t>(oldCode);
            suffix[freeCode] = finChar;
            ++freeCode;
            if (freeCode >= maxCode && nBits < 13) {
                ++nBits;
                maxCode <<= 1u;
            }
        }
        oldCode = inCode;
    }

    return outBytes.size() == expectedOut;
}

struct ZooParsedEntry {
    std::wstring pathInArchive;
    uint32_t orgSize = 0;
    uint32_t sizeNow = 0;
    uint32_t dataOffset = 0;
    uint16_t fileCrc16 = 0;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    uint8_t packingMethod = 0; // 0 = Store, 1 = LZW-13, 2 = LH5
};

inline bool EnumerateZooEntries(const uint8_t* data, size_t size, std::vector<ZooParsedEntry>& outEntries, uint8_t& outMaj, uint8_t& outMin) {
    outEntries.clear();
    outMaj = 2;
    outMin = 1;
    if (!data || size < 42u) return false;
    if (ByteIO::RdU32LE(data + 20u) != 0xFDC4A7DCu) return false;

    const uint32_t zooStart = ByteIO::RdU32LE(data + 24u);
    const uint32_t zooMinus = ByteIO::RdU32LE(data + 28u);
    if (static_cast<uint32_t>(zooStart + zooMinus) != 0u) return false;
    if (zooStart < 34u || zooStart >= size) return false;
    outMaj = data[32u];
    outMin = data[33u];

    std::unordered_set<uint32_t> visitedOffsets;
    std::unordered_map<std::wstring, uint32_t> seenPaths;
    uint32_t curOff = zooStart;

    while (curOff != 0u && outEntries.size() < 50000u) {
        if (!visitedOffsets.insert(curOff).second) return false; // Corrupt cycle in direntry chain
        if (curOff > size || size - curOff < 51u) return false;

        const uint8_t* de = data + curOff;
        if (ByteIO::RdU32LE(de + 0u) != 0xFDC4A7DCu) return false;

        const uint8_t  deType   = de[4u];
        const uint8_t  method   = de[5u];
        const uint32_t nextOff  = ByteIO::RdU32LE(de + 6u);
        const uint32_t dataOff  = ByteIO::RdU32LE(de + 10u);
        const uint16_t dosDate  = ByteIO::RdU16LE(de + 14u);
        const uint16_t dosTime  = ByteIO::RdU16LE(de + 16u);
        const uint16_t fileCrc  = ByteIO::RdU16LE(de + 18u);
        const uint32_t orgSize  = ByteIO::RdU32LE(de + 20u);
        const uint32_t sizeNow  = ByteIO::RdU32LE(de + 24u);
        const uint8_t  deleted  = de[30u];

        // Terminal ZOO EOF direntry has next == 0
        if (nextOff == 0u) break;

        if (deleted == 0u) {
            if (dataOff > size || sizeNow > size - dataOff) return false;

            std::string shortName(reinterpret_cast<const char*>(de + 38u), strnlen(reinterpret_cast<const char*>(de + 38u), 13u));
            std::string longName;
            std::string dirName;

            if (deType == 2u && size - curOff >= 56u) {
                const uint16_t varDirLen = ByteIO::RdU16LE(de + 51u);
                if (varDirLen >= 2u && 56ULL + varDirLen <= size - curOff) {
                    const uint8_t namLen = de[56u];
                    const uint8_t dirLen = de[57u];
                    size_t vpos = 58u;
                    const size_t vend = 56u + varDirLen;
                    if (namLen > 0u && namLen <= vend - vpos) {
                        longName.assign(reinterpret_cast<const char*>(de + vpos), strnlen(reinterpret_cast<const char*>(de + vpos), namLen));
                        vpos += namLen;
                    }
                    if (dirLen > 0u && dirLen <= vend - vpos) {
                        dirName.assign(reinterpret_cast<const char*>(de + vpos), strnlen(reinterpret_cast<const char*>(de + vpos), dirLen));
                    }
                }
            }

            const std::string& leaf = !longName.empty() ? longName : shortName;
            if (!leaf.empty()) {
                std::string fullUtf8 = dirName.empty() ? leaf : (dirName + "/" + leaf);
                std::wstring normPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(fullUtf8));
                if (!normPath.empty()) {
                    // Disambiguate ZOO multi-generation files with identical path if present
                    uint32_t& gen = seenPaths[PathUtils::ToLower(normPath)];
                    ++gen;
                    if (gen > 1u) {
                        normPath += L";v" + std::to_wstring(gen);
                    }

                    ZooParsedEntry ent;
                    ent.pathInArchive = std::move(normPath);
                    ent.orgSize       = orgSize;
                    ent.sizeNow       = sizeNow;
                    ent.dataOffset    = dataOff;
                    ent.fileCrc16     = fileCrc;
                    ent.dosDate       = dosDate;
                    ent.dosTime       = dosTime;
                    ent.packingMethod = method;
                    outEntries.push_back(std::move(ent));
                }
            }
        }

        curOff = nextOff;
    }

    return !outEntries.empty();
}

} // namespace DetailZooArc

inline bool InspectZooArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::vector<DetailZooArc::ZooParsedEntry> entries;
    uint8_t maj = 2, minV = 1;
    if (!DetailZooArc::EnumerateZooEntries(data, size, entries, maj, minV)) return false;

    outInfo.formatName = L"ZOO Archive v" + std::to_wstring(maj) + L"." + std::to_wstring(minV) + L" (.zoo)";
    outInfo.entries.reserve(entries.size());
    for (const auto& e : entries) {
        ArchiveItemEntry item;
        item.pathInArchive     = e.pathInArchive;
        item.name              = ArchivePath::BaseName(e.pathInArchive);
        item.uncompressedSize  = e.orgSize;
        item.compressedSize    = e.sizeNow;
        item.crc32             = e.fileCrc16;
        item.dosDate           = e.dosDate;
        item.dosTime           = e.dosTime;
        item.isDirectory       = false;
        item.rawDataOffset     = (e.packingMethod == 0u) ? e.dataOffset : 0ULL;
        item.compressionMethod = e.packingMethod;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractZooArchiveEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<DetailZooArc::ZooParsedEntry> entries;
    uint8_t maj = 0, minV = 0;
    if (!DetailZooArc::EnumerateZooEntries(data, size, entries, maj, minV)) return false;

    const std::wstring want = ArchivePath::Normalize(normTarget);
    for (const auto& e : entries) {
        if (_wcsicmp(e.pathInArchive.c_str(), want.c_str()) != 0) continue;
        if (e.orgSize > 128u * 1024u * 1024u) return false;
        if (e.orgSize == 0u) {
            outBytes.clear();
            return true;
        }
        if (e.dataOffset > size || e.sizeNow > size - e.dataOffset) return false;

        const uint8_t* src = data + e.dataOffset;
        bool ok = false;
        if (e.packingMethod == 0u) { // Stored
            if (e.sizeNow < e.orgSize) return false;
            outBytes.assign(src, src + e.orgSize);
            ok = true;
        } else if (e.packingMethod == 1u) { // 13-bit LZW
            ok = DetailZooArc::DecodeZooLzw13(src, e.sizeNow, e.orgSize, outBytes);
        } else if (e.packingMethod == 2u) { // LH5 (8 KB window, np=14, pbit=4)
            ok = DetailUnixAndLegacyArc::DecodeLhaArjBlockHuff(src, e.sizeNow, e.orgSize, 14u, 4u, outBytes, 0x20u);
        } else {
            return false;
        }

        if (!ok || outBytes.size() != e.orgSize) {
            outBytes.clear();
            return false;
        }
        if (DetailUnixAndLegacyArc::Crc16Arc(outBytes.data(), outBytes.size()) != e.fileCrc16) {
            outBytes.clear();
            return false;
        }
        return true;
    }
    return false;
}

} // namespace Explorer2::Formats
