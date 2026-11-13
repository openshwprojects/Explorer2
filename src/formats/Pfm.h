#pragma once
// Pfm.h — Portable FloatMap HDR (.pfm, PF/Pf) Decoder

namespace Formats {

inline bool DecodePFM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16 || data[0] != 'P' || (data[1] != 'F' && data[1] != 'f')) return false;
    bool isColor = (data[1] == 'F');
    uint32_t channels = isColor ? 3 : 1;

    size_t pos = 2;
    auto skipWsAndComments = [&]() {
        while (pos < size) {
            if (data[pos] == '#') {
                while (pos < size && data[pos] != '\n') ++pos;
            } else if (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r' || data[pos] == '\n') {
                ++pos;
            } else {
                break;
            }
        }
    };
    auto readToken = [&]() -> std::string {
        skipWsAndComments();
        std::string s;
        while (pos < size && data[pos] > ' ' && data[pos] != '#') {
            s.push_back(static_cast<char>(data[pos++]));
        }
        return s;
    };

    uint32_t w = static_cast<uint32_t>(std::strtoul(readToken().c_str(), nullptr, 10));
    uint32_t h = static_cast<uint32_t>(std::strtoul(readToken().c_str(), nullptr, 10));
    float scale = std::strtof(readToken().c_str(), nullptr);
    if (pos < size && (data[pos] == '\r' || data[pos] == '\n' || data[pos] == ' ')) {
        if (data[pos] == '\r' && pos + 1 < size && data[pos + 1] == '\n') pos += 2;
        else ++pos;
    }
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || std::abs(scale) < 1e-9f) return false;
    bool littleEndian = (scale < 0.0f);
    size_t floatCount = static_cast<size_t>(w) * h * channels;
    if (pos + floatCount * 4 > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = channels;
    outInfo.bitsPerChannel = 32;
    outInfo.formatName = isColor ? L"Portable FloatMap RGB (.pfm)" : L"Portable FloatMap Gray (.pfm)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);

    auto readF32 = [&](const uint8_t* p) -> float {
        uint32_t u = littleEndian
            ? (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24))
            : ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return std::isfinite(f) ? std::max(0.0f, f) : 0.0f;
    };
    auto toneMap = [](float v) -> uint8_t {
        float m = v / (1.0f + v);
        float g = std::pow(std::clamp(m, 0.0f, 1.0f), 1.0f / 2.2f);
        return static_cast<uint8_t>( std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
    };

    const uint8_t* src = data + pos;
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t dstY = h - 1 - y; // PFM rows are stored bottom-to-top
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* px = src + (static_cast<size_t>(y) * w + x) * channels * 4;
            uint8_t r = toneMap(readF32(px));
            uint8_t g = isColor ? toneMap(readF32(px + 4)) : r;
            uint8_t b = isColor ? toneMap(readF32(px + 8)) : r;
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(dstY) * w + x) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
