#pragma once
// CapcomTex.h — Capcom RE Engine & MT Framework v112–v160 Texture Decoder (.tex)

namespace Formats {

namespace DetailCapcomTex {

inline ConsoleTexDetail::GpuSurfaceCodec MapMtFrameworkFormat(uint32_t fmt, bool bigEndian, bool& outYCoCg) {
    outYCoCg = false;
    switch (fmt) {
        case 0x07u:
        case 0x09u: return bigEndian ? ConsoleTexDetail::GpuSurfaceCodec::ARGB8 : ConsoleTexDetail::GpuSurfaceCodec::RGBA8;
        case 0x02u: return ConsoleTexDetail::GpuSurfaceCodec::RGBA16F;
        case 0x13u:
        case 0x14u:
        case 0x1Au: return ConsoleTexDetail::GpuSurfaceCodec::BC1; // DXT1
        case 0x15u:
        case 0x16u: return ConsoleTexDetail::GpuSurfaceCodec::BC2; // DXT3
        case 0x17u:
        case 0x18u:
        case 0x19u:
        case 0x1Eu:
        case 0x29u: return ConsoleTexDetail::GpuSurfaceCodec::BC3; // DXT5 / NM
        case 0x20u:
        case 0x2Au:
            outYCoCg = true;
            return ConsoleTexDetail::GpuSurfaceCodec::BC3;         // DXT5_YCoCg
        case 0x1Fu: return ConsoleTexDetail::GpuSurfaceCodec::BC4_U;
        case 0x21u:
        case 0x25u: return ConsoleTexDetail::GpuSurfaceCodec::BC5_U;
        case 0x27u:
        case 0x2Eu:
        case 0x2Fu: return ConsoleTexDetail::GpuSurfaceCodec::BC7;
        default:    return ConsoleTexDetail::GpuSurfaceCodec::None;
    }
}

inline void ConvertYCoCgBgraInPlace(std::vector<uint8_t>& bgra) {
    const size_t n = bgra.size() / 4u;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* p = &bgra[i * 4u];
        // DXT5 YCoCg: R=Co, G=Cg, B=scaleVal, A=Y -> our premul BGRA currently holds B=scale, G=Cg, R=Co, A=Y
        const int yv = p[3];
        const int scale = (p[0] >> 3) + 1;
        const int co = (static_cast<int>(p[2]) - 128) / scale;
        const int cg = (static_cast<int>(p[1]) - 128) / scale;
        const uint8_t r = static_cast<uint8_t>(std::clamp(yv + co - cg, 0, 255));
        const uint8_t g = static_cast<uint8_t>(std::clamp(yv + cg,      0, 255));
        const uint8_t b = static_cast<uint8_t>(std::clamp(yv - co - cg, 0, 255));
        WritePremulBGRA(p, r, g, b, 255u);
    }
}

inline bool TryDecodeMtFrameworkTex(const uint8_t* data, size_t size, bool bigEndian, ExtendedImageInfo& outInfo) {
    if (!data || size < 32u) return false;
    auto rd16 = [&](size_t off) { return bigEndian ? ConsoleTexDetail::RdU16BE(data + off) : ConsoleTexDetail::RdU16LE(data + off); };
    auto rd32 = [&](size_t off) { return bigEndian ? ConsoleTexDetail::RdU32BE(data + off) : ConsoleTexDetail::RdU32LE(data + off); };
    auto rd64 = [&](size_t off) { return bigEndian ? ConsoleTexDetail::RdU64BE(data + off) : ConsoleTexDetail::RdU64LE(data + off); };

    const uint32_t dw0 = rd32(4);
    const uint32_t ver12 = dw0 & 0xFFFu;
    const uint16_t ver16 = rd16(4);

    // 1. MT Framework v154–v180 (bit-packed 16-byte header: DDDA, RE6, REHD, DMC4SE, MHGU, MHW pre-RE)
    if (ver12 >= 154u && ver12 <= 180u) {
        const uint32_t rsrcType = (dw0 >> 28) & 0xFu;
        const uint32_t dw1 = rd32(8);
        const uint32_t dw2 = rd32(12);
        uint32_t numMips = dw1 & 0x3Fu;
        uint32_t w       = (dw1 >> 6) & 0x1FFFu;
        uint32_t h       = (dw1 >> 19) & 0x1FFFu;
        uint32_t faces   = std::max<uint32_t>(1u, dw2 & 0xFFu);
        uint32_t mtFmt   = (dw2 >> 8) & 0xFFu;

        // Alternate v154 bitfield shift used on 3DS/WiiU/PS3 builds: [rsrcType:4 | unk:12 | shift:4 | ver:12]
        if ((w == 0 || h == 0 || numMips == 0 || numMips > 16u) && bigEndian) {
            // Big-endian bitfield order in Xbox 360 / PS3 \0XET:
            numMips = (dw1 >> 26) & 0x3Fu;
            w       = (dw1 >> 13) & 0x1FFFu;
            h       = dw1 & 0x1FFFu;
            faces   = std::max<uint32_t>(1u, (dw2 >> 24) & 0xFFu);
            mtFmt   = (dw2 >> 16) & 0xFFu;
        }

        if (w > 0 && w <= 8192u && h > 0 && h <= 8192u && numMips >= 1u && numMips <= 16u && faces <= 64u) {
            bool isYCoCg = false;
            auto codec = MapMtFrameworkFormat(mtFmt, bigEndian, isYCoCg);
            if (codec != ConsoleTexDetail::GpuSurfaceCodec::None) {
                uint32_t blkW = 1, blkH = 1, bpb = 4;
                ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
                const uint64_t mip0Bytes = static_cast<uint64_t>((w + blkW - 1u) / blkW) * ((h + blkH - 1u) / blkH) * bpb;

                const size_t offTablePos = (rsrcType == 6u && size >= 16u + 108u + 4u) ? (16u + 108u) : 16u;
                uint64_t mipOff = 0;
                const size_t totalEntries = static_cast<size_t>(numMips) * faces;
                // 64-bit MT Framework (v157+/v160 PC DX11) stores uint64_t mip offsets; 32-bit stores uint32_t mip offsets
                if (offTablePos + totalEntries * 8u <= size) {
                    const uint64_t cand64 = rd64(offTablePos);
                    if (cand64 >= offTablePos + totalEntries * 8u && cand64 + mip0Bytes <= size) {
                        mipOff = cand64;
                    }
                }
                if (mipOff == 0 && offTablePos + totalEntries * 4u <= size) {
                    const uint32_t cand32 = rd32(offTablePos);
                    if (cand32 >= offTablePos + totalEntries * 4u && static_cast<uint64_t>(cand32) + mip0Bytes <= size) {
                        mipOff = cand32;
                    }
                }
                if (mipOff > 0 && mipOff + mip0Bytes <= size &&
                    ConsoleTexDetail::DecodeLinearGpuSurface(data + static_cast<size_t>(mipOff), size - static_cast<size_t>(mipOff), w, h, codec, outInfo.bgraPixels))
                {
                    if (isYCoCg) ConvertYCoCgBgraInPlace(outInfo.bgraPixels);
                    outInfo.width = w;
                    outInfo.height = h;
                    outInfo.origWidth = w;
                    outInfo.origHeight = h;
                    outInfo.channels = 4u;
                    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F) ? 16u : 8u;
                    outInfo.formatName = L"Capcom MT Framework Texture (v" + std::to_wstring(ver12) + L" .tex)";
                    outInfo.valid = true;
                    return true;
                }
            }
        }
    }

    // 2. MT Framework v112–v153 (DMC4, RE5, Lost Planet, Dead Rising 2, MVC3):
    //    +0x04: u16 version (104..153), +0x06: u16 flags, +0x08: u8 numMips, +0x09: u8 numFaces,
    //    +0x0C: u16 width, +0x0E: u16 height, +0x14: FourCC ("DXT1"/"DXT3"/"DXT5") or format u32,
    //    +0x1C (or +0x24): u32 mipOffsets[]
    if (ver16 >= 104u && ver16 <= 153u && size >= 32u) {
        const uint8_t  numMips  = data[8];
        const uint8_t  numFaces = std::max<uint8_t>(1u, data[9]);
        const uint16_t w        = rd16(12);
        const uint16_t h        = rd16(14);
        if (w > 0 && w <= 8192u && h > 0 && h <= 8192u && numMips >= 1u && numMips <= 16u && numFaces <= 6u) {
            ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
            bool isYCoCg = false;
            const uint8_t* fcc = data + 20;
            if (std::memcmp(fcc, "DXT1", 4) == 0 || std::memcmp(fcc, "1TXD", 4) == 0) {
                codec = ConsoleTexDetail::GpuSurfaceCodec::BC1;
            } else if (std::memcmp(fcc, "DXT3", 4) == 0 || std::memcmp(fcc, "3TXD", 4) == 0) {
                codec = ConsoleTexDetail::GpuSurfaceCodec::BC2;
            } else if (std::memcmp(fcc, "DXT5", 4) == 0 || std::memcmp(fcc, "5TXD", 4) == 0) {
                codec = ConsoleTexDetail::GpuSurfaceCodec::BC3;
            } else if (std::memcmp(fcc, "ATI1", 4) == 0 || std::memcmp(fcc, "1ITA", 4) == 0) {
                codec = ConsoleTexDetail::GpuSurfaceCodec::BC4_U;
            } else if (std::memcmp(fcc, "ATI2", 4) == 0 || std::memcmp(fcc, "2ITA", 4) == 0) {
                codec = ConsoleTexDetail::GpuSurfaceCodec::BC5_U;
            } else {
                const uint32_t fmtCode = rd32(20);
                if (fmtCode == 21u || fmtCode == 0x15u) { // D3DFMT_A8R8G8B8 (21 in D3D9)
                    codec = bigEndian ? ConsoleTexDetail::GpuSurfaceCodec::ARGB8 : ConsoleTexDetail::GpuSurfaceCodec::BGRA8;
                } else if (fmtCode == 22u || fmtCode == 0x16u) { // D3DFMT_X8R8G8B8
                    codec = bigEndian ? ConsoleTexDetail::GpuSurfaceCodec::ARGB8 : ConsoleTexDetail::GpuSurfaceCodec::BGRA8;
                } else if (fmtCode == 32u || fmtCode == 7u || fmtCode == 9u) { // A8B8G8R8 / RGBA8
                    codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8;
                } else {
                    codec = MapMtFrameworkFormat(fmtCode, bigEndian, isYCoCg);
                }
            }

            if (codec != ConsoleTexDetail::GpuSurfaceCodec::None) {
                uint32_t blkW = 1, blkH = 1, bpb = 4;
                ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
                const uint64_t mip0Bytes = static_cast<uint64_t>((w + blkW - 1u) / blkW) * ((h + blkH - 1u) / blkH) * bpb;

                uint32_t mipOff = 0;
                for (size_t offPos : { size_t(36), size_t(28), size_t(24), size_t(40) }) {
                    if (offPos + 4u <= size) {
                        const uint32_t cand = rd32(offPos);
                        if (cand >= offPos + 4u && static_cast<uint64_t>(cand) + mip0Bytes <= size) {
                            mipOff = cand;
                            break;
                        }
                    }
                }
                if (mipOff > 0 &&
                    ConsoleTexDetail::DecodeLinearGpuSurface(data + mipOff, size - mipOff, w, h, codec, outInfo.bgraPixels))
                {
                    if (isYCoCg) ConvertYCoCgBgraInPlace(outInfo.bgraPixels);
                    outInfo.width = w;
                    outInfo.height = h;
                    outInfo.origWidth = w;
                    outInfo.origHeight = h;
                    outInfo.channels = 4u;
                    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F) ? 16u : 8u;
                    outInfo.formatName = L"Capcom MT Framework Texture (v" + std::to_wstring(ver16) + L" .tex)";
                    outInfo.valid = true;
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace DetailCapcomTex

// ============================================================================
// 5. Capcom RE Engine & MT Framework Texture (.tex — "TEX\0" / "\0XET")
// ============================================================================
inline bool IsLikelyCapcomTexBinary(const uint8_t* data, size_t size) {
    return data && size >= 32u &&
           (std::memcmp(data, "TEX\0", 4) == 0 || std::memcmp(data, "\0XET", 4) == 0);
}

inline bool DecodeCapcomTex(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!IsLikelyCapcomTexBinary(data, size)) return false;

    const bool bigEndian = (std::memcmp(data, "\0XET", 4) == 0);
    if (bigEndian) {
        return DetailCapcomTex::TryDecodeMtFrameworkTex(data, size, true, outInfo);
    }

    // Little-endian "TEX\0": try MT Framework (v104–v180) first when version matches MT Framework range
    const uint32_t rawVer = ConsoleTexDetail::RdU32LE(data + 4);
    if (((rawVer & 0xFFFu) >= 104u && (rawVer & 0xFFFu) <= 180u) ||
        (ConsoleTexDetail::RdU16LE(data + 4) >= 104u && ConsoleTexDetail::RdU16LE(data + 4) <= 153u))
    {
        if (DetailCapcomTex::TryDecodeMtFrameworkTex(data, size, false, outInfo)) {
            return true;
        }
    }

    // Capcom RE Engine "TEX\0" (v8..v35 & date-versioned v190820018+ with DXGI format + 16-byte MipHeader at +40)
    if (size < 56u) return false;
    const uint16_t w       = ConsoleTexDetail::RdU16LE(data + 8);
    const uint16_t h       = ConsoleTexDetail::RdU16LE(data + 10);
    uint32_t dxgiFmt       = ConsoleTexDetail::RdU32LE(data + 16);
    if (rawVer < 8u || w == 0 || h == 0 || w > 8192u || h > 8192u) return false;

    auto codec = ConsoleTexDetail::MapDxgiToSurfaceCodec(dxgiFmt);
    if (codec == ConsoleTexDetail::GpuSurfaceCodec::None) {
        // RE Engine v28+ (RE4R / SF6 / DD2) places dxgiFormat at +24 and mip0 header at +40 (or +32)
        dxgiFmt = ConsoleTexDetail::RdU32LE(data + 24);
        codec = ConsoleTexDetail::MapDxgiToSurfaceCodec(dxgiFmt);
        if (codec == ConsoleTexDetail::GpuSurfaceCodec::None) return false;
    }

    uint64_t mipOff  = ConsoleTexDetail::RdU64LE(data + 40);
    uint32_t mipSize = ConsoleTexDetail::RdU32LE(data + 52);
    if (mipOff < 48u || mipOff >= size) {
        mipOff  = ConsoleTexDetail::RdU64LE(data + 32);
        mipSize = ConsoleTexDetail::RdU32LE(data + 44);
        if (mipOff < 48u || mipOff >= size) return false;
    }
    size_t avail = size - static_cast<size_t>(mipOff);
    if (mipSize > 0u && mipSize <= avail) avail = mipSize;

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

} // namespace Formats
