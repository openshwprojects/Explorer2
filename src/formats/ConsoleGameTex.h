#pragma once
// ConsoleGameTex.h — #63 Console & Modern Game Engine GPU Textures
// Covers:
//   1. Nintendo Switch Binary Texture (.bntx — "BNTX" + "BRTI" + NVIDIA Tegra X1 GOB Block-Linear Deswizzler)
//   2. PlayStation Vita SceGxm Texture (.gxt — "GXT\0" + Morton 2D Z-Order Deswizzler)
//   3. PlayStation 3 Cell GCM Texture (.gtf — Big-Endian CellGcmTexture LN/SZ + DXT1/3/5 & A8R8G8B8)
//   4. Bandai Namco Texture Container (.nut — "NTP3" / "NTWU" / "NTWD")
//   5. Capcom RE Engine / MT Framework Texture (.tex — binary "TEX\0" / "\0XET")
//   6. Valve Source 2 Compiled Texture (.vtex_c — ResourceHeader v12 + "DATA" VTex_t)

namespace Formats {

namespace ConsoleTexDetail {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(RdU32LE(p));
}
inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

enum class GpuSurfaceCodec {
    None,
    RGBA8,
    BGRA8,
    ARGB8,
    ABGR8,
    RGB565,
    R8,
    RGBA16F,
    BC1,
    BC2,
    BC3,
    BC4_U,
    BC4_S,
    BC5_U,
    BC5_S,
    BC6H_U,
    BC6H_S,
    BC7,
    ETC2_RGB,
    ETC2_RGBA,
    ASTC_4x4
};

inline bool GetCodecBlockLayout(GpuSurfaceCodec c, uint32_t& outBlockW, uint32_t& outBlockH, uint32_t& outBytesPerBlock) {
    outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 4u;
    switch (c) {
        case GpuSurfaceCodec::RGBA8:
        case GpuSurfaceCodec::BGRA8:
        case GpuSurfaceCodec::ARGB8:
        case GpuSurfaceCodec::ABGR8:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 4u; return true;
        case GpuSurfaceCodec::RGB565:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 2u; return true;
        case GpuSurfaceCodec::R8:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 1u; return true;
        case GpuSurfaceCodec::RGBA16F:
            outBlockW = 1u; outBlockH = 1u; outBytesPerBlock = 8u; return true;
        case GpuSurfaceCodec::BC1:
        case GpuSurfaceCodec::BC4_U:
        case GpuSurfaceCodec::BC4_S:
        case GpuSurfaceCodec::ETC2_RGB:
            outBlockW = 4u; outBlockH = 4u; outBytesPerBlock = 8u; return true;
        case GpuSurfaceCodec::BC2:
        case GpuSurfaceCodec::BC3:
        case GpuSurfaceCodec::BC5_U:
        case GpuSurfaceCodec::BC5_S:
        case GpuSurfaceCodec::BC6H_U:
        case GpuSurfaceCodec::BC6H_S:
        case GpuSurfaceCodec::BC7:
        case GpuSurfaceCodec::ETC2_RGBA:
        case GpuSurfaceCodec::ASTC_4x4:
            outBlockW = 4u; outBlockH = 4u; outBytesPerBlock = 16u; return true;
        default:
            return false;
    }
}

// Decode linear surface of blocks/texels into premultiplied BGRA8
inline bool DecodeLinearGpuSurface(
    const uint8_t* src,
    size_t srcLen,
    uint32_t w,
    uint32_t h,
    GpuSurfaceCodec codec,
    std::vector<uint8_t>& outBgra)
{
    uint32_t blkW = 1, blkH = 1, bpb = 4;
    if (!GetCodecBlockLayout(codec, blkW, blkH, bpb) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;
    uint64_t needBytes = static_cast<uint64_t>(gridW) * gridH * bpb;
    if (needBytes > srcLen) return false;

    if (codec == GpuSurfaceCodec::ASTC_4x4) {
        return DecodeAstcSurface(src, srcLen, w, h, 4, 4, outBgra);
    }

    size_t pxTotal = static_cast<size_t>(w) * h;
    if (codec == GpuSurfaceCodec::RGBA16F) {
        std::vector<float> flt(pxTotal * 4u, 0.0f);
        for (size_t i = 0; i < pxTotal; ++i) {
            const uint8_t* sp = src + i * 8u;
            flt[i * 4 + 0] = GpuTex::HalfToFloat(RdU16LE(sp + 0));
            flt[i * 4 + 1] = GpuTex::HalfToFloat(RdU16LE(sp + 2));
            flt[i * 4 + 2] = GpuTex::HalfToFloat(RdU16LE(sp + 4));
            flt[i * 4 + 3] = GpuTex::HalfToFloat(RdU16LE(sp + 6));
        }
        GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, false, outBgra);
        return true;
    }
    if (codec == GpuSurfaceCodec::BC6H_U || codec == GpuSurfaceCodec::BC6H_S) {
        std::vector<float> flt(pxTotal * 4u, 0.0f);
        float blkF[64];
        for (uint32_t by = 0; by < gridH; ++by) {
            for (uint32_t bx = 0; bx < gridW; ++bx) {
                GpuTex::DecodeBC6HBlock(src + (static_cast<size_t>(by) * gridW + bx) * 16u, blkF, codec == GpuSurfaceCodec::BC6H_S);
                for (uint32_t py = 0; py < 4u; ++py) {
                    uint32_t dy = by * 4u + py;
                    if (dy >= h) continue;
                    for (uint32_t px = 0; px < 4u; ++px) {
                        uint32_t dx = bx * 4u + px;
                        if (dx >= w) continue;
                        std::memcpy(&flt[(static_cast<size_t>(dy) * w + dx) * 4u], &blkF[(py * 4u + px) * 4u], 16u);
                    }
                }
            }
        }
        GpuTex::ConvertFloatRgbaToBgra(flt.data(), w, h, true, outBgra);
        return true;
    }

    outBgra.assign(pxTotal * 4u, 0u);
    if (blkW == 1u && blkH == 1u) {
        for (size_t i = 0; i < pxTotal; ++i) {
            const uint8_t* sp = src + i * bpb;
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            switch (codec) {
                case GpuSurfaceCodec::RGBA8: r = sp[0]; g = sp[1]; b = sp[2]; a = sp[3]; break;
                case GpuSurfaceCodec::BGRA8: b = sp[0]; g = sp[1]; r = sp[2]; a = sp[3]; break;
                case GpuSurfaceCodec::ARGB8: a = sp[0]; r = sp[1]; g = sp[2]; b = sp[3]; break;
                case GpuSurfaceCodec::ABGR8: a = sp[0]; b = sp[1]; g = sp[2]; r = sp[3]; break;
                case GpuSurfaceCodec::RGB565: {
                    uint16_t v = RdU16LE(sp);
                    r = static_cast<uint8_t>(((v >> 11) & 31u) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >> 5)  & 63u) * 255u / 63u);
                    b = static_cast<uint8_t>((v & 31u) * 255u / 31u);
                    break;
                }
                case GpuSurfaceCodec::R8: r = g = b = sp[0]; break;
                default: return false;
            }
            WritePremulBGRA(&outBgra[i * 4u], r, g, b, a);
        }
        return true;
    }

    uint8_t blkRgba[64];
    for (uint32_t by = 0; by < gridH; ++by) {
        for (uint32_t bx = 0; bx < gridW; ++bx) {
            const uint8_t* bp = src + (static_cast<size_t>(by) * gridW + bx) * bpb;
            switch (codec) {
                case GpuSurfaceCodec::BC1:       GpuTex::DecodeBC1Block(bp, blkRgba, true); break;
                case GpuSurfaceCodec::BC2:       GpuTex::DecodeBC2Block(bp, blkRgba); break;
                case GpuSurfaceCodec::BC3:       GpuTex::DecodeBC3Block(bp, blkRgba); break;
                case GpuSurfaceCodec::BC4_U:     GpuTex::DecodeBC4Block(bp, blkRgba, false); break;
                case GpuSurfaceCodec::BC4_S:     GpuTex::DecodeBC4Block(bp, blkRgba, true); break;
                case GpuSurfaceCodec::BC5_U:     GpuTex::DecodeBC5Block(bp, blkRgba, false); break;
                case GpuSurfaceCodec::BC5_S:     GpuTex::DecodeBC5Block(bp, blkRgba, true); break;
                case GpuSurfaceCodec::BC7:       GpuTex::DecodeBC7Block(bp, blkRgba); break;
                case GpuSurfaceCodec::ETC2_RGB:  GpuTex::DecodeETC2RgbBlock(bp, blkRgba, true); break;
                case GpuSurfaceCodec::ETC2_RGBA: GpuTex::DecodeETC2Block(bp, blkRgba, true); break;
                default: return false;
            }
            GpuTex::BlitBlock4x4RgbaToPremulBgra(blkRgba, bx, by, w, h, outBgra);
        }
    }
    return true;
}

// NVIDIA Tegra X1 Block-Linear GOB (64B x 8 rows = 512B) deswizzler for Nintendo Switch .bntx
inline bool DeswizzleTegraBlockLinear(
    const uint8_t* swizzled,
    size_t swizzledLen,
    uint32_t gridW,
    uint32_t gridH,
    uint32_t bpb,
    uint32_t blockHeightLog2,
    std::vector<uint8_t>& outLinear)
{
    uint64_t linearBytes = static_cast<uint64_t>(gridW) * gridH * bpb;
    if (linearBytes == 0 || linearBytes > 256ULL * 1024 * 1024) return false;
    outLinear.assign(static_cast<size_t>(linearBytes), 0u);

    uint32_t bhGobs = 1u << std::min<uint32_t>(blockHeightLog2, 5u);
    while (bhGobs > 1u && gridH <= (bhGobs >> 1u) * 8u) {
        bhGobs >>= 1u;
    }

    const uint32_t rowBytes = gridW * bpb;
    const uint32_t widthInGobs = (rowBytes + 63u) / 64u;
    const uint32_t blockRows = bhGobs * 8u;

    for (uint32_t by = 0; by < gridH; ++by) {
        uint32_t gobBlockY = by / blockRows;
        uint32_t remY      = by % blockRows;
        uint32_t gobInBlkY = remY / 8u;
        uint32_t yInGob    = remY & 7u;

        for (uint32_t bx = 0; bx < gridW; ++bx) {
            uint32_t byteX  = bx * bpb;
            uint32_t gobX   = byteX / 64u;
            uint32_t xInGob = byteX & 63u;

            // Intra-GOB 9-bit Tegra X1 Sector-Z swizzle
            uint32_t intraGob = ((xInGob & 32u) << 3) |
                                ((yInGob & 6u)  << 5) |
                                ((xInGob & 16u) << 1) |
                                ((yInGob & 1u)  << 4) |
                                 (xInGob & 15u);

            uint64_t gobIndex = (static_cast<uint64_t>(gobBlockY) * widthInGobs + gobX) * bhGobs + gobInBlkY;
            uint64_t srcOff   = gobIndex * 512ull + intraGob;
            if (srcOff + bpb > swizzledLen) return false;

            size_t dstOff = (static_cast<size_t>(by) * gridW + bx) * bpb;
            std::memcpy(&outLinear[dstOff], swizzled + static_cast<size_t>(srcOff), bpb);
        }
    }
    return true;
}

// General 2D Morton (Z-order) index for power-of-2 & rectangular grids (PS Vita SceGxm / PS3 CellGcm SZ)
inline uint32_t CalcMorton2D(uint32_t x, uint32_t y, uint32_t gridW, uint32_t gridH, bool vitaOrder) {
    // Vita SceGxm interleaves Y in bit 0, X in bit 1: (x0<<1)|y0!
    // PS3 CellGcm SZ interleaves X in bit 0, Y in bit 1: (y0<<1)|x0!
    uint32_t minDim = std::min(gridW, gridH);
    uint32_t logMin = 0;
    while ((1u << (logMin + 1u)) <= minDim) ++logMin;
    if ((1u << logMin) < minDim) ++logMin; // round up if non-pow2

    uint32_t idx = 0;
    uint32_t bitOut = 0;
    for (uint32_t b = 0; b < logMin; ++b) {
        uint32_t xb = (x >> b) & 1u;
        uint32_t yb = (y >> b) & 1u;
        if (vitaOrder) {
            idx |= (yb << bitOut);
            idx |= (xb << (bitOut + 1u));
        } else {
            idx |= (xb << bitOut);
            idx |= (yb << (bitOut + 1u));
        }
        bitOut += 2u;
    }
    if (gridW >= gridH) {
        idx |= ((x >> logMin) << bitOut);
    } else {
        idx |= ((y >> logMin) << bitOut);
    }
    return idx;
}

inline bool DeswizzleMortonSurface(
    const uint8_t* swizzled,
    size_t swizzledLen,
    uint32_t gridW,
    uint32_t gridH,
    uint32_t bpb,
    bool vitaOrder,
    std::vector<uint8_t>& outLinear)
{
    uint64_t totalBytes = static_cast<uint64_t>(gridW) * gridH * bpb;
    if (totalBytes == 0 || totalBytes > 256ULL * 1024 * 1024) return false;
    outLinear.assign(static_cast<size_t>(totalBytes), 0u);

    for (uint32_t by = 0; by < gridH; ++by) {
        for (uint32_t bx = 0; bx < gridW; ++bx) {
            uint32_t mIdx = CalcMorton2D(bx, by, gridW, gridH, vitaOrder);
            uint64_t srcOff = static_cast<uint64_t>(mIdx) * bpb;
            if (srcOff + bpb > swizzledLen) return false;
            std::memcpy(&outLinear[(static_cast<size_t>(by) * gridW + bx) * bpb],
                        swizzled + static_cast<size_t>(srcOff), bpb);
        }
    }
    return true;
}

inline GpuSurfaceCodec MapDxgiToSurfaceCodec(uint32_t dxgi) {
    switch (dxgi) {
        case 9: case 10:               return GpuSurfaceCodec::RGBA16F;
        case 27: case 28: case 29:     return GpuSurfaceCodec::RGBA8;
        case 60: case 61:              return GpuSurfaceCodec::R8;
        case 70: case 71: case 72:     return GpuSurfaceCodec::BC1;
        case 73: case 74: case 75:     return GpuSurfaceCodec::BC2;
        case 76: case 77: case 78:     return GpuSurfaceCodec::BC3;
        case 79: case 80:              return GpuSurfaceCodec::BC4_U;
        case 81:                       return GpuSurfaceCodec::BC4_S;
        case 82: case 83:              return GpuSurfaceCodec::BC5_U;
        case 84:                       return GpuSurfaceCodec::BC5_S;
        case 85:                       return GpuSurfaceCodec::RGB565;
        case 87: case 88: case 90: case 91: return GpuSurfaceCodec::BGRA8;
        case 94: case 95:              return GpuSurfaceCodec::BC6H_U;
        case 96:                       return GpuSurfaceCodec::BC6H_S;
        case 97: case 98: case 99:     return GpuSurfaceCodec::BC7;
        default:                       return GpuSurfaceCodec::None;
    }
}

} // namespace ConsoleTexDetail

// ============================================================================
// 1. Nintendo Switch Binary Texture (.bntx — "BNTX" + "NX  " + "BRTI")
// ============================================================================
inline bool DecodeSwitchBntx(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 144 || std::memcmp(data, "BNTX", 4) != 0) return false;
    uint16_t bom = ConsoleTexDetail::RdU16LE(data + 12);
    if (bom != 0xFEFFu) return false; // Switch NX is little-endian

    // Locate first "BRTI" block (via NX infoPtrArray at +40, or bounded scan)
    size_t brtiOff = SIZE_MAX;
    if (size >= 56 && std::memcmp(data + 32, "NX  ", 4) == 0) {
        uint32_t texCount = ConsoleTexDetail::RdU32LE(data + 36);
        uint64_t ptrArr   = ConsoleTexDetail::RdU64LE(data + 40);
        if (texCount > 0 && ptrArr >= 32 && ptrArr + 8ull <= size) {
            uint64_t cand = ConsoleTexDetail::RdU64LE(data + static_cast<size_t>(ptrArr));
            if (cand + 120ull <= size && std::memcmp(data + static_cast<size_t>(cand), "BRTI", 4) == 0) {
                brtiOff = static_cast<size_t>(cand);
            }
        }
    }
    if (brtiOff == SIZE_MAX) {
        for (size_t p = 32; p + 120 <= std::min<size_t>(size, 8192u); p += 4) {
            if (std::memcmp(data + p, "BRTI", 4) == 0) { brtiOff = p; break; }
        }
    }
    if (brtiOff == SIZE_MAX || brtiOff + 120 > size) return false;

    const uint8_t* brti   = data + brtiOff;
    uint16_t tileMode     = ConsoleTexDetail::RdU16LE(brti + 18); // 0 = Tegra GOB Block-Linear, 1 = Linear
    uint32_t bntxFmt      = ConsoleTexDetail::RdU32LE(brti + 28);
    uint32_t w            = ConsoleTexDetail::RdU32LE(brti + 36);
    uint32_t h            = ConsoleTexDetail::RdU32LE(brti + 40);
    uint32_t blkHeightLog2= ConsoleTexDetail::RdU32LE(brti + 52);
    uint32_t imageSize    = ConsoleTexDetail::RdU32LE(brti + 72);
    uint64_t namePtr      = ConsoleTexDetail::RdU64LE(brti + 96);
    uint64_t mipArrPtr    = ConsoleTexDetail::RdU64LE(brti + 112);

    if (w == 0 || h == 0 || w > 8192 || h > 8192 || mipArrPtr == 0 || mipArrPtr + 8ull > size) return false;
    uint64_t mip0Off = ConsoleTexDetail::RdU64LE(data + static_cast<size_t>(mipArrPtr));
    if (mip0Off == 0 || mip0Off >= size) return false;

    std::wstring texName;
    if (namePtr >= 32 && namePtr + 2ull < size) {
        uint16_t nLen = ConsoleTexDetail::RdU16LE(data + static_cast<size_t>(namePtr));
        if (nLen > 0 && nLen <= 256 && namePtr + 2ull + nLen <= size) {
            std::string ns(reinterpret_cast<const char*>(data + static_cast<size_t>(namePtr) + 2), nLen);
            texName = PathUtils::Utf8ToWide(ns);
            outInfo.layerNames.push_back(texName);
        }
    }

    uint32_t fmtKind = (bntxFmt >> 8) & 0xFFu;
    uint32_t fmtType = bntxFmt & 0xFFu;
    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (fmtKind) {
        case 0x02: codec = ConsoleTexDetail::GpuSurfaceCodec::R8; break;
        case 0x07: codec = ConsoleTexDetail::GpuSurfaceCodec::RGB565; break;
        case 0x0B: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 0x0C: codec = ConsoleTexDetail::GpuSurfaceCodec::BGRA8; break;
        case 0x1A: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 0x1B: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 0x1C: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 0x1D: codec = (fmtType == 2u) ? ConsoleTexDetail::GpuSurfaceCodec::BC4_S : ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 0x1E: codec = (fmtType == 2u) ? ConsoleTexDetail::GpuSurfaceCodec::BC5_S : ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 0x1F: codec = (fmtType == 5u) ? ConsoleTexDetail::GpuSurfaceCodec::BC6H_S : ConsoleTexDetail::GpuSurfaceCodec::BC6H_U; break;
        case 0x20: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        case 0x2D: codec = ConsoleTexDetail::GpuSurfaceCodec::ASTC_4x4; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;
    size_t avail = size - static_cast<size_t>(mip0Off);
    if (imageSize > 0 && imageSize < avail) avail = imageSize;
    const uint8_t* mip0Ptr = data + static_cast<size_t>(mip0Off);

    std::vector<uint8_t> linearBuf;
    const uint8_t* linearPtr = mip0Ptr;
    size_t linearLen = avail;

    if (tileMode == 0u) { // Tegra X1 Block-Linear GOB swizzled!
        if (!ConsoleTexDetail::DeswizzleTegraBlockLinear(mip0Ptr, avail, gridW, gridH, bpb, blkHeightLog2, linearBuf)) {
            return false;
        }
        linearPtr = linearBuf.data();
        linearLen = linearBuf.size();
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(linearPtr, linearLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Nintendo Switch BNTX Texture (.bntx" + (texName.empty() ? L"" : (L", " + texName)) + L")";
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 2. PlayStation Vita SceGxm Texture (.gxt — "GXT\0")
// ============================================================================
inline bool DecodeVitaGxt(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64 || std::memcmp(data, "GXT\0", 4) != 0) return false;
    uint32_t version = ConsoleTexDetail::RdU32LE(data + 4);
    uint32_t numTex  = ConsoleTexDetail::RdU32LE(data + 8);
    if ((version & 0xFFFF0000u) != 0x10000000u || numTex == 0 || numTex > 1024) return false;

    const uint8_t* ent = data + 32;
    uint32_t dataOff   = ConsoleTexDetail::RdU32LE(ent + 0);
    uint32_t dataSz    = ConsoleTexDetail::RdU32LE(ent + 4);
    uint32_t texType   = ConsoleTexDetail::RdU32LE(ent + 16); // 0x00000000 = Swizzled, 0x60000000 = Linear
    uint32_t texFormat = ConsoleTexDetail::RdU32LE(ent + 20);
    uint16_t w         = ConsoleTexDetail::RdU16LE(ent + 24);
    uint16_t h         = ConsoleTexDetail::RdU16LE(ent + 26);

    if (w == 0 || h == 0 || w > 8192 || h > 8192 || dataOff < 64u || dataOff > size || dataSz == 0 || dataSz > size - dataOff) {
        return false;
    }

    uint32_t baseFmt = texFormat & 0xFF000000u;
    uint32_t swizFmt = texFormat & 0x0000F000u;
    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (baseFmt) {
        case 0x0C000000u:
            if (swizFmt == 0x1000u)      codec = ConsoleTexDetail::GpuSurfaceCodec::BGRA8; // U8U8U8U8_ARGB in LE memory = B,G,R,A
            else if (swizFmt == 0x2000u) codec = ConsoleTexDetail::GpuSurfaceCodec::ABGR8;
            else if (swizFmt == 0x3000u) codec = ConsoleTexDetail::GpuSurfaceCodec::ARGB8;
            else                         codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; // U8U8U8U8_ABGR in LE memory = R,G,B,A
            break;
        case 0x85000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 0x86000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 0x87000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;

    std::vector<uint8_t> linearBuf;
    const uint8_t* srcPtr = data + dataOff;
    size_t srcLen = dataSz;

    if ((texType & 0xF0000000u) == 0x00000000u) { // SceGxm Morton Swizzled!
        if (!ConsoleTexDetail::DeswizzleMortonSurface(srcPtr, srcLen, gridW, gridH, bpb, true, linearBuf)) {
            return false;
        }
        srcPtr = linearBuf.data();
        srcLen = linearBuf.size();
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(srcPtr, srcLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"PlayStation Vita SceGxm Texture (.gxt)";
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 3. PlayStation 3 Cell GCM Texture (.gtf — Big-Endian Header + CellGcmTexture)
// ============================================================================
inline bool DecodePs3Gtf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 44) return false;
    uint32_t version = ConsoleTexDetail::RdU32BE(data + 0);
    uint32_t numTex  = ConsoleTexDetail::RdU32BE(data + 8);
    uint8_t  verMajor = static_cast<uint8_t>((version >> 24) & 0xFFu);
    if ((verMajor != 1u && verMajor != 2u) || (version & 0xFFFFu) != 0u || numTex == 0u || numTex > 256u) {
        return false;
    }

    // Entry 0 starts at +12 (12-byte GTF header: version[4], size[4], numTex[4])
    // Note: some GTF packers pad header to 16 bytes when id==0 at +12 and offset is at +16; check +16 vs +20!
    size_t entOff = 12u;
    uint32_t texOff = ConsoleTexDetail::RdU32BE(data + entOff + 4);
    uint32_t texSz  = ConsoleTexDetail::RdU32BE(data + entOff + 8);
    if (texOff < 36u || texOff > size || texSz == 0u || texSz > size - texOff) {
        entOff = 16u;
        if (entOff + 28u > size) return false;
        texOff = ConsoleTexDetail::RdU32BE(data + entOff + 4);
        texSz  = ConsoleTexDetail::RdU32BE(data + entOff + 8);
        if (texOff < 40u || texOff > size || texSz == 0u || texSz > size - texOff) return false;
    }

    const uint8_t* attr = data + entOff + 12u; // CellGcmTexture (24 bytes)
    uint8_t rawFmt = attr[0];
    uint8_t dim    = attr[2];
    uint16_t w     = ConsoleTexDetail::RdU16BE(attr + 8);
    uint16_t h     = ConsoleTexDetail::RdU16BE(attr + 10);
    if ((dim != 1u && dim != 2u && dim != 3u) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    bool isLinear = (rawFmt & 0x20u) != 0u;
    uint8_t baseFmt = static_cast<uint8_t>(rawFmt & ~(0x20u | 0x40u));

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (baseFmt) {
        case 0x81u: codec = ConsoleTexDetail::GpuSurfaceCodec::R8;    break;
        case 0x85u: codec = ConsoleTexDetail::GpuSurfaceCodec::ARGB8; break; // CELL_GCM_TEXTURE_A8R8G8B8
        case 0x86u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1;   break; // CELL_GCM_TEXTURE_COMPRESSED_DXT1
        case 0x87u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2;   break; // CELL_GCM_TEXTURE_COMPRESSED_DXT23
        case 0x88u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3;   break; // CELL_GCM_TEXTURE_COMPRESSED_DXT45
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;

    std::vector<uint8_t> linearBuf;
    const uint8_t* srcPtr = data + texOff;
    size_t srcLen = texSz;

    if (!isLinear && blkW == 1u) { // PS3 CellGcm SZ Morton swizzle on uncompressed texels
        if (!ConsoleTexDetail::DeswizzleMortonSurface(srcPtr, srcLen, gridW, gridH, bpb, false, linearBuf)) {
            return false;
        }
        srcPtr = linearBuf.data();
        srcLen = linearBuf.size();
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(srcPtr, srcLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"PlayStation 3 Cell GCM Texture (.gtf)";
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 4. Bandai Namco Texture Container (.nut — "NTP3" / "NTWU" / "NTWD")
// ============================================================================
inline bool DecodeNamcoNut(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64) return false;
    bool isBE = false;
    if (std::memcmp(data, "NTP3", 4) == 0 || std::memcmp(data, "NTWU", 4) == 0) isBE = true;
    else if (std::memcmp(data, "NTWD", 4) == 0) isBE = false;
    else return false;

    auto rd16 = [&](const uint8_t* p) { return isBE ? ConsoleTexDetail::RdU16BE(p) : ConsoleTexDetail::RdU16LE(p); };
    auto rd32 = [&](const uint8_t* p) { return isBE ? ConsoleTexDetail::RdU32BE(p) : ConsoleTexDetail::RdU32LE(p); };

    uint16_t numTex = rd16(data + 6);
    if (numTex == 0u || numTex > 2048u) return false;

    const uint8_t* th  = data + 16;
    uint32_t totalSz   = rd32(th + 0);
    uint32_t dataSz    = rd32(th + 8);
    uint16_t hdrSz     = rd16(th + 12);
    uint8_t  pixFmt    = th[19];
    uint16_t w         = rd16(th + 20);
    uint16_t h         = rd16(th + 22);

    if (hdrSz < 48u || w == 0 || h == 0 || w > 8192 || h > 8192 || 16ull + hdrSz > size) return false;
    size_t pixOff = 16u + hdrSz;
    size_t avail = size - pixOff;
    if (dataSz > 0 && dataSz <= avail) avail = dataSz;
    else if (totalSz > hdrSz && (totalSz - hdrSz) <= avail) avail = totalSz - hdrSz;

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (pixFmt) {
        case 0:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 1:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 2:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 8:  codec = ConsoleTexDetail::GpuSurfaceCodec::RGB565; break;
        case 14: codec = isBE ? ConsoleTexDetail::GpuSurfaceCodec::ARGB8 : ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 17: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 21: codec = ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 22: codec = ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 23: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        default: return false;
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(data + pixOff, avail, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Bandai Namco NUT Texture (.nut)";
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 5. Capcom RE Engine Texture (.tex — binary "TEX\0")
// ============================================================================
inline bool IsLikelyCapcomTexBinary(const uint8_t* data, size_t size) {
    return data && size >= 56 &&
           (std::memcmp(data, "TEX\0", 4) == 0 || std::memcmp(data, "\0XET", 4) == 0);
}

inline bool DecodeCapcomTex(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!IsLikelyCapcomTexBinary(data, size)) return false;
    if (std::memcmp(data, "TEX\0", 4) != 0) return false;

    uint32_t version = ConsoleTexDetail::RdU32LE(data + 4);
    uint16_t w       = ConsoleTexDetail::RdU16LE(data + 8);
    uint16_t h       = ConsoleTexDetail::RdU16LE(data + 10);
    uint32_t dxgiFmt = ConsoleTexDetail::RdU32LE(data + 16);
    if (version < 8u || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    // Mip 0 header (16 bytes at +40: uint64_t mipOffset, uint32_t pitch, uint32_t mipByteSize)
    if (size < 56u) return false;
    uint64_t mipOff  = ConsoleTexDetail::RdU64LE(data + 40);
    uint32_t mipSize = ConsoleTexDetail::RdU32LE(data + 52);
    if (mipOff < 56u || mipOff >= size) return false;
    size_t avail = size - static_cast<size_t>(mipOff);
    if (mipSize > 0u && mipSize <= avail) avail = mipSize;

    auto codec = ConsoleTexDetail::MapDxgiToSurfaceCodec(dxgiFmt);
    if (codec == ConsoleTexDetail::GpuSurfaceCodec::None) return false;

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(data + static_cast<size_t>(mipOff), avail, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_U ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_S) ? 16u : 8u;
    outInfo.formatName = L"Capcom RE Engine Texture (.tex)";
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 6. Valve Source 2 Compiled Texture (.vtex_c — ResourceHeader v12 + "DATA")
// ============================================================================
inline bool DecodeSource2VtexC(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 68) return false;
    uint32_t hdrFileSize  = ConsoleTexDetail::RdU32LE(data + 0);
    uint16_t hdrVersion   = ConsoleTexDetail::RdU16LE(data + 4);
    uint32_t blockRelOff  = ConsoleTexDetail::RdU32LE(data + 8);
    uint32_t blockCount   = ConsoleTexDetail::RdU32LE(data + 12);
    (void)hdrFileSize;

    if (hdrVersion != 12u || blockCount == 0u || blockCount > 64u) return false;
    uint64_t blockDirOff = 8ull + blockRelOff;
    if (blockDirOff > size || static_cast<uint64_t>(blockCount) * 12ull > size - blockDirOff) return false;

    size_t dataBlkOff = SIZE_MAX, dataBlkSz = 0, maxBlkEnd = static_cast<size_t>(blockDirOff + blockCount * 12u);
    for (uint32_t i = 0; i < blockCount; ++i) {
        size_t entPos = static_cast<size_t>(blockDirOff) + static_cast<size_t>(i) * 12u;
        int32_t rel   = ConsoleTexDetail::RdI32LE(data + entPos + 4);
        uint32_t bsz  = ConsoleTexDetail::RdU32LE(data + entPos + 8);
        int64_t absOff = static_cast<int64_t>(entPos + 4u) + rel;
        if (absOff < 16 || static_cast<uint64_t>(absOff) > size || bsz > size - static_cast<size_t>(absOff)) {
            return false;
        }
        maxBlkEnd = std::max(maxBlkEnd, static_cast<size_t>(absOff) + bsz);
        if (std::memcmp(data + entPos, "DATA", 4) == 0) {
            dataBlkOff = static_cast<size_t>(absOff);
            dataBlkSz  = bsz;
        }
    }
    if (dataBlkOff == SIZE_MAX || dataBlkSz < 40u) return false;

    const uint8_t* vtex = data + dataBlkOff;
    uint16_t vtexVer    = ConsoleTexDetail::RdU16LE(vtex + 0);
    uint16_t w          = ConsoleTexDetail::RdU16LE(vtex + 20);
    uint16_t h          = ConsoleTexDetail::RdU16LE(vtex + 22);
    uint8_t  vtexFmt    = vtex[26];
    uint8_t  numMips    = vtex[27];
    if (vtexVer != 1u || w == 0 || h == 0 || w > 8192 || h > 8192 || numMips == 0 || numMips > 16) return false;

    size_t streamOff = maxBlkEnd;
    if (streamOff >= size) {
        // Fallback when pixel stream is appended inside the DATA block tail after 40-byte VTex_t header
        if (dataBlkOff + 40u < size) streamOff = dataBlkOff + 40u;
        else return false;
    }

    // Embedded PNG formats (16 = PNG_RGBA8888, 18 = PNG_DXT5)
    if (vtexFmt == 16u || vtexFmt == 18u) {
        for (size_t p = streamOff; p + 24 <= size; ++p) {
            if (std::memcmp(data + p, "\x89PNG\r\n\x1A\n", 8) == 0) {
                if (DetailImg100::DecodeEmbeddedWicBuffer(data + p, size - p, L"Valve Source 2 Texture (.vtex_c)", outInfo)) {
                    outInfo.origWidth = outInfo.width;
                    outInfo.origHeight = outInfo.height;
                    return true;
                }
            }
        }
        return false;
    }

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (vtexFmt) {
        case 1:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 2:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 3:  codec = ConsoleTexDetail::GpuSurfaceCodec::R8; break;
        case 4:  codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 11: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA16F; break;
        case 20: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        case 21: codec = ConsoleTexDetail::GpuSurfaceCodec::BC6H_U; break;
        case 22: codec = ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 23: codec = ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 27: codec = ConsoleTexDetail::GpuSurfaceCodec::ETC2_RGB; break;
        case 28: codec = ConsoleTexDetail::GpuSurfaceCodec::ETC2_RGBA; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint64_t mip0Bytes = static_cast<uint64_t>((w + blkW - 1u) / blkW) * ((h + blkH - 1u) / blkH) * bpb;
    if (mip0Bytes == 0 || mip0Bytes > size - streamOff) return false;

    // Source 2 stores mips smallest-to-largest at the end of the file! Mip 0 is the final mip0Bytes slice.
    size_t mip0Pos = size - static_cast<size_t>(mip0Bytes);
    if (mip0Pos < streamOff) mip0Pos = streamOff;

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(data + mip0Pos, static_cast<size_t>(mip0Bytes), w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_U) ? 16u : 8u;
    outInfo.formatName = L"Valve Source 2 Compiled Texture (.vtex_c)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
