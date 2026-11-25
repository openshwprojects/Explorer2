#pragma once
// XboxXprImage.h — Original Xbox ("XPR0" / "XPR2" NV2A) & Xbox 360 ("XPR2" Xenos) Packed Resource Decoder (.xpr)

namespace Formats {

namespace DetailXboxXpr {

// Rectangular NV2A 2D Morton / Z-order coordinate encoder:
// Interleaves (x, y) bits up to min(log2(w), log2(h)), then appends remaining high bits of the longer axis linearly.
inline size_t EncodeXboxNv2aSwizzle(uint32_t x, uint32_t y, uint32_t w, uint32_t h) noexcept {
    size_t off = 0;
    size_t shift = 0;
    uint32_t mask = 1u;
    while (mask < w || mask < h) {
        if (mask < w) {
            if ((x & mask) != 0u) off |= (size_t{1} << shift);
            ++shift;
        }
        if (mask < h) {
            if ((y & mask) != 0u) off |= (size_t{1} << shift);
            ++shift;
        }
        mask <<= 1u;
    }
    return off;
}

inline bool DecodeNv2aTextureDescriptor(
    const uint8_t* fileData,
    size_t fileSize,
    uint32_t hdrSize,
    uint32_t dataRelOff,
    uint32_t dwFormat,
    uint32_t dwSize,
    const uint8_t* optPaletteBgra,
    ExtendedImageInfo& outInfo)
{
    if (hdrSize >= fileSize || dataRelOff > fileSize - hdrSize) return false;
    const size_t dataAbsOff = static_cast<size_t>(hdrSize) + dataRelOff;
    const size_t availBytes = fileSize - dataAbsOff;
    const uint8_t* src = fileData + dataAbsOff;

    const uint32_t dims   = (dwFormat >> 4u) & 0x0Fu;
    const uint32_t gpuFmt = (dwFormat >> 8u) & 0xFFu;
    if (dims != 2u && dims != 1u) return false;

    uint32_t w = 0, h = 0, linearPitch = 0;
    if (dwSize != 0u) {
        w = (dwSize & 0x0FFFu) + 1u;
        h = ((dwSize >> 12u) & 0x0FFFu) + 1u;
        linearPitch = (((dwSize >> 24u) & 0xFFu) + 1u) * 64u;
    } else {
        const uint32_t logW = (dwFormat >> 20u) & 0x0Fu;
        const uint32_t logH = (dwFormat >> 24u) & 0x0Fu;
        if (logW > 12u || logH > 12u) return false;
        w = 1u << logW;
        h = (dims == 1u) ? 1u : (1u << logH);
    }
    if (w == 0 || h == 0 || w > 4096u || h > 4096u) return false;

    const size_t totalPixels = static_cast<size_t>(w) * h;

    // 1. S3TC Block-Compressed NV2A formats (stored in linear 4x4 block order on Original Xbox!)
    if (gpuFmt == 0x0Cu || gpuFmt == 0x0Eu || gpuFmt == 0x0Fu) {
        const int bcMode = (gpuFmt == 0x0Cu) ? 1 : ((gpuFmt == 0x0Eu) ? 2 : 3);
        std::vector<uint8_t> bgra;
        if (!ConsolePx::DecodeLinearS3tcToPbgra(src, availBytes, w, h, bcMode, bgra)) return false;
        outInfo = {};
        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = 4u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = (bcMode == 1) ? L"Original Xbox XPR (DXT1 .xpr)"
                           : (bcMode == 2) ? L"Original Xbox XPR (DXT3 .xpr)"
                                           : L"Original Xbox XPR (DXT5 .xpr)";
        outInfo.bgraPixels = std::move(bgra);
        outInfo.valid = true;
        return true;
    }

    // 2. Determine Swizzled vs Linear NV2A direct/paletted pixel formats
    bool isSwizzled = true;
    uint32_t bppBytes = 0;
    switch (gpuFmt) {
        case 0x00u: // L8
        case 0x01u: // AL8
        case 0x0Bu: // P8
        case 0x19u: // A8
            bppBytes = 1u; isSwizzled = true; break;
        case 0x02u: // A1R5G5B5
        case 0x03u: // X1R5G5B5
        case 0x04u: // A4R4G4B4
        case 0x05u: // R5G6B5
        case 0x1Au: // A8L8
            bppBytes = 2u; isSwizzled = true; break;
        case 0x06u: // A8R8G8B8
        case 0x07u: // X8R8G8B8
        case 0x29u: // A8B8G8R8
            bppBytes = 4u; isSwizzled = true; break;
        case 0x10u: // LIN_A1R5G5B5
        case 0x11u: // LIN_R5G6B5
        case 0x1Cu: // LIN_X1R5G5B5
        case 0x1Du: // LIN_A4R4G4B4
        case 0x20u: // LIN_A8L8
            bppBytes = 2u; isSwizzled = false; break;
        case 0x12u: // LIN_A8R8G8B8
        case 0x1Eu: // LIN_X8R8G8B8
        case 0x3Fu: // LIN_A8B8G8R8
            bppBytes = 4u; isSwizzled = false; break;
        case 0x13u: // LIN_L8
        case 0x1Fu: // LIN_A8
            bppBytes = 1u; isSwizzled = false; break;
        default:
            return false;
    }

    const size_t rowStride = isSwizzled
        ? (static_cast<size_t>(w) * bppBytes)
        : (linearPitch >= static_cast<size_t>(w) * bppBytes ? linearPitch : static_cast<size_t>(w) * bppBytes);
    const size_t needed = isSwizzled ? (totalPixels * bppBytes) : (rowStride * (h - 1u) + static_cast<size_t>(w) * bppBytes);
    if (needed > availBytes) return false;
    if (gpuFmt == 0x0Bu && !optPaletteBgra) {
        // Check if 1024-byte palette immediately trails pixel data
        if (availBytes >= needed + 1024u) {
            optPaletteBgra = src + needed;
        } else {
            return false;
        }
    }

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Original Xbox XPR Texture (.xpr)";
    outInfo.bgraPixels.resize(totalPixels * 4u);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const size_t pxByteOff = isSwizzled
                ? (EncodeXboxNv2aSwizzle(x, y, w, h) * bppBytes)
                : (static_cast<size_t>(y) * rowStride + static_cast<size_t>(x) * bppBytes);
            const uint8_t* p = src + pxByteOff;

            uint8_t r = 0, g = 0, b = 0, a = 255u;
            switch (gpuFmt) {
                case 0x00u: case 0x13u: r = g = b = p[0]; break;
                case 0x01u: r = g = b = a = p[0]; break;
                case 0x19u: case 0x1Fu: r = g = b = 255u; a = p[0]; break;
                case 0x0Bu: {
                    const uint8_t* pe = optPaletteBgra + static_cast<size_t>(p[0]) * 4u;
                    b = pe[0]; g = pe[1]; r = pe[2]; a = pe[3];
                    break;
                }
                case 0x1Au: case 0x20u: r = g = b = p[0]; a = p[1]; break;
                case 0x05u: case 0x11u: GpuTex::UnpackRgb565(ByteIO::RdU16LE(p), r, g, b); break;
                case 0x02u: case 0x10u: {
                    const uint16_t v = ByteIO::RdU16LE(p);
                    a = (v & 0x8000u) ? 255u : 0u;
                    r = static_cast<uint8_t>(((v >> 10u) & 0x1Fu) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >>  5u) & 0x1Fu) * 255u / 31u);
                    b = static_cast<uint8_t>(( v         & 0x1Fu) * 255u / 31u);
                    break;
                }
                case 0x03u: case 0x1Cu: {
                    const uint16_t v = ByteIO::RdU16LE(p);
                    r = static_cast<uint8_t>(((v >> 10u) & 0x1Fu) * 255u / 31u);
                    g = static_cast<uint8_t>(((v >>  5u) & 0x1Fu) * 255u / 31u);
                    b = static_cast<uint8_t>(( v         & 0x1Fu) * 255u / 31u);
                    break;
                }
                case 0x04u: case 0x1Du: {
                    const uint16_t v = ByteIO::RdU16LE(p);
                    a = static_cast<uint8_t>(((v >> 12u) & 0x0Fu) * 17u);
                    r = static_cast<uint8_t>(((v >>  8u) & 0x0Fu) * 17u);
                    g = static_cast<uint8_t>(((v >>  4u) & 0x0Fu) * 17u);
                    b = static_cast<uint8_t>(( v         & 0x0Fu) * 17u);
                    break;
                }
                case 0x06u: case 0x12u: b = p[0]; g = p[1]; r = p[2]; a = p[3]; break;
                case 0x07u: case 0x1Eu: b = p[0]; g = p[1]; r = p[2]; a = 255u; break;
                case 0x29u: case 0x3Fu: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
                default: break;
            }
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4u], r, g, b, a);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace DetailXboxXpr

inline bool DecodeXboxXprImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailXboxXpr;
    if (!data || size < 32u) return false;

    const bool isXpr0 = (std::memcmp(data, "XPR0", 4) == 0 || std::memcmp(data, "XPR1", 4) == 0);
    const bool isXpr2 = (std::memcmp(data, "XPR2", 4) == 0);
    if (!isXpr0 && !isXpr2) return false;

    // 1. Original Xbox XPR0 / XPR1 & Little-Endian XPR2 Multi-Resource Bundles
    const uint32_t leTotalSz = ByteIO::RdU32LE(data + 4);
    const uint32_t leHdrSz   = ByteIO::RdU32LE(data + 8);
    if (leHdrSz >= 24u && leHdrSz < size && leTotalSz >= leHdrSz && leTotalSz <= size + 4096u) {
        // Scan header block [12 .. leHdrSz] for optional D3DPalette (type 3 << 16) and D3DTexture (type 4 << 16)
        const uint8_t* foundPalette = nullptr;
        for (size_t hp = 12u; hp + 16u <= leHdrSz; hp += 4u) {
            const uint32_t common = ByteIO::RdU32LE(data + hp);
            if ((common & 0x00070000u) == 0x00030000u && (common & 0xFFFFu) <= 16u) {
                const uint32_t palOff = ByteIO::RdU32LE(data + hp + 4u);
                if (static_cast<uint64_t>(leHdrSz) + palOff + 1024ull <= size) {
                    foundPalette = data + leHdrSz + palOff;
                    break;
                }
            }
        }

        // In XPR2 LE bundle, walk directory at +12: [numResources:u32, {type4CC, descOffset, size, nameOff}...]
        if (isXpr2 && leHdrSz >= 32u) {
            const uint32_t numRes = ByteIO::RdU32LE(data + 12);
            if (numRes >= 1u && numRes <= 512u && 16ull + static_cast<uint64_t>(numRes) * 16ull <= leHdrSz) {
                for (uint32_t i = 0; i < numRes; ++i) {
                    const size_t ep = 16u + static_cast<size_t>(i) * 16u;
                    if (std::memcmp(data + ep, "PAL ", 4) == 0 || std::memcmp(data + ep, " PAL", 4) == 0) {
                        const uint32_t dOff = ByteIO::RdU32LE(data + ep + 4u);
                        const size_t descPos = (dOff >= 16u && dOff + 16u <= leHdrSz) ? dOff : (12u + dOff);
                        if (descPos + 16u <= leHdrSz) {
                            const uint32_t palRel = ByteIO::RdU32LE(data + descPos + 4u);
                            if (static_cast<uint64_t>(leHdrSz) + palRel + 1024ull <= size) {
                                foundPalette = data + leHdrSz + palRel;
                            }
                        }
                    }
                }
                for (uint32_t i = 0; i < numRes; ++i) {
                    const size_t ep = 16u + static_cast<size_t>(i) * 16u;
                    if (std::memcmp(data + ep, "TX2D", 4) == 0 || std::memcmp(data + ep, "D2XT", 4) == 0) {
                        const uint32_t dOff = ByteIO::RdU32LE(data + ep + 4u);
                        for (size_t descPos : { static_cast<size_t>(dOff), static_cast<size_t>(12u + dOff) }) {
                            if (descPos >= 16u && descPos + 20u <= leHdrSz) {
                                const uint32_t dataRel = ByteIO::RdU32LE(data + descPos + 4u);
                                const uint32_t dwFmt   = ByteIO::RdU32LE(data + descPos + 12u);
                                const uint32_t dwSz    = ByteIO::RdU32LE(data + descPos + 16u);
                                if (DecodeNv2aTextureDescriptor(data, size, leHdrSz, dataRel, dwFmt, dwSz, foundPalette, outInfo)) {
                                    outInfo.formatName = L"Original Xbox XPR2 Bundle (.xpr)";
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Direct D3DTexture at +12 (standard XPR0) or inside bundled XPR0/XPR2 header block
        for (size_t hp = 12u; hp + 20u <= leHdrSz; hp += 4u) {
            const uint32_t common = ByteIO::RdU32LE(data + hp);
            if ((common & 0x00070000u) == 0x00040000u && (common & 0xFFFFu) <= 16u) {
                const uint32_t dataRel = ByteIO::RdU32LE(data + hp + 4u);
                const uint32_t dwFmt   = ByteIO::RdU32LE(data + hp + 12u);
                const uint32_t dwSz    = ByteIO::RdU32LE(data + hp + 16u);
                if (DecodeNv2aTextureDescriptor(data, size, leHdrSz, dataRel, dwFmt, dwSz, foundPalette, outInfo)) {
                    return true;
                }
            }
        }
    }

    // 2. Big-Endian Xbox 360 "XPR2" Multi-Resource Bundle:
    //    [magic:"XPR2"][hdrSize:u32BE][dataSize:u32BE][numRes:u32BE][entries: {type4CC, hdrOff, hdrLen, nameOff}...]
    if (isXpr2) {
        const uint32_t beHdrSz  = ByteIO::RdU32BE(data + 4);
        const uint32_t beDataSz = ByteIO::RdU32BE(data + 8);
        const uint32_t numRes   = ByteIO::RdU32BE(data + 12);
        const size_t dataRegionStart = 12u + static_cast<size_t>(beHdrSz);
        if (beHdrSz >= 20u && dataRegionStart < size && numRes >= 1u && numRes <= 512u &&
            16ull + static_cast<uint64_t>(numRes) * 16ull <= dataRegionStart)
        {
            (void)beDataSz;
            for (uint32_t i = 0; i < numRes; ++i) {
                const size_t ep = 16u + static_cast<size_t>(i) * 16u;
                if (std::memcmp(data + ep, "TX2D", 4) != 0) continue;
                const uint32_t hOff = ByteIO::RdU32BE(data + ep + 4u);
                const size_t texDesc = 12u + static_cast<size_t>(hOff);
                if (texDesc + 52u > dataRegionStart) continue;

                // Xenos D3DTexture: 28B base + 24B GPUTEXTURE_FETCH_CONSTANT at +28 (or +32)
                for (size_t fcOff : { texDesc + 28u, texDesc + 32u }) {
                    if (fcOff + 24u > dataRegionStart) continue;
                    const uint32_t fc0 = ByteIO::RdU32BE(data + fcOff + 0u);
                    const uint32_t fc1 = ByteIO::RdU32BE(data + fcOff + 4u);
                    const uint32_t fc3 = ByteIO::RdU32BE(data + fcOff + 12u);
                    const uint32_t fc4 = ByteIO::RdU32BE(data + fcOff + 16u);
                    (void)fc3;
                    const uint32_t xFmt = fc1 & 0x3Fu;
                    const uint32_t baseAddr = (fc1 & ~0xFFFu);
                    const uint32_t w = (fc4 & 0x1FFFu) + 1u;
                    const uint32_t h = ((fc4 >> 13u) & 0x1FFFu) + 1u;
                    (void)fc0;
                    if (w == 0 || h == 0 || w > 4096u || h > 4096u) continue;
                    if (dataRegionStart + baseAddr >= size) continue;
                    const uint8_t* raw = data + dataRegionStart + baseAddr;
                    const size_t avail = size - (dataRegionStart + baseAddr);

                    if (xFmt == 0x12u || xFmt == 0x13u || xFmt == 0x14u) { // DXT1 / DXT3 / DXT5 (16-bit endian-swapped words on Xenos)
                        const int bcMode = (xFmt == 0x12u) ? 1 : ((xFmt == 0x13u) ? 2 : 3);
                        const size_t blkBytes = (bcMode == 1) ? 8u : 16u;
                        const size_t need = static_cast<size_t>((w + 3u) / 4u) * ((h + 3u) / 4u) * blkBytes;
                        if (need > avail) continue;
                        std::vector<uint8_t> swapped(need);
                        for (size_t k = 0; k < need; k += 2u) {
                            swapped[k] = raw[k + 1u];
                            swapped[k + 1u] = raw[k];
                        }
                        if (ConsolePx::DecodeLinearS3tcToPbgra(swapped.data(), swapped.size(), w, h, bcMode, outInfo.bgraPixels)) {
                            outInfo.width = outInfo.origWidth = w;
                            outInfo.height = outInfo.origHeight = h;
                            outInfo.channels = 4u;
                            outInfo.bitsPerChannel = 8u;
                            outInfo.formatName = L"Xbox 360 XPR2 Bundle (.xpr)";
                            outInfo.valid = true;
                            return true;
                        }
                    } else if (xFmt == 0x06u && avail >= static_cast<size_t>(w) * h * 4u) { // ARGB8 BE
                        outInfo.width = outInfo.origWidth = w;
                        outInfo.height = outInfo.origHeight = h;
                        outInfo.channels = 4u;
                        outInfo.bitsPerChannel = 8u;
                        outInfo.formatName = L"Xbox 360 XPR2 Bundle (.xpr)";
                        outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);
                        for (size_t p = 0; p < static_cast<size_t>(w) * h; ++p) {
                            WritePremulBGRA(&outInfo.bgraPixels[p * 4u], raw[p * 4u + 1u], raw[p * 4u + 2u], raw[p * 4u + 3u], raw[p * 4u + 0u]);
                        }
                        outInfo.valid = true;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

} // namespace Formats
