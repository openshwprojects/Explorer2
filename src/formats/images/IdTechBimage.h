#pragma once
// IdTechBimage.h — id Tech 5 / DOOM 3 BFG Binary Image (.bimage — "BIM\x0A".."BIM\x14") Decoder

namespace Formats {

inline bool DecodeIdTechBimage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 48u) return false;

    // Standard DOOM 3 BFG / id Tech 5 .bimage header:
    //   [0..7] int64BE sourceFileTime, [8..11] "BIM" + version byte (10..20),
    //   or [0..3] "BIM" + version byte when timestamp is omitted.
    size_t hdrBase = 0;
    uint8_t bimVer = 0;
    if (std::memcmp(data + 8, "BIM", 3) == 0 && data[11] >= 0x07u && data[11] <= 0x20u) {
        bimVer = data[11];
        hdrBase = 12u;
    } else if (std::memcmp(data, "BIM", 3) == 0 && data[3] >= 0x07u && data[3] <= 0x20u) {
        bimVer = data[3];
        hdrBase = 4u;
    } else {
        return false;
    }

    if (hdrBase + 44u > size) return false;

    const uint32_t texType     = ByteIO::RdU32BE(data + hdrBase + 0u);  // 0 = TT_2D, 1 = TT_CUBIC
    const uint32_t texFmt      = ByteIO::RdU32BE(data + hdrBase + 4u);  // textureFormat_t
    const uint32_t colorFmt    = ByteIO::RdU32BE(data + hdrBase + 8u);  // 0=DEFAULT, 1=NORMAL_DXT5, 2=YCOCG_DXT5, 3=GREEN_ALPHA
    const uint32_t hdrWidth    = ByteIO::RdU32BE(data + hdrBase + 12u);
    const uint32_t hdrHeight   = ByteIO::RdU32BE(data + hdrBase + 16u);
    const uint32_t numLevels   = ByteIO::RdU32BE(data + hdrBase + 20u);

    if (texType > 2u || hdrWidth == 0 || hdrHeight == 0 || hdrWidth > 8192u || hdrHeight > 8192u ||
        numLevels == 0 || numLevels > 64u)
    {
        return false;
    }

    // Mip 0 header (idBinaryImageData = 5 x uint32_t BE: [level, destZ, width, height, dataSize])
    const size_t mipHdrOff = hdrBase + 24u;
    if (mipHdrOff + 20u > size) return false;

    const uint32_t mipLevel = ByteIO::RdU32BE(data + mipHdrOff + 0u);
    const uint32_t mipDestZ = ByteIO::RdU32BE(data + mipHdrOff + 4u);
    const uint32_t w        = ByteIO::RdU32BE(data + mipHdrOff + 8u);
    const uint32_t h        = ByteIO::RdU32BE(data + mipHdrOff + 12u);
    const uint32_t dataSize = ByteIO::RdU32BE(data + mipHdrOff + 16u);
    (void)mipLevel; (void)mipDestZ;

    const size_t pixOff = mipHdrOff + 20u;
    if (w == 0 || h == 0 || w > 8192u || h > 8192u || dataSize == 0 || dataSize > size - pixOff) {
        return false;
    }

    const uint8_t* src = data + pixOff;
    const size_t nPx = static_cast<size_t>(w) * h;

    // Decode into straight RGBA8 first so CFM_YCOCG_DXT5 / CFM_NORMAL_DXT5 / CFM_GREEN_ALPHA apply before premultiplication
    std::vector<uint8_t> rgba(nPx * 4u, 255u);
    uint32_t channels = 4u;

    switch (texFmt) {
        case 1u: { // FMT_RGBA8
            if (dataSize < nPx * 4u) return false;
            std::memcpy(rgba.data(), src, nPx * 4u);
            break;
        }
        case 2u: { // FMT_XRGB8 (4 bytes per pixel, alpha forced 255)
            if (dataSize < nPx * 4u) return false;
            channels = 3u;
            for (size_t i = 0; i < nPx; ++i) {
                rgba[i * 4u + 0u] = src[i * 4u + 0u];
                rgba[i * 4u + 1u] = src[i * 4u + 1u];
                rgba[i * 4u + 2u] = src[i * 4u + 2u];
                rgba[i * 4u + 3u] = 255u;
            }
            break;
        }
        case 3u: { // FMT_ALPHA (1 byte alpha mask -> white ink with alpha)
            if (dataSize < nPx) return false;
            for (size_t i = 0; i < nPx; ++i) {
                rgba[i * 4u + 0u] = rgba[i * 4u + 1u] = rgba[i * 4u + 2u] = 255u;
                rgba[i * 4u + 3u] = src[i];
            }
            break;
        }
        case 4u: { // FMT_L8A8
            if (dataSize < nPx * 2u) return false;
            channels = 2u;
            for (size_t i = 0; i < nPx; ++i) {
                rgba[i * 4u + 0u] = rgba[i * 4u + 1u] = rgba[i * 4u + 2u] = src[i * 2u + 0u];
                rgba[i * 4u + 3u] = src[i * 2u + 1u];
            }
            break;
        }
        case 5u:   // FMT_LUM8
        case 6u: { // FMT_INT8 (intensity: L=A=v for INT8, L=v A=255 for LUM8)
            if (dataSize < nPx) return false;
            channels = 1u;
            for (size_t i = 0; i < nPx; ++i) {
                rgba[i * 4u + 0u] = rgba[i * 4u + 1u] = rgba[i * 4u + 2u] = src[i];
                rgba[i * 4u + 3u] = (texFmt == 6u) ? src[i] : 255u;
            }
            break;
        }
        case 7u:   // FMT_DXT1
        case 8u:   // FMT_DXT5
        case 13u: { // FMT_ETC1
            const uint32_t bw = (w + 3u) / 4u;
            const uint32_t bh = (h + 3u) / 4u;
            const size_t blkBytes = (texFmt == 8u) ? 16u : 8u;
            if (dataSize < static_cast<size_t>(bw) * bh * blkBytes) return false;
            uint8_t blk[64];
            const uint8_t* bp = src;
            for (uint32_t by = 0; by < bh; ++by) {
                for (uint32_t bx = 0; bx < bw; ++bx) {
                    if (texFmt == 7u)      GpuTex::DecodeBC1Block(bp, blk, true, false);
                    else if (texFmt == 8u) GpuTex::DecodeBC3Block(bp, blk);
                    else                   GpuTex::DecodeETC1Block(bp, blk);
                    bp += blkBytes;
                    for (uint32_t py = 0; py < 4u && by * 4u + py < h; ++py) {
                        for (uint32_t px = 0; px < 4u && bx * 4u + px < w; ++px) {
                            std::memcpy(&rgba[(static_cast<size_t>(by * 4u + py) * w + (bx * 4u + px)) * 4u],
                                        &blk[(py * 4u + px) * 4u], 4u);
                        }
                    }
                }
            }
            break;
        }
        case 10u: { // FMT_X16 (16-bit luminance)
            if (dataSize < nPx * 2u) return false;
            channels = 1u;
            for (size_t i = 0; i < nPx; ++i) {
                uint8_t v = static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16LE(src + i * 2u)) * 255u + 32767u) / 65535u);
                rgba[i * 4u + 0u] = rgba[i * 4u + 1u] = rgba[i * 4u + 2u] = v;
                rgba[i * 4u + 3u] = 255u;
            }
            break;
        }
        case 11u: { // FMT_Y16_X16 (16-bit RG)
            if (dataSize < nPx * 4u) return false;
            channels = 2u;
            for (size_t i = 0; i < nPx; ++i) {
                rgba[i * 4u + 0u] = static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16LE(src + i * 4u + 0u)) * 255u + 32767u) / 65535u);
                rgba[i * 4u + 1u] = static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16LE(src + i * 4u + 2u)) * 255u + 32767u) / 65535u);
                rgba[i * 4u + 2u] = 0u;
                rgba[i * 4u + 3u] = 255u;
            }
            break;
        }
        case 12u: { // FMT_RGB565 (big-endian on BFG console/BE tools or LE on PC; check standard RGB565)
            if (dataSize < nPx * 2u) return false;
            channels = 3u;
            for (size_t i = 0; i < nPx; ++i) {
                uint8_t r = 0, g = 0, b = 0;
                GpuTex::UnpackRgb565(ByteIO::RdU16LE(src + i * 2u), r, g, b);
                rgba[i * 4u + 0u] = r; rgba[i * 4u + 1u] = g; rgba[i * 4u + 2u] = b; rgba[i * 4u + 3u] = 255u;
            }
            break;
        }
        case 14u: { // FMT_RG8
            if (dataSize < nPx * 2u) return false;
            channels = 2u;
            for (size_t i = 0; i < nPx; ++i) {
                rgba[i * 4u + 0u] = src[i * 2u + 0u];
                rgba[i * 4u + 1u] = src[i * 2u + 1u];
                rgba[i * 4u + 2u] = 0u;
                rgba[i * 4u + 3u] = 255u;
            }
            break;
        }
        default:
            return false;
    }

    // Apply id Tech 5 textureColor_t transformations
    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = hdrWidth;
    outInfo.origHeight = hdrHeight;
    outInfo.channels = channels;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"id Tech 5 Binary Image (BIM v" + std::to_wstring(bimVer) + L" .bimage)";
    outInfo.bgraPixels.resize(nPx * 4u);

    for (size_t i = 0; i < nPx; ++i) {
        uint8_t r = rgba[i * 4u + 0u];
        uint8_t g = rgba[i * 4u + 1u];
        uint8_t b = rgba[i * 4u + 2u];
        uint8_t a = rgba[i * 4u + 3u];

        if (colorFmt == 2u) { // CFM_YCOCG_DXT5: Co=R, Cg=G, scale=(B>>3)+1, Y=A
            const int scale = (static_cast<int>(b) >> 3) + 1;
            const int co = (static_cast<int>(r) - 128) / scale;
            const int cg = (static_cast<int>(g) - 128) / scale;
            const int yv = static_cast<int>(a);
            r = static_cast<uint8_t>(std::clamp(yv + co - cg, 0, 255));
            g = static_cast<uint8_t>(std::clamp(yv + cg,      0, 255));
            b = static_cast<uint8_t>(std::clamp(yv - co - cg, 0, 255));
            a = 255u;
        } else if (colorFmt == 1u) { // CFM_NORMAL_DXT5: X in Alpha, Y in Green -> reconstruct tangent Z
            const float nx = (static_cast<float>(a) / 255.0f) * 2.0f - 1.0f;
            const float ny = (static_cast<float>(g) / 255.0f) * 2.0f - 1.0f;
            const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
            r = a;
            b = static_cast<uint8_t>(std::clamp(std::lround((nz * 0.5f + 0.5f) * 255.0f), 0l, 255l));
            a = 255u;
        } else if (colorFmt == 3u) { // CFM_GREEN_ALPHA
            r = b = g;
        }

        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
