#pragma once
// LithTechDtxImage.h — Monolith LithTech Engine Texture (.dtx — DTX_VERSION_LT1..LT2: -2 / -3 / -5)
// Used across Shogo, Blood II, No One Lives Forever 1 & 2, Aliens vs. Predator 2, and TRON 2.0.
// Supports BPP_8P (paletted), BPP_16 (ARGB4444/RGB565), BPP_32 (BGRA8888), and S3TC DXT1/DXT3/DXT5.

namespace Formats {

inline bool DecodeLithTechDtx(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 44u || size > (64u << 20)) return false;

    const uint32_t resType  = ByteIO::RdU32LE(data + 0x00u);
    const int32_t  version  = ByteIO::RdI32LE(data + 0x04u);
    const uint16_t width    = ByteIO::RdU16LE(data + 0x08u);
    const uint16_t height   = ByteIO::RdU16LE(data + 0x0Au);
    const uint16_t mipCount = ByteIO::RdU16LE(data + 0x0Cu);
    const uint16_t nSecs    = ByteIO::RdU16LE(data + 0x0Eu);
    const uint32_t flags    = ByteIO::RdU32LE(data + 0x10u);

    if (resType != 0u) return false;
    if (version != -2 && version != -3 && version != -5) return false;
    if (width == 0u || height == 0u || width > 4096u || height > 4096u) return false;
    if (mipCount == 0u || mipCount > 16u || nSecs > 32u) return false;

    const size_t hdrSize = (version == -5) ? 164u : 36u;
    if (size <= hdrSize) return false;

    // In LT2 (-5), extra[2] at +0x1E holds BPPIdent (0=8P, 1=8, 2=16, 3=32, 4=DXT1, 5=DXT3, 6=DXT5);
    // if extra[2] == 0 and DTX_FULLBRITE / 32-bit flag or LT1 (-2), inspect size / flags.
    uint8_t bppIdent = (version == -5) ? data[0x1Eu] : 0u;
    if (version != -5) {
        // LT1 (-2) stores 8-bit paletted (`1024`B palette at `36` + mip0..mip3) unless 32-bit
        bppIdent = (size >= hdrSize + 1024u + static_cast<size_t>(width) * height) ? 0u : 3u;
    } else if (bppIdent > 6u) {
        return false;
    }

    const size_t pixAvail = size - hdrSize;
    const uint8_t* pixPtr = data + hdrSize;
    const size_t nPixels  = static_cast<size_t>(width) * height;
    std::vector<uint8_t> bgra;
    const wchar_t* fmtLbl = L"BPP_32";

    if (bppIdent == 4u || bppIdent == 5u || bppIdent == 6u) {
        const int bcKind = static_cast<int>(bppIdent - 3u); // 1=DXT1, 2=DXT3, 3=DXT5
        fmtLbl = (bcKind == 1) ? L"DXT1" : ((bcKind == 2) ? L"DXT3" : L"DXT5");
        if (!ConsolePx::DecodeLinearS3tcToPbgra(pixPtr, pixAvail, width, height, bcKind, bgra)) return false;
    } else if (bppIdent == 3u) {
        if (pixAvail < nPixels * 4u) return false;
        bgra.assign(nPixels * 4u, 0u);
        // DTX_NOSYSCACHE / standard LT2 BPP_32 stores B, G, R, A
        for (size_t i = 0u; i < nPixels; ++i) {
            const uint8_t b = pixPtr[i * 4u + 0u];
            const uint8_t g = pixPtr[i * 4u + 1u];
            const uint8_t r = pixPtr[i * 4u + 2u];
            const uint8_t a = (pixPtr[i * 4u + 3u] == 0u && (flags & 0x0001u) == 0u) ? 255u : pixPtr[i * 4u + 3u];
            WritePremulBGRA(&bgra[i * 4u], r, g, b, a);
        }
    } else if (bppIdent == 2u) {
        fmtLbl = L"BPP_16";
        if (pixAvail < nPixels * 2u) return false;
        bgra.assign(nPixels * 4u, 0u);
        for (size_t i = 0u; i < nPixels; ++i) {
            uint8_t r = 0, g = 0, b = 0;
            GpuTex::UnpackRgb565(ByteIO::RdU16LE(pixPtr + i * 2u), r, g, b);
            WritePremulBGRA(&bgra[i * 4u], r, g, b, 255u);
        }
    } else if (bppIdent == 0u) {
        fmtLbl = L"BPP_8P";
        // LT1 (-2): 8B palHeader + 256*4 ARGB palette (`1032` or `1024` bytes) followed by mip0 indices
        if (pixAvail < 1024u + nPixels) return false;
        const size_t palHdrSkip = (pixAvail >= 1032u + nPixels && version == -2) ? 8u : 0u;
        const uint8_t* pal = pixPtr + palHdrSkip;
        const uint8_t* idx = pal + 1024u;
        bgra.assign(nPixels * 4u, 0u);
        for (size_t i = 0u; i < nPixels; ++i) {
            const uint8_t cIdx = idx[i];
            const uint8_t a = pal[cIdx * 4u + 0u] ? pal[cIdx * 4u + 0u] : 255u;
            const uint8_t r = pal[cIdx * 4u + 1u];
            const uint8_t g = pal[cIdx * 4u + 2u];
            const uint8_t b = pal[cIdx * 4u + 3u];
            WritePremulBGRA(&bgra[i * 4u], r, g, b, a);
        }
    } else {
        return false;
    }

    wchar_t title[128] = {};
    swprintf_s(title, L"LithTech Engine Texture (.dtx v%d %ls)", version, fmtLbl);
    return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), title, maxDimension);
}

} // namespace Formats
