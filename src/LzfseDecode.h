#pragma once
// LzfseDecode.h — Pure-C++ Apple LZFSE (bvx1, bvx2, bvxn, bvx$) and LZVN (bvx-) stream decompressor.
// Used by Apple UDIF disk images (.dmg block type 0x80000007) and Procreate 5+ tile chunks (.procreate).
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Explorer2::Lzfse {

namespace Detail {

inline uint16_t Rd16LE(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t Rd32LE(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t Rd64LE(const uint8_t* p) noexcept {
    return static_cast<uint64_t>(Rd32LE(p)) | (static_cast<uint64_t>(Rd32LE(p + 4)) << 32);
}

inline int Clz32(uint32_t x) noexcept {
    if (x == 0u) return 32;
    int n = 0;
    if ((x & 0xFFFF0000u) == 0u) { n += 16; x <<= 16; }
    if ((x & 0xFF000000u) == 0u) { n += 8;  x <<= 8;  }
    if ((x & 0xF0000000u) == 0u) { n += 4;  x <<= 4;  }
    if ((x & 0xC0000000u) == 0u) { n += 2;  x <<= 2;  }
    if ((x & 0x80000000u) == 0u) { n += 1; }
    return n;
}

constexpr uint32_t kMagicBvx1 = 0x31787662u; // "bvx1" - uncompressed FSE header
constexpr uint32_t kMagicBvx2 = 0x32787662u; // "bvx2" - bit-packed FSE header
constexpr uint32_t kMagicBvxn = 0x6E787662u; // "bvxn" - raw uncompressed block
constexpr uint32_t kMagicLzvn = 0x2D787662u; // "bvx-" - LZVN compressed block
constexpr uint32_t kMagicEnd  = 0x24787662u; // "bvx$" - end of stream

constexpr size_t kNumLSymbols   = 20;
constexpr size_t kNumMSymbols   = 20;
constexpr size_t kNumDSymbols   = 64;
constexpr size_t kNumLitSymbols = 256;

constexpr uint32_t kLStates   = 64;
constexpr uint32_t kMStates   = 64;
constexpr uint32_t kDStates   = 256;
constexpr uint32_t kLitStates = 1024;

constexpr uint8_t kLExtraBits[kNumLSymbols] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 2, 3, 5, 8
};
constexpr int32_t kLBaseValue[kNumLSymbols] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 16, 20, 28, 60
};

constexpr uint8_t kMExtraBits[kNumMSymbols] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 3, 5, 8, 11
};
constexpr int32_t kMBaseValue[kNumMSymbols] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 16, 24, 56, 312
};

constexpr uint8_t kDExtraBits[kNumDSymbols] = {
    0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3,
    4,4,4,4, 5,5,5,5, 6,6,6,6, 7,7,7,7,
    8,8,8,8, 9,9,9,9, 10,10,10,10, 11,11,11,11,
    12,12,12,12, 13,13,13,13, 14,14,14,14, 15,15,15,15
};

constexpr std::array<int32_t, kNumDSymbols> BuildDBaseTable() noexcept {
    std::array<int32_t, kNumDSymbols> t{};
    int32_t cur = 0;
    for (size_t i = 0; i < kNumDSymbols; ++i) {
        t[i] = cur;
        cur += (1 << kDExtraBits[i]);
    }
    return t;
}
constexpr auto kDBaseValue = BuildDBaseTable();

struct FseLitEntry {
    uint8_t  k = 0;
    uint8_t  symbol = 0;
    int16_t  delta = 0;
};

struct FseValueEntry {
    uint8_t  totalBits = 0;
    uint8_t  valueBits = 0;
    int16_t  delta = 0;
    int32_t  vbase = 0;
};

inline bool InitFseLitDecoderTable(
    uint32_t nStates,
    size_t nSymbols,
    const uint16_t* freq,
    FseLitEntry* outTable) noexcept
{
    uint32_t sum = 0;
    for (size_t i = 0; i < nSymbols; ++i) sum += freq[i];
    if (sum == 0u) {
        // An empty literal stream has all zero frequencies
        std::fill_n(outTable, nStates, FseLitEntry{});
        return true;
    }
    if (sum != nStates) return false;

    uint32_t idx = 0;
    for (size_t i = 0; i < nSymbols; ++i) {
        const uint32_t f = freq[i];
        if (f == 0u) continue;
        const int k = Clz32(f) - (32 - static_cast<int>(Clz32(nStates) == 0 ? 0 : (31 - Clz32(nStates))));
        // Note: nStates is always a power of two (64, 256, 1024), so log2(nStates) = 31 - Clz32(nStates).
        const int nbits = (31 - Clz32(nStates)) - (31 - Clz32(f));
        const int32_t j0 = static_cast<int32_t>((2u * f) - (nStates >> nbits));
        for (uint32_t j = 0; j < f; ++j) {
            if (idx >= nStates) return false;
            FseLitEntry e{};
            e.symbol = static_cast<uint8_t>(i);
            if (static_cast<int32_t>(j) < j0) {
                e.k = static_cast<uint8_t>(nbits);
                e.delta = static_cast<int16_t>(((f + j) << nbits) - nStates);
            } else {
                e.k = static_cast<uint8_t>(nbits - 1);
                e.delta = static_cast<int16_t>((j - static_cast<uint32_t>(j0)) << (nbits - 1));
            }
            outTable[idx++] = e;
        }
        (void)k;
    }
    return idx == nStates;
}

inline bool InitFseValueDecoderTable(
    uint32_t nStates,
    size_t nSymbols,
    const uint16_t* freq,
    const uint8_t* vbitsTable,
    const int32_t* vbaseTable,
    FseValueEntry* outTable) noexcept
{
    uint32_t sum = 0;
    for (size_t i = 0; i < nSymbols; ++i) sum += freq[i];
    if (sum == 0u) {
        std::fill_n(outTable, nStates, FseValueEntry{});
        return true;
    }
    if (sum != nStates) return false;

    const int logStates = 31 - Clz32(nStates);
    uint32_t idx = 0;
    for (size_t i = 0; i < nSymbols; ++i) {
        const uint32_t f = freq[i];
        if (f == 0u) continue;
        const int k = logStates - (31 - Clz32(f));
        const int32_t j0 = static_cast<int32_t>((2u * f) - (nStates >> k));
        const uint8_t vb = vbitsTable[i];
        const int32_t vbase = vbaseTable[i];
        for (uint32_t j = 0; j < f; ++j) {
            if (idx >= nStates) return false;
            FseValueEntry e{};
            e.valueBits = vb;
            e.vbase = vbase;
            if (static_cast<int32_t>(j) < j0) {
                e.totalBits = static_cast<uint8_t>(k + vb);
                e.delta = static_cast<int16_t>(((f + j) << k) - nStates);
            } else {
                e.totalBits = static_cast<uint8_t>((k - 1) + vb);
                e.delta = static_cast<int16_t>((j - static_cast<uint32_t>(j0)) << (k - 1));
            }
            outTable[idx++] = e;
        }
    }
    return idx == nStates;
}

// Backward bit-stream reader matching Apple LZFSE's fse_in_stream64.
struct FseBackwardStream {
    uint64_t accum = 0;
    int      accumBits = 0; // valid bits in MSB of accum (32..64 when refilled)
    const uint8_t* bufStart = nullptr;
    const uint8_t* ptr = nullptr; // points to next byte boundary moving backward toward bufStart

    bool Init(const uint8_t* buf, size_t byteCount, int nbitsInitial) noexcept {
        bufStart = buf;
        if (nbitsInitial < -7 || nbitsInitial > 0) return false;
        if (byteCount == 0) {
            if (nbitsInitial != 0) return false;
            accum = 0;
            accumBits = 64;
            ptr = buf;
            return true;
        }
        ptr = buf + byteCount;
        accum = 0;
        accumBits = nbitsInitial + 64;
        if (byteCount >= 8) {
            ptr -= 8;
            accum = Rd64LE(ptr);
        } else {
            uint64_t v = 0;
            for (size_t i = 0; i < byteCount; ++i) {
                v |= static_cast<uint64_t>(buf[i]) << (i * 8);
            }
            const int missingBytes = static_cast<int>(8 - byteCount);
            accum = v << (missingBytes * 8);
            ptr = buf;
            accumBits -= missingBytes * 8;
        }
        if (accumBits < 0 || accumBits > 64) return false;
        if (accumBits < 64) {
            accum <<= (64 - accumBits);
        }
        return true;
    }

    void Flush() noexcept {
        while (accumBits <= 56 && ptr > bufStart) {
            --ptr;
            accum |= (static_cast<uint64_t>(*ptr) << (56 - accumBits));
            accumBits += 8;
        }
    }

    uint32_t Pull(int n) noexcept {
        if (n <= 0) return 0u;
        if (n > accumBits) {
            // Zero-pad past start of buffer cleanly without UB shifts
            const uint32_t res = (accumBits > 0)
                ? static_cast<uint32_t>(accum >> (64 - n))
                : 0u;
            accum = 0;
            accumBits = 0;
            return res;
        }
        const uint32_t res = static_cast<uint32_t>(accum >> (64 - n));
        accum = (n == 64) ? 0ULL : (accum << n);
        accumBits -= n;
        return res;
    }

    uint8_t DecodeLit(uint16_t& state, const FseLitEntry* table) noexcept {
        const FseLitEntry& e = table[state];
        const uint32_t bits = Pull(e.k);
        state = static_cast<uint16_t>(e.delta + static_cast<int32_t>(bits));
        return e.symbol;
    }

    int32_t DecodeValue(uint16_t& state, const FseValueEntry* table) noexcept {
        const FseValueEntry& e = table[state];
        const uint32_t bits = Pull(e.totalBits);
        const int stateBits = static_cast<int>(e.totalBits - e.valueBits);
        const uint32_t sPart = (e.valueBits > 0) ? (bits >> e.valueBits) : bits;
        const uint32_t vPart = (e.valueBits > 0) ? (bits & ((1u << e.valueBits) - 1u)) : 0u;
        (void)stateBits;
        state = static_cast<uint16_t>(e.delta + static_cast<int32_t>(sPart));
        return e.vbase + static_cast<int32_t>(vPart);
    }
};

struct BlockHeaderV1 {
    uint32_t nRawBytes = 0;
    uint32_t nPayloadBytes = 0;
    uint32_t nLiterals = 0;
    uint32_t nMatches = 0;
    uint32_t nLiteralPayloadBytes = 0;
    uint32_t nLmdPayloadBytes = 0;
    int32_t  literalBits = 0;
    uint16_t literalState[4] = {};
    int32_t  lmdBits = 0;
    uint16_t lState = 0;
    uint16_t mState = 0;
    uint16_t dState = 0;
    uint16_t lFreq[kNumLSymbols] = {};
    uint16_t mFreq[kNumMSymbols] = {};
    uint16_t dFreq[kNumDSymbols] = {};
    uint16_t literalFreq[kNumLitSymbols] = {};
};

inline bool ParseBvx1Header(const uint8_t* src, size_t avail, BlockHeaderV1& out, size_t& outHdrBytes) noexcept {
    // Fixed v1 header layout:
    // 11 * 4 bytes (magic + 8 uint32/int32 + 4*uint16 + int32 + 3*uint16? Wait: check exact C struct offsets!)
    // In Apple lzfse_compressed_block_header_v1:
    //   0: uint32 magic
    //   4: uint32 n_raw_bytes
    //   8: uint32 n_payload_bytes
    //  12: uint32 n_literals
    //  16: uint32 n_matches
    //  20: uint32 n_literal_payload_bytes
    //  24: uint32 n_lmd_payload_bytes
    //  28: int32  literal_bits
    //  32: uint16 literal_state[4] (8 bytes: 32..39)
    //  40: int32  lmd_bits
    //  44: uint16 l_state
    //  46: uint16 m_state
    //  48: uint16 d_state
    //  50: uint16 l_freq[20]       (40 bytes: 50..89)
    //  90: uint16 m_freq[20]       (40 bytes: 90..129)
    // 130: uint16 d_freq[64]       (128 bytes: 130..257)
    // 258: uint16 literal_freq[256](512 bytes: 258..769)
    // Total = 770 bytes (with 2 bytes struct tail padding in C -> 772 bytes)!
    constexpr size_t kV1HeaderBytes = 772;
    if (avail < kV1HeaderBytes) return false;
    out.nRawBytes            = Rd32LE(src + 4);
    out.nPayloadBytes        = Rd32LE(src + 8);
    out.nLiterals            = Rd32LE(src + 12);
    out.nMatches             = Rd32LE(src + 16);
    out.nLiteralPayloadBytes = Rd32LE(src + 20);
    out.nLmdPayloadBytes     = Rd32LE(src + 24);
    out.literalBits          = static_cast<int32_t>(Rd32LE(src + 28));
    for (int i = 0; i < 4; ++i) out.literalState[i] = Rd16LE(src + 32 + i * 2);
    out.lmdBits              = static_cast<int32_t>(Rd32LE(src + 40));
    out.lState               = Rd16LE(src + 44);
    out.mState               = Rd16LE(src + 46);
    out.dState               = Rd16LE(src + 48);

    size_t off = 50;
    for (size_t i = 0; i < kNumLSymbols; ++i, off += 2)   out.lFreq[i] = Rd16LE(src + off);
    for (size_t i = 0; i < kNumMSymbols; ++i, off += 2)   out.mFreq[i] = Rd16LE(src + off);
    for (size_t i = 0; i < kNumDSymbols; ++i, off += 2)   out.dFreq[i] = Rd16LE(src + off);
    for (size_t i = 0; i < kNumLitSymbols; ++i, off += 2) out.literalFreq[i] = Rd16LE(src + off);

    outHdrBytes = kV1HeaderBytes;
    return true;
}

inline bool ParseBvx2Header(const uint8_t* src, size_t avail, BlockHeaderV1& out, size_t& outHdrBytes) noexcept {
    if (avail < 32) return false;
    out.nRawBytes = Rd32LE(src + 4);
    const uint64_t v0 = Rd64LE(src + 8);
    const uint64_t v1 = Rd64LE(src + 16);
    const uint64_t v2 = Rd64LE(src + 24);

    out.nLiterals            = static_cast<uint32_t>(v0 & 0xFFFFFu);
    out.nLiteralPayloadBytes = static_cast<uint32_t>((v0 >> 20) & 0xFFFFFu);
    out.nMatches             = static_cast<uint32_t>((v0 >> 40) & 0xFFFFFu);
    out.literalBits          = static_cast<int32_t>((v0 >> 60) & 0x7u) - 7;

    out.literalState[0]      = static_cast<uint16_t>(v1 & 0x3FFu);
    out.literalState[1]      = static_cast<uint16_t>((v1 >> 10) & 0x3FFu);
    out.literalState[2]      = static_cast<uint16_t>((v1 >> 20) & 0x3FFu);
    out.literalState[3]      = static_cast<uint16_t>((v1 >> 30) & 0x3FFu);
    out.nLmdPayloadBytes     = static_cast<uint32_t>((v1 >> 40) & 0xFFFFFu);
    out.lmdBits              = static_cast<int32_t>((v1 >> 60) & 0x7u) - 7;

    const uint32_t headerSize = static_cast<uint32_t>(v2 & 0xFFFFFFFFu);
    out.lState               = static_cast<uint16_t>((v2 >> 32) & 0x3FFu);
    out.mState               = static_cast<uint16_t>((v2 >> 42) & 0x3FFu);
    out.dState               = static_cast<uint16_t>((v2 >> 52) & 0x3FFu);
    out.nPayloadBytes        = out.nLiteralPayloadBytes + out.nLmdPayloadBytes;

    if (headerSize < 32u || headerSize > avail) return false;

    static constexpr uint8_t kFreqNbits[32] = {
        2, 3, 2, 5, 2, 3, 2, 8, 2, 3, 2, 5, 2, 3, 2, 14,
        2, 3, 2, 5, 2, 3, 2, 8, 2, 3, 2, 5, 2, 3, 2, 14
    };
    static constexpr int8_t kFreqValue[32] = {
        0, 2, 1, 4, 0, 3, 1, -1, 0, 2, 1, 5, 0, 3, 1, -2,
        0, 2, 1, 6, 0, 3, 1, -1, 0, 2, 1, 7, 0, 3, 1, -2
    };

    constexpr size_t kTotalFreqs = kNumLSymbols + kNumMSymbols + kNumDSymbols + kNumLitSymbols;
    uint16_t allFreqs[kTotalFreqs] = {};

    // Forward LSB-first bit reader over [32 .. headerSize)
    size_t bytePos = 32;
    uint64_t bitBuf = 0;
    int bitCnt = 0;
    for (size_t i = 0; i < kTotalFreqs; ++i) {
        while (bitCnt <= 24 && bytePos < headerSize) {
            bitBuf |= static_cast<uint64_t>(src[bytePos++]) << bitCnt;
            bitCnt += 8;
        }
        const uint32_t b14 = static_cast<uint32_t>(bitBuf & 0x3FFFu);
        const uint32_t idx5 = b14 & 0x1Fu;
        const int nbits = kFreqNbits[idx5];
        if (bitCnt < nbits && bytePos >= headerSize) {
            // Remaining trailing zeros if header was exact-trimmed ( only valid if b14 == 0 -> freq 0 )
            if (b14 != 0u) return false;
        }
        const int8_t code = kFreqValue[idx5];
        uint16_t val = 0;
        if (code == -1) {
            val = static_cast<uint16_t>(8u + ((b14 >> 4) & 0xFu));
        } else if (code == -2) {
            val = static_cast<uint16_t>(24u + ((b14 >> 4) & 0x3FFu));
        } else {
            val = static_cast<uint16_t>(code);
        }
        allFreqs[i] = val;
        bitBuf >>= nbits;
        bitCnt -= nbits;
        if (bitCnt < 0) bitCnt = 0;
    }

    size_t fi = 0;
    for (size_t i = 0; i < kNumLSymbols; ++i)   out.lFreq[i] = allFreqs[fi++];
    for (size_t i = 0; i < kNumMSymbols; ++i)   out.mFreq[i] = allFreqs[fi++];
    for (size_t i = 0; i < kNumDSymbols; ++i)   out.dFreq[i] = allFreqs[fi++];
    for (size_t i = 0; i < kNumLitSymbols; ++i) out.literalFreq[i] = allFreqs[fi++];

    outHdrBytes = headerSize;
    return true;
}

inline bool DecodeLzfseBlock(
    const BlockHeaderV1& hdr,
    const uint8_t* payload,
    size_t availPayload,
    std::vector<uint8_t>& out,
    size_t maxOut)
{
    const uint64_t neededPayload = static_cast<uint64_t>(hdr.nLiteralPayloadBytes) + hdr.nLmdPayloadBytes;
    if (neededPayload > availPayload || hdr.nPayloadBytes < neededPayload || hdr.nPayloadBytes > availPayload) {
        return false;
    }
    if (hdr.nRawBytes > maxOut - out.size()) return false;
    if (hdr.nLiterals > (1u << 22) || hdr.nMatches > (1u << 22)) return false;
    if (hdr.lState >= kLStates || hdr.mState >= kMStates || hdr.dState >= kDStates) return false;
    for (int i = 0; i < 4; ++i) {
        if (hdr.literalState[i] >= kLitStates) return false;
    }

    std::vector<FseLitEntry> litTable(kLitStates);
    FseValueEntry lTable[kLStates] = {};
    FseValueEntry mTable[kMStates] = {};
    FseValueEntry dTable[kDStates] = {};

    if (!InitFseLitDecoderTable(kLitStates, kNumLitSymbols, hdr.literalFreq, litTable.data())) return false;
    if (!InitFseValueDecoderTable(kLStates, kNumLSymbols, hdr.lFreq, kLExtraBits, kLBaseValue, lTable)) return false;
    if (!InitFseValueDecoderTable(kMStates, kNumMSymbols, hdr.mFreq, kMExtraBits, kMBaseValue, mTable)) return false;
    if (!InitFseValueDecoderTable(kDStates, kNumDSymbols, hdr.dFreq, kDExtraBits, kDBaseValue.data(), dTable)) return false;

    // 1. Decode interleaved 4-way literal stream
    std::vector<uint8_t> literals(hdr.nLiterals + 4u, 0u);
    if (hdr.nLiterals > 0u) {
        FseBackwardStream litIn;
        if (!litIn.Init(payload, hdr.nLiteralPayloadBytes, hdr.literalBits)) return false;
        uint16_t s0 = hdr.literalState[0];
        uint16_t s1 = hdr.literalState[1];
        uint16_t s2 = hdr.literalState[2];
        uint16_t s3 = hdr.literalState[3];
        uint32_t i = 0;
        const uint32_t quadEnd = hdr.nLiterals & ~3u;
        for (; i < quadEnd; i += 4u) {
            litIn.Flush();
            literals[i + 0] = litIn.DecodeLit(s0, litTable.data());
            literals[i + 1] = litIn.DecodeLit(s1, litTable.data());
            literals[i + 2] = litIn.DecodeLit(s2, litTable.data());
            literals[i + 3] = litIn.DecodeLit(s3, litTable.data());
        }
        litIn.Flush();
        if (i < hdr.nLiterals) literals[i++] = litIn.DecodeLit(s0, litTable.data());
        if (i < hdr.nLiterals) literals[i++] = litIn.DecodeLit(s1, litTable.data());
        if (i < hdr.nLiterals) literals[i++] = litIn.DecodeLit(s2, litTable.data());
    }

    // 2. Decode L, M, D stream & execute LZ matches into out
    const size_t blockOutStart = out.size();
    const size_t blockOutLimit = blockOutStart + hdr.nRawBytes;
    out.reserve(blockOutLimit);

    FseBackwardStream lmdIn;
    if (!lmdIn.Init(payload + hdr.nLiteralPayloadBytes, hdr.nLmdPayloadBytes, hdr.lmdBits)) return false;

    uint16_t lState = hdr.lState;
    uint16_t mState = hdr.mState;
    uint16_t dState = hdr.dState;
    int32_t prevD = -1;
    size_t litPos = 0;

    for (uint32_t k = 0; k < hdr.nMatches; ++k) {
        lmdIn.Flush();
        const int32_t L = lmdIn.DecodeValue(lState, lTable);
        lmdIn.Flush();
        const int32_t M = lmdIn.DecodeValue(mState, mTable);
        lmdIn.Flush();
        const int32_t rawD = lmdIn.DecodeValue(dState, dTable);
        const int32_t D = (rawD == 0) ? prevD : rawD;
        prevD = D;

        if (L < 0 || M < 0) return false;
        const size_t uL = static_cast<size_t>(L);
        const size_t uM = static_cast<size_t>(M);
        if (uL > hdr.nLiterals - litPos) return false;
        if (uL > blockOutLimit - out.size()) return false;

        if (uL > 0) {
            out.insert(out.end(), literals.data() + litPos, literals.data() + litPos + uL);
            litPos += uL;
        }
        if (uM > 0) {
            if (D <= 0 || static_cast<size_t>(D) > out.size() || uM > blockOutLimit - out.size()) return false;
            const size_t uD = static_cast<size_t>(D);
            for (size_t c = 0; c < uM; ++c) {
                out.push_back(out[out.size() - uD]);
            }
        }
    }

    // Copy any trailing literals after the final match
    if (litPos < hdr.nLiterals) {
        const size_t remLit = hdr.nLiterals - litPos;
        if (remLit > blockOutLimit - out.size()) return false;
        out.insert(out.end(), literals.data() + litPos, literals.data() + litPos + remLit);
    }

    return out.size() == blockOutLimit;
}

inline bool DecodeLzvnPayload(
    const uint8_t* src,
    size_t srcLen,
    uint32_t nRawBytes,
    std::vector<uint8_t>& out,
    size_t maxOut)
{
    if (nRawBytes > maxOut - out.size()) return false;
    const size_t startOut = out.size();
    const size_t targetOut = startOut + nRawBytes;
    out.reserve(targetOut);

    size_t pos = 0;
    size_t prevD = 0;

    auto emitLiteralsAndMatch = [&](size_t L, size_t M, size_t D) -> bool {
        if (L > srcLen - pos || L > targetOut - out.size()) return false;
        if (L > 0) {
            out.insert(out.end(), src + pos, src + pos + L);
            pos += L;
        }
        if (M > 0) {
            if (D == 0 || D > out.size() || M > targetOut - out.size()) return false;
            for (size_t i = 0; i < M; ++i) {
                out.push_back(out[out.size() - D]);
            }
        }
        return true;
    };

    while (out.size() < targetOut) {
        if (pos >= srcLen) return false;
        const uint8_t opc = src[pos++];

        if (opc == 0x06u) {
            // EOS
            break;
        }
        if (opc == 0x0Eu || opc == 0x16u) {
            // NOP
            continue;
        }
        if (opc >= 0xE0u) {
            if (opc == 0xE0u) {
                // LRG_L
                if (pos >= srcLen) return false;
                const size_t L = static_cast<size_t>(src[pos++]) + 16u;
                if (!emitLiteralsAndMatch(L, 0, 0)) return false;
            } else if (opc < 0xF0u) {
                // SML_L (0xE1..0xEF)
                const size_t L = opc & 0x0Fu;
                if (!emitLiteralsAndMatch(L, 0, 0)) return false;
            } else if (opc == 0xF0u) {
                // LRG_M
                if (pos >= srcLen) return false;
                const size_t M = static_cast<size_t>(src[pos++]) + 16u;
                if (!emitLiteralsAndMatch(0, M, prevD)) return false;
            } else {
                // SML_M (0xF1..0xFF)
                const size_t M = opc & 0x0Fu;
                if (!emitLiteralsAndMatch(0, M, prevD)) return false;
            }
            continue;
        }

        if (opc >= 0xD0u) {
            // 0xD0..0xDF: UDEF
            return false;
        }

        if (opc >= 0xA0u && opc <= 0xBFu) {
            // MED_D: 3 bytes (0b101LLMMM DDDDDDMM DDDDDDDD)
            if (pos + 2 > srcLen) return false;
            const uint8_t b1 = src[pos++];
            const uint8_t b2 = src[pos++];
            const size_t L = (opc >> 3) & 0x03u;
            const size_t M = (static_cast<size_t>(opc & 0x07u) << 2 | (b1 & 0x03u)) + 3u;
            const size_t D = (static_cast<size_t>(b2) << 6) | (b1 >> 2);
            prevD = D;
            if (!emitLiteralsAndMatch(L, M, D)) return false;
            continue;
        }

        const uint8_t low3 = opc & 0x07u;
        const size_t L = (opc >> 6) & 0x03u;
        const size_t M = ((opc >> 3) & 0x07u) + 3u;

        if (low3 == 0x06u) {
            // L == 0 here means 0x1E, 0x26, 0x2E, 0x36, 0x3E -> UDEF
            if (L == 0u) return false;
            // PRE_D
            if (!emitLiteralsAndMatch(L, M, prevD)) return false;
        } else if (low3 == 0x07u) {
            // LRG_D: 16-bit LE distance
            if (pos + 2 > srcLen) return false;
            const size_t D = static_cast<size_t>(src[pos]) | (static_cast<size_t>(src[pos + 1]) << 8);
            pos += 2;
            prevD = D;
            if (!emitLiteralsAndMatch(L, M, D)) return false;
        } else {
            // SML_D: 11-bit distance
            if (pos >= srcLen) return false;
            const size_t D = (static_cast<size_t>(low3) << 8) | src[pos++];
            prevD = D;
            if (!emitLiteralsAndMatch(L, M, D)) return false;
        }
    }

    return out.size() == targetOut;
}

} // namespace Detail

// Returns true when `src` begins with a valid Apple LZFSE / LZVN block magic ("bvx1", "bvx2", "bvxn", "bvx-").
inline bool HasMagic(const uint8_t* src, size_t srcLen) noexcept {
    if (!src || srcLen < 4) return false;
    const uint32_t m = Detail::Rd32LE(src);
    return m == Detail::kMagicBvx1 || m == Detail::kMagicBvx2 ||
           m == Detail::kMagicBvxn || m == Detail::kMagicLzvn;
}

// Decompresses a multi-block Apple LZFSE / LZVN stream ("bvx1", "bvx2", "bvxn", "bvx-", terminated by "bvx$" or EOF).
inline bool Decompress(
    const uint8_t* src,
    size_t srcLen,
    std::vector<uint8_t>& out,
    size_t maxOut = 128 * 1024 * 1024)
{
    out.clear();
    if (!src || srcLen < 4) return false;

    size_t pos = 0;
    bool decodedAnyBlock = false;

    while (pos + 4 <= srcLen) {
        const uint32_t magic = Detail::Rd32LE(src + pos);
        if (magic == Detail::kMagicEnd) {
            pos += 4;
            return decodedAnyBlock || srcLen == 4;
        }
        if (magic == Detail::kMagicBvxn) {
            if (pos + 8 > srcLen) return false;
            const uint32_t nRaw = Detail::Rd32LE(src + pos + 4);
            pos += 8;
            if (nRaw > srcLen - pos || nRaw > maxOut - out.size()) return false;
            out.insert(out.end(), src + pos, src + pos + nRaw);
            pos += nRaw;
            decodedAnyBlock = true;
        } else if (magic == Detail::kMagicLzvn) {
            if (pos + 12 > srcLen) return false;
            const uint32_t nRaw     = Detail::Rd32LE(src + pos + 4);
            const uint32_t nPayload = Detail::Rd32LE(src + pos + 8);
            pos += 12;
            if (nPayload > srcLen - pos) return false;
            if (!Detail::DecodeLzvnPayload(src + pos, nPayload, nRaw, out, maxOut)) return false;
            pos += nPayload;
            decodedAnyBlock = true;
        } else if (magic == Detail::kMagicBvx1 || magic == Detail::kMagicBvx2) {
            Detail::BlockHeaderV1 hdr{};
            size_t hdrBytes = 0;
            const bool hdrOk = (magic == Detail::kMagicBvx1)
                ? Detail::ParseBvx1Header(src + pos, srcLen - pos, hdr, hdrBytes)
                : Detail::ParseBvx2Header(src + pos, srcLen - pos, hdr, hdrBytes);
            if (!hdrOk) return false;
            pos += hdrBytes;
            if (hdr.nPayloadBytes > srcLen - pos) return false;
            if (!Detail::DecodeLzfseBlock(hdr, src + pos, hdr.nPayloadBytes, out, maxOut)) return false;
            pos += hdr.nPayloadBytes;
            decodedAnyBlock = true;
        } else {
            return false;
        }
    }

    return decodedAnyBlock;
}

// Decompresses a raw headerless LZVN buffer when the caller already knows `expectedRawBytes` (e.g. raw LZVN tiles).
inline bool DecompressRawLzvn(
    const uint8_t* src,
    size_t srcLen,
    uint32_t expectedRawBytes,
    std::vector<uint8_t>& out,
    size_t maxOut = 128 * 1024 * 1024)
{
    out.clear();
    if (!src) return false;
    return Detail::DecodeLzvnPayload(src, srcLen, expectedRawBytes, out, maxOut);
}

} // namespace Explorer2::Lzfse
