#pragma once
// LasLidar.h — ASPRS LAS 1.0–1.4 (.las) & LASzip v1/v2/v3 (.laz) LiDAR Point Cloud 3D Parser
// Supports LAS 1.4 64-bit Extended Number of Point Records (offset 247), Point Formats 0..10,
// per-point RGB, and real LASzip VLR 22204 chunked arithmetic decoding (POINT10 & POINT14).

namespace Formats {

namespace DetailLas {

inline uint16_t RdU16Las(const uint8_t* p) {
    uint16_t v = 0; std::memcpy(&v, p, 2); return v;
}
inline uint32_t RdU32Las(const uint8_t* p) {
    uint32_t v = 0; std::memcpy(&v, p, 4); return v;
}
inline int32_t RdI32Las(const uint8_t* p) {
    int32_t v = 0; std::memcpy(&v, p, 4); return v;
}
inline uint64_t RdU64Las(const uint8_t* p) {
    uint64_t v = 0; std::memcpy(&v, p, 8); return v;
}
inline int64_t RdI64Las(const uint8_t* p) {
    int64_t v = 0; std::memcpy(&v, p, 8); return v;
}
inline double RdF64Las(const uint8_t* p) {
    double v = 0.0; std::memcpy(&v, p, 8); return v;
}

inline int LasRgbByteOffset(uint8_t baseFmt) {
    switch (baseFmt) {
    case 2:  return 20;
    case 3:
    case 5:  return 28;
    case 7:
    case 8:  return 30;
    case 10: return 34;
    default: return -1;
    }
}

// ============================================================================
// LASzip Arithmetic Decoder & Models (Martin Isenburg / Amir Said FastAC)
// ============================================================================
static constexpr uint32_t kLazAcMinLength = 0x01000000u;
static constexpr uint32_t kLazAcMaxLength = 0xFFFFFFFFu;
static constexpr uint32_t kLazBmShift     = 13u;
static constexpr uint32_t kLazBmMaxCount  = 1u << kLazBmShift; // 8192
static constexpr uint32_t kLazDmShift     = 15u;
static constexpr uint32_t kLazDmMaxCount  = 1u << kLazDmShift; // 32768

struct LazBitModel {
    uint32_t updateCycle = 4;
    uint32_t bitsUntilUpdate = 4;
    uint32_t bit0Prob = 1u << (kLazBmShift - 1);
    uint32_t bit0Count = 1;
    uint32_t bitCount = 2;

    void Init() {
        bit0Count = 1;
        bitCount = 2;
        bit0Prob = 1u << (kLazBmShift - 1);
        updateCycle = bitsUntilUpdate = 4;
    }

    void Update() {
        if ((bitCount += updateCycle) > kLazBmMaxCount) {
            bitCount = (bitCount + 1u) >> 1;
            bit0Count = (bit0Count + 1u) >> 1;
            if (bit0Count == bitCount) ++bitCount;
        }
        uint32_t scale = 0x80000000u / bitCount;
        bit0Prob = (bit0Count * scale) >> (31u - kLazBmShift);
        updateCycle = (5u * updateCycle) >> 2;
        if (updateCycle > 64u) updateCycle = 64u;
        bitsUntilUpdate = updateCycle;
    }
};

struct LazSymbolModel {
    uint32_t symbols = 0;
    uint32_t totalCount = 0;
    uint32_t updateCycle = 0;
    uint32_t symbolsUntilUpdate = 0;
    std::vector<uint32_t> symbolCount;
    std::vector<uint32_t> distribution;

    void Init(uint32_t numSymbols) {
        symbols = numSymbols;
        symbolCount.assign(symbols, 1u);
        distribution.assign(symbols, 0u);
        totalCount = 0;
        updateCycle = symbols;
        Update();
        symbolsUntilUpdate = updateCycle = (symbols + 6u) >> 1;
    }

    bool IsInitialized() const { return symbols != 0; }

    void Update() {
        if ((totalCount += updateCycle) > kLazDmMaxCount) {
            totalCount = 0;
            for (uint32_t n = 0; n < symbols; ++n) {
                totalCount += (symbolCount[n] = (symbolCount[n] + 1u) >> 1);
            }
        }
        uint32_t sum = 0;
        uint32_t scale = 0x80000000u / totalCount;
        for (uint32_t k = 0; k < symbols; ++k) {
            distribution[k] = (scale * sum) >> (31u - kLazDmShift);
            sum += symbolCount[k];
        }
        updateCycle = (5u * updateCycle) >> 2;
        uint32_t maxCycle = (symbols + 6u) << 3;
        if (maxCycle > (kLazDmMaxCount >> 1)) maxCycle = (kLazDmMaxCount >> 1);
        if (updateCycle > maxCycle) updateCycle = maxCycle;
        symbolsUntilUpdate = updateCycle;
    }
};

struct LazArithmeticDecoder {
    const uint8_t* ptr = nullptr;
    const uint8_t* end = nullptr;
    uint32_t value = 0;
    uint32_t length = kLazAcMaxLength;
    size_t overreadBytes = 0;

    bool Init(const uint8_t* streamStart, const uint8_t* streamEnd, bool readInitBytes = true) {
        ptr = streamStart;
        end = streamEnd;
        value = 0;
        length = kLazAcMaxLength;
        overreadBytes = 0;
        if (readInitBytes) {
            if (!ptr || static_cast<size_t>(end - ptr) < 4) return false;
            value = (static_cast<uint32_t>(ptr[0]) << 24) |
                    (static_cast<uint32_t>(ptr[1]) << 16) |
                    (static_cast<uint32_t>(ptr[2]) << 8)  |
                     static_cast<uint32_t>(ptr[3]);
            ptr += 4;
        }
        return true;
    }

    bool Ok() const { return overreadBytes <= 8; }

    uint8_t NextStreamByte() {
        if (ptr < end) return *ptr++;
        ++overreadBytes;
        return 0;
    }

    void Renorm() {
        do {
            value = (value << 8) | static_cast<uint32_t>(NextStreamByte());
        } while ((length <<= 8) < kLazAcMinLength);
    }

    uint32_t DecodeBit(LazBitModel& m) {
        uint32_t x = m.bit0Prob * (length >> kLazBmShift);
        uint32_t sym = (value >= x) ? 1u : 0u;
        if (sym == 0u) {
            length = x;
            ++m.bit0Count;
        } else {
            value -= x;
            length -= x;
        }
        if (length < kLazAcMinLength) Renorm();
        if (--m.bitsUntilUpdate == 0u) m.Update();
        return sym;
    }

    uint32_t DecodeSymbol(LazSymbolModel& m) {
        uint32_t y = length;
        uint32_t x = 0;
        uint32_t lenScaled = (length >>= kLazDmShift);
        uint32_t lo = 0, hi = m.symbols;
        while (lo + 1u < hi) {
            uint32_t mid = (lo + hi) >> 1;
            uint32_t z = lenScaled * m.distribution[mid];
            if (z > value) {
                hi = mid;
                y = z;
            } else {
                lo = mid;
                x = z;
            }
        }
        value -= x;
        length = y - x;
        if (length < kLazAcMinLength) Renorm();
        ++m.symbolCount[lo];
        if (--m.symbolsUntilUpdate == 0u) m.Update();
        return lo;
    }

    uint32_t ReadBits(uint32_t bits) {
        if (bits == 0u) return 0u;
        if (bits > 19u) {
            uint32_t lo = ReadBits(19u);
            uint32_t hi = ReadBits(bits - 19u);
            return (hi << 19) | lo;
        }
        uint32_t sym = value / (length >>= bits);
        value -= length * sym;
        if (length < kLazAcMinLength) Renorm();
        return sym;
    }

    uint8_t ReadByte() { return static_cast<uint8_t>(ReadBits(8u)); }
    uint16_t ReadShort() { return static_cast<uint16_t>(ReadBits(16u)); }
    uint32_t ReadInt() {
        uint32_t lo = ReadBits(16u);
        uint32_t hi = ReadBits(16u);
        return (hi << 16) | lo;
    }
    uint64_t ReadInt64() {
        uint64_t lo = ReadInt();
        uint64_t hi = ReadInt();
        return (hi << 32) | lo;
    }
};

struct LazIntegerCompressor {
    uint32_t bits = 16;
    uint32_t contexts = 1;
    uint32_t bitsHigh = 8;
    uint32_t corrBits = 16;
    uint32_t k = 0;
    std::vector<LazSymbolModel> mBits;
    LazBitModel mCorrector0;
    std::vector<LazSymbolModel> mCorrector;

    void Setup(uint32_t numBits, uint32_t numContexts = 1, uint32_t highBits = 8) {
        bits = numBits;
        contexts = numContexts;
        bitsHigh = highBits;
        corrBits = numBits;
        k = 0;
        mBits.resize(contexts);
        mCorrector.resize(corrBits + 1);
    }

    void InitDecompressor() {
        k = 0;
        for (uint32_t c = 0; c < contexts; ++c) {
            mBits[c].Init(corrBits + 1u);
        }
        mCorrector0.Init();
        for (uint32_t i = 1; i <= corrBits; ++i) {
            mCorrector[i].Init((i <= bitsHigh) ? (1u << i) : (1u << bitsHigh));
        }
    }

    uint32_t GetK() const { return k; }

    int32_t Decompress(LazArithmeticDecoder& dec, int32_t pred, uint32_t context = 0) {
        k = dec.DecodeSymbol(mBits[context]);
        int32_t corr = 0;
        if (k == 0u) {
            corr = static_cast<int32_t>(dec.DecodeBit(mCorrector0));
        } else if (k < 32u) {
            uint32_t c = 0;
            if (k <= bitsHigh) {
                c = dec.DecodeSymbol(mCorrector[k]);
            } else {
                uint32_t k1 = k - bitsHigh;
                uint32_t c1 = dec.ReadBits(k1);
                uint32_t c2 = dec.DecodeSymbol(mCorrector[bitsHigh]);
                c = (c2 << k1) | c1;
            }
            if (c >= (1u << (k - 1u))) {
                corr = static_cast<int32_t>(c + 1u);
            } else {
                corr = static_cast<int32_t>(c) - static_cast<int32_t>((1u << k) - 1u);
            }
        } else {
            corr = static_cast<int32_t>(dec.ReadInt());
        }

        int64_t realVal = static_cast<int64_t>(pred) + static_cast<int64_t>(corr);
        if (bits < 32u) {
            int64_t modRange = 1LL << bits;
            if (realVal < 0) realVal += modRange;
            else if (realVal >= modRange) realVal -= modRange;
        }
        return static_cast<int32_t>(realVal);
    }
};

struct LazStreamingMedian5 {
    int32_t values[5] = {};
    bool high = true;

    void Init() {
        for (int& v : values) v = 0;
        high = true;
    }
    int32_t Get() const { return values[2]; }
    void Add(int32_t v) {
        if (high) {
            if (v < values[2]) {
                values[4] = values[3];
                values[3] = values[2];
                if (v < values[0]) { values[2] = values[1]; values[1] = values[0]; values[0] = v; }
                else if (v < values[1]) { values[2] = values[1]; values[1] = v; }
                else { values[2] = v; }
            } else {
                if (v < values[3]) { values[4] = values[3]; values[3] = v; }
                else { values[4] = v; }
                high = false;
            }
        } else {
            if (values[2] < v) {
                values[0] = values[1];
                values[1] = values[2];
                if (values[4] < v) { values[2] = values[3]; values[3] = values[4]; values[4] = v; }
                else if (values[3] < v) { values[2] = values[3]; values[3] = v; }
                else { values[2] = v; }
            } else {
                if (values[1] < v) { values[0] = values[1]; values[1] = v; }
                else { values[0] = v; }
                high = true;
            }
        }
    }
};

static constexpr uint8_t kLazNumberReturnMap[8][8] = {
    { 15, 14, 13, 12, 11, 10,  9,  8 },
    { 14,  0, 13, 12, 11, 10,  9,  8 },
    { 13,  1,  2, 12, 11, 10,  9,  8 },
    { 12,  1,  3,  2, 11, 10,  9,  8 },
    { 11,  1,  3,  3,  2, 10,  9,  8 },
    { 10,  1,  3,  3,  3,  2,  9,  8 },
    {  9,  1,  3,  3,  3,  3,  2,  8 },
    {  8,  1,  3,  3,  3,  3,  3,  2 }
};

static constexpr uint8_t kLazNumberReturnLevel[8][8] = {
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 1, 0, 1, 2, 3, 4, 5, 6 },
    { 2, 1, 0, 1, 2, 3, 4, 5 },
    { 3, 2, 1, 0, 1, 2, 3, 4 },
    { 4, 3, 2, 1, 0, 1, 2, 3 },
    { 5, 4, 3, 2, 1, 0, 1, 2 },
    { 6, 5, 4, 3, 2, 1, 0, 1 },
    { 7, 6, 5, 4, 3, 2, 1, 0 }
};

// LASreadItemCompressed_POINT10_v2 state
struct LazItemPoint10v2 {
    uint8_t lastItem[20] = {};
    uint16_t lastIntensity[16] = {};
    LazStreamingMedian5 lastXDiffMedian5[16];
    LazStreamingMedian5 lastYDiffMedian5[16];
    int32_t lastHeight[8] = {};

    LazSymbolModel mChangedValues;
    LazIntegerCompressor icIntensity;
    LazSymbolModel mScanAngleRank[2];
    LazIntegerCompressor icPointSourceID;
    LazSymbolModel mBitByte[256];
    LazSymbolModel mClassification[256];
    LazSymbolModel mUserData[256];
    LazIntegerCompressor icDx;
    LazIntegerCompressor icDy;
    LazIntegerCompressor icZ;

    void Construct() {
        mChangedValues.Init(64);
        icIntensity.Setup(16, 4);
        mScanAngleRank[0].Init(256);
        mScanAngleRank[1].Init(256);
        icPointSourceID.Setup(16, 1);
        icDx.Setup(32, 2);
        icDy.Setup(32, 22);
        icZ.Setup(32, 20);
    }

    void InitSeed(const uint8_t* seed20) {
        std::memcpy(lastItem, seed20, 20);
        for (int i = 0; i < 16; ++i) {
            lastXDiffMedian5[i].Init();
            lastYDiffMedian5[i].Init();
            lastIntensity[i] = 0;
            lastHeight[i / 2] = 0;
        }
        mChangedValues.Init(64);
        icIntensity.InitDecompressor();
        mScanAngleRank[0].Init(256);
        mScanAngleRank[1].Init(256);
        icPointSourceID.InitDecompressor();
        for (int i = 0; i < 256; ++i) {
            if (mBitByte[i].IsInitialized()) mBitByte[i].Init(256);
            if (mClassification[i].IsInitialized()) mClassification[i].Init(256);
            if (mUserData[i].IsInitialized()) mUserData[i].Init(256);
        }
        icDx.InitDecompressor();
        icDy.InitDecompressor();
        icZ.InitDecompressor();
        // Per LASzip POINT10_v2::init: last_item intensity field is zeroed for first diff prediction
        lastItem[12] = 0;
        lastItem[13] = 0;
    }

    void Read(LazArithmeticDecoder& dec, int32_t& outX, int32_t& outY, int32_t& outZ) {
        uint32_t changedValues = dec.DecodeSymbol(mChangedValues);
        uint32_t r = 0, n = 0, m = 0, l = 0;
        if (changedValues != 0u) {
            if (changedValues & 32u) {
                uint8_t prev = lastItem[14];
                if (!mBitByte[prev].IsInitialized()) mBitByte[prev].Init(256);
                lastItem[14] = static_cast<uint8_t>(dec.DecodeSymbol(mBitByte[prev]));
            }
            r = lastItem[14] & 7u;
            n = (lastItem[14] >> 3) & 7u;
            m = kLazNumberReturnMap[n][r];
            l = kLazNumberReturnLevel[n][r];

            if (changedValues & 16u) {
                uint16_t inten = static_cast<uint16_t>(icIntensity.Decompress(dec, lastIntensity[m], (m < 3u) ? m : 3u));
                lastIntensity[m] = inten;
                std::memcpy(lastItem + 12, &inten, 2);
            } else {
                std::memcpy(lastItem + 12, &lastIntensity[m], 2);
            }

            if (changedValues & 8u) {
                uint8_t prev = lastItem[15];
                if (!mClassification[prev].IsInitialized()) mClassification[prev].Init(256);
                lastItem[15] = static_cast<uint8_t>(dec.DecodeSymbol(mClassification[prev]));
            }
            if (changedValues & 4u) {
                uint32_t val = dec.DecodeSymbol(mScanAngleRank[(lastItem[14] >> 6) & 1u]);
                lastItem[16] = static_cast<uint8_t>(val + lastItem[16]);
            }
            if (changedValues & 2u) {
                uint8_t prev = lastItem[17];
                if (!mUserData[prev].IsInitialized()) mUserData[prev].Init(256);
                lastItem[17] = static_cast<uint8_t>(dec.DecodeSymbol(mUserData[prev]));
            }
            if (changedValues & 1u) {
                uint16_t psid = RdU16Las(lastItem + 18);
                psid = static_cast<uint16_t>(icPointSourceID.Decompress(dec, psid, 0));
                std::memcpy(lastItem + 18, &psid, 2);
            }
        } else {
            r = lastItem[14] & 7u;
            n = (lastItem[14] >> 3) & 7u;
            m = kLazNumberReturnMap[n][r];
            l = kLazNumberReturnLevel[n][r];
            std::memcpy(lastItem + 12, &lastIntensity[m], 2);
        }

        int32_t medianX = lastXDiffMedian5[m].Get();
        int32_t diffX = icDx.Decompress(dec, medianX, (n == 1u) ? 1u : 0u);
        int32_t curX = RdI32Las(lastItem + 0) + diffX;
        std::memcpy(lastItem + 0, &curX, 4);
        lastXDiffMedian5[m].Add(diffX);

        int32_t medianY = lastYDiffMedian5[m].Get();
        uint32_t kBitsX = icDx.GetK();
        uint32_t ctxY = ((n == 1u) ? 1u : 0u) + ((kBitsX < 20u) ? (kBitsX & ~1u) : 20u);
        int32_t diffY = icDy.Decompress(dec, medianY, ctxY);
        int32_t curY = RdI32Las(lastItem + 4) + diffY;
        std::memcpy(lastItem + 4, &curY, 4);
        lastYDiffMedian5[m].Add(diffY);

        uint32_t kBits = (icDx.GetK() + icDy.GetK()) / 2u;
        uint32_t ctxZ = ((n == 1u) ? 1u : 0u) + ((kBits < 18u) ? (kBits & ~1u) : 18u);
        int32_t curZ = icZ.Decompress(dec, lastHeight[l], ctxZ);
        std::memcpy(lastItem + 8, &curZ, 4);
        lastHeight[l] = curZ;

        outX = curX;
        outY = curY;
        outZ = curZ;
    }
};

// LASreadItemCompressed_POINT10_v1 state
struct LazItemPoint10v1 {
    uint8_t lastItem[20] = {};
    int32_t lastXDiff[3] = {};
    int32_t lastYDiff[3] = {};
    int32_t lastIncr = 0;
    LazIntegerCompressor icDx, icDy, icZ, icIntensity, icScanAngle, icPointSourceID;
    LazSymbolModel mChangedValues;
    LazSymbolModel mBitByte[256];
    LazSymbolModel mClassification[256];
    LazSymbolModel mUserData[256];

    void InitSeed(const uint8_t* seed20) {
        std::memcpy(lastItem, seed20, 20);
        for (int i = 0; i < 3; ++i) { lastXDiff[i] = 0; lastYDiff[i] = 0; }
        lastIncr = 0;
        icDx.Setup(32, 1); icDx.InitDecompressor();
        icDy.Setup(32, 20); icDy.InitDecompressor();
        icZ.Setup(32, 20); icZ.InitDecompressor();
        icIntensity.Setup(16, 1); icIntensity.InitDecompressor();
        icScanAngle.Setup(8, 2); icScanAngle.InitDecompressor();
        icPointSourceID.Setup(16, 1); icPointSourceID.InitDecompressor();
        mChangedValues.Init(64);
        for (int i = 0; i < 256; ++i) {
            if (mBitByte[i].IsInitialized()) mBitByte[i].Init(256);
            if (mClassification[i].IsInitialized()) mClassification[i].Init(256);
            if (mUserData[i].IsInitialized()) mUserData[i].Init(256);
        }
    }

    static int32_t Median3(int32_t a, int32_t b, int32_t c) {
        if (a < b) {
            if (b < c) return b;
            return (a < c) ? c : a;
        }
        if (a < c) return a;
        return (b < c) ? c : b;
    }

    void Read(LazArithmeticDecoder& dec, int32_t& outX, int32_t& outY, int32_t& outZ) {
        int32_t medX = Median3(lastXDiff[0], lastXDiff[1], lastXDiff[2]);
        int32_t medY = Median3(lastYDiff[0], lastYDiff[1], lastYDiff[2]);
        int32_t dx = icDx.Decompress(dec, medX, 0);
        int32_t curX = RdI32Las(lastItem + 0) + dx;
        std::memcpy(lastItem + 0, &curX, 4);

        uint32_t kBits = icDx.GetK();
        int32_t dy = icDy.Decompress(dec, medY, (kBits < 19u) ? kBits : 19u);
        int32_t curY = RdI32Las(lastItem + 4) + dy;
        std::memcpy(lastItem + 4, &curY, 4);

        kBits = (kBits + icDy.GetK()) / 2u;
        int32_t curZ = icZ.Decompress(dec, RdI32Las(lastItem + 8), (kBits < 19u) ? kBits : 19u);
        std::memcpy(lastItem + 8, &curZ, 4);

        uint32_t cv = dec.DecodeSymbol(mChangedValues);
        if (cv) {
            if (cv & 32u) {
                uint16_t inten = static_cast<uint16_t>(icIntensity.Decompress(dec, RdU16Las(lastItem + 12), 0));
                std::memcpy(lastItem + 12, &inten, 2);
            }
            if (cv & 16u) {
                uint8_t p = lastItem[14];
                if (!mBitByte[p].IsInitialized()) mBitByte[p].Init(256);
                lastItem[14] = static_cast<uint8_t>(dec.DecodeSymbol(mBitByte[p]));
            }
            if (cv & 8u) {
                uint8_t p = lastItem[15];
                if (!mClassification[p].IsInitialized()) mClassification[p].Init(256);
                lastItem[15] = static_cast<uint8_t>(dec.DecodeSymbol(mClassification[p]));
            }
            if (cv & 4u) {
                lastItem[16] = static_cast<uint8_t>(icScanAngle.Decompress(dec, lastItem[16], (kBits < 3u) ? 1u : 0u));
            }
            if (cv & 2u) {
                uint8_t p = lastItem[17];
                if (!mUserData[p].IsInitialized()) mUserData[p].Init(256);
                lastItem[17] = static_cast<uint8_t>(dec.DecodeSymbol(mUserData[p]));
            }
            if (cv & 1u) {
                uint16_t psid = static_cast<uint16_t>(icPointSourceID.Decompress(dec, RdU16Las(lastItem + 18), 0));
                std::memcpy(lastItem + 18, &psid, 2);
            }
        }
        lastXDiff[lastIncr] = dx;
        lastYDiff[lastIncr] = dy;
        if (++lastIncr == 3) lastIncr = 0;
        outX = curX; outY = curY; outZ = curZ;
    }
};

// Extra v2 sub-items so POINT10+GPSTIME11 / RGB12 / BYTE streams stay synchronized!
struct LazItemGpsTime11v2 {
    uint32_t last = 0, next = 0;
    int64_t lastGpstime[4] = {};
    int32_t lastGpstimeDiff[4] = {};
    int32_t multiExtremeCounter[4] = {};
    LazSymbolModel mGpstimeMulti;
    LazSymbolModel mGpstime0diff;
    LazIntegerCompressor icGpstime;

    void InitSeed(const uint8_t* seed8) {
        last = 0; next = 0;
        lastGpstime[0] = RdI64Las(seed8);
        lastGpstime[1] = lastGpstime[2] = lastGpstime[3] = 0;
        for (int i = 0; i < 4; ++i) { lastGpstimeDiff[i] = 0; multiExtremeCounter[i] = 0; }
        mGpstimeMulti.Init(515);
        mGpstime0diff.Init(6);
        icGpstime.Setup(32, 9);
        icGpstime.InitDecompressor();
    }

    void Read(LazArithmeticDecoder& dec) {
        if (lastGpstimeDiff[last] == 0) {
            uint32_t multi = dec.DecodeSymbol(mGpstime0diff);
            if (multi == 1u) {
                lastGpstimeDiff[last] = icGpstime.Decompress(dec, 0, 0);
                lastGpstime[last] += lastGpstimeDiff[last];
                multiExtremeCounter[last] = 0;
            } else if (multi == 2u) {
                next = (next + 1u) & 3u;
                lastGpstime[next] = icGpstime.Decompress(dec, static_cast<int32_t>(lastGpstime[last] >> 32), 8);
                lastGpstime[next] = (lastGpstime[next] << 32) | static_cast<uint32_t>(dec.ReadInt());
                last = next;
                lastGpstimeDiff[last] = 0;
                multiExtremeCounter[last] = 0;
            } else if (multi > 2u) {
                last = (last + multi - 2u) & 3u;
                Read(dec);
            }
        } else {
            uint32_t multi = dec.DecodeSymbol(mGpstimeMulti);
            if (multi == 1u) {
                lastGpstime[last] += icGpstime.Decompress(dec, lastGpstimeDiff[last], 1);
                multiExtremeCounter[last] = 0;
            } else if (multi < 500u) {
                int32_t gpstimeDiff = 0;
                if (multi == 0u) {
                    gpstimeDiff = icGpstime.Decompress(dec, 0, 7);
                    if (++multiExtremeCounter[last] > 3) {
                        lastGpstimeDiff[last] = gpstimeDiff;
                        multiExtremeCounter[last] = 0;
                    }
                } else if (multi < 400u) {
                    gpstimeDiff = icGpstime.Decompress(dec, static_cast<int32_t>(multi) * lastGpstimeDiff[last], (multi < 10u) ? 2u : 3u);
                } else if (multi == 400u) {
                    gpstimeDiff = icGpstime.Decompress(dec, 400 * lastGpstimeDiff[last], 4);
                    if (++multiExtremeCounter[last] > 3) {
                        lastGpstimeDiff[last] = gpstimeDiff;
                        multiExtremeCounter[last] = 0;
                    }
                } else {
                    int32_t mSigned = 400 - static_cast<int32_t>(multi);
                    if (mSigned > -100) {
                        gpstimeDiff = icGpstime.Decompress(dec, mSigned * lastGpstimeDiff[last], 5);
                    } else {
                        gpstimeDiff = icGpstime.Decompress(dec, mSigned * lastGpstimeDiff[last], 6);
                        if (++multiExtremeCounter[last] > 3) {
                            lastGpstimeDiff[last] = gpstimeDiff;
                            multiExtremeCounter[last] = 0;
                        }
                    }
                }
                lastGpstime[last] += gpstimeDiff;
            } else if (multi == 510u || multi == 511u) {
                next = (next + 1u) & 3u;
                lastGpstime[next] = icGpstime.Decompress(dec, static_cast<int32_t>(lastGpstime[last] >> 32), 8);
                lastGpstime[next] = (lastGpstime[next] << 32) | static_cast<uint32_t>(dec.ReadInt());
                last = next;
                lastGpstimeDiff[last] = 0;
                multiExtremeCounter[last] = 0;
            } else if (multi >= 512u) {
                last = (last + multi - 511u) & 3u;
                Read(dec);
            }
        }
    }
};

struct LazItemRgb12v2 {
    uint16_t lastRgb[3] = {};
    LazSymbolModel mByteUsed;
    LazSymbolModel mRgbDiff0, mRgbDiff1, mRgbDiff2, mRgbDiff3, mRgbDiff4, mRgbDiff5;

    void InitSeed(const uint8_t* seed6) {
        lastRgb[0] = RdU16Las(seed6 + 0);
        lastRgb[1] = RdU16Las(seed6 + 2);
        lastRgb[2] = RdU16Las(seed6 + 4);
        mByteUsed.Init(128);
        mRgbDiff0.Init(256); mRgbDiff1.Init(256);
        mRgbDiff2.Init(256); mRgbDiff3.Init(256);
        mRgbDiff4.Init(256); mRgbDiff5.Init(256);
    }

    void Read(LazArithmeticDecoder& dec) {
        uint32_t sym = dec.DecodeSymbol(mByteUsed);
        uint16_t rgb[3] = { lastRgb[0], lastRgb[1], lastRgb[2] };
        int32_t corr = 0, diff = 0;
        if (sym & 1u) {
            corr = static_cast<int32_t>(dec.DecodeSymbol(mRgbDiff0));
            rgb[0] = static_cast<uint16_t>((rgb[0] & 0xFF00u) | static_cast<uint8_t>(corr + (lastRgb[0] & 0xFFu)));
        }
        if (sym & 2u) {
            corr = static_cast<int32_t>(dec.DecodeSymbol(mRgbDiff1));
            rgb[0] = static_cast<uint16_t>((rgb[0] & 0x00FFu) | (static_cast<uint16_t>(static_cast<uint8_t>(corr + (lastRgb[0] >> 8))) << 8));
        }
        if (sym & 64u) {
            diff = static_cast<int32_t>(rgb[0] & 0xFFu) - static_cast<int32_t>(lastRgb[0] & 0xFFu);
            if (sym & 4u) {
                corr = static_cast<int32_t>(dec.DecodeSymbol(mRgbDiff2));
                rgb[1] = static_cast<uint16_t>((rgb[1] & 0xFF00u) | static_cast<uint8_t>(corr + std::clamp(diff + static_cast<int32_t>(lastRgb[1] & 0xFFu), 0, 255)));
            }
            if (sym & 16u) {
                corr = static_cast<int32_t>(dec.DecodeSymbol(mRgbDiff4));
                diff = (diff + (static_cast<int32_t>(rgb[1] & 0xFFu) - static_cast<int32_t>(lastRgb[1] & 0xFFu))) / 2;
                rgb[2] = static_cast<uint16_t>((rgb[2] & 0xFF00u) | static_cast<uint8_t>(corr + std::clamp(diff + static_cast<int32_t>(lastRgb[2] & 0xFFu), 0, 255)));
            }
            diff = static_cast<int32_t>(rgb[0] >> 8) - static_cast<int32_t>(lastRgb[0] >> 8);
            if (sym & 8u) {
                corr = static_cast<int32_t>(dec.DecodeSymbol(mRgbDiff3));
                rgb[1] = static_cast<uint16_t>((rgb[1] & 0x00FFu) | (static_cast<uint16_t>(static_cast<uint8_t>(corr + std::clamp(diff + static_cast<int32_t>(lastRgb[1] >> 8), 0, 255))) << 8));
            }
            if (sym & 32u) {
                corr = static_cast<int32_t>(dec.DecodeSymbol(mRgbDiff5));
                diff = (diff + (static_cast<int32_t>(rgb[1] >> 8) - static_cast<int32_t>(lastRgb[1] >> 8))) / 2;
                rgb[2] = static_cast<uint16_t>((rgb[2] & 0x00FFu) | (static_cast<uint16_t>(static_cast<uint8_t>(corr + std::clamp(diff + static_cast<int32_t>(lastRgb[2] >> 8), 0, 255))) << 8));
            }
        }
        lastRgb[0] = rgb[0]; lastRgb[1] = rgb[1]; lastRgb[2] = rgb[2];
    }
};

struct LazItemBytev2 {
    std::vector<uint8_t> lastBytes;
    std::vector<LazSymbolModel> mBytes;

    void InitSeed(const uint8_t* seed, uint16_t numBytes) {
        lastBytes.assign(seed, seed + numBytes);
        mBytes.resize(numBytes);
        for (uint16_t i = 0; i < numBytes; ++i) mBytes[i].Init(256);
    }
    void Read(LazArithmeticDecoder& dec) {
        for (size_t i = 0; i < lastBytes.size(); ++i) {
            uint32_t val = lastBytes[i] + dec.DecodeSymbol(mBytes[i]);
            lastBytes[i] = static_cast<uint8_t>(val);
        }
    }
};

// LASreadItemCompressed_POINT14_v3/v4 layered XY + Z decoder (compressor == 3)
struct LazItemPoint14v3 {
    struct ScContext {
        int32_t lastX = 0, lastY = 0;
        int32_t lastHeight[8] = {};
        uint8_t lastReturnByte = 0;
        uint8_t lastChannelAndFlags = 0;
        LazSymbolModel mChangedValues[8];
        LazBitModel mScannerChannelBool;
        LazSymbolModel mScannerChannel;
        LazSymbolModel mNumberOfReturns[16];
        LazSymbolModel mReturnNumber[16];
        LazBitModel mReturnNumberGpsSame;
        LazIntegerCompressor icDx;
        LazIntegerCompressor icDy;
        LazIntegerCompressor icZ;
        LazStreamingMedian5 lastXDiffMedian5[12];
        LazStreamingMedian5 lastYDiffMedian5[12];
    };

    ScContext ctx[4];
    uint32_t currentSc = 0;

    void InitChannelFromSeed(uint32_t sc, const uint8_t* seed30) {
        ScContext& c = ctx[sc];
        c.lastX = RdI32Las(seed30 + 0);
        c.lastY = RdI32Las(seed30 + 4);
        int32_t z0 = RdI32Las(seed30 + 8);
        for (int i = 0; i < 8; ++i) c.lastHeight[i] = z0;
        c.lastReturnByte = seed30[14];
        c.lastChannelAndFlags = seed30[15];
        for (int i = 0; i < 8; ++i) c.mChangedValues[i].Init(128);
        c.mScannerChannelBool.Init();
        c.mScannerChannel.Init(3);
        for (int i = 0; i < 16; ++i) {
            c.mNumberOfReturns[i].Init(16);
            c.mReturnNumber[i].Init(16);
        }
        c.mReturnNumberGpsSame.Init();
        c.icDx.Setup(32, 2);  c.icDx.InitDecompressor();
        c.icDy.Setup(32, 22); c.icDy.InitDecompressor();
        c.icZ.Setup(32, 20);  c.icZ.InitDecompressor();
        for (int i = 0; i < 12; ++i) {
            c.lastXDiffMedian5[i].Init();
            c.lastYDiffMedian5[i].Init();
        }
    }

    static bool DecodeChunkPoints(
        const uint8_t* chunkPtr, size_t chunkLen, size_t seedBytes, uint32_t numPtsInChunk,
        double scaleX, double scaleY, double scaleZ,
        double offX, double offY, double offZ,
        uint64_t globalStartIdx, uint64_t sampleStride, size_t maxOutPts,
        std::vector<Vec3f>& outPts)
    {
        if (chunkLen < seedBytes || numPtsInChunk == 0) return false;
        int32_t sx0 = RdI32Las(chunkPtr + 0);
        int32_t sy0 = RdI32Las(chunkPtr + 4);
        int32_t sz0 = RdI32Las(chunkPtr + 8);
        if ((globalStartIdx % sampleStride) == 0 && outPts.size() < maxOutPts) {
            float x = static_cast<float>(sx0 * scaleX + offX);
            float y = static_cast<float>(sy0 * scaleY + offY);
            float z = static_cast<float>(sz0 * scaleZ + offZ);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) outPts.push_back({ x, z, -y });
        }
        if (numPtsInChunk == 1) return true;

        // POINT14 layered header has 9 uint32 layer byte-counts right after seed record
        if (chunkLen < seedBytes + 36) return false;
        const uint8_t* lp = chunkPtr + seedBytes;
        uint32_t bytesChanRetXY = RdU32Las(lp + 0);
        uint32_t bytesZ         = RdU32Las(lp + 4);
        uint64_t totalPoint14Hdr = 36;
        if (seedBytes + totalPoint14Hdr + bytesChanRetXY + bytesZ > chunkLen) return false;

        // Note extra items (RGB14=3*4B, BYTE14=N*4B) may sit between layer sizes and layer streams;
        // compute total layer-size directory by summing POINT14's 9 sizes and verifying stream start
        uint64_t sum9 = 0;
        for (int i = 0; i < 9; ++i) sum9 += RdU32Las(lp + i * 4);
        size_t streamsOff = seedBytes + 36;
        if (streamsOff + sum9 > chunkLen) return false;

        const uint8_t* streamXY = chunkPtr + streamsOff;
        const uint8_t* streamZ  = streamXY + bytesChanRetXY;
        LazArithmeticDecoder decXY, decZ;
        if (bytesChanRetXY == 0 || !decXY.Init(streamXY, streamXY + bytesChanRetXY)) return false;
        bool hasZLayer = (bytesZ > 0);
        if (hasZLayer && !decZ.Init(streamZ, streamZ + bytesZ)) return false;

        LazItemPoint14v3 st;
        uint32_t sc = (chunkPtr[15] >> 6) & 3u;
        st.currentSc = sc;
        bool scInited[4] = {};
        scInited[sc] = true;
        st.InitChannelFromSeed(sc, chunkPtr);

        static constexpr uint8_t kRetCtx14[16][16] = {
            // (n==1 && r==1)->0, (r==1 && n>1)->1, (r==n && n>1)->2, else->3
            {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
            {3,0,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
            {3,1,2,3,3,3,3,3,3,3,3,3,3,3,3,3},
            {3,1,3,2,3,3,3,3,3,3,3,3,3,3,3,3},
            {3,1,3,3,2,3,3,3,3,3,3,3,3,3,3,3},
            {3,1,3,3,3,2,3,3,3,3,3,3,3,3,3,3},
            {3,1,3,3,3,3,2,3,3,3,3,3,3,3,3,3},
            {3,1,3,3,3,3,3,2,3,3,3,3,3,3,3,3},
            {3,1,3,3,3,3,3,3,2,3,3,3,3,3,3,3},
            {3,1,3,3,3,3,3,3,3,2,3,3,3,3,3,3},
            {3,1,3,3,3,3,3,3,3,3,2,3,3,3,3,3},
            {3,1,3,3,3,3,3,3,3,3,3,2,3,3,3,3},
            {3,1,3,3,3,3,3,3,3,3,3,3,2,3,3,3},
            {3,1,3,3,3,3,3,3,3,3,3,3,3,2,3,3},
            {3,1,3,3,3,3,3,3,3,3,3,3,3,3,2,3},
            {3,1,3,3,3,3,3,3,3,3,3,3,3,3,3,2}
        };

        for (uint32_t pi = 1; pi < numPtsInChunk; ++pi) {
            ScContext& c = st.ctx[st.currentSc];
            uint32_t lLast = c.lastReturnByte & 0x0Fu;
            uint32_t nLast = (c.lastReturnByte >> 4) & 0x0Fu;
            uint32_t lastCtxIdx = ((nLast == 1u) ? 1u : 0u) | ((lLast == 1u) ? 2u : 0u) | ((lLast >= nLast) ? 4u : 0u);
            uint32_t cv = decXY.DecodeSymbol(c.mChangedValues[lastCtxIdx]);
            if (cv & 64u) {
                uint32_t dsc = decXY.DecodeSymbol(c.mScannerChannel);
                st.currentSc = (st.currentSc + dsc + 1u) & 3u;
                if (!scInited[st.currentSc]) {
                    scInited[st.currentSc] = true;
                    st.InitChannelFromSeed(st.currentSc, chunkPtr);
                }
            }
            ScContext& ca = st.ctx[st.currentSc];
            bool gpsTimeChange = (cv & 32u) != 0u;
            uint32_t r = ca.lastReturnByte & 0x0Fu;
            uint32_t n = (ca.lastReturnByte >> 4) & 0x0Fu;
            if (cv & 16u) {
                n = decXY.DecodeSymbol(ca.mNumberOfReturns[n]);
            }
            uint32_t rnMode = (cv >> 2) & 3u;
            if (rnMode == 1u) r = (r + 1u) & 0x0Fu;
            else if (rnMode == 2u) r = (r + 15u) & 0x0Fu;
            else if (rnMode == 3u) {
                if (gpsTimeChange) r = decXY.DecodeSymbol(ca.mReturnNumber[r]);
                else {
                    uint32_t sym = decXY.DecodeSymbol(ca.mReturnNumber[r]);
                    r = (r + sym + 2u) & 0x0Fu;
                }
            }
            ca.lastReturnByte = static_cast<uint8_t>((n << 4) | r);

            uint32_t m = kRetCtx14[n][r];
            uint32_t medCtx = (m << 1) | (gpsTimeChange ? 1u : 0u);
            int32_t dx = ca.icDx.Decompress(decXY, ca.lastXDiffMedian5[medCtx].Get(), (n == 1u) ? 1u : 0u);
            ca.lastX += dx;
            ca.lastXDiffMedian5[medCtx].Add(dx);

            uint32_t kBitsX = ca.icDx.GetK();
            uint32_t ctxY = ((n == 1u) ? 1u : 0u) + ((kBitsX < 20u) ? (kBitsX & ~1u) : 20u);
            int32_t dy = ca.icDy.Decompress(decXY, ca.lastYDiffMedian5[medCtx].Get(), ctxY);
            ca.lastY += dy;
            ca.lastYDiffMedian5[medCtx].Add(dy);

            int32_t curZ = ca.lastHeight[0];
            uint32_t l = std::min<uint32_t>(7u, static_cast<uint32_t>(std::abs(static_cast<int>(n) - static_cast<int>(r))));
            if (hasZLayer) {
                uint32_t kBits = (ca.icDx.GetK() + ca.icDy.GetK()) / 2u;
                uint32_t ctxZ = ((n == 1u) ? 1u : 0u) + ((kBits < 18u) ? (kBits & ~1u) : 18u);
                curZ = ca.icZ.Decompress(decZ, ca.lastHeight[l], ctxZ);
                ca.lastHeight[l] = curZ;
            } else {
                curZ = ca.lastHeight[l];
            }

            if (!decXY.Ok() || (hasZLayer && !decZ.Ok())) return false;
            if (((globalStartIdx + pi) % sampleStride) == 0 && outPts.size() < maxOutPts) {
                float fx = static_cast<float>(ca.lastX * scaleX + offX);
                float fy = static_cast<float>(ca.lastY * scaleY + offY);
                float fz = static_cast<float>(curZ * scaleZ + offZ);
                if (std::isfinite(fx) && std::isfinite(fy) && std::isfinite(fz)) {
                    outPts.push_back({ fx, fz, -fy });
                }
            }
        }
        return true;
    }
};

struct LazVlrItem {
    uint16_t type = 0;    // 6=POINT10, 7=GPSTIME11, 8=RGB12, 1=BYTE, 10=POINT14
    uint16_t size = 0;
    uint16_t version = 0;
};

struct LazVlrHeader {
    uint16_t compressor = 0; // 1=POINTWISE, 2=POINTWISE_CHUNKED, 3=LAYERED_CHUNKED
    uint16_t coder = 0;      // 0=ARITHMETIC
    uint32_t chunkSize = 50000;
    std::vector<LazVlrItem> items;
};

inline bool FindLaszipVlr(
    const uint8_t* data, size_t size,
    uint16_t headerSize, uint32_t numVlrs, uint8_t verMajor, uint8_t verMinor,
    LazVlrHeader& outVlr)
{
    auto parsePayload = [&](const uint8_t* pay, size_t payLen) -> bool {
        if (payLen < 34) return false;
        outVlr.compressor = RdU16Las(pay + 0);
        outVlr.coder      = RdU16Las(pay + 2);
        outVlr.chunkSize  = RdU32Las(pay + 12);
        uint16_t numItems = RdU16Las(pay + 32);
        if ((outVlr.compressor != 1u && outVlr.compressor != 2u && outVlr.compressor != 3u) ||
            outVlr.coder != 0u || numItems == 0u || 34u + static_cast<size_t>(numItems) * 6u > payLen) {
            return false;
        }
        outVlr.items.resize(numItems);
        for (uint16_t i = 0; i < numItems; ++i) {
            const uint8_t* ip = pay + 34 + static_cast<size_t>(i) * 6;
            outVlr.items[i].type    = RdU16Las(ip + 0);
            outVlr.items[i].size    = RdU16Las(ip + 2);
            outVlr.items[i].version = RdU16Las(ip + 4);
        }
        return true;
    };

    size_t vlrPos = headerSize;
    for (uint32_t v = 0; v < numVlrs; ++v) {
        if (vlrPos + 54 > size) break;
        uint16_t recId  = RdU16Las(data + vlrPos + 18);
        uint16_t recLen = RdU16Las(data + vlrPos + 20);
        if (vlrPos + 54 + recLen > size) break;
        if (recId == 22204u && std::memcmp(data + vlrPos + 2, "laszip encoded", 14) == 0) {
            return parsePayload(data + vlrPos + 54, recLen);
        }
        vlrPos += 54 + recLen;
    }

    // Check LAS 1.4 Extended VLRs if present
    if (verMajor == 1 && verMinor >= 4 && headerSize >= 375 && size >= 247) {
        uint64_t evlrOff = RdU64Las(data + 235);
        uint32_t numEvlr = RdU32Las(data + 243);
        for (uint32_t ev = 0; ev < numEvlr && evlrOff > 0 && evlrOff + 60 <= size; ++ev) {
            uint16_t recId  = RdU16Las(data + evlrOff + 18);
            uint64_t recLen = RdU64Las(data + evlrOff + 20);
            if (recLen > size - (evlrOff + 60)) break;
            if (recId == 22204u && std::memcmp(data + evlrOff + 2, "laszip encoded", 14) == 0) {
                return parsePayload(data + evlrOff + 60, static_cast<size_t>(recLen));
            }
            evlrOff += 60 + recLen;
        }
    }
    return false;
}

inline bool DecodeLazPointCloud(
    const uint8_t* data, size_t size,
    uint32_t offsetToPoints, uint64_t numPoints,
    double scaleX, double scaleY, double scaleZ,
    double offX, double offY, double offZ,
    const LazVlrHeader& vlr,
    std::vector<Vec3f>& outPts)
{
    if (numPoints == 0 || offsetToPoints >= size || vlr.items.empty()) return false;

    size_t seedRecordBytes = 0;
    for (const auto& it : vlr.items) seedRecordBytes += it.size;
    if (seedRecordBytes < 20) return false;

    constexpr size_t kMaxLazPts = 25000;
    const uint64_t sampleStride = std::max<uint64_t>(1, numPoints / kMaxLazPts);
    outPts.reserve(static_cast<size_t>(std::min<uint64_t>(numPoints, kMaxLazPts)));

    struct ChunkSpan {
        size_t byteOffset = 0;
        size_t byteLength = 0;
        uint64_t ptCount = 0;
    };
    std::vector<ChunkSpan> chunks;

    if (vlr.compressor == 1u) {
        // Non-chunked POINTWISE stream starts directly at offsetToPoints
        if (offsetToPoints + seedRecordBytes > size) return false;
        chunks.push_back({ offsetToPoints, size - offsetToPoints, numPoints });
    } else {
        // Chunked stream (compressor == 2 or 3): int64_t chunkTableStart at offsetToPoints
        if (offsetToPoints + 8 > size) return false;
        int64_t chunkTableStart = RdI64Las(data + offsetToPoints);
        size_t dataStart = offsetToPoints + 8;

        if (chunkTableStart < static_cast<int64_t>(dataStart) ||
            static_cast<uint64_t>(chunkTableStart) + 8ULL > size) {
            return false;
        }
        size_t ctOff = static_cast<size_t>(chunkTableStart);
        uint32_t ctVer = RdU32Las(data + ctOff);
        uint32_t numChunks = RdU32Las(data + ctOff + 4);
        if (ctVer != 0u || numChunks == 0u || numChunks > 10000000u) return false;

        LazArithmeticDecoder ctDec;
        if (!ctDec.Init(data + ctOff + 8, data + size)) return false;
        LazIntegerCompressor ctIc;
        ctIc.Setup(32, 2);
        ctIc.InitDecompressor();

        const bool adaptiveChunks = (vlr.chunkSize == 0u || vlr.chunkSize == 0xFFFFFFFFu);
        int32_t prevCnt = 0, prevBytes = 0;
        size_t curByteOff = dataStart;
        uint64_t remainingPts = numPoints;

        chunks.reserve(std::min<uint32_t>(numChunks, 65536u));
        for (uint32_t ci = 0; ci < numChunks && remainingPts > 0; ++ci) {
            uint64_t cPts = 0;
            if (adaptiveChunks) {
                prevCnt = ctIc.Decompress(ctDec, prevCnt, 0);
                if (prevCnt <= 0) return false;
                cPts = std::min<uint64_t>(remainingPts, static_cast<uint64_t>(prevCnt));
            } else {
                cPts = std::min<uint64_t>(remainingPts, static_cast<uint64_t>(vlr.chunkSize));
            }
            prevBytes = ctIc.Decompress(ctDec, prevBytes, 1);
            if (!ctDec.Ok() || prevBytes < static_cast<int32_t>(seedRecordBytes)) return false;
            size_t cBytes = static_cast<size_t>(prevBytes);
            if (curByteOff + cBytes > ctOff) return false;
            chunks.push_back({ curByteOff, cBytes, cPts });
            curByteOff += cBytes;
            remainingPts -= cPts;
        }
        if (chunks.empty()) return false;
    }

    const uint16_t baseItemType = vlr.items[0].type;
    const uint16_t baseItemVer  = vlr.items[0].version;

    if (vlr.compressor == 3u) {
        if (baseItemType != 10u || vlr.items[0].size < 30u) return false;
        uint64_t globalPtIdx = 0;
        for (const auto& ch : chunks) {
            if (ch.ptCount > 0xFFFFFFFFULL) return false;
            if (!LazItemPoint14v3::DecodeChunkPoints(
                    data + ch.byteOffset, ch.byteLength, seedRecordBytes, static_cast<uint32_t>(ch.ptCount),
                    scaleX, scaleY, scaleZ, offX, offY, offZ,
                    globalPtIdx, sampleStride, kMaxLazPts, outPts)) {
                return false;
            }
            globalPtIdx += ch.ptCount;
            if (outPts.size() >= kMaxLazPts) break;
        }
        return !outPts.empty();
    }

    // compressor == 1 or 2: POINT10 (type == 6, size == 20, version == 1 or 2)
    if (baseItemType != 6u || vlr.items[0].size != 20u || (baseItemVer != 1u && baseItemVer != 2u)) {
        return false;
    }
    // Verify any extra items are supported v2 items (when >1 pt per chunk) so arithmetic decoder stays aligned
    for (size_t ii = 1; ii < vlr.items.size(); ++ii) {
        uint16_t t = vlr.items[ii].type;
        uint16_t v = vlr.items[ii].version;
        if (!((t == 7u && vlr.items[ii].size == 8u && v == 2u) ||
              (t == 8u && vlr.items[ii].size == 6u && v == 2u) ||
              (t == 1u && vlr.items[ii].size >= 1u && v == 2u))) {
            return false;
        }
    }

    LazItemPoint10v2 pt10v2;
    if (baseItemVer == 2u) pt10v2.Construct();
    LazItemPoint10v1 pt10v1;

    uint64_t globalPtIdx = 0;
    for (const auto& ch : chunks) {
        if (ch.byteLength < seedRecordBytes || ch.ptCount == 0) return false;
        const uint8_t* cPtr = data + ch.byteOffset;
        const uint8_t* const cEnd = cPtr + ch.byteLength;

        int32_t sx = RdI32Las(cPtr + 0);
        int32_t sy = RdI32Las(cPtr + 4);
        int32_t sz = RdI32Las(cPtr + 8);
        if ((globalPtIdx % sampleStride) == 0 && outPts.size() < kMaxLazPts) {
            float fx = static_cast<float>(sx * scaleX + offX);
            float fy = static_cast<float>(sy * scaleY + offY);
            float fz = static_cast<float>(sz * scaleZ + offZ);
            if (std::isfinite(fx) && std::isfinite(fy) && std::isfinite(fz)) {
                outPts.push_back({ fx, fz, -fy });
            }
        }

        if (ch.ptCount > 1) {
            LazArithmeticDecoder dec;
            if (!dec.Init(cPtr + seedRecordBytes, cEnd)) return false;

            if (baseItemVer == 2u) pt10v2.InitSeed(cPtr);
            else                   pt10v1.InitSeed(cPtr);

            LazItemGpsTime11v2 gpsSub;
            LazItemRgb12v2 rgbSub;
            LazItemBytev2 byteSub;
            bool hasGps = false, hasRgb = false, hasByte = false;
            size_t subOff = 20;
            for (size_t ii = 1; ii < vlr.items.size(); ++ii) {
                const auto& it = vlr.items[ii];
                if (it.type == 7u) { gpsSub.InitSeed(cPtr + subOff); hasGps = true; }
                else if (it.type == 8u) { rgbSub.InitSeed(cPtr + subOff); hasRgb = true; }
                else if (it.type == 1u) { byteSub.InitSeed(cPtr + subOff, it.size); hasByte = true; }
                subOff += it.size;
            }

            for (uint64_t pi = 1; pi < ch.ptCount; ++pi) {
                int32_t ix = 0, iy = 0, iz = 0;
                if (baseItemVer == 2u) pt10v2.Read(dec, ix, iy, iz);
                else                   pt10v1.Read(dec, ix, iy, iz);

                for (size_t ii = 1; ii < vlr.items.size(); ++ii) {
                    uint16_t t = vlr.items[ii].type;
                    if (t == 7u && hasGps) gpsSub.Read(dec);
                    else if (t == 8u && hasRgb) rgbSub.Read(dec);
                    else if (t == 1u && hasByte) byteSub.Read(dec);
                }

                if (!dec.Ok()) return false;
                if (((globalPtIdx + pi) % sampleStride) == 0 && outPts.size() < kMaxLazPts) {
                    float fx = static_cast<float>(ix * scaleX + offX);
                    float fy = static_cast<float>(iy * scaleY + offY);
                    float fz = static_cast<float>(iz * scaleZ + offZ);
                    if (std::isfinite(fx) && std::isfinite(fy) && std::isfinite(fz)) {
                        outPts.push_back({ fx, fz, -fy });
                    }
                }
            }
        }
        globalPtIdx += ch.ptCount;
        if (outPts.size() >= kMaxLazPts) break;
    }

    return !outPts.empty();
}

} // namespace DetailLas

inline bool ParseLasLidarMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 227 || std::memcmp(data, "LASF", 4) != 0) return false;

    const uint8_t verMajor = data[24];
    const uint8_t verMinor = data[25];
    if (verMajor != 1 || verMinor > 4) return false;

    const uint16_t headerSize     = DetailLas::RdU16Las(data + 94);
    const uint32_t offsetToPoints = DetailLas::RdU32Las(data + 96);
    const uint32_t numVlrs        = DetailLas::RdU32Las(data + 100);
    const uint8_t rawPtFmt        = data[104];
    const uint16_t recLen         = DetailLas::RdU16Las(data + 105);
    uint64_t numPoints            = DetailLas::RdU32Las(data + 107);

    if (headerSize < 227 || headerSize > size || offsetToPoints < headerSize || offsetToPoints > size) {
        return false;
    }

    // LAS 1.4 Extended 64-bit Number of Point Records at byte offset 247!
    if (verMajor == 1 && verMinor >= 4 && size >= 255 && (numPoints == 0 || headerSize >= 375)) {
        uint64_t extPts = DetailLas::RdU64Las(data + 247);
        if (numPoints == 0 || extPts > numPoints) {
            numPoints = extPts;
        }
    }

    const bool isLazCompressed = (rawPtFmt & 0x80u) != 0u;
    const uint8_t baseFmt = rawPtFmt & 0x3Fu;
    if (baseFmt > 10u || numPoints == 0 || recLen < 20u) {
        return false;
    }

    double scaleX = DetailLas::RdF64Las(data + 131);
    double scaleY = DetailLas::RdF64Las(data + 139);
    double scaleZ = DetailLas::RdF64Las(data + 147);
    const double offX = DetailLas::RdF64Las(data + 155);
    const double offY = DetailLas::RdF64Las(data + 163);
    const double offZ = DetailLas::RdF64Las(data + 171);
    if (scaleX == 0.0 || !std::isfinite(scaleX)) scaleX = 0.01;
    if (scaleY == 0.0 || !std::isfinite(scaleY)) scaleY = 0.01;
    if (scaleZ == 0.0 || !std::isfinite(scaleZ)) scaleZ = 0.01;

    std::vector<Vec3f> pts;
    if (isLazCompressed) {
        DetailLas::LazVlrHeader vlr;
        if (!DetailLas::FindLaszipVlr(data, size, headerSize, numVlrs, verMajor, verMinor, vlr)) {
            return false;
        }
        if (!DetailLas::DecodeLazPointCloud(
                data, size, offsetToPoints, numPoints,
                scaleX, scaleY, scaleZ, offX, offY, offZ, vlr, pts)) {
            return false;
        }
        mesh.formatName = L"LASzip v" + std::to_wstring(verMajor) + L"." + std::to_wstring(verMinor) +
                          L" Compressed LiDAR (.laz)";
    } else {
        if (offsetToPoints >= size || size - offsetToPoints < recLen) return false;
        const uint64_t availPts = std::min<uint64_t>(numPoints, (size - offsetToPoints) / recLen);
        if (availPts == 0) return false;

        constexpr size_t kMaxLasPts = 25000;
        const uint64_t step = std::max<uint64_t>(1, availPts / kMaxLasPts);
        pts.reserve(static_cast<size_t>(std::min<uint64_t>(availPts, kMaxLasPts)));
        const int rgbOff = DetailLas::LasRgbByteOffset(baseFmt);

        for (uint64_t i = 0; i < availPts && pts.size() < kMaxLasPts; i += step) {
            const uint8_t* rp = data + offsetToPoints + static_cast<size_t>(i * recLen);
            int32_t ix = DetailLas::RdI32Las(rp + 0);
            int32_t iy = DetailLas::RdI32Las(rp + 4);
            int32_t iz = DetailLas::RdI32Las(rp + 8);
            if (rgbOff > 0 && static_cast<size_t>(rgbOff + 6) <= recLen) {
                (void)DetailLas::RdU16Las(rp + rgbOff);
            }
            float x = static_cast<float>(ix * scaleX + offX);
            float y = static_cast<float>(iy * scaleY + offY);
            float z = static_cast<float>(iz * scaleZ + offZ);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                pts.push_back({ x, z, -y });
            }
        }
        mesh.formatName = L"ASPRS LAS v" + std::to_wstring(verMajor) + L"." + std::to_wstring(verMinor) +
                          L" LiDAR (.las)";
    }

    if (pts.empty()) return false;

    Vec3f bmin = pts[0], bmax = pts[0];
    for (const auto& p : pts) {
        bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
        bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
    }
    float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
    float r = std::max(0.015f, (span > 1e-5f) ? (span * 0.018f) : 0.05f);

    mesh.vertices.reserve(pts.size() * 4);
    mesh.tris.reserve(pts.size() * 4);
    for (const auto& p : pts) {
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
        mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
        mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
        mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
        mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
    }
    return !mesh.tris.empty();
}

} // namespace Formats
