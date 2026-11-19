#pragma once
// Astc.h — Khronos / ARM Adaptive Scalable Texture Compression (.astc) LDR/Void-Extent Decoder

namespace Formats {

namespace DetailAstc {

struct BiseDesc {
    uint8_t bits;
    uint8_t trits;
    uint8_t quints;
};

inline constexpr BiseDesc kBiseRanges[21] = {
    {1,0,0}, {0,1,0}, {2,0,0}, {0,0,1}, {1,1,0}, {3,0,0}, // 0..5 (weights low)
    {1,0,1}, {2,1,0}, {4,0,0}, {2,0,1}, {3,1,0}, {5,0,0}, // 6..11 (weights high)
    {3,0,1}, {4,1,0}, {6,0,0}, {4,0,1}, {5,1,0}, {7,0,0}, // 12..17 (endpoints)
    {5,0,1}, {6,1,0}, {8,0,0}                             // 18..20 (endpoints)
};

inline int BiseBitCount(int rangeIdx, int count) {
    const BiseDesc& d = kBiseRanges[rangeIdx];
    int total = count * d.bits;
    if (d.trits)       total += (count * 8 + 4) / 5;
    else if (d.quints) total += (count * 7 + 2) / 3;
    return total;
}

inline bool DecodeBlockMode2D(
    uint32_t bm,
    int& gridW,
    int& gridH,
    int& wtRangeIdx,
    bool& dualPlane)
{
    if ((bm & 0x1FFu) == 0) return false;
    int R = 0, H = 0, D = 0;
    if ((bm & 3u) != 0) {
        R = static_cast<int>((((bm >> 1) & 1u) << 2) | ((bm & 1u) << 1) | ((bm >> 4) & 1u));
        if (R < 2) return false;
        uint32_t sub = (bm >> 2) & 3u;
        H = static_cast<int>((bm >> 9) & 1u);
        D = static_cast<int>((bm >> 10) & 1u);
        int A = static_cast<int>((bm >> 5) & 3u);
        if (sub == 0) {
            int B = static_cast<int>((bm >> 7) & 3u);
            gridW = B + 4; gridH = A + 2;
        } else if (sub == 1) {
            int B = static_cast<int>((bm >> 7) & 3u);
            gridW = B + 8; gridH = A + 2;
        } else if (sub == 2) {
            int B = static_cast<int>((bm >> 7) & 3u);
            gridW = A + 2; gridH = B + 8;
        } else {
            int B = static_cast<int>((bm >> 7) & 1u);
            if (((bm >> 8) & 1u) == 0) {
                gridW = A + 2; gridH = B + 6;
            } else {
                gridW = A + 2;
                gridH = B + 2;
            }
        }
    } else {
        R = static_cast<int>((((bm >> 3) & 1u) << 2) | (((bm >> 2) & 1u) << 1) | ((bm >> 4) & 1u));
        if (R < 2) return false;
        uint32_t hiMode = (bm >> 7) & 3u;
        int A = static_cast<int>((bm >> 5) & 3u);
        if (hiMode == 0) {
            gridW = 12; gridH = A + 2;
            H = static_cast<int>((bm >> 9) & 1u);
            D = static_cast<int>((bm >> 10) & 1u);
        } else if (hiMode == 1) {
            gridW = A + 2; gridH = 12;
            H = static_cast<int>((bm >> 9) & 1u);
            D = static_cast<int>((bm >> 10) & 1u);
        } else if (hiMode == 2) {
            gridW = A + 6;
            gridH = static_cast<int>((bm >> 9) & 3u) + 6;
            H = 0;
            D = 0;
        } else {
            if (A == 0) { gridW = 6; gridH = 10; }
            else if (A == 1) { gridW = 10; gridH = 6; }
            else return false;
            H = static_cast<int>((bm >> 9) & 1u);
            D = static_cast<int>((bm >> 10) & 1u);
        }
    }
    wtRangeIdx = (R - 2) + (H ? 6 : 0);
    dualPlane = (D != 0);
    return true;
}

template <typename ReadBitFn>
inline void UnpackBise(int rangeIdx, int count, ReadBitFn&& rdBit, uint8_t* outVals) {
    const BiseDesc& d = kBiseRanges[rangeIdx];
    auto rdBits = [&](int n) -> int {
        int v = 0;
        for (int i = 0; i < n; ++i) v |= (rdBit() << i);
        return v;
    };

    if (!d.trits && !d.quints) {
        for (int i = 0; i < count; ++i) {
            outVals[i] = static_cast<uint8_t>(rdBits(d.bits));
        }
        return;
    }

    if (d.trits) {
        static const uint8_t kTritBits[5] = { 2, 2, 1, 2, 1 };
        static const uint8_t kTritShift[5] = { 0, 2, 4, 5, 7 };
        for (int base = 0; base < count; base += 5) {
            int m[5] = {};
            int T = 0;
            int groupCnt = std::min(5, count - base);
            for (int i = 0; i < groupCnt; ++i) {
                m[i] = rdBits(d.bits);
                T |= (rdBits(kTritBits[i]) << kTritShift[i]);
            }
            int C = 0, t[5] = {};
            if ((T & 0x1C) == 0x1C) {
                C = ((T >> 5) << 2) | (T & 3);
                t[4] = t[3] = 2;
            } else {
                C = T & 0x1F;
                if ((T & 0x60) == 0x60) {
                    t[4] = 2;
                    t[3] = (T >> 7) & 1;
                } else {
                    t[4] = (T >> 7) & 1;
                    t[3] = (T >> 5) & 3;
                }
            }
            if ((C & 3) == 3) {
                t[2] = 2;
                t[1] = (C >> 4) & 1;
                int c3 = (C >> 3) & 1;
                int c2 = (C >> 2) & 1;
                t[0] = (c3 << 1) | (c2 & (1 - c3));
            } else if (((C >> 2) & 3) == 3) {
                t[2] = 2;
                t[1] = 2;
                t[0] = C & 3;
            } else {
                t[2] = (C >> 4) & 1;
                t[1] = (C >> 2) & 3;
                t[0] = C & 3;
            }
            for (int i = 0; i < groupCnt; ++i) {
                outVals[base + i] = static_cast<uint8_t>((t[i] << d.bits) | m[i]);
            }
        }
        return;
    }

    // Quints
    static const uint8_t kQuintBits[3] = { 3, 2, 2 };
    static const uint8_t kQuintShift[3] = { 0, 3, 5 };
    for (int base = 0; base < count; base += 3) {
        int m[3] = {};
        int Q = 0;
        int groupCnt = std::min(3, count - base);
        for (int i = 0; i < groupCnt; ++i) {
            m[i] = rdBits(d.bits);
            Q |= (rdBits(kQuintBits[i]) << kQuintShift[i]);
        }
        int C = 0, q[3] = {};
        if (((Q >> 1) & 3) == 3 && ((Q >> 5) & 3) == 0) {
            int q4 = (Q >> 4) & 1;
            int q3 = (Q >> 3) & 1;
            q[2] = ((Q & 1) << 2) | ((q4 & (1 - q3)) << 1) | (q3 & (1 - q4));
            q[1] = q[0] = 4;
        } else {
            if (((Q >> 1) & 3) == 3) {
                q[2] = 4;
                C = (((Q >> 3) & 3) << 3) | ((~(Q >> 5) & 3) << 1) | (Q & 1);
            } else {
                q[2] = (Q >> 5) & 3;
                C = Q & 0x1F;
            }
            if ((C & 7) == 5) {
                q[1] = 4;
                q[0] = (C >> 3) & 3;
            } else {
                q[1] = (C >> 3) & 3;
                q[0] = C & 7;
            }
        }
        for (int i = 0; i < groupCnt; ++i) {
            outVals[base + i] = static_cast<uint8_t>((q[i] << d.bits) | m[i]);
        }
    }
}

inline uint8_t UnquantizeWeight(int rangeIdx, uint8_t v) {
    const BiseDesc& d = kBiseRanges[rangeIdx];
    int u = 0;
    if (!d.trits && !d.quints) {
        switch (d.bits) {
        case 1: u = v ? 63 : 0; break;
        case 2: u = v * 21; break;
        case 3: u = (v << 3) | v; break;
        case 4: u = (v << 2) | (v >> 2); break;
        case 5: u = (v << 1) | (v >> 4); break;
        default: u = 0; break;
        }
    } else if (d.bits == 0) {
        if (d.trits) { static const uint8_t kT[3] = { 0, 32, 63 }; u = kT[std::min<int>(v, 2)]; }
        else         { static const uint8_t kQ[5] = { 0, 16, 32, 47, 63 }; u = kQ[std::min<int>(v, 4)]; }
    } else {
        int A = (v & 1) ? 0x7F : 0x00;
        int B = 0, C = 0;
        int x = v >> 1;
        int tq = v >> d.bits;
        if (d.trits) {
            if (d.bits == 1)      { C = 50; B = 0; }
            else if (d.bits == 2) { C = 23; B = (x & 1) * 0x45; }
            else                  { C = 11; B = (x & 3) * 0x21; }
        } else {
            if (d.bits == 1)      { C = 28; B = 0; }
            else                  { C = 13; B = (x & 1) * 0x42; }
        }
        int T = (tq * C + B) ^ A;
        u = (A & 0x20) | (T >> 2);
    }
    if (u > 32) ++u;
    return static_cast<uint8_t>(u);
}

inline uint8_t UnquantizeEndpoint(int rangeIdx, uint8_t v) {
    const BiseDesc& d = kBiseRanges[rangeIdx];
    if (!d.trits && !d.quints) {
        if (d.bits >= 8) return v;
        if (d.bits == 0) return 0;
        uint32_t x = v;
        uint32_t out = 0;
        for (int sh = 8 - d.bits; sh >= -8; sh -= d.bits) {
            out |= (sh >= 0) ? (x << sh) : (x >> (-sh));
        }
        return static_cast<uint8_t>(out & 0xFFu);
    }
    if (d.bits == 0) {
        if (d.trits) { static const uint8_t kT[3] = { 0, 128, 255 }; return kT[std::min<int>(v, 2)]; }
        static const uint8_t kQ[5] = { 0, 64, 128, 192, 255 };
        return kQ[std::min<int>(v, 4)];
    }
    int A = (v & 1) ? 0x1FF : 0x000;
    int B = 0, C = 0;
    int x = (v >> 1) & ((1 << (d.bits - 1)) - 1);
    int tq = v >> d.bits;
    if (d.trits) {
        switch (d.bits) {
        case 1: C = 204; B = 0; break;
        case 2: C = 93;  B = x * 278; break;
        case 3: C = 44;  B = (x << 7) | (x << 2) | (x >> 1); break;
        case 4: C = 22;  B = (x << 6) | x; break;
        case 5: C = 11;  B = (x << 5) | (x >> 2); break;
        default: C = 5;  B = (x << 4) | (x >> 4); break;
        }
    } else {
        switch (d.bits) {
        case 1: C = 113; B = 0; break;
        case 2: C = 54;  B = x * 268; break;
        case 3: C = 26;  B = (x << 7) | (x << 1) | (x >> 1); break;
        case 4: C = 13;  B = (x << 6) | (x >> 1); break;
        default: C = 6;  B = (x << 5) | (x >> 3); break;
        }
    }
    int T = (tq * C + B) ^ A;
    return static_cast<uint8_t>((A & 0x80) | (T >> 2));
}

inline uint32_t Hash52(uint32_t p) {
    p ^= p >> 15; p -= p << 17; p += p << 7; p += p << 4;
    p ^= p >> 5;  p += p << 16; p ^= p >> 7; p ^= p >> 3;
    p ^= p << 6;  p ^= p >> 17;
    return p;
}

inline int SelectPartition(int seed, int x, int y, int partitionCount, bool smallBlock) {
    if (smallBlock) { x <<= 1; y <<= 1; }
    seed += (partitionCount - 1) * 1024;
    uint32_t rnum = Hash52(static_cast<uint32_t>(seed));
    uint8_t s1 = static_cast<uint8_t>(rnum & 0xFu);
    uint8_t s2 = static_cast<uint8_t>((rnum >> 4) & 0xFu);
    uint8_t s3 = static_cast<uint8_t>((rnum >> 8) & 0xFu);
    uint8_t s4 = static_cast<uint8_t>((rnum >> 12) & 0xFu);
    uint8_t s5 = static_cast<uint8_t>((rnum >> 16) & 0xFu);
    uint8_t s6 = static_cast<uint8_t>((rnum >> 20) & 0xFu);
    uint8_t s7 = static_cast<uint8_t>((rnum >> 24) & 0xFu);
    uint8_t s8 = static_cast<uint8_t>((rnum >> 28) & 0xFu);

    s1 *= s1; s2 *= s2; s3 *= s3; s4 *= s4;
    s5 *= s5; s6 *= s6; s7 *= s7; s8 *= s8;

    int sh1, sh2;
    if (seed & 1) { sh1 = (seed & 2) ? 4 : 5; sh2 = (partitionCount == 3) ? 6 : 5; }
    else          { sh1 = (partitionCount == 3) ? 6 : 5; sh2 = (seed & 2) ? 4 : 5; }

    s1 >>= sh1; s2 >>= sh2; s3 >>= sh1; s4 >>= sh2;
    s5 >>= sh1; s6 >>= sh2; s7 >>= sh1; s8 >>= sh2;

    int a = (s1 * x + s2 * y + static_cast<int>(rnum >> 14)) & 0x3F;
    int b = (s3 * x + s4 * y + static_cast<int>(rnum >> 10)) & 0x3F;
    int c = (partitionCount >= 3) ? ((s5 * x + s6 * y + static_cast<int>(rnum >> 6)) & 0x3F) : 0;
    int d = (partitionCount >= 4) ? ((s7 * x + s8 * y + static_cast<int>(rnum >> 2)) & 0x3F) : 0;

    if (a >= b && a >= c && a >= d) return 0;
    if (b >= c && b >= d) return 1;
    if (c >= d) return 2;
    return 3;
}

inline void BitTransferSigned(int& a, int& b) {
    b >>= 1;
    b |= (a & 0x80);
    a = (a >> 1) & 0x3F;
    if (b & 0x80) b -= 0x100;
}

inline void BlueContract(int r, int g, int b, int a, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>(std::clamp((r + b) >> 1, 0, 255));
    out[1] = static_cast<uint8_t>(std::clamp((g + b) >> 1, 0, 255));
    out[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
    out[3] = static_cast<uint8_t>(std::clamp(a, 0, 255));
}

inline void DecodeLdrEndpoints(int cem, const uint8_t* v, uint8_t e0[4], uint8_t e1[4]) {
    switch (cem) {
    case 0: // LDR Luminance Direct
        e0[0] = e0[1] = e0[2] = v[0]; e0[3] = 255;
        e1[0] = e1[1] = e1[2] = v[1]; e1[3] = 255;
        break;
    case 1: { // LDR Luminance Base+Offset
        int l0 = (v[0] >> 2) | (v[1] & 0xC0);
        int l1 = std::min(255, l0 + (v[1] & 0x3F));
        e0[0] = e0[1] = e0[2] = static_cast<uint8_t>(l0); e0[3] = 255;
        e1[0] = e1[1] = e1[2] = static_cast<uint8_t>(l1); e1[3] = 255;
        break;
    }
    case 4: // LDR Luminance+Alpha Direct
        e0[0] = e0[1] = e0[2] = v[0]; e0[3] = v[2];
        e1[0] = e1[1] = e1[2] = v[1]; e1[3] = v[3];
        break;
    case 5: { // LDR Luminance+Alpha Base+Offset
        int v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];
        BitTransferSigned(v1, v0);
        BitTransferSigned(v3, v2);
        uint8_t l0 = static_cast<uint8_t>(std::clamp(v0, 0, 255));
        uint8_t l1 = static_cast<uint8_t>(std::clamp(v0 + v1, 0, 255));
        e0[0] = e0[1] = e0[2] = l0; e0[3] = static_cast<uint8_t>(std::clamp(v2, 0, 255));
        e1[0] = e1[1] = e1[2] = l1; e1[3] = static_cast<uint8_t>(std::clamp(v2 + v3, 0, 255));
        break;
    }
    case 6: // LDR RGB Base+Scale
        e0[0] = static_cast<uint8_t>((v[0] * v[3]) >> 8);
        e0[1] = static_cast<uint8_t>((v[1] * v[3]) >> 8);
        e0[2] = static_cast<uint8_t>((v[2] * v[3]) >> 8);
        e0[3] = 255;
        e1[0] = v[0]; e1[1] = v[1]; e1[2] = v[2]; e1[3] = 255;
        break;
    case 8: // LDR RGB Direct
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
            e0[0] = v[0]; e0[1] = v[2]; e0[2] = v[4]; e0[3] = 255;
            e1[0] = v[1]; e1[1] = v[3]; e1[2] = v[5]; e1[3] = 255;
        } else {
            BlueContract(v[1], v[3], v[5], 255, e0);
            BlueContract(v[0], v[2], v[4], 255, e1);
        }
        break;
    case 9: { // LDR RGB Base+Offset
        int p0 = v[0], p1 = v[1], p2 = v[2], p3 = v[3], p4 = v[4], p5 = v[5];
        BitTransferSigned(p1, p0);
        BitTransferSigned(p3, p2);
        BitTransferSigned(p5, p4);
        if (p1 + p3 + p5 >= 0) {
            e0[0] = static_cast<uint8_t>(std::clamp(p0, 0, 255));
            e0[1] = static_cast<uint8_t>(std::clamp(p2, 0, 255));
            e0[2] = static_cast<uint8_t>(std::clamp(p4, 0, 255));
            e0[3] = 255;
            e1[0] = static_cast<uint8_t>(std::clamp(p0 + p1, 0, 255));
            e1[1] = static_cast<uint8_t>(std::clamp(p2 + p3, 0, 255));
            e1[2] = static_cast<uint8_t>(std::clamp(p4 + p5, 0, 255));
            e1[3] = 255;
        } else {
            BlueContract(p0 + p1, p2 + p3, p4 + p5, 255, e0);
            BlueContract(p0, p2, p4, 255, e1);
        }
        break;
    }
    case 10: // LDR RGB Base+Scale + Two A
        e0[0] = static_cast<uint8_t>((v[0] * v[3]) >> 8);
        e0[1] = static_cast<uint8_t>((v[1] * v[3]) >> 8);
        e0[2] = static_cast<uint8_t>((v[2] * v[3]) >> 8);
        e0[3] = v[4];
        e1[0] = v[0]; e1[1] = v[1]; e1[2] = v[2]; e1[3] = v[5];
        break;
    case 12: // LDR RGBA Direct
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
            e0[0] = v[0]; e0[1] = v[2]; e0[2] = v[4]; e0[3] = v[6];
            e1[0] = v[1]; e1[1] = v[3]; e1[2] = v[5]; e1[3] = v[7];
        } else {
            BlueContract(v[1], v[3], v[5], v[7], e0);
            BlueContract(v[0], v[2], v[4], v[6], e1);
        }
        break;
    case 13: { // LDR RGBA Base+Offset
        int p0 = v[0], p1 = v[1], p2 = v[2], p3 = v[3], p4 = v[4], p5 = v[5], p6 = v[6], p7 = v[7];
        BitTransferSigned(p1, p0);
        BitTransferSigned(p3, p2);
        BitTransferSigned(p5, p4);
        BitTransferSigned(p7, p6);
        if (p1 + p3 + p5 >= 0) {
            e0[0] = static_cast<uint8_t>(std::clamp(p0, 0, 255));
            e0[1] = static_cast<uint8_t>(std::clamp(p2, 0, 255));
            e0[2] = static_cast<uint8_t>(std::clamp(p4, 0, 255));
            e0[3] = static_cast<uint8_t>(std::clamp(p6, 0, 255));
            e1[0] = static_cast<uint8_t>(std::clamp(p0 + p1, 0, 255));
            e1[1] = static_cast<uint8_t>(std::clamp(p2 + p3, 0, 255));
            e1[2] = static_cast<uint8_t>(std::clamp(p4 + p5, 0, 255));
            e1[3] = static_cast<uint8_t>(std::clamp(p6 + p7, 0, 255));
        } else {
            BlueContract(p0 + p1, p2 + p3, p4 + p5, p6 + p7, e0);
            BlueContract(p0, p2, p4, p6, e1);
        }
        break;
    }
    default: {
        // HDR CEM fallback (2, 3, 7, 11, 14, 15): map available pairs linearly
        int n = 2 * ((cem >> 2) + 1);
        e0[0] = v[0]; e1[0] = v[1];
        e0[1] = (n >= 4) ? v[2] : v[0]; e1[1] = (n >= 4) ? v[3] : v[1];
        e0[2] = (n >= 6) ? v[4] : e0[0]; e1[2] = (n >= 6) ? v[5] : e1[0];
        e0[3] = (n >= 8) ? v[6] : 255;   e1[3] = (n >= 8) ? v[7] : 255;
        break;
    }
    }
}

inline void DecodeAstcBlock(
    const uint8_t* ptr,
    uint32_t blockX,
    uint32_t blockY,
    uint32_t bx,
    uint32_t by,
    uint32_t imgW,
    uint32_t imgH,
    std::vector<uint8_t>& dstBgra)
{
    uint64_t lo = ReadU64LE(ptr);
    uint64_t hi = ReadU64LE(ptr + 8);

    auto getBit = [&](int pos) -> int {
        if (pos < 0 || pos >= 128) return 0;
        return (pos < 64) ? static_cast<int>((lo >> pos) & 1u)
                          : static_cast<int>((hi >> (pos - 64)) & 1u);
    };
    auto getBits = [&](int pos, int count) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) v |= (static_cast<uint32_t>(getBit(pos + i)) << i);
        return v;
    };

    // 1. Void-Extent Constant-Color Block ((lo & 0x1FF) == 0x1FC)
    if ((lo & 0x1FFu) == 0x1FCu) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if ((lo & 0x200u) == 0) {
            r = ptr[9]; g = ptr[11]; b = ptr[13]; a = ptr[15];
        } else {
            auto mapH = [](uint16_t h16) -> uint8_t {
                float f = GpuTex::HalfToFloat(h16);
                if (!std::isfinite(f) || f <= 0.0f) return 0;
                if (f > 1.0f) f = f / (1.0f + f);
                return static_cast<uint8_t>(std::clamp(f * 255.0f + 0.5f, 0.0f, 255.0f));
            };
            r = mapH(ReadU16LE(ptr + 8));
            g = mapH(ReadU16LE(ptr + 10));
            b = mapH(ReadU16LE(ptr + 12));
            a = mapH(ReadU16LE(ptr + 14));
        }
        for (uint32_t py = 0; py < blockY; ++py) {
            uint32_t dy = by * blockY + py;
            if (dy >= imgH) continue;
            for (uint32_t px = 0; px < blockX; ++px) {
                uint32_t dx = bx * blockX + px;
                if (dx >= imgW) continue;
                WritePremulBGRA(&dstBgra[(static_cast<size_t>(dy) * imgW + dx) * 4], r, g, b, a);
            }
        }
        return;
    }

    int gridW = 0, gridH = 0, wtRangeIdx = 0;
    bool dualPlane = false;
    uint32_t bm = static_cast<uint32_t>(lo & 0x7FFu);
    int numPartitions = static_cast<int>((lo >> 11) & 3u) + 1;

    bool validMode = DecodeBlockMode2D(bm, gridW, gridH, wtRangeIdx, dualPlane);
    int numWeights = gridW * gridH * (dualPlane ? 2 : 1);
    int wtBits = validMode ? BiseBitCount(wtRangeIdx, numWeights) : 0;
    if (!validMode || numWeights > 64 || wtBits < 24 || wtBits > 96 || (dualPlane && numPartitions == 4)) {
        return;
    }

    int partSeed = 0;
    int cems[4] = {};
    int epStartBit = 17;
    int configTopBit = 128 - wtBits;

    if (numPartitions == 1) {
        cems[0] = static_cast<int>(getBits(13, 4));
        epStartBit = 17;
    } else {
        partSeed = static_cast<int>(getBits(13, 10));
        uint32_t cemField = getBits(23, 6);
        epStartBit = 29;
        uint32_t cemBase = cemField & 3u;
        if (cemBase == 0) {
            int sharedCem = static_cast<int>((cemField >> 2) & 0xFu);
            for (int p = 0; p < numPartitions; ++p) cems[p] = sharedCem;
        } else {
            int extraCemBits = 3 * numPartitions - 4;
            configTopBit -= extraCemBits;
            if (configTopBit < epStartBit) return;
            uint32_t extra = getBits(configTopBit, extraCemBits);
            uint32_t fullCem = (extra << 6) | cemField;
            uint8_t cBits[4] = {}, mBits[4] = {};
            for (int p = 0; p < numPartitions; ++p) {
                cBits[p] = static_cast<uint8_t>((fullCem >> (2 + p)) & 1u);
            }
            int mStart = 2 + numPartitions;
            for (int p = 0; p < numPartitions; ++p) {
                mBits[p] = static_cast<uint8_t>((fullCem >> (mStart + 2 * p)) & 3u);
                int cls = static_cast<int>(cemBase) - 1 + cBits[p];
                cems[p] = (cls << 2) | mBits[p];
            }
        }
    }

    int ccs = 0;
    if (dualPlane) {
        configTopBit -= 2;
        if (configTopBit < epStartBit) return;
        ccs = static_cast<int>(getBits(configTopBit, 2));
    }

    int totalEpVals = 0;
    for (int p = 0; p < numPartitions; ++p) {
        totalEpVals += 2 * ((cems[p] >> 2) + 1);
    }
    int availEpBits = configTopBit - epStartBit;
    int epRangeIdx = -1;
    for (int r = 20; r >= 0; --r) {
        if (BiseBitCount(r, totalEpVals) <= availEpBits) {
            epRangeIdx = r;
            break;
        }
    }
    if (epRangeIdx < 0) return;

    uint8_t rawEp[32] = {};
    int epBitCursor = epStartBit;
    UnpackBise(epRangeIdx, totalEpVals, [&]() -> int {
        return (epBitCursor < configTopBit) ? getBit(epBitCursor++) : 0;
    }, rawEp);
    for (int i = 0; i < totalEpVals; ++i) {
        rawEp[i] = UnquantizeEndpoint(epRangeIdx, rawEp[i]);
    }

    uint8_t e0[4][4] = {}, e1[4][4] = {};
    int epCursor = 0;
    for (int p = 0; p < numPartitions; ++p) {
        DecodeLdrEndpoints(cems[p], rawEp + epCursor, e0[p], e1[p]);
        epCursor += 2 * ((cems[p] >> 2) + 1);
    }

    uint8_t rawWt[64] = {};
    int wtBitCursor = 0;
    UnpackBise(wtRangeIdx, numWeights, [&]() -> int {
        int b = (wtBitCursor < wtBits) ? getBit(127 - wtBitCursor) : 0;
        ++wtBitCursor;
        return b;
    }, rawWt);
    for (int i = 0; i < numWeights; ++i) {
        rawWt[i] = UnquantizeWeight(wtRangeIdx, rawWt[i]);
    }

    bool smallBlock = (blockX * blockY < 31);
    int ds = (blockX > 1) ? static_cast<int>((1024u + blockX / 2u) / (blockX - 1u)) : 0;
    int dt = (blockY > 1) ? static_cast<int>((1024u + blockY / 2u) / (blockY - 1u)) : 0;

    auto sampleGrid = [&](int px, int py, int plane) -> int {
        int cs = ds * px;
        int ct = dt * py;
        int gs = (cs * (gridW - 1) + 32) >> 6;
        int gt = (ct * (gridH - 1) + 32) >> 6;
        int js = gs >> 4, fs = gs & 0xF;
        int jt = gt >> 4, ft = gt & 0xF;
        int stride = dualPlane ? 2 : 1;
        int v0 = js + jt * gridW;
        int p00 = rawWt[v0 * stride + plane];
        int p01 = (js + 1 < gridW) ? rawWt[(v0 + 1) * stride + plane] : p00;
        int p10 = (jt + 1 < gridH) ? rawWt[(v0 + gridW) * stride + plane] : p00;
        int p11 = (js + 1 < gridW && jt + 1 < gridH) ? rawWt[(v0 + gridW + 1) * stride + plane] : p00;
        int w11 = (fs * ft + 8) >> 4;
        int w10 = ft - w11;
        int w01 = fs - w11;
        int w00 = 16 - fs - ft + w11;
        return (p00 * w00 + p01 * w01 + p10 * w10 + p11 * w11 + 8) >> 4;
    };

    for (uint32_t py = 0; py < blockY; ++py) {
        uint32_t dy = by * blockY + py;
        if (dy >= imgH) continue;
        for (uint32_t px = 0; px < blockX; ++px) {
            uint32_t dx = bx * blockX + px;
            if (dx >= imgW) continue;
            int part = (numPartitions > 1) ? SelectPartition(partSeed, static_cast<int>(px), static_cast<int>(py), numPartitions, smallBlock) : 0;
            int w0 = sampleGrid(static_cast<int>(px), static_cast<int>(py), 0);
            int w1 = dualPlane ? sampleGrid(static_cast<int>(px), static_cast<int>(py), 1) : w0;

            uint8_t ch[4];
            for (int c = 0; c < 4; ++c) {
                int w = (dualPlane && c == ccs) ? w1 : w0;
                ch[c] = static_cast<uint8_t>((e0[part][c] * (64 - w) + e1[part][c] * w + 32) >> 6);
            }
            WritePremulBGRA(&dstBgra[(static_cast<size_t>(dy) * imgW + dx) * 4], ch[0], ch[1], ch[2], ch[3]);
        }
    }
}

} // namespace DetailAstc

inline bool DecodeAstcSurface(
    const uint8_t* blocks,
    size_t blocksBytes,
    uint32_t w,
    uint32_t h,
    uint32_t blockX,
    uint32_t blockY,
    std::vector<uint8_t>& outBgra)
{
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || blockX == 0 || blockY == 0 || blockX > 12 || blockY > 12) {
        return false;
    }
    uint32_t bw = (w + blockX - 1u) / blockX;
    uint32_t bh = (h + blockY - 1u) / blockY;
    size_t needed = static_cast<size_t>(bw) * bh * 16u;
    if (blocksBytes < needed) return false;

    outBgra.assign(static_cast<size_t>(w) * h * 4u, 0);
    const uint8_t* ptr = blocks;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            DetailAstc::DecodeAstcBlock(ptr, blockX, blockY, bx, by, w, h, outBgra);
            ptr += 16;
        }
    }
    return true;
}

inline bool DecodeASTC(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 32 || data[0] != 0x13 || data[1] != 0xAB || data[2] != 0xA1 || data[3] != 0x5C) return false;
    uint32_t blockX = data[4];
    uint32_t blockY = data[5];
    uint32_t blockZ = data[6];
    if (blockX < 4 || blockX > 12 || blockY < 4 || blockY > 12 || blockZ == 0 || blockZ > 6) return false;

    uint32_t w = static_cast<uint32_t>(data[7]) | (static_cast<uint32_t>(data[8]) << 8) | (static_cast<uint32_t>(data[9]) << 16);
    uint32_t h = static_cast<uint32_t>(data[10]) | (static_cast<uint32_t>(data[11]) << 8) | (static_cast<uint32_t>(data[12]) << 16);
    uint32_t z = static_cast<uint32_t>(data[13]) | (static_cast<uint32_t>(data[14]) << 8) | (static_cast<uint32_t>(data[15]) << 16);
    if (w == 0 || h == 0 || z == 0 || w > 8192 || h > 8192) return false;

    if (!DecodeAstcSurface(data + 16, size - 16, w, h, blockX, blockY, outInfo.bgraPixels)) {
        return false;
    }
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ARM ASTC Texture (" + std::to_wstring(blockX) + L"x" + std::to_wstring(blockY) + L")";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
