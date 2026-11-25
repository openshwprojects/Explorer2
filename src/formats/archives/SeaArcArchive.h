#pragma once
// SeaArcArchive.h — SEA ARC / PKPAK (0x1A + method 1..9) archive reader & Unpacked/RLE90/Squeeze/Crunch/Squash extractor (.arc).

#include "../common/ArchiveFormatCommon.h"

namespace Explorer2::Formats {

namespace DetailSeaArc {

// Method 4: Richard Greenlaw / CP/M USQ Binary Huffman Squeeze + RLE90
inline bool DecodeArcSqueeze4(
    const uint8_t* src,
    size_t srcLen,
    uint32_t origSize,
    std::vector<uint8_t>& outBytes,
    size_t maxOut = 128u * 1024u * 1024u)
{
    outBytes.clear();
    if (origSize == 0u) return true;
    if (!src || srcLen < 2u || origSize > maxOut) return false;

    const uint16_t numNodes = ByteIO::RdU16LE(src + 0u);
    if (numNodes == 0u || numNodes > 258u) return false;
    const size_t treeBytes = 2u + static_cast<size_t>(numNodes) * 4u;
    if (treeBytes > srcLen) return false;

    struct SqNode {
        int16_t child[2];
    };
    std::vector<SqNode> nodes(numNodes);
    for (uint16_t i = 0; i < numNodes; ++i) {
        const uint8_t* np = src + 2u + static_cast<size_t>(i) * 4u;
        nodes[i].child[0] = static_cast<int16_t>(ByteIO::RdU16LE(np + 0u));
        nodes[i].child[1] = static_cast<int16_t>(ByteIO::RdU16LE(np + 2u));
    }

    std::vector<uint8_t> huffOut;
    huffOut.reserve(std::min<size_t>(origSize + 256u, maxOut));

    constexpr int kSpEof = 256;
    int curNode = 0;
    bool sawEof = false;

    for (size_t byteIdx = treeBytes; byteIdx < srcLen && !sawEof; ++byteIdx) {
        const uint8_t b = src[byteIdx];
        for (int bit = 0; bit < 8; ++bit) {
            const int branch = (b >> bit) & 1;
            if (curNode < 0 || curNode >= static_cast<int>(numNodes)) return false;
            const int next = nodes[static_cast<size_t>(curNode)].child[branch];
            if (next < 0) {
                const int sym = -(next + 1);
                if (sym == kSpEof) {
                    sawEof = true;
                    break;
                }
                if (sym < 0 || sym > 255 || huffOut.size() >= maxOut) return false;
                huffOut.push_back(static_cast<uint8_t>(sym));
                curNode = 0;
            } else {
                curNode = next;
            }
        }
    }

    return DetailUnixAndLegacyArc::DecodeRle90(huffOut.data(), huffOut.size(), outBytes, maxOut, origSize);
}

// Methods 5, 6, 7: SEA ARC 12-bit Fixed-Width Open-Hash LZW (arclzw.c) + optional RLE90
inline bool DecodeArcCrunch12(
    const uint8_t* src,
    size_t srcLen,
    uint8_t method,
    uint32_t origSize,
    std::vector<uint8_t>& outBytes,
    size_t maxOut = 128u * 1024u * 1024u)
{
    outBytes.clear();
    if (origSize == 0u) return true;
    if (!src || srcLen < 2u || origSize > maxOut) return false;

    struct HashEntry {
        uint16_t pred = 0xFFFFu;
        uint16_t next = 0u;
        uint8_t  foll = 0u;
        bool     used = false;
    };

    std::vector<HashEntry> tab(4096u);
    uint32_t entryCount = 0;
    size_t freeScan = 0;

    auto hashFunc = [method](uint16_t pred, uint8_t foll) noexcept -> uint16_t {
        if (method == 7u) {
            // SEA ARC 4.6 new hash
            const uint32_t sum = (static_cast<uint32_t>(pred) + foll + 1u) & 0xFFFFu;
            return static_cast<uint16_t>(((sum * 15073u) >> 4u) & 0x0FFFu);
        }
        // SEA ARC 4.0 old hash: (((pred + foll) | 0x0800) ^ ... ) or classic ((pred + foll) | 0x0800) & 0xFFF
        const uint32_t local = (static_cast<uint32_t>(pred) + foll) | 0x0800u;
        return static_cast<uint16_t>(((local * local) >> 6u) & 0x0FFFu);
    };

    auto updateTable = [&](uint16_t pred, uint8_t foll) {
        if (entryCount >= 4096u) return;
        const uint16_t h = hashFunc(pred, foll);
        if (!tab[h].used) {
            tab[h].used = true;
            tab[h].pred = pred;
            tab[h].foll = foll;
            tab[h].next = 0u;
            ++entryCount;
            return;
        }
        uint16_t cur = h;
        int guard = 0;
        while (tab[cur].next != 0u && guard++ < 4096) {
            cur = tab[cur].next;
        }
        while (freeScan < 4096u && tab[freeScan].used) ++freeScan;
        if (freeScan >= 4096u) return;
        tab[cur].next = static_cast<uint16_t>(freeScan);
        tab[freeScan].used = true;
        tab[freeScan].pred = pred;
        tab[freeScan].foll = foll;
        tab[freeScan].next = 0u;
        ++freeScan;
        ++entryCount;
    };

    for (uint32_t c = 0; c < 256u; ++c) {
        updateTable(0xFFFFu, static_cast<uint8_t>(c));
    }

    size_t bytePos = 0;
    bool oddNibble = false;
    uint8_t savedByte = 0;
    auto getCode12 = [&]() -> int32_t {
        if (!oddNibble) {
            if (srcLen - bytePos < 2u) return -1;
            const uint8_t b0 = src[bytePos++];
            savedByte = src[bytePos++];
            oddNibble = true;
            return static_cast<int32_t>((static_cast<uint32_t>(b0) << 4u) | (savedByte >> 4u));
        } else {
            if (bytePos >= srcLen) return -1;
            const uint8_t b2 = src[bytePos++];
            oddNibble = false;
            return static_cast<int32_t>((static_cast<uint32_t>(savedByte & 0x0Fu) << 8u) | b2);
        }
    };

    std::vector<uint8_t> lzwOut;
    lzwOut.reserve(std::min<size_t>(origSize + 256u, maxOut));
    std::vector<uint8_t> stack(4112u, 0u);

    const int32_t firstCode = getCode12();
    if (firstCode < 0 || !tab[static_cast<size_t>(firstCode)].used) return false;
    uint16_t oldCode = static_cast<uint16_t>(firstCode);
    uint8_t finChar  = tab[oldCode].foll;
    lzwOut.push_back(finChar);

    while (lzwOut.size() < maxOut) {
        if (method == 5u && lzwOut.size() >= origSize) break;
        const int32_t raw = getCode12();
        if (raw < 0) break;
        const uint16_t inCode = static_cast<uint16_t>(raw);

        size_t sp = 0;
        uint16_t code = inCode;
        if (!tab[code].used) {
            stack[sp++] = finChar;
            code = oldCode;
        }
        while (tab[code].pred != 0xFFFFu) {
            if (!tab[code].used || sp >= 4096u) return false;
            stack[sp++] = tab[code].foll;
            code = tab[code].pred;
        }
        if (!tab[code].used) return false;
        finChar = tab[code].foll;
        stack[sp++] = finChar;

        for (size_t k = sp; k > 0u && lzwOut.size() < maxOut; --k) {
            lzwOut.push_back(stack[k - 1u]);
            if (method == 5u && lzwOut.size() >= origSize) break;
        }

        updateTable(oldCode, finChar);
        oldCode = inCode;
    }

    if (method == 5u) {
        outBytes = std::move(lzwOut);
        return outBytes.size() == origSize;
    }
    return DetailUnixAndLegacyArc::DecodeRle90(lzwOut.data(), lzwOut.size(), outBytes, maxOut, origSize);
}

struct SeaArcEntryRecord {
    std::wstring pathInArchive;
    uint32_t origSize = 0;
    uint32_t compSize = 0;
    size_t   dataOffset = 0;
    uint16_t crc16 = 0;
    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    uint8_t  method = 0; // 1..9 (or 10 PKPAK Squash)
};

inline bool ValidateSeaArcNameField(const uint8_t* field13, std::string& outName) {
    outName.clear();
    size_t nulIdx = SIZE_MAX;
    for (size_t i = 0; i < 13u; ++i) {
        if (field13[i] == 0u) {
            nulIdx = i;
            break;
        }
    }
    if (nulIdx == 0u || nulIdx == SIZE_MAX) return false;
    if (field13[0] <= 0x20u || field13[0] >= 0x7Fu) return false;
    for (size_t i = 0; i < nulIdx; ++i) {
        const uint8_t c = field13[i];
        if (c < 0x20u || c >= 0x7Fu) return false;
    }
    outName.assign(reinterpret_cast<const char*>(field13), nulIdx);
    return true;
}

inline bool EnumerateSeaArcEntries(const uint8_t* data, size_t size, std::vector<SeaArcEntryRecord>& outEntries) {
    outEntries.clear();
    if (!data || size < 27u || data[0] != 0x1Au) return false;

    size_t pos = 0;
    bool sawEndMarker = false;

    while (pos < size && outEntries.size() < 32768u) {
        if (size - pos < 2u) {
            // Allow minor 1-byte EOF pad only if we already parsed valid entries
            break;
        }
        if (data[pos] != 0x1Au) return false;

        const uint8_t method = data[pos + 1u];
        if (method == 0u) { // 0x1A 0x00 EOF marker
            sawEndMarker = true;
            break;
        }
        // Standard SEA ARC / PKPAK methods 1..10 (exclude FreeArc 'ArC\x01' where data[0]=='A' anyway)
        if (method > 10u) return false;

        const size_t hdrLen = (method == 1u) ? 25u : 29u;
        if (size - pos < hdrLen) return false;

        std::string rawName;
        if (!ValidateSeaArcNameField(data + pos + 2u, rawName)) return false;

        const uint32_t compSz  = ByteIO::RdU32LE(data + pos + 15u);
        const uint16_t dosDate = ByteIO::RdU16LE(data + pos + 19u);
        const uint16_t dosTime = ByteIO::RdU16LE(data + pos + 21u);
        const uint16_t crc16   = ByteIO::RdU16LE(data + pos + 23u);
        const uint32_t origSz  = (method == 1u) ? compSz : ByteIO::RdU32LE(data + pos + 25u);

        const size_t payloadOff = pos + hdrLen;
        if (compSz > size - payloadOff) return false;
        if ((method == 1u || method == 2u) && compSz != origSz) return false;

        const std::wstring normPath = ArchivePath::Normalize(PathUtils::Utf8ToWide(rawName));
        if (normPath.empty()) return false;

        SeaArcEntryRecord rec;
        rec.pathInArchive = normPath;
        rec.origSize      = origSz;
        rec.compSize      = compSz;
        rec.dataOffset    = payloadOff;
        rec.crc16         = crc16;
        rec.dosDate       = dosDate;
        rec.dosTime       = dosTime;
        rec.method        = method;
        outEntries.push_back(std::move(rec));

        pos = payloadOff + compSz;
    }

    (void)sawEndMarker;
    return !outEntries.empty();
}

} // namespace DetailSeaArc

inline bool InspectSeaArcArchive(const uint8_t* data, size_t size, ArchiveSummaryInfo& outInfo) {
    std::vector<DetailSeaArc::SeaArcEntryRecord> entries;
    if (!DetailSeaArc::EnumerateSeaArcEntries(data, size, entries)) return false;

    outInfo.formatName = L"SEA ARC / PKPAK Archive (.arc)";
    outInfo.entries.reserve(entries.size());
    for (const auto& r : entries) {
        ArchiveItemEntry item;
        item.pathInArchive     = r.pathInArchive;
        item.name              = ArchivePath::BaseName(r.pathInArchive);
        item.uncompressedSize  = r.origSize;
        item.compressedSize    = r.compSize;
        item.crc32             = r.crc16;
        item.dosDate           = r.dosDate;
        item.dosTime           = r.dosTime;
        item.isDirectory       = false;
        item.rawDataOffset     = (r.method == 1u || r.method == 2u) ? r.dataOffset : 0ULL;
        item.compressionMethod = (r.method <= 2u) ? 0u : r.method;
        outInfo.entries.push_back(std::move(item));
    }
    outInfo.valid = !outInfo.entries.empty();
    return outInfo.valid;
}

inline bool ExtractSeaArcEntry(
    const uint8_t* data,
    size_t size,
    const std::wstring& normTarget,
    std::vector<uint8_t>& outBytes)
{
    std::vector<DetailSeaArc::SeaArcEntryRecord> entries;
    if (!DetailSeaArc::EnumerateSeaArcEntries(data, size, entries)) return false;

    const std::wstring want = ArchivePath::Normalize(normTarget);
    for (const auto& r : entries) {
        if (_wcsicmp(r.pathInArchive.c_str(), want.c_str()) != 0) continue;
        if (r.origSize > 128u * 1024u * 1024u) return false;
        if (r.origSize == 0u) {
            outBytes.clear();
            return true;
        }
        if (r.dataOffset > size || r.compSize > size - r.dataOffset) return false;

        const uint8_t* src = data + r.dataOffset;
        const size_t cLen  = r.compSize;
        bool ok = false;

        if (r.method == 1u || r.method == 2u) { // Unpacked
            if (cLen != r.origSize) return false;
            outBytes.assign(src, src + r.origSize);
            ok = true;
        } else if (r.method == 3u) { // RLE90 Pack
            ok = DetailUnixAndLegacyArc::DecodeRle90(src, cLen, outBytes, 128u * 1024u * 1024u, r.origSize);
        } else if (r.method == 4u) { // Huffman Squeeze + RLE90
            ok = DetailSeaArc::DecodeArcSqueeze4(src, cLen, r.origSize, outBytes);
        } else if (r.method == 5u || r.method == 6u || r.method == 7u) { // 12-bit Crunch
            ok = DetailSeaArc::DecodeArcCrunch12(src, cLen, r.method, r.origSize, outBytes);
            // Fallback for third-party sequential 12-bit LZW Crunch tools if CRC/decode didn't match
            if (!ok || DetailUnixAndLegacyArc::Crc16Arc(outBytes.data(), outBytes.size()) != r.crc16) {
                std::vector<uint8_t> lzwTmp;
                if (r.method == 5u) {
                    ok = DetailUnixAndLegacyArc::DecodeUnixCompressLzw(src, cLen, 12, false, outBytes, 128u * 1024u * 1024u, r.origSize);
                } else if (DetailUnixAndLegacyArc::DecodeUnixCompressLzw(src, cLen, 12, false, lzwTmp, 128u * 1024u * 1024u)) {
                    ok = DetailUnixAndLegacyArc::DecodeRle90(lzwTmp.data(), lzwTmp.size(), outBytes, 128u * 1024u * 1024u, r.origSize);
                }
            }
        } else if (r.method == 8u) { // Dynamic 9..12-bit LZW + RLE90 (byte 0 = maxBits)
            if (cLen < 2u) return false;
            const int maxBits = static_cast<int>(src[0] & 0x1Fu);
            if (maxBits < 9 || maxBits > 16) return false;
            std::vector<uint8_t> lzwOut;
            if (DetailUnixAndLegacyArc::DecodeUnixCompressLzw(src + 1u, cLen - 1u, maxBits, true, lzwOut, 128u * 1024u * 1024u)) {
                ok = DetailUnixAndLegacyArc::DecodeRle90(lzwOut.data(), lzwOut.size(), outBytes, 128u * 1024u * 1024u, r.origSize);
            }
        } else if (r.method == 9u || r.method == 10u) { // 13-bit Squash (UNIX compress LZW without RLE90)
            ok = DetailUnixAndLegacyArc::DecodeUnixCompressLzw(src, cLen, 13, true, outBytes, 128u * 1024u * 1024u, r.origSize);
        } else {
            return false;
        }

        if (!ok || outBytes.size() != r.origSize) {
            outBytes.clear();
            return false;
        }
        if (DetailUnixAndLegacyArc::Crc16Arc(outBytes.data(), outBytes.size()) != r.crc16) {
            outBytes.clear();
            return false;
        }
        return true;
    }
    return false;
}

} // namespace Explorer2::Formats
