#pragma once
// RitualSinSwlImage.h — Ritual Entertainment SiN Paletted MipTexture (.swl)
// Validates 64B texture name, width/height (multiples of 8), 256-entry RGBA palette (`0x88..0x488`),
// and 4-level mipmap offsets (`0x4BC..0x4CC`), decoding Level 0 to premultiplied BGRA8.

namespace Formats {

inline bool DecodeRitualSinSwl(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 0x4CCu || size > (32u << 20)) return false;

    std::string texName;
    if (!FormatPrologue::ReadPrintableFixedAscii(data + 0x00u, 64u, texName, true)) return false;

    const uint32_t width  = ByteIO::RdU32LE(data + 0x80u);
    const uint32_t height = ByteIO::RdU32LE(data + 0x84u);
    if (width < 8u || height < 8u || width > 4096u || height > 4096u) return false;
    if ((width & 7u) != 0u || (height & 7u) != 0u) return false;

    const uint8_t* palette = data + 0x88u; // 256 * 4 RGBA bytes (0x88 .. 0x488)

    // Standard SiN `sinmiptex_t` header is 0x4CC (1228) bytes with `offsets[4]` at 0x4BC..0x4CC
    uint32_t mipOffsets[4] = {
        ByteIO::RdU32LE(data + 0x4BCu),
        ByteIO::RdU32LE(data + 0x4C0u),
        ByteIO::RdU32LE(data + 0x4C4u),
        ByteIO::RdU32LE(data + 0x4C8u)
    };

    for (uint32_t level = 0u; level < 4u; ++level) {
        const uint64_t lw = width  >> level;
        const uint64_t lh = height >> level;
        const uint64_t lBytes = lw * lh;
        if (mipOffsets[level] < 0x4CCu || mipOffsets[level] > size || lBytes > size - mipOffsets[level]) {
            return false;
        }
        if (level > 0u && mipOffsets[level] < mipOffsets[level - 1u]) return false;
    }

    const size_t nPx = static_cast<size_t>(width) * height;
    const uint8_t* idxPtr = data + mipOffsets[0];
    std::vector<uint8_t> bgra(nPx * 4u, 0u);

    for (size_t i = 0u; i < nPx; ++i) {
        const uint8_t idx = idxPtr[i];
        const uint8_t r = palette[idx * 4u + 0u];
        const uint8_t g = palette[idx * 4u + 1u];
        const uint8_t b = palette[idx * 4u + 2u];
        uint8_t a = palette[idx * 4u + 3u];
        // SiN uses index 255 as transparent color-key when palette alpha is all 0 or 255
        if (idx == 255u && r == 255u && g == 0u && b == 255u) a = 0u;
        else if (a == 0u && idx != 255u) a = 255u;
        WritePremulBGRA(&bgra[i * 4u], r, g, b, a);
    }

    std::wstring title = L"Ritual SiN MipTexture (.swl)";
    if (!texName.empty()) title += L" — " + PathUtils::Utf8ToWide(texName);
    return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), std::move(title), maxDimension);
}

} // namespace Formats
