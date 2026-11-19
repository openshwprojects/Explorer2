#pragma once
// Wbmp.h — Wireless Application Protocol Bitmap (.wbmp) Decoder

namespace Formats {

inline bool DecodeWBMP(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 4 || data[0] != 0 || data[1] != 0) return false;
    size_t pos = 2;
    auto readUintVar = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 5 && pos < size; ++i) {
            uint8_t b = data[pos++];
            v = (v << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) break;
        }
        return v;
    };
    uint32_t w = readUintVar();
    uint32_t h = readUintVar();
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;
    size_t rowBytes = (w + 7) / 8;
    if (pos + rowBytes * h > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 1;
    outInfo.bitsPerChannel = 1;
    outInfo.formatName = L"WAP Wireless Bitmap (.wbmp)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);

    const uint8_t* src = data + pos;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            bool white = ((src[y * rowBytes + (x / 8)] >> (7 - (x & 7))) & 1) != 0;
            uint8_t c = white ? 248 : 20;
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
            dst[0] = c; dst[1] = c; dst[2] = c; dst[3] = 255;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
