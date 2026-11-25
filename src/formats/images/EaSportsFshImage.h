#pragma once
// EaSportsFshImage.h — Electronic Arts "SHPI" FSH Shape Texture (.fsh — Need for Speed / SimCity 4 / FIFA)
// Parses `"SHPI"` directory & chained sub-records, supporting BGRA32 (`0x7D`), BGR24 (`0x7F`),
// ARGB1555 (`0x7E`), RGB565 (`0x78`), ARGB4444 (`0x6D`), 8-bit Paletted (`0x7B`), and DXT1/3/5 (`0x60`–`0x62`).

namespace Formats {

inline bool DecodeEaSportsFsh(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 40u || size > (64u << 20)) return false;
    if (std::memcmp(data, "SHPI", 4) != 0) return false;

    const uint32_t totalSize  = ByteIO::RdU32LE(data + 4u);
    const uint32_t numEntries = ByteIO::RdU32LE(data + 8u);
    if (totalSize < 32u || totalSize > size || numEntries == 0u || numEntries > 2048u) return false;
    const uint64_t dirEnd = 16ULL + static_cast<uint64_t>(numEntries) * 8ULL;
    if (dirEnd > totalSize) return false;

    // Scan directory for first valid bitmap entry (and optional global palette entry `"!pal"` / `0x22..0x2D`)
    std::array<std::array<uint8_t, 4>, 256> globalPal{};
    bool hasGlobalPal = false;

    auto decodePaletteRecord = [&](size_t palRecOff, std::array<std::array<uint8_t, 4>, 256>& dstPal) -> bool {
        if (palRecOff >= totalSize || totalSize - palRecOff < 16u) return false;
        const uint8_t pCode = data[palRecOff];
        const uint16_t nCol = ByteIO::RdU16LE(data + palRecOff + 4u);
        if (nCol == 0u || nCol > 256u) return false;
        const uint8_t* pp = data + palRecOff + 16u;
        const size_t rem = totalSize - (palRecOff + 16u);

        for (auto& c : dstPal) c = { 0, 0, 0, 255 };
        if (pCode == 0x2Au || pCode == 0x29u) { // 32-bit BGRA (`0x2A`) or RGBA
            if (rem < static_cast<size_t>(nCol) * 4u) return false;
            for (uint16_t i = 0u; i < nCol; ++i) {
                dstPal[i] = { pp[i * 4u + 2u], pp[i * 4u + 1u], pp[i * 4u + 0u], pp[i * 4u + 3u] };
            }
            return true;
        }
        if (pCode == 0x24u || pCode == 0x22u) { // 24-bit RGB (`0x24` 8-bit, `0x22` 6-bit VGA)
            if (rem < static_cast<size_t>(nCol) * 3u) return false;
            const uint32_t mul = (pCode == 0x22u) ? 4u : 1u;
            for (uint16_t i = 0u; i < nCol; ++i) {
                dstPal[i] = {
                    static_cast<uint8_t>(std::min(255u, pp[i * 3u + 0u] * mul)),
                    static_cast<uint8_t>(std::min(255u, pp[i * 3u + 1u] * mul)),
                    static_cast<uint8_t>(std::min(255u, pp[i * 3u + 2u] * mul)),
                    255u
                };
            }
            return true;
        }
        if (pCode == 0x2Du) { // 16-bit ARGB1555 palette
            if (rem < static_cast<size_t>(nCol) * 2u) return false;
            for (uint16_t i = 0u; i < nCol; ++i) {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                ConsolePx::UnpackArgb1555(ByteIO::RdU16LE(pp + i * 2u), r, g, b, a);
                dstPal[i] = { r, g, b, a };
            }
            return true;
        }
        return false;
    };

    for (uint32_t i = 0u; i < numEntries; ++i) {
        const uint8_t* de = data + 16u + static_cast<size_t>(i) * 8u;
        const uint32_t off = ByteIO::RdU32LE(de + 4u);
        if (off >= 16u && off < totalSize && (data[off] >= 0x22u && data[off] <= 0x2Du)) {
            hasGlobalPal = decodePaletteRecord(off, globalPal);
        }
    }

    for (uint32_t i = 0u; i < numEntries; ++i) {
        const uint8_t* de = data + 16u + static_cast<size_t>(i) * 8u;
        const uint32_t recOff = ByteIO::RdU32LE(de + 4u);
        if (recOff < 16u || recOff >= totalSize || totalSize - recOff < 16u) return false;

        const uint8_t  code   = data[recOff + 0u] & 0x7Fu;
        const uint32_t nextRel = ByteIO::RdU24LE(data + recOff + 1u);
        const uint16_t width  = ByteIO::RdU16LE(data + recOff + 4u);
        const uint16_t height = ByteIO::RdU16LE(data + recOff + 6u);
        if (code >= 0x22u && code <= 0x2Du) continue; // Palette-only directory entry

        if (width == 0u || height == 0u || width > 4096u || height > 4096u) return false;
        const uint8_t* pixPtr = data + recOff + 16u;
        const size_t   pixAvail = totalSize - (recOff + 16u);
        const size_t   nPx = static_cast<size_t>(width) * height;

        std::vector<uint8_t> bgra;
        const wchar_t* fmtName = L"BGRA32";

        if (code == 0x60u || code == 0x61u || code == 0x62u) {
            const int bc = (code == 0x60u) ? 1 : ((code == 0x61u) ? 2 : 3);
            fmtName = (bc == 1) ? L"DXT1" : ((bc == 2) ? L"DXT3" : L"DXT5");
            if (!ConsolePx::DecodeLinearS3tcToPbgra(pixPtr, pixAvail, width, height, bc, bgra)) return false;
        } else if (code == 0x7Du) {
            if (pixAvail < nPx * 4u) return false;
            bgra.assign(nPx * 4u, 0u);
            for (size_t k = 0u; k < nPx; ++k) {
                WritePremulBGRA(&bgra[k * 4u], pixPtr[k * 4u + 2u], pixPtr[k * 4u + 1u], pixPtr[k * 4u + 0u], pixPtr[k * 4u + 3u]);
            }
        } else if (code == 0x7Fu) {
            fmtName = L"BGR24";
            if (pixAvail < nPx * 3u) return false;
            bgra.assign(nPx * 4u, 0u);
            for (size_t k = 0u; k < nPx; ++k) {
                WritePremulBGRA(&bgra[k * 4u], pixPtr[k * 3u + 2u], pixPtr[k * 3u + 1u], pixPtr[k * 3u + 0u], 255u);
            }
        } else if (code == 0x7Eu) {
            fmtName = L"ARGB1555";
            if (pixAvail < nPx * 2u) return false;
            bgra.assign(nPx * 4u, 0u);
            for (size_t k = 0u; k < nPx; ++k) {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                ConsolePx::UnpackArgb1555(ByteIO::RdU16LE(pixPtr + k * 2u), r, g, b, a);
                WritePremulBGRA(&bgra[k * 4u], r, g, b, a);
            }
        } else if (code == 0x78u) {
            fmtName = L"RGB565";
            if (pixAvail < nPx * 2u) return false;
            bgra.assign(nPx * 4u, 0u);
            for (size_t k = 0u; k < nPx; ++k) {
                uint8_t r = 0, g = 0, b = 0;
                GpuTex::UnpackRgb565(ByteIO::RdU16LE(pixPtr + k * 2u), r, g, b);
                WritePremulBGRA(&bgra[k * 4u], r, g, b, 255u);
            }
        } else if (code == 0x6Du) {
            fmtName = L"ARGB4444";
            if (pixAvail < nPx * 2u) return false;
            bgra.assign(nPx * 4u, 0u);
            for (size_t k = 0u; k < nPx; ++k) {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                ConsolePx::UnpackArgb4444(ByteIO::RdU16LE(pixPtr + k * 2u), r, g, b, a);
                WritePremulBGRA(&bgra[k * 4u], r, g, b, a);
            }
        } else if (code == 0x7Bu) {
            fmtName = L"Paletted8";
            if (pixAvail < nPx) return false;
            std::array<std::array<uint8_t, 4>, 256> localPal = globalPal;
            bool gotPal = hasGlobalPal;
            if (nextRel >= 16u + nPx && recOff + nextRel < totalSize) {
                gotPal = decodePaletteRecord(recOff + nextRel, localPal) || gotPal;
            }
            if (!gotPal) return false;
            bgra.assign(nPx * 4u, 0u);
            for (size_t k = 0u; k < nPx; ++k) {
                const auto& c = localPal[pixPtr[k]];
                WritePremulBGRA(&bgra[k * 4u], c[0], c[1], c[2], c[3]);
            }
        } else {
            return false;
        }

        std::wstring title = std::wstring(L"EA Sports SHPI/FSH Texture (") + fmtName + L")";
        return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), std::move(title), maxDimension);
    }

    return false;
}

} // namespace Formats
