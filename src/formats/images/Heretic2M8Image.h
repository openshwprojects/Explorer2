#pragma once
// Heretic2M8Image.h — Raven Software Heretic II 8-bit Paletted MipTexture (.m8, version == 2)
// Reuses shared QuakeMipTexCommon.h & FormatPrologueCommon.h.

namespace Formats {

inline bool DecodeHeretic2M8(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    constexpr size_t kM8HeaderBytes = 1040u;
    if (!data || size <= kM8HeaderBytes || ByteIO::RdI32LE(data) != 2) return false;

    uint32_t w0 = 0u, h0 = 0u, off0 = 0u;
    if (!QuakeMipCommon::Validate16LevelMipChain(
            data, size, kM8HeaderBytes, 0x024u, 0x064u, 0x0A4u, 1u, w0, h0, off0)) {
        return false;
    }

    std::string texName;
    if (!FormatPrologue::ReadPrintableFixedAscii(data + 4u, 32u, texName)) return false;

    const uint8_t* palRgb = data + 260u;
    const bool hasMagentaKey = (palRgb[255u * 3u + 0u] == 255u &&
                                palRgb[255u * 3u + 1u] == 0u &&
                                palRgb[255u * 3u + 2u] == 255u);

    const size_t pxCount = static_cast<size_t>(w0) * h0;
    outInfo = {};
    outInfo.width = w0;
    outInfo.height = h0;
    outInfo.origWidth = w0;
    outInfo.origHeight = h0;
    outInfo.channels = hasMagentaKey ? 4u : 3u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels.resize(pxCount * 4u);

    const uint8_t* indices = data + off0;
    for (size_t i = 0u; i < pxCount; ++i) {
        const uint8_t idx = indices[i];
        const uint8_t r = palRgb[static_cast<size_t>(idx) * 3u + 0u];
        const uint8_t g = palRgb[static_cast<size_t>(idx) * 3u + 1u];
        const uint8_t b = palRgb[static_cast<size_t>(idx) * 3u + 2u];
        const uint8_t a = (hasMagentaKey && idx == 255u) ? 0u : 255u;
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
    }

    outInfo.formatName = L"Raven Heretic II M8 (8-bit Paletted)";
    if (!texName.empty()) outInfo.formatName += L" — " + PathUtils::Utf8ToWide(texName);
    outInfo.valid = true;
    return true;
}

} // namespace Formats
