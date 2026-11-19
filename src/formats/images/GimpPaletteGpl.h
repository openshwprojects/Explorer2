#pragma once
// GimpPaletteGpl.h — GIMP Color Palette Swatch Renderer (.gpl)

namespace Formats {

inline bool DecodeGimpPaletteGpl(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;
    std::vector<DetailPalette::Rgb> colors;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    DetailPalette::ParseTextPaletteLines(sv, colors);
    return DetailPalette::RenderPaletteSwatchGrid(colors, outInfo);
}

} // namespace Formats
