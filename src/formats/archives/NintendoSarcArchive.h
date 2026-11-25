#pragma once
// NintendoSarcArchive.h — Nintendo Wii U / 3DS / Switch SARC ("SARC" + "SFAT" + "SFNT", LE & BE)
// and Yaz0 ("Yaz0" big-endian LZ77) compressed SARC (.sarc, .szs) reader & extractor.

#include "../common/ArchiveFormatCommon.h"
#include <mutex>

namespace Explorer2::Formats {

namespace DetailNintendoSarc {

inline bool DecompressYaz0(
    const uint8_t* data,
    size_t size,
    std::vector<uint8_t>& outBytes,
    size_t maxOut = 128u * 1024u * 1024u)
{
    outBytes.clear();
    if (!data || size < 16u || std::memcmp(data, "Yaz0", 4) != 0) return false;

    const uint32_t uncompSize = ByteIO::RdU32BE(data + 4u);
    if (uncompSize == 0u || uncompSize > maxOut) return false;
    outBytes.reserve(uncompSize);

    size_t srcPos = 16u;
    while (outBytes.size() < uncompSize) {
        if (srcPos >= size) {
            outBytes.clear();
            return false;
        }
        const uint8_t groupHdr = data[srcPos++];
        for (int bit = 7; bit >= 0 && outBytes.size() < uncompSize; --bit) {
            if (((groupHdr >> bit) & 1u) != 0u) {
                if (srcPos >= size) {
                    outBytes.clear();
                    return false;
                }
                outBytes.push_back(data[srcPos++]);
            } else {
                if (size - srcPos < 2u) {
                    outBytes.clear();
                    return false;
                }
                const uint8_t b1 = data[srcPos++];
                const uint8_t b2 = data[srcPos++];
                const size_t dist = (((static_cast<size_t>(b1) & 0x0Fu) << 8u) | b2) + 1u;
                size_t copyLen = b1 >> 4u;
                if (copyLen == 0u) {
                    if (srcPos >= size) {
                        outBytes.clear();
                        return false;
                    }
                    copyLen = static_cast<size_t>(data[srcPos++]) + 0x12u;
                } else {
                    copyLen += 2u;
                }
                if (dist > outBytes.size()) {
                    outBytes.clear();
                    return false;
                }
                const size_t take = std::min<size_t>(copyLen, uncompSize - outBytes.size());
                for (size_t k = 0; k < take; ++k) {
                    outBytes.push_back(outBytes[outBytes.size() - dist]);
                }
            }
        }
    }

    return outBytes.size() == uncompSize;
}

struct CachedYaz0Buffer {
    size_t srcSize = 0;
    uint32_t headCrc = 0;
    uint32_t tailCrc = 0;
    std::vector<uint8_t> unpacked;
};
inline std::mutex g_yaz0CacheMutex;
static CachedYaz0Buffer g_yaz0Cache;

inline bool UnwrapYaz0Cached(const uint8_t* data, size_t size, std::vector<uint8_t>& outSarc) {
    if (!data || size < 16u) return false;
    const uint32_t headCrc = Checksum::Crc32(data, std::min<size_t>(size, 64u));
    const size_t tailOff   = (size > 64u) ? (size - 64u) : 0u;
    const uint32_t tailCrc = Checksum::Crc32(data + tailOff, size - tailOff);
    {
        std::lock_guard<std::mutex> lock(g_yaz0CacheMutex);
        if (g_yaz0Cache.srcSize == size && g_yaz0Cache.headCrc == headCrc &&
            g_yaz0Cache.tailCrc == tailCrc && !g_yaz0Cache.unpacked.empty())
        {
            outSarc = g_yaz0Cache.unpacked;
            return true;
        }
    }
    if (!DecompressYaz0(data, size, outSarc)) return false;
    if (outSarc.size() <= 64u * 1024u * 1024u) {
        std::lock_guard<std::mutex> lock(g_yaz0CacheMutex);
        g_yaz0Cache.srcSize  = size;
        g_yaz0Cache.headCrc  = headCrc;
        g_yaz0Cache.tailCrc  = tailCrc;
        g_yaz0Cache.unpacked = outSarc;
    }
    return true;
}

inline uint16_t RdSarcU16(const uint8_t* p, bool be) noexcept {
    return be ? ByteIO::RdU16BE(p) : ByteIO::RdU16LE(p);
}
inline uint32_t RdSarcU32(const uint8_t* p, bool be) noexcept {
    return be ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p);
}

inline bool ParseOrExtractSarcBuffer(
    const uint8_t* sarc,
    size_t sarcSize,
    bool wasYaz0,
    size_t outerSize,
    ArchiveSummaryInfo* outInfo,
    const std::wstring* extractTargetNorm,
    std::vector<uint8_t>* outExtracted)
{
    if (!sarc || sarcSize < 40u || std::memcmp(sarc, "SARC", 4) != 0) return false;

    bool bigEndian = false;
    if (sarc[6u] == 0xFFu && sarc[7u] == 0xFEu) {
        bigEndian = false; // 0xFEFF stored LE
    } else if (sarc[6u] == 0xFEu && sarc[7u] == 0xFFu) {
        bigEndian = true;  // 0xFEFF stored BE
    } else {
        return false;
    }

    const uint16_t sarcHdrLen = RdSarcU16(sarc + 4u, bigEndian);
    const uint32_t sarcFileSz = RdSarcU32(sarc + 8u, bigEndian);
    const uint32_t dataOffset = RdSarcU32(sarc + 12u, bigEndian);
    if (sarcHdrLen < 20u || sarcHdrLen > sarcSize || dataOffset > sarcSize) return false;
    const size_t effSize = (sarcFileSz >= dataOffset && sarcFileSz <= sarcSize) ? sarcFileSz : sarcSize;

    // SFAT header at sarcHdrLen (12 bytes: "SFAT" + u16 hdrLen + u16 nodeCount + u32 hashKey)
    if (effSize - sarcHdrLen < 12u || std::memcmp(sarc + sarcHdrLen, "SFAT", 4) != 0) return false;
    const uint16_t sfatHdrLen = RdSarcU16(sarc + sarcHdrLen + 4u, bigEndian);
    const uint16_t nodeCount  = RdSarcU16(sarc + sarcHdrLen + 6u, bigEndian);
    if (sfatHdrLen < 12u || nodeCount == 0u || nodeCount > 32768u) return false;

    const size_t nodesOff = static_cast<size_t>(sarcHdrLen) + sfatHdrLen;
    if (nodesOff > effSize) return false;

    // Standard Nintendo SFAT nodes are 16 bytes ([hash:4][nameAttr:4][begin:4][end:4]);
    // also support 12-byte compact nodes if "SFNT" immediately follows 12 * nodeCount.
    size_t nodeStride = 16u;
    size_t sfntOff = 0u;
    if (static_cast<uint64_t>(nodeCount) * 16ULL <= effSize - nodesOff &&
        effSize - (nodesOff + static_cast<size_t>(nodeCount) * 16u) >= 8u &&
        std::memcmp(sarc + nodesOff + static_cast<size_t>(nodeCount) * 16u, "SFNT", 4) == 0)
    {
        nodeStride = 16u;
        sfntOff = nodesOff + static_cast<size_t>(nodeCount) * 16u;
    } else if (static_cast<uint64_t>(nodeCount) * 12ULL <= effSize - nodesOff &&
               effSize - (nodesOff + static_cast<size_t>(nodeCount) * 12u) >= 8u &&
               std::memcmp(sarc + nodesOff + static_cast<size_t>(nodeCount) * 12u, "SFNT", 4) == 0)
    {
        nodeStride = 12u;
        sfntOff = nodesOff + static_cast<size_t>(nodeCount) * 12u;
    } else {
        return false;
    }

    const uint16_t sfntHdrLen = RdSarcU16(sarc + sfntOff + 4u, bigEndian);
    if (sfntHdrLen < 8u || sfntHdrLen > effSize - sfntOff) return false;
    const size_t sfntStrBase = sfntOff + sfntHdrLen;
    if (sfntStrBase > dataOffset) return false;

    if (outInfo) {
        std::wstring fmt = wasYaz0 ? L"Nintendo Yaz0-SARC Archive (.szs" : L"Nintendo SARC Archive (.sarc";
        fmt += bigEndian ? L", BE)" : L", LE)";
        outInfo->formatName = std::move(fmt);
        outInfo->entries.reserve(nodeCount);
    }

    const std::wstring wantNorm = extractTargetNorm ? ArchivePath::Normalize(*extractTargetNorm) : std::wstring{};

    for (uint16_t i = 0; i < nodeCount; ++i) {
        const uint8_t* nd = sarc + nodesOff + static_cast<size_t>(i) * nodeStride;
        uint32_t nameHash = 0u;
        uint32_t nameAttr = 0u;
        uint32_t relBegin = 0u;
        uint32_t relEnd   = 0u;
        if (nodeStride == 16u) {
            nameHash = RdSarcU32(nd + 0u, bigEndian);
            nameAttr = RdSarcU32(nd + 4u, bigEndian);
            relBegin = RdSarcU32(nd + 8u, bigEndian);
            relEnd   = RdSarcU32(nd + 12u, bigEndian);
        } else {
            nameAttr = RdSarcU32(nd + 0u, bigEndian);
            relBegin = RdSarcU32(nd + 4u, bigEndian);
            relEnd   = RdSarcU32(nd + 8u, bigEndian);
        }

        if (relEnd < relBegin || relBegin > effSize - dataOffset || relEnd > effSize - dataOffset) {
            if (outInfo) *outInfo = {};
            return false;
        }
        const uint32_t fileLen = relEnd - relBegin;
        const size_t absFileOff = static_cast<size_t>(dataOffset) + relBegin;

        std::wstring entryPath;
        const bool hasNameFlag = (nodeStride == 12u) || ((nameAttr & 0xFF000000u) != 0u);
        if (hasNameFlag) {
            const size_t strByteOff = sfntStrBase + static_cast<size_t>(nameAttr & 0x00FFFFFFu) * 4u;
            if (strByteOff < dataOffset) {
                const size_t maxStr = dataOffset - strByteOff;
                const size_t len = strnlen(reinterpret_cast<const char*>(sarc + strByteOff), std::min<size_t>(maxStr, 1024u));
                if (len > 0u) {
                    entryPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(
                        std::string(reinterpret_cast<const char*>(sarc + strByteOff), len)));
                }
            }
        }
        if (entryPath.empty()) {
            wchar_t fb[48] = {};
            swprintf_s(fb, L"node_%04u_%08X.bin", static_cast<unsigned>(i), nameHash);
            entryPath = fb;
        }

        if (extractTargetNorm && outExtracted) {
            if (_wcsicmp(entryPath.c_str(), wantNorm.c_str()) == 0) {
                outExtracted->assign(sarc + absFileOff, sarc + absFileOff + fileLen);
                return true;
            }
        } else if (outInfo) {
            ArchiveItemEntry item;
            item.pathInArchive     = std::move(entryPath);
            item.name              = ArchivePath::BaseName(item.pathInArchive);
            item.isDirectory       = false;
            item.uncompressedSize  = fileLen;
            item.compressedSize    = (wasYaz0 && sarcSize > 0u)
                ? ((static_cast<uint64_t>(outerSize) * fileLen) / sarcSize)
                : fileLen;
            item.rawDataOffset     = wasYaz0 ? 0ULL : absFileOff;
            item.compressionMethod = wasYaz0 ? 1u : 0u;
            outInfo->entries.push_back(std::move(item));
        }
    }

    if (extractTargetNorm) return false;
    if (!outInfo || outInfo->entries.empty()) return false;
    outInfo->valid = true;
    return true;
}

} // namespace DetailNintendoSarc

inline bool InspectNintendoSarcArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    if (!data || size < 16u) return false;
    if (std::memcmp(data, "Yaz0", 4) == 0) {
        std::vector<uint8_t> unpacked;
        if (!DetailNintendoSarc::UnwrapYaz0Cached(data, size, unpacked)) return false;
        return DetailNintendoSarc::ParseOrExtractSarcBuffer(
            unpacked.data(), unpacked.size(), true, size, &outInfo, nullptr, nullptr);
    }
    return DetailNintendoSarc::ParseOrExtractSarcBuffer(data, size, false, size, &outInfo, nullptr, nullptr);
}

inline bool ExtractNintendoSarcEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    if (!data || size < 16u) return false;
    if (std::memcmp(data, "Yaz0", 4) == 0) {
        std::vector<uint8_t> unpacked;
        if (!DetailNintendoSarc::UnwrapYaz0Cached(data, size, unpacked)) return false;
        return DetailNintendoSarc::ParseOrExtractSarcBuffer(
            unpacked.data(), unpacked.size(), true, size, nullptr, &normTarget, &outBytes);
    }
    return DetailNintendoSarc::ParseOrExtractSarcBuffer(data, size, false, size, nullptr, &normTarget, &outBytes);
}

} // namespace Explorer2::Formats
