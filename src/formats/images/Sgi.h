#pragma once
// Sgi.h — Silicon Graphics Image (.sgi / .rgb / .bw / .rgba) 8-bit & 16-bit Verbatim/RLE Decoder

namespace Formats {

inline bool DecodeSGI(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 512 || data[0] != 0x01 || data[1] != 0xDA) return false;
    const uint8_t storage = data[2]; // 0 = VERBATIM, 1 = RLE
    const uint8_t bpc     = data[3]; // 1 or 2 bytes per channel
    auto readBE16 = [](const uint8_t* p) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    };
    auto readBE32 = [](const uint8_t* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
    };
    auto scale16To8 = [](uint16_t v) -> uint8_t {
        return static_cast<uint8_t>((static_cast<uint32_t>(v) * 255u + 32767u) / 65535u);
    };

    const uint16_t dim = readBE16(data + 4);
    const uint16_t w   = readBE16(data + 6);
    const uint16_t h   = (dim >= 2) ? readBE16(data + 8) : 1u;
    const uint16_t z   = (dim >= 3) ? readBE16(data + 10) : 1u;
    if (dim < 1 || dim > 3 || w == 0 || h == 0 || w > 8192 || h > 8192 || z == 0 || z > 4 || (bpc != 1 && bpc != 2)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = z;
    outInfo.bitsPerChannel = bpc * 8u;
    outInfo.formatName = L"Silicon Graphics Image (.sgi)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4u, 255u);

    const size_t totalSamples = static_cast<size_t>(w) * h * z;
    std::vector<uint8_t> planes(totalSamples, 0u);

    if (storage == 0) {
        const size_t req = 512u + totalSamples * bpc;
        if (req > size) return false;
        const uint8_t* src = data + 512u;
        if (bpc == 1u) {
            std::memcpy(planes.data(), src, totalSamples);
        } else {
            for (size_t i = 0; i < totalSamples; ++i) {
                planes[i] = scale16To8(readBE16(src + i * 2u));
            }
        }
    } else if (storage == 1) {
        const size_t tabEntries = static_cast<size_t>(h) * z;
        if (512u + tabEntries * 8u > size) return false;
        for (uint16_t ch = 0; ch < z; ++ch) {
            for (uint16_t y = 0; y < h; ++y) {
                const size_t tabIdx = static_cast<size_t>(ch) * h + y;
                const uint32_t rleOff = readBE32(data + 512u + tabIdx * 4u);
                const uint32_t rleLen = readBE32(data + 512u + (tabEntries + tabIdx) * 4u);
                if (rleOff >= size || rleLen == 0) continue;
                const uint8_t* p = data + rleOff;
                const uint8_t* pEnd = data + std::min<size_t>(size, static_cast<size_t>(rleOff) + rleLen);
                uint8_t* dstRow = &planes[(static_cast<size_t>(ch) * h + y) * w];
                uint16_t x = 0;
                if (bpc == 1u) {
                    while (p < pEnd && x < w) {
                        const uint8_t pkt = *p++;
                        uint8_t count = pkt & 0x7Fu;
                        if (count == 0) break;
                        if (pkt & 0x80u) {
                            while (count-- && p < pEnd && x < w) dstRow[x++] = *p++;
                        } else {
                            if (p >= pEnd) break;
                            const uint8_t val = *p++;
                            while (count-- && x < w) dstRow[x++] = val;
                        }
                    }
                } else {
                    // 16-bit SGI RLE stores big-endian uint16_t control words and uint16_t sample values!
                    while (p + 2u <= pEnd && x < w) {
                        const uint16_t pkt = readBE16(p);
                        p += 2u;
                        uint16_t count = pkt & 0x7Fu;
                        if (count == 0) break;
                        if (pkt & 0x80u) {
                            while (count-- && p + 2u <= pEnd && x < w) {
                                dstRow[x++] = scale16To8(readBE16(p));
                                p += 2u;
                            }
                        } else {
                            if (p + 2u > pEnd) break;
                            const uint8_t val = scale16To8(readBE16(p));
                            p += 2u;
                            while (count-- && x < w) dstRow[x++] = val;
                        }
                    }
                }
            }
        }
    } else {
        return false;
    }

    const size_t planeStride = static_cast<size_t>(w) * h;
    for (uint16_t y = 0; y < h; ++y) {
        const uint16_t dstY = h - 1u - y; // SGI origin is bottom-left
        for (uint16_t x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const uint8_t r = planes[idx];
            const uint8_t g = (z >= 3u) ? planes[planeStride + idx] : r;
            const uint8_t b = (z >= 3u) ? planes[planeStride * 2u + idx] : r;
            const uint8_t a = (z == 2u) ? planes[planeStride + idx] : ((z == 4u) ? planes[planeStride * 3u + idx] : 255u);
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dstY) * w + x) * 4u], r, g, b, a);
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
