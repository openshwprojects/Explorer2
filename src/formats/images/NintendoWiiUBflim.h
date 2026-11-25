#pragma once
// NintendoWiiUBflim.h — Nintendo Wii U / Café Binary Layout Image (.bflim — "FLIM" footer + GX2 tile deswizzling)

namespace Formats {

inline bool DecodeNintendoWiiUBflim(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 0x2Cu || size > (64u << 20)) return false;

    const size_t footerOff = size - 0x28u;
    const uint8_t* flim = data + footerOff;
    if (std::memcmp(flim, "FLIM", 4) != 0) return false;
    if (std::memcmp(flim + 0x14u, "imag", 4) != 0) return false;

    const uint16_t bomLE = ByteIO::RdU16LE(flim + 4u);
    if (bomLE != 0xFEFFu && bomLE != 0xFFFEu) return false;
    const bool be = (bomLE == 0xFFFEu);

    const uint16_t hdrSz   = ByteIO::RdU16(flim + 6u, be);
    const uint32_t totalSz = ByteIO::RdU32(flim + 12u, be);
    const uint16_t nBlocks = ByteIO::RdU16(flim + 16u, be);
    if (hdrSz != 0x14u || totalSz != size || nBlocks < 1u) return false;

    const uint8_t* imag    = flim + 0x14u;
    const uint32_t imagSz  = ByteIO::RdU32(imag + 4u, be);
    const uint16_t width   = ByteIO::RdU16(imag + 8u, be);
    const uint16_t height  = ByteIO::RdU16(imag + 10u, be);
    const uint8_t  flimFmt = imag[14u];
    const uint8_t  swzTile = imag[15u];
    const uint32_t dataLen = ByteIO::RdU32(imag + 16u, be);

    if (imagSz != 0x10u || width == 0u || height == 0u || width > 4096u || height > 4096u) return false;
    if (dataLen == 0u || dataLen > footerOff) return false;

    // Wii U Cafe BFLIM imag[15]: low 5 bits = GX2TileMode (0..15), bit 5 = pipeSwizzle, bits 6..7 = bankSwizzle
    // (Note: 3DS CTR BFLIM uses be == false; Wii U Cafe BFLIM uses be == true with GX2 tiling!)
    const uint32_t tileMode = be ? (swzTile & 0x1Fu) : 0u;
    const uint32_t pipeSwiz = (swzTile >> 5u) & 1u;
    const uint32_t bankSwiz = (swzTile >> 6u) & 3u;
    const size_t srcCap = std::min<size_t>(footerOff, std::max<size_t>(dataLen, footerOff));

    std::vector<uint8_t> deswizzled;
    std::vector<uint8_t> bgra;
    const wchar_t* fmtName = L"RGBA8";

    auto getLinearBlockStream = [&](uint32_t bytesPerBlock, const uint8_t*& outPtr, size_t& outLen) -> bool {
        const uint32_t bw = (width + 3u) / 4u;
        const uint32_t bh = (height + 3u) / 4u;
        if (tileMode != 0u) {
            if (!GpuTex::DeswizzleWiiUGx2Surface(data, srcCap, bw, bh, bytesPerBlock, tileMode, pipeSwiz, bankSwiz, deswizzled)) {
                return false;
            }
            outPtr = deswizzled.data();
            outLen = deswizzled.size();
            return true;
        }
        outPtr = data;
        outLen = dataLen;
        return true;
    };

    if (flimFmt == 0x0Cu || flimFmt == 0x11u || flimFmt == 0x15u) { // BC1 / BC1_SRGB
        fmtName = L"BC1";
        const uint8_t* sPtr = nullptr; size_t sLen = 0;
        if (!getLinearBlockStream(8u, sPtr, sLen)) return false;
        if (!ConsolePx::DecodeLinearS3tcToPbgra(sPtr, sLen, width, height, 1, bgra)) return false;
    } else if (flimFmt == 0x0Du || flimFmt == 0x12u || flimFmt == 0x16u) { // BC2 / BC2_SRGB
        fmtName = L"BC2";
        const uint8_t* sPtr = nullptr; size_t sLen = 0;
        if (!getLinearBlockStream(16u, sPtr, sLen)) return false;
        if (!ConsolePx::DecodeLinearS3tcToPbgra(sPtr, sLen, width, height, 2, bgra)) return false;
    } else if (flimFmt == 0x0Eu || flimFmt == 0x13u || flimFmt == 0x17u) { // BC3 / BC3_SRGB
        fmtName = L"BC3";
        const uint8_t* sPtr = nullptr; size_t sLen = 0;
        if (!getLinearBlockStream(16u, sPtr, sLen)) return false;
        if (!ConsolePx::DecodeLinearS3tcToPbgra(sPtr, sLen, width, height, 3, bgra)) return false;
    } else if (flimFmt == 0x0Fu || flimFmt == 0x10u) { // BC4_L / BC4_A
        fmtName = L"BC4";
        const uint8_t* sPtr = nullptr; size_t sLen = 0;
        if (!getLinearBlockStream(8u, sPtr, sLen)) return false;
        const uint32_t bw = (width + 3u) / 4u;
        const uint32_t bh = (height + 3u) / 4u;
        if (sLen < static_cast<size_t>(bw) * bh * 8u) return false;
        bgra.assign(static_cast<size_t>(width) * height * 4u, 0u);
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                uint8_t ch[16];
                GpuTex::DecodeBC4Block(sPtr + (static_cast<size_t>(by) * bw + bx) * 8u, ch, false);
                for (uint32_t py = 0; py < 4u && by * 4u + py < height; ++py) {
                    for (uint32_t px = 0; px < 4u && bx * 4u + px < width; ++px) {
                        uint8_t v = ch[py * 4u + px];
                        uint8_t r = (flimFmt == 0x10u) ? 255u : v;
                        uint8_t a = (flimFmt == 0x10u) ? v : 255u;
                        WritePremulBGRA(&bgra[(static_cast<size_t>(by * 4u + py) * width + (bx * 4u + px)) * 4u], r, r, r, a);
                    }
                }
            }
        }
    } else {
        size_t bytesPerPx = 0u;
        switch (flimFmt) {
        case 0x00u: bytesPerPx = 1u; fmtName = L"L8";       break;
        case 0x01u: bytesPerPx = 1u; fmtName = L"A8";       break;
        case 0x02u: bytesPerPx = 1u; fmtName = L"LA4";      break;
        case 0x03u: bytesPerPx = 2u; fmtName = L"LA8";      break;
        case 0x05u: bytesPerPx = 2u; fmtName = L"RGB565";   break;
        case 0x06u: bytesPerPx = 3u; fmtName = L"RGB8";     break;
        case 0x07u: bytesPerPx = 2u; fmtName = L"RGBA5551"; break;
        case 0x08u: bytesPerPx = 2u; fmtName = L"RGBA4444"; break;
        case 0x09u:
        case 0x14u: bytesPerPx = 4u; fmtName = L"RGBA8";    break;
        default: return false;
        }

        const uint8_t* pixSrc = data;
        size_t pixSrcLen = dataLen;
        if (tileMode != 0u && (bytesPerPx == 1u || bytesPerPx == 2u || bytesPerPx == 4u)) {
            if (!GpuTex::DeswizzleWiiUGx2Surface(
                    data, srcCap, width, height, static_cast<uint32_t>(bytesPerPx),
                    tileMode, pipeSwiz, bankSwiz, deswizzled))
            {
                return false;
            }
            pixSrc = deswizzled.data();
            pixSrcLen = deswizzled.size();
        }

        const uint64_t need = static_cast<uint64_t>(width) * height * bytesPerPx;
        if (need > pixSrcLen) return false;

        bgra.assign(static_cast<size_t>(width) * height * 4u, 0u);
        const size_t nPx = static_cast<size_t>(width) * height;
        for (size_t i = 0u; i < nPx; ++i) {
            const uint8_t* p = pixSrc + i * bytesPerPx;
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            switch (flimFmt) {
            case 0x00u: r = g = b = p[0]; break;
            case 0x01u: r = g = b = 255u; a = p[0]; break;
            case 0x02u: a = static_cast<uint8_t>((p[0] & 0x0Fu) * 17u); r = g = b = static_cast<uint8_t>((p[0] >> 4u) * 17u); break;
            case 0x03u: r = g = b = p[0]; a = p[1]; break;
            case 0x05u: GpuTex::UnpackRgb565(ByteIO::RdU16(p, be), r, g, b); break;
            case 0x06u: r = p[0]; g = p[1]; b = p[2]; break;
            case 0x07u: {
                const uint16_t v = ByteIO::RdU16(p, be);
                r = static_cast<uint8_t>(((v >> 11u) & 0x1Fu) * 255u / 31u);
                g = static_cast<uint8_t>(((v >>  6u) & 0x1Fu) * 255u / 31u);
                b = static_cast<uint8_t>(((v >>  1u) & 0x1Fu) * 255u / 31u);
                a = (v & 1u) ? 255u : 0u;
                break;
            }
            case 0x08u: {
                const uint16_t v = ByteIO::RdU16(p, be);
                r = static_cast<uint8_t>(((v >> 12u) & 0x0Fu) * 17u);
                g = static_cast<uint8_t>(((v >>  8u) & 0x0Fu) * 17u);
                b = static_cast<uint8_t>(((v >>  4u) & 0x0Fu) * 17u);
                a = static_cast<uint8_t>((v & 0x0Fu) * 17u);
                break;
            }
            default:
                r = p[0]; g = p[1]; b = p[2]; a = p[3];
                break;
            }
            WritePremulBGRA(&bgra[i * 4u], r, g, b, a);
        }
    }

    std::wstring title = std::wstring(L"Nintendo Wii U BFLIM (") + fmtName + L")";
    return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), std::move(title), maxDimension);
}

} // namespace Formats
