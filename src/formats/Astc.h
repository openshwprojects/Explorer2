#pragma once
// Astc.h — ARM Adaptive Scalable Texture Compression (.astc) Decoder

namespace Formats {

inline bool DecodeASTC(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16 || data[0] != 0x13 || data[1] != 0xAB || data[2] != 0xA1 || data[3] != 0x5C) return false;
    uint32_t blockX = std::max<uint32_t>(1, data[4]);
    uint32_t blockY = std::max<uint32_t>(1, data[5]);
    uint32_t w = static_cast<uint32_t>(data[7]) | (static_cast<uint32_t>(data[8]) << 8) | (static_cast<uint32_t>(data[9]) << 16);
    uint32_t h = static_cast<uint32_t>(data[10]) | (static_cast<uint32_t>(data[11]) << 8) | (static_cast<uint32_t>(data[12]) << 16);
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    uint32_t bw = (w + blockX - 1) / blockX;
    uint32_t bh = (h + blockY - 1) / blockY;
    if (16 + static_cast<size_t>(bw) * bh * 16 > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ARM ASTC Texture (" + std::to_wstring(blockX) + L"x" + std::to_wstring(blockY) + L")";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    const uint8_t* ptr = data + 16;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            // Support void-extent constant color blocks (0xFC 0xFD) and endpoint interpolation
            bool isConstBlock = (ptr[0] == 0xFC && (ptr[1] & 0xFD) == 0xFD);
            uint8_t r0 = isConstBlock ? ptr[9]  : ptr[2];
            uint8_t g0 = isConstBlock ? ptr[11] : ptr[3];
            uint8_t b0 = isConstBlock ? ptr[13] : ptr[4];
            uint8_t r1 = isConstBlock ? r0      : ptr[5];
            uint8_t g1 = isConstBlock ? g0      : ptr[6];
            uint8_t b1 = isConstBlock ? b0      : ptr[7];
            for (uint32_t py = 0; py < blockY; ++py) {
                for (uint32_t px = 0; px < blockX; ++px) {
                    uint32_t x = bx * blockX + px, y = by * blockY + py;
                    if (x >= w || y >= h) continue;
                    uint8_t wgt = ptr[8 + ((px + py) & 7)];
                    uint8_t r = static_cast<uint8_t>((r0 * (255 - wgt) + r1 * wgt) / 255);
                    uint8_t g = static_cast<uint8_t>((g0 * (255 - wgt) + g1 * wgt) / 255);
                    uint8_t b = static_cast<uint8_t>((b0 * (255 - wgt) + b1 * wgt) / 255);
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
                    dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255;
                }
            }
            ptr += 16;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
