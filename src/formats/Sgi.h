#pragma once
// Sgi.h — Silicon Graphics Image (.sgi / .rgb / .bw / .rgba) Decoder

namespace Formats {

inline bool DecodeSGI(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 512 || data[0] != 0x01 || data[1] != 0xDA) return false;
    uint8_t storage = data[2]; // 0 = VERBATIM, 1 = RLE
    uint8_t bpc = data[3];     // 1 or 2 bytes per channel
    auto readBE16 = [](const uint8_t* p) -> uint16_t { return static_cast<uint16_t>((p[0] << 8) | p[1]); };
    auto readBE32 = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    };

    uint16_t w = readBE16(data + 6);
    uint16_t h = readBE16(data + 8);
    uint16_t z = readBE16(data + 10);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || z == 0 || z > 4 || (bpc != 1 && bpc != 2)) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = z;
    outInfo.bitsPerChannel = bpc * 8;
    outInfo.formatName = L"Silicon Graphics Image (.sgi)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    std::vector<uint8_t> planes(static_cast<size_t>(w) * h * z, 0);
    if (storage == 0) {
        size_t req = 512 + static_cast<size_t>(w) * h * z * bpc;
        if (req > size) return false;
        const uint8_t* src = data + 512;
        for (size_t i = 0; i < static_cast<size_t>(w) * h * z; ++i) {
            planes[i] = src[i * bpc];
        }
    } else if (storage == 1) {
        size_t tabEntries = static_cast<size_t>(h) * z;
        if (512 + tabEntries * 8 > size) return false;
        for (uint16_t ch = 0; ch < z; ++ch) {
            for (uint16_t y = 0; y < h; ++y) {
                size_t tabIdx = static_cast<size_t>(ch) * h + y;
                uint32_t rleOff = readBE32(data + 512 + tabIdx * 4);
                uint32_t rleLen = readBE32(data + 512 + (tabEntries + tabIdx) * 4);
                if (rleOff >= size || rleLen == 0) continue;
                const uint8_t* p = data + rleOff;
                const uint8_t* pEnd = data + std::min<size_t>(size, static_cast<size_t>(rleOff) + rleLen);
                uint8_t* dstRow = &planes[(static_cast<size_t>(ch) * h + y) * w];
                uint16_t x = 0;
                while (p < pEnd && x < w) {
                    uint8_t pixel = *p++;
                    uint8_t count = pixel & 0x7F;
                    if (count == 0) break;
                    if (pixel & 0x80) {
                        while (count-- && p < pEnd && x < w) dstRow[x++] = *p++;
                    } else {
                        if (p >= pEnd) break;
                        uint8_t val = *p++;
                        while (count-- && x < w) dstRow[x++] = val;
                    }
                }
            }
        }
    } else {
        return false;
    }

    size_t planeStride = static_cast<size_t>(w) * h;
    for (uint16_t y = 0; y < h; ++y) {
        uint16_t dstY = h - 1 - y; // SGI origin is bottom-left
        for (uint16_t x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            uint8_t r = planes[idx];
            uint8_t g = (z >= 3) ? planes[planeStride + idx] : r;
            uint8_t b = (z >= 3) ? planes[planeStride * 2 + idx] : r;
            uint8_t a = (z == 2) ? planes[planeStride + idx] : ((z == 4) ? planes[planeStride * 3 + idx] : 255);
            uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(dstY) * w + x) * 4];
            dst[0] = static_cast<uint8_t>((b * a) / 255);
            dst[1] = static_cast<uint8_t>((g * a) / 255);
            dst[2] = static_cast<uint8_t>((r * a) / 255);
            dst[3] = a;
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
