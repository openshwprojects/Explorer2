#pragma once
// AdobeSwatchAco.h — Adobe Color Swatch (.aco) & Adobe Swatch Exchange ('ASEF') Renderer

namespace Formats {

inline bool DecodeAdobeSwatchAco(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 4) return false;
    std::vector<DetailPalette::Rgb> colors;
    if (size >= 12 && std::memcmp(data, "ASEF", 4) == 0) {
        // Adobe Swatch Exchange (.ase "ASEF")
        uint32_t numBlocks = (static_cast<uint32_t>(data[8]) << 24) |
                             (static_cast<uint32_t>(data[9]) << 16) |
                             (static_cast<uint32_t>(data[10]) << 8) | data[11];
        size_t pos = 12;
        for (uint32_t b = 0; b < numBlocks && pos + 6 <= size && colors.size() < 256; ++b) {
            uint16_t btype = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
            uint32_t blen  = (static_cast<uint32_t>(data[pos + 2]) << 24) |
                             (static_cast<uint32_t>(data[pos + 3]) << 16) |
                             (static_cast<uint32_t>(data[pos + 4]) << 8) | data[pos + 5];
            pos += 6;
            if (pos + blen > size) break;
            if (btype == 0x0001 && blen >= 18) {
                uint16_t nameLen = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
                size_t cpos = pos + 2 + static_cast<size_t>(nameLen) * 2;
                if (cpos + 16 <= pos + blen && std::memcmp(data + cpos, "RGB ", 4) == 0) {
                    auto rdF32BE = [&](size_t off) -> float {
                        uint32_t u = (static_cast<uint32_t>(data[off]) << 24) |
                                     (static_cast<uint32_t>(data[off + 1]) << 16) |
                                     (static_cast<uint32_t>(data[off + 2]) << 8) | data[off + 3];
                        float f = 0.0f; std::memcpy(&f, &u, 4); return f;
                    };
                    float rf = std::clamp(rdF32BE(cpos + 4), 0.0f, 1.0f);
                    float gf = std::clamp(rdF32BE(cpos + 8), 0.0f, 1.0f);
                    float bf = std::clamp(rdF32BE(cpos + 12), 0.0f, 1.0f);
                    colors.push_back({
                        static_cast<uint8_t>(rf * 255.0f),
                        static_cast<uint8_t>(gf * 255.0f),
                        static_cast<uint8_t>(bf * 255.0f)
                    });
                }
            }
            pos += blen;
        }
    } else {
        // Adobe Color Swatch (.aco v1/v2 big-endian)
        uint16_t ver = static_cast<uint16_t>((data[0] << 8) | data[1]);
        uint16_t cnt = static_cast<uint16_t>((data[2] << 8) | data[3]);
        if ((ver == 1 || ver == 2) && cnt > 0) {
            size_t pos = 4;
            for (uint16_t i = 0; i < cnt && pos + 10 <= size && colors.size() < 256; ++i) {
                uint16_t cspace = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
                uint8_t w0 = data[pos + 2], w1 = data[pos + 4], w2 = data[pos + 6];
                if (cspace == 0) { // RGB
                    colors.push_back({ w0, w1, w2 });
                } else if (cspace == 2) { // CMYK
                    uint8_t k = data[pos + 8];
                    colors.push_back({
                        static_cast<uint8_t>((w0 * k) / 255),
                        static_cast<uint8_t>((w1 * k) / 255),
                        static_cast<uint8_t>((w2 * k) / 255)
                    });
                } else {
                    colors.push_back({ w0, w0, w0 });
                }
                pos += 10;
                if (ver == 2 && pos + 4 <= size) {
                    uint32_t nlen = (static_cast<uint32_t>(data[pos]) << 24) |
                                    (static_cast<uint32_t>(data[pos + 1]) << 16) |
                                    (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
                    pos += 4 + static_cast<size_t>(nlen) * 2;
                }
            }
        }
    }
    return DetailPalette::RenderPaletteSwatchGrid(colors, outInfo);
}

} // namespace Formats
