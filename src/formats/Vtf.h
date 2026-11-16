#pragma once
// Vtf.h — Valve Texture Format (.vtf, Source Engine / Strata Source v7.0–v7.6) Decoder
// Supports: RGBA8888, ABGR8888, RGB888, BGR888, RGB565, I8, IA88, P8, A8, Bluescreen RGB/BGR,
//           ARGB8888, BGRA8888, BGRX8888, BGR565, BGRX5551, BGRA4444, BGRA5551, UV88, UVWQ8888,
//           DXT1, DXT1_ONEBITALPHA, DXT3, DXT5, ATI1N (BC4, 38), ATI2N (BC5, 37), BC7 (68), BC6H (69),
//           RGBA16161616F (24), RGBA16161616 (25), R32F (27), RGB323232F (28), RGBA32323232F (29),
//           and VTF v7.6 'AXC' (AUX) Zlib/Deflate & Zstd per-mip resource decompression.

namespace Formats {

namespace DetailVtf {

inline bool ComputeVtfSurfaceBytes(uint32_t w, uint32_t h, int32_t fmt, size_t& outBytes) {
    size_t px = static_cast<size_t>(w) * h;
    size_t bx = (w + 3u) / 4u;
    size_t by = (h + 3u) / 4u;
    switch (fmt) {
    case 0:  // RGBA8888
    case 1:  // ABGR8888
    case 11: // ARGB8888
    case 12: // BGRA8888
    case 16: // BGRX8888
    case 23: // UVWQ8888
    case 26: // UVLX8888
    case 27: // R32F (4 bytes/px)
        outBytes = px * 4u;
        return true;
    case 2:  // RGB888
    case 3:  // BGR888
    case 9:  // RGB888_BLUESCREEN
    case 10: // BGR888_BLUESCREEN
        outBytes = px * 3u;
        return true;
    case 4:  // RGB565
    case 6:  // IA88
    case 17: // BGR565
    case 18: // BGRX5551
    case 19: // BGRA4444
    case 21: // BGRA5551
    case 22: // UV88
        outBytes = px * 2u;
        return true;
    case 5:  // I8
    case 7:  // P8
    case 8:  // A8
        outBytes = px;
        return true;
    case 13: // DXT1
    case 20: // DXT1_ONEBITALPHA
    case 38: // ATI1N (BC4)
        outBytes = bx * by * 8u;
        return true;
    case 14: // DXT3
    case 15: // DXT5
    case 37: // ATI2N (BC5)
    case 68: // Strata Source BC7
    case 69: // Strata Source BC6H
        outBytes = bx * by * 16u;
        return true;
    case 24: // RGBA16161616F
    case 25: // RGBA16161616
        outBytes = px * 8u;
        return true;
    case 28: // RGB323232F
        outBytes = px * 12u;
        return true;
    case 29: // RGBA32323232F
        outBytes = px * 16u;
        return true;
    default:
        outBytes = 0;
        return false;
    }
}

} // namespace DetailVtf

inline bool DecodeVTF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64 || std::memcmp(data, "VTF\0", 4) != 0) return false;

    uint32_t verMajor  = ReadU32LE(data + 4);
    uint32_t verMinor  = ReadU32LE(data + 8);
    uint32_t hdrSize   = ReadU32LE(data + 12);
    uint16_t w16       = ReadU16LE(data + 16);
    uint16_t h16       = ReadU16LE(data + 18);
    uint32_t flags     = ReadU32LE(data + 20);
    uint16_t rawFrames = ReadU16LE(data + 24);
    int32_t  imgFmt    = static_cast<int32_t>(ReadU32LE(data + 52));
    uint8_t  rawMips   = data[56];
    int32_t  lowFmt    = static_cast<int32_t>(ReadU32LE(data + 57));
    uint8_t  lowW      = data[61];
    uint8_t  lowH      = data[62];

    if (verMajor != 7 || verMinor > 6) return false;
    if (w16 == 0 || h16 == 0 || w16 > 8192 || h16 > 8192) return false;
    if (hdrSize < 64 || hdrSize > size) return false;

    uint32_t w = w16;
    uint32_t h = h16;
    uint32_t frames = std::max<uint32_t>(1u, rawFrames);
    uint32_t mipCount = std::max<uint32_t>(1u, rawMips);
    if (mipCount > 20 || frames > 4096) return false;

    uint32_t slices = 1u;
    if ((verMinor >= 2) && hdrSize >= 80 && size >= 65) {
        slices = std::max<uint32_t>(1u, ReadU16LE(data + 63));
    }
    uint32_t faces = ((flags & 0x00004000u) != 0) ? 6u : 1u;

    size_t topSliceBytes = 0;
    if (!DetailVtf::ComputeVtfSurfaceBytes(w, h, imgFmt, topSliceBytes) || topSliceBytes == 0) {
        return false;
    }

    size_t highResStart = 0;
    bool foundRes30 = false;
    size_t auxResOff = 0;
    bool foundAuxRes = false;

    if (verMinor >= 3 && hdrSize >= 80) {
        uint32_t numRes = ReadU32LE(data + 68);
        if (numRes > 0 && numRes <= 64 && 80u + static_cast<size_t>(numRes) * 8u <= hdrSize) {
            for (uint32_t r = 0; r < numRes; ++r) {
                const uint8_t* ent = data + 80 + static_cast<size_t>(r) * 8u;
                bool inlineData = (ent[3] & 0x02u) != 0u;
                if (ent[0] == 0x30 && ent[1] == 0x00 && ent[2] == 0x00 && !inlineData) {
                    highResStart = ReadU32LE(ent + 4);
                    foundRes30 = true;
                } else if (ent[0] == 'A' && ent[1] == 'X' && ent[2] == 'C' && !inlineData) {
                    auxResOff = ReadU32LE(ent + 4);
                    foundAuxRes = true;
                }
            }
        }
    }
    if (!foundRes30) {
        size_t lowBytes = 0;
        if (lowW > 0 && lowH > 0 && lowFmt >= 0) {
            if (!DetailVtf::ComputeVtfSurfaceBytes(lowW, lowH, lowFmt, lowBytes)) {
                lowBytes = 0;
            }
        }
        highResStart = static_cast<size_t>(hdrSize) + lowBytes;
    }
    if (highResStart > size) return false;

    std::vector<uint8_t> inflatedMip0;
    const uint8_t* src = nullptr;

    // VTF v7.6 'AXC' AUX Deflate / Zstd per-mip compression table
    bool usedAuxCompression = false;
    if (foundAuxRes && auxResOff >= 64u && auxResOff + 8u <= size) {
        uint32_t auxTotalBytes = ReadU32LE(data + auxResOff + 0);
        int32_t  compLevel     = static_cast<int32_t>(ReadU32LE(data + auxResOff + 4));
        uint64_t totalEntries  = static_cast<uint64_t>(mipCount) * frames * faces;
        if (compLevel != 0 && auxTotalBytes >= 8u + totalEntries * 4ull &&
            auxTotalBytes <= size - auxResOff && totalEntries <= 65536ull)
        {
            const uint8_t* lenTbl = data + auxResOff + 8u;
            // Disk order in VTF: mip = (mipCount-1) down to 0, frame = 0..frames-1, face = 0..faces-1
            // So (mip=0, frame=0, face=0) is at entry index (mipCount - 1) * frames * faces!
            size_t targetIdx = static_cast<size_t>(mipCount - 1u) * frames * faces;
            uint64_t compBefore = 0;
            for (size_t idx = 0; idx < targetIdx; ++idx) {
                compBefore += ReadU32LE(lenTbl + idx * 4u);
            }
            uint32_t compMip0Len = ReadU32LE(lenTbl + targetIdx * 4u);
            if (compMip0Len == 0 || compBefore > size - highResStart || compMip0Len > (size - highResStart) - compBefore) {
                return false;
            }
            const uint8_t* compPtr = data + highResStart + static_cast<size_t>(compBefore);
            if (compMip0Len == topSliceBytes) {
                src = compPtr;
            } else {
                bool okDec = false;
                if (compMip0Len >= 4u && ReadU32LE(compPtr) == 0xFD2FB528u) {
                    okDec = StreamCodecs::DecompressZstdFrame(compPtr, compMip0Len, inflatedMip0, topSliceBytes + 64u);
                } else {
                    okDec = ZipDeflate::InflateZlib(compPtr, compMip0Len, inflatedMip0, topSliceBytes + 64u) ||
                            ZipDeflate::InflateRaw(compPtr, compMip0Len, inflatedMip0, topSliceBytes + 64u);
                }
                if (!okDec || inflatedMip0.size() < topSliceBytes) return false;
                src = inflatedMip0.data();
            }
            usedAuxCompression = true;
        }
    }

    if (!usedAuxCompression) {
        auto sumSmallerMips = [&](uint32_t faceCnt) -> size_t {
            size_t total = 0;
            for (uint32_t m = mipCount - 1; m >= 1; --m) {
                uint32_t mw = std::max(1u, w >> m);
                uint32_t mh = std::max(1u, h >> m);
                uint32_t ms = std::max(1u, slices >> m);
                size_t surfBytes = 0;
                DetailVtf::ComputeVtfSurfaceBytes(mw, mh, imgFmt, surfBytes);
                total += surfBytes * frames * faceCnt * ms;
            }
            return total;
        };

        size_t smallerBytes = (mipCount > 1) ? sumSmallerMips(faces) : 0;
        if (faces == 6 && verMinor >= 1 && verMinor <= 4) {
            size_t smaller7 = (mipCount > 1) ? sumSmallerMips(7u) : 0;
            size_t topAll7 = topSliceBytes * frames * 7u * slices;
            if (highResStart + smaller7 + topAll7 == size) {
                smallerBytes = smaller7;
            }
        }

        size_t mip0Off = highResStart + smallerBytes;
        if (mip0Off > size || topSliceBytes > size - mip0Off) return false;
        src = data + mip0Off;
    }

    size_t pixelCount = static_cast<size_t>(w) * h;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;

    if (imgFmt == 38 || imgFmt == 5 || imgFmt == 7 || imgFmt == 27) outInfo.channels = 1;
    else if (imgFmt == 37 || imgFmt == 22 || imgFmt == 6) outInfo.channels = 2;
    else if (imgFmt == 2 || imgFmt == 3 || imgFmt == 4 || imgFmt == 13 || imgFmt == 16 || imgFmt == 17 || imgFmt == 18 || imgFmt == 28 || imgFmt == 69) outInfo.channels = 3;
    else outInfo.channels = 4;

    outInfo.bitsPerChannel = (imgFmt == 27 || imgFmt == 28 || imgFmt == 29) ? 32 :
                             ((imgFmt == 24 || imgFmt == 25 || imgFmt == 69) ? 16 : 8);
    outInfo.formatName = usedAuxCompression
        ? L"Valve Texture Format v7.6 Deflate AUX (.vtf)"
        : L"Valve Texture Format (.vtf)";
    outInfo.bgraPixels.resize(pixelCount * 4u);

    if (imgFmt == 13 || imgFmt == 20 || imgFmt == 14 || imgFmt == 15 || imgFmt == 37 || imgFmt == 38 || imgFmt == 68) {
        uint32_t bw = (w + 3u) / 4u;
        uint32_t bh = (h + 3u) / 4u;
        size_t blockStride = (imgFmt == 13 || imgFmt == 20 || imgFmt == 38) ? 8u : 16u;
        uint8_t blk[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                if (imgFmt == 13)      GpuTex::DecodeBC1Block(bp, blk, true, true);
                else if (imgFmt == 20) GpuTex::DecodeBC1Block(bp, blk, true, false);
                else if (imgFmt == 14) GpuTex::DecodeBC2Block(bp, blk);
                else if (imgFmt == 15) GpuTex::DecodeBC3Block(bp, blk);
                else if (imgFmt == 38) GpuTex::DecodeBC4Block(bp, blk, false); // ATI1N = BC4 UNORM
                else if (imgFmt == 37) GpuTex::DecodeBC5Block(bp, blk, false); // ATI2N = BC5 UNORM Normal
                else                   GpuTex::DecodeBC7Block(bp, blk);        // BC7
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                bp += blockStride;
            }
        }
    } else if (imgFmt == 69) { // BC6H HDR
        uint32_t bw = (w + 3u) / 4u;
        uint32_t bh = (h + 3u) / 4u;
        std::vector<float> flt(pixelCount * 4u, 0.0f);
        float blkF[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                GpuTex::DecodeBC6HBlock(bp, blkF, false);
                for (uint32_t py = 0; py < 4u; ++py) {
                    uint32_t dy = by * 4u + py;
                    if (dy >= h) continue;
                    for (uint32_t px = 0; px < 4u; ++px) {
                        uint32_t dx = bx * 4u + px;
                        if (dx >= w) continue;
                        std::memcpy(&flt[(static_cast<size_t>(dy) * w + dx) * 4u], &blkF[(py * 4u + px) * 4u], 16u);
                    }
                }
                bp += 16u;
            }
        }
        GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, true, outInfo.bgraPixels);
    } else if (imgFmt == 24 || imgFmt == 27 || imgFmt == 28 || imgFmt == 29) { // Float16 / Float32 HDR
        std::vector<float> rgba(pixelCount * 4u, 1.0f);
        for (size_t i = 0; i < pixelCount; ++i) {
            if (imgFmt == 24) {
                rgba[i * 4 + 0] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 0));
                rgba[i * 4 + 1] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 2));
                rgba[i * 4 + 2] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 4));
                rgba[i * 4 + 3] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 6));
            } else if (imgFmt == 27) { // R32F
                float v = GpuTex::ReadF32LE(src + i * 4);
                rgba[i * 4 + 0] = v; rgba[i * 4 + 1] = v; rgba[i * 4 + 2] = v; rgba[i * 4 + 3] = 1.0f;
            } else if (imgFmt == 28) { // RGB323232F
                rgba[i * 4 + 0] = GpuTex::ReadF32LE(src + i * 12 + 0);
                rgba[i * 4 + 1] = GpuTex::ReadF32LE(src + i * 12 + 4);
                rgba[i * 4 + 2] = GpuTex::ReadF32LE(src + i * 12 + 8);
                rgba[i * 4 + 3] = 1.0f;
            } else { // RGBA32323232F
                rgba[i * 4 + 0] = GpuTex::ReadF32LE(src + i * 16 + 0);
                rgba[i * 4 + 1] = GpuTex::ReadF32LE(src + i * 16 + 4);
                rgba[i * 4 + 2] = GpuTex::ReadF32LE(src + i * 16 + 8);
                rgba[i * 4 + 3] = GpuTex::ReadF32LE(src + i * 16 + 12);
            }
        }
        bool anyHdrOver1 = false;
        for (size_t i = 0; i < pixelCount * 4u; i += 4u) {
            if (rgba[i] > 1.05f || rgba[i + 1] > 1.05f || rgba[i + 2] > 1.05f) { anyHdrOver1 = true; break; }
        }
        GpuTex::ConvertFloatRgbaToBgra(rgba.data(), w, h, anyHdrOver1, outInfo.bgraPixels);
    } else if (imgFmt == 25) { // RGBA16161616 UNORM
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t r = static_cast<uint8_t>(ReadU16LE(src + i * 8 + 0) >> 8);
            uint8_t g = static_cast<uint8_t>(ReadU16LE(src + i * 8 + 2) >> 8);
            uint8_t b = static_cast<uint8_t>(ReadU16LE(src + i * 8 + 4) >> 8);
            uint8_t a = static_cast<uint8_t>(ReadU16LE(src + i * 8 + 6) >> 8);
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (imgFmt) {
            case 0: // RGBA8888
                r = src[i * 4 + 0]; g = src[i * 4 + 1]; b = src[i * 4 + 2]; a = src[i * 4 + 3]; break;
            case 1: // ABGR8888
                a = src[i * 4 + 0]; b = src[i * 4 + 1]; g = src[i * 4 + 2]; r = src[i * 4 + 3]; break;
            case 2: // RGB888
                r = src[i * 3 + 0]; g = src[i * 3 + 1]; b = src[i * 3 + 2]; break;
            case 3: // BGR888
                b = src[i * 3 + 0]; g = src[i * 3 + 1]; r = src[i * 3 + 2]; break;
            case 4: // RGB565
                GpuTex::UnpackRgb565(ReadU16LE(src + i * 2), r, g, b); break;
            case 5: // I8
            case 7: // P8
                r = g = b = src[i]; break;
            case 6: // IA88
                r = g = b = src[i * 2 + 0]; a = src[i * 2 + 1]; break;
            case 8: // A8
                r = g = b = 255; a = src[i]; break;
            case 9: // RGB888_BLUESCREEN
                r = src[i * 3 + 0]; g = src[i * 3 + 1]; b = src[i * 3 + 2];
                if (r == 0 && g == 0 && b == 255) a = 0;
                break;
            case 10: // BGR888_BLUESCREEN
                b = src[i * 3 + 0]; g = src[i * 3 + 1]; r = src[i * 3 + 2];
                if (r == 0 && g == 0 && b == 255) a = 0;
                break;
            case 11: // ARGB8888
                a = src[i * 4 + 0]; r = src[i * 4 + 1]; g = src[i * 4 + 2]; b = src[i * 4 + 3]; break;
            case 12: // BGRA8888
                b = src[i * 4 + 0]; g = src[i * 4 + 1]; r = src[i * 4 + 2]; a = src[i * 4 + 3]; break;
            case 16: // BGRX8888
                b = src[i * 4 + 0]; g = src[i * 4 + 1]; r = src[i * 4 + 2]; a = 255; break;
            case 17: { // BGR565
                uint16_t v = ReadU16LE(src + i * 2);
                b = static_cast<uint8_t>(((v >> 11) & 31u) * 255u / 31u);
                g = static_cast<uint8_t>(((v >> 5)  & 63u) * 255u / 63u);
                r = static_cast<uint8_t>((v & 31u) * 255u / 31u);
                break;
            }
            case 18:
            case 21: { // BGRX5551 / BGRA5551
                uint16_t v = ReadU16LE(src + i * 2);
                b = static_cast<uint8_t>((v & 31u) * 255u / 31u);
                g = static_cast<uint8_t>(((v >> 5) & 31u) * 255u / 31u);
                r = static_cast<uint8_t>(((v >> 10) & 31u) * 255u / 31u);
                a = (imgFmt == 21) ? (((v & 0x8000u) != 0u) ? 255u : 0u) : 255u;
                break;
            }
            case 19: { // BGRA4444
                uint16_t v = ReadU16LE(src + i * 2);
                b = static_cast<uint8_t>((v & 15u) * 17u);
                g = static_cast<uint8_t>(((v >> 4) & 15u) * 17u);
                r = static_cast<uint8_t>(((v >> 8) & 15u) * 17u);
                a = static_cast<uint8_t>(((v >> 12) & 15u) * 17u);
                break;
            }
            case 22: // UV88 signed normal
                r = static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(src[i * 2 + 0])) + 128);
                g = static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(src[i * 2 + 1])) + 128);
                b = 255;
                break;
            case 23:
            case 26: // UVWQ8888 / UVLX8888
                r = static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(src[i * 4 + 0])) + 128);
                g = static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(src[i * 4 + 1])) + 128);
                b = static_cast<uint8_t>(static_cast<int>(static_cast<int8_t>(src[i * 4 + 2])) + 128);
                a = 255;
                break;
            default:
                return false;
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
