#pragma once
// LhaArjHuffmanCommon.h — Shared classic archiver codecs:
//   - Reflected CRC-16/ARC (0xA001) used by ZOO, SEA ARC / PKPAK, and StuffIt Classic
//   - RLE90 (0x90 DLE byte-run packing) used by SEA ARC and StuffIt Classic
//   - UNIX compress / Squash / StuffIt LSB-first variable-width LZW (9..14 bits)
//   - LHA / ARJ / ZOO LH5 MSB-first block-adaptive canonical Huffman + LZ77 decoder

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Explorer2::Formats::DetailUnixAndLegacyArc {

// ----------------------------------------------------------------------------
// Reflected CRC-16/ARC (polynomial 0x8005 -> reflected 0xA001, init 0x0000)
// ----------------------------------------------------------------------------
inline uint16_t Crc16Arc(const uint8_t* data, size_t len, uint16_t crc = 0u) noexcept {
    static const std::array<uint16_t, 256> kTable = [] {
        std::array<uint16_t, 256> t{};
        for (uint32_t i = 0; i < 256u; ++i) {
            uint16_t c = static_cast<uint16_t>(i);
            for (int b = 0; b < 8; ++b) {
                c = (c & 1u) ? static_cast<uint16_t>((c >> 1) ^ 0xA001u) : static_cast<uint16_t>(c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < len; ++i) {
        crc = static_cast<uint16_t>((crc >> 8) ^ kTable[(crc ^ data[i]) & 0xFFu]);
    }
    return crc;
}

// ----------------------------------------------------------------------------
// RLE90 (0x90 DLE escape packing: 0x90 0x00 -> literal 0x90; 0x90 N -> repeat prev N-1 times)
// ----------------------------------------------------------------------------
inline bool DecodeRle90(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut = 128 * 1024 * 1024,
    size_t stopAtExactOut = SIZE_MAX)
{
    out.clear();
    if (!src && srcLen > 0) return false;
    if (stopAtExactOut != SIZE_MAX) {
        if (stopAtExactOut > maxOut) return false;
        out.reserve(std::min<size_t>(stopAtExactOut, srcLen * 8u + 64u));
    }

    uint8_t lastByte = 0u;
    bool hasLast = false;
    bool inEscape = false;

    for (size_t i = 0; i < srcLen; ++i) {
        if (out.size() >= stopAtExactOut) break;
        const uint8_t b = src[i];
        if (!inEscape) {
            if (b == 0x90u) {
                inEscape = true;
            } else {
                if (out.size() >= maxOut) return false;
                out.push_back(b);
                lastByte = b;
                hasLast = true;
            }
        } else {
            inEscape = false;
            if (b == 0x00u) {
                if (out.size() >= maxOut) return false;
                out.push_back(0x90u);
                lastByte = 0x90u;
                hasLast = true;
            } else {
                if (!hasLast) return false;
                const size_t rep = static_cast<size_t>(b) - 1u;
                const size_t cap = std::min<size_t>(maxOut, stopAtExactOut);
                if (rep > cap - out.size()) {
                    if (stopAtExactOut != SIZE_MAX) {
                        out.insert(out.end(), stopAtExactOut - out.size(), lastByte);
                        break;
                    }
                    return false;
                }
                out.insert(out.end(), rep, lastByte);
            }
        }
    }
    return stopAtExactOut == SIZE_MAX || out.size() == stopAtExactOut;
}

// ----------------------------------------------------------------------------
// Classic UNIX `compress` / SEA ARC Method 8 & 9 / StuffIt Method 2 LZW Decoder
//   LSB-first codes in n_bits-byte aligned groups, optional Clear code 256.
// ----------------------------------------------------------------------------
inline bool DecodeUnixCompressLzw(
    const uint8_t* src,
    size_t srcLen,
    int maxBits,
    bool blockClear,
    std::vector<uint8_t>& out,
    size_t maxOut = 128 * 1024 * 1024,
    size_t expectedOut = SIZE_MAX)
{
    out.clear();
    if (expectedOut == 0) return true;
    if (!src || srcLen == 0 || maxBits < 9 || maxBits > 16) return false;
    if (expectedOut != SIZE_MAX && expectedOut > maxOut) return false;
    if (expectedOut != SIZE_MAX) {
        out.reserve(std::min<size_t>(expectedOut, srcLen * 8u + 256u));
    }

    const uint32_t maxMaxCode = 1u << maxBits;
    std::vector<uint16_t> prefix(maxMaxCode, 0u);
    std::vector<uint8_t>  suffix(maxMaxCode, 0u);
    std::vector<uint8_t>  stack(maxMaxCode + 16u, 0u);
    for (uint32_t c = 0; c < 256u; ++c) {
        suffix[c] = static_cast<uint8_t>(c);
    }

    int nBits = 9;
    uint32_t maxCode = (1u << nBits) - 1u;
    uint32_t freeEnt = blockClear ? 257u : 256u;
    bool clearFlg = false;

    size_t srcPos = 0;
    uint8_t gBuf[20] = {};
    int gOffset = 0;   // bit offset in gBuf
    int gSizeBits = 0; // valid bits in gBuf

    auto getCode = [&]() -> int32_t {
        if (clearFlg || gOffset >= gSizeBits || freeEnt > maxCode) {
            if (freeEnt > maxCode) {
                ++nBits;
                maxCode = (nBits == maxBits) ? maxMaxCode : ((1u << nBits) - 1u);
            }
            if (clearFlg) {
                nBits = 9;
                maxCode = (1u << nBits) - 1u;
                clearFlg = false;
            }
            if (srcPos >= srcLen) return -1;
            const size_t chunk = std::min<size_t>(static_cast<size_t>(nBits), srcLen - srcPos);
            std::memset(gBuf, 0, sizeof(gBuf));
            std::memcpy(gBuf, src + srcPos, chunk);
            srcPos += chunk;
            gOffset = 0;
            // Truncate to integral number of nBits codes in this group (exact ncompress behavior)
            gSizeBits = static_cast<int>((chunk << 3) - (nBits - 1));
            if (gSizeBits <= 0) return -1;
        }
        const int rOff = gOffset;
        int bits = nBits;
        const uint8_t* bp = gBuf + (rOff >> 3);
        int bShift = rOff & 7;
        uint32_t code = (*bp++) >> bShift;
        bits -= (8 - bShift);
        int outShift = 8 - bShift;
        if (bits >= 8) {
            code |= static_cast<uint32_t>(*bp++) << outShift;
            outShift += 8;
            bits -= 8;
        }
        if (bits > 0) {
            code |= static_cast<uint32_t>((*bp) & ((1u << bits) - 1u)) << outShift;
        }
        gOffset += nBits;
        return static_cast<int32_t>(code);
    };

    int32_t oldCode = getCode();
    if (oldCode < 0) return false;
    if (oldCode > 255) return false;
    uint8_t finChar = static_cast<uint8_t>(oldCode);
    out.push_back(finChar);

    const size_t hardCap = (expectedOut != SIZE_MAX) ? expectedOut : maxOut;
    while (out.size() < hardCap) {
        int32_t inCode = getCode();
        if (inCode < 0) break;

        if (inCode == 256 && blockClear) {
            clearFlg = true;
            freeEnt = 256u;
            inCode = getCode();
            if (inCode < 0) break;
            // After CLEAR, the very first code is a literal root or table reset
            if (inCode >= 256) return false;
            finChar = static_cast<uint8_t>(inCode);
            oldCode = inCode;
            freeEnt = 257u;
            out.push_back(finChar);
            continue;
        }

        int32_t code = inCode;
        size_t sp = 0;
        if (static_cast<uint32_t>(code) >= freeEnt) {
            if (static_cast<uint32_t>(code) > freeEnt) return false;
            stack[sp++] = finChar;
            code = oldCode;
        }

        while (code >= 256) {
            if (sp >= maxMaxCode) return false;
            stack[sp++] = suffix[static_cast<uint32_t>(code)];
            code = prefix[static_cast<uint32_t>(code)];
        }
        finChar = suffix[static_cast<uint32_t>(code)];
        stack[sp++] = finChar;

        if (sp > hardCap - out.size()) {
            const size_t take = hardCap - out.size();
            for (size_t k = 0; k < take; ++k) {
                out.push_back(stack[sp - 1u - k]);
            }
            break;
        }
        for (size_t k = sp; k > 0; --k) {
            out.push_back(stack[k - 1u]);
        }

        if (freeEnt < maxMaxCode) {
            prefix[freeEnt] = static_cast<uint16_t>(oldCode);
            suffix[freeEnt] = finChar;
            ++freeEnt;
        }
        oldCode = inCode;
    }

    return (expectedOut == SIZE_MAX) ? !out.empty() : (out.size() == expectedOut);
}

// ----------------------------------------------------------------------------
// LHA / ARJ / ZOO LH5 MSB-First Bit Reader & Block-Adaptive Canonical Huffman
// ----------------------------------------------------------------------------
struct LhaMsbBitReader {
    const uint8_t* src = nullptr;
    size_t srcLen = 0;
    size_t bytePos = 0;
    uint64_t bitBuf = 0;
    int bitCount = 0;
    size_t totalBitsDropped = 0;
    bool overread = false;

    LhaMsbBitReader(const uint8_t* s, size_t n) : src(s), srcLen(n) {}

    void Fill() {
        while (bitCount <= 56) {
            uint8_t b = (bytePos < srcLen) ? src[bytePos++] : 0u;
            bitBuf |= static_cast<uint64_t>(b) << (56 - bitCount);
            bitCount += 8;
        }
    }

    uint32_t PeekBits(uint32_t n) {
        if (n == 0u) return 0u;
        if (bitCount < static_cast<int>(n)) Fill();
        return static_cast<uint32_t>(bitBuf >> (64u - n));
    }

    void DropBits(uint32_t n) {
        if (n == 0u) return;
        if (bitCount < static_cast<int>(n)) Fill();
        bitBuf <<= n;
        bitCount -= static_cast<int>(n);
        totalBitsDropped += n;
        if (totalBitsDropped > srcLen * 8u) {
            overread = true;
        }
    }

    uint32_t ReadBits(uint32_t n) {
        if (n == 0u) return 0u;
        uint32_t v = PeekBits(n);
        DropBits(n);
        return v;
    }
};

struct LhaMsbHuffTable {
    bool isSingle = false;
    uint16_t singleSym = 0;
    uint32_t maxLen = 0;
    std::vector<uint32_t> table;

    void SetSingle(uint16_t sym) {
        isSingle = true;
        singleSym = sym;
        maxLen = 0;
        table.clear();
    }

    bool Build(const uint8_t* lengths, size_t alphaSz) {
        uint32_t cnt[17] = {};
        uint32_t maxL = 0;
        for (size_t s = 0; s < alphaSz; ++s) {
            uint8_t l = lengths[s];
            if (l > 16u) return false;
            if (l > 0u) {
                ++cnt[l];
                if (l > maxL) maxL = l;
            }
        }
        if (maxL == 0u) return false;

        uint32_t weightSum = 0;
        for (uint32_t l = 1u; l <= 16u; ++l) {
            weightSum += cnt[l] << (16u - l);
        }
        if (weightSum == 0u || weightSum > 65536u) return false;

        isSingle = false;
        maxLen = maxL;
        table.assign(size_t(1) << maxL, 0xFFFFFFFFu);

        uint32_t firstCode[17] = {};
        uint32_t code = 0;
        for (uint32_t l = 1u; l <= maxL; ++l) {
            code = (code + cnt[l - 1u]) << 1u;
            firstCode[l] = code;
        }

        for (size_t s = 0; s < alphaSz; ++s) {
            uint32_t l = lengths[s];
            if (l == 0u) continue;
            uint32_t c = firstCode[l]++;
            uint32_t shift = maxL - l;
            size_t startIdx = static_cast<size_t>(c) << shift;
            size_t span = size_t(1) << shift;
            if (startIdx + span > table.size()) return false;
            uint32_t packed = static_cast<uint32_t>(s) | (l << 16u);
            for (size_t k = 0; k < span; ++k) {
                table[startIdx + k] = packed;
            }
        }
        return true;
    }

    bool Decode(LhaMsbBitReader& br, uint16_t& outSym) const {
        if (isSingle) {
            if (br.overread) return false;
            outSym = singleSym;
            return true;
        }
        if (maxLen == 0u || table.empty()) return false;
        uint32_t entry = table[br.PeekBits(maxLen)];
        if (entry == 0xFFFFFFFFu) return false;
        br.DropBits(entry >> 16u);
        if (br.overread) return false;
        outSym = static_cast<uint16_t>(entry & 0xFFFFu);
        return true;
    }
};

inline bool ReadLhaPtLen(LhaMsbBitReader& br, uint32_t nn, uint32_t nbit, int iSpecial, LhaMsbHuffTable& outTbl) {
    uint32_t n = br.ReadBits(nbit);
    if (br.overread) return false;
    if (n == 0u) {
        uint32_t c = br.ReadBits(nbit);
        if (br.overread || c >= nn) return false;
        outTbl.SetSingle(static_cast<uint16_t>(c));
        return true;
    }
    if (n > nn || nn > 32u) return false;

    uint8_t ptLen[32] = {};
    uint32_t i = 0;
    while (i < n) {
        uint32_t c = br.PeekBits(3u);
        if (c == 7u) {
            uint32_t ext = br.PeekBits(16u);
            uint32_t mask = 1u << 12u;
            while ((ext & mask) != 0u) {
                ++c;
                mask >>= 1u;
                if (c > 16u) return false;
            }
            br.DropBits(c - 3u);
        } else {
            br.DropBits(3u);
        }
        if (br.overread) return false;
        ptLen[i++] = static_cast<uint8_t>(c);
        if (static_cast<int>(i) == iSpecial) {
            uint32_t skip = br.ReadBits(2u);
            if (br.overread) return false;
            while (skip-- > 0u && i < nn) {
                ptLen[i++] = 0u;
            }
        }
    }
    return outTbl.Build(ptLen, nn);
}

inline bool ReadLhaCLen(LhaMsbBitReader& br, const LhaMsbHuffTable& ptTbl, LhaMsbHuffTable& outCTbl) {
    constexpr uint32_t kNC = 510u;
    uint32_t n = br.ReadBits(9u);
    if (br.overread) return false;
    if (n == 0u) {
        uint32_t c = br.ReadBits(9u);
        if (br.overread || c >= kNC) return false;
        outCTbl.SetSingle(static_cast<uint16_t>(c));
        return true;
    }
    if (n > kNC) return false;

    uint8_t cLen[kNC] = {};
    uint32_t i = 0;
    while (i < n) {
        uint16_t c = 0;
        if (!ptTbl.Decode(br, c)) return false;
        if (c <= 2u) {
            uint32_t run = 1u;
            if (c == 1u) run = br.ReadBits(4u) + 3u;
            else if (c == 2u) run = br.ReadBits(9u) + 20u;
            if (br.overread) return false;
            while (run-- > 0u && i < kNC) {
                cLen[i++] = 0u;
            }
        } else {
            cLen[i++] = static_cast<uint8_t>(c - 2u);
        }
    }
    return outCTbl.Build(cLen, kNC);
}

inline bool DecodeLhaArjBlockHuff(
    const uint8_t* src,
    size_t compSize,
    uint64_t uncompSize,
    uint32_t np,
    uint32_t pbit,
    std::vector<uint8_t>& outBytes,
    uint8_t initFillByte = 0x20u)
{
    outBytes.clear();
    if (uncompSize == 0) return true;
    if (!src || compSize == 0 || uncompSize > 256ULL * 1024 * 1024) return false;

    outBytes.reserve(static_cast<size_t>(std::min<uint64_t>(uncompSize, static_cast<uint64_t>(compSize) * 16ULL + 8192ULL)));
    LhaMsbBitReader br(src, compSize);
    LhaMsbHuffTable ptTbl, cTbl, pTbl;
    uint32_t blockRemaining = 0;

    while (outBytes.size() < uncompSize) {
        if (blockRemaining == 0u) {
            blockRemaining = br.ReadBits(16u);
            if (br.overread || blockRemaining == 0u) {
                outBytes.clear();
                return false;
            }
            if (!ReadLhaPtLen(br, 19u, 5u, 3, ptTbl) ||
                !ReadLhaCLen(br, ptTbl, cTbl) ||
                !ReadLhaPtLen(br, np, pbit, -1, pTbl))
            {
                outBytes.clear();
                return false;
            }
        }
        --blockRemaining;

        uint16_t sym = 0;
        if (!cTbl.Decode(br, sym)) {
            outBytes.clear();
            return false;
        }
        if (sym < 256u) {
            outBytes.push_back(static_cast<uint8_t>(sym));
        } else {
            const size_t matchLen = static_cast<size_t>(sym - 256u) + 3u;
            uint16_t p = 0;
            if (!pTbl.Decode(br, p) || p > 16u) {
                outBytes.clear();
                return false;
            }
            uint32_t dist0 = 0;
            if (p > 0u) {
                dist0 = (1u << (p - 1u)) | br.ReadBits(p - 1u);
                if (br.overread) {
                    outBytes.clear();
                    return false;
                }
            }
            const size_t dist = static_cast<size_t>(dist0) + 1u;
            for (size_t k = 0; k < matchLen && outBytes.size() < uncompSize; ++k) {
                const uint8_t ch = (outBytes.size() < dist) ? initFillByte : outBytes[outBytes.size() - dist];
                outBytes.push_back(ch);
            }
        }
    }
    return outBytes.size() == uncompSize;
}

} // namespace Explorer2::Formats::DetailUnixAndLegacyArc
