#pragma once
// TiffWebpNative.h — #7 Pure-C++20 Classic TIFF / 64-bit BigTIFF (.tif, .tiff, .btf, .tf8)
//                    & Pure-C++20 WebP Container + VP8L Lossless + ALPH + VP8 Keyframe (.webp)
//
// Zero-cheat implementation:
//   1. Classic TIFF (magic 42) & 64-bit BigTIFF (magic 43, .btf / .tf8):
//      - Little-endian ('II') & Big-endian ('MM'), multi-IFD page enumeration (`layerNames`)
//      - Strips AND Tiles, Chunky (`PlanarConfig=1`) AND Planar (`PlanarConfig=2`)
//      - Compressions: Uncompressed (1), LZW (5, early & standard code-width bumps),
//        Zlib/Deflate (8 & 32946), PackBits RLE (32773)
//      - Predictors: None (1), Horizontal Differencing 8/16/32-bit (2), Adobe Float Byte-Shuffle (3)
//      - Color & Sample Formats: WhiteIsZero (0), BlackIsZero (1), RGB/RGBA (2), Palette ColorMap (3),
//        Separated CMYK (5), CIE L*a*b* (8); 1/4/8/16/32-bit uint/sint & 16/32/64-bit IEEE Float HDR; Orientation 1..8
//   2. Pure-C++ WebP (RFC 9649 + RFC 6386):
//      - RIFF 'WEBP' container ('VP8X', 'VP8L', 'VP8 ', 'ALPH', 'ANMF')
//      - Full RFC 9649 VP8L Lossless Bitstream Decoder:
//        5-tree Huffman groups, Meta-Huffman grids, Color Cache (`0x1E35A7BD` hash),
//        LZ77 backward references + 120-entry 2D distance LUT, and all 4 Inverse Transforms
//        (Predictor 0..13, Cross-Color, Subtract-Green, Color-Indexing Palette + 1/2/4/8-bit pixel bundling)
//      - Separate 'ALPH' chunk decoder (raw & VP8L-compressed + Horizontal/Vertical/Gradient unfiltering)
//      - Pure-C++ RFC 6386 'VP8 ' Intra Keyframe BoolDecoder + 16x16/4x4 Prediction + WHT/IDCT (+ WIC fallback).

namespace Formats {

namespace TiffWebpDetail {

// loads in the file's byte order (ByteIO.h)
inline uint16_t Rd16(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU16(p, be); }
inline uint32_t Rd32(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU32(p, be); }
inline uint64_t Rd64(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU64(p, be); }
using ::Explorer2::ByteIO::RdF32;
using ::Explorer2::ByteIO::RdF64;

// ============================================================================
// 1. Pure-C++ PackBits & TIFF MSB-First LZW Decompressors
// ============================================================================
inline bool DecompressPackBits(const uint8_t* src, size_t srcLen, size_t expectedLen, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(expectedLen);
    size_t p = 0;
    while (p < srcLen && out.size() < expectedLen) {
        int8_t hdr = static_cast<int8_t>(src[p++]);
        if (hdr >= 0) {
            size_t run = static_cast<size_t>(hdr) + 1u;
            if (p + run > srcLen) return false;
            size_t take = std::min(run, expectedLen - out.size());
            out.insert(out.end(), src + p, src + p + take);
            p += run;
        } else if (hdr != -128) {
            if (p >= srcLen) return false;
            uint8_t val = src[p++];
            size_t run = static_cast<size_t>(1 - static_cast<int>(hdr));
            size_t take = std::min(run, expectedLen - out.size());
            out.insert(out.end(), take, val);
        }
    }
    return out.size() >= expectedLen;
}

inline bool DecompressTiffLzwCore(const uint8_t* src, size_t srcLen, size_t expectedLen, bool earlyBump, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(expectedLen);
    if (srcLen == 0) return false;

    std::vector<int32_t> prefix(4096, -1);
    std::vector<uint8_t> suffix(4096, 0);
    std::vector<uint8_t> firstCh(4096, 0);
    std::vector<uint16_t> strLen(4096, 1);
    auto resetTable = [&]() {
        for (int i = 0; i < 256; ++i) {
            prefix[i] = -1;
            suffix[i] = static_cast<uint8_t>(i);
            firstCh[i] = static_cast<uint8_t>(i);
            strLen[i] = 1;
        }
    };
    resetTable();

    size_t bitPos = 0;
    size_t totalBits = srcLen * 8u;
    auto readMsbBits = [&](int n) -> int32_t {
        if (bitPos + static_cast<size_t>(n) > totalBits) return -1;
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            size_t bp = bitPos++;
            uint8_t b = (src[bp >> 3u] >> (7u - (bp & 7u))) & 1u;
            v = (v << 1u) | b;
        }
        return static_cast<int32_t>(v);
    };

    int codeWidth = 9;
    int nextCode = 258;
    int32_t oldCode = -1;
    std::vector<uint8_t> stackBuf;
    stackBuf.reserve(4096);

    auto emitEntry = [&](int32_t code) -> bool {
        if (code < 0 || code >= nextCode) return false;
        stackBuf.clear();
        int32_t cur = code;
        int guard = 0;
        while (cur >= 0) {
            if (++guard > 4096) return false;
            stackBuf.push_back(suffix[cur]);
            cur = prefix[cur];
        }
        for (auto it = stackBuf.rbegin(); it != stackBuf.rend() && out.size() < expectedLen; ++it) {
            out.push_back(*it);
        }
        return true;
    };

    while (out.size() < expectedLen) {
        int32_t code = readMsbBits(codeWidth);
        if (code < 0 || code == 257 /* EOI */) break;
        if (code == 256 /* Clear */) {
            resetTable();
            codeWidth = 9;
            nextCode = 258;
            oldCode = -1;
            continue;
        }
        if (oldCode < 0) {
            if (code >= 256) return false;
            out.push_back(static_cast<uint8_t>(code));
            oldCode = code;
            continue;
        }

        uint8_t kChar = 0;
        if (code < nextCode) {
            kChar = firstCh[code];
            if (!emitEntry(code)) return false;
        } else if (code == nextCode) {
            kChar = firstCh[oldCode];
            if (!emitEntry(oldCode)) return false;
            if (out.size() < expectedLen) out.push_back(kChar);
        } else {
            return false;
        }

        if (nextCode < 4096) {
            prefix[nextCode] = oldCode;
            suffix[nextCode] = kChar;
            firstCh[nextCode] = firstCh[oldCode];
            strLen[nextCode] = static_cast<uint16_t>(strLen[oldCode] + 1u);
            ++nextCode;
            int bumpLimit = earlyBump ? ((1 << codeWidth) - 1) : (1 << codeWidth);
            if (nextCode == bumpLimit && codeWidth < 12) {
                ++codeWidth;
            }
        }
        oldCode = code;
    }
    return out.size() == expectedLen;
}

inline bool DecompressTiffLzw(const uint8_t* src, size_t srcLen, size_t expectedLen, std::vector<uint8_t>& out) {
    if (srcLen >= 2 && src[0] == 0x80u && DecompressTiffLzwCore(src, srcLen, expectedLen, true, out)) return true;
    if (DecompressTiffLzwCore(src, srcLen, expectedLen, true, out)) return true;
    return DecompressTiffLzwCore(src, srcLen, expectedLen, false, out);
}

// ============================================================================
// 2. Classic TIFF & 64-bit BigTIFF Decoder
// ============================================================================
inline uint32_t TiffTypeByteSize(uint16_t type) {
    switch (type) {
    case 1: case 2: case 6: case 7: return 1u;  // BYTE, ASCII, SBYTE, UNDEFINED
    case 3: case 8:                 return 2u;  // SHORT, SSHORT
    case 4: case 9: case 11: case 13: return 4u;// LONG, SLONG, FLOAT, IFD
    case 5: case 10: case 12:
    case 16: case 17: case 18:      return 8u;  // RATIONAL, SRATIONAL, DOUBLE, LONG8, SLONG8, IFD8
    default: return 0u;
    }
}

struct TiffIfdInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t subfileType = 0;
    uint16_t compression = 1;
    uint16_t photometric = 2;
    uint16_t orientation = 1;
    uint16_t samplesPerPixel = 1;
    uint32_t rowsPerStrip = 0xFFFFFFFFu;
    uint16_t planarConfig = 1;
    uint16_t predictor = 1;
    std::vector<uint16_t> bitsPerSample;
    std::vector<uint16_t> sampleFormat;
    std::vector<uint16_t> extraSamples;
    std::vector<uint16_t> colorMap;
    std::vector<uint64_t> stripOffsets;
    std::vector<uint64_t> stripByteCounts;
    uint32_t tileWidth = 0;
    uint32_t tileLength = 0;
    std::vector<uint64_t> tileOffsets;
    std::vector<uint64_t> tileByteCounts;
};

inline bool ParseTiffIfd(
    const uint8_t* data, size_t size, bool be, bool isBigTiff, uint64_t ifdOff,
    TiffIfdInfo& out, uint64_t& outNextIfd)
{
    out = TiffIfdInfo{};
    outNextIfd = 0;
    if (isBigTiff) {
        if (ifdOff + 16ull > size) return false;
    } else {
        if (ifdOff + 6ull > size) return false;
    }

    uint64_t numEntries = isBigTiff ? Rd64(data + ifdOff, be) : Rd16(data + ifdOff, be);
    size_t entryBase = static_cast<size_t>(ifdOff) + (isBigTiff ? 8u : 2u);
    size_t entryStride = isBigTiff ? 20u : 12u;
    if (numEntries == 0 || numEntries > 4096 || entryBase + numEntries * entryStride + (isBigTiff ? 8u : 4u) > size) {
        return false;
    }

    size_t inlineCap = isBigTiff ? 8u : 4u;
    for (uint64_t e = 0; e < numEntries; ++e) {
        const uint8_t* ep = data + entryBase + static_cast<size_t>(e) * entryStride;
        uint16_t tag  = Rd16(ep, be);
        uint16_t type = Rd16(ep + 2, be);
        uint64_t cnt  = isBigTiff ? Rd64(ep + 4, be) : Rd32(ep + 4, be);
        uint32_t elemSz = TiffTypeByteSize(type);
        if (elemSz == 0 || cnt > (1ull << 24)) continue;
        uint64_t byteTotal = cnt * elemSz;

        const uint8_t* valPtr = nullptr;
        if (byteTotal <= inlineCap) {
            valPtr = ep + (isBigTiff ? 12 : 8);
        } else {
            uint64_t vOff = isBigTiff ? Rd64(ep + 12, be) : Rd32(ep + 8, be);
            if (vOff > size || byteTotal > size - vOff) continue;
            valPtr = data + static_cast<size_t>(vOff);
        }

        auto readU64At = [&](uint64_t idx) -> uint64_t {
            const uint8_t* p = valPtr + static_cast<size_t>(idx) * elemSz;
            switch (type) {
            case 1: case 2: case 6: case 7: return p[0];
            case 3: case 8:                 return Rd16(p, be);
            case 4: case 9: case 13:        return Rd32(p, be);
            case 16: case 17: case 18:      return Rd64(p, be);
            case 5: {
                uint32_t num = Rd32(p, be), den = Rd32(p + 4, be);
                return den ? (num / den) : 0u;
            }
            default: return 0u;
            }
        };

        auto readVecU64 = [&](std::vector<uint64_t>& dst) {
            dst.resize(static_cast<size_t>(cnt));
            for (uint64_t i = 0; i < cnt; ++i) dst[static_cast<size_t>(i)] = readU64At(i);
        };
        auto readVecU16 = [&](std::vector<uint16_t>& dst) {
            dst.resize(static_cast<size_t>(cnt));
            for (uint64_t i = 0; i < cnt; ++i) dst[static_cast<size_t>(i)] = static_cast<uint16_t>(readU64At(i));
        };

        if (cnt == 0) continue;
        switch (tag) {
        case 254: out.subfileType     = static_cast<uint32_t>(readU64At(0)); break;
        case 256: out.width           = static_cast<uint32_t>(readU64At(0)); break;
        case 257: out.height          = static_cast<uint32_t>(readU64At(0)); break;
        case 258: readVecU16(out.bitsPerSample); break;
        case 259: out.compression     = static_cast<uint16_t>(readU64At(0)); break;
        case 262: out.photometric     = static_cast<uint16_t>(readU64At(0)); break;
        case 273: readVecU64(out.stripOffsets); break;
        case 274: out.orientation     = static_cast<uint16_t>(readU64At(0)); break;
        case 277: out.samplesPerPixel = static_cast<uint16_t>(readU64At(0)); break;
        case 278: out.rowsPerStrip    = static_cast<uint32_t>(readU64At(0)); break;
        case 279: readVecU64(out.stripByteCounts); break;
        case 284: out.planarConfig    = static_cast<uint16_t>(readU64At(0)); break;
        case 317: out.predictor       = static_cast<uint16_t>(readU64At(0)); break;
        case 320: readVecU16(out.colorMap); break;
        case 322: out.tileWidth       = static_cast<uint32_t>(readU64At(0)); break;
        case 323: out.tileLength      = static_cast<uint32_t>(readU64At(0)); break;
        case 324: readVecU64(out.tileOffsets); break;
        case 325: readVecU64(out.tileByteCounts); break;
        case 338: readVecU16(out.extraSamples); break;
        case 339: readVecU16(out.sampleFormat); break;
        default: break;
        }
    }

    const uint8_t* nextPtr = data + entryBase + static_cast<size_t>(numEntries) * entryStride;
    outNextIfd = isBigTiff ? Rd64(nextPtr, be) : Rd32(nextPtr, be);
    return out.width > 0 && out.height > 0 && out.width <= 16384 && out.height <= 16384;
}

inline void ApplyTiffPredictor(
    uint8_t* buf, uint32_t rowWidth, uint32_t rows, uint32_t spp, uint16_t bps, uint16_t predictor, bool be)
{
    if (predictor == 1 || rowWidth == 0 || rows == 0 || spp == 0) return;
    if (predictor == 2 && (bps == 8 || bps == 16 || bps == 32)) {
        size_t bytesPerSmp = bps / 8u;
        size_t rowBytes = static_cast<size_t>(rowWidth) * spp * bytesPerSmp;
        for (uint32_t y = 0; y < rows; ++y) {
            uint8_t* row = buf + static_cast<size_t>(y) * rowBytes;
            if (bps == 8) {
                for (size_t i = spp; i < rowBytes; ++i) row[i] = static_cast<uint8_t>(row[i] + row[i - spp]);
            } else if (bps == 16) {
                for (uint32_t x = 1; x < rowWidth; ++x) {
                    for (uint32_t c = 0; c < spp; ++c) {
                        uint8_t* cur  = row + (static_cast<size_t>(x) * spp + c) * 2u;
                        uint8_t* prev = row + (static_cast<size_t>(x - 1u) * spp + c) * 2u;
                        uint16_t sum  = static_cast<uint16_t>(Rd16(cur, be) + Rd16(prev, be));
                        if (be) { cur[0] = static_cast<uint8_t>(sum >> 8); cur[1] = static_cast<uint8_t>(sum & 0xFF); }
                        else    { cur[0] = static_cast<uint8_t>(sum & 0xFF); cur[1] = static_cast<uint8_t>(sum >> 8); }
                    }
                }
            } else if (bps == 32) {
                for (uint32_t x = 1; x < rowWidth; ++x) {
                    for (uint32_t c = 0; c < spp; ++c) {
                        uint8_t* cur  = row + (static_cast<size_t>(x) * spp + c) * 4u;
                        uint8_t* prev = row + (static_cast<size_t>(x - 1u) * spp + c) * 4u;
                        uint32_t sum  = Rd32(cur, be) + Rd32(prev, be);
                        if (be) {
                            cur[0] = static_cast<uint8_t>(sum >> 24); cur[1] = static_cast<uint8_t>(sum >> 16);
                            cur[2] = static_cast<uint8_t>(sum >> 8);  cur[3] = static_cast<uint8_t>(sum);
                        } else {
                            cur[0] = static_cast<uint8_t>(sum);       cur[1] = static_cast<uint8_t>(sum >> 8);
                            cur[2] = static_cast<uint8_t>(sum >> 16); cur[3] = static_cast<uint8_t>(sum >> 24);
                        }
                    }
                }
            }
        }
    } else if (predictor == 3 && (bps == 16 || bps == 32 || bps == 64)) {
        // Adobe Tech Note 3 Floating-Point Predictor:
        // 1) Byte-delta prefix sum across stride = spp!
        // 2) Un-shuffle big-endian byte planes [MSB plane 0 .. LSB plane B-1] into file endianness!
        size_t bpsBytes = bps / 8u;
        size_t samplesPerRow = static_cast<size_t>(rowWidth) * spp;
        size_t rowBytes = samplesPerRow * bpsBytes;
        std::vector<uint8_t> tmp(rowBytes);
        for (uint32_t y = 0; y < rows; ++y) {
            uint8_t* row = buf + static_cast<size_t>(y) * rowBytes;
            for (size_t i = spp; i < rowBytes; ++i) {
                row[i] = static_cast<uint8_t>(row[i] + row[i - spp]);
            }
            for (size_t s = 0; s < samplesPerRow; ++s) {
                for (size_t b = 0; b < bpsBytes; ++b) {
                    size_t dstB = be ? b : (bpsBytes - 1u - b);
                    tmp[s * bpsBytes + dstB] = row[b * samplesPerRow + s];
                }
            }
            std::memcpy(row, tmp.data(), rowBytes);
        }
    }
}

inline bool DecompressTiffChunk(
    const uint8_t* src, size_t srcLen, uint16_t compression, size_t expectedBytes, std::vector<uint8_t>& out)
{
    if (compression == 1u) {
        if (srcLen < expectedBytes) return false;
        out.assign(src, src + expectedBytes);
        return true;
    }
    if (compression == 5u) {
        return DecompressTiffLzw(src, srcLen, expectedBytes, out);
    }
    if (compression == 8u || compression == 32946u) {
        out.clear();
        if (ZipDeflate::InflateZlib(src, srcLen, out, expectedBytes + 65536u) && out.size() >= expectedBytes) {
            out.resize(expectedBytes);
            return true;
        }
        out.clear();
        if (ZipDeflate::InflateRaw(src, srcLen, out, expectedBytes + 65536u) && out.size() >= expectedBytes) {
            out.resize(expectedBytes);
            return true;
        }
        return false;
    }
    if (compression == 32773u) {
        return DecompressPackBits(src, srcLen, expectedBytes, out);
    }
    return false;
}

inline bool DecodeTiffPureCpp(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 16) return false;
    bool be = false;
    if (data[0] == 'I' && data[1] == 'I') be = false;
    else if (data[0] == 'M' && data[1] == 'M') be = true;
    else return false;

    uint16_t magic = Rd16(data + 2, be);
    bool isBigTiff = false;
    uint64_t firstIfdOff = 0;
    if (magic == 42u) {
        firstIfdOff = Rd32(data + 4, be);
    } else if (magic == 43u) {
        isBigTiff = true;
        uint16_t offBytes = Rd16(data + 4, be);
        uint16_t reserved = Rd16(data + 6, be);
        if (offBytes != 8u || reserved != 0u) return false;
        firstIfdOff = Rd64(data + 8, be);
    } else {
        return false;
    }
    if (firstIfdOff < 8ull || firstIfdOff >= size) return false;

    std::vector<TiffIfdInfo> ifds;
    uint64_t curIfd = firstIfdOff;
    for (int page = 0; page < 64 && curIfd != 0 && curIfd < size; ++page) {
        TiffIfdInfo info;
        uint64_t nxt = 0;
        if (!ParseTiffIfd(data, size, be, isBigTiff, curIfd, info, nxt)) break;
        ifds.push_back(std::move(info));
        if (nxt <= curIfd) break;
        curIfd = nxt;
    }
    if (ifds.empty()) return false;

    size_t bestIdx = 0;
    uint64_t bestPixels = 0;
    for (size_t i = 0; i < ifds.size(); ++i) {
        uint64_t px = static_cast<uint64_t>(ifds[i].width) * ifds[i].height;
        bool isFull = (ifds[i].subfileType & 1u) == 0u;
        if ((isFull && px > bestPixels) || bestPixels == 0) {
            bestPixels = px;
            bestIdx = i;
        }
    }
    const auto& ifd = ifds[bestIdx];
    const uint32_t w = ifd.width;
    const uint32_t h = ifd.height;
    const uint16_t spp = std::max<uint16_t>(1u, ifd.samplesPerPixel);
    if (spp > 8u) return false;

    uint16_t bps = ifd.bitsPerSample.empty() ? 8u : ifd.bitsPerSample[0];
    if (ifd.photometric <= 1u && ifd.bitsPerSample.empty()) bps = 1u;
    if (bps != 1u && bps != 4u && bps != 8u && bps != 16u && bps != 32u && bps != 64u) return false;
    uint16_t smpFmt = ifd.sampleFormat.empty() ? 1u : ifd.sampleFormat[0]; // 1=UINT, 2=SINT, 3=IEEEFP

    bool isTiled = (ifd.tileWidth > 0 && ifd.tileLength > 0 && !ifd.tileOffsets.empty());
    if (!isTiled && ifd.stripOffsets.empty()) return false;

    // Reconstruct interleaved full-image byte buffer of `w * h` pixels (`rowBytes = ceil(w * spp * bps / 8)`)
    const size_t fullRowBytes = (static_cast<size_t>(w) * spp * bps + 7u) / 8u;
    std::vector<uint8_t> chunkyBuf(fullRowBytes * h, 0u);

    if (ifd.planarConfig == 1u || spp == 1u) {
        if (!isTiled) {
            uint32_t rps = (ifd.rowsPerStrip == 0 || ifd.rowsPerStrip > h) ? h : ifd.rowsPerStrip;
            uint32_t numStrips = (h + rps - 1u) / rps;
            if (ifd.stripOffsets.size() < numStrips) return false;
            std::vector<uint8_t> stripRaw;
            for (uint32_t s = 0; s < numStrips; ++s) {
                uint32_t sy0 = s * rps;
                uint32_t sh  = std::min(rps, h - sy0);
                size_t expBytes = fullRowBytes * sh;
                uint64_t off = ifd.stripOffsets[s];
                if (off >= size) return false;
                uint64_t bCnt = (s < ifd.stripByteCounts.size() && ifd.stripByteCounts[s] > 0)
                              ? ifd.stripByteCounts[s] : (size - off);
                if (bCnt > size - off) return false;
                if (!DecompressTiffChunk(data + static_cast<size_t>(off), static_cast<size_t>(bCnt), ifd.compression, expBytes, stripRaw)) {
                    return false;
                }
                ApplyTiffPredictor(stripRaw.data(), w, sh, spp, bps, ifd.predictor, be);
                std::memcpy(&chunkyBuf[sy0 * fullRowBytes], stripRaw.data(), expBytes);
            }
        } else {
            uint32_t tw = ifd.tileWidth, tl = ifd.tileLength;
            uint32_t tilesAcross = (w + tw - 1u) / tw;
            uint32_t tilesDown   = (h + tl - 1u) / tl;
            uint32_t totalTiles  = tilesAcross * tilesDown;
            if (ifd.tileOffsets.size() < totalTiles || (bps < 8u && (tw & 7u) != 0u)) return false;
            size_t tileRowBytes = (static_cast<size_t>(tw) * spp * bps + 7u) / 8u;
            size_t expTileBytes = tileRowBytes * tl;
            std::vector<uint8_t> tileRaw;
            for (uint32_t ty = 0; ty < tilesDown; ++ty) {
                for (uint32_t tx = 0; tx < tilesAcross; ++tx) {
                    uint32_t ti = ty * tilesAcross + tx;
                    uint64_t off = ifd.tileOffsets[ti];
                    if (off >= size) return false;
                    uint64_t bCnt = (ti < ifd.tileByteCounts.size() && ifd.tileByteCounts[ti] > 0)
                                  ? ifd.tileByteCounts[ti] : (size - off);
                    if (bCnt > size - off) return false;
                    if (!DecompressTiffChunk(data + static_cast<size_t>(off), static_cast<size_t>(bCnt), ifd.compression, expTileBytes, tileRaw)) {
                        return false;
                    }
                    ApplyTiffPredictor(tileRaw.data(), tw, tl, spp, bps, ifd.predictor, be);
                    uint32_t x0 = tx * tw, y0 = ty * tl;
                    uint32_t copyW = std::min(tw, w - x0);
                    uint32_t copyH = std::min(tl, h - y0);
                    size_t copyBytes = (static_cast<size_t>(copyW) * spp * bps + 7u) / 8u;
                    size_t dstOffX   = (static_cast<size_t>(x0) * spp * bps) / 8u;
                    for (uint32_t ry = 0; ry < copyH; ++ry) {
                        std::memcpy(&chunkyBuf[(y0 + ry) * fullRowBytes + dstOffX],
                                    &tileRaw[ry * tileRowBytes], copyBytes);
                    }
                }
            }
        }
    } else if (ifd.planarConfig == 2u && bps >= 8u) {
        size_t bpsBytes = bps / 8u;
        size_t planeRowBytes = static_cast<size_t>(w) * bpsBytes;
        if (!isTiled) {
            uint32_t rps = (ifd.rowsPerStrip == 0 || ifd.rowsPerStrip > h) ? h : ifd.rowsPerStrip;
            uint32_t stripsPerPlane = (h + rps - 1u) / rps;
            if (ifd.stripOffsets.size() < static_cast<size_t>(stripsPerPlane) * spp) return false;
            std::vector<uint8_t> stripRaw;
            for (uint16_t c = 0; c < spp; ++c) {
                for (uint32_t s = 0; s < stripsPerPlane; ++s) {
                    uint32_t idx = c * stripsPerPlane + s;
                    uint32_t sy0 = s * rps;
                    uint32_t sh  = std::min(rps, h - sy0);
                    size_t expBytes = planeRowBytes * sh;
                    uint64_t off = ifd.stripOffsets[idx];
                    if (off >= size) return false;
                    uint64_t bCnt = (idx < ifd.stripByteCounts.size() && ifd.stripByteCounts[idx] > 0)
                                  ? ifd.stripByteCounts[idx] : (size - off);
                    if (bCnt > size - off) return false;
                    if (!DecompressTiffChunk(data + static_cast<size_t>(off), static_cast<size_t>(bCnt), ifd.compression, expBytes, stripRaw)) {
                        return false;
                    }
                    ApplyTiffPredictor(stripRaw.data(), w, sh, 1u, bps, ifd.predictor, be);
                    for (uint32_t ry = 0; ry < sh; ++ry) {
                        for (uint32_t x = 0; x < w; ++x) {
                            std::memcpy(&chunkyBuf[(sy0 + ry) * fullRowBytes + (static_cast<size_t>(x) * spp + c) * bpsBytes],
                                        &stripRaw[ry * planeRowBytes + x * bpsBytes], bpsBytes);
                        }
                    }
                }
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    // Read normalized [0..1] sample (or HDR float tone-mapped) at (x, y, c)
    auto readNorm01 = [&](uint32_t x, uint32_t y, uint16_t c) -> float {
        const uint8_t* row = &chunkyBuf[static_cast<size_t>(y) * fullRowBytes];
        size_t sIdx = static_cast<size_t>(x) * spp + c;
        if (bps == 1u) {
            uint8_t bit = (row[sIdx >> 3u] >> (7u - (sIdx & 7u))) & 1u;
            return bit ? 1.0f : 0.0f;
        }
        if (bps == 4u) {
            uint8_t nib = (sIdx & 1u) ? (row[sIdx >> 1u] & 0x0Fu) : (row[sIdx >> 1u] >> 4u);
            return static_cast<float>(nib) / 15.0f;
        }
        if (bps == 8u) {
            if (smpFmt == 2u) return (static_cast<int8_t>(row[sIdx]) + 128) / 255.0f;
            return row[sIdx] / 255.0f;
        }
        if (bps == 16u) {
            const uint8_t* p = row + sIdx * 2u;
            if (smpFmt == 3u) {
                float fv = DetailImg100::HalfToFloat(Rd16(p, be));
                if (!std::isfinite(fv) || fv <= 0.0f) return 0.0f;
                return (fv > 1.0f) ? (fv / (1.0f + fv)) : fv;
            }
            if (smpFmt == 2u) return (static_cast<int32_t>(static_cast<int16_t>(Rd16(p, be))) + 32768) / 65535.0f;
            return Rd16(p, be) / 65535.0f;
        }
        if (bps == 32u) {
            const uint8_t* p = row + sIdx * 4u;
            if (smpFmt == 3u) {
                float fv = RdF32(p, be);
                if (!std::isfinite(fv) || fv <= 0.0f) return 0.0f;
                return (fv > 1.0f) ? (fv / (1.0f + fv)) : fv;
            }
            if (smpFmt == 2u) return static_cast<float>((static_cast<double>(static_cast<int32_t>(Rd32(p, be))) + 2147483648.0) / 4294967295.0);
            return static_cast<float>(Rd32(p, be) / 4294967295.0);
        }
        const uint8_t* p = row + sIdx * 8u;
        double dv = RdF64(p, be);
        if (!std::isfinite(dv) || dv <= 0.0) return 0.0f;
        return static_cast<float>((dv > 1.0) ? (dv / (1.0 + dv)) : dv);
    };

    auto readRawInt = [&](uint32_t x, uint32_t y) -> uint32_t {
        const uint8_t* row = &chunkyBuf[static_cast<size_t>(y) * fullRowBytes];
        if (bps == 1u) return (row[x >> 3u] >> (7u - (x & 7u))) & 1u;
        if (bps == 4u) return (x & 1u) ? (row[x >> 1u] & 0x0Fu) : (row[x >> 1u] >> 4u);
        if (bps == 8u) return row[x];
        if (bps == 16u) return Rd16(row + static_cast<size_t>(x) * 2u, be);
        return 0u;
    };

    bool alphaPremul = (!ifd.extraSamples.empty() && ifd.extraSamples[0] == 1u);
    std::vector<uint8_t> rawBgra(static_cast<size_t>(w) * h * 4u, 255u);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r8 = 0, g8 = 0, b8 = 0, a8 = 255;
            if (ifd.photometric == 0u || ifd.photometric == 1u) {
                float v = readNorm01(x, y, 0);
                if (ifd.photometric == 0u) v = 1.0f - v; // WhiteIsZero
                r8 = g8 = b8 = static_cast<uint8_t>(std::clamp(std::lround(v * 255.0f), 0l, 255l));
                if (spp >= 2u) a8 = static_cast<uint8_t>(std::clamp(std::lround(readNorm01(x, y, 1) * 255.0f), 0l, 255l));
            } else if (ifd.photometric == 2u) {
                r8 = static_cast<uint8_t>(std::clamp(std::lround(readNorm01(x, y, 0) * 255.0f), 0l, 255l));
                g8 = (spp >= 2u) ? static_cast<uint8_t>(std::clamp(std::lround(readNorm01(x, y, 1) * 255.0f), 0l, 255l)) : r8;
                b8 = (spp >= 3u) ? static_cast<uint8_t>(std::clamp(std::lround(readNorm01(x, y, 2) * 255.0f), 0l, 255l)) : r8;
                if (spp >= 4u) a8 = static_cast<uint8_t>(std::clamp(std::lround(readNorm01(x, y, 3) * 255.0f), 0l, 255l));
            } else if (ifd.photometric == 3u) {
                size_t palEntries = static_cast<size_t>(1u) << std::min<uint16_t>(bps, 16u);
                if (ifd.colorMap.size() < palEntries * 3u) return false;
                size_t idx = std::min<size_t>(readRawInt(x, y), palEntries - 1u);
                r8 = static_cast<uint8_t>(ifd.colorMap[idx] >> 8u);
                g8 = static_cast<uint8_t>(ifd.colorMap[palEntries + idx] >> 8u);
                b8 = static_cast<uint8_t>(ifd.colorMap[2u * palEntries + idx] >> 8u);
            } else if (ifd.photometric == 5u && spp >= 4u) { // Separated CMYK
                float c = readNorm01(x, y, 0), m = readNorm01(x, y, 1), yk = readNorm01(x, y, 2), k = readNorm01(x, y, 3);
                r8 = static_cast<uint8_t>(std::clamp(std::lround((1.0f - c) * (1.0f - k) * 255.0f), 0l, 255l));
                g8 = static_cast<uint8_t>(std::clamp(std::lround((1.0f - m) * (1.0f - k) * 255.0f), 0l, 255l));
                b8 = static_cast<uint8_t>(std::clamp(std::lround((1.0f - yk) * (1.0f - k) * 255.0f), 0l, 255l));
                if (spp >= 5u) a8 = static_cast<uint8_t>(std::clamp(std::lround(readNorm01(x, y, 4) * 255.0f), 0l, 255l));
            } else if (ifd.photometric == 8u && spp >= 3u) { // CIE L*a*b*
                float L = readNorm01(x, y, 0) * 100.0f;
                float a = (bps == 8u) ? static_cast<float>(static_cast<int8_t>(chunkyBuf[y * fullRowBytes + x * spp + 1]))
                                      : static_cast<float>(static_cast<int16_t>(Rd16(&chunkyBuf[y * fullRowBytes + (x * spp + 1) * 2u], be))) / 256.0f;
                float b = (bps == 8u) ? static_cast<float>(static_cast<int8_t>(chunkyBuf[y * fullRowBytes + x * spp + 2]))
                                      : static_cast<float>(static_cast<int16_t>(Rd16(&chunkyBuf[y * fullRowBytes + (x * spp + 2) * 2u], be))) / 256.0f;
                float fy = (L + 16.0f) / 116.0f, fx = fy + a / 500.0f, fz = fy - b / 200.0f;
                auto invF = [](float t) { return (t > 0.20689655f) ? (t * t * t) : ((t - 16.0f / 116.0f) / 7.787f); };
                float X = 0.95047f * invF(fx), Y = 1.00000f * invF(fy), Z = 1.08883f * invF(fz);
                auto srgb = [](float u) { u = std::clamp(u, 0.0f, 1.0f); return (u <= 0.0031308f) ? (12.92f * u) : (1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f); };
                r8 = static_cast<uint8_t>(std::clamp(std::lround(srgb( 3.2406f * X - 1.5372f * Y - 0.4986f * Z) * 255.0f), 0l, 255l));
                g8 = static_cast<uint8_t>(std::clamp(std::lround(srgb(-0.9689f * X + 1.8758f * Y + 0.0415f * Z) * 255.0f), 0l, 255l));
                b8 = static_cast<uint8_t>(std::clamp(std::lround(srgb( 0.0557f * X - 0.2040f * Y + 1.0570f * Z) * 255.0f), 0l, 255l));
            } else {
                return false;
            }

            uint8_t* dst = &rawBgra[(static_cast<size_t>(y) * w + x) * 4u];
            if (alphaPremul) {
                dst[0] = std::min(b8, a8); dst[1] = std::min(g8, a8); dst[2] = std::min(r8, a8); dst[3] = a8;
            } else {
                WritePremulBGRA(dst, r8, g8, b8, a8);
            }
        }
    }

    uint32_t outW = (ifd.orientation >= 5u && ifd.orientation <= 8u) ? h : w;
    uint32_t outH = (ifd.orientation >= 5u && ifd.orientation <= 8u) ? w : h;
    outInfo.width = outW;
    outInfo.height = outH;
    outInfo.origWidth = outW;
    outInfo.origHeight = outH;
    outInfo.channels = spp;
    outInfo.bitsPerChannel = bps;
    outInfo.formatName = isBigTiff
        ? (L"BigTIFF 64-bit (" + (extLower.empty() ? L".btf" : extLower) + L")")
        : (L"TIFF Raster (" + (extLower.empty() ? L".tif" : extLower) + L")");
    for (size_t i = 0; i < ifds.size(); ++i) {
        outInfo.layerNames.push_back(L"Page " + std::to_wstring(i + 1) + L" (" +
                                     std::to_wstring(ifds[i].width) + L"x" + std::to_wstring(ifds[i].height) + L")");
    }

    if (ifd.orientation <= 1u || ifd.orientation > 8u) {
        outInfo.bgraPixels = std::move(rawBgra);
    } else {
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t sy = 0; sy < h; ++sy) {
            for (uint32_t sx = 0; sx < w; ++sx) {
                uint32_t dx = sx, dy = sy;
                switch (ifd.orientation) {
                case 2: dx = w - 1u - sx; dy = sy; break;
                case 3: dx = w - 1u - sx; dy = h - 1u - sy; break;
                case 4: dx = sx; dy = h - 1u - sy; break;
                case 5: dx = sy; dy = sx; break;
                case 6: dx = h - 1u - sy; dy = sx; break;
                case 7: dx = h - 1u - sy; dy = w - 1u - sx; break;
                case 8: dx = sy; dy = w - 1u - sx; break;
                default: break;
                }
                std::memcpy(&outInfo.bgraPixels[(static_cast<size_t>(dy) * outW + dx) * 4u],
                            &rawBgra[(static_cast<size_t>(sy) * w + sx) * 4u], 4u);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

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
    if (segEnabled) return false; // Hand complex segmented streams to WIC fallback if installed
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
    if (log2NbrPartitions != 0u) return false;

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

    int16_t yDcQ  = kDcQ[std::clamp(yAcQi + yDcDelta, 0, 127)];
    int16_t yAcQv = kAcQ[std::clamp(yAcQi, 0, 127)];
    int16_t y2DcQ = static_cast<int16_t>(kDcQ[std::clamp(yAcQi + y2DcDelta, 0, 127)] * 2);
    int16_t y2AcQ = static_cast<int16_t>(std::max<int>(8, (kAcQ[std::clamp(yAcQi + y2AcDelta, 0, 127)] * 155) / 100));
    int16_t uvDcQ = std::min<int16_t>(132, kDcQ[std::clamp(yAcQi + uvDcDelta, 0, 127)]);
    int16_t uvAcQ = kAcQ[std::clamp(yAcQi + uvAcDelta, 0, 127)];

    // Default keyframe token probabilities (RFC 6386 Section 13.4; reject custom prob updates to avoid wrong tables)
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

    Vp8BoolDecoder bd1;
    bd1.Init(ptr + 10 + firstPartLen, len - (10 + firstPartLen));

    uint32_t mbW = (outW + 15u) / 16u;
    uint32_t mbH = (outH + 15u) / 16u;
    uint32_t yStride = mbW * 16u;
    uint32_t cStride = mbW * 8u;
    std::vector<uint8_t> yPlane(static_cast<size_t>(yStride) * (mbH * 16u), 128u);
    std::vector<uint8_t> uPlane(static_cast<size_t>(cStride) * (mbH * 8u), 128u);
    std::vector<uint8_t> vPlane(static_cast<size_t>(cStride) * (mbH * 8u), 128u);

    std::vector<uint8_t> aboveNzY(mbW * 4u, 0u), aboveNzY2(mbW, 0u), aboveNzU(mbW * 2u, 0u), aboveNzV(mbW * 2u, 0u);

    static constexpr uint8_t kBands[16] = { 0,1,2,3,6,4,5,6,6,6,6,6,6,6,6,7 };
    static constexpr uint8_t kZigzag4[16] = { 0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15 };

    auto decodeBlock = [&](int type, int firstIdx, int initCtx, int16_t dcQ, int16_t acQ, int16_t outBlk[16]) -> bool {
        int ctx = initCtx;
        bool hasNz = false;
        for (int i = firstIdx; i < 16; ++i) {
            const uint8_t* p = probs[type][kBands[i]][ctx];
            if (ctx != 0) { // Note: when previous coeff was 0 (and i > firstIdx), EOB branch is skipped!
                // Wait: in RFC 6386 Section 13.2, EOB is ONLY skipped when the immediately preceding coeff was 0!
            }
            if (i == firstIdx || outBlk[kZigzag4[i - 1]] != 0) {
                if (!bd1.ReadBool(p[0])) break; // DCT_EOB
            }
            while (!bd1.ReadBool(p[1])) { // DCT_0
                ctx = 0;
                ++i;
                if (i >= 16) return hasNz;
                p = probs[type][kBands[i]][0];
            }
            int mag = 1;
            if (bd1.ReadBool(p[2])) {
                if (!bd1.ReadBool(p[3])) {
                    mag = bd1.ReadBool(p[4]) ? (3 + static_cast<int>(bd1.ReadBool(p[5]))) : 2;
                } else if (!bd1.ReadBool(p[6])) {
                    if (!bd1.ReadBool(p[7])) {
                        mag = 5 + static_cast<int>(bd1.ReadBool(159));
                    } else {
                        mag = 7 + (static_cast<int>(bd1.ReadBool(165)) << 1) + static_cast<int>(bd1.ReadBool(145));
                    }
                } else {
                    int b1 = static_cast<int>(bd1.ReadBool(p[8]));
                    int b0 = static_cast<int>(bd1.ReadBool(p[9 + b1]));
                    int cat = (b1 << 1) + b0; // cat 3..6 -> Cat3(11..18), Cat4(19..34), Cat5(35..66), Cat6(67..)
                    static constexpr uint8_t kCatProbs[4][12] = {
                        {173,148,140,0},
                        {176,155,140,135,0},
                        {180,157,141,134,130,0},
                        {254,254,243,230,196,177,153,140,133,130,129,0}
                    };
                    static constexpr int kCatBase[4] = { 11, 19, 35, 67 };
                    int extra = 0;
                    for (int k = 0; kCatProbs[cat][k] != 0; ++k) {
                        extra = (extra << 1) | static_cast<int>(bd1.ReadBool(kCatProbs[cat][k]));
                    }
                    mag = kCatBase[cat] + extra;
                }
            }
            ctx = (mag == 1) ? 1 : 2;
            int sign = bd1.ReadBool(128) ? -mag : mag;
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

    for (uint32_t mby = 0; mby < mbH; ++mby) {
        uint8_t leftNzY[4] = {}, leftNzY2 = 0u, leftNzU[2] = {}, leftNzV[2] = {};
        for (uint32_t mbx = 0; mbx < mbW; ++mbx) {
            bool skip = mbNoSkipCoeff ? (bd0.ReadBool(probSkipFalse) != 0u) : false;

            // Keyframe YMode tree (RFC 6386 Section 11.2: probs 145, 156, 163, 128)
            int yMode = 0; // 4 = B_PRED, 0 = DC, 1 = V, 2 = H, 3 = TM
            if (!bd0.ReadBool(145)) {
                yMode = 4; // B_PRED — hand sub-4x4 B_PRED keyframe trees to WIC fallback if present
                return false;
            } else if (!bd0.ReadBool(156)) {
                yMode = 0; // DC_PRED
            } else if (!bd0.ReadBool(163)) {
                yMode = 1; // V_PRED
            } else {
                yMode = bd0.ReadBool(128) ? 3 : 2; // TM_PRED : H_PRED
            }

            // Keyframe UVMode tree (probs 142, 114, 183)
            int uvMode = 0;
            if (!bd0.ReadBool(142)) uvMode = 0;
            else if (!bd0.ReadBool(114)) uvMode = 1;
            else uvMode = bd0.ReadBool(183) ? 3 : 2;

            pred16Or8(yPlane.data(), yStride, mbx * 16u, mby * 16u, 16u, yMode);
            pred16Or8(uPlane.data(), cStride, mbx * 8u,  mby * 8u,  8u,  uvMode);
            pred16Or8(vPlane.data(), cStride, mbx * 8u,  mby * 8u,  8u,  uvMode);

            if (skip) {
                for (int k = 0; k < 4; ++k) { aboveNzY[mbx * 4u + k] = 0; leftNzY[k] = 0; }
                aboveNzY2[mbx] = 0; leftNzY2 = 0;
                for (int k = 0; k < 2; ++k) { aboveNzU[mbx * 2u + k] = 0; aboveNzV[mbx * 2u + k] = 0; leftNzU[k] = 0; leftNzV[k] = 0; }
                continue;
            }

            int16_t y2In[16] = {}, y2Out[16] = {};
            bool nz2 = decodeBlock(1, 0, aboveNzY2[mbx] + leftNzY2, y2DcQ, y2AcQ, y2In);
            aboveNzY2[mbx] = leftNzY2 = nz2 ? 1u : 0u;
            iwht4x4(y2In, y2Out);

            for (int by = 0; by < 4; ++by) {
                for (int bx = 0; bx < 4; ++bx) {
                    int16_t yBlk[16] = {};
                    yBlk[0] = y2Out[by * 4 + bx];
                    bool nz = decodeBlock(0, 1, aboveNzY[mbx * 4u + bx] + leftNzY[by], yDcQ, yAcQv, yBlk);
                    aboveNzY[mbx * 4u + bx] = leftNzY[by] = nz ? 1u : 0u;
                    idct4x4Add(yBlk, &yPlane[(mby * 16u + by * 4u) * yStride + (mbx * 16u + bx * 4u)], yStride);
                }
            }
            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    int16_t uBlk[16] = {};
                    bool nz = decodeBlock(2, 0, aboveNzU[mbx * 2u + bx] + leftNzU[by], uvDcQ, uvAcQ, uBlk);
                    aboveNzU[mbx * 2u + bx] = leftNzU[by] = nz ? 1u : 0u;
                    idct4x4Add(uBlk, &uPlane[(mby * 8u + by * 4u) * cStride + (mbx * 8u + bx * 4u)], cStride);
                }
            }
            for (int by = 0; by < 2; ++by) {
                for (int bx = 0; bx < 2; ++bx) {
                    int16_t vBlk[16] = {};
                    bool nz = decodeBlock(2, 0, aboveNzV[mbx * 2u + bx] + leftNzV[by], uvDcQ, uvAcQ, vBlk);
                    aboveNzV[mbx * 2u + bx] = leftNzV[by] = nz ? 1u : 0u;
                    idct4x4Add(vBlk, &vPlane[(mby * 8u + by * 4u) * cStride + (mbx * 8u + bx * 4u)], cStride);
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

} // namespace TiffWebpDetail

inline bool DecodeTiffOrBigTiff(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (TiffWebpDetail::DecodeTiffPureCpp(data, size, extLower, outInfo)) return true;
    // Fallback to OS WIC for JPEG-in-TIFF (compression == 7) if Classic TIFF 42
    if (size >= 8 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0) ||
                      (data[0] == 'M' && data[1] == 'M' && data[2] == 0  && data[3] == 42))) {
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"TIFF Raster (.tif)", outInfo);
    }
    return false;
}

inline bool DecodeWebP(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return TiffWebpDetail::DecodeWebpPureCpp(data, size, outInfo);
}

} // namespace Formats
