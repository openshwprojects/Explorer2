#pragma once
// Farbfeld.h — suckless Farbfeld 16-bit RGBA (.ff / .farbfeld) Decoder

namespace Formats {

inline bool DecodeFarbfeld(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16 || std::memcmp(data, "farbfeld", 8) != 0) return false;
    auto rdBE32 = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    };
    uint32_t w = rdBE32(data + 8);
    uint32_t h = rdBE32(data + 12);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    if (16 + static_cast<size_t>(w) * h * 8 > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 16;
    outInfo.formatName = L"Farbfeld 16-bit RGBA (.ff)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);

    const uint8_t* src = data + 16;
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        uint8_t r = src[i * 8 + 0];
        uint8_t g = src[i * 8 + 2];
        uint8_t b = src[i * 8 + 4];
        uint8_t a = src[i * 8 + 6];
        uint8_t* dst = &outInfo.bgraPixels[i * 4];
        dst[0] = static_cast<uint8_t>((b * a) / 255);
        dst[1] = static_cast<uint8_t>((g * a) / 255);
        dst[2] = static_cast<uint8_t>((r * a) / 255);
        dst[3] = a;
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
