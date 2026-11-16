#pragma once
// CameraRawUpgrade.h — #8 Pure-C++20 Camera RAW Sensor Unpackers & Color Matrices
//                      (.cr3 CMP1/CRAW, .arw ARW2, .nef Huffman, .rw2 Panasonic 12-bit, .dng ColorMatrix1)
// Included inside `namespace Formats::DetailImg200` in `ExtendedImages200.h`.

// ============================================================================
// 1. DNG / Camera Color Science: ColorMatrix1 (XYZ D50 -> Cam) -> Linear sRGB
// ============================================================================
inline bool BuildCameraToSrgbMatrix(
    const float colorMatrix1[9],
    bool hasCamCal, const float camCal1[9],
    const float wb[3],
    float outCamToSrgb[9])
{
    // Bradford-adapted sRGB (D65) -> CIE XYZ (D50) matrix (Adobe DNG 1.7 Spec)
    static constexpr double kSrgbToXyzD50[9] = {
        0.4360747, 0.3850649, 0.1430804,
        0.2225045, 0.7168786, 0.0606169,
        0.0139322, 0.0971045, 0.7141733
    };

    // cm = (hasCamCal ? camCal1 * colorMatrix1 : colorMatrix1)
    double cm[9];
    if (hasCamCal) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                cm[r * 3 + c] = static_cast<double>(camCal1[r * 3 + 0]) * colorMatrix1[0 * 3 + c] +
                                static_cast<double>(camCal1[r * 3 + 1]) * colorMatrix1[1 * 3 + c] +
                                static_cast<double>(camCal1[r * 3 + 2]) * colorMatrix1[2 * 3 + c];
            }
        }
    } else {
        for (int i = 0; i < 9; ++i) cm[i] = colorMatrix1[i];
    }

    // srgbToCam = cm * kSrgbToXyzD50 (maps linear sRGB -> un-white-balanced Camera RGB)
    // Since input to our matrix is ALREADY white-balanced by wb[0..2] (or we normalize each row so sRGB [1,1,1] -> [1,1,1]!),
    // normalizing each row of srgbToCam to sum to 1.0 ensures neutral gray [v, v, v] after WB stays exact neutral [v, v, v]!
    double srgbToCam[9];
    for (int r = 0; r < 3; ++r) {
        double rowSum = 0.0;
        for (int c = 0; c < 3; ++c) {
            srgbToCam[r * 3 + c] = cm[r * 3 + 0] * kSrgbToXyzD50[0 * 3 + c] +
                                   cm[r * 3 + 1] * kSrgbToXyzD50[1 * 3 + c] +
                                   cm[r * 3 + 2] * kSrgbToXyzD50[2 * 3 + c];
            rowSum += srgbToCam[r * 3 + c];
        }
        if (std::abs(rowSum) < 1e-7) return false;
        for (int c = 0; c < 3; ++c) srgbToCam[r * 3 + c] /= rowSum;
    }
    (void)wb;

    double det =
        srgbToCam[0] * (srgbToCam[4] * srgbToCam[8] - srgbToCam[5] * srgbToCam[7]) -
        srgbToCam[1] * (srgbToCam[3] * srgbToCam[8] - srgbToCam[5] * srgbToCam[6]) +
        srgbToCam[2] * (srgbToCam[3] * srgbToCam[7] - srgbToCam[4] * srgbToCam[6]);
    if (std::abs(det) < 1e-7) return false;

    double invDet = 1.0 / det;
    outCamToSrgb[0] = static_cast<float>( (srgbToCam[4] * srgbToCam[8] - srgbToCam[5] * srgbToCam[7]) * invDet);
    outCamToSrgb[1] = static_cast<float>(-(srgbToCam[1] * srgbToCam[8] - srgbToCam[2] * srgbToCam[7]) * invDet);
    outCamToSrgb[2] = static_cast<float>( (srgbToCam[1] * srgbToCam[5] - srgbToCam[2] * srgbToCam[4]) * invDet);
    outCamToSrgb[3] = static_cast<float>(-(srgbToCam[3] * srgbToCam[8] - srgbToCam[5] * srgbToCam[6]) * invDet);
    outCamToSrgb[4] = static_cast<float>( (srgbToCam[0] * srgbToCam[8] - srgbToCam[2] * srgbToCam[6]) * invDet);
    outCamToSrgb[5] = static_cast<float>(-(srgbToCam[0] * srgbToCam[5] - srgbToCam[2] * srgbToCam[3]) * invDet);
    outCamToSrgb[6] = static_cast<float>( (srgbToCam[3] * srgbToCam[7] - srgbToCam[4] * srgbToCam[6]) * invDet);
    outCamToSrgb[7] = static_cast<float>(-(srgbToCam[0] * srgbToCam[7] - srgbToCam[1] * srgbToCam[6]) * invDet);
    outCamToSrgb[8] = static_cast<float>( (srgbToCam[0] * srgbToCam[4] - srgbToCam[1] * srgbToCam[3]) * invDet);
    return true;
}

// ============================================================================
// 2. Sony Alpha ARW2 Delta-Compressed Raw Unpacker (Compression == 32767)
// ============================================================================
inline bool UnpackSonyArw2Sensor(
    const uint8_t* src, size_t srcLen, uint32_t width, uint32_t height,
    const uint16_t* optCurveLut4096,
    std::vector<uint16_t>& outSamples)
{
    if (!src || width < 32 || (width & 31u) != 0u || height == 0) return false;
    size_t neededBytes = static_cast<size_t>(width) * height;
    if (srcLen < neededBytes) return false;

    outSamples.assign(neededBytes, 0u);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* rowSrc = src + static_cast<size_t>(y) * width;
        uint16_t* rowDst = &outSamples[static_cast<size_t>(y) * width];
        for (uint32_t x0 = 0; x0 < width; x0 += 32u) {
            for (uint32_t parity = 0; parity < 2u; ++parity) {
                const uint8_t* blk = rowSrc + x0 + parity * 16u;
                size_t bitPos = 0;
                auto rdBits = [&](int n) -> uint32_t {
                    uint32_t v = 0;
                    for (int k = 0; k < n; ++k) {
                        size_t bp = bitPos++;
                        v |= static_cast<uint32_t>((blk[bp >> 3u] >> (bp & 7u)) & 1u) << k;
                    }
                    return v;
                };
                uint32_t maxV = rdBits(11);
                uint32_t minV = rdBits(11);
                uint32_t imax = rdBits(4);
                uint32_t imin = rdBits(4);
                if (maxV < minV) return false;
                int sh = 0;
                while (sh < 4 && (0x80u << sh) <= (maxV - minV)) ++sh;

                for (uint32_t i = 0; i < 16u; ++i) {
                    uint32_t pix11 = 0u;
                    if (i == imax) pix11 = maxV;
                    else if (i == imin) pix11 = minV;
                    else pix11 = std::min<uint32_t>(2047u, (rdBits(7) << sh) + minV);

                    uint16_t v12 = static_cast<uint16_t>(pix11 << 1u);
                    if (optCurveLut4096) v12 = optCurveLut4096[std::min<uint32_t>(v12, 4095u)];
                    rowDst[x0 + i * 2u + parity] = v12;
                }
            }
        }
    }
    return true;
}

// ============================================================================
// 3. Nikon NEF Compressed / Lossless Huffman Sensor Unpacker (Compression == 34713)
// ============================================================================
struct NikonMetaParams {
    bool valid = false;
    uint16_t nefComp = 3; // 1=Lossy1, 3=Lossless, 4=Lossy2
    uint16_t vpred[2][2] = { { 512, 512 }, { 512, 512 } };
    std::vector<uint16_t> curveLut;
};

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

// ============================================================================
// 4. Panasonic Lumix RW2 12-bit 14-Pixel-per-16-Byte Sensor Unpacker
// ============================================================================
inline bool UnpackPanasonicRw2Sensor(
    const uint8_t* src, size_t srcLen, uint32_t width, uint32_t height,
    std::vector<uint16_t>& outSamples)
{
    if (!src || width < 2 || height == 0) return false;
    size_t totalPixels = static_cast<size_t>(width) * height;
    size_t minBlocks = (totalPixels + 13u) / 14u;
    if (srcLen < minBlocks * 16u) return false;

    outSamples.assign(totalPixels, 0u);
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;

    auto panaBits = [&](int n) -> int32_t {
        while (bitCnt < n) {
            uint8_t b = (bytePos < srcLen) ? src[bytePos++] : 0u;
            bitBuf |= static_cast<uint32_t>(b) << bitCnt;
            bitCnt += 8;
        }
        int32_t res = static_cast<int32_t>(bitBuf & ((1u << n) - 1u));
        bitBuf >>= n;
        bitCnt -= n;
        return res;
    };

    for (uint32_t y = 0; y < height; ++y) {
        int32_t pred[2] = { 0, 0 };
        int32_t nonz[2] = { 0, 0 };
        int sh = 0;
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t i = x % 14u;
            if (i == 0u) {
                pred[0] = nonz[0] = pred[1] = nonz[1] = 0;
            }
            if (i % 3u == 2u) {
                sh = 4 >> (3 - panaBits(2));
            }
            int pIdx = static_cast<int>(x & 1u);
            if (nonz[pIdx] != 0) {
                int32_t j = panaBits(8);
                if (j != 0) {
                    pred[pIdx] -= (0x80 << sh);
                    if (pred[pIdx] < 0 || sh == 4) pred[pIdx] &= ~(-1 << sh);
                    pred[pIdx] += (j << sh);
                }
            } else {
                nonz[pIdx] = panaBits(8);
                if (nonz[pIdx] != 0 || i > 11u) {
                    pred[pIdx] = (nonz[pIdx] << 4) | panaBits(4);
                }
            }
            outSamples[static_cast<size_t>(y) * width + x] = static_cast<uint16_t>(std::clamp<int32_t>(pred[pIdx], 0, 4095));
        }
    }
    return true;
}

// ============================================================================
// 5. Canon CR3 'CRAW' + 'CMP1' Sensor Track Unpacker (SOF3 Lossless JPEG / 14-bit CFA)
// ============================================================================
struct Cr3Cmp1TrackInfo {
    bool hasCmp1 = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bitDepth = 14;
    uint8_t cfaLayout = 0; // 0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR
    uint64_t mdatOffset = 0;
    uint32_t mdatSize = 0;
};

inline void ScanCr3Cmp1InTrak(
    const uint8_t* fileData, size_t fileSize,
    size_t startPos, size_t endPos, int depth,
    Cr3Cmp1TrackInfo& curTrack,
    std::vector<Cr3Cmp1TrackInfo>& outTracks)
{
    if (depth > 10 || startPos + 8 > endPos || endPos > fileSize) return;
    std::vector<uint64_t> offsets;
    std::vector<uint32_t> sizes;

    size_t pos = startPos;
    while (pos + 8 <= endPos) {
        uint64_t boxLen = DetailImg100::RdU32BE(fileData + pos);
        size_t hdrLen = 8;
        if (boxLen == 1) {
            if (pos + 16 > endPos) break;
            boxLen = DetailImg100::RdU64BE(fileData + pos + 8);
            hdrLen = 16;
        } else if (boxLen == 0) {
            boxLen = endPos - pos;
        }
        if (boxLen < hdrLen || pos + boxLen > endPos) break;
        const char* bt = reinterpret_cast<const char*>(fileData + pos + 4);
        size_t pay = pos + hdrLen;
        size_t boxEnd = pos + static_cast<size_t>(boxLen);
        size_t payLen = boxEnd - pay;

        if (std::memcmp(bt, "trak", 4) == 0) {
            Cr3Cmp1TrackInfo tr;
            ScanCr3Cmp1InTrak(fileData, fileSize, pay, boxEnd, depth + 1, tr, outTracks);
            if (tr.hasCmp1 && tr.mdatOffset > 0 && tr.mdatSize > 0) outTracks.push_back(tr);
        } else if (std::memcmp(bt, "moov", 4) == 0 || std::memcmp(bt, "mdia", 4) == 0 ||
                   std::memcmp(bt, "minf", 4) == 0 || std::memcmp(bt, "stbl", 4) == 0) {
            ScanCr3Cmp1InTrak(fileData, fileSize, pay, boxEnd, depth + 1, curTrack, outTracks);
        } else if (std::memcmp(bt, "stsd", 4) == 0 && payLen >= 16) {
            // stsd: [verFlags:4][entryCount:4][entry0...]
            ScanCr3Cmp1InTrak(fileData, fileSize, pay + 8, boxEnd, depth + 1, curTrack, outTracks);
        } else if (std::memcmp(bt, "CRAW", 4) == 0 && payLen >= 78) {
            // VisualSampleEntry ('CRAW'): width at +24, height at +26, child boxes ('CMP1', 'CDI1') at +78 (or +82)
            curTrack.width  = DetailImg100::RdU16BE(fileData + pay + 24);
            curTrack.height = DetailImg100::RdU16BE(fileData + pay + 26);
            for (size_t childOff : { size_t(78), size_t(82), size_t(86) }) {
                if (childOff + 8 <= payLen &&
                    (std::memcmp(fileData + pay + childOff + 4, "CMP1", 4) == 0 ||
                     std::memcmp(fileData + pay + childOff + 4, "CDI1", 4) == 0)) {
                    ScanCr3Cmp1InTrak(fileData, fileSize, pay + childOff, boxEnd, depth + 1, curTrack, outTracks);
                    break;
                }
            }
        } else if (std::memcmp(bt, "CMP1", 4) == 0 && payLen >= 24) {
            // Canon CMP1 header: [unk:4][hdrSz:2][ver:2][fWidth:4][fHeight:4][tWidth:4][tHeight:4][bitDepth:1][planesCfa:1]...
            const uint8_t* cp = fileData + pay;
            uint32_t fw = DetailImg100::RdU32BE(cp + 8);
            uint32_t fh = DetailImg100::RdU32BE(cp + 12);
            if (fw > 0 && fh > 0 && fw <= 16384 && fh <= 16384) {
                curTrack.width = fw;
                curTrack.height = fh;
            }
            uint8_t bd = cp[24 < payLen ? 24 : 20];
            if (bd >= 8 && bd <= 16) curTrack.bitDepth = bd;
            if (payLen >= 26) curTrack.cfaLayout = cp[25] & 0x03u;
            curTrack.hasCmp1 = true;
        } else if (std::memcmp(bt, "stco", 4) == 0 && payLen >= 12) {
            offsets.push_back(DetailImg100::RdU32BE(fileData + pay + 8));
        } else if (std::memcmp(bt, "co64", 4) == 0 && payLen >= 16) {
            offsets.push_back(DetailImg100::RdU64BE(fileData + pay + 8));
        } else if (std::memcmp(bt, "stsz", 4) == 0 && payLen >= 12) {
            uint32_t fsz = DetailImg100::RdU32BE(fileData + pay + 4);
            if (fsz > 0) sizes.push_back(fsz);
            else if (payLen >= 16) sizes.push_back(DetailImg100::RdU32BE(fileData + pay + 12));
        }
        pos = boxEnd;
    }
    if (!offsets.empty() && !sizes.empty()) {
        curTrack.mdatOffset = offsets[0];
        curTrack.mdatSize = sizes[0];
    }
}
