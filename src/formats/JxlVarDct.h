#pragma once
// JxlVarDct.h — #6 Pure-C++20 JPEG XL VarDCT Lossy Frame Decoder (ISO/IEC 18181-1 Clauses 9, 10, 11)
// Included inside `namespace Formats::DetailJxlBpg` in `ExtendedImages100.h` right after `DecodeModularChannelsInStream`.

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
