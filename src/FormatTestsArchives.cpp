// FormatTestsArchives.cpp: self-tests of the archive readers (ArchiveExplorer, ZipDeflate, formats/), stage 15 ("15.archives" runs these alone)
#include "Crc32.h"
#include "SelfTestCommon.h"
#include "TestBytes.h"
#include "ArchiveExplorer.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

namespace Explorer2::SelfTestSuite {

namespace {

// ============================================================================
// Binary & Checksum Helpers for Self-Contained Stage 15 Archive Generators
// ============================================================================
void RemoveDirTreeForStage15(const std::wstring& dir) {
    if (dir.empty()) return;
    const std::wstring ext = (dir.rfind(L"\\\\?\\", 0) == 0) ? dir : (L"\\\\?\\" + dir);
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((ext + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring p = ext + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirTreeForStage15(p);
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(ext.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(ext.c_str());
}

using namespace ::Explorer2::TestBytes; // byte writers (TestBytes.h)



uint32_t CalcAdler32(const uint8_t* data, size_t len) {
    uint32_t s1 = 1u, s2 = 0u;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

bool BytesMatchStr(const std::vector<uint8_t>& v, const std::string& s) {
    return v.size() == s.size() && (s.empty() || std::memcmp(v.data(), s.data(), s.size()) == 0);
}

const ArchiveItemEntry* FindEntryCI(const ArchiveSummaryInfo& info, const std::wstring& path) {
    for (const auto& e : info.entries) {
        if (_wcsicmp(e.pathInArchive.c_str(), path.c_str()) == 0) return &e;
    }
    return nullptr;
}

// ============================================================================
// 1. Real RFC 1951 Fixed-Huffman Deflate / Zlib / Gzip Encoder (with LZ77)
// ============================================================================
struct LsbBitWriter {
    std::vector<uint8_t> out;
    uint32_t bitBuf = 0;
    int bitCnt = 0;

    void WriteBits(uint32_t val, int n) {
        bitBuf |= (val << bitCnt);
        bitCnt += n;
        while (bitCnt >= 8) {
            out.push_back(static_cast<uint8_t>(bitBuf & 0xFFu));
            bitBuf >>= 8;
            bitCnt -= 8;
        }
    }
    void WriteHuffMSB(uint32_t code, int n) {
        for (int i = n - 1; i >= 0; --i) {
            WriteBits((code >> i) & 1u, 1);
        }
    }
    void Flush() {
        if (bitCnt > 0) {
            out.push_back(static_cast<uint8_t>(bitBuf & 0xFFu));
            bitBuf = 0;
            bitCnt = 0;
        }
    }
};

std::vector<uint8_t> CompressDeflateFixed(const uint8_t* data, size_t len) {
    LsbBitWriter bw;
    bw.WriteBits(1u, 1); // BFINAL = 1
    bw.WriteBits(1u, 2); // BTYPE = 01 (Fixed Huffman)

    auto emitLit = [&](uint32_t sym) {
        if (sym <= 143u) bw.WriteHuffMSB(0x30u + sym, 8);
        else if (sym <= 255u) bw.WriteHuffMSB(0x190u + (sym - 144u), 9);
        else if (sym <= 279u) bw.WriteHuffMSB(sym - 256u, 7);
        else bw.WriteHuffMSB(0xC0u + (sym - 280u), 8);
    };

    static const uint16_t kLenBase[29] = {
        3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
        35,43,51,59, 67,83,99,115, 131,163,195,227, 258
    };
    static const uint8_t kLenExtra[29] = {
        0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
        3,3,3,3, 4,4,4,4, 5,5,5,5, 0
    };
    static const uint16_t kDistBase[30] = {
        1,2,3,4, 5,7, 9,13, 17,25, 33,49, 65,97, 129,193,
        257,385, 513,769, 1025,1537, 2049,3073, 4097,6145, 8193,12289, 16385,24577
    };
    static const uint8_t kDistExtra[30] = {
        0,0,0,0, 1,1, 2,2, 3,3, 4,4, 5,5, 6,6,
        7,7, 8,8, 9,9, 10,10, 11,11, 12,12, 13,13
    };

    size_t pos = 0;
    while (pos < len) {
        size_t bestLen = 0;
        size_t bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 4096);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(258, len - pos);
            while (m < maxM && data[pos + m] == data[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) {
                bestLen = m;
                bestDist = d;
                if (bestLen >= 64) break;
            }
        }
        if (bestLen >= 4) {
            int lCode = 28;
            for (int c = 0; c < 28; ++c) {
                if (bestLen >= kLenBase[c] && bestLen < kLenBase[c + 1]) { lCode = c; break; }
            }
            emitLit(static_cast<uint32_t>(257 + lCode));
            if (kLenExtra[lCode] > 0) {
                bw.WriteBits(static_cast<uint32_t>(bestLen - kLenBase[lCode]), kLenExtra[lCode]);
            }
            int dCode = 29;
            for (int c = 0; c < 29; ++c) {
                if (bestDist >= kDistBase[c] && bestDist < kDistBase[c + 1]) { dCode = c; break; }
            }
            bw.WriteHuffMSB(static_cast<uint32_t>(dCode), 5);
            if (kDistExtra[dCode] > 0) {
                bw.WriteBits(static_cast<uint32_t>(bestDist - kDistBase[dCode]), kDistExtra[dCode]);
            }
            pos += bestLen;
        } else {
            emitLit(data[pos++]);
        }
    }
    emitLit(256u); // End-of-block
    bw.Flush();
    return bw.out;
}

std::vector<uint8_t> CompressZlib(const uint8_t* data, size_t len) {
    std::vector<uint8_t> z = { 0x78u, 0x9Cu };
    auto def = CompressDeflateFixed(data, len);
    z.insert(z.end(), def.begin(), def.end());
    AppendU32BE(z, CalcAdler32(data, len));
    return z;
}

std::vector<uint8_t> CompressGzip(const uint8_t* data, size_t len) {
    std::vector<uint8_t> gz = { 0x1Fu, 0x8Bu, 0x08u, 0x00u, 0,0,0,0, 0x00u, 0x03u };
    auto def = CompressDeflateFixed(data, len);
    gz.insert(gz.end(), def.begin(), def.end());
    AppendU32LE(gz, Checksum::Crc32(data, len));
    AppendU32LE(gz, static_cast<uint32_t>(len));
    return gz;
}

std::vector<uint8_t> BuildPosixTar(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> tar;
    for (const auto& kv : files) {
        uint8_t hdr[512] = {};
        std::memcpy(hdr, kv.first.data(), std::min<size_t>(kv.first.size(), 99));
        snprintf(reinterpret_cast<char*>(hdr + 100), 8, "%07o", 0644);
        snprintf(reinterpret_cast<char*>(hdr + 124), 12, "%011llo", static_cast<unsigned long long>(kv.second.size()));
        hdr[156] = (!kv.first.empty() && kv.first.back() == '/') ? '5' : '0';
        std::memcpy(hdr + 257, "ustar", 5);
        std::memset(hdr + 148, ' ', 8);
        uint32_t sum = 0;
        for (int i = 0; i < 512; ++i) sum += hdr[i];
        snprintf(reinterpret_cast<char*>(hdr + 148), 8, "%06o", sum);
        tar.insert(tar.end(), hdr, hdr + 512);
        if (hdr[156] == '0') {
            tar.insert(tar.end(), kv.second.begin(), kv.second.end());
            size_t rem = kv.second.size() % 512;
            if (rem != 0) tar.insert(tar.end(), 512 - rem, 0);
        }
    }
    tar.insert(tar.end(), 1024, 0);
    return tar;
}

// ============================================================================
// 2. Real LZMA1 Range Encoder + LZMA2 Chunk Builder + BCJ x86 Encoder + .7z + .xz
// ============================================================================
struct LzmaRangeEncoder {
    std::vector<uint8_t> out;
    uint64_t low = 0;
    uint32_t range = 0xFFFFFFFFu;
    uint8_t cache = 0;
    uint64_t cacheSize = 1;

    void ShiftLow() {
        uint32_t lowHi = static_cast<uint32_t>(low >> 32);
        if (lowHi != 0 || static_cast<uint32_t>(low) < 0xFF000000u) {
            uint8_t temp = cache;
            do {
                out.push_back(static_cast<uint8_t>(temp + lowHi));
                temp = 0xFFu;
            } while (--cacheSize != 0);
            cache = static_cast<uint8_t>(static_cast<uint32_t>(low) >> 24);
        }
        ++cacheSize;
        low = static_cast<uint32_t>(low) << 8;
    }

    void Normalize() {
        if (range < 0x01000000u) {
            range <<= 8;
            ShiftLow();
        }
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
        Normalize();
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
            if (((val >> i) & 1u) != 0) low += range;
            Normalize();
        }
    }

    void Flush() {
        for (int i = 0; i < 5; ++i) ShiftLow();
    }
};

// Encodes data with LZMA1 (lc=3, lp=0, pb=2 => propsByte = 0x5D) using real literals + LZ77 matches!
std::vector<uint8_t> EncodeTestLzma1Raw(const uint8_t* data, size_t len) {
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

    LzmaRangeEncoder rc;
    uint32_t state = 0;
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    (void)rep1; (void)rep2; (void)rep3;

    auto posSlotOf = [](uint32_t dist) -> uint32_t {
        if (dist < 4u) return dist;
        uint32_t hb = 0, tmp = dist;
        while (tmp > 1u) { tmp >>= 1; ++hb; }
        return (hb << 1) + ((dist >> (hb - 1u)) & 1u);
    };

    size_t pos = 0;
    while (pos < len) {
        uint32_t posState = static_cast<uint32_t>(pos) & posMask;

        // Search for LZ77 match of length 4..273 within last 4096 bytes
        size_t bestLen = 0;
        size_t bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 4096);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(273, len - pos);
            while (m < maxM && data[pos + m] == data[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) {
                bestLen = m;
                bestDist = d;
                if (bestLen >= 64) break;
            }
        }

        if (bestLen >= 4) {
            rc.EncodeBit(isMatch[state][posState], 1u);
            rc.EncodeBit(isRep[state], 0u);
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            rep0 = static_cast<uint32_t>(bestDist - 1);

            uint32_t remLen = static_cast<uint32_t>(bestLen - 2);
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
            uint32_t lenToPos = (remLen < 4u) ? remLen : 3u;
            uint32_t slot = posSlotOf(rep0);
            rc.EncodeBitTree(posSlotEnc[lenToPos], 6, slot);
            if (slot >= 4u) {
                int numDirect = static_cast<int>((slot >> 1) - 1u);
                uint32_t base = (2u | (slot & 1u)) << numDirect;
                uint32_t distReduced = rep0 - base;
                if (slot < 14u) {
                    rc.EncodeRevBitTree(posEnc + base - slot - 1, numDirect, distReduced);
                } else {
                    rc.EncodeDirectBits(distReduced >> 4, numDirect - 4);
                    rc.EncodeRevBitTree(alignEnc, 4, distReduced & 0xFu);
                }
            }
            pos += bestLen;
        } else {
            rc.EncodeBit(isMatch[state][posState], 0u);
            uint8_t prevB = (pos > 0) ? data[pos - 1] : 0u;
            uint32_t litState = static_cast<uint32_t>(prevB) >> (8 - lc);
            uint16_t* probs = &litProbs[litState * 0x300u];
            uint8_t curB = data[pos];
            uint32_t sym = 1u;
            if (state >= 7u) {
                uint32_t matchByte = (pos > rep0) ? data[pos - rep0 - 1] : 0u;
                bool matched = true;
                for (int bitIdx = 7; bitIdx >= 0; --bitIdx) {
                    uint32_t bit = (curB >> bitIdx) & 1u;
                    uint32_t matchBit = (matchByte >> 7) & 1u;
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
                    uint32_t bit = (curB >> bitIdx) & 1u;
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

// Wraps data as an LZMA2 stream (single compressed chunk 0xE0.. + 0x00 end marker)
std::vector<uint8_t> EncodeTestLzma2(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    if (len == 0) {
        out.push_back(0x00u);
        return out;
    }
    auto lzma1 = EncodeTestLzma1Raw(data, len);
    uint32_t u = static_cast<uint32_t>(len - 1);
    uint16_t p = static_cast<uint16_t>(lzma1.size() - 1);
    out.push_back(static_cast<uint8_t>(0xE0u | ((u >> 16) & 0x1Fu))); // Reset dict + state + new props
    out.push_back(static_cast<uint8_t>((u >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(u & 0xFFu));
    out.push_back(static_cast<uint8_t>((p >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(p & 0xFFu));
    out.push_back(0x5Du); // lc=3, lp=0, pb=2
    out.insert(out.end(), lzma1.begin(), lzma1.end());
    out.push_back(0x00u); // End of LZMA2 stream
    return out;
}

// Forward BCJ x86 encoder matching Pavlov's Bra86.c (encoding=1)
void ApplyBcjX86Encode(uint8_t* data, size_t size, uint32_t ip = 0) {
    if (!data || size < 5) return;
    static const bool kAllowed[8] = { true, true, true, false, true, false, false, false };
    static const uint8_t kBitNum[8] = { 0, 1, 2, 2, 3, 3, 3, 3 };
    size_t pos = 0;
    size_t prevPos = static_cast<size_t>(0) - 5;
    uint32_t prevMask = 0;
    while (pos + 5 <= size) {
        if ((data[pos] & 0xFEu) != 0xE8u) { ++pos; continue; }
        size_t d = pos - prevPos;
        prevPos = pos;
        if (d > 3) prevMask = 0;
        else {
            prevMask >>= (d - 1);
            if (prevMask != 0) {
                uint8_t b = data[pos + 4 - kBitNum[prevMask]];
                if (!kAllowed[prevMask] || b == 0x00u || b == 0xFFu) {
                    prevMask = ((prevMask << 1) & 0x7u) | 1u;
                    ++pos;
                    continue;
                }
            }
        }
        if (data[pos + 4] == 0x00u || data[pos + 4] == 0xFFu) {
            uint32_t src = static_cast<uint32_t>(data[pos + 1]) |
                           (static_cast<uint32_t>(data[pos + 2]) << 8) |
                           (static_cast<uint32_t>(data[pos + 3]) << 16) |
                           (static_cast<uint32_t>(data[pos + 4]) << 24);
            uint32_t dest = src + (ip + static_cast<uint32_t>(pos) + 5u);
            data[pos + 1] = static_cast<uint8_t>(dest & 0xFFu);
            data[pos + 2] = static_cast<uint8_t>((dest >> 8) & 0xFFu);
            data[pos + 3] = static_cast<uint8_t>((dest >> 16) & 0xFFu);
            data[pos + 4] = ((dest & 0x01000000u) != 0) ? 0xFFu : 0x00u;
            pos += 5;
        } else {
            prevMask = ((prevMask << 1) & 0x7u) | 1u;
            ++pos;
        }
    }
}

void PushXzVli(std::vector<uint8_t>& b, uint64_t v) {
    while (v >= 0x80ULL) {
        b.push_back(static_cast<uint8_t>((v & 0x7FULL) | 0x80ULL));
        v >>= 7;
    }
    b.push_back(static_cast<uint8_t>(v));
}

// Builds a spec-compliant .xz stream (Check=CRC32 0x01, optional BCJ x86 filter + LZMA2)
std::vector<uint8_t> BuildXzStream(const uint8_t* rawData, size_t rawLen, bool useBcjX86 = false) {
    std::vector<uint8_t> filtered(rawData, rawData + rawLen);
    if (useBcjX86) ApplyBcjX86Encode(filtered.data(), filtered.size(), 0);
    auto lzma2 = EncodeTestLzma2(filtered.data(), filtered.size());

    std::vector<uint8_t> xz = { 0xFD, '7', 'z', 'X', 'Z', 0x00, 0x00, 0x01 }; // CRC32 check (4 bytes)
    AppendU32LE(xz, Checksum::Crc32(xz.data() + 6, 2));

    // Block Header
    std::vector<uint8_t> blkHdr = { 0x00, static_cast<uint8_t>(useBcjX86 ? 0x01u : 0x00u) };
    if (useBcjX86) {
        blkHdr.push_back(0x04u); // x86 BCJ filter ID
        blkHdr.push_back(0x00u); // 0 bytes props
    }
    blkHdr.push_back(0x21u); // LZMA2 filter ID
    blkHdr.push_back(0x01u); // 1 byte prop
    blkHdr.push_back(0x14u); // dictProp = 20 (1 MB dictionary)
    while ((blkHdr.size() + 4) % 4 != 0) blkHdr.push_back(0x00u);
    blkHdr[0] = static_cast<uint8_t>(((blkHdr.size() + 4) / 4) - 1);
    uint32_t blkHdrCrc = Checksum::Crc32(blkHdr.data(), blkHdr.size());
    AppendU32LE(blkHdr, blkHdrCrc);

    size_t unpaddedBlockSize = blkHdr.size() + lzma2.size() + 4; // +4 for CRC32 check
    xz.insert(xz.end(), blkHdr.begin(), blkHdr.end());
    xz.insert(xz.end(), lzma2.begin(), lzma2.end());
    while (xz.size() % 4 != 0) xz.push_back(0x00u);
    AppendU32LE(xz, Checksum::Crc32(rawData, rawLen)); // Block Check (CRC32)

    // Index
    std::vector<uint8_t> index = { 0x00u };
    PushXzVli(index, 1); // 1 record
    PushXzVli(index, unpaddedBlockSize);
    PushXzVli(index, rawLen);
    while (index.size() % 4 != 0) index.push_back(0x00u);
    uint32_t idxCrc = Checksum::Crc32(index.data(), index.size());
    AppendU32LE(index, idxCrc);
    xz.insert(xz.end(), index.begin(), index.end());

    // Stream Footer: CRC32(BackwardSize + StreamFlags) + BackwardSize(u32) + Flags(0x00, 0x01) + "YZ"
    uint32_t backwardVal = static_cast<uint32_t>((index.size() / 4) - 1);
    uint8_t footerBody[6] = {};
    PutU32LE(footerBody, backwardVal);
    footerBody[4] = 0x00u;
    footerBody[5] = 0x01u;
    AppendU32LE(xz, Checksum::Crc32(footerBody, 6));
    xz.insert(xz.end(), footerBody, footerBody + 6);
    xz.push_back('Y');
    xz.push_back('Z');
    return xz;
}

void Push7zNum(std::vector<uint8_t>& b, uint64_t val) {
    if (val < 0x80ULL) {
        b.push_back(static_cast<uint8_t>(val));
        return;
    }
    int extra = 1;
    while (extra < 8 && val >= (1ULL << ((extra + 1) * 7))) ++extra;
    if (extra >= 8) {
        b.push_back(0xFFu);
        AppendU64LE(b, val);
        return;
    }
    uint8_t first = static_cast<uint8_t>(0xFFu << (8 - extra));
    first |= static_cast<uint8_t>(val >> (extra * 8));
    b.push_back(first);
    for (int i = 0; i < extra; ++i) {
        b.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFFu));
    }
}

// Builds a real .7z archive with 0x17 kEncodedHeader (LZMA1-compressed kHeader!) + solid LZMA2 (+ optional BCJ x86) payload!
std::vector<uint8_t> Build7zWithEncodedHeader(
    const std::vector<std::pair<std::wstring, std::string>>& files,
    bool useBcjX86 = false)
{
    std::vector<uint8_t> solidPlain;
    for (const auto& kv : files) {
        solidPlain.insert(solidPlain.end(), kv.second.begin(), kv.second.end());
    }
    std::vector<uint8_t> toPack = solidPlain;
    if (useBcjX86 && !toPack.empty()) ApplyBcjX86Encode(toPack.data(), toPack.size(), 0);
    std::vector<uint8_t> mainPacked = EncodeTestLzma2(toPack.data(), toPack.size());

    // Build inner 0x01 kHeader
    std::vector<uint8_t> kHdr = { 0x01u }; // kHeader
    if (!files.empty()) {
        kHdr.push_back(0x04u); // kMainStreamsInfo
        // kPackInfo (0x06)
        kHdr.push_back(0x06u);
        Push7zNum(kHdr, 0); // packPos = 0 (offset 32)
        Push7zNum(kHdr, 1); // 1 pack stream
        kHdr.push_back(0x09u); // kSize
        Push7zNum(kHdr, mainPacked.size());
        kHdr.push_back(0x00u); // kEnd PackInfo

        // kUnPackInfo (0x07)
        kHdr.push_back(0x07u);
        kHdr.push_back(0x0Bu); // kFolder
        Push7zNum(kHdr, 1);    // 1 folder
        kHdr.push_back(0x00u); // external = 0
        if (!useBcjX86) {
            Push7zNum(kHdr, 1); // 1 coder: LZMA2
            kHdr.push_back(0x21u); // idSize=1, hasAttrs=1
            kHdr.push_back(0x21u); // methodId = LZMA2 (0x21)
            Push7zNum(kHdr, 1);    // propsLen = 1
            kHdr.push_back(0x14u); // dictProp = 20 (1 MB)
        } else {
            Push7zNum(kHdr, 2); // 2 coders: [0]=LZMA2, [1]=BCJ x86
            kHdr.push_back(0x21u);
            kHdr.push_back(0x21u);
            Push7zNum(kHdr, 1);
            kHdr.push_back(0x14u);
            kHdr.push_back(0x04u); // idSize=4, no attrs
            kHdr.insert(kHdr.end(), { 0x03u, 0x03u, 0x01u, 0x03u }); // BCJ x86
            // 1 bindPair: coder 1's input (inIndex=1) comes from coder 0's output (outIndex=0)
            Push7zNum(kHdr, 1);
            Push7zNum(kHdr, 0);
        }
        kHdr.push_back(0x0Cu); // kCodersUnPackSize
        Push7zNum(kHdr, solidPlain.size());
        if (useBcjX86) Push7zNum(kHdr, solidPlain.size());
        kHdr.push_back(0x00u); // kEnd UnPackInfo

        // kSubStreamsInfo (0x08) if > 1 file
        if (files.size() > 1) {
            kHdr.push_back(0x08u);
            kHdr.push_back(0x0Du); // kNumUnPackStream
            Push7zNum(kHdr, files.size());
            kHdr.push_back(0x09u); // kSize (N - 1 sizes)
            for (size_t i = 0; i + 1 < files.size(); ++i) {
                Push7zNum(kHdr, files[i].second.size());
            }
            kHdr.push_back(0x00u); // kEnd SubStreamsInfo
        }
        kHdr.push_back(0x00u); // kEnd MainStreamsInfo
    }

    // kFilesInfo (0x05)
    kHdr.push_back(0x05u);
    Push7zNum(kHdr, files.size());
    std::vector<uint8_t> namesProp = { 0x00u }; // external = 0
    for (const auto& kv : files) {
        for (wchar_t wc : kv.first) AppendU16LE(namesProp, static_cast<uint16_t>(wc));
        AppendU16LE(namesProp, 0);
    }
    kHdr.push_back(0x11u); // kName
    Push7zNum(kHdr, namesProp.size());
    kHdr.insert(kHdr.end(), namesProp.begin(), namesProp.end());
    kHdr.push_back(0x00u); // kEnd FilesInfo
    kHdr.push_back(0x00u); // kEnd kHeader

    // Compress kHeader using LZMA1 so NextHeader is 0x17 kEncodedHeader!
    std::vector<uint8_t> packedHdr = EncodeTestLzma1Raw(kHdr.data(), kHdr.size());
    std::vector<uint8_t> encHdr = {
        0x17u, // kEncodedHeader
        0x06u  // kPackInfo
    };
    Push7zNum(encHdr, mainPacked.size()); // packPos right after mainPacked
    Push7zNum(encHdr, 1);
    encHdr.push_back(0x09u);
    Push7zNum(encHdr, packedHdr.size());
    encHdr.push_back(0x00u); // kEnd PackInfo
    encHdr.push_back(0x07u); // kUnPackInfo
    encHdr.push_back(0x0Bu); // kFolder
    Push7zNum(encHdr, 1);
    encHdr.push_back(0x00u); // external=0
    Push7zNum(encHdr, 1);    // 1 coder: LZMA1 (0x030101)
    encHdr.push_back(0x23u); // idSize=3, hasAttrs=1
    encHdr.insert(encHdr.end(), { 0x03u, 0x01u, 0x01u });
    Push7zNum(encHdr, 5);    // 5-byte LZMA1 props (propsByte=0x5D + u32 dictSize=65536)
    encHdr.push_back(0x5Du);
    AppendU32LE(encHdr, 65536u);
    encHdr.push_back(0x0Cu); // kCodersUnPackSize
    Push7zNum(encHdr, kHdr.size());
    encHdr.push_back(0x00u); // kEnd UnPackInfo
    encHdr.push_back(0x00u); // kEnd EncodedHeader

    // 32-byte SignatureHeader
    std::vector<uint8_t> arc = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0x00, 0x04 };
    uint8_t startHdr[20] = {};
    PutU64LE(startHdr + 0, mainPacked.size() + packedHdr.size()); // NextHeaderOffset
    PutU64LE(startHdr + 8, encHdr.size());                        // NextHeaderSize
    PutU32LE(startHdr + 16, Checksum::Crc32(encHdr.data(), encHdr.size()));
    AppendU32LE(arc, Checksum::Crc32(startHdr, 20));
    arc.insert(arc.end(), startHdr, startHdr + 20);
    arc.insert(arc.end(), mainPacked.begin(), mainPacked.end());
    arc.insert(arc.end(), packedHdr.begin(), packedHdr.end());
    arc.insert(arc.end(), encHdr.begin(), encHdr.end());
    return arc;
}

// ============================================================================
// 3. Real BZip2 Stream Encoder (RLE1 + BWT + MTF/RUNA/RUNB + Canonical Huffman)
// ============================================================================
struct MsbBitWriter {
    std::vector<uint8_t> out;
    uint32_t buf = 0;
    int cnt = 0;
    void Write(uint32_t val, int n) {
        for (int i = n - 1; i >= 0; --i) {
            buf = (buf << 1) | ((val >> i) & 1u);
            if (++cnt == 8) {
                out.push_back(static_cast<uint8_t>(buf & 0xFFu));
                buf = 0;
                cnt = 0;
            }
        }
    }
    void Flush() {
        if (cnt > 0) {
            out.push_back(static_cast<uint8_t>((buf << (8 - cnt)) & 0xFFu));
            buf = 0;
            cnt = 0;
        }
    }
};

std::vector<uint8_t> BuildBzip2Stream(const uint8_t* raw, size_t len) {
    // 1. RLE1 encode (runs of >=4 identical bytes emit 4 bytes + repeat count)
    std::vector<uint8_t> rle;
    size_t p = 0;
    while (p < len) {
        uint8_t b = raw[p];
        size_t run = 1;
        while (p + run < len && raw[p + run] == b && run < 255) ++run;
        if (run >= 4) {
            rle.insert(rle.end(), 4, b);
            rle.push_back(static_cast<uint8_t>(run - 4));
        } else {
            rle.insert(rle.end(), run, b);
        }
        p += run;
    }
    if (rle.empty()) rle.push_back(0);
    const size_t n = rle.size();

    // 2. Exact cyclic Burrows-Wheeler Transform
    std::vector<uint32_t> rot(n);
    std::iota(rot.begin(), rot.end(), 0u);
    std::sort(rot.begin(), rot.end(), [&](uint32_t a, uint32_t b) {
        if (a == b) return false;
        for (size_t k = 0; k < n; ++k) {
            uint8_t ca = rle[(a + k) % n];
            uint8_t cb = rle[(b + k) % n];
            if (ca != cb) return ca < cb;
        }
        return a < b;
    });

    uint32_t origPtr = 0;
    std::vector<uint8_t> L(n);
    bool inUse[256] = {};
    for (size_t i = 0; i < n; ++i) {
        if (rot[i] == 0) origPtr = static_cast<uint32_t>(i);
        uint8_t ch = rle[(rot[i] + n - 1) % n];
        L[i] = ch;
        inUse[ch] = true;
    }

    uint8_t unseqToSeq[256] = {};
    uint8_t yy[256] = {};
    int nInUse = 0;
    for (int c = 0; c < 256; ++c) {
        if (inUse[c]) {
            unseqToSeq[c] = static_cast<uint8_t>(nInUse);
            yy[nInUse] = static_cast<uint8_t>(nInUse);
            ++nInUse;
        }
    }
    const int alphaSize = nInUse + 2;
    const int eobSym = nInUse + 1;

    // 3. MTF + bijective base-2 RUNA(0) / RUNB(1)
    std::vector<uint16_t> syms;
    auto flushZeroRun = [&](uint32_t z) {
        while (z > 0) {
            uint32_t rem = (z - 1u) & 1u; // 0 => RUNA, 1 => RUNB
            syms.push_back(static_cast<uint16_t>(rem));
            z = (z - 1u) >> 1;
        }
    };
    uint32_t zeroRun = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t target = unseqToSeq[L[i]];
        int idx = 0;
        while (yy[idx] != target) ++idx;
        if (idx == 0) {
            ++zeroRun;
        } else {
            if (zeroRun > 0) { flushZeroRun(zeroRun); zeroRun = 0; }
            for (int k = idx; k > 0; --k) yy[k] = yy[k - 1];
            yy[0] = target;
            syms.push_back(static_cast<uint16_t>(idx + 1));
        }
    }
    if (zeroRun > 0) flushZeroRun(zeroRun);
    syms.push_back(static_cast<uint16_t>(eobSym));

    // 4. Flat canonical Huffman code length Lbits for all alphaSize symbols (1<<Lbits >= alphaSize)
    int codeBits = 1;
    while ((1 << codeBits) < alphaSize) ++codeBits;
    // Canonical assignment when first `twoCount` symbols have length `codeBits - 1` and remaining have `codeBits`:
    // Wait: (1 << codeBits) - alphaSize symbols can have length `codeBits - 1`, making a complete Kraft-sum=1 tree!
    int shortCount = (1 << codeBits) - alphaSize;
    std::vector<uint8_t> codeLen(alphaSize, static_cast<uint8_t>(codeBits));
    for (int i = 0; i < shortCount; ++i) codeLen[i] = static_cast<uint8_t>(codeBits - 1);

    std::vector<uint32_t> codes(alphaSize, 0);
    uint32_t curCode = 0;
    for (int bits = 1; bits <= codeBits; ++bits) {
        for (int i = 0; i < alphaSize; ++i) {
            if (codeLen[i] == bits) codes[i] = curCode++;
        }
        curCode <<= 1;
    }

    MsbBitWriter bw;
    bw.Write('B', 8); bw.Write('Z', 8); bw.Write('h', 8); bw.Write('1', 8);
    // Block magic 0x314159265359
    bw.Write(0x314159u, 24); bw.Write(0x265359u, 24);
    bw.Write(0u, 32); // blockCRC
    bw.Write(0u, 1);  // randomised = 0
    bw.Write(origPtr, 24);

    uint32_t inUse16 = 0;
    for (int g = 0; g < 16; ++g) {
        for (int j = 0; j < 16; ++j) if (inUse[g * 16 + j]) inUse16 |= (1u << (15 - g));
    }
    bw.Write(inUse16, 16);
    for (int g = 0; g < 16; ++g) {
        if (inUse16 & (1u << (15 - g))) {
            uint32_t grp16 = 0;
            for (int j = 0; j < 16; ++j) if (inUse[g * 16 + j]) grp16 |= (1u << (15 - j));
            bw.Write(grp16, 16);
        }
    }

    uint32_t nSelectors = static_cast<uint32_t>((syms.size() + 49u) / 50u);
    bw.Write(2u, 3); // 2 Huffman groups (minimum required by BZip2 spec)
    bw.Write(nSelectors, 15);
    for (uint32_t s = 0; s < nSelectors; ++s) bw.Write(0u, 1); // Always select group 0

    for (int grp = 0; grp < 2; ++grp) {
        int cur = codeLen[0];
        bw.Write(static_cast<uint32_t>(cur), 5);
        for (int i = 0; i < alphaSize; ++i) {
            while (cur < codeLen[i]) { bw.Write(1u, 1); bw.Write(0u, 1); ++cur; }
            while (cur > codeLen[i]) { bw.Write(1u, 1); bw.Write(1u, 1); --cur; }
            bw.Write(0u, 1);
        }
    }

    for (uint16_t sym : syms) {
        bw.Write(codes[sym], codeLen[sym]);
    }

    // End-of-stream magic 0x177245385090 + combinedCRC(0)
    bw.Write(0x177245u, 24); bw.Write(0x385090u, 24);
    bw.Write(0u, 32);
    bw.Flush();
    return bw.out;
}

// ============================================================================
// 4. Real LZ4 Frame Encoder (with LZ4 match tokens) & Zstandard RFC 8878 Encoder
// ============================================================================
std::vector<uint8_t> BuildLz4Frame(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> blk;
    size_t pos = 0;
    size_t anchor = 0;
    while (pos + 5 <= len) {
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 8192);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            // Leave last 5 bytes as trailing literals per LZ4 block convention
            size_t maxM = (len - pos > 5) ? (len - pos - 5) : 0;
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) { bestLen = m; bestDist = d; if (bestLen >= 64) break; }
        }
        if (bestLen >= 4) {
            size_t litLen = pos - anchor;
            size_t mCode = bestLen - 4;
            uint8_t tok = static_cast<uint8_t>((std::min<size_t>(litLen, 15) << 4) | std::min<size_t>(mCode, 15));
            blk.push_back(tok);
            if (litLen >= 15) {
                size_t rem = litLen - 15;
                while (rem >= 255) { blk.push_back(255); rem -= 255; }
                blk.push_back(static_cast<uint8_t>(rem));
            }
            blk.insert(blk.end(), raw + anchor, raw + pos);
            AppendU16LE(blk, static_cast<uint16_t>(bestDist));
            if (mCode >= 15) {
                size_t rem = mCode - 15;
                while (rem >= 255) { blk.push_back(255); rem -= 255; }
                blk.push_back(static_cast<uint8_t>(rem));
            }
            pos += bestLen;
            anchor = pos;
        } else {
            ++pos;
        }
    }
    // Final literal-only sequence
    size_t tailLit = len - anchor;
    uint8_t tok = static_cast<uint8_t>(std::min<size_t>(tailLit, 15) << 4);
    blk.push_back(tok);
    if (tailLit >= 15) {
        size_t rem = tailLit - 15;
        while (rem >= 255) { blk.push_back(255); rem -= 255; }
        blk.push_back(static_cast<uint8_t>(rem));
    }
    blk.insert(blk.end(), raw + anchor, raw + len);

    std::vector<uint8_t> frm;
    AppendU32LE(frm, 0x184D2204u);
    frm.push_back(0x68u); // Version 01, B.Indep=1, C.Size=1
    frm.push_back(0x70u); // BD: 4MB max block
    AppendU64LE(frm, len);
    frm.push_back(0x00u); // HC byte
    AppendU32LE(frm, static_cast<uint32_t>(blk.size()));
    frm.insert(frm.end(), blk.begin(), blk.end());
    AppendU32LE(frm, 0u); // EndMark
    return frm;
}

// Builds a Zstandard RFC 8878 frame containing a real Compressed_Block (RLE/Raw literals + RLE FSE sequence match)
// followed by a final Raw_Block so both Compressed_Block (sequences + offsets) and Raw_Block paths are tested!
std::vector<uint8_t> BuildZstdCompressedFrame(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> zst;
    AppendU32LE(zst, 0xFD2FB528u);
    zst.push_back(0xA0u); // fcsFlag=2 (4B content size), singleSegment=1
    AppendU32LE(zst, static_cast<uint32_t>(len));

    // Check if we have a repeating prefix or build a Compressed_Block when len >= 16 and there is a >=4B match,
    // else emit a Compressed_Block with numSeq=0 + Raw_Literals!
    size_t matchPos = 0, matchDist = 0, matchLen = 0;
    for (size_t p = 4; p + 4 <= len; ++p) {
        for (size_t d = 1; d <= p && d <= 28; ++d) {
            size_t m = 0;
            while (p + m < len && m < 34 && raw[p + m] == raw[p - d + m]) ++m;
            if (m >= 4) { matchPos = p; matchDist = d; matchLen = m; break; }
        }
        if (matchLen >= 4) break;
    }

    if (matchLen >= 4 && matchPos <= 15) {
        // Build a Compressed_Block (lastBlock=1, blockType=2) with 1 sequence using RLE FSE mode!
        std::vector<uint8_t> lits;
        lits.insert(lits.end(), raw, raw + matchPos);
        lits.insert(lits.end(), raw + matchPos + matchLen, raw + len);

        std::vector<uint8_t> cblk;
        // Raw_Literals_Block header (sizeFmt=3 -> 3-byte header for any regenSize up to 1MB)
        uint32_t litHdr = (static_cast<uint32_t>(lits.size()) << 4) | 0x0Cu; // litType=0, sizeFmt=3
        cblk.push_back(static_cast<uint8_t>(litHdr & 0xFFu));
        cblk.push_back(static_cast<uint8_t>((litHdr >> 8) & 0xFFu));
        cblk.push_back(static_cast<uint8_t>((litHdr >> 16) & 0xFFu));
        cblk.insert(cblk.end(), lits.begin(), lits.end());

        // Sequences section: 1 sequence, modes = (1<<6)|(1<<4)|(1<<2) = 0x54 (all 3 RLE mode -> accuracyLog=0!)
        cblk.push_back(1u);    // numSeq = 1
        cblk.push_back(0x54u); // LL=RLE, OF=RLE, ML=RLE
        uint8_t llCode = static_cast<uint8_t>(matchPos); // 0..15 has 0 extra bits!
        uint32_t rawOff = static_cast<uint32_t>(matchDist + 3); // > 3 => actualOffset = rawOff - 3 = matchDist
        uint8_t ofCode = 0;
        { uint32_t t = rawOff; while (t > 1u) { t >>= 1; ++ofCode; } }
        uint32_t ofExtra = rawOff - (1u << ofCode);
        uint8_t mlCode = static_cast<uint8_t>(matchLen - 3); // 0..31 has 0 extra bits!

        cblk.push_back(llCode);
        cblk.push_back(ofCode);
        cblk.push_back(mlCode);

        // Backward bitstream for 1 sequence with accuracyLog=0: only ofExtra (ofCode bits) + sentinel 1-bit!
        uint32_t streamBits = (1u << ofCode) | ofExtra;
        int totalBits = static_cast<int>(ofCode) + 1;
        while (totalBits > 0) {
            cblk.push_back(static_cast<uint8_t>(streamBits & 0xFFu));
            streamBits >>= 8;
            totalBits -= 8;
        }

        uint32_t bh = (static_cast<uint32_t>(cblk.size()) << 3) | (2u << 1) | 1u;
        zst.push_back(static_cast<uint8_t>(bh & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 8) & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 16) & 0xFFu));
        zst.insert(zst.end(), cblk.begin(), cblk.end());
    } else {
        uint32_t bh = (static_cast<uint32_t>(len) << 3) | (0u << 1) | 1u;
        zst.push_back(static_cast<uint8_t>(bh & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 8) & 0xFFu));
        zst.push_back(static_cast<uint8_t>((bh >> 16) & 0xFFu));
        zst.insert(zst.end(), raw, raw + len);
    }
    return zst;
}

// ============================================================================
// 5. Microsoft Cabinet (.cab "MSCF") Builder (Stored & MSZIP Deflate)
// ============================================================================
uint32_t ComputeCabCsum(const uint8_t* p, size_t n, uint32_t seed = 0) {
    uint32_t csum = seed;
    size_t words = n / 4;
    for (size_t i = 0; i < words; ++i) {
        uint32_t w = static_cast<uint32_t>(p[i * 4]) |
                     (static_cast<uint32_t>(p[i * 4 + 1]) << 8) |
                     (static_cast<uint32_t>(p[i * 4 + 2]) << 16) |
                     (static_cast<uint32_t>(p[i * 4 + 3]) << 24);
        csum ^= w;
    }
    uint32_t rem = 0;
    const uint8_t* tail = p + words * 4;
    if ((n & 3) == 3) rem = (static_cast<uint32_t>(tail[0]) << 16) | (static_cast<uint32_t>(tail[1]) << 8) | tail[2];
    else if ((n & 3) == 2) rem = (static_cast<uint32_t>(tail[0]) << 8) | tail[1];
    else if ((n & 3) == 1) rem = tail[0];
    return csum ^ rem;
}

std::vector<uint8_t> BuildCabinetMscf(
    const std::vector<std::pair<std::string, std::string>>& files,
    bool useMszip)
{
    std::vector<uint8_t> unpFolder;
    for (const auto& kv : files) {
        unpFolder.insert(unpFolder.end(), kv.second.begin(), kv.second.end());
    }

    // Build CFDATA blocks (<= 32768 uncompressed bytes per block)
    std::vector<uint8_t> cfDataBytes;
    uint16_t numBlocks = 0;
    size_t off = 0;
    while (off < unpFolder.size() || (off == 0 && unpFolder.empty())) {
        size_t chunk = std::min<size_t>(32768, unpFolder.size() - off);
        std::vector<uint8_t> compPayload;
        if (useMszip) {
            compPayload.push_back('C');
            compPayload.push_back('K');
            auto def = CompressDeflateFixed(unpFolder.data() + off, chunk);
            compPayload.insert(compPayload.end(), def.begin(), def.end());
        } else {
            compPayload.assign(unpFolder.data() + off, unpFolder.data() + off + chunk);
        }
        uint8_t lenHdr[4] = {};
        PutU16LE(lenHdr + 0, static_cast<uint16_t>(compPayload.size()));
        PutU16LE(lenHdr + 2, static_cast<uint16_t>(chunk));
        uint32_t csum = ComputeCabCsum(lenHdr, 4, ComputeCabCsum(compPayload.data(), compPayload.size(), 0));
        AppendU32LE(cfDataBytes, csum);
        cfDataBytes.insert(cfDataBytes.end(), lenHdr, lenHdr + 4);
        cfDataBytes.insert(cfDataBytes.end(), compPayload.begin(), compPayload.end());
        ++numBlocks;
        off += chunk;
        if (unpFolder.empty()) break;
    }

    // Build CFFILE records
    std::vector<uint8_t> cfFileBytes;
    uint32_t uoff = 0;
    for (const auto& kv : files) {
        AppendU32LE(cfFileBytes, static_cast<uint32_t>(kv.second.size())); // cbFile
        AppendU32LE(cfFileBytes, uoff);                                    // uoffFolderStart
        AppendU16LE(cfFileBytes, 0);                                       // iFolder = 0
        AppendU16LE(cfFileBytes, 0x596Fu);                                 // date
        AppendU16LE(cfFileBytes, 0x6000u);                                 // time
        AppendU16LE(cfFileBytes, 0x0020u);                                 // attribs
        for (char c : kv.first) cfFileBytes.push_back(static_cast<uint8_t>(c == '/' ? '\\' : c));
        cfFileBytes.push_back(0);
        uoff += static_cast<uint32_t>(kv.second.size());
    }

    uint32_t coffFiles = 36u + 8u;
    uint32_t coffData  = coffFiles + static_cast<uint32_t>(cfFileBytes.size());
    uint32_t cbCabinet = coffData + static_cast<uint32_t>(cfDataBytes.size());

    std::vector<uint8_t> cab = { 'M', 'S', 'C', 'F', 0, 0, 0, 0 };
    AppendU32LE(cab, cbCabinet);
    AppendU32LE(cab, 0);
    AppendU32LE(cab, coffFiles);
    AppendU32LE(cab, 0);
    cab.push_back(3); // versionMinor
    cab.push_back(1); // versionMajor
    AppendU16LE(cab, 1); // cFolders = 1
    AppendU16LE(cab, static_cast<uint16_t>(files.size()));
    AppendU16LE(cab, 0); // flags
    AppendU16LE(cab, 0x1234u);
    AppendU16LE(cab, 0);

    // CFFOLDER
    AppendU32LE(cab, coffData);
    AppendU16LE(cab, numBlocks);
    AppendU16LE(cab, useMszip ? 0x0001u : 0x0000u);

    cab.insert(cab.end(), cfFileBytes.begin(), cfFileBytes.end());
    cab.insert(cab.end(), cfDataBytes.begin(), cfDataBytes.end());
    return cab;
}

// ============================================================================
// 6. Bethesda .bsa (v104) & .ba2 ("BTDX" GNRL + DX10) Builders
// ============================================================================
std::vector<uint8_t> BuildBethesdaBsaV104(
    const std::string& folderName,
    const std::vector<std::pair<std::string, std::string>>& files,
    bool compressedByDefault,
    bool embedFileNames)
{
    uint32_t archiveFlags = 0x01u | 0x02u | (compressedByDefault ? 0x04u : 0u) | (embedFileNames ? 0x100u : 0u);
    uint32_t totalFolderNameLen = static_cast<uint32_t>(folderName.size() + 1);
    std::string fileNamesBlob;
    for (const auto& kv : files) {
        fileNamesBlob += kv.first;
        fileNamesBlob.push_back('\0');
    }
    uint32_t totalFileNameLen = static_cast<uint32_t>(fileNamesBlob.size());

    // Header (36B) + 1 Folder Record (16B) + FolderBlock (1B len + folderName\0 + N * 16B file records) + fileNamesBlob + payloads
    uint32_t folderBlockActualOff = 36u + 16u;
    uint32_t folderRecordStoredOff = folderBlockActualOff + totalFileNameLen;
    uint32_t fileNamesActualOff = folderBlockActualOff + 1u + totalFolderNameLen + static_cast<uint32_t>(files.size()) * 16u;
    uint32_t dataStartOff = fileNamesActualOff + totalFileNameLen;

    std::vector<uint8_t> payloads;
    std::vector<std::pair<uint32_t, uint32_t>> sizeAndOff;
    for (const auto& kv : files) {
        uint32_t dOff = dataStartOff + static_cast<uint32_t>(payloads.size());
        std::vector<uint8_t> block;
        if (embedFileNames) {
            std::string fullEmbedded = folderName + "\\" + kv.first;
            block.push_back(static_cast<uint8_t>(fullEmbedded.size()));
            block.insert(block.end(), fullEmbedded.begin(), fullEmbedded.end());
        }
        if (compressedByDefault) {
            AppendU32LE(block, static_cast<uint32_t>(kv.second.size()));
            auto z = CompressZlib(reinterpret_cast<const uint8_t*>(kv.second.data()), kv.second.size());
            block.insert(block.end(), z.begin(), z.end());
        } else {
            block.insert(block.end(), kv.second.begin(), kv.second.end());
        }
        sizeAndOff.push_back({ static_cast<uint32_t>(block.size()), dOff });
        payloads.insert(payloads.end(), block.begin(), block.end());
    }

    std::vector<uint8_t> bsa = { 'B', 'S', 'A', 0 };
    AppendU32LE(bsa, 104u); // version 104
    AppendU32LE(bsa, 36u);  // folderOffset
    AppendU32LE(bsa, archiveFlags);
    AppendU32LE(bsa, 1u);   // folderCount = 1
    AppendU32LE(bsa, static_cast<uint32_t>(files.size()));
    AppendU32LE(bsa, totalFolderNameLen);
    AppendU32LE(bsa, totalFileNameLen);
    AppendU32LE(bsa, 0u);   // fileFlags

    // Folder record (16 bytes)
    AppendU64LE(bsa, 0x1122334455667788ULL);
    AppendU32LE(bsa, static_cast<uint32_t>(files.size()));
    AppendU32LE(bsa, folderRecordStoredOff);

    // Folder block
    bsa.push_back(static_cast<uint8_t>(totalFolderNameLen)); // includes trailing NUL
    for (char c : folderName) bsa.push_back(static_cast<uint8_t>(c == '/' ? '\\' : c));
    bsa.push_back(0);
    for (size_t i = 0; i < files.size(); ++i) {
        AppendU64LE(bsa, 0xAAAABBBB00000000ULL + i);
        AppendU32LE(bsa, sizeAndOff[i].first);
        AppendU32LE(bsa, sizeAndOff[i].second);
    }

    bsa.insert(bsa.end(), fileNamesBlob.begin(), fileNamesBlob.end());
    bsa.insert(bsa.end(), payloads.begin(), payloads.end());
    return bsa;
}

std::vector<uint8_t> BuildBethesdaBa2Gnrl(const std::vector<std::pair<std::string, std::string>>& files) {
    // 24B header + N * 36B records + payloads + nameTable
    uint64_t dataBase = 24ULL + files.size() * 36ULL;
    std::vector<uint8_t> payloads;
    std::vector<uint8_t> recs;
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& kv = files[i];
        auto z = CompressZlib(reinterpret_cast<const uint8_t*>(kv.second.data()), kv.second.size());
        uint64_t fOff = dataBase + payloads.size();
        payloads.insert(payloads.end(), z.begin(), z.end());

        AppendU32LE(recs, static_cast<uint32_t>(0x1000u + i)); // nameHash
        recs.insert(recs.end(), { 't', 'x', 't', 0 });       // ext[4]
        AppendU32LE(recs, 0x2000u);                            // dirHash
        AppendU32LE(recs, 0u);                                 // flags
        AppendU64LE(recs, fOff);
        AppendU32LE(recs, static_cast<uint32_t>(z.size()));    // packedSize
        AppendU32LE(recs, static_cast<uint32_t>(kv.second.size())); // unpackedSize
        AppendU32LE(recs, 0xBAADF00Du);
    }

    uint64_t nameTableOff = dataBase + payloads.size();
    std::vector<uint8_t> ba2 = { 'B', 'T', 'D', 'X' };
    AppendU32LE(ba2, 1u); // version 1
    ba2.insert(ba2.end(), { 'G', 'N', 'R', 'L' });
    AppendU32LE(ba2, static_cast<uint32_t>(files.size()));
    AppendU64LE(ba2, nameTableOff);
    ba2.insert(ba2.end(), recs.begin(), recs.end());
    ba2.insert(ba2.end(), payloads.begin(), payloads.end());
    for (const auto& kv : files) {
        AppendU16LE(ba2, static_cast<uint16_t>(kv.first.size()));
        ba2.insert(ba2.end(), kv.first.begin(), kv.first.end());
    }
    return ba2;
}

std::vector<uint8_t> BuildBethesdaBa2Dx10(const std::string& texPath, uint16_t w, uint16_t h, const std::vector<uint8_t>& mipPixels) {
    // 24B header + 24B DX10 tex header + 24B chunk record + zlib mip + nameTable
    auto zMip = CompressZlib(mipPixels.data(), mipPixels.size());
    uint64_t chunkDataOff = 24ULL + 24ULL + 24ULL;
    uint64_t nameTableOff = chunkDataOff + zMip.size();

    std::vector<uint8_t> ba2 = { 'B', 'T', 'D', 'X' };
    AppendU32LE(ba2, 1u);
    ba2.insert(ba2.end(), { 'D', 'X', '1', '0' });
    AppendU32LE(ba2, 1u); // 1 texture
    AppendU64LE(ba2, nameTableOff);

    // 24B Texture header
    AppendU32LE(ba2, 0xABCDE001u);
    ba2.insert(ba2.end(), { 'd', 'd', 's', 0 });
    AppendU32LE(ba2, 0x12345678u);
    ba2.push_back(0u);  // unk
    ba2.push_back(1u);  // numChunks = 1
    AppendU16LE(ba2, 24u);// chunkHdrSize = 24
    AppendU16LE(ba2, h);
    AppendU16LE(ba2, w);
    ba2.push_back(1u);  // numMips = 1
    ba2.push_back(28u); // DXGI_FORMAT_R8G8B8A8_UNORM = 28
    AppendU16LE(ba2, 8u); // tileMode

    // 24B Chunk record
    AppendU64LE(ba2, chunkDataOff);
    AppendU32LE(ba2, static_cast<uint32_t>(zMip.size()));
    AppendU32LE(ba2, static_cast<uint32_t>(mipPixels.size()));
    AppendU16LE(ba2, 0u); // startMip
    AppendU16LE(ba2, 0u); // endMip
    AppendU32LE(ba2, 0xBAADF00Du);

    ba2.insert(ba2.end(), zMip.begin(), zMip.end());
    AppendU16LE(ba2, static_cast<uint16_t>(texPath.size()));
    ba2.insert(ba2.end(), texPath.begin(), texPath.end());
    return ba2;
}

// ============================================================================
// 7. RedHat .rpm (Lead + 8B-Aligned Signature + Metadata Tags + CPIO) Builder
// ============================================================================
std::vector<uint8_t> BuildNewcCpio(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> cpio;
    auto pushEntry = [&](const std::string& name, uint32_t mode, const std::string& body, uint32_t ino) {
        char hdr[111] = {};
        snprintf(hdr, sizeof(hdr),
            "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
            ino, mode, 0u, 0u, 1u, 0u,
            static_cast<unsigned>(body.size()), 0u, 0u, 0u, 0u,
            static_cast<unsigned>(name.size() + 1), 0u);
        cpio.insert(cpio.end(), hdr, hdr + 110);
        cpio.insert(cpio.end(), name.begin(), name.end());
        cpio.push_back(0);
        while (cpio.size() % 4 != 0) cpio.push_back(0);
        cpio.insert(cpio.end(), body.begin(), body.end());
        while (cpio.size() % 4 != 0) cpio.push_back(0);
    };
    for (size_t i = 0; i < files.size(); ++i) {
        pushEntry("./" + files[i].first, 0100644u, files[i].second, static_cast<uint32_t>(100 + i));
    }
    pushEntry("TRAILER!!!", 0, "", 0);
    return cpio;
}

std::vector<uint8_t> BuildRedHatRpm(
    const std::string& pkgName,
    const std::vector<std::pair<std::string, std::string>>& files,
    bool useXzPayload)
{
    std::vector<uint8_t> rpm(96, 0);
    rpm[0] = 0xEDu; rpm[1] = 0xABu; rpm[2] = 0xEEu; rpm[3] = 0xDBu;
    rpm[4] = 3; rpm[5] = 0; // RPM v3.0
    std::memcpy(rpm.data() + 10, pkgName.data(), std::min<size_t>(pkgName.size(), 60));
    rpm[78] = 0x00; rpm[79] = 0x05; // Signature type = RPMSIGTYPE_HEADERSIG (5)

    // Signature Header (1 dummy tag so size is non-8B-aligned 5 bytes -> tests 8-byte alignment padding!)
    std::vector<uint8_t> sigHdr = { 0x8Eu, 0xADu, 0xE8u, 0x01u, 0,0,0,0 };
    AppendU32BE(sigHdr, 1u); // nindex = 1
    AppendU32BE(sigHdr, 5u); // hsize = 5 (requires 3 padding bytes to 8-byte boundary!)
    AppendU32BE(sigHdr, 1000u); AppendU32BE(sigHdr, 7u); AppendU32BE(sigHdr, 0u); AppendU32BE(sigHdr, 5u);
    sigHdr.insert(sigHdr.end(), { 1, 2, 3, 4, 5 });
    while (sigHdr.size() % 8 != 0) sigHdr.push_back(0); // Pad signature header to 8B boundary!
    rpm.insert(rpm.end(), sigHdr.begin(), sigHdr.end());

    // Prepare Metadata Header store & index entries (DIRNAMES, BASENAMES, DIRINDEXES, FILESIZES, FILEMODES, PAYLOADCOMPRESSOR)
    std::vector<std::string> dirNames;
    std::vector<std::string> baseNames;
    std::vector<uint32_t> dirIndexes;
    std::vector<uint32_t> fileSizes;
    std::vector<uint16_t> fileModes;

    for (const auto& kv : files) {
        size_t sl = kv.first.find_last_of('/');
        std::string dir = (sl == std::string::npos) ? "/" : ("/" + kv.first.substr(0, sl + 1));
        std::string base = (sl == std::string::npos) ? kv.first : kv.first.substr(sl + 1);
        auto it = std::find(dirNames.begin(), dirNames.end(), dir);
        uint32_t dIdx = 0;
        if (it == dirNames.end()) {
            dIdx = static_cast<uint32_t>(dirNames.size());
            dirNames.push_back(dir);
        } else {
            dIdx = static_cast<uint32_t>(it - dirNames.begin());
        }
        dirIndexes.push_back(dIdx);
        baseNames.push_back(base);
        fileSizes.push_back(static_cast<uint32_t>(kv.second.size()));
        fileModes.push_back(0100644u);
    }

    struct RpmIdx { uint32_t tag, type, off, count; };
    std::vector<RpmIdx> idxList;
    std::vector<uint8_t> store;
    auto alignStore = [&](size_t a) { while (store.size() % a != 0) store.push_back(0); };

    // 1097 = FILESIZES (INT32 = 4)
    alignStore(4);
    idxList.push_back({ 1028u, 4u, static_cast<uint32_t>(store.size()), static_cast<uint32_t>(fileSizes.size()) });
    for (uint32_t sz : fileSizes) AppendU32BE(store, sz);

    // 1030 = FILEMODES (INT16 = 3)
    alignStore(2);
    idxList.push_back({ 1030u, 3u, static_cast<uint32_t>(store.size()), static_cast<uint32_t>(fileModes.size()) });
    for (uint16_t m : fileModes) AppendU16BE(store, m);

    // 1116 = DIRINDEXES (INT32 = 4)
    alignStore(4);
    idxList.push_back({ 1116u, 4u, static_cast<uint32_t>(store.size()), static_cast<uint32_t>(dirIndexes.size()) });
    for (uint32_t di : dirIndexes) AppendU32BE(store, di);

    // 1117 = BASENAMES (STRING_ARRAY = 8)
    idxList.push_back({ 1117u, 8u, static_cast<uint32_t>(store.size()), static_cast<uint32_t>(baseNames.size()) });
    for (const auto& s : baseNames) { store.insert(store.end(), s.begin(), s.end()); store.push_back(0); }

    // 1118 = DIRNAMES (STRING_ARRAY = 8)
    idxList.push_back({ 1118u, 8u, static_cast<uint32_t>(store.size()), static_cast<uint32_t>(dirNames.size()) });
    for (const auto& s : dirNames) { store.insert(store.end(), s.begin(), s.end()); store.push_back(0); }

    // 1125 = PAYLOADCOMPRESSOR (STRING = 6)
    std::string compName = useXzPayload ? "xz" : "gzip";
    idxList.push_back({ 1125u, 6u, static_cast<uint32_t>(store.size()), 1u });
    store.insert(store.end(), compName.begin(), compName.end());
    store.push_back(0);

    std::vector<uint8_t> metaHdr = { 0x8Eu, 0xADu, 0xE8u, 0x01u, 0,0,0,0 };
    AppendU32BE(metaHdr, static_cast<uint32_t>(idxList.size()));
    AppendU32BE(metaHdr, static_cast<uint32_t>(store.size()));
    for (const auto& ix : idxList) {
        AppendU32BE(metaHdr, ix.tag);
        AppendU32BE(metaHdr, ix.type);
        AppendU32BE(metaHdr, ix.off);
        AppendU32BE(metaHdr, ix.count);
    }
    metaHdr.insert(metaHdr.end(), store.begin(), store.end());
    rpm.insert(rpm.end(), metaHdr.begin(), metaHdr.end());

    // Compressed CPIO payload immediately follows Metadata Header!
    auto cpio = BuildNewcCpio(files);
    auto packedPayload = useXzPayload
        ? BuildXzStream(cpio.data(), cpio.size(), false)
        : CompressGzip(cpio.data(), cpio.size());
    rpm.insert(rpm.end(), packedPayload.begin(), packedPayload.end());
    return rpm;
}

// ============================================================================
// 8. Windows Installer .msi (OLE2 Compound File + MSI 6-Bit Mangling + Embedded CAB)
// ============================================================================
std::wstring EncodeMsiMangledName(const std::string& plain) {
    auto toSixBit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
        if (c >= 'a' && c <= 'z') return 36 + (c - 'a');
        if (c == '.') return 62;
        if (c == '_') return 63;
        return -1;
    };
    std::wstring out;
    size_t i = 0;
    if (!plain.empty() && plain[0] == '!') {
        out.push_back(static_cast<wchar_t>(0x4840u));
        i = 1;
    }
    while (i < plain.size()) {
        int c0 = toSixBit(plain[i]);
        if (c0 >= 0 && i + 1 < plain.size()) {
            int c1 = toSixBit(plain[i + 1]);
            if (c1 >= 0) {
                out.push_back(static_cast<wchar_t>(0x3800u + c0 + (c1 << 6)));
                i += 2;
                continue;
            }
        }
        if (c0 >= 0) {
            out.push_back(static_cast<wchar_t>(0x4800u + c0));
        } else {
            out.push_back(static_cast<wchar_t>(static_cast<uint8_t>(plain[i])));
        }
        ++i;
    }
    return out;
}

std::vector<uint8_t> BuildOle2MsiPackage(
    const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& streams)
{
    // Sector layout (512B v3 sectors):
    // Sector 0: Directory sector (up to 4 entries; we allocate enough directory sectors for Root + streams)
    // Next sectors: Regular stream sectors (we set miniStreamCutoff = 0 or pad streams >= 4096 / set miniCutoff=1 in header?
    // Note: Ole2Compound.h line 171: `out.miniStreamCutoff = (miniCutoff > 0) ? miniCutoff : 4096;` — setting miniCutoff = 1 in header makes every stream >= 1 byte use regular 512B FAT sectors!)
    const size_t numEntries = 1 + streams.size(); // Root + streams
    const size_t dirSectors = (numEntries + 3) / 4;

    std::vector<uint32_t> streamStartSec(streams.size(), 0xFFFFFFFEu);
    std::vector<uint8_t> sectorData(dirSectors * 512, 0);
    std::vector<uint32_t> fat;

    // Chain directory sectors
    for (size_t d = 0; d < dirSectors; ++d) {
        fat.push_back((d + 1 < dirSectors) ? static_cast<uint32_t>(d + 1) : 0xFFFFFFFEu);
    }

    // Append each stream's sectors
    for (size_t s = 0; s < streams.size(); ++s) {
        const auto& bytes = streams[s].second;
        if (bytes.empty()) continue;
        uint32_t firstSec = static_cast<uint32_t>(fat.size());
        streamStartSec[s] = firstSec;
        size_t nSecs = (bytes.size() + 511) / 512;
        for (size_t k = 0; k < nSecs; ++k) {
            fat.push_back((k + 1 < nSecs) ? static_cast<uint32_t>(firstSec + k + 1) : 0xFFFFFFFEu);
        }
        size_t oldSz = sectorData.size();
        sectorData.resize(oldSz + nSecs * 512, 0);
        std::memcpy(sectorData.data() + oldSz, bytes.data(), bytes.size());
    }

    // Add 1 FAT sector at the end
    uint32_t fatSecIdx = static_cast<uint32_t>(fat.size());
    fat.push_back(0xFFFFFFFDu); // FATSECT
    while (fat.size() < 128) fat.push_back(0xFFFFFFFFu); // FREESECT
    size_t fatByteOff = sectorData.size();
    sectorData.resize(fatByteOff + 512, 0xFFu);
    for (size_t i = 0; i < 128; ++i) {
        PutU32LE(sectorData.data() + fatByteOff + i * 4, fat[i]);
    }

    // Populate Directory Entries (128 bytes each):
    // Entry 0 = Root Entry (childId = 1), Entry 1..N = right-leaning chain (rightSibling = i + 1)
    auto writeDirEntry = [&](size_t idx, const std::wstring& wname, uint8_t objType,
                             uint32_t leftSib, uint32_t rightSib, uint32_t childId,
                             uint32_t startSec, uint64_t streamSz)
    {
        uint8_t* de = sectorData.data() + idx * 128;
        std::memset(de, 0, 128);
        size_t wlen = std::min<size_t>(wname.size(), 31);
        for (size_t k = 0; k < wlen; ++k) PutU16LE(de + k * 2, static_cast<uint16_t>(wname[k]));
        PutU16LE(de + 64, static_cast<uint16_t>((wlen + 1) * 2));
        de[66] = objType;
        de[67] = 1; // black node
        PutU32LE(de + 68, leftSib);
        PutU32LE(de + 72, rightSib);
        PutU32LE(de + 76, childId);
        PutU32LE(de + 116, startSec);
        PutU64LE(de + 120, streamSz);
    };

    writeDirEntry(0, L"Root Entry", 5, 0xFFFFFFFFu, 0xFFFFFFFFu,
                  streams.empty() ? 0xFFFFFFFFu : 1u, 0xFFFFFFFEu, 0);
    for (size_t s = 0; s < streams.size(); ++s) {
        uint32_t rightSib = (s + 1 < streams.size()) ? static_cast<uint32_t>(s + 2) : 0xFFFFFFFFu;
        writeDirEntry(1 + s, streams[s].first, 2, 0xFFFFFFFFu, rightSib, 0xFFFFFFFFu,
                      streamStartSec[s], streams[s].second.size());
    }

    // 512-byte OLE2 Header
    std::vector<uint8_t> ole(512, 0);
    static const uint8_t kSig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(ole.data(), kSig, 8);
    PutU16LE(ole.data() + 24, 0x003Eu); // minorVersion
    PutU16LE(ole.data() + 26, 0x0003u); // majorVersion = 3 (512B sectors)
    PutU16LE(ole.data() + 28, 0xFFFEu); // byteOrder
    PutU16LE(ole.data() + 30, 9u);      // sectorShift = 9 (512)
    PutU16LE(ole.data() + 32, 6u);      // miniSectorShift = 6 (64)
    PutU32LE(ole.data() + 44, 1u);      // numFatSectors = 1
    PutU32LE(ole.data() + 48, 0u);      // firstDirSector = 0
    PutU32LE(ole.data() + 56, 1u);      // miniStreamCutoff = 1 (all non-empty streams use regular sectors!)
    PutU32LE(ole.data() + 60, 0xFFFFFFFEu); // firstMiniFatSector
    PutU32LE(ole.data() + 64, 0u);      // numMiniFatSectors
    PutU32LE(ole.data() + 68, 0xFFFFFFFEu); // firstDifatSector
    PutU32LE(ole.data() + 72, 0u);      // numDifatSectors
    std::memset(ole.data() + 76, 0xFF, 436);
    PutU32LE(ole.data() + 76, fatSecIdx); // DIFAT[0] = fatSecIdx

    ole.insert(ole.end(), sectorData.begin(), sectorData.end());
    return ole;
}

// ============================================================================
// 9. MBR + FAT16 Filesystem + Fixed & Dynamic VHD / QCOW2 / VDI / VMDK / WIM Builders
// ============================================================================
struct FatTestFile {
    std::string shortDir83;  // Empty if in root, else 11-char 8.3 dir name (e.g. "DOCS       ")
    std::wstring lfnName;    // VFAT Long File Name (if non-empty, emits LFN entries!)
    std::string shortName83; // 11-char 8.3 name (e.g. "README  TXT")
    std::string content;
};

// Builds a raw disk image with an MBR partition at LBA 1 containing a formatted FAT16 volume!
std::vector<uint8_t> BuildMbrFat16Disk(
    const std::string& volumeLabel11,
    const std::string& subDirName,
    const std::string& subDir83,
    const std::vector<FatTestFile>& files)
{
    const uint32_t totalSectors = 512; // 256 KB raw disk
    const uint32_t partStartLba = 1;
    const uint32_t partSectors  = totalSectors - partStartLba;
    std::vector<uint8_t> disk(totalSectors * 512, 0);

    // 1. MBR at LBA 0
    uint8_t* mbr = disk.data();
    mbr[446 + 0] = 0x80u; // active
    mbr[446 + 4] = 0x06u; // FAT16 partition type
    PutU32LE(mbr + 446 + 8, partStartLba);
    PutU32LE(mbr + 446 + 12, partSectors);
    mbr[510] = 0x55u;
    mbr[511] = 0xAAu;

    // 2. FAT16 Boot Sector / BPB at LBA 1 (offset 512)
    // Layout inside partition:
    //   Reserved = 1 sector (LBA 0 of partition)
    //   numFATs = 2, fatSz16 = 2 sectors each -> FAT1 @ relSec 1..2, FAT2 @ relSec 3..4
    //   rootEntCnt = 32 (2 sectors -> relSec 5..6)
    //   dataStart = relSec 7, secPerClus = 1 (512B clusters, cluster 2 = relSec 7!)
    uint8_t* bpb = disk.data() + partStartLba * 512;
    bpb[0] = 0xEBu; bpb[1] = 0x3Cu; bpb[2] = 0x90u;
    std::memcpy(bpb + 3, "MSWIN4.1", 8);
    PutU16LE(bpb + 11, 512u); // bytesPerSec
    bpb[13] = 1u;             // secPerClus = 1
    PutU16LE(bpb + 14, 1u);   // rsvdSecCnt = 1
    bpb[16] = 2u;             // numFATs = 2
    PutU16LE(bpb + 17, 32u);  // rootEntCnt = 32
    PutU16LE(bpb + 19, static_cast<uint16_t>(partSectors));
    bpb[21] = 0xF8u;          // media
    PutU16LE(bpb + 22, 2u);   // fatSz16 = 2
    PutU16LE(bpb + 24, 32u);
    PutU16LE(bpb + 26, 64u);
    PutU32LE(bpb + 28, partStartLba);
    bpb[38] = 0x29u;
    std::memcpy(bpb + 43, volumeLabel11.data(), std::min<size_t>(11, volumeLabel11.size()));
    std::memcpy(bpb + 54, "FAT16   ", 8);
    bpb[510] = 0x55u;
    bpb[511] = 0xAAu;

    uint8_t* fat1 = bpb + 1 * 512;
    uint8_t* fat2 = bpb + 3 * 512;
    uint8_t* rootDir = bpb + 5 * 512;
    uint8_t* dataArea = bpb + 7 * 512;

    auto setFat16 = [&](uint16_t clus, uint16_t val) {
        PutU16LE(fat1 + clus * 2, val);
        PutU16LE(fat2 + clus * 2, val);
    };
    setFat16(0, 0xFFF8u);
    setFat16(1, 0xFFFFu);

    uint16_t nextFreeCluster = 2;
    auto allocBytes = [&](const uint8_t* src, size_t n) -> uint16_t {
        if (n == 0) return 0;
        uint16_t first = nextFreeCluster;
        size_t nClus = (n + 511) / 512;
        for (size_t k = 0; k < nClus; ++k) {
            uint16_t c = nextFreeCluster++;
            size_t chunk = std::min<size_t>(512, n - k * 512);
            std::memcpy(dataArea + (c - 2) * 512, src + k * 512, chunk);
            setFat16(c, (k + 1 < nClus) ? static_cast<uint16_t>(c + 1) : 0xFFFFu);
        }
        return first;
    };

    auto calc83Chk = [](const char name83[11]) -> uint8_t {
        uint8_t sum = 0;
        for (int i = 0; i < 11; ++i) {
            sum = static_cast<uint8_t>(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + static_cast<uint8_t>(name83[i]));
        }
        return sum;
    };

    auto appendDirEntry = [&](std::vector<uint8_t>& dirBuf, const std::wstring& lfn, const std::string& s83,
                              uint8_t attr, uint16_t firstClus, uint32_t fileSz)
    {
        char pad83[11];
        std::memset(pad83, ' ', 11);
        std::memcpy(pad83, s83.data(), std::min<size_t>(11, s83.size()));
        if (!lfn.empty()) {
            uint8_t chk = calc83Chk(pad83);
            size_t numSlots = (lfn.size() + 12) / 13;
            for (size_t s = numSlots; s >= 1; --s) {
                uint8_t slot[32];
                std::memset(slot, 0xFF, 32);
                slot[0] = static_cast<uint8_t>(s | ((s == numSlots) ? 0x40u : 0u));
                slot[11] = 0x0Fu; // ATTR_LONG_NAME
                slot[12] = 0u;
                slot[13] = chk;
                slot[26] = 0u;
                slot[27] = 0u;
                static const int kOffs[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };
                size_t base = (s - 1) * 13;
                for (int k = 0; k < 13; ++k) {
                    size_t ci = base + k;
                    uint16_t wc = (ci < lfn.size()) ? static_cast<uint16_t>(lfn[ci])
                                : ((ci == lfn.size()) ? 0x0000u : 0xFFFFu);
                    PutU16LE(slot + kOffs[k], wc);
                }
                dirBuf.insert(dirBuf.end(), slot, slot + 32);
            }
        }
        uint8_t de[32] = {};
        std::memcpy(de, pad83, 11);
        de[11] = attr;
        PutU16LE(de + 22, 0x6000u);
        PutU16LE(de + 24, 0x596Fu);
        PutU16LE(de + 26, firstClus);
        PutU32LE(de + 28, fileSz);
        dirBuf.insert(dirBuf.end(), de, de + 32);
    };

    std::vector<uint8_t> rootEntries;
    std::vector<uint8_t> subEntries;

    uint16_t subDirClus = 0;
    if (!subDirName.empty()) {
        subDirClus = nextFreeCluster++;
        setFat16(subDirClus, 0xFFFFu);
        appendDirEntry(subEntries, L"", ".          ", 0x10u, subDirClus, 0);
        appendDirEntry(subEntries, L"", "..         ", 0x10u, 0, 0);
        appendDirEntry(rootEntries, PathUtils::Utf8ToWide(subDirName), subDir83, 0x10u, subDirClus, 0);
    }

    for (const auto& f : files) {
        uint16_t clus = allocBytes(reinterpret_cast<const uint8_t*>(f.content.data()), f.content.size());
        if (!f.shortDir83.empty() && subDirClus != 0) {
            appendDirEntry(subEntries, f.lfnName, f.shortName83, 0x20u, clus, static_cast<uint32_t>(f.content.size()));
        } else {
            appendDirEntry(rootEntries, f.lfnName, f.shortName83, 0x20u, clus, static_cast<uint32_t>(f.content.size()));
        }
    }

    std::memcpy(rootDir, rootEntries.data(), std::min<size_t>(rootEntries.size(), 1024));
    if (subDirClus != 0 && !subEntries.empty()) {
        std::memcpy(dataArea + (subDirClus - 2) * 512, subEntries.data(), std::min<size_t>(subEntries.size(), 512));
    }
    return disk;
}

std::vector<uint8_t> WrapAsFixedVhd(const std::vector<uint8_t>& rawDisk) {
    std::vector<uint8_t> vhd = rawDisk;
    uint8_t ftr[512] = {};
    std::memcpy(ftr, "conectix", 8);
    PutU32BE(ftr + 8, 2u);
    PutU32BE(ftr + 12, 0x00010000u);
    PutU64BE(ftr + 16, 0xFFFFFFFFFFFFFFFFULL); // dataOffset = -1 for Fixed VHD
    PutU64BE(ftr + 40, rawDisk.size());
    PutU64BE(ftr + 48, rawDisk.size());
    PutU32BE(ftr + 60, 2u); // diskType = 2 (Fixed)
    vhd.insert(vhd.end(), ftr, ftr + 512);
    return vhd;
}

std::vector<uint8_t> WrapAsDynamicVhd(const std::vector<uint8_t>& rawDisk) {
    // Dynamic VHD: 512B Header copy + 1024B Dynamic Header ("cxsparse") @ 512 + 512B BAT @ 1536 + Data Block + 512B Footer
    const uint32_t blockSize = 512 * 512; // 256 KB block (512 sectors -> 64-byte sector bitmap = 1 bitmap sector!)
    uint8_t ftr[512] = {};
    std::memcpy(ftr, "conectix", 8);
    PutU32BE(ftr + 8, 2u);
    PutU32BE(ftr + 12, 0x00010000u);
    PutU64BE(ftr + 16, 512ULL); // dataOffset = 512 -> cxsparse
    PutU64BE(ftr + 40, rawDisk.size());
    PutU64BE(ftr + 48, rawDisk.size());
    PutU32BE(ftr + 60, 3u); // diskType = 3 (Dynamic)

    uint8_t dyn[1024] = {};
    std::memcpy(dyn, "cxsparse", 8);
    PutU64BE(dyn + 8, 0xFFFFFFFFFFFFFFFFULL);
    PutU64BE(dyn + 16, 1536ULL); // tableOffset (BAT) = 1536 (sector 3)
    PutU32BE(dyn + 24, 0x00010000u);
    PutU32BE(dyn + 28, 1u); // maxTableEntries = 1
    PutU32BE(dyn + 32, blockSize);

    uint8_t bat[512];
    std::memset(bat, 0xFF, 512);
    PutU32BE(bat, 4u); // Block 0 starts at sector 4 (offset 2048): 1 sector bitmap + 512 sectors data!

    uint8_t bitmapSec[512];
    std::memset(bitmapSec, 0xFF, 512);

    std::vector<uint8_t> vhd;
    vhd.insert(vhd.end(), ftr, ftr + 512);
    vhd.insert(vhd.end(), dyn, dyn + 1024);
    vhd.insert(vhd.end(), bat, bat + 512);
    vhd.insert(vhd.end(), bitmapSec, bitmapSec + 512);
    std::vector<uint8_t> padded = rawDisk;
    padded.resize(blockSize, 0);
    vhd.insert(vhd.end(), padded.begin(), padded.end());
    vhd.insert(vhd.end(), ftr, ftr + 512);
    return vhd;
}

std::vector<uint8_t> WrapAsQcow2(const std::vector<uint8_t>& rawDisk) {
    // Cluster 0: Header, Cluster 1: L1 table, Cluster 2: L2 table, Cluster 3+: Data clusters (clusterBits=12 => 4096B)
    const size_t clusSz = 4096;
    const size_t nClus = (rawDisk.size() + clusSz - 1) / clusSz;
    std::vector<uint8_t> q(clusSz * (3 + nClus), 0);
    q[0] = 'Q'; q[1] = 'F'; q[2] = 'I'; q[3] = 0xFBu;
    PutU32BE(q.data() + 4, 3u);      // version 3
    PutU32BE(q.data() + 20, 12u);    // cluster_bits = 12 (4096B)
    PutU64BE(q.data() + 24, rawDisk.size());
    PutU32BE(q.data() + 36, 1u);     // l1_size = 1
    PutU64BE(q.data() + 40, clusSz); // l1_table_offset = 4096
    PutU64BE(q.data() + (clusSz), (2ULL * clusSz) | (1ULL << 63)); // L1[0] -> L2 @ cluster 2
    for (size_t c = 0; c < nClus; ++c) {
        uint64_t hostOff = (3ULL + c) * clusSz;
        PutU64BE(q.data() + 2 * clusSz + c * 8, hostOff | (1ULL << 63));
        size_t copyLen = std::min<size_t>(clusSz, rawDisk.size() - c * clusSz);
        std::memcpy(q.data() + static_cast<size_t>(hostOff), rawDisk.data() + c * clusSz, copyLen);
    }
    return q;
}

// Builds a real Windows Imaging (.wim "MSWIM\0\0\0") container with UTF-16LE XML manifest,
// uncompressed rhOffsetTable (50-byte SHA-1 resource entries), and 102-byte DIRENTRY tree!
std::vector<uint8_t> BuildWindowsWim(const std::wstring& imageName, const std::wstring& fileName, const std::string& payload) {
    std::vector<uint8_t> wim(208, 0);
    std::memcpy(wim.data(), "MSWIM\0\0\0", 8);
    PutU32LE(wim.data() + 8, 208u);   // cbSize
    PutU32LE(wim.data() + 12, 0x00010D00u); // dwVersion
    PutU16LE(wim.data() + 40, 1u);    // usPartNumber = 1
    PutU16LE(wim.data() + 42, 1u);    // usTotalParts = 1
    PutU32LE(wim.data() + 44, 1u);    // dwImageCount = 1

    auto writeResHdr = [](uint8_t* dst, uint64_t compSz, uint8_t flags, uint64_t off, uint64_t origSz) {
        uint64_t szAndFlags = (compSz & 0x00FFFFFFFFFFFFFFULL) | (static_cast<uint64_t>(flags) << 56);
        PutU64LE(dst + 0, szAndFlags);
        PutU64LE(dst + 8, off);
        PutU64LE(dst + 16, origSz);
    };

    // 1. File payload resource @ 208
    uint8_t fileHash[20] = {};
    for (int i = 0; i < 20; ++i) fileHash[i] = static_cast<uint8_t>(0xA0 + i + payload.size());
    uint64_t fileOff = wim.size();
    wim.insert(wim.end(), payload.begin(), payload.end());

    // 2. Metadata resource (8-byte SECURITYBLOCK {totalLen=8, numEntries=0} + Root DIRENTRY + Child file DIRENTRY + end marker)
    std::vector<uint8_t> meta(8, 0);
    PutU32LE(meta.data(), 8u); // totalLength = 8 => aligned to 8 bytes -> root DIRENTRY at offset 8!

    auto appendDirEntry = [&](uint32_t attr, uint64_t subOff, const uint8_t* hash20, const std::wstring& name) {
        size_t nameBytes = name.size() * 2;
        size_t recLen = (102 + nameBytes + 2 + 7) & ~static_cast<size_t>(7);
        size_t start = meta.size();
        meta.resize(start + recLen, 0);
        PutU64LE(meta.data() + start + 0, recLen);
        PutU32LE(meta.data() + start + 8, attr);
        PutU32LE(meta.data() + start + 12, 0xFFFFFFFFu); // securityId = -1
        PutU64LE(meta.data() + start + 16, subOff);
        if (hash20) std::memcpy(meta.data() + start + 64, hash20, 20);
        PutU16LE(meta.data() + start + 100, static_cast<uint16_t>(nameBytes));
        for (size_t i = 0; i < name.size(); ++i) {
            PutU16LE(meta.data() + start + 102 + i * 2, static_cast<uint16_t>(name[i]));
        }
    };

    // Root directory @ offset 8: 102+2 aligned to 8 = 104 bytes -> children start at 8 + 104 + 8 (terminator) = 120!
    appendDirEntry(0x10u, 120ULL, nullptr, L"");
    meta.resize(meta.size() + 8, 0); // End of root level (liLength == 0)
    // Child @ offset 120:
    appendDirEntry(0x20u, 0ULL, fileHash, fileName);
    meta.resize(meta.size() + 8, 0); // End of children

    uint8_t metaHash[20] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20 };
    uint64_t metaOff = wim.size();
    wim.insert(wim.end(), meta.begin(), meta.end());

    // 3. Lookup Table (rhOffsetTable): two 50-byte _RESHDR_TABLE_ENTRY records (24B RESHDR + 2B part + 4B refCount + 20B SHA-1)
    uint64_t lutOff = wim.size();
    std::vector<uint8_t> lut(100, 0);
    writeResHdr(lut.data() + 0, meta.size(), 0x02u, metaOff, meta.size()); // 0x02 = RESHDR_FLAG_METADATA
    PutU16LE(lut.data() + 24, 1u);
    PutU32LE(lut.data() + 26, 1u);
    std::memcpy(lut.data() + 30, metaHash, 20);

    writeResHdr(lut.data() + 50, payload.size(), 0x00u, fileOff, payload.size());
    PutU16LE(lut.data() + 50 + 24, 1u);
    PutU32LE(lut.data() + 50 + 26, 1u);
    std::memcpy(lut.data() + 50 + 30, fileHash, 20);
    wim.insert(wim.end(), lut.begin(), lut.end());

    // 4. UTF-16LE XML Data (rhXmlData)
    std::wstring xmlStr = L"\xFEFF<WIM><IMAGE INDEX=\"1\"><NAME>" + imageName + L"</NAME></IMAGE></WIM>";
    uint64_t xmlOff = wim.size();
    size_t xmlBytes = xmlStr.size() * 2;
    for (wchar_t wc : xmlStr) AppendU16LE(wim, static_cast<uint16_t>(wc));

    writeResHdr(wim.data() + 48, lut.size(), 0x02u, lutOff, lut.size()); // rhOffsetTable @ 48
    writeResHdr(wim.data() + 72, xmlBytes,   0x02u, xmlOff, xmlBytes);   // rhXmlData @ 72
    return wim;
}

// ============================================================================
// 9. UnityFS AssetBundle, Blizzard MPQ, RPG Maker RGSS, SquashFS 4.0 & EROFS Builders
// ============================================================================
std::vector<uint8_t> EncodeRawLz4Block(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> blk;
    size_t pos = 0;
    size_t anchor = 0;
    while (pos + 5 <= len) {
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 8192);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = (len - pos > 5) ? (len - pos - 5) : 0;
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) { bestLen = m; bestDist = d; if (bestLen >= 64) break; }
        }
        if (bestLen >= 4) {
            size_t litLen = pos - anchor;
            size_t mCode = bestLen - 4;
            uint8_t tok = static_cast<uint8_t>((std::min<size_t>(litLen, 15) << 4) | std::min<size_t>(mCode, 15));
            blk.push_back(tok);
            if (litLen >= 15) {
                size_t rem = litLen - 15;
                while (rem >= 255) { blk.push_back(255); rem -= 255; }
                blk.push_back(static_cast<uint8_t>(rem));
            }
            blk.insert(blk.end(), raw + anchor, raw + pos);
            AppendU16LE(blk, static_cast<uint16_t>(bestDist));
            if (mCode >= 15) {
                size_t rem = mCode - 15;
                while (rem >= 255) { blk.push_back(255); rem -= 255; }
                blk.push_back(static_cast<uint8_t>(rem));
            }
            pos += bestLen;
            anchor = pos;
        } else {
            ++pos;
        }
    }
    size_t tailLit = len - anchor;
    uint8_t tok = static_cast<uint8_t>(std::min<size_t>(tailLit, 15) << 4);
    blk.push_back(tok);
    if (tailLit >= 15) {
        size_t rem = tailLit - 15;
        while (rem >= 255) { blk.push_back(255); rem -= 255; }
        blk.push_back(static_cast<uint8_t>(rem));
    }
    blk.insert(blk.end(), raw + anchor, raw + len);
    return blk;
}

std::vector<uint8_t> BuildUnityFsBundle(
    uint32_t fmtVer,
    const std::string& engineVer,
    size_t maxBlockUncomp,
    const std::vector<std::pair<std::string, std::string>>& files)
{
    std::vector<uint8_t> uncompAll;
    for (const auto& kv : files) {
        uncompAll.insert(uncompAll.end(), kv.second.begin(), kv.second.end());
    }

    struct BlkRec { uint32_t uSz; uint32_t cSz; uint16_t flags; std::vector<uint8_t> comp; };
    std::vector<BlkRec> blks;
    for (size_t off = 0; off < uncompAll.size(); off += maxBlockUncomp) {
        size_t chunk = std::min<size_t>(maxBlockUncomp, uncompAll.size() - off);
        auto c = EncodeRawLz4Block(uncompAll.data() + off, chunk);
        blks.push_back({ static_cast<uint32_t>(chunk), static_cast<uint32_t>(c.size()), 0x0003u, std::move(c) });
    }

    std::vector<uint8_t> infoRaw(16, 0); // 16-byte uncompressedDataHash
    AppendU32BE(infoRaw, static_cast<uint32_t>(blks.size()));
    for (const auto& b : blks) {
        AppendU32BE(infoRaw, b.uSz);
        AppendU32BE(infoRaw, b.cSz);
        AppendU16BE(infoRaw, b.flags);
    }
    AppendU32BE(infoRaw, static_cast<uint32_t>(files.size()));
    uint64_t nOff = 0;
    for (const auto& kv : files) {
        AppendU64BE(infoRaw, nOff);
        AppendU64BE(infoRaw, kv.second.size());
        AppendU32BE(infoRaw, 0x00000004u);
        infoRaw.insert(infoRaw.end(), kv.first.begin(), kv.first.end());
        infoRaw.push_back(0);
        nOff += kv.second.size();
    }

    auto infoComp = EncodeRawLz4Block(infoRaw.data(), infoRaw.size());

    std::vector<uint8_t> out = { 'U','n','i','t','y','F','S', 0 };
    AppendU32BE(out, fmtVer);
    const char* pVer = "5.x.x";
    out.insert(out.end(), pVer, pVer + 6);
    out.insert(out.end(), engineVer.begin(), engineVer.end());
    out.push_back(0);

    size_t bundleSzPos = out.size();
    AppendU64BE(out, 0ULL); // patched below
    AppendU32BE(out, static_cast<uint32_t>(infoComp.size()));
    AppendU32BE(out, static_cast<uint32_t>(infoRaw.size()));
    uint32_t flags = 0x00000043u | ((fmtVer >= 7) ? 0x00000200u : 0u); // LZ4HC blocksInfo + combined dir
    AppendU32BE(out, flags);

    if (fmtVer >= 7) {
        while ((out.size() & 15u) != 0) out.push_back(0);
    }
    out.insert(out.end(), infoComp.begin(), infoComp.end());
    if ((flags & 0x200u) != 0) {
        while ((out.size() & 15u) != 0) out.push_back(0);
    }
    for (const auto& b : blks) {
        out.insert(out.end(), b.comp.begin(), b.comp.end());
    }
    PutU64BE(out.data() + bundleSzPos, out.size());
    return out;
}

std::vector<uint8_t> BuildBlizzardMpqTestArchive(
    const std::string& block0FileBody,
    const std::string& block1FileBody)
{
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
    auto mpqHash = [&](const std::string& s, uint32_t hashType) -> uint32_t {
        uint32_t s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
        for (char rc : s) {
            char c = (rc == '/') ? '\\' : rc;
            uint8_t ch = static_cast<uint8_t>(toupper(static_cast<unsigned char>(c)));
            s1 = cryptTbl[hashType + ch] ^ (s1 + s2);
            s2 = ch + s1 + s2 + (s2 << 5) + 3u;
        }
        return s1;
    };
    auto mpqEncryptBytes = [&](uint8_t* buf, size_t byteLen, uint32_t k1) {
        uint32_t k2 = 0xEEEEEEEEu;
        size_t dwords = byteLen / 4u;
        for (size_t i = 0; i < dwords; ++i) {
            k2 += cryptTbl[0x400u + (k1 & 0xFFu)];
            uint32_t plain = static_cast<uint32_t>(buf[i * 4u + 0]) |
                             (static_cast<uint32_t>(buf[i * 4u + 1]) << 8) |
                             (static_cast<uint32_t>(buf[i * 4u + 2]) << 16) |
                             (static_cast<uint32_t>(buf[i * 4u + 3]) << 24);
            uint32_t enc = plain ^ (k1 + k2);
            k1 = ((~k1 << 0x15) + 0x11111111u) | (k1 >> 0x0B);
            k2 = plain + k2 + (k2 << 5) + 3u;
            PutU32LE(buf + i * 4u, enc);
        }
    };

    const uint32_t sectorSize = 512u; // secShift = 0
    std::vector<uint8_t> mpq(32, 0);

    auto packMpqMultiSector = [&](const std::string& plainBaseName, const std::string& body, bool fixKey, uint32_t& outRelPos, uint32_t& outCSz, uint32_t& outFlags) {
        outRelPos = static_cast<uint32_t>(mpq.size());
        uint32_t fSz = static_cast<uint32_t>(body.size());
        uint32_t fileKey = mpqHash(plainBaseName, 0x300u);
        if (fixKey) fileKey = (fileKey + outRelPos) ^ fSz;

        uint32_t numSecs = (fSz + sectorSize - 1u) / sectorSize;
        size_t tblBytes = static_cast<size_t>(numSecs + 1u) * 4u;
        std::vector<uint32_t> secOffs(numSecs + 1u, 0);
        std::vector<std::vector<uint8_t>> encSectors;
        uint32_t curOff = static_cast<uint32_t>(tblBytes);
        for (uint32_t s = 0; s < numSecs; ++s) {
            secOffs[s] = curOff;
            size_t sPos = static_cast<size_t>(s) * sectorSize;
            size_t sLen = std::min<size_t>(sectorSize, fSz - sPos);
            auto z = CompressZlib(reinterpret_cast<const uint8_t*>(body.data() + sPos), sLen);
            std::vector<uint8_t> sec;
            if (1u + z.size() < sLen) {
                sec.push_back(0x02u); // MPQ_COMPRESSION_ZLIB
                sec.insert(sec.end(), z.begin(), z.end());
            } else {
                sec.assign(body.begin() + static_cast<ptrdiff_t>(sPos), body.begin() + static_cast<ptrdiff_t>(sPos + sLen));
            }
            mpqEncryptBytes(sec.data(), sec.size(), fileKey + s);
            curOff += static_cast<uint32_t>(sec.size());
            encSectors.push_back(std::move(sec));
        }
        secOffs[numSecs] = curOff;

        std::vector<uint8_t> tbl(tblBytes, 0);
        for (uint32_t s = 0; s <= numSecs; ++s) PutU32LE(tbl.data() + s * 4u, secOffs[s]);
        mpqEncryptBytes(tbl.data(), tbl.size(), fileKey - 1u);

        mpq.insert(mpq.end(), tbl.begin(), tbl.end());
        for (const auto& es : encSectors) mpq.insert(mpq.end(), es.begin(), es.end());
        outCSz = curOff;
        outFlags = 0x80000000u | 0x00000200u | 0x00010000u | (fixKey ? 0x00020000u : 0u);
    };

    // Block 0: "Units\\Knight.mdx", Block 1: "Scripts\\war3map.j", Block 2: "(listfile)"
    // Note (listfile) lists Scripts\war3map.j FIRST -> non-sequential block mapping (1 then 0)!
    uint32_t b0Pos = 0, b0CSz = 0, b0Flg = 0;
    uint32_t b1Pos = 0, b1CSz = 0, b1Flg = 0;
    uint32_t b2Pos = 0, b2CSz = 0, b2Flg = 0;
    std::string listfileText = "Scripts\\war3map.j\r\nUnits\\Knight.mdx\r\n";

    packMpqMultiSector("Knight.mdx", block0FileBody, true,  b0Pos, b0CSz, b0Flg);
    packMpqMultiSector("war3map.j",  block1FileBody, false, b1Pos, b1CSz, b1Flg);
    packMpqMultiSector("(listfile)", listfileText,   false, b2Pos, b2CSz, b2Flg);

    const uint32_t hashCount = 16u;
    std::vector<uint8_t> htBytes(hashCount * 16u, 0xFFu);
    auto insertHash = [&](const std::string& fullName, uint32_t blockIdx) {
        uint32_t start = mpqHash(fullName, 0x000u) % hashCount;
        uint32_t nA    = mpqHash(fullName, 0x100u);
        uint32_t nB    = mpqHash(fullName, 0x200u);
        for (uint32_t step = 0; step < hashCount; ++step) {
            uint32_t slot = (start + step) % hashCount;
            uint8_t* p = htBytes.data() + slot * 16u;
            if (p[12] == 0xFFu && p[13] == 0xFFu && p[14] == 0xFFu && p[15] == 0xFFu) {
                PutU32LE(p + 0, nA);
                PutU32LE(p + 4, nB);
                PutU32LE(p + 8, 0u);
                PutU32LE(p + 12, blockIdx);
                return;
            }
        }
    };
    insertHash("Units\\Knight.mdx", 0u);
    insertHash("Scripts\\war3map.j", 1u);
    insertHash("(listfile)", 2u);
    mpqEncryptBytes(htBytes.data(), htBytes.size(), mpqHash("(hash table)", 0x300u));

    std::vector<uint8_t> btBytes(3u * 16u, 0);
    auto putBlock = [&](uint32_t idx, uint32_t fPos, uint32_t cSz, uint32_t fSz, uint32_t flg) {
        uint8_t* p = btBytes.data() + idx * 16u;
        PutU32LE(p + 0, fPos);
        PutU32LE(p + 4, cSz);
        PutU32LE(p + 8, fSz);
        PutU32LE(p + 12, flg);
    };
    putBlock(0u, b0Pos, b0CSz, static_cast<uint32_t>(block0FileBody.size()), b0Flg);
    putBlock(1u, b1Pos, b1CSz, static_cast<uint32_t>(block1FileBody.size()), b1Flg);
    putBlock(2u, b2Pos, b2CSz, static_cast<uint32_t>(listfileText.size()),   b2Flg);
    mpqEncryptBytes(btBytes.data(), btBytes.size(), mpqHash("(block table)", 0x300u));

    uint32_t htPos = static_cast<uint32_t>(mpq.size());
    mpq.insert(mpq.end(), htBytes.begin(), htBytes.end());
    uint32_t btPos = static_cast<uint32_t>(mpq.size());
    mpq.insert(mpq.end(), btBytes.begin(), btBytes.end());

    std::memcpy(mpq.data() + 0, "MPQ\x1A", 4);
    PutU32LE(mpq.data() + 4,  32u);
    PutU32LE(mpq.data() + 8,  static_cast<uint32_t>(mpq.size()));
    PutU16LE(mpq.data() + 12, 0u); // v1
    PutU16LE(mpq.data() + 14, 0u); // sectorSize = 512
    PutU32LE(mpq.data() + 16, htPos);
    PutU32LE(mpq.data() + 20, btPos);
    PutU32LE(mpq.data() + 24, hashCount);
    PutU32LE(mpq.data() + 28, 3u);
    return mpq;
}

std::vector<uint8_t> BuildRpgMakerRgss3a(const std::vector<std::pair<std::string, std::string>>& files) {
    const uint32_t rawSeed = 0x13572468u;
    const uint32_t masterKey = rawSeed * 9u + 3u;
    std::vector<uint8_t> out = { 'R','G','S','S','A','D', 0, 3 };
    AppendU32LE(out, rawSeed);

    // Compute exact TOC byte size: each entry is 16 + nameLen, followed by 16-byte sentinel (fOff == 0)
    size_t tocEnd = 12 + 16;
    for (const auto& kv : files) tocEnd += 16 + kv.first.size();

    std::vector<uint8_t> payloads;
    for (size_t idx = 0; idx < files.size(); ++idx) {
        const auto& kv = files[idx];
        uint32_t fOff   = static_cast<uint32_t>(tocEnd + payloads.size());
        uint32_t fSize  = static_cast<uint32_t>(kv.second.size());
        uint32_t subKey = 0xA5A50001u + static_cast<uint32_t>(idx) * 0x11111111u;
        uint32_t nLen   = static_cast<uint32_t>(kv.first.size());

        AppendU32LE(out, fOff   ^ masterKey);
        AppendU32LE(out, fSize  ^ masterKey);
        AppendU32LE(out, subKey ^ masterKey);
        AppendU32LE(out, nLen   ^ masterKey);
        for (uint32_t i = 0; i < nLen; ++i) {
            out.push_back(static_cast<uint8_t>(kv.first[i] ^ ((masterKey >> (8u * (i & 3u))) & 0xFFu)));
        }

        uint32_t curKey = subKey;
        for (size_t i = 0; i < kv.second.size(); ++i) {
            payloads.push_back(static_cast<uint8_t>(static_cast<uint8_t>(kv.second[i]) ^ ((curKey >> (8u * (i & 3u))) & 0xFFu)));
            if ((i & 3u) == 3u) curKey = curKey * 7u + 3u;
        }
    }
    // End-of-TOC sentinel (fOff == 0 => encrypted = 0 ^ masterKey)
    AppendU32LE(out, 0u ^ masterKey);
    AppendU32LE(out, 0u ^ masterKey);
    AppendU32LE(out, 0u ^ masterKey);
    AppendU32LE(out, 0u ^ masterKey);
    out.insert(out.end(), payloads.begin(), payloads.end());
    return out;
}

std::vector<uint8_t> BuildRpgMakerRgssV1(const std::vector<std::pair<std::string, std::string>>& files) {
    std::vector<uint8_t> out = { 'R','G','S','S','A','D', 0, 1 };
    uint32_t key = 0xDEADCAFEu;
    for (const auto& kv : files) {
        uint32_t nLen = static_cast<uint32_t>(kv.first.size());
        AppendU32LE(out, nLen ^ key);
        key = key * 7u + 3u;
        for (uint32_t i = 0; i < nLen; ++i) {
            out.push_back(static_cast<uint8_t>(static_cast<uint8_t>(kv.first[i]) ^ (key & 0xFFu)));
            key = key * 7u + 3u;
        }
        uint32_t fSize = static_cast<uint32_t>(kv.second.size());
        AppendU32LE(out, fSize ^ key);
        key = key * 7u + 3u;

        uint32_t curKey = key;
        for (size_t i = 0; i < kv.second.size(); ++i) {
            out.push_back(static_cast<uint8_t>(static_cast<uint8_t>(kv.second[i]) ^ ((curKey >> (8u * (i & 3u))) & 0xFFu)));
            if ((i & 3u) == 3u) curKey = curKey * 7u + 3u;
        }
    }
    return out;
}

std::vector<uint8_t> BuildSquashFs4ZlibImage(const std::string& readmeBody, const std::string& etcConfBody) {
    auto zFile1 = CompressZlib(reinterpret_cast<const uint8_t*>(readmeBody.data()), readmeBody.size());
    auto zFile2 = CompressZlib(reinterpret_cast<const uint8_t*>(etcConfBody.data()), etcConfBody.size());
    const uint32_t dataOff1 = 96u;
    const uint32_t dataOff2 = 96u + static_cast<uint32_t>(zFile1.size());

    auto buildDirBlob = [](uint32_t baseIno, const std::vector<std::tuple<uint16_t, int16_t, uint16_t, std::string>>& ents) {
        std::vector<uint8_t> d;
        AppendU32LE(d, static_cast<uint32_t>(ents.size() - 1u));
        AppendU32LE(d, 0u); // start_block = 0
        AppendU32LE(d, baseIno);
        for (const auto& t : ents) {
            AppendU16LE(d, std::get<0>(t)); // inode inner offset
            AppendU16LE(d, static_cast<uint16_t>(std::get<1>(t)));
            AppendU16LE(d, std::get<2>(t)); // type (1=dir, 2=reg)
            const std::string& nm = std::get<3>(t);
            AppendU16LE(d, static_cast<uint16_t>(nm.size() - 1u));
            d.insert(d.end(), nm.begin(), nm.end());
        }
        return d;
    };

    // Inode offsets inside inodeMetaRaw: 0=rootDir(32B), 32=readme.txt(36B), 68=etcDir(32B), 100=config.ini(36B)
    auto rootDirRaw = buildDirBlob(2u, { { 68u, 1, 1u, "etc" }, { 32u, 0, 2u, "readme.txt" } });
    auto etcDirRaw  = buildDirBlob(4u, { { 100u, 0, 2u, "config.ini" } });

    std::vector<uint8_t> inodeRaw;
    auto pushDirInode = [&](uint32_t inoNum, uint16_t rawDirLen, uint16_t dirInnerOff, uint32_t parentIno) {
        size_t s = inodeRaw.size();
        inodeRaw.resize(s + 32, 0);
        PutU16LE(inodeRaw.data() + s + 0, 1u); // basic dir
        PutU16LE(inodeRaw.data() + s + 2, 0755u);
        PutU32LE(inodeRaw.data() + s + 12, inoNum);
        PutU32LE(inodeRaw.data() + s + 16, 0u); // start_block = 0
        PutU32LE(inodeRaw.data() + s + 20, 2u);
        PutU16LE(inodeRaw.data() + s + 24, static_cast<uint16_t>(rawDirLen + 3u));
        PutU16LE(inodeRaw.data() + s + 26, dirInnerOff);
        PutU32LE(inodeRaw.data() + s + 28, parentIno);
    };
    auto pushRegInode = [&](uint32_t inoNum, uint32_t blocksStart, uint32_t fSize, uint32_t zBlkSz) {
        size_t s = inodeRaw.size();
        inodeRaw.resize(s + 36, 0);
        PutU16LE(inodeRaw.data() + s + 0, 2u); // basic file
        PutU16LE(inodeRaw.data() + s + 2, 0644u);
        PutU32LE(inodeRaw.data() + s + 12, inoNum);
        PutU32LE(inodeRaw.data() + s + 16, blocksStart);
        PutU32LE(inodeRaw.data() + s + 20, 0xFFFFFFFFu); // no fragment
        PutU32LE(inodeRaw.data() + s + 24, 0u);
        PutU32LE(inodeRaw.data() + s + 28, fSize);
        PutU32LE(inodeRaw.data() + s + 32, zBlkSz); // compressed block size (bit 24 == 0)
    };

    pushDirInode(1u, static_cast<uint16_t>(rootDirRaw.size()), 0u, 1u);
    pushRegInode(2u, dataOff1, static_cast<uint32_t>(readmeBody.size()), static_cast<uint32_t>(zFile1.size()));
    pushDirInode(3u, static_cast<uint16_t>(etcDirRaw.size()), static_cast<uint16_t>(rootDirRaw.size()), 1u);
    pushRegInode(4u, dataOff2, static_cast<uint32_t>(etcConfBody.size()), static_cast<uint32_t>(zFile2.size()));

    std::vector<uint8_t> dirRaw = rootDirRaw;
    dirRaw.insert(dirRaw.end(), etcDirRaw.begin(), etcDirRaw.end());

    // Zlib-compressed metadata blocks: bit 15 (0x8000) is CLEAR!
    auto zInodeMeta = CompressZlib(inodeRaw.data(), inodeRaw.size());
    auto zDirMeta   = CompressZlib(dirRaw.data(), dirRaw.size());

    std::vector<uint8_t> img(96, 0);
    img.insert(img.end(), zFile1.begin(), zFile1.end());
    img.insert(img.end(), zFile2.begin(), zFile2.end());

    uint64_t inodeTblStart = img.size();
    AppendU16LE(img, static_cast<uint16_t>(zInodeMeta.size() & 0x7FFFu));
    img.insert(img.end(), zInodeMeta.begin(), zInodeMeta.end());

    uint64_t dirTblStart = img.size();
    AppendU16LE(img, static_cast<uint16_t>(zDirMeta.size() & 0x7FFFu));
    img.insert(img.end(), zDirMeta.begin(), zDirMeta.end());

    std::memcpy(img.data() + 0, "hsqs", 4);
    PutU32LE(img.data() + 4,  4u);     // inodes
    PutU32LE(img.data() + 12, 4096u);  // block_size
    PutU32LE(img.data() + 16, 0u);     // fragments
    PutU16LE(img.data() + 20, 1u);     // comp_id = 1 (zlib)
    PutU16LE(img.data() + 22, 12u);    // block_log = 12
    PutU16LE(img.data() + 28, 4u);     // s_major = 4
    PutU16LE(img.data() + 30, 0u);     // s_minor = 0
    PutU64LE(img.data() + 32, 0ULL);   // root_inode = (0 << 16) | 0
    PutU64LE(img.data() + 40, img.size());
    PutU64LE(img.data() + 48, UINT64_MAX);
    PutU64LE(img.data() + 56, UINT64_MAX);
    PutU64LE(img.data() + 64, inodeTblStart);
    PutU64LE(img.data() + 72, dirTblStart);
    PutU64LE(img.data() + 80, UINT64_MAX);
    PutU64LE(img.data() + 88, UINT64_MAX);
    return img;
}

std::vector<uint8_t> BuildErofsTestImage(const std::string& bootPlain, const std::string& inlineMultiBody) {
    // 5 x 4096B blocks = 20480 bytes (inlineMultiBody must be > 4096 and < 4300 bytes)
    const size_t blkSz = 4096u;
    std::vector<uint8_t> img(5u * blkSz, 0);

    // Superblock @ 1024
    uint8_t* sb = img.data() + 1024;
    PutU32LE(sb + 0,  0xE0F5E1E2u);
    sb[12] = 12u; // 4096B blocks
    PutU16LE(sb + 14, 0u); // root_nid = 0
    PutU64LE(sb + 16, 4ULL);
    PutU32LE(sb + 36, 5u); // 5 blocks
    PutU32LE(sb + 40, 1u); // meta_blkaddr = 1 (byte 4096)
    std::memcpy(sb + 64, "EROFS_STAGE15", 13);

    // NID 0 @ 4096 + 0: compact root dir (FLAT_PLAIN at block 2)
    uint8_t* n0 = img.data() + 4096 + 0 * 32;
    PutU16LE(n0 + 0, 0u);        // COMPACT | FLAT_PLAIN
    PutU16LE(n0 + 4, 0040755u);  // S_IFDIR
    PutU32LE(n0 + 8, 48u);       // 2 dirents + names
    PutU32LE(n0 + 16, 2u);       // raw_blkaddr = 2

    // Block 2 @ 8192: root dirents ("boot.cfg" -> nid 4, "etc" -> nid 2)
    uint8_t* rd = img.data() + 2 * blkSz;
    PutU64LE(rd + 0,  4ULL); PutU16LE(rd + 8,  24u); rd[10] = 1u;
    PutU64LE(rd + 12, 2ULL); PutU16LE(rd + 20, 32u); rd[22] = 2u;
    std::memcpy(rd + 24, "boot.cfg", 8);
    std::memcpy(rd + 32, "etc", 3);

    // NID 2 @ 4096 + 64: compact "etc" dir (FLAT_INLINE dirents right at +32!)
    uint8_t* n2 = img.data() + 4096 + 2 * 32;
    PutU16LE(n2 + 0, (2u << 1) | 0u); // COMPACT | FLAT_INLINE
    PutU16LE(n2 + 4, 0040755u);
    PutU32LE(n2 + 8, 23u); // 12B dirent + 11B "inline.conf"
    uint8_t* ed = n2 + 32;
    PutU64LE(ed + 0, 8ULL); PutU16LE(ed + 8, 12u); ed[10] = 1u;
    std::memcpy(ed + 12, "inline.conf", 11);

    // NID 4 @ 4096 + 128: compact "boot.cfg" reg file (FLAT_PLAIN at block 3)
    uint8_t* n4 = img.data() + 4096 + 4 * 32;
    PutU16LE(n4 + 0, 0u); // COMPACT | FLAT_PLAIN
    PutU16LE(n4 + 4, 0100644u);
    PutU32LE(n4 + 8, static_cast<uint32_t>(bootPlain.size()));
    PutU32LE(n4 + 16, 3u); // raw_blkaddr = 3
    std::memcpy(img.data() + 3 * blkSz, bootPlain.data(), bootPlain.size());

    // NID 8 @ 4096 + 256: extended 64B "etc/inline.conf" reg file (FLAT_INLINE: 4096B @ block 4 + tail inline @ +64!)
    uint8_t* n8 = img.data() + 4096 + 8 * 32;
    PutU16LE(n8 + 0, (2u << 1) | 1u); // EXTENDED | FLAT_INLINE
    PutU16LE(n8 + 4, 0100644u);
    PutU64LE(n8 + 8, inlineMultiBody.size());
    PutU32LE(n8 + 16, 4u); // raw_blkaddr = 4
    std::memcpy(img.data() + 4 * blkSz, inlineMultiBody.data(), blkSz);
    std::memcpy(n8 + 64, inlineMultiBody.data() + blkSz, inlineMultiBody.size() - blkSz);

    return img;
}

// ============================================================================
// 10. Item #20 Synthetic Builders:
//     .dmg (UDIF koly+mish Zlib/ADC/Zero + APM + HFS+ Catalog B-Tree),
//     .ext4 (Extent Tree + Legacy Indirect Blocks),
//     .wim (XPRESS Huffman Compressed Metadata & Multi-Chunk Stream),
//     .vpk (Multi-Chunk _dir.vpk + _000.vpk + Preload + CRC32),
//     .asar (Nested Recursive JSON + .asar.unpacked),
//     .big (EA BIGF + RefPack 0x10FB Compressed Stream)
// ============================================================================

std::string EncodeBase64ForDmg(const uint8_t* src, size_t len) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = static_cast<uint32_t>(src[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(src[i + 1]) << 8;
        if (i + 2 < len) v |= static_cast<uint32_t>(src[i + 2]);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back((i + 1 < len) ? kB64[(v >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kB64[v & 63] : '=');
    }
    return out;
}

// Encodes raw bytes with Apple ADC (0x80000004): exercises both 0x80|len literal runs and 2-byte LZ77 back-references!
std::vector<uint8_t> EncodeAppleAdcForDmg(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos < len) {
        // Look for an LZ77 back-reference of length 3..18 within distance 1..1024
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 1024);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(18, len - pos);
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) {
                bestLen = m;
                bestDist = d;
                if (bestLen == 18) break;
            }
        }
        if (bestLen >= 4) {
            uint32_t d0 = static_cast<uint32_t>(bestDist - 1);
            uint8_t op = static_cast<uint8_t>(((bestLen - 3) << 2) | ((d0 >> 8) & 0x03u));
            out.push_back(op);
            out.push_back(static_cast<uint8_t>(d0 & 0xFFu));
            pos += bestLen;
            continue;
        }
        // Emit literal run up to 64 bytes (stopping early if a 6+ byte repeat appears)
        size_t litEnd = pos + 1;
        while (litEnd < len && (litEnd - pos) < 64) {
            if (litEnd + 6 <= len && litEnd >= 4 && std::memcmp(raw + litEnd, raw + litEnd - 4, 6) == 0) break;
            ++litEnd;
        }
        size_t litLen = litEnd - pos;
        out.push_back(static_cast<uint8_t>(0x80u | (litLen - 1)));
        out.insert(out.end(), raw + pos, raw + litEnd);
        pos = litEnd;
    }
    return out;
}

std::vector<uint8_t> BuildDmgApmHfsPlusImage(
    const std::string& infoPlistBody,
    const std::string& multiExtentReadme)
{
    // Virtual disk: 48 sectors * 512B = 24576 bytes
    //   Sector 0 (0..511):      Apple Driver Descriptor Map ("ER", sbBlkSize=512)
    //   Sector 1 (512..1023):   APM Entry 1 ("PM", Apple_partition_map)
    //   Sector 2 (1024..1535):  APM Entry 2 ("PM", Apple_HFS @ sector 4, 44 sectors)
    //   Sector 4..47 (2048..):  HFS+ Volume (blockSize = 512, totalBlocks = 44)
    const uint32_t blkSz = 512u;
    std::vector<uint8_t> vdisk(48u * blkSz, 0);

    // Driver Descriptor Map @ 0
    vdisk[0] = 'E'; vdisk[1] = 'R';
    PutU16BE(vdisk.data() + 2, 512u);
    PutU32BE(vdisk.data() + 4, 48u);

    // APM Entry 1 @ 512
    uint8_t* pm1 = vdisk.data() + 512;
    pm1[0] = 'P'; pm1[1] = 'M';
    PutU32BE(pm1 + 4, 2u); // dpme_map_entries = 2
    PutU32BE(pm1 + 8, 1u);
    PutU32BE(pm1 + 12, 2u);
    std::memcpy(pm1 + 16, "Apple", 5);
    std::memcpy(pm1 + 48, "Apple_partition_map", 19);

    // APM Entry 2 @ 1024 -> HFS+ partition at sector 4 (byte 2048), 44 sectors
    uint8_t* pm2 = vdisk.data() + 1024;
    pm2[0] = 'P'; pm2[1] = 'M';
    PutU32BE(pm2 + 4, 2u);
    PutU32BE(pm2 + 8, 4u);  // dpme_pblock_start = 4
    PutU32BE(pm2 + 12, 44u); // dpme_pblocks = 44
    std::memcpy(pm2 + 16, "MacAppDisk", 10);
    std::memcpy(pm2 + 48, "Apple_HFS", 9);

    const size_t partBase = 4u * blkSz; // 2048

    // HFS+ Volume Header @ partBase + 1024
    uint8_t* vh = vdisk.data() + partBase + 1024;
    vh[0] = 'H'; vh[1] = '+';
    PutU16BE(vh + 2, 4u);    // version = 4
    PutU32BE(vh + 32, 2u);   // fileCount = 2
    PutU32BE(vh + 36, 2u);   // folderCount = 2
    PutU32BE(vh + 40, blkSz); // blockSize = 512
    PutU32BE(vh + 44, 44u);   // totalBlocks = 44

    // Catalog B-Tree ForkData @ vh + 272: 2048 bytes (4 blocks starting at HFS block 4)
    uint8_t* catFk = vh + 272;
    PutU64BE(catFk + 0, 2048ULL);
    PutU32BE(catFk + 8, 2048u);
    PutU32BE(catFk + 12, 4u);
    PutU32BE(catFk + 16, 4u); // extents[0].startBlock = 4
    PutU32BE(catFk + 20, 4u); // extents[0].blockCount = 4

    // Catalog Node 0 (Header Node, 1024B) @ partBase + 4 * 512
    uint8_t* node0 = vdisk.data() + partBase + 4u * blkSz;
    node0[8] = 1; // kBTHeaderNode
    PutU16BE(node0 + 10, 1u); // numRecords = 1
    // BTHeaderRec @ node0 + 14
    PutU16BE(node0 + 14 + 0,  1u);    // treeDepth = 1
    PutU32BE(node0 + 14 + 2,  1u);    // rootNode = 1
    PutU32BE(node0 + 14 + 6,  5u);    // leafRecords = 5
    PutU32BE(node0 + 14 + 10, 1u);    // firstLeafNode = 1
    PutU32BE(node0 + 14 + 14, 1u);    // lastLeafNode = 1
    PutU16BE(node0 + 14 + 18, 1024u); // nodeSize = 1024
    PutU32BE(node0 + 14 + 22, 2u);    // totalNodes = 2

    // Write file payloads into HFS+ blocks:
    //   File 1 (Info.plist): HFS blocks 10..11 (up to 1024B)
    std::memcpy(vdisk.data() + partBase + 10u * blkSz, infoPlistBody.data(), infoPlistBody.size());
    //   File 2 (ReadMe.txt): split across NON-CONTIGUOUS extents!
    //     Extent 0 = block 14 (1 block = 512B), Extent 1 = blocks 20..21 (1024B), with zero blocks 15..19 in between!
    size_t part0 = std::min<size_t>(blkSz, multiExtentReadme.size());
    std::memcpy(vdisk.data() + partBase + 14u * blkSz, multiExtentReadme.data(), part0);
    if (multiExtentReadme.size() > part0) {
        std::memcpy(vdisk.data() + partBase + 20u * blkSz,
                    multiExtentReadme.data() + part0,
                    multiExtentReadme.size() - part0);
    }

    // Catalog Node 1 (Leaf Node, 1024B) @ partBase + 6 * 512
    uint8_t* node1 = vdisk.data() + partBase + 6u * blkSz;
    PutU32BE(node1 + 0, 0u); // fLink = 0 (end of leaf chain)
    node1[8] = 0xFFu;        // kBTLeafNode (-1)
    node1[9] = 1u;           // height = 1
    std::vector<uint16_t> recOffsets;
    uint16_t curOff = 14u;

    auto writeCatKey = [&](uint32_t parentCnid, const std::string& asciiName) {
        recOffsets.push_back(curOff);
        uint16_t uLen = static_cast<uint16_t>(asciiName.size());
        uint16_t keyLen = static_cast<uint16_t>(6u + uLen * 2u);
        PutU16BE(node1 + curOff + 0, keyLen);
        PutU32BE(node1 + curOff + 2, parentCnid);
        PutU16BE(node1 + curOff + 6, uLen);
        for (uint16_t i = 0; i < uLen; ++i) {
            PutU16BE(node1 + curOff + 8 + i * 2u, static_cast<uint8_t>(asciiName[i]));
        }
        curOff = static_cast<uint16_t>(curOff + 2u + keyLen + (keyLen & 1u));
    };

    auto addCatFolder = [&](uint32_t parentCnid, const std::string& name, uint32_t folderCnid) {
        writeCatKey(parentCnid, name);
        PutU16BE(node1 + curOff + 0, 0x0001u); // kHFSPlusFolderRecord (88B)
        PutU32BE(node1 + curOff + 8, folderCnid);
        curOff = static_cast<uint16_t>(curOff + 88u);
    };

    auto addCatFile = [&](uint32_t parentCnid, const std::string& name, uint32_t fileCnid,
                          uint64_t logSz, uint32_t totBlks,
                          uint32_t e0Start, uint32_t e0Cnt,
                          uint32_t e1Start, uint32_t e1Cnt)
    {
        writeCatKey(parentCnid, name);
        PutU16BE(node1 + curOff + 0, 0x0002u); // kHFSPlusFileRecord (248B)
        PutU32BE(node1 + curOff + 8, fileCnid);
        uint8_t* df = node1 + curOff + 88u;
        PutU64BE(df + 0, logSz);
        PutU32BE(df + 12, totBlks);
        PutU32BE(df + 16, e0Start); PutU32BE(df + 20, e0Cnt);
        PutU32BE(df + 24, e1Start); PutU32BE(df + 28, e1Cnt);
        curOff = static_cast<uint16_t>(curOff + 248u);
    };

    addCatFolder(1u,  "MacAppDisk", 2u);  // Root volume folder CNID = 2
    addCatFolder(2u,  "App.app",    16u);
    addCatFolder(16u, "Contents",   17u);
    addCatFile(17u, "Info.plist", 18u, infoPlistBody.size(), 2u, 10u, 2u, 0u, 0u);
    addCatFile(2u,  "ReadMe.txt", 19u, multiExtentReadme.size(), 3u, 14u, 1u, 20u, 2u);

    PutU16BE(node1 + 10, static_cast<uint16_t>(recOffsets.size()));
    recOffsets.push_back(curOff); // Free-space offset sentinel
    for (size_t r = 0; r < recOffsets.size(); ++r) {
        PutU16BE(node1 + 1024u - 2u * (r + 1u), recOffsets[r]);
    }

    // Wrap vdisk (48 sectors) into UDIF .dmg with Zlib + Apple ADC + Zero-Fill runs!
    //   Sectors 0..17  (9216B): Zlib (0x80000005)
    //   Sectors 18..18 (512B, HFS block 14): Apple ADC (0x80000004)
    //   Sectors 19..23 (2560B, HFS blocks 15..19): Zero fill (0x00000000)
    //   Sectors 24..47 (12288B): Zlib (0x80000005)
    auto zRun0  = CompressZlib(vdisk.data() + 0,          18u * 512u);
    auto adcRun = EncodeAppleAdcForDmg(vdisk.data() + 18u * 512u, 1u * 512u);
    auto zRun3  = CompressZlib(vdisk.data() + 24u * 512u, 24u * 512u);

    std::vector<uint8_t> dmg;
    uint64_t offRun0 = dmg.size(); dmg.insert(dmg.end(), zRun0.begin(), zRun0.end());
    uint64_t offAdc  = dmg.size(); dmg.insert(dmg.end(), adcRun.begin(), adcRun.end());
    uint64_t offRun3 = dmg.size(); dmg.insert(dmg.end(), zRun3.begin(), zRun3.end());
    uint64_t dataForkLen = dmg.size();

    // Build mish block (204B header + 5 * 40B runs)
    std::vector<uint8_t> mish(204, 0);
    std::memcpy(mish.data(), "mish", 4);
    PutU32BE(mish.data() + 4, 1u);
    PutU64BE(mish.data() + 8, 0ULL);  // firstSectorNumber = 0
    PutU64BE(mish.data() + 16, 48ULL); // sectorCount = 48
    PutU32BE(mish.data() + 200, 5u);   // 5 blkx run records

    auto pushMishRun = [&](uint32_t rType, uint64_t secStart, uint64_t secCnt, uint64_t compOff, uint64_t compLen) {
        size_t s = mish.size();
        mish.resize(s + 40, 0);
        PutU32BE(mish.data() + s + 0,  rType);
        PutU64BE(mish.data() + s + 8,  secStart);
        PutU64BE(mish.data() + s + 16, secCnt);
        PutU64BE(mish.data() + s + 24, compOff);
        PutU64BE(mish.data() + s + 32, compLen);
    };
    pushMishRun(0x80000005u, 0ULL,  18ULL, offRun0, zRun0.size());
    pushMishRun(0x80000004u, 18ULL, 1ULL,  offAdc,  adcRun.size());
    pushMishRun(0x00000000u, 19ULL, 5ULL,  0ULL,    0ULL);
    pushMishRun(0x80000005u, 24ULL, 24ULL, offRun3, zRun3.size());
    pushMishRun(0xFFFFFFFFu, 48ULL, 0ULL,  0ULL,    0ULL);

    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>"
                      "<key>resource-fork</key><dict><key>blkx</key><array><dict><key>Data</key><data>" +
                      EncodeBase64ForDmg(mish.data(), mish.size()) +
                      "</data></dict></array></dict></dict></plist>\n";

    uint64_t xmlOffset = dmg.size();
    dmg.insert(dmg.end(), xml.begin(), xml.end());
    uint64_t xmlLength = xml.size();

    // 512-byte koly trailer
    size_t kolyPos = dmg.size();
    dmg.resize(kolyPos + 512, 0);
    uint8_t* koly = dmg.data() + kolyPos;
    std::memcpy(koly, "koly", 4);
    PutU32BE(koly + 4, 4u);
    PutU32BE(koly + 8, 512u);
    PutU64BE(koly + 24, 0ULL);        // DataForkOffset
    PutU64BE(koly + 32, dataForkLen); // DataForkLength
    PutU64BE(koly + 216, xmlOffset);  // XMLOffset
    PutU64BE(koly + 224, xmlLength);  // XMLLength
    PutU64BE(koly + 492, 48ULL);      // SectorCount
    return dmg;
}

std::vector<uint8_t> BuildExt4ExtentAndIndirectImage(
    const std::string& etcAppConf2500,
    const std::string& legacyIndirect13500)
{
    // 40 blocks * 1024B = 40960 bytes
    //   Block 0: boot pad
    //   Block 1: Superblock (1024..2047)
    //   Block 2: 64-byte Group Descriptor Table (2048..3071)
    //   Blocks 3..7: Inode Table (20 inodes * 256B = 5120B = 5 blocks)
    //   Block 8: Root dir (inode 2)
    //   Block 9: Empty lost+found dir (inode 11)
    //   Block 10: /etc dir (inode 12)
    //   Block 11: ext4 extent tree index leaf block (eh_depth=0) for inode 13
    //   Blocks 12..13 + Block 15: data blocks for /etc/app.conf (2500B across 2 extents!)
    //   Blocks 16..27: 12 direct blocks for legacy_indirect.bin (inode 14)
    //   Block 28: single-indirect u32 block table -> points to Blocks 29..30!
    const uint32_t blkSz = 1024u;
    const uint16_t inoSz = 256u;
    std::vector<uint8_t> img(40u * blkSz, 0);

    uint8_t* sb = img.data() + 1024;
    PutU32LE(sb + 0,  20u);     // s_inodes_count
    PutU32LE(sb + 4,  40u);     // s_blocks_count_lo
    PutU32LE(sb + 20, 1u);      // s_first_data_block
    PutU32LE(sb + 24, 0u);      // s_log_block_size = 0 (1024B)
    PutU32LE(sb + 32, 8192u);   // s_blocks_per_group
    PutU32LE(sb + 40, 20u);     // s_inodes_per_group
    PutU16LE(sb + 56, 0xEF53u); // EXT_SUPER_MAGIC
    PutU32LE(sb + 76, 1u);      // s_rev_level = 1
    PutU16LE(sb + 88, inoSz);   // s_inode_size = 256
    PutU32LE(sb + 96, 0x02u | 0x40u | 0x80u); // FILETYPE | EXTENTS | 64BIT
    PutU16LE(sb + 254, 64u);    // s_desc_size = 64
    std::memcpy(sb + 120, "EXT4_STAGE15", 12);

    // Group Descriptor 0 @ Block 2 (2048): bg_inode_table_lo = 3
    uint8_t* gd0 = img.data() + 2u * blkSz;
    PutU32LE(gd0 + 8, 3u);

    auto inoPtr = [&](uint32_t inoNum) -> uint8_t* {
        return img.data() + 3u * blkSz + static_cast<size_t>(inoNum - 1u) * inoSz;
    };

    auto writeDirBlock = [&](uint32_t blk, const std::vector<std::tuple<uint32_t, uint8_t, std::string>>& entries) {
        uint8_t* p = img.data() + blk * blkSz;
        size_t off = 0;
        for (size_t i = 0; i < entries.size(); ++i) {
            uint32_t dIno = std::get<0>(entries[i]);
            uint8_t  fTyp = std::get<1>(entries[i]);
            const std::string& nm = std::get<2>(entries[i]);
            uint16_t ideal = static_cast<uint16_t>((8u + nm.size() + 3u) & ~3u);
            uint16_t rLen  = (i + 1 == entries.size()) ? static_cast<uint16_t>(blkSz - off) : ideal;
            PutU32LE(p + off + 0, dIno);
            PutU16LE(p + off + 4, rLen);
            p[off + 6] = static_cast<uint8_t>(nm.size());
            p[off + 7] = fTyp;
            std::memcpy(p + off + 8, nm.data(), nm.size());
            off += rLen;
        }
    };

    // Inode 2: Root directory @ Block 8
    {
        uint8_t* ino = inoPtr(2u);
        PutU16LE(ino + 0, 0x41EDu); // S_IFDIR | 0755
        PutU32LE(ino + 4, blkSz);
        PutU32LE(ino + 40, 8u);     // direct block 0 = 8
        writeDirBlock(8u, {
            { 2u,  2u, "." },
            { 2u,  2u, ".." },
            { 11u, 2u, "lost+found" },
            { 12u, 2u, "etc" },
            { 14u, 1u, "legacy_indirect.bin" }
        });
    }

    // Inode 11: Empty lost+found @ Block 9 (only "." and ".." -> should be skipped!)
    {
        uint8_t* ino = inoPtr(11u);
        PutU16LE(ino + 0, 0x41C0u);
        PutU32LE(ino + 4, blkSz);
        PutU32LE(ino + 40, 9u);
        writeDirBlock(9u, { { 11u, 2u, "." }, { 2u, 2u, ".." } });
    }

    // Inode 12: /etc directory (using inline ext4 extent eh_depth=0 -> Block 10)
    {
        uint8_t* ino = inoPtr(12u);
        PutU16LE(ino + 0, 0x41EDu);
        PutU32LE(ino + 4, blkSz);
        PutU32LE(ino + 32, 0x00080000u); // EXT4_EXTENTS_FL
        PutU16LE(ino + 40, 0xF30Au);
        PutU16LE(ino + 42, 1u); // eh_entries = 1
        PutU16LE(ino + 44, 4u); // eh_max = 4
        PutU16LE(ino + 46, 0u); // eh_depth = 0
        PutU32LE(ino + 52, 0u); // ee_block = 0
        PutU16LE(ino + 56, 1u); // ee_len = 1
        PutU32LE(ino + 60, 10u); // ee_start_lo = 10
        writeDirBlock(10u, {
            { 12u, 2u, "." },
            { 2u,  2u, ".." },
            { 13u, 1u, "app.conf" }
        });
    }

    // Inode 13: /etc/app.conf (2500B, eh_depth=1 index node -> Block 11 leaf extent node -> Blocks 12..13 + Block 15)
    {
        uint8_t* ino = inoPtr(13u);
        PutU16LE(ino + 0, 0x81A4u); // S_IFREG | 0644
        PutU32LE(ino + 4, static_cast<uint32_t>(etcAppConf2500.size()));
        PutU32LE(ino + 32, 0x00080000u); // EXT4_EXTENTS_FL
        // Root index header (eh_depth = 1 -> ei_leaf_lo = 11)
        PutU16LE(ino + 40, 0xF30Au);
        PutU16LE(ino + 42, 1u); // 1 index entry
        PutU16LE(ino + 44, 4u);
        PutU16LE(ino + 46, 1u); // eh_depth = 1!
        PutU32LE(ino + 52, 0u); // ei_block = 0
        PutU32LE(ino + 56, 11u); // ei_leaf_lo = 11

        // Leaf extent block @ Block 11: 2 non-contiguous extents covering 3 blocks (2048B + 452B = 2500B)
        uint8_t* leaf = img.data() + 11u * blkSz;
        PutU16LE(leaf + 0, 0xF30Au);
        PutU16LE(leaf + 2, 2u); // 2 extents
        PutU16LE(leaf + 4, 80u);
        PutU16LE(leaf + 6, 0u); // eh_depth = 0
        // Extent 0: logical 0..1 -> physical 12..13
        PutU32LE(leaf + 12, 0u); PutU16LE(leaf + 16, 2u); PutU32LE(leaf + 20, 12u);
        // Extent 1: logical 2 -> physical 15
        PutU32LE(leaf + 24, 2u); PutU16LE(leaf + 28, 1u); PutU32LE(leaf + 32, 15u);

        std::memcpy(img.data() + 12u * blkSz, etcAppConf2500.data(), 2u * blkSz);
        std::memcpy(img.data() + 15u * blkSz, etcAppConf2500.data() + 2u * blkSz, etcAppConf2500.size() - 2u * blkSz);
    }

    // Inode 14: /legacy_indirect.bin (13500B -> 12 direct blocks 16..27 + single-indirect block 28 -> blocks 29..30)
    {
        uint8_t* ino = inoPtr(14u);
        PutU16LE(ino + 0, 0x81A4u);
        PutU32LE(ino + 4, static_cast<uint32_t>(legacyIndirect13500.size()));
        for (uint32_t b = 0; b < 12u; ++b) {
            PutU32LE(ino + 40 + b * 4u, 16u + b);
            std::memcpy(img.data() + (16u + b) * blkSz, legacyIndirect13500.data() + b * blkSz, blkSz);
        }
        PutU32LE(ino + 40 + 12u * 4u, 28u); // i_block[12] = single indirect block 28
        uint8_t* ind = img.data() + 28u * blkSz;
        PutU32LE(ind + 0, 29u);
        PutU32LE(ind + 4, 30u);
        std::memcpy(img.data() + 29u * blkSz, legacyIndirect13500.data() + 12u * blkSz, blkSz);
        std::memcpy(img.data() + 30u * blkSz, legacyIndirect13500.data() + 13u * blkSz,
                    legacyIndirect13500.size() - 13u * blkSz);
    }

    return img;
}

// Exact [MS-XCA] XPRESS Huffman Chunk Compressor (used for WIM compressed metadata & file chunks <= 2048B)
std::vector<uint8_t> CompressXpressHuffmanChunkForTest(const uint8_t* raw, size_t len) {
    struct XcaToken {
        uint16_t sym = 0;
        int offsetBits = 0;
        uint32_t lowOff = 0;
        uint32_t matchLenMinus3 = 0;
    };
    std::vector<XcaToken> toks;
    uint32_t freq[512] = {};

    size_t pos = 0;
    while (pos < len) {
        size_t bestLen = 0, bestOff = 0;
        size_t maxBack = std::min<size_t>(pos, 8192);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(260, len - pos);
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) {
                bestLen = m;
                bestOff = d;
                if (bestLen >= 64) break;
            }
        }
        if (bestLen >= 4) {
            int ob = 0;
            while ((static_cast<size_t>(1) << (ob + 1)) <= bestOff) ++ob;
            uint32_t low = static_cast<uint32_t>(bestOff - (static_cast<size_t>(1) << ob));
            uint32_t ml3 = static_cast<uint32_t>(bestLen - 3);
            uint16_t lc  = static_cast<uint16_t>(std::min<uint32_t>(ml3, 15u));
            uint16_t sym = static_cast<uint16_t>(256u + (ob << 4) + lc);
            toks.push_back({ sym, ob, low, ml3 });
            ++freq[sym];
            pos += bestLen;
        } else {
            uint16_t sym = raw[pos++];
            toks.push_back({ sym, 0, 0, 0 });
            ++freq[sym];
        }
    }
    // Add EOF symbol 256 so tree has >= 2 symbols and clean termination
    ++freq[256];

    // Build Huffman code lengths via min-priority merge (for chunk <= 2048B, max depth is guaranteed <= 14!)
    struct HNode { uint32_t weight; int left; int right; int sym; };
    std::vector<HNode> nodes;
    std::vector<int> active;
    for (int s = 0; s < 512; ++s) {
        if (freq[s] > 0) {
            active.push_back(static_cast<int>(nodes.size()));
            nodes.push_back({ freq[s], -1, -1, s });
        }
    }
    if (active.size() == 1) {
        int alt = (nodes[0].sym == 0) ? 1 : 0;
        active.push_back(static_cast<int>(nodes.size()));
        nodes.push_back({ 1u, -1, -1, alt });
    }
    while (active.size() > 1) {
        std::sort(active.begin(), active.end(), [&](int a, int b) {
            return nodes[a].weight > nodes[b].weight;
        });
        int n1 = active.back(); active.pop_back();
        int n2 = active.back(); active.pop_back();
        int parent = static_cast<int>(nodes.size());
        nodes.push_back({ nodes[n1].weight + nodes[n2].weight, n1, n2, -1 });
        active.push_back(parent);
    }

    uint8_t lens[512] = {};
    auto assignDepth = [&](auto& self, int idx, uint8_t d) -> void {
        if (nodes[idx].sym >= 0) {
            lens[nodes[idx].sym] = d;
            return;
        }
        self(self, nodes[idx].left, static_cast<uint8_t>(d + 1));
        self(self, nodes[idx].right, static_cast<uint8_t>(d + 1));
    };
    assignDepth(assignDepth, active[0], 0);

    // Compute canonical MSB codes matching [MS-XCA]
    uint32_t blCount[16] = {};
    for (int s = 0; s < 512; ++s) if (lens[s] > 0) ++blCount[lens[s]];
    uint32_t nextCode[16] = {};
    uint32_t code = 0;
    for (int b = 1; b <= 15; ++b) {
        code = (code + blCount[b - 1]) << 1;
        nextCode[b] = code;
    }
    uint32_t codes[512] = {};
    for (int s = 0; s < 512; ++s) {
        if (lens[s] > 0) codes[s] = nextCode[lens[s]]++;
    }

    // Emit 256-byte nibble length table + initial two U16LE words at 256 and 258
    std::vector<uint8_t> out(260, 0);
    for (size_t i = 0; i < 256; ++i) {
        out[i] = static_cast<uint8_t>((lens[2 * i] & 0x0Fu) | ((lens[2 * i + 1] & 0x0Fu) << 4));
    }

    std::vector<size_t> wordSlots = { 256u, 258u };
    size_t bitPos = 0;
    int extraBits = 16;

    auto emitBits = [&](uint32_t val, int n) {
        if (n <= 0) return;
        for (int i = n - 1; i >= 0; --i) {
            uint32_t bit = (val >> i) & 1u;
            size_t wIdx  = bitPos / 16u;
            int bitInW   = 15 - static_cast<int>(bitPos % 16u);
            size_t wOff  = wordSlots[wIdx];
            uint16_t cur = static_cast<uint16_t>(out[wOff] | (static_cast<uint16_t>(out[wOff + 1]) << 8));
            cur = static_cast<uint16_t>(cur | (bit << bitInW));
            PutU16LE(out.data() + wOff, cur);
            ++bitPos;
        }
        extraBits -= n;
        if (extraBits < 0) {
            wordSlots.push_back(out.size());
            out.push_back(0);
            out.push_back(0);
            extraBits += 16;
        }
    };

    for (const auto& t : toks) {
        emitBits(codes[t.sym], lens[t.sym]);
        if (t.sym >= 256u) {
            if ((t.sym & 0x0Fu) == 15u) {
                uint32_t rem = t.matchLenMinus3 - 15u;
                if (rem < 255u) {
                    out.push_back(static_cast<uint8_t>(rem));
                } else {
                    out.push_back(255u);
                    AppendU16LE(out, static_cast<uint16_t>(t.matchLenMinus3));
                }
            }
            emitBits(t.lowOff, t.offsetBits);
        }
    }
    emitBits(codes[256], lens[256]);
    return out;
}

std::vector<uint8_t> CompressWimResourceXpress(const uint8_t* raw, size_t origLen, uint32_t chunkSize) {
    size_t numChunks = (origLen + chunkSize - 1u) / chunkSize;
    std::vector<std::vector<uint8_t>> chunks(numChunks);
    for (size_t c = 0; c < numChunks; ++c) {
        size_t off  = c * chunkSize;
        size_t clen = std::min<size_t>(chunkSize, origLen - off);
        auto comp   = CompressXpressHuffmanChunkForTest(raw + off, clen);
        if (comp.size() >= clen) {
            chunks[c].assign(raw + off, raw + off + clen); // Stored uncompressed when comp >= uncomp!
        } else {
            chunks[c] = std::move(comp);
        }
    }
    std::vector<uint8_t> res;
    uint32_t runningOff = 0;
    for (size_t c = 0; c + 1 < numChunks; ++c) {
        runningOff += static_cast<uint32_t>(chunks[c].size());
        AppendU32LE(res, runningOff); // (numChunks - 1) u32 chunk end/start offsets
    }
    for (const auto& ch : chunks) {
        res.insert(res.end(), ch.begin(), ch.end());
    }
    return res;
}

std::vector<uint8_t> BuildXpressWindowsWim(
    const std::wstring& imageName,
    const std::wstring& subDirName,
    const std::wstring& fileName,
    const std::string& payload)
{
    const uint32_t kChunkSz = 1024u;
    std::vector<uint8_t> wim(208, 0);
    std::memcpy(wim.data(), "MSWIM\0\0\0", 8);
    PutU32LE(wim.data() + 8,  208u);
    PutU32LE(wim.data() + 12, 0x00010D00u);
    PutU32LE(wim.data() + 16, 0x00000002u | 0x00020000u); // FLAG_HEADER_COMPRESSION | FLAG_HEADER_COMPRESS_XPRESS
    PutU32LE(wim.data() + 20, kChunkSz);                  // dwCompressionSize = 1024
    PutU16LE(wim.data() + 40, 1u);
    PutU16LE(wim.data() + 42, 1u);
    PutU32LE(wim.data() + 44, 1u);

    auto writeResHdr = [](uint8_t* dst, uint64_t compSz, uint8_t flags, uint64_t off, uint64_t origSz) {
        uint64_t szAndFlags = (compSz & 0x00FFFFFFFFFFFFFFULL) | (static_cast<uint64_t>(flags) << 56);
        PutU64LE(dst + 0, szAndFlags);
        PutU64LE(dst + 8, off);
        PutU64LE(dst + 16, origSz);
    };

    // 1. Multi-chunk XPRESS-compressed file stream
    uint8_t fileHash[20] = {};
    for (int i = 0; i < 20; ++i) fileHash[i] = static_cast<uint8_t>(0x33u + i * 7u);
    auto compFile = CompressWimResourceXpress(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), kChunkSz);
    uint64_t fileOff = wim.size();
    wim.insert(wim.end(), compFile.begin(), compFile.end());

    // 2. Build uncompressed metadata tree (padded with compressible security descriptor so XPRESS shrinks it < 1024B)
    std::vector<uint8_t> meta(64, 0); // 64-byte SECURITYBLOCK (aligned to 8)
    PutU32LE(meta.data() + 0, 64u);
    auto appendDe = [&](uint32_t attr, uint64_t subOff, const uint8_t* hash20, const std::wstring& nm) -> size_t {
        size_t nb = nm.size() * 2;
        size_t rl = (102 + nb + 2 + 7) & ~static_cast<size_t>(7);
        size_t st = meta.size();
        meta.resize(st + rl, 0);
        PutU64LE(meta.data() + st + 0, rl);
        PutU32LE(meta.data() + st + 8, attr);
        PutU32LE(meta.data() + st + 12, 0xFFFFFFFFu);
        PutU64LE(meta.data() + st + 16, subOff);
        if (hash20) std::memcpy(meta.data() + st + 64, hash20, 20);
        PutU16LE(meta.data() + st + 100, static_cast<uint16_t>(nb));
        for (size_t i = 0; i < nm.size(); ++i) PutU16LE(meta.data() + st + 102 + i * 2, static_cast<uint16_t>(nm[i]));
        return st;
    };
    size_t rootDe = appendDe(0x10u, 0ULL, nullptr, L"");
    meta.resize(meta.size() + 8, 0);
    size_t subDirOff = meta.size();
    PutU64LE(meta.data() + rootDe + 16, subDirOff);
    size_t subDe = appendDe(0x10u, 0ULL, nullptr, subDirName);
    meta.resize(meta.size() + 8, 0);
    size_t leafOff = meta.size();
    PutU64LE(meta.data() + subDe + 16, leafOff);
    appendDe(0x20u, 0ULL, fileHash, fileName);
    meta.resize(meta.size() + 512, 0); // Repeating zero tail ensures XPRESS compresses metadata well below raw size!

    uint8_t metaHash[20] = { 9,8,7,6,5,4,3,2,1,11,12,13,14,15,16,17,18,19,20,21 };
    auto compMeta = CompressWimResourceXpress(meta.data(), meta.size(), kChunkSz);
    uint64_t metaOff = wim.size();
    wim.insert(wim.end(), compMeta.begin(), compMeta.end());

    // 3. Lookup Table (50B entries: metadata flags=0x06 [METADATA|COMPRESSED], file flags=0x04 [COMPRESSED])
    uint64_t lutOff = wim.size();
    std::vector<uint8_t> lut(100, 0);
    writeResHdr(lut.data() + 0,  compMeta.size(), 0x06u, metaOff, meta.size());
    PutU16LE(lut.data() + 24, 1u); PutU32LE(lut.data() + 26, 1u);
    std::memcpy(lut.data() + 30, metaHash, 20);

    writeResHdr(lut.data() + 50, compFile.size(), 0x04u, fileOff, payload.size());
    PutU16LE(lut.data() + 50 + 24, 1u); PutU32LE(lut.data() + 50 + 26, 1u);
    std::memcpy(lut.data() + 50 + 30, fileHash, 20);
    wim.insert(wim.end(), lut.begin(), lut.end());

    // 4. UTF-16LE XML Manifest
    std::wstring xmlStr = L"\xFEFF<WIM><IMAGE INDEX=\"1\"><NAME>" + imageName + L"</NAME></IMAGE></WIM>";
    uint64_t xmlOff = wim.size();
    for (wchar_t wc : xmlStr) AppendU16LE(wim, static_cast<uint16_t>(wc));
    size_t xmlBytes = xmlStr.size() * 2;

    writeResHdr(wim.data() + 48, lut.size(), 0x02u, lutOff, lut.size());
    writeResHdr(wim.data() + 72, xmlBytes,   0x02u, xmlOff, xmlBytes);
    return wim;
}

void BuildMultiChunkValveVpk(
    const std::string& inlineVmt,
    const std::string& chunkedMdl,
    std::vector<uint8_t>& outDirVpk,
    std::vector<uint8_t>& outChunk000Vpk)
{
    // Build _000.vpk with 32-byte pad prefix + chunkedMdl tail (after 24B inline preload)
    const size_t preloadMdl = 24u;
    outChunk000Vpk.assign(32, 0xAAu);
    uint32_t chunkEntryOff = static_cast<uint32_t>(outChunk000Vpk.size());
    uint32_t chunkEntryLen = static_cast<uint32_t>(chunkedMdl.size() - preloadMdl);
    outChunk000Vpk.insert(outChunk000Vpk.end(), chunkedMdl.begin() + preloadMdl, chunkedMdl.end());

    // Inline VMT splits 16B preload + remainder right after directory tree
    const size_t preloadVmt = 16u;
    uint32_t inlineTailLen  = static_cast<uint32_t>(inlineVmt.size() - preloadVmt);

    std::vector<uint8_t> tree;
    auto pushCStr = [&](const char* s) {
        tree.insert(tree.end(), s, s + std::strlen(s) + 1);
    };
    auto pushVpkEntry = [&](uint32_t crc, uint16_t preloadSz, uint16_t arcIdx, uint32_t eOff, uint32_t eLen, const uint8_t* preloadBytes) {
        AppendU32LE(tree, crc);
        AppendU16LE(tree, preloadSz);
        AppendU16LE(tree, arcIdx);
        AppendU32LE(tree, eOff);
        AppendU32LE(tree, eLen);
        AppendU16LE(tree, 0xFFFFu);
        if (preloadSz > 0) tree.insert(tree.end(), preloadBytes, preloadBytes + preloadSz);
    };

    // Extension 1: "vmt" -> "materials/brick" -> "wall01" (inline 0x7FFF)
    pushCStr("vmt");
    pushCStr("materials/brick");
    pushCStr("wall01");
    pushVpkEntry(
        Checksum::Crc32(reinterpret_cast<const uint8_t*>(inlineVmt.data()), inlineVmt.size()),
        static_cast<uint16_t>(preloadVmt), 0x7FFFu, 0u, inlineTailLen,
        reinterpret_cast<const uint8_t*>(inlineVmt.data()));
    pushCStr(""); // end files
    pushCStr(""); // end paths

    // Extension 2: "mdl" -> "models/props" -> "crate01" (external _000.vpk archiveIdx == 0)
    pushCStr("mdl");
    pushCStr("models/props");
    pushCStr("crate01");
    pushVpkEntry(
        Checksum::Crc32(reinterpret_cast<const uint8_t*>(chunkedMdl.data()), chunkedMdl.size()),
        static_cast<uint16_t>(preloadMdl), 0u, chunkEntryOff, chunkEntryLen,
        reinterpret_cast<const uint8_t*>(chunkedMdl.data()));
    pushCStr(""); // end files
    pushCStr(""); // end paths
    pushCStr(""); // end extensions

    // VPK v2 28-byte header
    outDirVpk.clear();
    AppendU32LE(outDirVpk, 0x55AA1234u);
    AppendU32LE(outDirVpk, 2u); // version 2
    AppendU32LE(outDirVpk, static_cast<uint32_t>(tree.size()));
    AppendU32LE(outDirVpk, inlineTailLen); // FileDataSectionSize
    AppendU32LE(outDirVpk, 0u);
    AppendU32LE(outDirVpk, 0u);
    AppendU32LE(outDirVpk, 0u);
    outDirVpk.insert(outDirVpk.end(), tree.begin(), tree.end());
    outDirVpk.insert(outDirVpk.end(), inlineVmt.begin() + preloadVmt, inlineVmt.end());
}

std::vector<uint8_t> BuildElectronAsarArchive(const std::string& jsonStr, const std::vector<uint8_t>& concatPayload) {
    uint32_t jsonLen   = static_cast<uint32_t>(jsonStr.size());
    uint32_t alignedJs = (jsonLen + 3u) & ~3u;
    uint32_t pickleSz  = 4u + alignedJs;
    uint32_t headerSz  = 4u + pickleSz; // payloadBase = 8 + headerSz = 16 + alignedJs

    std::vector<uint8_t> asar;
    AppendU32LE(asar, 4u);
    AppendU32LE(asar, headerSz);
    AppendU32LE(asar, pickleSz);
    AppendU32LE(asar, jsonLen);
    asar.insert(asar.end(), jsonStr.begin(), jsonStr.end());
    while ((asar.size() % 4u) != 0) asar.push_back(0);
    asar.insert(asar.end(), concatPayload.begin(), concatPayload.end());
    return asar;
}

// Encodes raw bytes into EA RefPack (0x10FB) using literal blocks (0xE0..0xFB), 2B & 3B LZ77 matches, and 0xFC..0xFF stop!
std::vector<uint8_t> EncodeEaRefPackForTest(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> out = {
        0x10u, 0xFBu,
        static_cast<uint8_t>((len >> 16) & 0xFFu),
        static_cast<uint8_t>((len >> 8) & 0xFFu),
        static_cast<uint8_t>(len & 0xFFu)
    };

    size_t pos = 0;
    size_t anchor = 0;
    auto flushLitsDownTo3 = [&]() {
        while (pos - anchor >= 4u) {
            size_t blocks4 = std::min<size_t>((pos - anchor) / 4u, 28u); // up to 112 bytes
            size_t nBytes  = blocks4 * 4u;
            out.push_back(static_cast<uint8_t>(0xE0u + (blocks4 - 1u)));
            out.insert(out.end(), raw + anchor, raw + anchor + nBytes);
            anchor += nBytes;
        }
    };

    while (pos < len) {
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 16384u);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(67u, len - pos);
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4u && m > bestLen) {
                bestLen  = m;
                bestDist = d;
                if (bestLen >= 60u) break;
            }
        }
        if (bestLen >= 4u) {
            flushLitsDownTo3();
            size_t procLen = pos - anchor; // 0..3
            size_t d0 = bestDist - 1u;
            if (bestLen <= 10u && d0 < 1024u) {
                // 2-byte command (0x00..0x7F)
                uint8_t b0 = static_cast<uint8_t>(((d0 >> 3) & 0x60u) | ((bestLen - 3u) << 2) | procLen);
                uint8_t b1 = static_cast<uint8_t>(d0 & 0xFFu);
                out.push_back(b0);
                out.push_back(b1);
            } else {
                // 3-byte command (0x80..0xBF)
                uint8_t b0 = static_cast<uint8_t>(0x80u | (bestLen - 4u));
                uint8_t b1 = static_cast<uint8_t>((procLen << 6) | ((d0 >> 8) & 0x3Fu));
                uint8_t b2 = static_cast<uint8_t>(d0 & 0xFFu);
                out.push_back(b0);
                out.push_back(b1);
                out.push_back(b2);
            }
            if (procLen > 0) out.insert(out.end(), raw + anchor, raw + pos);
            pos += bestLen;
            anchor = pos;
        } else {
            ++pos;
        }
    }

    flushLitsDownTo3();
    size_t tail = len - anchor; // 0..3
    out.push_back(static_cast<uint8_t>(0xFCu | tail));
    if (tail > 0) out.insert(out.end(), raw + anchor, raw + len);
    return out;
}

std::vector<uint8_t> BuildEaBigTestArchive(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& entries)
{
    std::vector<uint8_t> hdr = { 'B','I','G','F', 0,0,0,0 };
    AppendU32BE(hdr, static_cast<uint32_t>(entries.size()));
    AppendU32BE(hdr, 0u); // patched below

    std::vector<size_t> patchOffs;
    for (const auto& kv : entries) {
        patchOffs.push_back(hdr.size());
        AppendU32BE(hdr, 0u); // offset patched below
        AppendU32BE(hdr, static_cast<uint32_t>(kv.second.size()));
        hdr.insert(hdr.end(), kv.first.begin(), kv.first.end());
        hdr.push_back(0);
    }
    while ((hdr.size() % 8u) != 0) hdr.push_back(0);
    uint32_t hdrEnd = static_cast<uint32_t>(hdr.size());
    PutU32BE(hdr.data() + 12, hdrEnd);

    std::vector<uint8_t> big = std::move(hdr);
    for (size_t i = 0; i < entries.size(); ++i) {
        uint32_t dataOff = static_cast<uint32_t>(big.size());
        PutU32BE(big.data() + patchOffs[i], dataOff);
        big.insert(big.end(), entries[i].second.begin(), entries[i].second.end());
    }
    PutU32LE(big.data() + 4, static_cast<uint32_t>(big.size()));
    return big;
}

// ============================================================================
// 11. Encoders & Builders for Tests 15.20 – 15.27:
//     LHA -lh5- (Level 1) & ARJ (method 1 & method 4), Debian .deb + Unix/BSD .ar,
//     Linux z_erofs (LZ4 Legacy/Full & Compact), Bohemia .pbo ("srpC" LZSS),
//     RFC 7932 Brotli (.br / .tbr / .tar.br), ECMA-167 UDF (.udf) & Nero (.nrg),
//     ISO 28500 WARC (.warc + multi-member gzip), Android Sparse (.simg + ext4)
// ============================================================================

void BuildKraftCompleteLengths(const std::vector<uint16_t>& activeSyms, size_t alphaSz,
                               std::vector<uint8_t>& outLens, std::vector<uint32_t>& outMsbCodes)
{
    outLens.assign(alphaSz, 0u);
    outMsbCodes.assign(alphaSz, 0u);
    const size_t k = activeSyms.size();
    if (k < 2u) return;

    uint32_t bits = 1u;
    while ((size_t(1) << bits) < k) ++bits;
    const size_t shortCnt = (size_t(1) << bits) - k;
    for (size_t i = 0; i < k; ++i) {
        outLens[activeSyms[i]] = static_cast<uint8_t>((i < shortCnt) ? (bits - 1u) : bits);
    }

    uint32_t cnt[17] = {};
    for (uint8_t l : outLens) if (l > 0u) ++cnt[l];
    uint32_t firstCode[17] = {};
    uint32_t code = 0u;
    for (uint32_t l = 1u; l <= bits; ++l) {
        code = (code + cnt[l - 1u]) << 1u;
        firstCode[l] = code;
    }
    for (size_t s = 0; s < alphaSz; ++s) {
        if (outLens[s] > 0u) outMsbCodes[s] = firstCode[outLens[s]]++;
    }
}

std::vector<uint8_t> EncodeLhaArjBlockHuffForTest(
    const uint8_t* raw, size_t len, uint32_t np, uint32_t pbit, size_t maxWindow)
{
    struct LzTok { uint16_t cSym; uint16_t pSym; uint32_t distExtra; uint32_t distExtraBits; };
    std::vector<LzTok> toks;
    bool cUsed[510] = {};
    bool pUsed[32]  = {};

    size_t pos = 0;
    while (pos < len) {
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, maxWindow);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(256u, len - pos);
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4u && m > bestLen) {
                bestLen = m;
                bestDist = d;
                if (bestLen >= 64u) break;
            }
        }
        if (bestLen >= 4u) {
            uint16_t cSym = static_cast<uint16_t>(256u + bestLen - 3u);
            uint32_t dist0 = static_cast<uint32_t>(bestDist - 1u);
            uint16_t pSym = 0u;
            uint32_t eb = 0u, ev = 0u;
            if (dist0 > 0u) {
                uint32_t tmp = dist0;
                while (tmp > 0u) { ++pSym; tmp >>= 1u; }
                eb = pSym - 1u;
                ev = dist0 & ((1u << eb) - 1u);
            }
            toks.push_back({ cSym, pSym, ev, eb });
            cUsed[cSym] = true;
            pUsed[pSym] = true;
            pos += bestLen;
        } else {
            uint16_t cSym = raw[pos++];
            toks.push_back({ cSym, 0u, 0u, 0u });
            cUsed[cSym] = true;
        }
    }
    if (!pUsed[0]) pUsed[0] = true;

    std::vector<uint16_t> cAct, pAct;
    for (uint16_t s = 0; s < 510u; ++s) if (cUsed[s]) cAct.push_back(s);
    for (uint16_t s = 0; s < np; ++s)   if (pUsed[s]) pAct.push_back(s);

    std::vector<uint8_t> cLens, pLens;
    std::vector<uint32_t> cCodes, pCodes;
    BuildKraftCompleteLengths(cAct, 510u, cLens, cCodes);
    BuildKraftCompleteLengths(pAct, np,   pLens, pCodes);

    MsbBitWriter bw;
    bw.Write(static_cast<uint32_t>(toks.size()), 16); // 16-bit block size

    auto writePtTree = [&](const std::vector<uint8_t>& lens, const std::vector<uint16_t>& act,
                           uint32_t nn, uint32_t nbit, int iSpecial)
    {
        if (act.size() == 1u) {
            bw.Write(0u, static_cast<int>(nbit));
            bw.Write(act[0], static_cast<int>(nbit));
            return;
        }
        uint32_t n = nn;
        while (n > 0u && lens[n - 1u] == 0u) --n;
        bw.Write(n, static_cast<int>(nbit));
        uint32_t i = 0u;
        while (i < n) {
            uint8_t c = lens[i++];
            if (c < 7u) {
                bw.Write(c, 3);
            } else {
                bw.Write(7u, 3);
                for (uint8_t k = 0; k < c - 7u; ++k) bw.Write(1u, 1);
                bw.Write(0u, 1);
            }
            if (static_cast<int>(i) == iSpecial) {
                uint32_t z = 0u;
                while (z < 3u && i < n && lens[i] == 0u) { ++z; ++i; }
                bw.Write(z, 2);
            }
        }
    };

    if (cAct.size() == 1u) {
        // Dummy 1-symbol pt_tree + 1-symbol c_tree
        bw.Write(0u, 5);
        bw.Write(0u, 5);
        bw.Write(0u, 9);
        bw.Write(cAct[0], 9);
    } else {
        uint32_t nc = 510u;
        while (nc > 0u && cLens[nc - 1u] == 0u) --nc;
        // RLE-encode cLens[0..nc-1] into pt symbols (0=1 zero, 1=3..18 zeroes, 2=20..531 zeroes, c+2=length c)
        struct ClItem { uint16_t ptSym; int extraBits; uint32_t extraVal; };
        std::vector<ClItem> clSeq;
        bool ptUsed[19] = {};
        uint32_t idx = 0u;
        while (idx < nc) {
            if (cLens[idx] == 0u) {
                uint32_t run = 0u;
                while (idx + run < nc && cLens[idx + run] == 0u) ++run;
                idx += run;
                while (run > 0u) {
                    if (run >= 20u) {
                        uint32_t take = std::min<uint32_t>(run, 531u);
                        clSeq.push_back({ 2u, 9, take - 20u });
                        ptUsed[2] = true;
                        run -= take;
                    } else if (run >= 3u) {
                        uint32_t take = std::min<uint32_t>(run, 18u);
                        clSeq.push_back({ 1u, 4, take - 3u });
                        ptUsed[1] = true;
                        run -= take;
                    } else {
                        clSeq.push_back({ 0u, 0, 0u });
                        ptUsed[0] = true;
                        --run;
                    }
                }
            } else {
                uint16_t ps = static_cast<uint16_t>(cLens[idx++] + 2u);
                clSeq.push_back({ ps, 0, 0u });
                ptUsed[ps] = true;
            }
        }

        std::vector<uint16_t> ptAct;
        for (uint16_t s = 0; s < 19u; ++s) if (ptUsed[s]) ptAct.push_back(s);
        std::vector<uint8_t> ptLens;
        std::vector<uint32_t> ptCodes;
        BuildKraftCompleteLengths(ptAct, 19u, ptLens, ptCodes);
        writePtTree(ptLens, ptAct, 19u, 5u, 3);

        bw.Write(nc, 9);
        for (const auto& it : clSeq) {
            if (ptAct.size() > 1u) {
                bw.Write(ptCodes[it.ptSym], ptLens[it.ptSym]);
            }
            if (it.extraBits > 0) bw.Write(it.extraVal, it.extraBits);
        }
    }

    writePtTree(pLens, pAct, np, pbit, -1);

    for (const auto& t : toks) {
        if (cAct.size() > 1u) bw.Write(cCodes[t.cSym], cLens[t.cSym]);
        if (t.cSym >= 256u) {
            if (pAct.size() > 1u) bw.Write(pCodes[t.pSym], pLens[t.pSym]);
            if (t.distExtraBits > 0u) bw.Write(t.distExtra, static_cast<int>(t.distExtraBits));
        }
    }
    bw.Flush();
    return bw.out;
}

std::vector<uint8_t> EncodeArjMethod4ForTest(const uint8_t* raw, size_t len) {
    MsbBitWriter bw;
    size_t pos = 0;
    while (pos < len) {
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 15872u);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = std::min<size_t>(256u, len - pos);
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4u && m > bestLen) {
                bestLen = m;
                bestDist = d;
                if (bestLen >= 64u) break;
            }
        }
        if (bestLen >= 4u) {
            uint32_t lenVal = static_cast<uint32_t>(bestLen - 2u);
            uint32_t w = 1u;
            while (w < 7u && lenVal >= ((1u << (w + 1u)) - 1u)) ++w;
            for (uint32_t k = 0; k < w; ++k) bw.Write(1u, 1);
            if (w < 7u) bw.Write(0u, 1);
            bw.Write(lenVal - ((1u << w) - 1u), static_cast<int>(w));

            uint32_t dist0 = static_cast<uint32_t>(bestDist - 1u);
            uint32_t pw = 0u;
            while (pw < 4u && dist0 >= (((1u << (pw + 1u)) - 1u) << 9u)) ++pw;
            for (uint32_t k = 0; k < pw; ++k) bw.Write(1u, 1);
            if (pw < 4u) bw.Write(0u, 1);
            bw.Write(dist0 - (((1u << pw) - 1u) << 9u), static_cast<int>(9u + pw));
            pos += bestLen;
        } else {
            bw.Write(0u, 1); // lenVal == 0 -> literal
            bw.Write(raw[pos++], 8);
        }
    }
    bw.Flush();
    return bw.out;
}

std::vector<uint8_t> BuildLzhLevel1Archive(
    const std::string& dirPart,
    const std::string& filePart,
    const std::string& plainBody,
    const std::string& storedFile,
    const std::string& storedBody)
{
    auto compLh5 = EncodeLhaArjBlockHuffForTest(
        reinterpret_cast<const uint8_t*>(plainBody.data()), plainBody.size(), 14u, 4u, 8192u);

    // Extended header 0x02 (directory name with 0xFF separator): [0x02][dir + 0xFF][u16 nextExt = 0]
    std::vector<uint8_t> extDir = { 0x02u };
    extDir.insert(extDir.end(), dirPart.begin(), dirPart.end());
    extDir.push_back(0xFFu);
    AppendU16LE(extDir, 0u); // end of extended headers

    // Level-1 basic header: [0]=hdrSz, [1]=sum, [2..6]="-lh5-", [7..10]=skipSz(ext+comp), [11..14]=origSz,
    // [15..18]=dosTime, [19]=0x20, [20]=1, [21]=fnLen, [22..]=fn, [crc16], [os='M'], [u16 firstExtSz]
    const uint8_t fnLen = static_cast<uint8_t>(filePart.size());
    const uint8_t hdrSz = static_cast<uint8_t>(25u + fnLen); // 2 + hdrSz == 27 + fnLen
    std::vector<uint8_t> h1(2u + hdrSz, 0u);
    h1[0] = hdrSz;
    std::memcpy(h1.data() + 2, "-lh5-", 5);
    PutU32LE(h1.data() + 7,  static_cast<uint32_t>(extDir.size() + compLh5.size())); // Level-1 skip_size!
    PutU32LE(h1.data() + 11, static_cast<uint32_t>(plainBody.size()));
    h1[19] = 0x20u;
    h1[20] = 1u; // Level 1
    h1[21] = fnLen;
    std::memcpy(h1.data() + 22, filePart.data(), fnLen);
    h1[24 + fnLen] = 'M';
    PutU16LE(h1.data() + 25 + fnLen, static_cast<uint16_t>(extDir.size()));
    uint8_t csum = 0u;
    for (size_t i = 2; i < h1.size(); ++i) csum = static_cast<uint8_t>(csum + h1[i]);
    h1[1] = csum;

    // Second entry: Level-0 "-lh0-" stored file
    const uint8_t fn2Len = static_cast<uint8_t>(storedFile.size());
    const uint8_t hdr2Sz = static_cast<uint8_t>(22u + fn2Len);
    std::vector<uint8_t> h2(2u + hdr2Sz, 0u);
    h2[0] = hdr2Sz;
    std::memcpy(h2.data() + 2, "-lh0-", 5);
    PutU32LE(h2.data() + 7,  static_cast<uint32_t>(storedBody.size()));
    PutU32LE(h2.data() + 11, static_cast<uint32_t>(storedBody.size()));
    h2[19] = 0x20u;
    h2[20] = 0u; // Level 0
    h2[21] = fn2Len;
    std::memcpy(h2.data() + 22, storedFile.data(), fn2Len);
    uint8_t csum2 = 0u;
    for (size_t i = 2; i < h2.size(); ++i) csum2 = static_cast<uint8_t>(csum2 + h2[i]);
    h2[1] = csum2;

    std::vector<uint8_t> arc;
    arc.insert(arc.end(), h1.begin(), h1.end());
    arc.insert(arc.end(), extDir.begin(), extDir.end());
    arc.insert(arc.end(), compLh5.begin(), compLh5.end());
    arc.insert(arc.end(), h2.begin(), h2.end());
    arc.insert(arc.end(), storedBody.begin(), storedBody.end());
    arc.push_back(0u); // End-of-archive sentinel
    return arc;
}

std::vector<uint8_t> BuildArjTestArchive(
    const std::string& nameM1, const std::string& plainM1,
    const std::string& nameM4, const std::string& plainM4)
{
    auto compM1 = EncodeLhaArjBlockHuffForTest(
        reinterpret_cast<const uint8_t*>(plainM1.data()), plainM1.size(), 17u, 5u, 26624u);
    auto compM4 = EncodeArjMethod4ForTest(
        reinterpret_cast<const uint8_t*>(plainM4.data()), plainM4.size());

    std::vector<uint8_t> arc;
    auto pushArjHeader = [&](uint8_t method, uint8_t fileType, const std::string& fn,
                             const uint8_t* compPtr, size_t compSz, size_t origSz, uint32_t fileCrc)
    {
        std::vector<uint8_t> basic(30u, 0u);
        basic[0] = 30u; // first_hdr_size
        basic[1] = 6u;  // archiver version
        basic[2] = 1u;  // min version
        basic[3] = method;
        basic[4] = fileType;
        PutU32LE(basic.data() + 12, static_cast<uint32_t>(compSz));
        PutU32LE(basic.data() + 16, static_cast<uint32_t>(origSz));
        PutU32LE(basic.data() + 20, fileCrc);
        basic.insert(basic.end(), fn.begin(), fn.end());
        basic.push_back(0u); // filename NUL
        basic.push_back(0u); // comment NUL

        arc.push_back(0x60u);
        arc.push_back(0xEAu);
        AppendU16LE(arc, static_cast<uint16_t>(basic.size()));
        arc.insert(arc.end(), basic.begin(), basic.end());
        AppendU32LE(arc, Checksum::Crc32(basic.data(), basic.size()));
        AppendU16LE(arc, 0u); // no extended headers
        if (compPtr && compSz > 0u) {
            arc.insert(arc.end(), compPtr, compPtr + compSz);
        }
    };

    pushArjHeader(0u, 2u, "TEST.ARJ", nullptr, 0u, 0u, 0u); // Main archive header
    pushArjHeader(1u, 0u, nameM1, compM1.data(), compM1.size(), plainM1.size(),
                  Checksum::Crc32(reinterpret_cast<const uint8_t*>(plainM1.data()), plainM1.size()));
    pushArjHeader(4u, 0u, nameM4, compM4.data(), compM4.size(), plainM4.size(),
                  Checksum::Crc32(reinterpret_cast<const uint8_t*>(plainM4.data()), plainM4.size()));

    // End-of-archive marker: 0x60 0xEA + basicSz = 0
    arc.push_back(0x60u);
    arc.push_back(0xEAu);
    AppendU16LE(arc, 0u);
    return arc;
}

void AppendArMemberForTest(std::vector<uint8_t>& ar, const std::string& rawNameField, const uint8_t* body, size_t bodyLen) {
    uint8_t hdr[60];
    std::memset(hdr, ' ', sizeof(hdr));
    std::memcpy(hdr, rawNameField.data(), std::min<size_t>(rawNameField.size(), 16u));
    std::memcpy(hdr + 16, "1700000000", 10);
    std::memcpy(hdr + 40, "100644", 6);
    char szBuf[16] = {};
    snprintf(szBuf, sizeof(szBuf), "%llu", static_cast<unsigned long long>(bodyLen));
    std::memcpy(hdr + 48, szBuf, std::strlen(szBuf));
    hdr[58] = 0x60u;
    hdr[59] = 0x0Au;
    ar.insert(ar.end(), hdr, hdr + 60);
    if (body && bodyLen > 0u) ar.insert(ar.end(), body, body + bodyLen);
    if ((bodyLen & 1u) != 0u) ar.push_back('\n');
}

std::vector<uint8_t> BuildDebianDebArchive(const std::vector<std::pair<std::string, std::string>>& dataFiles) {
    std::vector<uint8_t> deb = { '!','<','a','r','c','h','>','\n' };
    const std::string debVer = "2.0\n";
    AppendArMemberForTest(deb, "debian-binary/", reinterpret_cast<const uint8_t*>(debVer.data()), debVer.size());

    auto ctrlTar = BuildPosixTar({ { "control", "Package: explorer2-test\nVersion: 1.0\nArchitecture: amd64\n" } });
    auto ctrlGz  = CompressGzip(ctrlTar.data(), ctrlTar.size());
    AppendArMemberForTest(deb, "control.tar.gz/", ctrlGz.data(), ctrlGz.size());

    auto dataTar = BuildPosixTar(dataFiles);
    auto dataGz  = CompressGzip(dataTar.data(), dataTar.size());
    AppendArMemberForTest(deb, "data.tar.gz/", dataGz.data(), dataGz.size());
    return deb;
}

std::vector<uint8_t> CompressRawLz4BlockForTest(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> blk;
    size_t pos = 0, anchor = 0;
    while (pos + 5 <= len) {
        size_t bestLen = 0, bestDist = 0;
        size_t maxBack = std::min<size_t>(pos, 8192u);
        for (size_t d = 1; d <= maxBack; ++d) {
            size_t m = 0;
            size_t maxM = (len - pos > 5) ? (len - pos - 5) : 0;
            while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
            if (m >= 4 && m > bestLen) { bestLen = m; bestDist = d; if (bestLen >= 64) break; }
        }
        if (bestLen >= 4) {
            size_t litLen = pos - anchor;
            size_t mCode  = bestLen - 4;
            blk.push_back(static_cast<uint8_t>((std::min<size_t>(litLen, 15) << 4) | std::min<size_t>(mCode, 15)));
            if (litLen >= 15) {
                size_t rem = litLen - 15;
                while (rem >= 255) { blk.push_back(255); rem -= 255; }
                blk.push_back(static_cast<uint8_t>(rem));
            }
            blk.insert(blk.end(), raw + anchor, raw + pos);
            AppendU16LE(blk, static_cast<uint16_t>(bestDist));
            if (mCode >= 15) {
                size_t rem = mCode - 15;
                while (rem >= 255) { blk.push_back(255); rem -= 255; }
                blk.push_back(static_cast<uint8_t>(rem));
            }
            pos += bestLen;
            anchor = pos;
        } else {
            ++pos;
        }
    }
    size_t tailLit = len - anchor;
    blk.push_back(static_cast<uint8_t>(std::min<size_t>(tailLit, 15) << 4));
    if (tailLit >= 15) {
        size_t rem = tailLit - 15;
        while (rem >= 255) { blk.push_back(255); rem -= 255; }
        blk.push_back(static_cast<uint8_t>(rem));
    }
    blk.insert(blk.end(), raw + anchor, raw + len);
    return blk;
}

std::vector<uint8_t> BuildCompressedErofsTestImage(
    const std::string& legacyFullBody,
    const std::string& compactBody)
{
    // 8 x 4096B blocks:
    //   Block 0: Superblock @ 1024 (LZ4_0PADDING = 0x01)
    //   Block 1: Metadata inodes (NID 0 root dir, NID 4 legacy/full compressed file, NID 12 compact compressed file)
    //   Block 2: Root directory dirents
    //   Blocks 3..4: Two LZ4-compressed physical clusters for NID 4 (legacyFullBody > 4096B)
    //   Block 5: LZ4-compressed physical cluster for NID 12 (compactBody <= 4096B)
    const size_t blkSz = 4096u;
    std::vector<uint8_t> img(6u * blkSz, 0u);

    uint8_t* sb = img.data() + 1024u;
    PutU32LE(sb + 0,  0xE0F5E1E2u);
    sb[12] = 12u;          // blkszbits = 12 (4096B)
    PutU16LE(sb + 14, 0u); // root_nid = 0
    PutU32LE(sb + 20, 0x01u); // EROFS_FEATURE_INCOMPAT_LZ4_0PADDING
    PutU32LE(sb + 36, 6u); // blocks = 6
    PutU32LE(sb + 40, 1u); // meta_blkaddr = 1
    std::memcpy(sb + 64, "EROFS_ZL4", 9);

    // NID 0 @ 4096 + 0: root dir -> "legacy_lz4.bin" (NID 4), "compact_lz4.bin" (NID 12)
    uint8_t* n0 = img.data() + 4096u;
    PutU16LE(n0 + 0, 0u);
    PutU16LE(n0 + 4, 0040755u);
    PutU32LE(n0 + 8, 53u);
    PutU32LE(n0 + 16, 2u);
    uint8_t* rd = img.data() + 2u * blkSz;
    PutU64LE(rd + 0,  4ULL);  PutU16LE(rd + 8,  24u); rd[10] = 1u;
    PutU64LE(rd + 12, 12ULL); PutU16LE(rd + 20, 38u); rd[22] = 1u;
    std::memcpy(rd + 24, "legacy_lz4.bin", 14);
    std::memcpy(rd + 38, "compact_lz4.bin", 15);

    auto writeZeroPaddedLz4Block = [&](size_t physBlk, const uint8_t* src, size_t len) {
        auto lz = CompressRawLz4BlockForTest(src, len);
        // Right-align LZ4 payload with leading 0x00 padding per EROFS_FEATURE_INCOMPAT_LZ4_0PADDING!
        size_t pad = blkSz - lz.size();
        std::memcpy(img.data() + physBlk * blkSz + pad, lz.data(), lz.size());
    };

    // NID 4 @ 4096 + 128: dataLayout = 1 (FLAT_COMPRESSION_LEGACY / COMPRESSED_FULL, 2 lclusters -> blocks 3 & 4)
    {
        size_t c0Len = blkSz;
        size_t c1Len = legacyFullBody.size() - blkSz;
        writeZeroPaddedLz4Block(3u, reinterpret_cast<const uint8_t*>(legacyFullBody.data()), c0Len);
        writeZeroPaddedLz4Block(4u, reinterpret_cast<const uint8_t*>(legacyFullBody.data()) + c0Len, c1Len);

        uint8_t* n4 = img.data() + 4096u + 4u * 32u;
        PutU16LE(n4 + 0, (1u << 1) | 0u); // COMPACT | dataLayout=1
        PutU16LE(n4 + 4, 0100644u);
        PutU32LE(n4 + 8, static_cast<uint32_t>(legacyFullBody.size()));
        // 8B z_erofs_map_header @ n4 + 32: h_advise=0, h_algorithmtype=0 (LZ4), h_clusterbits=0 (4096B)
        uint8_t* mh = n4 + 32u;
        PutU16LE(mh + 4, 0u);
        mh[6] = 0u;
        mh[7] = 0u;
        // Two 8B z_erofs_lcluster_index entries @ n4 + 40:
        uint8_t* idx0 = n4 + 40u;
        PutU16LE(idx0 + 0, 1u); // Z_EROFS_LCLUSTER_TYPE_HEAD1
        PutU16LE(idx0 + 2, 0u); // clusterofs = 0
        PutU32LE(idx0 + 4, 3u); // blkaddr = 3
        uint8_t* idx1 = n4 + 48u;
        PutU16LE(idx1 + 0, 1u); // HEAD1
        PutU16LE(idx1 + 2, 0u); // clusterofs = 0
        PutU32LE(idx1 + 4, 4u); // blkaddr = 4
    }

    // NID 12 @ 4096 + 384: dataLayout = 4 (COMPRESSED_COMPACT 4B pack -> block 5)
    {
        writeZeroPaddedLz4Block(5u, reinterpret_cast<const uint8_t*>(compactBody.data()), compactBody.size());
        uint8_t* n12 = img.data() + 4096u + 12u * 32u;
        PutU16LE(n12 + 0, (4u << 1) | 0u); // COMPACT | dataLayout=4 (COMPRESSED_COMPACT)
        PutU16LE(n12 + 4, 0100644u);
        PutU32LE(n12 + 8, static_cast<uint32_t>(compactBody.size()));
        uint8_t* mh = n12 + 32u;
        PutU16LE(mh + 4, 0u); // 4B compact pack (amortizedshift=2, vcnt=2)
        mh[6] = 0u;           // LZ4
        mh[7] = 0u;           // lclusterbits = 12
        // 8B 4B-pack @ n12 + 40: slot0 = (HEAD1 << 12) | 0, slot1 = 0, baseBlkAddr = 5
        uint8_t* pk = n12 + 40u;
        PutU16LE(pk + 0, static_cast<uint16_t>((1u << 12u) | 0u));
        PutU16LE(pk + 2, 0u);
        PutU32LE(pk + 4, 5u);
    }

    return img;
}

std::vector<uint8_t> EncodeBohemiaPboLzssForTest(const uint8_t* raw, size_t len) {
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos < len) {
        size_t flagPos = out.size();
        out.push_back(0u);
        uint8_t flags = 0u;
        for (int bit = 0; bit < 8 && pos < len; ++bit) {
            size_t bestLen = 0, bestDist = 0;
            size_t maxBack = std::min<size_t>(pos, 4095u);
            for (size_t d = 1; d <= maxBack; ++d) {
                size_t m = 0;
                size_t maxM = std::min<size_t>(18u, len - pos);
                while (m < maxM && raw[pos + m] == raw[pos - d + m]) ++m;
                if (m >= 3u && m > bestLen) {
                    bestLen = m;
                    bestDist = d;
                    if (bestLen == 18u) break;
                }
            }
            if (bestLen >= 3u) {
                out.push_back(static_cast<uint8_t>(bestDist & 0xFFu));
                out.push_back(static_cast<uint8_t>(((bestDist >> 4u) & 0xF0u) | ((bestLen - 3u) & 0x0Fu)));
                pos += bestLen;
            } else {
                flags = static_cast<uint8_t>(flags | (1u << bit));
                out.push_back(raw[pos++]);
            }
        }
        out[flagPos] = flags;
    }
    uint32_t sum = 0u;
    for (size_t i = 0; i < len; ++i) sum += raw[i];
    AppendU32LE(out, sum);
    return out;
}

std::vector<uint8_t> BuildBohemiaPboTestArchive(
    const std::string& prefix,
    const std::string& compFileName, const std::string& compPlain,
    const std::string& rawFileName,  const std::string& rawPlain)
{
    auto lzss = EncodeBohemiaPboLzssForTest(reinterpret_cast<const uint8_t*>(compPlain.data()), compPlain.size());
    std::vector<uint8_t> pbo;
    auto pushHdr = [&](const std::string& fn, uint32_t mime, uint32_t origSz, uint32_t dataSz) {
        pbo.insert(pbo.end(), fn.begin(), fn.end());
        pbo.push_back(0u);
        AppendU32LE(pbo, mime);
        AppendU32LE(pbo, origSz);
        AppendU32LE(pbo, 0u);
        AppendU32LE(pbo, 0u);
        AppendU32LE(pbo, dataSz);
    };

    // "sreV" version properties block
    pushHdr("", 0x56657273u, 0u, 0u);
    const std::string k = "prefix";
    pbo.insert(pbo.end(), k.begin(), k.end()); pbo.push_back(0u);
    pbo.insert(pbo.end(), prefix.begin(), prefix.end()); pbo.push_back(0u);
    pbo.push_back(0u); // end properties

    pushHdr(compFileName, 0x43707273u, static_cast<uint32_t>(compPlain.size()), static_cast<uint32_t>(lzss.size()));
    pushHdr(rawFileName,  0u,          static_cast<uint32_t>(rawPlain.size()),  static_cast<uint32_t>(rawPlain.size()));
    pushHdr("", 0u, 0u, 0u); // End of header table

    pbo.insert(pbo.end(), lzss.begin(), lzss.end());
    pbo.insert(pbo.end(), rawPlain.begin(), rawPlain.end());
    return pbo;
}

// Genuine RFC 7932 Brotli Compressor (LZ77 + Canonical Prefix Trees for Literals, IAC 128..383, and Distances 0..63)
std::vector<uint8_t> CompressBrotliRfc7932ForTest(const uint8_t* raw, size_t len) {
    auto revBits = [](uint32_t v, int n) -> uint32_t {
        uint32_t r = 0u;
        for (int i = 0; i < n; ++i) r = (r << 1u) | ((v >> i) & 1u);
        return r;
    };

    static constexpr uint32_t kInsBase[16]  = { 0,1,2,3,4,5,6,8,10,14,18,26,34,50,66,98 };
    static constexpr uint8_t  kInsExtra[16] = { 0,0,0,0,0,0,1,1,2,2,3,3,4,4,5,5 };
    static constexpr uint32_t kCopyBase[16] = { 2,3,4,5,6,7,8,9,10,12,14,18,22,30,38,54 };
    static constexpr uint8_t  kCopyExtra[16]= { 0,0,0,0,0,0,0,0,1,1,2,2,3,3,4,4 };

    auto mapIns = [&](size_t insLen, uint32_t& code, uint32_t& eb, uint32_t& ev) {
        code = 15u;
        for (uint32_t c = 0; c < 15u; ++c) {
            if (insLen >= kInsBase[c] && insLen < kInsBase[c + 1u]) { code = c; break; }
        }
        eb = kInsExtra[code];
        ev = static_cast<uint32_t>(insLen - kInsBase[code]);
    };
    auto mapCopy = [&](size_t copyLen, uint32_t& code, uint32_t& eb, uint32_t& ev) {
        code = 15u;
        for (uint32_t c = 0; c < 15u; ++c) {
            if (copyLen >= kCopyBase[c] && copyLen < kCopyBase[c + 1u]) { code = c; break; }
        }
        eb = kCopyExtra[code];
        ev = static_cast<uint32_t>(copyLen - kCopyBase[code]);
    };
    auto mapIacSym = [](uint32_t insCode, uint32_t copyCode) -> uint32_t {
        // Maps (insCode 0..15, copyCode 0..15) into rangeIdx 2..5 => iacSym 128..383!
        uint32_t rangeIdx = 2u + ((insCode >= 8u) ? 2u : 0u) + ((copyCode >= 8u) ? 1u : 0u);
        return (rangeIdx << 6u) | ((insCode & 7u) << 3u) | (copyCode & 7u);
    };
    auto mapDist = [](size_t dist, uint32_t& dSym, uint32_t& eb, uint32_t& ev) {
        // Direct RFC 7932 distance code (npostfix=0, ndirect=0 => dSym = 16 + hcode)
        for (uint32_t hcode = 0; hcode < 48u; ++hcode) {
            uint32_t ndb = 1u + (hcode >> 1u);
            size_t lo = (((size_t(2u) + (hcode & 1u)) << ndb) - 4u) + 1u;
            size_t hi = lo + (size_t(1u) << ndb) - 1u;
            if (dist >= lo && dist <= hi) {
                dSym = 16u + hcode;
                eb = ndb;
                ev = static_cast<uint32_t>(dist - lo);
                return;
            }
        }
        dSym = 16u; eb = 1u; ev = 0u;
    };

    LsbBitWriter bw;
    bw.WriteBits(0u, 1); // WBITS = 16

    size_t chunkOff = 0;
    while (chunkOff < len) {
        // Slice into chunks where every literal run <= 129 bytes; if an uncompressible run > 129 exists,
        // emit a small uncompressed non-last meta-block for it, else emit a compressed meta-block!
        size_t chunkLen = std::min<size_t>(len - chunkOff, 32768u);
        const uint8_t* src = raw + chunkOff;
        const bool isLastChunk = (chunkOff + chunkLen == len);

        struct BrCmd { size_t litStart; size_t insLen; size_t copyLen; size_t dist; };
        std::vector<BrCmd> cmds;
        bool fitsCompressed = true;
        size_t pos = 0, anchor = 0;
        while (pos < chunkLen) {
            size_t bestLen = 0, bestDist = 0;
            size_t maxBack = std::min<size_t>(chunkOff + pos, 32768u);
            for (size_t d = 1; d <= maxBack; ++d) {
                size_t m = 0;
                size_t maxM = std::min<size_t>(69u, chunkLen - pos);
                while (m < maxM && raw[chunkOff + pos + m] == raw[chunkOff + pos - d + m]) ++m;
                if (m >= 4u && m > bestLen) {
                    bestLen = m;
                    bestDist = d;
                    if (bestLen >= 54u) break;
                }
            }
            if (bestLen >= 4u) {
                if (pos - anchor > 129u) { fitsCompressed = false; break; }
                cmds.push_back({ anchor, pos - anchor, bestLen, bestDist });
                pos += bestLen;
                anchor = pos;
            } else {
                if (pos - anchor >= 129u) {
                    if (cmds.empty()) {
                        fitsCompressed = false;
                        break;
                    }
                    // Truncate this meta-block right at `anchor` (which ended cleanly on a match!)
                    chunkLen = anchor;
                    break;
                }
                ++pos;
            }
        }
        if (fitsCompressed && chunkLen - anchor > 129u) {
            if (!cmds.empty()) chunkLen = anchor;
            else fitsCompressed = false;
        }

        const bool finalMeta = (chunkOff + chunkLen == len) && fitsCompressed;
        if (!fitsCompressed) {
            size_t rawSlice = std::min<size_t>(len - chunkOff, 4096u);
            bw.WriteBits(0u, 1); // ISLAST = 0
            bw.WriteBits(0u, 2); // MNIBBLES = 4
            bw.WriteBits(static_cast<uint32_t>(rawSlice - 1u), 16);
            bw.WriteBits(1u, 1); // ISUNCOMPRESSED = 1
            bw.Flush();          // AlignToByteZero!
            bw.out.insert(bw.out.end(), raw + chunkOff, raw + chunkOff + rawSlice);
            chunkOff += rawSlice;
            continue;
        }

        if (anchor < chunkLen) {
            cmds.push_back({ anchor, chunkLen - anchor, 2u, 1u }); // trailing literals; copy skipped at produced==mlen
        }

        bw.WriteBits(finalMeta ? 1u : 0u, 1); // ISLAST
        if (finalMeta) bw.WriteBits(0u, 1);   // ISLASTEMPTY = 0
        bw.WriteBits(0u, 2);                  // MNIBBLES = 4
        bw.WriteBits(static_cast<uint32_t>(chunkLen - 1u), 16);
        if (!finalMeta) bw.WriteBits(0u, 1);  // ISUNCOMPRESSED = 0

        // NBLTYPES[0..2] = 1 (three 0 bits), NPOSTFIX=0 (2b), NDIRECT=0 (4b), ctxMode[0]=0 (2b), 2x ContextMap=1 (2b)
        bw.WriteBits(0u, 3);
        bw.WriteBits(0u, 2);
        bw.WriteBits(0u, 4);
        bw.WriteBits(0u, 2);
        bw.WriteBits(0u, 2);

        auto writeSingleClTree = [&](uint32_t targetClSym) {
            // Complex prefix code (hskip=0) with single active code-length symbol => 0 run bits!
            static constexpr uint8_t kClOrd[18] = { 1,2,3,4,0,5,17,6,16,7,8,9,10,11,12,13,14,15 };
            bw.WriteBits(0u, 2); // hskip = 0
            for (int i = 0; i < 18; ++i) {
                if (kClOrd[i] == targetClSym) bw.WriteBits(7u, 4); // v = 1 (4 bits: 0111)
                else bw.WriteBits(0u, 2);                          // v = 0 (2 bits: 00)
            }
        };

        // 1. Literal tree: all 256 bytes have code length 8
        writeSingleClTree(8u);

        // 2. IAC tree (704): 0..127 have length 0, 128..383 have length 8 (Kraft sum = 256 * 128 = 32768!)
        //    Encode clTree with sym 0 (len=1 => code 0) and sym 8 (len=1 => code 1)
        bw.WriteBits(0u, 2); // hskip = 0
        {
            static constexpr uint8_t kClOrd[18] = { 1,2,3,4,0,5,17,6,16,7,8,9,10,11,12,13,14,15 };
            uint32_t sp = 32u;
            for (int i = 0; i < 18 && sp > 0u; ++i) {
                if (kClOrd[i] == 0u || kClOrd[i] == 8u) {
                    bw.WriteBits(7u, 4); // v = 1 => cost 16
                    sp -= 16u;
                } else {
                    bw.WriteBits(0u, 2); // v = 0
                }
            }
            for (int s = 0; s < 128; ++s) bw.WriteBits(0u, 1); // sym 0 (length 0)
            for (int s = 0; s < 256; ++s) bw.WriteBits(1u, 1); // sym 8 (length 8)
        }

        // 3. Distance tree (64): all 64 symbols have code length 6 (Kraft sum = 64 * 512 = 32768!)
        writeSingleClTree(6u);

        size_t produced = 0u;
        for (const auto& cmd : cmds) {
            uint32_t ic = 0, ieb = 0, iev = 0;
            uint32_t cc = 0, ceb = 0, cev = 0;
            mapIns(cmd.insLen, ic, ieb, iev);
            mapCopy(cmd.copyLen, cc, ceb, cev);
            uint32_t iacSym = mapIacSym(ic, cc);
            bw.WriteBits(revBits(iacSym - 128u, 8), 8);
            if (ieb > 0u) bw.WriteBits(iev, static_cast<int>(ieb));
            if (ceb > 0u) bw.WriteBits(cev, static_cast<int>(ceb));

            for (size_t k = 0; k < cmd.insLen; ++k) {
                bw.WriteBits(revBits(src[cmd.litStart + k], 8), 8);
            }
            produced += cmd.insLen;
            if (produced == chunkLen) break;

            uint32_t dSym = 0, deb = 0, dev = 0;
            mapDist(cmd.dist, dSym, deb, dev);
            bw.WriteBits(revBits(dSym, 6), 6);
            if (deb > 0u) bw.WriteBits(dev, static_cast<int>(deb));
            produced += cmd.copyLen;
        }

        chunkOff += chunkLen;
        if (finalMeta) {
            bw.Flush();
            return bw.out;
        }
    }

    // Emit empty ISLAST meta-block if previous meta-block was non-last
    bw.WriteBits(1u, 1); // ISLAST = 1
    bw.WriteBits(1u, 1); // ISLASTEMPTY = 1
    bw.Flush();
    return bw.out;
}

void FinalizeUdfTagForTest(uint8_t* buf, uint16_t tagId, uint32_t tagLocation) {
    PutU16LE(buf + 0, tagId);
    PutU16LE(buf + 2, 3u); // UDF 2.xx descriptor version 3
    PutU32LE(buf + 12, tagLocation);
    uint8_t sum = 0u;
    for (int i = 0; i < 16; ++i) {
        if (i != 4) sum = static_cast<uint8_t>(sum + buf[i]);
    }
    buf[4] = sum;
}

std::vector<uint8_t> EncodeOstaCs0Utf16BEForTest(const std::wstring& s) {
    std::vector<uint8_t> out = { 16u }; // compId = 16 (UTF-16BE)
    for (wchar_t wc : s) {
        out.push_back(static_cast<uint8_t>((static_cast<uint16_t>(wc) >> 8u) & 0xFFu));
        out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(wc) & 0xFFu));
    }
    return out;
}

std::vector<uint8_t> BuildUdfTestImage(
    const std::wstring& unicodeDirName,
    const std::wstring& inlineFileName, const std::string& inlinePayload,
    const std::wstring& shortAdFileName, const std::string& shortAdPayload,
    bool useUdf250MetadataPart = false)
{
    const size_t secSz = 2048u;
    const uint32_t partStart = 64u;
    const uint32_t metaBasePhysLba = useUdf250MetadataPart ? 30u : 0u;
    const uint16_t metaPartRef     = useUdf250MetadataPart ? 1u  : 0u;
    std::vector<uint8_t> img(280u * secSz, 0u);

    std::memcpy(img.data() + 16u * secSz + 1u, "BEA01\x01", 6);
    std::memcpy(img.data() + 17u * secSz + 1u, "NSR03\x01", 6);
    std::memcpy(img.data() + 18u * secSz + 1u, "TEA01\x01", 6);

    // AVDP [2] @ sector 256
    {
        uint8_t* avdp = img.data() + 256u * secSz;
        PutU32LE(avdp + 16, 3u * static_cast<uint32_t>(secSz)); // MVDS byte len
        PutU32LE(avdp + 20, 32u);                               // MVDS sector loc = 32
        FinalizeUdfTagForTest(avdp, 2u, 256u);
    }

    // MVDS @ sectors 32..34
    {
        uint8_t* pd = img.data() + 32u * secSz;
        PutU16LE(pd + 22,  0u);        // PartitionNumber = 0
        PutU32LE(pd + 188, partStart); // PartitionStartingLocation = 64
        PutU32LE(pd + 192, 180u);      // PartitionLength = 180
        FinalizeUdfTagForTest(pd, 5u, 32u);

        uint8_t* lvd = img.data() + 33u * secSz;
        lvd[84] = 8u;
        std::memcpy(lvd + 85, "UDF_TEST", 8);
        lvd[84 + 127] = 9u;
        PutU32LE(lvd + 212, static_cast<uint32_t>(secSz)); // LogicalBlockSize = 2048
        PutU16LE(lvd + 240, 0x0250u);                      // UDF 2.50 revision
        PutU32LE(lvd + 248, static_cast<uint32_t>(secSz));
        PutU32LE(lvd + 252, 0u);          // FSD @ LBA 0 in metaPartRef
        PutU16LE(lvd + 256, metaPartRef); // partRef = 0 (Type 1) or 1 (Type 2 Metadata Partition!)
        PutU32LE(lvd + 264, useUdf250MetadataPart ? 70u : 6u);
        PutU32LE(lvd + 268, useUdf250MetadataPart ? 2u  : 1u);
        lvd[440] = 1u; // Type 1 Physical Partition Map (partRef 0 -> phys part 0)
        lvd[441] = 6u;
        PutU16LE(lvd + 442, 1u);
        PutU16LE(lvd + 444, 0u);
        if (useUdf250MetadataPart) {
            uint8_t* pm2 = lvd + 446u;
            pm2[0] = 2u;  // Type 2 Partition Map
            pm2[1] = 64u;
            std::memcpy(pm2 + 5, "*UDF Metadata Partition", 23);
            PutU16LE(pm2 + 36, 1u);
            PutU16LE(pm2 + 38, 0u);          // Physical PartitionNumber = 0
            PutU32LE(pm2 + 40, 20u);         // MetadataFileLocation = phys LBA 20!
            PutU32LE(pm2 + 44, 0xFFFFFFFFu); // MetadataMirrorFileLocation
            PutU32LE(pm2 + 52, 32u);         // AllocUnitSize
        }
        FinalizeUdfTagForTest(lvd, 6u, 33u);

        uint8_t* td = img.data() + 34u * secSz;
        FinalizeUdfTagForTest(td, 8u, 34u);
    }

    auto pBlk = [&](uint32_t physLba) -> uint8_t* {
        return img.data() + (partStart + physLba) * secSz;
    };
    auto mBlk = [&](uint32_t metaLba) -> uint8_t* {
        return pBlk(metaBasePhysLba + metaLba);
    };

    if (useUdf250MetadataPart) {
        // Metadata File ICB [266] @ physical LBA 20 -> maps Metadata Partition LBA 0..15 to physical LBA 30..45!
        uint8_t* mfe = pBlk(20u);
        mfe[27] = 250u; // UDF 2.50 Metadata File
        PutU16LE(mfe + 34, 0u); // Short AD on physical partition 0
        PutU64LE(mfe + 56, 16u * secSz);
        PutU32LE(mfe + 212, 8u);
        PutU32LE(mfe + 216, static_cast<uint32_t>(16u * secSz));
        PutU32LE(mfe + 220, metaBasePhysLba); // physical LBA 30!
        FinalizeUdfTagForTest(mfe, 266u, 20u);
    }

    auto appendFid = [&](std::vector<uint8_t>& dirBuf, uint32_t dirLba, uint8_t fileChars,
                         uint32_t childLba, const std::vector<uint8_t>& cs0Ident)
    {
        size_t off = dirBuf.size();
        size_t rawLen = 38u + cs0Ident.size();
        size_t padLen = (rawLen + 3u) & ~size_t(3u);
        dirBuf.resize(off + padLen, 0u);
        uint8_t* fid = dirBuf.data() + off;
        PutU16LE(fid + 16, 1u);
        fid[18] = fileChars;
        fid[19] = static_cast<uint8_t>(cs0Ident.size());
        PutU32LE(fid + 20, 2048u);
        PutU32LE(fid + 24, childLba);
        PutU16LE(fid + 28, metaPartRef);
        PutU16LE(fid + 36, 0u);
        if (!cs0Ident.empty()) std::memcpy(fid + 38u, cs0Ident.data(), cs0Ident.size());
        FinalizeUdfTagForTest(fid, 257u, dirLba);
    };

    // Metadata LBA 0: FSD [256] -> Root ICB @ Metadata LBA 1
    {
        uint8_t* fsd = mBlk(0u);
        PutU32LE(fsd + 400, static_cast<uint32_t>(secSz));
        PutU32LE(fsd + 404, 1u);
        PutU16LE(fsd + 408, metaPartRef);
        FinalizeUdfTagForTest(fsd, 256u, 0u);
    }

    // Metadata LBA 2: Root Directory FIDs (parent + unicode subdir @ LBA 3 + shortAdFile @ LBA 5)
    std::vector<uint8_t> rootDirBytes;
    appendFid(rootDirBytes, 2u, 0x08u | 0x02u, 1u, {}); // Parent ".."
    appendFid(rootDirBytes, 2u, 0x02u, 3u, EncodeOstaCs0Utf16BEForTest(unicodeDirName));
    appendFid(rootDirBytes, 2u, 0x00u, 5u, EncodeOstaCs0Utf16BEForTest(shortAdFileName));
    std::memcpy(mBlk(2u), rootDirBytes.data(), rootDirBytes.size());

    // Metadata LBA 1: Root Directory Extended File Entry [266] (Short AD -> Metadata LBA 2)
    {
        uint8_t* efe = mBlk(1u);
        efe[27] = 4u;             // Directory
        PutU16LE(efe + 34, 0u);   // allocType = 0 (Short AD inside metaPartRef)
        PutU64LE(efe + 56, rootDirBytes.size());
        PutU32LE(efe + 212, 8u);  // lenAllocDescs = 8
        PutU32LE(efe + 216, static_cast<uint32_t>(rootDirBytes.size()));
        PutU32LE(efe + 220, 2u);  // Metadata LBA 2
        FinalizeUdfTagForTest(efe, 266u, 1u);
    }

    // Metadata LBA 3: Subdirectory File Entry [261] using Inline ICB (allocType = 3) -> inlineFile @ LBA 4
    {
        std::vector<uint8_t> subDirBytes;
        appendFid(subDirBytes, 3u, 0x08u | 0x02u, 1u, {});
        appendFid(subDirBytes, 3u, 0x00u, 4u, EncodeOstaCs0Utf16BEForTest(inlineFileName));
        uint8_t* fe = mBlk(3u);
        fe[27] = 4u;            // Directory
        PutU16LE(fe + 34, 3u);  // allocType = 3 (Inline ICB!)
        PutU64LE(fe + 56, subDirBytes.size());
        PutU32LE(fe + 172, static_cast<uint32_t>(subDirBytes.size()));
        std::memcpy(fe + 176u, subDirBytes.data(), subDirBytes.size());
        FinalizeUdfTagForTest(fe, 261u, 3u);
    }

    // Metadata LBA 4: Inline File Entry [261] (allocType = 3 Inline ICB!)
    {
        uint8_t* fe = mBlk(4u);
        fe[27] = 5u;            // Regular file
        PutU16LE(fe + 34, 3u);  // allocType = 3 (Inline ICB!)
        PutU64LE(fe + 56, inlinePayload.size());
        PutU32LE(fe + 172, static_cast<uint32_t>(inlinePayload.size()));
        std::memcpy(fe + 176u, inlinePayload.data(), inlinePayload.size());
        FinalizeUdfTagForTest(fe, 261u, 4u);
    }

    // Metadata LBA 5: Multi-Sector File Extended File Entry [266]
    // Uses Short AD (allocType=0) in standard mode, and Long AD (allocType=1 -> phys partRef 0, LBA 6) in UDF 2.50 Metadata mode!
    {
        uint8_t* efe = mBlk(5u);
        efe[27] = 5u; // Regular file
        PutU64LE(efe + 56, shortAdPayload.size());
        if (!useUdf250MetadataPart) {
            PutU16LE(efe + 34, 0u);  // allocType = 0 (Short AD!)
            PutU32LE(efe + 212, 8u); // 1 Short AD
            PutU32LE(efe + 216, static_cast<uint32_t>(shortAdPayload.size()));
            PutU32LE(efe + 220, 6u); // physical LBA 6
        } else {
            PutU16LE(efe + 34, 1u);   // allocType = 1 (Long AD -> partRef 0, LBA 6!)
            PutU32LE(efe + 212, 16u); // 1 Long AD
            PutU32LE(efe + 216, static_cast<uint32_t>(shortAdPayload.size()));
            PutU32LE(efe + 220, 6u);
            PutU16LE(efe + 224, 0u);  // partRef 0 (Physical Partition!)
        }
        FinalizeUdfTagForTest(efe, 266u, 5u);
        std::memcpy(pBlk(6u), shortAdPayload.data(), shortAdPayload.size());
    }

    return img;
}

// Wraps a 2048B-sector optical image into a Nero .nrg ("NER5" + "DAOX" + 2352-byte Mode 1 raw sectors + pregap!)
std::vector<uint8_t> WrapOpticalImageInNeroNer5Raw2352(const std::vector<uint8_t>& isoOrUdf2048) {
    const size_t numSecs = isoOrUdf2048.size() / 2048u;
    const size_t pregapBytes = 2u * 2352u; // 2-sector index0 pregap before Track 1 index1!
    std::vector<uint8_t> nrg(pregapBytes, 0xAAu);
    nrg.reserve(pregapBytes + numSecs * 2352u + 128u);

    static const uint8_t kSync[12] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
    };
    for (size_t s = 0; s < numSecs; ++s) {
        size_t base = nrg.size();
        nrg.resize(base + 2352u, 0u);
        std::memcpy(nrg.data() + base, kSync, 12);
        nrg[base + 15] = 1u; // Mode 1 (user data at +16..+2063)
        std::memcpy(nrg.data() + base + 16u, isoOrUdf2048.data() + s * 2048u, 2048u);
    }

    const uint64_t trackEndOff = nrg.size();
    const uint64_t trailerOff  = nrg.size();

    // DAOX chunk: 4B "DAOX" + u32BE(64) + 22B header + 42B track 1 record
    nrg.insert(nrg.end(), { 'D','A','O','X' });
    AppendU32BE(nrg, 64u);
    size_t db = nrg.size();
    nrg.resize(db + 64u, 0u);
    PutU32BE(nrg.data() + db + 0, 64u);
    nrg[db + 20] = 1u; // first track
    nrg[db + 21] = 1u; // last track
    PutU16BE(nrg.data() + db + 22 + 12, 2352u); // raw sector size = 2352!
    nrg[db + 22 + 14] = 0x07u;                  // Mode 1 raw
    PutU64BE(nrg.data() + db + 22 + 18, 0ULL);          // index0 (pregap)
    PutU64BE(nrg.data() + db + 22 + 26, pregapBytes);   // index1 (Track 1 data start!)
    PutU64BE(nrg.data() + db + 22 + 34, trackEndOff);   // track end

    // END! chunk + NER5 12-byte footer
    nrg.insert(nrg.end(), { 'E','N','D','!', 0,0,0,0 });
    nrg.insert(nrg.end(), { 'N','E','R','5' });
    AppendU64BE(nrg, trailerOff);
    return nrg;
}

std::string MakeWarc11Record(
    const std::string& warcType,
    const std::string& targetUri,
    const std::string& contentType,
    const std::string& payload)
{
    std::string r = "WARC/1.1\r\n";
    r += "WARC-Type: " + warcType + "\r\n";
    r += "WARC-Date: 2026-05-12T14:30:00Z\r\n";
    if (!targetUri.empty()) r += "WARC-Target-URI: " + targetUri + "\r\n";
    r += "Content-Type: " + contentType + "\r\n";
    r += "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
    r += payload;
    r += "\r\n\r\n";
    return r;
}

std::vector<uint8_t> EncodeRawImageAsAndroidSimg(const std::vector<uint8_t>& rawImg, uint32_t blkSz) {
    const uint32_t totalBlks = static_cast<uint32_t>(rawImg.size() / blkSz);
    struct SChunk { uint16_t type; uint32_t blks; uint32_t fillVal; size_t rawOff; };
    std::vector<SChunk> chunks;

    for (uint32_t b = 0; b < totalBlks; ++b) {
        const uint8_t* p = rawImg.data() + static_cast<size_t>(b) * blkSz;
        bool allZero = true;
        uint32_t pat0 = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        bool allFill = true;
        for (size_t i = 0; i < blkSz; i += 4u) {
            uint32_t w = static_cast<uint32_t>(p[i]) | (static_cast<uint32_t>(p[i + 1]) << 8) |
                         (static_cast<uint32_t>(p[i + 2]) << 16) | (static_cast<uint32_t>(p[i + 3]) << 24);
            if (w != 0u) allZero = false;
            if (w != pat0) allFill = false;
        }
        uint16_t tp = allZero ? 0xCAC3u : (allFill ? 0xCAC2u : 0xCAC1u);
        if (!chunks.empty() && chunks.back().type == tp &&
            (tp != 0xCAC2u || chunks.back().fillVal == pat0) &&
            (tp != 0xCAC1u))
        {
            ++chunks.back().blks;
        } else if (!chunks.empty() && tp == 0xCAC1u && chunks.back().type == 0xCAC1u &&
                   chunks.back().rawOff + static_cast<size_t>(chunks.back().blks) * blkSz == static_cast<size_t>(b) * blkSz)
        {
            ++chunks.back().blks;
        } else {
            chunks.push_back({ tp, 1u, pat0, static_cast<size_t>(b) * blkSz });
        }
    }

    // Also append CHUNK_TYPE_CRC32 (0xCAC4) trailer chunk
    const uint32_t totalChunkRecords = static_cast<uint32_t>(chunks.size() + 1u);
    std::vector<uint8_t> simg(28u, 0u);
    PutU32LE(simg.data() + 0,  0xED26FF3Au); // SPARSE_HEADER_MAGIC
    PutU16LE(simg.data() + 4,  1u);          // major_version = 1
    PutU16LE(simg.data() + 6,  0u);          // minor_version = 0
    PutU16LE(simg.data() + 8,  28u);         // file_hdr_sz = 28
    PutU16LE(simg.data() + 10, 12u);         // chunk_hdr_sz = 12
    PutU32LE(simg.data() + 12, blkSz);
    PutU32LE(simg.data() + 16, totalBlks);
    PutU32LE(simg.data() + 20, totalChunkRecords);
    PutU32LE(simg.data() + 24, Checksum::Crc32(rawImg.data(), rawImg.size()));

    for (const auto& ch : chunks) {
        uint32_t payloadBytes = (ch.type == 0xCAC1u) ? (ch.blks * blkSz) : ((ch.type == 0xCAC2u) ? 4u : 0u);
        AppendU16LE(simg, ch.type);
        AppendU16LE(simg, 0u);
        AppendU32LE(simg, ch.blks);
        AppendU32LE(simg, 12u + payloadBytes);
        if (ch.type == 0xCAC1u) {
            simg.insert(simg.end(), rawImg.data() + ch.rawOff, rawImg.data() + ch.rawOff + payloadBytes);
        } else if (ch.type == 0xCAC2u) {
            AppendU32LE(simg, ch.fillVal);
        }
    }

    // CHUNK_TYPE_CRC32 (0xCAC4)
    AppendU16LE(simg, 0xCAC4u);
    AppendU16LE(simg, 0u);
    AppendU32LE(simg, 0u);
    AppendU32LE(simg, 16u);
    AppendU32LE(simg, Checksum::Crc32(rawImg.data(), rawImg.size()));
    return simg;
}

} // namespace

// ============================================================================
// Stage 15 Entry Point: Archive, Installer, Disk Image & Game Container Tests
// ============================================================================
void RunFormatTestsArchives(TestCounter& tc) {
    const std::wstring baseDir = ScratchTestDir() + L"\\Stage15_Archives";
    RemoveDirTreeForStage15(baseDir);
    CreateDirectoryW(baseDir.c_str(), nullptr);

    const std::wstring tdDir = TestDataDir();
    const uint64_t guardBefore = ReaderGuard::StoppedCount();

    // ------------------------------------------------------------------------
    // Test 15.1: Repository testdata/ Samples (ZIP, TAR, optional 7z/RAR) & RAR4/RAR5
    // ------------------------------------------------------------------------
    {
        ArchiveSummaryInfo zipSum, tarSum;
        std::vector<uint8_t> zipOut, tarOut;
        bool okZip = ArchiveExplorer::InspectArchiveFile(tdDir + L"\\sample_bundle.zip", zipSum) &&
                     zipSum.valid && !zipSum.entries.empty() &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(tdDir + L"\\sample_bundle.zip", zipSum.entries[0].pathInArchive, zipOut);
        bool okTar = ArchiveExplorer::InspectArchiveFile(tdDir + L"\\posix_archive.tar", tarSum) &&
                     tarSum.valid && !tarSum.entries.empty();
        for (const auto& e : tarSum.entries) {
            if (!e.isDirectory) {
                okTar = okTar && ArchiveExplorer::ExtractSingleEntryToMemory(tdDir + L"\\posix_archive.tar", e.pathInArchive, tarOut) &&
                        tarOut.size() == e.uncompressedSize;
                break;
            }
        }

        // If sample.7z or sample.rar exist in testdata/, verify them too
        bool okOpt7z = true, okOptRar = true;
        if (GetFileAttributesW((tdDir + L"\\sample.7z").c_str()) != INVALID_FILE_ATTRIBUTES) {
            ArchiveSummaryInfo s7;
            okOpt7z = ArchiveExplorer::InspectArchiveFile(tdDir + L"\\sample.7z", s7) && s7.valid && !s7.entries.empty();
        }
        if (GetFileAttributesW((tdDir + L"\\sample.rar").c_str()) != INVALID_FILE_ATTRIBUTES) {
            ArchiveSummaryInfo sr;
            okOptRar = ArchiveExplorer::InspectArchiveFile(tdDir + L"\\sample.rar", sr) && sr.valid && !sr.entries.empty();
        }

        tc.Record(okZip && okTar && okOpt7z && okOptRar,
            L"Stage15.Archives.TestdataSamples_Zip_Tar_7z_Rar",
            L"zip=" + std::to_wstring(zipSum.entries.size()) + L", tar=" + std::to_wstring(tarSum.entries.size()));
    }

    // ------------------------------------------------------------------------
    // Test 15.2: Two Distinct .7z Archives with 0x17 kEncodedHeader (LZMA1) +
    //            LZMA2 Solid Streams + BCJ x86 Filter + Native Extraction
    // ------------------------------------------------------------------------
    {
        std::string p1a = "Explorer2 7-Zip Solid Payload #1 — Alpha! Explorer2 7-Zip Solid Payload #1 — Alpha!\n";
        std::string p1b = "Second file inside solid LZMA2 folder with repeated tokens repeated tokens repeated tokens!\n";

        // Second .7z archive uses BCJ x86 (0x03030103) + LZMA2 with x86 E8/E9 CALL/JMP opcodes!
        std::string p2exe(256, '\x90');
        p2exe[16] = '\xE8'; PutU32LE(reinterpret_cast<uint8_t*>(&p2exe[17]), 0x00001234u);
        p2exe[48] = '\xE9'; PutU32LE(reinterpret_cast<uint8_t*>(&p2exe[49]), 0xFFFFFC00u);
        p2exe += "BCJ_X86_FILTER_DECODED_OK_BCJ_X86_FILTER_DECODED_OK!";

        auto bytes7z1 = Build7zWithEncodedHeader({ { L"docs/alpha.txt", p1a }, { L"docs/beta.txt", p1b } }, false);
        auto bytes7z2 = Build7zWithEncodedHeader({ { L"bin/launcher.exe", p2exe } }, true);

        std::wstring f1 = baseDir + L"\\encoded_solid_1.7z";
        std::wstring f2 = baseDir + L"\\encoded_bcj_2.7z";
        PathUtils::WriteFileBytes(f1, bytes7z1.data(), bytes7z1.size());
        PathUtils::WriteFileBytes(f2, bytes7z2.data(), bytes7z2.size());

        ArchiveSummaryInfo s1, s2;
        std::vector<uint8_t> out1a, out1b, out2;
        bool ok1 = ArchiveExplorer::InspectArchiveFile(f1, s1) && s1.valid && s1.entries.size() == 2 &&
                   FindEntryCI(s1, L"docs/alpha.txt") != nullptr &&
                   FindEntryCI(s1, L"docs/beta.txt") != nullptr &&
                   FindEntryCI(s1, L"docs/alpha.txt")->uncompressedSize == p1a.size() &&
                   FindEntryCI(s1, L"docs/beta.txt")->uncompressedSize == p1b.size() &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(f1, L"docs/alpha.txt", out1a) && BytesMatchStr(out1a, p1a) &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(f1, L"docs/beta.txt", out1b) && BytesMatchStr(out1b, p1b);

        bool ok2 = ArchiveExplorer::InspectArchiveFile(f2, s2) && s2.valid && s2.entries.size() == 1 &&
                   FindEntryCI(s2, L"bin/launcher.exe") != nullptr &&
                   FindEntryCI(s2, L"bin/launcher.exe")->uncompressedSize == p2exe.size() &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(f2, L"bin/launcher.exe", out2) && BytesMatchStr(out2, p2exe);

        std::wstring extFolder = baseDir + L"\\ext_7z";
        bool okFolder = ArchiveExplorer::ExtractEntireArchiveToFolder(f1, extFolder);
        std::vector<uint8_t> diskBeta;
        okFolder = okFolder && PathUtils::ReadFileBytes(extFolder + L"\\docs\\beta.txt", 65536, diskBeta) && BytesMatchStr(diskBeta, p1b);

        tc.Record(ok1 && ok2 && okFolder && (s1.entries[0].pathInArchive != s2.entries[0].pathInArchive),
            L"Stage15.Archives.7Zip_EncodedHeader_LZMA1_LZMA2_BCJx86",
            L"solid2=" + std::to_wstring(ok1) + L", bcjX86=" + std::to_wstring(ok2) + L", folderExt=" + std::to_wstring(okFolder));
    }

    // ------------------------------------------------------------------------
    // Test 15.3: Two Distinct Windows Cabinet (.cab) Archives (Stored & MSZIP)
    //            Extracted Natively via Win32 SetupIterateCabinetW
    // ------------------------------------------------------------------------
    {
        std::string c1 = "Cabinet #1 Stored Configuration Payload = TRUE\r\n";
        std::string c2a = "Cabinet #2 MSZIP Deflate Payload — Repeating String Repeating String Repeating String!\r\n";
        std::string c2b = "Nested driver INF content inside MSZIP cabinet archive.\r\n";

        auto cab1 = BuildCabinetMscf({ { "config/setup.ini", c1 } }, false);
        auto cab2 = BuildCabinetMscf({ { "drivers/net.inf", c2b }, { "readme_cab.txt", c2a } }, true);

        std::wstring p1 = baseDir + L"\\pkg1.cab";
        std::wstring p2 = baseDir + L"\\pkg2_mszip.cab";
        PathUtils::WriteFileBytes(p1, cab1.data(), cab1.size());
        PathUtils::WriteFileBytes(p2, cab2.data(), cab2.size());

        ArchiveSummaryInfo s1, s2;
        std::vector<uint8_t> e1, e2a, e2b;
        bool ok1 = ArchiveExplorer::InspectArchiveFile(p1, s1) && s1.valid && s1.entries.size() == 1 &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p1, L"config/setup.ini", e1) && BytesMatchStr(e1, c1);
        bool ok2 = ArchiveExplorer::InspectArchiveFile(p2, s2) && s2.valid && s2.entries.size() == 2 &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p2, L"readme_cab.txt", e2a) && BytesMatchStr(e2a, c2a) &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p2, L"drivers/net.inf", e2b) && BytesMatchStr(e2b, c2b);

        std::wstring cabOutDir = baseDir + L"\\ext_cab";
        bool okBulk = ArchiveExplorer::ExtractEntireArchiveToFolder(p2, cabOutDir);
        std::vector<uint8_t> onDisk;
        okBulk = okBulk && PathUtils::ReadFileBytes(cabOutDir + L"\\drivers\\net.inf", 65536, onDisk) && BytesMatchStr(onDisk, c2b);

        tc.Record(ok1 && ok2 && okBulk,
            L"Stage15.Archives.WindowsCab_SetupIterateCabinetW_StoredAndMSZIP",
            L"stored=" + std::to_wstring(ok1) + L", mszip=" + std::to_wstring(ok2) + L", bulk=" + std::to_wstring(okBulk));
    }

    // ------------------------------------------------------------------------
    // Test 15.4: Bethesda .bsa (v104 Raw & Zlib+EmbeddedNames) & .ba2 (GNRL + DX10 DDS)
    // ------------------------------------------------------------------------
    {
        std::string nif1 = "Gamebryo File Format, Version 20.2.0.7 — IronSword Mesh";
        std::string psc2 = "ScriptName DragonQuest Extends Quest ; Skyrim SE Zlib Compressed + Embedded bString! Skyrim SE Zlib!";
        auto bsa1 = BuildBethesdaBsaV104("meshes/weapons/iron", { { "ironsword.nif", nif1 } }, false, false);
        auto bsa2 = BuildBethesdaBsaV104("scripts/source", { { "dragonquest.psc", psc2 } }, true, true);

        std::wstring bp1 = baseDir + L"\\oblivion_1.bsa";
        std::wstring bp2 = baseDir + L"\\skyrim_2.bsa";
        PathUtils::WriteFileBytes(bp1, bsa1.data(), bsa1.size());
        PathUtils::WriteFileBytes(bp2, bsa2.data(), bsa2.size());

        ArchiveSummaryInfo bs1, bs2;
        std::vector<uint8_t> bo1, bo2;
        bool okBsa1 = ArchiveExplorer::InspectArchiveFile(bp1, bs1) && bs1.valid && bs1.entries.size() == 1 &&
                      FindEntryCI(bs1, L"meshes/weapons/iron/ironsword.nif") != nullptr &&
                      FindEntryCI(bs1, L"asset_0.nif") == nullptr &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(bp1, L"meshes/weapons/iron/ironsword.nif", bo1) &&
                      BytesMatchStr(bo1, nif1);
        bool okBsa2 = ArchiveExplorer::InspectArchiveFile(bp2, bs2) && bs2.valid && bs2.entries.size() == 1 &&
                      FindEntryCI(bs2, L"scripts/source/dragonquest.psc") != nullptr &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(bp2, L"scripts/source/dragonquest.psc", bo2) &&
                      BytesMatchStr(bo2, psc2);

        std::string gnrlData = "Fallout 4 Material BGSM JSON Payload — Zlib Compressed inside GNRL BA2! Zlib Compressed!";
        std::vector<uint8_t> dx10Mip(4 * 4 * 4, 0xCCu);
        auto ba2Gnrl = BuildBethesdaBa2Gnrl({ { "Materials/Armor/VaultSuit.bgsm", gnrlData } });
        auto ba2Dx10 = BuildBethesdaBa2Dx10("Textures/Armor/VaultSuit_d.dds", 4, 4, dx10Mip);

        std::wstring ap1 = baseDir + L"\\main_gnrl.ba2";
        std::wstring ap2 = baseDir + L"\\textures_dx10.ba2";
        PathUtils::WriteFileBytes(ap1, ba2Gnrl.data(), ba2Gnrl.size());
        PathUtils::WriteFileBytes(ap2, ba2Dx10.data(), ba2Dx10.size());

        ArchiveSummaryInfo as1, as2;
        std::vector<uint8_t> ao1, ao2;
        bool okBa2Gnrl = ArchiveExplorer::InspectArchiveFile(ap1, as1) && as1.valid && as1.entries.size() == 1 &&
                         FindEntryCI(as1, L"Materials/Armor/VaultSuit.bgsm") != nullptr &&
                         FindEntryCI(as1, L"asset_0.dds") == nullptr &&
                         ArchiveExplorer::ExtractSingleEntryToMemory(ap1, L"Materials/Armor/VaultSuit.bgsm", ao1) &&
                         BytesMatchStr(ao1, gnrlData);
        bool okBa2Dx10 = ArchiveExplorer::InspectArchiveFile(ap2, as2) && as2.valid && as2.entries.size() == 1 &&
                         FindEntryCI(as2, L"Textures/Armor/VaultSuit_d.dds") != nullptr &&
                         ArchiveExplorer::ExtractSingleEntryToMemory(ap2, L"Textures/Armor/VaultSuit_d.dds", ao2) &&
                         ao2.size() == 148 + dx10Mip.size() &&
                         std::memcmp(ao2.data(), "DDS ", 4) == 0 &&
                         std::memcmp(ao2.data() + 84, "DX10", 4) == 0 &&
                         std::memcmp(ao2.data() + 148, dx10Mip.data(), dx10Mip.size()) == 0;

        tc.Record(okBsa1 && okBsa2 && okBa2Gnrl && okBa2Dx10,
            L"Stage15.Archives.Bethesda_BSA_v104_And_BA2_GNRL_DX10",
            L"bsaRaw=" + std::to_wstring(okBsa1) + L", bsaZlib=" + std::to_wstring(okBsa2) +
            L", ba2Gnrl=" + std::to_wstring(okBa2Gnrl) + L", ba2Dx10Dds=" + std::to_wstring(okBa2Dx10));
    }

    // ------------------------------------------------------------------------
    // Test 15.5: Two Distinct RedHat .rpm Packages (Metadata Tags + Gzip & XZ CPIO)
    // ------------------------------------------------------------------------
    {
        std::string r1Body = "#!/bin/sh\necho 'Hello from RPM #1 gzipped CPIO'\n";
        std::string r2Conf = "[daemon]\nport = 8080\nworkers = 4\n";
        std::string r2Bin  = "ELF_MOCK_PAYLOAD_FROM_XZ_COMPRESSED_RPM_CPIO_ARCHIVE!";

        auto rpm1 = BuildRedHatRpm("hello-tool-1.0", { { "usr/bin/hello-tool", r1Body } }, false);
        auto rpm2 = BuildRedHatRpm("net-svc-2.4", { { "etc/netsvc/daemon.conf", r2Conf }, { "usr/sbin/netsvcd", r2Bin } }, true);

        std::wstring rp1 = baseDir + L"\\hello-1.0.rpm";
        std::wstring rp2 = baseDir + L"\\netsvc-2.4.rpm";
        PathUtils::WriteFileBytes(rp1, rpm1.data(), rpm1.size());
        PathUtils::WriteFileBytes(rp2, rpm2.data(), rpm2.size());

        ArchiveSummaryInfo rs1, rs2;
        std::vector<uint8_t> ro1, ro2a, ro2b;
        bool ok1 = ArchiveExplorer::InspectArchiveFile(rp1, rs1) && rs1.valid && rs1.entries.size() == 1 &&
                   FindEntryCI(rs1, L"usr/bin/hello-tool") != nullptr &&
                   FindEntryCI(rs1, L"usr/bin/hello-tool")->uncompressedSize == r1Body.size() &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(rp1, L"usr/bin/hello-tool", ro1) && BytesMatchStr(ro1, r1Body);
        bool ok2 = ArchiveExplorer::InspectArchiveFile(rp2, rs2) && rs2.valid && rs2.entries.size() == 2 &&
                   FindEntryCI(rs2, L"etc/netsvc/daemon.conf") != nullptr &&
                   FindEntryCI(rs2, L"etc/netsvc/daemon.conf")->uncompressedSize == r2Conf.size() &&
                   FindEntryCI(rs2, L"usr/sbin/netsvcd") != nullptr &&
                   FindEntryCI(rs2, L"usr/sbin/netsvcd")->uncompressedSize == r2Bin.size() &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(rp2, L"etc/netsvc/daemon.conf", ro2a) && BytesMatchStr(ro2a, r2Conf) &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(rp2, L"usr/sbin/netsvcd", ro2b) && BytesMatchStr(ro2b, r2Bin);

        tc.Record(ok1 && ok2,
            L"Stage15.Archives.RedHatRPM_MetadataTags_And_GzipXzCpioExtract",
            L"rpmGz=" + std::to_wstring(ok1) + L", rpmXz=" + std::to_wstring(ok2));
    }

    // ------------------------------------------------------------------------
    // Test 15.6: Two Distinct Windows Installer .msi Packages
    //            (OLE2 Compound + MSI 6-Bit Demangling + Embedded MSCF .cab)
    // ------------------------------------------------------------------------
    {
        std::string tbl1 = "Property\0File\0Component\0";
        std::string prop1 = "ProductName=Explorer2 SDK\r\nProductVersion=2.0.0\r\n";
        std::string dll2 = "MZ_EMBEDDED_MSI_CABINET_DLL_PAYLOAD_EXTRACTED_OK_REPEATED_OK!";
        auto embeddedCab = BuildCabinetMscf({ { "bin/core_engine.dll", dll2 } }, true);

        auto msi1 = BuildOle2MsiPackage({
            { EncodeMsiMangledName("!_Tables"), std::vector<uint8_t>(tbl1.begin(), tbl1.end()) },
            { EncodeMsiMangledName("!Property"), std::vector<uint8_t>(prop1.begin(), prop1.end()) }
        });
        auto msi2 = BuildOle2MsiPackage({
            { EncodeMsiMangledName("!_Tables"), std::vector<uint8_t>(tbl1.begin(), tbl1.end()) },
            { EncodeMsiMangledName("Data1.cab"), embeddedCab }
        });

        std::wstring mp1 = baseDir + L"\\sdk_setup.msi";
        std::wstring mp2 = baseDir + L"\\app_with_cab.msi";
        PathUtils::WriteFileBytes(mp1, msi1.data(), msi1.size());
        PathUtils::WriteFileBytes(mp2, msi2.data(), msi2.size());

        ArchiveSummaryInfo ms1, ms2;
        std::vector<uint8_t> mo1, mo2;
        bool ok1 = ArchiveExplorer::InspectArchiveFile(mp1, ms1) && ms1.valid &&
                   FindEntryCI(ms1, L"!_Tables") != nullptr &&
                   FindEntryCI(ms1, L"!Property") != nullptr &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(mp1, L"!Property", mo1) && BytesMatchStr(mo1, prop1);
        bool ok2 = ArchiveExplorer::InspectArchiveFile(mp2, ms2) && ms2.valid &&
                   FindEntryCI(ms2, L"Data1.cab") != nullptr &&
                   FindEntryCI(ms2, L"Data1.cab/bin/core_engine.dll") != nullptr &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(mp2, L"Data1.cab/bin/core_engine.dll", mo2) &&
                   BytesMatchStr(mo2, dll2);

        tc.Record(ok1 && ok2,
            L"Stage15.Archives.WindowsInstallerMSI_Ole2Demangle_And_EmbeddedCab",
            L"msiTables=" + std::to_wstring(ok1) + L", embeddedCabDll=" + std::to_wstring(ok2));
    }

    // ------------------------------------------------------------------------
    // Test 15.7: Stream Decompressors (.xz/.txz, .bz2/.tbz2, .lz4/.tlz4, .zst/.tzst)
    //            Single-File Exact Size/Extract + Automatic Inner POSIX TAR Browsing
    // ------------------------------------------------------------------------
    {
        std::string rawXz  = "Single XZ LZMA2+BCJ stream payload — repeated phrase repeated phrase repeated phrase!";
        std::string rawBz2 = "BZip2 Burrows-Wheeler + MTF + RLE1 AAAAAAAABBBBBBBBCCCCCCCC payload test!";
        std::string rawLz4 = "LZ4 Frame native token decompression — fast backreference fast backreference fast backreference!";
        std::string rawZst = "ZSTD_COMPRESSED_BLOCK_TEST! ZSTD_COMPRESSED_BLOCK_TEST! RFC 8878 sequence & offset decode!";

        auto innerTarBytes = BuildPosixTar({
            { "pkg/manifest.json", "{\"name\":\"explorer2\",\"ok\":true}\n" },
            { "pkg/src/main.cpp",  "int main() { return 0; }\n" }
        });

        auto xzSingle  = BuildXzStream(reinterpret_cast<const uint8_t*>(rawXz.data()), rawXz.size(), true);
        auto txzBytes  = BuildXzStream(innerTarBytes.data(), innerTarBytes.size(), false);
        auto bz2Single = BuildBzip2Stream(reinterpret_cast<const uint8_t*>(rawBz2.data()), rawBz2.size());
        auto tbz2Bytes = BuildBzip2Stream(innerTarBytes.data(), innerTarBytes.size());
        auto lz4Single = BuildLz4Frame(reinterpret_cast<const uint8_t*>(rawLz4.data()), rawLz4.size());
        auto tlz4Bytes = BuildLz4Frame(innerTarBytes.data(), innerTarBytes.size());
        auto zstSingle = BuildZstdCompressedFrame(reinterpret_cast<const uint8_t*>(rawZst.data()), rawZst.size());
        auto tzstBytes = BuildZstdCompressedFrame(innerTarBytes.data(), innerTarBytes.size());

        auto verifySingleAndTar = [&](const wchar_t* sFile, const std::vector<uint8_t>& sBytes, const std::wstring& sInner, const std::string& wantS,
                                      const wchar_t* tFile, const std::vector<uint8_t>& tBytes) -> bool
        {
            std::wstring sp = baseDir + L"\\" + sFile;
            std::wstring tp = baseDir + L"\\" + tFile;
            PathUtils::WriteFileBytes(sp, sBytes.data(), sBytes.size());
            PathUtils::WriteFileBytes(tp, tBytes.data(), tBytes.size());

            ArchiveSummaryInfo ss, ts;
            std::vector<uint8_t> so, to;
            bool okS = ArchiveExplorer::InspectArchiveFile(sp, ss) && ss.valid && ss.entries.size() == 1 &&
                       ss.entries[0].uncompressedSize == wantS.size() &&
                       ArchiveExplorer::ExtractSingleEntryToMemory(sp, sInner, so) && BytesMatchStr(so, wantS);
            bool okT = ArchiveExplorer::InspectArchiveFile(tp, ts) && ts.valid && ts.entries.size() == 2 &&
                       FindEntryCI(ts, L"pkg/src/main.cpp") != nullptr &&
                       ArchiveExplorer::ExtractSingleEntryToMemory(tp, L"pkg/src/main.cpp", to) &&
                       BytesMatchStr(to, "int main() { return 0; }\n");
            return okS && okT;
        };

        bool okXz  = verifySingleAndTar(L"note.txt.xz",  xzSingle,  L"note.txt",  rawXz,  L"bundle.txz",     txzBytes);
        bool okBz2 = verifySingleAndTar(L"note.txt.bz2", bz2Single, L"note.txt",  rawBz2, L"bundle.tbz2",    tbz2Bytes);
        bool okLz4 = verifySingleAndTar(L"note.txt.lz4", lz4Single, L"note.txt",  rawLz4, L"bundle.tar.lz4", tlz4Bytes);
        bool okZst = verifySingleAndTar(L"note.txt.zst", zstSingle, L"note.txt",  rawZst, L"bundle.tzst",    tzstBytes);

        tc.Record(okXz && okBz2 && okLz4 && okZst,
            L"Stage15.Archives.StreamCodecs_XZ_BZ2_LZ4_ZSTD_And_InnerTar",
            L"xz=" + std::to_wstring(okXz) + L", bz2=" + std::to_wstring(okBz2) +
            L", lz4=" + std::to_wstring(okLz4) + L", zst=" + std::to_wstring(okZst));
    }

    // ------------------------------------------------------------------------
    // Test 15.8: Virtual Disk Images (.vhd Fixed & Dynamic cxsparse + .qcow2) with
    //            MBR Partition + FAT16 (8.3 + VFAT LFN + Subfolder + Multi-Cluster Extract)
    //            + Windows Imaging (.wim XML + LookupTable + DIRENTRY Extract)
    // ------------------------------------------------------------------------
    {
        std::string f1Text = "Boot configuration inside Fixed VHD MBR FAT16 partition!\r\n";
        std::string f2Long(1350, 'X'); // Spans 3 FAT16 clusters!
        f2Long.replace(0, 28, "MULTI_CLUSTER_VFAT_LFN_DATA:");

        auto rawDisk1 = BuildMbrFat16Disk("BOOTVOL1   ", "DOCS", "DOCS       ", {
            { "", L"ReadMe_First.txt", "README~1TXT", f1Text },
            { "DOCS       ", L"Architecture_Guide_2026.md", "ARCHIT~1MD ", f2Long }
        });
        std::string disk2Payload = "Distinct second Dynamic VHD disk image with different files & BAT translation!";
        auto rawDisk2 = BuildMbrFat16Disk("DATAVOL2   ", "BIN", "BIN        ", {
            { "BIN        ", L"firmware_v2.bin", "FIRMWA~1BIN", disk2Payload }
        });

        auto vhdFixed   = WrapAsFixedVhd(rawDisk1);
        auto vhdDynamic = WrapAsDynamicVhd(rawDisk2);
        auto qcow2Img   = WrapAsQcow2(rawDisk1);

        std::wstring vp1 = baseDir + L"\\disk_fixed.vhd";
        std::wstring vp2 = baseDir + L"\\disk_dynamic.vhd";
        std::wstring qp1 = baseDir + L"\\disk_cluster.qcow2";
        PathUtils::WriteFileBytes(vp1, vhdFixed.data(), vhdFixed.size());
        PathUtils::WriteFileBytes(vp2, vhdDynamic.data(), vhdDynamic.size());
        PathUtils::WriteFileBytes(qp1, qcow2Img.data(), qcow2Img.size());

        ArchiveSummaryInfo vs1, vs2, qs1;
        std::vector<uint8_t> vo1a, vo1b, vo2, qo1;
        bool okVhd1 = ArchiveExplorer::InspectArchiveFile(vp1, vs1) && vs1.valid &&
                      FindEntryCI(vs1, L"ReadMe_First.txt") != nullptr &&
                      FindEntryCI(vs1, L"DOCS/Architecture_Guide_2026.md") != nullptr &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(vp1, L"ReadMe_First.txt", vo1a) && BytesMatchStr(vo1a, f1Text) &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(vp1, L"DOCS/Architecture_Guide_2026.md", vo1b) && BytesMatchStr(vo1b, f2Long);

        bool okVhd2 = ArchiveExplorer::InspectArchiveFile(vp2, vs2) && vs2.valid &&
                      FindEntryCI(vs2, L"BIN/firmware_v2.bin") != nullptr &&
                      FindEntryCI(vs2, L"ReadMe_First.txt") == nullptr &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(vp2, L"BIN/firmware_v2.bin", vo2) && BytesMatchStr(vo2, disk2Payload);

        bool okQcow2 = ArchiveExplorer::InspectArchiveFile(qp1, qs1) && qs1.valid &&
                       ArchiveExplorer::ExtractSingleEntryToMemory(qp1, L"DOCS/Architecture_Guide_2026.md", qo1) &&
                       BytesMatchStr(qo1, f2Long);

        std::string wimPayload1 = "Windows Imaging MSWIM #1 DIRENTRY + SHA-1 LookupTable Extracted File!";
        std::string wimPayload2 = "<unattend xmlns=\"urn:schemas-microsoft-com:unattend\"><settings pass=\"windowsPE\"/></unattend>";
        auto wimBytes1 = BuildWindowsWim(L"WinPE_Boot", L"winpeshl.ini", wimPayload1);
        auto wimBytes2 = BuildWindowsWim(L"ServerCore_Setup", L"unattend.xml", wimPayload2);
        std::wstring wp1 = baseDir + L"\\boot1.wim";
        std::wstring wp2 = baseDir + L"\\setup2.wim";
        PathUtils::WriteFileBytes(wp1, wimBytes1.data(), wimBytes1.size());
        PathUtils::WriteFileBytes(wp2, wimBytes2.data(), wimBytes2.size());

        ArchiveSummaryInfo ws1, ws2;
        std::vector<uint8_t> wo1, wo2;
        bool okWim = ArchiveExplorer::InspectArchiveFile(wp1, ws1) && ws1.valid &&
                     FindEntryCI(ws1, L"WinPE_Boot/winpeshl.ini") != nullptr &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(wp1, L"WinPE_Boot/winpeshl.ini", wo1) &&
                     BytesMatchStr(wo1, wimPayload1) &&
                     ArchiveExplorer::InspectArchiveFile(wp2, ws2) && ws2.valid &&
                     FindEntryCI(ws2, L"ServerCore_Setup/unattend.xml") != nullptr &&
                     FindEntryCI(ws2, L"WinPE_Boot/winpeshl.ini") == nullptr &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(wp2, L"ServerCore_Setup/unattend.xml", wo2) &&
                     BytesMatchStr(wo2, wimPayload2);

        tc.Record(okVhd1 && okVhd2 && okQcow2 && okWim,
            L"Stage15.Archives.VirtualDisks_VHD_DynamicBAT_QCOW2_MBR_FAT16_And_WIM",
            L"vhdFixed=" + std::to_wstring(okVhd1) + L", vhdDyn=" + std::to_wstring(okVhd2) +
            L", qcow2=" + std::to_wstring(okQcow2) + L", wim=" + std::to_wstring(okWim));
    }

    // ------------------------------------------------------------------------
    // Test 15.9: UnityFS v6 & v7 AssetBundles (.bundle) with LZ4 blocksInfo,
    //            Multi-StorageBlock Spanning Files, and Corrupt/Empty Rejection
    // ------------------------------------------------------------------------
    {
        std::string u1Hdr = "UnityFS v6 Header Asset — small metadata node!";
        std::string u1Big =
            "UnityFS v6 Multi-Block Asset Payload! Repeating shader bytecode block AAAA_BBBB_CCCC_DDDD "
            "spanning across three distinct LZ4-compressed StorageBlocks (chunk=64B) for slice verification!";
        std::string u2Mesh =
            "UnityFS v7 16-Byte Aligned Bundle Mesh Stream — vertex/index buffer payload 0123456789ABCDEF "
            "0123456789ABCDEF spanning multiple LZ4 storage blocks cleanly!";

        auto bun1 = BuildUnityFsBundle(6u, "2020.3.48f1", 64u, {
            { "CAB-1111/manifest.txt", u1Hdr },
            { "CAB-1111/CAB-1111.resS", u1Big }
        });
        auto bun2 = BuildUnityFsBundle(7u, "2022.3.20f1", 64u, {
            { "CAB-9999/mesh_stream.bin", u2Mesh }
        });

        std::wstring bp1 = baseDir + L"\\assets_v6.bundle";
        std::wstring bp2 = baseDir + L"\\assets_v7.bundle";
        std::wstring bpBad = baseDir + L"\\corrupt_empty.bundle";
        PathUtils::WriteFileBytes(bp1, bun1.data(), bun1.size());
        PathUtils::WriteFileBytes(bp2, bun2.data(), bun2.size());

        // Corrupt UnityFS header with invalid blocksInfo -> MUST return false (no fake CAB-SerializedAssets.assets!)
        std::vector<uint8_t> badBun = { 'U','n','i','t','y','F','S',0, 0,0,0,7, '5','.','x',0, '2','0','2','2',0 };
        badBun.resize(64, 0xFFu);
        PathUtils::WriteFileBytes(bpBad, badBun.data(), badBun.size());

        ArchiveSummaryInfo us1, us2, usBad;
        std::vector<uint8_t> uo1a, uo1b, uo2;
        bool okU1 = ArchiveExplorer::InspectArchiveFile(bp1, us1) && us1.valid && us1.entries.size() == 2 &&
                    FindEntryCI(us1, L"CAB-SerializedAssets.assets") == nullptr &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(bp1, L"CAB-1111/manifest.txt", uo1a) && BytesMatchStr(uo1a, u1Hdr) &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(bp1, L"CAB-1111/CAB-1111.resS", uo1b) && BytesMatchStr(uo1b, u1Big);
        bool okU2 = ArchiveExplorer::InspectArchiveFile(bp2, us2) && us2.valid && us2.entries.size() == 1 &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(bp2, L"CAB-9999/mesh_stream.bin", uo2) && BytesMatchStr(uo2, u2Mesh);
        bool rejBadU = !ArchiveExplorer::InspectArchiveFile(bpBad, usBad) &&
                       FindEntryCI(usBad, L"CAB-SerializedAssets.assets") == nullptr;

        tc.Record(okU1 && okU2 && rejBadU,
            L"Stage15.Archives.UnityFS_LZ4BlocksInfo_MultiStorageBlock_And_NoFakeFallback",
            L"v6=" + std::to_wstring(okU1) + L", v7=" + std::to_wstring(okU2) + L", rejBad=" + std::to_wstring(rejBadU));
    }

    // ------------------------------------------------------------------------
    // Test 15.10: Blizzard MoPaQ (.mpq) with Storm-Encrypted Hash & Block Tables,
    //             Non-Sequential (listfile) Mapping, and Zlib+Encrypted Multi-Sector Files
    // ------------------------------------------------------------------------
    {
        std::string knightMdx;
        while (knightMdx.size() < 740) knightMdx += "MDLX_VERS_800_KNIGHT_GEOMETRY_CHUNK_";
        std::string war3MapJ;
        while (war3MapJ.size() < 860) war3MapJ += "function main takes nothing returns nothing // JASS script line\r\n";

        auto mpqBytes = BuildBlizzardMpqTestArchive(knightMdx, war3MapJ);
        std::wstring mpqPath = baseDir + L"\\war3_patch.mpq";
        PathUtils::WriteFileBytes(mpqPath, mpqBytes.data(), mpqBytes.size());

        ArchiveSummaryInfo ms;
        std::vector<uint8_t> outKnight, outJass;
        bool okInspect = ArchiveExplorer::InspectArchiveFile(mpqPath, ms) && ms.valid && ms.entries.size() == 2 &&
                         FindEntryCI(ms, L"(listfile)") == nullptr &&
                         FindEntryCI(ms, L"Units/Knight.mdx") != nullptr &&
                         FindEntryCI(ms, L"Units/Knight.mdx")->uncompressedSize == knightMdx.size() &&
                         FindEntryCI(ms, L"Scripts/war3map.j") != nullptr &&
                         FindEntryCI(ms, L"Scripts/war3map.j")->uncompressedSize == war3MapJ.size();
        bool okExtract = ArchiveExplorer::ExtractSingleEntryToMemory(mpqPath, L"Units/Knight.mdx", outKnight) &&
                         BytesMatchStr(outKnight, knightMdx) &&
                         ArchiveExplorer::ExtractSingleEntryToMemory(mpqPath, L"Scripts/war3map.j", outJass) &&
                         BytesMatchStr(outJass, war3MapJ);

        tc.Record(okInspect && okExtract,
            L"Stage15.Archives.BlizzardMPQ_StormHashBlockTables_ZlibEncryptedMultiSector",
            L"inspect=" + std::to_wstring(okInspect) + L", extract=" + std::to_wstring(okExtract));
    }

    // ------------------------------------------------------------------------
    // Test 15.11: RPG Maker Encrypted Archives (.rgss3a v3 & .rgssad v1)
    //             Multi-Dword Rolling XOR Stream Decryption
    // ------------------------------------------------------------------------
    {
        std::string rvActors = "Marshal.4.8 RPG::Actor table payload spanning > 16 dwords for key*7+3 stream advance check!";
        std::string rvBgm    = "OggS_VX_Ace_Encrypted_Audio_Stream_0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ!";
        std::string rxSys    = "RPG Maker XP v1 RGSSAD System.rxdata rolling 0xDEADCAFE stream decryption verification!";
        std::string rxLogo   = "PNG_MOCK_HEADER_89504E47_MULTI_DWORD_RGSS_V1_DECRYPTED_EXACTLY!";

        auto rgss3 = BuildRpgMakerRgss3a({ { "Data\\Actors.rvdata2", rvActors }, { "Audio\\BGM\\Field.ogg", rvBgm } });
        auto rgss1 = BuildRpgMakerRgssV1({ { "Data\\System.rxdata", rxSys }, { "Graphics\\Titles\\Logo.png", rxLogo } });

        std::wstring p3 = baseDir + L"\\Game.rgss3a";
        std::wstring p1 = baseDir + L"\\Game.rgssad";
        PathUtils::WriteFileBytes(p3, rgss3.data(), rgss3.size());
        PathUtils::WriteFileBytes(p1, rgss1.data(), rgss1.size());

        ArchiveSummaryInfo s3, s1;
        std::vector<uint8_t> o3a, o3b, o1a, o1b;
        bool ok3 = ArchiveExplorer::InspectArchiveFile(p3, s3) && s3.valid && s3.entries.size() == 2 &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p3, L"Data/Actors.rvdata2", o3a) && BytesMatchStr(o3a, rvActors) &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p3, L"Audio/BGM/Field.ogg", o3b) && BytesMatchStr(o3b, rvBgm);
        bool ok1 = ArchiveExplorer::InspectArchiveFile(p1, s1) && s1.valid && s1.entries.size() == 2 &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p1, L"Data/System.rxdata", o1a) && BytesMatchStr(o1a, rxSys) &&
                   ArchiveExplorer::ExtractSingleEntryToMemory(p1, L"Graphics/Titles/Logo.png", o1b) && BytesMatchStr(o1b, rxLogo);

        tc.Record(ok3 && ok1,
            L"Stage15.Archives.RpgMaker_RGSS3A_And_RGSSAD_StreamDecryption",
            L"rgss3a=" + std::to_wstring(ok3) + L", rgssad=" + std::to_wstring(ok1));
    }

    // ------------------------------------------------------------------------
    // Test 15.12: Linux SquashFS 4.0 (Zlib Metadata + Nested Dir + Zlib Data) &
    //             Linux EROFS (Root + Subdir + FLAT_PLAIN & Extended FLAT_INLINE)
    // ------------------------------------------------------------------------
    {
        std::string sqReadme = "SquashFS 4.0 Root File — Zlib compressed data & Zlib compressed metadata blocks!\n";
        std::string sqConf   = "[service]\nmode = production\nthreads = 16\ndescription = Nested SquashFS file\n";
        auto sqImg = BuildSquashFs4ZlibImage(sqReadme, sqConf);

        std::wstring sqPath = baseDir + L"\\rootfs.squashfs";
        std::wstring sqStubPath = baseDir + L"\\stub_only.squashfs";
        PathUtils::WriteFileBytes(sqPath, sqImg.data(), sqImg.size());

        // Header-only "hsqs" stub (96B) -> MUST be rejected without fake "squashfs-root (...)"!
        std::vector<uint8_t> sqStub(96, 0);
        std::memcpy(sqStub.data(), "hsqs", 4);
        PutU32LE(sqStub.data() + 4, 5u);
        PutU32LE(sqStub.data() + 12, 4096u);
        PutU16LE(sqStub.data() + 20, 1u);
        PutU16LE(sqStub.data() + 28, 4u);
        PathUtils::WriteFileBytes(sqStubPath, sqStub.data(), sqStub.size());

        ArchiveSummaryInfo sqSum, sqBadSum;
        std::vector<uint8_t> sqOut1, sqOut2;
        bool okSq = ArchiveExplorer::InspectArchiveFile(sqPath, sqSum) && sqSum.valid &&
                    FindEntryCI(sqSum, L"readme.txt") != nullptr &&
                    FindEntryCI(sqSum, L"readme.txt")->uncompressedSize == sqReadme.size() &&
                    FindEntryCI(sqSum, L"etc/config.ini") != nullptr &&
                    FindEntryCI(sqSum, L"etc/config.ini")->uncompressedSize == sqConf.size() &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(sqPath, L"readme.txt", sqOut1) && BytesMatchStr(sqOut1, sqReadme) &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(sqPath, L"etc/config.ini", sqOut2) && BytesMatchStr(sqOut2, sqConf);
        bool rejSqStub = !ArchiveExplorer::InspectArchiveFile(sqStubPath, sqBadSum) && sqBadSum.entries.empty();

        std::string erBoot = "console=ttyS0,115200 root=/dev/vda ro quiet\n";
        std::string erInline(4096, 'E');
        erInline.replace(0, 24, "EROFS_FULL_BLOCK_HEADER:");
        erInline += "::INLINE_TAIL_PAYLOAD_AFTER_EXTENDED_64B_INODE!";
        auto erImg = BuildErofsTestImage(erBoot, erInline);

        std::wstring erPath = baseDir + L"\\system.erofs";
        std::wstring erStubPath = baseDir + L"\\empty_sb.erofs";
        PathUtils::WriteFileBytes(erPath, erImg.data(), erImg.size());

        // Empty EROFS superblock -> MUST be rejected without fake "erofs_root (...)"!
        std::vector<uint8_t> erStub(2048, 0);
        PutU32LE(erStub.data() + 1024, 0xE0F5E1E2u);
        erStub[1024 + 12] = 12u;
        PutU32LE(erStub.data() + 1024 + 36, 1u);
        PathUtils::WriteFileBytes(erStubPath, erStub.data(), erStub.size());

        ArchiveSummaryInfo erSum, erBadSum;
        std::vector<uint8_t> erOut1, erOut2;
        bool okEr = ArchiveExplorer::InspectArchiveFile(erPath, erSum) && erSum.valid &&
                    FindEntryCI(erSum, L"boot.cfg") != nullptr &&
                    FindEntryCI(erSum, L"boot.cfg")->uncompressedSize == erBoot.size() &&
                    FindEntryCI(erSum, L"etc/inline.conf") != nullptr &&
                    FindEntryCI(erSum, L"etc/inline.conf")->uncompressedSize == erInline.size() &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(erPath, L"boot.cfg", erOut1) && BytesMatchStr(erOut1, erBoot) &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(erPath, L"etc/inline.conf", erOut2) && BytesMatchStr(erOut2, erInline);
        bool rejErStub = !ArchiveExplorer::InspectArchiveFile(erStubPath, erBadSum) && erBadSum.entries.empty();

        tc.Record(okSq && rejSqStub && okEr && rejErStub,
            L"Stage15.Archives.LinuxFS_SquashFS4Zlib_And_EROFS_PlainAndInline",
            L"sqfs=" + std::to_wstring(okSq) + L", rejSq=" + std::to_wstring(rejSqStub) +
            L", erofs=" + std::to_wstring(okEr) + L", rejEr=" + std::to_wstring(rejErStub));
    }

    // ------------------------------------------------------------------------
    // Test 15.13: Apple Disk Image (.dmg UDIF Zlib + Apple ADC + Zero Fill) +
    //             APM ("PM") + HFS+ ("H+") Catalog B-Tree & Multi-Extent Extraction
    // ------------------------------------------------------------------------
    {
        std::string plistXml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<plist version=\"1.0\"><dict><key>CFBundleExecutable</key><string>Explorer2Mac</string></dict></plist>\n";
        std::string readmeMulti;
        while (readmeMulti.size() < 920) {
            readmeMulti += "Apple_ADC_And_MultiExtent_HFSPlus_Catalog_BTree_Payload_Block_1234567890!\n";
        }

        auto dmgBytes = BuildDmgApmHfsPlusImage(plistXml, readmeMulti);
        std::wstring dmgPath = baseDir + L"\\Installer.dmg";
        std::wstring badDmgPath = baseDir + L"\\CorruptCatalog.dmg";
        PathUtils::WriteFileBytes(dmgPath, dmgBytes.data(), dmgBytes.size());

        // Corrupt raw HFS+ .dmg (valid "H+" magic at +1024, zeroed Catalog B-Tree) -> MUST reject without fake HFSPlus_Volume_!
        std::vector<uint8_t> badDmg(4096, 0);
        badDmg[1024] = 'H'; badDmg[1025] = '+';
        PutU16BE(badDmg.data() + 1026, 4u);
        PutU32BE(badDmg.data() + 1024 + 32, 7u);
        PutU32BE(badDmg.data() + 1024 + 40, 512u);
        PutU32BE(badDmg.data() + 1024 + 44, 8u);
        PathUtils::WriteFileBytes(badDmgPath, badDmg.data(), badDmg.size());

        ArchiveSummaryInfo ds, badDs;
        std::vector<uint8_t> outPlist, outReadme;
        bool okInspect = ArchiveExplorer::InspectArchiveFile(dmgPath, ds) && ds.valid &&
                         FindEntryCI(ds, L"App.app") != nullptr && FindEntryCI(ds, L"App.app")->isDirectory &&
                         FindEntryCI(ds, L"App.app/Contents") != nullptr && FindEntryCI(ds, L"App.app/Contents")->isDirectory &&
                         FindEntryCI(ds, L"App.app/Contents/Info.plist") != nullptr &&
                         FindEntryCI(ds, L"App.app/Contents/Info.plist")->uncompressedSize == plistXml.size() &&
                         FindEntryCI(ds, L"ReadMe.txt") != nullptr &&
                         FindEntryCI(ds, L"ReadMe.txt")->uncompressedSize == readmeMulti.size();
        bool noFakeHfs = true;
        for (const auto& e : ds.entries) {
            if (e.pathInArchive.find(L"HFSPlus_Volume_") != std::wstring::npos) noFakeHfs = false;
        }
        bool okExtract = ArchiveExplorer::ExtractSingleEntryToMemory(dmgPath, L"App.app/Contents/Info.plist", outPlist) &&
                         BytesMatchStr(outPlist, plistXml) &&
                         ArchiveExplorer::ExtractSingleEntryToMemory(dmgPath, L"ReadMe.txt", outReadme) &&
                         BytesMatchStr(outReadme, readmeMulti);
        bool rejBadDmg = !ArchiveExplorer::InspectArchiveFile(badDmgPath, badDs) && badDs.entries.empty();

        tc.Record(okInspect && noFakeHfs && okExtract && rejBadDmg,
            L"Stage15.Archives.AppleDMG_UDIF_ZlibADC_APM_And_HFSPlus_CatalogBTree",
            L"inspect=" + std::to_wstring(okInspect) + L", extract=" + std::to_wstring(okExtract) +
            L", rejCorrupt=" + std::to_wstring(rejBadDmg));
    }

    // ------------------------------------------------------------------------
    // Test 15.14: Linux ext2/ext3/ext4 Recursive Inodes, Depth-1 Extent Tree (0xF30A),
    //             Legacy Indirect Block Pointers, and Exact Non-Block-Multiple Sizes
    // ------------------------------------------------------------------------
    {
        std::string appConf(2500, 'C');
        appConf.replace(0, 32, "[ext4-extent-index-tree]\nheader=");
        appConf.replace(2468, 32, "\nfooter=EXACT_2500_BYTES_END!!\n");

        std::string indirectPayload(13500, 'I');
        indirectPayload.replace(0, 28, "EXT2_DIRECT_BLOCKS_START>>>");
        indirectPayload.replace(12288, 30, "<<<SINGLE_INDIRECT_BLOCK_13>>>");
        indirectPayload.replace(13470, 30, "<<<EXACT_13500_BYTES_FINISH!!!");

        auto extImg = BuildExt4ExtentAndIndirectImage(appConf, indirectPayload);
        std::wstring extPath = baseDir + L"\\ubuntu_root.ext4";
        std::wstring badExtPath = baseDir + L"\\broken_sb.ext4";
        PathUtils::WriteFileBytes(extPath, extImg.data(), extImg.size());

        // Superblock-only stub (0xEF53 magic, zeroed inode table) -> MUST return false (no fake "...inodes.ext4"!)
        std::vector<uint8_t> badExt(4096, 0);
        PutU32LE(badExt.data() + 1024 + 0, 16u);
        PutU32LE(badExt.data() + 1024 + 4, 4u);
        PutU32LE(badExt.data() + 1024 + 32, 8192u);
        PutU32LE(badExt.data() + 1024 + 40, 16u);
        PutU16LE(badExt.data() + 1024 + 56, 0xEF53u);
        PathUtils::WriteFileBytes(badExtPath, badExt.data(), badExt.size());

        ArchiveSummaryInfo es, badEs;
        std::vector<uint8_t> eoConf, eoInd;
        bool okExt = ArchiveExplorer::InspectArchiveFile(extPath, es) && es.valid &&
                     FindEntryCI(es, L"lost+found") == nullptr &&
                     FindEntryCI(es, L"etc") != nullptr && FindEntryCI(es, L"etc")->isDirectory &&
                     FindEntryCI(es, L"etc/app.conf") != nullptr &&
                     FindEntryCI(es, L"etc/app.conf")->uncompressedSize == appConf.size() &&
                     FindEntryCI(es, L"legacy_indirect.bin") != nullptr &&
                     FindEntryCI(es, L"legacy_indirect.bin")->uncompressedSize == indirectPayload.size() &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(extPath, L"etc/app.conf", eoConf) &&
                     BytesMatchStr(eoConf, appConf) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(extPath, L"legacy_indirect.bin", eoInd) &&
                     BytesMatchStr(eoInd, indirectPayload);
        bool rejBadExt = !ArchiveExplorer::InspectArchiveFile(badExtPath, badEs) && badEs.entries.empty();

        tc.Record(okExt && rejBadExt,
            L"Stage15.Archives.LinuxExt4_ExtentTreeIndex_And_Ext2IndirectBlocks",
            L"ext4=" + std::to_wstring(okExt) + L", rejBadExt=" + std::to_wstring(rejBadExt));
    }

    // ------------------------------------------------------------------------
    // Test 15.15: Windows Imaging (.wim) XPRESS Huffman Compressed Metadata &
    //             Multi-Chunk XPRESS File Stream + Corrupt WIM Rejection (No Fake Image_1)
    // ------------------------------------------------------------------------
    {
        std::string wimXpressFile;
        while (wimXpressFile.size() < 1680) { // Spans 2 XPRESS Huffman chunks (chunkSize = 1024B) + long backrefs!
            wimXpressFile += "MS_WIM_XPRESS_HUFFMAN_COMPRESSED_STREAM_LINE_WITH_LONG_MATCHES_0123456789ABCDEF\r\n";
        }
        auto wimXpress = BuildXpressWindowsWim(L"Windows11_Setup", L"Windows", L"system.ini", wimXpressFile);
        std::wstring wxPath = baseDir + L"\\install_xpress.wim";
        std::wstring wxBadPath = baseDir + L"\\corrupt_meta.wim";
        PathUtils::WriteFileBytes(wxPath, wimXpress.data(), wimXpress.size());

        // Corrupt WIM header with valid MSWIM magic + imgCount=2 but broken metadata -> MUST reject without fake Image_1!
        std::vector<uint8_t> badWim(256, 0);
        std::memcpy(badWim.data(), "MSWIM\0\0\0", 8);
        PutU32LE(badWim.data() + 8, 208u);
        PutU32LE(badWim.data() + 12, 0x00010D00u);
        PutU32LE(badWim.data() + 44, 2u);
        PathUtils::WriteFileBytes(wxBadPath, badWim.data(), badWim.size());

        ArchiveSummaryInfo wxs, wxBadSum;
        std::vector<uint8_t> wxOut;
        bool okWx = ArchiveExplorer::InspectArchiveFile(wxPath, wxs) && wxs.valid &&
                    FindEntryCI(wxs, L"Windows11_Setup/Windows") != nullptr &&
                    FindEntryCI(wxs, L"Windows11_Setup/Windows/system.ini") != nullptr &&
                    FindEntryCI(wxs, L"Windows11_Setup/Windows/system.ini")->uncompressedSize == wimXpressFile.size() &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(wxPath, L"Windows11_Setup/Windows/system.ini", wxOut) &&
                    BytesMatchStr(wxOut, wimXpressFile);
        bool rejBadWim = !ArchiveExplorer::InspectArchiveFile(wxBadPath, wxBadSum) && wxBadSum.entries.empty();

        tc.Record(okWx && rejBadWim,
            L"Stage15.Archives.WindowsWIM_XpressHuffmanMetadata_MultiChunkStream_NoFakeImage",
            L"xpressWim=" + std::to_wstring(okWx) + L", rejBadWim=" + std::to_wstring(rejBadWim));
    }

    // ------------------------------------------------------------------------
    // Test 15.16: Valve Pack (.vpk v2) Multi-Chunk pak01_dir.vpk -> pak01_000.vpk
    //             Preload Stitching + IEEE CRC32 Verification & Corrupt Rejection
    // ------------------------------------------------------------------------
    {
        std::string vmtInline = "\"LightmappedGeneric\" { \"$basetexture\" \"brick/wall01\" \"$surfaceprop\" \"brick\" }\n";
        std::string mdlChunk  = "IDST0_VALVE_SOURCE_MDL_HEADER_24B_PRELOAD::EXTERNAL_CHUNK_000_GEOMETRY_PAYLOAD_VERIFIED_BY_CRC32!";

        std::vector<uint8_t> dirVpk, chunk000Vpk;
        BuildMultiChunkValveVpk(vmtInline, mdlChunk, dirVpk, chunk000Vpk);

        std::wstring dirVpkPath   = baseDir + L"\\pak01_dir.vpk";
        std::wstring chunk000Path = baseDir + L"\\pak01_000.vpk";
        PathUtils::WriteFileBytes(dirVpkPath, dirVpk.data(), dirVpk.size());
        PathUtils::WriteFileBytes(chunk000Path, chunk000Vpk.data(), chunk000Vpk.size());

        ArchiveSummaryInfo vpkSum;
        std::vector<uint8_t> outVmt, outMdl, outBadCrc;
        bool okVpk = ArchiveExplorer::InspectArchiveFile(dirVpkPath, vpkSum) && vpkSum.valid && vpkSum.entries.size() == 2 &&
                     FindEntryCI(vpkSum, L"materials/brick/wall01.vmt") != nullptr &&
                     FindEntryCI(vpkSum, L"materials/brick/wall01.vmt")->uncompressedSize == vmtInline.size() &&
                     FindEntryCI(vpkSum, L"models/props/crate01.mdl") != nullptr &&
                     FindEntryCI(vpkSum, L"models/props/crate01.mdl")->uncompressedSize == mdlChunk.size() &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(dirVpkPath, L"materials/brick/wall01.vmt", outVmt) &&
                     BytesMatchStr(outVmt, vmtInline) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(dirVpkPath, L"models/props/crate01.mdl", outMdl) &&
                     BytesMatchStr(outMdl, mdlChunk);

        // Flip 1 bit in pak01_000.vpk -> CRC32 check MUST fail extraction!
        chunk000Vpk.back() ^= 0x55u;
        PathUtils::WriteFileBytes(chunk000Path, chunk000Vpk.data(), chunk000Vpk.size());
        bool rejCorruptCrc = !ArchiveExplorer::ExtractSingleEntryToMemory(dirVpkPath, L"models/props/crate01.mdl", outBadCrc);

        tc.Record(okVpk && rejCorruptCrc,
            L"Stage15.Archives.ValveVPK_MultiChunk_PreloadStitching_And_CRC32Check",
            L"multiChunk=" + std::to_wstring(okVpk) + L", rejBadCrc=" + std::to_wstring(rejCorruptCrc));
    }

    // ------------------------------------------------------------------------
    // Test 15.17: Electron ASAR (.asar) Recursive Nested JSON Hierarchy +
    //             .asar.unpacked Sidecar Extraction + Out-of-Bounds Offset Rejection
    // ------------------------------------------------------------------------
    {
        std::string pkgJson = "{\"name\":\"explorer2-app\",\"main\":\"src/core/engine.js\"}\n";
        std::string engJs   = "export function bootEngine() { return 'ASAR_NESTED_OK'; }\n";
        std::string natBin  = "MZ_UNPACKED_NATIVE_ADDON_PAYLOAD_FROM_SIDECAR_FOLDER!";

        std::vector<uint8_t> payload;
        payload.insert(payload.end(), pkgJson.begin(), pkgJson.end());
        size_t engOff = payload.size();
        payload.insert(payload.end(), engJs.begin(), engJs.end());

        std::string asarJson =
            "{\"files\":{"
            "\"package.json\":{\"size\":" + std::to_string(pkgJson.size()) + ",\"offset\":\"0\"},"
            "\"src\":{\"files\":{"
                "\"core\":{\"files\":{"
                    "\"engine.js\":{\"size\":" + std::to_string(engJs.size()) + ",\"offset\":\"" + std::to_string(engOff) + "\"}"
                "}},"
                "\"native.node\":{\"size\":" + std::to_string(natBin.size()) + ",\"unpacked\":true}"
            "}}}}";

        auto asarBytes = BuildElectronAsarArchive(asarJson, payload);
        std::wstring asarPath = baseDir + L"\\app.asar";
        std::wstring unpDir   = baseDir + L"\\app.asar.unpacked";
        std::wstring unpSrc   = unpDir + L"\\src";
        CreateDirectoryW(unpDir.c_str(), nullptr);
        CreateDirectoryW(unpSrc.c_str(), nullptr);
        PathUtils::WriteFileBytes(asarPath, asarBytes.data(), asarBytes.size());
        PathUtils::WriteFileBytes(unpSrc + L"\\native.node",
                                  reinterpret_cast<const uint8_t*>(natBin.data()), natBin.size());

        // Corrupt ASAR with out-of-bounds offset -> MUST reject!
        std::string oobJson = "{\"files\":{\"bad.js\":{\"size\":64,\"offset\":\"999999\"}}}";
        auto oobAsar = BuildElectronAsarArchive(oobJson, { 'x', 'y' });
        std::wstring oobAsarPath = baseDir + L"\\corrupt_oob.asar";
        PathUtils::WriteFileBytes(oobAsarPath, oobAsar.data(), oobAsar.size());

        ArchiveSummaryInfo as, oobSum;
        std::vector<uint8_t> aoPkg, aoEng, aoNat;
        bool okAsar = ArchiveExplorer::InspectArchiveFile(asarPath, as) && as.valid &&
                      FindEntryCI(as, L"engine.js") == nullptr && // Must NOT flatten nested path!
                      FindEntryCI(as, L"package.json") != nullptr &&
                      FindEntryCI(as, L"src") != nullptr && FindEntryCI(as, L"src")->isDirectory &&
                      FindEntryCI(as, L"src/core") != nullptr && FindEntryCI(as, L"src/core")->isDirectory &&
                      FindEntryCI(as, L"src/core/engine.js") != nullptr &&
                      FindEntryCI(as, L"src/core/engine.js")->uncompressedSize == engJs.size() &&
                      FindEntryCI(as, L"src/native.node") != nullptr &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(asarPath, L"package.json", aoPkg) && BytesMatchStr(aoPkg, pkgJson) &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(asarPath, L"src/core/engine.js", aoEng) && BytesMatchStr(aoEng, engJs) &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(asarPath, L"src/native.node", aoNat) && BytesMatchStr(aoNat, natBin);
        bool rejOob = !ArchiveExplorer::InspectArchiveFile(oobAsarPath, oobSum) && oobSum.entries.empty();

        tc.Record(okAsar && rejOob,
            L"Stage15.Archives.ElectronASAR_RecursiveNestedJSON_UnpackedSidecar_And_BoundsCheck",
            L"nestedAsar=" + std::to_wstring(okAsar) + L", rejOob=" + std::to_wstring(rejOob));
    }

    // ------------------------------------------------------------------------
    // Test 15.18: Electronic Arts .big (BIGF / BIG4) RefPack (0x10FB) Decompression
    //             True Uncompressed Size Reporting + Byte-Exact Extraction + Corrupt Check
    // ------------------------------------------------------------------------
    {
        std::string iniPlain;
        while (iniPlain.size() < 460) {
            iniPlain += "Object AmericaTankCrusader\r\n  Armor = TankArmor 250\r\n  Weapon = CrusaderGun\r\nEnd\r\n";
        }
        std::string rawTxt = "Uncompressed companion file stored alongside RefPack INI inside BIGF archive.\r\n";

        auto refPackedIni = EncodeEaRefPackForTest(reinterpret_cast<const uint8_t*>(iniPlain.data()), iniPlain.size());
        std::vector<uint8_t> rawTxtBytes(rawTxt.begin(), rawTxt.end());
        std::vector<uint8_t> badRefPack = { 0x10u, 0xFBu, 0x00u, 0x01u, 0x00u, 0x04u, 0xFFu, 0xFCu }; // Invalid back-ref before any literals!

        auto bigBytes = BuildEaBigTestArchive({
            { "Data\\INI\\GameData.ini", refPackedIni },
            { "Data\\Scripts\\readme.txt", rawTxtBytes },
            { "Data\\Corrupt\\broken.ini", badRefPack }
        });
        std::wstring bigPath = baseDir + L"\\generals_patch.big";
        PathUtils::WriteFileBytes(bigPath, bigBytes.data(), bigBytes.size());

        ArchiveSummaryInfo bigSum;
        std::vector<uint8_t> outIni, outRaw, outBroken;
        bool okBig = refPackedIni.size() < iniPlain.size() &&
                     ArchiveExplorer::InspectArchiveFile(bigPath, bigSum) && bigSum.valid && bigSum.entries.size() == 3 &&
                     FindEntryCI(bigSum, L"Data/INI/GameData.ini") != nullptr &&
                     FindEntryCI(bigSum, L"Data/INI/GameData.ini")->uncompressedSize == iniPlain.size() &&
                     FindEntryCI(bigSum, L"Data/INI/GameData.ini")->compressedSize == refPackedIni.size() &&
                     FindEntryCI(bigSum, L"Data/Scripts/readme.txt") != nullptr &&
                     FindEntryCI(bigSum, L"Data/Scripts/readme.txt")->uncompressedSize == rawTxt.size() &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(bigPath, L"Data/INI/GameData.ini", outIni) &&
                     BytesMatchStr(outIni, iniPlain) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(bigPath, L"Data/Scripts/readme.txt", outRaw) &&
                     BytesMatchStr(outRaw, rawTxt);
        bool rejBadRefPack = !ArchiveExplorer::ExtractSingleEntryToMemory(bigPath, L"Data/Corrupt/broken.ini", outBroken);

        tc.Record(okBig && rejBadRefPack,
            L"Stage15.Archives.ElectronicArtsBIG_RefPack0x10FB_SizeAndDecompression",
            L"refPack=" + std::to_wstring(okBig) + L", rejBadRef=" + std::to_wstring(rejBadRefPack));
    }

    // ------------------------------------------------------------------------
    // Test 15.20: LHA / LZH (-lh5- Block Huffman + LZSS + Level-1 Extended Header)
    //             & ARJ (method 1 Block Huffman + LZSS & method 4 Fast Unary LZ77 + CRC32)
    // ------------------------------------------------------------------------
    {
        std::string lh5Plain;
        while (lh5Plain.size() < 1200) {
            lh5Plain += "LHA_LH5_CANONICAL_HUFFMAN_LZSS_LEVEL1_EXTENDED_DIR_HEADER_PAYLOAD_LINE_1234567890!\r\n";
        }
        std::string lh0Plain = "Companion stored -lh0- README inside LZH archive.\n";

        auto lzhBytes = BuildLzhLevel1Archive("docs\\sub", "manual.txt", lh5Plain, "readme.txt", lh0Plain);
        std::wstring lzhPath = baseDir + L"\\retro_lh5.lzh";
        PathUtils::WriteFileBytes(lzhPath, lzhBytes.data(), lzhBytes.size());

        ArchiveSummaryInfo lzhSum;
        std::vector<uint8_t> outLh5, outLh0;
        bool okLzh = lzhBytes.size() < lh5Plain.size() &&
                     ArchiveExplorer::InspectArchiveFile(lzhPath, lzhSum) && lzhSum.valid && lzhSum.entries.size() == 2 &&
                     FindEntryCI(lzhSum, L"docs/sub/manual.txt") != nullptr &&
                     FindEntryCI(lzhSum, L"docs/sub/manual.txt")->uncompressedSize == lh5Plain.size() &&
                     FindEntryCI(lzhSum, L"docs/sub/manual.txt")->compressedSize < lh5Plain.size() &&
                     FindEntryCI(lzhSum, L"readme.txt") != nullptr &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(lzhPath, L"docs/sub/manual.txt", outLh5) &&
                     BytesMatchStr(outLh5, lh5Plain) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(lzhPath, L"readme.txt", outLh0) &&
                     BytesMatchStr(outLh0, lh0Plain);

        std::string arjM1Plain, arjM4Plain;
        while (arjM1Plain.size() < 1100) {
            arjM1Plain += "ARJ_METHOD_1_BLOCK_HUFFMAN_26624_WINDOW_TEST_DATA_ABCDEF_0123456789\n";
        }
        while (arjM4Plain.size() < 950) {
            arjM4Plain += "ARJ_METHOD_4_FAST_UNARY_VARIABLE_WIDTH_LZ77_DECODER_PAYLOAD_XYZ_987654\n";
        }
        auto arjBytes = BuildArjTestArchive("src/engine.c", arjM1Plain, "docs/notes.txt", arjM4Plain);
        std::wstring arjPath = baseDir + L"\\bundle_m1_m4.arj";
        PathUtils::WriteFileBytes(arjPath, arjBytes.data(), arjBytes.size());

        ArchiveSummaryInfo arjSum;
        std::vector<uint8_t> outArjM1, outArjM4, outArjBad;
        bool okArj = arjBytes.size() < (arjM1Plain.size() + arjM4Plain.size()) &&
                     ArchiveExplorer::InspectArchiveFile(arjPath, arjSum) && arjSum.valid && arjSum.entries.size() == 2 &&
                     FindEntryCI(arjSum, L"src/engine.c") != nullptr &&
                     FindEntryCI(arjSum, L"src/engine.c")->uncompressedSize == arjM1Plain.size() &&
                     FindEntryCI(arjSum, L"docs/notes.txt") != nullptr &&
                     FindEntryCI(arjSum, L"docs/notes.txt")->uncompressedSize == arjM4Plain.size() &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(arjPath, L"src/engine.c", outArjM1) &&
                     BytesMatchStr(outArjM1, arjM1Plain) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(arjPath, L"docs/notes.txt", outArjM4) &&
                     BytesMatchStr(outArjM4, arjM4Plain);

        // Corrupt ARJ method 4 compressed stream -> CRC32 / decoder check must fail!
        if (const auto* e4 = FindEntryCI(arjSum, L"docs/notes.txt")) {
            auto badArj = arjBytes;
            badArj[static_cast<size_t>(e4->rawDataOffset)] ^= 0xFFu;
            std::wstring badArjPath = baseDir + L"\\corrupt_crc.arj";
            PathUtils::WriteFileBytes(badArjPath, badArj.data(), badArj.size());
            okArj = okArj && !ArchiveExplorer::ExtractSingleEntryToMemory(badArjPath, L"docs/notes.txt", outArjBad);
        } else {
            okArj = false;
        }

        tc.Record(okLzh && okArj,
            L"Stage15.Archives.LhaLh5Level1_And_ArjMethod1And4_InProcessDecompression",
            L"lzhLh5=" + std::to_wstring(okLzh) + L", arjM1M4=" + std::to_wstring(okArj));
    }

    // ------------------------------------------------------------------------
    // Test 15.21: Debian .deb Inner data.tar.gz Unpacking &
    //             Unix/BSD .ar ("//" GNU Long Names & "#1/N" BSD Inline Names, No DEB Hijack)
    // ------------------------------------------------------------------------
    {
        std::string debBinScript = "#!/bin/sh\necho 'Explorer2 Debian Unpacked Binary OK'\n";
        std::string debConfFile  = "listen_port = 8080\nlog_level = info\n";
        auto debBytes = BuildDebianDebArchive({
            { "./usr/", "" },
            { "./usr/bin/", "" },
            { "./usr/bin/explorer2-cli", debBinScript },
            { "./etc/explorer2.conf", debConfFile }
        });
        std::wstring debPath = baseDir + L"\\explorer2_1.0_amd64.deb";
        PathUtils::WriteFileBytes(debPath, debBytes.data(), debBytes.size());

        ArchiveSummaryInfo debSum;
        std::vector<uint8_t> outDebBin, outDebConf;
        bool okDeb = ArchiveExplorer::InspectArchiveFile(debPath, debSum) && debSum.valid &&
                     debSum.formatName == L"Debian Package (DEB)" &&
                     FindEntryCI(debSum, L"data.tar.gz") == nullptr &&
                     FindEntryCI(debSum, L"usr/bin/explorer2-cli") != nullptr &&
                     FindEntryCI(debSum, L"usr/bin/explorer2-cli")->uncompressedSize == debBinScript.size() &&
                     FindEntryCI(debSum, L"etc/explorer2.conf") != nullptr &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(debPath, L"usr/bin/explorer2-cli", outDebBin) &&
                     BytesMatchStr(outDebBin, debBinScript) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(debPath, L"etc/explorer2.conf", outDebConf) &&
                     BytesMatchStr(outDebConf, debConfFile);

        // Build Unix/BSD .a static library with GNU "//" long name + BSD "#1/28" inline name + "__.SYMDEF"
        std::vector<uint8_t> arBytes = { '!','<','a','r','c','h','>','\n' };
        const std::string symdefDummy = "SYMDEF_TABLE_SKIP";
        AppendArMemberForTest(arBytes, "__.SYMDEF", reinterpret_cast<const uint8_t*>(symdefDummy.data()), symdefDummy.size());
        const std::string gnuStrTbl = "very_long_gnu_archive_module.o/\n";
        AppendArMemberForTest(arBytes, "//", reinterpret_cast<const uint8_t*>(gnuStrTbl.data()), gnuStrTbl.size());
        const std::string gnuObjPayload = "ELF64_GNU_LONG_NAME_OBJECT_CODE_PAYLOAD_V1";
        AppendArMemberForTest(arBytes, "/0", reinterpret_cast<const uint8_t*>(gnuObjPayload.data()), gnuObjPayload.size());

        // BSD "#1/28": 28-byte NUL-padded inline filename + exact object payload immediately following
        std::string bsdInlineName = "bsd_inline_extended_unit.o";
        std::vector<uint8_t> bsdCombined(28u, 0u);
        std::memcpy(bsdCombined.data(), bsdInlineName.data(), bsdInlineName.size());
        const std::string bsdObjPayload = "MACHO64_BSD_EXTENDED_INLINE_OBJECT_EXACT_BYTES!";
        bsdCombined.insert(bsdCombined.end(), bsdObjPayload.begin(), bsdObjPayload.end());
        AppendArMemberForTest(arBytes, "#1/28", bsdCombined.data(), bsdCombined.size());

        std::wstring arPath = baseDir + L"\\libbsd_gnu_test.a";
        PathUtils::WriteFileBytes(arPath, arBytes.data(), arBytes.size());

        ArchiveSummaryInfo arSum;
        std::vector<uint8_t> outGnuObj, outBsdObj;
        bool okAr = ArchiveExplorer::InspectArchiveFile(arPath, arSum) && arSum.valid &&
                    arSum.formatName == L"Unix Static Archive (.ar/.a)" &&
                    arSum.entries.size() == 2 &&
                    FindEntryCI(arSum, L"very_long_gnu_archive_module.o") != nullptr &&
                    FindEntryCI(arSum, L"bsd_inline_extended_unit.o") != nullptr &&
                    FindEntryCI(arSum, L"bsd_inline_extended_unit.o")->uncompressedSize == bsdObjPayload.size() &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(arPath, L"very_long_gnu_archive_module.o", outGnuObj) &&
                    BytesMatchStr(outGnuObj, gnuObjPayload) &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(arPath, L"bsd_inline_extended_unit.o", outBsdObj) &&
                    BytesMatchStr(outBsdObj, bsdObjPayload);

        tc.Record(okDeb && okAr,
            L"Stage15.Archives.DebianDeb_InnerDataTarGz_And_UnixBsdAr_LongNamesNoHijack",
            L"deb=" + std::to_wstring(okDeb) + L", ar=" + std::to_wstring(okAr));
    }

    // ------------------------------------------------------------------------
    // Test 15.22: Linux EROFS (.erofs) z_erofs LZ4-Compressed Inodes
    //             (FLAT_COMPRESSION_LEGACY / COMPRESSED_FULL & COMPRESSED_COMPACT)
    // ------------------------------------------------------------------------
    {
        std::string erofsLegacyPlain, erofsCompactPlain;
        while (erofsLegacyPlain.size() < 5400) { // Spans 2 x 4096B logical clusters!
            erofsLegacyPlain += "Z_EROFS_LZ4_0PADDING_LEGACY_FULL_2_CLUSTER_FILE_LINE_0123456789ABCDEF!\n";
        }
        while (erofsCompactPlain.size() < 2600) {
            erofsCompactPlain += "Z_EROFS_COMPRESSED_COMPACT_4B_PACK_LZ4_INODE_CONTENT_9876543210!\n";
        }

        auto erofsImg = BuildCompressedErofsTestImage(erofsLegacyPlain, erofsCompactPlain);
        std::wstring zErofsPath = baseDir + L"\\system_lz4.erofs";
        PathUtils::WriteFileBytes(zErofsPath, erofsImg.data(), erofsImg.size());

        ArchiveSummaryInfo zeSum;
        std::vector<uint8_t> outZeLeg, outZeComp;
        bool okZErofs = ArchiveExplorer::InspectArchiveFile(zErofsPath, zeSum) && zeSum.valid && zeSum.entries.size() == 2 &&
                        FindEntryCI(zeSum, L"legacy_lz4.bin") != nullptr &&
                        FindEntryCI(zeSum, L"legacy_lz4.bin")->uncompressedSize == erofsLegacyPlain.size() &&
                        FindEntryCI(zeSum, L"compact_lz4.bin") != nullptr &&
                        FindEntryCI(zeSum, L"compact_lz4.bin")->uncompressedSize == erofsCompactPlain.size() &&
                        ArchiveExplorer::ExtractSingleEntryToMemory(zErofsPath, L"legacy_lz4.bin", outZeLeg) &&
                        BytesMatchStr(outZeLeg, erofsLegacyPlain) &&
                        ArchiveExplorer::ExtractSingleEntryToMemory(zErofsPath, L"compact_lz4.bin", outZeComp) &&
                        BytesMatchStr(outZeComp, erofsCompactPlain);

        tc.Record(okZErofs,
            L"Stage15.Archives.LinuxEROFS_ZeroPaddedLZ4_CompressedLegacyAndCompact",
            L"zErofs=" + std::to_wstring(okZErofs));
    }

    // ------------------------------------------------------------------------
    // Test 15.23: Bohemia Interactive PBO (.pbo) "srpC" LZSS Decompression +
    //             Unsigned Byte-Sum Checksum Verification & Corrupt Checksum Rejection
    // ------------------------------------------------------------------------
    {
        std::string sqfPlain = "    "; // Leading spaces + repeating Arma SQF script lines
        while (sqfPlain.size() < 900) {
            sqfPlain += "player addMagazine \"30Rnd_65x39_caseless_mag\"; hintSilent \"PBO_LZSS_OK\";\r\n";
        }
        std::string sqmPlain = "version=54;\nclass EditorData { moveGridStep=1.0; };\n";

        auto pboBytes = BuildBohemiaPboTestArchive("addons\\mission_pack", "scripts\\init.sqf", sqfPlain, "mission.sqm", sqmPlain);
        std::wstring pboPath = baseDir + L"\\mission_pack.pbo";
        PathUtils::WriteFileBytes(pboPath, pboBytes.data(), pboBytes.size());

        ArchiveSummaryInfo pboSum;
        std::vector<uint8_t> outSqf, outSqm, outBadPbo;
        bool okPbo = ArchiveExplorer::InspectArchiveFile(pboPath, pboSum) && pboSum.valid && pboSum.entries.size() == 2 &&
                     FindEntryCI(pboSum, L"addons/mission_pack/scripts/init.sqf") != nullptr &&
                     FindEntryCI(pboSum, L"addons/mission_pack/scripts/init.sqf")->uncompressedSize == sqfPlain.size() &&
                     FindEntryCI(pboSum, L"addons/mission_pack/scripts/init.sqf")->compressedSize < sqfPlain.size() &&
                     FindEntryCI(pboSum, L"addons/mission_pack/mission.sqm") != nullptr &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(pboPath, L"addons/mission_pack/scripts/init.sqf", outSqf) &&
                     BytesMatchStr(outSqf, sqfPlain) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(pboPath, L"addons/mission_pack/mission.sqm", outSqm) &&
                     BytesMatchStr(outSqm, sqmPlain);

        // Corrupt the 4-byte unsigned byte-sum checksum at the end of init.sqf's compressed stream -> MUST reject!
        if (const auto* sqfEnt = FindEntryCI(pboSum, L"addons/mission_pack/scripts/init.sqf")) {
            auto badPbo = pboBytes;
            size_t csumOff = static_cast<size_t>(sqfEnt->rawDataOffset + sqfEnt->compressedSize - 4u);
            badPbo[csumOff] ^= 0x01u;
            std::wstring badPboPath = baseDir + L"\\bad_csum.pbo";
            PathUtils::WriteFileBytes(badPboPath, badPbo.data(), badPbo.size());
            okPbo = okPbo && !ArchiveExplorer::ExtractSingleEntryToMemory(
                badPboPath, L"addons/mission_pack/scripts/init.sqf", outBadPbo);
        } else {
            okPbo = false;
        }

        tc.Record(okPbo,
            L"Stage15.Archives.BohemiaPBO_SrpCLZSS_Decompression_And_ChecksumVerify",
            L"pboLzss=" + std::to_wstring(okPbo));
    }

    // ------------------------------------------------------------------------
    // Test 15.24: RFC 7932 Brotli Single Stream (.br) & Multi-File Tarballs (.tbr / .tar.br)
    // ------------------------------------------------------------------------
    {
        std::string brSinglePlain;
        while (brSinglePlain.size() < 1400) {
            brSinglePlain += "{\"service\":\"explorer2\",\"codec\":\"RFC7932_BROTLI\",\"status\":\"VERIFIED\"}\n";
        }
        auto brSingle = CompressBrotliRfc7932ForTest(
            reinterpret_cast<const uint8_t*>(brSinglePlain.data()), brSinglePlain.size());
        std::wstring brPath = baseDir + L"\\telemetry.json.br";
        PathUtils::WriteFileBytes(brPath, brSingle.data(), brSingle.size());

        // Trailing garbage after valid Brotli stream MUST be rejected due to requireExactEnd=true!
        auto brTrailingJunk = brSingle;
        brTrailingJunk.push_back(0xDEu);
        brTrailingJunk.push_back(0xADu);
        std::wstring badBrPath = baseDir + L"\\trailing_junk.json.br";
        PathUtils::WriteFileBytes(badBrPath, brTrailingJunk.data(), brTrailingJunk.size());

        std::string tarFileA = "Brotli tarball first config payload inside subdir.\n";
        std::string tarFileB;
        while (tarFileB.size() < 900) {
            tarFileB += "export const BROTLI_TAR_LINE = 'MULTI_FILE_TAR_BR_OK_12345';\n";
        }
        auto rawTar = BuildPosixTar({
            { "pkg/", "" },
            { "pkg/config.toml", tarFileA },
            { "pkg/index.ts", tarFileB }
        });
        auto brTar = CompressBrotliRfc7932ForTest(rawTar.data(), rawTar.size());
        std::wstring tbrPath   = baseDir + L"\\release_pkg.tbr";
        std::wstring tarBrPath = baseDir + L"\\release_pkg.tar.br";
        PathUtils::WriteFileBytes(tbrPath, brTar.data(), brTar.size());
        PathUtils::WriteFileBytes(tarBrPath, brTar.data(), brTar.size());

        ArchiveSummaryInfo brSum, tbrSum, tarBrSum, badBrSum;
        std::vector<uint8_t> outBrSingle, outTbrA, outTarBrB;
        bool okBr = brSingle.size() < brSinglePlain.size() &&
                    brTar.size() < rawTar.size() &&
                    ArchiveExplorer::IsArchiveExtension(L".br") &&
                    ArchiveExplorer::IsArchiveExtension(L".tbr") &&
                    ArchiveExplorer::InspectArchiveFile(brPath, brSum) && brSum.valid && brSum.entries.size() == 1 &&
                    FindEntryCI(brSum, L"telemetry.json") != nullptr &&
                    FindEntryCI(brSum, L"telemetry.json")->uncompressedSize == brSinglePlain.size() &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(brPath, L"telemetry.json", outBrSingle) &&
                    BytesMatchStr(outBrSingle, brSinglePlain) &&
                    !ArchiveExplorer::InspectArchiveFile(badBrPath, badBrSum) &&
                    ArchiveExplorer::InspectArchiveFile(tbrPath, tbrSum) && tbrSum.valid && tbrSum.formatName == L"TAR.BR" &&
                    FindEntryCI(tbrSum, L"pkg/config.toml") != nullptr &&
                    FindEntryCI(tbrSum, L"pkg/index.ts") != nullptr &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(tbrPath, L"pkg/config.toml", outTbrA) &&
                    BytesMatchStr(outTbrA, tarFileA) &&
                    ArchiveExplorer::InspectArchiveFile(tarBrPath, tarBrSum) && tarBrSum.valid && tarBrSum.formatName == L"TAR.BR" &&
                    ArchiveExplorer::ExtractSingleEntryToMemory(tarBrPath, L"pkg/index.ts", outTarBrB) &&
                    BytesMatchStr(outTarBrB, tarFileB);

        tc.Record(okBr,
            L"Stage15.Archives.BrotliStream_And_TarBr_Tbr_Decompression",
            L"brotli=" + std::to_wstring(okBr));
    }

    // ------------------------------------------------------------------------
    // Test 15.25: Universal Disk Format (.udf — OSTA UTF-16BE, Subdir, Inline ICB + Short AD)
    //             & Nero Optical Image (.nrg — NER5 + DAOX 2352-Byte Mode 1 Raw Sector Slicing)
    // ------------------------------------------------------------------------
    {
        const std::wstring uDirName    = L"Docs_\u03A9";
        const std::wstring uInlineName = L"note_\u00E9.txt";
        const std::wstring uShortName  = L"video_stream.bin";
        const std::string inlineTxt    = "UDF 2.50 Inline ICB (allocType == 3) payload stored directly inside File Entry!";
        std::string shortAdData;
        while (shortAdData.size() < 2900) { // Spans 2 x 2048B UDF logical blocks!
            shortAdData += "UDF_SHORT_ALLOCATION_DESCRIPTOR_MULTI_SECTOR_PAYLOAD_LINE_0123456789!\r\n";
        }

        auto udfBytes = BuildUdfTestImage(uDirName, uInlineName, inlineTxt, uShortName, shortAdData, false);
        std::wstring udfPath = baseDir + L"\\optical_disc.udf";
        PathUtils::WriteFileBytes(udfPath, udfBytes.data(), udfBytes.size());

        auto udf250MetaBytes = BuildUdfTestImage(uDirName, uInlineName, inlineTxt, uShortName, shortAdData, true);
        auto nrgBytes = WrapOpticalImageInNeroNer5Raw2352(udf250MetaBytes);
        std::wstring nrgPath = baseDir + L"\\nero_disc.nrg";
        PathUtils::WriteFileBytes(nrgPath, nrgBytes.data(), nrgBytes.size());

        const std::wstring wantInlinePath = uDirName + L"/" + uInlineName;
        ArchiveSummaryInfo udfSum, nrgSum;
        std::vector<uint8_t> outUdfInline, outUdfShort, outNrgInline, outNrgShort;
        bool okUdf = ArchiveExplorer::IsArchiveExtension(L".udf") &&
                     ArchiveExplorer::InspectArchiveFile(udfPath, udfSum) && udfSum.valid &&
                     FindEntryCI(udfSum, uDirName) != nullptr && FindEntryCI(udfSum, uDirName)->isDirectory &&
                     FindEntryCI(udfSum, wantInlinePath) != nullptr &&
                     FindEntryCI(udfSum, wantInlinePath)->uncompressedSize == inlineTxt.size() &&
                     FindEntryCI(udfSum, uShortName) != nullptr &&
                     FindEntryCI(udfSum, uShortName)->uncompressedSize == shortAdData.size() &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(udfPath, wantInlinePath, outUdfInline) &&
                     BytesMatchStr(outUdfInline, inlineTxt) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(udfPath, uShortName, outUdfShort) &&
                     BytesMatchStr(outUdfShort, shortAdData);

        bool okNrg = ArchiveExplorer::IsArchiveExtension(L".nrg") &&
                     ArchiveExplorer::InspectArchiveFile(nrgPath, nrgSum) && nrgSum.valid &&
                     nrgSum.formatName.find(L"Nero") != std::wstring::npos &&
                     FindEntryCI(nrgSum, wantInlinePath) != nullptr &&
                     FindEntryCI(nrgSum, uShortName) != nullptr &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(nrgPath, wantInlinePath, outNrgInline) &&
                     BytesMatchStr(outNrgInline, inlineTxt) &&
                     ArchiveExplorer::ExtractSingleEntryToMemory(nrgPath, uShortName, outNrgShort) &&
                     BytesMatchStr(outNrgShort, shortAdData);

        tc.Record(okUdf && okNrg,
            L"Stage15.Archives.UdfOptical_OstaUnicode_InlineAndShortAD_And_NeroNer5Raw2352",
            L"udf=" + std::to_wstring(okUdf) + L", nrg=" + std::to_wstring(okNrg));
    }

    // ------------------------------------------------------------------------
    // Test 15.26: ISO 28500 Web ARChive (.warc — Uncompressed WARC/1.1 + Multi-Member GZIP,
    //             warcinfo + HTTP Response Header Stripping + Chunked Transfer Decoding)
    // ------------------------------------------------------------------------
    {
        const std::string warcInfoBody = "software: Explorer2 Crawler/1.0\r\nformat: WARC File Format 1.1\r\n";
        const std::string httpReqRaw   = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
        const std::string htmlHomeBody = "<!doctype html><html><body><h1>Example Archived Home</h1></body></html>\n";
        const std::string httpRespHome =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
            std::to_string(htmlHomeBody.size()) + "\r\n\r\n" + htmlHomeBody;

        const std::string jsonPart1 = "{\"archived\":true,";
        const std::string jsonPart2 = "\"items\":[10,20,30]}";
        const std::string jsonExpected = jsonPart1 + jsonPart2;
        char hex1[16] = {}, hex2[16] = {};
        snprintf(hex1, sizeof(hex1), "%zx", jsonPart1.size());
        snprintf(hex2, sizeof(hex2), "%zx", jsonPart2.size());
        const std::string httpRespChunked =
            std::string("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n") +
            hex1 + "\r\n" + jsonPart1 + "\r\n" +
            hex2 + ";ext=ok\r\n" + jsonPart2 + "\r\n0\r\n\r\n";

        const std::string rec1 = MakeWarc11Record("warcinfo", "", "application/warc-fields", warcInfoBody);
        const std::string rec2 = MakeWarc11Record("request", "https://example.com/", "application/http; msgtype=request", httpReqRaw);
        const std::string rec3 = MakeWarc11Record("response", "https://example.com/", "application/http; msgtype=response", httpRespHome);
        const std::string rec4 = MakeWarc11Record("response", "https://example.com/api/items.json", "application/http; msgtype=response", httpRespChunked);
        const std::string plainWarc = rec1 + rec2 + rec3 + rec4;

        std::wstring warcPath = baseDir + L"\\archive_crawl.warc";
        PathUtils::WriteFileBytes(warcPath, reinterpret_cast<const uint8_t*>(plainWarc.data()), plainWarc.size());

        // Multi-member GZIP .warc: compress (rec1+rec2) as GZIP member 1 and (rec3+rec4) as GZIP member 2!
        const std::string m1Plain = rec1 + rec2;
        const std::string m2Plain = rec3 + rec4;
        auto gz1 = CompressGzip(reinterpret_cast<const uint8_t*>(m1Plain.data()), m1Plain.size());
        auto gz2 = CompressGzip(reinterpret_cast<const uint8_t*>(m2Plain.data()), m2Plain.size());
        std::vector<uint8_t> gzWarc = std::move(gz1);
        gzWarc.insert(gzWarc.end(), gz2.begin(), gz2.end());
        std::wstring gzWarcPath = baseDir + L"\\archive_multimember.warc";
        PathUtils::WriteFileBytes(gzWarcPath, gzWarc.data(), gzWarc.size());

        ArchiveSummaryInfo wSum, gzwSum;
        std::vector<uint8_t> outWi, outHome, outJson, outGzHome, outGzJson;
        bool okWarc = ArchiveExplorer::IsArchiveExtension(L".warc") &&
                      ArchiveExplorer::InspectArchiveFile(warcPath, wSum) && wSum.valid &&
                      FindEntryCI(wSum, L"warcinfo.txt") != nullptr &&
                      FindEntryCI(wSum, L"example.com/index.html") != nullptr &&
                      FindEntryCI(wSum, L"example.com/index.html")->uncompressedSize == htmlHomeBody.size() &&
                      FindEntryCI(wSum, L"example.com/api/items.json") != nullptr &&
                      FindEntryCI(wSum, L"example.com/api/items.json")->uncompressedSize == jsonExpected.size() &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(warcPath, L"warcinfo.txt", outWi) &&
                      BytesMatchStr(outWi, warcInfoBody) &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(warcPath, L"example.com/index.html", outHome) &&
                      BytesMatchStr(outHome, htmlHomeBody) &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(warcPath, L"example.com/api/items.json", outJson) &&
                      BytesMatchStr(outJson, jsonExpected);

        bool okGzWarc = ArchiveExplorer::InspectArchiveFile(gzWarcPath, gzwSum) && gzwSum.valid &&
                        FindEntryCI(gzwSum, L"warcinfo.txt") != nullptr &&
                        FindEntryCI(gzwSum, L"example.com/index.html") != nullptr &&
                        FindEntryCI(gzwSum, L"example.com/api/items.json") != nullptr &&
                        ArchiveExplorer::ExtractSingleEntryToMemory(gzWarcPath, L"example.com/index.html", outGzHome) &&
                        BytesMatchStr(outGzHome, htmlHomeBody) &&
                        ArchiveExplorer::ExtractSingleEntryToMemory(gzWarcPath, L"example.com/api/items.json", outGzJson) &&
                        BytesMatchStr(outGzJson, jsonExpected);

        tc.Record(okWarc && okGzWarc,
            L"Stage15.Archives.Iso28500Warc_HttpResponseBody_Chunked_And_MultiMemberGzip",
            L"plainWarc=" + std::to_wstring(okWarc) + L", gzWarc=" + std::to_wstring(okGzWarc));
    }

    // ------------------------------------------------------------------------
    // Test 15.27: Android Sparse Filesystem Image (.simg — RAW + FILL + DONT_CARE + CRC32)
    //             Mounting Inner ext4 Filesystem with Byte-Exact Extraction
    // ------------------------------------------------------------------------
    {
        std::string simgConf;
        while (simgConf.size() < 2500) {
            simgConf += "ro.product.system.brand=Explorer2\nro.build.type=user\n";
        }
        simgConf.resize(2500, '#');

        // Include a 8192-byte constant 0x5A5A5A5A region inside the 13,500B file so 4096B blocks
        // inside the ext4 image become real CHUNK_TYPE_FILL (0xCAC2) chunks in the .simg!
        std::string simgLarge(13500, 'Z'); // 0x5A5A5A5A!
        for (size_t i = 0; i < 1200; ++i) simgLarge[i] = static_cast<char>('A' + (i % 26));
        for (size_t i = 12800; i < simgLarge.size(); ++i) simgLarge[i] = static_cast<char>('0' + (i % 10));

        // Build 40KB ext4 image (pad to 64KB = 16 x 4096B sparse blocks so trailing zero blocks become DONT_CARE!)
        auto rawExt4 = BuildExt4ExtentAndIndirectImage(simgConf, simgLarge);
        rawExt4.resize(16u * 4096u, 0u);

        auto simgBytes = EncodeRawImageAsAndroidSimg(rawExt4, 4096u);
        std::wstring simgPath = baseDir + L"\\system_sparse.simg";
        PathUtils::WriteFileBytes(simgPath, simgBytes.data(), simgBytes.size());

        ArchiveSummaryInfo simgSum;
        std::vector<uint8_t> outSimgConf, outSimgLarge;
        bool okSimg = simgBytes.size() < rawExt4.size() &&
                      ArchiveExplorer::IsArchiveExtension(L".simg") &&
                      ArchiveExplorer::InspectArchiveFile(simgPath, simgSum) && simgSum.valid &&
                      simgSum.formatName.find(L"ext4") != std::wstring::npos &&
                      FindEntryCI(simgSum, L"etc/app.conf") != nullptr &&
                      FindEntryCI(simgSum, L"etc/app.conf")->uncompressedSize == simgConf.size() &&
                      FindEntryCI(simgSum, L"legacy_indirect.bin") != nullptr &&
                      FindEntryCI(simgSum, L"legacy_indirect.bin")->uncompressedSize == simgLarge.size() &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(simgPath, L"etc/app.conf", outSimgConf) &&
                      BytesMatchStr(outSimgConf, simgConf) &&
                      ArchiveExplorer::ExtractSingleEntryToMemory(simgPath, L"legacy_indirect.bin", outSimgLarge) &&
                      BytesMatchStr(outSimgLarge, simgLarge);

        tc.Record(okSimg,
            L"Stage15.Archives.AndroidSparseSimg_RawFillDontCareExtents_MountingExt4",
            L"simgExt4=" + std::to_wstring(okSimg));
    }

    // ------------------------------------------------------------------------
    // Test 15.19: Corrupt / Truncated Archive & Stream Safety (Zero Crashes, Zero ReaderGuard Stops)
    // ------------------------------------------------------------------------
    {
        struct BadSample { const wchar_t* name; std::vector<uint8_t> bytes; };
        std::vector<BadSample> bads = {
            { L"trunc.7z",     { '7','z',0xBC,0xAF,0x27,0x1C, 0,4, 0,0,0,0, 99,0,0,0,0,0,0,0, 50,0,0,0,0,0,0,0, 0,0,0,0, 0x17,0x06,0xFF } },
            { L"bad.xz",       { 0xFD,'7','z','X','Z',0x00, 0x00,0x01, 0,0,0,0, 0x02,0xFF,0x21,0x99 } },
            { L"bad.bz2",      { 'B','Z','h','9', 0x31,0x41,0x59,0x26,0x53,0x59, 0xFF,0xFF,0xFF } },
            { L"bad.lz4",      { 0x04,0x22,0x4D,0x18, 0x60, 0x70, 0x00, 0xFF,0xFF,0x00,0x00, 0xFF } },
            { L"bad.zst",      { 0x28,0xB5,0x2F,0xFD, 0x20, 0x10, 0xFF,0xFF,0x7F } },
            { L"bad.bsa",      { 'B','S','A',0, 104,0,0,0, 36,0,0,0, 0xFF,0,0,0, 99,99,0,0 } },
            { L"bad.ba2",      { 'B','T','D','X', 1,0,0,0, 'G','N','R','L', 99,99,0,0, 0xFF,0xFF,0,0,0,0,0,0 } },
            { L"bad.rpm",      { 0xED,0xAB,0xEE,0xDB, 3,0, 0,0,0,0 } },
            { L"bad.vhd",      { 'c','o','n','e','c','t','i','x', 0,0,0,2 } },
            { L"bad.bundle",   { 'U','n','i','t','y','F','S',0, 0,0,0,7, '5',0,'2',0, 0,0,0,0,0,0,1,0, 0,0,1,0, 0,0,2,0, 0,0,0,0x43 } },
            { L"bad.mpq",      { 'M','P','Q',0x1A, 32,0,0,0, 64,0,0,0, 0,0,0,0, 99,0,0,0, 99,0,0,0, 16,0,0,0, 8,0,0,0 } },
            { L"bad.rgss3a",   { 'R','G','S','S','A','D',0,3, 1,2,3,4, 0xFF,0xFF,0xFF,0xFF } },
            { L"bad.squashfs", { 'h','s','q','s', 1,0,0,0, 0,0,0,0, 0,16,0,0 } },
            { L"bad.dmg",      { 'k','o','l','y', 0,0,0,4, 0,0,2,0, 0xFF,0xFF,0xFF,0xFF } },
            { L"bad.ext4",     { 0,0,0,0, 0x53,0xEF, 0xFF,0xFF,0xFF,0xFF } },
            { L"bad.wim",      { 'M','S','W','I','M',0,0,0, 208,0,0,0, 0,13,1,0, 2,0,2,0, 0xFF,0xFF } },
            { L"bad.vpk",      { 0x34,0x12,0xAA,0x55, 2,0,0,0, 99,0,0,0, 't','x','t',0, 'a',0 } },
            { L"bad.asar",     { 4,0,0,0, 32,0,0,0, 28,0,0,0, 20,0,0,0, '{','"','f','i','l','e','s','"',':','{' } },
            { L"bad.big",      { 'B','I','G','F', 64,0,0,0, 0,0,0,5, 0,0,0,32, 0xFF,0xFF,0xFF,0xFF, 0,0,1,0, 'a',0 } },
            { L"bad.br",       { 0xFF, 0xFF, 0xFF, 0x7F, 0xAA, 0x55 } },
            { L"bad.tbr",      { 0x01, 0xF0, 0xFF, 0x99, 0x12, 0x34 } },
            { L"bad.udf",      { 2,0, 3,0, 5,0, 0,0, 0,0, 0,0, 0,1,0,0 } },
            { L"bad.nrg",      { 0,0,0,0, 'D','A','O','X', 0,0,0,99, 'N','E','R','5', 0,0,0,0, 0xFF,0xFF,0xFF,0xFF } },
            { L"bad.warc",     { 'W','A','R','C','/','1','.','1','\r','\n','C','o','n','t','e','n','t','-','L','e','n','g','t','h',':',' ','9','9','9','9','\r','\n','\r','\n','x' } },
            { L"bad.simg",     { 0x3A,0xFF,0x26,0xED, 1,0, 0,0, 28,0, 12,0, 0,16,0,0, 99,0,0,0, 5,0,0,0, 0,0,0,0, 0xC1,0xCA,0,0, 9,0,0,0, 12,0,0,0 } },
            { L"bad.lzh",      { 26,0,'-','l','h','5','-', 99,0,0,0, 99,0,0,0, 0,0,0,0, 0x20,1, 3,'a','.','c', 0,0,'M', 99,0 } },
            { L"bad.arj",      { 0x60,0xEA, 34,0, 30,6,1,4,0,0,0,0, 0,0,0,0, 99,0,0,0, 99,0,0,0 } },
            { L"bad.pbo",      { 'a','.','s','q','f',0, 's','r','p','C', 99,0,0,0, 0,0,0,0, 0,0,0,0, 99,0,0,0, 0 } }
        };
        for (const auto& b : bads) {
            std::wstring p = baseDir + L"\\" + b.name;
            PathUtils::WriteFileBytes(p, b.bytes.data(), b.bytes.size());
            ArchiveSummaryInfo dummy;
            std::vector<uint8_t> dummyOut;
            ArchiveExplorer::InspectArchiveFile(p, dummy);
            ArchiveExplorer::ExtractSingleEntryToMemory(p, L"anything.bin", dummyOut);
        }

        const uint64_t guardAfter = ReaderGuard::StoppedCount();
        tc.Record(guardAfter == guardBefore,
            L"Stage15.Archives.CorruptInputs_ZeroExceptions_ZeroReaderFaults",
            L"guardStopsDelta=" + std::to_wstring(guardAfter - guardBefore));
    }

    RemoveDirTreeForStage15(baseDir);
    RunFormatTestsArchivesBatch70(tc);
}

} // namespace Explorer2::SelfTestSuite
