#pragma once
// NintendoGxTileCommon.h — Shared DRY GameCube / Wii Flipper & Hollywood GX Texture Tile Decoder
// Used by NintendoTplImage.h (.tpl), NintendoBtiImage.h (.bti), and SegaGvrImage.h (.gvr).
// Supports I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8 (dual-cacheline 64B), CI4, CI8, CI14X2, and CMPR (2x2 S3TC).

namespace Formats::NintendoGx {

enum GxTexFmt : uint32_t {
    GX_TF_I4     = 0x00,
    GX_TF_I8     = 0x01,
    GX_TF_IA4    = 0x02,
    GX_TF_IA8    = 0x03,
    GX_TF_RGB565 = 0x04,
    GX_TF_RGB5A3 = 0x05,
    GX_TF_RGBA8  = 0x06,
    GX_TF_CI4    = 0x08,
    GX_TF_CI8    = 0x09,
    GX_TF_CI14X2 = 0x0A,
    GX_TF_CMPR   = 0x0E,
};

enum GxTlutFmt : uint32_t {
    GX_TL_IA8    = 0x00,
    GX_TL_RGB565 = 0x01,
    GX_TL_RGB5A3 = 0x02,
};

inline void UnpackGxRgb5A3(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if ((px & 0x8000u) != 0u) {
        r = static_cast<uint8_t>(((px >> 10) & 0x1Fu) * 255u / 31u);
        g = static_cast<uint8_t>(((px >>  5) & 0x1Fu) * 255u / 31u);
        b = static_cast<uint8_t>(((px >>  0) & 0x1Fu) * 255u / 31u);
        a = 255u;
    } else {
        a = static_cast<uint8_t>(((px >> 12) & 0x07u) * 255u / 7u);
        r = static_cast<uint8_t>(((px >>  8) & 0x0Fu) * 17u);
        g = static_cast<uint8_t>(((px >>  4) & 0x0Fu) * 17u);
        b = static_cast<uint8_t>(((px >>  0) & 0x0Fu) * 17u);
    }
}

inline const wchar_t* GxFormatName(uint32_t fmt) {
    switch (fmt) {
    case GX_TF_I4:     return L"I4";
    case GX_TF_I8:     return L"I8";
    case GX_TF_IA4:    return L"IA4";
    case GX_TF_IA8:    return L"IA8";
    case GX_TF_RGB565: return L"RGB565";
    case GX_TF_RGB5A3: return L"RGB5A3";
    case GX_TF_RGBA8:  return L"RGBA8";
    case GX_TF_CI4:    return L"CI4";
    case GX_TF_CI8:    return L"CI8";
    case GX_TF_CI14X2: return L"CI14X2";
    case GX_TF_CMPR:   return L"CMPR";
    default:           return L"Unknown";
    }
}

inline bool GetGxTileMetrics(
    uint32_t fmt,
    uint32_t& outTileW,
    uint32_t& outTileH,
    uint32_t& outTileBytes,
    bool& outNeedsPalette)
{
    outNeedsPalette = false;
    switch (fmt) {
    case GX_TF_I4:     outTileW = 8; outTileH = 8; outTileBytes = 32; return true;
    case GX_TF_I8:     outTileW = 8; outTileH = 4; outTileBytes = 32; return true;
    case GX_TF_IA4:    outTileW = 8; outTileH = 4; outTileBytes = 32; return true;
    case GX_TF_IA8:    outTileW = 4; outTileH = 4; outTileBytes = 32; return true;
    case GX_TF_RGB565: outTileW = 4; outTileH = 4; outTileBytes = 32; return true;
    case GX_TF_RGB5A3: outTileW = 4; outTileH = 4; outTileBytes = 32; return true;
    case GX_TF_RGBA8:  outTileW = 4; outTileH = 4; outTileBytes = 64; return true;
    case GX_TF_CI4:    outTileW = 8; outTileH = 8; outTileBytes = 32; outNeedsPalette = true; return true;
    case GX_TF_CI8:    outTileW = 8; outTileH = 4; outTileBytes = 32; outNeedsPalette = true; return true;
    case GX_TF_CI14X2: outTileW = 4; outTileH = 4; outTileBytes = 32; outNeedsPalette = true; return true;
    case GX_TF_CMPR:   outTileW = 8; outTileH = 8; outTileBytes = 32; return true;
    default:           return false;
    }
}

inline bool ComputeGxLevel0Size(uint32_t width, uint32_t height, uint32_t fmt, uint64_t& outBytes) {
    uint32_t tw = 0, th = 0, tb = 0;
    bool pal = false;
    if (width == 0 || height == 0 || !GetGxTileMetrics(fmt, tw, th, tb, pal)) return false;
    const uint64_t blocksX = (static_cast<uint64_t>(width)  + tw - 1ULL) / tw;
    const uint64_t blocksY = (static_cast<uint64_t>(height) + th - 1ULL) / th;
    outBytes = blocksX * blocksY * tb;
    return true;
}

inline bool DecodeGxTlut(
    const uint8_t* tlutData,
    size_t availBytes,
    uint32_t entryCount,
    uint32_t tlutFmt,
    std::vector<std::array<uint8_t, 4>>& outPalette)
{
    if (!tlutData || entryCount == 0 || entryCount > 16384u) return false;
    if (static_cast<uint64_t>(entryCount) * 2ULL > availBytes) return false;
    if (tlutFmt > GX_TL_RGB5A3) return false;

    outPalette.resize(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i) {
        const uint8_t* p = tlutData + static_cast<size_t>(i) * 2u;
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (tlutFmt == GX_TL_IA8) {
            a = p[0];
            r = g = b = p[1];
        } else if (tlutFmt == GX_TL_RGB565) {
            GpuTex::UnpackRgb565(ByteIO::RdU16BE(p), r, g, b);
            a = 255u;
        } else {
            UnpackGxRgb5A3(ByteIO::RdU16BE(p), r, g, b, a);
        }
        outPalette[i] = { r, g, b, a };
    }
    return true;
}

inline bool DecodeGxSurfaceToPbgra(
    const uint8_t* src,
    size_t srcBytes,
    uint32_t width,
    uint32_t height,
    uint32_t fmt,
    const std::vector<std::array<uint8_t, 4>>* palette,
    std::vector<uint8_t>& outBgra)
{
    if (!src || width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) {
        return false;
    }
    if (static_cast<uint64_t>(width) * height > 64ULL * 1024ULL * 1024ULL) {
        return false;
    }

    uint32_t tw = 0, th = 0, tb = 0;
    bool needsPal = false;
    if (!GetGxTileMetrics(fmt, tw, th, tb, needsPal)) return false;
    if (needsPal && (!palette || palette->empty())) return false;

    uint64_t neededBytes = 0;
    if (!ComputeGxLevel0Size(width, height, fmt, neededBytes) || neededBytes > srcBytes) {
        return false;
    }

    outBgra.assign(static_cast<size_t>(width) * height * 4u, 0u);
    auto putPx = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < width && y < height) {
            WritePremulBGRA(&outBgra[(static_cast<size_t>(y) * width + x) * 4u], r, g, b, a);
        }
    };

    const uint32_t tilesX = (width  + tw - 1u) / tw;
    const uint32_t tilesY = (height + th - 1u) / th;
    size_t off = 0;

    for (uint32_t ty = 0; ty < tilesY; ++ty) {
        for (uint32_t tx = 0; tx < tilesX; ++tx) {
            const uint8_t* tile = src + off;
            off += tb;
            const uint32_t bx = tx * tw;
            const uint32_t by = ty * th;

            switch (fmt) {
            case GX_TF_I4: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 8u; ++dy) {
                    for (uint32_t dx = 0; dx < 8u; dx += 2u) {
                        const uint8_t byte = tile[k++];
                        const uint8_t i0 = static_cast<uint8_t>((byte >> 4) * 17u);
                        const uint8_t i1 = static_cast<uint8_t>((byte & 0x0Fu) * 17u);
                        putPx(bx + dx,      by + dy, i0, i0, i0, 255u);
                        putPx(bx + dx + 1u, by + dy, i1, i1, i1, 255u);
                    }
                }
                break;
            }
            case GX_TF_I8: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 8u; ++dx) {
                        const uint8_t iv = tile[k++];
                        putPx(bx + dx, by + dy, iv, iv, iv, 255u);
                    }
                }
                break;
            }
            case GX_TF_IA4: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 8u; ++dx) {
                        const uint8_t byte = tile[k++];
                        const uint8_t a  = static_cast<uint8_t>((byte >> 4) * 17u);
                        const uint8_t iv = static_cast<uint8_t>((byte & 0x0Fu) * 17u);
                        putPx(bx + dx, by + dy, iv, iv, iv, a);
                    }
                }
                break;
            }
            case GX_TF_IA8: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 4u; ++dx) {
                        const uint8_t a  = tile[k++];
                        const uint8_t iv = tile[k++];
                        putPx(bx + dx, by + dy, iv, iv, iv, a);
                    }
                }
                break;
            }
            case GX_TF_RGB565: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 4u; ++dx) {
                        uint8_t r = 0, g = 0, b = 0;
                        GpuTex::UnpackRgb565(ByteIO::RdU16BE(tile + k), r, g, b);
                        k += 2u;
                        putPx(bx + dx, by + dy, r, g, b, 255u);
                    }
                }
                break;
            }
            case GX_TF_RGB5A3: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 4u; ++dx) {
                        uint8_t r = 0, g = 0, b = 0, a = 255;
                        UnpackGxRgb5A3(ByteIO::RdU16BE(tile + k), r, g, b, a);
                        k += 2u;
                        putPx(bx + dx, by + dy, r, g, b, a);
                    }
                }
                break;
            }
            case GX_TF_RGBA8: {
                size_t i = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 4u; ++dx) {
                        const uint8_t a = tile[i * 2u + 0u];
                        const uint8_t r = tile[i * 2u + 1u];
                        const uint8_t g = tile[32u + i * 2u + 0u];
                        const uint8_t b = tile[32u + i * 2u + 1u];
                        ++i;
                        putPx(bx + dx, by + dy, r, g, b, a);
                    }
                }
                break;
            }
            case GX_TF_CI4: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 8u; ++dy) {
                    for (uint32_t dx = 0; dx < 8u; dx += 2u) {
                        const uint8_t byte = tile[k++];
                        const uint8_t idx0 = static_cast<uint8_t>((byte >> 4) & 0x0Fu);
                        const uint8_t idx1 = static_cast<uint8_t>(byte & 0x0Fu);
                        const auto c0 = (idx0 < palette->size()) ? (*palette)[idx0] : std::array<uint8_t, 4>{ 0, 0, 0, 0 };
                        const auto c1 = (idx1 < palette->size()) ? (*palette)[idx1] : std::array<uint8_t, 4>{ 0, 0, 0, 0 };
                        putPx(bx + dx,      by + dy, c0[0], c0[1], c0[2], c0[3]);
                        putPx(bx + dx + 1u, by + dy, c1[0], c1[1], c1[2], c1[3]);
                    }
                }
                break;
            }
            case GX_TF_CI8: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 8u; ++dx) {
                        const uint8_t idx = tile[k++];
                        const auto c = (idx < palette->size()) ? (*palette)[idx] : std::array<uint8_t, 4>{ 0, 0, 0, 0 };
                        putPx(bx + dx, by + dy, c[0], c[1], c[2], c[3]);
                    }
                }
                break;
            }
            case GX_TF_CI14X2: {
                size_t k = 0;
                for (uint32_t dy = 0; dy < 4u; ++dy) {
                    for (uint32_t dx = 0; dx < 4u; ++dx) {
                        const uint16_t idx = static_cast<uint16_t>(ByteIO::RdU16BE(tile + k) & 0x3FFFu);
                        k += 2u;
                        const auto c = (idx < palette->size()) ? (*palette)[idx] : std::array<uint8_t, 4>{ 0, 0, 0, 0 };
                        putPx(bx + dx, by + dy, c[0], c[1], c[2], c[3]);
                    }
                }
                break;
            }
            case GX_TF_CMPR: {
                // Four 4x4 big-endian DXT1 sub-blocks in Z-order: (0,0), (4,0), (0,4), (4,4)
                for (uint32_t sb = 0; sb < 4u; ++sb) {
                    const uint32_t sx = bx + ((sb & 1u) * 4u);
                    const uint32_t sy = by + ((sb >> 1u) * 4u);
                    const uint8_t* sp = tile + sb * 8u;
                    const uint16_t c0 = ByteIO::RdU16BE(sp + 0u);
                    const uint16_t c1 = ByteIO::RdU16BE(sp + 2u);
                    uint8_t cr[4]{}, cg[4]{}, cb[4]{}, ca[4]{ 255u, 255u, 255u, 255u };
                    GpuTex::UnpackRgb565(c0, cr[0], cg[0], cb[0]);
                    GpuTex::UnpackRgb565(c1, cr[1], cg[1], cb[1]);
                    if (c0 > c1) {
                        cr[2] = static_cast<uint8_t>((5u * cr[0] + 3u * cr[1] + 4u) >> 3);
                        cg[2] = static_cast<uint8_t>((5u * cg[0] + 3u * cg[1] + 4u) >> 3);
                        cb[2] = static_cast<uint8_t>((5u * cb[0] + 3u * cb[1] + 4u) >> 3);
                        cr[3] = static_cast<uint8_t>((3u * cr[0] + 5u * cr[1] + 4u) >> 3);
                        cg[3] = static_cast<uint8_t>((3u * cg[0] + 5u * cg[1] + 4u) >> 3);
                        cb[3] = static_cast<uint8_t>((3u * cb[0] + 5u * cb[1] + 4u) >> 3);
                    } else {
                        cr[2] = static_cast<uint8_t>((static_cast<uint32_t>(cr[0]) + cr[1]) >> 1);
                        cg[2] = static_cast<uint8_t>((static_cast<uint32_t>(cg[0]) + cg[1]) >> 1);
                        cb[2] = static_cast<uint8_t>((static_cast<uint32_t>(cb[0]) + cb[1]) >> 1);
                        cr[3] = cg[3] = cb[3] = 0u;
                        ca[3] = 0u;
                    }
                    for (uint32_t ry = 0; ry < 4u; ++ry) {
                        const uint8_t rowBits = sp[4u + ry];
                        for (uint32_t rx = 0; rx < 4u; ++rx) {
                            const uint8_t sel = static_cast<uint8_t>((rowBits >> (6u - rx * 2u)) & 0x03u);
                            putPx(sx + rx, sy + ry, cr[sel], cg[sel], cb[sel], ca[sel]);
                        }
                    }
                }
                break;
            }
            default:
                return false;
            }
        }
    }

    return true;
}

} // namespace Formats::NintendoGx
