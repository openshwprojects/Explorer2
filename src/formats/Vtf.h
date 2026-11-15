#pragma once
// Vtf.h — Valve Texture Format (.vtf, Source Engine 7.0–7.5) Decoder

namespace Formats {

namespace DetailVtf {

inline bool ComputeVtfSurfaceBytes(uint32_t w, uint32_t h, int32_t fmt, size_t& outBytes) {
    size_t px = static_cast<size_t>(w) * h;
    size_t bx = (w + 3u) / 4u;
    size_t by = (h + 3u) / 4u;
    switch (fmt) {
    case 0:  // RGBA8888
    case 1:  // ABGR8888
    case 12: // BGRA8888
    case 16: // BGRX8888
        outBytes = px * 4;
        return true;
    case 2:  // RGB888
    case 3:  // BGR888
        outBytes = px * 3;
        return true;
    case 4:  // RGB565
    case 6:  // IA88
        outBytes = px * 2;
        return true;
    case 5:  // I8
        outBytes = px;
        return true;
    case 13: // DXT1
    case 20: // DXT1_ONEBITALPHA
        outBytes = bx * by * 8;
        return true;
    case 14: // DXT3
    case 15: // DXT5
        outBytes = bx * by * 16;
        return true;
    case 24: // RGBA16161616F
        outBytes = px * 8;
        return true;
    default:
        outBytes = 0;
        return false;
    }
}

} // namespace DetailVtf

inline bool DecodeVTF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 64 || std::memcmp(data, "VTF\0", 4) != 0) return false;

    uint32_t verMajor = ReadU32LE(data + 4);
    uint32_t verMinor = ReadU32LE(data + 8);
    uint32_t hdrSize  = ReadU32LE(data + 12);
    uint16_t w16      = ReadU16LE(data + 16);
    uint16_t h16      = ReadU16LE(data + 18);
    uint32_t flags    = ReadU32LE(data + 20);
    uint16_t rawFrames = ReadU16LE(data + 24);
    int32_t  imgFmt   = static_cast<int32_t>(ReadU32LE(data + 52));
    uint8_t  rawMips  = data[56];
    int32_t  lowFmt   = static_cast<int32_t>(ReadU32LE(data + 57));
    uint8_t  lowW     = data[61];
    uint8_t  lowH     = data[62];

    if (verMajor != 7 || verMinor > 6) return false;
    if (w16 == 0 || h16 == 0 || w16 > 8192 || h16 > 8192) return false;
    if (hdrSize < 64 || hdrSize > size) return false;

    uint32_t w = w16;
    uint32_t h = h16;
    uint32_t frames = std::max<uint32_t>(1u, rawFrames);
    uint32_t mipCount = std::max<uint32_t>(1u, rawMips);
    if (mipCount > 20) return false;

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
    if (verMinor >= 3 && hdrSize >= 80) {
        uint32_t numRes = ReadU32LE(data + 68);
        if (numRes > 0 && numRes <= 64 && 80u + static_cast<size_t>(numRes) * 8u <= hdrSize) {
            for (uint32_t r = 0; r < numRes; ++r) {
                const uint8_t* ent = data + 80 + static_cast<size_t>(r) * 8;
                if (ent[0] == 0x30 && ent[1] == 0x00 && ent[2] == 0x00 && (ent[3] & 0x02u) == 0) {
                    highResStart = ReadU32LE(ent + 4);
                    foundRes30 = true;
                    break;
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
    // Handle VTF v7.1–7.4 cubemaps that included a 7th spheremap face
    if (faces == 6 && verMinor >= 1 && verMinor <= 4) {
        size_t smaller7 = (mipCount > 1) ? sumSmallerMips(7u) : 0;
        size_t topAll7 = topSliceBytes * frames * 7u * slices;
        if (highResStart + smaller7 + topAll7 == size) {
            smallerBytes = smaller7;
        }
    }

    size_t mip0Off = highResStart + smallerBytes;
    if (mip0Off > size || topSliceBytes > size - mip0Off) return false;

    const uint8_t* src = data + mip0Off;
    size_t pixelCount = static_cast<size_t>(w) * h;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = (imgFmt == 2 || imgFmt == 3 || imgFmt == 4 || imgFmt == 5 || imgFmt == 13 || imgFmt == 16) ? 3 : 4;
    outInfo.bitsPerChannel = (imgFmt == 24) ? 16 : 8;
    outInfo.formatName = L"Valve Texture Format (.vtf)";
    outInfo.bgraPixels.resize(pixelCount * 4);

    if (imgFmt == 13 || imgFmt == 20 || imgFmt == 14 || imgFmt == 15) {
        uint32_t bw = (w + 3u) / 4u;
        uint32_t bh = (h + 3u) / 4u;
        size_t blockStride = (imgFmt == 13 || imgFmt == 20) ? 8 : 16;
        uint8_t blk[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                if (imgFmt == 13)      GpuTex::DecodeBC1Block(bp, blk, true, true);
                else if (imgFmt == 20) GpuTex::DecodeBC1Block(bp, blk, true, false);
                else if (imgFmt == 14) GpuTex::DecodeBC2Block(bp, blk);
                else                   GpuTex::DecodeBC3Block(bp, blk);
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                bp += blockStride;
            }
        }
    } else if (imgFmt == 24) {
        std::vector<float> rgba(pixelCount * 4);
        for (size_t i = 0; i < pixelCount; ++i) {
            rgba[i * 4 + 0] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 0));
            rgba[i * 4 + 1] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 2));
            rgba[i * 4 + 2] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 4));
            rgba[i * 4 + 3] = GpuTex::HalfToFloat(ReadU16LE(src + i * 8 + 6));
        }
        GpuTex::ConvertFloatRgbaToBgra(rgba.data(), w, h, false, outInfo.bgraPixels);
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (imgFmt) {
            case 0: // RGBA8888
                r = src[i * 4 + 0]; g = src[i * 4 + 1]; b = src[i * 4 + 2]; a = src[i * 4 + 3];
                break;
            case 1: // ABGR8888
                a = src[i * 4 + 0]; b = src[i * 4 + 1]; g = src[i * 4 + 2]; r = src[i * 4 + 3];
                break;
            case 2: // RGB888
                r = src[i * 3 + 0]; g = src[i * 3 + 1]; b = src[i * 3 + 2];
                break;
            case 3: // BGR888
                b = src[i * 3 + 0]; g = src[i * 3 + 1]; r = src[i * 3 + 2];
                break;
            case 4: // RGB565
                GpuTex::UnpackRgb565(ReadU16LE(src + i * 2), r, g, b);
                break;
            case 5: // I8
                r = g = b = src[i];
                break;
            case 6: // IA88
                r = g = b = src[i * 2 + 0]; a = src[i * 2 + 1];
                break;
            case 12: // BGRA8888
                b = src[i * 4 + 0]; g = src[i * 4 + 1]; r = src[i * 4 + 2]; a = src[i * 4 + 3];
                break;
            case 16: // BGRX8888
                b = src[i * 4 + 0]; g = src[i * 4 + 1]; r = src[i * 4 + 2]; a = 255;
                break;
            default:
                return false;
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
