#pragma once
// NikonNef.h — Nikon Electronic Format Compressed/Lossless Huffman CFA RAW Decoder (.nef)

namespace Formats {

namespace DetailImg200 {

// ============================================================================
// 3. Nikon NEF Compressed / Lossless Huffman Sensor Unpacker (Compression == 34713)
// ============================================================================
inline void ParseNikonMakerNote(const uint8_t* mn, size_t mnLen, uint16_t bps, NikonMetaParams& out) {
    if (!mn || mnLen < 18) return;
    size_t base = 0;
    if (mnLen >= 18 && std::memcmp(mn, "Nikon\0", 6) == 0) {
        base = 10; // Embedded TIFF header starts at offset 10
    }
    if (base + 8 > mnLen) return;
    bool be = false;
    if (mn[base] == 'I' && mn[base + 1] == 'I') be = false;
    else if (mn[base] == 'M' && mn[base + 1] == 'M') be = true;
    else return;
    if (Rd16(mn + base + 2, be) != 42u) return;
    uint32_t ifdOff = Rd32(mn + base + 4, be);
    if (base + ifdOff + 2 > mnLen) return;

    const uint8_t* tiffBase = mn + base;
    size_t tiffLen = mnLen - base;
    uint16_t nEnt = Rd16(tiffBase + ifdOff, be);
    if (ifdOff + 2u + static_cast<size_t>(nEnt) * 12u > tiffLen) return;

    for (uint16_t i = 0; i < nEnt; ++i) {
        const uint8_t* ent = tiffBase + ifdOff + 2u + static_cast<size_t>(i) * 12u;
        uint16_t tag = Rd16(ent, be);
        uint16_t pt = 0; uint32_t pc = 0;
        const uint8_t* pp = GetTiffTagPayload(tiffBase, tiffLen, ent, be, pt, pc);
        if (!pp) continue;
        if (tag == 0x0093 && pc >= 1 && pt == 3) {
            out.nefComp = Rd16(pp, be);
        } else if (tag == 0x0096 && pc >= 10) {
            uint8_t v0 = pp[0], v1 = pp[1];
            if (v0 == 0x49 || v0 == 0x58 || (v0 == 0x46) || (v0 == 0x44)) {
                size_t vpOff = (v0 == 0x46) ? 2u : 2u;
                if (vpOff + 8u <= pc) {
                    out.vpred[0][0] = Rd16(pp + vpOff + 0, be);
                    out.vpred[0][1] = Rd16(pp + vpOff + 2, be);
                    out.vpred[1][0] = Rd16(pp + vpOff + 4, be);
                    out.vpred[1][1] = Rd16(pp + vpOff + 6, be);
                    out.valid = true;
                }
                (void)v1;
                if (pc >= 14 && vpOff + 10u <= pc) {
                    uint16_t cEntries = Rd16(pp + vpOff + 8, be);
                    if (cEntries > 1 && cEntries <= 4096 && vpOff + 10u + static_cast<size_t>(cEntries) * 2u <= pc) {
                        out.curveLut.resize(cEntries);
                        for (uint16_t k = 0; k < cEntries; ++k) {
                            out.curveLut[k] = Rd16(pp + vpOff + 10u + static_cast<size_t>(k) * 2u, be);
                        }
                    }
                }
            }
        }
    }
    if (!out.valid) {
        uint16_t mid = static_cast<uint16_t>(1u << (std::clamp<uint16_t>(bps, 8u, 14u) - 1u));
        out.vpred[0][0] = out.vpred[0][1] = out.vpred[1][0] = out.vpred[1][1] = mid;
    }
}

inline bool UnpackNikonNefHuffmanSensor(
    const uint8_t* src, size_t srcLen,
    uint32_t width, uint32_t height, uint16_t bps,
    const NikonMetaParams& nm,
    std::vector<uint16_t>& outSamples)
{
    if (!src || srcLen < 4 || width < 2 || height == 0 || bps < 8 || bps > 16) return false;

    // Canonical dcraw/LibRaw nikon_tree tables:
    // [0] = 12-bit Lossy, [1] = 12-bit Lossless, [2] = 14-bit Lossy, [3] = 14-bit Lossless
    static constexpr uint8_t kNikonTrees[4][32] = {
        // 0: 12-bit Lossy
        { 0,1,5,1,1,1,1,1,1,2,0,0,0,0,0,0, 5,4,3,6,2,7,1,0,8,9,11,10,12, 0,0,0 },
        // 1: 12-bit Lossless
        { 0,1,4,2,3,1,2,0,0,0,0,0,0,0,0,0, 5,4,6,3,7,2,8,1,9,0,10,11,12, 0,0,0 },
        // 2: 14-bit Lossy
        { 0,1,4,3,1,1,1,1,1,2,0,0,0,0,0,0, 6,5,4,7,3,2,8,1,9,0,10,11,12,13,14, 0 },
        // 3: 14-bit Lossless
        { 0,1,4,2,2,3,1,2,0,0,0,0,0,0,0,0, 7,6,8,5,9,4,10,3,11,12,2,0,1,13,14, 0 }
    };

    int treeIdx = (bps >= 14) ? ((nm.nefComp == 3) ? 3 : 2) : ((nm.nefComp == 3) ? 1 : 0);
    const uint8_t* td = kNikonTrees[treeIdx];

    int32_t minCode[17] = {}, maxCode[17] = {}, valPtr[17] = {};
    uint8_t syms[16] = {};
    int32_t code = 0, symCursor = 0;
    for (int len = 1; len <= 16; ++len) {
        int cnt = td[len - 1];
        if (cnt == 0) {
            maxCode[len] = -1;
        } else {
            valPtr[len] = symCursor;
            minCode[len] = code;
            symCursor = std::min(16, symCursor + cnt);
            code += cnt - 1;
            maxCode[len] = code;
            ++code;
        }
        code <<= 1;
    }
    for (int k = 0; k < symCursor; ++k) syms[k] = td[16 + k];

    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;
    bool eof = false;

    auto getBits = [&](int n) -> int32_t {
        if (n == 0) return 0;
        while (bitCnt < n) {
            if (bytePos >= srcLen) {
                eof = true;
                return 0;
            }
            bitBuf = (bitBuf << 8u) | src[bytePos++];
            bitCnt += 8;
        }
        bitCnt -= n;
        return static_cast<int32_t>((bitBuf >> bitCnt) & ((1u << n) - 1u));
    };

    auto decodeNikonDiff = [&]() -> int32_t {
        int32_t c = 0;
        int sym = -1;
        for (int len = 1; len <= 16; ++len) {
            int32_t b = getBits(1);
            if (eof) return 0;
            c = (c << 1) | b;
            if (c <= maxCode[len]) {
                sym = syms[valPtr[len] + (c - minCode[len])];
                break;
            }
        }
        if (sym < 0) { eof = true; return 0; }
        int lenBits = sym & 0x0F;
        int shBits  = sym >> 4;
        if (lenBits == 0) return 0;
        if (lenBits == 16) return 32768;
        int32_t v = getBits(lenBits - shBits);
        if ((v & (1 << (lenBits - shBits - 1))) == 0) {
            v -= (1 << (lenBits - shBits)) - 1;
        }
        return (v << shBits) + (shBits > 0 ? getBits(shBits) : 0);
    };

    int32_t vpred[2][2] = {
        { nm.vpred[0][0], nm.vpred[0][1] },
        { nm.vpred[1][0], nm.vpred[1][1] }
    };
    int32_t maxVal = (1 << bps) - 1;
    outSamples.assign(static_cast<size_t>(width) * height, 0u);

    for (uint32_t y = 0; y < height; ++y) {
        int32_t hpred[2] = {};
        for (uint32_t x = 0; x < width; ++x) {
            int32_t diff = decodeNikonDiff();
            if (eof) return false;
            int32_t val = 0;
            if (x < 2u) {
                vpred[y & 1u][x] += diff;
                hpred[x] = vpred[y & 1u][x];
                val = hpred[x];
            } else {
                hpred[x & 1u] += diff;
                val = hpred[x & 1u];
            }
            uint16_t u = static_cast<uint16_t>(std::clamp<int32_t>(val, 0, maxVal));
            if (!nm.curveLut.empty()) {
                u = nm.curveLut[std::min<size_t>(u, nm.curveLut.size() - 1u)];
            }
            outSamples[static_cast<size_t>(y) * width + x] = u;
        }
    }
    return true;
}

} // namespace DetailImg200

inline bool DecodeNikonNef(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailImg200::ParseTiffRawIfds(data, size, L"Nikon NEF RAW (.nef)", outInfo);
}

} // namespace Formats
