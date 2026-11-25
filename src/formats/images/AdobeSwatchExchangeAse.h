#pragma once
// AdobeSwatchExchangeAse.h — Adobe Swatch Exchange ("ASEF" .ase) Palette Decoder & Renderer
// Supports group blocks (0xC001 / 0xC002), UTF-16BE swatch names, and RGB / CMYK / LAB / Gray color models.

namespace Formats {

inline bool DecodeAdobeSwatchExchangeAse(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 12 || std::memcmp(data, "ASEF", 4) != 0) return false;

    uint16_t verMaj = ::Explorer2::ByteIO::RdU16BE(data + 4);
    uint16_t verMin = ::Explorer2::ByteIO::RdU16BE(data + 6);
    if (verMaj == 0 || verMaj > 16) return false;
    (void)verMin;

    uint32_t numBlocks = ::Explorer2::ByteIO::RdU32BE(data + 8);
    if (numBlocks == 0 || numBlocks > 65536u) return false;

    std::vector<DetailPalette::Rgb> colors;
    std::vector<std::wstring> swatchNames;

    size_t pos = 12;
    for (uint32_t b = 0; b < numBlocks && pos + 6 <= size && colors.size() < 256; ++b) {
        uint16_t btype = ::Explorer2::ByteIO::RdU16BE(data + pos);
        uint32_t blen  = ::Explorer2::ByteIO::RdU32BE(data + pos + 2);
        pos += 6;
        if (blen > size - pos) break;

        if (btype == 0x0001 && blen >= 6) { // Color Entry block
            uint16_t nameLen = ::Explorer2::ByteIO::RdU16BE(data + pos);
            size_t nameBytes = static_cast<size_t>(nameLen) * 2u;
            if (2u + nameBytes + 4u <= blen) {
                if (nameLen > 0 && swatchNames.size() < 64) {
                    std::wstring nm = DetailPalette::ReadUtf16BeName(data + pos + 2u, nameLen);
                    if (!nm.empty()) swatchNames.push_back(std::move(nm));
                }
                const uint8_t* cptr = data + pos + 2u + nameBytes;
                size_t rem = blen - (2u + nameBytes);
                // Color model is 4 ASCII chars ('RGB ', 'CMYK', 'LAB ', 'Gray')
                if (std::memcmp(cptr, "RGB ", 4) == 0 && rem >= 16u) {
                    float rf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 4),  0.0f, 1.0f);
                    float gf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 8),  0.0f, 1.0f);
                    float bf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 12), 0.0f, 1.0f);
                    colors.push_back({
                        static_cast<uint8_t>(std::lround(rf * 255.0f)),
                        static_cast<uint8_t>(std::lround(gf * 255.0f)),
                        static_cast<uint8_t>(std::lround(bf * 255.0f))
                    });
                } else if (std::memcmp(cptr, "CMYK", 4) == 0 && rem >= 20u) {
                    float cf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 4),  0.0f, 1.0f);
                    float mf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 8),  0.0f, 1.0f);
                    float yf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 12), 0.0f, 1.0f);
                    float kf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 16), 0.0f, 1.0f);
                    colors.push_back({
                        static_cast<uint8_t>(std::lround((1.0f - cf) * (1.0f - kf) * 255.0f)),
                        static_cast<uint8_t>(std::lround((1.0f - mf) * (1.0f - kf) * 255.0f)),
                        static_cast<uint8_t>(std::lround((1.0f - yf) * (1.0f - kf) * 255.0f))
                    });
                } else if ((std::memcmp(cptr, "LAB ", 4) == 0 || std::memcmp(cptr, "Lab ", 4) == 0) && rem >= 16u) {
                    // In Adobe .ase, L is stored as float 0.0..1.0 (representing L* 0..100; if > 1.001, already 0..100), a & b as -128..127
                    double rawL = ::Explorer2::ByteIO::RdF32BE(cptr + 4);
                    double L = (rawL <= 1.001 && rawL >= 0.0) ? (rawL * 100.0) : rawL;
                    double a = ::Explorer2::ByteIO::RdF32BE(cptr + 8);
                    double bVal = ::Explorer2::ByteIO::RdF32BE(cptr + 12);
                    colors.push_back(DetailPalette::CieLabD50ToSrgb(L, a, bVal));
                } else if ((std::memcmp(cptr, "Gray", 4) == 0 || std::memcmp(cptr, "GRAY", 4) == 0) && rem >= 8u) {
                    float gf = std::clamp(::Explorer2::ByteIO::RdF32BE(cptr + 4), 0.0f, 1.0f);
                    uint8_t g8 = static_cast<uint8_t>(std::lround(gf * 255.0f));
                    colors.push_back({ g8, g8, g8 });
                }
            }
        }
        pos += blen;
    }

    if (!DetailPalette::RenderPaletteSwatchGrid(colors, outInfo)) return false;
    outInfo.formatName = L"Adobe Swatch Exchange (.ase, " + std::to_wstring(colors.size()) + L" colors)";
    outInfo.layerNames = std::move(swatchNames);
    return true;
}

} // namespace Formats
