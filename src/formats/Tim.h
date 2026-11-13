#pragma once
// Tim.h — Sony PlayStation 1 TIM Texture (.tim) Decoder

namespace Formats {

inline bool DecodeTIM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 20 || data[0] != 0x10 || data[1] != 0x00 || data[2] != 0x00 || data[3] != 0x00) return false;
    uint32_t flags = 0;
    std::memcpy(&flags, data + 4, 4);
    uint32_t pmode = flags & 7; // 0=4bpp, 1=8bpp, 2=16bpp, 3=24bpp
    bool hasClut = (flags & 8) != 0;

    size_t pos = 8;
    uint16_t clut[256] = {};
    if (hasClut) {
        if (pos + 12 > size) return false;
        uint32_t clutLen = 0;
        std::memcpy(&clutLen, data + pos, 4);
        if (clutLen < 12 || pos + clutLen > size) return false;
        size_t nCols = std::min<size_t>(256, (clutLen - 12) / 2);
        std::memcpy(clut, data + pos + 12, nCols * 2);
        pos += clutLen;
    }
    if (pos + 12 > size) return false;
    uint32_t imgLen = 0;
    uint16_t wWords = 0, h = 0;
    std::memcpy(&imgLen, data + pos, 4);
    std::memcpy(&wWords, data + pos + 8, 2);
    std::memcpy(&h, data + pos + 10, 2);
    pos += 12;

    uint32_t w = wWords;
    if (pmode == 0) w = wWords * 4;
    else if (pmode == 1) w = wWords * 2;
    else if (pmode == 3) w = (wWords * 2) / 3;
    if (w == 0 || h == 0 || w > 4096 || h > 4096 || pos >= size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"PlayStation 1 TIM (.tim)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    auto unpackPs1_16 = [](uint16_t c, uint8_t* dst) {
        uint8_t r = static_cast<uint8_t>(((c & 31) * 255) / 31);
        uint8_t g = static_cast<uint8_t>((((c >> 5) & 31) * 255) / 31);
        uint8_t b = static_cast<uint8_t>((((c >> 10) & 31) * 255) / 31);
        dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
    };

    const uint8_t* src = data + pos;
    size_t avail = size - pos;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t pxIdx = static_cast<size_t>(y) * w + x;
            uint8_t* dst = &outInfo.bgraPixels[pxIdx * 4];
            if (pmode == 2 && (pxIdx * 2 + 2) <= avail) {
                uint16_t v = 0; std::memcpy(&v, src + pxIdx * 2, 2);
                unpackPs1_16(v, dst);
            } else if (pmode == 1 && pxIdx < avail) {
                unpackPs1_16(clut[src[pxIdx]], dst);
            } else if (pmode == 0 && (pxIdx / 2) < avail) {
                uint8_t byte = src[pxIdx / 2];
                uint8_t nib = (x & 1) ? (byte >> 4) : (byte & 0x0F);
                unpackPs1_16(clut[nib], dst);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
