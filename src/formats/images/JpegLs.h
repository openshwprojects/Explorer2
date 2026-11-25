#pragma once
// JpegLs.h — Zero-dependency C++20 ISO/IEC 14495-1 / ITU-T T.87 JPEG-LS (.jls) LOCO-I lossless & near-lossless decoder.

namespace Formats {

struct JpegLsDecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t components = 0;
    uint16_t precision = 0;
    int32_t maxVal = 255;
    std::vector<uint16_t> samples; // Pixel-interleaved [height][width][components]
    bool hasPalette = false;
    uint8_t palWt = 0;
    std::vector<uint8_t> paletteBytes; // [entry * palWt + c]
};


namespace DetailJpegLs {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU24LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;

static constexpr int kJlsLog2Run[32] = {
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

inline int JlsIsoClip(int v, int vmin, int vmax) {
    return (v > vmax || v < vmin) ? vmin : v;
}

struct JlsState {
    int T1 = 0, T2 = 0, T3 = 0;
    int A[367] = {};
    int B[367] = {};
    int C[365] = {};
    int N[367] = {};
    int limit = 0;
    int reset = 64;
    int bpp = 8;
    int qbpp = 8;
    int maxVal = 255;
    int range = 256;
    int nearVal = 0;
    int twoNear = 1;
    int runIndex[4] = {};

    void ComputeDefaultThresholds(int customMaxVal, int customT1, int customT2, int customT3, int customReset) {
        maxVal = (customMaxVal > 0) ? customMaxVal : ((1 << bpp) - 1);
        T1 = customT1;
        T2 = customT2;
        T3 = customT3;
        reset = (customReset > 0) ? customReset : 64;

        const int basicT1 = 3, basicT2 = 7, basicT3 = 21;
        if (maxVal >= 128) {
            int factor = (std::min(maxVal, 4095) + 128) >> 8;
            if (T1 == 0) T1 = JlsIsoClip(factor * (basicT1 - 2) + 2 + 3 * nearVal, nearVal + 1, maxVal);
            if (T2 == 0) T2 = JlsIsoClip(factor * (basicT2 - 3) + 3 + 5 * nearVal, T1, maxVal);
            if (T3 == 0) T3 = JlsIsoClip(factor * (basicT3 - 4) + 4 + 7 * nearVal, T2, maxVal);
        } else {
            int factor = 256 / (maxVal + 1);
            if (T1 == 0) T1 = JlsIsoClip(std::max(2, basicT1 / factor + 3 * nearVal), nearVal + 1, maxVal);
            if (T2 == 0) T2 = JlsIsoClip(std::max(3, basicT2 / factor + 5 * nearVal), T1, maxVal);
            if (T3 == 0) T3 = JlsIsoClip(std::max(4, basicT3 / factor + 7 * nearVal), T2, maxVal);
        }
    }

    void InitContexts() {
        twoNear = nearVal * 2 + 1;
        range = (maxVal + 2 * nearVal) / twoNear + 1;
        for (qbpp = 1; (1 << qbpp) < range; ++qbpp) {}
        int effBpp = 2;
        while ((1 << effBpp) <= maxVal && effBpp < 16) ++effBpp;
        bpp = std::max(bpp, effBpp);
        limit = 2 * (bpp + std::max(8, bpp)) - qbpp;

        int initA = std::max((range + 32) >> 6, 2);
        for (int i = 0; i < 367; ++i) {
            A[i] = initA;
            B[i] = 0;
            N[i] = 1;
        }
        std::memset(C, 0, sizeof(C));
        std::memset(runIndex, 0, sizeof(runIndex));
    }

    int QuantizeGrad(int d) const {
        if (d == 0) return 0;
        if (d < 0) {
            if (d <= -T3) return -4;
            if (d <= -T2) return -3;
            if (d <= -T1) return -2;
            if (d < -nearVal) return -1;
            return 0;
        }
        if (d <= nearVal) return 0;
        if (d < T1) return 1;
        if (d < T2) return 2;
        if (d < T3) return 3;
        return 4;
    }

    void Downscale(int Q) {
        if (N[Q] == reset) {
            A[Q] >>= 1;
            B[Q] >>= 1;
            N[Q] >>= 1;
        }
        ++N[Q];
    }

    int UpdateRegular(int Q, int err) {
        int absErr = std::abs(err);
        if (absErr > 0x1FFFF) return -0x100000;
        A[Q] += absErr;
        int scaledErr = err * twoNear;
        B[Q] += scaledErr;
        Downscale(Q);

        if (B[Q] <= -N[Q]) {
            B[Q] = std::max(B[Q] + N[Q], 1 - N[Q]);
            if (C[Q] > -128) --C[Q];
        } else if (B[Q] > 0) {
            B[Q] = std::min(B[Q] - N[Q], 0);
            if (C[Q] < 127) ++C[Q];
        }
        return scaledErr;
    }

    int ReconstructSample(int px, int scaledErr) const {
        int rx = px + scaledErr;
        if (nearVal == 0) {
            if (rx < 0) rx += range;
            else if (rx >= range) rx -= range;
            return std::clamp(rx, 0, maxVal);
        }
        int modSpan = range * twoNear;
        if (rx < -nearVal) rx += modSpan;
        else if (rx > maxVal + nearVal) rx -= modSpan;
        return std::clamp(rx, 0, maxVal);
    }
};

// ITU-T T.87 Annex A.1 bit reader with JPEG-LS 0xFF -> 7-bit bit-stuffing (0xFF followed by 0xxxxxxx has 7 data bits!)
struct JlsBitReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    uint64_t bitBuf = 0;
    int bitCount = 0;
    bool hitMarker = false;
    int zeroFeedCount = 0;

    void Init(const uint8_t* d, size_t sz, size_t startPos) {
        data = d;
        size = sz;
        pos = startPos;
        bitBuf = 0;
        bitCount = 0;
        hitMarker = false;
        zeroFeedCount = 0;
    }

    void Refill() {
        while (bitCount <= 48) {
            if (hitMarker || pos >= size) {
                bitBuf <<= 8;
                bitCount += 8;
                ++zeroFeedCount;
                continue;
            }
            uint8_t b = data[pos++];
            if (b == 0xFFu) {
                if (pos >= size) {
                    hitMarker = true;
                    bitBuf <<= 8;
                    bitCount += 8;
                    ++zeroFeedCount;
                    continue;
                }
                uint8_t nx = data[pos];
                if ((nx & 0x80u) == 0u) {
                    // Stuffed 0 bit in MSB of nx: push 8 bits of 0xFF + 7 bits of nx = 15 bits!
                    ++pos;
                    bitBuf = (bitBuf << 15) | (0xFFull << 7) | static_cast<uint64_t>(nx & 0x7Fu);
                    bitCount += 15;
                    continue;
                } else {
                    // Marker 0xFF 0x80..0xFF: rewind onto 0xFF so outer scan loop sees marker
                    --pos;
                    hitMarker = true;
                    bitBuf <<= 8;
                    bitCount += 8;
                    ++zeroFeedCount;
                    continue;
                }
            }
            bitBuf = (bitBuf << 8) | b;
            bitCount += 8;
        }
    }

    bool Exhausted() const {
        return zeroFeedCount > 64;
    }

    uint32_t GetBit() {
        if (bitCount < 1) Refill();
        --bitCount;
        return static_cast<uint32_t>((bitBuf >> bitCount) & 1ull);
    }

    uint32_t GetBits(int n) {
        if (n <= 0) return 0u;
        if (bitCount < n) Refill();
        bitCount -= n;
        return static_cast<uint32_t>((bitBuf >> bitCount) & ((1ull << n) - 1ull));
    }

    int ReadGolomb(int k, int limit, int qbpp) {
        if (limit <= 0 || k < 0 || k > 24 || qbpp <= 0 || qbpp > 24) return -1;
        int unaryZeros = 0;
        while (unaryZeros < limit) {
            if (Exhausted()) return -1;
            if (GetBit() != 0u) break;
            ++unaryZeros;
        }
        if (unaryZeros < limit - 1) {
            uint32_t rem = (k > 0) ? GetBits(k) : 0u;
            return static_cast<int>((static_cast<uint32_t>(unaryZeros) << k) + rem);
        }
        if (unaryZeros == limit - 1) {
            uint32_t esc = GetBits(qbpp);
            return static_cast<int>(esc + 1u);
        }
        return -1;
    }
};

inline int MedPredict(int ra, int rb, int rc) {
    if (rc >= std::max(ra, rb)) return std::min(ra, rb);
    if (rc <= std::min(ra, rb)) return std::max(ra, rb);
    return ra + rb - rc;
}

inline bool DecodeRegularErr(JlsBitReader& br, JlsState& st, int Q, int& outScaledErr) {
    int k = 0;
    while ((static_cast<uint32_t>(st.N[Q]) << k) < static_cast<uint32_t>(st.A[Q]) && k < 24) {
        ++k;
    }
    int me = br.ReadGolomb(k, st.limit, st.qbpp);
    if (me < 0) return false;

    int err = (me & 1) ? -((me + 1) >> 1) : (me >> 1);
    if (st.nearVal == 0 && k == 0 && (2 * st.B[Q] <= -st.N[Q])) {
        err = -(err + 1);
    }
    int scaled = st.UpdateRegular(Q, err);
    if (scaled <= -0x100000) return false;
    outScaledErr = scaled;
    return true;
}

inline bool DecodeRunTermErr(JlsBitReader& br, JlsState& st, int riType, int limitAdd, int& outScaledErr) {
    int Q = 365 + riType;
    int temp = st.A[Q] + (riType ? (st.N[Q] >> 1) : 0);
    int k = 0;
    while ((static_cast<uint32_t>(st.N[Q]) << k) < static_cast<uint32_t>(temp) && k < 24) {
        ++k;
    }
    int me = br.ReadGolomb(k, st.limit - limitAdd - 1, st.qbpp);
    if (me < 0) return false;

    int map = (k == 0 && (riType != 0 || me != 0) && (2 * st.B[Q] < st.N[Q])) ? 1 : 0;
    int em = me + riType + map;
    int err = 0;
    if (em & 1) {
        err = map - ((em + 1) >> 1);
        ++st.B[Q];
    } else {
        err = em >> 1;
    }
    if (std::abs(err) > 0x1FFFF) return false;
    st.A[Q] += std::abs(err) - riType;
    outScaledErr = err * st.twoNear;
    st.Downscale(Q);
    return true;
}

inline bool DecodeSingleCompLine(
    JlsBitReader& br, JlsState& st,
    const int32_t* prevLine, int32_t* curLine,
    int32_t prevLineFirstSample, uint32_t width, int compRunSlot)
{
    uint32_t x = 0;
    while (x < width) {
        if (br.Exhausted()) return false;
        int ra = (x > 0) ? curLine[x - 1u] : prevLine[0];
        int rb = prevLine[x];
        int rc = (x > 0) ? prevLine[x - 1u] : prevLineFirstSample;
        int rd = (x + 1u < width) ? prevLine[x + 1u] : rb;

        int d0 = rd - rb;
        int d1 = rb - rc;
        int d2 = rc - ra;

        if (std::abs(d0) <= st.nearVal && std::abs(d1) <= st.nearVal && std::abs(d2) <= st.nearVal) {
            // Run Mode
            while (br.GetBit() != 0u) {
                if (br.Exhausted()) return false;
                uint32_t rg = 1u << kJlsLog2Run[st.runIndex[compRunSlot]];
                uint32_t cnt = std::min(rg, width - x);
                for (uint32_t i = 0; i < cnt; ++i) curLine[x++] = ra;
                if (cnt != rg) return true; // Truncated at EOL without incrementing RUNindex
                if (st.runIndex[compRunSlot] < 31) ++st.runIndex[compRunSlot];
                if (x >= width) return true; // Exact fit at EOL
            }
            int rk = kJlsLog2Run[st.runIndex[compRunSlot]];
            uint32_t remRun = (rk > 0) ? br.GetBits(rk) : 0u;
            if (x + remRun > width) return false; // Run overflow past EOL
            for (uint32_t i = 0; i < remRun; ++i) curLine[x++] = ra;
            if (x >= width) return false;

            rb = prevLine[x];
            int riType = (std::abs(ra - rb) <= st.nearVal) ? 1 : 0;
            int scaledErr = 0;
            if (!DecodeRunTermErr(br, st, riType, rk, scaledErr)) return false;
            if (st.runIndex[compRunSlot] > 0) --st.runIndex[compRunSlot];

            int px = riType ? ra : rb;
            if (!riType && (rb < ra)) scaledErr = -scaledErr;
            curLine[x++] = st.ReconstructSample(px, scaledErr);
        } else {
            // Regular Mode
            int q = st.QuantizeGrad(d0) * 81 + st.QuantizeGrad(d1) * 9 + st.QuantizeGrad(d2);
            int sign = 1;
            if (q < 0) { q = -q; sign = -1; }

            int px = MedPredict(ra, rb, rc);
            px = std::clamp(px + sign * st.C[q], 0, st.maxVal);

            int scaledErr = 0;
            if (!DecodeRegularErr(br, st, q, scaledErr)) return false;
            curLine[x++] = st.ReconstructSample(px, sign * scaledErr);
        }
    }
    return true;
}

inline bool DecodeSampleInterleavedLine(
    JlsBitReader& br, JlsState& st,
    const int32_t* prevLine, int32_t* curLine,
    const int32_t prevFirst[4], uint32_t width, uint32_t ns)
{
    uint32_t x = 0;
    while (x < width) {
        if (br.Exhausted()) return false;
        int ra[4], rb[4], rc[4], rd[4];
        int d0[4], d1[4], d2[4];
        bool allFlat = true;
        for (uint32_t c = 0; c < ns; ++c) {
            ra[c] = (x > 0) ? curLine[(x - 1u) * ns + c] : prevLine[c];
            rb[c] = prevLine[x * ns + c];
            rc[c] = (x > 0) ? prevLine[(x - 1u) * ns + c] : prevFirst[c];
            rd[c] = (x + 1u < width) ? prevLine[(x + 1u) * ns + c] : rb[c];
            d0[c] = rd[c] - rb[c];
            d1[c] = rb[c] - rc[c];
            d2[c] = rc[c] - ra[c];
            if (std::abs(d0[c]) > st.nearVal || std::abs(d1[c]) > st.nearVal || std::abs(d2[c]) > st.nearVal) {
                allFlat = false;
            }
        }

        if (allFlat) {
            // ITU-T T.87 Annex B.2.2 Sample-Interleaved Run Mode
            while (br.GetBit() != 0u) {
                if (br.Exhausted()) return false;
                uint32_t rg = 1u << kJlsLog2Run[st.runIndex[0]];
                uint32_t cnt = std::min(rg, width - x);
                for (uint32_t i = 0; i < cnt; ++i) {
                    for (uint32_t c = 0; c < ns; ++c) curLine[x * ns + c] = ra[c];
                    ++x;
                }
                if (cnt != rg) return true;
                if (st.runIndex[0] < 31) ++st.runIndex[0];
                if (x >= width) return true;
            }
            int rk = kJlsLog2Run[st.runIndex[0]];
            uint32_t remRun = (rk > 0) ? br.GetBits(rk) : 0u;
            if (x + remRun > width) return false;
            for (uint32_t i = 0; i < remRun; ++i) {
                for (uint32_t c = 0; c < ns; ++c) curLine[x * ns + c] = ra[c];
                ++x;
            }
            if (x >= width) return false;

            // Sample-interleaved run interruption always uses RItype = 0 for all components
            for (uint32_t c = 0; c < ns; ++c) {
                int rbc = prevLine[x * ns + c];
                int scaledErr = 0;
                if (!DecodeRunTermErr(br, st, 0, rk, scaledErr)) return false;
                if (rbc < ra[c]) scaledErr = -scaledErr;
                curLine[x * ns + c] = st.ReconstructSample(rbc, scaledErr);
            }
            if (st.runIndex[0] > 0) --st.runIndex[0];
            ++x;
        } else {
            for (uint32_t c = 0; c < ns; ++c) {
                int q = st.QuantizeGrad(d0[c]) * 81 + st.QuantizeGrad(d1[c]) * 9 + st.QuantizeGrad(d2[c]);
                int sign = 1;
                if (q < 0) { q = -q; sign = -1; }
                int px = MedPredict(ra[c], rb[c], rc[c]);
                px = std::clamp(px + sign * st.C[q], 0, st.maxVal);
                int scaledErr = 0;
                if (!DecodeRegularErr(br, st, q, scaledErr)) return false;
                curLine[x * ns + c] = st.ReconstructSample(px, sign * scaledErr);
            }
            ++x;
        }
    }
    return true;
}

} // namespace DetailJpegLs

inline bool IsLikelyJpegLsSignature(const uint8_t* data, size_t size) {
    if (!data || size < 16 || data[0] != 0xFFu || data[1] != 0xD8u) return false;
    size_t p = 2;
    for (int iter = 0; iter < 32 && p + 2 <= size; ++iter) {
        if (data[p] != 0xFFu) return false;
        while (p < size && data[p] == 0xFFu) ++p;
        if (p >= size) return false;
        uint8_t marker = data[p++];
        if (marker == 0xF7u) return true; // SOF55 = JPEG-LS!
        if (marker == 0xC0u || marker == 0xC1u || marker == 0xC2u || marker == 0xC3u || marker == 0xD9u || marker == 0xDAu) {
            return false;
        }
        if (marker == 0xD8u || (marker >= 0xD0u && marker <= 0xD7u)) continue;
        if (p + 2 > size) return false;
        uint16_t segLen = DetailJpegLs::RdU16BE(data + p);
        if (segLen < 2 || p + segLen > size) return false;
        p += segLen;
    }
    return false;
}

inline bool DecodeJpegLsToSamples(const uint8_t* data, size_t size, JpegLsDecodedImage& out) {
    using namespace DetailJpegLs;

    out = {};
    if (!data || size < 16 || data[0] != 0xFFu || data[1] != 0xD8u) return false;

    struct CompSpec {
        uint8_t id = 0;
        uint8_t hSamp = 1;
        uint8_t vSamp = 1;
    };
    CompSpec comps[4] = {};
    bool hasSof55 = false;
    int customMaxVal = 0, customT1 = 0, customT2 = 0, customT3 = 0, customReset = 0;
    uint16_t restartInterval = 0;
    uint8_t hpXfrm = 0;
    uint32_t scansDecoded = 0;

    std::vector<uint16_t> compPlanes[4];
    size_t p = 2;
    while (p + 2 <= size) {
        while (p < size && data[p] != 0xFFu) ++p;
        while (p < size && data[p] == 0xFFu) ++p;
        if (p >= size) break;
        uint8_t marker = data[p++];

        if (marker == 0xD9u) break; // EOI
        if (marker == 0xD8u || marker == 0x00u || (marker >= 0xD0u && marker <= 0xD7u)) continue;

        if (p + 2 > size) return false;
        uint16_t segLen = RdU16BE(data + p);
        if (segLen < 2 || p + segLen > size) return false;
        const uint8_t* seg = data + p + 2;
        size_t payloadLen = segLen - 2u;

        if (marker == 0xF7u) { // SOF55 — JPEG-LS Frame Header
            if (payloadLen < 6) return false;
            uint8_t prec = seg[0];
            uint32_t h = RdU16BE(seg + 1);
            uint32_t w = RdU16BE(seg + 3);
            uint8_t nf = seg[5];
            if (prec < 2 || prec > 16 || nf < 1 || nf > 4) return false;
            if (payloadLen < 6u + static_cast<size_t>(nf) * 3u) return false;
            out.precision = prec;
            if (h > 0) out.height = h;
            if (w > 0) out.width = w;
            out.components = nf;
            for (uint8_t c = 0; c < nf; ++c) {
                comps[c].id = seg[6 + c * 3 + 0];
                uint8_t hv = seg[6 + c * 3 + 1];
                comps[c].hSamp = std::max<uint8_t>(1u, static_cast<uint8_t>((hv >> 4) & 0x0Fu));
                comps[c].vSamp = std::max<uint8_t>(1u, static_cast<uint8_t>(hv & 0x0Fu));
            }
            hasSof55 = true;
            p += segLen;
        } else if (marker == 0xF8u) { // LSE — JPEG-LS Preset Parameters
            if (payloadLen < 1) return false;
            uint8_t lseId = seg[0];
            if (lseId == 1) {
                if (payloadLen < 11) return false;
                customMaxVal = RdU16BE(seg + 1);
                customT1     = RdU16BE(seg + 3);
                customT2     = RdU16BE(seg + 5);
                customT3     = RdU16BE(seg + 7);
                customReset  = RdU16BE(seg + 9);
            } else if (lseId == 2 || lseId == 3) {
                if (payloadLen >= 3) {
                    uint8_t wt = seg[2];
                    if (wt >= 1 && wt <= 4) {
                        out.hasPalette = true;
                        out.palWt = wt;
                        if (lseId == 2) out.paletteBytes.clear();
                        out.paletteBytes.insert(out.paletteBytes.end(), seg + 3, seg + payloadLen);
                    }
                }
            } else if (lseId == 4 && payloadLen >= 2) {
                uint8_t wxy = seg[1];
                if (wxy == 4 && payloadLen >= 10) {
                    out.height = RdU32BE(seg + 2);
                    out.width  = RdU32BE(seg + 6);
                } else if (wxy == 2 && payloadLen >= 6) {
                    out.height = RdU16BE(seg + 2);
                    out.width  = RdU16BE(seg + 4);
                }
            }
            p += segLen;
        } else if (marker == 0xDDu && payloadLen >= 2) { // DRI
            restartInterval = RdU16BE(seg);
            p += segLen;
        } else if (marker == 0xE8u && payloadLen >= 5 && std::memcmp(seg, "mrfx", 4) == 0) { // HP color xfrm
            hpXfrm = seg[4];
            p += segLen;
        } else if (marker == 0xDAu) { // SOS — Start of Scan
            if (!hasSof55 || out.width == 0 || out.height == 0 || out.width > 16384 || out.height > 16384) return false;
            if (static_cast<uint64_t>(out.width) * out.height * out.components > 64ull * 1024ull * 1024ull) return false;
            if (payloadLen < 4) return false;

            uint8_t ns = seg[0];
            if (ns < 1 || ns > out.components || payloadLen < 1u + static_cast<size_t>(ns) * 2u + 3u) return false;

            uint32_t scanCompIdx[4] = {};
            for (uint8_t sc = 0; sc < ns; ++sc) {
                uint8_t cid = seg[1 + sc * 2u];
                uint32_t found = sc;
                for (uint32_t k = 0; k < out.components; ++k) {
                    if (comps[k].id == cid) { found = k; break; }
                }
                scanCompIdx[sc] = std::min(found, out.components - 1u);
            }

            int nearParam = seg[1 + ns * 2u + 0];
            int ilv       = seg[1 + ns * 2u + 1];
            int pt        = seg[1 + ns * 2u + 2] & 0x0Fu;
            if (ilv < 0 || ilv > 2 || pt >= out.precision) return false;
            if (ilv == 0 && ns != 1) return false;

            JlsState st;
            st.bpp = std::max<int>(2, out.precision - pt);
            st.nearVal = nearParam;
            st.ComputeDefaultThresholds(customMaxVal, customT1, customT2, customT3, customReset);
            if (st.maxVal <= 0 || st.maxVal >= (1 << 16) || st.nearVal > st.maxVal) return false;
            if (st.T1 < st.nearVal + 1 || st.T1 > st.T2 || st.T2 > st.T3 || st.T3 > st.maxVal) return false;
            st.InitContexts();
            out.maxVal = (st.maxVal << pt);

            for (uint32_t c = 0; c < out.components; ++c) {
                if (compPlanes[c].empty()) {
                    compPlanes[c].assign(static_cast<size_t>(out.width) * out.height, 0u);
                }
            }

            size_t scanDataStart = p + segLen;
            JlsBitReader br;
            br.Init(data, size, scanDataStart);

            auto handleRestartIfNeeded = [&](uint32_t rowIdx, std::vector<int32_t>& prevRow, int32_t prevFirst[4]) -> bool {
                if (restartInterval == 0 || rowIdx == 0 || (rowIdx % restartInterval) != 0) return true;
                size_t mp = br.pos;
                while (mp + 1 < size && !(data[mp] == 0xFFu && data[mp + 1] >= 0xD0u && data[mp + 1] <= 0xD7u)) {
                    if (data[mp] == 0xFFu && (data[mp + 1] == 0xD9u || data[mp + 1] == 0xDAu)) break;
                    ++mp;
                }
                if (mp + 1 < size && data[mp] == 0xFFu && data[mp + 1] >= 0xD0u && data[mp + 1] <= 0xD7u) {
                    mp += 2;
                }
                br.Init(data, size, mp);
                st.InitContexts();
                std::fill(prevRow.begin(), prevRow.end(), 0);
                for (int k = 0; k < 4; ++k) prevFirst[k] = 0;
                return true;
            };

            if (ilv == 0) {
                uint32_t dstComp = scanCompIdx[0];
                if (scansDecoded >= out.components && compPlanes[dstComp].size() > 0 && dstComp == 0) {
                    dstComp = std::min(scansDecoded, out.components - 1u);
                }
                std::vector<int32_t> prevRow(out.width, 0);
                std::vector<int32_t> curRow(out.width, 0);
                int32_t prevFirst[4] = {};

                for (uint32_t y = 0; y < out.height; ++y) {
                    handleRestartIfNeeded(y, prevRow, prevFirst);
                    if (!DecodeSingleCompLine(br, st, prevRow.data(), curRow.data(), prevFirst[0], out.width, 0)) {
                        return false;
                    }
                    prevFirst[0] = prevRow[0];
                    uint16_t* dstP = &compPlanes[dstComp][static_cast<size_t>(y) * out.width];
                    for (uint32_t x = 0; x < out.width; ++x) {
                        dstP[x] = static_cast<uint16_t>(std::clamp(curRow[x] << pt, 0, 65535));
                    }
                    prevRow.swap(curRow);
                }
            } else if (ilv == 1) {
                std::vector<int32_t> prevRows[4], curRows[4];
                int32_t prevFirst[4] = {};
                for (uint8_t sc = 0; sc < ns; ++sc) {
                    prevRows[sc].assign(out.width, 0);
                    curRows[sc].assign(out.width, 0);
                }
                for (uint32_t y = 0; y < out.height; ++y) {
                    if (restartInterval > 0 && y > 0 && (y % restartInterval) == 0) {
                        handleRestartIfNeeded(y, prevRows[0], prevFirst);
                        for (uint8_t sc = 1; sc < ns; ++sc) std::fill(prevRows[sc].begin(), prevRows[sc].end(), 0);
                    }
                    for (uint8_t sc = 0; sc < ns; ++sc) {
                        if (!DecodeSingleCompLine(br, st, prevRows[sc].data(), curRows[sc].data(), prevFirst[sc], out.width, sc)) {
                            return false;
                        }
                        prevFirst[sc] = prevRows[sc][0];
                        uint32_t dstComp = scanCompIdx[sc];
                        uint16_t* dstP = &compPlanes[dstComp][static_cast<size_t>(y) * out.width];
                        for (uint32_t x = 0; x < out.width; ++x) {
                            dstP[x] = static_cast<uint16_t>(std::clamp(curRows[sc][x] << pt, 0, 65535));
                        }
                        prevRows[sc].swap(curRows[sc]);
                    }
                }
            } else { // ilv == 2 (Sample-Interleaved)
                std::vector<int32_t> prevRow(static_cast<size_t>(out.width) * ns, 0);
                std::vector<int32_t> curRow(static_cast<size_t>(out.width) * ns, 0);
                int32_t prevFirst[4] = {};

                for (uint32_t y = 0; y < out.height; ++y) {
                    handleRestartIfNeeded(y, prevRow, prevFirst);
                    if (!DecodeSampleInterleavedLine(br, st, prevRow.data(), curRow.data(), prevFirst, out.width, ns)) {
                        return false;
                    }
                    for (uint8_t sc = 0; sc < ns; ++sc) {
                        prevFirst[sc] = prevRow[sc];
                        uint32_t dstComp = scanCompIdx[sc];
                        uint16_t* dstP = &compPlanes[dstComp][static_cast<size_t>(y) * out.width];
                        for (uint32_t x = 0; x < out.width; ++x) {
                            dstP[x] = static_cast<uint16_t>(std::clamp(curRow[x * ns + sc] << pt, 0, 65535));
                        }
                    }
                    prevRow.swap(curRow);
                }
            }

            ++scansDecoded;
            p = br.pos;
        } else {
            p += segLen;
        }
    }

    if (scansDecoded == 0 || out.width == 0 || out.height == 0) return false;

    const size_t numPx = static_cast<size_t>(out.width) * out.height;
    out.samples.resize(numPx * out.components);
    for (size_t i = 0; i < numPx; ++i) {
        for (uint32_t c = 0; c < out.components; ++c) {
            out.samples[i * out.components + c] = compPlanes[c][i];
        }
    }

    // Optional HP LOCO-I lossless color transform (APP8 "mrfx") for 8-bit 3-component RGB
    if (hpXfrm >= 1 && hpXfrm <= 3 && out.components == 3 && out.precision <= 8) {
        for (size_t i = 0; i < numPx; ++i) {
            uint8_t c0 = static_cast<uint8_t>(out.samples[i * 3 + 0]);
            uint8_t c1 = static_cast<uint8_t>(out.samples[i * 3 + 1]);
            uint8_t c2 = static_cast<uint8_t>(out.samples[i * 3 + 2]);
            if (hpXfrm == 1) {
                out.samples[i * 3 + 0] = static_cast<uint8_t>(c0 + c1 + 128u);
                out.samples[i * 3 + 2] = static_cast<uint8_t>(c2 + c1 + 128u);
            } else if (hpXfrm == 2) {
                uint8_t r = static_cast<uint8_t>(c0 + c1 + 128u);
                uint8_t b = static_cast<uint8_t>(c2 + ((static_cast<int>(r) + c1) >> 1) + 128);
                out.samples[i * 3 + 0] = r;
                out.samples[i * 3 + 2] = b;
            } else if (hpXfrm == 3) {
                uint8_t g = static_cast<uint8_t>(c0 - ((static_cast<int>(c2) + c1) >> 2) + 64);
                out.samples[i * 3 + 0] = static_cast<uint8_t>(c2 + g + 128u);
                out.samples[i * 3 + 1] = g;
                out.samples[i * 3 + 2] = static_cast<uint8_t>(c1 + g + 128u);
            }
        }
    }
    return true;
}

inline bool DecodeJpegLs(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    JpegLsDecodedImage img;
    if (!DecodeJpegLsToSamples(data, size, img)) return false;

    const size_t numPx = static_cast<size_t>(img.width) * img.height;
    const int32_t denom = std::max<int32_t>(1, (img.maxVal > 0) ? img.maxVal : ((1 << img.precision) - 1));
    auto scaleTo8 = [&](uint16_t v) -> uint8_t {
        if (denom == 255) return static_cast<uint8_t>(std::min<uint16_t>(v, 255u));
        return static_cast<uint8_t>(std::clamp<int64_t>((static_cast<int64_t>(v) * 255 + (denom / 2)) / denom, 0, 255));
    };

    outInfo.width = img.width;
    outInfo.height = img.height;
    outInfo.channels = (img.components == 4 || (img.hasPalette && img.palWt == 4)) ? 4u : 3u;
    outInfo.bitsPerChannel = img.precision;
    outInfo.formatName = L"JPEG-LS Lossless Image (.jls)";
    outInfo.bgraPixels.resize(numPx * 4u);

    for (size_t i = 0; i < numPx; ++i) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (img.components == 1 && img.hasPalette && img.palWt >= 1) {
            size_t idx = img.samples[i];
            size_t palEntries = img.paletteBytes.size() / img.palWt;
            if (idx < palEntries) {
                const uint8_t* pe = &img.paletteBytes[idx * img.palWt];
                if (img.palWt == 1) { r = g = b = pe[0]; }
                else if (img.palWt == 3) { r = pe[0]; g = pe[1]; b = pe[2]; }
                else if (img.palWt == 4) { r = pe[0]; g = pe[1]; b = pe[2]; a = pe[3]; }
            }
        } else if (img.components == 1) {
            r = g = b = scaleTo8(img.samples[i]);
        } else if (img.components == 3) {
            r = scaleTo8(img.samples[i * 3 + 0]);
            g = scaleTo8(img.samples[i * 3 + 1]);
            b = scaleTo8(img.samples[i * 3 + 2]);
        } else if (img.components >= 4) {
            r = scaleTo8(img.samples[i * img.components + 0]);
            g = scaleTo8(img.samples[i * img.components + 1]);
            b = scaleTo8(img.samples[i * img.components + 2]);
            a = scaleTo8(img.samples[i * img.components + 3]);
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
