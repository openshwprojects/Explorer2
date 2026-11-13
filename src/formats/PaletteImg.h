#pragma once
// PaletteImg.h — Color Palette Swatch Renderer (.gpl GIMP Palette, .pal JASC-PAL, .act Adobe Color Table)

namespace Formats {

inline bool DecodePaletteSwatch(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    struct Rgb { uint8_t r, g, b; };
    std::vector<Rgb> colors;

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.rfind("GIMP Palette", 0) == 0 || sv.rfind("JASC-PAL", 0) == 0 || extLower == L".gpl" || extLower == L".pal") {
        size_t pos = 0;
        while (pos < sv.size() && colors.size() < 256) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string_view ln = sv.substr(pos, eol - pos);
            pos = eol + 1;
            while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
            if (ln.empty() || ln[0] == '#' || ln.rfind("GIMP", 0) == 0 || ln.rfind("Name:", 0) == 0 ||
                ln.rfind("Columns:", 0) == 0 || ln.rfind("JASC", 0) == 0 || ln == "0100\r" || ln == "0100") {
                continue;
            }
            int r = -1, g = -1, b = -1;
            if (sscanf_s(std::string(ln).c_str(), "%d %d %d", &r, &g, &b) == 3) {
                colors.push_back({
                    static_cast<uint8_t>(std::clamp(r, 0, 255)),
                    static_cast<uint8_t>(std::clamp(g, 0, 255)),
                    static_cast<uint8_t>(std::clamp(b, 0, 255))
                });
            }
        }
    } else if (extLower == L".act" && size >= 768) {
        for (size_t i = 0; i < 256; ++i) {
            colors.push_back({ data[i * 3 + 0], data[i * 3 + 1], data[i * 3 + 2] });
        }
    } else if (extLower == L".aco" && size >= 4) {
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
    } else if (size >= 12 && std::memcmp(data, "ASEF", 4) == 0) {
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
    }
    if (colors.empty()) return false;

    const uint32_t cols = 8, cell = 16;
    uint32_t rows = std::max(1u, static_cast<uint32_t>((colors.size() + cols - 1) / cols));
    uint32_t w = cols * cell, h = rows * cell;
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Color Palette Swatch (" + std::to_wstring(colors.size()) + L" colors)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    for (size_t i = 0; i < colors.size(); ++i) {
        uint32_t cx = static_cast<uint32_t>(i % cols) * cell;
        uint32_t cy = static_cast<uint32_t>(i / cols) * cell;
        for (uint32_t py = 1; py + 1 < cell; ++py) {
            for (uint32_t px = 1; px + 1 < cell; ++px) {
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(cy + py) * w + (cx + px)) * 4];
                dst[0] = colors[i].b; dst[1] = colors[i].g; dst[2] = colors[i].r; dst[3] = 255;
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
