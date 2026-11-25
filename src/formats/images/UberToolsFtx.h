#pragma once
// UberToolsFtx.h — Ritual Entertainment UberTools Texture (.ftx — FAKK2 / American McGee's Alice)
// Exact 12-byte ftx_t header (int32 width, height, has_alpha in {0,1}) + width*height*4 RGBA8888 texels.

namespace Formats {

inline bool DecodeUberToolsFtx(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 16u) return false;

    const int32_t width    = ByteIO::RdI32LE(data + 0x00);
    const int32_t height   = ByteIO::RdI32LE(data + 0x04);
    const int32_t hasAlpha = ByteIO::RdI32LE(data + 0x08);

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    if (hasAlpha != 0 && hasAlpha != 1) return false;

    const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    if (12ULL + pixelCount * 4ULL != static_cast<uint64_t>(size)) {
        return false;
    }

    outInfo = {};
    outInfo.width = static_cast<uint32_t>(width);
    outInfo.height = static_cast<uint32_t>(height);
    outInfo.origWidth = outInfo.width;
    outInfo.origHeight = outInfo.height;
    outInfo.channels = (hasAlpha != 0) ? 4u : 3u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels.resize(static_cast<size_t>(pixelCount) * 4u);

    const uint8_t* src = data + 12u;
    for (size_t i = 0; i < static_cast<size_t>(pixelCount); ++i) {
        const uint8_t r = src[i * 4u + 0u];
        const uint8_t g = src[i * 4u + 1u];
        const uint8_t b = src[i * 4u + 2u];
        const uint8_t a = (hasAlpha != 0) ? src[i * 4u + 3u] : 255u;
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }

    outInfo.formatName = (hasAlpha != 0)
        ? L"Ritual UberTools FTX (RGBA32)"
        : L"Ritual UberTools FTX (RGBX32)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
