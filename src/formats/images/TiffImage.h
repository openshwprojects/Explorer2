#pragma once
// TiffImage.h — Classic TIFF (42) & 64-bit BigTIFF (43) Decoder (.tif, .tiff, .btf, .tf8)

namespace Formats {

namespace TiffDetail {

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

} // namespace TiffDetail

inline bool DecodeTiffOrBigTiff(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (TiffDetail::DecodeTiffPureCpp(data, size, extLower, outInfo)) return true;
    // Fallback to OS WIC for JPEG-in-TIFF (compression == 7) if Classic TIFF 42
    if (size >= 8 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0) ||
                      (data[0] == 'M' && data[1] == 'M' && data[2] == 0  && data[3] == 42))) {
        return DetailImg100::DecodeEmbeddedWicBuffer(data, size, L"TIFF Raster (.tif)", outInfo);
    }
    return false;
}

} // namespace Formats
