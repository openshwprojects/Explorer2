#pragma once
// OpenTypeFontOtf.h — OpenType CFF / CFF2 / COLR / CBDT / sbix Font (.otf) Specimen Decoder

namespace Formats {

inline bool DecodeOpenTypeFontOtf(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 28u) return false;
    if (std::memcmp(data, "wOFF", 4) == 0 || std::memcmp(data, "wOF2", 4) == 0) return false;
    (void)extLower;
    std::wstring_view kindOverride = (std::memcmp(data, "OTTO", 4) == 0) ? L"OpenType CFF" : L"";
    return FontOutline::RenderSfntFontSpecimen(data, size, kindOverride, outInfo);
}

} // namespace Formats
