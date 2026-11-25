#pragma once
// TrueTypeFontTtf.h — TrueType Font (.ttf) & TrueType Collection (.ttc) Specimen Decoder

namespace Formats {

inline bool DecodeTrueTypeFontTtf(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 28u) return false;
    if (std::memcmp(data, "wOFF", 4) == 0 || std::memcmp(data, "wOF2", 4) == 0) return false;
    (void)extLower;
    return FontOutline::RenderSfntFontSpecimen(data, size, L"", outInfo);
}

} // namespace Formats
