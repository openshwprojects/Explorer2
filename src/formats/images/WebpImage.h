#pragma once
// WebpImage.h — RFC 9649 WebP Container + VP8L Lossless + ALPH + VP8 Keyframe Decoder (.webp)

namespace Formats {

namespace WebpDetail {

inline uint16_t Rd16(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU16(p, be); }
inline uint32_t Rd32(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU32(p, be); }

// ============================================================================
// 3. Pure-C++ RFC 9649 WebP VP8L Lossless + ALPH + VP8 Keyframe Decoder
// ============================================================================
inline uint32_t DivRoundUpU32(uint32_t num, uint32_t den) {
    return (num + den - 1u) / den;
}

inline bool ReadVp8lPrefixCode(DetailBrotli::BitReader& br, uint32_t alphabetSize, DetailBrotli::PrefixTree& outTree) {
    bool isSimple = br.ReadBits(1u) != 0u;
    if (br.eof) return false;
    if (isSimple) {
        uint32_t numSyms = br.ReadBits(1u) + 1u;
        uint32_t isFirst8 = br.ReadBits(1u);
        uint32_t s0 = br.ReadBits(isFirst8 ? 8u : 1u);
        if (br.eof || s0 >= alphabetSize) return false;
        if (numSyms == 1u) {
            outTree.isSingle = true;
            outTree.singleSym = s0;
            outTree.maxLen = 0u;
            outTree.table.clear();
            return true;
        }
        uint32_t s1 = br.ReadBits(8u);
        if (br.eof || s1 >= alphabetSize || s1 == s0) return false;
        outTree.isSingle = false;
        outTree.maxLen = 1u;
        outTree.table.resize(2u);
        outTree.table[0] = (1u << 16u) | s0;
        outTree.table[1] = (1u << 16u) | s1;
        return true;
    }

    static constexpr uint8_t kClOrder[19] = {
        17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    uint32_t numCl = br.ReadBits(4u) + 4u;
    if (br.eof) return false;
    uint8_t clLengths[19] = {};
    for (uint32_t i = 0; i < numCl; ++i) {
        clLengths[kClOrder[i]] = static_cast<uint8_t>(br.ReadBits(3u));
    }
    if (br.eof) return false;

    DetailBrotli::PrefixTree clTree;
    if (!clTree.BuildFromLengths(clLengths, 19u)) return false;

    uint32_t maxSymbol = alphabetSize;
    if (br.ReadBits(1u) != 0u) {
        uint32_t lenNbits = 2u + 2u * br.ReadBits(3u);
        maxSymbol = 2u + br.ReadBits(lenNbits);
        if (br.eof || maxSymbol > alphabetSize) maxSymbol = alphabetSize;
    }

    std::vector<uint8_t> lengths(alphabetSize, 0u);
    uint8_t prevNonZero = 8u;
    uint32_t sym = 0u;
    while (sym < alphabetSize && maxSymbol > 0u) {
        --maxSymbol;
        uint32_t code = 0u;
        if (!clTree.Decode(br, code)) return false;
        if (code < 16u) {
            lengths[sym++] = static_cast<uint8_t>(code);
            if (code != 0u) prevNonZero = static_cast<uint8_t>(code);
        } else if (code == 16u) {
            uint32_t rep = 3u + br.ReadBits(2u);
            if (br.eof || sym + rep > alphabetSize) return false;
            for (uint32_t k = 0; k < rep; ++k) lengths[sym++] = prevNonZero;
        } else if (code == 17u) {
            uint32_t rep = 3u + br.ReadBits(3u);
            if (br.eof || sym + rep > alphabetSize) return false;
            sym += rep;
        } else {
            uint32_t rep = 11u + br.ReadBits(7u);
            if (br.eof || sym + rep > alphabetSize) return false;
            sym += rep;
        }
    }
    return outTree.BuildFromLengths(lengths.data(), alphabetSize);
}

inline uint32_t DecodeVp8lLzPrefix(DetailBrotli::BitReader& br, uint32_t prefixCode) {
    if (prefixCode < 4u) return prefixCode + 1u;
    uint32_t extra = (prefixCode - 2u) >> 1u;
    uint32_t offset = (2u + (prefixCode & 1u)) << extra;
    return offset + br.ReadBits(extra) + 1u;
}

inline uint32_t AddArgbBytes(uint32_t a, uint32_t b) {
    uint32_t a8 = ((a >> 24) + (b >> 24)) & 0xFFu;
    uint32_t r8 = (((a >> 16) & 0xFFu) + ((b >> 16) & 0xFFu)) & 0xFFu;
    uint32_t g8 = (((a >> 8)  & 0xFFu) + ((b >> 8)  & 0xFFu)) & 0xFFu;
    uint32_t b8 = ((a & 0xFFu) + (b & 0xFFu)) & 0xFFu;
    return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
}

inline uint32_t Avg2Argb(uint32_t a, uint32_t b) {
    uint32_t a8 = (((a >> 24) & 0xFFu) + ((b >> 24) & 0xFFu)) >> 1u;
    uint32_t r8 = (((a >> 16) & 0xFFu) + ((b >> 16) & 0xFFu)) >> 1u;
    uint32_t g8 = (((a >> 8)  & 0xFFu) + ((b >> 8)  & 0xFFu)) >> 1u;
    uint32_t b8 = ((a & 0xFFu) + (b & 0xFFu)) >> 1u;
    return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
}

inline uint32_t PredictVp8lPixel(uint8_t mode, uint32_t L, uint32_t T, uint32_t TL, uint32_t TR) {
    auto clamp255 = [](int v) -> uint32_t { return static_cast<uint32_t>(std::clamp(v, 0, 255)); };
    switch (mode) {
    case 0:  return 0xFF000000u;
    case 1:  return L;
    case 2:  return T;
    case 3:  return TR;
    case 4:  return TL;
    case 5:  return Avg2Argb(Avg2Argb(L, TR), T);
    case 6:  return Avg2Argb(L, TL);
    case 7:  return Avg2Argb(L, T);
    case 8:  return Avg2Argb(TL, T);
    case 9:  return Avg2Argb(T, TR);
    case 10: return Avg2Argb(Avg2Argb(L, TL), Avg2Argb(T, TR));
    case 11: { // Select(L, T, TL)
        int pa = static_cast<int>((L >> 24) & 0xFF) + static_cast<int>((T >> 24) & 0xFF) - static_cast<int>((TL >> 24) & 0xFF);
        int pr = static_cast<int>((L >> 16) & 0xFF) + static_cast<int>((T >> 16) & 0xFF) - static_cast<int>((TL >> 16) & 0xFF);
        int pg = static_cast<int>((L >> 8)  & 0xFF) + static_cast<int>((T >> 8)  & 0xFF) - static_cast<int>((TL >> 8)  & 0xFF);
        int pb = static_cast<int>(L & 0xFF)         + static_cast<int>(T & 0xFF)         - static_cast<int>(TL & 0xFF);
        int pL = std::abs(pa - static_cast<int>((L >> 24) & 0xFF)) + std::abs(pr - static_cast<int>((L >> 16) & 0xFF)) +
                 std::abs(pg - static_cast<int>((L >> 8)  & 0xFF)) + std::abs(pb - static_cast<int>(L & 0xFF));
        int pT = std::abs(pa - static_cast<int>((T >> 24) & 0xFF)) + std::abs(pr - static_cast<int>((T >> 16) & 0xFF)) +
                 std::abs(pg - static_cast<int>((T >> 8)  & 0xFF)) + std::abs(pb - static_cast<int>(T & 0xFF));
        return (pL < pT) ? L : T;
    }
    case 12: { // ClampAddSubtractFull
        uint32_t a8 = clamp255(static_cast<int>((L >> 24) & 0xFF) + static_cast<int>((T >> 24) & 0xFF) - static_cast<int>((TL >> 24) & 0xFF));
        uint32_t r8 = clamp255(static_cast<int>((L >> 16) & 0xFF) + static_cast<int>((T >> 16) & 0xFF) - static_cast<int>((TL >> 16) & 0xFF));
        uint32_t g8 = clamp255(static_cast<int>((L >> 8)  & 0xFF) + static_cast<int>((T >> 8)  & 0xFF) - static_cast<int>((TL >> 8)  & 0xFF));
        uint32_t b8 = clamp255(static_cast<int>(L & 0xFF)         + static_cast<int>(T & 0xFF)         - static_cast<int>(TL & 0xFF));
        return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
    }
    case 13: { // ClampAddSubtractHalf
        uint32_t avg = Avg2Argb(L, T);
        auto halfStep = [&](int av, int tl) -> uint32_t { return clamp255(av + (av - tl) / 2); };
        uint32_t a8 = halfStep(static_cast<int>((avg >> 24) & 0xFF), static_cast<int>((TL >> 24) & 0xFF));
        uint32_t r8 = halfStep(static_cast<int>((avg >> 16) & 0xFF), static_cast<int>((TL >> 16) & 0xFF));
        uint32_t g8 = halfStep(static_cast<int>((avg >> 8)  & 0xFF), static_cast<int>((TL >> 8)  & 0xFF));
        uint32_t b8 = halfStep(static_cast<int>(avg & 0xFF),         static_cast<int>(TL & 0xFF));
        return (a8 << 24) | (r8 << 16) | (g8 << 8) | b8;
    }
    default: return 0xFF000000u;
    }
}

struct Vp8lTransformRecord {
    uint8_t type = 0;
    uint8_t sizeBits = 0;
    uint32_t origWidth = 0;
    uint8_t widthBits = 0;
    uint32_t subW = 0, subH = 0;
    std::vector<uint32_t> data;
};

inline bool DecodeVp8lImageStream(
    DetailBrotli::BitReader& br, uint32_t width, uint32_t height, bool isRootLevel, int depth,
    std::vector<uint32_t>& outArgb)
{
    if (depth > 4 || width == 0 || height == 0 || width > 16384 || height > 16384) return false;

    std::vector<Vp8lTransformRecord> transforms;
    uint32_t curW = width;
    uint8_t seenMask = 0u;

    if (isRootLevel) {
        while (br.ReadBits(1u) != 0u) {
            uint8_t trType = static_cast<uint8_t>(br.ReadBits(2u));
            if (br.eof || (seenMask & (1u << trType)) != 0u) return false;
            seenMask |= static_cast<uint8_t>(1u << trType);

            Vp8lTransformRecord tr;
            tr.type = trType;
            tr.origWidth = curW;
            if (trType == 0u || trType == 1u) {
                tr.sizeBits = static_cast<uint8_t>(br.ReadBits(3u) + 2u);
                tr.subW = DivRoundUpU32(curW, 1u << tr.sizeBits);
                tr.subH = DivRoundUpU32(height, 1u << tr.sizeBits);
                if (!DecodeVp8lImageStream(br, tr.subW, tr.subH, false, depth + 1, tr.data)) return false;
            } else if (trType == 2u) {
                // SubtractGreen has no sub-stream
            } else { // ColorIndexing (palette)
                uint32_t palSize = br.ReadBits(8u) + 1u;
                if (br.eof) return false;
                if (!DecodeVp8lImageStream(br, palSize, 1u, false, depth + 1, tr.data)) return false;
                for (uint32_t i = 1u; i < palSize; ++i) {
                    tr.data[i] = AddArgbBytes(tr.data[i], tr.data[i - 1u]);
                }
                if (palSize <= 2u) tr.widthBits = 3u;
                else if (palSize <= 4u) tr.widthBits = 2u;
                else if (palSize <= 16u) tr.widthBits = 1u;
                else tr.widthBits = 0u;
                curW = DivRoundUpU32(curW, 1u << tr.widthBits);
            }
            transforms.push_back(std::move(tr));
        }
    }

    // Color Cache
    uint32_t colorCacheBits = 0u;
    uint32_t colorCacheSize = 0u;
    if (br.ReadBits(1u) != 0u) {
        colorCacheBits = br.ReadBits(4u);
        if (br.eof || colorCacheBits < 1u || colorCacheBits > 11u) return false;
        colorCacheSize = 1u << colorCacheBits;
    }

    // Meta-Huffman grid (only at root level!)
    uint32_t huffBits = 0u;
    uint32_t huffGridW = 0u;
    uint32_t numHuffGroups = 1u;
    std::vector<uint32_t> huffImage;
    if (isRootLevel && br.ReadBits(1u) != 0u) {
        huffBits = br.ReadBits(3u) + 2u;
        huffGridW = DivRoundUpU32(curW, 1u << huffBits);
        uint32_t huffGridH = DivRoundUpU32(height, 1u << huffBits);
        if (!DecodeVp8lImageStream(br, huffGridW, huffGridH, false, depth + 1, huffImage)) return false;
        for (uint32_t px : huffImage) {
            uint32_t grp = (px >> 8u) & 0xFFFFu;
            if (grp + 1u > numHuffGroups) numHuffGroups = grp + 1u;
        }
        if (numHuffGroups > 4096u) return false;
    }

    struct HuffGroup {
        DetailBrotli::PrefixTree trees[5];
    };
    std::vector<HuffGroup> groups(numHuffGroups);
    uint32_t greenAlphaSize = 256u + 24u + colorCacheSize;
    for (uint32_t g = 0; g < numHuffGroups; ++g) {
        if (!ReadVp8lPrefixCode(br, greenAlphaSize, groups[g].trees[0]) ||
            !ReadVp8lPrefixCode(br, 256u,           groups[g].trees[1]) ||
            !ReadVp8lPrefixCode(br, 256u,           groups[g].trees[2]) ||
            !ReadVp8lPrefixCode(br, 256u,           groups[g].trees[3]) ||
            !ReadVp8lPrefixCode(br, 40u,            groups[g].trees[4])) {
            return false;
        }
    }

    // RFC 9649 Table 1: 120 2D neighborhood distance codes (dx, dy)
    static constexpr int8_t kDistLut2D[120][2] = {
        {0,1},{1,0},{1,1},{-1,1},{0,2},{2,0},{1,2},{-1,2},{2,1},{-2,1},{2,2},{-2,2},
        {0,3},{3,0},{1,3},{-1,3},{3,1},{-3,1},{2,3},{-2,3},{3,2},{-3,2},{0,4},{4,0},
        {1,4},{-1,4},{4,1},{-4,1},{3,3},{-3,3},{2,4},{-2,4},{4,2},{-4,2},{0,5},{3,4},
        {-3,4},{4,3},{-4,3},{5,0},{1,5},{-1,5},{5,1},{-5,1},{2,5},{-2,5},{5,2},{-5,2},
        {4,4},{-4,4},{3,5},{-3,5},{5,3},{-5,3},{0,6},{6,0},{1,6},{-1,6},{6,1},{-6,1},
        {2,6},{-2,6},{6,2},{-6,2},{4,5},{-4,5},{5,4},{-5,4},{3,6},{-3,6},{6,3},{-6,3},
        {0,7},{7,0},{1,7},{-1,7},{5,5},{-5,5},{7,1},{-7,1},{4,6},{-4,6},{6,4},{-6,4},
        {2,7},{-2,7},{7,2},{-7,2},{3,7},{-3,7},{7,3},{-7,3},{5,6},{-5,6},{6,5},{-6,5},
        {8,0},{4,7},{-4,7},{7,4},{-7,4},{8,1},{8,2},{6,6},{-6,6},{8,3},{5,7},{-5,7},
        {7,5},{-7,5},{8,4},{6,7},{-6,7},{7,6},{-7,6},{8,5},{7,7},{-7,7},{8,6},{8,7}
    };

    std::vector<uint32_t> cache(colorCacheSize, 0u);
    auto insertCache = [&](uint32_t argb) {
        if (colorCacheSize > 0u) {
            uint32_t key = (0x1E35A7BDu * argb) >> (32u - colorCacheBits);
            cache[key] = argb;
        }
    };

    const size_t totalPx = static_cast<size_t>(curW) * height;
    std::vector<uint32_t> codedPx(totalPx, 0u);
    size_t pos = 0;
    while (pos < totalPx) {
        uint32_t grpIdx = 0u;
        if (!huffImage.empty()) {
            uint32_t x = static_cast<uint32_t>(pos % curW);
            uint32_t y = static_cast<uint32_t>(pos / curW);
            uint32_t metaPx = huffImage[static_cast<size_t>(y >> huffBits) * huffGridW + (x >> huffBits)];
            grpIdx = (metaPx >> 8u) & 0xFFFFu;
        }
        const auto& grp = groups[grpIdx];

        uint32_t s = 0u;
        if (!grp.trees[0].Decode(br, s)) return false;
        if (s < 256u) {
            uint32_t r = 0u, b = 0u, a = 0u;
            if (!grp.trees[1].Decode(br, r) || !grp.trees[2].Decode(br, b) || !grp.trees[3].Decode(br, a)) {
                return false;
            }
            uint32_t argb = (a << 24u) | (r << 16u) | (s << 8u) | b;
            codedPx[pos++] = argb;
            insertCache(argb);
        } else if (s < 280u) {
            uint32_t len = DecodeVp8lLzPrefix(br, s - 256u);
            uint32_t distSym = 0u;
            if (!grp.trees[4].Decode(br, distSym)) return false;
            uint32_t distCode = DecodeVp8lLzPrefix(br, distSym);
            if (br.eof) return false;

            int64_t dist = 1;
            if (distCode <= 120u) {
                int dx = kDistLut2D[distCode - 1u][0];
                int dy = kDistLut2D[distCode - 1u][1];
                dist = static_cast<int64_t>(dy) * static_cast<int64_t>(curW) + dx;
                if (dist < 1) dist = 1;
            } else {
                dist = static_cast<int64_t>(distCode) - 120;
            }
            if (static_cast<size_t>(dist) > pos || pos + len > totalPx) return false;
            for (uint32_t k = 0; k < len; ++k) {
                uint32_t v = codedPx[pos - static_cast<size_t>(dist)];
                codedPx[pos++] = v;
                insertCache(v);
            }
        } else {
            uint32_t cIdx = s - 280u;
            if (cIdx >= colorCacheSize) return false;
            uint32_t argb = cache[cIdx];
            codedPx[pos++] = argb;
            insertCache(argb);
        }
    }

    // Undo transforms in reverse order!
    for (int ti = static_cast<int>(transforms.size()) - 1; ti >= 0; --ti) {
        const auto& tr = transforms[ti];
        if (tr.type == 3u) { // ColorIndexing (Palette + Pixel Unbundling)
            uint32_t dstW = tr.origWidth;
            std::vector<uint32_t> expanded(static_cast<size_t>(dstW) * height, 0xFF000000u);
            uint32_t palSz = static_cast<uint32_t>(tr.data.size());
            if (tr.widthBits == 0u) {
                for (size_t i = 0; i < expanded.size(); ++i) {
                    uint32_t idx = (codedPx[i] >> 8u) & 0xFFu;
                    expanded[i] = (idx < palSz) ? tr.data[idx] : 0u;
                }
            } else {
                uint32_t pxPerByte = 1u << tr.widthBits;
                uint32_t bitsPerPx = 8u >> tr.widthBits;
                uint32_t mask = (1u << bitsPerPx) - 1u;
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < dstW; ++x) {
                        uint32_t packed = (codedPx[static_cast<size_t>(y) * curW + (x >> tr.widthBits)] >> 8u) & 0xFFu;
                        uint32_t shift  = (x & (pxPerByte - 1u)) * bitsPerPx;
                        uint32_t idx    = (packed >> shift) & mask;
                        expanded[static_cast<size_t>(y) * dstW + x] = (idx < palSz) ? tr.data[idx] : 0u;
                    }
                }
            }
            codedPx = std::move(expanded);
            curW = dstW;
        } else if (tr.type == 2u) { // SubtractGreen
            for (uint32_t& px : codedPx) {
                uint32_t g = (px >> 8u) & 0xFFu;
                uint32_t r = (((px >> 16u) & 0xFFu) + g) & 0xFFu;
                uint32_t b = ((px & 0xFFu) + g) & 0xFFu;
                px = (px & 0xFF00FF00u) | (r << 16u) | b;
            }
        } else if (tr.type == 1u) { // CrossColorTransform
            auto colorDelta = [](int8_t t, int8_t c) -> int {
                return (static_cast<int>(t) * static_cast<int>(c)) >> 5;
            };
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < curW; ++x) {
                    uint32_t ccode = tr.data[static_cast<size_t>(y >> tr.sizeBits) * tr.subW + (x >> tr.sizeBits)];
                    int8_t g2r = static_cast<int8_t>(ccode & 0xFFu);
                    int8_t g2b = static_cast<int8_t>((ccode >> 8u) & 0xFFu);
                    int8_t r2b = static_cast<int8_t>((ccode >> 16u) & 0xFFu);
                    uint32_t& px = codedPx[static_cast<size_t>(y) * curW + x];
                    uint8_t g = static_cast<uint8_t>((px >> 8u) & 0xFFu);
                    uint8_t r = static_cast<uint8_t>(((px >> 16u) & 0xFFu) + colorDelta(g2r, static_cast<int8_t>(g)));
                    uint8_t b = static_cast<uint8_t>((px & 0xFFu) + colorDelta(g2b, static_cast<int8_t>(g)) +
                                                     colorDelta(r2b, static_cast<int8_t>(r)));
                    px = (px & 0xFF00FF00u) | (static_cast<uint32_t>(r) << 16u) | b;
                }
            }
        } else if (tr.type == 0u) { // PredictorTransform
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < curW; ++x) {
                    size_t idx = static_cast<size_t>(y) * curW + x;
                    uint32_t pred = 0xFF000000u;
                    if (y == 0u && x == 0u) {
                        pred = 0xFF000000u;
                    } else if (y == 0u) {
                        pred = codedPx[idx - 1u];
                    } else if (x == 0u) {
                        pred = codedPx[idx - curW];
                    } else {
                        uint8_t mode = static_cast<uint8_t>((tr.data[static_cast<size_t>(y >> tr.sizeBits) * tr.subW + (x >> tr.sizeBits)] >> 8u) & 0x0Fu);
                        uint32_t L  = codedPx[idx - 1u];
                        uint32_t T  = codedPx[idx - curW];
                        uint32_t TL = codedPx[idx - curW - 1u];
                        uint32_t TR = (x + 1u < curW) ? codedPx[idx - curW + 1u] : codedPx[static_cast<size_t>(y) * curW];
                        pred = PredictVp8lPixel(mode, L, T, TL, TR);
                    }
                    codedPx[idx] = AddArgbBytes(codedPx[idx], pred);
                }
            }
        }
    }

    if (curW != width) return false;
    outArgb = std::move(codedPx);
    return true;
}

inline bool DecodeVp8lChunk(const uint8_t* ptr, size_t len, uint32_t& outW, uint32_t& outH, bool& outHasAlpha, std::vector<uint32_t>& outArgb) {
    if (!ptr || len < 5 || ptr[0] != 0x2Fu) return false;
    DetailBrotli::BitReader br(ptr + 1, len - 1);
    outW = br.ReadBits(14u) + 1u;
    outH = br.ReadBits(14u) + 1u;
    outHasAlpha = br.ReadBits(1u) != 0u;
    uint32_t ver = br.ReadBits(3u);
    if (br.eof || ver != 0u) return false;
    return DecodeVp8lImageStream(br, outW, outH, true, 0, outArgb);
}

inline bool DecodeWebpAlphaChunk(const uint8_t* ptr, size_t len, uint32_t w, uint32_t h, std::vector<uint8_t>& outAlpha) {
    if (!ptr || len < 1 || w == 0 || h == 0) return false;
    uint8_t hdr = ptr[0];
    uint8_t comp = hdr & 0x03u;
    uint8_t filt = (hdr >> 2u) & 0x03u;
    uint8_t pre  = (hdr >> 4u) & 0x03u;
    if ((hdr & 0xC0u) != 0u || comp > 1u || pre > 1u) return false;

    const size_t nPx = static_cast<size_t>(w) * h;
    outAlpha.assign(nPx, 255u);
    if (comp == 0u) {
        if (len - 1u < nPx) return false;
        std::memcpy(outAlpha.data(), ptr + 1, nPx);
    } else {
        // Synthesize 5-byte VP8L header + raw lossless stream (RFC 9649 Section 2.7.2: ALPH comp=1 is a headerless VP8L ImageStream!)
        DetailBrotli::BitReader br(ptr + 1, len - 1);
        std::vector<uint32_t> argb;
        if (!DecodeVp8lImageStream(br, w, h, true, 0, argb) || argb.size() != nPx) return false;
        for (size_t i = 0; i < nPx; ++i) outAlpha[i] = static_cast<uint8_t>((argb[i] >> 8u) & 0xFFu); // green channel!
    }

    if (filt == 1u) { // Horizontal
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t pred = (y == 0u) ? 0u : outAlpha[static_cast<size_t>(y - 1u) * w];
            outAlpha[static_cast<size_t>(y) * w] = static_cast<uint8_t>(outAlpha[static_cast<size_t>(y) * w] + pred);
            for (uint32_t x = 1u; x < w; ++x) {
                size_t i = static_cast<size_t>(y) * w + x;
                outAlpha[i] = static_cast<uint8_t>(outAlpha[i] + outAlpha[i - 1u]);
            }
        }
    } else if (filt == 2u) { // Vertical
        for (uint32_t x = 1u; x < w; ++x) outAlpha[x] = static_cast<uint8_t>(outAlpha[x] + outAlpha[x - 1u]);
        for (uint32_t y = 1u; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(y) * w + x;
                outAlpha[i] = static_cast<uint8_t>(outAlpha[i] + outAlpha[i - w]);
            }
        }
    } else if (filt == 3u) { // Gradient
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(y) * w + x;
                uint8_t pred = 0u;
                if (y == 0u && x > 0u) pred = outAlpha[i - 1u];
                else if (y > 0u && x == 0u) pred = outAlpha[i - w];
                else if (y > 0u && x > 0u) {
                    int g = static_cast<int>(outAlpha[i - 1u]) + static_cast<int>(outAlpha[i - w]) - static_cast<int>(outAlpha[i - w - 1u]);
                    pred = static_cast<uint8_t>(std::clamp(g, 0, 255));
                }
                outAlpha[i] = static_cast<uint8_t>(outAlpha[i] + pred);
            }
        }
    }
    return true;
}

// RFC 6386 VP8 BoolDecoder & Baseline Keyframe Intra Decoder (for 'VP8 ' lossy chunks without needing WIC!)
struct Vp8BoolDecoder {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;
    uint32_t range = 255;
    uint32_t value = 0;
    int bitCount = 0;

    void Init(const uint8_t* b, size_t n) {
        buf = b; len = n; pos = 0; range = 255u;
        uint32_t b0 = (pos < len) ? buf[pos++] : 0u;
        uint32_t b1 = (pos < len) ? buf[pos++] : 0u;
        value = (b0 << 8u) | b1;
        bitCount = 0;
    }

    uint32_t ReadBool(uint8_t prob) {
        uint32_t split = 1u + (((range - 1u) * prob) >> 8u);
        uint32_t bigSplit = split << 8u;
        uint32_t bit = 0u;
        if (value >= bigSplit) {
            range -= split;
            value -= bigSplit;
            bit = 1u;
        } else {
            range = split;
        }
        while (range < 128u) {
            value <<= 1u;
            range <<= 1u;
            if (++bitCount == 8) {
                bitCount = 0;
                value |= (pos < len) ? buf[pos++] : 0u;
            }
        }
        return bit;
    }

    uint32_t ReadLit(int n) {
        uint32_t v = 0u;
        for (int i = 0; i < n; ++i) v = (v << 1u) | ReadBool(128u);
        return v;
    }
    int32_t ReadSignedLit(int n) {
        int32_t v = static_cast<int32_t>(ReadLit(n));
        return ReadBool(128u) ? -v : v;
    }
};

inline bool DecodeVp8LossyKeyframe(const uint8_t* ptr, size_t len, uint32_t& outW, uint32_t& outH, std::vector<uint8_t>& outRgb) {
    if (!ptr || len < 10) return false;
    uint32_t tag = static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8) | (static_cast<uint32_t>(ptr[2]) << 16);
    if ((tag & 1u) != 0u) return false; // Must be keyframe (0)
    uint32_t firstPartLen = tag >> 5u;
    if (ptr[3] != 0x9Du || ptr[4] != 0x01u || ptr[5] != 0x2Au) return false;
    outW = Rd16(ptr + 6, false) & 0x3FFFu;
    outH = Rd16(ptr + 8, false) & 0x3FFFu;
    if (outW == 0 || outH == 0 || outW > 16384 || outH > 16384) return false;
    if (10u + firstPartLen > len) return false;

    Vp8BoolDecoder bd0;
    bd0.Init(ptr + 10, firstPartLen);
    (void)bd0.ReadBool(128); // color_space
    (void)bd0.ReadBool(128); // clamping_type
    bool segEnabled = bd0.ReadBool(128) != 0u;
    bool updateMbSegMap = false;
    bool segAbsMode = false;
    bool segHasQuant[4] = { false, false, false, false };
    int  segQuantVal[4] = { 0, 0, 0, 0 };
    uint8_t segTreeProbs[3] = { 255u, 255u, 255u };
    if (segEnabled) {
        updateMbSegMap = bd0.ReadBool(128) != 0u;
        bool updateSegFeatureData = bd0.ReadBool(128) != 0u;
        if (updateSegFeatureData) {
            segAbsMode = bd0.ReadBool(128) != 0u; // 0 = delta, 1 = absolute value
            for (int s = 0; s < 4; ++s) {
                if (bd0.ReadBool(128) != 0u) {
                    segHasQuant[s] = true;
                    segQuantVal[s] = bd0.ReadSignedLit(7);
                }
            }
            for (int s = 0; s < 4; ++s) {
                if (bd0.ReadBool(128) != 0u) {
                    (void)bd0.ReadSignedLit(6); // loop_filter_level feature
                }
            }
        }
        if (updateMbSegMap) {
            for (int s = 0; s < 3; ++s) {
                segTreeProbs[s] = bd0.ReadBool(128) ? static_cast<uint8_t>(bd0.ReadLit(8)) : 255u;
            }
        }
    }
    (void)bd0.ReadBool(128);      // filter_type
    (void)bd0.ReadLit(6);         // loop_filter_level
    (void)bd0.ReadLit(3);         // sharpness_level
    if (bd0.ReadBool(128) != 0u) { // loop_filter_adj_enable
        if (bd0.ReadBool(128) != 0u) {
            for (int i = 0; i < 8; ++i) {
                if (bd0.ReadBool(128) != 0u) (void)bd0.ReadSignedLit(6);
            }
        }
    }
    uint32_t log2NbrPartitions = bd0.ReadLit(2);
    uint32_t numParts = 1u << log2NbrPartitions; // 1, 2, 4, or 8 token partitions

    // Quantizer indices (RFC 6386 Section 9.6)
    static constexpr int16_t kDcQ[128] = {
        4,5,6,7,8,9,10,10,11,12,13,14,15,16,17,17,18,19,20,20,21,21,22,22,23,23,24,25,25,26,27,28,
        29,30,31,32,33,34,35,36,37,37,38,39,40,41,42,43,44,45,46,46,47,48,49,50,51,52,53,54,55,56,57,58,
        59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,76,77,78,79,80,81,82,83,84,85,86,87,88,89,
        91,93,95,96,98,100,101,102,104,106,108,110,112,114,116,118,122,124,126,128,130,132,134,136,138,140,143,145,148,151,154,157
    };
    static constexpr int16_t kAcQ[128] = {
        4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,
        36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,60,62,64,66,68,70,72,74,76,
        78,80,82,84,86,88,90,92,94,96,98,100,102,104,106,108,110,112,114,116,119,122,125,128,131,134,137,140,143,146,149,152,
        155,158,161,164,167,170,173,177,181,185,189,193,197,201,205,209,213,217,221,225,229,234,239,245,249,254,259,264,269,274,279,284
    };
    int yAcQi = static_cast<int>(bd0.ReadLit(7));
    int yDcDelta  = bd0.ReadBool(128) ? bd0.ReadSignedLit(4) : 0;
    int y2DcDelta = bd0.ReadBool(128) ? bd0.ReadSignedLit(4) : 0;
    int y2AcDelta = bd0.ReadBool(128) ? bd0.ReadSignedLit(4) : 0;
    int uvDcDelta = bd0.ReadBool(128) ? bd0.ReadSignedLit(4) : 0;
    int uvAcDelta = bd0.ReadBool(128) ? bd0.ReadSignedLit(4) : 0;
    (void)bd0.ReadBool(128); // refresh_entropy_probs

    struct Vp8QuantFactors {
        int16_t yDcQ = 4, yAcQ = 4, y2DcQ = 8, y2AcQ = 8, uvDcQ = 4, uvAcQ = 4;
    };
    Vp8QuantFactors segQuant[4];
    for (int s = 0; s < 4; ++s) {
        int qi = yAcQi;
        if (segEnabled && segHasQuant[s]) {
            qi = segAbsMode ? segQuantVal[s] : (yAcQi + segQuantVal[s]);
        }
        qi = std::clamp(qi, 0, 127);
        segQuant[s].yDcQ  = kDcQ[std::clamp(qi + yDcDelta, 0, 127)];
        segQuant[s].yAcQ  = kAcQ[qi];
        segQuant[s].y2DcQ = static_cast<int16_t>(kDcQ[std::clamp(qi + y2DcDelta, 0, 127)] * 2);
        segQuant[s].y2AcQ = static_cast<int16_t>(std::max<int>(8, (kAcQ[std::clamp(qi + y2AcDelta, 0, 127)] * 155) / 100));
        segQuant[s].uvDcQ = std::min<int16_t>(132, kDcQ[std::clamp(qi + uvDcDelta, 0, 127)]);
        segQuant[s].uvAcQ = kAcQ[std::clamp(qi + uvAcDelta, 0, 127)];
    }

    // Default keyframe token probabilities (RFC 6386 Section 13.4)
    static constexpr uint8_t kDefCoeffProbs[4][8][3][11] = {
        { // type 0: Y after Y2
          {{128,128,128,128,128,128,128,128,128,128,128},{128,128,128,128,128,128,128,128,128,128,128},{128,128,128,128,128,128,128,128,128,128,128}},
          {{253,136,254,255,228,219,128,128,128,128,128},{189,129,242,255,227,213,254,219,128,128,128},{106,126,227,252,214,209,254,253,128,128,128}},
          {{1,98,248,255,236,226,255,255,128,128,128},{181,133,238,254,221,234,255,154,128,128,128},{78,134,202,247,198,180,255,219,128,128,128}},
          {{1,185,249,255,243,255,128,128,128,128,128},{184,150,247,255,236,224,128,128,128,128,128},{77,110,216,255,236,230,128,128,128,128,128}},
          {{1,101,251,255,241,255,128,128,128,128,128},{170,139,241,252,236,209,255,255,128,128,128},{37,116,196,243,228,255,255,255,128,128,128}},
          {{1,204,254,255,245,255,128,128,128,128,128},{207,160,250,255,238,128,128,128,128,128,128},{102,103,231,255,211,171,128,128,128,128,128}},
          {{1,152,252,255,240,255,128,128,128,128,128},{177,135,243,255,234,225,128,128,128,128,128},{80,129,211,255,194,224,128,128,128,128,128}},
          {{1,1,255,128,128,128,128,128,128,128,128},{246,1,255,128,128,128,128,128,128,128,128},{255,128,128,128,128,128,128,128,128,128,128}}
        },
        { // type 1: Y2 (WHT)
          {{198,35,237,223,193,187,162,160,145,155,62},{131,45,198,221,172,176,220,157,252,221,1},{68,47,146,208,149,167,221,162,255,223,128}},
          {{1,149,241,255,221,224,255,255,128,128,128},{184,141,234,253,222,220,255,199,128,128,128},{81,99,181,242,176,190,249,202,255,255,128}},
          {{1,129,232,253,214,197,242,196,255,255,128},{99,121,210,250,201,198,255,202,128,128,128},{23,91,163,242,170,187,247,210,255,255,128}},
          {{1,200,246,255,234,255,128,128,128,128,128},{109,178,241,255,231,245,255,255,128,128,128},{44,130,201,253,205,192,255,255,128,128,128}},
          {{1,132,239,251,219,209,255,165,128,128,128},{94,136,225,251,218,190,255,255,128,128,128},{22,100,174,245,186,161,255,199,128,128,128}},
          {{1,182,249,255,232,235,128,128,128,128,128},{124,143,241,255,227,234,128,128,128,128,128},{35,77,181,251,193,211,255,205,128,128,128}},
          {{1,157,247,255,236,231,255,255,128,128,128},{121,141,235,255,225,227,255,255,128,128,128},{45,99,188,251,195,217,255,224,128,128,128}},
          {{1,1,251,255,213,255,128,128,128,128,128},{203,1,248,255,255,128,128,128,128,128,128},{137,1,177,255,224,255,128,128,128,128,128}}
        },
        { // type 2: UV
          {{253,9,248,251,207,208,255,192,128,128,128},{175,13,224,243,193,185,249,198,255,255,128},{73,17,171,221,161,179,236,167,255,234,128}},
          {{1,95,247,253,212,183,255,255,128,128,128},{239,90,244,250,211,209,255,255,128,128,128},{155,77,195,248,188,195,255,255,128,128,128}},
          {{1,24,239,251,218,219,255,205,128,128,128},{201,51,219,255,196,186,128,128,128,128,128},{69,46,190,239,201,218,255,228,128,128,128}},
          {{1,191,251,255,255,128,128,128,128,128,128},{223,165,249,255,213,255,128,128,128,128,128},{141,124,248,255,255,128,128,128,128,128,128}},
          {{1,16,248,255,255,128,128,128,128,128,128},{190,36,230,255,236,255,128,128,128,128,128},{149,1,255,128,128,128,128,128,128,128,128}},
          {{1,226,255,128,128,128,128,128,128,128,128},{247,192,255,128,128,128,128,128,128,128,128},{240,128,255,128,128,128,128,128,128,128,128}},
          {{1,134,252,255,255,128,128,128,128,128,128},{213,62,250,255,255,128,128,128,128,128,128},{55,93,255,128,128,128,128,128,128,128,128}},
          {{128,128,128,128,128,128,128,128,128,128,128},{128,128,128,128,128,128,128,128,128,128,128},{128,128,128,128,128,128,128,128,128,128,128}}
        },
        { // type 3: Y full (B_PRED)
          {{202,24,213,235,186,191,220,160,240,175,255},{126,38,182,232,169,184,228,174,255,187,128},{61,46,138,219,151,178,240,170,255,216,128}},
          {{1,112,230,250,199,191,247,159,255,255,128},{166,109,228,252,211,215,255,174,128,128,128},{39,77,162,232,172,180,245,178,255,255,128}},
          {{1,52,220,246,198,199,249,220,255,255,128},{124,74,191,243,183,193,250,221,255,255,128},{24,71,130,219,154,170,243,182,255,255,128}},
          {{1,182,225,249,219,240,255,224,128,128,128},{149,150,226,252,216,205,255,171,128,128,128},{28,108,170,242,183,194,254,223,255,255,128}},
          {{1,81,230,252,204,203,255,192,128,128,128},{123,102,209,247,188,196,255,233,128,128,128},{20,95,153,243,164,173,255,203,128,128,128}},
          {{1,222,248,255,216,213,128,128,128,128,128},{168,175,246,252,235,205,255,255,128,128,128},{47,116,215,255,211,212,255,255,128,128,128}},
          {{1,121,236,253,212,214,255,255,128,128,128},{141,84,213,252,201,202,255,219,128,128,128},{42,80,160,240,162,185,255,205,128,128,128}},
          {{1,1,255,128,128,128,128,128,128,128,128},{244,1,255,128,128,128,128,128,128,128,128},{238,1,255,128,128,128,128,128,128,128,128}}
        }
    };
    uint8_t probs[4][8][3][11];
    std::memcpy(probs, kDefCoeffProbs, sizeof(probs));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 11; ++l)
                    if (bd0.ReadBool(252)) probs[i][j][k][l] = static_cast<uint8_t>(bd0.ReadLit(8));

    bool mbNoSkipCoeff = bd0.ReadBool(128) != 0u;
    uint8_t probSkipFalse = mbNoSkipCoeff ? static_cast<uint8_t>(bd0.ReadLit(8)) : 0u;

    // Multi-token partitions (RFC 6386 Section 9.5): (numParts - 1) 24-bit LE sizes follow Partition 0
    size_t partSizeTblOff = 10u + static_cast<size_t>(firstPartLen);
    if (partSizeTblOff + 3u * static_cast<size_t>(numParts - 1u) > len) return false;
    size_t curPartOff = partSizeTblOff + 3u * static_cast<size_t>(numParts - 1u);
    Vp8BoolDecoder partDecoders[8];
    for (uint32_t p = 0; p < numParts; ++p) {
        size_t pSize = 0;
        if (p + 1u < numParts) {
            const uint8_t* ps = ptr + partSizeTblOff + static_cast<size_t>(p) * 3u;
            pSize = static_cast<size_t>(ps[0]) |
                    (static_cast<size_t>(ps[1]) << 8) |
                    (static_cast<size_t>(ps[2]) << 16);
            if (curPartOff + pSize > len) return false;
        } else {
            pSize = len - curPartOff;
        }
        partDecoders[p].Init(ptr + curPartOff, pSize);
        curPartOff += pSize;
    }

    // RFC 6386 Section 11.3 & 11.5: Keyframe 4x4 subblock mode tree & context probability table
    static constexpr int8_t kVp8BmodeTree[18] = {
        0, 2, -1, 4, -2, 6, 8, 12, -3, 10, -5, -6, -4, 14, -7, 16, -8, -9
    };
    static constexpr uint8_t kVp8KfBmodeProbs[10][10][9] = {
        { {231,120, 48, 89,115,113,120,152,112}, {152,179, 64,126,170,118, 46, 70, 95},
          {175, 69,143, 80, 85, 82, 72,155,103}, { 56, 58, 10,171,218,189, 17, 13,152},
          {144, 71, 10, 38,171,213,144, 34, 26}, {114, 26, 17,163, 44,195, 21, 10,173},
          {121, 24, 80,195, 26, 62, 44, 64, 85}, {170, 46, 55, 19,136,160, 33,206, 71},
          { 63, 20,  8,114,114,208, 12,  9,226}, { 81, 40, 11, 96,182, 84, 29, 16, 36} },
        { {134,183, 89,137, 98,101,106,165,148}, { 72,187,100,130,157,111, 32, 75, 80},
          { 66,102,167, 99, 74, 62, 40,234,128}, { 41, 53,  9,178,241,141, 26,  8,107},
          {104, 79, 12, 27,217,255, 87, 17,  7}, { 74, 43, 26,146, 73,166, 49, 23,157},
          { 65, 38,105,160, 51, 52, 31,115,128}, { 87, 68, 71, 44,114, 51, 15,186, 23},
          { 47, 41, 14,110,182,183, 21, 17,194}, { 66, 45, 25,102,197,189, 23, 18, 22} },
        { { 88, 88,147,150, 42, 46, 45,196,205}, { 43, 97,183,117, 85, 38, 35,179, 61},
          { 39, 53,200, 87, 26, 21, 43,232,171}, { 56, 34, 51,104,114,102, 29, 93, 77},
          {107, 54, 32, 26, 51,  1, 81, 43, 31}, { 39, 28, 85,171, 58,165, 90, 98, 64},
          { 34, 22,116,206, 23, 34, 43,166, 73}, { 68, 25,106, 22, 64,171, 36,225,114},
          { 34, 19, 21,102,132,188, 16, 76,124}, { 62, 18, 78, 95, 85, 57, 50, 48, 51} },
        { {193,101, 35,159,215,111, 89, 46,111}, { 60,148, 31,172,219,228, 21, 18,111},
          {112,113, 77, 85,179,255, 38,120,114}, { 40, 42,  1,196,245,209, 10, 25,109},
          {100, 80,  8, 43,154,  1, 51, 26, 71}, { 88, 43, 29,140,166,213, 37, 43,154},
          { 61, 63, 30,155, 67, 45, 68,  1,209}, {142, 78, 78, 16,255,128, 34,197,171},
          { 41, 40,  5,102,211,183,  4,  1,221}, { 51, 50, 17,168,209,192, 23, 25, 82} },
        { {125, 98, 42, 88,104, 85,117,175, 82}, { 95, 84, 53, 89,128,100,113,101, 45},
          { 75, 79,123, 47, 51,128, 81,171,  1}, { 57, 17,  5, 71,102, 57, 53, 41, 49},
          {115, 21,  2, 10,102,255,166, 23,  6}, { 38, 33, 13,121, 57, 73, 26,  1, 85},
          { 41, 10, 67,138, 77,110, 90, 47,114}, {101, 29, 16, 10, 85,128,101,196, 26},
          { 57, 18, 10,102,102,213, 34, 20, 43}, {117, 20, 15, 36,163,128, 68,  1, 26} },
        { {138, 31, 36,171, 27,166, 38, 44,229}, { 67, 87, 58,169, 82,115, 26, 59,179},
          { 63, 59, 90,180, 59,166, 93, 73,154}, { 40, 40, 21,116,143,209, 34, 39,175},
          { 57, 46, 22, 24,128,  1, 54, 17, 37}, { 47, 15, 16,183, 34,223, 49, 45,183},
          { 46, 17, 33,183,  6, 98, 15, 32,183}, { 65, 32, 73,115, 28,128, 23,128,205},
          { 40,  3,  9,115, 51,192, 18,  6,223}, { 87, 37,  9,115, 59, 77, 64, 21, 47} },
        { {104, 55, 44,218,  9, 54, 53,130,226}, { 64, 90, 70,205, 40, 41, 23, 26, 57},
          { 54, 57,112,184,  5, 41, 38,166,213}, { 30, 34, 26,133,152,116, 10, 32,134},
          { 75, 32, 12, 51,192,255,160, 43, 51}, { 39, 19, 53,221, 26,114, 32, 73,255},
          { 31,  9, 65,234,  2, 15,  1,118, 73}, { 88, 31, 35, 67,102, 85, 55,186, 85},
          { 56, 21, 23,111, 59,205, 45, 37,192}, { 55, 38, 70,124, 73,102,  1, 34, 98} },
        { {102, 61, 71, 37, 34, 53, 31,243,192}, { 69, 60, 71, 38, 73,119, 28,222, 37},
          { 68, 45,128, 34,  1, 47, 11,245,171}, { 62, 17, 19, 70,146, 85, 55, 62, 70},
          { 75, 15,  9,  9, 64,255,184,119, 16}, { 37, 43, 37,154,100,163, 85,160,  1},
          { 63,  9, 92,136, 28, 64, 32,201, 85}, { 86,  6, 28,  5, 64,255, 25,248,  1},
          { 56,  8, 17,132,137,255, 55,116,128}, { 58, 15, 20, 82,135, 57, 26,121, 40} },
        { {164, 50, 31,137,154,133, 25, 35,218}, { 51,103, 44,131,131,123, 31,  6,158},
          { 86, 40, 64,135,148,224, 45,183,128}, { 22, 26, 17,131,240,154, 14,  1,209},
          { 83, 12, 13, 54,192,255, 68, 47, 28}, { 45, 16, 21, 91, 64,222,  7,  1,197},
          { 56, 21, 39,155, 60,138, 23,102,213}, { 85, 26, 85, 85,128,128, 32,146,171},
          { 18, 11,  7, 63,144,171,  4,  4,246}, { 35, 27, 10,146,174,171, 12, 26,128} },
        { {190, 80, 35, 99,180, 80,126, 54, 45}, { 85,126, 47, 87,176, 51, 41, 20, 32},
          {101, 75,128,139,118,146,116,128, 85}, { 56, 41, 15,176,236, 85, 37,  9, 62},
          {146, 36, 19, 30,171,255, 97, 27, 20}, { 71, 30, 17,119,118,255, 17, 18,138},
          {101, 38, 60,138, 55, 70, 43, 26,142}, {138, 45, 61, 62,219,  1, 81,188, 64},
          { 32, 41, 20,117,151,142, 20, 21,163}, {112, 19, 12, 61,195,128, 48,  4, 24} }
    };

    uint32_t mbW = (outW + 15u) / 16u;
    uint32_t mbH = (outH + 15u) / 16u;
    uint32_t yStride = mbW * 16u;
    uint32_t cStride = mbW * 8u;
    std::vector<uint8_t> yPlane(static_cast<size_t>(yStride) * (mbH * 16u), 128u);
    std::vector<uint8_t> uPlane(static_cast<size_t>(cStride) * (mbH * 8u), 128u);
    std::vector<uint8_t> vPlane(static_cast<size_t>(cStride) * (mbH * 8u), 128u);

    std::vector<uint8_t> aboveNzY(mbW * 4u, 0u), aboveNzY2(mbW, 0u), aboveNzU(mbW * 2u, 0u), aboveNzV(mbW * 2u, 0u);
    std::vector<uint8_t> aboveBMode(mbW * 4u, 0u); // B_DC_PRED = 0

    static constexpr uint8_t kBands[16] = { 0,1,2,3,6,4,5,6,6,6,6,6,6,6,6,7 };
    static constexpr uint8_t kZigzag4[16] = { 0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15 };

    auto decodeBlock = [&](Vp8BoolDecoder& tokenBd, int type, int firstIdx, int initCtx, int16_t dcQ, int16_t acQ, int16_t outBlk[16]) -> bool {
        int ctx = initCtx;
        bool hasNz = false;
        for (int i = firstIdx; i < 16; ++i) {
            const uint8_t* p = probs[type][kBands[i]][ctx];
            if (i == firstIdx || outBlk[kZigzag4[i - 1]] != 0) {
                if (!tokenBd.ReadBool(p[0])) break; // DCT_EOB
            }
            while (!tokenBd.ReadBool(p[1])) { // DCT_0
                ctx = 0;
                ++i;
                if (i >= 16) return hasNz;
                p = probs[type][kBands[i]][0];
            }
            int mag = 1;
            if (tokenBd.ReadBool(p[2])) {
                if (!tokenBd.ReadBool(p[3])) {
                    mag = tokenBd.ReadBool(p[4]) ? (3 + static_cast<int>(tokenBd.ReadBool(p[5]))) : 2;
                } else if (!tokenBd.ReadBool(p[6])) {
                    if (!tokenBd.ReadBool(p[7])) {
                        mag = 5 + static_cast<int>(tokenBd.ReadBool(159));
                    } else {
                        mag = 7 + (static_cast<int>(tokenBd.ReadBool(165)) << 1) + static_cast<int>(tokenBd.ReadBool(145));
                    }
                } else {
                    int b1 = static_cast<int>(tokenBd.ReadBool(p[8]));
                    int b0 = static_cast<int>(tokenBd.ReadBool(p[9 + b1]));
                    int cat = (b1 << 1) + b0;
                    static constexpr uint8_t kCatProbs[4][12] = {
                        {173,148,140,0},
                        {176,155,140,135,0},
                        {180,157,141,134,130,0},
                        {254,254,243,230,196,177,153,140,133,130,129,0}
                    };
                    static constexpr int kCatBase[4] = { 11, 19, 35, 67 };
                    int extra = 0;
                    for (int k = 0; kCatProbs[cat][k] != 0; ++k) {
                        extra = (extra << 1) | static_cast<int>(tokenBd.ReadBool(kCatProbs[cat][k]));
                    }
                    mag = kCatBase[cat] + extra;
                }
            }
            ctx = (mag == 1) ? 1 : 2;
            int sign = tokenBd.ReadBool(128) ? -mag : mag;
            outBlk[kZigzag4[i]] = static_cast<int16_t>(sign * ((i == 0) ? dcQ : acQ));
            hasNz = true;
        }
        return hasNz;
    };

    auto iwht4x4 = [](const int16_t in[16], int16_t out[16]) {
        int tmp[16];
        for (int i = 0; i < 4; ++i) {
            int a = in[i] + in[12 + i], b = in[4 + i] + in[8 + i];
            int c = in[4 + i] - in[8 + i], d = in[i] - in[12 + i];
            tmp[i] = a + b; tmp[4 + i] = c + d; tmp[8 + i] = a - b; tmp[12 + i] = d - c;
        }
        for (int i = 0; i < 4; ++i) {
            const int* r = &tmp[i * 4];
            int a = r[0] + r[3] + 3, b = r[1] + r[2];
            int c = r[1] - r[2],     d = r[0] - r[3] + 3;
            out[i * 4 + 0] = static_cast<int16_t>((a + b) >> 3);
            out[i * 4 + 1] = static_cast<int16_t>((c + d) >> 3);
            out[i * 4 + 2] = static_cast<int16_t>((a - b) >> 3);
            out[i * 4 + 3] = static_cast<int16_t>((d - c) >> 3);
        }
    };

    auto idct4x4Add = [](const int16_t in[16], uint8_t* dst, uint32_t stride) {
        int tmp[16];
        for (int i = 0; i < 4; ++i) {
            int a = in[i] + in[8 + i], b = in[i] - in[8 + i];
            int c = ((in[4 + i] * 35468) >> 16) - (in[12 + i] + ((in[12 + i] * 20091) >> 16));
            int d = (in[4 + i] + ((in[4 + i] * 20091) >> 16)) + ((in[12 + i] * 35468) >> 16);
            tmp[i] = a + d; tmp[4 + i] = b + c; tmp[8 + i] = b - c; tmp[12 + i] = a - d;
        }
        for (int y = 0; y < 4; ++y) {
            const int* r = &tmp[y * 4];
            int a = r[0] + r[2], b = r[0] - r[2];
            int c = ((r[1] * 35468) >> 16) - (r[3] + ((r[3] * 20091) >> 16));
            int d = (r[1] + ((r[1] * 20091) >> 16)) + ((r[3] * 35468) >> 16);
            int p[4] = { (a + d + 4) >> 3, (b + c + 4) >> 3, (b - c + 4) >> 3, (a - d + 4) >> 3 };
            for (int x = 0; x < 4; ++x) {
                dst[y * stride + x] = static_cast<uint8_t>(std::clamp(static_cast<int>(dst[y * stride + x]) + p[x], 0, 255));
            }
        }
    };

    auto pred16Or8 = [](uint8_t* pl, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t dim, int mode) {
        if (mode == 0) { // DC_PRED
            int sum = 0, cnt = 0;
            if (y0 > 0) { for (uint32_t x = 0; x < dim; ++x) sum += pl[(y0 - 1u) * stride + x0 + x]; cnt += static_cast<int>(dim); }
            if (x0 > 0) { for (uint32_t y = 0; y < dim; ++y) sum += pl[(y0 + y) * stride + x0 - 1u]; cnt += static_cast<int>(dim); }
            uint8_t dc = (cnt > 0) ? static_cast<uint8_t>((sum + (cnt >> 1)) / cnt) : 128u;
            for (uint32_t y = 0; y < dim; ++y) std::memset(&pl[(y0 + y) * stride + x0], dc, dim);
        } else if (mode == 1) { // V_PRED
            uint8_t def = 127u;
            for (uint32_t y = 0; y < dim; ++y) {
                for (uint32_t x = 0; x < dim; ++x) {
                    pl[(y0 + y) * stride + x0 + x] = (y0 > 0) ? pl[(y0 - 1u) * stride + x0 + x] : def;
                }
            }
        } else if (mode == 2) { // H_PRED
            uint8_t def = 129u;
            for (uint32_t y = 0; y < dim; ++y) {
                uint8_t lv = (x0 > 0) ? pl[(y0 + y) * stride + x0 - 1u] : def;
                std::memset(&pl[(y0 + y) * stride + x0], lv, dim);
            }
        } else { // TM_PRED
            int tl = (x0 > 0 && y0 > 0) ? pl[(y0 - 1u) * stride + x0 - 1u] : (y0 > 0 ? 129 : 127);
            for (uint32_t y = 0; y < dim; ++y) {
                int lv = (x0 > 0) ? pl[(y0 + y) * stride + x0 - 1u] : 129;
                for (uint32_t x = 0; x < dim; ++x) {
                    int tv = (y0 > 0) ? pl[(y0 - 1u) * stride + x0 + x] : 127;
                    pl[(y0 + y) * stride + x0 + x] = static_cast<uint8_t>(std::clamp(lv + tv - tl, 0, 255));
                }
            }
        }
    };

    // RFC 6386 Section 12.3: Exact 4x4 Luma Subblock Intra Prediction (all 10 modes B_DC_PRED .. B_HU_PRED)
    auto pred4x4 = [&](uint32_t mby, uint32_t mbx, int sbY, int sbX, uint8_t mode) {
        const uint32_t x0 = mbx * 16u + static_cast<uint32_t>(sbX) * 4u;
        const uint32_t y0 = mby * 16u + static_cast<uint32_t>(sbY) * 4u;
        int A[8];
        int L[4];
        int P = (x0 > 0u && y0 > 0u)
            ? static_cast<int>(yPlane[(y0 - 1u) * yStride + x0 - 1u])
            : ((y0 > 0u) ? 129 : 127);
        for (int i = 0; i < 4; ++i) {
            A[i] = (y0 > 0u) ? static_cast<int>(yPlane[(y0 - 1u) * yStride + x0 + static_cast<uint32_t>(i)]) : 127;
            L[i] = (x0 > 0u) ? static_cast<int>(yPlane[(y0 + static_cast<uint32_t>(i)) * yStride + x0 - 1u]) : 129;
        }
        // Extra top-right pixels A[4..7] (RFC 6386 Section 12.3 right-edge subblocks 3, 7, 11, 15 rule)
        if (sbX == 3) {
            if (mby == 0u) {
                A[4] = A[5] = A[6] = A[7] = 127;
            } else if (mbx + 1u < mbW) {
                uint32_t mbTopRow = mby * 16u - 1u;
                for (int i = 0; i < 4; ++i) {
                    A[4 + i] = yPlane[mbTopRow * yStride + mbx * 16u + 16u + static_cast<uint32_t>(i)];
                }
            } else {
                int edgePx = yPlane[(mby * 16u - 1u) * yStride + mbW * 16u - 1u];
                A[4] = A[5] = A[6] = A[7] = edgePx;
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                A[4 + i] = (y0 > 0u) ? static_cast<int>(yPlane[(y0 - 1u) * yStride + x0 + 4u + static_cast<uint32_t>(i)]) : 127;
            }
        }

        auto avg2 = [](int x, int y) -> uint8_t { return static_cast<uint8_t>((x + y + 1) >> 1); };
        auto avg3 = [](int x, int y, int z) -> uint8_t { return static_cast<uint8_t>((x + y + y + z + 2) >> 2); };

        uint8_t B[4][4] = {};
        const int E[9] = { L[3], L[2], L[1], L[0], P, A[0], A[1], A[2], A[3] };
        switch (mode) {
        case 0: { // B_DC_PRED
            int v = 4;
            for (int i = 0; i < 4; ++i) v += A[i] + L[i];
            uint8_t dc = static_cast<uint8_t>(v >> 3);
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) B[r][c] = dc;
            break;
        }
        case 1: { // B_TM_PRED
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    B[r][c] = static_cast<uint8_t>(std::clamp(L[r] + A[c] - P, 0, 255));
            break;
        }
        case 2: { // B_VE_PRED
            for (int c = 0; c < 4; ++c) {
                uint8_t v = avg3((c == 0) ? P : A[c - 1], A[c], A[c + 1]);
                B[0][c] = B[1][c] = B[2][c] = B[3][c] = v;
            }
            break;
        }
        case 3: { // B_HE_PRED
            for (int r = 0; r < 4; ++r) {
                uint8_t v = (r == 3) ? avg3(L[2], L[3], L[3]) : avg3((r == 0) ? P : L[r - 1], L[r], L[r + 1]);
                B[r][0] = B[r][1] = B[r][2] = B[r][3] = v;
            }
            break;
        }
        case 4: { // B_LD_PRED
            B[0][0] = avg3(A[0], A[1], A[2]);
            B[0][1] = B[1][0] = avg3(A[1], A[2], A[3]);
            B[0][2] = B[1][1] = B[2][0] = avg3(A[2], A[3], A[4]);
            B[0][3] = B[1][2] = B[2][1] = B[3][0] = avg3(A[3], A[4], A[5]);
            B[1][3] = B[2][2] = B[3][1] = avg3(A[4], A[5], A[6]);
            B[2][3] = B[3][2] = avg3(A[5], A[6], A[7]);
            B[3][3] = avg3(A[6], A[7], A[7]);
            break;
        }
        case 5: { // B_RD_PRED
            B[3][0] = avg3(E[0], E[1], E[2]);
            B[3][1] = B[2][0] = avg3(E[1], E[2], E[3]);
            B[3][2] = B[2][1] = B[1][0] = avg3(E[2], E[3], E[4]);
            B[3][3] = B[2][2] = B[1][1] = B[0][0] = avg3(E[3], E[4], E[5]);
            B[2][3] = B[1][2] = B[0][1] = avg3(E[4], E[5], E[6]);
            B[1][3] = B[0][2] = avg3(E[5], E[6], E[7]);
            B[0][3] = avg3(E[6], E[7], E[8]);
            break;
        }
        case 6: { // B_VR_PRED
            B[3][0] = avg3(E[1], E[2], E[3]);
            B[2][0] = avg3(E[2], E[3], E[4]);
            B[3][1] = B[1][0] = avg3(E[3], E[4], E[5]);
            B[2][1] = B[0][0] = avg2(E[4], E[5]);
            B[3][2] = B[1][1] = avg3(E[4], E[5], E[6]);
            B[2][2] = B[0][1] = avg2(E[5], E[6]);
            B[3][3] = B[1][2] = avg3(E[5], E[6], E[7]);
            B[2][3] = B[0][2] = avg2(E[6], E[7]);
            B[1][3] = avg3(E[6], E[7], E[8]);
            B[0][3] = avg2(E[7], E[8]);
            break;
        }
        case 7: { // B_VL_PRED
            B[0][0] = avg2(A[0], A[1]);
            B[1][0] = avg3(A[0], A[1], A[2]);
            B[2][0] = B[0][1] = avg2(A[1], A[2]);
            B[1][1] = B[3][0] = avg3(A[1], A[2], A[3]);
            B[2][1] = B[0][2] = avg2(A[2], A[3]);
            B[3][1] = B[1][2] = avg3(A[2], A[3], A[4]);
            B[2][2] = B[0][3] = avg2(A[3], A[4]);
            B[3][2] = B[1][3] = avg3(A[3], A[4], A[5]);
            B[2][3] = avg3(A[4], A[5], A[6]);
            B[3][3] = avg3(A[5], A[6], A[7]);
            break;
        }
        case 8: { // B_HD_PRED
            B[3][0] = avg2(E[0], E[1]);
            B[3][1] = avg3(E[0], E[1], E[2]);
            B[2][0] = B[3][2] = avg2(E[1], E[2]);
            B[2][1] = B[3][3] = avg3(E[1], E[2], E[3]);
            B[2][2] = B[1][0] = avg2(E[2], E[3]);
            B[2][3] = B[1][1] = avg3(E[2], E[3], E[4]);
            B[1][2] = B[0][0] = avg2(E[3], E[4]);
            B[1][3] = B[0][1] = avg3(E[3], E[4], E[5]);
            B[0][2] = avg3(E[4], E[5], E[6]);
            B[0][3] = avg3(E[5], E[6], E[7]);
            break;
        }
        default: { // 9: B_HU_PRED
            B[0][0] = avg2(L[0], L[1]);
            B[0][1] = avg3(L[0], L[1], L[2]);
            B[0][2] = B[1][0] = avg2(L[1], L[2]);
            B[0][3] = B[1][1] = avg3(L[1], L[2], L[3]);
            B[1][2] = B[2][0] = avg2(L[2], L[3]);
            B[1][3] = B[2][1] = avg3(L[2], L[3], L[3]);
            B[2][2] = B[2][3] = B[3][0] = B[3][1] = B[3][2] = B[3][3] = static_cast<uint8_t>(L[3]);
            break;
        }
        }
        for (uint32_t r = 0; r < 4u; ++r) {
            std::memcpy(&yPlane[(y0 + r) * yStride + x0], B[r], 4u);
        }
    };

    for (uint32_t mby = 0; mby < mbH; ++mby) {
        Vp8BoolDecoder& tokenBd = partDecoders[mby & (numParts - 1u)];
        uint8_t leftNzY[4] = {}, leftNzY2 = 0u, leftNzU[2] = {}, leftNzV[2] = {};
        uint8_t leftBMode[4] = { 0u, 0u, 0u, 0u }; // B_DC_PRED = 0

        for (uint32_t mbx = 0; mbx < mbW; ++mbx) {
            int segId = 0;
            if (segEnabled && updateMbSegMap) {
                if (!bd0.ReadBool(segTreeProbs[0])) {
                    segId = bd0.ReadBool(segTreeProbs[1]) ? 1 : 0;
                } else {
                    segId = bd0.ReadBool(segTreeProbs[2]) ? 3 : 2;
                }
            }
            const Vp8QuantFactors& qf = segQuant[segId];

            bool skip = mbNoSkipCoeff ? (bd0.ReadBool(probSkipFalse) != 0u) : false;

            // Keyframe YMode tree (RFC 6386 Section 11.2: probs 145, 156, 163, 128)
            int yMode = 0; // 4 = B_PRED, 0 = DC, 1 = V, 2 = H, 3 = TM
            if (!bd0.ReadBool(145)) {
                yMode = 4; // B_PRED
            } else if (!bd0.ReadBool(156)) {
                yMode = 0; // DC_PRED
            } else if (!bd0.ReadBool(163)) {
                yMode = 1; // V_PRED
            } else {
                yMode = bd0.ReadBool(128) ? 3 : 2; // TM_PRED : H_PRED
            }

            uint8_t sbModes[16] = {};
            if (yMode == 4) {
                for (int by = 0; by < 4; ++by) {
                    for (int bx = 0; bx < 4; ++bx) {
                        uint8_t aM = std::min<uint8_t>(9u, aboveBMode[mbx * 4u + static_cast<uint32_t>(bx)]);
                        uint8_t lM = std::min<uint8_t>(9u, leftBMode[by]);
                        const uint8_t* bp = kVp8KfBmodeProbs[aM][lM];
                        int ti = 0;
                        do {
                            ti = kVp8BmodeTree[ti + static_cast<int>(bd0.ReadBool(bp[ti >> 1]))];
                        } while (ti > 0);
                        uint8_t bm = static_cast<uint8_t>(-ti);
                        sbModes[by * 4 + bx] = bm;
                        aboveBMode[mbx * 4u + static_cast<uint32_t>(bx)] = bm;
                        leftBMode[by] = bm;
                    }
                }
            } else {
                // Non-B_PRED macroblocks map DC->B_DC(0), V->B_VE(2), H->B_HE(3), TM->B_TM(1)
                static constexpr uint8_t kYModeToBMode[4] = { 0u, 2u, 3u, 1u };
                uint8_t mappedBm = kYModeToBMode[yMode & 3];
                for (int k = 0; k < 4; ++k) {
                    aboveBMode[mbx * 4u + static_cast<uint32_t>(k)] = mappedBm;
                    leftBMode[k] = mappedBm;
                }
            }

            // Keyframe UVMode tree (probs 142, 114, 183)
            int uvMode = 0;
            if (!bd0.ReadBool(142)) uvMode = 0;
            else if (!bd0.ReadBool(114)) uvMode = 1;
            else uvMode = bd0.ReadBool(183) ? 3 : 2;

            pred16Or8(uPlane.data(), cStride, mbx * 8u, mby * 8u, 8u, uvMode);
            pred16Or8(vPlane.data(), cStride, mbx * 8u, mby * 8u, 8u, uvMode);

            if (yMode != 4) {
                pred16Or8(yPlane.data(), yStride, mbx * 16u, mby * 16u, 16u, yMode);
                if (skip) {
                    for (int k = 0; k < 4; ++k) { aboveNzY[mbx * 4u + static_cast<uint32_t>(k)] = 0; leftNzY[k] = 0; }
                    aboveNzY2[mbx] = 0; leftNzY2 = 0;
                    for (int k = 0; k < 2; ++k) { aboveNzU[mbx * 2u + static_cast<uint32_t>(k)] = 0; aboveNzV[mbx * 2u + static_cast<uint32_t>(k)] = 0; leftNzU[k] = 0; leftNzV[k] = 0; }
                    continue;
                }

                int16_t y2In[16] = {}, y2Out[16] = {};
                bool nz2 = decodeBlock(tokenBd, 1, 0, aboveNzY2[mbx] + leftNzY2, qf.y2DcQ, qf.y2AcQ, y2In);
                aboveNzY2[mbx] = leftNzY2 = nz2 ? 1u : 0u;
                iwht4x4(y2In, y2Out);

                for (int by = 0; by < 4; ++by) {
                    for (int bx = 0; bx < 4; ++bx) {
                        int16_t yBlk[16] = {};
                        yBlk[0] = y2Out[by * 4 + bx];
                        bool nz = decodeBlock(tokenBd, 0, 1, aboveNzY[mbx * 4u + static_cast<uint32_t>(bx)] + leftNzY[by], qf.yDcQ, qf.yAcQ, yBlk);
                        aboveNzY[mbx * 4u + static_cast<uint32_t>(bx)] = leftNzY[by] = nz ? 1u : 0u;
                        idct4x4Add(yBlk, &yPlane[(mby * 16u + static_cast<uint32_t>(by) * 4u) * yStride + (mbx * 16u + static_cast<uint32_t>(bx) * 4u)], yStride);
                    }
                }
            } else {
                // B_PRED: no Y2 WHT block (preserve aboveNzY2/leftNzY2 per RFC 6386 Section 13.3);
                // immediately predict + add 4x4 IDCT residual so subsequent 4x4 subblocks see reconstructed neighbors!
                for (int by = 0; by < 4; ++by) {
                    for (int bx = 0; bx < 4; ++bx) {
                        pred4x4(mby, mbx, by, bx, sbModes[by * 4 + bx]);
                        if (skip) {
                            aboveNzY[mbx * 4u + static_cast<uint32_t>(bx)] = leftNzY[by] = 0u;
                        } else {
                            int16_t yBlk[16] = {};
                            bool nz = decodeBlock(tokenBd, 3, 0, aboveNzY[mbx * 4u + static_cast<uint32_t>(bx)] + leftNzY[by], qf.yDcQ, qf.yAcQ, yBlk);
                            aboveNzY[mbx * 4u + static_cast<uint32_t>(bx)] = leftNzY[by] = nz ? 1u : 0u;
                            idct4x4Add(yBlk, &yPlane[(mby * 16u + static_cast<uint32_t>(by) * 4u) * yStride + (mbx * 16u + static_cast<uint32_t>(bx) * 4u)], yStride);
                        }
                    }
                }
                if (skip) {
                    for (int k = 0; k < 2; ++k) { aboveNzU[mbx * 2u + static_cast<uint32_t>(k)] = 0; aboveNzV[mbx * 2u + static_cast<uint32_t>(k)] = 0; leftNzU[k] = 0; leftNzV[k] = 0; }
                    continue;
                }
            }

            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    int16_t uBlk[16] = {};
                    bool nz = decodeBlock(tokenBd, 2, 0, aboveNzU[mbx * 2u + static_cast<uint32_t>(bx)] + leftNzU[by], qf.uvDcQ, qf.uvAcQ, uBlk);
                    aboveNzU[mbx * 2u + static_cast<uint32_t>(bx)] = leftNzU[by] = nz ? 1u : 0u;
                    idct4x4Add(uBlk, &uPlane[(mby * 8u + static_cast<uint32_t>(by) * 4u) * cStride + (mbx * 8u + static_cast<uint32_t>(bx) * 4u)], cStride);
                }
            }
            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    int16_t vBlk[16] = {};
                    bool nz = decodeBlock(tokenBd, 2, 0, aboveNzV[mbx * 2u + static_cast<uint32_t>(bx)] + leftNzV[by], qf.uvDcQ, qf.uvAcQ, vBlk);
                    aboveNzV[mbx * 2u + static_cast<uint32_t>(bx)] = leftNzV[by] = nz ? 1u : 0u;
                    idct4x4Add(vBlk, &vPlane[(mby * 8u + static_cast<uint32_t>(by) * 4u) * cStride + (mbx * 8u + static_cast<uint32_t>(bx) * 4u)], cStride);
                }
            }
        }
    }

    outRgb.resize(static_cast<size_t>(outW) * outH * 3u);
    for (uint32_t y = 0; y < outH; ++y) {
        for (uint32_t x = 0; x < outW; ++x) {
            int yv = yPlane[y * yStride + x];
            int u  = static_cast<int>(uPlane[(y >> 1u) * cStride + (x >> 1u)]) - 128;
            int v  = static_cast<int>(vPlane[(y >> 1u) * cStride + (x >> 1u)]) - 128;
            // ITU-R BT.601 full/video WebP YUV -> RGB
            int r = yv + ((1436 * v) >> 10);
            int g = yv - ((352 * u + 731 * v) >> 10);
            int b = yv + ((1815 * u) >> 10);
            outRgb[(static_cast<size_t>(y) * outW + x) * 3u + 0u] = static_cast<uint8_t>(std::clamp(r, 0, 255));
            outRgb[(static_cast<size_t>(y) * outW + x) * 3u + 1u] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            outRgb[(static_cast<size_t>(y) * outW + x) * 3u + 2u] = static_cast<uint8_t>(std::clamp(b, 0, 255));
        }
    }
    return true;
}

inline bool DecodeWebpPureCpp(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 20) return false;
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WEBP", 4) != 0) return false;
    uint32_t riffLen = Rd32(data + 4, false);
    if (riffLen < 12u || 8ull + riffLen > size + 4ull) {
        if (size < 20) return false;
    }
    size_t limit = std::min<size_t>(size, 8ull + riffLen);

    const uint8_t* vp8lPtr = nullptr; size_t vp8lLen = 0;
    const uint8_t* vp8Ptr  = nullptr; size_t vp8Len  = 0;
    const uint8_t* alphPtr = nullptr; size_t alphLen = 0;

    auto scanChunks = [&](const uint8_t* base, size_t total, auto& self) -> bool {
        size_t pos = 0;
        while (pos + 8u <= total) {
            char tag[5] = {};
            std::memcpy(tag, base + pos, 4);
            uint32_t cLen = Rd32(base + pos + 4, false);
            if (cLen > total - (pos + 8u)) return false;
            const uint8_t* pay = base + pos + 8u;
            if (std::strcmp(tag, "VP8L") == 0 && !vp8lPtr && !vp8Ptr) {
                vp8lPtr = pay; vp8lLen = cLen;
            } else if (std::strcmp(tag, "VP8 ") == 0 && !vp8Ptr && !vp8lPtr) {
                vp8Ptr = pay; vp8Len = cLen;
            } else if (std::strcmp(tag, "ALPH") == 0 && !alphPtr) {
                alphPtr = pay; alphLen = cLen;
            } else if (std::strcmp(tag, "ANMF") == 0 && cLen > 16u && !vp8lPtr && !vp8Ptr) {
                if (!self(pay + 16u, cLen - 16u, self)) return false;
            }
            pos += 8u + static_cast<size_t>(cLen) + (cLen & 1u);
        }
        return true;
    };

    if (!scanChunks(data + 12, limit - 12u, scanChunks)) return false;

    if (vp8lPtr) {
        uint32_t w = 0, h = 0;
        bool hasAlpha = false;
        std::vector<uint32_t> argb;
        if (!DecodeVp8lChunk(vp8lPtr, vp8lLen, w, h, hasAlpha, argb)) return false;
        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = hasAlpha ? 4u : 3u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = L"WebP Lossless VP8L (.webp)";
        outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);
        for (size_t i = 0; i < argb.size(); ++i) {
            uint32_t px = argb[i];
            uint8_t a = hasAlpha ? static_cast<uint8_t>((px >> 24u) & 0xFFu) : 255u;
            uint8_t r = static_cast<uint8_t>((px >> 16u) & 0xFFu);
            uint8_t g = static_cast<uint8_t>((px >> 8u)  & 0xFFu);
            uint8_t b = static_cast<uint8_t>(px & 0xFFu);
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
        outInfo.valid = true;
        return true;
    }

    if (vp8Ptr) {
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> rgb;
        if (DecodeVp8LossyKeyframe(vp8Ptr, vp8Len, w, h, rgb)) {
            std::vector<uint8_t> alpha;
            bool hasAlpha = false;
            if (alphPtr) {
                if (!DecodeWebpAlphaChunk(alphPtr, alphLen, w, h, alpha)) return false;
                hasAlpha = true;
            }
            outInfo.width = w;
            outInfo.height = h;
            outInfo.origWidth = w;
            outInfo.origHeight = h;
            outInfo.channels = hasAlpha ? 4u : 3u;
            outInfo.bitsPerChannel = 8u;
            outInfo.formatName = hasAlpha ? L"WebP Lossy + ALPH (.webp)" : L"WebP Lossy VP8 (.webp)";
            outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
                uint8_t a = hasAlpha ? alpha[i] : 255u;
                WritePremulBGRA(&outInfo.bgraPixels[i * 4u], rgb[i * 3u], rgb[i * 3u + 1u], rgb[i * 3u + 2u], a);
            }
            outInfo.valid = true;
            return true;
        }
        // Fallback to OS WIC WebP decoder for complex B_PRED / segmented VP8 streams
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"WebP Raster (.webp)", outInfo);
    }

    return false;
}


} // namespace WebpDetail

inline bool DecodeWebP(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return WebpDetail::DecodeWebpPureCpp(data, size, outInfo);
}

} // namespace Formats
