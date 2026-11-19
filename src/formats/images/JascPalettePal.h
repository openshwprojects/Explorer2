#pragma once
// JascPalettePal.h — JASC-PAL Color Palette Swatch Renderer (.pal)

namespace Formats {

inline bool DecodeJascPalettePal(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;
    std::vector<DetailPalette::Rgb> colors;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    DetailPalette::ParseTextPaletteLines(sv, colors);
    return DetailPalette::RenderPaletteSwatchGrid(colors, outInfo);
}

} // namespace Formats
