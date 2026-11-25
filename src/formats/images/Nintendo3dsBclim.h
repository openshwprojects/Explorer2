#pragma once
// Nintendo3dsBclim.h — Nintendo 3DS CTR Binary Layout Image (.bclim — "CLIM" footer)
// Decodes PICA200 8x8 Z-order Morton-swizzled formats (`L8`, `A8`, `LA4`, `LA8`, `HILO8`, `RGB565`,
// `RGB8`, `RGBA5551`, `RGBA4444`, `RGBA8`, `ETC1`, `ETC1A4`, `L4`, `A4`) via ConsolePixelFormats.h.

namespace Formats {

namespace DetailNintendo3dsBclim {

inline bool CtrFormatBitsPerPixel(uint32_t fmt, uint32_t& outBpp, const wchar_t*& outName) {
    switch (fmt) {
    case 0u:  outBpp = 8u;  outName = L"L8";       return true;
    case 1u:  outBpp = 8u;  outName = L"A8";       return true;
    case 2u:  outBpp = 8u;  outName = L"LA4";      return true;
    case 3u:  outBpp = 16u; outName = L"LA8";      return true;
    case 4u:  outBpp = 16u; outName = L"HILO8";    return true;
    case 5u:  outBpp = 16u; outName = L"RGB565";   return true;
    case 6u:  outBpp = 24u; outName = L"RGB8";     return true;
    case 7u:  outBpp = 16u; outName = L"RGBA5551"; return true;
    case 8u:  outBpp = 16u; outName = L"RGBA4444"; return true;
    case 9u:  outBpp = 32u; outName = L"RGBA8";    return true;
    case 10u: outBpp = 4u;  outName = L"ETC1";     return true;
    case 11u: outBpp = 8u;  outName = L"ETC1A4";   return true;
    case 12u: outBpp = 4u;  outName = L"L4";       return true;
    case 13u: outBpp = 4u;  outName = L"A4";       return true;
    default:  return false;
    }
}

} // namespace DetailNintendo3dsBclim

inline bool DecodeNintendo3dsBclim(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 0x2Cu || size > (64u << 20)) return false;

    const size_t footerOff = size - 0x28u;
    const uint8_t* clim = data + footerOff;
    if (std::memcmp(clim, "CLIM", 4) != 0) return false;
    if (std::memcmp(clim + 0x14u, "imag", 4) != 0) return false;

    const uint16_t bomRaw = ByteIO::RdU16LE(clim + 4u);
    if (bomRaw != 0xFEFFu && bomRaw != 0xFFFEu) return false;
    const bool be = (bomRaw == 0xFFFEu);

    const uint16_t hdrSz   = ByteIO::RdU16(clim + 6u, be);
    const uint32_t totalSz = ByteIO::RdU32(clim + 12u, be);
    const uint16_t nBlocks = ByteIO::RdU16(clim + 16u, be);
    if (hdrSz != 0x14u || totalSz != size || nBlocks < 1u) return false;

    const uint8_t* imag    = clim + 0x14u;
    const uint32_t imagSz  = ByteIO::RdU32(imag + 4u, be);
    const uint16_t width   = ByteIO::RdU16(imag + 8u, be);
    const uint16_t height  = ByteIO::RdU16(imag + 10u, be);
    const uint32_t ctrFmt  = ByteIO::RdU32(imag + 12u, be);
    const uint32_t imgDataLen = ByteIO::RdU32(imag + 16u, be);

    if (imagSz != 0x10u || width == 0u || height == 0u || width > 4096u || height > 4096u) return false;
    if (imgDataLen == 0u || imgDataLen > footerOff) return false;

    uint32_t bpp = 0u;
    const wchar_t* fmtName = L"";
    if (!DetailNintendo3dsBclim::CtrFormatBitsPerPixel(ctrFmt, bpp, fmtName)) return false;

    const uint32_t tilesX = (static_cast<uint32_t>(width)  + 7u) / 8u;
    const uint32_t tilesY = (static_cast<uint32_t>(height) + 7u) / 8u;
    const uint64_t tilePixels = static_cast<uint64_t>(tilesX) * tilesY * 64ULL;
    const uint64_t neededBytes = (tilePixels * bpp) / 8ULL;
    if (neededBytes > imgDataLen) return false;

    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4u, 0u);
    auto putPx = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < width && y < height) {
            WritePremulBGRA(&bgra[(static_cast<size_t>(y) * width + x) * 4u], r, g, b, a);
        }
    };

    if (ctrFmt == 10u || ctrFmt == 11u) {
        // PICA200 ETC1 / ETC1A4: 8x8 tile split into four 4x4 sub-blocks in Z-order (0,0), (4,0), (0,4), (4,4)
        const bool hasA4 = (ctrFmt == 11u);
        size_t pos = 0u;
        for (uint32_t ty = 0u; ty < tilesY; ++ty) {
            for (uint32_t tx = 0u; tx < tilesX; ++tx) {
                for (uint32_t sb = 0u; sb < 4u; ++sb) {
                    uint64_t alphaBits = 0xFFFFFFFFFFFFFFFFULL;
                    if (hasA4) {
                        alphaBits = ByteIO::RdU64LE(data + pos);
                        pos += 8u;
                    }
                    // 3DS stores ETC1 64-bit word little-endian; byte-swap to big-endian for standard ETC1 decoder
                    uint8_t etcBe[8];
                    for (int k = 0; k < 8; ++k) etcBe[k] = data[pos + 7 - k];
                    pos += 8u;

                    uint8_t subRgba[64]{};
                    GpuTex::DecodeETC1Block(etcBe, subRgba);

                    const uint32_t sx = tx * 8u + ((sb & 1u) * 4u);
                    const uint32_t sy = ty * 8u + ((sb >> 1u) * 4u);
                    for (uint32_t py = 0u; py < 4u; ++py) {
                        for (uint32_t px = 0u; px < 4u; ++px) {
                            const uint8_t* c = &subRgba[(py * 4u + px) * 4u];
                            const uint8_t a = hasA4
                                ? static_cast<uint8_t>(((alphaBits >> ((px * 4u + py) * 4u)) & 0x0Fu) * 17u)
                                : 255u;
                            putPx(sx + px, sy + py, c[0], c[1], c[2], a);
                        }
                    }
                }
            }
        }
    } else {
        const size_t tileBytes = (64u * bpp) / 8u;
        for (uint32_t ty = 0u; ty < tilesY; ++ty) {
            for (uint32_t tx = 0u; tx < tilesX; ++tx) {
                const uint8_t* tileBase = data + (static_cast<size_t>(ty) * tilesX + tx) * tileBytes;
                for (uint32_t ly = 0u; ly < 8u; ++ly) {
                    for (uint32_t lx = 0u; lx < 8u; ++lx) {
                        const uint32_t mIdx = ConsolePx::Part1By1(lx) | (ConsolePx::Part1By1(ly) << 1u);
                        uint8_t r = 0, g = 0, b = 0, a = 255u;
                        switch (ctrFmt) {
                        case 0u: r = g = b = tileBase[mIdx]; break;
                        case 1u: r = g = b = 255u; a = tileBase[mIdx]; break;
                        case 2u: {
                            const uint8_t v = tileBase[mIdx];
                            a = static_cast<uint8_t>((v & 0x0Fu) * 17u);
                            r = g = b = static_cast<uint8_t>((v >> 4u) * 17u);
                            break;
                        }
                        case 3u: a = tileBase[mIdx * 2u + 0u]; r = g = b = tileBase[mIdx * 2u + 1u]; break;
                        case 4u: g = tileBase[mIdx * 2u + 0u]; r = tileBase[mIdx * 2u + 1u]; b = 0u; break;
                        case 5u: GpuTex::UnpackRgb565(ByteIO::RdU16(tileBase + mIdx * 2u, be), r, g, b); break;
                        case 6u:
                            b = tileBase[mIdx * 3u + 0u];
                            g = tileBase[mIdx * 3u + 1u];
                            r = tileBase[mIdx * 3u + 2u];
                            break;
                        case 7u: {
                            const uint16_t px = ByteIO::RdU16(tileBase + mIdx * 2u, be);
                            r = static_cast<uint8_t>(((px >> 11u) & 0x1Fu) * 255u / 31u);
                            g = static_cast<uint8_t>(((px >>  6u) & 0x1Fu) * 255u / 31u);
                            b = static_cast<uint8_t>(((px >>  1u) & 0x1Fu) * 255u / 31u);
                            a = (px & 1u) ? 255u : 0u;
                            break;
                        }
                        case 8u: {
                            const uint16_t px = ByteIO::RdU16(tileBase + mIdx * 2u, be);
                            r = static_cast<uint8_t>(((px >> 12u) & 0x0Fu) * 17u);
                            g = static_cast<uint8_t>(((px >>  8u) & 0x0Fu) * 17u);
                            b = static_cast<uint8_t>(((px >>  4u) & 0x0Fu) * 17u);
                            a = static_cast<uint8_t>((px & 0x0Fu) * 17u);
                            break;
                        }
                        case 9u: {
                            const uint32_t px = ByteIO::RdU32(tileBase + mIdx * 4u, be);
                            r = static_cast<uint8_t>((px >> 24u) & 0xFFu);
                            g = static_cast<uint8_t>((px >> 16u) & 0xFFu);
                            b = static_cast<uint8_t>((px >>  8u) & 0xFFu);
                            a = static_cast<uint8_t>(px & 0xFFu);
                            break;
                        }
                        case 12u: {
                            const uint8_t nib = (mIdx & 1u) ? (tileBase[mIdx >> 1u] >> 4u) : (tileBase[mIdx >> 1u] & 0x0Fu);
                            r = g = b = static_cast<uint8_t>(nib * 17u);
                            break;
                        }
                        case 13u: {
                            const uint8_t nib = (mIdx & 1u) ? (tileBase[mIdx >> 1u] >> 4u) : (tileBase[mIdx >> 1u] & 0x0Fu);
                            r = g = b = 255u;
                            a = static_cast<uint8_t>(nib * 17u);
                            break;
                        }
                        default: return false;
                        }
                        putPx(tx * 8u + lx, ty * 8u + ly, r, g, b, a);
                    }
                }
            }
        }
    }

    std::wstring title = std::wstring(L"Nintendo 3DS BCLIM (") + fmtName + L")";
    return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), std::move(title), maxDimension);
}

} // namespace Formats
