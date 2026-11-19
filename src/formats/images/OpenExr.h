#pragma once
// OpenExr.h — OpenEXR v1 & Multi-Part v2 HDR Image Decoder (.exr)

namespace Formats {

namespace ExrDetail {

inline void UndoExrBytePredictorAndInterleave(std::vector<uint8_t>& temp, std::vector<uint8_t>& rawOut) {
    size_t n = temp.size();
    for (size_t i = 1; i < n; ++i) {
        temp[i] = static_cast<uint8_t>(static_cast<int>(temp[i - 1]) + static_cast<int>(temp[i]) - 128);
    }
    rawOut.resize(n);
    size_t half = (n + 1) / 2;
    for (size_t j = 0; j < n / 2; ++j) {
        rawOut[2 * j]     = temp[j];
        rawOut[2 * j + 1] = temp[half + j];
    }
    if ((n & 1u) != 0) {
        rawOut[n - 1] = temp[half - 1];
    }
}

inline bool ExrRleDecompressRaw(const uint8_t* src, size_t srcLen, size_t expectedLen, std::vector<uint8_t>& dst) {
    dst.clear();
    dst.reserve(expectedLen);
    size_t sp = 0;
    while (sp < srcLen && dst.size() < expectedLen) {
        int count = static_cast<int>(static_cast<int8_t>(src[sp++]));
        if (count < 0) {
            size_t lit = static_cast<size_t>(-count);
            if (sp + lit > srcLen || dst.size() + lit > expectedLen) return false;
            dst.insert(dst.end(), src + sp, src + sp + lit);
            sp += lit;
        } else {
            size_t rep = static_cast<size_t>(count) + 1u;
            if (sp >= srcLen || dst.size() + rep > expectedLen) return false;
            uint8_t val = src[sp++];
            dst.insert(dst.end(), rep, val);
        }
    }
    return dst.size() == expectedLen;
}

// ============================================================================
// OpenEXR ImfHuf 65537-symbol Canonical Huffman Decoder (used by PIZ & DWAA/DWAB)
// ============================================================================
inline bool ExrHufUncompress(const uint8_t* comp, size_t compSize, uint16_t* outRaw, size_t nRaw) {
    if (nRaw == 0) return true;
    if (!comp || compSize < 20) return false;

    uint32_t im      = DetailImg100::RdU32LE(comp + 0);
    uint32_t iM      = DetailImg100::RdU32LE(comp + 4);
    uint32_t nBits   = DetailImg100::RdU32LE(comp + 12);
    uint32_t future  = DetailImg100::RdU32LE(comp + 16);
    if (im > 65536u || iM > 65536u || im > iM || future != 0) return false;

    constexpr uint32_t kHufEncSize = 65537u;
    std::vector<uint8_t> freqLen(kHufEncSize, 0);

    const uint8_t* tblPtr = comp + 20;
    size_t tblAvail = compSize - 20;
    uint64_t bitBuf = 0;
    int bitCnt = 0;
    size_t byteIdx = 0;

    auto getTblBits = [&](int n) -> int {
        while (bitCnt < n) {
            if (byteIdx >= tblAvail) return -1;
            bitBuf = (bitBuf << 8) | tblPtr[byteIdx++];
            bitCnt += 8;
        }
        bitCnt -= n;
        return static_cast<int>((bitBuf >> bitCnt) & ((1ULL << n) - 1ULL));
    };

    for (uint32_t sym = im; sym <= iM; ) {
        int l = getTblBits(6);
        if (l < 0) return false;
        if (l == 63) { // LONG_ZEROCODE_RUN
            int zerun = getTblBits(8);
            if (zerun < 0) return false;
            zerun += 6;
            if (sym + static_cast<uint32_t>(zerun) > iM + 1u) return false;
            while (zerun-- > 0) freqLen[sym++] = 0;
        } else if (l >= 59) { // SHORT_ZEROCODE_RUN (59..62 -> 2..5 zeros)
            int zerun = l - 59 + 2;
            if (sym + static_cast<uint32_t>(zerun) > iM + 1u) return false;
            while (zerun-- > 0) freqLen[sym++] = 0;
        } else {
            freqLen[sym++] = static_cast<uint8_t>(l);
        }
    }

    size_t dataOffset = 20 + byteIdx;
    if (dataOffset > compSize) return false;
    const uint8_t* bitStream = comp + dataOffset;
    size_t bitStreamBytes = compSize - dataOffset;
    if ((static_cast<uint64_t>(nBits) + 7ULL) / 8ULL > bitStreamBytes) return false;

    // Build canonical Huffman codes per ImfHuf.cpp: longer codes start at 0, shorter codes shift right
    uint64_t lenCounts[59] = {};
    int maxCodeLen = 0;
    for (uint32_t i = 0; i < kHufEncSize; ++i) {
        uint8_t l = freqLen[i];
        if (l > 58) return false;
        lenCounts[l]++;
        if (l > maxCodeLen) maxCodeLen = l;
    }
    if (maxCodeLen == 0) return false;

    uint64_t baseCode[59] = {};
    uint64_t c = 0;
    for (int i = 58; i > 0; --i) {
        uint64_t nc = (c + lenCounts[i]) >> 1;
        baseCode[i] = c;
        c = nc;
    }
    if (c != 1ULL) {
        // Single-symbol degenerate tree or slightly incomplete check: ImfHuf ensures c == 1 when valid
        if (c > 1ULL) return false;
    }

    // Build binary trie for fast variable-length 1..58 bit decoding (at most 2 * 65537 nodes)
    struct TrieNode {
        int32_t child[2] = { -1, -1 };
        int32_t sym = -1;
    };
    std::vector<TrieNode> trie(1);
    trie.reserve(4096);
    uint64_t nextCode[59];
    std::memcpy(nextCode, baseCode, sizeof(baseCode));

    for (uint32_t s = im; s <= iM; ++s) {
        uint8_t l = freqLen[s];
        if (l == 0) continue;
        uint64_t code = nextCode[l]++;
        int32_t cur = 0;
        for (int b = l - 1; b >= 0; --b) {
            uint32_t bit = static_cast<uint32_t>((code >> b) & 1ULL);
            int32_t nxt = trie[cur].child[bit];
            if (nxt < 0) {
                nxt = static_cast<int32_t>(trie.size());
                trie.emplace_back();
                trie[cur].child[bit] = nxt;
            }
            cur = nxt;
        }
        trie[cur].sym = static_cast<int32_t>(s);
    }

    // Decode nBits bits from bitStream
    size_t outIdx = 0;
    uint32_t bitPos = 0;
    int32_t node = 0;
    while (bitPos < nBits && outIdx < nRaw) {
        uint32_t bit = (bitStream[bitPos >> 3] >> (7u - (bitPos & 7u))) & 1u;
        ++bitPos;
        node = trie[node].child[bit];
        if (node < 0) return false;
        int32_t sym = trie[node].sym;
        if (sym >= 0) {
            node = 0;
            if (static_cast<uint32_t>(sym) == iM) {
                // 8-bit RLE repeat count of previous symbol
                if (bitPos + 8u > nBits || outIdx == 0) return false;
                uint32_t rep = 0;
                for (int k = 0; k < 8; ++k, ++bitPos) {
                    rep = (rep << 1) | ((bitStream[bitPos >> 3] >> (7u - (bitPos & 7u))) & 1u);
                }
                if (outIdx + rep > nRaw) return false;
                uint16_t prev = outRaw[outIdx - 1];
                for (uint32_t r = 0; r < rep; ++r) outRaw[outIdx++] = prev;
            } else {
                outRaw[outIdx++] = static_cast<uint16_t>(sym);
            }
        }
    }
    return outIdx == nRaw;
}

// ============================================================================
// OpenEXR ImfWav 2D Inverse Haar/PIZ Wavelet (wdec14 & wdec16)
// ============================================================================
inline void ExrWdec14(uint16_t l, uint16_t h, uint16_t& a, uint16_t& b) {
    int16_t as = static_cast<int16_t>(l);
    int16_t bs = static_cast<int16_t>(h);
    int ai = static_cast<int>(as) + (static_cast<int>(bs) & 1) + (static_cast<int>(bs) >> 1);
    int bi = ai - static_cast<int>(bs);
    a = static_cast<uint16_t>(static_cast<int16_t>(ai));
    b = static_cast<uint16_t>(static_cast<int16_t>(bi));
}

inline void ExrWdec16(uint16_t l, uint16_t h, uint16_t& a, uint16_t& b) {
    constexpr int kAOffset = 1 << 13;
    constexpr int kMOffset = 1 << 15;
    constexpr int kModMask = (1 << 16) - 1;
    int m = static_cast<int>(l);
    int d = static_cast<int>(h);
    int bb = (m - (d >> 1) + kAOffset) & kModMask;
    int aa = (d + bb - kMOffset) & kModMask;
    b = static_cast<uint16_t>(bb);
    a = static_cast<uint16_t>(aa);
}

inline void ExrWav2Decode(uint16_t* in, int nx, int ox, int ny, int oy, uint16_t mx) {
    bool w14 = (mx < (1u << 14));
    int n = (nx > ny) ? ny : nx;
    int p = 1;
    while (p <= n) p <<= 1;
    p >>= 1;
    int p2 = p;
    p >>= 1;

    while (p >= 1) {
        uint16_t* py = in;
        uint16_t* ey = in + oy * (ny - p2);
        int oy1 = oy * p, oy2 = oy * p2;
        int ox1 = ox * p, ox2 = ox * p2;

        for (; py <= ey; py += oy2) {
            uint16_t* px = py;
            uint16_t* ex = py + ox * (nx - p2);
            for (; px <= ex; px += ox2) {
                uint16_t* p01 = px + ox1;
                uint16_t* p10 = px + oy1;
                uint16_t* p11 = p10 + ox1;
                uint16_t i00, i01, i10, i11;
                if (w14) {
                    ExrWdec14(*px,  *p10, i00, i10);
                    ExrWdec14(*p01, *p11, i01, i11);
                    ExrWdec14(i00, i01, *px,  *p01);
                    ExrWdec14(i10, i11, *p10, *p11);
                } else {
                    ExrWdec16(*px,  *p10, i00, i10);
                    ExrWdec16(*p01, *p11, i01, i11);
                    ExrWdec16(i00, i01, *px,  *p01);
                    ExrWdec16(i10, i11, *p10, *p11);
                }
            }
            if (nx & p) {
                uint16_t* p10 = px + oy1;
                if (w14) ExrWdec14(*px, *p10, *px, *p10);
                else     ExrWdec16(*px, *p10, *px, *p10);
            }
        }
        if (ny & p) {
            uint16_t* px = py;
            uint16_t* ex = py + ox * (nx - p2);
            for (; px <= ex; px += ox2) {
                uint16_t* p01 = px + ox1;
                if (w14) ExrWdec14(*px, *p01, *px, *p01);
                else     ExrWdec16(*px, *p01, *px, *p01);
            }
        }
        p2 = p;
        p >>= 1;
    }
}

// ============================================================================
// OpenEXR B44 / B44A 4x4 HALF Block Unpackers (ImfB44Compressor.cpp)
// ============================================================================
inline uint16_t ExrB44BiasedToHalf(uint16_t t) {
    return (t & 0x8000u) ? static_cast<uint16_t>(t & 0x7FFFu) : static_cast<uint16_t>(~t);
}

inline void ExrB44Unpack14(const uint8_t b[14], uint16_t s[16]) {
    s[0] = static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
    uint32_t shift = static_cast<uint32_t>(b[2] >> 2);
    if (shift > 15) shift = 15;
    uint32_t bias = 0x20u << shift;
    auto step = [&](uint16_t base, uint32_t sixBit) -> uint16_t {
        return static_cast<uint16_t>(static_cast<uint32_t>(base) + ((sixBit & 0x3Fu) << shift) - bias);
    };

    s[ 4] = step(s[ 0], (static_cast<uint32_t>(b[ 2]) << 4) | (b[ 3] >> 4));
    s[ 8] = step(s[ 4], (static_cast<uint32_t>(b[ 3]) << 2) | (b[ 4] >> 6));
    s[12] = step(s[ 8], b[ 4]);

    s[ 1] = step(s[ 0], b[ 5] >> 2);
    s[ 5] = step(s[ 4], (static_cast<uint32_t>(b[ 5]) << 4) | (b[ 6] >> 4));
    s[ 9] = step(s[ 8], (static_cast<uint32_t>(b[ 6]) << 2) | (b[ 7] >> 6));
    s[13] = step(s[12], b[ 7]);

    s[ 2] = step(s[ 1], b[ 8] >> 2);
    s[ 6] = step(s[ 5], (static_cast<uint32_t>(b[ 8]) << 4) | (b[ 9] >> 4));
    s[10] = step(s[ 9], (static_cast<uint32_t>(b[ 9]) << 2) | (b[10] >> 6));
    s[14] = step(s[13], b[10]);

    s[ 3] = step(s[ 2], b[11] >> 2);
    s[ 7] = step(s[ 6], (static_cast<uint32_t>(b[11]) << 4) | (b[12] >> 4));
    s[11] = step(s[10], (static_cast<uint32_t>(b[12]) << 2) | (b[13] >> 6));
    s[15] = step(s[14], b[13]);

    for (int i = 0; i < 16; ++i) s[i] = ExrB44BiasedToHalf(s[i]);
}

inline void ExrB44Unpack3(const uint8_t b[3], uint16_t s[16]) {
    uint16_t h = ExrB44BiasedToHalf(static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]));
    for (int i = 0; i < 16; ++i) s[i] = h;
}

// ============================================================================
// OpenEXR DWAA / DWAB 8x8 Inverse Orthonormal DCT
// ============================================================================
inline void ExrDwaIdct8x8(const float coeff[64], float outPix[64]) {
    static const float kCos[8][8] = {
        { 0.35355339f,  0.49039264f,  0.46193977f,  0.41573481f,  0.35355339f,  0.27778512f,  0.19134172f,  0.09754516f },
        { 0.35355339f,  0.41573481f,  0.19134172f, -0.09754516f, -0.35355339f, -0.49039264f, -0.46193977f, -0.27778512f },
        { 0.35355339f,  0.27778512f, -0.19134172f, -0.49039264f, -0.35355339f,  0.09754516f,  0.46193977f,  0.41573481f },
        { 0.35355339f,  0.09754516f, -0.46193977f, -0.27778512f,  0.35355339f,  0.41573481f, -0.19134172f, -0.49039264f },
        { 0.35355339f, -0.09754516f, -0.46193977f,  0.27778512f,  0.35355339f, -0.41573481f, -0.19134172f,  0.49039264f },
        { 0.35355339f, -0.27778512f, -0.19134172f,  0.49039264f, -0.35355339f, -0.09754516f,  0.46193977f, -0.41573481f },
        { 0.35355339f, -0.41573481f,  0.19134172f,  0.09754516f, -0.35355339f,  0.49039264f, -0.46193977f,  0.27778512f },
        { 0.35355339f, -0.49039264f,  0.46193977f, -0.41573481f,  0.35355339f, -0.27778512f,  0.19134172f, -0.09754516f }
    };
    float tmp[64];
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k) sum += coeff[y * 8 + k] * kCos[x][k];
            tmp[y * 8 + x] = sum;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k) sum += tmp[k * 8 + x] * kCos[y][k];
            outPix[y * 8 + x] = sum;
        }
    }
}

struct ExrChannelDesc {
    std::string name;
    int32_t pixelType = 1; // 0 = UINT, 1 = HALF, 2 = FLOAT
    int32_t xSampling = 1;
    int32_t ySampling = 1;
    uint32_t bytesPerSample = 2;
};

struct ExrPartDesc {
    std::string partName;
    std::string partType;
    int32_t minX = 0, minY = 0, maxX = -1, maxY = -1;
    bool hasDataWindow = false;
    bool hasDisplayWindow = false;
    bool isTiled = false;
    uint8_t compression = 0;
    uint32_t tileDescX = 64, tileDescY = 64;
    uint32_t chunkCount = 0;
    bool hasChunkCount = false;
    std::vector<ExrChannelDesc> channels;
};

inline uint32_t ExrLinesPerScanlineBlock(uint8_t compression) {
    switch (compression) {
        case 0: case 1: case 2: return 1u;
        case 3: case 5:         return 16u;
        case 4: case 6: case 7: case 8: return 32u;
        case 9:                 return 256u;
        default:                return 1u;
    }
}

// Decompresses any OpenEXR chunk (compressions 0..9) into standard scanline-interleaved `rawOut` bytes!
inline bool DecompressExrChunkAny(
    uint8_t compression,
    const uint8_t* packed,
    size_t packedSize,
    size_t expectedRawSize,
    uint32_t blockW,
    uint32_t blockH,
    int32_t blockAbsMinY,
    const std::vector<ExrChannelDesc>& channels,
    std::vector<uint8_t>& rawOut)
{
    if (expectedRawSize == 0) {
        rawOut.clear();
        return true;
    }
    if (!packed) return false;

    // 0: NO_COMPRESSION
    if (compression == 0) {
        if (packedSize < expectedRawSize) return false;
        rawOut.assign(packed, packed + expectedRawSize);
        return true;
    }

    // Compute per-channel row counts and sample widths in this block
    struct ChSlice {
        uint32_t nx = 0;
        uint32_t ny = 0;
    };
    std::vector<ChSlice> slices(channels.size());
    for (size_t ci = 0; ci < channels.size(); ++ci) {
        const auto& ch = channels[ci];
        slices[ci].nx = (blockW + static_cast<uint32_t>(ch.xSampling) - 1u) / static_cast<uint32_t>(ch.xSampling);
        uint32_t rows = 0;
        for (uint32_t ly = 0; ly < blockH; ++ly) {
            if (((blockAbsMinY + static_cast<int32_t>(ly)) % ch.ySampling) == 0) ++rows;
        }
        slices[ci].ny = rows;
    }

    // 1: RLE_COMPRESSION
    if (compression == 1) {
        std::vector<uint8_t> temp;
        if (packedSize != expectedRawSize && ExrRleDecompressRaw(packed, packedSize, expectedRawSize, temp)) {
            UndoExrBytePredictorAndInterleave(temp, rawOut);
            return true;
        }
        if (packedSize == expectedRawSize) {
            rawOut.assign(packed, packed + expectedRawSize);
            return true;
        }
        return false;
    }

    // 2, 3: ZIPS / ZIP
    if (compression == 2 || compression == 3) {
        std::vector<uint8_t> temp;
        if (ZipDeflate::InflateZlib(packed, packedSize, temp, expectedRawSize + 64) && temp.size() == expectedRawSize) {
            UndoExrBytePredictorAndInterleave(temp, rawOut);
            return true;
        }
        if (packedSize == expectedRawSize) {
            rawOut.assign(packed, packed + expectedRawSize);
            return true;
        }
        return false;
    }

    // 4: PIZ_COMPRESSION (Wavelet + Huffman + LUT)
    if (compression == 4) {
        if ((expectedRawSize & 1u) != 0) return false;
        if (packedSize == expectedRawSize) {
            rawOut.assign(packed, packed + expectedRawSize);
            return true;
        }
        if (packedSize < 8) return false;
        uint16_t minNonZero = DetailImg100::RdU16LE(packed + 0);
        uint16_t maxNonZero = DetailImg100::RdU16LE(packed + 2);
        uint8_t bitmap[8192] = {};
        size_t hp = 4;
        if (minNonZero <= maxNonZero) {
            if (maxNonZero >= 8192u) return false;
            size_t bmBytes = static_cast<size_t>(maxNonZero - minNonZero) + 1u;
            if (hp + bmBytes + 4 > packedSize) return false;
            std::memcpy(bitmap + minNonZero, packed + hp, bmBytes);
            hp += bmBytes;
        }
        if (hp + 4 > packedSize) return false;
        uint32_t hufLen = DetailImg100::RdU32LE(packed + hp);
        hp += 4;
        if (hufLen > packedSize - hp) return false;

        std::vector<uint16_t> lut(65536, 0);
        int k = 0;
        for (int i = 0; i < 65536; ++i) {
            if (i == 0 || ((bitmap[i >> 3] & (1u << (i & 7))) != 0)) {
                lut[k++] = static_cast<uint16_t>(i);
            }
        }
        uint16_t maxValue = (k > 0) ? static_cast<uint16_t>(k - 1) : 0;

        size_t nU16 = expectedRawSize / 2;
        std::vector<uint16_t> tmpU16(nU16, 0);
        if (!ExrHufUncompress(packed + hp, hufLen, tmpU16.data(), nU16)) return false;

        // Channel-planar 2D inverse wavelet + LUT
        std::vector<size_t> chBase(channels.size(), 0);
        size_t u16Cursor = 0;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            chBase[ci] = u16Cursor;
            int u16PerSample = static_cast<int>(channels[ci].bytesPerSample / 2);
            int nx = static_cast<int>(slices[ci].nx);
            int ny = static_cast<int>(slices[ci].ny);
            size_t chCount = static_cast<size_t>(nx) * ny * u16PerSample;
            if (u16Cursor + chCount > nU16) return false;
            if (nx > 0 && ny > 0) {
                for (int j = 0; j < u16PerSample; ++j) {
                    ExrWav2Decode(tmpU16.data() + u16Cursor + j, nx, u16PerSample, ny, nx * u16PerSample, maxValue);
                }
            }
            u16Cursor += chCount;
        }
        for (size_t i = 0; i < nU16; ++i) {
            tmpU16[i] = lut[tmpU16[i]];
        }

        // Convert channel-planar tmpU16 -> scanline-interleaved little-endian rawOut
        rawOut.resize(expectedRawSize);
        size_t dstOff = 0;
        std::vector<uint32_t> chRowIdx(channels.size(), 0);
        for (uint32_t ly = 0; ly < blockH; ++ly) {
            int32_t absY = blockAbsMinY + static_cast<int32_t>(ly);
            for (size_t ci = 0; ci < channels.size(); ++ci) {
                const auto& ch = channels[ci];
                if ((absY % ch.ySampling) != 0) continue;
                uint32_t rIdx = chRowIdx[ci]++;
                uint32_t u16PerRow = slices[ci].nx * (ch.bytesPerSample / 2);
                const uint16_t* srcRow = tmpU16.data() + chBase[ci] + static_cast<size_t>(rIdx) * u16PerRow;
                for (uint32_t u = 0; u < u16PerRow; ++u) {
                    uint16_t v = srcRow[u];
                    rawOut[dstOff++] = static_cast<uint8_t>(v & 0xFFu);
                    rawOut[dstOff++] = static_cast<uint8_t>(v >> 8);
                }
            }
        }
        return dstOff == expectedRawSize;
    }

    // 5: PXR24_COMPRESSION (Pixar 24-bit float + byte-transposed delta + Zlib)
    if (compression == 5) {
        size_t expectedPxrBytes = 0;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            size_t pxrBps = (channels[ci].pixelType == 2) ? 3u : channels[ci].bytesPerSample;
            expectedPxrBytes += static_cast<size_t>(slices[ci].nx) * slices[ci].ny * pxrBps;
        }
        std::vector<uint8_t> inf;
        if (ZipDeflate::InflateZlib(packed, packedSize, inf, expectedPxrBytes + 64) && inf.size() == expectedPxrBytes) {
            rawOut.resize(expectedRawSize);
            size_t sp = 0, dp = 0;
            for (uint32_t ly = 0; ly < blockH; ++ly) {
                int32_t absY = blockAbsMinY + static_cast<int32_t>(ly);
                for (size_t ci = 0; ci < channels.size(); ++ci) {
                    const auto& ch = channels[ci];
                    if ((absY % ch.ySampling) != 0) continue;
                    uint32_t n = slices[ci].nx;
                    if (ch.pixelType == 1) { // HALF (2 planes: MSB, LSB)
                        const uint8_t* p0 = inf.data() + sp;
                        const uint8_t* p1 = p0 + n;
                        sp += 2u * n;
                        uint32_t acc0 = 0, acc1 = 0;
                        for (uint32_t x = 0; x < n; ++x) {
                            acc0 += p0[x]; acc1 += p1[x];
                            rawOut[dp++] = static_cast<uint8_t>(acc1);
                            rawOut[dp++] = static_cast<uint8_t>(acc0);
                        }
                    } else if (ch.pixelType == 2) { // FLOAT (3 planes: bits 31..24, 23..16, 15..8; bits 7..0 = 0)
                        const uint8_t* p0 = inf.data() + sp;
                        const uint8_t* p1 = p0 + n;
                        const uint8_t* p2 = p1 + n;
                        sp += 3u * n;
                        uint32_t acc0 = 0, acc1 = 0, acc2 = 0;
                        for (uint32_t x = 0; x < n; ++x) {
                            acc0 += p0[x]; acc1 += p1[x]; acc2 += p2[x];
                            rawOut[dp++] = 0;
                            rawOut[dp++] = static_cast<uint8_t>(acc2);
                            rawOut[dp++] = static_cast<uint8_t>(acc1);
                            rawOut[dp++] = static_cast<uint8_t>(acc0);
                        }
                    } else { // UINT (4 planes: MSB..LSB)
                        const uint8_t* p0 = inf.data() + sp;
                        const uint8_t* p1 = p0 + n;
                        const uint8_t* p2 = p1 + n;
                        const uint8_t* p3 = p2 + n;
                        sp += 4u * n;
                        uint32_t acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;
                        for (uint32_t x = 0; x < n; ++x) {
                            acc0 += p0[x]; acc1 += p1[x]; acc2 += p2[x]; acc3 += p3[x];
                            rawOut[dp++] = static_cast<uint8_t>(acc3);
                            rawOut[dp++] = static_cast<uint8_t>(acc2);
                            rawOut[dp++] = static_cast<uint8_t>(acc1);
                            rawOut[dp++] = static_cast<uint8_t>(acc0);
                        }
                    }
                }
            }
            return dp == expectedRawSize;
        }
        if (packedSize == expectedRawSize) {
            rawOut.assign(packed, packed + expectedRawSize);
            return true;
        }
        return false;
    }

    // 6, 7: B44 / B44A_COMPRESSION
    if (compression == 6 || compression == 7) {
        // Compute exact compressed byte count by walking channel blocks first
        size_t sp = 0;
        std::vector<std::vector<uint8_t>> chPlanes(channels.size());
        bool okB44 = true;
        for (size_t ci = 0; ci < channels.size() && okB44; ++ci) {
            const auto& ch = channels[ci];
            uint32_t nx = slices[ci].nx, ny = slices[ci].ny;
            chPlanes[ci].assign(static_cast<size_t>(nx) * ny * ch.bytesPerSample, 0);
            if (nx == 0 || ny == 0) continue;
            if (ch.pixelType != 1) {
                size_t rawBytes = static_cast<size_t>(nx) * ny * ch.bytesPerSample;
                if (sp + rawBytes > packedSize) { okB44 = false; break; }
                std::memcpy(chPlanes[ci].data(), packed + sp, rawBytes);
                sp += rawBytes;
            } else {
                for (uint32_t by = 0; by < ny && okB44; by += 4) {
                    for (uint32_t bx = 0; bx < nx && okB44; bx += 4) {
                        if (sp + 3 > packedSize) { okB44 = false; break; }
                        uint16_t s16[16];
                        if (compression == 7 && packed[sp + 2] == 0xFCu) {
                            ExrB44Unpack3(packed + sp, s16);
                            sp += 3;
                        } else {
                            if (sp + 14 > packedSize) { okB44 = false; break; }
                            ExrB44Unpack14(packed + sp, s16);
                            sp += 14;
                        }
                        for (uint32_t py = 0; py < 4 && by + py < ny; ++py) {
                            for (uint32_t px = 0; px < 4 && bx + px < nx; ++px) {
                                uint16_t hVal = s16[py * 4 + px];
                                size_t off = (static_cast<size_t>(by + py) * nx + (bx + px)) * 2;
                                chPlanes[ci][off + 0] = static_cast<uint8_t>(hVal & 0xFFu);
                                chPlanes[ci][off + 1] = static_cast<uint8_t>(hVal >> 8);
                            }
                        }
                    }
                }
            }
        }
        if (okB44 && sp == packedSize) {
            rawOut.resize(expectedRawSize);
            size_t dp = 0;
            std::vector<uint32_t> chRow(channels.size(), 0);
            for (uint32_t ly = 0; ly < blockH; ++ly) {
                int32_t absY = blockAbsMinY + static_cast<int32_t>(ly);
                for (size_t ci = 0; ci < channels.size(); ++ci) {
                    const auto& ch = channels[ci];
                    if ((absY % ch.ySampling) != 0) continue;
                    uint32_t rIdx = chRow[ci]++;
                    size_t rowBytes = static_cast<size_t>(slices[ci].nx) * ch.bytesPerSample;
                    std::memcpy(&rawOut[dp], chPlanes[ci].data() + static_cast<size_t>(rIdx) * rowBytes, rowBytes);
                    dp += rowBytes;
                }
            }
            return dp == expectedRawSize;
        }
        if (packedSize == expectedRawSize) {
            rawOut.assign(packed, packed + expectedRawSize);
            return true;
        }
        return false;
    }

    // 8, 9: DWAA / DWAB_COMPRESSION (DreamWorks 8x8 Lossy DCT + RLE + Zlib)
    if (compression == 8 || compression == 9) {
        if (packedSize < 88) {
            if (packedSize == expectedRawSize) {
                rawOut.assign(packed, packed + expectedRawSize);
                return true;
            }
            return false;
        }
        // Note: OpenEXR DWA stores the 11 int64 header fields in BIG-ENDIAN (network order) in ImfDwaCompressor!
        // Let's support both BE (standard IlmImf htonll) and LE gracefully by checking version in [1..2]:
        auto rdDwa64 = [&](size_t idx, bool be) -> uint64_t {
            return be ? DetailImg100::RdU64BE(packed + idx * 8) : DetailImg100::RdU64LE(packed + idx * 8);
        };
        uint64_t verBE = rdDwa64(0, true);
        uint64_t verLE = rdDwa64(0, false);
        bool hdrBE = (verBE == 1 || verBE == 2);
        if (!hdrBE && verLE != 1 && verLE != 2) return false;

        uint64_t dwaVer           = rdDwa64(0, hdrBE);
        uint64_t unkUncompSize    = rdDwa64(1, hdrBE);
        uint64_t unkCompSize      = rdDwa64(2, hdrBE);
        uint64_t acCompSize       = rdDwa64(3, hdrBE);
        uint64_t dcCompSize       = rdDwa64(4, hdrBE);
        uint64_t rleCompSize      = rdDwa64(5, hdrBE);
        uint64_t rleUncompSize    = rdDwa64(6, hdrBE);
        uint64_t rleRawSize       = rdDwa64(7, hdrBE);
        uint64_t totalAcCount     = rdDwa64(8, hdrBE);
        uint64_t totalDcCount     = rdDwa64(9, hdrBE);
        uint64_t acCompression    = rdDwa64(10, hdrBE);

        size_t dp = 88;
        struct DwaRule {
            std::string name;
            uint8_t scheme = 0; // 0 = UNKNOWN, 1 = LOSSY_DCT, 2 = RLE
            int32_t reqType = -1;
            bool caseInsens = true;
            bool csc = false;
        };
        std::vector<DwaRule> rules;
        if (dwaVer >= 2) {
            if (dp + 2 > packedSize) return false;
            uint16_t ruleBytes = hdrBE ? DetailImg100::RdU16BE(packed + dp) : DetailImg100::RdU16LE(packed + dp);
            if (ruleBytes < 2 || dp + ruleBytes > packedSize) return false;
            size_t rEnd = dp + ruleBytes;
            size_t rp = dp + 2;
            while (rp < rEnd) {
                size_t z = rp;
                while (z < rEnd && packed[z] != 0) ++z;
                if (z + 3 > rEnd) break;
                DwaRule rl;
                rl.name.assign(reinterpret_cast<const char*>(packed + rp), z - rp);
                uint8_t b = packed[z + 1];
                rl.scheme = (b >> 4) & 3u;
                rl.caseInsens = (b & 1u) != 0;
                rl.csc = (b & 2u) != 0;
                rl.reqType = static_cast<int8_t>(packed[z + 2]);
                rules.push_back(std::move(rl));
                rp = z + 3;
            }
            dp = rEnd;
        } else {
            rules.push_back({ "R", 1, 1, true, true });
            rules.push_back({ "R", 1, 2, true, true });
            rules.push_back({ "G", 1, 1, true, true });
            rules.push_back({ "G", 1, 2, true, true });
            rules.push_back({ "B", 1, 1, true, true });
            rules.push_back({ "B", 1, 2, true, true });
            rules.push_back({ "Y", 1, 1, true, false });
            rules.push_back({ "Y", 1, 2, true, false });
            rules.push_back({ "A", 2, -1, true, false });
        }

        if (unkCompSize > packedSize - dp) return false;
        const uint8_t* unkPtr = packed + dp; dp += static_cast<size_t>(unkCompSize);
        if (acCompSize > packedSize - dp) return false;
        const uint8_t* acPtr  = packed + dp; dp += static_cast<size_t>(acCompSize);
        if (dcCompSize > packedSize - dp) return false;
        const uint8_t* dcPtr  = packed + dp; dp += static_cast<size_t>(dcCompSize);
        if (rleCompSize > packedSize - dp) return false;
        const uint8_t* rlePtr = packed + dp; dp += static_cast<size_t>(rleCompSize);

        // Decompress sub-streams
        std::vector<uint8_t> unkRaw;
        if (unkUncompSize > 0) {
            if (!ZipDeflate::InflateZlib(unkPtr, static_cast<size_t>(unkCompSize), unkRaw, static_cast<size_t>(unkUncompSize) + 64) ||
                unkRaw.size() != unkUncompSize) return false;
        }

        std::vector<uint16_t> acWords(static_cast<size_t>(totalAcCount), 0);
        if (totalAcCount > 0) {
            if (acCompression == 0) { // STATIC_HUFFMAN
                if (!ExrHufUncompress(acPtr, static_cast<size_t>(acCompSize), acWords.data(), static_cast<size_t>(totalAcCount))) return false;
            } else { // DEFLATE
                std::vector<uint8_t> acBytes;
                if (!ZipDeflate::InflateZlib(acPtr, static_cast<size_t>(acCompSize), acBytes, static_cast<size_t>(totalAcCount) * 2 + 64) ||
                    acBytes.size() != totalAcCount * 2) return false;
                std::memcpy(acWords.data(), acBytes.data(), acBytes.size());
            }
        }

        std::vector<uint16_t> dcWords(static_cast<size_t>(totalDcCount), 0);
        if (totalDcCount > 0) {
            std::vector<uint8_t> dcTemp, dcInterleaved;
            if (!ZipDeflate::InflateZlib(dcPtr, static_cast<size_t>(dcCompSize), dcTemp, static_cast<size_t>(totalDcCount) * 2 + 64) ||
                dcTemp.size() != totalDcCount * 2) return false;
            UndoExrBytePredictorAndInterleave(dcTemp, dcInterleaved);
            std::memcpy(dcWords.data(), dcInterleaved.data(), dcInterleaved.size());
        }

        std::vector<uint8_t> rleDecoded;
        if (rleRawSize > 0) {
            std::vector<uint8_t> rleZ;
            if (!ZipDeflate::InflateZlib(rlePtr, static_cast<size_t>(rleCompSize), rleZ, static_cast<size_t>(rleUncompSize) + 64) ||
                rleZ.size() != rleUncompSize) return false;
            if (!ExrRleDecompressRaw(rleZ.data(), rleZ.size(), static_cast<size_t>(rleRawSize), rleDecoded)) return false;
        }

        // Classify channels
        std::vector<uint8_t> chScheme(channels.size(), 0);
        int cscR = -1, cscG = -1, cscB = -1;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            std::string suffix = channels[ci].name;
            auto dot = suffix.rfind('.');
            if (dot != std::string::npos) suffix = suffix.substr(dot + 1);
            for (const auto& rl : rules) {
                bool nameMatch = rl.caseInsens ? DetailImg100::EqualsIgnoreCaseAscii(suffix, rl.name) : (suffix == rl.name);
                if (nameMatch && (rl.reqType < 0 || rl.reqType == channels[ci].pixelType)) {
                    chScheme[ci] = rl.scheme;
                    if (rl.csc && DetailImg100::EqualsIgnoreCaseAscii(suffix, "R")) cscR = static_cast<int>(ci);
                    if (rl.csc && DetailImg100::EqualsIgnoreCaseAscii(suffix, "G")) cscG = static_cast<int>(ci);
                    if (rl.csc && DetailImg100::EqualsIgnoreCaseAscii(suffix, "B")) cscB = static_cast<int>(ci);
                    break;
                }
            }
        }
        bool useCsc = (cscR >= 0 && cscG >= 0 && cscB >= 0 &&
                       slices[cscR].nx == slices[cscG].nx && slices[cscR].nx == slices[cscB].nx &&
                       slices[cscR].ny == slices[cscG].ny && slices[cscR].ny == slices[cscB].ny);

        static const uint8_t kZigZag[64] = {
             0,  1,  8, 16,  9,  2,  3, 10,
            17, 24, 32, 25, 18, 11,  4,  5,
            12, 19, 26, 33, 40, 48, 41, 34,
            27, 20, 13,  6,  7, 14, 21, 28,
            35, 42, 49, 56, 57, 50, 43, 36,
            29, 22, 15, 23, 30, 37, 44, 51,
            58, 59, 52, 45, 38, 31, 39, 46,
            53, 60, 61, 54, 47, 55, 62, 63
        };

        auto floatToHalfBits = [](float f) -> uint16_t {
            uint32_t x = 0;
            std::memcpy(&x, &f, 4);
            uint32_t sign = (x >> 16) & 0x8000u;
            int exp = static_cast<int>((x >> 23) & 0xFFu) - 127 + 15;
            uint32_t mant = (x >> 13) & 0x3FFu;
            if (exp <= 0) return static_cast<uint16_t>(sign);
            if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
        };

        size_t dcCursor = 0, acCursor = 0;
        auto decodeDctPlane = [&](uint32_t nx, uint32_t ny, std::vector<float>& plane) -> bool {
            plane.assign(static_cast<size_t>(nx) * ny, 0.0f);
            uint32_t bxCnt = (nx + 7u) / 8u;
            uint32_t byCnt = (ny + 7u) / 8u;
            if (dcCursor + static_cast<size_t>(bxCnt) * byCnt > dcWords.size()) return false;

            for (uint32_t by = 0; by < byCnt; ++by) {
                for (uint32_t bx = 0; bx < bxCnt; ++bx) {
                    float dct[64] = {};
                    dct[0] = GpuTex::HalfToFloat(dcWords[dcCursor++]);
                    int k = 1;
                    while (k < 64) {
                        if (acCursor >= acWords.size()) return false;
                        uint16_t sym = acWords[acCursor++];
                        if (sym == 0x0000u) { // EOB
                            break;
                        } else if ((sym & 0xFF00u) == 0xFF00u) {
                            k += static_cast<int>(sym & 0x00FFu);
                        } else {
                            dct[kZigZag[k]] = GpuTex::HalfToFloat(sym);
                            ++k;
                        }
                    }
                    float pix[64];
                    ExrDwaIdct8x8(dct, pix);
                    for (uint32_t py = 0; py < 8 && by * 8 + py < ny; ++py) {
                        for (uint32_t px = 0; px < 8 && bx * 8 + px < nx; ++px) {
                            plane[static_cast<size_t>(by * 8 + py) * nx + (bx * 8 + px)] = pix[py * 8 + px];
                        }
                    }
                }
            }
            return true;
        };

        std::vector<std::vector<uint8_t>> chPlanes(channels.size());
        std::vector<bool> chDone(channels.size(), false);

        // 1) Decode 709 CSC RGB set if active (order: Y' = cscR, Cb = cscG, Cr = cscB in ImfDwaCompressor!)
        if (useCsc) {
            uint32_t nx = slices[cscR].nx, ny = slices[cscR].ny;
            std::vector<float> pY, pCb, pCr;
            if (!decodeDctPlane(nx, ny, pY) || !decodeDctPlane(nx, ny, pCb) || !decodeDctPlane(nx, ny, pCr)) return false;

            int idxs[3] = { cscR, cscG, cscB };
            for (int m = 0; m < 3; ++m) {
                chPlanes[idxs[m]].resize(static_cast<size_t>(nx) * ny * channels[idxs[m]].bytesPerSample);
                chDone[idxs[m]] = true;
            }
            for (size_t i = 0; i < static_cast<size_t>(nx) * ny; ++i) {
                float yv = pY[i], cb = pCb[i], cr = pCr[i];
                float rgb[3] = {
                    yv + 1.5747f * cr,
                    yv - 0.1873f * cb - 0.4682f * cr,
                    yv + 1.8556f * cb
                };
                for (int m = 0; m < 3; ++m) {
                    int ci = idxs[m];
                    if (channels[ci].pixelType == 1) {
                        uint16_t h16 = floatToHalfBits(rgb[m]);
                        chPlanes[ci][i * 2 + 0] = static_cast<uint8_t>(h16 & 0xFFu);
                        chPlanes[ci][i * 2 + 1] = static_cast<uint8_t>(h16 >> 8);
                    } else {
                        std::memcpy(&chPlanes[ci][i * 4], &rgb[m], 4);
                    }
                }
            }
        }

        // 2) Decode remaining LOSSY_DCT channels
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            if (chDone[ci] || chScheme[ci] != 1) continue;
            uint32_t nx = slices[ci].nx, ny = slices[ci].ny;
            std::vector<float> pF;
            if (!decodeDctPlane(nx, ny, pF)) return false;
            chPlanes[ci].resize(static_cast<size_t>(nx) * ny * channels[ci].bytesPerSample);
            for (size_t i = 0; i < pF.size(); ++i) {
                if (channels[ci].pixelType == 1) {
                    uint16_t h16 = floatToHalfBits(pF[i]);
                    chPlanes[ci][i * 2 + 0] = static_cast<uint8_t>(h16 & 0xFFu);
                    chPlanes[ci][i * 2 + 1] = static_cast<uint8_t>(h16 >> 8);
                } else {
                    std::memcpy(&chPlanes[ci][i * 4], &pF[i], 4);
                }
            }
            chDone[ci] = true;
        }

        // 3) Decode RLE channels (byte-planar across each channel's samples)
        size_t rleOff = 0;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            if (chDone[ci] || chScheme[ci] != 2) continue;
            size_t nSamp = static_cast<size_t>(slices[ci].nx) * slices[ci].ny;
            uint32_t bps = channels[ci].bytesPerSample;
            if (rleOff + nSamp * bps > rleDecoded.size()) return false;
            chPlanes[ci].resize(nSamp * bps);
            for (uint32_t b = 0; b < bps; ++b) {
                for (size_t s = 0; s < nSamp; ++s) {
                    chPlanes[ci][s * bps + b] = rleDecoded[rleOff + static_cast<size_t>(b) * nSamp + s];
                }
            }
            rleOff += nSamp * bps;
            chDone[ci] = true;
        }

        // 4) Decode UNKNOWN Zlib channels
        size_t unkOff = 0;
        for (size_t ci = 0; ci < channels.size(); ++ci) {
            if (chDone[ci]) continue;
            size_t bytes = static_cast<size_t>(slices[ci].nx) * slices[ci].ny * channels[ci].bytesPerSample;
            if (unkOff + bytes > unkRaw.size()) return false;
            chPlanes[ci].assign(unkRaw.data() + unkOff, unkRaw.data() + unkOff + bytes);
            unkOff += bytes;
            chDone[ci] = true;
        }

        rawOut.resize(expectedRawSize);
        size_t outOff = 0;
        std::vector<uint32_t> chRow(channels.size(), 0);
        for (uint32_t ly = 0; ly < blockH; ++ly) {
            int32_t absY = blockAbsMinY + static_cast<int32_t>(ly);
            for (size_t ci = 0; ci < channels.size(); ++ci) {
                const auto& ch = channels[ci];
                if ((absY % ch.ySampling) != 0) continue;
                uint32_t rIdx = chRow[ci]++;
                size_t rowBytes = static_cast<size_t>(slices[ci].nx) * ch.bytesPerSample;
                std::memcpy(&rawOut[outOff], chPlanes[ci].data() + static_cast<size_t>(rIdx) * rowBytes, rowBytes);
                outOff += rowBytes;
            }
        }
        return outOff == expectedRawSize;
    }

    return false;
}

} // namespace ExrDetail

inline bool DecodeOpenEXRUpgraded(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo, uint32_t requestedPart = UINT32_MAX) {
    if (!data || size < 32 || data[0] != 0x76 || data[1] != 0x2F || data[2] != 0x31 || data[3] != 0x01) {
        return false;
    }
    uint32_t verFlags = DetailImg100::RdU32LE(data + 4);
    uint32_t version = verFlags & 0xFFu;
    if (version != 1 && version != 2) return false;
    bool globalTiled     = (verFlags & 0x0200u) != 0;
    bool isDeep          = (verFlags & 0x0800u) != 0;
    bool isMultiPart     = (verFlags & 0x1000u) != 0;
    if (isDeep) return false;

    std::vector<ExrDetail::ExrPartDesc> parts;
    size_t pos = 8;

    auto parseOnePartHeader = [&](ExrDetail::ExrPartDesc& pt) -> bool {
        pt.isTiled = globalTiled;
        pt.compression = 0xFF;
        while (pos < size) {
            if (data[pos] == 0) {
                ++pos;
                return true;
            }
            size_t nameEnd = pos;
            while (nameEnd < size && data[nameEnd] != 0) ++nameEnd;
            if (nameEnd >= size) return false;
            std::string attrName(reinterpret_cast<const char*>(data + pos), nameEnd - pos);
            pos = nameEnd + 1;

            size_t typeEnd = pos;
            while (typeEnd < size && data[typeEnd] != 0) ++typeEnd;
            if (typeEnd + 5 > size) return false;
            pos = typeEnd + 1;

            uint32_t attrSize = DetailImg100::RdU32LE(data + pos);
            pos += 4;
            if (attrSize > size - pos) return false;

            if (attrName == "name") {
                pt.partName.assign(reinterpret_cast<const char*>(data + pos), attrSize);
                while (!pt.partName.empty() && pt.partName.back() == '\0') pt.partName.pop_back();
            } else if (attrName == "type") {
                pt.partType.assign(reinterpret_cast<const char*>(data + pos), attrSize);
                while (!pt.partType.empty() && pt.partType.back() == '\0') pt.partType.pop_back();
                if (pt.partType == "tiledimage") pt.isTiled = true;
                else if (pt.partType == "scanlineimage") pt.isTiled = false;
                else if (pt.partType == "deepscanline" || pt.partType == "deeptile") return false;
            } else if (attrName == "dataWindow" && attrSize == 16) {
                pt.minX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 0));
                pt.minY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 4));
                pt.maxX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 8));
                pt.maxY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 12));
                pt.hasDataWindow = true;
            } else if (attrName == "displayWindow" && attrSize == 16 && !pt.hasDataWindow) {
                pt.minX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 0));
                pt.minY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 4));
                pt.maxX = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 8));
                pt.maxY = static_cast<int32_t>(DetailImg100::RdU32LE(data + pos + 12));
                pt.hasDisplayWindow = true;
            } else if (attrName == "compression" && attrSize >= 1) {
                pt.compression = data[pos];
            } else if (attrName == "chunkCount" && attrSize == 4) {
                pt.chunkCount = DetailImg100::RdU32LE(data + pos);
                pt.hasChunkCount = true;
            } else if (attrName == "tiles" && attrSize >= 8) {
                pt.tileDescX = DetailImg100::RdU32LE(data + pos + 0);
                pt.tileDescY = DetailImg100::RdU32LE(data + pos + 4);
            } else if (attrName == "channels" && attrSize >= 1) {
                size_t cpos = pos;
                size_t cend = pos + attrSize;
                while (cpos < cend && data[cpos] != 0) {
                    size_t ce = cpos;
                    while (ce < cend && data[ce] != 0) ++ce;
                    if (ce + 1 + 16 > cend) return false;
                    ExrDetail::ExrChannelDesc ch;
                    ch.name.assign(reinterpret_cast<const char*>(data + cpos), ce - cpos);
                    const uint8_t* meta = data + ce + 1;
                    ch.pixelType = static_cast<int32_t>(DetailImg100::RdU32LE(meta + 0));
                    ch.xSampling = std::max(1, static_cast<int32_t>(DetailImg100::RdU32LE(meta + 8)));
                    ch.ySampling = std::max(1, static_cast<int32_t>(DetailImg100::RdU32LE(meta + 12)));
                    if (ch.pixelType == 0 || ch.pixelType == 2) ch.bytesPerSample = 4;
                    else if (ch.pixelType == 1) ch.bytesPerSample = 2;
                    else return false;
                    pt.channels.push_back(std::move(ch));
                    cpos = ce + 1 + 16;
                }
            }
            pos += attrSize;
        }
        return false;
    };

    if (isMultiPart) {
        while (pos < size) {
            if (data[pos] == 0) { ++pos; break; }
            ExrDetail::ExrPartDesc pt;
            if (!parseOnePartHeader(pt)) return false;
            parts.push_back(std::move(pt));
            if (parts.size() > 256) return false;
        }
    } else {
        ExrDetail::ExrPartDesc pt;
        if (!parseOnePartHeader(pt)) return false;
        parts.push_back(std::move(pt));
    }
    if (parts.empty()) return false;

    // Validate parts & compute exact chunkCount per part
    std::vector<std::wstring> allLayerNames;
    size_t bestColorPart = 0;
    int bestScore = -1;

    for (size_t pi = 0; pi < parts.size(); ++pi) {
        auto& pt = parts[pi];
        if ((!pt.hasDataWindow && !pt.hasDisplayWindow) || pt.channels.empty()) return false;
        if (pt.compression == 0xFF) pt.compression = 0;
        if (pt.compression > 9) return false;

        int64_t sx = static_cast<int64_t>(pt.maxX) - static_cast<int64_t>(pt.minX) + 1;
        int64_t sy = static_cast<int64_t>(pt.maxY) - static_cast<int64_t>(pt.minY) + 1;
        if (sx < 1 || sx > 8192 || sy < 1 || sy > 8192) return false;

        if (!pt.hasChunkCount || pt.chunkCount == 0) {
            if (pt.isTiled) {
                if (pt.tileDescX == 0 || pt.tileDescY == 0) return false;
                uint32_t tx = (static_cast<uint32_t>(sx) + pt.tileDescX - 1u) / pt.tileDescX;
                uint32_t ty = (static_cast<uint32_t>(sy) + pt.tileDescY - 1u) / pt.tileDescY;
                pt.chunkCount = tx * ty;
            } else {
                uint32_t lpb = ExrDetail::ExrLinesPerScanlineBlock(pt.compression);
                pt.chunkCount = (static_cast<uint32_t>(sy) + lpb - 1u) / lpb;
            }
        }

        int score = 0;
        for (const auto& ch : pt.channels) {
            std::string full = pt.partName.empty() ? ch.name : (pt.partName + "." + ch.name);
            allLayerNames.push_back(PathUtils::Utf8ToWide(full));
            if (ch.name == "R" || ch.name == "G" || ch.name == "B") score += 10;
            else if (ch.name.size() >= 2 && ch.name[ch.name.size() - 2] == '.' &&
                     (ch.name.back() == 'R' || ch.name.back() == 'G' || ch.name.back() == 'B')) score += 5;
            else if (ch.name == "Y") score += 2;
        }
        if (score > bestScore) { bestScore = score; bestColorPart = pi; }
    }

    size_t selectedPart = (requestedPart < parts.size()) ? requestedPart : bestColorPart;

    // Advance offset table cursor to selectedPart (also compute end of ALL offset tables for sequential fallback)
    size_t partOffsetTablePos = pos;
    size_t allTablesEnd = pos;
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        size_t tblBytes = static_cast<size_t>(parts[pi].chunkCount) * 8u;
        if (tblBytes > size - allTablesEnd) return false;
        if (pi == selectedPart) partOffsetTablePos = allTablesEnd;
        allTablesEnd += tblBytes;
    }

    const auto& pt = parts[selectedPart];
    const auto& channels = pt.channels;
    uint32_t w = static_cast<uint32_t>(pt.maxX - pt.minX + 1);
    uint32_t h = static_cast<uint32_t>(pt.maxY - pt.minY + 1);

    int idxR = -1, idxG = -1, idxB = -1, idxA = -1, idxY = -1;
    auto matchSuffix = [](const std::string& s, char target) -> bool {
        if (s.size() == 1 && (s[0] == target || s[0] == (target - 'A' + 'a'))) return true;
        if (s.size() >= 2 && s[s.size() - 2] == '.') {
            char c = s.back();
            if (c == target || c == (target - 'A' + 'a')) return true;
        }
        return false;
    };
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        const std::string& cn = channels[i].name;
        if (cn == "R" || cn == "r") idxR = i;
        else if (cn == "G" || cn == "g") idxG = i;
        else if (cn == "B" || cn == "b") idxB = i;
        else if (cn == "A" || cn == "a") idxA = i;
        else if (cn == "Y" || cn == "y" || cn == "L" || cn == "Z") { if (idxY < 0) idxY = i; }
    }
    for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        const std::string& cn = channels[i].name;
        if (idxR < 0 && matchSuffix(cn, 'R')) idxR = i;
        if (idxG < 0 && matchSuffix(cn, 'G')) idxG = i;
        if (idxB < 0 && matchSuffix(cn, 'B')) idxB = i;
        if (idxA < 0 && matchSuffix(cn, 'A')) idxA = i;
        if (idxY < 0 && matchSuffix(cn, 'Y')) idxY = i;
    }
    if (idxR < 0 && idxG < 0 && idxB < 0) {
        if (idxY >= 0) { idxR = idxG = idxB = idxY; }
        else if (channels.size() >= 3) { idxR = 0; idxG = 1; idxB = 2; }
        else if (channels.size() == 2) { idxR = 0; idxG = 1; idxB = 1; }
        else { idxR = idxG = idxB = 0; }
    } else {
        if (idxR < 0) idxR = (idxG >= 0) ? idxG : idxB;
        if (idxG < 0) idxG = idxR;
        if (idxB < 0) idxB = idxG;
    }

    std::vector<float> floatRgba(static_cast<size_t>(w) * h * 4, 0.0f);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) floatRgba[i * 4 + 3] = 1.0f;
    bool hasUintChannel = false;
    float maxUintVal = 1.0f;
    for (const auto& ch : channels) if (ch.pixelType == 0) hasUintChannel = true;

    auto readSampleAsFloat = [&](int32_t pixelType, const uint8_t* sp) -> float {
        if (pixelType == 1) return GpuTex::HalfToFloat(DetailImg100::RdU16LE(sp));
        if (pixelType == 2) return GpuTex::ReadF32LE(sp);
        uint32_t u = DetailImg100::RdU32LE(sp);
        float fv = static_cast<float>(u);
        if (fv > maxUintVal) maxUintVal = fv;
        return fv;
    };

    if (pt.isTiled) {
        if (pt.tileDescX == 0 || pt.tileDescY == 0 || pt.tileDescX > 4096 || pt.tileDescY > 4096) return false;
        uint32_t tilesX = (w + pt.tileDescX - 1u) / pt.tileDescX;
        uint32_t tilesY = (h + pt.tileDescY - 1u) / pt.tileDescY;
        size_t numTiles = pt.chunkCount;
        size_t seqOff = allTablesEnd;
        std::vector<uint8_t> rawBlock;
        size_t decodedTiles = 0;

        for (size_t ti = 0; ti < numTiles; ++ti) {
            uint64_t cOff64 = DetailImg100::RdU64LE(data + partOffsetTablePos + ti * 8);
            size_t cOff = (cOff64 >= allTablesEnd && cOff64 + 20 <= size) ? static_cast<size_t>(cOff64) : seqOff;
            if (isMultiPart) {
                if (cOff + 24 > size) return false;
                uint32_t partNum = DetailImg100::RdU32LE(data + cOff);
                if (partNum != selectedPart) continue;
                cOff += 4;
            } else if (cOff + 20 > size) {
                return false;
            }
            uint32_t tx = DetailImg100::RdU32LE(data + cOff + 0);
            uint32_t ty = DetailImg100::RdU32LE(data + cOff + 4);
            uint32_t lx = DetailImg100::RdU32LE(data + cOff + 8);
            uint32_t lyLvl = DetailImg100::RdU32LE(data + cOff + 12);
            uint32_t pSize = DetailImg100::RdU32LE(data + cOff + 16);
            if (pSize > size - (cOff + 20)) return false;
            seqOff = cOff + 20 + pSize;
            if (lx != 0 || lyLvl != 0 || tx >= tilesX || ty >= tilesY) continue;

            uint32_t x0 = tx * pt.tileDescX;
            uint32_t y0 = ty * pt.tileDescY;
            uint32_t tw = std::min(pt.tileDescX, w - x0);
            uint32_t th = std::min(pt.tileDescY, h - y0);
            int32_t tileAbsMinY = pt.minY + static_cast<int32_t>(y0);
            size_t expected = 0;
            for (uint32_t ly = 0; ly < th; ++ly) {
                int32_t absY = tileAbsMinY + static_cast<int32_t>(ly);
                for (const auto& ch : channels) {
                    if ((absY % ch.ySampling) == 0) {
                        expected += static_cast<size_t>((tw + ch.xSampling - 1) / ch.xSampling) * ch.bytesPerSample;
                    }
                }
            }
            if (!ExrDetail::DecompressExrChunkAny(pt.compression, data + cOff + 20, pSize, expected, tw, th, tileAbsMinY, channels, rawBlock)) {
                return false;
            }
            size_t rp = 0;
            for (uint32_t ly = 0; ly < th; ++ly) {
                uint32_t dstY = y0 + ly;
                int32_t absY = tileAbsMinY + static_cast<int32_t>(ly);
                for (int ci = 0; ci < static_cast<int>(channels.size()); ++ci) {
                    const auto& ch = channels[ci];
                    if ((absY % ch.ySampling) != 0) continue;
                    uint32_t cw = (tw + ch.xSampling - 1) / ch.xSampling;
                    for (uint32_t cx = 0; cx < cw; ++cx) {
                        float val = readSampleAsFloat(ch.pixelType, rawBlock.data() + rp);
                        rp += ch.bytesPerSample;
                        for (int sx = 0; sx < ch.xSampling && cx * ch.xSampling + sx < tw; ++sx) {
                            uint32_t dstX = x0 + cx * ch.xSampling + sx;
                            float* px = &floatRgba[(static_cast<size_t>(dstY) * w + dstX) * 4];
                            if (ci == idxR) px[0] = val;
                            if (ci == idxG) px[1] = val;
                            if (ci == idxB) px[2] = val;
                            if (ci == idxA) px[3] = val;
                        }
                    }
                }
            }
            ++decodedTiles;
        }
        if (decodedTiles == 0) return false;
    } else {
        uint32_t linesPerBlock = ExrDetail::ExrLinesPerScanlineBlock(pt.compression);
        uint32_t numBlocks = pt.chunkCount;
        size_t seqOff = allTablesEnd;
        std::vector<uint8_t> rawBlock;
        uint32_t decodedLines = 0;

        for (uint32_t b = 0; b < numBlocks; ++b) {
            uint64_t cOff64 = DetailImg100::RdU64LE(data + partOffsetTablePos + static_cast<size_t>(b) * 8);
            size_t cOff = (cOff64 >= allTablesEnd && cOff64 + 8 <= size) ? static_cast<size_t>(cOff64) : seqOff;
            if (isMultiPart) {
                if (cOff + 12 > size) return false;
                uint32_t partNum = DetailImg100::RdU32LE(data + cOff);
                if (partNum != selectedPart) continue;
                cOff += 4;
            } else if (cOff + 8 > size) {
                return false;
            }
            int32_t chunkY = static_cast<int32_t>(DetailImg100::RdU32LE(data + cOff + 0));
            uint32_t pSize = DetailImg100::RdU32LE(data + cOff + 4);
            if (pSize > size - (cOff + 8)) return false;
            seqOff = cOff + 8 + pSize;

            int64_t relY = static_cast<int64_t>(chunkY) - static_cast<int64_t>(pt.minY);
            uint32_t startY = (relY >= 0 && relY < static_cast<int64_t>(h)) ? static_cast<uint32_t>(relY) : (b * linesPerBlock);
            if (startY >= h) return false;
            uint32_t blockH = std::min(linesPerBlock, h - startY);
            int32_t blockAbsMinY = pt.minY + static_cast<int32_t>(startY);

            size_t expected = 0;
            for (uint32_t ly = 0; ly < blockH; ++ly) {
                int32_t absY = blockAbsMinY + static_cast<int32_t>(ly);
                for (const auto& ch : channels) {
                    if ((absY % ch.ySampling) == 0) {
                        uint32_t cw = (w + static_cast<uint32_t>(ch.xSampling) - 1u) / static_cast<uint32_t>(ch.xSampling);
                        expected += static_cast<size_t>(cw) * ch.bytesPerSample;
                    }
                }
            }

            if (!ExrDetail::DecompressExrChunkAny(pt.compression, data + cOff + 8, pSize, expected, w, blockH, blockAbsMinY, channels, rawBlock)) {
                return false;
            }

            size_t rp = 0;
            for (uint32_t ly = 0; ly < blockH; ++ly) {
                uint32_t dstY = startY + ly;
                int32_t absY = blockAbsMinY + static_cast<int32_t>(ly);
                for (int ci = 0; ci < static_cast<int>(channels.size()); ++ci) {
                    const auto& ch = channels[ci];
                    if ((absY % ch.ySampling) != 0) continue;
                    uint32_t cw = (w + static_cast<uint32_t>(ch.xSampling) - 1u) / static_cast<uint32_t>(ch.xSampling);
                    for (uint32_t lx = 0; lx < cw; ++lx) {
                        float val = readSampleAsFloat(ch.pixelType, rawBlock.data() + rp);
                        rp += ch.bytesPerSample;
                        for (int sy = 0; sy < ch.ySampling && dstY + static_cast<uint32_t>(sy) < h; ++sy) {
                            for (int sx = 0; sx < ch.xSampling && lx * static_cast<uint32_t>(ch.xSampling) + static_cast<uint32_t>(sx) < w; ++sx) {
                                uint32_t dstX = lx * static_cast<uint32_t>(ch.xSampling) + static_cast<uint32_t>(sx);
                                float* px = &floatRgba[(static_cast<size_t>(dstY + sy) * w + dstX) * 4];
                                if (ci == idxR) px[0] = val;
                                if (ci == idxG) px[1] = val;
                                if (ci == idxB) px[2] = val;
                                if (ci == idxA) px[3] = val;
                            }
                        }
                    }
                }
                ++decodedLines;
            }
        }
        if (decodedLines == 0) return false;
    }

    if (hasUintChannel && maxUintVal > 1.0f) {
        float invScale = 1.0f / maxUintVal;
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            if (idxR >= 0 && channels[idxR].pixelType == 0) floatRgba[i * 4 + 0] *= invScale;
            if (idxG >= 0 && channels[idxG].pixelType == 0) floatRgba[i * 4 + 1] *= invScale;
            if (idxB >= 0 && channels[idxB].pixelType == 0) floatRgba[i * 4 + 2] *= invScale;
            if (idxA >= 0 && channels[idxA].pixelType == 0) floatRgba[i * 4 + 3] *= invScale;
        }
    }

    uint32_t bitsPerCh = 16;
    for (const auto& ch : channels) {
        if (ch.pixelType == 0 || ch.pixelType == 2) { bitsPerCh = 32; break; }
    }

    static const wchar_t* kCompNames[10] = {
        L"NO", L"RLE", L"ZIPS", L"ZIP", L"PIZ", L"PXR24", L"B44", L"B44A", L"DWAA", L"DWAB"
    };
    std::wstring compTag = (pt.compression < 10) ? kCompNames[pt.compression] : L"HDR";

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = static_cast<uint32_t>(channels.size());
    outInfo.bitsPerChannel = bitsPerCh;
    outInfo.layerNames = std::move(allLayerNames);
    outInfo.formatName = L"OpenEXR " + compTag + L" " + std::to_wstring(bitsPerCh) + L"-bit HDR" +
                         (parts.size() > 1 ? (L" (" + std::to_wstring(parts.size()) + L" parts)") : L"") + L" (.exr)";
    GpuTex::ConvertFloatRgbaToBgra(floatRgba.data(), w, h, false, outInfo.bgraPixels);
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 1. OpenEXR (.exr) — Magic 0x76, 0x2F, 0x31, 0x01
//    Delegates to ExrUpgrade.h (NO, RLE, ZIPS, ZIP, PIZ, PXR24, B44, B44A, DWAA, DWAB + Multi-Part v2)
// ============================================================================
inline bool DecodeOpenEXR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DecodeOpenEXRUpgraded(data, size, outInfo);
}


} // namespace Formats
