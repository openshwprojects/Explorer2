#pragma once
// Vtf.h — Valve Texture Format (.vtf, Source Engine 7.x) Decoder

namespace Formats {

inline bool DecodeVTF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 64 || std::memcmp(data, "VTF\0", 4) != 0) return false;
    uint32_t hdrSize = 0, imgFmt = 0;
    uint16_t w = 0, h = 0;
    std::memcpy(&hdrSize, data + 12, 4);
    std::memcpy(&w, data + 16, 2);
    std::memcpy(&h, data + 18, 2);
    std::memcpy(&imgFmt, data + 52, 4);
    uint8_t mipCount = data[56];
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || hdrSize > size) return false;
    (void)mipCount;

    // Image format bytes per pixel (0=RGBA8888, 2=RGB888, 3=BGR888, 12=BGRA8888)
    uint32_t bpp = 4;
    bool bgrOrder = (imgFmt == 3 || imgFmt == 12);
    if (imgFmt == 2 || imgFmt == 3) bpp = 3;
    size_t topMipBytes = static_cast<size_t>(w) * h * bpp;
    if (topMipBytes > size - hdrSize) return false;

    // VTF stores smallest mip first and largest mip at the end of the file
    const uint8_t* src = data + (size - topMipBytes);
    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = bpp;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Valve Texture Format (.vtf)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);

    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const uint8_t* px = src + i * bpp;
        uint8_t r = bgrOrder ? px[2] : px[0];
        uint8_t g = px[1];
        uint8_t b = bgrOrder ? px[0] : px[2];
        uint8_t a = (bpp == 4) ? px[3] : 255;
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
