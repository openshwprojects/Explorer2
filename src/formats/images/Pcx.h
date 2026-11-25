#pragma once
// ============================================================================
// 6. ZSOFT PCX (.pcx) — RLE 1/2/4-bpp EGA Planar/Packed, 8-bpp VGA & 24/32-bpp RGB(A)
// ============================================================================
bool DecodePCX(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 128 || data[0] != 0x0A || (data[2] != 0 && data[2] != 1)) return false;

    const bool isRle            = (data[2] == 1);
    const uint8_t bpp           = data[3];
    const uint16_t xmin         = ReadU16LE(data + 4);
    const uint16_t ymin         = ReadU16LE(data + 6);
    const uint16_t xmax         = ReadU16LE(data + 8);
    const uint16_t ymax         = ReadU16LE(data + 10);
    const uint8_t numPlanes     = data[65];
    const uint16_t bytesPerLine = ReadU16LE(data + 66);

    if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) return false;
    if (xmax < xmin || ymax < ymin) return false;
    const uint32_t width  = static_cast<uint32_t>(xmax - xmin + 1);
    const uint32_t height = static_cast<uint32_t>(ymax - ymin + 1);
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (bytesPerLine == 0 || numPlanes == 0 || numPlanes > 4) return false;

    const size_t minBytesPerPlane = (static_cast<size_t>(width) * bpp + 7u) / 8u;
    if (bytesPerLine < minBytesPerPlane) return false;

    const uint8_t* vgaPalette = (data[1] >= 5 && size >= 769 && data[size - 769] == 0x0C) ? (data + size - 768) : nullptr;
    const uint8_t* egaPalette = data + 16; // 16 * 3 RGB bytes at header[16..63]

    const size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.origWidth = width;
    outInfo.origHeight = height;
    outInfo.channels = (bpp == 8 && numPlanes >= 3) ? numPlanes : 3u;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ZSoft PCX (" + std::to_wstring(bpp * numPlanes) + L"-bit)";
    outInfo.bgraPixels.resize(totalPixels * 4, 255);

    const size_t scanlineBytes = static_cast<size_t>(bytesPerLine) * numPlanes;
    std::vector<uint8_t> lineBuf(scanlineBytes, 0);
    size_t pos = 128;
    const size_t dataLimit = vgaPalette ? (size - 769) : size;

    for (uint32_t y = 0; y < height; ++y) {
        std::fill(lineBuf.begin(), lineBuf.end(), uint8_t{0});
        size_t lx = 0;
        if (isRle) {
            while (lx < scanlineBytes && pos < dataLimit) {
                const uint8_t b = data[pos++];
                uint32_t count = 1;
                uint8_t val = b;
                if ((b & 0xC0u) == 0xC0u) {
                    count = b & 0x3Fu;
                    if (pos >= dataLimit) break;
                    val = data[pos++];
                }
                for (uint32_t k = 0; k < count && lx < scanlineBytes; ++k, ++lx) {
                    lineBuf[lx] = val;
                }
            }
        } else {
            const size_t take = (pos < dataLimit) ? std::min(scanlineBytes, dataLimit - pos) : 0u;
            if (take > 0) {
                std::memcpy(lineBuf.data(), data + pos, take);
                pos += take;
            }
        }

        uint8_t* dstRow = outInfo.bgraPixels.data() + static_cast<size_t>(y) * width * 4;
        if (bpp == 8 && numPlanes >= 3) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t r = lineBuf[x];
                const uint8_t g = lineBuf[bytesPerLine + x];
                const uint8_t b = lineBuf[bytesPerLine * 2u + x];
                const uint8_t a = (numPlanes == 4) ? lineBuf[bytesPerLine * 3u + x] : 255u;
                WritePremulBGRA(dstRow + x * 4, r, g, b, a);
            }
        } else if (bpp == 8 && numPlanes == 1) {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t idx = lineBuf[x];
                if (vgaPalette) {
                    WritePremulBGRA(dstRow + x * 4, vgaPalette[idx * 3], vgaPalette[idx * 3 + 1], vgaPalette[idx * 3 + 2], 255);
                } else {
                    WritePremulBGRA(dstRow + x * 4, idx, idx, idx, 255);
                }
            }
        } else if (bpp == 1 && numPlanes == 1) {
            for (uint32_t x = 0; x < width; ++x) {
                const bool bit = (lineBuf[x >> 3] & (0x80u >> (x & 7u))) != 0;
                const uint8_t c = bit ? 255u : 0u;
                WritePremulBGRA(dstRow + x * 4, c, c, c, 255);
            }
        } else if (bpp == 1 && numPlanes >= 2 && numPlanes <= 4) {
            // Planar EGA (e.g. 4-bpp = 1-bit x 4 planes: plane 0 is LSB, plane 3 is MSB)
            for (uint32_t x = 0; x < width; ++x) {
                const size_t byteOff = x >> 3;
                const uint8_t bitMask = static_cast<uint8_t>(0x80u >> (x & 7u));
                uint8_t idx = 0;
                for (uint8_t p = 0; p < numPlanes; ++p) {
                    if ((lineBuf[static_cast<size_t>(p) * bytesPerLine + byteOff] & bitMask) != 0u) {
                        idx |= static_cast<uint8_t>(1u << p);
                    }
                }
                idx &= 0x0Fu;
                WritePremulBGRA(dstRow + x * 4, egaPalette[idx * 3], egaPalette[idx * 3 + 1], egaPalette[idx * 3 + 2], 255);
            }
        } else if (bpp == 4 && numPlanes == 1) {
            // Packed 4-bpp 1-plane: high nibble = even x, low nibble = odd x
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t packed = lineBuf[x >> 1];
                const uint8_t idx = ((x & 1u) == 0u) ? (packed >> 4u) : (packed & 0x0Fu);
                WritePremulBGRA(dstRow + x * 4, egaPalette[idx * 3], egaPalette[idx * 3 + 1], egaPalette[idx * 3 + 2], 255);
            }
        } else if (bpp == 2 && numPlanes == 1) {
            // Packed 2-bpp 1-plane CGA: MSB-first 2-bit crumbs
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t shift = static_cast<uint8_t>(6u - ((x & 3u) * 2u));
                const uint8_t idx = (lineBuf[x >> 2] >> shift) & 0x03u;
                WritePremulBGRA(dstRow + x * 4, egaPalette[idx * 3], egaPalette[idx * 3 + 1], egaPalette[idx * 3 + 2], 255);
            }
        } else {
            return false;
        }
    }

    outInfo.valid = true;
    return true;
}
