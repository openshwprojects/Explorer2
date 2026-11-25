// FormatTestsArchivesSystemPak.cpp: self-tests of filesystem, classic archiver, console/Steam & game-engine archive readers
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "ArchiveReaders.h"
#include "Crc32.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "StreamCodecs.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

using namespace ::Explorer2::TestBytes;

std::wstring JoinFsPath(const std::wstring& dir, const wchar_t* rel) {
#ifdef _WIN32
    constexpr wchar_t kSep = L'\\';
#else
    constexpr wchar_t kSep = L'/';
#endif
    std::wstring out = dir;
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') {
        out.push_back(kSep);
    }
    for (const wchar_t* p = rel; p && *p; ++p) {
        if (*p == L'\\' || *p == L'/') out.push_back(kSep);
        else out.push_back(*p);
    }
    return out;
}

void AppendFixedAscii(std::vector<uint8_t>& b, std::string_view s, size_t fieldLen, uint8_t pad = 0u) {
    const size_t n = std::min(s.size(), fieldLen);
    if (n > 0) AppendBytes(b, s.data(), n);
    if (n < fieldLen) b.insert(b.end(), fieldLen - n, pad);
}

bool BytesEqualStr(const std::vector<uint8_t>& v, std::string_view s) {
    return v.size() == s.size() && (s.empty() || std::memcmp(v.data(), s.data(), s.size()) == 0);
}

const ArchiveItemEntry* FindEntryCaseInsensitive(const ArchiveSummaryInfo& info, const std::wstring& path) {
    for (const auto& e : info.entries) {
        if (_wcsicmp(e.pathInArchive.c_str(), path.c_str()) == 0) return &e;
    }
    return nullptr;
}

uint64_t HashBytes64(const std::vector<uint8_t>& v) {
    uint64_t h = 14695981039346656037ULL;
    for (uint8_t b : v) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    h ^= static_cast<uint64_t>(v.size());
    h *= 1099511628211ULL;
    return h;
}

uint64_t HashArchiveToc(const ArchiveSummaryInfo& info) {
    uint64_t h = 14695981039346656037ULL;
    auto mixU64 = [&](uint64_t val) {
        for (int i = 0; i < 8; ++i) {
            h ^= (val >> (i * 8)) & 0xFFULL;
            h *= 1099511628211ULL;
        }
    };
    for (wchar_t ch : info.formatName) mixU64(static_cast<uint32_t>(ch));
    mixU64(info.entries.size());
    for (const auto& e : info.entries) {
        for (wchar_t ch : e.pathInArchive) mixU64(static_cast<uint32_t>(ch));
        mixU64(e.uncompressedSize);
        mixU64(e.compressedSize);
        mixU64(e.isDirectory ? 1ULL : 0ULL);
    }
    return h;
}

void AssertArcRejected(TestCounter& tc, const wchar_t* testName, const std::wstring& path) {
    ArchiveSummaryInfo bad;
    const bool ok = ArchiveReaders::InspectArchiveFile(path, bad);
    tc.Record(!ok && !bad.valid && bad.entries.empty(), testName,
              ok ? L"unexpectedly accepted corrupt archive" : L"rejected cleanly");
}

uint16_t ComputeCrc16Arc(const uint8_t* data, size_t len) {
    uint16_t crc = 0u;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1u) ^ 0xA001u) : static_cast<uint16_t>(crc >> 1u);
        }
    }
    return crc;
}

std::vector<uint8_t> WrapZlibStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z = { 0x78u, 0x01u, 0x01u };
    const uint16_t n = static_cast<uint16_t>(raw.size());
    AppendU16LE(z, n);
    AppendU16LE(z, static_cast<uint16_t>(~n));
    z.insert(z.end(), raw.begin(), raw.end());
    uint32_t s1 = 1u, s2 = 0u;
    for (uint8_t b : raw) {
        s1 = (s1 + b) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    AppendU32BE(z, (s2 << 16) | s1);
    return z;
}

// Minimal LZMA1 + LZMA2 + XZ & Zstandard encoders for ZIP64 multicodec (methods 93 & 95) verification
struct MiniLzmaRangeEnc {
    std::vector<uint8_t> out;
    uint64_t low = 0;
    uint32_t range = 0xFFFFFFFFu;
    uint8_t cache = 0;
    uint64_t cacheSize = 1;

    void ShiftLow() {
        uint32_t lowHi = static_cast<uint32_t>(low >> 32);
        if (lowHi != 0u || static_cast<uint32_t>(low) < 0xFF000000u) {
            uint8_t temp = cache;
            do {
                out.push_back(static_cast<uint8_t>(temp + lowHi));
                temp = 0xFFu;
            } while (--cacheSize != 0u);
            cache = static_cast<uint8_t>(static_cast<uint32_t>(low) >> 24);
        }
        ++cacheSize;
        low = static_cast<uint32_t>(low) << 8;
    }

    void EncodeBit(uint16_t& prob, uint32_t bit) {
        uint32_t bound = (range >> 11) * prob;
        if (bit == 0u) {
            range = bound;
            prob = static_cast<uint16_t>(prob + ((2048u - prob) >> 5));
        } else {
            low += bound;
            range -= bound;
            prob = static_cast<uint16_t>(prob - (prob >> 5));
        }
        if (range < 0x01000000u) {
            range <<= 8;
            ShiftLow();
        }
    }

    void EncodeBitTree(uint16_t* probs, int numBits, uint32_t sym) {
        uint32_t m = 1u;
        for (int i = numBits - 1; i >= 0; --i) {
            uint32_t bit = (sym >> i) & 1u;
            EncodeBit(probs[m], bit);
            m = (m << 1) | bit;
        }
    }

    void EncodeRevBitTree(uint16_t* probs, int numBits, uint32_t sym) {
        uint32_t m = 1u;
        for (int i = 0; i < numBits; ++i) {
            uint32_t bit = (sym >> i) & 1u;
            EncodeBit(probs[m], bit);
            m = (m << 1) | bit;
        }
    }

    void EncodeDirectBits(uint32_t val, int numBits) {
        for (int i = numBits - 1; i >= 0; --i) {
            range >>= 1;
            if (((val >> i) & 1u) != 0u) low += range;
            if (range < 0x01000000u) {
                range <<= 8;
                ShiftLow();
            }
        }
    }

    void Flush() {
        for (int i = 0; i < 5; ++i) ShiftLow();
    }
};

std::vector<uint8_t> EncodeLzma1Stream(const uint8_t* data, size_t len) {
    constexpr int lc = 3, pb = 2;
    constexpr uint32_t posMask = (1u << pb) - 1u;
    uint16_t isMatch[12][16];
    uint16_t isRep[12];
    uint16_t posSlotEnc[4][64];
    uint16_t posEnc[128];
    uint16_t alignEnc[16];
    uint16_t lenChoice = 1024, lenChoice2 = 1024;
    uint16_t lenLow[16][8], lenMid[16][8], lenHigh[256];
    std::vector<uint16_t> litProbs(8 * 0x300, 1024);

    for (int s = 0; s < 12; ++s) {
        isRep[s] = 1024;
        for (int p = 0; p < 16; ++p) isMatch[s][p] = 1024;
    }
    for (int l = 0; l < 4; ++l) for (int i = 0; i < 64; ++i) posSlotEnc[l][i] = 1024;
    for (int i = 0; i < 128; ++i) posEnc[i] = 1024;
    for (int i = 0; i < 16; ++i) alignEnc[i] = 1024;
    for (int p = 0; p < 16; ++p) for (int i = 0; i < 8; ++i) { lenLow[p][i] = 1024; lenMid[p][i] = 1024; }
    for (int i = 0; i < 256; ++i) lenHigh[i] = 1024;

    MiniLzmaRangeEnc rc;
    uint32_t state = 0, rep0 = 0;

    auto posSlotOf = [](uint32_t dist) -> uint32_t {
        if (dist < 4u) return dist;
        uint32_t hb = 0, tmp = dist;
        while (tmp > 1u) { tmp >>= 1; ++hb; }
        return (hb << 1) + ((dist >> (hb - 1u)) & 1u);
    };

    size_t pos = 0;
    while (pos < len) {
        const uint32_t posState = static_cast<uint32_t>(pos) & posMask;
        size_t bestLen = 0, bestDist = 0;
        const size_t maxBack = std::min<size_t>(pos, 2048u);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            const size_t maxM = std::min<size_t>(273u, len - pos);
            while (m < maxM && data[pos + m] == data[pos - d + m]) ++m;
            if (m >= 4u && m > bestLen) { bestLen = m; bestDist = d; if (bestLen >= 64u) break; }
        }
        if (bestLen >= 4u) {
            rc.EncodeBit(isMatch[state][posState], 1u);
            rc.EncodeBit(isRep[state], 0u);
            rep0 = static_cast<uint32_t>(bestDist - 1u);
            const uint32_t remLen = static_cast<uint32_t>(bestLen - 2u);
            if (remLen < 8u) {
                rc.EncodeBit(lenChoice, 0u);
                rc.EncodeBitTree(lenLow[posState], 3, remLen);
            } else if (remLen < 16u) {
                rc.EncodeBit(lenChoice, 1u);
                rc.EncodeBit(lenChoice2, 0u);
                rc.EncodeBitTree(lenMid[posState], 3, remLen - 8u);
            } else {
                rc.EncodeBit(lenChoice, 1u);
                rc.EncodeBit(lenChoice2, 1u);
                rc.EncodeBitTree(lenHigh, 8, remLen - 16u);
            }
            state = (state < 7u) ? 7u : 10u;
            const uint32_t lenToPos = (remLen < 4u) ? remLen : 3u;
            const uint32_t slot = posSlotOf(rep0);
            rc.EncodeBitTree(posSlotEnc[lenToPos], 6, slot);
            if (slot >= 4u) {
                const int numDirect = static_cast<int>((slot >> 1) - 1u);
                const uint32_t base = (2u | (slot & 1u)) << numDirect;
                const uint32_t distReduced = rep0 - base;
                if (slot < 14u) {
                    rc.EncodeRevBitTree(posEnc + base - slot - 1, numDirect, distReduced);
                } else {
                    rc.EncodeDirectBits(distReduced >> 4, numDirect - 4);
                    rc.EncodeRevBitTree(alignEnc, 4, distReduced & 0x0Fu);
                }
            }
            pos += bestLen;
        } else {
            rc.EncodeBit(isMatch[state][posState], 0u);
            const uint8_t prevB = (pos > 0) ? data[pos - 1] : 0u;
            const uint32_t litState = static_cast<uint32_t>(prevB) >> (8 - lc);
            uint16_t* probs = &litProbs[litState * 0x300u];
            const uint8_t curB = data[pos];
            uint32_t sym = 1u;
            if (state >= 7u) {
                uint32_t matchByte = (pos > rep0) ? data[pos - rep0 - 1] : 0u;
                bool matched = true;
                for (int bitIdx = 7; bitIdx >= 0; --bitIdx) {
                    const uint32_t bit = (curB >> bitIdx) & 1u;
                    const uint32_t matchBit = (matchByte >> 7) & 1u;
                    matchByte <<= 1;
                    if (matched) {
                        rc.EncodeBit(probs[((1u + matchBit) << 8) + sym], bit);
                        if (bit != matchBit) matched = false;
                    } else {
                        rc.EncodeBit(probs[sym], bit);
                    }
                    sym = (sym << 1) | bit;
                }
            } else {
                for (int bitIdx = 7; bitIdx >= 0; --bitIdx) {
                    const uint32_t bit = (curB >> bitIdx) & 1u;
                    rc.EncodeBit(probs[sym], bit);
                    sym = (sym << 1) | bit;
                }
            }
            state = (state < 4u) ? 0u : ((state < 10u) ? (state - 3u) : (state - 6u));
            ++pos;
        }
    }
    rc.Flush();
    return rc.out;
}

std::vector<uint8_t> BuildXzMethod95Payload(const uint8_t* raw, size_t rawLen) {
    const auto lzma1 = EncodeLzma1Stream(raw, rawLen);
    std::vector<uint8_t> lzma2;
    const uint32_t u = static_cast<uint32_t>(rawLen - 1u);
    const uint16_t p = static_cast<uint16_t>(lzma1.size() - 1u);
    lzma2.push_back(static_cast<uint8_t>(0xE0u | ((u >> 16) & 0x1Fu)));
    lzma2.push_back(static_cast<uint8_t>((u >> 8) & 0xFFu));
    lzma2.push_back(static_cast<uint8_t>(u & 0xFFu));
    lzma2.push_back(static_cast<uint8_t>((p >> 8) & 0xFFu));
    lzma2.push_back(static_cast<uint8_t>(p & 0xFFu));
    lzma2.push_back(0x5Du);
    lzma2.insert(lzma2.end(), lzma1.begin(), lzma1.end());
    lzma2.push_back(0x00u);

    auto pushVli = [](std::vector<uint8_t>& b, uint64_t v) {
        while (v >= 0x80ULL) {
            b.push_back(static_cast<uint8_t>((v & 0x7FULL) | 0x80ULL));
            v >>= 7;
        }
        b.push_back(static_cast<uint8_t>(v));
    };

    std::vector<uint8_t> xz = { 0xFDu, '7', 'z', 'X', 'Z', 0x00u, 0x00u, 0x01u };
    AppendU32LE(xz, Checksum::Crc32(xz.data() + 6, 2));

    std::vector<uint8_t> blkHdr = { 0x02u, 0x00u, 0x21u, 0x01u, 0x14u, 0x00u, 0x00u, 0x00u };
    AppendU32LE(blkHdr, Checksum::Crc32(blkHdr.data(), blkHdr.size()));

    const size_t unpaddedBlockSize = blkHdr.size() + lzma2.size() + 4u;
    xz.insert(xz.end(), blkHdr.begin(), blkHdr.end());
    xz.insert(xz.end(), lzma2.begin(), lzma2.end());
    while ((xz.size() & 3u) != 0u) xz.push_back(0x00u);
    AppendU32LE(xz, Checksum::Crc32(raw, rawLen));

    std::vector<uint8_t> index = { 0x00u };
    pushVli(index, 1u);
    pushVli(index, unpaddedBlockSize);
    pushVli(index, rawLen);
    while ((index.size() & 3u) != 0u) index.push_back(0x00u);
    AppendU32LE(index, Checksum::Crc32(index.data(), index.size()));
    xz.insert(xz.end(), index.begin(), index.end());

    uint8_t footerBody[6] = {};
    PutU32LE(footerBody, static_cast<uint32_t>((index.size() / 4u) - 1u));
    footerBody[4] = 0x00u;
    footerBody[5] = 0x01u;
    AppendU32LE(xz, Checksum::Crc32(footerBody, 6));
    xz.insert(xz.end(), footerBody, footerBody + 6);
    xz.push_back('Y');
    xz.push_back('Z');
    return xz;
}

std::vector<uint8_t> BuildZstdMethod93Payload(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> zst;
    AppendU32LE(zst, 0xFD2FB528u);
    zst.push_back(0xA0u); // fcsFlag=2 (4B content size), singleSegment=1
    AppendU32LE(zst, static_cast<uint32_t>(len));

    size_t matchPos = 0, matchDist = 0, matchLen = 0;
    for (size_t p = 4; p + 4 <= len && p <= 15; ++p) {
        for (size_t d = 1; d <= p && d <= 28; ++d) {
            size_t m = 0;
            while (p + m < len && m < 34 && raw[p + m] == raw[p - d + m]) ++m;
            if (m >= 4) { matchPos = p; matchDist = d; matchLen = m; break; }
        }
        if (matchLen >= 4) break;
    }

    if (matchLen >= 4) {
        std::vector<uint8_t> lits(raw, raw + matchPos);
        lits.insert(lits.end(), raw + matchPos + matchLen, raw + len);

        std::vector<uint8_t> cblk;
        const uint32_t litHdr = (static_cast<uint32_t>(lits.size()) << 4) | 0x0Cu;
        cblk.push_back(static_cast<uint8_t>(litHdr & 0xFFu));
        cblk.push_back(static_cast<uint8_t>((litHdr >> 8) & 0xFFu));
        cblk.push_back(static_cast<uint8_t>((litHdr >> 16) & 0xFFu));
        cblk.insert(cblk.end(), lits.begin(), lits.end());

        cblk.push_back(1u);    // numSeq = 1
        cblk.push_back(0x54u); // all RLE FSE modes
        const uint8_t llCode = static_cast<uint8_t>(matchPos);
        const uint32_t rawOff = static_cast<uint32_t>(matchDist + 3u);
        uint8_t ofCode = 0;
        { uint32_t t = rawOff; while (t > 1u) { t >>= 1; ++ofCode; } }
        const uint32_t ofMantissa = rawOff - (1u << ofCode);
        const uint8_t mlCode = static_cast<uint8_t>(matchLen - 3u);

        cblk.push_back(llCode);
        cblk.push_back(ofCode);
        cblk.push_back(mlCode);

        uint32_t streamBits = (1u << ofCode) | ofMantissa;
        int totalBits = static_cast<int>(ofCode) + 1;
        while (totalBits > 0) {
            cblk.push_back(static_cast<uint8_t>(streamBits & 0xFFu));
            streamBits >>= 8;
            totalBits -= 8;
        }

        const uint32_t bh = (static_cast<uint32_t>(cblk.size()) << 3) | (2u << 1) | 1u;
        zst.push_back(static_cast<uint8_t>(bh & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 8) & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 16) & 0xFFu));
        zst.insert(zst.end(), cblk.begin(), cblk.end());
    } else {
        const uint32_t bh = (static_cast<uint32_t>(len) << 3) | 1u;
        zst.push_back(static_cast<uint8_t>(bh & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 8) & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 16) & 0xFFu));
        zst.insert(zst.end(), raw, raw + len);
    }
    return zst;
}

std::vector<uint8_t> BuildZip64Archive(
    const std::vector<std::tuple<std::string, uint16_t, std::string, std::vector<uint8_t>>>& items)
{
    std::vector<uint8_t> body;
    std::vector<uint8_t> cd;
    for (const auto& [name, method, rawStr, comp] : items) {
        const uint64_t locOff = body.size();
        const uint32_t crc = Checksum::Crc32(reinterpret_cast<const uint8_t*>(rawStr.data()), rawStr.size());

        // ZIP64 local extension field (0x0001: 8B uncomp + 8B comp)
        std::vector<uint8_t> locZip64Field;
        AppendU16LE(locZip64Field, 0x0001u);
        AppendU16LE(locZip64Field, 16u);
        AppendU64LE(locZip64Field, rawStr.size());
        AppendU64LE(locZip64Field, comp.size());

        AppendU32LE(body, 0x04034B50u);
        AppendU16LE(body, 45u);
        AppendU16LE(body, 0x0800u);
        AppendU16LE(body, method);
        AppendU16LE(body, 0x6000u);
        AppendU16LE(body, 0x5B79u);
        AppendU32LE(body, crc);
        AppendU32LE(body, 0xFFFFFFFFu);
        AppendU32LE(body, 0xFFFFFFFFu);
        AppendU16LE(body, static_cast<uint16_t>(name.size()));
        AppendU16LE(body, static_cast<uint16_t>(locZip64Field.size()));
        AppendStr(body, name);
        body.insert(body.end(), locZip64Field.begin(), locZip64Field.end());
        body.insert(body.end(), comp.begin(), comp.end());

        // ZIP64 central directory extension field (0x0001: 8B uncomp + 8B comp + 8B localHdrOff)
        std::vector<uint8_t> cdZip64Field;
        AppendU16LE(cdZip64Field, 0x0001u);
        AppendU16LE(cdZip64Field, 24u);
        AppendU64LE(cdZip64Field, rawStr.size());
        AppendU64LE(cdZip64Field, comp.size());
        AppendU64LE(cdZip64Field, locOff);

        AppendU32LE(cd, 0x02014B50u);
        AppendU16LE(cd, 45u);
        AppendU16LE(cd, 45u);
        AppendU16LE(cd, 0x0800u);
        AppendU16LE(cd, method);
        AppendU16LE(cd, 0x6000u);
        AppendU16LE(cd, 0x5B79u);
        AppendU32LE(cd, crc);
        AppendU32LE(cd, 0xFFFFFFFFu);
        AppendU32LE(cd, 0xFFFFFFFFu);
        AppendU16LE(cd, static_cast<uint16_t>(name.size()));
        AppendU16LE(cd, static_cast<uint16_t>(cdZip64Field.size()));
        AppendU16LE(cd, 0u);
        AppendU16LE(cd, 0u);
        AppendU16LE(cd, 0u);
        AppendU32LE(cd, 0x20u);
        AppendU32LE(cd, 0xFFFFFFFFu);
        AppendStr(cd, name);
        cd.insert(cd.end(), cdZip64Field.begin(), cdZip64Field.end());
    }

    const uint64_t cdOff = body.size();
    const uint64_t cdSz  = cd.size();
    const uint64_t eocd64Off = cdOff + cdSz;
    body.insert(body.end(), cd.begin(), cd.end());

    // ZIP64 End of Central Directory Record (0x06064B50)
    AppendU32LE(body, 0x06064B50u);
    AppendU64LE(body, 44ULL);
    AppendU16LE(body, 45u);
    AppendU16LE(body, 45u);
    AppendU32LE(body, 0u);
    AppendU32LE(body, 0u);
    AppendU64LE(body, items.size());
    AppendU64LE(body, items.size());
    AppendU64LE(body, cdSz);
    AppendU64LE(body, cdOff);

    // ZIP64 End of Central Directory Locator (0x06074B50)
    AppendU32LE(body, 0x06074B50u);
    AppendU32LE(body, 0u);
    AppendU64LE(body, eocd64Off);
    AppendU32LE(body, 1u);

    // Classic EOCD32 with 0xFFFF / 0xFFFFFFFF sentinel fields
    AppendU32LE(body, 0x06054B50u);
    AppendU16LE(body, 0u);
    AppendU16LE(body, 0u);
    AppendU16LE(body, 0xFFFFu);
    AppendU16LE(body, 0xFFFFu);
    AppendU32LE(body, 0xFFFFFFFFu);
    AppendU32LE(body, 0xFFFFFFFFu);
    AppendU16LE(body, 0u);
    return body;
}

// Synthetic FAT12 floppy image builder (12-bit FAT nibbles across non-sequential clusters 2 -> 4 -> 3)
std::vector<uint8_t> BuildFat12FloppyImg(const std::string& bootCfg, const std::string& multiClusDoc) {
    constexpr uint32_t bps = 512u;
    constexpr uint32_t totalSectors = 64u; // 32 KB < 4085 clusters => FAT12!
    constexpr uint32_t rsvdSec = 1u;
    constexpr uint32_t numFats = 2u;
    constexpr uint32_t fatSecs = 1u;
    constexpr uint32_t rootEnts = 16u;     // 1 sector
    constexpr uint32_t firstDataSec = rsvdSec + numFats * fatSecs + 1u; // sector 4
    std::vector<uint8_t> img(totalSectors * bps, 0u);

    img[0] = 0xEBu; img[1] = 0x3Cu; img[2] = 0x90u;
    std::memcpy(img.data() + 3, "MSDOS5.0", 8);
    PutU16LE(img.data() + 11, bps);
    img[13] = 1u; // 1 sector/cluster = 512B
    PutU16LE(img.data() + 14, rsvdSec);
    img[16] = numFats;
    PutU16LE(img.data() + 17, rootEnts);
    PutU16LE(img.data() + 19, totalSectors);
    img[21] = 0xF0u;
    PutU16LE(img.data() + 22, fatSecs);
    PutU16LE(img.data() + 24, 18u);
    PutU16LE(img.data() + 26, 2u);
    img[38] = 0x29u;
    std::memcpy(img.data() + 43, "FAT12FLOPPY", 11);
    std::memcpy(img.data() + 54, "FAT12   ", 8);
    img[510] = 0x55u; img[511] = 0xAAu;

    auto setFat12 = [&](uint8_t* fat, uint16_t clus, uint16_t val12) {
        const size_t off = clus + (clus / 2u);
        uint16_t cur = static_cast<uint16_t>(fat[off] | (static_cast<uint16_t>(fat[off + 1u]) << 8u));
        if ((clus & 1u) != 0u) {
            cur = static_cast<uint16_t>((cur & 0x000Fu) | ((val12 & 0x0FFFu) << 4u));
        } else {
            cur = static_cast<uint16_t>((cur & 0xF000u) | (val12 & 0x0FFFu));
        }
        PutU16LE(fat + off, cur);
    };
    auto setBothFat12 = [&](uint16_t clus, uint16_t val12) {
        setFat12(img.data() + 1u * bps, clus, val12);
        setFat12(img.data() + 2u * bps, clus, val12);
    };

    setBothFat12(0u, 0xFF0u);
    setBothFat12(1u, 0xFFFu);
    // Cluster 2: CONFIG.SYS (1 cluster -> 0xFFF)
    setBothFat12(2u, 0xFFFu);
    std::memcpy(img.data() + firstDataSec * bps, bootCfg.data(), bootCfg.size());

    // Non-sequential FAT12 chain 3 -> 5 -> 4 for README.TXT (> 1024B across 3 clusters: odd+even nibbles!)
    setBothFat12(3u, 5u);
    setBothFat12(5u, 4u);
    setBothFat12(4u, 0xFFFu);
    std::memcpy(img.data() + (firstDataSec + 1u) * bps, multiClusDoc.data(), bps);
    std::memcpy(img.data() + (firstDataSec + 3u) * bps, multiClusDoc.data() + bps, bps);
    std::memcpy(img.data() + (firstDataSec + 2u) * bps, multiClusDoc.data() + 2u * bps, multiClusDoc.size() - 2u * bps);

    uint8_t* rd = img.data() + 3u * bps;
    std::memcpy(rd + 0, "CONFIG  SYS", 11);
    rd[11] = 0x20u;
    PutU16LE(rd + 26, 2u);
    PutU32LE(rd + 28, static_cast<uint32_t>(bootCfg.size()));

    std::memcpy(rd + 32, "README  TXT", 11);
    rd[32 + 11] = 0x20u;
    PutU16LE(rd + 32 + 26, 3u);
    PutU32LE(rd + 32 + 28, static_cast<uint32_t>(multiClusDoc.size()));
    return img;
}

} // namespace

void RunFormatTestsArchivesSystemPak(TestCounter& tc) {
    const std::wstring sampleArcDir = JoinFsPath(TestDataDir(), L"archives");
    const std::wstring workDir = JoinFsPath(ScratchTestDir(), L"fmt_archives_system_pak");
    CreateDirectoryW(workDir.c_str(), nullptr);
    std::vector<std::wstring> createdFiles;

    auto writeArcBytes = [&](const wchar_t* name, const std::vector<uint8_t>& bytes) -> std::wstring {
        std::wstring p = JoinFsPath(workDir, name);
        PathUtils::WriteFileBytes(p, bytes.data(), bytes.size());
        createdFiles.push_back(p);
        return p;
    };

    const uint64_t guardBefore = ReaderGuard::StoppedCount();

    // ========================================================================
    // Section 1: Migrated Game Engine & BioWare / WAD Archive Format Tests
    // ========================================================================

    // 1.1 BioWare Aurora / Odyssey Archives (.erf/.mod/.hak/.nwm/.sav, .rim, .bif+.key)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".erf") &&
                  ArchiveReaders::IsArchiveExtension(L".mod") &&
                  ArchiveReaders::IsArchiveExtension(L".hak") &&
                  ArchiveReaders::IsArchiveExtension(L".nwm") &&
                  ArchiveReaders::IsArchiveExtension(L".sav") &&
                  ArchiveReaders::IsArchiveExtension(L".rim") &&
                  ArchiveReaders::IsArchiveExtension(L".bif"),
                  L"Stage15.Archives.BioWareAurora.ExtensionsRegistered");

        const std::wstring erfPath = JoinFsPath(sampleArcDir, L"neverwinter_quest.erf");
        const std::wstring rimPath = JoinFsPath(sampleArcDir, L"dantooine_enclave.rim");
        const std::wstring bifPath = JoinFsPath(sampleArcDir, L"aurora_data.bif");

        ArchiveSummaryInfo erfInfo, rimInfo, bifInfo;
        const bool okErf = ArchiveReaders::InspectArchiveFile(erfPath, erfInfo);
        const bool okRim = ArchiveReaders::InspectArchiveFile(rimPath, rimInfo);
        const bool okBif = ArchiveReaders::InspectArchiveFile(bifPath, bifInfo);

        std::vector<uint8_t> extNss, extLyt, extVis, extIni;
        const bool exErf = ArchiveReaders::ExtractSingleEntryToMemory(erfPath, L"module_init.nss", extNss);
        const bool exRimLyt = ArchiveReaders::ExtractSingleEntryToMemory(rimPath, L"m13aa.lyt", extLyt);
        const bool exRimVis = ArchiveReaders::ExtractSingleEntryToMemory(rimPath, L"m13aa.vis", extVis);
        const bool exBif = ArchiveReaders::ExtractSingleEntryToMemory(bifPath, L"nwnplayer.ini", extIni);

        tc.Record(okErf && erfInfo.valid && erfInfo.totalFiles == 2 && exErf && extNss.size() > 20 &&
                  std::string_view(reinterpret_cast<const char*>(extNss.data()), extNss.size()).find("Welcome to Neverwinter") != std::string_view::npos,
                  L"Stage15.Archives.BioWareAurora.SampleErfInspectAndExtract");
        tc.Record(okRim && rimInfo.valid && rimInfo.totalFiles == 2 &&
                  FindEntryCaseInsensitive(rimInfo, L"m13aa.lyt") != nullptr &&
                  FindEntryCaseInsensitive(rimInfo, L"m13aa.vis") != nullptr &&
                  exRimLyt && exRimVis &&
                  std::string_view(reinterpret_cast<const char*>(extLyt.data()), extLyt.size()).find("beginlayout") != std::string_view::npos &&
                  std::string_view(reinterpret_cast<const char*>(extVis.data()), extVis.size()).find("m13aa_01b") != std::string_view::npos,
                  L"Stage15.Archives.BioWareAurora.SampleRimSameStemDistinctResTypes");
        tc.Record(okBif && bifInfo.valid && bifInfo.totalFiles == 2 && exBif && extIni.size() > 15 &&
                  HashBytes64(extIni) != HashBytes64(extNss) &&
                  std::string_view(reinterpret_cast<const char*>(extIni.data()), extIni.size()).find("Difficulty=2") != std::string_view::npos,
                  L"Stage15.Archives.BioWareAurora.SampleBifWithKeyInspectAndExtract");

        // Negative checks: out-of-bounds ERF resource offset, zero-entry RIM, truncated BIF
        std::vector<uint8_t> badErf(180, 0);
        std::memcpy(badErf.data(), "ERF V1.0", 8);
        PutU32LE(badErf.data() + 16, 1u);
        PutU32LE(badErf.data() + 24, 160u);
        PutU32LE(badErf.data() + 28, 184u);
        AssertArcRejected(tc, L"Stage15.Archives.BioWareAurora.RejectTruncatedERF", writeArcBytes(L"bad.erf", badErf));

        std::vector<uint8_t> badRim(120, 0);
        std::memcpy(badRim.data(), "RIM V1.0", 8);
        AssertArcRejected(tc, L"Stage15.Archives.BioWareAurora.RejectZeroEntryRIM", writeArcBytes(L"empty.rim", badRim));

        std::vector<uint8_t> badBif(24, 0);
        std::memcpy(badBif.data(), "BIFFV1  ", 8);
        PutU32LE(badBif.data() + 8, 1u);
        PutU32LE(badBif.data() + 16, 20u);
        AssertArcRejected(tc, L"Stage15.Archives.BioWareAurora.RejectTruncatedBIF", writeArcBytes(L"bad.bif", badBif));
    }

    // 1.2 Quake WAD2, Half-Life WAD3 & DOOM IWAD/PWAD Map-Scoped Lump Archives (.wad)
    {
        const std::wstring w3Path = JoinFsPath(sampleArcDir, L"halflife_cstrike.wad");
        const std::wstring w2Path = JoinFsPath(sampleArcDir, L"quake1_gfx.wad");

        ArchiveSummaryInfo wad3Arc, wad2Arc;
        const bool okW3 = ArchiveReaders::InspectArchiveFile(w3Path, wad3Arc);
        const bool okW2 = ArchiveReaders::InspectArchiveFile(w2Path, wad2Arc);

        std::vector<uint8_t> extMiptex, extPal;
        const bool exW3 = ArchiveReaders::ExtractSingleEntryToMemory(w3Path, L"BRICK_WALL.miptex", extMiptex);
        const bool exW2 = ArchiveReaders::ExtractSingleEntryToMemory(w2Path, L"PALETTE.pal", extPal);

        tc.Record(okW3 && wad3Arc.valid && wad3Arc.totalFiles == 2 && exW3 &&
                  extMiptex.size() == (40u + 256u + 64u + 16u + 4u + 2u + 768u) &&
                  std::memcmp(extMiptex.data(), "BRICK_WALL", 10) == 0 &&
                  okW2 && wad2Arc.valid && wad2Arc.totalFiles == 2 && exW2 && extPal.size() == 768u &&
                  extPal[1 * 3 + 0] == 250,
                  L"Stage15.Archives.RetroGameWad.SampleWad2AndWad3InspectAndExtract");

        // DOOM PWAD multi-map namespace scoping: E1M1/THINGS.lmp vs E1M2/THINGS.lmp must never collide!
        const std::string palLump    = "PLAYPAL_DEMO_16B";
        const std::string e1m1Things = "E1M1_THINGS_PAYLOAD_01";
        const std::string e1m1Lines  = "E1M1_LINEDEFS_DATA_01";
        const std::string e1m2Things = "E1M2_THINGS_PAYLOAD_02";

        std::vector<uint8_t> pwad = { 'P', 'W', 'A', 'D' };
        AppendU32LE(pwad, 6u); // 6 lumps: PLAYPAL, E1M1(0B), THINGS, LINEDEFS, E1M2(0B), THINGS
        const uint32_t offPal   = 12u;
        const uint32_t offT1    = offPal + static_cast<uint32_t>(palLump.size());
        const uint32_t offL1    = offT1 + static_cast<uint32_t>(e1m1Things.size());
        const uint32_t offT2    = offL1 + static_cast<uint32_t>(e1m1Lines.size());
        const uint32_t dirStart = offT2 + static_cast<uint32_t>(e1m2Things.size());
        AppendU32LE(pwad, dirStart);
        AppendStr(pwad, palLump);
        AppendStr(pwad, e1m1Things);
        AppendStr(pwad, e1m1Lines);
        AppendStr(pwad, e1m2Things);

        auto pushDoomLump = [&](uint32_t fOff, uint32_t fLen, std::string_view name8) {
            AppendU32LE(pwad, fOff);
            AppendU32LE(pwad, fLen);
            AppendFixedAscii(pwad, name8, 8u);
        };
        pushDoomLump(offPal, static_cast<uint32_t>(palLump.size()),    "PLAYPAL");
        pushDoomLump(0u,     0u,                                       "E1M1");
        pushDoomLump(offT1,  static_cast<uint32_t>(e1m1Things.size()), "THINGS");
        pushDoomLump(offL1,  static_cast<uint32_t>(e1m1Lines.size()),  "LINEDEFS");
        pushDoomLump(0u,     0u,                                       "E1M2");
        pushDoomLump(offT2,  static_cast<uint32_t>(e1m2Things.size()), "THINGS");

        const std::wstring pwadPath = writeArcBytes(L"doom_maps.wad", pwad);
        ArchiveSummaryInfo doomInfo;
        std::vector<uint8_t> outT1, outT2, outPal;
        const bool okDoom = ArchiveReaders::InspectArchiveFile(pwadPath, doomInfo) &&
                            doomInfo.valid && doomInfo.formatName == L"DOOM PWAD" &&
                            FindEntryCaseInsensitive(doomInfo, L"PLAYPAL.lmp") != nullptr &&
                            FindEntryCaseInsensitive(doomInfo, L"E1M1") != nullptr &&
                            FindEntryCaseInsensitive(doomInfo, L"E1M1")->isDirectory &&
                            FindEntryCaseInsensitive(doomInfo, L"E1M1/THINGS.lmp") != nullptr &&
                            FindEntryCaseInsensitive(doomInfo, L"E1M1/LINEDEFS.lmp") != nullptr &&
                            FindEntryCaseInsensitive(doomInfo, L"E1M2/THINGS.lmp") != nullptr &&
                            ArchiveReaders::ExtractSingleEntryToMemory(pwadPath, L"E1M1/THINGS.lmp", outT1) &&
                            BytesEqualStr(outT1, e1m1Things) &&
                            ArchiveReaders::ExtractSingleEntryToMemory(pwadPath, L"E1M2/THINGS", outT2) &&
                            BytesEqualStr(outT2, e1m2Things) &&
                            ArchiveReaders::ExtractSingleEntryToMemory(pwadPath, L"PLAYPAL.lmp", outPal) &&
                            BytesEqualStr(outPal, palLump);
        tc.Record(okDoom, L"Stage15.Archives.DoomWad.MapScopedDuplicateLumpsAndExtract");

        std::vector<uint8_t> badWad3 = { 'W','A','D','3', 5,0,0,0, 200,0,0,0, 0,0,0,0 };
        AssertArcRejected(tc, L"Stage15.Archives.RetroGameWad.RejectTruncatedWad3", writeArcBytes(L"bad.wad", badWad3));

        std::vector<uint8_t> badPwad = { 'I','W','A','D', 2,0,0,0, 12,0,0,0 };
        AppendU32LE(badPwad, 9999u);
        AppendU32LE(badPwad, 64u);
        AppendFixedAscii(badPwad, "THINGS", 8u);
        AssertArcRejected(tc, L"Stage15.Archives.DoomWad.RejectOutOfBoundsLump", writeArcBytes(L"bad_iwad.wad", badPwad));
    }

    // 1.3 Ren'Py Visual Novel Archive (.rpa — RPA-3.0 & RPA-2.0)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".rpa"),
                  L"Stage15.Archives.RenPyRPA_ExtensionRegistered");

        const std::wstring rpaPath = JoinFsPath(sampleArcDir, L"visual_novel_assets.rpa");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(rpaPath, a1);
        tc.Record(ok1 && a1.valid && a1.totalFiles == 3 && a1.entries.size() == 3,
                  L"Stage15.Archives.RenPyRPA_Rpa30IndexAndXorParsed");

        std::vector<uint8_t> scriptBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(rpaPath, L"game/script.rpy", scriptBytes);
        const char* expectedScript = "label start:\n    scene bg clubroom\n    \"Monika\" \"Welcome to Explorer2!\"\n    return\n";
        tc.Record(ex1 && BytesEqualStr(scriptBytes, expectedScript),
                  L"Stage15.Archives.RenPyRPA_ExtractPrefixPlusBodyExact");

        std::vector<uint8_t> metaBytes;
        const bool exMeta = ArchiveReaders::ExtractSingleEntryToMemory(rpaPath, L"images/ui/logo_meta.txt", metaBytes);
        const char* expectedMeta = "PNG_META:width=256;height=128;format=RGBA8\n";
        tc.Record(exMeta && BytesEqualStr(metaBytes, expectedMeta),
                  L"Stage15.Archives.RenPyRPA_ExtractNestedSubfolderEntry");

        std::string rpa2Hdr = "RPA-2.0 0000000000000024\n";
        std::string rpa2Pay = "HELLO_RPA2\n";
        std::string pkl0 = "(dS'hello.txt'\n(l(I25\nI11\ntas.";
        std::vector<uint8_t> zPkl0 = WrapZlibStored(std::vector<uint8_t>(pkl0.begin(), pkl0.end()));
        std::vector<uint8_t> rpa2Blob(rpa2Hdr.begin(), rpa2Hdr.end());
        rpa2Blob.insert(rpa2Blob.end(), rpa2Pay.begin(), rpa2Pay.end());
        rpa2Blob.insert(rpa2Blob.end(), zPkl0.begin(), zPkl0.end());

        const std::wstring rpa2Path = writeArcBytes(L"rpa2_proto0.rpa", rpa2Blob);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(rpa2Path, a2);
        std::vector<uint8_t> rpa2Ext;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(rpa2Path, L"hello.txt", rpa2Ext);
        tc.Record(ok2 && a2.valid && ex2 && BytesEqualStr(rpa2Ext, rpa2Pay) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.RenPyRPA_Rpa20Proto0AndDistinctHash");

        std::string evilPkl = "cos\nsystem\n(S'calc'\ntR.";
        std::vector<uint8_t> zEvil = WrapZlibStored(std::vector<uint8_t>(evilPkl.begin(), evilPkl.end()));
        std::string evilHdr = "RPA-3.0 0000000000000022 00000000\n";
        std::vector<uint8_t> evilRpa(evilHdr.begin(), evilHdr.end());
        evilRpa.insert(evilRpa.end(), zEvil.begin(), zEvil.end());
        AssertArcRejected(tc, L"Stage15.Archives.RenPyRPA_RejectPickleGlobalCodeExec",
                          writeArcBytes(L"evil_global.rpa", evilRpa));

        std::string oobPkl = "(dS'bad.bin'\n(l(I999999\nI64\ntas.";
        std::vector<uint8_t> zOob = WrapZlibStored(std::vector<uint8_t>(oobPkl.begin(), oobPkl.end()));
        std::vector<uint8_t> oobRpa(evilHdr.begin(), evilHdr.end());
        oobRpa.insert(oobRpa.end(), zOob.begin(), zOob.end());
        AssertArcRejected(tc, L"Stage15.Archives.RenPyRPA_RejectOutOfBoundsChunk",
                          writeArcBytes(L"oob.rpa", oobRpa));
    }

    // 1.4 Parallax Descent 1/2 ("DHF") & Outrage Descent 3 ("HOG2") (.hog)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".hog"),
                  L"Stage15.Archives.DescentHOG_ExtensionRegistered");

        const std::wstring hog1Path = JoinFsPath(sampleArcDir, L"descent_mines.hog");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(hog1Path, a1);
        tc.Record(ok1 && a1.valid && a1.totalFiles == 3,
                  L"Stage15.Archives.DescentHOG_DhfMissionParsed");

        std::vector<uint8_t> briefing;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(hog1Path, L"briefing.txb", briefing);
        const char* expectedBrief = "PTRO_BRIEFING: Destroy the reactor core in Sector 7 and escape!\n";
        tc.Record(ex1 && BytesEqualStr(briefing, expectedBrief),
                  L"Stage15.Archives.DescentHOG_ExtractDhfEntryExact");

        std::vector<uint8_t> hog2 = { 'H','O','G','2' };
        AppendU32LE(hog2, 2u);
        AppendU32LE(hog2, 68u + 2u * 48u);
        hog2.insert(hog2.end(), 56u, 0xFFu);
        auto pushHog2Dir = [&](const char* name, uint32_t len, uint32_t ts) {
            AppendFixedAscii(hog2, name, 36u);
            AppendU32LE(hog2, 0u);
            AppendU32LE(hog2, len);
            AppendU32LE(hog2, ts);
        };
        const std::string pA = "D3_LEVEL_ORBITAL.D3L";
        const std::string pB = "D3_REACTOR_SOUND.WAV";
        pushHog2Dir("orbital.d3l", static_cast<uint32_t>(pA.size()), 1700000000u);
        pushHog2Dir("reactor.wav", static_cast<uint32_t>(pB.size()), 1700003600u);
        AppendStr(hog2, pA);
        AppendStr(hog2, pB);

        const std::wstring hog2Path = writeArcBytes(L"descent3.hog", hog2);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(hog2Path, a2);
        std::vector<uint8_t> d3Ext;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(hog2Path, L"reactor.wav", d3Ext);
        tc.Record(ok2 && a2.valid && a2.totalFiles == 2 && a2.entries[0].dosDate != 0 &&
                  ex2 && BytesEqualStr(d3Ext, pB) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.DescentHOG_Hog2AndDistinctHash");

        std::vector<uint8_t> badDhf = { 'D','H','F', 't','e','s','t','.','b','i','n',0,0,0,0,0, 99,0,0,0, 1,2,3 };
        AssertArcRejected(tc, L"Stage15.Archives.DescentHOG_RejectTruncatedDhfPayload",
                          writeArcBytes(L"trunc.hog", badDhf));

        std::vector<uint8_t> travDhf = { 'D','H','F', '.','.','/','x','.','b','i','n',0,0,0,0,0, 2,0,0,0, 0xAA,0xBB };
        AssertArcRejected(tc, L"Stage15.Archives.DescentHOG_RejectPathTraversal",
                          writeArcBytes(L"trav.hog", travDhf));
    }

    // 1.5 Volition Red Faction / Summoner VPP Package (.vpp)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".vpp"),
                  L"Stage15.Archives.VolitionVPP_ExtensionRegistered");

        const std::wstring vpp1Path = JoinFsPath(sampleArcDir, L"red_faction_mars.vpp");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(vpp1Path, a1);
        tc.Record(ok1 && a1.valid && a1.totalFiles == 3,
                  L"Stage15.Archives.VolitionVPP_SectorAlignedPackageParsed");

        std::vector<uint8_t> tblBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(vpp1Path, L"weapons.tbl", tblBytes);
        const char* expectedTbl = "#Primary Weapons\n$Name: \"Rail Driver\"\n$Mass: 0.4\n$Velocity: 275.0\n#End\n";
        tc.Record(ex1 && BytesEqualStr(tblBytes, expectedTbl),
                  L"Stage15.Archives.VolitionVPP_ExtractSector0FileExact");

        std::vector<uint8_t> rflBytes;
        const bool exRfl = ArchiveReaders::ExtractSingleEntryToMemory(vpp1Path, L"mars_colony.rfl", rflBytes);
        static const uint8_t expectedRfl[35] = {
            0xCE, 0x0A, 0x89, 0x51,
            'R','F','L','_','G','E','O','M','O','D','_','S','E','C','T','O','R','_','H','E','A','D','E','R','_','V','1',
            0x00, 0x10, 0x20, 0x30
        };
        tc.Record(exRfl && rflBytes.size() == sizeof(expectedRfl) &&
                  std::memcmp(rflBytes.data(), expectedRfl, sizeof(expectedRfl)) == 0,
                  L"Stage15.Archives.VolitionVPP_ExtractSecondSectorAlignedFile");

        std::vector<uint8_t> vppSummoner;
        AppendU32LE(vppSummoner, 0x51890ACEu);
        AppendU32LE(vppSummoner, 1u);
        AppendU32LE(vppSummoner, 1u);
        AppendU32LE(vppSummoner, 2048u * 4u);
        vppSummoner.resize(4096u, 0u);
        const char* sName = "summoner_spells.tbl";
        std::memcpy(vppSummoner.data() + 2048u, sName, std::strlen(sName));
        const uint32_t bigSz = 2500u;
        PutU32LE(vppSummoner.data() + 2048u + 60u, bigSz);
        vppSummoner.resize(8192u, 0u);
        for (uint32_t i = 0; i < bigSz; ++i) vppSummoner[4096u + i] = static_cast<uint8_t>((i * 7u + 3u) & 0xFFu);

        const std::wstring vppSummonerPath = writeArcBytes(L"summoner.vpp", vppSummoner);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(vppSummonerPath, a2);
        std::vector<uint8_t> extBig;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(vppSummonerPath, L"summoner_spells.tbl", extBig);
        tc.Record(ok2 && a2.valid && ex2 && extBig.size() == 2500u &&
                  extBig[0] == 3u && extBig[2499] == static_cast<uint8_t>((2499u * 7u + 3u) & 0xFFu) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.VolitionVPP_MultiSectorFileAndDistinctHash");

        std::vector<uint8_t> badVpp = vppSummoner;
        badVpp[2048u + 62u] = 0x10u;
        AssertArcRejected(tc, L"Stage15.Archives.VolitionVPP_RejectOversizeEntry",
                          writeArcBytes(L"bad_sz.vpp", badVpp));
    }

    // 1.6 Westwood Studios Command & Conquer / Red Alert MIX (.mix)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".mix"),
                  L"Stage15.Archives.WestwoodMIX_ExtensionRegistered");

        const std::wstring mix1Path = JoinFsPath(sampleArcDir, L"conquer_allied.mix");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(mix1Path, a1);
        tc.Record(ok1 && a1.valid && a1.totalFiles == 4,
                  L"Stage15.Archives.WestwoodMIX_RaFlagsAndXccNameDbParsed");

        std::vector<uint8_t> voxIni;
        const bool exVox = ArchiveReaders::ExtractSingleEntryToMemory(mix1Path, L"grizzly_vox.ini", voxIni);
        const char* expectedVox = "[MTNK]\nImage=GTNK\nVoxel=yes\nPrimary=105mm\n";
        tc.Record(exVox && BytesEqualStr(voxIni, expectedVox),
                  L"Stage15.Archives.WestwoodMIX_ExtractXccResolvedEntryExact");

        std::vector<uint8_t> mixClassic;
        const std::string palPay = "TD_PALETTE_768_DEMO";
        AppendU16LE(mixClassic, 1u);
        AppendU32LE(mixClassic, static_cast<uint32_t>(palPay.size()));
        AppendU32LE(mixClassic, 0xDEADBEEFu);
        AppendU32LE(mixClassic, 0u);
        AppendU32LE(mixClassic, static_cast<uint32_t>(palPay.size()));
        AppendStr(mixClassic, palPay);

        const std::wstring mixClassicPath = writeArcBytes(L"classic_td.mix", mixClassic);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(mixClassicPath, a2);
        std::vector<uint8_t> palExt;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(mixClassicPath, L"MIX_DEADBEEF.bin", palExt);
        tc.Record(ok2 && a2.valid && a2.totalFiles == 1 && ex2 && BytesEqualStr(palExt, palPay) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.WestwoodMIX_ClassicTdAndFallbackHexExtract");

        std::vector<uint8_t> encMix;
        AppendU16LE(encMix, 0u);
        AppendU16LE(encMix, 0x0002u);
        encMix.insert(encMix.end(), 64u, 0x55u);
        AssertArcRejected(tc, L"Stage15.Archives.WestwoodMIX_DeclineEncryptedHeaderCleanly",
                          writeArcBytes(L"encrypted.mix", encMix));

        std::vector<uint8_t> badMix = mixClassic;
        badMix[14] = 0xFFu;
        AssertArcRejected(tc, L"Stage15.Archives.WestwoodMIX_RejectSubBlockOverflow",
                          writeArcBytes(L"overflow.mix", badMix));
    }

    // 1.7 Volition FreeSpace 1 & 2 "VPVP" Package (.vp)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".vp"),
                  L"Stage15.Archives.VolitionVP_ExtensionRegistered");

        const std::wstring vpPath = JoinFsPath(sampleArcDir, L"freespace2_root.vp");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(vpPath, a1);
        tc.Record(ok1 && a1.valid && a1.totalFiles == 2 && a1.entries.size() == 2,
                  L"Stage15.Archives.VolitionVP_HierarchicalBackdirTocParsed");

        std::vector<uint8_t> tblBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(vpPath, L"data/tables/ships.tbl", tblBytes);
        const char* expectedTbl = "$Name: GTF Ulysses\n$Max Velocity: 0.0, 0.0, 70.0\n";
        tc.Record(ex1 && BytesEqualStr(tblBytes, expectedTbl),
                  L"Stage15.Archives.VolitionVP_ExtractNestedShipsTblExact");

        std::vector<uint8_t> vpFlat = { 'V', 'P', 'V', 'P' };
        AppendI32LE(vpFlat, 2);
        AppendI32LE(vpFlat, 24);
        AppendI32LE(vpFlat, 1);
        AppendStr(vpFlat, "FS1_PILO");
        AppendI32LE(vpFlat, 16);
        AppendI32LE(vpFlat, 8);
        AppendFixedAscii(vpFlat, "readme.txt", 32u);
        AppendU32LE(vpFlat, 910000000u);

        const std::wstring vpFlatPath = writeArcBytes(L"flat.vp", vpFlat);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(vpFlatPath, a2);
        tc.Record(ok2 && a2.valid && a2.entries.size() == 1 &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.VolitionVP_FlatArchiveAndDistinctHash");

        std::vector<uint8_t> badPop = { 'V', 'P', 'V', 'P' };
        AppendI32LE(badPop, 2);
        AppendI32LE(badPop, 16);
        AppendI32LE(badPop, 1);
        AppendI32LE(badPop, 0);
        AppendI32LE(badPop, 0);
        AppendFixedAscii(badPop, "..", 32u);
        AppendU32LE(badPop, 0u);
        AssertArcRejected(tc, L"Stage15.Archives.VolitionVP_RejectRootBackdirUnderflow",
                          writeArcBytes(L"bad_pop.vp", badPop));

        std::vector<uint8_t> badLen = vpFlat;
        badLen[24 + 4] = 0xFFu;
        AssertArcRejected(tc, L"Stage15.Archives.VolitionVP_RejectEntryPastEof",
                          writeArcBytes(L"bad_len.vp", badLen));
    }

    // 1.8 LucasArts Dark Forces ("GOB\x0A") & Jedi Knight ("GOB " v20) (.gob)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".gob"),
                  L"Stage15.Archives.LucasArtsGOB_ExtensionRegistered");

        const std::wstring gobPath = JoinFsPath(sampleArcDir, L"jedi_knight_patch.gob");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(gobPath, a1);
        std::vector<uint8_t> cogBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(gobPath, L"cog/kyle_saber.cog", cogBytes);
        const char* expectedCog = "# Jedi Knight COG Script\nsymbols\nmessage startup\nend\ncode\nstartup: Return;\nend\n";
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2 && ex1 && BytesEqualStr(cogBytes, expectedCog),
                  L"Stage15.Archives.LucasArtsGOB_JediKnightV20AndExtractExact");

        std::vector<uint8_t> dfGob = { 'G', 'O', 'B', 0x0Au };
        const std::string palPay = "DF_PAL_768";
        AppendU32LE(dfGob, 18u);
        AppendStr(dfGob, palPay);
        AppendU32LE(dfGob, 1u);
        AppendU32LE(dfGob, 8u);
        AppendU32LE(dfGob, 10u);
        AppendFixedAscii(dfGob, "SECBASE.PAL", 13u);

        const std::wstring dfPath = writeArcBytes(L"darkforces.gob", dfGob);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(dfPath, a2);
        std::vector<uint8_t> dfOut;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(dfPath, L"SECBASE.PAL", dfOut);
        tc.Record(ok2 && a2.valid && ex2 && BytesEqualStr(dfOut, palPay) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.LucasArtsGOB_DarkForcesV1AndDistinctHash");

        std::vector<uint8_t> travGob = { 'G', 'O', 'B', ' ' };
        AppendU32LE(travGob, 20u);
        AppendU32LE(travGob, 12u);
        AppendU32LE(travGob, 1u);
        AppendU32LE(travGob, 152u);
        AppendU32LE(travGob, 4u);
        AppendFixedAscii(travGob, "..\\evil.dll", 128u);
        travGob.insert(travGob.end(), 4u, 'A');
        AssertArcRejected(tc, L"Stage15.Archives.LucasArtsGOB_RejectPathTraversal",
                          writeArcBytes(L"trav.gob", travGob));

        std::vector<uint8_t> badMaster = dfGob;
        badMaster[4] = 0xFFu;
        AssertArcRejected(tc, L"Stage15.Archives.LucasArtsGOB_RejectMasterOffPastEof",
                          writeArcBytes(L"bad_master.gob", badMaster));
    }

    // 1.9 LucasArts Grim Fandango / Outlaws / EMI "LABN" Archive (.lab)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".lab"),
                  L"Stage15.Archives.LucasArtsLAB_ExtensionRegistered");

        const std::wstring labPath = JoinFsPath(sampleArcDir, L"grim_fandango_data.lab");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(labPath, a1);
        std::vector<uint8_t> setBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(labPath, L"rubacava/dock.set", setBytes);
        const char* expectedSet = "SET_LIGHTS 4\nAMBIENT 0.25 0.30 0.45\n";
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2 && ex1 && BytesEqualStr(setBytes, expectedSet),
                  L"Stage15.Archives.LucasArtsLAB_GrimFandangoTocAndExtractExact");

        const char* emiName = "guybrush.lua";
        const size_t poolLen = std::strlen(emiName) + 1u;
        const std::string emiPay = "say_line('Look behind you!')\n";
        const uint32_t dataOff = static_cast<uint32_t>(16u + 16u + poolLen);
        std::vector<uint8_t> emiLab = { 'L', 'A', 'B', 'N' };
        AppendU32LE(emiLab, 0x00010000u);
        AppendU32LE(emiLab, 1u);
        AppendU32LE(emiLab, static_cast<uint32_t>(poolLen));
        AppendU32LE(emiLab, 0u);
        AppendU32LE(emiLab, dataOff);
        AppendU32LE(emiLab, static_cast<uint32_t>(emiPay.size()));
        AppendU32LE(emiLab, 0u);
        for (size_t i = 0; i < poolLen; ++i) {
            emiLab.push_back(static_cast<uint8_t>(emiName[i]) ^ 0x96u);
        }
        AppendStr(emiLab, emiPay);

        const std::wstring emiPath = writeArcBytes(L"emi_xor.lab", emiLab);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(emiPath, a2);
        std::vector<uint8_t> emiOut;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(emiPath, L"guybrush.lua", emiOut);
        tc.Record(ok2 && a2.valid && ex2 && BytesEqualStr(emiOut, emiPay) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.LucasArtsLAB_EmiXorStringPoolAndDistinctHash");

        std::vector<uint8_t> badPool = emiLab;
        badPool[0x0D] = 0x40u;
        AssertArcRejected(tc, L"Stage15.Archives.LucasArtsLAB_RejectOversizeStringPool",
                          writeArcBytes(L"bad_pool.lab", badPool));

        std::vector<uint8_t> overlapLab = emiLab;
        overlapLab[0x14] = 8u;
        AssertArcRejected(tc, L"Stage15.Archives.LucasArtsLAB_RejectOverlappingDataOffset",
                          writeArcBytes(L"overlap.lab", overlapLab));
    }

    // 1.10 Terminal Reality POD1 / "POD2" / "POD3" Archive (.pod)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".pod"),
                  L"Stage15.Archives.TerminalRealityPOD_ExtensionRegistered");

        const std::wstring pod3Path = JoinFsPath(sampleArcDir, L"bloodrayne_assets.pod");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(pod3Path, a1);
        std::vector<uint8_t> trkBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(pod3Path, L"trucks/gravedigger.trk", trkBytes);
        const char* expectedTrk = "[Vehicle]\nName=GraveDigger\nMassKg=4500\n";
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2 && ex1 && BytesEqualStr(trkBytes, expectedTrk),
                  L"Stage15.Archives.TerminalRealityPOD_Pod3TocAndExtractExact");

        std::vector<uint8_t> pod1;
        AppendU32LE(pod1, 1u);
        AppendFixedAscii(pod1, "Terminal Velocity Shareware POD", 80u);
        AppendFixedAscii(pod1, "DATA\\SHIP.BIN", 32u);
        const std::string pod1Pay = "TV_HELLBENDER_95";
        AppendU32LE(pod1, static_cast<uint32_t>(pod1Pay.size()));
        AppendU32LE(pod1, 124u);
        AppendStr(pod1, pod1Pay);

        const std::wstring pod1Path = writeArcBytes(L"termvel.pod", pod1);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(pod1Path, a2);
        std::vector<uint8_t> pod1Out;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(pod1Path, L"DATA/SHIP.BIN", pod1Out);
        tc.Record(ok2 && a2.valid && ex2 && BytesEqualStr(pod1Out, pod1Pay) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.TerminalRealityPOD_Pod1LegacyAndDistinctHash");

        std::vector<uint8_t> badPod1 = pod1;
        badPod1[5] = 0x03u;
        AssertArcRejected(tc, L"Stage15.Archives.TerminalRealityPOD_RejectGarbagePod1",
                          writeArcBytes(L"garbage.pod", badPod1));

        std::vector<uint8_t> badPod3(296u, 0u);
        std::memcpy(badPod3.data(), "POD3", 4);
        badPod3[0x058] = 1u;
        badPod3[0x109] = 0x50u;
        badPod3[0x110] = 8u;
        AssertArcRejected(tc, L"Stage15.Archives.TerminalRealityPOD_RejectPod3DirPastEof",
                          writeArcBytes(L"bad_dir.pod", badPod3));
    }

    // 1.11 Piranha Bytes Gothic I & II ZenGin "PSVDSC_V2.00" VDFS Volume (.vdf)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".vdf"),
                  L"Stage15.Archives.GothicVDFS_ExtensionRegistered");

        const std::wstring vdfPath = JoinFsPath(sampleArcDir, L"gothic2_khorinis.vdf");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(vdfPath, a1);
        std::vector<uint8_t> daedBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(vdfPath, L"_WORK/DATA/SWORD.D", daedBytes);
        const char* expectedDaed = "INSTANCE ItMw_1H_Sword_01 (C_Item) { damageTotal = 45; };\n";
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2 && ex1 && BytesEqualStr(daedBytes, expectedDaed),
                  L"Stage15.Archives.GothicVDFS_SpacePaddedHierarchyAndExtractExact");

        std::vector<uint8_t> g1Vdf(256u, 0x1Au);
        const char* g1Sig = "PSVDSC_V2.00\r\n\r\n";
        g1Vdf.insert(g1Vdf.end(), g1Sig, g1Sig + 16);
        AppendU32LE(g1Vdf, 1u);
        AppendU32LE(g1Vdf, 1u);
        AppendU32LE(g1Vdf, 0u);
        AppendU32LE(g1Vdf, 12u);
        AppendU32LE(g1Vdf, 296u);
        AppendU32LE(g1Vdf, 0x50u);
        AppendFixedAscii(g1Vdf, "WORLD.ZEN", 64u, ' ');
        AppendU32LE(g1Vdf, 376u);
        AppendU32LE(g1Vdf, 12u);
        AppendU32LE(g1Vdf, 0x40000000u);
        AppendU32LE(g1Vdf, 0u);
        AppendStr(g1Vdf, "ZenGin_1.08k");

        const std::wstring g1Path = writeArcBytes(L"gothic1.vdf", g1Vdf);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(g1Path, a2);
        tc.Record(ok2 && a2.valid && a2.entries.size() == 1 &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.GothicVDFS_Gothic1SigAndDistinctHash");

        std::vector<uint8_t> cycVdf(256u, 0x1Au);
        cycVdf.insert(cycVdf.end(), g1Sig, g1Sig + 16);
        AppendU32LE(cycVdf, 2u); AppendU32LE(cycVdf, 0u); AppendU32LE(cycVdf, 0u);
        AppendU32LE(cycVdf, 0u); AppendU32LE(cycVdf, 296u); AppendU32LE(cycVdf, 0x50u);
        AppendFixedAscii(cycVdf, "DIR_A", 64u, ' ');
        AppendU32LE(cycVdf, 1u); AppendU32LE(cycVdf, 0u); AppendU32LE(cycVdf, 0xC0000000u); AppendU32LE(cycVdf, 0u);
        AppendFixedAscii(cycVdf, "DIR_B", 64u, ' ');
        AppendU32LE(cycVdf, 1u); AppendU32LE(cycVdf, 0u); AppendU32LE(cycVdf, 0xC0000000u); AppendU32LE(cycVdf, 0u);
        AssertArcRejected(tc, L"Stage15.Archives.GothicVDFS_RejectDirectoryCycleBomb",
                          writeArcBytes(L"cycle.vdf", cycVdf));
    }

    // 1.12 Monolith LithTech "RezMgr Version 1" Resource Archive (.rez)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".rez"),
                  L"Stage15.Archives.LithTechREZ_ExtensionRegistered");

        const std::wstring rezPath = JoinFsPath(sampleArcDir, L"nolf_gadgets.rez");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(rezPath, a1);
        std::vector<uint8_t> wepBytes;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(rezPath, L"ATTRIBUTES/WEAPONS.TXT", wepBytes);
        const char* expectedWep = "Name = \"Lipstick Explosive\"\nDamage = 120\n";
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2 && ex1 && BytesEqualStr(wepBytes, expectedWep),
                  L"Stage15.Archives.LithTechREZ_SubdirTypToStrAndExtractExact");

        std::vector<uint8_t> rezBlood2 = { '\r', '\n' };
        AppendFixedAscii(rezBlood2, "RezMgr Version 1 Copyright (C) 1995 MONOLITH INC.", 60u, ' ');
        rezBlood2.push_back('\r'); rezBlood2.push_back('\n');
        AppendFixedAscii(rezBlood2, "BLOOD2 SKIN", 60u, ' ');
        rezBlood2.push_back('\r'); rezBlood2.push_back('\n'); rezBlood2.push_back(0x1Au);

        const std::string calebPay = "DTX_CALEB";
        std::vector<uint8_t> r2Dir;
        AppendU32LE(r2Dir, 0u);
        AppendU32LE(r2Dir, 168u);
        AppendU32LE(r2Dir, 9u);
        AppendU32LE(r2Dir, 0u);
        AppendU32LE(r2Dir, 1u);
        AppendU32LE(r2Dir, (uint32_t('D') << 16) | (uint32_t('T') << 8) | uint32_t('X'));
        AppendU32LE(r2Dir, 0u);
        const char r2Names[7] = { 'C','A','L','E','B',0,0 };
        r2Dir.insert(r2Dir.end(), r2Names, r2Names + 7);

        AppendU32LE(rezBlood2, 1u);
        AppendU32LE(rezBlood2, 177u);
        AppendU32LE(rezBlood2, static_cast<uint32_t>(r2Dir.size()));
        for (int k = 0; k < 7; ++k) AppendU32LE(rezBlood2, 0u);
        rezBlood2.push_back(1u);
        AppendStr(rezBlood2, calebPay);
        rezBlood2.insert(rezBlood2.end(), r2Dir.begin(), r2Dir.end());

        const std::wstring rezBlood2Path = writeArcBytes(L"blood2.rez", rezBlood2);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(rezBlood2Path, a2);
        std::vector<uint8_t> r2Out;
        const bool ex2 = ArchiveReaders::ExtractSingleEntryToMemory(rezBlood2Path, L"CALEB.DTX", r2Out);
        tc.Record(ok2 && a2.valid && ex2 && BytesEqualStr(r2Out, calebPay) &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.LithTechREZ_Blood2DtxAndDistinctHash");

        std::vector<uint8_t> badSub = rezBlood2;
        badSub[126] = 0x20u;
        AssertArcRejected(tc, L"Stage15.Archives.LithTechREZ_RejectMissing0x1AMarker",
                          writeArcBytes(L"no_1a.rez", badSub));

        std::vector<uint8_t> cycRez(rezBlood2.begin(), rezBlood2.begin() + 177u);
        std::vector<uint8_t> cycDir;
        AppendU32LE(cycDir, 1u);
        AppendU32LE(cycDir, 177u);
        AppendU32LE(cycDir, 21u);
        AppendU32LE(cycDir, 0u);
        const char loopNm[5] = { 'L','O','O','P',0 };
        cycDir.insert(cycDir.end(), loopNm, loopNm + 5);
        PutU32LE(cycRez.data() + 135u, static_cast<uint32_t>(cycDir.size()));
        PutU32LE(cycDir.data() + 8u, static_cast<uint32_t>(cycDir.size()));
        cycRez.insert(cycRez.end(), cycDir.begin(), cycDir.end());
        AssertArcRejected(tc, L"Stage15.Archives.LithTechREZ_RejectSelfLoopDirectory",
                          writeArcBytes(L"loop.rez", cycRez));
    }

    // 1.13 Monolith Blood "RFF\x1A" Encrypted Archive (.rff)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".rff"),
                  L"Stage15.Archives.BloodRFF_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"blood_cryptic.rff");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 1 && a1.entries[0].pathInArchive == L"BLOOD.INI",
                  L"Stage15.Archives.BloodRFF_V31EncryptedTocParsed");

        std::vector<uint8_t> outB;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"BLOOD.INI", outB);
        const char* exp1 = "[Episode1]\nTitle = The Way of All Flesh\n";
        tc.Record(ex1 && BytesEqualStr(outB, exp1),
                  L"Stage15.Archives.BloodRFF_DecryptAndExtractExact");

        std::vector<uint8_t> rffPlain(32u, 0u);
        std::memcpy(rffPlain.data(), "RFF\x1A", 4);
        rffPlain[5] = 0x02u;
        PutU32LE(rffPlain.data() + 8u, 36u);
        rffPlain[12] = 1u;
        rffPlain.insert(rffPlain.end(), { 'Q','A','V','1' });
        std::vector<uint8_t> e2(48u, 0u);
        e2[0x10] = 32u; e2[0x14] = 4u;
        std::memcpy(e2.data() + 0x21u, "QAV", 3);
        std::memcpy(e2.data() + 0x24u, "FORK", 4);
        rffPlain.insert(rffPlain.end(), e2.begin(), e2.end());

        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"unenc.rff", rffPlain), a2);
        tc.Record(ok2 && a2.valid && HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.BloodRFF_V20UnencryptedAndDistinctHash");

        rffPlain[5] = 0x99u;
        AssertArcRejected(tc, L"Stage15.Archives.BloodRFF_RejectUnsupportedVersion", writeArcBytes(L"bad.rff", rffPlain));
    }

    // 1.14 Ritual Entertainment SiN "SPAK" Archive (.sin)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".sin"),
                  L"Stage15.Archives.RitualSIN_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"sin_base_pak.sin");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2,
                  L"Stage15.Archives.RitualSIN_SpakTocParsed");

        std::vector<uint8_t> outB;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"cfg/autoexec.cfg", outB);
        const char* exp1 = "map base1\nskill 2\n";
        tc.Record(ex1 && BytesEqualStr(outB, exp1),
                  L"Stage15.Archives.RitualSIN_ExtractExact");

        std::vector<uint8_t> sinMini = { 'S','P','A','K' };
        AppendU32LE(sinMini, 16u);
        AppendU32LE(sinMini, 128u);
        sinMini.insert(sinMini.end(), { 'S','I','N','!' });
        AppendFixedAscii(sinMini, "base.cfg", 120u);
        AppendU32LE(sinMini, 12u);
        AppendU32LE(sinMini, 4u);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"mini.sin", sinMini), a2);
        tc.Record(ok2 && a2.valid && HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.RitualSIN_VariantAndDistinctHash");

        sinMini[8] = 77u;
        AssertArcRejected(tc, L"Stage15.Archives.RitualSIN_RejectMisalignedDirLen", writeArcBytes(L"bad.sin", sinMini));
    }

    // 1.15 Ensemble Studios Age of Empires III "ESPN" BAR Archive (.bar)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".bar"),
                  L"Stage15.Archives.EnsembleBAR_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"aoe3_art_pack.bar");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 1 && a1.entries[0].pathInArchive == L"Data/protoy.xml",
                  L"Stage15.Archives.EnsembleBAR_EspnUtf16TocParsed");

        std::vector<uint8_t> outB;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"Data/protoy.xml", outB);
        const char* exp1 = "<proto version='2'><unit name='Falconet'><hp>150</hp></unit></proto>\n";
        tc.Record(ex1 && BytesEqualStr(outB, exp1),
                  L"Stage15.Archives.EnsembleBAR_ExtractExact");

        std::vector<uint8_t> barAom(0x124u, 0u);
        std::memcpy(barAom.data(), "ESPN", 4);
        barAom[4] = 1u;
        PutU32LE(barAom.data() + 8u, 0x44332211u);
        barAom.insert(barAom.end(), { 'X','M','B','1' });
        const uint32_t ftrOff = 0x128u;
        std::vector<uint8_t> ftr;
        AppendU32LE(ftr, 0u);
        AppendU32LE(ftr, 1u);
        AppendU32LE(ftr, 12u);
        AppendU32LE(ftr, 0x124u); AppendU32LE(ftr, 4u); AppendU32LE(ftr, 4u);
        AppendU32LE(ftr, 0u); AppendU32LE(ftr, 0u); AppendU32LE(ftr, 5u);
        for (char c : std::string_view("a.xmb")) AppendU16LE(ftr, static_cast<uint16_t>(c));
        PutU32LE(barAom.data() + 0x114u, 1u);
        PutU32LE(barAom.data() + 0x11Cu, static_cast<uint32_t>(ftr.size()));
        PutU32LE(barAom.data() + 0x120u, ftrOff);
        barAom.insert(barAom.end(), ftr.begin(), ftr.end());

        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"aom.bar", barAom), a2);
        tc.Record(ok2 && a2.valid && a2.entries[0].pathInArchive == L"a.xmb" &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.EnsembleBAR_AomV1AndDistinctHash");

        barAom[8] = 0x99u;
        AssertArcRejected(tc, L"Stage15.Archives.EnsembleBAR_RejectInvalidEndianTag", writeArcBytes(L"bad.bar", barAom));
    }

    // 1.16 Ensemble Studios Genie DRS Archive (.drs)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".drs"),
                  L"Stage15.Archives.EnsembleDRS_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"aoe2_interfac.drs");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 1 && a1.entries[0].pathInArchive == L"bina/50100.bina",
                  L"Stage15.Archives.EnsembleDRS_ReversedTagTocParsed");

        std::vector<uint8_t> outB;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"bina/50100.bina", outB);
        const char* exp1 = "// AoE2 Palette & UI Table\n255 0 0\n0 255 0\n";
        tc.Record(ex1 && BytesEqualStr(outB, exp1),
                  L"Stage15.Archives.EnsembleDRS_ExtractExact");

        std::vector<uint8_t> drsSounds;
        PathUtils::ReadFileBytes(p1, 1u << 20, drsSounds);
        if (drsSounds.size() >= 80u) {
            std::memcpy(drsSounds.data() + 64u, " vaw", 4);
        }
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"sounds.drs", drsSounds), a2);
        tc.Record(ok2 && a2.valid && a2.entries[0].pathInArchive == L"wav/50100.wav" &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.EnsembleDRS_WavTableAndDistinctHash");

        if (drsSounds.size() > 40u) drsSounds[40] = '9';
        AssertArcRejected(tc, L"Stage15.Archives.EnsembleDRS_RejectInvalidVersion", writeArcBytes(L"bad.drs", drsSounds));
    }

    // 1.17 Heroes of Might and Magic III "LOD\0" Archive (.lod)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".lod"),
                  L"Stage15.Archives.Heroes3LOD_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"h3bitmap_core.lod");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 2,
                  L"Stage15.Archives.Heroes3LOD_MixedZlibAndRawTocParsed");

        std::vector<uint8_t> zOut, rawOut;
        const bool exZ = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"crtraits.txt", zOut);
        const bool exR = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"heroes3.cfg", rawOut);
        const char* expZ = "Name\tAttack\tDefense\nArchangel\t30\t30\n";
        const char* expR = "SoundVolume=100\n";
        tc.Record(exZ && BytesEqualStr(zOut, expZ) && exR && BytesEqualStr(rawOut, expR),
                  L"Stage15.Archives.Heroes3LOD_ZlibInflateAndRawExtractExact");

        std::vector<uint8_t> lodSingle(0x5Cu, 0u);
        std::memcpy(lodSingle.data(), "LOD\0", 4);
        lodSingle[8] = 1u;
        AppendFixedAscii(lodSingle, "map.h3m", 16u);
        AppendU32LE(lodSingle, 0x5Cu + 32u);
        AppendU32LE(lodSingle, 4u);
        AppendU32LE(lodSingle, 1u);
        AppendU32LE(lodSingle, 0u);
        lodSingle.insert(lodSingle.end(), { 'H','3','M','!' });
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"single.lod", lodSingle), a2);
        tc.Record(ok2 && a2.valid && HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.Heroes3LOD_SingleEntryAndDistinctHash");

        lodSingle[0x5Cu + 16u] = 0xFFu;
        AssertArcRejected(tc, L"Stage15.Archives.Heroes3LOD_RejectOobOffset", writeArcBytes(L"bad.lod", lodSingle));
    }

    // 1.18 Activision "ZFSF" Archive (.zfs)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".zfs"),
                  L"Stage15.Archives.ActivisionZFS_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"i76_scripts.zfs");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 1 && a1.entries[0].pathInArchive == L"misn01.msn",
                  L"Stage15.Archives.ActivisionZFS_TocParsed");

        std::vector<uint8_t> outB;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"misn01.msn", outB);
        const char* exp1 = "[Mission01]\nObjective = Escort Convoy\n";
        tc.Record(ex1 && BytesEqualStr(outB, exp1),
                  L"Stage15.Archives.ActivisionZFS_XorDecryptAndExtractExact");

        std::vector<uint8_t> zfsChained = { 'Z','F','S','F' };
        AppendU32LE(zfsChained, 1u); AppendU32LE(zfsChained, 0u); AppendU32LE(zfsChained, 1u);
        AppendU32LE(zfsChained, 2u);
        AppendU32LE(zfsChained, 0u);
        AppendU32LE(zfsChained, 28u);
        AppendU32LE(zfsChained, 68u);
        AppendFixedAscii(zfsChained, "a.odf", 16u);
        AppendU32LE(zfsChained, 108u); AppendU32LE(zfsChained, 1u); AppendU32LE(zfsChained, 4u); AppendU32LE(zfsChained, 0u); AppendU32LE(zfsChained, 0u);
        AppendU32LE(zfsChained, 0u);
        AppendFixedAscii(zfsChained, "b.odf", 16u);
        AppendU32LE(zfsChained, 112u); AppendU32LE(zfsChained, 2u); AppendU32LE(zfsChained, 4u); AppendU32LE(zfsChained, 0u); AppendU32LE(zfsChained, 0u);
        zfsChained.insert(zfsChained.end(), { 'T','A','N','K', 'H','O','V','R' });

        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"bz_chained.zfs", zfsChained), a2);
        tc.Record(ok2 && a2.valid && a2.entries.size() == 2 &&
                  HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.ActivisionZFS_ChainedPagesAndDistinctHash");

        PutU32LE(zfsChained.data() + 28u, 28u);
        AssertArcRejected(tc, L"Stage15.Archives.ActivisionZFS_RejectDirectoryCycleBomb", writeArcBytes(L"cycle.zfs", zfsChained));
    }

    // 1.19 LucasArts SCUMM "LB83" Bundle (.bun)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".bun"),
                  L"Stage15.Archives.LucasBUN_ExtensionRegistered");

        const std::wstring p1 = JoinFsPath(sampleArcDir, L"comi_vox_bundle.bun");
        ArchiveSummaryInfo a1;
        const bool ok1 = ArchiveReaders::InspectArchiveFile(p1, a1);
        tc.Record(ok1 && a1.valid && a1.entries.size() == 1 && a1.entries[0].pathInArchive == L"plunder_dlg.txt",
                  L"Stage15.Archives.LucasBUN_Lb83TocParsed");

        std::vector<uint8_t> outB;
        const bool ex1 = ArchiveReaders::ExtractSingleEntryToMemory(p1, L"plunder_dlg.txt", outB);
        const char* exp1 = "GUYBRUSH: Look behind you, a three-headed monkey!\n";
        tc.Record(ex1 && BytesEqualStr(outB, exp1),
                  L"Stage15.Archives.LucasBUN_ExtractExact");

        std::vector<uint8_t> bunMus = { 'L','B','8','3' };
        AppendU32BE(bunMus, 16u);
        AppendU32BE(bunMus, 1u);
        bunMus.insert(bunMus.end(), { 'I','M','U','S' });
        AppendFixedAscii(bunMus, "banjo.imu", 24u);
        AppendU32BE(bunMus, 12u);
        AppendU32BE(bunMus, 4u);
        ArchiveSummaryInfo a2;
        const bool ok2 = ArchiveReaders::InspectArchiveFile(writeArcBytes(L"mus.bun", bunMus), a2);
        tc.Record(ok2 && a2.valid && HashArchiveToc(a1) != HashArchiveToc(a2),
                  L"Stage15.Archives.LucasBUN_VariantAndDistinctHash");

        bunMus[8] = 0x7Fu;
        AssertArcRejected(tc, L"Stage15.Archives.LucasBUN_RejectTruncatedDir", writeArcBytes(L"bad.bun", bunMus));
    }

    // ========================================================================
    // Section 2: Upgraded Container & Filesystem Codec Verification
    // ========================================================================

    // 2.1 ZIP64 Multicodec (.zip: 0x0001 Extra Fields + EOCD64 + BZip2 12, LZMA 14, Deflate 8, Stored 0, Zstd 93, XZ 95)
    {
        const std::wstring z64SamplePath = JoinFsPath(sampleArcDir, L"zip64_multicodec.zip");
        ArchiveSummaryInfo zInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(z64SamplePath, zInfo);

        std::string expBz, expLz, expDf;
        for (int i = 0; i < 10; ++i) expBz += "ZIP64 BZip2 (Method 12) Payload Verified OK!\n";
        for (int i = 0; i < 10; ++i) expLz += "ZIP64 PKWARE LZMA (Method 14) Payload Verified OK!\n";
        for (int i = 0; i < 6; ++i)  expDf += "ZIP64 Deflate (Method 8) Payload Verified OK!\n";

        std::vector<uint8_t> outBz, outLz, outDf, outZero;
        const auto* eBz = FindEntryCaseInsensitive(zInfo, L"codecs/bzip2_file.txt");
        const auto* eLz = FindEntryCaseInsensitive(zInfo, L"codecs/lzma_file.txt");
        const auto* eDf = FindEntryCaseInsensitive(zInfo, L"codecs/deflate_file.txt");
        const auto* eZr = FindEntryCaseInsensitive(zInfo, L"empty_zero.txt");

        const bool okSample = okInspect && zInfo.valid && zInfo.entries.size() == 4 &&
                              eBz && eBz->compressionMethod == 12u && eBz->uncompressedSize == expBz.size() &&
                              eLz && eLz->compressionMethod == 14u && eLz->uncompressedSize == expLz.size() &&
                              eDf && eDf->compressionMethod == 8u  && eDf->uncompressedSize == expDf.size() &&
                              eZr && eZr->compressionMethod == 0u  && eZr->uncompressedSize == 0ULL &&
                              ArchiveReaders::ExtractSingleEntryToMemory(z64SamplePath, L"codecs/bzip2_file.txt", outBz) &&
                              BytesEqualStr(outBz, expBz) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(z64SamplePath, L"codecs/lzma_file.txt", outLz) &&
                              BytesEqualStr(outLz, expLz) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(z64SamplePath, L"codecs/deflate_file.txt", outDf) &&
                              BytesEqualStr(outDf, expDf) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(z64SamplePath, L"empty_zero.txt", outZero) &&
                              outZero.empty();
        tc.Record(okSample, L"Stage15.Archives.Zip64Multicodec.SampleBzip2LzmaDeflateZero");

        // Companion ZIP64 archive exercising Zstandard (method 93) & XZ (method 95)
        const std::string rawZst = "ZIP64_ZSTD93_OK! ZIP64_ZSTD93_OK! Method 93 Zstandard sequence frame verified!\n";
        const std::string rawXz  = "ZIP64 XZ (Method 95) LZMA2+CRC32 Stream Payload Verified! ZIP64 XZ (Method 95)!\n";
        const auto compZst = BuildZstdMethod93Payload(reinterpret_cast<const uint8_t*>(rawZst.data()), rawZst.size());
        const auto compXz  = BuildXzMethod95Payload(reinterpret_cast<const uint8_t*>(rawXz.data()), rawXz.size());

        const auto z64ZstXz = BuildZip64Archive({
            { "codecs/zstd_file.txt", 93u, rawZst, compZst },
            { "codecs/xz_file.txt",   95u, rawXz,  compXz  }
        });
        const std::wstring z64ZstXzPath = writeArcBytes(L"zip64_zstd_xz.zip", z64ZstXz);

        ArchiveSummaryInfo z2Info;
        std::vector<uint8_t> outZst, outXz;
        const bool okZstXz = ArchiveReaders::InspectArchiveFile(z64ZstXzPath, z2Info) &&
                             z2Info.valid && z2Info.entries.size() == 2 &&
                             FindEntryCaseInsensitive(z2Info, L"codecs/zstd_file.txt") != nullptr &&
                             FindEntryCaseInsensitive(z2Info, L"codecs/zstd_file.txt")->compressionMethod == 93u &&
                             FindEntryCaseInsensitive(z2Info, L"codecs/xz_file.txt") != nullptr &&
                             FindEntryCaseInsensitive(z2Info, L"codecs/xz_file.txt")->compressionMethod == 95u &&
                             ArchiveReaders::ExtractSingleEntryToMemory(z64ZstXzPath, L"codecs/zstd_file.txt", outZst) &&
                             BytesEqualStr(outZst, rawZst) &&
                             ArchiveReaders::ExtractSingleEntryToMemory(z64ZstXzPath, L"codecs/xz_file.txt", outXz) &&
                             BytesEqualStr(outXz, rawXz);
        tc.Record(okZstXz, L"Stage15.Archives.Zip64Multicodec.Zstd93AndXz95Extract");

        // Negative: corrupt ZIP EOCD with invalid central directory & zero local headers
        std::vector<uint8_t> badZip(32u, 0xFFu);
        PutU32LE(badZip.data(), 0x06054B50u);
        AssertArcRejected(tc, L"Stage15.Archives.Zip64Multicodec.RejectCorruptZip", writeArcBytes(L"corrupt.zip", badZip));
    }

    // 2.2 RAR 5.0 Pure-C++ v5 LZ+Huffman + E8 & Delta Filter Extraction (.rar)
    {
        const std::wstring rar5Path = JoinFsPath(sampleArcDir, L"rar5_compressed_delta_e8.rar");
        ArchiveSummaryInfo r5Info;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(rar5Path, r5Info);

        static const uint8_t kX86Unit[] = {
            0x55,0x89,0xE5, 0xE8,0x20,0x00,0x00,0x00,
            0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
            0xE8,0x10,0x00,0x00,0x00, 0x31,0xC0,0x5D,0xC3
        };
        std::vector<uint8_t> expX86;
        for (int r = 0; r < 4; ++r) expX86.insert(expX86.end(), kX86Unit, kX86Unit + sizeof(kX86Unit));

        std::vector<uint8_t> expDelta;
        for (uint32_t k = 0; k < 64u; ++k) {
            AppendU16LE(expDelta, static_cast<uint16_t>((k * 3u) & 0xFFFFu));
            AppendU16LE(expDelta, static_cast<uint16_t>((k * 5u) & 0xFFFFu));
        }

        std::vector<uint8_t> outX86, outDelta;
        const bool okRar5 = okInspect && r5Info.valid && r5Info.totalFiles == 2 &&
                            FindEntryCaseInsensitive(r5Info, L"bin/x86_e8_stub.bin") != nullptr &&
                            FindEntryCaseInsensitive(r5Info, L"bin/x86_e8_stub.bin")->uncompressedSize == expX86.size() &&
                            FindEntryCaseInsensitive(r5Info, L"audio/stereo_delta.pcm") != nullptr &&
                            FindEntryCaseInsensitive(r5Info, L"audio/stereo_delta.pcm")->uncompressedSize == expDelta.size() &&
                            ArchiveReaders::ExtractSingleEntryToMemory(rar5Path, L"bin/x86_e8_stub.bin", outX86) &&
                            outX86 == expX86 &&
                            ArchiveReaders::ExtractSingleEntryToMemory(rar5Path, L"audio/stereo_delta.pcm", outDelta) &&
                            outDelta == expDelta;
        tc.Record(okRar5, L"Stage15.Archives.Rar5Compressed.LzHuffAndE8DeltaFiltersExtract");

        // Negative 1: RAR5 -hp Archive Encryption Header (Type=4) must be rejected cleanly
        std::vector<uint8_t> encRar5 = { 'R','a','r','!',0x1A,0x07,0x01,0x00, 0,0,0,0, 4, 4,0,0,0 };
        AssertArcRejected(tc, L"Stage15.Archives.Rar5Compressed.RejectEncryptedArchiveHeader",
                          writeArcBytes(L"hp_encrypted.rar", encRar5));

        // Negative 2: Corrupt compressed bitstream / CRC mismatch must fail in-memory extraction
        std::vector<uint8_t> rar5Corrupt;
        PathUtils::ReadFileBytes(rar5Path, 1u << 20, rar5Corrupt);
        if (const auto* e = FindEntryCaseInsensitive(r5Info, L"bin/x86_e8_stub.bin")) {
            if (e->rawDataOffset + 4u < rar5Corrupt.size()) {
                rar5Corrupt[static_cast<size_t>(e->rawDataOffset + 4u)] ^= 0xFFu;
            }
        }
        std::vector<uint8_t> badOut;
        const std::wstring badRar5Path = writeArcBytes(L"bad_crc.rar", rar5Corrupt);
        tc.Record(!ArchiveReaders::ExtractSingleEntryToMemory(badRar5Path, L"bin/x86_e8_stub.bin", badOut),
                  L"Stage15.Archives.Rar5Compressed.RejectCorruptPayloadCrc");
    }

    // 2.3 7-Zip Solid BCJ2 (0x0303011B) 4-Stream x86 Reassembler (.7z)
    {
        const std::wstring bcj2Path = JoinFsPath(sampleArcDir, L"win32_solid_bcj2.7z");
        ArchiveSummaryInfo s7Info;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(bcj2Path, s7Info);

        std::vector<uint8_t> expExeUnit = { 'M','Z',0x90,0x00 };
        expExeUnit.insert(expExeUnit.end(), 60u, 0x00u);
        const uint8_t codeTail[] = {
            0x55,0x89,0xE5,
            0xE8,0x40,0x00,0x00,0x00,
            0x85,0xC0,
            0x0F,0x85,0x24,0x00,0x00,0x00,
            0xE9,0x80,0x00,0x00,0x00,
            0xE8,0xEC,0xFF,0xFF,0xFF,
            0x31,0xC0,0x5D,0xC3
        };
        expExeUnit.insert(expExeUnit.end(), codeTail, codeTail + sizeof(codeTail));
        std::vector<uint8_t> expExe;
        for (int r = 0; r < 3; ++r) expExe.insert(expExe.end(), expExeUnit.begin(), expExeUnit.end());
        const std::string expDoc = "7-Zip Solid BCJ2 (0x0303011B) 4-Stream Extraction Verified!\n";

        std::vector<uint8_t> outExe, outDoc;
        const bool ok7zBcj2 = okInspect && s7Info.valid && s7Info.totalFiles == 2 &&
                              FindEntryCaseInsensitive(s7Info, L"bin/app_x86.exe") != nullptr &&
                              FindEntryCaseInsensitive(s7Info, L"bin/app_x86.exe")->uncompressedSize == expExe.size() &&
                              FindEntryCaseInsensitive(s7Info, L"docs/readme.txt") != nullptr &&
                              FindEntryCaseInsensitive(s7Info, L"docs/readme.txt")->uncompressedSize == expDoc.size() &&
                              ArchiveReaders::ExtractSingleEntryToMemory(bcj2Path, L"bin/app_x86.exe", outExe) &&
                              outExe == expExe &&
                              ArchiveReaders::ExtractSingleEntryToMemory(bcj2Path, L"docs/readme.txt", outDoc) &&
                              BytesEqualStr(outDoc, expDoc);
        tc.Record(ok7zBcj2, L"Stage15.Archives.SevenZipBcj2.Solid4StreamInspectAndExtract");

        // Negative 1: Truncated NextHeaderOffset past EOF
        std::vector<uint8_t> bad7z = { '7','z',0xBC,0xAF,0x27,0x1C, 0,4, 0,0,0,0 };
        AppendU64LE(bad7z, 4096ULL);
        AppendU64LE(bad7z, 64ULL);
        AppendU32LE(bad7z, 0u);
        AssertArcRejected(tc, L"Stage15.Archives.SevenZipBcj2.RejectTruncatedHeader", writeArcBytes(L"bad_hdr.7z", bad7z));

        // Negative 2: Fake raw uncompressed bytes labeled as LZMA1 (0x030101) MUST NOT extract via removed raw fallback!
        const std::string fakeRaw = "THIS_IS_RAW_ASCII_NOT_LZMA1_RANGE_CODED_DATA!";
        std::vector<uint8_t> fakeHdr = {
            0x01u, 0x04u,
            0x06u, 0x00u, 0x01u, 0x09u, static_cast<uint8_t>(fakeRaw.size()), 0x00u,
            0x07u, 0x0Bu, 0x01u, 0x00u, 0x01u, 0x23u, 0x03u, 0x01u, 0x01u, 0x05u, 0x5Du, 0x00u, 0x00u, 0x01u, 0x00u,
            0x0Cu, static_cast<uint8_t>(fakeRaw.size()), 0x00u,
            0x00u,
            0x05u, 0x01u, 0x11u, 0x0Bu, 0x00u, 'a',0,'.',0,'t',0,'x',0,'t',0, 0,0, 0x00u,
            0x00u
        };
        std::vector<uint8_t> fakeLzma7z = { '7','z',0xBC,0xAF,0x27,0x1C, 0,4 };
        uint8_t stHdr[20] = {};
        PutU64LE(stHdr + 0, fakeRaw.size());
        PutU64LE(stHdr + 8, fakeHdr.size());
        PutU32LE(stHdr + 16, Checksum::Crc32(fakeHdr.data(), fakeHdr.size()));
        AppendU32LE(fakeLzma7z, Checksum::Crc32(stHdr, 20));
        fakeLzma7z.insert(fakeLzma7z.end(), stHdr, stHdr + 20);
        AppendStr(fakeLzma7z, fakeRaw);
        fakeLzma7z.insert(fakeLzma7z.end(), fakeHdr.begin(), fakeHdr.end());

        const std::wstring fake7zPath = writeArcBytes(L"fake_raw_lzma.7z", fakeLzma7z);
        std::vector<uint8_t> fakeOut;
        const bool extFake = ArchiveReaders::ExtractSingleEntryToMemory(fake7zPath, L"a.txt", fakeOut);
        tc.Record(!extFake || !BytesEqualStr(fakeOut, fakeRaw),
                  L"Stage15.Archives.SevenZipBcj2.RejectFakeRawBytesLabeledAsLzma1");
    }

    // 2.4 Apple UDIF DMG with LZFSE bvx2 (0x80000007) Block Runs (.dmg)
    {
        const std::wstring dmgPath = JoinFsPath(sampleArcDir, L"macos_installer_bvx2.dmg");
        ArchiveSummaryInfo dmgInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(dmgPath, dmgInfo);

        const std::string expPlist =
            "<?xml version=\"1.0\"?><plist><dict><key>CFBundleName</key><string>Explorer2</string></dict></plist>\n";
        std::string expReadme;
        for (int i = 0; i < 8; ++i) expReadme += "Apple UDIF DMG LZFSE (0x80000007 bvx2) Decompression Verified!\n";

        std::vector<uint8_t> outPlist, outReadme;
        const bool okDmg = okInspect && dmgInfo.valid &&
                           FindEntryCaseInsensitive(dmgInfo, L"Applications") != nullptr &&
                           FindEntryCaseInsensitive(dmgInfo, L"Applications")->isDirectory &&
                           FindEntryCaseInsensitive(dmgInfo, L"Applications/AppInfo.plist") != nullptr &&
                           FindEntryCaseInsensitive(dmgInfo, L"README.txt") != nullptr &&
                           ArchiveReaders::ExtractSingleEntryToMemory(dmgPath, L"Applications/AppInfo.plist", outPlist) &&
                           BytesEqualStr(outPlist, expPlist) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(dmgPath, L"README.txt", outReadme) &&
                           BytesEqualStr(outReadme, expReadme);
        tc.Record(okDmg, L"Stage15.Archives.AppleDmgLzfse.Bvx2RunsInspectAndExtract");

        // Negative: Corrupt the first LZFSE "bvx2" block magic inside data_fork -> MUST reject (never zero-fill!)
        std::vector<uint8_t> badDmg;
        PathUtils::ReadFileBytes(dmgPath, 4u << 20, badDmg);
        if (badDmg.size() >= 16u) {
            badDmg[0] = 'X'; badDmg[1] = 'X'; badDmg[2] = 'X'; badDmg[3] = 'X';
        }
        AssertArcRejected(tc, L"Stage15.Archives.AppleDmgLzfse.RejectCorruptCompressedChunkNoZeroFill",
                          writeArcBytes(L"corrupt_bvx2.dmg", badDmg));
    }

    // 2.5 NTFS Large Fragmented $MFT Runlist (> Record 2048) (.ntfs)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".ntfs"),
                  L"Stage15.Archives.NtfsLargeMft.ExtensionRegistered");

        const std::wstring ntfsPath = JoinFsPath(sampleArcDir, L"ntfs_large_mft.ntfs");
        ArchiveSummaryInfo ntfsInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(ntfsPath, ntfsInfo);

        std::string expDeep;
        for (int i = 0; i < 6; ++i) {
            expDeep += "NTFS Fragmented $MFT Runlist Record #2060 (> 2048) Extracted Successfully!\n";
        }
        std::vector<uint8_t> outDeep;
        const bool okNtfs = okInspect && ntfsInfo.valid &&
                            FindEntryCaseInsensitive(ntfsInfo, L"DeepRecords") != nullptr &&
                            FindEntryCaseInsensitive(ntfsInfo, L"DeepRecords")->isDirectory &&
                            FindEntryCaseInsensitive(ntfsInfo, L"DeepRecords/record_2060.txt") != nullptr &&
                            FindEntryCaseInsensitive(ntfsInfo, L"DeepRecords/record_2060.txt")->uncompressedSize == expDeep.size() &&
                            ArchiveReaders::ExtractSingleEntryToMemory(ntfsPath, L"DeepRecords/record_2060.txt", outDeep) &&
                            BytesEqualStr(outDeep, expDeep);
        tc.Record(okNtfs, L"Stage15.Archives.NtfsLargeMft.FragmentedRunlistRecord2060Extract");

        // Negative: NTFS boot sector with empty/zeroed MFT
        std::vector<uint8_t> badNtfs(8192u, 0u);
        std::memcpy(badNtfs.data() + 3u, "NTFS    ", 8);
        PutU16LE(badNtfs.data() + 11u, 512u);
        badNtfs[13] = 2u;
        PutU64LE(badNtfs.data() + 48u, 2ULL);
        badNtfs[64] = static_cast<uint8_t>(-10);
        badNtfs[510] = 0x55u; badNtfs[511] = 0xAAu;
        AssertArcRejected(tc, L"Stage15.Archives.NtfsLargeMft.RejectEmptyZeroedMft",
                          writeArcBytes(L"bad_mft.ntfs", badNtfs));
    }

    // 2.6 Raw FAT Superfloppy (.img — Sample VFAT/FAT16 + Synthetic FAT12 12-Bit Cluster Chain)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".img") &&
                  ArchiveReaders::IsArchiveExtension(L".ima") &&
                  ArchiveReaders::IsArchiveExtension(L".flp"),
                  L"Stage15.Archives.FatFloppyImg.ExtensionsRegistered");

        const std::wstring imgPath = JoinFsPath(sampleArcDir, L"dos_boot_floppy.img");
        ArchiveSummaryInfo imgInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(imgPath, imgInfo);

        const std::string expBat = "@ECHO OFF\r\nPATH C:\\DOS\r\nECHO FAT16_RAW_IMG_OK\r\n";
        std::string expNotes;
        for (int i = 0; i < 6; ++i) expNotes += "Raw FAT16 Superfloppy Image Inspection & Extraction Verified!\n";

        std::vector<uint8_t> outBat, outNotes;
        const bool okSample = okInspect && imgInfo.valid &&
                              FindEntryCaseInsensitive(imgInfo, L"System/AUTOEXEC.BAT") != nullptr &&
                              FindEntryCaseInsensitive(imgInfo, L"Docs/LongFileName_Notes.txt") != nullptr &&
                              ArchiveReaders::ExtractSingleEntryToMemory(imgPath, L"System/AUTOEXEC.BAT", outBat) &&
                              BytesEqualStr(outBat, expBat) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(imgPath, L"Docs/LongFileName_Notes.txt", outNotes) &&
                              BytesEqualStr(outNotes, expNotes);
        tc.Record(okSample, L"Stage15.Archives.FatFloppyImg.SampleVfatInspectAndExtract");

        // Also test true 12-bit FAT12 packed-nibble cluster chain (3 -> 5 -> 4)
        const std::string fat12Cfg = "FILES=30\r\nBUFFERS=20\r\n";
        std::string fat12Doc(1200u, 'F');
        fat12Doc.replace(0, 20, "FAT12_CLUS_3_START>>");
        fat12Doc.replace(512, 20, "FAT12_CLUS_5_MID>>>>");
        fat12Doc.replace(1024, 20, "FAT12_CLUS_4_FINAL>>");
        const auto fat12Bytes = BuildFat12FloppyImg(fat12Cfg, fat12Doc);
        const std::wstring fat12Path = writeArcBytes(L"dos_fat12.img", fat12Bytes);

        ArchiveSummaryInfo f12Info;
        std::vector<uint8_t> outCfg12, outDoc12;
        const bool okFat12 = ArchiveReaders::InspectArchiveFile(fat12Path, f12Info) &&
                             f12Info.valid && f12Info.totalFiles == 2 &&
                             ArchiveReaders::ExtractSingleEntryToMemory(fat12Path, L"CONFIG.SYS", outCfg12) &&
                             BytesEqualStr(outCfg12, fat12Cfg) &&
                             ArchiveReaders::ExtractSingleEntryToMemory(fat12Path, L"README.TXT", outDoc12) &&
                             BytesEqualStr(outDoc12, fat12Doc);
        tc.Record(okFat12, L"Stage15.Archives.FatFloppyImg.Fat12NibbleChainExtract");

        std::vector<uint8_t> badImg(4096u, 0u);
        badImg[0] = 0xEBu; badImg[1] = 0x3Cu; badImg[2] = 0x90u;
        badImg[510] = 0x55u; badImg[511] = 0xAAu; // spc=0, bps=0 -> invalid BPB
        AssertArcRejected(tc, L"Stage15.Archives.FatFloppyImg.RejectInvalidBootSector",
                          writeArcBytes(L"bad_boot.img", badImg));
    }

    // 2.7 Hybrid ISO 9660 Bridge + UDF 1.02 Optical Image (.iso Preferring UDF TOC)
    {
        const std::wstring hybPath = JoinFsPath(sampleArcDir, L"hybrid_udf_bridge.iso");
        ArchiveSummaryInfo hybInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(hybPath, hybInfo);

        const std::string expGuide   = "Hybrid ISO+UDF Disc: UDF 1.02 inspected instead of 1-file ISO9660 bridge!\n";
        const std::string expAutorun = "[autorun]\r\nopen=setup.exe\r\nlabel=UDF_Full_Filesystem\r\n";
        std::string expPayload;
        for (int i = 0; i < 40; ++i) expPayload += "UDF_PAYLOAD_STREAM_OK_";

        std::vector<uint8_t> outGuide, outAutorun, outPayload;
        const bool okHybrid = okInspect && hybInfo.valid &&
                              hybInfo.totalFiles == 4 &&
                              FindEntryCaseInsensitive(hybInfo, L"README.TXT") == nullptr && // ISO9660 bridge stub NOT chosen!
                              FindEntryCaseInsensitive(hybInfo, L"Setup") != nullptr &&
                              FindEntryCaseInsensitive(hybInfo, L"Setup")->isDirectory &&
                              FindEntryCaseInsensitive(hybInfo, L"udf_guide.txt") != nullptr &&
                              FindEntryCaseInsensitive(hybInfo, L"Setup/autorun.inf") != nullptr &&
                              FindEntryCaseInsensitive(hybInfo, L"Setup/payload.bin") != nullptr &&
                              ArchiveReaders::ExtractSingleEntryToMemory(hybPath, L"udf_guide.txt", outGuide) &&
                              BytesEqualStr(outGuide, expGuide) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(hybPath, L"Setup/autorun.inf", outAutorun) &&
                              BytesEqualStr(outAutorun, expAutorun) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(hybPath, L"Setup/payload.bin", outPayload) &&
                              BytesEqualStr(outPayload, expPayload);
        tc.Record(okHybrid, L"Stage15.Archives.HybridUdfIso.PreferUdfTocOverBridgeAndExtract");

        std::vector<uint8_t> badIso(0x8400u, 0u);
        badIso[0x8000] = 1u;
        std::memcpy(badIso.data() + 0x8001u, "CD001", 5); // truncated < 0x8800
        AssertArcRejected(tc, L"Stage15.Archives.HybridUdfIso.RejectTruncatedIso",
                          writeArcBytes(L"trunc.iso", badIso));
    }

    // ========================================================================
    // Section 3: Filesystem, Classic Archiver & Console/Steam Archive Formats
    // ========================================================================

    // 3.1 Microsoft exFAT Filesystem (.exfat — ExFatFilesystemImg.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".exfat"),
                  L"Stage15.Archives.ExFatFs.ExtensionRegistered");

        const std::wstring exfatPath = JoinFsPath(sampleArcDir, L"usb_media_volume.exfat");
        ArchiveSummaryInfo exInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(exfatPath, exInfo);

        std::string expSpec = "exFAT Specification Sample\n";
        for (int i = 0; i < 45; ++i) expSpec += "Contiguous NoFatChain=0x02 stream across clusters 6..7!\n";
        std::string expChained = "[ExFatBootConfig]\nMode=NonContiguousFatChain\n";
        for (int i = 0; i < 140; ++i) expChained += "ClusterHop=8->10->9_Verified_OK!\n";

        std::vector<uint8_t> outSpec, outChained;
        const bool okExfat = okInspect && exInfo.valid &&
                             exInfo.formatName == L"Microsoft exFAT Image (.exfat)" &&
                             FindEntryCaseInsensitive(exInfo, L"docs") != nullptr &&
                             FindEntryCaseInsensitive(exInfo, L"docs")->isDirectory &&
                             FindEntryCaseInsensitive(exInfo, L"docs/specification.txt") != nullptr &&
                             FindEntryCaseInsensitive(exInfo, L"docs/specification.txt")->uncompressedSize == expSpec.size() &&
                             FindEntryCaseInsensitive(exInfo, L"firmware_boot.ini") != nullptr &&
                             FindEntryCaseInsensitive(exInfo, L"firmware_boot.ini")->uncompressedSize == expChained.size() &&
                             ArchiveReaders::ExtractSingleEntryToMemory(exfatPath, L"docs/specification.txt", outSpec) &&
                             BytesEqualStr(outSpec, expSpec) &&
                             ArchiveReaders::ExtractSingleEntryToMemory(exfatPath, L"firmware_boot.ini", outChained) &&
                             BytesEqualStr(outChained, expChained);
        tc.Record(okExfat, L"Stage15.Archives.ExFatFs.NoFatChainAndClusterChainInspectExtract");

        std::vector<uint8_t> badExfat;
        PathUtils::ReadFileBytes(exfatPath, 1u << 20, badExfat);
        if (badExfat.size() >= 12u * 512u) {
            badExfat[11u * 512u] ^= 0xFFu; // Corrupt 11-sector BootChecksum in Sector 11
        }
        AssertArcRejected(tc, L"Stage15.Archives.ExFatFs.RejectCorruptBootChecksum",
                          writeArcBytes(L"bad_csum.exfat", badExfat));

        std::vector<uint8_t> badMbz;
        PathUtils::ReadFileBytes(exfatPath, 1u << 20, badMbz);
        if (badMbz.size() >= 64u) badMbz[16] = 0x01u; // Non-zero MustBeZero byte
        AssertArcRejected(tc, L"Stage15.Archives.ExFatFs.RejectNonZeroMustBeZero",
                          writeArcBytes(L"bad_mbz.exfat", badMbz));
    }

    // 3.2 Linux ROMFS v1 (.romfs — RomFsFilesystem.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".romfs"),
                  L"Stage15.Archives.LinuxRomFs.ExtensionRegistered");

        const std::wstring romfsPath = JoinFsPath(sampleArcDir, L"initrd_boot.romfs");
        ArchiveSummaryInfo romInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(romfsPath, romInfo);

        const std::string expHost = "explorer2-romfs-node\n";
        const std::string expInit = "#!/bin/sh\necho ROMFS_BOOT_VERIFIED_OK\n";

        std::vector<uint8_t> outHost, outInit, outAlias;
        const bool okRomFs = okInspect && romInfo.valid &&
                             romInfo.formatName == L"Linux ROMFS v1 (INITRD_V1)" &&
                             FindEntryCaseInsensitive(romInfo, L"etc") != nullptr &&
                             FindEntryCaseInsensitive(romInfo, L"etc")->isDirectory &&
                             FindEntryCaseInsensitive(romInfo, L"bin") != nullptr &&
                             FindEntryCaseInsensitive(romInfo, L"bin")->isDirectory &&
                             FindEntryCaseInsensitive(romInfo, L"etc/hostname") != nullptr &&
                             FindEntryCaseInsensitive(romInfo, L"bin/init.sh") != nullptr &&
                             FindEntryCaseInsensitive(romInfo, L"bin/sh_alias") != nullptr &&
                             ArchiveReaders::ExtractSingleEntryToMemory(romfsPath, L"etc/hostname", outHost) &&
                             BytesEqualStr(outHost, expHost) &&
                             ArchiveReaders::ExtractSingleEntryToMemory(romfsPath, L"bin/init.sh", outInit) &&
                             BytesEqualStr(outInit, expInit) &&
                             ArchiveReaders::ExtractSingleEntryToMemory(romfsPath, L"bin/sh_alias", outAlias) &&
                             BytesEqualStr(outAlias, expInit);
        tc.Record(okRomFs, L"Stage15.Archives.LinuxRomFs.TreeHardlinkAndExtractExact");

        std::vector<uint8_t> badRom;
        PathUtils::ReadFileBytes(romfsPath, 65536u, badRom);
        if (badRom.size() >= 16u) badRom[12] ^= 0x01u; // Break 512-byte superblock checksum
        AssertArcRejected(tc, L"Stage15.Archives.LinuxRomFs.RejectBadSuperblockChecksum",
                          writeArcBytes(L"bad_csum.romfs", badRom));

        std::vector<uint8_t> truncRom;
        PathUtils::ReadFileBytes(romfsPath, 65536u, truncRom);
        if (truncRom.size() >= 16u) PutU32BE(truncRom.data() + 8u, 99999u); // fullSize > size
        AssertArcRejected(tc, L"Stage15.Archives.LinuxRomFs.RejectTruncatedFullSize",
                          writeArcBytes(L"trunc.romfs", truncRom));
    }

    // 3.3 Linux CramFS (.cramfs — CramFsFilesystem.h, LE & BE)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".cramfs"),
                  L"Stage15.Archives.LinuxCramFs.ExtensionRegistered");

        const std::wstring cramPath = JoinFsPath(sampleArcDir, L"embedded_rootfs.cramfs");
        ArchiveSummaryInfo cramInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(cramPath, cramInfo);

        const std::string expConf = "hostname=cramfs-router\nwatchdog=enabled\n";
        std::string expReadme;
        for (int i = 0; i < 110; ++i) expReadme += "CramFS Multi-Page Zlib Verification Line 0123456789ABCDEF\n";

        std::vector<uint8_t> outConf, outReadme;
        const bool okCramLe = okInspect && cramInfo.valid &&
                              cramInfo.formatName == L"Linux CramFS (LE) (CRAMFS_ROOT)" &&
                              FindEntryCaseInsensitive(cramInfo, L"etc") != nullptr &&
                              FindEntryCaseInsensitive(cramInfo, L"etc")->isDirectory &&
                              FindEntryCaseInsensitive(cramInfo, L"etc/system.conf") != nullptr &&
                              FindEntryCaseInsensitive(cramInfo, L"readme_multi.txt") != nullptr &&
                              FindEntryCaseInsensitive(cramInfo, L"readme_multi.txt")->uncompressedSize == expReadme.size() &&
                              ArchiveReaders::ExtractSingleEntryToMemory(cramPath, L"etc/system.conf", outConf) &&
                              BytesEqualStr(outConf, expConf) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(cramPath, L"readme_multi.txt", outReadme) &&
                              BytesEqualStr(outReadme, expReadme);
        tc.Record(okCramLe, L"Stage15.Archives.LinuxCramFs.LittleEndianMultiPageZlibExtract");

        // Big-Endian CramFS variant verifying BE bitfield inode decoding
        auto packInodeBe = [](uint16_t mode, uint32_t fSize, uint32_t nameBytes, uint32_t offBytes) {
            std::vector<uint8_t> b;
            AppendU32BE(b, static_cast<uint32_t>(mode) << 16u);
            AppendU32BE(b, (fSize & 0x00FFFFFFu) << 8u);
            AppendU32BE(b, (((nameBytes / 4u) & 0x3Fu) << 26u) | ((offBytes / 4u) & 0x03FFFFFFu));
            return b;
        };
        const std::string bePay = "Big-Endian PowerPC/MIPS CramFS Payload OK!\n";
        const auto zBePage = WrapZlibStored(std::vector<uint8_t>(bePay.begin(), bePay.end()));
        // Layout: 80B superblock + 20B root dir (12B inode + 8B "boot.sh\0") + 4B ptr + zBePage
        const uint32_t rootOff = 80u;
        const uint32_t rootLen = 20u;
        const uint32_t fileOff = 100u;
        const uint32_t pageEnd = fileOff + 4u + static_cast<uint32_t>(zBePage.size());

        std::vector<uint8_t> cramBe(80u, 0u);
        PutU32BE(cramBe.data() + 0u, 0x28CD3D45u);
        PutU32BE(cramBe.data() + 8u, 0x00000001u);
        std::memcpy(cramBe.data() + 16u, "Compressed ROMFS", 16);
        std::memcpy(cramBe.data() + 48u, "BE_ROUTER", 9);
        const auto rootBe = packInodeBe(0040755u, rootLen, 0u, rootOff);
        std::memcpy(cramBe.data() + 64u, rootBe.data(), 12);

        const auto childBe = packInodeBe(0100755u, static_cast<uint32_t>(bePay.size()), 8u, fileOff);
        cramBe.insert(cramBe.end(), childBe.begin(), childBe.end());
        AppendFixedAscii(cramBe, "boot.sh", 8u);
        AppendU32BE(cramBe, pageEnd);
        cramBe.insert(cramBe.end(), zBePage.begin(), zBePage.end());
        while ((cramBe.size() & 3u) != 0u) cramBe.push_back(0u);
        PutU32BE(cramBe.data() + 4u, static_cast<uint32_t>(cramBe.size()));

        const std::wstring cramBePath = writeArcBytes(L"mips_be.cramfs", cramBe);
        ArchiveSummaryInfo cramBeInfo;
        std::vector<uint8_t> outBe;
        const bool okCramBe = ArchiveReaders::InspectArchiveFile(cramBePath, cramBeInfo) &&
                              cramBeInfo.valid && cramBeInfo.formatName == L"Linux CramFS (BE) (BE_ROUTER)" &&
                              ArchiveReaders::ExtractSingleEntryToMemory(cramBePath, L"boot.sh", outBe) &&
                              BytesEqualStr(outBe, bePay) &&
                              HashArchiveToc(cramInfo) != HashArchiveToc(cramBeInfo);
        tc.Record(okCramBe, L"Stage15.Archives.LinuxCramFs.BigEndianBitfieldAndDistinctHash");

        std::vector<uint8_t> badCram = cramBe;
        PutU32BE(badCram.data() + 4u, 999999u); // fsSize > actual size with VERSION_2 flag
        AssertArcRejected(tc, L"Stage15.Archives.LinuxCramFs.RejectOversizeSuperblock",
                          writeArcBytes(L"bad_sz.cramfs", badCram));
    }

    // 3.4 ESTsoft ALZip (.alz — AlzipAlzArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".alz"),
                  L"Stage15.Archives.AlzipAlz.ExtensionRegistered");

        const std::wstring alzPath = JoinFsPath(sampleArcDir, L"estsoft_bundle.alz");
        ArchiveSummaryInfo alzInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(alzPath, alzInfo);

        const std::string expStore = "ALZip Stored Entry OK\n";
        std::string expBz2, expDef;
        for (int i = 0; i < 12; ++i) expBz2 += "ALZip BZip2 Compressed Stream Payload! ";
        for (int i = 0; i < 16; ++i) expDef += "int alz_deflate_verify(void) { return 42; }\n";

        std::vector<uint8_t> outStore, outBz2, outDef;
        const bool okAlz = okInspect && alzInfo.valid &&
                           alzInfo.formatName == L"ESTsoft ALZip Archive (.alz)" &&
                           alzInfo.totalFiles == 3 &&
                           ArchiveReaders::ExtractSingleEntryToMemory(alzPath, L"manifest.txt", outStore) &&
                           BytesEqualStr(outStore, expStore) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(alzPath, L"notes/bzip2_doc.txt", outBz2) &&
                           BytesEqualStr(outBz2, expBz2) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(alzPath, L"src/deflate_code.c", outDef) &&
                           BytesEqualStr(outDef, expDef);
        tc.Record(okAlz, L"Stage15.Archives.AlzipAlz.StoreBzip2DeflateInspectAndExtract");

        // Negative 1: Truncated BLZ\x01 payload
        std::vector<uint8_t> badAlz = {
            'A','L','Z',0x01, 10,0, 0,0,
            'B','L','Z',0x01, 5,0, 0x20, 0,0,0,0, 0x40, 0,
            0,0, 0,0,0,0, 99,0,0,0, 99,0,0,0, 'a','.','t','x','t', 'X'
        };
        AssertArcRejected(tc, L"Stage15.Archives.AlzipAlz.RejectTruncatedEntry",
                          writeArcBytes(L"trunc.alz", badAlz));

        // Negative 2: Corrupt stored CRC32 on manifest.txt must fail extraction
        std::vector<uint8_t> badCrcAlz;
        PathUtils::ReadFileBytes(alzPath, 65536u, badCrcAlz);
        if (badCrcAlz.size() > 26u) badCrcAlz[23] ^= 0xFFu; // Flip byte inside first BLZ\x01 fileCrc
        std::vector<uint8_t> dummy;
        const std::wstring badCrcPath = writeArcBytes(L"bad_crc.alz", badCrcAlz);
        tc.Record(!ArchiveReaders::ExtractSingleEntryToMemory(badCrcPath, L"manifest.txt", dummy),
                  L"Stage15.Archives.AlzipAlz.RejectCorruptEntryCrc32");
    }

    // 3.5 Rahul Dhesi ZOO 2.x (.zoo — ZooArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".zoo"),
                  L"Stage15.Archives.ZooArchive.ExtensionRegistered");

        const std::wstring zooPath = JoinFsPath(sampleArcDir, L"vintage_backup.zoo");
        ArchiveSummaryInfo zooInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(zooPath, zooInfo);

        const std::string expStore = "Rahul Dhesi ZOO 2.10 Stored File OK!\n";
        std::string expLzw, expLh5;
        for (int i = 0; i < 14; ++i) expLzw += "ZOO 13-bit LZW Compression Test Line! ";
        for (int i = 0; i < 16; ++i) expLh5 += "/* ZOO LH5 Huffman+LZ77 */ int zoo_ok = 1;\n";

        std::vector<uint8_t> outStore, outLzw, outLh5;
        const bool okZoo = okInspect && zooInfo.valid &&
                           zooInfo.formatName == L"ZOO Archive v2.1 (.zoo)" &&
                           zooInfo.totalFiles == 3 &&
                           ArchiveReaders::ExtractSingleEntryToMemory(zooPath, L"readme.txt", outStore) &&
                           BytesEqualStr(outStore, expStore) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(zooPath, L"docs/manual_lzw.txt", outLzw) &&
                           BytesEqualStr(outLzw, expLzw) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(zooPath, L"src/core/engine_lh5.c", outLh5) &&
                           BytesEqualStr(outLh5, expLh5);
        tc.Record(okZoo, L"Stage15.Archives.ZooArchive.StoreLzw13AndLh5InspectAndExtract");

        // Negative 1: Broken zoo_start + zoo_minus != 0 invariant
        std::vector<uint8_t> badZooMinus;
        PathUtils::ReadFileBytes(zooPath, 65536u, badZooMinus);
        if (badZooMinus.size() > 32u) badZooMinus[28] ^= 0x01u;
        AssertArcRejected(tc, L"Stage15.Archives.ZooArchive.RejectInvalidMinusHeader",
                          writeArcBytes(L"bad_minus.zoo", badZooMinus));

        // Negative 2: Direntry nextOff self-cycle at offset 42
        std::vector<uint8_t> cycZoo;
        PathUtils::ReadFileBytes(zooPath, 65536u, cycZoo);
        if (cycZoo.size() > 52u) PutU32LE(cycZoo.data() + 42u + 6u, 42u);
        AssertArcRejected(tc, L"Stage15.Archives.ZooArchive.RejectDirentryCycle",
                          writeArcBytes(L"cycle.zoo", cycZoo));
    }

    // 3.6 SEA ARC / PKPAK (.arc — SeaArcArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".arc"),
                  L"Stage15.Archives.SeaArc.ExtensionRegistered");

        const std::wstring arcPath = JoinFsPath(sampleArcDir, L"bbs_distro.arc");
        ArchiveSummaryInfo arcInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(arcPath, arcInfo);

        const std::string expDoc    = "SEA ARC 5.21 Unpacked README Document.\r\n";
        const std::string expBanner = std::string(40, '=') + "\r\n  WELCOME TO EXPLORER2 BBS!  \r\n" + std::string(40, '=') + "\r\n";
        std::string expSqz, expCrn, expSqs;
        for (int i = 0; i < 6; ++i)  expSqz += "Richard Greenlaw USQ Huffman + RLE90 Squeeze Verified! ";
        for (int i = 0; i < 12; ++i) expCrn += "SEA ARC Method 8 Dynamic 12-bit LZW + RLE90 Crunch Verified! ";
        for (int i = 0; i < 12; ++i) expSqs += "PKPAK Method 9 13-bit Squash LZW Stream Verified! ";

        std::vector<uint8_t> o2, o3, o4, o8, o9;
        const bool okArc = okInspect && arcInfo.valid &&
                           arcInfo.formatName == L"SEA ARC / PKPAK Archive (.arc)" &&
                           arcInfo.totalFiles == 5 &&
                           ArchiveReaders::ExtractSingleEntryToMemory(arcPath, L"README.DOC", o2) && BytesEqualStr(o2, expDoc) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(arcPath, L"BANNER.TXT", o3) && BytesEqualStr(o3, expBanner) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(arcPath, L"SQUEEZE.TXT", o4) && BytesEqualStr(o4, expSqz) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(arcPath, L"CRUNCH.TXT", o8) && BytesEqualStr(o8, expCrn) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(arcPath, L"SQUASH.TXT", o9) && BytesEqualStr(o9, expSqs);
        tc.Record(okArc, L"Stage15.Archives.SeaArc.StoreRleSqueezeCrunchSquashExtract");

        std::vector<uint8_t> badArc = { 0x1Au, 0x08u };
        AppendFixedAscii(badArc, "BAD.TXT", 13u);
        AppendU32LE(badArc, 500u); // compSz > remaining bytes
        AppendU16LE(badArc, 0u); AppendU16LE(badArc, 0u); AppendU16LE(badArc, 0u);
        AppendU32LE(badArc, 1000u);
        AssertArcRejected(tc, L"Stage15.Archives.SeaArc.RejectTruncatedMember",
                          writeArcBytes(L"trunc.arc", badArc));
    }

    // 3.7 Aladdin StuffIt Classic (.sit — StuffItSitArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".sit"),
                  L"Stage15.Archives.StuffItSit.ExtensionRegistered");

        const std::wstring sitPath = JoinFsPath(sampleArcDir, L"mac_classic_folder.sit");
        ArchiveSummaryInfo sitInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(sitPath, sitInfo);

        const std::string expBanner = std::string(32, '*') + "\rClassic Mac RLE90 StuffIt Fork!\r" + std::string(32, '*');
        std::string expLzw, expHuff, expDef;
        for (int i = 0; i < 12; ++i) expLzw  += "StuffIt Classic 14-bit LZW Data Fork Payload! ";
        for (int i = 0; i < 8; ++i)  expHuff += "StuffIt Prefix-Tree MSB Huffman Method 3 Verified! ";
        for (int i = 0; i < 10; ++i) expDef  += "StuffIt Deluxe Method 14 Raw Deflate Stream Verified! ";
        const std::string expAbout = "StuffIt Root Stored File OK!\n";

        std::vector<uint8_t> oBan, oLzw, oHuf, oDef, oAbt;
        const bool okSit = okInspect && sitInfo.valid &&
                           sitInfo.formatName == L"Aladdin StuffIt Classic Archive (.sit)" &&
                           FindEntryCaseInsensitive(sitInfo, L"MacApp") != nullptr &&
                           FindEntryCaseInsensitive(sitInfo, L"MacApp")->isDirectory &&
                           sitInfo.totalFiles == 5 &&
                           ArchiveReaders::ExtractSingleEntryToMemory(sitPath, L"MacApp/Banner.txt", oBan) && BytesEqualStr(oBan, expBanner) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(sitPath, L"MacApp/Manual.txt", oLzw) && BytesEqualStr(oLzw, expLzw) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(sitPath, L"MacApp/Huffman.txt", oHuf) && BytesEqualStr(oHuf, expHuff) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(sitPath, L"MacApp/Deflate.txt", oDef) && BytesEqualStr(oDef, expDef) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(sitPath, L"About.txt", oAbt) && BytesEqualStr(oAbt, expAbout);
        tc.Record(okSit, L"Stage15.Archives.StuffItSit.FolderRleLzw14HuffmanDeflateStoreExtract");

        // Second variant: 128-byte MacBinary II wrapped "ST50" + "rLau" archive
        const std::string mbPayload = "MacBinary Wrapped ST50 StuffIt Data Fork!\r";
        std::vector<uint8_t> sitEntry(112u, 0u);
        sitEntry[0] = 0u;
        sitEntry[1] = 0u; // Stored
        sitEntry[2] = 9u;
        std::memcpy(sitEntry.data() + 3u, "ReadMe.hq", 9);
        PutU32BE(sitEntry.data() + 88u, static_cast<uint32_t>(mbPayload.size()));
        PutU32BE(sitEntry.data() + 96u, static_cast<uint32_t>(mbPayload.size()));
        PutU16BE(sitEntry.data() + 102u, ComputeCrc16Arc(reinterpret_cast<const uint8_t*>(mbPayload.data()), mbPayload.size()));
        PutU16BE(sitEntry.data() + 110u, ComputeCrc16Arc(sitEntry.data(), 110u));

        std::vector<uint8_t> macBinSit(128u, 0u);
        macBinSit[0] = 0u;
        macBinSit[1] = 10u;
        std::memcpy(macBinSit.data() + 2u, "Archive.sit", 10);
        AppendStr(macBinSit, "ST50");
        AppendU16BE(macBinSit, 1u);
        AppendU32BE(macBinSit, static_cast<uint32_t>(22u + sitEntry.size() + mbPayload.size()));
        AppendStr(macBinSit, "rLau");
        macBinSit.push_back(1u);
        macBinSit.insert(macBinSit.end(), 7u, 0u);
        macBinSit.insert(macBinSit.end(), sitEntry.begin(), sitEntry.end());
        AppendStr(macBinSit, mbPayload);

        const std::wstring mbSitPath = writeArcBytes(L"macbinary_st50.sit", macBinSit);
        ArchiveSummaryInfo sit2Info;
        std::vector<uint8_t> mbOut;
        const bool okMbSit = ArchiveReaders::InspectArchiveFile(mbSitPath, sit2Info) &&
                             sit2Info.valid && sit2Info.totalFiles == 1 &&
                             ArchiveReaders::ExtractSingleEntryToMemory(mbSitPath, L"ReadMe.hq", mbOut) &&
                             BytesEqualStr(mbOut, mbPayload) &&
                             HashArchiveToc(sitInfo) != HashArchiveToc(sit2Info);
        tc.Record(okMbSit, L"Stage15.Archives.StuffItSit.MacBinaryWrappedSt50AndDistinctHash");

        // Negative: Corrupt 110-byte entry header CRC16 on first root entry
        std::vector<uint8_t> badSit = macBinSit;
        badSit[128u + 22u + 110u] ^= 0xFFu;
        AssertArcRejected(tc, L"Stage15.Archives.StuffItSit.RejectCorruptHeaderCrc16",
                          writeArcBytes(L"bad_hdr_crc.sit", badSit));
    }

    // 3.8 Nintendo SARC & Yaz0-Compressed SZS (.sarc / .szs — NintendoSarcArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".sarc") &&
                  ArchiveReaders::IsArchiveExtension(L".szs"),
                  L"Stage15.Archives.NintendoSarc.ExtensionsRegistered");

        const std::wstring sarcPath = JoinFsPath(sampleArcDir, L"switch_ui_pack.sarc");
        const std::wstring szsPath  = JoinFsPath(sampleArcDir, L"wiiu_course_pack.szs");

        ArchiveSummaryInfo sarcInfo, szsInfo;
        const bool okSarc = ArchiveReaders::InspectArchiveFile(sarcPath, sarcInfo);
        const bool okSzs  = ArchiveReaders::InspectArchiveFile(szsPath, szsInfo);

        std::string expFlyt, expFlan;
        for (int i = 0; i < 8; ++i) expFlyt += "FLYT_SWITCH_LE_PAYLOAD_";
        for (int i = 0; i < 8; ++i) expFlan += "FLAN_SWITCH_LE_ANIM_";

        std::vector<uint8_t> expKmp = { 'K','M','P','_','H','E','A','D','E','R','_','W','I','I','U','_','B','E','_' };
        for (int i = 0; i < 32; ++i) expKmp.insert(expKmp.end(), { 1u, 2u, 3u, 4u });
        const std::string expJson = "{\"track\":\"Mario_Circuit\",\"laps\":3,\"engine\":\"SARC_BE\"}\n";

        std::vector<uint8_t> outFlyt, outFlan, outKmp, outJson;
        const bool okSwitch = okSarc && sarcInfo.valid &&
                              sarcInfo.formatName == L"Nintendo SARC Archive (.sarc, LE)" &&
                              sarcInfo.totalFiles == 2 &&
                              ArchiveReaders::ExtractSingleEntryToMemory(sarcPath, L"ui/hud_layout.bflyt", outFlyt) &&
                              BytesEqualStr(outFlyt, expFlyt) &&
                              ArchiveReaders::ExtractSingleEntryToMemory(sarcPath, L"ui/hud_intro.bflan", outFlan) &&
                              BytesEqualStr(outFlan, expFlan);

        const bool okWiiU = okSzs && szsInfo.valid &&
                            szsInfo.formatName == L"Nintendo Yaz0-SARC Archive (.szs, BE)" &&
                            szsInfo.totalFiles == 2 &&
                            ArchiveReaders::ExtractSingleEntryToMemory(szsPath, L"course/course_map.bin", outKmp) &&
                            outKmp == expKmp &&
                            ArchiveReaders::ExtractSingleEntryToMemory(szsPath, L"course/course_info.json", outJson) &&
                            BytesEqualStr(outJson, expJson) &&
                            HashArchiveToc(sarcInfo) != HashArchiveToc(szsInfo);

        tc.Record(okSwitch && okWiiU, L"Stage15.Archives.NintendoSarc.SwitchLeAndWiiUYaz0BeInspectExtract");

        // Negative 1: SARC with invalid BOM (0x1234)
        std::vector<uint8_t> badSarc(64u, 0u);
        std::memcpy(badSarc.data(), "SARC", 4);
        badSarc[6] = 0x12u; badSarc[7] = 0x34u;
        AssertArcRejected(tc, L"Stage15.Archives.NintendoSarc.RejectInvalidBom",
                          writeArcBytes(L"bad_bom.sarc", badSarc));

        // Negative 2: Yaz0 with invalid immediate LZ77 back-reference before any output bytes
        std::vector<uint8_t> badYaz0(32u, 0u);
        std::memcpy(badYaz0.data(), "Yaz0", 4);
        PutU32BE(badYaz0.data() + 4u, 64u);
        badYaz0[16] = 0x00u; // bit 7 == 0 -> match at output pos 0!
        badYaz0[17] = 0x30u;
        badYaz0[18] = 0x05u; // dist = 6 > 0!
        AssertArcRejected(tc, L"Stage15.Archives.NintendoSarc.RejectCorruptYaz0Backref",
                          writeArcBytes(L"bad_ref.szs", badYaz0));
    }

    // 3.9 Nintendo DS Nitro Archive (.narc — NintendoNarcArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".narc"),
                  L"Stage15.Archives.NintendoNarc.ExtensionRegistered");

        const std::wstring narcPath = JoinFsPath(sampleArcDir, L"nds_pokemon_data.narc");
        ArchiveSummaryInfo narcInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(narcPath, narcInfo);

        static const uint8_t expStats[37] = {
            'N','D','S','_','S','P','E','C','I','E','S','_','B','A','S','E','_','S','T','A','T','S','_','R','E','C','O','R','D','_','0','0','1',
            0x00, 0x11, 0x22, 0x33
        };
        const std::string expIntro = "Welcome to NitroSDK NARC hierarchical FNT!\n";

        std::vector<uint8_t> outStats, outIntro;
        const bool okNarc = okInspect && narcInfo.valid &&
                            narcInfo.formatName == L"Nintendo DS Nitro Archive (.narc)" &&
                            FindEntryCaseInsensitive(narcInfo, L"data") != nullptr &&
                            FindEntryCaseInsensitive(narcInfo, L"data")->isDirectory &&
                            FindEntryCaseInsensitive(narcInfo, L"scripts") != nullptr &&
                            FindEntryCaseInsensitive(narcInfo, L"scripts")->isDirectory &&
                            ArchiveReaders::ExtractSingleEntryToMemory(narcPath, L"data/stats.bin", outStats) &&
                            outStats.size() == sizeof(expStats) &&
                            std::memcmp(outStats.data(), expStats, sizeof(expStats)) == 0 &&
                            ArchiveReaders::ExtractSingleEntryToMemory(narcPath, L"scripts/intro.txt", outIntro) &&
                            BytesEqualStr(outIntro, expIntro);
        tc.Record(okNarc, L"Stage15.Archives.NintendoNarc.HierarchicalFntInspectAndExtract");

        // Second variant: Nameless NARC (empty 8B root-only BTNF -> fallback file_0000.bin)
        const std::string namelessPay = "NARC_NAMELESS_BTAF_ONLY_PAYLOAD!";
        std::vector<uint8_t> narcNameless = { 'N','A','R','C' };
        AppendU16LE(narcNameless, 0xFFFEu);
        AppendU16LE(narcNameless, 0x0100u);
        AppendU32LE(narcNameless, 16u + 20u + 16u + 8u + static_cast<uint32_t>(namelessPay.size()));
        AppendU16LE(narcNameless, 16u);
        AppendU16LE(narcNameless, 3u);
        // BTAF (20B)
        AppendStr(narcNameless, "BTAF");
        AppendU32LE(narcNameless, 20u);
        AppendU16LE(narcNameless, 1u);
        AppendU16LE(narcNameless, 0u);
        AppendU32LE(narcNameless, 0u);
        AppendU32LE(narcNameless, static_cast<uint32_t>(namelessPay.size()));
        // BTNF (16B: 8B chunk hdr + 8B empty FNT header)
        AppendStr(narcNameless, "BTNF");
        AppendU32LE(narcNameless, 16u);
        AppendU32LE(narcNameless, 4u);
        AppendU16LE(narcNameless, 0u);
        AppendU16LE(narcNameless, 1u);
        // GMIF
        AppendStr(narcNameless, "GMIF");
        AppendU32LE(narcNameless, 8u + static_cast<uint32_t>(namelessPay.size()));
        AppendStr(narcNameless, namelessPay);

        const std::wstring nlPath = writeArcBytes(L"nameless.narc", narcNameless);
        ArchiveSummaryInfo nlInfo;
        std::vector<uint8_t> nlOut;
        const bool okNl = ArchiveReaders::InspectArchiveFile(nlPath, nlInfo) &&
                          nlInfo.valid && nlInfo.totalFiles == 1 &&
                          FindEntryCaseInsensitive(nlInfo, L"file_0000.bin") != nullptr &&
                          ArchiveReaders::ExtractSingleEntryToMemory(nlPath, L"file_0000.bin", nlOut) &&
                          BytesEqualStr(nlOut, namelessPay) &&
                          HashArchiveToc(narcInfo) != HashArchiveToc(nlInfo);
        tc.Record(okNl, L"Stage15.Archives.NintendoNarc.NamelessFallbackAndDistinctHash");

        // Negative: BTAF endOff exceeding GMIF image size
        std::vector<uint8_t> badNarc = narcNameless;
        PutU32LE(badNarc.data() + 16u + 16u, 99999u); // endOff = 99999 > imgAvail
        AssertArcRejected(tc, L"Stage15.Archives.NintendoNarc.RejectOutOfBoundsFatEntry",
                          writeArcBytes(L"bad_fat.narc", badNarc));
    }

    // 3.10 Valve Steam Legacy GCF v6 (.gcf — ValveGcfArchive.h)
    {
        tc.Record(ArchiveReaders::IsArchiveExtension(L".gcf"),
                  L"Stage15.Archives.ValveGcf.ExtensionRegistered");

        const std::wstring gcfPath = JoinFsPath(sampleArcDir, L"steam_hl2_cache.gcf");
        ArchiveSummaryInfo gcfInfo;
        const bool okInspect = ArchiveReaders::InspectArchiveFile(gcfPath, gcfInfo);

        const std::string expCfg = "unbindall\nbind \"w\" \"+forward\"\ncl_showfps \"1\"\n";
        const std::string expLarge =
            std::string("CLUSTER_PART_A_") + std::string(512 - 15, 'A') +
            std::string("CLUSTER_PART_B_") + std::string(512 - 15, 'B') +
            std::string("CLUSTER_PART_C_TAIL_OK!\n");

        std::vector<uint8_t> outCfg, outLarge;
        const bool okGcf = okInspect && gcfInfo.valid &&
                           gcfInfo.formatName == L"Valve Steam GCF v6 (.gcf)" &&
                           FindEntryCaseInsensitive(gcfInfo, L"hl2") != nullptr &&
                           FindEntryCaseInsensitive(gcfInfo, L"hl2")->isDirectory &&
                           FindEntryCaseInsensitive(gcfInfo, L"hl2/cfg/config.cfg") != nullptr &&
                           FindEntryCaseInsensitive(gcfInfo, L"hl2/materials/readme_large.txt") != nullptr &&
                           FindEntryCaseInsensitive(gcfInfo, L"hl2/materials/readme_large.txt")->uncompressedSize == expLarge.size() &&
                           ArchiveReaders::ExtractSingleEntryToMemory(gcfPath, L"hl2/cfg/config.cfg", outCfg) &&
                           BytesEqualStr(outCfg, expCfg) &&
                           ArchiveReaders::ExtractSingleEntryToMemory(gcfPath, L"hl2/materials/readme_large.txt", outLarge) &&
                           BytesEqualStr(outLarge, expLarge);
        tc.Record(okGcf, L"Stage15.Archives.ValveGcf.FragmentedClusterChainInspectAndExtract");

        // Negative 1: Mismatched GCFBlockEntryHeader blockCount -> rejected
        std::vector<uint8_t> badGcfHdr;
        PathUtils::ReadFileBytes(gcfPath, 65536u, badGcfHdr);
        if (badGcfHdr.size() > 56u) PutU32LE(badGcfHdr.data() + 44u, 999u);
        AssertArcRejected(tc, L"Stage15.Archives.ValveGcf.RejectMismatchedBlockCount",
                          writeArcBytes(L"bad_cnt.gcf", badGcfHdr));

        // Negative 2: Corrupt FragmentationMap into a cluster cycle (cluster 2 -> 0 -> 2) -> extraction must reject cleanly!
        std::vector<uint8_t> cycGcf;
        PathUtils::ReadFileBytes(gcfPath, 65536u, cycGcf);
        // FragMap starts at 44 (GCFHeader) + 32 + 4*28 (BlockEntries) + 16 (FragMapHdr) = 204
        if (cycGcf.size() >= 220u) {
            PutU32LE(cycGcf.data() + 204u + 0u * 4u, 2u); // cluster 0 -> 2 (cycle 2 -> 0 -> 2!)
        }
        std::vector<uint8_t> cycOut;
        const std::wstring cycGcfPath = writeArcBytes(L"frag_cycle.gcf", cycGcf);
        tc.Record(!ArchiveReaders::ExtractSingleEntryToMemory(cycGcfPath, L"hl2/materials/readme_large.txt", cycOut),
                  L"Stage15.Archives.ValveGcf.RejectFragmentationClusterCycle");
    }

    const uint64_t guardAfter = ReaderGuard::StoppedCount();
    tc.Record(guardAfter == guardBefore,
              L"Stage15.Archives.SystemPak.ZeroReaderFaults",
              L"guardStopsDelta=" + std::to_wstring(guardAfter - guardBefore));

    for (const auto& f : createdFiles) DeleteFileW(f.c_str());
    RemoveDirectoryW(workDir.c_str());
}

} // namespace Explorer2::SelfTestSuite
