#include "ZipDeflate.h"
#include "PathUtils.h"
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
        if (bitCount < 0) bitCount = 0;
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
        return 0;
    }
};

struct HuffmanTable {
    static constexpr int kMaxBits = 15;
    uint16_t counts[kMaxBits + 1] = {};
    uint16_t symbols[320] = {};
    int maxCodeLen = 0;

    bool Build(const uint8_t* lengths, int numSymbols) {
        std::memset(counts, 0, sizeof(counts));
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
        return true;
    }

    int Decode(BitStream& bs) const {
        bs.Fill(kMaxBits);
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= maxCodeLen; ++len) {
            code |= static_cast<int>(bs.ReadBits(1));
            int count = counts[len];
            if (code - count < first) {
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

inline uint16_t ReadLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t ReadLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t ReadLE64(const uint8_t* p) {
    return static_cast<uint64_t>(ReadLE32(p)) | (static_cast<uint64_t>(ReadLE32(p + 4)) << 32);
}

} // namespace

bool InflateRaw(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& outData,
    size_t maxOutputBytes)
{
    outData.clear();
    if (!src || srcLen == 0) return false;

    BitStream bs{ src, srcLen, 0, 0, 0 };
    uint32_t finalBlock = 0;

    while (!finalBlock) {
        if (bs.bytePos >= bs.size && bs.bitCount < 3) break;
        finalBlock = bs.ReadBits(1);
        uint32_t blockType = bs.ReadBits(2);

        if (blockType == 0) {
            // Uncompressed stored block
            bs.AlignToByte();
            if (!bs.HasBytes(4)) return false;
            uint16_t len = static_cast<uint16_t>(bs.ReadAlignedByte());
            len |= static_cast<uint16_t>(bs.ReadAlignedByte()) << 8;
            uint16_t nlen = static_cast<uint16_t>(bs.ReadAlignedByte());
            nlen |= static_cast<uint16_t>(bs.ReadAlignedByte()) << 8;
            if ((len ^ 0xFFFFu) != nlen) return false;
            if (!bs.HasBytes(len)) return false;
            if (outData.size() + len > maxOutputBytes) return false;
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
                HuffmanTable codeLenTable;
                if (!codeLenTable.Build(codeLenLengths, 19)) return false;

                uint8_t allLengths[320] = {};
                int totalCodes = hlit + hdist;
                int idx = 0;
                while (idx < totalCodes) {
                    int sym = codeLenTable.Decode(bs);
                    if (sym < 0) return false;
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
                if (!litLenTable.Build(allLengths, hlit)) return false;
                if (!distTable.Build(allLengths + hlit, hdist)) return false;
            }

            while (true) {
                int sym = litLenTable.Decode(bs);
                if (sym < 0) return false;
                if (sym < 256) {
                    if (outData.size() + 1 > maxOutputBytes) return false;
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
                    if (matchDist == 0 || matchDist > outData.size()) return false;
                    if (outData.size() + matchLen > maxOutputBytes) return false;

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
    return true;
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
    if ((cmf & 0x0F) != 8) return false; // Must be DEFLATE
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
    if (!src || srcLen < 18) return false;
    if (src[0] != 0x1F || src[1] != 0x8B || src[2] != 8) return false;
    uint8_t flags = src[3];
    size_t pos = 10;
    if (flags & 0x04) { // FEXTRA
        if (pos + 2 > srcLen) return false;
        uint16_t xlen = ReadLE16(src + pos);
        pos += 2 + xlen;
    }
    if (flags & 0x08) { // FNAME
        while (pos < srcLen && src[pos] != 0) ++pos;
        ++pos;
    }
    if (flags & 0x10) { // FCOMMENT
        while (pos < srcLen && src[pos] != 0) ++pos;
        ++pos;
    }
    if (flags & 0x02) { // FHCRC
        pos += 2;
    }
    if (pos >= srcLen) return false;
    return InflateRaw(src + pos, srcLen - pos, outData, maxOutputBytes);
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

    // Locate End of Central Directory (EOCD) signature 0x06054b50 scanning backwards from end
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
        uint16_t totalEntries = ReadLE16(data + eocdPos + 10);
        uint32_t cdSize = ReadLE32(data + eocdPos + 12);
        uint32_t cdOffset = ReadLE32(data + eocdPos + 16);

        if (cdOffset < dataLen) {
            size_t pos = cdOffset;
            size_t endPos = std::min<size_t>(dataLen, static_cast<size_t>(cdOffset) + cdSize);
            outEntries.reserve(totalEntries);
            while (pos + 46 <= endPos) {
                if (ReadLE32(data + pos) != 0x02014B50u) break;
                uint16_t flags = ReadLE16(data + pos + 8);
                uint16_t method = ReadLE16(data + pos + 10);
                uint16_t modTime = ReadLE16(data + pos + 12);
                uint16_t modDate = ReadLE16(data + pos + 14);
                uint32_t crc = ReadLE32(data + pos + 16);
                uint32_t compSz = ReadLE32(data + pos + 20);
                uint32_t uncompSz = ReadLE32(data + pos + 24);
                uint16_t nameLen = ReadLE16(data + pos + 28);
                uint16_t extraLen = ReadLE16(data + pos + 30);
                uint16_t commentLen = ReadLE16(data + pos + 32);
                uint32_t extAttr = ReadLE32(data + pos + 38);
                uint32_t localHdrOff = ReadLE32(data + pos + 42);

                if (pos + 46 + nameLen > dataLen) break;
                std::string rawName(reinterpret_cast<const char*>(data + pos + 46), nameLen);
                std::wstring wName = PathUtils::Utf8ToWide(rawName);
                for (wchar_t& ch : wName) {
                    if (ch == L'\\') ch = L'/';
                }

                ArchiveItemMeta meta;
                meta.isDirectory = (!wName.empty() && wName.back() == L'/') || ((extAttr & 0x10) != 0);
                while (!wName.empty() && wName.back() == L'/') wName.pop_back();
                meta.pathInArchive = wName;
                size_t slash = wName.find_last_of(L'/');
                meta.name = (slash != std::wstring::npos) ? wName.substr(slash + 1) : wName;
                meta.uncompressedSize = uncompSz;
                meta.compressedSize = compSz;
                meta.localHeaderOffset = localHdrOff;
                meta.crc32 = crc;
                meta.compressionMethod = method;
                meta.dosTime = modTime;
                meta.dosDate = modDate;
                meta.isEncrypted = (flags & 0x01) != 0;

                if (!meta.pathInArchive.empty()) {
                    outEntries.push_back(std::move(meta));
                }
                pos += 46 + nameLen + extraLen + commentLen;
            }
            return !outEntries.empty() || totalEntries == 0;
        }
    }

    // Fallback: sequential Local File Header scan (0x04034B50)
    size_t pos = 0;
    while (pos + 30 <= dataLen) {
        uint32_t sig = ReadLE32(data + pos);
        if (sig != 0x04034B50u) break;
        uint16_t flags = ReadLE16(data + pos + 6);
        uint16_t method = ReadLE16(data + pos + 8);
        uint16_t modTime = ReadLE16(data + pos + 10);
        uint16_t modDate = ReadLE16(data + pos + 12);
        uint32_t crc = ReadLE32(data + pos + 14);
        uint32_t compSz = ReadLE32(data + pos + 18);
        uint32_t uncompSz = ReadLE32(data + pos + 22);
        uint16_t nameLen = ReadLE16(data + pos + 26);
        uint16_t extraLen = ReadLE16(data + pos + 28);
        if (pos + 30 + nameLen > dataLen) break;

        std::string rawName(reinterpret_cast<const char*>(data + pos + 30), nameLen);
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
        meta.dosTime = modTime;
        meta.dosDate = modDate;
        meta.isEncrypted = (flags & 0x01) != 0;
        if (!meta.pathInArchive.empty()) outEntries.push_back(std::move(meta));

        if (compSz == 0 && (flags & 0x08)) break;
        pos += 30 + nameLen + extraLen + compSz;
    }
    return !outEntries.empty();
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

    // If file is <= 16 MB, read it once; if larger, read tail 65 KB to locate Central Directory!
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

    // Large ZIP file: read tail for Central Directory
    size_t tailSize = static_cast<size_t>(std::min<uint64_t>(fileSize, 65557));
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

    uint32_t cdSize = ReadLE32(tail.data() + eocdPos + 12);
    uint32_t cdOffset = ReadLE32(tail.data() + eocdPos + 16);
    if (static_cast<uint64_t>(cdOffset) + cdSize > fileSize || cdSize > 64 * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    seekPos.QuadPart = cdOffset;
    SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN);
    std::vector<uint8_t> cdBuf(cdSize);
    DWORD cdRead = 0;
    ReadFile(hFile, cdBuf.data(), cdSize, &cdRead, nullptr);
    CloseHandle(hFile);

    size_t pos = 0;
    while (pos + 46 <= cdRead) {
        if (ReadLE32(cdBuf.data() + pos) != 0x02014B50u) break;
        uint16_t flags = ReadLE16(cdBuf.data() + pos + 8);
        uint16_t method = ReadLE16(cdBuf.data() + pos + 10);
        uint16_t modTime = ReadLE16(cdBuf.data() + pos + 12);
        uint16_t modDate = ReadLE16(cdBuf.data() + pos + 14);
        uint32_t crc = ReadLE32(cdBuf.data() + pos + 16);
        uint32_t compSz = ReadLE32(cdBuf.data() + pos + 20);
        uint32_t uncompSz = ReadLE32(cdBuf.data() + pos + 24);
        uint16_t nameLen = ReadLE16(cdBuf.data() + pos + 28);
        uint16_t extraLen = ReadLE16(cdBuf.data() + pos + 30);
        uint16_t commentLen = ReadLE16(cdBuf.data() + pos + 32);
        uint32_t extAttr = ReadLE32(cdBuf.data() + pos + 38);
        uint32_t localHdrOff = ReadLE32(cdBuf.data() + pos + 42);
        if (pos + 46 + nameLen > cdRead) break;

        std::string rawName(reinterpret_cast<const char*>(cdBuf.data() + pos + 46), nameLen);
        std::wstring wName = PathUtils::Utf8ToWide(rawName);
        for (wchar_t& ch : wName) if (ch == L'\\') ch = L'/';

        ArchiveItemMeta meta;
        meta.isDirectory = (!wName.empty() && wName.back() == L'/') || ((extAttr & 0x10) != 0);
        while (!wName.empty() && wName.back() == L'/') wName.pop_back();
        meta.pathInArchive = wName;
        size_t slash = wName.find_last_of(L'/');
        meta.name = (slash != std::wstring::npos) ? wName.substr(slash + 1) : wName;
        meta.uncompressedSize = uncompSz;
        meta.compressedSize = compSz;
        meta.localHeaderOffset = localHdrOff;
        meta.crc32 = crc;
        meta.compressionMethod = method;
        meta.dosTime = modTime;
        meta.dosDate = modDate;
        meta.isEncrypted = (flags & 0x01) != 0;
        if (!meta.pathInArchive.empty()) outEntries.push_back(std::move(meta));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    if (outFormatSummary) *outFormatSummary = L"ZIP Archive (64-bit / Large)";
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

    uint16_t method = ReadLE16(localHdr + 8);
    uint16_t nameLen = ReadLE16(localHdr + 26);
    uint16_t extraLen = ReadLE16(localHdr + 28);
    seekPos.QuadPart = static_cast<LONGLONG>(entry.localHeaderOffset + 30 + nameLen + extraLen);
    if (!SetFilePointerEx(hFile, seekPos, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    size_t compSz = static_cast<size_t>(std::min<uint64_t>(entry.compressedSize, maxOutputBytes));
    std::vector<uint8_t> compBuf(compSz);
    DWORD compRead = 0;
    if (compSz > 0 && (!ReadFile(hFile, compBuf.data(), static_cast<DWORD>(compSz), &compRead, nullptr) || compRead != compSz)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    if (method == 0) {
        outBytes = std::move(compBuf);
        return true;
    } else if (method == 8) {
        return InflateRaw(compBuf.data(), compBuf.size(), outBytes, maxOutputBytes);
    }
    return false;
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

static uint64_t ParseOctalField(const char* field, size_t len) {
    uint64_t val = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = field[i];
        if (c == '\0' || c == ' ') {
            if (val > 0) break;
            continue;
        }
        if (c >= '0' && c <= '7') {
            val = (val << 3) + static_cast<uint64_t>(c - '0');
        }
    }
    return val;
}

static bool ParseTarBuffer(const uint8_t* buf, size_t len, std::vector<ArchiveItemMeta>& outEntries) {
    outEntries.clear();
    size_t pos = 0;
    while (pos + 512 <= len) {
        const uint8_t* blk = buf + pos;
        bool allZero = true;
        for (int i = 0; i < 512; ++i) {
            if (blk[i] != 0) { allZero = false; break; }
        }
        if (allZero) break;

        char nameBuf[256] = {};
        std::memcpy(nameBuf, blk, 100);
        // Check USTAR prefix at offset 345
        if (std::memcmp(blk + 257, "ustar", 5) == 0 && blk[345] != 0) {
            char prefixBuf[156] = {};
            std::memcpy(prefixBuf, blk + 345, 155);
            std::string combined = std::string(prefixBuf) + "/" + std::string(nameBuf);
            strncpy_s(nameBuf, sizeof(nameBuf), combined.c_str(), _TRUNCATE);
        }

        uint64_t fileSz = ParseOctalField(reinterpret_cast<const char*>(blk + 124), 12);
        char typeFlag = static_cast<char>(blk[156]);

        std::wstring wName = PathUtils::Utf8ToWide(nameBuf);
        for (wchar_t& ch : wName) if (ch == L'\\') ch = L'/';
        bool isDir = (typeFlag == '5') || (!wName.empty() && wName.back() == L'/');
        while (!wName.empty() && wName.back() == L'/') wName.pop_back();

        if (!wName.empty() && typeFlag != 'L' && typeFlag != 'x' && typeFlag != 'g') {
            ArchiveItemMeta meta;
            meta.pathInArchive = wName;
            size_t slash = wName.find_last_of(L'/');
            meta.name = (slash != std::wstring::npos) ? wName.substr(slash + 1) : wName;
            meta.uncompressedSize = fileSz;
            meta.compressedSize = fileSz;
            meta.localHeaderOffset = pos + 512; // Direct data offset
            meta.compressionMethod = 0;
            meta.isDirectory = isDir;
            outEntries.push_back(std::move(meta));
        }

        uint64_t paddedData = ((fileSz + 511ULL) / 512ULL) * 512ULL;
        pos += 512 + static_cast<size_t>(paddedData);
    }
    return !outEntries.empty();
}

bool ListTarEntriesFromFile(
    const std::wstring& tarFilePath,
    std::vector<ArchiveItemMeta>& outEntries,
    std::wstring* outFormatSummary)
{
    std::vector<uint8_t> raw;
    if (!PathUtils::ReadFileBytes(tarFilePath, 64 * 1024 * 1024, raw) || raw.size() < 18) {
        return false;
    }
    if (raw[0] == 0x1F && raw[1] == 0x8B) {
        std::vector<uint8_t> decomp;
        if (!InflateGzip(raw.data(), raw.size(), decomp, 128 * 1024 * 1024)) return false;
        if (outFormatSummary) *outFormatSummary = L"TAR.GZ (Gzip Compressed Tarball)";
        return ParseTarBuffer(decomp.data(), decomp.size(), outEntries);
    }
    if (outFormatSummary) *outFormatSummary = L"POSIX TAR Archive";
    return ParseTarBuffer(raw.data(), raw.size(), outEntries);
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

    if (entry.localHeaderOffset + entry.uncompressedSize > tarLen) return false;
    outBytes.assign(
        tarPtr + entry.localHeaderOffset,
        tarPtr + entry.localHeaderOffset + static_cast<size_t>(entry.uncompressedSize));
    return true;
}

} // namespace Explorer2::ZipDeflate
