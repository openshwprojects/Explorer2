#include "ZipDeflate.h"
#include "PathUtils.h"
#include "ByteIO.h"
#include "StreamCodecs.h"
#include "formats/common/ArchiveFormatCommon.h"
#include <algorithm>
#include <cstring>

namespace Explorer2::ZipDeflate {

namespace {

struct BitStream {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;
    bool overrun = false;

    void Fill(int needBits) {
        while (bitCount < needBits && bytePos < size) {
            bitBuf |= static_cast<uint32_t>(data[bytePos++]) << bitCount;
            bitCount += 8;
        }
    }

    uint32_t PeekBits(int n) {
        Fill(n);
        return bitBuf & ((1u << n) - 1u);
    }

    void DropBits(int n) {
        bitBuf >>= n;
        bitCount -= n;
        if (bitCount < 0) {
            bitCount = 0;
            overrun = true;
        }
    }

    uint32_t ReadBits(int n) {
        if (n <= 0) return 0;
        Fill(n);
        uint32_t val = bitBuf & ((1u << n) - 1u);
        DropBits(n);
        return val;
    }

    void AlignToByte() {
        int rem = bitCount & 7;
        if (rem > 0) DropBits(rem);
    }

    size_t ConsumedBytes() const {
        const size_t unreadWhole = static_cast<size_t>(bitCount / 8);
        return (bytePos >= unreadWhole) ? (bytePos - unreadWhole) : 0u;
    }

    bool HasBytes(size_t count) const {
        size_t unreadInBuf = static_cast<size_t>(bitCount / 8);
        size_t totalAvail = (size - bytePos) + unreadInBuf;
        return totalAvail >= count;
    }

    uint8_t ReadAlignedByte() {
        if (bitCount >= 8) {
            uint8_t b = static_cast<uint8_t>(bitBuf & 0xFFu);
            DropBits(8);
            return b;
        }
        if (bytePos < size) return data[bytePos++];
        overrun = true;
        return 0;
    }
};

struct HuffmanTable {
    static constexpr int kMaxBits = 15;
    static constexpr int kFastBits = 10;
    uint16_t counts[kMaxBits + 1] = {};
    uint16_t symbols[320] = {};
    uint16_t fast[1u << kFastBits] = {};
    int maxCodeLen = 0;

    bool Build(const uint8_t* lengths, int numSymbols) {
        std::memset(counts, 0, sizeof(counts));
        std::memset(fast, 0, sizeof(fast));
        maxCodeLen = 0;
        for (int i = 0; i < numSymbols; ++i) {
            uint8_t len = lengths[i];
            if (len > kMaxBits) return false;
            if (len > 0) {
                counts[len]++;
                if (len > maxCodeLen) maxCodeLen = len;
            }
        }
        if (maxCodeLen == 0) return true;

        int room = 1;
        for (int bits = 1; bits <= kMaxBits; ++bits) {
            room = (room << 1) - counts[bits];
            if (room < 0) return false;
        }

        uint16_t offsets[kMaxBits + 1] = {};
        uint16_t sum = 0;
        for (int bits = 1; bits <= kMaxBits; ++bits) {
            offsets[bits] = sum;
            sum += counts[bits];
        }
        if (sum > 320) return false;

        for (int i = 0; i < numSymbols; ++i) {
            uint8_t len = lengths[i];
            if (len > 0) {
                symbols[offsets[len]++] = static_cast<uint16_t>(i);
            }
        }

        uint16_t nextCode[kMaxBits + 1] = {};
        uint32_t code = 0;
        for (int bits = 1; bits <= kMaxBits; ++bits) {
            code = (code + counts[bits - 1]) << 1;
            nextCode[bits] = static_cast<uint16_t>(code);
        }
        for (int i = 0; i < numSymbols; ++i) {
            int len = lengths[i];
            if (len == 0) continue;
            uint32_t c = nextCode[len]++;
            if (len > kFastBits) continue;
            uint32_t rev = 0;
            for (int b = 0; b < len; ++b) rev = (rev << 1) | ((c >> b) & 1u);
            uint16_t entry = static_cast<uint16_t>((len << 9) | i);
            for (uint32_t idx = rev; idx < (1u << kFastBits); idx += (1u << len)) fast[idx] = entry;
        }
        return true;
    }

    int Decode(BitStream& bs) const {
        bs.Fill(kMaxBits);
        const uint32_t peek = bs.bitBuf;
        uint16_t entry = fast[peek & ((1u << kFastBits) - 1u)];
        if (entry != 0) {
            bs.DropBits(entry >> 9);
            return entry & 0x1FF;
        }
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= maxCodeLen; ++len) {
            code |= static_cast<int>((peek >> (len - 1)) & 1u);
            int count = counts[len];
            if (code - count < first) {
                bs.DropBits(len);
                return symbols[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

static const uint16_t kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint8_t kCodeLenOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

inline uint16_t ReadLE16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t ReadLE32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline uint64_t ReadLE64(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64LE(p); }

// Parses ZIP64 (0x0001) and Info-ZIP Unicode Path (0x7075) extra fields on Central Directory or Local File Headers
void ApplyZipExtraFields(
    const uint8_t* extra,
    size_t extraLen,
    bool isLocalHeader,
    uint32_t rawUncomp32,
    uint32_t rawComp32,
    uint32_t rawOff32,
    uint64_t& ioUncompSz,
    uint64_t& ioCompSz,
    uint64_t& ioLocalHdrOff,
    std::string& ioUtf8Name)
{
    size_t p = 0;
    while (p + 4u <= extraLen) {
        const uint16_t hdrId = ReadLE16(extra + p);
        const uint16_t dSize = ReadLE16(extra + p + 2u);
        p += 4u;
        if (dSize > extraLen - p) break;
        const uint8_t* field = extra + p;

        if (hdrId == 0x0001u) { // ZIP64 Extended Information Extra Field
            size_t fp = 0;
            if (isLocalHeader) {
                if (rawUncomp32 == 0xFFFFFFFFu && fp + 8u <= dSize) {
                    ioUncompSz = ReadLE64(field + fp);
                    fp += 8u;
                }
                if (rawComp32 == 0xFFFFFFFFu && fp + 8u <= dSize) {
                    ioCompSz = ReadLE64(field + fp);
                    fp += 8u;
                }
            } else {
                if (rawUncomp32 == 0xFFFFFFFFu && fp + 8u <= dSize) {
                    ioUncompSz = ReadLE64(field + fp);
                    fp += 8u;
                }
                if (rawComp32 == 0xFFFFFFFFu && fp + 8u <= dSize) {
                    ioCompSz = ReadLE64(field + fp);
                    fp += 8u;
                }
                if (rawOff32 == 0xFFFFFFFFu && fp + 8u <= dSize) {
                    ioLocalHdrOff = ReadLE64(field + fp);
                    fp += 8u;
                }
            }
        } else if (hdrId == 0x7075u && dSize > 5u && field[0] == 1u) { // Info-ZIP Unicode Path
            ioUtf8Name.assign(reinterpret_cast<const char*>(field + 5u), dSize - 5u);
        }
        p += dSize;
    }
}

bool ParseZipCentralDirSlice(
    const uint8_t* cdBuf,
    size_t cdLen,
    uint64_t expectedEntriesHint,
    std::vector<ArchiveItemMeta>& outEntries)
{
    size_t pos = 0;
    if (expectedEntriesHint > 0 && expectedEntriesHint <= 262144ULL) {
        outEntries.reserve(static_cast<size_t>(expectedEntriesHint));
    }
    while (pos + 46u <= cdLen) {
        if (ReadLE32(cdBuf + pos) != 0x02014B50u) break;
        const uint16_t flags       = ReadLE16(cdBuf + pos + 8);
        const uint16_t method      = ReadLE16(cdBuf + pos + 10);
        const uint16_t modTime     = ReadLE16(cdBuf + pos + 12);
        const uint16_t modDate     = ReadLE16(cdBuf + pos + 14);
        const uint32_t crc         = ReadLE32(cdBuf + pos + 16);
        const uint32_t compSz32    = ReadLE32(cdBuf + pos + 20);
        const uint32_t uncompSz32  = ReadLE32(cdBuf + pos + 24);
        const uint16_t nameLen     = ReadLE16(cdBuf + pos + 28);
        const uint16_t extraLen    = ReadLE16(cdBuf + pos + 30);
        const uint16_t commentLen  = ReadLE16(cdBuf + pos + 32);
        const uint32_t extAttr     = ReadLE32(cdBuf + pos + 38);
        const uint32_t localOff32  = ReadLE32(cdBuf + pos + 42);

        if (nameLen > cdLen - (pos + 46u)) break;
        const size_t extraOff = pos + 46u + nameLen;
        if (extraLen > cdLen - extraOff) break;
        const size_t nextOff = extraOff + extraLen;
        if (commentLen > cdLen - nextOff) break;

        std::string rawName(reinterpret_cast<const char*>(cdBuf + pos + 46u), nameLen);
        uint64_t uncompSz    = uncompSz32;
        uint64_t compSz      = compSz32;
        uint64_t localHdrOff = localOff32;
        if (extraLen > 0u) {
            ApplyZipExtraFields(
                cdBuf + extraOff, extraLen, false,
                uncompSz32, compSz32, localOff32,
                uncompSz, compSz, localHdrOff, rawName);
        }

        std::wstring wName = PathUtils::Utf8ToWide(rawName);
        for (wchar_t& ch : wName) {
            if (ch == L'\\') ch = L'/';
        }

        ArchiveItemMeta meta;
        meta.isDirectory = (!wName.empty() && wName.back() == L'/') || ((extAttr & 0x10u) != 0);
        while (!wName.empty() && wName.back() == L'/') wName.pop_back();
        meta.pathInArchive = wName;
        size_t slash = wName.find_last_of(L'/');
        meta.name = (slash != std::wstring::npos) ? wName.substr(slash + 1) : wName;
        meta.uncompressedSize = uncompSz;
        meta.compressedSize = compSz;
        meta.localHeaderOffset = localHdrOff;
        meta.crc32 = crc;
        meta.compressionMethod = method;
        meta.generalFlags = flags;
        meta.dosTime = modTime;
        meta.dosDate = modDate;
        meta.isEncrypted = (flags & 0x01u) != 0;

        if (!meta.pathInArchive.empty()) {
            outEntries.push_back(std::move(meta));
        }
        pos = nextOff + commentLen;
    }
    return !outEntries.empty() || expectedEntriesHint == 0;
}

bool DecompressZipPayloadBuffer(
    uint16_t method,
    uint16_t flags,
    const uint8_t* payload,
    size_t compSz,
    uint64_t expectedUncompSz,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes)
{
    outBytes.clear();
    if (expectedUncompSz > maxOutputBytes) return false;
    if (method == 0u) { // Stored
        if (compSz > maxOutputBytes) return false;
        outBytes.assign(payload, payload + compSz);
        return true;
    }
    if (method == 8u) { // Deflate
        return InflateRaw(payload, compSz, outBytes, maxOutputBytes);
    }
    if (method == 12u) { // BZip2
        return StreamCodecs::DecompressBzip2Stream(payload, compSz, outBytes, maxOutputBytes);
    }
    if (method == 14u) { // LZMA (PKWARE APPNOTE 5.8.8: 4-byte version+propsLen + 5-byte props + raw LZMA1)
        if (compSz >= 9u && ReadLE16(payload + 2) == 5u) {
            const uint8_t propsByte = payload[4];
            const uint32_t dictSize = ReadLE32(payload + 5);
            const bool eosMarker = (flags & 0x02u) != 0;
            if (StreamCodecs::DecompressLzma1(
                    propsByte, dictSize, payload + 9, compSz - 9,
                    outBytes, expectedUncompSz, !eosMarker, maxOutputBytes))
            {
                return true;
            }
            return StreamCodecs::DecompressLzma1(
                propsByte, dictSize, payload + 9, compSz - 9,
                outBytes, expectedUncompSz, eosMarker, maxOutputBytes);
        }
        if (compSz >= 5u) {
            return StreamCodecs::DecompressLzma1(
                payload[0], ReadLE32(payload + 1), payload + 5, compSz - 5,
                outBytes, expectedUncompSz, true, maxOutputBytes);
        }
        return false;
    }
    if (method == 93u || method == 20u) { // Zstandard (Zstd)
        return StreamCodecs::DecompressZstdFrame(payload, compSz, outBytes, maxOutputBytes);
    }
    if (method == 95u) { // XZ
        return StreamCodecs::DecompressXzStream(payload, compSz, outBytes, maxOutputBytes);
    }
    return false;
}

} // namespace

bool InflateRawAppend(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes,
    size_t* outBytesConsumed)
{
    if (outBytesConsumed) *outBytesConsumed = 0;
    if (!src || srcLen == 0) return false;

    BitStream bs{ src, srcLen, 0, 0, 0 };
    uint32_t finalBlock = 0;

    while (!finalBlock) {
        if (bs.bytePos >= bs.size && bs.bitCount < 3) break;
        finalBlock = bs.ReadBits(1);
        uint32_t blockType = bs.ReadBits(2);

        if (blockType == 0) {
            bs.AlignToByte();
            if (!bs.HasBytes(4)) return false;
            uint16_t len = static_cast<uint16_t>(bs.ReadAlignedByte());
            len |= static_cast<uint16_t>(bs.ReadAlignedByte()) << 8;
            uint16_t nlen = static_cast<uint16_t>(bs.ReadAlignedByte());
            nlen |= static_cast<uint16_t>(bs.ReadAlignedByte()) << 8;
            if ((len ^ 0xFFFFu) != nlen) return false;
            if (!bs.HasBytes(len)) return false;
            if (len > maxOutputBytes - outData.size()) return false;
            size_t oldSz = outData.size();
            outData.resize(oldSz + len);
            for (uint16_t i = 0; i < len; ++i) {
                outData[oldSz + i] = bs.ReadAlignedByte();
            }
        } else if (blockType == 1 || blockType == 2) {
            HuffmanTable litLenTable;
            HuffmanTable distTable;

            if (blockType == 1) {
                uint8_t litLens[288] = {};
                for (int i = 0; i <= 143; ++i) litLens[i] = 8;
                for (int i = 144; i <= 255; ++i) litLens[i] = 9;
                for (int i = 256; i <= 279; ++i) litLens[i] = 7;
                for (int i = 280; i <= 287; ++i) litLens[i] = 8;
                uint8_t distLens[32] = {};
                for (int i = 0; i < 32; ++i) distLens[i] = 5;
                litLenTable.Build(litLens, 288);
                distTable.Build(distLens, 32);
            } else {
                int hlit = static_cast<int>(bs.ReadBits(5)) + 257;
                int hdist = static_cast<int>(bs.ReadBits(5)) + 1;
                int hclen = static_cast<int>(bs.ReadBits(4)) + 4;
                if (hlit > 286 || hdist > 30) return false;

                uint8_t codeLenLengths[19] = {};
                for (int i = 0; i < hclen; ++i) {
                    codeLenLengths[kCodeLenOrder[i]] = static_cast<uint8_t>(bs.ReadBits(3));
                }
                if (bs.overrun) return false;
                HuffmanTable codeLenTable;
                if (!codeLenTable.Build(codeLenLengths, 19)) return false;

                uint8_t allLengths[320] = {};
                int totalCodes = hlit + hdist;
                int idx = 0;
                while (idx < totalCodes) {
                    int sym = codeLenTable.Decode(bs);
                    if (sym < 0 || bs.overrun) return false;
                    if (sym < 16) {
                        allLengths[idx++] = static_cast<uint8_t>(sym);
                    } else if (sym == 16) {
                        if (idx == 0) return false;
                        int rep = static_cast<int>(bs.ReadBits(2)) + 3;
                        uint8_t prev = allLengths[idx - 1];
                        while (rep-- > 0 && idx < totalCodes) allLengths[idx++] = prev;
                    } else if (sym == 17) {
                        int rep = static_cast<int>(bs.ReadBits(3)) + 3;
                        while (rep-- > 0 && idx < totalCodes) allLengths[idx++] = 0;
                    } else if (sym == 18) {
                        int rep = static_cast<int>(bs.ReadBits(7)) + 11;
                        while (rep-- > 0 && idx < totalCodes) allLengths[idx++] = 0;
                    } else {
                        return false;
                    }
                }
                if (bs.overrun) return false;
                if (!litLenTable.Build(allLengths, hlit)) return false;
                if (!distTable.Build(allLengths + hlit, hdist)) return false;
            }

            while (true) {
                int sym = litLenTable.Decode(bs);
                if (sym < 0 || bs.overrun) return false;
                if (sym < 256) {
                    if (outData.size() >= maxOutputBytes) return false;
                    outData.push_back(static_cast<uint8_t>(sym));
                } else if (sym == 256) {
                    break;
                } else {
                    int lenIdx = sym - 257;
                    if (lenIdx < 0 || lenIdx >= 29) return false;
                    uint32_t matchLen = kLenBase[lenIdx] + bs.ReadBits(kLenExtra[lenIdx]);

                    int distSym = distTable.Decode(bs);
                    if (distSym < 0 || distSym >= 30) return false;
                    uint32_t matchDist = kDistBase[distSym] + bs.ReadBits(kDistExtra[distSym]);
                    if (bs.overrun) return false;
                    if (matchDist == 0 || matchDist > outData.size()) return false;
                    if (matchLen > maxOutputBytes - outData.size()) return false;

                    size_t startIdx = outData.size() - matchDist;
                    for (uint32_t i = 0; i < matchLen; ++i) {
                        outData.push_back(outData[startIdx + i]);
                    }
                }
            }
        } else {
            return false;
        }
    }
    bs.AlignToByte();
    if (outBytesConsumed) *outBytesConsumed = bs.ConsumedBytes();
    return true;
}

bool InflateRaw(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes)
{
    outData.clear();
    return InflateRawAppend(src, srcLen, outData, maxOutputBytes, nullptr);
}

bool InflateZlib(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes)
{
    if (!src || srcLen < 6) return false;
    uint8_t cmf = src[0];
    uint8_t flg = src[1];
    if ((cmf & 0x0F) != 8) return false;
    if (((static_cast<uint16_t>(cmf) << 8) + flg) % 31 != 0) return false;
    size_t hdrSize = (flg & 0x20) ? 6 : 2;
    if (srcLen <= hdrSize) return false;
    return InflateRaw(src + hdrSize, srcLen - hdrSize, outData, maxOutputBytes);
}

bool InflateGzip(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes)
{
    outData.clear();
    if (!src || srcLen < 18) return false;
    if (src[0] != 0x1Fu || src[1] != 0x8Bu || src[2] != 8u) return false;

    size_t memberStart = 0;
    size_t membersDecoded = 0;

    while (memberStart < srcLen) {
        if (srcLen - memberStart < 18u ||
            src[memberStart] != 0x1Fu ||
            src[memberStart + 1] != 0x8Bu ||
            src[memberStart + 2] != 8u)
        {
            break;
        }

        const uint8_t flags = src[memberStart + 3];
        if ((flags & 0xE0u) != 0u) {
            if (membersDecoded > 0) break;
            return false;
        }

        size_t pos = memberStart + 10u;
        if (flags & 0x04u) { // FEXTRA
            if (2u > srcLen - pos) return false;
            const uint16_t xlen = ReadLE16(src + pos);
            pos += 2u;
            if (xlen > srcLen - pos) return false;
            pos += xlen;
        }
        if (flags & 0x08u) { // FNAME
            while (pos < srcLen && src[pos] != 0u) ++pos;
            if (pos >= srcLen) return false;
            ++pos;
        }
        if (flags & 0x10u) { // FCOMMENT
            while (pos < srcLen && src[pos] != 0u) ++pos;
            if (pos >= srcLen) return false;
            ++pos;
        }
        if (flags & 0x02u) { // FHCRC
            if (2u > srcLen - pos) return false;
            pos += 2u;
        }
        if (pos >= srcLen) return false;

        // Decode raw DEFLATE member without retaining previous member's LZ77 window across independent GZIP members
        std::vector<uint8_t> memberOut;
        size_t consumed = 0;
        const size_t remCap = maxOutputBytes - outData.size();
        if (!InflateRawAppend(src + pos, srcLen - pos, memberOut, remCap, &consumed)) {
            if (membersDecoded > 0) break;
            return false;
        }
        outData.insert(outData.end(), memberOut.begin(), memberOut.end());
        ++membersDecoded;

        pos += consumed;
        if (srcLen - pos < 8u) break;
        pos += 8u; // Skip 4-byte CRC32 + 4-byte ISIZE trailer

        // Skip optional zero padding between concatenated GZIP members
        size_t nextMember = pos;
        while (nextMember < srcLen && src[nextMember] == 0u) ++nextMember;
        if (srcLen - nextMember >= 18u &&
            src[nextMember] == 0x1Fu && src[nextMember + 1] == 0x8Bu && src[nextMember + 2] == 8u)
        {
            memberStart = nextMember;
        } else {
            break;
        }
    }

    return membersDecoded > 0;
}

FILETIME DosDateTimeToFileTime(uint16_t dosDate, uint16_t dosTime) {
    FILETIME localFt = {}, utcFt = {};
    if (::DosDateTimeToFileTime(dosDate, dosTime, &localFt)) {
        ::LocalFileTimeToFileTime(&localFt, &utcFt);
    }
    return utcFt;
}

bool ListZipEntriesFromMemory(
    const uint8_t* data,
    size_t dataLen,
    std::vector<ArchiveItemMeta>& outEntries)
{
    outEntries.clear();
    if (!data || dataLen < 22) return false;

    size_t maxComment = std::min<size_t>(dataLen, 65557);
    size_t eocdPos = SIZE_MAX;
    for (size_t back = 22; back <= maxComment; ++back) {
        size_t p = dataLen - back;
        if (ReadLE32(data + p) == 0x06054B50u) {
            eocdPos = p;
            break;
        }
    }

    if (eocdPos != SIZE_MAX) {
        uint64_t totalEntries = ReadLE16(data + eocdPos + 10);
        uint64_t cdSize       = ReadLE32(data + eocdPos + 12);
        uint64_t cdOffset     = ReadLE32(data + eocdPos + 16);

        // Check ZIP64 End of Central Directory Locator (0x06074B50, 20 bytes before EOCD)
        if (eocdPos >= 20u && ReadLE32(data + eocdPos - 20u) == 0x06074B50u) {
            const uint64_t zip64EocdOff = ReadLE64(data + eocdPos - 20u + 8u);
            if (zip64EocdOff <= dataLen && dataLen - static_cast<size_t>(zip64EocdOff) >= 56u &&
                ReadLE32(data + static_cast<size_t>(zip64EocdOff)) == 0x06064B50u)
            {
                const size_t z64 = static_cast<size_t>(zip64EocdOff);
                totalEntries = ReadLE64(data + z64 + 32u);
                cdSize       = ReadLE64(data + z64 + 40u);
                cdOffset     = ReadLE64(data + z64 + 48u);
            }
        }

        if (cdOffset <= dataLen) {
            const size_t pos = static_cast<size_t>(cdOffset);
            const size_t avail = dataLen - pos;
            const size_t sliceLen = (cdSize < avail) ? static_cast<size_t>(cdSize) : avail;
            if (ParseZipCentralDirSlice(data + pos, sliceLen, totalEntries, outEntries)) {
                return true;
            }
        }
    }

    // Fallback: sequential Local File Header scan (0x04034B50)
    size_t pos = 0;
    while (pos + 30u <= dataLen) {
        uint32_t sig = ReadLE32(data + pos);
        if (sig != 0x04034B50u) break;
        uint16_t flags      = ReadLE16(data + pos + 6);
        uint16_t method     = ReadLE16(data + pos + 8);
        uint16_t modTime    = ReadLE16(data + pos + 10);
        uint16_t modDate    = ReadLE16(data + pos + 12);
        uint32_t crc        = ReadLE32(data + pos + 14);
        uint32_t compSz32   = ReadLE32(data + pos + 18);
        uint32_t uncompSz32 = ReadLE32(data + pos + 22);
        uint16_t nameLen    = ReadLE16(data + pos + 26);
        uint16_t extraLen   = ReadLE16(data + pos + 28);
        if (nameLen > dataLen - (pos + 30u)) break;
        const size_t extraOff = pos + 30u + nameLen;
        if (extraLen > dataLen - extraOff) break;

        std::string rawName(reinterpret_cast<const char*>(data + pos + 30u), nameLen);
        uint64_t uncompSz    = uncompSz32;
        uint64_t compSz      = compSz32;
        uint64_t localHdrOff = pos;
        if (extraLen > 0u) {
            ApplyZipExtraFields(
                data + extraOff, extraLen, true,
                uncompSz32, compSz32, 0u,
                uncompSz, compSz, localHdrOff, rawName);
        }

        std::wstring wName = PathUtils::Utf8ToWide(rawName);
        for (wchar_t& ch : wName) if (ch == L'\\') ch = L'/';

        ArchiveItemMeta meta;
        meta.isDirectory = (!wName.empty() && wName.back() == L'/');
        while (!wName.empty() && wName.back() == L'/') wName.pop_back();
        meta.pathInArchive = wName;
        size_t slash = wName.find_last_of(L'/');
        meta.name = (slash != std::wstring::npos) ? wName.substr(slash + 1) : wName;
        meta.uncompressedSize = uncompSz;
        meta.compressedSize = compSz;
        meta.localHeaderOffset = pos;
        meta.crc32 = crc;
        meta.compressionMethod = method;
        meta.generalFlags = flags;
        meta.dosTime = modTime;
        meta.dosDate = modDate;
        meta.isEncrypted = (flags & 0x01u) != 0;
        if (!meta.pathInArchive.empty()) outEntries.push_back(std::move(meta));

        if (compSz == 0 && (flags & 0x08u)) break;
        const size_t payloadOff = extraOff + extraLen;
        if (compSz > dataLen - payloadOff) break;
        pos = payloadOff + static_cast<size_t>(compSz);
    }
    return !outEntries.empty();
}

bool ExtractZipEntryFromMemory(
    const uint8_t* data,
    size_t dataLen,
    const ArchiveItemMeta& entry,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes)
{
    outBytes.clear();
    if (!data || dataLen < 30 || entry.isDirectory || entry.isEncrypted) return false;
    if (entry.localHeaderOffset > dataLen || dataLen - static_cast<size_t>(entry.localHeaderOffset) < 30u) return false;

    const size_t hdrPos = static_cast<size_t>(entry.localHeaderOffset);
    if (ReadLE32(data + hdrPos) != 0x04034B50u) return false;
    const uint16_t flags    = ReadLE16(data + hdrPos + 6);
    const uint16_t method   = ReadLE16(data + hdrPos + 8);
    const uint16_t nameLen  = ReadLE16(data + hdrPos + 26);
    const uint16_t extraLen = ReadLE16(data + hdrPos + 28);
    if (nameLen > dataLen - (hdrPos + 30u)) return false;
    const size_t extraPos = hdrPos + 30u + nameLen;
    if (extraLen > dataLen - extraPos) return false;
    const size_t payloadPos = extraPos + extraLen;
    if (entry.compressedSize > dataLen - payloadPos) return false;

    return DecompressZipPayloadBuffer(
        method,
        flags,
        data + payloadPos,
        static_cast<size_t>(entry.compressedSize),
        entry.uncompressedSize,
        outBytes,
        maxOutputBytes);
}

bool ExtractZipEntryFromMemory(
    const uint8_t* data,
    size_t dataLen,
    const char* nameSubstr,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes)
{
    outBytes.clear();
    if (!data || dataLen < 30 || !nameSubstr) return false;
    std::vector<ArchiveItemMeta> entries;
    if (!ListZipEntriesFromMemory(data, dataLen, entries)) return false;

    std::wstring wTarget = PathUtils::ToLower(PathUtils::Utf8ToWide(nameSubstr));
    for (wchar_t& ch : wTarget) if (ch == L'\\') ch = L'/';

    for (const auto& entry : entries) {
        if (entry.isDirectory || entry.isEncrypted) continue;
        std::wstring lowPath = PathUtils::ToLower(entry.pathInArchive);
        if (lowPath.find(wTarget) == std::wstring::npos) continue;
        if (ExtractZipEntryFromMemory(data, dataLen, entry, outBytes, maxOutputBytes)) {
            return true;
        }
    }
    return false;
}

bool ListZipEntriesFromFile(
    const std::wstring& zipFilePath,
    std::vector<ArchiveItemMeta>& outEntries,
    std::wstring* outFormatSummary)
{
    outEntries.clear();
    HANDLE hFile = CreateFileW(zipFilePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 22) {
        CloseHandle(hFile);
        return false;
    }
    uint64_t fileSize = static_cast<uint64_t>(li.QuadPart);

    if (fileSize <= 16 * 1024 * 1024) {
        std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
        DWORD readBytes = 0;
        bool ok = ReadFile(hFile, buf.data(), static_cast<DWORD>(fileSize), &readBytes, nullptr) != FALSE;
        CloseHandle(hFile);
        if (!ok || readBytes < 22) return false;
        bool parsed = ListZipEntriesFromMemory(buf.data(), readBytes, outEntries);
        if (parsed && outFormatSummary) *outFormatSummary = L"ZIP Archive";
        return parsed;
    }

    size_t tailSize = static_cast<size_t>(std::min<uint64_t>(fileSize, 65636));
    LARGE_INTEGER seekPos = {};
    seekPos.QuadPart = static_cast<LONGLONG>(fileSize - tailSize);
    SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN);
    std::vector<uint8_t> tail(tailSize);
    DWORD tailRead = 0;
    ReadFile(hFile, tail.data(), static_cast<DWORD>(tailSize), &tailRead, nullptr);

    size_t eocdPos = SIZE_MAX;
    for (size_t back = 22; back <= tailRead; ++back) {
        size_t p = tailRead - back;
        if (ReadLE32(tail.data() + p) == 0x06054B50u) {
            eocdPos = p;
            break;
        }
    }
    if (eocdPos == SIZE_MAX) {
        CloseHandle(hFile);
        return false;
    }

    uint64_t totalEntries = ReadLE16(tail.data() + eocdPos + 10);
    uint64_t cdSize       = ReadLE32(tail.data() + eocdPos + 12);
    uint64_t cdOffset     = ReadLE32(tail.data() + eocdPos + 16);
    bool isZip64 = false;

    if (eocdPos >= 20u && ReadLE32(tail.data() + eocdPos - 20u) == 0x06074B50u) {
        const uint64_t zip64EocdOff = ReadLE64(tail.data() + eocdPos - 20u + 8u);
        if (zip64EocdOff <= fileSize && fileSize - zip64EocdOff >= 56ULL) {
            LARGE_INTEGER z64Seek = {};
            z64Seek.QuadPart = static_cast<LONGLONG>(zip64EocdOff);
            uint8_t z64Hdr[56] = {};
            DWORD z64Read = 0;
            if (SetFilePointerEx(hFile, z64Seek, nullptr, FILE_BEGIN) &&
                ReadFile(hFile, z64Hdr, 56, &z64Read, nullptr) && z64Read == 56 &&
                ReadLE32(z64Hdr) == 0x06064B50u)
            {
                totalEntries = ReadLE64(z64Hdr + 32);
                cdSize       = ReadLE64(z64Hdr + 40);
                cdOffset     = ReadLE64(z64Hdr + 48);
                isZip64 = true;
            }
        }
    }

    if (cdOffset > fileSize || cdSize > fileSize - cdOffset || cdSize > 64ULL * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    seekPos.QuadPart = static_cast<LONGLONG>(cdOffset);
    SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN);
    std::vector<uint8_t> cdBuf(static_cast<size_t>(cdSize));
    DWORD cdRead = 0;
    ReadFile(hFile, cdBuf.data(), static_cast<DWORD>(cdSize), &cdRead, nullptr);
    CloseHandle(hFile);

    if (!ParseZipCentralDirSlice(cdBuf.data(), cdRead, totalEntries, outEntries)) {
        return false;
    }
    if (outFormatSummary) {
        *outFormatSummary = isZip64 ? L"ZIP64 Archive" : L"ZIP Archive (64-bit / Large)";
    }
    return true;
}

bool ExtractZipEntryFromFile(
    const std::wstring& zipFilePath,
    const ArchiveItemMeta& entry,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes)
{
    outBytes.clear();
    if (entry.isDirectory || entry.isEncrypted) return false;
    if (entry.uncompressedSize > maxOutputBytes) return false;
    const uint64_t maxCompCap = static_cast<uint64_t>(maxOutputBytes) + 65536ULL;
    if (entry.compressedSize > maxCompCap) return false;

    HANDLE hFile = CreateFileW(zipFilePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER seekPos = {};
    seekPos.QuadPart = static_cast<LONGLONG>(entry.localHeaderOffset);
    if (!SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    uint8_t localHdr[30] = {};
    DWORD hdrRead = 0;
    if (!ReadFile(hFile, localHdr, 30, &hdrRead, nullptr) || hdrRead < 30 || ReadLE32(localHdr) != 0x04034B50u) {
        CloseHandle(hFile);
        return false;
    }

    const uint16_t flags    = ReadLE16(localHdr + 6);
    const uint16_t method   = ReadLE16(localHdr + 8);
    const uint16_t nameLen  = ReadLE16(localHdr + 26);
    const uint16_t extraLen = ReadLE16(localHdr + 28);
    seekPos.QuadPart = static_cast<LONGLONG>(entry.localHeaderOffset + 30ULL + nameLen + extraLen);
    if (!SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    const size_t compSz = static_cast<size_t>(entry.compressedSize);
    std::vector<uint8_t> compBuf(compSz);
    DWORD compRead = 0;
    if (compSz > 0 && (!ReadFile(hFile, compBuf.data(), static_cast<DWORD>(compSz), &compRead, nullptr) || compRead != compSz)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    return DecompressZipPayloadBuffer(
        method,
        flags,
        compBuf.data(),
        compBuf.size(),
        entry.uncompressedSize,
        outBytes,
        maxOutputBytes);
}

bool ExtractZipFileByName(
    const std::wstring& zipFilePath,
    const std::wstring& innerPathRelative,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes)
{
    std::vector<ArchiveItemMeta> entries;
    if (!ListZipEntriesFromFile(zipFilePath, entries)) return false;

    std::wstring targetNorm = PathUtils::ToLower(innerPathRelative);
    for (wchar_t& ch : targetNorm) if (ch == L'\\') ch = L'/';
    while (!targetNorm.empty() && targetNorm.front() == L'/') targetNorm.erase(targetNorm.begin());

    for (const auto& e : entries) {
        std::wstring cand = PathUtils::ToLower(e.pathInArchive);
        for (wchar_t& ch : cand) if (ch == L'\\') ch = L'/';
        if (cand == targetNorm) {
            return ExtractZipEntryFromFile(zipFilePath, e, outBytes, maxOutputBytes);
        }
    }
    return false;
}

bool ListTarEntriesFromFile(
    const std::wstring& tarFilePath,
    std::vector<ArchiveItemMeta>& outEntries,
    std::wstring* outFormatSummary)
{
    outEntries.clear();
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(tarFilePath, 64 * 1024 * 1024, raw) || raw.size() < 18) {
        return false;
    }
    const uint8_t* tarPtr = raw.data();
    size_t tarLen = raw.size();
    std::vector<uint8_t> decomp;
    if (raw[0] == 0x1F && raw[1] == 0x8B) {
        if (!InflateGzip(raw.data(), raw.size(), decomp, 128 * 1024 * 1024)) return false;
        if (outFormatSummary) *outFormatSummary = L"TAR.GZ (Gzip Compressed Tarball)";
        tarPtr = decomp.data();
        tarLen = decomp.size();
    } else if (outFormatSummary) {
        *outFormatSummary = L"POSIX TAR Archive";
    }

    ArchiveSummaryInfo info;
    if (!StreamCodecs::ParsePosixTarFromMemory(tarPtr, tarLen, info) || info.entries.empty()) {
        return false;
    }
    outEntries.reserve(info.entries.size());
    for (const auto& e : info.entries) {
        ArchiveItemMeta meta;
        meta.pathInArchive     = e.pathInArchive;
        meta.name              = e.name;
        meta.uncompressedSize  = e.uncompressedSize;
        meta.compressedSize    = e.compressedSize;
        meta.localHeaderOffset = e.rawDataOffset;
        meta.compressionMethod = 0;
        meta.isDirectory       = e.isDirectory;
        outEntries.push_back(std::move(meta));
    }
    return !outEntries.empty();
}

bool ExtractTarEntryFromFile(
    const std::wstring& tarFilePath,
    const ArchiveItemMeta& entry,
    std::vector<uint8_t>& outBytes,
    size_t maxOutputBytes)
{
    outBytes.clear();
    if (entry.isDirectory || entry.uncompressedSize > maxOutputBytes) return false;
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(tarFilePath, 64 * 1024 * 1024, raw) || raw.empty()) return false;

    const uint8_t* tarPtr = raw.data();
    size_t tarLen = raw.size();
    std::vector<uint8_t> decomp;
    if (raw.size() >= 18 && raw[0] == 0x1F && raw[1] == 0x8B) {
        if (!InflateGzip(raw.data(), raw.size(), decomp, 128 * 1024 * 1024)) return false;
        tarPtr = decomp.data();
        tarLen = decomp.size();
    }

    if (entry.localHeaderOffset > tarLen || entry.uncompressedSize > tarLen - static_cast<size_t>(entry.localHeaderOffset)) {
        std::wstring normWant = ArchivePath::Normalize(entry.pathInArchive);
        ArchiveSummaryInfo dummy;
        return StreamCodecs::ParsePosixTarFromMemory(tarPtr, tarLen, dummy, &normWant, &outBytes) &&
               outBytes.size() <= maxOutputBytes;
    }
    outBytes.assign(
        tarPtr + static_cast<size_t>(entry.localHeaderOffset),
        tarPtr + static_cast<size_t>(entry.localHeaderOffset) + static_cast<size_t>(entry.uncompressedSize));
    return true;
}

} // namespace Explorer2::ZipDeflate
