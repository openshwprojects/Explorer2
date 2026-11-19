#pragma once
// Jpeg2000.h — #5 Pure-C++20 JPEG 2000 Part 1 Codestream & JP2/JPX/JPF Box Decoder
//              (.jp2, .j2k, .jpf, .jpx, .j2c)
//
// Zero-cheat ISO/IEC 15444-1 (ITU-T T.800) implementation:
//   - JP2 / JPX / JPF ISO Box Parser ('jP  ' signature, 'ftyp', 'jp2h' -> 'ihdr', 'colr',
//     'pclr' palette LUT, 'cmap' component mapping, 'cdef' alpha/channel roles, 'jp2c' codestream)
//   - Full Codestream Marker Parser: SOC(0xFF4F), SIZ(0xFF51), COD(0xFF52), COC(0xFF53),
//     QCD(0xFF5C), QCC(0xFF5D), RGN(0xFF5E), POC(0xFF5F), COM(0xFF64), SOT(0xFF90),
//     SOP(0xFF91), EPH(0xFF92), SOD(0xFF93), EOC(0xFFD9)
//   - Tier-2 Packet Header Decoder: 0xFF->7-bit de-stuffing, Inclusion & Zero-Bitplane Tag Trees,
//     Lblock comma codes, multi-layer LRCP / RLCP / RPCL / PCRL / CPRL progressions
//   - Tier-1 EBCOT + 47-State MQ Context-Adaptive Arithmetic Decoder (ITU-T T.800 Annex C & D):
//     19 contexts (ZC, SC, MR, RLC, UNIFORM), Significance Propagation, Magnitude Refinement & Cleanup passes
//   - 2D Inverse Discrete Wavelet Transform (IDWT):
//     Reversible 5/3 integer lifting & Irreversible Daubechies 9/7 float lifting (multi-level NL = 0..10)
//   - Multi-Component Transforms: Reversible Color Transform (RCT) & Irreversible Color Transform (ICT),
//     sYCC ('colr' enumCS=18), CMYK ('colr' enumCS=12), 1..16-bit depth, signed/unsigned DC level shift.

namespace Formats {

namespace J2kDetail {

// big-endian loads (ByteIO.h)
inline uint16_t RdBE16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16BE(p); }
inline uint32_t RdBE32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32BE(p); }
inline uint64_t RdBE64(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64BE(p); }

inline int32_t CeilDivI32(int32_t a, int32_t b) {
    if (b <= 0) return 0;
    if (a >= 0) return (a + b - 1) / b;
    return -((-a) / b);
}

inline int FloorLog2U32(uint32_t v) {
    int r = 0;
    while (v > 1u) { v >>= 1; ++r; }
    return r;
}

// ============================================================================
// ITU-T T.800 Annex B.10.2 Tag Tree (2D Hierarchical Min-Value Prefix Tree)
// ============================================================================
struct J2kTagTree {
    struct Level {
        uint32_t w = 0, h = 0;
        size_t offset = 0;
    };
    std::vector<Level> levels;
    std::vector<int32_t> vals;
    std::vector<uint8_t> known;

    void Init(uint32_t width, uint32_t height) {
        levels.clear();
        vals.clear();
        known.clear();
        if (width == 0 || height == 0) return;
        uint32_t cw = width, ch = height;
        size_t total = 0;
        while (true) {
            levels.push_back({ cw, ch, total });
            total += static_cast<size_t>(cw) * ch;
            if (cw == 1 && ch == 1) break;
            cw = (cw + 1u) >> 1;
            ch = (ch + 1u) >> 1;
        }
        vals.assign(total, 0);
        known.assign(total, 0);
    }

    template <typename BitFn>
    int32_t Decode(uint32_t x, uint32_t y, int32_t threshold, BitFn&& readBit) {
        if (levels.empty()) return threshold;
        int32_t curMin = 0;
        for (int lv = static_cast<int>(levels.size()) - 1; lv >= 0; --lv) {
            uint32_t lx = x >> lv;
            uint32_t ly = y >> lv;
            const auto& L = levels[lv];
            size_t idx = L.offset + static_cast<size_t>(ly) * L.w + lx;
            if (vals[idx] < curMin) vals[idx] = curMin;
            while (!known[idx] && vals[idx] < threshold) {
                int b = readBit();
                if (b < 0) return -1;
                if (b == 0) {
                    vals[idx]++;
                } else {
                    known[idx] = 1;
                }
            }
            curMin = vals[idx];
        }
        return curMin;
    }
};

// ============================================================================
// Packet Header Bit Reader (ITU-T T.800 B.10.1: 0xFF followed by 7-bit byte)
// ============================================================================
struct PacketBitReader {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;
    uint8_t curByte = 0;
    int bitsLeft = 0;
    bool prevWasFF = false;

    PacketBitReader(const uint8_t* b, size_t n, size_t startPos)
        : buf(b), len(n), pos(startPos) {}

    int ReadBit() {
        if (bitsLeft == 0) {
            if (pos >= len) return -1;
            curByte = buf[pos++];
            if (prevWasFF) {
                // Bit 7 must be 0 (stuffed bit); if 1, a marker prematurely occurred
                if ((curByte & 0x80u) != 0) return -1;
                bitsLeft = 7;
                prevWasFF = false;
            } else {
                bitsLeft = 8;
                prevWasFF = (curByte == 0xFFu);
            }
        }
        --bitsLeft;
        return (curByte >> bitsLeft) & 1;
    }

    int32_t ReadBits(int n) {
        int32_t v = 0;
        for (int i = 0; i < n; ++i) {
            int b = ReadBit();
            if (b < 0) return -1;
            v = (v << 1) | b;
        }
        return v;
    }

    bool FinishHeader() {
        if (prevWasFF) {
            if (pos >= len) return false;
            uint8_t nxt = buf[pos++];
            if ((nxt & 0x80u) != 0) return false;
            prevWasFF = false;
        }
        bitsLeft = 0;
        return true;
    }
};

// ============================================================================
// ITU-T T.800 Annex C: 47-State MQ Context-Adaptive Arithmetic Decoder
// ============================================================================
struct MqDecoder {
    struct StateEntry {
        uint16_t qe;
        uint8_t nmps;
        uint8_t nlps;
        uint8_t sw;
    };
    static const StateEntry* Table() {
        static const StateEntry kTbl[47] = {
            {0x5601,  1,  1, 1}, {0x3401,  2,  6, 0}, {0x1801,  3,  9, 0}, {0x0AC1,  4, 12, 0},
            {0x0521,  5, 29, 0}, {0x0221, 38, 33, 0}, {0x5601,  7,  6, 1}, {0x5401,  8, 14, 0},
            {0x4801,  9, 14, 0}, {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0},
            {0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1}, {0x5401, 16, 14, 0},
            {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0},
            {0x3001, 21, 19, 0}, {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0},
            {0x1C01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0},
            {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0}, {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0},
            {0x08A1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0}, {0x02A1, 36, 33, 0},
            {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0},
            {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0}, {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0},
            {0x0005, 45, 42, 0}, {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0}
        };
        return kTbl;
    }

    uint8_t stateIdx[19] = {};
    uint8_t mpsVal[19] = {};
    const uint8_t* dataPtr = nullptr;
    size_t dataLen = 0;
    size_t bpIdx = 0;
    uint32_t regA = 0x8000u;
    uint32_t regC = 0;
    int ct = 0;

    void ResetContexts() {
        std::memset(stateIdx, 0, sizeof(stateIdx));
        std::memset(mpsVal, 0, sizeof(mpsVal));
        stateIdx[0]  = 4;  // ZC all-zero neighbors
        stateIdx[17] = 3;  // RLC
        stateIdx[18] = 46; // UNIFORM
    }

    uint8_t PeekByte(size_t idx) const {
        return (idx < dataLen) ? dataPtr[idx] : 0xFFu;
    }

    void ByteIn() {
        uint8_t cur = PeekByte(bpIdx);
        if (cur == 0xFFu) {
            uint8_t b1 = PeekByte(bpIdx + 1u);
            if (b1 > 0x8Fu) {
                regC += 0xFF00u;
                ct = 8;
            } else {
                if (bpIdx < dataLen) ++bpIdx;
                regC += static_cast<uint32_t>(PeekByte(bpIdx)) << 9;
                ct = 7;
            }
        } else {
            if (bpIdx < dataLen) ++bpIdx;
            regC += static_cast<uint32_t>(PeekByte(bpIdx)) << 8;
            ct = 8;
        }
    }

    void InitSegment(const uint8_t* data, size_t len) {
        dataPtr = data;
        dataLen = len;
        bpIdx = 0;
        regC = static_cast<uint32_t>(PeekByte(0)) << 16;
        ByteIn();
        regC <<= 7;
        ct -= 7;
        regA = 0x8000u;
    }

    void RenormD() {
        do {
            if (ct == 0) ByteIn();
            regA <<= 1;
            regC <<= 1;
            --ct;
        } while ((regA & 0x8000u) == 0);
    }

    uint32_t Decode(int cx) {
        const auto& st = Table()[stateIdx[cx]];
        uint32_t qe = st.qe;
        regA -= qe;
        uint32_t d = mpsVal[cx];
        if ((regC >> 16) >= qe) {
            regC -= (qe << 16);
            if ((regA & 0x8000u) == 0) {
                if (regA < qe) {
                    d = 1u - d;
                    if (st.sw) mpsVal[cx] ^= 1u;
                    stateIdx[cx] = st.nlps;
                } else {
                    stateIdx[cx] = st.nmps;
                }
                RenormD();
            }
        } else {
            if (regA < qe) {
                regA = qe;
                stateIdx[cx] = st.nmps;
            } else {
                regA = qe;
                d = 1u - d;
                if (st.sw) mpsVal[cx] ^= 1u;
                stateIdx[cx] = st.nlps;
            }
            RenormD();
        }
        return d;
    }
};

// Raw Bypass Bit Reader (for selective arithmetic coding bypass pass segments, cbStyle & 0x01)
struct RawBypassReader {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;
    uint8_t c = 0;
    int ct = 0;
    bool prevFF = false;

    void Init(const uint8_t* b, size_t n) { buf = b; len = n; pos = 0; c = 0; ct = 0; prevFF = false; }

    uint32_t ReadBit() {
        if (ct == 0) {
            c = (pos < len) ? buf[pos++] : 0xFFu;
            if (prevFF) {
                if (c > 0x8Fu) { c = 0xFFu; ct = 8; prevFF = true; }
                else           { ct = 7; prevFF = false; }
            } else {
                ct = 8;
                prevFF = (c == 0xFFu);
            }
        }
        --ct;
        return (c >> ct) & 1u;
    }
};

// ============================================================================
// Tier-1 EBCOT Code-Block Decoder (ITU-T T.800 Annex D)
// ============================================================================
struct CodeBlockSeg {
    std::vector<uint8_t> bytes;
    uint32_t numPasses = 0;
    bool isRawBypass = false;
};

struct CodeBlockState {
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool included = false;
    int32_t zeroBitPlanes = 0;
    int32_t lblock = 3;
    uint32_t totalPasses = 0;
    std::vector<CodeBlockSeg> segments;
};

// Subband type: 0 = LL, 1 = HL (horiz high, vert low), 2 = LH (horiz low, vert high), 3 = HH
inline bool DecodeEbcotCodeBlock(
    const CodeBlockState& cb,
    int subbandType,
    int32_t maxBitPlanes,
    uint8_t cbStyle,
    std::vector<int32_t>& outQuantSignMag, // signed fixed-point shifted so LSB of bit-plane 0 == 1 (<< 1 for half-LSB)
    int32_t& outDecodedLowestBp)
{
    uint32_t w = static_cast<uint32_t>(cb.x1 - cb.x0);
    uint32_t h = static_cast<uint32_t>(cb.y1 - cb.y0);
    outQuantSignMag.assign(static_cast<size_t>(w) * h, 0);
    outDecodedLowestBp = maxBitPlanes;
    if (!cb.included || cb.totalPasses == 0 || w == 0 || h == 0 || cb.segments.empty()) return true;
    if (cb.zeroBitPlanes >= maxBitPlanes) return true;

    // Padded 2D state grid (w+2) x (h+2) so 8-neighbor lookups need zero bounds branches!
    uint32_t stride = w + 2u;
    size_t gridSz = static_cast<size_t>(stride) * (h + 2u);
    std::vector<uint8_t> flags(gridSz, 0); // bit0=sigma, bit1=chi(sign), bit2=pi(visited), bit3=refined
    std::vector<int32_t> mag(static_cast<size_t>(w) * h, 0);
    bool vsc = (cbStyle & 0x08u) != 0;

    auto getSigma = [&](uint32_t gx, uint32_t gy, bool clampBottomStripe, uint32_t stripeBottomGy) -> int {
        if (clampBottomStripe && gy > stripeBottomGy) return 0;
        return flags[static_cast<size_t>(gy) * stride + gx] & 1;
    };

    auto zcContext = [&](uint32_t gx, uint32_t gy, bool clampBot, uint32_t botGy) -> int {
        int hSum = getSigma(gx - 1, gy, clampBot, botGy) + getSigma(gx + 1, gy, clampBot, botGy);
        int vSum = getSigma(gx, gy - 1, clampBot, botGy) + getSigma(gx, gy + 1, clampBot, botGy);
        int dSum = getSigma(gx - 1, gy - 1, clampBot, botGy) + getSigma(gx + 1, gy - 1, clampBot, botGy) +
                   getSigma(gx - 1, gy + 1, clampBot, botGy) + getSigma(gx + 1, gy + 1, clampBot, botGy);
        if (subbandType == 1) std::swap(hSum, vSum); // HL swaps h and v!
        if (subbandType == 3) { // HH
            int hv = hSum + vSum;
            if (dSum >= 3) return 8;
            if (dSum == 2) return (hv >= 1) ? 7 : 6;
            if (dSum == 1) return (hv >= 2) ? 5 : (hv == 1 ? 4 : 3);
            return (hv >= 2) ? 2 : (hv == 1 ? 1 : 0);
        }
        if (hSum == 2) return 8;
        if (hSum == 1) return (vSum >= 1) ? 7 : (dSum >= 1 ? 6 : 5);
        if (vSum == 2) return 4;
        if (vSum == 1) return 3;
        return (dSum >= 2) ? 2 : (dSum == 1 ? 1 : 0);
    };

    auto scContext = [&](uint32_t gx, uint32_t gy, bool clampBot, uint32_t botGy, uint32_t& outXor) -> int {
        auto contrib = [&](uint32_t nx, uint32_t ny) -> int {
            if (clampBot && ny > botGy) return 0;
            uint8_t f = flags[static_cast<size_t>(ny) * stride + nx];
            if ((f & 1u) == 0) return 0;
            return ((f & 2u) != 0) ? -1 : 1;
        };
        int hc = std::clamp(contrib(gx - 1, gy) + contrib(gx + 1, gy), -1, 1);
        int vc = std::clamp(contrib(gx, gy - 1) + contrib(gx, gy + 1), -1, 1);
        outXor = 0;
        if (hc < 0 || (hc == 0 && vc < 0)) {
            hc = -hc; vc = -vc; outXor = 1;
        }
        if (hc == 1) return (vc == 1) ? 13 : (vc == 0 ? 12 : 11);
        return (vc == 1) ? 10 : 9;
    };

    auto mrContext = [&](uint32_t gx, uint32_t gy, bool clampBot, uint32_t botGy) -> int {
        uint8_t f = flags[static_cast<size_t>(gy) * stride + gx];
        if ((f & 8u) != 0) return 16;
        int sum8 = getSigma(gx - 1, gy, clampBot, botGy) + getSigma(gx + 1, gy, clampBot, botGy) +
                   getSigma(gx, gy - 1, clampBot, botGy) + getSigma(gx, gy + 1, clampBot, botGy) +
                   getSigma(gx - 1, gy - 1, clampBot, botGy) + getSigma(gx + 1, gy - 1, clampBot, botGy) +
                   getSigma(gx - 1, gy + 1, clampBot, botGy) + getSigma(gx + 1, gy + 1, clampBot, botGy);
        return (sum8 > 0) ? 15 : 14;
    };

    auto hasSigNeighbor = [&](uint32_t gx, uint32_t gy, bool clampBot, uint32_t botGy) -> bool {
        return (getSigma(gx - 1, gy, clampBot, botGy) | getSigma(gx + 1, gy, clampBot, botGy) |
                getSigma(gx, gy - 1, clampBot, botGy) | getSigma(gx, gy + 1, clampBot, botGy) |
                getSigma(gx - 1, gy - 1, clampBot, botGy) | getSigma(gx + 1, gy - 1, clampBot, botGy) |
                getSigma(gx - 1, gy + 1, clampBot, botGy) | getSigma(gx + 1, gy + 1, clampBot, botGy)) != 0;
    };

    MqDecoder mq;
    RawBypassReader rawBr;
    mq.ResetContexts();

    int32_t bp = maxBitPlanes - 1 - cb.zeroBitPlanes;
    int passType = 2; // 0 = SigProp, 1 = MagRef, 2 = Cleanup (first pass is ALWAYS Cleanup!)
    uint32_t globalPassIdx = 0;

    for (const auto& seg : cb.segments) {
        if (seg.isRawBypass) rawBr.Init(seg.bytes.data(), seg.bytes.size());
        else                 mq.InitSegment(seg.bytes.data(), seg.bytes.size());

        for (uint32_t pInSeg = 0; pInSeg < seg.numPasses && bp >= 0; ++pInSeg, ++globalPassIdx) {
            int32_t bitMask = (1 << bp);

            if (passType == 0) { // Significance Propagation Pass
                for (uint32_t y0 = 0; y0 < h; y0 += 4) {
                    uint32_t sH = std::min<uint32_t>(4u, h - y0);
                    uint32_t botGy = y0 + sH;
                    for (uint32_t x = 0; x < w; ++x) {
                        uint32_t gx = x + 1u;
                        for (uint32_t dy = 0; dy < sH; ++dy) {
                            uint32_t y = y0 + dy, gy = y + 1u;
                            bool clampBot = vsc && (dy + 1u == sH);
                            uint8_t& fl = flags[static_cast<size_t>(gy) * stride + gx];
                            if ((fl & 1u) == 0 && hasSigNeighbor(gx, gy, clampBot, botGy)) {
                                uint32_t sig = seg.isRawBypass ? rawBr.ReadBit()
                                                               : mq.Decode(zcContext(gx, gy, clampBot, botGy));
                                fl |= 4u; // visited (pi = 1)
                                if (sig) {
                                    uint32_t xorBit = 0;
                                    int sc = scContext(gx, gy, clampBot, botGy, xorBit);
                                    uint32_t sign = seg.isRawBypass ? rawBr.ReadBit() : (mq.Decode(sc) ^ xorBit);
                                    fl |= static_cast<uint8_t>(1u | (sign ? 2u : 0u));
                                    mag[static_cast<size_t>(y) * w + x] |= bitMask;
                                }
                            }
                        }
                    }
                }
            } else if (passType == 1) { // Magnitude Refinement Pass
                for (uint32_t y0 = 0; y0 < h; y0 += 4) {
                    uint32_t sH = std::min<uint32_t>(4u, h - y0);
                    uint32_t botGy = y0 + sH;
                    for (uint32_t x = 0; x < w; ++x) {
                        uint32_t gx = x + 1u;
                        for (uint32_t dy = 0; dy < sH; ++dy) {
                            uint32_t y = y0 + dy, gy = y + 1u;
                            bool clampBot = vsc && (dy + 1u == sH);
                            uint8_t& fl = flags[static_cast<size_t>(gy) * stride + gx];
                            if ((fl & 1u) != 0 && (fl & 4u) == 0) {
                                uint32_t mr = seg.isRawBypass ? rawBr.ReadBit()
                                                              : mq.Decode(mrContext(gx, gy, clampBot, botGy));
                                fl |= 8u; // refined = 1
                                if (mr) mag[static_cast<size_t>(y) * w + x] |= bitMask;
                            }
                        }
                    }
                }
            } else { // Cleanup Pass (always MQ-coded, never raw bypass)
                for (uint32_t y0 = 0; y0 < h; y0 += 4) {
                    uint32_t sH = std::min<uint32_t>(4u, h - y0);
                    uint32_t botGy = y0 + sH;
                    for (uint32_t x = 0; x < w; ++x) {
                        uint32_t gx = x + 1u;
                        uint32_t dy = 0;
                        if (sH == 4) {
                            bool allEligible = true;
                            for (uint32_t k = 0; k < 4; ++k) {
                                uint32_t gy = y0 + k + 1u;
                                uint8_t fl = flags[static_cast<size_t>(gy) * stride + gx];
                                bool clampBot = vsc && (k == 3);
                                if ((fl & 5u) != 0 || hasSigNeighbor(gx, gy, clampBot, botGy)) {
                                    allEligible = false;
                                    break;
                                }
                            }
                            if (allEligible) {
                                uint32_t rlc = mq.Decode(17);
                                if (rlc == 0) {
                                    continue; // all 4 samples stay 0
                                }
                                uint32_t rlcB0 = mq.Decode(18);
                                uint32_t rlcB1 = mq.Decode(18);
                                uint32_t runOff = (rlcB0 << 1) | rlcB1;
                                dy = runOff;
                                uint32_t y = y0 + dy, gy = y + 1u;
                                bool clampBot = vsc && (dy == 3);
                                uint32_t xorBit = 0;
                                int sc = scContext(gx, gy, clampBot, botGy, xorBit);
                                uint32_t sign = mq.Decode(sc) ^ xorBit;
                                flags[static_cast<size_t>(gy) * stride + gx] |= static_cast<uint8_t>(1u | (sign ? 2u : 0u));
                                mag[static_cast<size_t>(y) * w + x] |= bitMask;
                                ++dy;
                            }
                        }
                        for (; dy < sH; ++dy) {
                            uint32_t y = y0 + dy, gy = y + 1u;
                            bool clampBot = vsc && (dy + 1u == sH);
                            uint8_t& fl = flags[static_cast<size_t>(gy) * stride + gx];
                            if ((fl & 5u) == 0) {
                                uint32_t sig = mq.Decode(zcContext(gx, gy, clampBot, botGy));
                                if (sig) {
                                    uint32_t xorBit = 0;
                                    int sc = scContext(gx, gy, clampBot, botGy, xorBit);
                                    uint32_t sign = mq.Decode(sc) ^ xorBit;
                                    fl |= static_cast<uint8_t>(1u | (sign ? 2u : 0u));
                                    mag[static_cast<size_t>(y) * w + x] |= bitMask;
                                }
                            }
                            fl &= ~4u; // clear visited pi flag at end of bit-plane
                        }
                    }
                }
                if ((cbStyle & 0x20u) != 0) { // SEGSYM: 4 uniform bits == 0xA
                    uint32_t s0 = mq.Decode(18);
                    uint32_t s1 = mq.Decode(18);
                    uint32_t s2 = mq.Decode(18);
                    uint32_t s3 = mq.Decode(18);
                    uint32_t sym = (s0 << 3) | (s1 << 2) | (s2 << 1) | s3;
                    (void)sym;
                }
            }

            if ((cbStyle & 0x02u) != 0) mq.ResetContexts(); // RESET contexts after each pass

            outDecodedLowestBp = bp;
            if (passType == 2) {
                --bp;
                passType = 0;
            } else {
                ++passType;
            }
        }
    }

    // Store signed magnitudes (if truncated above bp=0, add half-LSB midpoint for significant samples)
    int32_t halfMid = (outDecodedLowestBp > 0) ? (1 << (outDecodedLowestBp - 1)) : 0;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t fl = flags[static_cast<size_t>(y + 1u) * stride + (x + 1u)];
            if ((fl & 1u) != 0) {
                int32_t v = mag[static_cast<size_t>(y) * w + x] + halfMid;
                outQuantSignMag[static_cast<size_t>(y) * w + x] = ((fl & 2u) != 0) ? -v : v;
            }
        }
    }
    return true;
}

// ============================================================================
// 1D & 2D Reversible 5/3 & Irreversible 9/7 Inverse DWT (ITU-T T.800 Annex F)
// ============================================================================
inline void Idwt53_1D(const int32_t* low, int32_t nLow, const int32_t* high, int32_t nHigh, int cas, int32_t* out) {
    int32_t total = nLow + nHigh;
    if (total <= 0) return;
    if (total == 1) {
        out[0] = (cas == 0) ? low[0] : (high[0] / 2);
        return;
    }
    auto getH = [&](int32_t idx) -> int32_t {
        if (idx < 0) idx = -idx - (cas == 0 ? 1 : 0);
        if (idx >= nHigh) idx = 2 * nHigh - 1 - idx - (cas == 0 ? 0 : 1);
        idx = std::clamp(idx, 0, nHigh - 1);
        return high[idx];
    };
    if (cas == 0) {
        // Even positions are low[0..nLow-1], odd positions are high[0..nHigh-1]
        for (int32_t i = 0; i < nLow; ++i) {
            out[2 * i] = low[i] - ((getH(i - 1) + getH(i) + 2) >> 2);
        }
        for (int32_t i = 0; i < nHigh; ++i) {
            int32_t e0 = out[2 * i];
            int32_t e1 = (2 * i + 2 < total) ? out[2 * i + 2] : e0;
            out[2 * i + 1] = high[i] + ((e0 + e1) >> 1);
        }
    } else {
        // cas == 1: first sample is high-pass (odd global coordinate)
        for (int32_t i = 0; i < nLow; ++i) {
            out[2 * i + 1] = low[i] - ((getH(i) + getH(i + 1) + 2) >> 2);
        }
        for (int32_t i = 0; i < nHigh; ++i) {
            int32_t e0 = (2 * i - 1 >= 0) ? out[2 * i - 1] : out[1];
            int32_t e1 = (2 * i + 1 < total) ? out[2 * i + 1] : out[total - 2];
            out[2 * i] = high[i] + ((e0 + e1) >> 1);
        }
    }
}

inline void Idwt97_1D(const float* low, int32_t nLow, const float* high, int32_t nHigh, int cas, float* out) {
    int32_t total = nLow + nHigh;
    if (total <= 0) return;
    if (total == 1) {
        out[0] = (cas == 0) ? low[0] : (high[0] * 0.5f);
        return;
    }
    constexpr float kAlpha = -1.586134342f;
    constexpr float kBeta  = -0.05298011857f;
    constexpr float kGamma =  0.8829110755f;
    constexpr float kDelta =  0.4435068520f;
    constexpr float kK     =  1.230174105f;
    constexpr float kInvK  =  1.0f / kK;

    // Interleave scaled samples into `out`, then perform 4 lifting steps with symmetric reflection
    if (cas == 0) {
        for (int32_t i = 0; i < nLow; ++i)  out[2 * i]     = low[i] * kK;
        for (int32_t i = 0; i < nHigh; ++i) out[2 * i + 1] = high[i] * kInvK;
    } else {
        for (int32_t i = 0; i < nHigh; ++i) out[2 * i]     = high[i] * kInvK;
        for (int32_t i = 0; i < nLow; ++i)  out[2 * i + 1] = low[i] * kK;
    }

    auto symAt = [&](int32_t idx) -> float {
        if (idx < 0) idx = -idx;
        if (idx >= total) idx = 2 * (total - 1) - idx;
        return out[std::clamp(idx, 0, total - 1)];
    };

    auto lift = [&](int parity, float factor) {
        for (int32_t i = parity; i < total; i += 2) {
            out[i] -= factor * (symAt(i - 1) + symAt(i + 1));
        }
    };
    int evenP = (cas == 0) ? 0 : 1;
    int oddP  = 1 - evenP;
    lift(evenP, kDelta);
    lift(oddP,  kGamma);
    lift(evenP, kBeta);
    lift(oddP,  kAlpha);
}

// ============================================================================
// Codestream Structures & Decoder
// ============================================================================
struct CompStyle {
    uint8_t scod = 0;
    uint8_t progOrder = 0; // 0=LRCP, 1=RLCP, 2=RPCL, 3=PCRL, 4=CPRL
    uint16_t numLayers = 1;
    uint8_t mct = 0;
    uint8_t numLevels = 0;
    uint8_t cbWExp = 6, cbHExp = 6;
    uint8_t cbStyle = 0;
    uint8_t xform = 0; // 0 = 9-7 irreversible, 1 = 5-3 reversible
    std::vector<uint8_t> precExpX, precExpY;

    uint8_t qStyle = 0; // 0 = reversible, 1 = scalar derived, 2 = scalar expounded
    uint8_t guardBits = 2;
    std::vector<uint8_t> qEps;
    std::vector<uint16_t> qMu;
};

struct CompMeta {
    uint8_t bitDepth = 8;
    bool isSigned = false;
    uint8_t xr = 1, yr = 1;
};

struct DecodedJ2kCodestream {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<CompMeta> comps;
    std::vector<std::vector<int32_t>> samples; // unsigned [0 .. (1<<bitDepth)-1] per component at full image resolution!
};

inline bool DecodeJ2kCodestream(const uint8_t* cs, size_t csLen, DecodedJ2kCodestream& out) {
    if (!cs || csLen < 32 || RdBE16(cs) != 0xFF4Fu /* SOC */) return false;
    size_t pos = 2;
    if (pos + 4 > csLen || RdBE16(cs + pos) != 0xFF51u /* SIZ */) return false;

    uint16_t lSiz = RdBE16(cs + pos + 2);
    if (lSiz < 41 || pos + 2 + lSiz > csLen) return false;
    const uint8_t* siz = cs + pos + 4;
    uint32_t xSiz  = RdBE32(siz + 2);
    uint32_t ySiz  = RdBE32(siz + 6);
    uint32_t xOSiz = RdBE32(siz + 10);
    uint32_t yOSiz = RdBE32(siz + 14);
    uint32_t xTSiz = RdBE32(siz + 18);
    uint32_t yTSiz = RdBE32(siz + 22);
    uint32_t xTOSiz = RdBE32(siz + 26);
    uint32_t yTOSiz = RdBE32(siz + 30);
    uint16_t cSiz  = RdBE16(siz + 34);
    if (cSiz == 0 || cSiz > 16 || xSiz <= xOSiz || ySiz <= yOSiz || xTSiz == 0 || yTSiz == 0) return false;
    if (36u + static_cast<size_t>(cSiz) * 3u > lSiz - 2u) return false;

    uint32_t imgW = xSiz - xOSiz;
    uint32_t imgH = ySiz - yOSiz;
    if (imgW > 16384 || imgH > 16384) return false;

    out.width = imgW;
    out.height = imgH;
    out.comps.resize(cSiz);
    for (uint16_t c = 0; c < cSiz; ++c) {
        uint8_t ss = siz[36 + c * 3 + 0];
        uint8_t xr = siz[36 + c * 3 + 1];
        uint8_t yr = siz[36 + c * 3 + 2];
        if (xr == 0 || yr == 0) return false;
        out.comps[c].bitDepth = static_cast<uint8_t>((ss & 0x7Fu) + 1u);
        out.comps[c].isSigned = (ss & 0x80u) != 0;
        out.comps[c].xr = xr;
        out.comps[c].yr = yr;
        if (out.comps[c].bitDepth > 24) return false;
    }
    pos += 2 + lSiz;

    CompStyle defStyle;
    std::vector<CompStyle> compStyles;
    bool hasMainCod = false, hasMainQcd = false;

    auto parseCodInto = [](const uint8_t* p, size_t bodyLen, CompStyle& st, bool isCoc, uint16_t numComps, uint16_t& outCompIdx) -> bool {
        size_t idx = 0;
        if (isCoc) {
            size_t cidBytes = (numComps < 257) ? 1u : 2u;
            if (idx + cidBytes + 6 > bodyLen) return false;
            outCompIdx = (cidBytes == 1) ? p[idx] : RdBE16(p + idx);
            idx += cidBytes;
            uint8_t scoc = p[idx++];
            st.scod = (st.scod & 0xFEu) | (scoc & 1u);
        } else {
            if (bodyLen < 10) return false;
            st.scod = p[0];
            st.progOrder = p[1];
            st.numLayers = std::max<uint16_t>(1u, RdBE16(p + 2));
            st.mct = p[4];
            idx = 5;
        }
        if (idx + 5 > bodyLen) return false;
        st.numLevels = p[idx];
        if (st.numLevels > 12) return false;
        st.cbWExp = static_cast<uint8_t>((p[idx + 1] & 0x0Fu) + 2u);
        st.cbHExp = static_cast<uint8_t>((p[idx + 2] & 0x0Fu) + 2u);
        if (st.cbWExp > 10 || st.cbHExp > 10 || st.cbWExp + st.cbHExp > 12) return false;
        st.cbStyle = p[idx + 3];
        st.xform   = p[idx + 4];
        idx += 5;

        st.precExpX.assign(st.numLevels + 1u, 15);
        st.precExpY.assign(st.numLevels + 1u, 15);
        if ((st.scod & 1u) != 0) {
            if (idx + st.numLevels + 1u > bodyLen) return false;
            for (uint8_t r = 0; r <= st.numLevels; ++r) {
                st.precExpX[r] = p[idx + r] & 0x0Fu;
                st.precExpY[r] = (p[idx + r] >> 4) & 0x0Fu;
            }
        }
        return true;
    };

    auto parseQcdInto = [](const uint8_t* p, size_t bodyLen, CompStyle& st, bool isQcc, uint16_t numComps, uint16_t& outCompIdx) -> bool {
        size_t idx = 0;
        if (isQcc) {
            size_t cidBytes = (numComps < 257) ? 1u : 2u;
            if (idx + cidBytes + 2 > bodyLen) return false;
            outCompIdx = (cidBytes == 1) ? p[idx] : RdBE16(p + idx);
            idx += cidBytes;
        }
        if (idx + 1 > bodyLen) return false;
        uint8_t sq = p[idx++];
        st.qStyle = sq & 0x1Fu;
        st.guardBits = (sq >> 5) & 0x07u;
        st.qEps.clear();
        st.qMu.clear();
        if (st.qStyle == 0) {
            while (idx < bodyLen) {
                st.qEps.push_back((p[idx++] >> 3) & 0x1Fu);
                st.qMu.push_back(0);
            }
        } else if (st.qStyle == 1 || st.qStyle == 2) {
            while (idx + 2 <= bodyLen) {
                uint16_t w = RdBE16(p + idx); idx += 2;
                st.qEps.push_back(static_cast<uint8_t>((w >> 11) & 0x1Fu));
                st.qMu.push_back(static_cast<uint16_t>(w & 0x7FFu));
            }
        } else {
            return false;
        }
        return !st.qEps.empty();
    };

    // Parse Main Header up to first SOT (0xFF90)
    struct RawCocQcc { bool isQcc; std::vector<uint8_t> body; };
    std::vector<RawCocQcc> pendingMainOverrides;

    while (pos + 2 <= csLen) {
        uint16_t mk = RdBE16(cs + pos);
        if (mk == 0xFF90u /* SOT */) break;
        if (mk == 0xFFD9u /* EOC */) return false;
        pos += 2;
        if (mk >= 0xFF30u && mk <= 0xFF3Fu) continue; // marker without parameters
        if (pos + 2 > csLen) return false;
        uint16_t mLen = RdBE16(cs + pos);
        if (mLen < 2 || pos + mLen > csLen) return false;
        const uint8_t* mBody = cs + pos + 2;
        size_t bLen = mLen - 2;
        uint16_t dummyIdx = 0;

        if (mk == 0xFF52u /* COD */) {
            if (!parseCodInto(mBody, bLen, defStyle, false, cSiz, dummyIdx)) return false;
            hasMainCod = true;
        } else if (mk == 0xFF5Cu /* QCD */) {
            if (!parseQcdInto(mBody, bLen, defStyle, false, cSiz, dummyIdx)) return false;
            hasMainQcd = true;
        } else if (mk == 0xFF53u /* COC */ || mk == 0xFF5Du /* QCC */) {
            pendingMainOverrides.push_back({ mk == 0xFF5Du, std::vector<uint8_t>(mBody, mBody + bLen) });
        }
        pos += mLen;
    }
    if (!hasMainCod || !hasMainQcd) return false;

    compStyles.assign(cSiz, defStyle);
    for (const auto& ov : pendingMainOverrides) {
        uint16_t cIdx = 0;
        CompStyle tmp = compStyles[0];
        if (ov.isQcc) {
            if (parseQcdInto(ov.body.data(), ov.body.size(), tmp, true, cSiz, cIdx) && cIdx < cSiz) {
                compStyles[cIdx].qStyle = tmp.qStyle;
                compStyles[cIdx].guardBits = tmp.guardBits;
                compStyles[cIdx].qEps = tmp.qEps;
                compStyles[cIdx].qMu  = tmp.qMu;
            }
        } else {
            if (parseCodInto(ov.body.data(), ov.body.size(), tmp, true, cSiz, cIdx) && cIdx < cSiz) {
                tmp.qStyle = compStyles[cIdx].qStyle;
                tmp.guardBits = compStyles[cIdx].guardBits;
                tmp.qEps = compStyles[cIdx].qEps;
                tmp.qMu  = compStyles[cIdx].qMu;
                compStyles[cIdx] = tmp;
            }
        }
    }

    uint32_t numTilesX = static_cast<uint32_t>(CeilDivI32(static_cast<int32_t>(xSiz - xTOSiz), static_cast<int32_t>(xTSiz)));
    uint32_t numTilesY = static_cast<uint32_t>(CeilDivI32(static_cast<int32_t>(ySiz - yTOSiz), static_cast<int32_t>(yTSiz)));
    uint32_t totalTiles = numTilesX * numTilesY;
    if (totalTiles == 0 || totalTiles > 16384) return false;

    struct TileAccum {
        std::vector<uint8_t> bitStream;
        std::vector<CompStyle> styles;
        bool hasAnyPart = false;
    };
    std::vector<TileAccum> tiles(totalTiles);
    for (uint32_t t = 0; t < totalTiles; ++t) tiles[t].styles = compStyles;

    // Gather all SOT .. SOD tile-parts
    while (pos + 2 <= csLen) {
        uint16_t mk = RdBE16(cs + pos);
        if (mk == 0xFFD9u /* EOC */) break;
        if (mk != 0xFF90u /* SOT */) { ++pos; continue; }
        if (pos + 12 > csLen) break;
        uint16_t lSot = RdBE16(cs + pos + 2);
        if (lSot != 10) return false;
        uint16_t iSot  = RdBE16(cs + pos + 4);
        uint32_t pSot  = RdBE32(cs + pos + 6);
        size_t tilePartEnd = (pSot == 0) ? csLen : std::min(csLen, pos + static_cast<size_t>(pSot));
        if (pSot != 0 && pSot < 14) return false;
        pos += 12;

        if (iSot >= totalTiles) {
            pos = tilePartEnd;
            continue;
        }
        auto& tile = tiles[iSot];
        tile.hasAnyPart = true;

        // Parse optional tile-part header markers up to SOD (0xFF93)
        bool foundSod = false;
        while (pos + 2 <= tilePartEnd) {
            uint16_t tmk = RdBE16(cs + pos);
            pos += 2;
            if (tmk == 0xFF93u /* SOD */) { foundSod = true; break; }
            if (pos + 2 > tilePartEnd) break;
            uint16_t tLen = RdBE16(cs + pos);
            if (tLen < 2 || pos + tLen > tilePartEnd) break;
            const uint8_t* tb = cs + pos + 2;
            size_t tbLen = tLen - 2;
            uint16_t cIdx = 0;
            if (tmk == 0xFF52u /* COD */) {
                CompStyle upd = tile.styles[0];
                if (parseCodInto(tb, tbLen, upd, false, cSiz, cIdx)) {
                    for (uint16_t c = 0; c < cSiz; ++c) {
                        upd.qStyle = tile.styles[c].qStyle; upd.guardBits = tile.styles[c].guardBits;
                        upd.qEps = tile.styles[c].qEps; upd.qMu = tile.styles[c].qMu;
                        tile.styles[c] = upd;
                    }
                }
            } else if (tmk == 0xFF5Cu /* QCD */) {
                CompStyle upd = tile.styles[0];
                if (parseQcdInto(tb, tbLen, upd, false, cSiz, cIdx)) {
                    for (uint16_t c = 0; c < cSiz; ++c) {
                        tile.styles[c].qStyle = upd.qStyle; tile.styles[c].guardBits = upd.guardBits;
                        tile.styles[c].qEps = upd.qEps; tile.styles[c].qMu = upd.qMu;
                    }
                }
            }
            pos += tLen;
        }
        if (!foundSod) return false;

        // If pSot == 0, scan until EOC (0xFFD9) or next SOT (0xFF90)
        size_t dataEnd = tilePartEnd;
        if (pSot == 0) {
            size_t scan = pos;
            while (scan + 1 < csLen) {
                if (cs[scan] == 0xFFu && (cs[scan + 1] == 0xD9u || cs[scan + 1] == 0x90u)) {
                    dataEnd = scan;
                    break;
                }
                ++scan;
            }
        }
        if (dataEnd > pos) {
            tile.bitStream.insert(tile.bitStream.end(), cs + pos, cs + dataEnd);
        }
        pos = dataEnd;
    }

    // Allocate output full-image component buffers
    out.samples.resize(cSiz);
    for (uint16_t c = 0; c < cSiz; ++c) {
        out.samples[c].assign(static_cast<size_t>(imgW) * imgH, 0);
    }

    // Decode each tile
    bool decodedAnyTile = false;
    for (uint32_t ty = 0; ty < numTilesY; ++ty) {
        for (uint32_t tx = 0; tx < numTilesX; ++tx) {
            uint32_t tIdx = ty * numTilesX + tx;
            const auto& tile = tiles[tIdx];
            if (!tile.hasAnyPart) continue;

            int32_t tx0 = std::max<int32_t>(static_cast<int32_t>(xTOSiz + tx * xTSiz), static_cast<int32_t>(xOSiz));
            int32_t ty0 = std::max<int32_t>(static_cast<int32_t>(yTOSiz + ty * yTSiz), static_cast<int32_t>(yOSiz));
            int32_t tx1 = std::min<int32_t>(static_cast<int32_t>(xTOSiz + (tx + 1) * xTSiz), static_cast<int32_t>(xSiz));
            int32_t ty1 = std::min<int32_t>(static_cast<int32_t>(yTOSiz + (ty + 1) * yTSiz), static_cast<int32_t>(ySiz));
            if (tx1 <= tx0 || ty1 <= ty0) continue;

            // Build per-component resolution / subband / precinct / code-block hierarchy
            struct PrecinctBand {
                J2kTagTree inclTree;
                J2kTagTree zbpTree;
                uint32_t cbx0 = 0, cby0 = 0, cbx1 = 0, cby1 = 0;
            };
            struct SubbandInfo {
                int bandType = 0; // 0=LL, 1=HL, 2=LH, 3=HH
                int32_t bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
                uint32_t numCbX = 0, numCbY = 0;
                int32_t maxBp = 8;
                float quantStep = 1.0f;
                std::vector<CodeBlockState> cbs;
            };
            struct ResLevel {
                int32_t rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
                uint32_t numPrecX = 1, numPrecY = 1;
                std::vector<SubbandInfo> bands; // 1 band (LL) at r==0; 3 bands (HL, LH, HH) at r>0
                std::vector<std::vector<PrecinctBand>> precBands; // [precIdx][bandIdxInRes]
            };
            struct CompTile {
                int32_t tcX0 = 0, tcY0 = 0, tcX1 = 0, tcY1 = 0;
                std::vector<ResLevel> res;
                std::vector<float> reconFloat;
                std::vector<int32_t> reconInt;
            };

            std::vector<CompTile> ctiles(cSiz);
            uint8_t maxNL = 0;
            for (uint16_t c = 0; c < cSiz; ++c) {
                const auto& cm = out.comps[c];
                const auto& st = tile.styles[c];
                maxNL = std::max(maxNL, st.numLevels);

                auto& ct = ctiles[c];
                ct.tcX0 = CeilDivI32(tx0, cm.xr);
                ct.tcY0 = CeilDivI32(ty0, cm.yr);
                ct.tcX1 = CeilDivI32(tx1, cm.xr);
                ct.tcY1 = CeilDivI32(ty1, cm.yr);
                uint8_t NL = st.numLevels;
                ct.res.resize(NL + 1u);

                int subbandGlobalIdx = 0;
                for (uint8_t r = 0; r <= NL; ++r) {
                    int shift = NL - r;
                    auto& rl = ct.res[r];
                    rl.rx0 = CeilDivI32(ct.tcX0, 1 << shift);
                    rl.ry0 = CeilDivI32(ct.tcY0, 1 << shift);
                    rl.rx1 = CeilDivI32(ct.tcX1, 1 << shift);
                    rl.ry1 = CeilDivI32(ct.tcY1, 1 << shift);

                    int ppx = st.precExpX[r], ppy = st.precExpY[r];
                    if (rl.rx1 > rl.rx0 && rl.ry1 > rl.ry0) {
                        int32_t px0 = rl.rx0 >> ppx, px1 = (rl.rx1 - 1) >> ppx;
                        int32_t py0 = rl.ry0 >> ppy, py1 = (rl.ry1 - 1) >> ppy;
                        rl.numPrecX = static_cast<uint32_t>(px1 - px0 + 1);
                        rl.numPrecY = static_cast<uint32_t>(py1 - py0 + 1);
                    } else {
                        rl.numPrecX = 0; rl.numPrecY = 0;
                    }

                    int nBands = (r == 0) ? 1 : 3;
                    rl.bands.resize(nBands);
                    for (int bi = 0; bi < nBands; ++bi, ++subbandGlobalIdx) {
                        auto& sb = rl.bands[bi];
                        if (r == 0) {
                            sb.bandType = 0; // LL
                            sb.bx0 = rl.rx0; sb.by0 = rl.ry0; sb.bx1 = rl.rx1; sb.by1 = rl.ry1;
                        } else {
                            sb.bandType = bi + 1; // 1=HL, 2=LH, 3=HH
                            int dShift = NL - r + 1;
                            int xo = (sb.bandType == 1 || sb.bandType == 3) ? (1 << (dShift - 1)) : 0;
                            int yo = (sb.bandType == 2 || sb.bandType == 3) ? (1 << (dShift - 1)) : 0;
                            sb.bx0 = CeilDivI32(ct.tcX0 - xo, 1 << dShift);
                            sb.by0 = CeilDivI32(ct.tcY0 - yo, 1 << dShift);
                            sb.bx1 = CeilDivI32(ct.tcX1 - xo, 1 << dShift);
                            sb.by1 = CeilDivI32(ct.tcY1 - yo, 1 << dShift);
                        }

                        // Quantization parameters for this subband
                        uint8_t eps = st.qEps[0];
                        uint16_t mu = st.qMu[0];
                        if (st.qStyle == 1) {
                            int eDer = static_cast<int>(st.qEps[0]) - static_cast<int>(NL) + static_cast<int>(r == 0 ? 0 : r);
                            eps = static_cast<uint8_t>(std::max(0, eDer));
                        } else if (static_cast<size_t>(subbandGlobalIdx) < st.qEps.size()) {
                            eps = st.qEps[subbandGlobalIdx];
                            mu  = st.qMu[subbandGlobalIdx];
                        }
                        sb.maxBp = std::max<int32_t>(0, static_cast<int32_t>(st.guardBits) + static_cast<int32_t>(eps) - 1);
                        sb.quantStep = std::ldexp(1.0f + static_cast<float>(mu) / 2048.0f, static_cast<int>(cm.bitDepth) - static_cast<int>(eps));

                        int cbExpX = std::min<int>(st.cbWExp, (r == 0) ? ppx : std::max(0, ppx - 1));
                        int cbExpY = std::min<int>(st.cbHExp, (r == 0) ? ppy : std::max(0, ppy - 1));
                        if (sb.bx1 > sb.bx0 && sb.by1 > sb.by0) {
                            int32_t cx0 = sb.bx0 >> cbExpX, cx1 = (sb.bx1 - 1) >> cbExpX;
                            int32_t cy0 = sb.by0 >> cbExpY, cy1 = (sb.by1 - 1) >> cbExpY;
                            sb.numCbX = static_cast<uint32_t>(cx1 - cx0 + 1);
                            sb.numCbY = static_cast<uint32_t>(cy1 - cy0 + 1);
                            sb.cbs.resize(static_cast<size_t>(sb.numCbX) * sb.numCbY);
                            for (uint32_t cy = 0; cy < sb.numCbY; ++cy) {
                                for (uint32_t cx = 0; cx < sb.numCbX; ++cx) {
                                    auto& cb = sb.cbs[static_cast<size_t>(cy) * sb.numCbX + cx];
                                    cb.x0 = std::max<int32_t>(sb.bx0, (cx0 + static_cast<int32_t>(cx)) << cbExpX);
                                    cb.y0 = std::max<int32_t>(sb.by0, (cy0 + static_cast<int32_t>(cy)) << cbExpY);
                                    cb.x1 = std::min<int32_t>(sb.bx1, (cx0 + static_cast<int32_t>(cx) + 1) << cbExpX);
                                    cb.y1 = std::min<int32_t>(sb.by1, (cy0 + static_cast<int32_t>(cy) + 1) << cbExpY);
                                }
                            }
                        }

                        // Build per-precinct code-block ranges + tag trees
                        uint32_t totalPrec = rl.numPrecX * rl.numPrecY;
                        if (rl.precBands.size() != totalPrec) rl.precBands.assign(totalPrec, std::vector<PrecinctBand>(nBands));
                        int sbPrecExpX = (r == 0) ? ppx : std::max(0, ppx - 1);
                        int sbPrecExpY = (r == 0) ? ppy : std::max(0, ppy - 1);
                        int32_t rpx0 = (rl.rx1 > rl.rx0) ? (rl.rx0 >> ppx) : 0;
                        int32_t rpy0 = (rl.ry1 > rl.ry0) ? (rl.ry0 >> ppy) : 0;

                        for (uint32_t py = 0; py < rl.numPrecY; ++py) {
                            for (uint32_t px = 0; px < rl.numPrecX; ++px) {
                                auto& pb = rl.precBands[static_cast<size_t>(py) * rl.numPrecX + px][bi];
                                int32_t pbx0 = std::max<int32_t>(sb.bx0, (rpx0 + static_cast<int32_t>(px)) << sbPrecExpX);
                                int32_t pby0 = std::max<int32_t>(sb.by0, (rpy0 + static_cast<int32_t>(py)) << sbPrecExpY);
                                int32_t pbx1 = std::min<int32_t>(sb.bx1, (rpx0 + static_cast<int32_t>(px) + 1) << sbPrecExpX);
                                int32_t pby1 = std::min<int32_t>(sb.by1, (rpy0 + static_cast<int32_t>(py) + 1) << sbPrecExpY);
                                if (pbx1 > pbx0 && pby1 > pby0 && sb.numCbX > 0 && sb.numCbY > 0) {
                                    int32_t cxBase = sb.bx0 >> cbExpX;
                                    int32_t cyBase = sb.by0 >> cbExpY;
                                    pb.cbx0 = static_cast<uint32_t>((pbx0 >> cbExpX) - cxBase);
                                    pb.cby0 = static_cast<uint32_t>((pby0 >> cbExpY) - cyBase);
                                    pb.cbx1 = static_cast<uint32_t>(((pbx1 - 1) >> cbExpX) - cxBase + 1);
                                    pb.cby1 = static_cast<uint32_t>(((pby1 - 1) >> cbExpY) - cyBase + 1);
                                    pb.inclTree.Init(pb.cbx1 - pb.cbx0, pb.cby1 - pb.cby0);
                                    pb.zbpTree.Init(pb.cbx1 - pb.cbx0, pb.cby1 - pb.cby0);
                                }
                            }
                        }
                    }
                }
            }

            // Tier-2 Packet Reader for one (layer, res, comp, prec) tuple
            const auto& tbs = tile.bitStream;
            size_t bsPos = 0;

            auto decodeOnePacket = [&](uint16_t lay, uint8_t r, uint16_t c, uint32_t precIdx) -> bool {
                const auto& st = tile.styles[c];
                if (r > st.numLevels) return true;
                auto& rl = ctiles[c].res[r];
                if (precIdx >= rl.precBands.size()) return true;
                if (bsPos >= tbs.size()) return true;

                // Optional SOP marker (0xFF91 + 0x0004 + Nsop)
                if ((st.scod & 0x02u) != 0 && bsPos + 6 <= tbs.size() && RdBE16(tbs.data() + bsPos) == 0xFF91u) {
                    bsPos += 6;
                }
                if (bsPos >= tbs.size()) return true;

                PacketBitReader pbr(tbs.data(), tbs.size(), bsPos);
                int nonEmpty = pbr.ReadBit();
                if (nonEmpty < 0) return false;

                struct PendingChunk {
                    CodeBlockState* cb = nullptr;
                    uint32_t passes = 0;
                    uint32_t byteLen = 0;
                    bool isRaw = false;
                };
                std::vector<PendingChunk> chunks;

                if (nonEmpty == 1) {
                    int nBands = static_cast<int>(rl.bands.size());
                    for (int bi = 0; bi < nBands; ++bi) {
                        auto& sb = rl.bands[bi];
                        auto& pb = rl.precBands[precIdx][bi];
                        if (pb.cbx1 <= pb.cbx0 || pb.cby1 <= pb.cby0) continue;

                        for (uint32_t cy = pb.cby0; cy < pb.cby1; ++cy) {
                            for (uint32_t cx = pb.cbx0; cx < pb.cbx1; ++cx) {
                                auto& cb = sb.cbs[static_cast<size_t>(cy) * sb.numCbX + cx];
                                uint32_t txRel = cx - pb.cbx0, tyRel = cy - pb.cby0;

                                bool incNow = false;
                                if (cb.included) {
                                    int b = pbr.ReadBit();
                                    if (b < 0) return false;
                                    incNow = (b == 1);
                                } else {
                                    int32_t incLayer = pb.inclTree.Decode(txRel, tyRel, static_cast<int32_t>(lay) + 1, [&]() { return pbr.ReadBit(); });
                                    if (incLayer < 0) return false;
                                    if (incLayer == static_cast<int32_t>(lay)) {
                                        incNow = true;
                                        cb.included = true;
                                        int32_t zbp = pb.zbpTree.Decode(txRel, tyRel, 999, [&]() { return pbr.ReadBit(); });
                                        if (zbp < 0) return false;
                                        cb.zeroBitPlanes = zbp;
                                    }
                                }
                                if (!incNow) continue;

                                // Decode number of coding passes (Table B.4)
                                uint32_t nPasses = 0;
                                int b0 = pbr.ReadBit(); if (b0 < 0) return false;
                                if (b0 == 0) {
                                    nPasses = 1;
                                } else {
                                    int b1 = pbr.ReadBit(); if (b1 < 0) return false;
                                    if (b1 == 0) {
                                        nPasses = 2;
                                    } else {
                                        int32_t two = pbr.ReadBits(2); if (two < 0) return false;
                                        if (two < 3) {
                                            nPasses = static_cast<uint32_t>(3 + two);
                                        } else {
                                            int32_t five = pbr.ReadBits(5); if (five < 0) return false;
                                            if (five < 31) {
                                                nPasses = static_cast<uint32_t>(6 + five);
                                            } else {
                                                int32_t seven = pbr.ReadBits(7); if (seven < 0) return false;
                                                nPasses = static_cast<uint32_t>(37 + seven);
                                            }
                                        }
                                    }
                                }

                                // Update Lblock comma code
                                while (true) {
                                    int bit = pbr.ReadBit();
                                    if (bit < 0) return false;
                                    if (bit == 0) break;
                                    cb.lblock++;
                                }

                                // Determine segment splits (TERMALL bit 2 or BYPASS bit 0)
                                uint32_t remPasses = nPasses;
                                uint32_t curPassIdx = cb.totalPasses;
                                while (remPasses > 0) {
                                    uint32_t segPasses = remPasses;
                                    bool segRaw = false;
                                    if ((st.cbStyle & 0x04u) != 0) { // TERMALL: 1 pass per segment
                                        segPasses = 1;
                                        if ((st.cbStyle & 0x01u) != 0 && curPassIdx >= 10 && ((curPassIdx - 10) % 3) != 2) {
                                            segRaw = true;
                                        }
                                    } else if ((st.cbStyle & 0x01u) != 0) { // BYPASS
                                        if (curPassIdx < 10) {
                                            segPasses = std::min<uint32_t>(remPasses, 10u - curPassIdx);
                                        } else {
                                            uint32_t mod3 = (curPassIdx - 10u) % 3u;
                                            if (mod3 < 2u) {
                                                segPasses = std::min<uint32_t>(remPasses, 2u - mod3);
                                                segRaw = true;
                                            } else {
                                                segPasses = 1u;
                                            }
                                        }
                                    }
                                    int lenBits = cb.lblock + FloorLog2U32(segPasses);
                                    int32_t segBytes = pbr.ReadBits(lenBits);
                                    if (segBytes < 0) return false;
                                    chunks.push_back({ &cb, segPasses, static_cast<uint32_t>(segBytes), segRaw });
                                    curPassIdx += segPasses;
                                    remPasses -= segPasses;
                                }
                                cb.totalPasses += nPasses;
                            }
                        }
                    }
                }

                if (!pbr.FinishHeader()) return false;
                bsPos = pbr.pos;

                // Optional EPH marker (0xFF92)
                if ((st.scod & 0x04u) != 0 && bsPos + 2 <= tbs.size() && RdBE16(tbs.data() + bsPos) == 0xFF92u) {
                    bsPos += 2;
                }

                // Read packet body code-block segments
                for (const auto& ch : chunks) {
                    if (ch.byteLen > tbs.size() - bsPos) return false;
                    CodeBlockSeg seg;
                    seg.bytes.assign(tbs.data() + bsPos, tbs.data() + bsPos + ch.byteLen);
                    seg.numPasses = ch.passes;
                    seg.isRawBypass = ch.isRaw;
                    ch.cb->segments.push_back(std::move(seg));
                    bsPos += ch.byteLen;
                }
                return true;
            };

            uint8_t prog = tile.styles[0].progOrder;
            uint16_t nLay = tile.styles[0].numLayers;
            bool pktOk = true;
            if (prog == 0) { // LRCP
                for (uint16_t l = 0; l < nLay && pktOk; ++l)
                    for (uint8_t r = 0; r <= maxNL && pktOk; ++r)
                        for (uint16_t c = 0; c < cSiz && pktOk; ++c)
                            if (r <= tile.styles[c].numLevels) {
                                uint32_t np = ctiles[c].res[r].numPrecX * ctiles[c].res[r].numPrecY;
                                for (uint32_t p = 0; p < np && pktOk; ++p) pktOk = decodeOnePacket(l, r, c, p);
                            }
            } else if (prog == 1) { // RLCP
                for (uint8_t r = 0; r <= maxNL && pktOk; ++r)
                    for (uint16_t l = 0; l < nLay && pktOk; ++l)
                        for (uint16_t c = 0; c < cSiz && pktOk; ++c)
                            if (r <= tile.styles[c].numLevels) {
                                uint32_t np = ctiles[c].res[r].numPrecX * ctiles[c].res[r].numPrecY;
                                for (uint32_t p = 0; p < np && pktOk; ++p) pktOk = decodeOnePacket(l, r, c, p);
                            }
            } else { // RPCL / PCRL / CPRL (for single-precinct-per-res or standard multi-precinct)
                if (prog == 4) { // CPRL
                    for (uint16_t c = 0; c < cSiz && pktOk; ++c)
                        for (uint8_t r = 0; r <= tile.styles[c].numLevels && pktOk; ++r) {
                            uint32_t np = ctiles[c].res[r].numPrecX * ctiles[c].res[r].numPrecY;
                            for (uint32_t p = 0; p < np && pktOk; ++p)
                                for (uint16_t l = 0; l < nLay && pktOk; ++l) pktOk = decodeOnePacket(l, r, c, p);
                        }
                } else {
                    for (uint8_t r = 0; r <= maxNL && pktOk; ++r)
                        for (uint16_t c = 0; c < cSiz && pktOk; ++c)
                            if (r <= tile.styles[c].numLevels) {
                                uint32_t np = ctiles[c].res[r].numPrecX * ctiles[c].res[r].numPrecY;
                                for (uint32_t p = 0; p < np && pktOk; ++p)
                                    for (uint16_t l = 0; l < nLay && pktOk; ++l) pktOk = decodeOnePacket(l, r, c, p);
                            }
                }
            }
            if (!pktOk) return false;

            // Tier-1 EBCOT + 2D Inverse DWT per component
            for (uint16_t c = 0; c < cSiz; ++c) {
                const auto& st = tile.styles[c];
                auto& ct = ctiles[c];
                uint8_t NL = st.numLevels;
                bool isRev53 = (st.xform == 1);

                // Decode LL0 into curInt / curFloat
                int32_t curW = ct.res[0].rx1 - ct.res[0].rx0;
                int32_t curH = ct.res[0].ry1 - ct.res[0].ry0;
                if (curW <= 0 || curH <= 0) continue;

                std::vector<int32_t> curInt;
                std::vector<float> curFloat;
                if (isRev53) curInt.assign(static_cast<size_t>(curW) * curH, 0);
                else         curFloat.assign(static_cast<size_t>(curW) * curH, 0.0f);

                auto decodeSubbandGrid = [&](const SubbandInfo& sb, std::vector<int32_t>& sbInt, std::vector<float>& sbFloat) -> bool {
                    int32_t bw = sb.bx1 - sb.bx0, bh = sb.by1 - sb.by0;
                    if (bw <= 0 || bh <= 0) return true;
                    if (isRev53) sbInt.assign(static_cast<size_t>(bw) * bh, 0);
                    else         sbFloat.assign(static_cast<size_t>(bw) * bh, 0.0f);

                    std::vector<int32_t> cbQ;
                    for (const auto& cb : sb.cbs) {
                        int32_t lowBp = 0;
                        if (!DecodeEbcotCodeBlock(cb, sb.bandType, sb.maxBp, st.cbStyle, cbQ, lowBp)) return false;
                        uint32_t cw = static_cast<uint32_t>(cb.x1 - cb.x0);
                        uint32_t ch = static_cast<uint32_t>(cb.y1 - cb.y0);
                        for (uint32_t y = 0; y < ch; ++y) {
                            int32_t dy = (cb.y0 - sb.by0) + static_cast<int32_t>(y);
                            for (uint32_t x = 0; x < cw; ++x) {
                                int32_t dx = (cb.x0 - sb.bx0) + static_cast<int32_t>(x);
                                int32_t qv = cbQ[static_cast<size_t>(y) * cw + x];
                                if (isRev53) {
                                    sbInt[static_cast<size_t>(dy) * bw + dx] = qv;
                                } else {
                                    sbFloat[static_cast<size_t>(dy) * bw + dx] = static_cast<float>(qv) * sb.quantStep;
                                }
                            }
                        }
                    }
                    return true;
                };

                if (!decodeSubbandGrid(ct.res[0].bands[0], curInt, curFloat)) return false;

                // Multi-level 2D synthesis from r = 1 .. NL
                for (uint8_t r = 1; r <= NL; ++r) {
                    const auto& rl = ct.res[r];
                    int32_t nxtW = rl.rx1 - rl.rx0;
                    int32_t nxtH = rl.ry1 - rl.ry0;
                    if (nxtW <= 0 || nxtH <= 0) continue;

                    std::vector<int32_t> hlI, lhI, hhI;
                    std::vector<float> hlF, lhF, hhF;
                    if (!decodeSubbandGrid(rl.bands[0], hlI, hlF) ||
                        !decodeSubbandGrid(rl.bands[1], lhI, lhF) ||
                        !decodeSubbandGrid(rl.bands[2], hhI, hhF)) return false;

                    int32_t hlW = rl.bands[0].bx1 - rl.bands[0].bx0;
                    int32_t lhH = rl.bands[1].by1 - rl.bands[1].by0;
                    int casH = rl.rx0 & 1;
                    int casV = rl.ry0 & 1;

                    if (isRev53) {
                        std::vector<int32_t> rowMerged(static_cast<size_t>(nxtW) * nxtH, 0);
                        std::vector<int32_t> tmpL(curH), tmpH(lhH), tmpCol(nxtH);
                        // Horizontal 1D IDWT on top (LL + HL) and bottom (LH + HH) rows
                        for (int32_t y = 0; y < curH; ++y) {
                            const int32_t* lPtr = curInt.data() + static_cast<size_t>(y) * curW;
                            const int32_t* hPtr = (hlW > 0) ? (hlI.data() + static_cast<size_t>(y) * hlW) : nullptr;
                            Idwt53_1D(lPtr, curW, hPtr, hlW, casH, rowMerged.data() + static_cast<size_t>(y) * nxtW);
                        }
                        for (int32_t y = 0; y < lhH; ++y) {
                            const int32_t* lPtr = (curW > 0) ? (lhI.data() + static_cast<size_t>(y) * curW) : nullptr;
                            const int32_t* hPtr = (hlW > 0) ? (hhI.data() + static_cast<size_t>(y) * hlW) : nullptr;
                            Idwt53_1D(lPtr, curW, hPtr, hlW, casH, rowMerged.data() + static_cast<size_t>(curH + y) * nxtW);
                        }
                        // Vertical 1D IDWT on each column
                        std::vector<int32_t> nxtInt(static_cast<size_t>(nxtW) * nxtH, 0);
                        for (int32_t x = 0; x < nxtW; ++x) {
                            for (int32_t y = 0; y < curH; ++y) tmpL[y] = rowMerged[static_cast<size_t>(y) * nxtW + x];
                            for (int32_t y = 0; y < lhH; ++y)  tmpH[y] = rowMerged[static_cast<size_t>(curH + y) * nxtW + x];
                            Idwt53_1D(tmpL.data(), curH, tmpH.data(), lhH, casV, tmpCol.data());
                            for (int32_t y = 0; y < nxtH; ++y) nxtInt[static_cast<size_t>(y) * nxtW + x] = tmpCol[y];
                        }
                        curInt = std::move(nxtInt);
                    } else {
                        std::vector<float> rowMerged(static_cast<size_t>(nxtW) * nxtH, 0.0f);
                        std::vector<float> tmpL(curH), tmpH(lhH), tmpCol(nxtH);
                        for (int32_t y = 0; y < curH; ++y) {
                            const float* lPtr = curFloat.data() + static_cast<size_t>(y) * curW;
                            const float* hPtr = (hlW > 0) ? (hlF.data() + static_cast<size_t>(y) * hlW) : nullptr;
                            Idwt97_1D(lPtr, curW, hPtr, hlW, casH, rowMerged.data() + static_cast<size_t>(y) * nxtW);
                        }
                        for (int32_t y = 0; y < lhH; ++y) {
                            const float* lPtr = (curW > 0) ? (lhF.data() + static_cast<size_t>(y) * curW) : nullptr;
                            const float* hPtr = (hlW > 0) ? (hhF.data() + static_cast<size_t>(y) * hlW) : nullptr;
                            Idwt97_1D(lPtr, curW, hPtr, hlW, casH, rowMerged.data() + static_cast<size_t>(curH + y) * nxtW);
                        }
                        std::vector<float> nxtFloat(static_cast<size_t>(nxtW) * nxtH, 0.0f);
                        for (int32_t x = 0; x < nxtW; ++x) {
                            for (int32_t y = 0; y < curH; ++y) tmpL[y] = rowMerged[static_cast<size_t>(y) * nxtW + x];
                            for (int32_t y = 0; y < lhH; ++y)  tmpH[y] = rowMerged[static_cast<size_t>(curH + y) * nxtW + x];
                            Idwt97_1D(tmpL.data(), curH, tmpH.data(), lhH, casV, tmpCol.data());
                            for (int32_t y = 0; y < nxtH; ++y) nxtFloat[static_cast<size_t>(y) * nxtW + x] = tmpCol[y];
                        }
                        curFloat = std::move(nxtFloat);
                    }
                    curW = nxtW;
                    curH = nxtH;
                }

                ct.reconInt = std::move(curInt);
                ct.reconFloat = std::move(curFloat);
            }

            // Multi-Component Transform (RCT or ICT on components 0, 1, 2 when matching tile dimensions!)
            bool doMct = (tile.styles[0].mct == 1 && cSiz >= 3 &&
                          (ctiles[0].tcX1 - ctiles[0].tcX0) == (ctiles[1].tcX1 - ctiles[1].tcX0) &&
                          (ctiles[0].tcX1 - ctiles[0].tcX0) == (ctiles[2].tcX1 - ctiles[2].tcX0) &&
                          (ctiles[0].tcY1 - ctiles[0].tcY0) == (ctiles[1].tcY1 - ctiles[1].tcY0) &&
                          (ctiles[0].tcY1 - ctiles[0].tcY0) == (ctiles[2].tcY1 - ctiles[2].tcY0));

            if (doMct) {
                size_t n = static_cast<size_t>(ctiles[0].tcX1 - ctiles[0].tcX0) * (ctiles[0].tcY1 - ctiles[0].tcY0);
                if (tile.styles[0].xform == 1 && ctiles[0].reconInt.size() == n &&
                    ctiles[1].reconInt.size() == n && ctiles[2].reconInt.size() == n)
                {
                    // Inverse RCT (ITU-T T.800 G.2)
                    for (size_t i = 0; i < n; ++i) {
                        int32_t y0 = ctiles[0].reconInt[i];
                        int32_t y1 = ctiles[1].reconInt[i];
                        int32_t y2 = ctiles[2].reconInt[i];
                        int32_t g  = y0 - ((y1 + y2) >> 2);
                        int32_t r  = y2 + g;
                        int32_t b  = y1 + g;
                        ctiles[0].reconInt[i] = r;
                        ctiles[1].reconInt[i] = g;
                        ctiles[2].reconInt[i] = b;
                    }
                } else if (tile.styles[0].xform == 0 && ctiles[0].reconFloat.size() == n &&
                           ctiles[1].reconFloat.size() == n && ctiles[2].reconFloat.size() == n)
                {
                    // Inverse ICT (ITU-T T.800 G.3)
                    for (size_t i = 0; i < n; ++i) {
                        float y0 = ctiles[0].reconFloat[i];
                        float y1 = ctiles[1].reconFloat[i];
                        float y2 = ctiles[2].reconFloat[i];
                        ctiles[0].reconFloat[i] = y0 + 1.402f * y2;
                        ctiles[1].reconFloat[i] = y0 - 0.34413f * y1 - 0.71414f * y2;
                        ctiles[2].reconFloat[i] = y0 + 1.772f * y1;
                    }
                }
            }

            // DC level shift, clamp to [0 .. (1<<bitDepth)-1], and upsample into full-image component buffers
            for (uint16_t c = 0; c < cSiz; ++c) {
                const auto& cm = out.comps[c];
                const auto& st = tile.styles[c];
                const auto& ct = ctiles[c];
                int32_t tw = ct.tcX1 - ct.tcX0, th = ct.tcY1 - ct.tcY0;
                if (tw <= 0 || th <= 0) continue;

                int32_t dcShift = cm.isSigned ? 0 : (1 << (cm.bitDepth - 1));
                int32_t signedBias = cm.isSigned ? (1 << (cm.bitDepth - 1)) : 0;
                int32_t maxVal = (1 << cm.bitDepth) - 1;
                bool isRev = (st.xform == 1);

                for (int32_t sy = ct.tcY0; sy < ct.tcY1; ++sy) {
                    for (int32_t sx = ct.tcX0; sx < ct.tcX1; ++sx) {
                        size_t si = static_cast<size_t>(sy - ct.tcY0) * tw + (sx - ct.tcX0);
                        int32_t v = 0;
                        if (isRev) {
                            v = ct.reconInt[si] + dcShift + signedBias;
                        } else {
                            v = static_cast<int32_t>(std::floor(ct.reconFloat[si] + 0.5f)) + dcShift + signedBias;
                        }
                        v = std::clamp(v, 0, maxVal);

                        int32_t gx0 = std::max<int32_t>(tx0, sx * cm.xr);
                        int32_t gy0 = std::max<int32_t>(ty0, sy * cm.yr);
                        int32_t gx1 = std::min<int32_t>(tx1, (sx + 1) * cm.xr);
                        int32_t gy1 = std::min<int32_t>(ty1, (sy + 1) * cm.yr);
                        for (int32_t gy = gy0; gy < gy1; ++gy) {
                            uint32_t iy = static_cast<uint32_t>(gy - static_cast<int32_t>(yOSiz));
                            for (int32_t gx = gx0; gx < gx1; ++gx) {
                                uint32_t ix = static_cast<uint32_t>(gx - static_cast<int32_t>(xOSiz));
                                out.samples[c][static_cast<size_t>(iy) * imgW + ix] = v;
                            }
                        }
                    }
                }
            }
            decodedAnyTile = true;
        }
    }
    return decodedAnyTile;
}

} // namespace J2kDetail

inline bool IsLikelyJpeg2000Signature(const uint8_t* data, size_t size) {
    if (!data || size < 12) return false;
    if (std::memcmp(data, "\x00\x00\x00\x0CjP  \x0D\x0A\x87\x0A", 12) == 0) return true;
    if (data[0] == 0xFFu && data[1] == 0x4Fu && data[2] == 0xFFu && data[3] == 0x51u) return true;
    return false;
}

inline bool DecodeJpeg2000(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    using namespace J2kDetail;
    if (!data || size < 24) return false;

    const uint8_t* csPtr = nullptr;
    size_t csLen = 0;
    uint32_t enumCS = 0; // 12=CMYK, 16=sRGB, 17=Gray, 18=sYCC

    struct PclrTable {
        uint16_t entries = 0;
        uint8_t cols = 0;
        std::vector<uint8_t> bitDepths;
        std::vector<int32_t> lut; // [entry * cols + col]
    } pclr;

    struct CmapEntry { uint16_t cmp = 0; uint8_t mtyp = 0; uint8_t pcol = 0; };
    std::vector<CmapEntry> cmap;

    struct CdefEntry { uint16_t cn = 0; uint16_t typ = 0; uint16_t asoc = 0; };
    std::vector<CdefEntry> cdefs;

    bool isBoxContainer = (size >= 12 && std::memcmp(data, "\x00\x00\x00\x0CjP  \x0D\x0A\x87\x0A", 12) == 0);
    if (isBoxContainer) {
        size_t pos = 12;
        while (pos + 8 <= size) {
            uint64_t bSz = RdBE32(data + pos);
            char bType[5] = {};
            std::memcpy(bType, data + pos + 4, 4);
            size_t hdr = 8;
            if (bSz == 1) {
                if (pos + 16 > size) return false;
                bSz = RdBE64(data + pos + 8);
                hdr = 16;
            } else if (bSz == 0) {
                bSz = size - pos;
            }
            if (bSz < hdr || bSz > size - pos) return false;
            size_t body = pos + hdr;
            size_t boxEnd = pos + static_cast<size_t>(bSz);

            if (std::strcmp(bType, "jp2h") == 0) {
                size_t sp = body;
                while (sp + 8 <= boxEnd) {
                    uint64_t sSz = RdBE32(data + sp);
                    char sType[5] = {};
                    std::memcpy(sType, data + sp + 4, 4);
                    size_t sHdr = 8;
                    if (sSz == 1) { if (sp + 16 > boxEnd) break; sSz = RdBE64(data + sp + 8); sHdr = 16; }
                    else if (sSz == 0) sSz = boxEnd - sp;
                    if (sSz < sHdr || sSz > boxEnd - sp) break;
                    const uint8_t* sb = data + sp + sHdr;
                    size_t sbLen = static_cast<size_t>(sSz) - sHdr;

                    if (std::strcmp(sType, "colr") == 0 && sbLen >= 7 && sb[0] == 1) {
                        enumCS = RdBE32(sb + 3);
                    } else if (std::strcmp(sType, "pclr") == 0 && sbLen >= 4) {
                        pclr.entries = RdBE16(sb + 0);
                        pclr.cols    = sb[2];
                        if (pclr.entries > 0 && pclr.entries <= 4096 && pclr.cols > 0 && pclr.cols <= 8 && 3u + pclr.cols <= sbLen) {
                            pclr.bitDepths.resize(pclr.cols);
                            for (uint8_t k = 0; k < pclr.cols; ++k) pclr.bitDepths[k] = static_cast<uint8_t>((sb[3 + k] & 0x7Fu) + 1u);
                            pclr.lut.assign(static_cast<size_t>(pclr.entries) * pclr.cols, 0);
                            size_t pp = 3u + pclr.cols;
                            for (uint16_t e = 0; e < pclr.entries; ++e) {
                                for (uint8_t k = 0; k < pclr.cols; ++k) {
                                    uint8_t nb = (pclr.bitDepths[k] + 7u) / 8u;
                                    if (pp + nb > sbLen) break;
                                    uint32_t v = 0;
                                    for (uint8_t b = 0; b < nb; ++b) v = (v << 8) | sb[pp++];
                                    pclr.lut[static_cast<size_t>(e) * pclr.cols + k] = static_cast<int32_t>(v);
                                }
                            }
                        }
                    } else if (std::strcmp(sType, "cmap") == 0 && sbLen >= 4) {
                        for (size_t cp = 0; cp + 4 <= sbLen; cp += 4) {
                            cmap.push_back({ RdBE16(sb + cp), sb[cp + 2], sb[cp + 3] });
                        }
                    } else if (std::strcmp(sType, "cdef") == 0 && sbLen >= 2) {
                        uint16_t nDef = RdBE16(sb);
                        for (uint16_t d = 0; d < nDef && 2u + static_cast<size_t>(d + 1) * 6u <= sbLen; ++d) {
                            const uint8_t* dp = sb + 2 + d * 6;
                            cdefs.push_back({ RdBE16(dp), RdBE16(dp + 2), RdBE16(dp + 4) });
                        }
                    }
                    sp += static_cast<size_t>(sSz);
                }
            } else if (std::strcmp(bType, "jp2c") == 0 && !csPtr) {
                csPtr = data + body;
                csLen = boxEnd - body;
            }
            pos = boxEnd;
        }
        if (!csPtr || csLen < 24) return false;
    } else if (data[0] == 0xFFu && data[1] == 0x4Fu) {
        csPtr = data;
        csLen = size;
    } else {
        return false;
    }

    DecodedJ2kCodestream dec;
    if (!DecodeJ2kCodestream(csPtr, csLen, dec)) {
        // Optional Windows WIC fallback for Part-2 JPX extensions if OS WIC JP2 codec is installed
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"JPEG 2000 (.jp2)", outInfo);
    }

    uint32_t w = dec.width, h = dec.height;
    size_t nPx = static_cast<size_t>(w) * h;

    // Expand 'cmap' + 'pclr' into logical channels
    struct LogicalChan {
        uint8_t bitDepth = 8;
        std::vector<int32_t> vals;
    };
    std::vector<LogicalChan> chans;
    if (!cmap.empty() && pclr.entries > 0) {
        for (const auto& cm : cmap) {
            if (cm.cmp >= dec.samples.size()) continue;
            if (cm.mtyp == 1 && cm.pcol < pclr.cols) {
                LogicalChan lc;
                lc.bitDepth = pclr.bitDepths[cm.pcol];
                lc.vals.resize(nPx);
                const auto& idxPlane = dec.samples[cm.cmp];
                for (size_t i = 0; i < nPx; ++i) {
                    int idx = std::clamp<int>(idxPlane[i], 0, static_cast<int>(pclr.entries) - 1);
                    lc.vals[i] = pclr.lut[static_cast<size_t>(idx) * pclr.cols + cm.pcol];
                }
                chans.push_back(std::move(lc));
            } else {
                chans.push_back({ dec.comps[cm.cmp].bitDepth, dec.samples[cm.cmp] });
            }
        }
    } else if (pclr.entries > 0 && dec.samples.size() == 1 && pclr.cols >= 3) {
        for (uint8_t k = 0; k < pclr.cols; ++k) {
            LogicalChan lc;
            lc.bitDepth = pclr.bitDepths[k];
            lc.vals.resize(nPx);
            for (size_t i = 0; i < nPx; ++i) {
                int idx = std::clamp<int>(dec.samples[0][i], 0, static_cast<int>(pclr.entries) - 1);
                lc.vals[i] = pclr.lut[static_cast<size_t>(idx) * pclr.cols + k];
            }
            chans.push_back(std::move(lc));
        }
    } else {
        for (size_t c = 0; c < dec.samples.size(); ++c) {
            chans.push_back({ dec.comps[c].bitDepth, std::move(dec.samples[c]) });
        }
    }
    if (chans.empty()) return false;

    auto normU8 = [&](int chIdx, size_t pix) -> uint8_t {
        const auto& lc = chans[chIdx];
        int32_t maxV = (1 << lc.bitDepth) - 1;
        if (maxV <= 0) return 0;
        if (lc.bitDepth == 8) return static_cast<uint8_t>(lc.vals[pix]);
        return static_cast<uint8_t>((static_cast<int64_t>(lc.vals[pix]) * 255 + (maxV / 2)) / maxV);
    };

    int chR = -1, chG = -1, chB = -1, chA = -1;
    bool alphaAlreadyPremul = false;
    for (const auto& cd : cdefs) {
        if (cd.cn >= chans.size()) continue;
        if (cd.typ == 0) {
            if (cd.asoc == 1) chR = cd.cn;
            else if (cd.asoc == 2) chG = cd.cn;
            else if (cd.asoc == 3) chB = cd.cn;
        } else if (cd.typ == 1 || cd.typ == 2) {
            chA = cd.cn;
            alphaAlreadyPremul = (cd.typ == 2);
        }
    }
    if (chR < 0 && chG < 0 && chB < 0) {
        if (chans.size() >= 3) {
            chR = 0; chG = 1; chB = 2;
            if (chA < 0 && chans.size() >= 4 && enumCS != 12) chA = 3;
        } else {
            chR = chG = chB = 0;
            if (chA < 0 && chans.size() == 2) chA = 1;
        }
    } else {
        if (chR < 0) chR = 0;
        if (chG < 0) chG = chR;
        if (chB < 0) chB = chG;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = static_cast<uint32_t>(chans.size());
    outInfo.bitsPerChannel = chans[0].bitDepth;
    outInfo.bgraPixels.resize(nPx * 4);

    for (size_t i = 0; i < nPx; ++i) {
        uint8_t r = normU8(chR, i);
        uint8_t g = normU8(chG, i);
        uint8_t b = normU8(chB, i);
        uint8_t a = (chA >= 0) ? normU8(chA, i) : 255;

        if (enumCS == 18 && chans.size() >= 3) { // sYCC -> sRGB
            float yv = static_cast<float>(r);
            float cb = static_cast<float>(g) - 128.0f;
            float cr = static_cast<float>(b) - 128.0f;
            r = static_cast<uint8_t>(std::clamp(yv + 1.402f * cr + 0.5f, 0.0f, 255.0f));
            g = static_cast<uint8_t>(std::clamp(yv - 0.344136f * cb - 0.714136f * cr + 0.5f, 0.0f, 255.0f));
            b = static_cast<uint8_t>(std::clamp(yv + 1.772f * cb + 0.5f, 0.0f, 255.0f));
        } else if (enumCS == 12 && chans.size() >= 4) { // CMYK
            uint8_t c = normU8(0, i), m = normU8(1, i), yk = normU8(2, i), k = normU8(3, i);
            r = static_cast<uint8_t>(((255u - c) * (255u - k)) / 255u);
            g = static_cast<uint8_t>(((255u - m) * (255u - k)) / 255u);
            b = static_cast<uint8_t>(((255u - yk) * (255u - k)) / 255u);
            a = (chans.size() >= 5) ? normU8(4, i) : 255;
        }

        if (alphaAlreadyPremul) {
            outInfo.bgraPixels[i * 4 + 0] = b;
            outInfo.bgraPixels[i * 4 + 1] = g;
            outInfo.bgraPixels[i * 4 + 2] = r;
            outInfo.bgraPixels[i * 4 + 3] = a;
        } else {
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        }
    }

    if (extLower == L".j2k" || extLower == L".j2c" || !isBoxContainer) {
        outInfo.formatName = L"JPEG 2000 Codestream (" + (extLower.empty() ? L".j2k" : extLower) + L")";
    } else if (extLower == L".jpx") {
        outInfo.formatName = L"JPEG 2000 Extended (.jpx)";
    } else if (extLower == L".jpf") {
        outInfo.formatName = L"JPEG 2000 Part 2 (.jpf)";
    } else {
        outInfo.formatName = L"JPEG 2000 (.jp2)";
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
