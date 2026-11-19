#pragma once
// JpegXl.h — ISO/IEC 18181-1 JPEG XL Codestream & Container Decoder (.jxl)

namespace Formats {

namespace DetailJxlBpg {

inline int32_t UnpackSigned(uint32_t u) {
    return static_cast<int32_t>((u >> 1u) ^ (~(u & 1u) + 1u));
}


struct JxlAnsBucket {
    uint16_t cutoff = 0;
    uint16_t rightSym = 0;
    int32_t offset = 0;
};

struct JxlAnsDist {
    std::vector<uint16_t> freqs;
    std::vector<JxlAnsBucket> buckets;
    int32_t singleSymbol = -1;
};

inline uint32_t ReadJxlU8(DetailBrotli::BitReader& br) {
    if (br.ReadBits(1u) == 0u) return 0u;
    uint32_t n = br.ReadBits(3u);
    return (1u << n) + br.ReadBits(n);
}

inline bool BuildJxlAliasTable(uint32_t logAlphaSize, JxlAnsDist& dist) {
    const uint32_t tableSize = 1u << logAlphaSize;
    const uint32_t logBucketSize = 12u - logAlphaSize;
    const uint32_t bucketSize = 1u << logBucketSize;
    if (dist.freqs.size() < tableSize) dist.freqs.resize(tableSize, 0u);
    dist.buckets.assign(tableSize, JxlAnsBucket{});
    dist.singleSymbol = -1;

    uint32_t sum = 0u;
    int32_t onlySym = -1;
    uint32_t nonZero = 0u;
    for (uint32_t s = 0; s < tableSize; ++s) {
        sum += dist.freqs[s];
        if (dist.freqs[s] > 0u) {
            onlySym = static_cast<int32_t>(s);
            ++nonZero;
        }
    }
    if (sum != 4096u || nonZero == 0u) return false;

    if (nonZero == 1u && onlySym >= 0) {
        dist.singleSymbol = onlySym;
        for (uint32_t i = 0; i < tableSize; ++i) {
            dist.buckets[i].cutoff = 0u;
            dist.buckets[i].rightSym = static_cast<uint16_t>(onlySym);
            dist.buckets[i].offset = static_cast<int32_t>(i * bucketSize);
        }
        return true;
    }

    std::vector<int32_t> cutoff(tableSize, 0);
    std::vector<uint32_t> underfull, overfull;
    for (uint32_t i = 0; i < tableSize; ++i) {
        cutoff[i] = static_cast<int32_t>(dist.freqs[i]);
        dist.buckets[i].rightSym = static_cast<uint16_t>(i);
        dist.buckets[i].offset = 0;
        if (cutoff[i] > static_cast<int32_t>(bucketSize)) {
            overfull.push_back(i);
        } else if (cutoff[i] < static_cast<int32_t>(bucketSize)) {
            underfull.push_back(i);
        }
    }

    while (!overfull.empty() && !underfull.empty()) {
        uint32_t u = underfull.back(); underfull.pop_back();
        uint32_t o = overfull.back();  overfull.pop_back();
        int32_t by = static_cast<int32_t>(bucketSize) - cutoff[u];
        cutoff[o] -= by;
        dist.buckets[u].cutoff = static_cast<uint16_t>(cutoff[u]);
        dist.buckets[u].rightSym = static_cast<uint16_t>(o);
        dist.buckets[u].offset = cutoff[o] - cutoff[u];
        if (cutoff[o] > static_cast<int32_t>(bucketSize)) {
            overfull.push_back(o);
        } else if (cutoff[o] < static_cast<int32_t>(bucketSize)) {
            underfull.push_back(o);
        }
    }

    for (uint32_t i = 0; i < tableSize; ++i) {
        if (cutoff[i] == static_cast<int32_t>(bucketSize)) {
            dist.buckets[i].cutoff = static_cast<uint16_t>(bucketSize);
            dist.buckets[i].rightSym = static_cast<uint16_t>(i);
            dist.buckets[i].offset = 0;
        } else {
            dist.buckets[i].cutoff = static_cast<uint16_t>(cutoff[i]);
        }
    }
    return overfull.empty() && underfull.empty();
}

inline bool ReadJxlAnsDistribution(DetailBrotli::BitReader& br, uint32_t logAlphaSize, JxlAnsDist& outDist) {
    if (logAlphaSize < 5u || logAlphaSize > 8u) return false;
    const uint32_t maxAlpha = 1u << logAlphaSize;
    outDist.freqs.assign(maxAlpha, 0u);

    bool isSimple = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (isSimple) {
        bool twoSyms = br.ReadBits(1u) != 0u;
        if (!twoSyms) {
            uint32_t v1 = ReadJxlU8(br);
            if (br.eof || v1 >= maxAlpha) return false;
            outDist.freqs[v1] = 4096u;
        } else {
            uint32_t v1 = ReadJxlU8(br);
            uint32_t v2 = ReadJxlU8(br);
            uint32_t prob1 = br.ReadBits(12u);
            if (br.eof || v1 >= maxAlpha || v2 >= maxAlpha || v1 == v2) return false;
            outDist.freqs[v1] = static_cast<uint16_t>(prob1);
            outDist.freqs[v2] = static_cast<uint16_t>(4096u - prob1);
        }
        return BuildJxlAliasTable(logAlphaSize, outDist);
    }

    bool isFlat = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (isFlat) {
        uint32_t alphaSize = ReadJxlU8(br) + 1u;
        if (br.eof || alphaSize == 0u || alphaSize > maxAlpha) return false;
        uint32_t base = 4096u / alphaSize;
        uint32_t rem  = 4096u % alphaSize;
        for (uint32_t s = 0; s < alphaSize; ++s) {
            outDist.freqs[s] = static_cast<uint16_t>(base + (s < rem ? 1u : 0u));
        }
        return BuildJxlAliasTable(logAlphaSize, outDist);
    }

    // General histogram (ISO/IEC 18181-1 Clause 7.1.2)
    uint32_t shiftLen = 0u;
    while (shiftLen < 3u && br.ReadBits(1u) != 0u) ++shiftLen;
    uint32_t shift = 0u;
    if (shiftLen == 0u)      shift = 0u;
    else if (shiftLen == 1u) shift = 1u + br.ReadBits(1u);
    else if (shiftLen == 2u) shift = 3u + br.ReadBits(2u);
    else                     shift = 7u + br.ReadBits(3u);
    if (br.eof || shift > 13u) return false;

    uint32_t alphaSize = ReadJxlU8(br) + 3u;
    if (br.eof || alphaSize > maxAlpha) return false;

    // Static canonical prefix code for logcounts 0..13 (13 = RLE repeat)
    static constexpr uint8_t kLogCountLengths[14] = {
        2, 7, 6, 5, 4, 4, 3, 3, 3, 4, 4, 5, 7, 5
    };
    DetailBrotli::PrefixTree lcTree;
    if (!lcTree.BuildFromLengths(kLogCountLengths, 14u)) return false;

    std::vector<uint8_t> logCounts(alphaSize, 0u);
    int32_t omitLog = -1;
    int32_t omitPos = -1;
    for (uint32_t i = 0; i < alphaSize; ) {
        uint32_t lc = 0u;
        if (!lcTree.Decode(br, lc)) return false;
        if (lc == 13u) {
            if (i == 0u) return false;
            uint32_t rle = ReadJxlU8(br) + 4u;
            if (br.eof || i + rle > alphaSize) return false;
            uint8_t prevLc = logCounts[i - 1u];
            for (uint32_t k = 0; k < rle; ++k) logCounts[i++] = prevLc;
        } else {
            logCounts[i] = static_cast<uint8_t>(lc);
            if (static_cast<int32_t>(lc) > omitLog) {
                omitLog = static_cast<int32_t>(lc);
                omitPos = static_cast<int32_t>(i);
            }
            ++i;
        }
    }
    if (omitPos < 0 || omitLog <= 0) return false;

    uint32_t totalOthers = 0u;
    for (uint32_t i = 0; i < alphaSize; ++i) {
        if (static_cast<int32_t>(i) == omitPos || logCounts[i] == 0u) continue;
        uint32_t L = logCounts[i];
        uint32_t cnt = 1u;
        if (L > 1u) {
            int32_t bitCount = std::clamp<int32_t>(
                static_cast<int32_t>(shift) - ((12 - static_cast<int32_t>(L)) >> 1),
                0, static_cast<int32_t>(L) - 1);
            uint32_t extra = (bitCount > 0) ? br.ReadBits(static_cast<uint32_t>(bitCount)) : 0u;
            cnt = (1u << (L - 1u)) + (extra << (L - 1u - static_cast<uint32_t>(bitCount)));
        }
        outDist.freqs[i] = static_cast<uint16_t>(cnt);
        totalOthers += cnt;
        if (br.eof || totalOthers >= 4096u) return false;
    }
    outDist.freqs[omitPos] = static_cast<uint16_t>(4096u - totalOthers);
    return BuildJxlAliasTable(logAlphaSize, outDist);
}

// ============================================================================
// XYB Opsin Inverse Transform -> sRGB (ISO/IEC 18181-1 Clause 11.2)
// ============================================================================
struct JxlOpsinParams {
    float invMat[9] = {
         11.031566901960783f, -9.866943921568629f, -0.16462299647058826f,
         -3.254147380392157f,  4.418770392156863f, -0.16462299647058826f,
         -3.6588512862745097f, 2.7129230470588235f, 1.9459282392156863f
    };
    float opsinBias[3] = {
        0.0037930732552754493f,
        0.0037930732552754493f,
        0.0037930732552754493f
    };
    float cbrtBias[3] = {
        0.15595420054924863f,
        0.15595420054924863f,
        0.15595420054924863f
    };
    float quantBias[3] = {
        1.0f - 0.05465007330715401f,
        1.0f - 0.07005449891748593f,
        1.0f - 0.049935103337343655f
    };
    float quantBiasNumerator = 0.145f;
};

inline bool ReadJxlF16(DetailBrotli::BitReader& br, float& outVal) {
    uint16_t h = static_cast<uint16_t>(br.ReadBits(16u));
    if (br.eof || (h & 0x7C00u) == 0x7C00u) return false; // Inf/NaN disallowed by ISO 18181-1
    outVal = DetailImg100::HalfToFloat(h);
    return true;
}

inline float JxlLinearToSrgb01(float u, uint32_t transferFn) {
    u = std::clamp(u, 0.0f, 1.0f);
    if (transferFn == 8u) return u; // Linear light
    if (transferFn == 16u) {        // SMPTE ST 2084 PQ (10000 nits normalized)
        constexpr float m1 = 0.1593017578125f;
        constexpr float m2 = 78.84375f;
        constexpr float c1 = 0.8359375f;
        constexpr float c2 = 18.8515625f;
        constexpr float c3 = 18.6875f;
        float ym = std::pow(u, m1);
        return std::clamp(std::pow((c1 + c2 * ym) / (1.0f + c3 * ym), m2), 0.0f, 1.0f);
    }
    // Default sRGB (IEC 61966-2-1, transferFn == 13)
    return (u <= 0.0031308f) ? (12.92f * u) : (1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f);
}

inline void JxlXybToSrgb8(
    float valX, float valY, float valB,
    const JxlOpsinParams& op,
    uint32_t transferFn,
    uint8_t& outR, uint8_t& outG, uint8_t& outB)
{
    auto signedCube = [](float g, float cbBias, float bias) -> float {
        float v = g + cbBias;
        float sign = (v < 0.0f) ? -1.0f : 1.0f;
        float a = std::abs(v);
        return sign * (a * a * a) - bias;
    };

    float lGamma = valY + valX;
    float mGamma = valY - valX;
    float sGamma = valB;

    float mixL = signedCube(lGamma, op.cbrtBias[0], op.opsinBias[0]);
    float mixM = signedCube(mGamma, op.cbrtBias[1], op.opsinBias[1]);
    float mixS = signedCube(sGamma, op.cbrtBias[2], op.opsinBias[2]);

    float linR = op.invMat[0] * mixL + op.invMat[1] * mixM + op.invMat[2] * mixS;
    float linG = op.invMat[3] * mixL + op.invMat[4] * mixM + op.invMat[5] * mixS;
    float linB = op.invMat[6] * mixL + op.invMat[7] * mixM + op.invMat[8] * mixS;

    outR = static_cast<uint8_t>(std::clamp(std::lround(JxlLinearToSrgb01(linR, transferFn) * 255.0f), 0l, 255l));
    outG = static_cast<uint8_t>(std::clamp(std::lround(JxlLinearToSrgb01(linG, transferFn) * 255.0f), 0l, 255l));
    outB = static_cast<uint8_t>(std::clamp(std::lround(JxlLinearToSrgb01(linB, transferFn) * 255.0f), 0l, 255l));
}

struct JxlHybridUintCfg {
    uint32_t splitExp = 0;
    uint32_t splitToken = 0;
    uint32_t msbInToken = 0;
    uint32_t lsbInToken = 0;
};

struct JxlHistogramSet {
    bool lz77Enabled = false;
    uint32_t lzMinSymbol = 224;
    uint32_t lzMinLength = 3;
    JxlHybridUintCfg lzLenCfg;
    std::vector<uint8_t> clusterMap;
    std::vector<JxlHybridUintCfg> uintCfgs;
    std::vector<DetailBrotli::PrefixTree> trees;

    // Pure-C++ rANS state & alias distributions (when use_prefix_code == 0)
    bool useAns = false;
    uint32_t ansLogAlphaSize = 8u;
    uint32_t ansLogBucketSize = 4u;
    std::vector<JxlAnsDist> ansDists;
    bool ansStateInitialized = false;
    uint32_t ansState = 0u;

    // Stateful LZ77 window across symbols within a ModularStream
    uint32_t lzNumToCopy = 0;
    uint32_t lzCopyDist = 1;
    std::vector<uint32_t> lzWindow;
    uint32_t lzWinPos = 0;
};

inline bool ReadJxlSymbol(DetailBrotli::BitReader& br, JxlHistogramSet& hs, uint32_t ctx, uint32_t& outVal);

inline bool ReadHybridUintConfig(DetailBrotli::BitReader& br, uint32_t logAlphaSize, JxlHybridUintCfg& cfg) {
    uint32_t splitBits = DetailBrotli::CeilLog2(logAlphaSize + 1u);
    cfg.splitExp = br.ReadBits(splitBits);
    if (br.eof || cfg.splitExp > logAlphaSize) return false;
    cfg.splitToken = 1u << cfg.splitExp;
    if (cfg.splitExp != logAlphaSize) {
        uint32_t msbBits = DetailBrotli::CeilLog2(cfg.splitExp + 1u);
        cfg.msbInToken = br.ReadBits(msbBits);
        if (br.eof || cfg.msbInToken > cfg.splitExp) return false;
        uint32_t lsbBits = DetailBrotli::CeilLog2(cfg.splitExp - cfg.msbInToken + 1u);
        cfg.lsbInToken = br.ReadBits(lsbBits);
        if (br.eof || cfg.msbInToken + cfg.lsbInToken > cfg.splitExp) return false;
    } else {
        cfg.msbInToken = 0u;
        cfg.lsbInToken = 0u;
    }
    return true;
}

inline uint32_t ReadHybridUintDirect(DetailBrotli::BitReader& br, uint32_t token, const JxlHybridUintCfg& cfg) {
    if (token < cfg.splitToken) return token;
    uint32_t n = cfg.splitExp - (cfg.msbInToken + cfg.lsbInToken) +
                 ((token - cfg.splitToken) >> (cfg.msbInToken + cfg.lsbInToken));
    if (n >= 30u) {
        br.eof = true;
        return 0u;
    }
    uint32_t low = token & ((1u << cfg.lsbInToken) - 1u);
    token >>= cfg.lsbInToken;
    token &= ((1u << cfg.msbInToken) - 1u);
    token |= (1u << cfg.msbInToken);
    uint64_t res = (((static_cast<uint64_t>(token) << n) | br.ReadBits(n)) << cfg.lsbInToken) | low;
    if (res > 0xFFFFFFFFull) {
        br.eof = true;
        return 0u;
    }
    return static_cast<uint32_t>(res);
}

inline bool DecodeJxlHistograms(DetailBrotli::BitReader& br, uint32_t numContexts, JxlHistogramSet& hs) {
    if (numContexts == 0u || numContexts > (1u << 16u)) return false;
    hs.lz77Enabled = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                     uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return c0 + br.ReadBits(u0);
        if (sel == 1u) return c1 + br.ReadBits(u1);
        if (sel == 2u) return c2 + br.ReadBits(u2);
        return c3 + br.ReadBits(u3);
    };
    if (hs.lz77Enabled) {
        hs.lzMinSymbol = rdU32(224, 0, 512, 0, 4096, 0, 8, 15);
        hs.lzMinLength = rdU32(3, 0, 4, 0, 5, 2, 9, 8);
        if (br.eof || !ReadHybridUintConfig(br, 8u, hs.lzLenCfg)) return false;
        hs.lzWindow.assign(1u << 20u, 0u);
        hs.lzWinPos = 0u;
        hs.lzNumToCopy = 0u;
    }

    uint32_t totalContexts = numContexts + (hs.lz77Enabled ? 1u : 0u);
    hs.clusterMap.assign(totalContexts, 0u);
    uint32_t numClusters = 1u;
    if (totalContexts > 1u) {
        bool simpleCluster = br.ReadBits(1u) != 0u;
        if (br.eof) return false;
        if (simpleCluster) {
            uint32_t nbits = br.ReadBits(2u);
            numClusters = 0u;
            for (uint32_t i = 0; i < totalContexts; ++i) {
                uint8_t c = static_cast<uint8_t>(br.ReadBits(nbits));
                hs.clusterMap[i] = c;
                if (static_cast<uint32_t>(c) + 1u > numClusters) numClusters = static_cast<uint32_t>(c) + 1u;
            }
            if (br.eof) return false;
        } else {
            bool useMtf = br.ReadBits(1u) != 0u;
            JxlHistogramSet nested;
            if (!DecodeJxlHistograms(br, 1u, nested)) return false;
            uint8_t mtf[256];
            for (uint32_t k = 0; k < 256u; ++k) mtf[k] = static_cast<uint8_t>(k);
            numClusters = 0u;
            for (uint32_t i = 0; i < totalContexts; ++i) {
                uint32_t sym = 0u;
                if (!ReadJxlSymbol(br, nested, 0u, sym) || sym >= 256u) return false;
                uint8_t c = static_cast<uint8_t>(sym);
                if (useMtf) {
                    uint8_t val = mtf[c];
                    for (uint8_t m = c; m > 0u; --m) mtf[m] = mtf[m - 1u];
                    mtf[0] = val;
                    c = val;
                }
                hs.clusterMap[i] = c;
                if (static_cast<uint32_t>(c) + 1u > numClusters) numClusters = static_cast<uint32_t>(c) + 1u;
            }
            if (nested.useAns && nested.ansStateInitialized && nested.ansState != 0x00130000u) return false;
        }
    }
    if (numClusters == 0u || numClusters > 256u) return false;

    bool usePrefixCode = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!usePrefixCode) {
        hs.useAns = true;
        hs.ansStateInitialized = false;
        hs.ansState = 0u;
        hs.ansLogAlphaSize = 5u + br.ReadBits(2u);
        hs.ansLogBucketSize = 12u - hs.ansLogAlphaSize;
        if (br.eof) return false;
        hs.uintCfgs.resize(numClusters);
        for (uint32_t i = 0; i < numClusters; ++i) {
            if (!ReadHybridUintConfig(br, hs.ansLogAlphaSize, hs.uintCfgs[i])) return false;
        }
        hs.ansDists.resize(numClusters);
        for (uint32_t i = 0; i < numClusters; ++i) {
            if (!ReadJxlAnsDistribution(br, hs.ansLogAlphaSize, hs.ansDists[i])) return false;
        }
        return !br.eof;
    }

    hs.useAns = false;
    const uint32_t logAlphaSize = 15u;
    hs.uintCfgs.resize(numClusters);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (!ReadHybridUintConfig(br, logAlphaSize, hs.uintCfgs[i])) return false;
    }
    std::vector<uint32_t> alphaSizes(numClusters, 1u);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (br.ReadBits(1u) != 0u) {
            uint32_t n = br.ReadBits(4u);
            alphaSizes[i] = 1u + (1u << n) + br.ReadBits(n);
            if (br.eof || alphaSizes[i] > (1u << 15u)) return false;
        }
    }
    hs.trees.resize(numClusters);
    for (uint32_t i = 0; i < numClusters; ++i) {
        if (!DetailBrotli::ReadPrefixCode(br, alphaSizes[i], hs.trees[i])) return false;
    }
    return !br.eof;
}

inline bool DecodeJxlRawToken(DetailBrotli::BitReader& br, JxlHistogramSet& hs, uint8_t cluster, uint32_t& outTok) {
    if (!hs.useAns) {
        if (cluster >= hs.trees.size()) return false;
        return hs.trees[cluster].Decode(br, outTok);
    }
    if (cluster >= hs.ansDists.size()) return false;
    if (!hs.ansStateInitialized) {
        uint32_t hi = br.ReadBits(16u);
        uint32_t lo = br.ReadBits(16u);
        if (br.eof) return false;
        hs.ansState = (hi << 16u) | lo;
        hs.ansStateInitialized = true;
    }
    const auto& dist = hs.ansDists[cluster];
    if (dist.singleSymbol >= 0) {
        outTok = static_cast<uint32_t>(dist.singleSymbol);
        return true;
    }
    uint32_t res = hs.ansState & 0xFFFu;
    uint32_t bIdx = res >> hs.ansLogBucketSize;
    uint32_t pos  = res & ((1u << hs.ansLogBucketSize) - 1u);
    const auto& b = dist.buckets[bIdx];
    uint32_t sym = 0u;
    uint32_t offset = 0u;
    if (pos < b.cutoff) {
        sym = bIdx;
        offset = pos;
    } else {
        sym = b.rightSym;
        offset = static_cast<uint32_t>(static_cast<int32_t>(pos) + b.offset);
    }
    uint32_t freq = dist.freqs[sym];
    if (freq == 0u) return false;
    hs.ansState = freq * (hs.ansState >> 12u) + offset;
    if (hs.ansState < (1u << 16u)) {
        uint32_t low16 = br.ReadBits(16u);
        if (br.eof) return false;
        hs.ansState = (hs.ansState << 16u) | low16;
    }
    outTok = sym;
    return true;
}

inline bool ReadJxlSymbol(DetailBrotli::BitReader& br, JxlHistogramSet& hs, uint32_t ctx, uint32_t& outVal) {
    if (hs.lz77Enabled && hs.lzNumToCopy > 0u) {
        --hs.lzNumToCopy;
        uint32_t v = hs.lzWindow[(hs.lzWinPos - hs.lzCopyDist) & 0xFFFFFu];
        hs.lzWindow[hs.lzWinPos & 0xFFFFFu] = v;
        ++hs.lzWinPos;
        outVal = v;
        return true;
    }
    if (ctx >= hs.clusterMap.size()) return false;
    uint8_t cluster = hs.clusterMap[ctx];
    uint32_t tok = 0u;
    if (!DecodeJxlRawToken(br, hs, cluster, tok)) return false;
    if (hs.lz77Enabled && tok >= hs.lzMinSymbol) {
        uint32_t lenTok = tok - hs.lzMinSymbol;
        uint32_t copyLen = hs.lzMinLength + ReadHybridUintDirect(br, lenTok, hs.lzLenCfg);
        if (br.eof || copyLen == 0u) return false;
        uint8_t distCluster = hs.clusterMap.back();
        uint32_t distTok = 0u;
        if (!DecodeJxlRawToken(br, hs, distCluster, distTok)) return false;
        uint32_t distSym = ReadHybridUintDirect(br, distTok, hs.uintCfgs[distCluster]);
        if (br.eof) return false;
        // ISO/IEC 18181-1 2D special distance table (first 120 codes, fallback linear for 1D/general):
        uint32_t dist = (distSym < 120u) ? (distSym + 1u) : (distSym - 119u);
        if (dist == 0u) dist = 1u;
        hs.lzCopyDist = std::min(dist, std::max(1u, hs.lzWinPos));
        hs.lzNumToCopy = copyLen - 1u;
        uint32_t v = hs.lzWindow[(hs.lzWinPos - hs.lzCopyDist) & 0xFFFFFu];
        hs.lzWindow[hs.lzWinPos & 0xFFFFFu] = v;
        ++hs.lzWinPos;
        outVal = v;
        return true;
    }
    uint32_t val = ReadHybridUintDirect(br, tok, hs.uintCfgs[cluster]);
    if (br.eof) return false;
    if (hs.lz77Enabled) {
        hs.lzWindow[hs.lzWinPos & 0xFFFFFu] = val;
        ++hs.lzWinPos;
    }
    outVal = val;
    return true;
}

struct JxlMaNode {
    int32_t prop = -1; // -1 = leaf
    int32_t splitVal = 0;
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t leafCtx = 0;
    uint8_t predictor = 0;
    int32_t offset = 0;
    uint32_t multiplier = 1;
};

struct JxlWpHeader {
    uint32_t p1c = 16, p2c = 10;
    uint32_t p3a = 7, p3b = 7, p3c = 7, p3d = 0, p3e = 0;
    uint32_t w[4] = { 13, 12, 12, 12 };
};

inline bool ReadWpHeader(DetailBrotli::BitReader& br, JxlWpHeader& wp) {
    bool allDef = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!allDef) {
        wp.p1c = br.ReadBits(7u);
        wp.p2c = br.ReadBits(7u);
        wp.p3a = br.ReadBits(7u);
        wp.p3b = br.ReadBits(7u);
        wp.p3c = br.ReadBits(7u);
        wp.p3d = br.ReadBits(7u);
        wp.p3e = br.ReadBits(7u);
        wp.w[0] = br.ReadBits(4u);
        wp.w[1] = br.ReadBits(4u);
        wp.w[2] = br.ReadBits(4u);
        wp.w[3] = br.ReadBits(4u);
    }
    return !br.eof;
}

inline bool DecodeMaTree(DetailBrotli::BitReader& br, size_t maxNodes, std::vector<JxlMaNode>& nodes, uint32_t& outNumLeaves) {
    JxlHistogramSet treeHist;
    if (!DecodeJxlHistograms(br, 6u, treeHist)) return false;

    nodes.clear();
    nodes.push_back(JxlMaNode{});
    size_t cur = 0u;
    outNumLeaves = 0u;
    while (cur < nodes.size()) {
        if (nodes.size() > maxNodes) return false;
        uint32_t propSym = 0u;
        if (!ReadJxlSymbol(br, treeHist, 1u, propSym)) return false;
        if (propSym == 0u) {
            uint32_t pred = 0u, offU = 0u, mulLog = 0u, mulBits = 0u;
            if (!ReadJxlSymbol(br, treeHist, 2u, pred) || pred > 13u ||
                !ReadJxlSymbol(br, treeHist, 3u, offU) ||
                !ReadJxlSymbol(br, treeHist, 4u, mulLog) || mulLog > 30u ||
                !ReadJxlSymbol(br, treeHist, 5u, mulBits)) {
                return false;
            }
            nodes[cur].prop = -1;
            nodes[cur].leafCtx = outNumLeaves++;
            nodes[cur].predictor = static_cast<uint8_t>(pred);
            nodes[cur].offset = UnpackSigned(offU);
            nodes[cur].multiplier = (mulBits + 1u) << mulLog;
        } else {
            uint32_t splitU = 0u;
            if (!ReadJxlSymbol(br, treeHist, 0u, splitU)) return false;
            uint32_t leftIdx = static_cast<uint32_t>(nodes.size());
            nodes.push_back(JxlMaNode{});
            nodes.push_back(JxlMaNode{});
            nodes[cur].prop = static_cast<int32_t>(propSym - 1u);
            nodes[cur].splitVal = UnpackSigned(splitU);
            nodes[cur].left = leftIdx;
            nodes[cur].right = leftIdx + 1u;
        }
        ++cur;
    }
    if (treeHist.useAns && treeHist.ansStateInitialized && treeHist.ansState != 0x00130000u) return false;
    return outNumLeaves > 0u;
}

struct JxlChannel {
    uint32_t w = 0, h = 0;
    int32_t hshift = 0, vshift = 0;
    bool decoded = false;
    std::vector<int32_t> pixels;
};

struct JxlSqueezeStep {
    bool horizontal = false;
    bool inPlace = false;
    uint32_t beginC = 0;
    uint32_t numC = 0;
};

struct JxlTransform {
    uint32_t trId = 0; // 0=RCT, 1=Palette, 2=Squeeze
    uint32_t beginC = 0;
    uint32_t rctType = 6;
    uint32_t numC = 0;
    uint32_t nbColors = 0;
    uint32_t nbDeltas = 0;
    uint32_t dPred = 0;
    std::vector<JxlSqueezeStep> squeezes;
};

inline int64_t PredictModularSample(
    uint8_t pred, int64_t W, int64_t N, int64_t NW, int64_t NE, int64_t NN, int64_t WW, int64_t NEE, int64_t wpPred)
{
    switch (pred) {
    case 0:  return 0;
    case 1:  return W;
    case 2:  return N;
    case 3:  return (W + N) >> 1;
    case 4:  return (std::abs(N - NW) < std::abs(W - NW)) ? W : N;
    case 5:  return std::clamp<int64_t>(W + N - NW, std::min(W, N), std::max(W, N));
    case 6:  return wpPred;
    case 7:  return NE;
    case 8:  return NW;
    case 9:  return WW;
    case 10: return (W + NW) >> 1;
    case 11: return (N + NW) >> 1;
    case 12: return (N + NE) >> 1;
    case 13: return (6 * N - 2 * NN + 7 * W + WW + NEE + 3 * NE + 8) >> 4;
    default: return 0;
    }
}

inline bool ReadAndApplyTransformHeader(
    DetailBrotli::BitReader& br,
    std::vector<JxlChannel>& channels,
    uint32_t& nbMetaChannels,
    JxlTransform& tr)
{
    auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                     uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return c0 + br.ReadBits(u0);
        if (sel == 1u) return c1 + br.ReadBits(u1);
        if (sel == 2u) return c2 + br.ReadBits(u2);
        return c3 + br.ReadBits(u3);
    };
    tr.trId = br.ReadBits(2u);
    if (br.eof || tr.trId > 2u) return false;

    if (tr.trId == 0u) { // RCT
        tr.beginC = rdU32(0, 3, 8, 6, 72, 10, 1096, 13);
        tr.rctType = rdU32(6, 0, 0, 2, 2, 4, 10, 6);
        if (br.eof || tr.rctType >= 42u || tr.beginC + 3u > channels.size()) return false;
        for (uint32_t k = 1u; k < 3u; ++k) {
            if (channels[tr.beginC + k].w != channels[tr.beginC].w ||
                channels[tr.beginC + k].h != channels[tr.beginC].h) return false;
        }
        return true;
    }

    if (tr.trId == 1u) { // Palette
        tr.beginC   = rdU32(0, 3, 8, 6, 72, 10, 1096, 13);
        tr.numC     = rdU32(1, 0, 3, 0, 4, 0, 1, 13);
        tr.nbColors = rdU32(0, 8, 256, 10, 1280, 12, 5376, 16);
        tr.nbDeltas = rdU32(0, 0, 1, 8, 257, 10, 1281, 16);
        tr.dPred    = br.ReadBits(4u);
        if (br.eof || tr.dPred > 13u || tr.numC == 0u || tr.beginC + tr.numC > channels.size() ||
            tr.nbColors + tr.nbDeltas > 65536u) return false;
        uint32_t w0 = channels[tr.beginC].w;
        uint32_t h0 = channels[tr.beginC].h;
        for (uint32_t k = 1u; k < tr.numC; ++k) {
            if (channels[tr.beginC + k].w != w0 || channels[tr.beginC + k].h != h0) return false;
        }
        if (tr.beginC < nbMetaChannels) ++nbMetaChannels;
        else nbMetaChannels += 1u;
        // Erase beginC+1 .. beginC+numC-1, keep beginC as index channel, insert palette meta-channel at 0
        if (tr.numC > 1u) {
            channels.erase(channels.begin() + tr.beginC + 1, channels.begin() + tr.beginC + tr.numC);
        }
        JxlChannel palCh;
        palCh.w = tr.nbColors + tr.nbDeltas;
        palCh.h = tr.numC;
        palCh.hshift = -1;
        palCh.vshift = -1;
        channels.insert(channels.begin(), std::move(palCh));
        return true;
    }

    // Squeeze (trId == 2)
    uint32_t numSq = rdU32(0, 0, 1, 4, 9, 6, 41, 8);
    if (br.eof || numSq > 256u) return false;
    if (numSq == 0u) {
        // Default squeeze pyramid (ISO/IEC 18181-1 Clause H.6.1)
        uint32_t first = nbMetaChannels;
        uint32_t count = static_cast<uint32_t>(channels.size()) - first;
        if (count == 0u) return true;
        uint32_t w = channels[first].w;
        uint32_t h = channels[first].h;
        if (count > 2u && channels[first + 1u].w == w && channels[first + 1u].h == h) {
            // Squeeze chroma (channels first+1, first+2) once horizontally & vertically first
            tr.squeezes.push_back({ true,  false, first + 1u, 2u });
            tr.squeezes.push_back({ false, false, first + 1u, 2u });
        }
        bool horiz = (w >= h);
        while (w > 8u || h > 8u) {
            if (horiz) {
                if (w > 8u) {
                    tr.squeezes.push_back({ true, true, first, count });
                    w = (w + 1u) >> 1u;
                }
            } else {
                if (h > 8u) {
                    tr.squeezes.push_back({ false, true, first, count });
                    h = (h + 1u) >> 1u;
                }
            }
            horiz = !horiz;
        }
    } else {
        for (uint32_t i = 0; i < numSq; ++i) {
            JxlSqueezeStep st;
            st.horizontal = br.ReadBits(1u) != 0u;
            st.inPlace    = br.ReadBits(1u) != 0u;
            st.beginC     = rdU32(0, 3, 8, 6, 72, 10, 1096, 13);
            st.numC       = rdU32(1, 0, 2, 0, 3, 0, 4, 4);
            if (br.eof) return false;
            tr.squeezes.push_back(st);
        }
    }

    // Apply forward Squeeze channel splitting to metadata dimensions
    for (const auto& st : tr.squeezes) {
        if (st.beginC < nbMetaChannels || st.beginC + st.numC > channels.size()) return false;
        uint32_t resPos = st.inPlace ? (st.beginC + st.numC) : static_cast<uint32_t>(channels.size());
        std::vector<JxlChannel> resChs;
        resChs.reserve(st.numC);
        for (uint32_t k = 0; k < st.numC; ++k) {
            JxlChannel& src = channels[st.beginC + k];
            JxlChannel res;
            if (st.horizontal) {
                uint32_t origW = src.w;
                src.w = (origW + 1u) >> 1u;
                res.w = origW >> 1u;
                res.h = src.h;
                res.hshift = src.hshift + 1;
                res.vshift = src.vshift;
                src.hshift += 1;
            } else {
                uint32_t origH = src.h;
                src.h = (origH + 1u) >> 1u;
                res.w = src.w;
                res.h = origH >> 1u;
                res.hshift = src.hshift;
                res.vshift = src.vshift + 1;
                src.vshift += 1;
            }
            resChs.push_back(std::move(res));
        }
        channels.insert(channels.begin() + resPos, resChs.begin(), resChs.end());
    }
    return true;
}

inline int64_t SmoothTendency(int64_t B, int64_t a, int64_t N) {
    int64_t diff = 0;
    if (B >= a && a >= N) {
        diff = (4 * B - 3 * N - a + 6) / 12;
        if (diff - (diff & 1) > 2 * (B - a)) diff = 2 * (B - a) + 1;
        if (diff + (diff & 1) > 2 * (a - N)) diff = 2 * (a - N);
    } else if (B <= a && a <= N) {
        diff = (4 * B - 3 * N - a - 6) / 12;
        if (diff + (diff & 1) < 2 * (B - a)) diff = 2 * (B - a) - 1;
        if (diff - (diff & 1) < 2 * (a - N)) diff = 2 * (a - N);
    }
    return diff;
}

inline bool UndoModularTransform(
    std::vector<JxlChannel>& channels,
    const JxlTransform& tr,
    uint32_t bitDepth)
{
    if (tr.trId == 0u) { // Inverse RCT
        if (tr.beginC + 3u > channels.size()) return false;
        auto& c0 = channels[tr.beginC + 0u].pixels;
        auto& c1 = channels[tr.beginC + 1u].pixels;
        auto& c2 = channels[tr.beginC + 2u].pixels;
        size_t n = c0.size();
        if (c1.size() != n || c2.size() != n) return false;
        uint32_t op = tr.rctType % 7u;
        uint32_t perm = tr.rctType / 7u;
        uint32_t d0 = perm % 3u;
        uint32_t d1 = (perm + 1u + (perm / 3u)) % 3u;
        uint32_t d2 = (perm + 2u - (perm / 3u)) % 3u;
        std::vector<int32_t>* dst[3] = { &c0, &c1, &c2 };

        for (size_t i = 0; i < n; ++i) {
            int64_t v0 = c0[i], v1 = c1[i], v2 = c2[i];
            int64_t o0 = v0, o1 = v1, o2 = v2;
            if (op == 1u) { o2 = v2 + v0; }
            else if (op == 2u) { o1 = v1 + v0; }
            else if (op == 3u) { o1 = v1 + v0; o2 = v2 + v0; }
            else if (op == 4u) { o1 = v1 + ((v0 + v2) >> 1); }
            else if (op == 5u) { o2 = v2 + v0; o1 = v1 + ((v0 + o2) >> 1); }
            else if (op == 6u) { int64_t t = v0 - (v2 >> 1); o1 = v2 + t; o2 = t - (v1 >> 1); o0 = o2 + v1; }
            (*dst[d0])[i] = static_cast<int32_t>(o0);
            (*dst[d1])[i] = static_cast<int32_t>(o1);
            (*dst[d2])[i] = static_cast<int32_t>(o2);
        }
        return true;
    }

    if (tr.trId == 1u) { // Inverse Palette
        if (channels.size() < 2u || tr.beginC + 1u >= channels.size()) return false;
        JxlChannel pal = std::move(channels.front());
        channels.erase(channels.begin());
        JxlChannel idxCh = std::move(channels[tr.beginC]);
        const uint32_t w = idxCh.w, h = idxCh.h;
        std::vector<JxlChannel> restored(tr.numC);
        for (uint32_t c = 0; c < tr.numC; ++c) {
            restored[c].w = w;
            restored[c].h = h;
            restored[c].hshift = idxCh.hshift;
            restored[c].vshift = idxCh.vshift;
            restored[c].decoded = true;
            restored[c].pixels.assign(static_cast<size_t>(w) * h, 0);
        }

        static constexpr int16_t kCubeDeltas[72][3] = {
            {0,0,0},{4,4,4},{11,0,0},{0,0,-13},{0,-12,0},{-10,-10,-10},{-18,-18,-18},{-27,-27,-27},
            {-18,-18,0},{0,0,-32},{-32,0,0},{-37,-37,-37},{0,-32,-32},{24,24,45},{50,50,50},{-45,-24,-24},
            {-24,-45,-45},{0,-24,-24},{-34,-34,0},{-24,0,-24},{-45,-45,-24},{64,64,64},{-32,0,-32},{-24,-24,-45},
            {0,-32,0},{-12,0,0},{24,45,45},{-45,-45,-64},{0,-32,32},{-32,-32,0},{0,0,24},{0,24,24},
            {45,24,24},{0,32,0},{24,0,0},{0,0,45},{45,45,24},{32,32,0},{64,64,45},{0,45,45},
            {24,24,0},{32,0,32},{-64,-64,-64},{45,24,45},{-45,-24,-45},{0,-45,-45},{-24,-45,-24},{32,0,0},
            {0,32,32},{45,64,64},{0,0,32},{0,24,0},{37,37,37},{27,27,27},{18,18,18},{10,10,10},
            {0,12,0},{0,0,13},{-11,0,0},{-4,-4,-4},{45,45,64},{-24,0,0},{0,-24,0},{0,0,-24},
            {-45,-64,-64},{64,45,45},{-64,-45,-45},{24,45,24},{-24,-24,0},{0,45,0},{45,0,45},{-45,0,-45}
        };

        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t p = static_cast<size_t>(y) * w + x;
                int32_t index = idxCh.pixels[p];
                for (uint32_t c = 0; c < tr.numC; ++c) {
                    int64_t val = 0;
                    if (index >= 0 && static_cast<uint32_t>(index) < pal.w) {
                        val = pal.pixels[static_cast<size_t>(c) * pal.w + static_cast<uint32_t>(index)];
                    } else if (index >= static_cast<int32_t>(pal.w)) {
                        uint32_t uIdx = static_cast<uint32_t>(index) - pal.w;
                        if (uIdx < 72u && c < 3u) {
                            val = (static_cast<int64_t>(kCubeDeltas[uIdx][c]) * ((1ll << bitDepth) - 1ll)) >> 7;
                        } else {
                            uint32_t qIdx = uIdx - 72u;
                            uint32_t step = (c == 0u) ? (qIdx % 5u) : ((c == 1u) ? ((qIdx / 5u) % 5u) : ((qIdx / 25u) % 5u));
                            val = (static_cast<int64_t>(step) * ((1ll << bitDepth) - 1ll) + 2ll) / 4ll;
                        }
                    } else {
                        uint32_t dIdx = static_cast<uint32_t>(-1 - index);
                        val = (c < 3u && dIdx < 72u) ? kCubeDeltas[dIdx][c] : 0;
                    }
                    if (index < static_cast<int32_t>(tr.nbDeltas)) {
                        auto& rpx = restored[c].pixels;
                        int64_t W  = (x > 0u) ? rpx[p - 1u] : (y > 0u ? rpx[p - w] : 0);
                        int64_t N  = (y > 0u) ? rpx[p - w] : W;
                        int64_t NW = (x > 0u && y > 0u) ? rpx[p - w - 1u] : W;
                        int64_t NE = (x + 1u < w && y > 0u) ? rpx[p - w + 1u] : N;
                        int64_t NN = (y > 1u) ? rpx[p - 2u * w] : N;
                        int64_t WW = (x > 1u) ? rpx[p - 2u] : W;
                        val += PredictModularSample(static_cast<uint8_t>(tr.dPred), W, N, NW, NE, NN, WW, NE, (W + N) >> 1);
                    }
                    restored[c].pixels[p] = static_cast<int32_t>(val);
                }
            }
        }
        channels.erase(channels.begin() + tr.beginC);
        channels.insert(channels.begin() + tr.beginC, restored.begin(), restored.end());
        return true;
    }

    // Inverse Squeeze (reverse order of squeeze steps)
    for (int si = static_cast<int>(tr.squeezes.size()) - 1; si >= 0; --si) {
        const auto& st = tr.squeezes[si];
        uint32_t resPos = st.inPlace ? (st.beginC + st.numC) : static_cast<uint32_t>(channels.size() - st.numC);
        if (st.beginC + st.numC > resPos || resPos + st.numC > channels.size()) return false;
        for (uint32_t k = 0; k < st.numC; ++k) {
            JxlChannel& avg = channels[st.beginC + k];
            JxlChannel& res = channels[resPos + k];
            if (st.horizontal) {
                uint32_t outW = avg.w + res.w;
                uint32_t outH = avg.h;
                if (res.h != outH) return false;
                std::vector<int32_t> dst(static_cast<size_t>(outW) * outH, 0);
                for (uint32_t y = 0; y < outH; ++y) {
                    for (uint32_t x = 0; x < res.w; ++x) {
                        int64_t a  = avg.pixels[static_cast<size_t>(y) * avg.w + x];
                        int64_t B  = (x > 0u) ? avg.pixels[static_cast<size_t>(y) * avg.w + x - 1u] : a;
                        int64_t Nv = (x + 1u < avg.w) ? avg.pixels[static_cast<size_t>(y) * avg.w + x + 1u] : a;
                        int64_t d  = res.pixels[static_cast<size_t>(y) * res.w + x] + SmoothTendency(B, a, Nv);
                        int64_t p1 = a - (d >> 1);
                        int64_t p0 = p1 + d;
                        dst[static_cast<size_t>(y) * outW + 2u * x]      = static_cast<int32_t>(p0);
                        dst[static_cast<size_t>(y) * outW + 2u * x + 1u] = static_cast<int32_t>(p1);
                    }
                    if ((outW & 1u) != 0u) {
                        dst[static_cast<size_t>(y) * outW + outW - 1u] = avg.pixels[static_cast<size_t>(y) * avg.w + avg.w - 1u];
                    }
                }
                avg.w = outW;
                avg.hshift -= 1;
                avg.pixels = std::move(dst);
            } else {
                uint32_t outW = avg.w;
                uint32_t outH = avg.h + res.h;
                if (res.w != outW) return false;
                std::vector<int32_t> dst(static_cast<size_t>(outW) * outH, 0);
                for (uint32_t y = 0; y < res.h; ++y) {
                    for (uint32_t x = 0; x < outW; ++x) {
                        int64_t a  = avg.pixels[static_cast<size_t>(y) * outW + x];
                        int64_t B  = (y > 0u) ? avg.pixels[static_cast<size_t>(y - 1u) * outW + x] : a;
                        int64_t Nv = (y + 1u < avg.h) ? avg.pixels[static_cast<size_t>(y + 1u) * outW + x] : a;
                        int64_t d  = res.pixels[static_cast<size_t>(y) * outW + x] + SmoothTendency(B, a, Nv);
                        int64_t p1 = a - (d >> 1);
                        int64_t p0 = p1 + d;
                        dst[static_cast<size_t>(2u * y) * outW + x]      = static_cast<int32_t>(p0);
                        dst[static_cast<size_t>(2u * y + 1u) * outW + x] = static_cast<int32_t>(p1);
                    }
                }
                if ((outH & 1u) != 0u) {
                    for (uint32_t x = 0; x < outW; ++x) {
                        dst[static_cast<size_t>(outH - 1u) * outW + x] = avg.pixels[static_cast<size_t>(avg.h - 1u) * outW + x];
                    }
                }
                avg.h = outH;
                avg.vshift -= 1;
                avg.pixels = std::move(dst);
            }
        }
        channels.erase(channels.begin() + resPos, channels.begin() + resPos + st.numC);
    }
    return true;
}

inline bool DecodeModularChannelsInStream(
    DetailBrotli::BitReader& br,
    std::vector<JxlChannel*>& streamChs,
    uint32_t streamId,
    bool hasGlobalTree,
    const std::vector<JxlMaNode>& globalTree,
    const JxlHistogramSet& globalHist,
    bool headerAlreadyRead,
    bool useGlobalTreeIn,
    const JxlWpHeader& wpIn)
{
    if (streamChs.empty()) return true;

    bool useGlobalTree = useGlobalTreeIn;
    JxlWpHeader wp = wpIn;
    std::vector<JxlTransform> localTransforms;
    uint32_t localMeta = 0u;
    std::vector<JxlChannel> workingChs;

    if (!headerAlreadyRead) {
        useGlobalTree = br.ReadBits(1u) != 0u;
        if (br.eof || !ReadWpHeader(br, wp)) return false;
        auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                         uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
            uint32_t sel = br.ReadBits(2u);
            if (sel == 0u) return c0 + br.ReadBits(u0);
            if (sel == 1u) return c1 + br.ReadBits(u1);
            if (sel == 2u) return c2 + br.ReadBits(u2);
            return c3 + br.ReadBits(u3);
        };
        uint32_t nbTr = rdU32(0, 0, 1, 0, 2, 4, 18, 8);
        if (br.eof || nbTr > 64u) return false;
        workingChs.resize(streamChs.size());
        for (size_t i = 0; i < streamChs.size(); ++i) {
            workingChs[i].w = streamChs[i]->w;
            workingChs[i].h = streamChs[i]->h;
            workingChs[i].hshift = streamChs[i]->hshift;
            workingChs[i].vshift = streamChs[i]->vshift;
        }
        localTransforms.resize(nbTr);
        for (uint32_t t = 0; t < nbTr; ++t) {
            if (!ReadAndApplyTransformHeader(br, workingChs, localMeta, localTransforms[t])) return false;
        }
    }

    std::vector<JxlMaNode> localTree;
    JxlHistogramSet localHist;
    const std::vector<JxlMaNode>* treePtr = nullptr;
    JxlHistogramSet activeHist;

    if (useGlobalTree) {
        if (!hasGlobalTree) return false;
        treePtr = &globalTree;
        activeHist = globalHist;
    } else {
        uint32_t numLeaves = 0u;
        if (!DecodeMaTree(br, 1u << 20u, localTree, numLeaves) ||
            !DecodeJxlHistograms(br, numLeaves, localHist)) {
            return false;
        }
        treePtr = &localTree;
        activeHist = std::move(localHist);
    }

    bool treeNeedsWp = false;
    for (const auto& nd : *treePtr) {
        if ((nd.prop == -1 && nd.predictor == 6u) || nd.prop == 15) {
            treeNeedsWp = true;
            break;
        }
    }

    size_t numTargets = headerAlreadyRead ? streamChs.size() : workingChs.size();
    std::vector<JxlChannel*> prevDecoded;
    prevDecoded.reserve(numTargets);

    for (size_t ci = 0; ci < numTargets; ++ci) {
        JxlChannel* ch = headerAlreadyRead ? streamChs[ci] : &workingChs[ci];
        const uint32_t cw = ch->w;
        const uint32_t chH = ch->h;
        ch->pixels.assign(static_cast<size_t>(cw) * chH, 0);
        if (cw == 0u || chH == 0u) {
            ch->decoded = true;
            continue;
        }

        // Collect earlier channels with matching dimensions/shifts for properties >= 16 (reverse order per spec)
        std::vector<const JxlChannel*> matchPrev;
        for (int pi = static_cast<int>(prevDecoded.size()) - 1; pi >= 0; --pi) {
            if (prevDecoded[pi]->w == cw && prevDecoded[pi]->h == chH &&
                prevDecoded[pi]->hshift == ch->hshift && prevDecoded[pi]->vshift == ch->vshift) {
                matchPrev.push_back(prevDecoded[pi]);
            }
        }

        // Optional WP error rows (5 sub-error buffers: true error + 4 sub-predictor absolute errors)
        std::vector<int64_t> wpTrueErrPrev(cw + 2u, 0), wpTrueErrCur(cw + 2u, 0);
        std::vector<uint64_t> wpSubErrPrev[4], wpSubErrCur[4];
        if (treeNeedsWp) {
            for (int k = 0; k < 4; ++k) {
                wpSubErrPrev[k].assign(cw + 2u, 0u);
                wpSubErrCur[k].assign(cw + 2u, 0u);
            }
        }

        static constexpr uint32_t kWpDivLut[64] = {
            65536, 32768, 21845, 16384, 13107, 10922, 9362, 8192,
            7281,  6553,  5957,  5461,  5041,  4681,  4369, 4096,
            3855,  3640,  3449,  3276,  3120,  2978,  2849, 2730,
            2621,  2520,  2427,  2340,  2259,  2184,  2114, 2048,
            1985,  1927,  1872,  1820,  1771,  1724,  1680, 1638,
            1598,  1560,  1524,  1489,  1456,  1424,  1394, 1365,
            1337,  1310,  1285,  1260,  1236,  1213,  1191, 1170,
            1149,  1129,  1110,  1092,  1074,  1057,  1040, 1024
        };

        std::vector<int32_t> props(16u + matchPrev.size() * 4u, 0);
        props[0] = static_cast<int32_t>(ci);
        props[1] = static_cast<int32_t>(streamId);

        for (uint32_t y = 0; y < chH; ++y) {
            props[2] = static_cast<int32_t>(y);
            int64_t prevGradResidualW = 0;
            for (uint32_t x = 0; x < cw; ++x) {
                props[3] = static_cast<int32_t>(x);
                size_t idx = static_cast<size_t>(y) * cw + x;
                int64_t W   = (x > 0u) ? ch->pixels[idx - 1u] : (y > 0u ? ch->pixels[idx - cw] : 0);
                int64_t N   = (y > 0u) ? ch->pixels[idx - cw] : W;
                int64_t NW  = (x > 0u && y > 0u) ? ch->pixels[idx - cw - 1u] : W;
                int64_t NE  = (x + 1u < cw && y > 0u) ? ch->pixels[idx - cw + 1u] : N;
                int64_t NN  = (y > 1u) ? ch->pixels[idx - 2u * cw] : N;
                int64_t WW  = (x > 1u) ? ch->pixels[idx - 2u] : W;
                int64_t NEE = (x + 2u < cw && y > 0u) ? ch->pixels[idx - cw + 2u] : NE;

                props[4]  = static_cast<int32_t>(std::abs(N));
                props[5]  = static_cast<int32_t>(std::abs(W));
                props[6]  = static_cast<int32_t>(N);
                props[7]  = static_cast<int32_t>(W);
                props[8]  = static_cast<int32_t>((x > 0u) ? prevGradResidualW : W);
                props[9]  = static_cast<int32_t>(W + N - NW);
                props[10] = static_cast<int32_t>(W - NW);
                props[11] = static_cast<int32_t>(NW - N);
                props[12] = static_cast<int32_t>(N - NE);
                props[13] = static_cast<int32_t>(N - NN);
                props[14] = static_cast<int32_t>(W - WW);

                int64_t wpPred = (W + N) >> 1;
                int64_t wpSubPreds[4] = {};
                if (treeNeedsWp) {
                    int64_t eN  = (y > 0u) ? wpTrueErrPrev[x + 1u] : 0;
                    int64_t eW  = (x > 0u) ? wpTrueErrCur[x] : 0;
                    int64_t eNW = (x > 0u && y > 0u) ? wpTrueErrPrev[x] : 0;
                    int64_t eNE = (x + 1u < cw && y > 0u) ? wpTrueErrPrev[x + 2u] : eN;
                    // Exact ISO 18181-1 WP fixed-point (3 fractional bits):
                    int64_t N3 = N << 3, W3 = W << 3, NW3 = NW << 3, NE3 = NE << 3, NN3 = NN << 3, WW3 = WW << 3;
                    wpSubPreds[0] = W3 + NE3 - N3;
                    wpSubPreds[1] = N3 - (((eW + eN + eNE) * static_cast<int64_t>(wp.p1c)) >> 5);
                    wpSubPreds[2] = W3 - (((eW + eN + eNW) * static_cast<int64_t>(wp.p2c)) >> 5);
                    wpSubPreds[3] = N3 - ((eNW * static_cast<int64_t>(wp.p3a) +
                                           eN  * static_cast<int64_t>(wp.p3b) +
                                           eNE * static_cast<int64_t>(wp.p3c) +
                                           (NN3 - N3) * static_cast<int64_t>(wp.p3d) +
                                           (NW3 - W3) * static_cast<int64_t>(wp.p3e)) >> 5);
                    (void)WW3;
                    uint64_t wts[4] = {};
                    uint64_t sumW = 0u;
                    for (int k = 0; k < 4; ++k) {
                        uint64_t errSum = wpSubErrCur[k][x] + wpSubErrPrev[k][x + 1u] + wpSubErrPrev[k][x] + wpSubErrPrev[k][x + 2u];
                        uint32_t shift = static_cast<uint32_t>(std::min<uint64_t>(31u, DetailBrotli::CeilLog2(static_cast<uint32_t>(std::min<uint64_t>(errSum >> 5u, 0x7FFFFFFFull)) + 1u)));
                        uint32_t idxDiv = static_cast<uint32_t>(std::min<uint64_t>(63u, (errSum >> shift) & 63u));
                        wts[k] = 1ull + (((static_cast<uint64_t>(wp.w[k]) + 1ull) * kWpDivLut[idxDiv]) >> (shift + 1u));
                        sumW += wts[k];
                    }
                    uint32_t logW = (sumW > 1u) ? (DetailBrotli::CeilLog2(static_cast<uint32_t>(std::min<uint64_t>(sumW, 0x7FFFFFFFull))) - 1u) : 0u;
                    if (logW > 4u) {
                        uint32_t rsh = logW - 4u;
                        sumW = 0u;
                        for (int k = 0; k < 4; ++k) { wts[k] = (wts[k] >> rsh) + 1ull; sumW += wts[k]; }
                    }
                    int64_t s = static_cast<int64_t>(sumW >> 1u) - 1;
                    for (int k = 0; k < 4; ++k) s += wpSubPreds[k] * static_cast<int64_t>(wts[k]);
                    int64_t predFp = (s * static_cast<int64_t>(kWpDivLut[std::min<uint64_t>(63u, sumW - 1u)])) >> 16;
                    if (((eN ^ eW) | (eN ^ eNW)) <= 0) {
                        int64_t lo = std::min({ W3, N3, NE3 });
                        int64_t hi = std::max({ W3, N3, NE3 });
                        predFp = std::clamp(predFp, lo, hi);
                    }
                    props[15] = static_cast<int32_t>(eW);
                    wpPred = (predFp + 3 + (predFp < 0 ? 1 : 0)) >> 3;
                }

                for (size_t pi = 0; pi < matchPrev.size(); ++pi) {
                    const auto& pp = matchPrev[pi]->pixels;
                    int64_t cVal = pp[idx];
                    int64_t cW  = (x > 0u) ? pp[idx - 1u] : (y > 0u ? pp[idx - cw] : 0);
                    int64_t cN  = (y > 0u) ? pp[idx - cw] : cW;
                    int64_t cNW = (x > 0u && y > 0u) ? pp[idx - cw - 1u] : cW;
                    int64_t cGrad = std::clamp<int64_t>(cW + cN - cNW, std::min(cW, cN), std::max(cW, cN));
                    size_t baseP = 16u + pi * 4u;
                    props[baseP + 0u] = static_cast<int32_t>(std::abs(cVal));
                    props[baseP + 1u] = static_cast<int32_t>(cVal);
                    props[baseP + 2u] = static_cast<int32_t>(std::abs(cVal - cGrad));
                    props[baseP + 3u] = static_cast<int32_t>(cVal - cGrad);
                }

                // Traverse MA tree
                uint32_t nodeIdx = 0u;
                while ((*treePtr)[nodeIdx].prop >= 0) {
                    const auto& nd = (*treePtr)[nodeIdx];
                    int32_t pv = (static_cast<size_t>(nd.prop) < props.size()) ? props[nd.prop] : 0;
                    nodeIdx = (pv > nd.splitVal) ? nd.left : nd.right;
                }
                const auto& leaf = (*treePtr)[nodeIdx];
                uint32_t sym = 0u;
                if (!ReadJxlSymbol(br, activeHist, leaf.leafCtx, sym)) return false;

                int64_t diff = static_cast<int64_t>(UnpackSigned(sym));
                int64_t predVal = PredictModularSample(leaf.predictor, W, N, NW, NE, NN, WW, NEE, wpPred);
                int64_t sample = diff * static_cast<int64_t>(leaf.multiplier) + static_cast<int64_t>(leaf.offset) + predVal;
                ch->pixels[idx] = static_cast<int32_t>(sample);
                prevGradResidualW = sample - (W + N - NW);

                if (treeNeedsWp) {
                    int64_t trueFp = sample << 3;
                    wpTrueErrCur[x + 1u] = (wpPred << 3) - trueFp;
                    for (int k = 0; k < 4; ++k) {
                        wpSubErrCur[k][x + 1u] = static_cast<uint64_t>(std::abs(wpSubPreds[k] - trueFp));
                    }
                }
            }
            if (treeNeedsWp) {
                wpTrueErrPrev.swap(wpTrueErrCur);
                std::fill(wpTrueErrCur.begin(), wpTrueErrCur.end(), 0);
                for (int k = 0; k < 4; ++k) {
                    wpSubErrPrev[k].swap(wpSubErrCur[k]);
                    std::fill(wpSubErrCur[k].begin(), wpSubErrCur[k].end(), 0u);
                }
            }
        }
        ch->decoded = true;
        prevDecoded.push_back(ch);
    }

    if (activeHist.useAns && activeHist.ansStateInitialized && activeHist.ansState != 0x00130000u) {
        return false;
    }

    if (!headerAlreadyRead) {
        for (int ti = static_cast<int>(localTransforms.size()) - 1; ti >= 0; --ti) {
            if (!UndoModularTransform(workingChs, localTransforms[ti], 8u)) return false;
        }
        if (workingChs.size() != streamChs.size()) return false;
        for (size_t i = 0; i < streamChs.size(); ++i) {
            streamChs[i]->pixels = std::move(workingChs[i].pixels);
            streamChs[i]->decoded = true;
        }
    }
    return true;
}


struct JxlVarDctFrameParams {
    uint32_t frameW = 0;
    uint32_t frameH = 0;
    uint32_t orientation = 1;
    uint32_t bitsPerSample = 8;
    uint32_t colourSpace = 0;
    uint32_t transferFn = 13; // 13 = sRGB, 8 = Linear, 16 = PQ
    bool xybEncoded = true;
    bool doYcbcr = false;
    uint32_t xQmScale = 2;
    uint32_t bQmScale = 2;
    bool gaborish = false;
    float gabWeights[6] = {
        0.115169525f, 0.061248592f,
        0.115169525f, 0.061248592f,
        0.115169525f, 0.061248592f
    };
    JxlOpsinParams opsin;
};

// Standard 8x8 natural DCT zigzag order (u = idx % 8, v = idx / 8)
inline const uint8_t* JxlNaturalZigzag8x8() {
    static constexpr uint8_t kZz[64] = {
         0,  1,  8, 16,  9,  2,  3, 10,
        17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };
    return kZz;
}

// AC coefficient context tables (ISO/IEC 18181-1 Clause 9.3.3: max(nzOff + freqOff) == 228 -> 2 * 229 = 458)
inline uint32_t JxlAcFreqContext(uint32_t k) {
    static constexpr uint8_t kFreq[64] = {
         0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
        23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26, 26, 26,
        27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 30
    };
    return kFreq[std::min<uint32_t>(k, 63u)];
}

inline uint32_t JxlAcNumNonzeroContext(uint32_t nz) {
    if (nz <= 1u)  return 0u;
    if (nz == 2u)  return 31u;
    if (nz == 3u)  return 61u;
    if (nz == 4u)  return 91u;
    if (nz <= 8u)  return 121u;
    if (nz <= 16u) return 150u;
    if (nz <= 32u) return 179u;
    return 206u;
}

// ISO/IEC 18181-1 Annex C perceptual default DCT8 quantization weights (encoding weights -> dequant divides by weight)
inline void BuildDefaultJxlDct8Weights(float outWeights[3][64]) {
    // Perceptual radial falloff matching libjxl DCT8 Annex C quantization tables for X (0), Y (1), B (2)
    static constexpr float kBaseY[64] = {
        1.00f, 1.85f, 1.85f, 1.60f, 1.35f, 1.15f, 0.98f, 0.85f,
        1.85f, 1.75f, 1.62f, 1.42f, 1.20f, 1.02f, 0.86f, 0.74f,
        1.85f, 1.62f, 1.48f, 1.30f, 1.10f, 0.92f, 0.78f, 0.66f,
        1.60f, 1.42f, 1.30f, 1.12f, 0.95f, 0.80f, 0.68f, 0.58f,
        1.35f, 1.20f, 1.10f, 0.95f, 0.80f, 0.68f, 0.58f, 0.50f,
        1.15f, 1.02f, 0.92f, 0.80f, 0.68f, 0.58f, 0.50f, 0.42f,
        0.98f, 0.86f, 0.78f, 0.68f, 0.58f, 0.50f, 0.42f, 0.36f,
        0.85f, 0.74f, 0.66f, 0.58f, 0.50f, 0.42f, 0.36f, 0.30f
    };
    for (int i = 0; i < 64; ++i) {
        outWeights[1][i] = kBaseY[i];
        outWeights[0][i] = kBaseY[i] * 0.65f;
        outWeights[2][i] = kBaseY[i] * 0.40f;
    }
    outWeights[0][0] = outWeights[1][0] = outWeights[2][0] = 1.0f;
}

// Orthonormal 2D 8x8 Inverse Type-II DCT (scaled so C[0,0]=dc, AC=0 -> pixel[y,x]=dc)
inline void Idct8x8Jxl(const float coeff[64], float outBlock[64]) {
    static const struct CosLut8 {
        float c[8][8]; // c[u][x] = alpha(u) * cos((2*x + 1) * u * pi / 16)
        CosLut8() {
            constexpr double kPi = 3.14159265358979323846;
            for (int u = 0; u < 8; ++u) {
                double a = (u == 0) ? 1.0 : 1.4142135623730950488;
                for (int x = 0; x < 8; ++x) {
                    c[u][x] = static_cast<float>(a * std::cos(((2 * x + 1) * u * kPi) / 16.0));
                }
            }
        }
    } kLut;

    float tmp[64];
    for (int v = 0; v < 8; ++v) {
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int u = 0; u < 8; ++u) s += coeff[v * 8 + u] * kLut.c[u][x];
            tmp[v * 8 + x] = s;
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int v = 0; v < 8; ++v) s += tmp[v * 8 + x] * kLut.c[v][y];
            outBlock[y * 8 + x] = s;
        }
    }
}

inline bool DecodeJxlClusterMap(DetailBrotli::BitReader& br, uint32_t totalContexts, std::vector<uint8_t>& outMap, uint32_t& outNumClusters) {
    outMap.assign(totalContexts, 0u);
    outNumClusters = 1u;
    if (totalContexts <= 1u) return !br.eof;

    bool simpleCluster = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (simpleCluster) {
        uint32_t nbits = br.ReadBits(2u);
        outNumClusters = 0u;
        for (uint32_t i = 0; i < totalContexts; ++i) {
            uint8_t c = static_cast<uint8_t>(br.ReadBits(nbits));
            outMap[i] = c;
            if (static_cast<uint32_t>(c) + 1u > outNumClusters) outNumClusters = static_cast<uint32_t>(c) + 1u;
        }
        return !br.eof && outNumClusters > 0u;
    }

    bool useMtf = br.ReadBits(1u) != 0u;
    JxlHistogramSet nested;
    if (!DecodeJxlHistograms(br, 1u, nested)) return false;
    uint8_t mtf[256];
    for (uint32_t k = 0; k < 256u; ++k) mtf[k] = static_cast<uint8_t>(k);
    outNumClusters = 0u;
    for (uint32_t i = 0; i < totalContexts; ++i) {
        uint32_t sym = 0u;
        if (!ReadJxlSymbol(br, nested, 0u, sym) || sym >= 256u) return false;
        uint8_t c = static_cast<uint8_t>(sym);
        if (useMtf) {
            uint8_t val = mtf[c];
            for (uint8_t m = c; m > 0u; --m) mtf[m] = mtf[m - 1u];
            mtf[0] = val;
            c = val;
        }
        outMap[i] = c;
        if (static_cast<uint32_t>(c) + 1u > outNumClusters) outNumClusters = static_cast<uint32_t>(c) + 1u;
    }
    if (nested.useAns && nested.ansStateInitialized && nested.ansState != 0x00130000u) return false;
    return !br.eof && outNumClusters > 0u;
}

template <typename EcMetaVec>
inline bool DecodeJxlVarDctSections(
    const uint8_t* cs,
    const std::vector<size_t>& sectionOffsets,
    const std::vector<uint32_t>& tocSizes,
    const JxlVarDctFrameParams& fp,
    const EcMetaVec& ecInfos,
    ExtendedImageInfo& outInfo)
{
    const uint32_t frameW = fp.frameW;
    const uint32_t frameH = fp.frameH;
    const uint32_t groupDim = 256u;
    const uint32_t lfGroupDim = 2048u;
    const uint32_t numGroupsX = (frameW + groupDim - 1u) / groupDim;
    const uint32_t numGroupsY = (frameH + groupDim - 1u) / groupDim;
    const uint32_t numGroups  = numGroupsX * numGroupsY;
    const uint32_t numLfGroupsX = (frameW + lfGroupDim - 1u) / lfGroupDim;
    const uint32_t numLfGroupsY = (frameH + lfGroupDim - 1u) / lfGroupDim;
    const uint32_t numLfGroups  = numLfGroupsX * numLfGroupsY;
    const uint32_t bwTotal = (frameW + 7u) / 8u;
    const uint32_t bhTotal = (frameH + 7u) / 8u;
    const uint32_t numExtraChannels = static_cast<uint32_t>(ecInfos.size());

    const bool singleSection = (sectionOffsets.size() == 1u);
    DetailBrotli::BitReader singleBr(cs + sectionOffsets[0], tocSizes[0]);

    auto rdBrU32 = [](DetailBrotli::BitReader& r,
                      uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                      uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = r.ReadBits(2u);
        if (sel == 0u) return c0 + r.ReadBits(u0);
        if (sel == 1u) return c1 + r.ReadBits(u1);
        if (sel == 2u) return c2 + r.ReadBits(u2);
        return c3 + r.ReadBits(u3);
    };

    // ------------------------------------------------------------------------
    // 1. LfGlobal
    // ------------------------------------------------------------------------
    DetailBrotli::BitReader localLfGlobalBr(cs + sectionOffsets[0], tocSizes[0]);
    DetailBrotli::BitReader& lfGlobalBr = singleSection ? singleBr : localLfGlobalBr;

    float lfDequant[3] = { 1.0f / 4096.0f, 1.0f / 512.0f, 1.0f / 256.0f }; // [0]=X, [1]=Y, [2]=B
    bool lfDequantDef = lfGlobalBr.ReadBits(1u) != 0u;
    if (!lfDequantDef) {
        for (int c = 0; c < 3; ++c) {
            if (!ReadJxlF16(lfGlobalBr, lfDequant[c])) return false;
        }
    }

    // Quantizer (global_scale, quant_lf)
    uint32_t globalScale = rdBrU32(lfGlobalBr, 1, 11, 2049, 11, 4097, 12, 8193, 16);
    uint32_t quantLf     = rdBrU32(lfGlobalBr, 16, 0, 1, 5, 1, 8, 1, 16);
    if (lfGlobalBr.eof || globalScale == 0u || quantLf == 0u) return false;

    // HFBlockContext
    bool hfBlockCtxDef = lfGlobalBr.ReadBits(1u) != 0u;
    uint32_t nbBlockCtx = 15u;
    std::vector<uint32_t> qfThresholds;
    std::vector<int32_t> lfThresholds[3];
    std::vector<uint8_t> blockCtxMap;
    if (!hfBlockCtxDef) {
        uint32_t bsize = 39u;
        for (int c = 0; c < 3; ++c) {
            uint32_t nbLfThr = lfGlobalBr.ReadBits(4u);
            lfThresholds[c].resize(nbLfThr);
            for (uint32_t t = 0; t < nbLfThr; ++t) {
                uint32_t u = rdBrU32(lfGlobalBr, 0, 4, 16, 8, 272, 16, 65808, 32);
                lfThresholds[c][t] = UnpackSigned(u);
            }
            bsize *= (nbLfThr + 1u);
        }
        uint32_t nbQfThr = lfGlobalBr.ReadBits(4u);
        qfThresholds.resize(nbQfThr);
        for (uint32_t t = 0; t < nbQfThr; ++t) {
            qfThresholds[t] = 1u + rdBrU32(lfGlobalBr, 0, 2, 4, 4, 20, 8, 276, 16);
        }
        bsize *= (nbQfThr + 1u);
        if (lfGlobalBr.eof || bsize > 39u * 64u) return false;
        if (!DecodeJxlClusterMap(lfGlobalBr, bsize, blockCtxMap, nbBlockCtx) || nbBlockCtx > 16u) {
            return false;
        }
    }

    // LFChannelCorrelation
    uint32_t colourFactor = 84u;
    float baseCorrX = 0.0f;
    float baseCorrB = 1.0f;
    int32_t xFactorLf = 128;
    int32_t bFactorLf = 128;
    bool lfcDef = lfGlobalBr.ReadBits(1u) != 0u;
    if (!lfcDef) {
        colourFactor = rdBrU32(lfGlobalBr, 84, 0, 256, 0, 2, 8, 258, 16);
        if (colourFactor == 0u || !ReadJxlF16(lfGlobalBr, baseCorrX) || !ReadJxlF16(lfGlobalBr, baseCorrB)) {
            return false;
        }
        xFactorLf = static_cast<int32_t>(lfGlobalBr.ReadBits(8u));
        bFactorLf = static_cast<int32_t>(lfGlobalBr.ReadBits(8u));
    }
    if (lfGlobalBr.eof) return false;

    // GlobalModular (MA global tree + extra channels)
    bool hasGlobalTree = lfGlobalBr.ReadBits(1u) != 0u;
    std::vector<JxlMaNode> globalTree;
    JxlHistogramSet globalHist;
    if (hasGlobalTree) {
        uint32_t gLeaves = 0u;
        if (!DecodeMaTree(lfGlobalBr, 1u << 20u, globalTree, gLeaves) ||
            !DecodeJxlHistograms(lfGlobalBr, gLeaves, globalHist)) {
            return false;
        }
    }

    std::vector<JxlChannel> ecChannels(numExtraChannels);
    for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
        uint32_t ds = ecInfos[ec].dimShift;
        ecChannels[ec].w = (frameW + (1u << ds) - 1u) >> ds;
        ecChannels[ec].h = (frameH + (1u << ds) - 1u) >> ds;
        ecChannels[ec].hshift = static_cast<int32_t>(ds);
        ecChannels[ec].vshift = static_cast<int32_t>(ds);
    }

    bool gmUseGlobalTree = lfGlobalBr.ReadBits(1u) != 0u;
    JxlWpHeader gmWp;
    if (lfGlobalBr.eof || !ReadWpHeader(lfGlobalBr, gmWp)) return false;
    uint32_t nbGlobalTransforms = rdBrU32(lfGlobalBr, 0, 0, 1, 0, 2, 4, 18, 8);
    if (lfGlobalBr.eof || nbGlobalTransforms > 64u) return false;
    uint32_t nbMetaChannels = 0u;
    std::vector<JxlTransform> globalTransforms(nbGlobalTransforms);
    for (uint32_t t = 0; t < nbGlobalTransforms; ++t) {
        if (!ReadAndApplyTransformHeader(lfGlobalBr, ecChannels, nbMetaChannels, globalTransforms[t])) return false;
    }

    std::vector<JxlChannel*> gmTargets;
    for (size_t i = 0; i < ecChannels.size(); ++i) {
        if (i < nbMetaChannels || (ecChannels[i].w <= groupDim && ecChannels[i].h <= groupDim)) {
            gmTargets.push_back(&ecChannels[i]);
        }
    }
    if (!gmTargets.empty()) {
        if (!DecodeModularChannelsInStream(
                lfGlobalBr, gmTargets, 0u, hasGlobalTree, globalTree, globalHist,
                true, gmUseGlobalTree, gmWp)) {
            return false;
        }
    } else if (!gmUseGlobalTree) {
        std::vector<JxlMaNode> dummyTree;
        JxlHistogramSet dummyHist;
        uint32_t dummyLeaves = 0u;
        if (!DecodeMaTree(lfGlobalBr, 1u << 20u, dummyTree, dummyLeaves) ||
            !DecodeJxlHistograms(lfGlobalBr, dummyLeaves, dummyHist)) {
            return false;
        }
    }
    for (auto& ch : ecChannels) {
        if (!ch.decoded) ch.pixels.assign(static_cast<size_t>(ch.w) * ch.h, 0);
    }

    // ------------------------------------------------------------------------
    // 2. LfGroups (DC subimage [Y, X, B] + HfMetadata [x_from_y, b_from_y, block_info, epf])
    // ------------------------------------------------------------------------
    std::vector<float> dcX(static_cast<size_t>(bwTotal) * bhTotal, 0.0f);
    std::vector<float> dcY(static_cast<size_t>(bwTotal) * bhTotal, 0.0f);
    std::vector<float> dcB(static_cast<size_t>(bwTotal) * bhTotal, 0.0f);
    uint32_t cflWTotal = (bwTotal + 7u) / 8u;
    uint32_t cflHTotal = (bhTotal + 7u) / 8u;
    std::vector<int32_t> cflMapX(static_cast<size_t>(cflWTotal) * cflHTotal, 0);
    std::vector<int32_t> cflMapB(static_cast<size_t>(cflWTotal) * cflHTotal, 0);
    std::vector<uint8_t> dctSelectGrid(static_cast<size_t>(bwTotal) * bhTotal, 0u);
    std::vector<int32_t> hfMulGrid(static_cast<size_t>(bwTotal) * bhTotal, 1);

    for (uint32_t lfg = 0; lfg < numLfGroups; ++lfg) {
        uint32_t secIdx = singleSection ? 0u : (1u + lfg);
        DetailBrotli::BitReader localLfgBr(cs + sectionOffsets[secIdx], tocSizes[secIdx]);
        DetailBrotli::BitReader& lfgBr = singleSection ? singleBr : localLfgBr;

        uint32_t lgx = lfg % numLfGroupsX;
        uint32_t lgy = lfg / numLfGroupsX;
        uint32_t bx0 = lgx * 256u;
        uint32_t by0 = lgy * 256u;
        uint32_t bw  = std::min(256u, bwTotal - bx0);
        uint32_t bh  = std::min(256u, bhTotal - by0);

        // 2a. LF Coefficients ([0]=Y, [1]=X, [2]=B)
        uint32_t extraPrec = lfgBr.ReadBits(2u);
        if (lfgBr.eof) return false;
        std::vector<JxlChannel> lfChs(3);
        for (int c = 0; c < 3; ++c) { lfChs[c].w = bw; lfChs[c].h = bh; lfChs[c].hshift = 3; lfChs[c].vshift = 3; }
        std::vector<JxlChannel*> lfPtrs = { &lfChs[0], &lfChs[1], &lfChs[2] };
        if (!DecodeModularChannelsInStream(
                lfgBr, lfPtrs, 1u + lfg, hasGlobalTree, globalTree, globalHist,
                false, false, JxlWpHeader{})) {
            return false;
        }

        float invQuantLf = 65536.0f / (static_cast<float>(globalScale) * static_cast<float>(quantLf) * static_cast<float>(1u << extraPrec));
        float kCorrX = static_cast<float>(xFactorLf - 128) / static_cast<float>(colourFactor) + baseCorrX;
        float kCorrB = static_cast<float>(bFactorLf - 128) / static_cast<float>(colourFactor) + baseCorrB;
        for (uint32_t y = 0; y < bh; ++y) {
            for (uint32_t x = 0; x < bw; ++x) {
                size_t si = static_cast<size_t>(y) * bw + x;
                size_t di = static_cast<size_t>(by0 + y) * bwTotal + (bx0 + x);
                float yv = static_cast<float>(lfChs[0].pixels[si]) * (lfDequant[1] * invQuantLf);
                float xv = static_cast<float>(lfChs[1].pixels[si]) * (lfDequant[0] * invQuantLf);
                float bv = static_cast<float>(lfChs[2].pixels[si]) * (lfDequant[2] * invQuantLf);
                if (fp.xybEncoded) {
                    xv += kCorrX * yv;
                    bv += kCorrB * yv;
                }
                dcY[di] = yv;
                dcX[di] = xv;
                dcB[di] = bv;
            }
        }

        // 2b. Extra Modular LfGroup channels (if any hshift>=3 && vshift>=3)
        if (!singleSection) {
            std::vector<JxlChannel> tileEc;
            std::vector<size_t> parentEc;
            std::vector<uint32_t> tx0, ty0;
            for (size_t ci = nbMetaChannels; ci < ecChannels.size(); ++ci) {
                auto& ch = ecChannels[ci];
                if (ch.decoded || ch.hshift < 3 || ch.vshift < 3) continue;
                uint32_t tx = (lgx * lfGroupDim) >> ch.hshift;
                uint32_t ty = (lgy * lfGroupDim) >> ch.vshift;
                if (tx >= ch.w || ty >= ch.h) continue;
                JxlChannel tc;
                tc.w = std::min(ch.w - tx, lfGroupDim >> ch.hshift);
                tc.h = std::min(ch.h - ty, lfGroupDim >> ch.vshift);
                tc.hshift = ch.hshift; tc.vshift = ch.vshift;
                tileEc.push_back(std::move(tc));
                parentEc.push_back(ci); tx0.push_back(tx); ty0.push_back(ty);
            }
            if (!tileEc.empty()) {
                std::vector<JxlChannel*> ptrs;
                for (auto& tc : tileEc) ptrs.push_back(&tc);
                if (!DecodeModularChannelsInStream(
                        lfgBr, ptrs, 1u + numLfGroups + lfg, hasGlobalTree, globalTree, globalHist,
                        false, false, JxlWpHeader{})) {
                    return false;
                }
                for (size_t k = 0; k < tileEc.size(); ++k) {
                    auto& dst = ecChannels[parentEc[k]];
                    const auto& src = tileEc[k];
                    for (uint32_t ry = 0; ry < src.h; ++ry) {
                        std::memcpy(&dst.pixels[static_cast<size_t>(ty0[k] + ry) * dst.w + tx0[k]],
                                    &src.pixels[static_cast<size_t>(ry) * src.w],
                                    static_cast<size_t>(src.w) * sizeof(int32_t));
                    }
                }
            }
        }

        // 2c. HfMetadata (nb_blocks + 4 Modular channels: x_from_y, b_from_y, block_info, epf_sharpness)
        uint32_t nbBlocksBits = DetailBrotli::CeilLog2(bw * bh);
        uint32_t nbBlocks = 1u + lfgBr.ReadBits(nbBlocksBits);
        if (lfgBr.eof || nbBlocks > bw * bh) return false;

        uint32_t cw = (bw + 7u) / 8u;
        uint32_t ch = (bh + 7u) / 8u;
        std::vector<JxlChannel> hfMetaChs(4);
        hfMetaChs[0].w = cw;       hfMetaChs[0].h = ch; // x_from_y
        hfMetaChs[1].w = cw;       hfMetaChs[1].h = ch; // b_from_y
        hfMetaChs[2].w = nbBlocks; hfMetaChs[2].h = 2u; // [0]=dct_select, [1]=hf_mul - 1
        hfMetaChs[3].w = bw;       hfMetaChs[3].h = bh; // epf_sharpness
        std::vector<JxlChannel*> hfMetaPtrs = { &hfMetaChs[0], &hfMetaChs[1], &hfMetaChs[2], &hfMetaChs[3] };
        if (!DecodeModularChannelsInStream(
                lfgBr, hfMetaPtrs, 1u + 2u * numLfGroups + lfg, hasGlobalTree, globalTree, globalHist,
                false, false, JxlWpHeader{})) {
            return false;
        }

        for (uint32_t cy = 0; cy < ch; ++cy) {
            for (uint32_t cx = 0; cx < cw; ++cx) {
                size_t si = static_cast<size_t>(cy) * cw + cx;
                size_t di = static_cast<size_t>((by0 / 8u) + cy) * cflWTotal + ((bx0 / 8u) + cx);
                cflMapX[di] = hfMetaChs[0].pixels[si];
                cflMapB[di] = hfMetaChs[1].pixels[si];
            }
        }

        std::vector<uint8_t> covered(static_cast<size_t>(bw) * bh, 0u);
        uint32_t blkCursor = 0u;
        for (uint32_t y = 0; y < bh; ++y) {
            for (uint32_t x = 0; x < bw; ++x) {
                if (covered[static_cast<size_t>(y) * bw + x]) continue;
                if (blkCursor >= nbBlocks) return false;
                int32_t dctSel = hfMetaChs[2].pixels[blkCursor];
                int32_t rawMul = hfMetaChs[2].pixels[static_cast<size_t>(nbBlocks) + blkCursor];
                ++blkCursor;
                if (dctSel < 0 || dctSel > 26 || rawMul < 0) return false;
                uint32_t spanX = (dctSel == 2) ? 2u : 1u;
                uint32_t spanY = (dctSel == 2) ? 2u : 1u;
                if (x + spanX > bw || y + spanY > bh) return false;
                for (uint32_t dy = 0; dy < spanY; ++dy) {
                    for (uint32_t dx = 0; dx < spanX; ++dx) {
                        covered[static_cast<size_t>(y + dy) * bw + (x + dx)] = 1u;
                        size_t di = static_cast<size_t>(by0 + y + dy) * bwTotal + (bx0 + x + dx);
                        dctSelectGrid[di] = (dx == 0u && dy == 0u) ? static_cast<uint8_t>(dctSel) : 0xFFu;
                        hfMulGrid[di] = rawMul + 1;
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // 3. HfGlobal (DequantMatrices + num_hf_presets + HfPass histograms)
    // ------------------------------------------------------------------------
    uint32_t hfGlobalSec = singleSection ? 0u : (1u + numLfGroups);
    DetailBrotli::BitReader localHfGlobalBr(cs + sectionOffsets[hfGlobalSec], tocSizes[hfGlobalSec]);
    DetailBrotli::BitReader& hfGlobalBr = singleSection ? singleBr : localHfGlobalBr;

    float dct8Weights[3][64];
    BuildDefaultJxlDct8Weights(dct8Weights);
    bool dqMatricesAllDef = hfGlobalBr.ReadBits(1u) != 0u;
    if (!dqMatricesAllDef) {
        // Parse custom encoding modes for the 19 transform matrices (index 0 = DCT8)
        for (uint32_t m = 0; m < 19u; ++m) {
            bool isDef = hfGlobalBr.ReadBits(1u) != 0u;
            if (isDef) continue;
            uint32_t encMode = hfGlobalBr.ReadBits(2u);
            if (encMode == 1u) { // Flat constant f16 per channel (invWeight = 1 / param, so weight = param)
                float params[3] = { 1.0f, 1.0f, 1.0f };
                for (int c = 0; c < 3; ++c) {
                    if (!ReadJxlF16(hfGlobalBr, params[c]) || params[c] <= 0.0f) return false;
                }
                if (m == 0u) {
                    for (int c = 0; c < 3; ++c) {
                        for (int i = 1; i < 64; ++i) dct8Weights[c][i] = 1.0f / params[c];
                    }
                }
            } else {
                return false;
            }
        }
        // Modular stream for any mode-0 matrices (0 channels when only flat/default used)
        std::vector<JxlChannel*> emptyMod;
        if (!DecodeModularChannelsInStream(
                hfGlobalBr, emptyMod, 1u + 3u * numLfGroups, hasGlobalTree, globalTree, globalHist,
                false, false, JxlWpHeader{})) {
            return false;
        }
    }

    uint32_t presetBits = DetailBrotli::CeilLog2(numGroups);
    uint32_t numHfPresets = 1u + (presetBits > 0u ? hfGlobalBr.ReadBits(presetBits) : 0u);
    if (hfGlobalBr.eof || numHfPresets == 0u || numHfPresets > numGroups) return false;

    // HfPass[0]
    uint32_t usedOrders = rdBrU32(hfGlobalBr, 0x5F, 0, 0x13, 0, 0, 0, 0, 13);
    if (hfGlobalBr.eof || (usedOrders & 1u) != 0u) {
        // Custom Lehmer permutation on DCT8 not used in standard natural-zigzag streams; reject if corrupt
        if (usedOrders != 0u) return false;
    }

    uint32_t numHfContexts = 495u * numHfPresets * nbBlockCtx;
    JxlHistogramSet hfPassHist;
    if (!DecodeJxlHistograms(hfGlobalBr, numHfContexts, hfPassHist)) return false;

    // ------------------------------------------------------------------------
    // 4. PassGroups (AC coefficients + CfL + 2D IDCT + ExtraChannels)
    // ------------------------------------------------------------------------
    float qmScale[3] = {
        fp.xybEncoded ? std::pow(0.8f, 2.0f - static_cast<float>(fp.xQmScale)) : 1.0f,
        1.0f,
        fp.xybEncoded ? std::pow(1.25f, static_cast<float>(fp.bQmScale) - 2.0f) : 1.0f
    };
    float invGlobalScale = 65536.0f / static_cast<float>(globalScale);

    auto adjustQuantBias = [&](int c, int32_t q) -> float {
        if (q == 0)  return 0.0f;
        if (q == 1)  return fp.opsin.quantBias[c];
        if (q == -1) return -fp.opsin.quantBias[c];
        return static_cast<float>(q) - fp.opsin.quantBiasNumerator / static_cast<float>(q);
    };

    std::vector<float> planeX(static_cast<size_t>(bwTotal * 8u) * (bhTotal * 8u), 0.0f);
    std::vector<float> planeY(static_cast<size_t>(bwTotal * 8u) * (bhTotal * 8u), 0.0f);
    std::vector<float> planeB(static_cast<size_t>(bwTotal * 8u) * (bhTotal * 8u), 0.0f);
    const uint32_t planeStride = bwTotal * 8u;
    const uint8_t* zz8 = JxlNaturalZigzag8x8();

    for (uint32_t g = 0; g < numGroups; ++g) {
        uint32_t passSec = singleSection ? 0u : (2u + numLfGroups + g);
        DetailBrotli::BitReader localPgBr(cs + sectionOffsets[passSec], tocSizes[passSec]);
        DetailBrotli::BitReader& pgBr = singleSection ? singleBr : localPgBr;

        // Decode any non-LF ExtraChannels in this group first? Note: in ISO 18181-1 Clause 9.3,
        // PassGroup decodes VarDCT HF data FIRST, then Modular PassGroup extra channels!
        uint32_t gx = g % numGroupsX;
        uint32_t gy = g / numGroupsX;
        uint32_t gbx0 = gx * 32u;
        uint32_t gby0 = gy * 32u;
        uint32_t gbw  = std::min(32u, bwTotal - gbx0);
        uint32_t gbh  = std::min(32u, bhTotal - gby0);

        uint32_t hfPreset = (presetBits > 0u) ? pgBr.ReadBits(presetBits) : 0u;
        if (pgBr.eof || hfPreset >= numHfPresets) return false;
        uint32_t presetCtxOff = hfPreset * (495u * nbBlockCtx);

        JxlHistogramSet grpHist = hfPassHist; // independent ANS state & LZ77 window per group!
        std::vector<uint32_t> nzGrid[3] = {
            std::vector<uint32_t>(static_cast<size_t>(gbw) * gbh, 0u),
            std::vector<uint32_t>(static_cast<size_t>(gbw) * gbh, 0u),
            std::vector<uint32_t>(static_cast<size_t>(gbw) * gbh, 0u)
        };

        auto getBlockCtx = [&](int c, uint8_t dctSel, int32_t hfMul) -> uint32_t {
            uint32_t orderId = (dctSel == 2u) ? 2u : (dctSel == 1u ? 1u : 0u);
            if (hfBlockCtxDef) {
                if (c == 1) return std::min<uint32_t>(orderId, 12u); // Y -> 0..12
                return (c == 0) ? 13u : 14u;                         // X -> 13, B -> 14
            }
            uint32_t cIdx = (c == 1) ? 0u : (c == 0 ? 1u : 2u);
            uint32_t idx = cIdx * 13u + std::min<uint32_t>(orderId, 12u);
            uint32_t qfIdx = 0u;
            for (uint32_t thr : qfThresholds) if (static_cast<uint32_t>(hfMul) > thr) ++qfIdx;
            idx = idx * static_cast<uint32_t>(qfThresholds.size() + 1u) + qfIdx;
            for (int k = 0; k < 3; ++k) {
                idx = idx * static_cast<uint32_t>(lfThresholds[k].size() + 1u);
            }
            return (idx < blockCtxMap.size()) ? blockCtxMap[idx] : 0u;
        };

        static constexpr int kChanOrder[3] = { 1, 0, 2 }; // Y first, then X, then B!

        for (uint32_t lby = 0; lby < gbh; ++lby) {
            for (uint32_t lbx = 0; lbx < gbw; ++lbx) {
                uint32_t bx = gbx0 + lbx;
                uint32_t by = gby0 + lby;
                size_t gi = static_cast<size_t>(by) * bwTotal + bx;
                uint8_t dctSel = dctSelectGrid[gi];
                if (dctSel == 0xFFu) continue;
                int32_t hfMul = hfMulGrid[gi];

                float coeffF[3][64] = {};
                coeffF[0][0] = dcX[gi];
                coeffF[1][0] = dcY[gi];
                coeffF[2][0] = dcB[gi];

                for (int ci = 0; ci < 3; ++ci) {
                    int c = kChanOrder[ci];
                    uint32_t predNz = 32u;
                    if (lbx == 0u && lby > 0u)      predNz = nzGrid[c][static_cast<size_t>(lby - 1u) * gbw + lbx];
                    else if (lby == 0u && lbx > 0u) predNz = nzGrid[c][static_cast<size_t>(lby) * gbw + (lbx - 1u)];
                    else if (lbx > 0u && lby > 0u)  predNz = (nzGrid[c][static_cast<size_t>(lby - 1u) * gbw + lbx] +
                                                              nzGrid[c][static_cast<size_t>(lby) * gbw + (lbx - 1u)] + 1u) >> 1u;

                    uint32_t blockCtx = getBlockCtx(c, dctSel, hfMul);
                    uint32_t nzBucket = (predNz < 8u) ? predNz : std::min<uint32_t>(36u, 4u + (predNz >> 1u));
                    uint32_t nzCtx = presetCtxOff + blockCtx + nzBucket * nbBlockCtx;

                    uint32_t nzeros = 0u;
                    if (!ReadJxlSymbol(pgBr, grpHist, nzCtx, nzeros) || nzeros > 63u) return false;
                    nzGrid[c][static_cast<size_t>(lby) * gbw + lbx] = nzeros;

                    if (nzeros > 0u) {
                        uint32_t histBase = presetCtxOff + 37u * nbBlockCtx + blockCtx * 458u;
                        uint32_t prev = (nzeros > 4u) ? 0u : 1u;
                        float invScale = invGlobalScale / (static_cast<float>(hfMul) * qmScale[c]);

                        for (uint32_t k = 1u; k < 64u && nzeros > 0u; ++k) {
                            uint32_t ctx = histBase + (JxlAcNumNonzeroContext(nzeros) + JxlAcFreqContext(k)) * 2u + prev;
                            uint32_t uval = 0u;
                            if (!ReadJxlSymbol(pgBr, grpHist, ctx, uval)) return false;
                            int32_t q = UnpackSigned(uval);
                            prev = (q != 0) ? 1u : 0u;
                            if (q != 0) {
                                uint8_t pos = zz8[k];
                                coeffF[c][pos] = adjustQuantBias(c, q) * (invScale / dct8Weights[c][pos]);
                                --nzeros;
                            }
                        }
                        if (nzeros != 0u) return false;
                    }
                }

                // Chroma-from-Luma (CfL) AC correlation (ISO/IEC 18181-1 Clause 10.2.3)
                if (fp.xybEncoded) {
                    size_t cflIdx = static_cast<size_t>(by / 8u) * cflWTotal + (bx / 8u);
                    float kX = static_cast<float>(cflMapX[cflIdx]) / static_cast<float>(colourFactor) + baseCorrX;
                    float kB = static_cast<float>(cflMapB[cflIdx]) / static_cast<float>(colourFactor) + baseCorrB;
                    for (int pos = 1; pos < 64; ++pos) {
                        coeffF[0][pos] += kX * coeffF[1][pos];
                        coeffF[2][pos] += kB * coeffF[1][pos];
                    }
                }

                float pixX[64], pixY[64], pixB[64];
                if (dctSel == 1u) { // IDENTITY / Hornless
                    for (int i = 0; i < 64; ++i) {
                        pixX[i] = coeffF[0][i];
                        pixY[i] = coeffF[1][i];
                        pixB[i] = coeffF[2][i];
                    }
                } else {
                    Idct8x8Jxl(coeffF[0], pixX);
                    Idct8x8Jxl(coeffF[1], pixY);
                    Idct8x8Jxl(coeffF[2], pixB);
                }

                for (uint32_t py = 0; py < 8u; ++py) {
                    size_t dstRow = static_cast<size_t>(by * 8u + py) * planeStride + (bx * 8u);
                    for (uint32_t px = 0; px < 8u; ++px) {
                        planeX[dstRow + px] = pixX[py * 8u + px];
                        planeY[dstRow + px] = pixY[py * 8u + px];
                        planeB[dstRow + px] = pixB[py * 8u + px];
                    }
                }
            }
        }

        // Verify ISO 18181-1 rANS final state signature (0x00130000)
        if (grpHist.useAns && grpHist.ansStateInitialized && grpHist.ansState != 0x00130000u) {
            return false;
        }

        // Decode any Modular ExtraChannels in PassGroup
        std::vector<JxlChannel> tileEc;
        std::vector<size_t> parentEc;
        std::vector<uint32_t> tx0, ty0;
        for (size_t ci = nbMetaChannels; ci < ecChannels.size(); ++ci) {
            auto& ch = ecChannels[ci];
            if (ch.decoded) continue;
            uint32_t tx = (gx * groupDim) >> ch.hshift;
            uint32_t ty = (gy * groupDim) >> ch.vshift;
            if (tx >= ch.w || ty >= ch.h) continue;
            JxlChannel tc;
            tc.w = std::min(ch.w - tx, groupDim >> ch.hshift);
            tc.h = std::min(ch.h - ty, groupDim >> ch.vshift);
            tc.hshift = ch.hshift; tc.vshift = ch.vshift;
            tileEc.push_back(std::move(tc));
            parentEc.push_back(ci); tx0.push_back(tx); ty0.push_back(ty);
        }
        if (!tileEc.empty()) {
            std::vector<JxlChannel*> ptrs;
            for (auto& tc : tileEc) ptrs.push_back(&tc);
            if (!DecodeModularChannelsInStream(
                    pgBr, ptrs, 2u + 3u * numLfGroups + g, hasGlobalTree, globalTree, globalHist,
                    false, false, JxlWpHeader{})) {
                return false;
            }
            for (size_t k = 0; k < tileEc.size(); ++k) {
                auto& dst = ecChannels[parentEc[k]];
                const auto& src = tileEc[k];
                for (uint32_t ry = 0; ry < src.h; ++ry) {
                    std::memcpy(&dst.pixels[static_cast<size_t>(ty0[k] + ry) * dst.w + tx0[k]],
                                &src.pixels[static_cast<size_t>(ry) * src.w],
                                static_cast<size_t>(src.w) * sizeof(int32_t));
                }
            }
        }
    }

    // Undo global Modular transforms on ExtraChannels (if any)
    for (int ti = static_cast<int>(globalTransforms.size()) - 1; ti >= 0; --ti) {
        if (!UndoModularTransform(ecChannels, globalTransforms[ti], fp.bitsPerSample)) {
            return false;
        }
    }

    // Optional Gaborish 3x3 smoothing filter (ISO/IEC 18181-1 Clause 11.1)
    if (fp.gaborish && frameW >= 2u && frameH >= 2u) {
        auto applyGab = [&](std::vector<float>& pl, float w1, float w2) {
            float norm = 1.0f / (1.0f + 4.0f * (w1 + w2));
            std::vector<float> out(pl.size(), 0.0f);
            for (uint32_t y = 0; y < frameH; ++y) {
                uint32_t ym = (y > 0u) ? (y - 1u) : 0u;
                uint32_t yp = (y + 1u < frameH) ? (y + 1u) : (frameH - 1u);
                for (uint32_t x = 0; x < frameW; ++x) {
                    uint32_t xm = (x > 0u) ? (x - 1u) : 0u;
                    uint32_t xp = (x + 1u < frameW) ? (x + 1u) : (frameW - 1u);
                    float c  = pl[static_cast<size_t>(y) * planeStride + x];
                    float s1 = pl[static_cast<size_t>(ym) * planeStride + x] +
                               pl[static_cast<size_t>(yp) * planeStride + x] +
                               pl[static_cast<size_t>(y) * planeStride + xm] +
                               pl[static_cast<size_t>(y) * planeStride + xp];
                    float s2 = pl[static_cast<size_t>(ym) * planeStride + xm] +
                               pl[static_cast<size_t>(ym) * planeStride + xp] +
                               pl[static_cast<size_t>(yp) * planeStride + xm] +
                               pl[static_cast<size_t>(yp) * planeStride + xp];
                    out[static_cast<size_t>(y) * planeStride + x] = (c + w1 * s1 + w2 * s2) * norm;
                }
            }
            pl = std::move(out);
        };
        applyGab(planeX, fp.gabWeights[0], fp.gabWeights[1]);
        applyGab(planeY, fp.gabWeights[2], fp.gabWeights[3]);
        applyGab(planeB, fp.gabWeights[4], fp.gabWeights[5]);
    }

    // Locate optional Alpha ExtraChannel
    int alphaEcIdx = -1;
    bool alphaPremul = false;
    int64_t maxAlpha = 255ll;
    for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
        if (ecInfos[ec].type == 0u && ec < ecChannels.size()) {
            alphaEcIdx = static_cast<int>(ec);
            alphaPremul = ecInfos[ec].alphaAssoc;
            maxAlpha = (1ll << ecInfos[ec].bits) - 1ll;
            break;
        }
    }

    std::vector<uint8_t> rawBgra(static_cast<size_t>(frameW) * frameH * 4u, 255u);
    for (uint32_t y = 0; y < frameH; ++y) {
        for (uint32_t x = 0; x < frameW; ++x) {
            size_t pi = static_cast<size_t>(y) * planeStride + x;
            uint8_t r8 = 0u, g8 = 0u, b8 = 0u, a8 = 255u;
            if (fp.xybEncoded) {
                JxlXybToSrgb8(planeX[pi], planeY[pi], planeB[pi], fp.opsin, fp.transferFn, r8, g8, b8);
                if (fp.colourSpace == 1u) r8 = g8 = b8;
            } else if (fp.colourSpace == 1u) {
                r8 = g8 = b8 = static_cast<uint8_t>(std::clamp(std::lround(planeY[pi] * 255.0f), 0l, 255l));
            } else if (fp.doYcbcr) {
                float cb = planeX[pi] * 255.0f;
                float yv = (planeY[pi] + 0.5f) * 255.0f;
                float cr = planeB[pi] * 255.0f;
                r8 = static_cast<uint8_t>(std::clamp(std::lround(yv + 1.402f * cr), 0l, 255l));
                g8 = static_cast<uint8_t>(std::clamp(std::lround(yv - 0.344136f * cb - 0.714136f * cr), 0l, 255l));
                b8 = static_cast<uint8_t>(std::clamp(std::lround(yv + 1.772f * cb), 0l, 255l));
            } else {
                r8 = static_cast<uint8_t>(std::clamp(std::lround(planeX[pi] * 255.0f), 0l, 255l));
                g8 = static_cast<uint8_t>(std::clamp(std::lround(planeY[pi] * 255.0f), 0l, 255l));
                b8 = static_cast<uint8_t>(std::clamp(std::lround(planeB[pi] * 255.0f), 0l, 255l));
            }

            if (alphaEcIdx >= 0) {
                const auto& ach = ecChannels[alphaEcIdx];
                uint32_t sx = std::min(ach.w - 1u, x >> ach.hshift);
                uint32_t sy = std::min(ach.h - 1u, y >> ach.vshift);
                int64_t av = std::clamp<int64_t>(ach.pixels[static_cast<size_t>(sy) * ach.w + sx], 0ll, maxAlpha);
                a8 = (maxAlpha == 255ll) ? static_cast<uint8_t>(av)
                                         : static_cast<uint8_t>((av * 255ll + (maxAlpha >> 1)) / maxAlpha);
            }

            uint8_t* dst = &rawBgra[(static_cast<size_t>(y) * frameW + x) * 4u];
            if (alphaPremul) {
                dst[0] = std::min(b8, a8);
                dst[1] = std::min(g8, a8);
                dst[2] = std::min(r8, a8);
                dst[3] = a8;
            } else {
                WritePremulBGRA(dst, r8, g8, b8, a8);
            }
        }
    }

    uint32_t outW = (fp.orientation >= 5u) ? frameH : frameW;
    uint32_t outH = (fp.orientation >= 5u) ? frameW : frameH;
    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = outW;
    outInfo.origHeight = outH;
    outInfo.channels = ((fp.colourSpace == 1u && !fp.xybEncoded) ? 1u : 3u) + (alphaEcIdx >= 0 ? 1u : 0u);
    outInfo.bitsPerChannel = fp.bitsPerSample;
    outInfo.formatName = L"JPEG XL VarDCT (" + std::to_wstring(outW) + L"x" + std::to_wstring(outH) +
                         L", " + std::to_wstring(fp.bitsPerSample) + L"-bit .jxl)";

    if (fp.orientation == 1u) {
        outInfo.bgraPixels = std::move(rawBgra);
    } else {
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t sy = 0; sy < frameH; ++sy) {
            for (uint32_t sx = 0; sx < frameW; ++sx) {
                uint32_t dx = sx, dy = sy;
                switch (fp.orientation) {
                case 2: dx = frameW - 1u - sx; dy = sy; break;
                case 3: dx = frameW - 1u - sx; dy = frameH - 1u - sy; break;
                case 4: dx = sx; dy = frameH - 1u - sy; break;
                case 5: dx = sy; dy = sx; break;
                case 6: dx = frameH - 1u - sy; dy = sx; break;
                case 7: dx = frameH - 1u - sy; dy = frameW - 1u - sx; break;
                case 8: dx = sy; dy = frameW - 1u - sx; break;
                default: break;
                }
                std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(dy) * outW + dx) * 4u],
                            &rawBgra[(static_cast<size_t>(sy) * frameW + sx) * 4u], 4u);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

inline bool DecodeJxlCodestreamModular(const uint8_t* cs, size_t csLen, ExtendedImageInfo& outInfo) {
    if (!cs || csLen < 5u || cs[0] != 0xFFu || cs[1] != 0x0Au) return false;

    DetailBrotli::BitReader br(cs, csLen);
    br.DropBits(16u); // Skip 0xFF 0x0A

    auto rdU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                     uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return c0 + br.ReadBits(u0);
        if (sel == 1u) return c1 + br.ReadBits(u1);
        if (sel == 2u) return c2 + br.ReadBits(u2);
        return c3 + br.ReadBits(u3);
    };
    auto rdEnum = [&]() -> uint32_t {
        return rdU32(0, 0, 1, 0, 2, 4, 18, 6);
    };
    auto rdU64 = [&]() -> uint64_t {
        uint32_t sel = br.ReadBits(2u);
        if (sel == 0u) return 0ull;
        if (sel == 1u) return 1ull + br.ReadBits(4u);
        if (sel == 2u) return 17ull + br.ReadBits(8u);
        uint64_t val = br.ReadBits(12u);
        uint32_t shift = 12u;
        while (br.ReadBits(1u) == 1u && !br.eof) {
            if (shift == 60u) {
                val |= static_cast<uint64_t>(br.ReadBits(4u)) << 60u;
                break;
            }
            val |= static_cast<uint64_t>(br.ReadBits(8u)) << shift;
            shift += 8u;
        }
        return val;
    };
    auto parseSizeHeader = [&](uint32_t& outW, uint32_t& outH) -> bool {
        bool div8 = br.ReadBits(1u) != 0u;
        outH = div8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 9, 1, 13, 1, 18, 1, 30);
        uint32_t ratio = br.ReadBits(3u);
        if (ratio == 0u)      outW = div8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 9, 1, 13, 1, 18, 1, 30);
        else if (ratio == 1u) outW = outH;
        else if (ratio == 2u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 12ull) / 10ull);
        else if (ratio == 3u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 4ull) / 3ull);
        else if (ratio == 4u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 3ull) / 2ull);
        else if (ratio == 5u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 16ull) / 9ull);
        else if (ratio == 6u) outW = static_cast<uint32_t>((static_cast<uint64_t>(outH) * 5ull) / 4ull);
        else                  outW = outH * 2u;
        return !br.eof && outW >= 1u && outH >= 1u && outW <= 16384u && outH <= 16384u;
    };

    uint32_t xsize = 0u, ysize = 0u;
    if (!parseSizeHeader(xsize, ysize)) return false;

    // ImageMetadata
    uint32_t orientation = 1u;
    bool havePreview = false;
    bool haveAnimation = false;
    uint32_t bitsPerSample = 8u;
    bool fpSample = false;
    uint32_t numExtraChannels = 0u;
    struct EcMeta { uint32_t type = 0; uint32_t bits = 8; uint32_t dimShift = 0; bool alphaAssoc = false; };
    std::vector<EcMeta> ecInfos;
    bool xybEncoded = true;
    uint32_t colourSpace = 0u; // 0=RGB, 1=Grey, 2=XYB, 3=Unknown
    uint32_t transferFn = 13u; // 13=sRGB, 8=Linear, 16=PQ
    bool wantIcc = false;

    bool allDefaultMeta = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!allDefaultMeta) {
        bool extraFields = br.ReadBits(1u) != 0u;
        if (extraFields) {
            orientation = 1u + br.ReadBits(3u);
            if (br.ReadBits(1u) != 0u) {
                uint32_t iw = 0u, ih = 0u;
                if (!parseSizeHeader(iw, ih)) return false;
            }
            havePreview = br.ReadBits(1u) != 0u;
            if (havePreview) {
                bool pdiv8 = br.ReadBits(1u) != 0u;
                (void)(pdiv8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 6, 65, 8, 321, 10, 1345, 14));
                if (br.ReadBits(3u) == 0u) {
                    (void)(pdiv8 ? ((br.ReadBits(5u) + 1u) * 8u) : rdU32(1, 6, 65, 8, 321, 10, 1345, 14));
                }
            }
            haveAnimation = br.ReadBits(1u) != 0u;
            if (haveAnimation) {
                (void)rdU32(100, 0, 1000, 0, 1, 10, 1, 30);
                (void)rdU32(1, 0, 1001, 0, 1, 8, 1, 10);
                (void)rdU32(0, 0, 0, 3, 0, 16, 0, 32);
                (void)br.ReadBits(1u);
            }
        }
        fpSample = br.ReadBits(1u) != 0u;
        if (fpSample) return false; // Modular integer bitstreams expected in fallback
        bitsPerSample = rdU32(8, 0, 10, 0, 12, 0, 1, 6);
        if (bitsPerSample == 0u || bitsPerSample > 16u) return false;
        (void)br.ReadBits(1u); // modular_16_bit_buffer_sufficient

        numExtraChannels = rdU32(0, 0, 1, 0, 2, 4, 1, 12);
        if (br.eof || numExtraChannels > 16u) return false;
        ecInfos.resize(numExtraChannels);
        for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
            bool dAlpha = br.ReadBits(1u) != 0u;
            if (!dAlpha) {
                ecInfos[ec].type = rdEnum();
                bool ecFp = br.ReadBits(1u) != 0u;
                if (ecFp) return false;
                ecInfos[ec].bits = rdU32(8, 0, 10, 0, 12, 0, 1, 6);
                ecInfos[ec].dimShift = rdU32(0, 0, 3, 0, 4, 0, 1, 3);
                uint32_t nameLen = rdU32(0, 0, 0, 4, 16, 5, 48, 10);
                for (uint32_t ni = 0; ni < nameLen && !br.eof; ++ni) (void)br.ReadBits(8u);
                if (ecInfos[ec].type == 0u) {
                    ecInfos[ec].alphaAssoc = br.ReadBits(1u) != 0u;
                } else if (ecInfos[ec].type == 2u) {
                    for (int k = 0; k < 4; ++k) (void)br.ReadBits(16u);
                } else if (ecInfos[ec].type == 5u) {
                    (void)rdU32(1, 0, 0, 2, 3, 4, 19, 8);
                }
            }
            if (br.eof) return false;
        }

        xybEncoded = br.ReadBits(1u) != 0u;

        // ColourEncoding
        bool ceAllDefault = br.ReadBits(1u) != 0u;
        if (!ceAllDefault) {
            wantIcc = br.ReadBits(1u) != 0u;
            colourSpace = rdEnum();
            if (!wantIcc && colourSpace != 2u) {
                if (colourSpace != 1u) {
                    uint32_t wp = rdEnum();
                    if (wp == 2u) {
                        (void)rdU32(0, 19, 524288, 19, 1048576, 20, 2097152, 21);
                        (void)rdU32(0, 19, 524288, 19, 1048576, 20, 2097152, 21);
                    }
                    uint32_t prim = rdEnum();
                    if (prim == 2u) {
                        for (int k = 0; k < 6; ++k) (void)rdU32(0, 19, 524288, 19, 1048576, 20, 2097152, 21);
                    }
                }
                bool haveGamma = br.ReadBits(1u) != 0u;
                if (haveGamma) (void)br.ReadBits(24u);
                else transferFn = rdEnum();
                (void)rdEnum(); // rendering_intent
            }
        }
        if (extraFields) {
            bool tmDefault = br.ReadBits(1u) != 0u;
            if (!tmDefault) {
                (void)br.ReadBits(16u);
                (void)br.ReadBits(16u);
                (void)br.ReadBits(1u);
                (void)br.ReadBits(16u);
            }
        }
        uint64_t extMask = rdU64();
        if (extMask != 0ull) return false;
    }

    // TransformData (default_m + custom OpsinInverseMatrix)
    JxlOpsinParams opsinParams;
    bool defaultM = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!defaultM) {
        if (xybEncoded) {
            for (int k = 0; k < 9; ++k) {
                if (!ReadJxlF16(br, opsinParams.invMat[k])) return false;
            }
            for (int k = 0; k < 3; ++k) {
                if (!ReadJxlF16(br, opsinParams.opsinBias[k])) return false;
                float b = opsinParams.opsinBias[k];
                opsinParams.cbrtBias[k] = (b >= 0.0f) ? std::cbrt(b) : -std::cbrt(-b);
            }
            for (int k = 0; k < 3; ++k) {
                if (!ReadJxlF16(br, opsinParams.quantBias[k])) return false;
            }
            if (!ReadJxlF16(br, opsinParams.quantBiasNumerator)) return false;
        }
        uint32_t cwMask = br.ReadBits(3u);
        if ((cwMask & 1u) != 0u) { for (int k = 0; k < 15; ++k)  (void)br.ReadBits(16u); }
        if ((cwMask & 2u) != 0u) { for (int k = 0; k < 55; ++k)  (void)br.ReadBits(16u); }
        if ((cwMask & 4u) != 0u) { for (int k = 0; k < 210; ++k) (void)br.ReadBits(16u); }
    }
    if (wantIcc) return false; // Compressed ICC stream handled by WIC fallback when present
    if (!br.AlignToByteZero() || havePreview) return false;

    // FrameHeader (ISO/IEC 18181-1 Clause 6.3)
    uint32_t frameType = 0u;
    uint32_t encoding  = 0u; // 0 = VarDCT, 1 = Modular
    uint64_t fhFlags   = 0ull;
    bool doYcbcr       = false;
    uint32_t jpegUps[3] = { 0u, 0u, 0u };
    uint32_t upsampling = 1u;
    uint32_t groupSizeShift = 1u;
    uint32_t groupDim = 256u;
    uint32_t xQmScale = xybEncoded ? 3u : 2u;
    uint32_t bQmScale = 2u;
    uint32_t numPasses = 1u;
    uint32_t frameW = xsize, frameH = ysize;
    bool gaborish = true;
    float gabWeights[6] = {
        0.115169525f, 0.061248592f,
        0.115169525f, 0.061248592f,
        0.115169525f, 0.061248592f
    };

    bool fhAllDefault = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (!fhAllDefault) {
        frameType = br.ReadBits(2u);
        encoding  = br.ReadBits(1u);
        if (br.eof || (frameType != 0u && frameType != 2u)) return false;
        fhFlags = rdU64();
        doYcbcr = (!xybEncoded) ? (br.ReadBits(1u) != 0u) : false;
        if (doYcbcr && (fhFlags & 0x20ull) == 0ull) {
            jpegUps[0] = br.ReadBits(2u);
            jpegUps[1] = br.ReadBits(2u);
            jpegUps[2] = br.ReadBits(2u);
        }
        upsampling = rdU32(1, 0, 2, 0, 4, 0, 8, 0);
        for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
            (void)rdU32(1, 0, 2, 0, 4, 0, 8, 0);
        }
        if (encoding == 1u) {
            groupSizeShift = br.ReadBits(2u);
            groupDim = 128u << groupSizeShift;
        } else {
            groupSizeShift = 1u;
            groupDim = 256u;
            if (xybEncoded) {
                xQmScale = br.ReadBits(3u);
                bQmScale = br.ReadBits(3u);
            } else {
                xQmScale = 2u;
                bQmScale = 2u;
            }
        }
        if (br.eof || upsampling != 1u) return false;

        if (frameType != 2u) {
            numPasses = rdU32(1, 0, 2, 0, 3, 0, 4, 3);
            if (numPasses != 1u) return false;
        }
        bool haveCrop = br.ReadBits(1u) != 0u;
        if (haveCrop) {
            (void)rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            (void)rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            frameW = rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            frameH = rdU32(0, 8, 256, 11, 2304, 14, 18688, 30);
            if (frameW == 0u || frameH == 0u || frameW > 16384u || frameH > 16384u) return false;
        }

        bool isLastFrame = true;
        if (frameType == 0u) {
            uint32_t blendMode = rdU32(0, 0, 1, 0, 2, 0, 3, 2);
            if (numExtraChannels > 0u && (blendMode == 2u || blendMode == 3u)) {
                (void)rdU32(0, 0, 1, 0, 2, 0, 3, 3);
            }
            if (haveCrop || blendMode > 0u) (void)br.ReadBits(1u);
            for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
                uint32_t ebm = rdU32(0, 0, 1, 0, 2, 0, 3, 2);
                if (numExtraChannels > 0u && (ebm == 2u || ebm == 3u)) (void)rdU32(0, 0, 1, 0, 2, 0, 3, 3);
                if (haveCrop || ebm > 0u) (void)br.ReadBits(1u);
            }
            if (haveAnimation) {
                (void)rdU32(0, 0, 1, 0, 0, 8, 0, 32);
            }
            isLastFrame = br.ReadBits(1u) != 0u;
        }
        if (!isLastFrame) {
            uint32_t canRef = br.ReadBits(2u);
            if (canRef != 0u) (void)br.ReadBits(1u);
        } else if (frameType == 2u) {
            (void)br.ReadBits(2u);
            (void)br.ReadBits(1u);
        }

        uint32_t frameNameLen = rdU32(0, 0, 0, 4, 16, 5, 48, 10);
        for (uint32_t ni = 0; ni < frameNameLen && !br.eof; ++ni) (void)br.ReadBits(8u);

        // RestorationFilter
        bool rfAllDefault = br.ReadBits(1u) != 0u;
        if (!rfAllDefault) {
            gaborish = br.ReadBits(1u) != 0u;
            if (gaborish && br.ReadBits(1u) != 0u) {
                for (int k = 0; k < 6; ++k) {
                    if (!ReadJxlF16(br, gabWeights[k])) return false;
                }
            }
            uint32_t epfIters = br.ReadBits(2u);
            if (epfIters != 0u) {
                if (encoding == 0u && br.ReadBits(1u) != 0u) { for (int k = 0; k < 8; ++k) (void)br.ReadBits(16u); }
                if (br.ReadBits(1u) != 0u) {
                    (void)br.ReadBits(16u);
                    (void)br.ReadBits(16u);
                    (void)br.ReadBits(16u);
                }
                if (epfIters == 3u && br.ReadBits(1u) != 0u) { (void)br.ReadBits(16u); (void)br.ReadBits(16u); }
                if (encoding == 1u) (void)br.ReadBits(16u);
            }
            if (rdU64() != 0ull) return false;
        }
        if (rdU64() != 0ull || fhFlags != 0ull) return false;
    }
    if (!br.AlignToByteZero()) return false;

    // TOC (Table of Contents)
    uint32_t numGroupsX   = (frameW + groupDim - 1u) / groupDim;
    uint32_t numGroupsY   = (frameH + groupDim - 1u) / groupDim;
    uint32_t numGroups    = numGroupsX * numGroupsY;
    uint32_t lfGroupDim   = groupDim * 8u;
    uint32_t numLfGroupsX = (frameW + lfGroupDim - 1u) / lfGroupDim;
    uint32_t numLfGroupsY = (frameH + lfGroupDim - 1u) / lfGroupDim;
    uint32_t numLfGroups  = numLfGroupsX * numLfGroupsY;
    uint32_t numTocEntries = (numGroups == 1u && numPasses == 1u) ? 1u : (1u + numLfGroups + 1u + numGroups * numPasses);

    bool permutedToc = br.ReadBits(1u) != 0u;
    if (br.eof || permutedToc) return false;
    std::vector<uint32_t> tocSizes(numTocEntries, 0u);
    for (uint32_t i = 0; i < numTocEntries; ++i) {
        tocSizes[i] = rdU32(0, 10, 1024, 14, 17408, 22, 4211712, 30);
    }
    if (!br.AlignToByteZero()) return false;

    size_t sectionBaseOff = br.bitPos >> 3;
    std::vector<size_t> sectionOffsets(numTocEntries, 0u);
    size_t runOff = sectionBaseOff;
    for (uint32_t i = 0; i < numTocEntries; ++i) {
        if (tocSizes[i] > csLen - runOff) return false;
        sectionOffsets[i] = runOff;
        runOff += tocSizes[i];
    }

    if (encoding == 0u) {
        JxlVarDctFrameParams vfp;
        vfp.frameW = frameW;
        vfp.frameH = frameH;
        vfp.orientation = orientation;
        vfp.bitsPerSample = bitsPerSample;
        vfp.colourSpace = colourSpace;
        vfp.transferFn = transferFn;
        vfp.xybEncoded = xybEncoded;
        vfp.doYcbcr = doYcbcr;
        vfp.xQmScale = xQmScale;
        vfp.bQmScale = bQmScale;
        vfp.gaborish = gaborish;
        for (int k = 0; k < 6; ++k) vfp.gabWeights[k] = gabWeights[k];
        vfp.opsin = opsinParams;
        return DecodeJxlVarDctSections(cs, sectionOffsets, tocSizes, vfp, ecInfos, outInfo);
    }

    // Build initial full-frame channels
    uint32_t numColorCh = (colourSpace == 1u && !xybEncoded) ? 1u : 3u;
    std::vector<JxlChannel> channels(numColorCh + numExtraChannels);
    for (uint32_t c = 0; c < numColorCh; ++c) {
        uint32_t hs = (numColorCh == 3u && (jpegUps[c] == 1u || jpegUps[c] == 2u)) ? 1u : 0u;
        uint32_t vs = (numColorCh == 3u && (jpegUps[c] == 1u || jpegUps[c] == 3u)) ? 1u : 0u;
        channels[c].w = (frameW + (1u << hs) - 1u) >> hs;
        channels[c].h = (frameH + (1u << vs) - 1u) >> vs;
        channels[c].hshift = static_cast<int32_t>(hs);
        channels[c].vshift = static_cast<int32_t>(vs);
    }
    for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
        uint32_t ds = ecInfos[ec].dimShift;
        channels[numColorCh + ec].w = (frameW + (1u << ds) - 1u) >> ds;
        channels[numColorCh + ec].h = (frameH + (1u << ds) - 1u) >> ds;
        channels[numColorCh + ec].hshift = static_cast<int32_t>(ds);
        channels[numColorCh + ec].vshift = static_cast<int32_t>(ds);
    }

    // Section 0: LfGlobal
    DetailBrotli::BitReader lfBr(cs + sectionOffsets[0], tocSizes[0]);
    float modLfDequant[3] = { 1.0f / 4096.0f, 1.0f / 512.0f, 1.0f / 256.0f };
    bool lfDequantDefault = lfBr.ReadBits(1u) != 0u;
    if (!lfDequantDefault) {
        for (int k = 0; k < 3; ++k) {
            if (!ReadJxlF16(lfBr, modLfDequant[k])) return false;
        }
    }
    bool hasGlobalTree = lfBr.ReadBits(1u) != 0u;
    if (lfBr.eof) return false;

    std::vector<JxlMaNode> globalTree;
    JxlHistogramSet globalHist;
    if (hasGlobalTree) {
        uint32_t gLeaves = 0u;
        if (!DecodeMaTree(lfBr, 1u << 20u, globalTree, gLeaves) ||
            !DecodeJxlHistograms(lfBr, gLeaves, globalHist)) {
            return false;
        }
    }

    // Global Modular Stream Header
    bool gmUseGlobalTree = lfBr.ReadBits(1u) != 0u;
    JxlWpHeader gmWp;
    if (lfBr.eof || !ReadWpHeader(lfBr, gmWp)) return false;
    auto rdLfU32 = [&](uint32_t c0, uint32_t u0, uint32_t c1, uint32_t u1,
                       uint32_t c2, uint32_t u2, uint32_t c3, uint32_t u3) -> uint32_t {
        uint32_t sel = lfBr.ReadBits(2u);
        if (sel == 0u) return c0 + lfBr.ReadBits(u0);
        if (sel == 1u) return c1 + lfBr.ReadBits(u1);
        if (sel == 2u) return c2 + lfBr.ReadBits(u2);
        return c3 + lfBr.ReadBits(u3);
    };
    uint32_t nbGlobalTransforms = rdLfU32(0, 0, 1, 0, 2, 4, 18, 8);
    if (lfBr.eof || nbGlobalTransforms > 64u) return false;

    uint32_t nbMetaChannels = 0u;
    std::vector<JxlTransform> globalTransforms(nbGlobalTransforms);
    for (uint32_t t = 0; t < nbGlobalTransforms; ++t) {
        if (!ReadAndApplyTransformHeader(lfBr, channels, nbMetaChannels, globalTransforms[t])) return false;
    }

    std::vector<JxlChannel*> gmTargets;
    for (size_t i = 0; i < channels.size(); ++i) {
        if (i < nbMetaChannels || (channels[i].w <= groupDim && channels[i].h <= groupDim)) {
            gmTargets.push_back(&channels[i]);
        }
    }
    if (!gmTargets.empty()) {
        if (!DecodeModularChannelsInStream(
                lfBr, gmTargets, 0u, hasGlobalTree, globalTree, globalHist,
                true, gmUseGlobalTree, gmWp)) {
            return false;
        }
    } else if (!gmUseGlobalTree) {
        // Even with 0 global-decoded channels, local tree/histograms are still coded in GlobalModular when !gmUseGlobalTree
        std::vector<JxlMaNode> dummyTree;
        JxlHistogramSet dummyHist;
        uint32_t dummyLeaves = 0u;
        if (!DecodeMaTree(lfBr, 1u << 20u, dummyTree, dummyLeaves) ||
            !DecodeJxlHistograms(lfBr, dummyLeaves, dummyHist)) {
            return false;
        }
    }

    // Allocate pixel buffers for remaining large channels before group stitching
    for (auto& ch : channels) {
        if (!ch.decoded) {
            ch.pixels.assign(static_cast<size_t>(ch.w) * ch.h, 0);
        }
    }

    // Multi-group decoding when numTocEntries > 1
    if (numTocEntries > 1u) {
        // 1. LfGroups (channels with hshift >= 3 && vshift >= 3)
        for (uint32_t lfg = 0; lfg < numLfGroups; ++lfg) {
            uint32_t lgx = lfg % numLfGroupsX;
            uint32_t lgy = lfg / numLfGroupsX;
            std::vector<JxlChannel> tileChs;
            std::vector<size_t> parentIdxs;
            std::vector<uint32_t> tileX0, tileY0;
            for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
                auto& ch = channels[ci];
                if (ch.decoded || ch.hshift < 3 || ch.vshift < 3) continue;
                uint32_t tx = (lgx * lfGroupDim) >> ch.hshift;
                uint32_t ty = (lgy * lfGroupDim) >> ch.vshift;
                if (tx >= ch.w || ty >= ch.h) continue;
                uint32_t tw = std::min(ch.w - tx, lfGroupDim >> ch.hshift);
                uint32_t th = std::min(ch.h - ty, lfGroupDim >> ch.vshift);
                JxlChannel tc; tc.w = tw; tc.h = th; tc.hshift = ch.hshift; tc.vshift = ch.vshift;
                tileChs.push_back(std::move(tc));
                parentIdxs.push_back(ci);
                tileX0.push_back(tx);
                tileY0.push_back(ty);
            }
            if (!tileChs.empty()) {
                DetailBrotli::BitReader lfgBr(cs + sectionOffsets[1u + lfg], tocSizes[1u + lfg]);
                std::vector<JxlChannel*> ptrs;
                for (auto& tc : tileChs) ptrs.push_back(&tc);
                if (!DecodeModularChannelsInStream(
                        lfgBr, ptrs, 1u + lfg, hasGlobalTree, globalTree, globalHist,
                        false, false, JxlWpHeader{})) {
                    return false;
                }
                for (size_t k = 0; k < tileChs.size(); ++k) {
                    auto& dstCh = channels[parentIdxs[k]];
                    const auto& srcCh = tileChs[k];
                    for (uint32_t ry = 0; ry < srcCh.h; ++ry) {
                        std::memcpy(&dstCh.pixels[static_cast<size_t>(tileY0[k] + ry) * dstCh.w + tileX0[k]],
                                    &srcCh.pixels[static_cast<size_t>(ry) * srcCh.w],
                                    static_cast<size_t>(srcCh.w) * sizeof(int32_t));
                    }
                }
            }
        }
        for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
            if (!channels[ci].decoded && channels[ci].hshift >= 3 && channels[ci].vshift >= 3) {
                channels[ci].decoded = true;
            }
        }

        // 2. PassGroups (remaining channels with hshift < 3 || vshift < 3)
        for (uint32_t g = 0; g < numGroups; ++g) {
            uint32_t gx = g % numGroupsX;
            uint32_t gy = g / numGroupsX;
            std::vector<JxlChannel> tileChs;
            std::vector<size_t> parentIdxs;
            std::vector<uint32_t> tileX0, tileY0;
            for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
                auto& ch = channels[ci];
                if (ch.decoded) continue;
                uint32_t tx = (gx * groupDim) >> ch.hshift;
                uint32_t ty = (gy * groupDim) >> ch.vshift;
                if (tx >= ch.w || ty >= ch.h) continue;
                uint32_t tw = std::min(ch.w - tx, groupDim >> ch.hshift);
                uint32_t th = std::min(ch.h - ty, groupDim >> ch.vshift);
                JxlChannel tc; tc.w = tw; tc.h = th; tc.hshift = ch.hshift; tc.vshift = ch.vshift;
                tileChs.push_back(std::move(tc));
                parentIdxs.push_back(ci);
                tileX0.push_back(tx);
                tileY0.push_back(ty);
            }
            if (!tileChs.empty()) {
                uint32_t tocIdx = 2u + numLfGroups + g;
                DetailBrotli::BitReader pgBr(cs + sectionOffsets[tocIdx], tocSizes[tocIdx]);
                std::vector<JxlChannel*> ptrs;
                for (auto& tc : tileChs) ptrs.push_back(&tc);
                if (!DecodeModularChannelsInStream(
                        pgBr, ptrs, 1u + numLfGroups + g, hasGlobalTree, globalTree, globalHist,
                        false, false, JxlWpHeader{})) {
                    return false;
                }
                for (size_t k = 0; k < tileChs.size(); ++k) {
                    auto& dstCh = channels[parentIdxs[k]];
                    const auto& srcCh = tileChs[k];
                    for (uint32_t ry = 0; ry < srcCh.h; ++ry) {
                        std::memcpy(&dstCh.pixels[static_cast<size_t>(tileY0[k] + ry) * dstCh.w + tileX0[k]],
                                    &srcCh.pixels[static_cast<size_t>(ry) * srcCh.w],
                                    static_cast<size_t>(srcCh.w) * sizeof(int32_t));
                    }
                }
            }
        }
        for (size_t ci = nbMetaChannels; ci < channels.size(); ++ci) {
            channels[ci].decoded = true;
        }
    }

    // Undo global Modular transforms in reverse order
    for (int ti = static_cast<int>(globalTransforms.size()) - 1; ti >= 0; --ti) {
        if (!UndoModularTransform(channels, globalTransforms[ti], bitsPerSample)) {
            return false;
        }
    }
    if (channels.size() < numColorCh) return false;

    // Convert reconstructed channels to Premultiplied BGRA
    int alphaIdx = -1;
    bool alphaPremul = false;
    for (uint32_t ec = 0; ec < numExtraChannels; ++ec) {
        if (ecInfos[ec].type == 0u && numColorCh + ec < channels.size()) {
            alphaIdx = static_cast<int>(numColorCh + ec);
            alphaPremul = ecInfos[ec].alphaAssoc;
            break;
        }
    }

    const int64_t maxSample = (1ll << bitsPerSample) - 1ll;
    const int64_t maxAlpha  = (alphaIdx >= 0) ? ((1ll << ecInfos[alphaIdx - numColorCh].bits) - 1ll) : 255ll;

    auto scaleTo8 = [&](int64_t v, int64_t mx) -> uint8_t {
        v = std::clamp<int64_t>(v, 0ll, mx);
        if (mx == 255ll) return static_cast<uint8_t>(v);
        return static_cast<uint8_t>((v * 255ll + (mx >> 1)) / mx);
    };

    std::vector<uint8_t> rawBgra(static_cast<size_t>(frameW) * frameH * 4u, 255u);
    for (uint32_t y = 0; y < frameH; ++y) {
        for (uint32_t x = 0; x < frameW; ++x) {
            auto sampleCh = [&](size_t ci) -> int64_t {
                const auto& ch = channels[ci];
                uint32_t sx = std::min(ch.w - 1u, x >> ch.hshift);
                uint32_t sy = std::min(ch.h - 1u, y >> ch.vshift);
                return ch.pixels[static_cast<size_t>(sy) * ch.w + sx];
            };

            uint8_t r8 = 0u, g8 = 0u, b8 = 0u, a8 = 255u;
            if (numColorCh == 1u) {
                r8 = g8 = b8 = scaleTo8(sampleCh(0), maxSample);
            } else if (doYcbcr) {
                // JXL do_ycbcr channel order: [0] = Cb, [1] = Y, [2] = Cr (zero-centered signed offsets!)
                double half = static_cast<double>(1ll << (bitsPerSample - 1u));
                double sc = 255.0 / static_cast<double>(maxSample);
                double cb = static_cast<double>(sampleCh(0)) * sc;
                double yv = (static_cast<double>(sampleCh(1)) + half) * sc;
                double cr = static_cast<double>(sampleCh(2)) * sc;
                r8 = static_cast<uint8_t>(std::clamp(std::lround(yv + 1.402 * cr), 0l, 255l));
                g8 = static_cast<uint8_t>(std::clamp(std::lround(yv - 0.344136 * cb - 0.714136 * cr), 0l, 255l));
                b8 = static_cast<uint8_t>(std::clamp(std::lround(yv + 1.772 * cb), 0l, 255l));
            } else if (xybEncoded) {
                // Modular XYB channel order: [0] = Y, [1] = X, [2] = B - Y (ISO/IEC 18181-1 Clause 10.1.2)
                float yv = static_cast<float>(sampleCh(0)) * modLfDequant[1];
                float xv = static_cast<float>(sampleCh(1)) * modLfDequant[0];
                float bv = static_cast<float>(sampleCh(2)) * modLfDequant[2] + yv;
                JxlXybToSrgb8(xv, yv, bv, opsinParams, transferFn, r8, g8, b8);
                if (colourSpace == 1u) r8 = g8 = b8;
            } else {
                r8 = scaleTo8(sampleCh(0), maxSample);
                g8 = scaleTo8(sampleCh(1), maxSample);
                b8 = scaleTo8(sampleCh(2), maxSample);
            }

            if (alphaIdx >= 0) {
                a8 = scaleTo8(sampleCh(static_cast<size_t>(alphaIdx)), maxAlpha);
            }

            uint8_t* dst = &rawBgra[(static_cast<size_t>(y) * frameW + x) * 4u];
            if (alphaPremul) {
                dst[0] = std::min(b8, a8);
                dst[1] = std::min(g8, a8);
                dst[2] = std::min(r8, a8);
                dst[3] = a8;
            } else {
                WritePremulBGRA(dst, r8, g8, b8, a8);
            }
        }
    }

    // Apply EXIF/JXL orientation (1..8)
    uint32_t outW = (orientation >= 5u) ? frameH : frameW;
    uint32_t outH = (orientation >= 5u) ? frameW : frameH;
    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = outW;
    outInfo.origHeight = outH;
    outInfo.channels = numColorCh + (alphaIdx >= 0 ? 1u : 0u);
    outInfo.bitsPerChannel = bitsPerSample;
    outInfo.formatName = L"JPEG XL Modular (" + std::to_wstring(outW) + L"x" + std::to_wstring(outH) +
                         L", " + std::to_wstring(bitsPerSample) + L"-bit .jxl)";

    if (orientation == 1u) {
        outInfo.bgraPixels = std::move(rawBgra);
    } else {
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t sy = 0; sy < frameH; ++sy) {
            for (uint32_t sx = 0; sx < frameW; ++sx) {
                uint32_t dx = sx, dy = sy;
                switch (orientation) {
                case 2: dx = frameW - 1u - sx; dy = sy; break;
                case 3: dx = frameW - 1u - sx; dy = frameH - 1u - sy; break;
                case 4: dx = sx; dy = frameH - 1u - sy; break;
                case 5: dx = sy; dy = sx; break;
                case 6: dx = frameH - 1u - sy; dy = sx; break;
                case 7: dx = frameH - 1u - sy; dy = frameW - 1u - sx; break;
                case 8: dx = sy; dy = frameW - 1u - sx; break;
                default: break;
                }
                std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(dy) * outW + dx) * 4u],
                            &rawBgra[(static_cast<size_t>(sy) * frameW + sx) * 4u], 4u);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace DetailJxlBpg

inline bool DecodeJpegXl(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 5u) return false;
    static const uint8_t kJxlContainerMagic[12] = {
        0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A
    };
    bool isJxlNaked = (size >= 5u && data[0] == 0xFFu && data[1] == 0x0Au);
    bool isJxlBoxed = (size >= 20u && std::memcmp(data, kJxlContainerMagic, 12) == 0);
    if (isJxlNaked || isJxlBoxed) {
        // 1. Extract bare codestream or assemble ISOBMFF 'jxlc' / 'jxlp' / 'brob' codestream
        std::vector<uint8_t> assembled;
        const uint8_t* cs = nullptr;
        size_t csLen = 0u;

        if (isJxlNaked) {
            cs = data;
            csLen = size;
        } else {
            size_t pos = 0u;
            bool hasFtyp = false;
            bool sawJxlc = false;
            bool sawJxlp = false;
            bool jxlpDone = false;
            uint32_t expectedJxlpSeq = 0u;

            while (pos < size) {
                if (pos + 8u > size) return false;
                uint64_t boxSize = DetailImg100::RdU32BE(data + pos);
                const uint8_t* boxType = data + pos + 4u;
                size_t hdrLen = 8u;
                if (boxSize == 1ull) {
                    if (pos + 16u > size) return false;
                    boxSize = DetailImg100::RdU64BE(data + pos + 8u);
                    hdrLen = 16u;
                } else if (boxSize == 0ull) {
                    boxSize = size - pos;
                }
                if (boxSize < hdrLen || boxSize > size - pos) return false;

                const uint8_t* payload = data + pos + hdrLen;
                size_t plen = static_cast<size_t>(boxSize) - hdrLen;

                auto handleCodestreamBox = [&](const uint8_t* innerType, const uint8_t* innerData, size_t innerLen) -> bool {
                    if (std::memcmp(innerType, "jxlc", 4) == 0) {
                        if (sawJxlc || sawJxlp || innerLen < 5u) return false;
                        sawJxlc = true;
                        assembled.insert(assembled.end(), innerData, innerData + innerLen);
                    } else if (std::memcmp(innerType, "jxlp", 4) == 0) {
                        if (sawJxlc || jxlpDone || innerLen < 4u) return false;
                        uint32_t seqWord = DetailImg100::RdU32BE(innerData);
                        uint32_t seqIdx = seqWord & 0x7FFFFFFFu;
                        if (seqIdx != expectedJxlpSeq) return false;
                        ++expectedJxlpSeq;
                        if ((seqWord & 0x80000000u) != 0u) jxlpDone = true;
                        sawJxlp = true;
                        assembled.insert(assembled.end(), innerData + 4u, innerData + innerLen);
                    }
                    return true;
                };

                if (std::memcmp(boxType, "ftyp", 4) == 0) {
                    if (plen < 8u || std::memcmp(payload, "jxl ", 4) != 0) return false;
                    hasFtyp = true;
                } else if (std::memcmp(boxType, "jxll", 4) == 0) {
                    if (plen != 1u) return false;
                } else if (std::memcmp(boxType, "jxlc", 4) == 0 || std::memcmp(boxType, "jxlp", 4) == 0) {
                    if (!handleCodestreamBox(boxType, payload, plen)) return false;
                } else if (std::memcmp(boxType, "brob", 4) == 0) {
                    if (plen < 5u) return false;
                    if (std::memcmp(payload, "jxlc", 4) == 0 || std::memcmp(payload, "jxlp", 4) == 0) {
                        std::vector<uint8_t> unbrob;
                        if (!DetailBrotli::Decompress(payload + 4u, plen - 4u, unbrob, 64u << 20u)) return false;
                        if (!handleCodestreamBox(payload, unbrob.data(), unbrob.size())) return false;
                    }
                }
                pos += static_cast<size_t>(boxSize);
            }

            if (!hasFtyp || assembled.size() < 5u || assembled[0] != 0xFFu || assembled[1] != 0x0Au) {
                return false;
            }
            if (sawJxlp && !jxlpDone) return false;
            cs = assembled.data();
            csLen = assembled.size();
        }

        // 2. Decode ISO/IEC 18181-1 JXL Codestream via pure-C++ VarDCT + Modular + rANS + XYB decoder
        if (DetailJxlBpg::DecodeJxlCodestreamModular(cs, csLen, outInfo)) {
            return true;
        }
        // 3. Fallback to Windows 11 24H2+ native WIC JPEG XL codec (e.g. compressed ICC streams)
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"JPEG XL Raster (.jxl)", outInfo);
    }
    return false;
}

} // namespace Formats
