#pragma once
// KonamiCtxrImage.h — Konami Metal Gear Solid 2/3 HD & Master Collection Texture (.ctxr — "TXRC")
// Validates 132-byte (`0x84`) big-endian `TXRC` v7 header + Level-0 byte length (`width * height * 4`),
// decoding 32-bit ARGB/BGRA pixels into premultiplied BGRA8.

namespace Formats {

inline bool DecodeKonamiCtxr(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 0x88u || size > (128u << 20)) return false;
    if (std::memcmp(data, "TXRC", 4) != 0) return false;

    const uint32_t version  = ByteIO::RdU32BE(data + 0x04u);
    const uint16_t width    = ByteIO::RdU16BE(data + 0x08u);
    const uint16_t height   = ByteIO::RdU16BE(data + 0x0Au);
    const uint16_t depth    = ByteIO::RdU16BE(data + 0x0Cu);
    const uint8_t  mipCount = data[0x26u];
    const uint32_t mip0Len  = ByteIO::RdU32BE(data + 0x80u);

    if (version != 7u && version != 8u) return false;
    if (width == 0u || height == 0u || width > 8192u || height > 8192u || depth == 0u || depth > 64u) return false;
    if (mipCount == 0u || mipCount > 16u) return false;

    const uint64_t expectedBytes = static_cast<uint64_t>(width) * height * 4ULL;
    if (mip0Len < expectedBytes || expectedBytes > size - 0x84u) return false;

    const size_t nPx = static_cast<size_t>(width) * height;
    const uint8_t* src = data + 0x84u;
    std::vector<uint8_t> bgra(nPx * 4u, 0u);

    // CTXR stores big-endian 32-bit ARGB `[A, R, G, B]` pixels at 0x84
    for (size_t i = 0u; i < nPx; ++i) {
        const uint8_t a = src[i * 4u + 0u];
        const uint8_t r = src[i * 4u + 1u];
        const uint8_t g = src[i * 4u + 2u];
        const uint8_t b = src[i * 4u + 3u];
        WritePremulBGRA(&bgra[i * 4u], r, g, b, a);
    }

    return ConsolePx::FinalizeConsoleImageInfo(
        outInfo, width, height, std::move(bgra),
        L"Konami MGS HD Texture (.ctxr TXRC)", maxDimension);
}

} // namespace Formats
