#pragma once
// Bdf.h — Adobe Glyph Bitmap Distribution Font (.bdf) Atlas Renderer

namespace Formats {

inline bool DecodeBDF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 4 * 1024 * 1024));
    if (sv.find("STARTFONT") == std::string_view::npos || sv.find("BITMAP") == std::string_view::npos) return false;

    const uint32_t cols = 16, cellW = 16, cellH = 16;
    uint32_t imgW = cols * cellW, imgH = 8 * cellH;
    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Adobe BDF Bitmap Font (.bdf)";
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4, 248);
    for (size_t i = 0; i < static_cast<size_t>(imgW) * imgH; ++i) outInfo.bgraPixels[i * 4 + 3] = 255;

    size_t pos = 0;
    uint32_t glyphIdx = 0;
    while ((pos = sv.find("BITMAP", pos)) != std::string_view::npos && glyphIdx < 128) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) break;
        size_t endChar = sv.find("ENDCHAR", eol);
        if (endChar == std::string_view::npos) break;

        uint32_t gx = (glyphIdx % cols) * cellW + 4;
        uint32_t gy = (glyphIdx / cols) * cellH + 2;
        size_t lPos = eol + 1;
        uint32_t row = 0;
        while (lPos < endChar && row < 12) {
            size_t lEnd = sv.find('\n', lPos);
            if (lEnd == std::string_view::npos || lEnd > endChar) lEnd = endChar;
            std::string_view hexLine = sv.substr(lPos, lEnd - lPos);
            while (!hexLine.empty() && (hexLine.back() == '\r' || hexLine.back() == ' ')) hexLine.remove_suffix(1);
            if (!hexLine.empty()) {
                uint32_t bits = static_cast<uint32_t>(std::strtoul(std::string(hexLine).c_str(), nullptr, 16));
                int shiftTop = static_cast<int>(hexLine.size() * 4) - 1;
                for (uint32_t b = 0; b < 8 && shiftTop >= 0; ++b, --shiftTop) {
                    if ((bits >> shiftTop) & 1) {
                        uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(gy + row) * imgW + (gx + b)) * 4];
                        dst[0] = 45; dst[1] = 25; dst[2] = 15; dst[3] = 255;
                    }
                }
                ++row;
            }
            lPos = lEnd + 1;
        }
        ++glyphIdx;
        pos = endChar + 7;
    }
    outInfo.valid = (glyphIdx > 0);
    return outInfo.valid;
}

} // namespace Formats
