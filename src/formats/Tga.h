#pragma once
// ============================================================================
// 1. TRUEVISION TGA (.tga, .icb, .vda, .vst) — Uncompressed & RLE (8/16/24/32-bit)
// ============================================================================
bool DecodeTGA(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 18) return false;

    uint8_t idLen = data[0];
    uint8_t colorMapType = data[1];
    uint8_t imageType = data[2];
    uint16_t cmapFirst = ReadU16LE(data + 3);
    uint16_t cmapLen   = ReadU16LE(data + 5);
    uint8_t  cmapDepth = data[7];
    uint16_t width     = ReadU16LE(data + 12);
    uint16_t height    = ReadU16LE(data + 14);
    uint8_t  bpp       = data[16];
    uint8_t  desc      = data[17];

    bool isRle = (imageType == 9 || imageType == 10 || imageType == 11);
    uint8_t baseType = isRle ? (imageType - 8) : imageType;
    if (baseType != 1 && baseType != 2 && baseType != 3) return false;
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false;

    size_t offset = 18 + idLen;
    if (offset > size) return false;

    // Color map palette (if present)
    std::vector<uint8_t> paletteBgra(256 * 4, 255);
    if (colorMapType == 1 && cmapLen > 0) {
        size_t cmapBytesPerEntry = (cmapDepth == 15) ? 2 : (cmapDepth / 8);
        if (cmapBytesPerEntry == 0 || offset + static_cast<size_t>(cmapLen) * cmapBytesPerEntry > size) {
            return false;
        }
        for (uint16_t i = 0; i < cmapLen; ++i) {
            uint32_t idx = cmapFirst + i;
            if (idx < 256) {
                const uint8_t* cp = data + offset + static_cast<size_t>(i) * cmapBytesPerEntry;
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (cmapBytesPerEntry == 2) {
                    uint16_t v = ReadU16LE(cp);
                    b = static_cast<uint8_t>(((v & 0x1F) * 255) / 31);
                    g = static_cast<uint8_t>((((v >> 5) & 0x1F) * 255) / 31);
                    r = static_cast<uint8_t>((((v >> 10) & 0x1F) * 255) / 31);
                } else if (cmapBytesPerEntry >= 3) {
                    b = cp[0]; g = cp[1]; r = cp[2];
                    if (cmapBytesPerEntry == 4 && cp[3] != 0) a = cp[3];
                }
                WritePremulBGRA(&paletteBgra[idx * 4], r, g, b, a);
            }
        }
        offset += static_cast<size_t>(cmapLen) * cmapBytesPerEntry;
    }

    size_t bytesPerPixel = (bpp == 15) ? 2 : (bpp / 8);
    size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.channels = static_cast<uint32_t>(bytesPerPixel);
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = isRle
        ? (L"TGA " + std::to_wstring(bpp) + L"-bit RLE")
        : (L"TGA " + std::to_wstring(bpp) + L"-bit");
    outInfo.bgraPixels.resize(totalPixels * 4, 0);

    bool topToBottom = (desc & 0x20) != 0;
    bool rightToLeft = (desc & 0x10) != 0;

    auto decodeSinglePixel = [&](const uint8_t* src, uint8_t* dstBgra) {
        if (baseType == 1) {
            uint8_t idx = src[0];
            std::memcpy(dstBgra, &paletteBgra[idx * 4], 4);
        } else if (baseType == 3) {
            uint8_t gray = src[0];
            uint8_t a = (bytesPerPixel == 2) ? src[1] : 255;
            WritePremulBGRA(dstBgra, gray, gray, gray, a);
        } else if (bytesPerPixel == 2) {
            uint16_t v = ReadU16LE(src);
            uint8_t b = static_cast<uint8_t>(((v & 0x1F) * 255) / 31);
            uint8_t g = static_cast<uint8_t>((((v >> 5) & 0x1F) * 255) / 31);
            uint8_t r = static_cast<uint8_t>((((v >> 10) & 0x1F) * 255) / 31);
            WritePremulBGRA(dstBgra, r, g, b, 255);
        } else if (bytesPerPixel == 3) {
            WritePremulBGRA(dstBgra, src[2], src[1], src[0], 255);
        } else if (bytesPerPixel == 4) {
            WritePremulBGRA(dstBgra, src[2], src[1], src[0], src[3]);
        }
    };

    auto getDstPixel = [&](size_t linearIdx) -> uint8_t* {
        uint32_t row = static_cast<uint32_t>(linearIdx / width);
        uint32_t col = static_cast<uint32_t>(linearIdx % width);
        uint32_t outY = topToBottom ? row : (height - 1 - row);
        uint32_t outX = rightToLeft ? (width - 1 - col) : col;
        return outInfo.bgraPixels.data() + (static_cast<size_t>(outY) * width + outX) * 4;
    };

    if (!isRle) {
        if (offset + totalPixels * bytesPerPixel > size) return false;
        for (size_t i = 0; i < totalPixels; ++i) {
            decodeSinglePixel(data + offset + i * bytesPerPixel, getDstPixel(i));
        }
    } else {
        size_t pixIdx = 0;
        while (pixIdx < totalPixels && offset < size) {
            uint8_t packetHdr = data[offset++];
            uint32_t count = (packetHdr & 0x7F) + 1;
            if (packetHdr & 0x80) {
                if (offset + bytesPerPixel > size) break;
                uint8_t pxBgra[4] = {};
                decodeSinglePixel(data + offset, pxBgra);
                offset += bytesPerPixel;
                for (uint32_t k = 0; k < count && pixIdx < totalPixels; ++k, ++pixIdx) {
                    std::memcpy(getDstPixel(pixIdx), pxBgra, 4);
                }
            } else {
                for (uint32_t k = 0; k < count && pixIdx < totalPixels; ++k, ++pixIdx) {
                    if (offset + bytesPerPixel > size) break;
                    decodeSinglePixel(data + offset, getDstPixel(pixIdx));
                    offset += bytesPerPixel;
                }
            }
        }
        if (pixIdx == 0) return false;
    }

    outInfo.valid = true;
    return true;
}

