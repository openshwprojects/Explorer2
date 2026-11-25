#pragma once
// AdobeColorAct.h — Adobe Color Table Swatch Renderer (.act, v1 768B & v2 772B with BE16 count)

namespace Formats {

inline bool DecodeAdobeColorAct(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 768) return false;
    size_t count = 256;
    if (size >= 772) {
        uint16_t explicitCount = ::Explorer2::ByteIO::RdU16BE(data + 768);
        if (explicitCount > 0 && explicitCount <= 256) {
            count = explicitCount;
        }
    }
    std::vector<DetailPalette::Rgb> colors;
    colors.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        colors.push_back({ data[i * 3 + 0], data[i * 3 + 1], data[i * 3 + 2] });
    }
    return DetailPalette::RenderPaletteSwatchGrid(colors, outInfo);
}

} // namespace Formats
