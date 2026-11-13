#pragma once
// ============================================================================
// 6. ZSOFT PCX (.pcx) — RLE 8-bit 256-Color Palette & 24-bit 3-Plane RGB
// ============================================================================
bool DecodePCX(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 128 || data[0] != 0x0A || data[2] != 1) return false;

    uint8_t bpp        = data[3];
    uint16_t xmin      = ReadU16LE(data + 4);
    uint16_t ymin      = ReadU16LE(data + 6);
    uint16_t xmax      = ReadU16LE(data + 8);
    uint16_t ymax      = ReadU16LE(data + 10);
    uint8_t numPlanes  = data[65];
    uint16_t bytesPerLine = ReadU16LE(data + 66);

    if (xmax < xmin || ymax < ymin) return false;
    uint32_t width  = static_cast<uint32_t>(xmax - xmin + 1);
    uint32_t height = static_cast<uint32_t>(ymax - ymin + 1);
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (bytesPerLine == 0 || numPlanes == 0 || numPlanes > 4) return false;

    const uint8_t* vgaPalette = (size >= 769 && data[size - 769] == 0x0C) ? (data + size - 768) : nullptr;
    const uint8_t* egaPalette = data + 16;

    size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.channels = numPlanes;
    outInfo.bitsPerChannel = bpp;
    outInfo.formatName = L"ZSoft PCX (" + std::to_wstring(bpp * numPlanes) + L"-bit)";
    outInfo.bgraPixels.resize(totalPixels * 4, 255);

    size_t scanlineBytes = static_cast<size_t>(bytesPerLine) * numPlanes;
    std::vector<uint8_t> lineBuf(scanlineBytes, 0);
    size_t pos = 128;

    for (uint32_t y = 0; y < height; ++y) {
        size_t lx = 0;
        while (lx < scanlineBytes && pos < size) {
            uint8_t b = data[pos++];
            uint32_t count = 1;
            uint8_t val = b;
            if ((b & 0xC0) == 0xC0) {
                count = b & 0x3F;
                if (pos >= size) break;
                val = data[pos++];
            }
            for (uint32_t k = 0; k < count && lx < scanlineBytes; ++k, ++lx) {
                lineBuf[lx] = val;
            }
        }

        uint8_t* dstRow = outInfo.bgraPixels.data() + static_cast<size_t>(y) * width * 4;
        if (bpp == 8 && numPlanes >= 3) {
            for (uint32_t x = 0; x < width && x < bytesPerLine; ++x) {
                uint8_t r = lineBuf[x];
                uint8_t g = lineBuf[bytesPerLine + x];
                uint8_t b = lineBuf[bytesPerLine * 2 + x];
                uint8_t a = (numPlanes == 4) ? lineBuf[bytesPerLine * 3 + x] : 255;
                WritePremulBGRA(dstRow + x * 4, r, g, b, a);
            }
        } else if (bpp == 8 && numPlanes == 1) {
            for (uint32_t x = 0; x < width && x < bytesPerLine; ++x) {
                uint8_t idx = lineBuf[x];
                if (vgaPalette) {
                    WritePremulBGRA(dstRow + x * 4, vgaPalette[idx * 3], vgaPalette[idx * 3 + 1], vgaPalette[idx * 3 + 2], 255);
                } else {
                    WritePremulBGRA(dstRow + x * 4, idx, idx, idx, 255);
                }
            }
        } else if (bpp == 1 && numPlanes == 1) {
            for (uint32_t x = 0; x < width; ++x) {
                bool bit = (lineBuf[x >> 3] & (0x80 >> (x & 7))) != 0;
                uint8_t c = bit ? 255 : 0;
                WritePremulBGRA(dstRow + x * 4, c, c, c, 255);
            }
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                uint8_t idx = lineBuf[x % scanlineBytes] & 0x0F;
                WritePremulBGRA(dstRow + x * 4, egaPalette[idx * 3], egaPalette[idx * 3 + 1], egaPalette[idx * 3 + 2], 255);
            }
        }
    }

    outInfo.valid = true;
    return true;
}

