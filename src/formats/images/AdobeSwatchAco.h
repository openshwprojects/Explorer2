#pragma once
// AdobeSwatchAco.h — Adobe Photoshop Color Swatch (.aco v1 & v2) Renderer
// Strictly handles .aco binary swatch tables (RGB, HSB, CMYK, CIE Lab D50, and Grayscale 0..10000).

namespace Formats {

inline bool DecodeAdobeSwatchAco(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 14) return false;
    if (std::memcmp(data, "ASEF", 4) == 0) return false;

    auto parseSection = [&](size_t startPos, uint16_t expectedVer, size_t& outEndPos,
                            std::vector<DetailPalette::Rgb>& outColors,
                            std::vector<std::wstring>& outNames) -> bool
    {
        if (startPos + 4 > size) return false;
        uint16_t ver = ::Explorer2::ByteIO::RdU16BE(data + startPos);
        uint16_t cnt = ::Explorer2::ByteIO::RdU16BE(data + startPos + 2);
        if (ver != expectedVer || cnt == 0 || cnt > 4096) return false;

        size_t pos = startPos + 4;
        outColors.clear();
        outNames.clear();
        for (uint16_t i = 0; i < cnt; ++i) {
            if (pos + 10 > size) return false;
            uint16_t cspace = ::Explorer2::ByteIO::RdU16BE(data + pos);
            uint16_t w0 = ::Explorer2::ByteIO::RdU16BE(data + pos + 2);
            uint16_t w1 = ::Explorer2::ByteIO::RdU16BE(data + pos + 4);
            uint16_t w2 = ::Explorer2::ByteIO::RdU16BE(data + pos + 6);
            uint16_t w3 = ::Explorer2::ByteIO::RdU16BE(data + pos + 8);
            pos += 10;

            DetailPalette::Rgb rgb{ 0, 0, 0 };
            if (cspace == 0) { // RGB: 0..65535
                rgb.r = static_cast<uint8_t>((static_cast<uint32_t>(w0) * 255u + 32767u) / 65535u);
                rgb.g = static_cast<uint8_t>((static_cast<uint32_t>(w1) * 255u + 32767u) / 65535u);
                rgb.b = static_cast<uint8_t>((static_cast<uint32_t>(w2) * 255u + 32767u) / 65535u);
            } else if (cspace == 1) { // HSB / HSV: H, S, B in 0..65535
                rgb = DetailPalette::HsvToSrgb(w0 / 65535.0, w1 / 65535.0, w2 / 65535.0);
            } else if (cspace == 2) { // CMYK in .aco: 65535 = 0% ink, 0 = 100% ink
                double r01 = (w0 / 65535.0) * (w3 / 65535.0);
                double g01 = (w1 / 65535.0) * (w3 / 65535.0);
                double b01 = (w2 / 65535.0) * (w3 / 65535.0);
                rgb.r = static_cast<uint8_t>(std::clamp(std::lround(r01 * 255.0), 0l, 255l));
                rgb.g = static_cast<uint8_t>(std::clamp(std::lround(g01 * 255.0), 0l, 255l));
                rgb.b = static_cast<uint8_t>(std::clamp(std::lround(b01 * 255.0), 0l, 255l));
            } else if (cspace == 7) { // CIE Lab (Photoshop): L in 0..10000 (L*100), a & b signed in -12800..12700 (a*100, b*100)
                double L = static_cast<double>(w0) / 100.0;
                double a = static_cast<double>(static_cast<int16_t>(w1)) / 100.0;
                double b = static_cast<double>(static_cast<int16_t>(w2)) / 100.0;
                rgb = DetailPalette::CieLabD50ToSrgb(L, a, b);
            } else if (cspace == 8) { // Grayscale: w0 in 0..10000 (0 = black, 10000 = white)
                uint32_t clamped = std::min<uint32_t>(w0, 10000u);
                uint8_t g = static_cast<uint8_t>((clamped * 255u + 5000u) / 10000u);
                rgb = { g, g, g };
            } else if (cspace == 9) { // Wide CMYK: 0..10000 ink (0 = 0% ink, 10000 = 100% ink)
                double c = std::clamp(w0 / 10000.0, 0.0, 1.0);
                double m = std::clamp(w1 / 10000.0, 0.0, 1.0);
                double y = std::clamp(w2 / 10000.0, 0.0, 1.0);
                double k = std::clamp(w3 / 10000.0, 0.0, 1.0);
                rgb.r = static_cast<uint8_t>(std::clamp(std::lround((1.0 - c) * (1.0 - k) * 255.0), 0l, 255l));
                rgb.g = static_cast<uint8_t>(std::clamp(std::lround((1.0 - m) * (1.0 - k) * 255.0), 0l, 255l));
                rgb.b = static_cast<uint8_t>(std::clamp(std::lround((1.0 - y) * (1.0 - k) * 255.0), 0l, 255l));
            } else {
                // Unknown/custom color space rejected unless standard 0,1,2,7,8,9
                return false;
            }

            if (ver == 2) {
                if (pos + 4 > size) return false;
                uint32_t nlen = ::Explorer2::ByteIO::RdU32BE(data + pos);
                pos += 4;
                if (nlen > 4096 || static_cast<uint64_t>(nlen) * 2ull > size - pos) return false;
                if (outNames.size() < 64 && nlen > 0) {
                    std::wstring nm = DetailPalette::ReadUtf16BeName(data + pos, nlen);
                    if (!nm.empty()) outNames.push_back(std::move(nm));
                }
                pos += static_cast<size_t>(nlen) * 2u;
            }

            if (outColors.size() < 256) {
                outColors.push_back(rgb);
            }
        }
        outEndPos = pos;
        return !outColors.empty();
    };

    uint16_t topVer = ::Explorer2::ByteIO::RdU16BE(data);
    std::vector<DetailPalette::Rgb> colors;
    std::vector<std::wstring> names;
    size_t v1End = 0;

    if (topVer == 1) {
        if (!parseSection(0, 1, v1End, colors, names)) return false;
        // Standard Photoshop .aco appends a v2 section with UTF-16BE names immediately after v1
        std::vector<DetailPalette::Rgb> v2Colors;
        std::vector<std::wstring> v2Names;
        size_t v2End = 0;
        if (v1End + 4 <= size && ::Explorer2::ByteIO::RdU16BE(data + v1End) == 2 &&
            parseSection(v1End, 2, v2End, v2Colors, v2Names))
        {
            colors = std::move(v2Colors);
            names = std::move(v2Names);
        }
    } else if (topVer == 2) {
        size_t v2End = 0;
        if (!parseSection(0, 2, v2End, colors, names)) return false;
    } else {
        return false;
    }

    if (!DetailPalette::RenderPaletteSwatchGrid(colors, outInfo)) return false;
    outInfo.formatName = L"Adobe Photoshop Swatch (.aco, " + std::to_wstring(colors.size()) + L" colors)";
    outInfo.layerNames = std::move(names);
    return true;
}

} // namespace Formats
