#pragma once
// BrotliDecode.h — Zero-dependency RFC 7932 Brotli stream decompressor.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Explorer2::Brotli {

// Implemented in StreamCodecs.cpp (RFC 7932 Appendix A & B static dictionary + Section 8 transformations).
bool ResolveStaticDictionaryWord(size_t copyLen, size_t dictIndex, uint8_t outBuf[64], size_t& outLen);

struct BitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t bitPos = 0;
    bool eof = false;

    BitReader(const uint8_t* d, size_t s) : data(d), size(s) {}

    uint32_t PeekBits(uint32_t n) const {
        if (n == 0u) return 0u;
        uint64_t acc = 0;
        size_t byteIdx = bitPos >> 3;
        uint32_t bitOff = static_cast<uint32_t>(bitPos & 7u);
        for (uint32_t k = 0; k < 5u; ++k) {
            uint64_t b = (byteIdx + k < size) ? data[byteIdx + k] : 0ull;
            acc |= (b << (k * 8u));
        }
        uint64_t mask = (n >= 32u) ? 0xFFFFFFFFull : ((1ull << n) - 1ull);
        return static_cast<uint32_t>((acc >> bitOff) & mask);
    }

    void DropBits(uint32_t n) {
        bitPos += n;
        if (bitPos > size * 8u) {
            eof = true;
        }
    }

    uint32_t ReadBits(uint32_t n) {
        if (n == 0u) return 0u;
        uint32_t v = PeekBits(n);
        DropBits(n);
        return v;
    }

    bool AlignToByteZero() {
        while ((bitPos & 7u) != 0u) {
            if (ReadBits(1u) != 0u) return false;
        }
        return !eof;
    }
};

inline uint32_t CeilLog2(uint32_t v) {
    if (v <= 1u) return 0u;
    uint32_t r = 0u;
    uint32_t x = v - 1u;
    while (x > 0u) {
        x >>= 1u;
        ++r;
    }
    return r;
}

inline uint32_t ReverseBits16(uint32_t v, uint32_t len) {
    uint32_t r = 0u;
    for (uint32_t i = 0; i < len; ++i) {
        r = (r << 1u) | ((v >> i) & 1u);
    }
    return r;
}

struct PrefixTree {
    uint32_t maxLen = 0;
    uint32_t singleSym = 0;
    bool isSingle = false;
    std::vector<uint32_t> table;

    bool BuildFromLengths(const uint8_t* lengths, uint32_t alphabetSize) {
        uint32_t count[16] = {};
        uint32_t maxL = 0u;
        uint32_t nonZeroCount = 0u;
        uint32_t lastSym = 0u;
        for (uint32_t s = 0; s < alphabetSize; ++s) {
            uint8_t l = lengths[s];
            if (l > 15u) return false;
            if (l > 0u) {
                ++count[l];
                if (l > maxL) maxL = l;
                ++nonZeroCount;
                lastSym = s;
            }
        }
        if (nonZeroCount == 0u) return false;
        if (nonZeroCount == 1u) {
            isSingle = true;
            singleSym = lastSym;
            maxLen = 0u;
            table.clear();
            return true;
        }
        uint32_t kraft = 0u;
        for (uint32_t l = 1u; l <= 15u; ++l) {
            kraft += count[l] << (15u - l);
        }
        if (kraft != 32768u) return false;

        isSingle = false;
        maxLen = maxL;
        uint32_t nextCode[16] = {};
        uint32_t code = 0u;
        for (uint32_t l = 1u; l <= maxL; ++l) {
            code = (code + count[l - 1u]) << 1u;
            nextCode[l] = code;
        }
        const uint32_t tabSize = 1u << maxL;
        table.assign(tabSize, 0u);
        for (uint32_t s = 0; s < alphabetSize; ++s) {
            uint8_t l = lengths[s];
            if (l == 0u) continue;
            uint32_t c = nextCode[l]++;
            uint32_t rev = ReverseBits16(c, l);
            uint32_t entry = (static_cast<uint32_t>(l) << 16u) | s;
            uint32_t step = 1u << l;
            for (uint32_t idx = rev; idx < tabSize; idx += step) {
                table[idx] = entry;
            }
        }
        return true;
    }

    bool Decode(BitReader& br, uint32_t& outSym) const {
        if (isSingle) {
            outSym = singleSym;
            return !br.eof;
        }
        if (table.empty()) return false;
        uint32_t peek = br.PeekBits(maxLen);
        uint32_t entry = table[peek];
        uint32_t len = entry >> 16u;
        if (len == 0u) return false;
        br.DropBits(len);
        if (br.eof) return false;
        outSym = entry & 0xFFFFu;
        return true;
    }
};

inline bool ReadPrefixCode(BitReader& br, uint32_t alphabetSize, PrefixTree& outTree) {
    if (alphabetSize == 0u || alphabetSize > 4096u) return false;
    if (alphabetSize == 1u) {
        outTree.isSingle = true;
        outTree.singleSym = 0u;
        outTree.maxLen = 0u;
        outTree.table.clear();
        return true;
    }

    uint32_t hskip = br.ReadBits(2u);
    if (br.eof) return false;

    if (hskip == 1u) {
        uint32_t nsym = br.ReadBits(2u) + 1u;
        uint32_t maxBits = CeilLog2(alphabetSize);
        uint32_t syms[4] = {};
        for (uint32_t i = 0; i < nsym; ++i) {
            syms[i] = br.ReadBits(maxBits);
            if (br.eof || syms[i] >= alphabetSize) return false;
            for (uint32_t j = 0; j < i; ++j) {
                if (syms[j] == syms[i]) return false;
            }
        }
        if (nsym == 1u) {
            outTree.isSingle = true;
            outTree.singleSym = syms[0];
            outTree.maxLen = 0u;
            outTree.table.clear();
            return true;
        }
        std::vector<uint8_t> lengths(alphabetSize, 0u);
        if (nsym == 2u) {
            lengths[syms[0]] = 1u;
            lengths[syms[1]] = 1u;
        } else if (nsym == 3u) {
            outTree.isSingle = false;
            outTree.maxLen = 2u;
            outTree.table.resize(4u);
            if (syms[1] > syms[2]) std::swap(syms[1], syms[2]);
            outTree.table[0] = (1u << 16u) | syms[0];
            outTree.table[2] = (1u << 16u) | syms[0];
            outTree.table[1] = (2u << 16u) | syms[1];
            outTree.table[3] = (2u << 16u) | syms[2];
            return true;
        } else {
            uint32_t treeSelect = br.ReadBits(1u);
            if (br.eof) return false;
            if (treeSelect == 0u) {
                for (uint32_t i = 0; i < 4u; ++i) lengths[syms[i]] = 2u;
            } else {
                outTree.isSingle = false;
                outTree.maxLen = 3u;
                outTree.table.resize(8u);
                if (syms[2] > syms[3]) std::swap(syms[2], syms[3]);
                for (uint32_t k = 0; k < 8u; k += 2u) outTree.table[k] = (1u << 16u) | syms[0];
                outTree.table[1] = (2u << 16u) | syms[1];
                outTree.table[5] = (2u << 16u) | syms[1];
                outTree.table[3] = (3u << 16u) | syms[2];
                outTree.table[7] = (3u << 16u) | syms[3];
                return true;
            }
        }
        return outTree.BuildFromLengths(lengths.data(), alphabetSize);
    }

    static constexpr uint8_t kCodeLenOrder[18] = {
        1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    uint8_t clLengths[18] = {};
    uint32_t space = 32u;
    uint32_t numCodes = 0u;
    for (uint32_t i = hskip; i < 18u && space > 0u; ++i) {
        uint32_t p4 = br.PeekBits(4u);
        uint32_t v = 0u, drop = 2u;
        if ((p4 & 3u) == 0u)      { v = 0u; drop = 2u; }
        else if ((p4 & 3u) == 2u) { v = 3u; drop = 2u; }
        else if ((p4 & 3u) == 1u) { v = 4u; drop = 2u; }
        else if ((p4 & 7u) == 3u) { v = 2u; drop = 3u; }
        else if (p4 == 7u)        { v = 1u; drop = 4u; }
        else                      { v = 5u; drop = 4u; }
        br.DropBits(drop);
        if (br.eof) return false;
        uint8_t sym = kCodeLenOrder[i];
        clLengths[sym] = static_cast<uint8_t>(v);
        if (v != 0u) {
            uint32_t cost = 32u >> v;
            if (cost > space) return false;
            space -= cost;
            ++numCodes;
        }
    }
    if (numCodes != 1u && space != 0u) return false;

    PrefixTree clTree;
    if (numCodes == 1u) {
        for (uint32_t s = 0; s < 18u; ++s) {
            if (clLengths[s] != 0u) {
                clTree.isSingle = true;
                clTree.singleSym = s;
                break;
            }
        }
    } else if (!clTree.BuildFromLengths(clLengths, 18u)) {
        return false;
    }

    std::vector<uint8_t> lengths(alphabetSize, 0u);
    uint32_t symIdx = 0u;
    uint8_t prevCodeLen = 8u;
    uint32_t repeat = 0u;
    uint8_t repeatLen = 0u;
    uint32_t mainSpace = 32768u;

    while (symIdx < alphabetSize && mainSpace > 0u) {
        uint32_t codeLenSym = 0u;
        if (!clTree.Decode(br, codeLenSym)) return false;
        if (codeLenSym < 16u) {
            repeat = 0u;
            lengths[symIdx++] = static_cast<uint8_t>(codeLenSym);
            if (codeLenSym != 0u) {
                prevCodeLen = static_cast<uint8_t>(codeLenSym);
                uint32_t cost = 32768u >> codeLenSym;
                if (cost > mainSpace) return false;
                mainSpace -= cost;
            }
        } else {
            uint32_t extraBits = (codeLenSym == 16u) ? 2u : 3u;
            uint8_t newLen = (codeLenSym == 16u) ? prevCodeLen : 0u;
            if (repeatLen != newLen) {
                repeat = 0u;
                repeatLen = newLen;
            }
            uint32_t oldRepeat = repeat;
            if (repeat > 0u) {
                repeat = (repeat - 2u) << extraBits;
            }
            repeat += br.ReadBits(extraBits) + 3u;
            if (br.eof || repeat <= oldRepeat) return false;
            uint32_t delta = repeat - oldRepeat;
            if (symIdx + delta > alphabetSize) return false;
            for (uint32_t k = 0; k < delta; ++k) {
                lengths[symIdx++] = repeatLen;
            }
            if (repeatLen != 0u) {
                uint64_t cost = static_cast<uint64_t>(delta) * (32768ull >> repeatLen);
                if (cost > mainSpace) return false;
                mainSpace -= static_cast<uint32_t>(cost);
            }
        }
    }
    if (mainSpace != 0u) return false;
    return outTree.BuildFromLengths(lengths.data(), alphabetSize);
}

inline uint32_t ReadVarLenUint8(BitReader& br) {
    if (br.ReadBits(1u) == 0u) return 0u;
    uint32_t n = br.ReadBits(3u);
    if (n == 0u) return 1u;
    return (1u << n) + br.ReadBits(n);
}

inline uint32_t DecodeBlockLength(BitReader& br, const PrefixTree& tree) {
    static constexpr uint32_t kBlockLenBase[26] = {
        1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113,
        145, 177, 209, 241, 305, 369, 497, 753, 1265, 2289, 4337, 8433, 16625
    };
    static constexpr uint8_t kBlockLenExtra[26] = {
        2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5,
        5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24
    };
    uint32_t code = 0u;
    if (!tree.Decode(br, code) || code >= 26u) return 0u;
    return kBlockLenBase[code] + br.ReadBits(kBlockLenExtra[code]);
}

inline bool ReadContextMap(BitReader& br, uint32_t mapSize, uint32_t& outNumTrees, std::vector<uint8_t>& outMap) {
    outNumTrees = ReadVarLenUint8(br) + 1u;
    if (br.eof || outNumTrees > 256u) return false;
    outMap.assign(mapSize, 0u);
    if (outNumTrees == 1u) return true;

    bool useRle = br.ReadBits(1u) != 0u;
    uint32_t rleMax = useRle ? (br.ReadBits(4u) + 1u) : 0u;
    if (br.eof) return false;

    PrefixTree cmTree;
    if (!ReadPrefixCode(br, outNumTrees + rleMax, cmTree)) return false;

    uint32_t i = 0u;
    while (i < mapSize) {
        uint32_t code = 0u;
        if (!cmTree.Decode(br, code)) return false;
        if (code == 0u) {
            outMap[i++] = 0u;
        } else if (code <= rleMax) {
            uint32_t reps = (1u << code) + br.ReadBits(code);
            if (br.eof || i + reps > mapSize) return false;
            for (uint32_t k = 0; k < reps; ++k) outMap[i++] = 0u;
        } else {
            outMap[i++] = static_cast<uint8_t>(code - rleMax);
        }
    }

    if (br.ReadBits(1u) != 0u) {
        uint8_t mtf[256];
        for (uint32_t k = 0; k < 256u; ++k) mtf[k] = static_cast<uint8_t>(k);
        for (uint32_t k = 0; k < mapSize; ++k) {
            uint8_t idx = outMap[k];
            uint8_t val = mtf[idx];
            outMap[k] = val;
            for (uint8_t m = idx; m > 0u; --m) mtf[m] = mtf[m - 1u];
            mtf[0] = val;
        }
    }
    return !br.eof;
}

inline uint8_t LiteralContext(uint8_t p1, uint8_t p2, uint8_t mode) {
    if (mode == 0u) return p1 & 0x3Fu;
    if (mode == 1u) return p1 >> 2u;
    static constexpr uint8_t kLut0[256] = {
        0,0,0,0,0,0,0,0,0,4,4,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        8,12,16,12,12,20,12,16,24,28,12,12,32,12,36,12,44,44,44,44,44,44,44,44,44,44,32,32,24,40,28,12,
        12,48,52,52,52,48,52,52,52,48,52,52,52,52,52,48,52,52,52,52,52,48,52,52,52,52,52,24,12,28,12,12,
        12,56,60,60,60,56,60,60,60,56,60,60,60,60,60,56,60,60,60,60,60,56,60,60,60,60,60,24,12,28,12,0,
        0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,
        0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,
        2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,
        2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3
    };
    static constexpr uint8_t kLut1[256] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,
        1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,
        1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,1,1,1,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2
    };
    static constexpr uint8_t kLut2[256] = {
        0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7
    };
    if (mode == 2u) return static_cast<uint8_t>(kLut0[p1] | kLut1[p2]);
    return static_cast<uint8_t>((kLut2[p1] << 3u) | kLut2[p2]);
}

inline bool Decompress(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out,
                       size_t maxOutSize = 128 * 1024 * 1024, bool requireExactEnd = false) {
    out.clear();
    if (!src || srcSize == 0u || maxOutSize == 0u) return false;
    BitReader br(src, srcSize);

    uint32_t wbits = 16u;
    if (br.ReadBits(1u) != 0u) {
        uint32_t n = br.ReadBits(3u);
        if (n != 0u) {
            wbits = 17u + n;
        } else {
            n = br.ReadBits(3u);
            if (n == 1u) return false;
            wbits = (n != 0u) ? (8u + n) : 17u;
        }
    }
    if (br.eof) return false;
    const size_t maxBackDist = (size_t(1u) << wbits) - 16u;

    int32_t distRing[4] = { 16, 15, 11, 4 };
    uint32_t distRingIdx = 0u;
    uint8_t prevByte1 = 0u, prevByte2 = 0u;

    static constexpr uint32_t kInsBase[24] = {
        0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98,
        130, 194, 322, 578, 1090, 2114, 6210, 22594
    };
    static constexpr uint8_t kInsExtra[24] = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
        6, 7, 8, 9, 10, 12, 14, 24
    };
    static constexpr uint32_t kCopyBase[24] = {
        2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38, 54,
        70, 102, 134, 198, 326, 582, 1094, 2118
    };
    static constexpr uint8_t kCopyExtra[24] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4,
        5, 5, 6, 7, 8, 9, 10, 24
    };
    static constexpr uint8_t kRangeIns[11]  = { 0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16 };
    static constexpr uint8_t kRangeCopy[11] = { 0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16 };

    bool isLast = false;
    bool sawEmptyLast = false;
    while (!isLast) {
        isLast = br.ReadBits(1u) != 0u;
        if (br.eof) return false;
        if (isLast) {
            bool isLastEmpty = br.ReadBits(1u) != 0u;
            if (br.eof) return false;
            if (isLastEmpty) {
                sawEmptyLast = true;
                break;
            }
        }

        uint32_t mnibblesCode = br.ReadBits(2u);
        if (br.eof) return false;
        if (mnibblesCode == 3u) {
            if (isLast) return false;
            if (br.ReadBits(1u) != 0u) return false;
            uint32_t mskipBytes = br.ReadBits(2u);
            uint32_t metaLen = 0u;
            if (mskipBytes > 0u) {
                for (uint32_t i = 0; i < mskipBytes; ++i) {
                    uint32_t b = br.ReadBits(8u);
                    if (i + 1u == mskipBytes && mskipBytes > 1u && b == 0u) return false;
                    metaLen |= (b << (i * 8u));
                }
                ++metaLen;
            }
            if (!br.AlignToByteZero()) return false;
            size_t curByte = br.bitPos >> 3;
            if (metaLen > srcSize - curByte) return false;
            br.bitPos += static_cast<size_t>(metaLen) * 8u;
            continue;
        }

        uint32_t mnibbles = mnibblesCode + 4u;
        uint32_t mlenMinus1 = 0u;
        for (uint32_t i = 0; i < mnibbles; ++i) {
            uint32_t nib = br.ReadBits(4u);
            if (i + 1u == mnibbles && mnibbles > 4u && nib == 0u) return false;
            mlenMinus1 |= (nib << (i * 4u));
        }
        if (br.eof) return false;
        size_t mlen = static_cast<size_t>(mlenMinus1) + 1u;
        if (mlen > maxOutSize - out.size()) return false;

        if (!isLast) {
            bool isUncompressed = br.ReadBits(1u) != 0u;
            if (br.eof) return false;
            if (isUncompressed) {
                if (!br.AlignToByteZero()) return false;
                size_t curByte = br.bitPos >> 3;
                if (mlen > srcSize - curByte) return false;
                out.insert(out.end(), src + curByte, src + curByte + mlen);
                if (mlen >= 1u) {
                    prevByte2 = (mlen >= 2u) ? src[curByte + mlen - 2u] : prevByte1;
                    prevByte1 = src[curByte + mlen - 1u];
                }
                br.bitPos += mlen * 8u;
                continue;
            }
        }

        uint32_t nblTypes[3] = { 1u, 1u, 1u };
        PrefixTree blockTypeTrees[3];
        PrefixTree blockLenTrees[3];
        uint32_t blockType[3] = { 0u, 0u, 0u };
        uint32_t prevBlockType[3] = { 1u, 1u, 1u };
        uint32_t blockRem[3] = { 1u << 28u, 1u << 28u, 1u << 28u };

        for (int cat = 0; cat < 3; ++cat) {
            nblTypes[cat] = ReadVarLenUint8(br) + 1u;
            if (br.eof || nblTypes[cat] > 256u) return false;
            if (nblTypes[cat] >= 2u) {
                if (!ReadPrefixCode(br, nblTypes[cat] + 2u, blockTypeTrees[cat]) ||
                    !ReadPrefixCode(br, 26u, blockLenTrees[cat])) {
                    return false;
                }
                blockRem[cat] = DecodeBlockLength(br, blockLenTrees[cat]);
                if (br.eof || blockRem[cat] == 0u) return false;
            }
        }

        uint32_t npostfix = br.ReadBits(2u);
        uint32_t ndirect = br.ReadBits(4u) << npostfix;
        if (br.eof) return false;

        std::vector<uint8_t> ctxModes(nblTypes[0], 0u);
        for (uint32_t i = 0; i < nblTypes[0]; ++i) {
            ctxModes[i] = static_cast<uint8_t>(br.ReadBits(2u));
        }
        if (br.eof) return false;

        uint32_t numLitTrees = 1u, numDistTrees = 1u;
        std::vector<uint8_t> litCtxMap, distCtxMap;
        if (!ReadContextMap(br, nblTypes[0] * 64u, numLitTrees, litCtxMap) ||
            !ReadContextMap(br, nblTypes[2] * 4u, numDistTrees, distCtxMap)) {
            return false;
        }

        std::vector<PrefixTree> litTrees(numLitTrees);
        std::vector<PrefixTree> iacTrees(nblTypes[1]);
        uint32_t distAlpha = 16u + ndirect + (48u << npostfix);
        std::vector<PrefixTree> distTrees(numDistTrees);

        for (uint32_t i = 0; i < numLitTrees; ++i) {
            if (!ReadPrefixCode(br, 256u, litTrees[i])) return false;
        }
        for (uint32_t i = 0; i < nblTypes[1]; ++i) {
            if (!ReadPrefixCode(br, 704u, iacTrees[i])) return false;
        }
        for (uint32_t i = 0; i < numDistTrees; ++i) {
            if (!ReadPrefixCode(br, distAlpha, distTrees[i])) return false;
        }

        auto stepBlockSwitch = [&](int cat) -> bool {
            if (nblTypes[cat] < 2u) return true;
            uint32_t code = 0u;
            if (!blockTypeTrees[cat].Decode(br, code)) return false;
            uint32_t nextType = 0u;
            if (code == 0u) nextType = prevBlockType[cat];
            else if (code == 1u) nextType = (blockType[cat] + 1u) % nblTypes[cat];
            else nextType = code - 2u;
            if (nextType >= nblTypes[cat]) return false;
            prevBlockType[cat] = blockType[cat];
            blockType[cat] = nextType;
            blockRem[cat] = DecodeBlockLength(br, blockLenTrees[cat]);
            return !br.eof && blockRem[cat] > 0u;
        };

        out.reserve(out.size() + mlen);
        size_t produced = 0u;
        while (produced < mlen) {
            if (blockRem[1] == 0u && !stepBlockSwitch(1)) return false;
            --blockRem[1];

            uint32_t iacSym = 0u;
            if (!iacTrees[blockType[1]].Decode(br, iacSym) || iacSym >= 704u) return false;
            uint32_t rangeIdx = iacSym >> 6u;
            uint32_t insCode  = static_cast<uint32_t>(kRangeIns[rangeIdx]) + ((iacSym >> 3u) & 7u);
            uint32_t copyCode = static_cast<uint32_t>(kRangeCopy[rangeIdx]) + (iacSym & 7u);
            bool distZeroFlag = (rangeIdx < 2u);

            size_t insLen  = kInsBase[insCode]  + br.ReadBits(kInsExtra[insCode]);
            size_t copyLen = kCopyBase[copyCode] + br.ReadBits(kCopyExtra[copyCode]);
            if (br.eof || insLen > mlen - produced) return false;

            for (size_t k = 0; k < insLen; ++k) {
                if (blockRem[0] == 0u && !stepBlockSwitch(0)) return false;
                --blockRem[0];
                uint8_t cMode = ctxModes[blockType[0]];
                uint8_t lCtx = LiteralContext(prevByte1, prevByte2, cMode);
                uint8_t treeIdx = litCtxMap[blockType[0] * 64u + lCtx];
                uint32_t lit = 0u;
                if (treeIdx >= numLitTrees || !litTrees[treeIdx].Decode(br, lit) || lit > 255u) return false;
                uint8_t b = static_cast<uint8_t>(lit);
                out.push_back(b);
                prevByte2 = prevByte1;
                prevByte1 = b;
            }
            produced += insLen;
            if (produced == mlen) break;

            int32_t dist = 0;
            bool recordInRing = false;
            if (distZeroFlag) {
                dist = distRing[(distRingIdx - 1u) & 3u];
            } else {
                if (blockRem[2] == 0u && !stepBlockSwitch(2)) return false;
                --blockRem[2];
                uint32_t dCtx = (copyLen > 4u) ? 3u : static_cast<uint32_t>(copyLen - 2u);
                uint8_t dTreeIdx = distCtxMap[blockType[2] * 4u + dCtx];
                uint32_t dSym = 0u;
                if (dTreeIdx >= numDistTrees || !distTrees[dTreeIdx].Decode(br, dSym)) return false;

                if (dSym < 16u) {
                    static constexpr uint8_t kRingBack[16]  = { 1, 2, 3, 4, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2 };
                    static constexpr int8_t  kRingDelta[16] = { 0, 0, 0, 0,-1, 1,-2, 2,-3, 3,-1, 1,-2, 2,-3, 3 };
                    dist = distRing[(distRingIdx - kRingBack[dSym]) & 3u] + kRingDelta[dSym];
                    recordInRing = (dSym > 0u);
                } else if (dSym < 16u + ndirect) {
                    dist = static_cast<int32_t>(dSym - 15u);
                    recordInRing = true;
                } else {
                    uint32_t dRem = dSym - 16u - ndirect;
                    uint32_t postfix = dRem & ((1u << npostfix) - 1u);
                    uint32_t hcode = dRem >> npostfix;
                    uint32_t ndistbits = 1u + (hcode >> 1u);
                    uint32_t offset = ((2u + (hcode & 1u)) << ndistbits) - 4u;
                    uint32_t dextra = br.ReadBits(ndistbits);
                    if (br.eof) return false;
                    uint64_t fullDist = ((static_cast<uint64_t>(offset) + dextra) << npostfix) + postfix + ndirect + 1ull;
                    if (fullDist > 0x7FFFFFFFull) return false;
                    dist = static_cast<int32_t>(fullDist);
                    recordInRing = true;
                }
            }
            if (dist <= 0) return false;
            size_t uDist = static_cast<size_t>(dist);
            size_t maxAllowed = std::min(out.size(), maxBackDist);
            if (uDist > maxAllowed) {
                uint8_t dictWord[64];
                size_t dictLen = 0u;
                if (!ResolveStaticDictionaryWord(copyLen, uDist - maxAllowed - 1u, dictWord, dictLen)) {
                    return false;
                }
                if (dictLen > mlen - produced) return false;
                for (size_t k = 0; k < dictLen; ++k) {
                    uint8_t b = dictWord[k];
                    out.push_back(b);
                    prevByte2 = prevByte1;
                    prevByte1 = b;
                }
                produced += dictLen;
                continue;
            }
            if (copyLen > mlen - produced) return false;
            if (recordInRing) {
                distRing[distRingIdx & 3u] = dist;
                ++distRingIdx;
            }

            for (size_t k = 0; k < copyLen; ++k) {
                uint8_t b = out[out.size() - uDist];
                out.push_back(b);
                prevByte2 = prevByte1;
                prevByte1 = b;
            }
            produced += copyLen;
        }
    }

    if (!br.AlignToByteZero()) return false;
    if (requireExactEnd && (br.bitPos >> 3) != srcSize) return false;
    return !out.empty() || sawEmptyLast;
}

} // namespace Explorer2::Brotli
