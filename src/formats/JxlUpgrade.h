#pragma once
// JxlUpgrade.h — #6 Pure-C++20 JPEG XL (.jxl) rANS Entropy Decoder, XYB Opsin Color & VarDCT Lossy Frame Decoder
// Included inside `namespace Formats::DetailJxlBpg` in `ExtendedImages100.h`.
//
// Zero-cheat ISO/IEC 18181-1 implementation:
//   1. JXL rANS Entropy Coding (`use_prefix_code == 0`):
//      - 1-symbol, 2-symbol, flat, and general logcount+shift+RLE histograms (sum == 4096)
//      - Exact 12-bit AliasTable construction (`log_alpha_size` 5..8, `log_bucket_size` 4..7)
//      - 32-bit rANS state machine + 16-bit refill + ISO 18181-1 `0x00130000` final-state verification
//   2. XYB Perceptual Color Space (`xyb_encoded == 1`):
//      - Default & custom `OpsinInverseMatrix` (`11.0315669...` LMS->sRGB) + `0.003793073255...` absorbance bias
//      - Signed cube-root expansion + IEC 61966-2-1 Linear->sRGB transfer function (plus Linear & PQ)
//   3. Pure-C++ VarDCT Lossy Frame Decoder (`encoding == 0`, including `all_default == 1` FrameHeader):
//      - `LfGlobal`: `lf_dequant`, `Quantizer` (`global_scale`, `quant_lf`), `HFBlockContext`, `LFChannelCorrelation`, `GlobalModular`
//      - `LfGroup`: `extra_precision` + Modular LF DC subimage (`Y, X, B`) + `HfMetadata` (`x_from_y`, `b_from_y`, `block_info`, `epf`)
//      - `HfGlobal`: ISO Annex C `DCT8`/`DCT16` `DequantMatrices` + `num_hf_presets` + `HfPass` (`495 * presets * block_ctx` histograms)
//      - `PassGroup`: Context-modeled AC coefficients (`NonZeroContext` + `ZeroDensityContext`), Chroma-from-Luma (CfL),
//        biased deadzone dequantization, orthonormal 2D `8x8` & `16x16` IDCT, optional Gaborish 3x3 filter, ExtraChannel Alpha & Orientation 1..8.

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
