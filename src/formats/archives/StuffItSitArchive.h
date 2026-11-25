#pragma once
// StuffItSitArchive.h — Aladdin StuffIt Classic ("SIT!"/"ST46"/"ST50"/"STin"/"STi2"/"STi3"/"STi4" + "rLau")
// reader & Stored / RLE90 / 14-bit LZW / Huffman / LZAH / Deflate data-fork extractor (.sit).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailStuffItSit {

inline std::wstring DecodeMacRomanName(const uint8_t* ptr, size_t len) {
    static constexpr uint16_t kMacRomanHigh[128] = {
        0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
        0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
        0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
        0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
        0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
        0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
        0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
        0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7
    };
    std::wstring out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = ptr[i];
        if (b == 0u) break;
        wchar_t wc = (b < 0x80u) ? static_cast<wchar_t>(b) : static_cast<wchar_t>(kMacRomanHigh[b - 0x80u]);
        if (wc == L'/' || wc == L'\\' || wc == L':') wc = L'_';
        if (wc >= 0x20u) out.push_back(wc);
    }
    return out;
}

inline bool IsSitTopSignature(const uint8_t* p) noexcept {
    return std::memcmp(p, "SIT!", 4) == 0 ||
           std::memcmp(p, "ST46", 4) == 0 ||
           std::memcmp(p, "ST50", 4) == 0 ||
           std::memcmp(p, "STin", 4) == 0 ||
           std::memcmp(p, "STi2", 4) == 0 ||
           std::memcmp(p, "STi3", 4) == 0 ||
           std::memcmp(p, "STi4", 4) == 0;
}

// StuffIt Classic Method 3: MSB-first serialized binary prefix Huffman tree + symbol stream
inline bool DecodeStuffItHuffman3(
    const uint8_t* src,
    size_t srcLen,
    uint32_t expectedOut,
    std::vector<uint8_t>& outBytes,
    size_t maxOut = 128u * 1024u * 1024u)
{
    outBytes.clear();
    if (expectedOut == 0u) return true;
    if (!src || srcLen == 0u || expectedOut > maxOut) return false;

    struct HuffNode {
        int left = -1;
        int right = -1;
        uint8_t sym = 0u;
        bool isLeaf = false;
    };
    std::vector<HuffNode> nodes;
    nodes.reserve(515u);

    DetailUnixAndLegacyArc::LhaMsbBitReader br(src, srcLen);
    auto readTree = [&](auto& self, int depth) -> int {
        if (depth > 40 || nodes.size() >= 1024u) return -1;
        const uint32_t isLeafBit = br.ReadBits(1u);
        if (br.overread) return -1;
        const int idx = static_cast<int>(nodes.size());
        nodes.push_back(HuffNode{});
        if (isLeafBit != 0u) {
            const uint32_t sym = br.ReadBits(8u);
            if (br.overread) return -1;
            nodes[static_cast<size_t>(idx)].isLeaf = true;
            nodes[static_cast<size_t>(idx)].sym = static_cast<uint8_t>(sym);
            return idx;
        }
        const int lc = self(self, depth + 1);
        if (lc < 0) return -1;
        const int rc = self(self, depth + 1);
        if (rc < 0) return -1;
        nodes[static_cast<size_t>(idx)].left  = lc;
        nodes[static_cast<size_t>(idx)].right = rc;
        return idx;
    };

    const int root = readTree(readTree, 0);
    if (root < 0 || nodes.empty()) return false;

    outBytes.reserve(std::min<size_t>(expectedOut, srcLen * 8u + 256u));
    if (nodes[static_cast<size_t>(root)].isLeaf) {
        outBytes.assign(expectedOut, nodes[static_cast<size_t>(root)].sym);
        return true;
    }

    while (outBytes.size() < expectedOut) {
        int cur = root;
        while (!nodes[static_cast<size_t>(cur)].isLeaf) {
            const uint32_t bit = br.ReadBits(1u);
            if (br.overread) {
                outBytes.clear();
                return false;
            }
            cur = (bit == 0u) ? nodes[static_cast<size_t>(cur)].left : nodes[static_cast<size_t>(cur)].right;
            if (cur < 0 || static_cast<size_t>(cur) >= nodes.size()) {
                outBytes.clear();
                return false;
            }
        }
        outBytes.push_back(nodes[static_cast<size_t>(cur)].sym);
    }

    return outBytes.size() == expectedOut;
}

struct SitEntryRecord {
    std::wstring pathInArchive;
    uint32_t dataUncompLen = 0;
    uint32_t dataCompLen = 0;
    size_t   dataOffset = 0;
    uint16_t dataCrc16 = 0;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    uint8_t  dataMethod = 0;
    bool     isDirectory = false;
    bool     isEncrypted = false;
};

inline bool EnumerateSitEntries(const uint8_t* data, size_t size, std::vector<SitEntryRecord>& outEntries) {
    outEntries.clear();
    if (!data || size < 22u) return false;

    // Support raw .sit at offset 0 as well as 128-byte MacBinary-wrapped .sit
    size_t baseOff = SIZE_MAX;
    if (IsSitTopSignature(data) && std::memcmp(data + 10u, "rLau", 4) == 0) {
        baseOff = 0u;
    } else if (size >= 150u && data[0] == 0u && data[1] > 0u && data[1] <= 63u &&
               IsSitTopSignature(data + 128u) && std::memcmp(data + 138u, "rLau", 4) == 0)
    {
        baseOff = 128u;
    }
    if (baseOff == SIZE_MAX) return false;

    const uint16_t rootItems = ByteIO::RdU16BE(data + baseOff + 4u);
    const uint32_t arcLen    = ByteIO::RdU32BE(data + baseOff + 6u);
    const size_t   arcEnd    = (arcLen >= 22u && baseOff + arcLen <= size) ? (baseOff + arcLen) : size;

    size_t pos = baseOff + 22u;
    std::vector<std::wstring> folderStack;
    uint32_t rootSeen = 0;

    while (arcEnd - pos >= 112u && outEntries.size() < 32768u) {
        if (folderStack.empty() && rootItems > 0u && rootSeen >= rootItems) {
            break;
        }

        const uint8_t* hdr = data + pos;
        const uint16_t savedHdrCrc = ByteIO::RdU16BE(hdr + 110u);
        const uint16_t calcHdrCrc  = DetailUnixAndLegacyArc::Crc16Arc(hdr, 110u);
        if (calcHdrCrc != savedHdrCrc) {
            // Stop cleanly if trailing resource fork / MacBinary padding follows rootItems
            if (!outEntries.empty() && folderStack.empty()) break;
            return false;
        }

        const uint8_t rMethodRaw = hdr[0u];
        const uint8_t dMethodRaw = hdr[1u];
        const uint8_t nameLen    = std::min<uint8_t>(hdr[2u], 63u);
        const uint32_t modMacSec = ByteIO::RdU32BE(hdr + 80u);
        const uint32_t rUncomp   = ByteIO::RdU32BE(hdr + 84u);
        const uint32_t dUncomp   = ByteIO::RdU32BE(hdr + 88u);
        const uint32_t rComp     = ByteIO::RdU32BE(hdr + 92u);
        const uint32_t dComp     = ByteIO::RdU32BE(hdr + 96u);
        const uint16_t dCrc16    = ByteIO::RdU16BE(hdr + 102u);

        uint16_t dosDate = 0, dosTime = 0;
        if (modMacSec > 2082844800u) {
            DetailDiskAndGameArc::UnixSecondsToDosDateTime(modMacSec - 2082844800u, dosDate, dosTime);
        }

        pos += 112u;
        const uint8_t rLow = static_cast<uint8_t>(rMethodRaw & 0x7Fu);
        const uint8_t dLow = static_cast<uint8_t>(dMethodRaw & 0x7Fu);

        // Folder End marker (33)
        if (rLow == 33u || dLow == 33u) {
            if (!folderStack.empty()) {
                folderStack.pop_back();
            }
            continue;
        }

        const std::wstring wName = DecodeMacRomanName(hdr + 3u, nameLen);

        // Folder Start marker (32)
        if (rLow == 32u || dLow == 32u) {
            if (folderStack.empty()) ++rootSeen;
            const std::wstring dirName = wName.empty() ? L"Folder" : wName;
            const std::wstring fullDir = ArchivePath::Normalize(
                folderStack.empty() ? dirName : (folderStack.back() + L"/" + dirName));
            if (!fullDir.empty()) {
                SitEntryRecord dirRec;
                dirRec.pathInArchive = fullDir;
                dirRec.dosDate       = dosDate;
                dirRec.dosTime       = dosTime;
                dirRec.isDirectory   = true;
                outEntries.push_back(std::move(dirRec));
                folderStack.push_back(fullDir);
            }
            continue;
        }

        if (folderStack.empty()) ++rootSeen;

        if (rComp > arcEnd - pos) return false;
        const size_t dataForkOff = pos + rComp;
        if (dComp > arcEnd - dataForkOff) return false;
        pos = dataForkOff + dComp;

        if (!wName.empty()) {
            const std::wstring fullPath = ArchivePath::Normalize(
                folderStack.empty() ? wName : (folderStack.back() + L"/" + wName));
            if (!fullPath.empty()) {
                SitEntryRecord rec;
                rec.pathInArchive = fullPath;
                rec.dataUncompLen = dUncomp;
                rec.dataCompLen   = dComp;
                rec.dataOffset    = dataForkOff;
                rec.dataCrc16     = dCrc16;
                rec.dosDate       = dosDate;
                rec.dosTime       = dosTime;
                rec.dataMethod    = static_cast<uint8_t>(dMethodRaw & 0x1Fu);
                rec.isDirectory   = false;
                rec.isEncrypted   = (dMethodRaw & 0x80u) != 0u;
                outEntries.push_back(std::move(rec));
            }
        }
        (void)rUncomp;
    }

    return !outEntries.empty();
}

} // namespace DetailStuffItSit

inline bool InspectStuffItSitArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::vector<DetailStuffItSit::SitEntryRecord> entries;
    if (!DetailStuffItSit::EnumerateSitEntries(data, size, entries)) return false;

    outInfo.formatName = L"Aladdin StuffIt Classic Archive (.sit)";
    outInfo.entries.reserve(entries.size());
    for (const auto& r : entries) {
        ArchiveItemEntry item;
        item.pathInArchive     = r.pathInArchive;
        item.name              = ArchivePath::BaseName(r.pathInArchive);
        item.uncompressedSize  = r.dataUncompLen;
        item.compressedSize    = r.dataCompLen;
        item.crc32             = r.dataCrc16;
        item.dosDate           = r.dosDate;
        item.dosTime           = r.dosTime;
        item.isDirectory       = r.isDirectory;
        item.rawDataOffset     = (r.dataMethod == 0u && !r.isEncrypted && !r.isDirectory) ? r.dataOffset : 0ULL;
        item.compressionMethod = r.isEncrypted ? 99u : r.dataMethod;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractStuffItSitEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<DetailStuffItSit::SitEntryRecord> entries;
    if (!DetailStuffItSit::EnumerateSitEntries(data, size, entries)) return false;

    const std::wstring want = ArchivePath::Normalize(normTarget);
    for (const auto& r : entries) {
        if (r.isDirectory || _wcsicmp(r.pathInArchive.c_str(), want.c_str()) != 0) continue;
        if (r.isEncrypted || r.dataUncompLen > 128u * 1024u * 1024u) return false;
        if (r.dataUncompLen == 0u) {
            outBytes.clear();
            return true;
        }
        if (r.dataOffset > size || r.dataCompLen > size - r.dataOffset) return false;

        const uint8_t* src = data + r.dataOffset;
        const size_t cLen  = r.dataCompLen;
        const uint32_t uLen = r.dataUncompLen;
        bool ok = false;

        if (r.dataMethod == 0u) { // Stored
            if (cLen < uLen) return false;
            outBytes.assign(src, src + uLen);
            ok = true;
        } else if (r.dataMethod == 1u) { // RLE90
            ok = DetailUnixAndLegacyArc::DecodeRle90(src, cLen, outBytes, 128u * 1024u * 1024u, uLen);
        } else if (r.dataMethod == 2u) { // 14-bit UNIX compress LZW
            ok = DetailUnixAndLegacyArc::DecodeUnixCompressLzw(src, cLen, 14, true, outBytes, 128u * 1024u * 1024u, uLen);
        } else if (r.dataMethod == 3u) { // StuffIt MSB prefix-tree Huffman
            ok = DetailStuffItSit::DecodeStuffItHuffman3(src, cLen, uLen, outBytes);
        } else if (r.dataMethod == 5u || r.dataMethod == 13u) { // LZAH / LZH block-adaptive Huffman+LZ77
            ok = DetailUnixAndLegacyArc::DecodeLhaArjBlockHuff(src, cLen, uLen, 14u, 4u, outBytes, 0x20u) ||
                 DetailUnixAndLegacyArc::DecodeLhaArjBlockHuff(src, cLen, uLen, 15u, 4u, outBytes, 0x20u) ||
                 DetailUnixAndLegacyArc::DecodeLhaArjBlockHuff(src, cLen, uLen, 17u, 5u, outBytes, 0x20u);
        } else if (r.dataMethod == 14u) { // Deflate
            ok = (ZipDeflate::InflateRaw(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen) ||
                 (ZipDeflate::InflateZlib(src, cLen, outBytes, uLen + 64u) && outBytes.size() == uLen);
        } else {
            return false;
        }

        if (!ok || outBytes.size() != uLen) {
            outBytes.clear();
            return false;
        }
        if (DetailUnixAndLegacyArc::Crc16Arc(outBytes.data(), outBytes.size()) != r.dataCrc16) {
            outBytes.clear();
            return false;
        }
        return true;
    }
    return false;
}

} // namespace Explorer2::Formats
