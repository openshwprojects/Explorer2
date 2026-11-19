#pragma once
// AdobeColorAct.h — Adobe Color Table Swatch Renderer (.act)

namespace Formats {

inline bool DecodeAdobeColorAct(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 768) return false;
    std::vector<DetailPalette::Rgb> colors;
        for (size_t i = 0; i < 256; ++i) {
            colors.push_back({ data[i * 3 + 0], data[i * 3 + 1], data[i * 3 + 2] });
        }
    return DetailPalette::RenderPaletteSwatchGrid(colors, outInfo);
}

} // namespace Formats
