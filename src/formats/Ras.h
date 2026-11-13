#pragma once
// Ras.h — Sun Rasterfile (.ras / .sun, magic 0x59A66A95) Decoder

namespace Formats {

inline bool DecodeSunRaster(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 32 || data[0] != 0x59 || data[1] != 0xA6 || data[2] != 0x6A || data[3] != 0x95) return false;
    auto readBE32 = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    };
    uint32_t w       = readBE32(data + 4);
    uint32_t h       = readBE32(data + 8);
    uint32_t depth   = readBE32(data + 12);
    uint32_t rasType = readBE32(data + 20); // 1=Standard, 2=RLE, 3=RGB
    uint32_t mapType = readBE32(data + 24);
    uint32_t mapLen  = readBE32(data + 28);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || (depth != 1 && depth != 8 && depth != 24 && depth != 32)) return false;
    if (32 + mapLen > size) return false;

    const uint8_t* cmap = data + 32;
    size_t pixelOff = 32 + mapLen;
    size_t rowBytes = ((w * depth + 15) / 16) * 2; // 16-bit padded scanlines
    size_t rawSize = rowBytes * h;

    std::vector<uint8_t> rawBuf;
    const uint8_t* srcRows = nullptr;
    if (rasType == 2) { // Byte-encoded RLE (0x80 escape)
        rawBuf.assign(rawSize, 0);
        size_t dstIdx = 0;
        const uint8_t* p = data + pixelOff;
        const uint8_t* pEnd = data + size;
        while (p < pEnd && dstIdx < rawSize) {
            uint8_t b = *p++;
            if (b == 0x80) {
                if (p >= pEnd) break;
                uint8_t count = *p++;
                if (count == 0) {
                    rawBuf[dstIdx++] = 0x80;
                } else {
                    if (p >= pEnd) break;
                    uint8_t val = *p++;
                    uint32_t run = static_cast<uint32_t>(count) + 1;
                    while (run-- && dstIdx < rawSize) rawBuf[dstIdx++] = val;
                }
            } else {
                rawBuf[dstIdx++] = b;
            }
        }
        srcRows = rawBuf.data();
    } else {
        if (pixelOff + rawSize > size) return false;
        srcRows = data + pixelOff;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = (depth >= 24) ? (depth / 8) : 1;
    outInfo.bitsPerChannel = (depth == 1) ? 1 : 8;
    outInfo.formatName = L"Sun Rasterfile (.ras)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4);

    bool isRgbOrder = (rasType == 3);
    uint32_t cmapColors = (mapType == 1 && mapLen >= 3) ? (mapLen / 3) : 0;

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = srcRows + static_cast<size_t>(y) * rowBytes;
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (depth == 1) {
                bool bit = ((row[x / 8] >> (7 - (x & 7))) & 1) != 0;
                r = g = b = bit ? 0 : 255;
            } else if (depth == 8) {
                uint8_t idx = row[x];
                if (cmapColors > 0 && idx < cmapColors) {
                    r = cmap[idx];
                    g = cmap[cmapColors + idx];
                    b = cmap[cmapColors * 2 + idx];
                } else {
                    r = g = b = idx;
                }
            } else if (depth == 24) {
                const uint8_t* px = row + x * 3;
                b = isRgbOrder ? px[2] : px[0];
                g = px[1];
                r = isRgbOrder ? px[0] : px[2];
            } else if (depth == 32) {
                const uint8_t* px = row + x * 4;
                b = isRgbOrder ? px[3] : px[1];
                g = px[2];
                r = isRgbOrder ? px[1] : px[3];
            }
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
            dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = a;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
