#pragma once
// ============================================================================
// 1. TRUEVISION TGA (.tga, .icb, .vda, .vst) — Uncompressed & RLE (8/15/16/24/32-bit)
// ============================================================================
bool DecodeTGA(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 18) return false;

    const uint8_t  idLen        = data[0];
    const uint8_t  colorMapType = data[1];
    const uint8_t  imageType    = data[2];
    const uint16_t cmapFirst    = ReadU16LE(data + 3);
    const uint16_t cmapLen      = ReadU16LE(data + 5);
    const uint8_t  cmapDepth    = data[7];
    const uint16_t width        = ReadU16LE(data + 12);
    const uint16_t height       = ReadU16LE(data + 14);
    const uint8_t  bpp          = data[16];
    const uint8_t  desc         = data[17];
    const uint8_t  alphaBits    = desc & 0x0Fu;

    const bool isRle = (imageType == 9 || imageType == 10 || imageType == 11);
    const uint8_t baseType = isRle ? static_cast<uint8_t>(imageType - 8) : imageType;
    if (baseType != 1 && baseType != 2 && baseType != 3) return false;
    if (colorMapType > 1) return false;
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false;

    size_t offset = 18u + idLen;
    if (offset > size) return false;

    // Color map palette (if present)
    std::vector<uint8_t> paletteRgba(256 * 4, 255);
    bool cmapHasNonZeroAlpha = false;
    if (colorMapType == 1 && cmapLen > 0) {
        const size_t cmapBytesPerEntry = (cmapDepth == 15 || cmapDepth == 16) ? 2u : (cmapDepth / 8u);
        if ((cmapBytesPerEntry != 2u && cmapBytesPerEntry != 3u && cmapBytesPerEntry != 4u) ||
            offset + static_cast<size_t>(cmapLen) * cmapBytesPerEntry > size)
        {
            return false;
        }
        for (uint16_t i = 0; i < cmapLen; ++i) {
            const uint32_t idx = cmapFirst + i;
            if (idx < 256u) {
                const uint8_t* cp = data + offset + static_cast<size_t>(i) * cmapBytesPerEntry;
                uint8_t r = 0, g = 0, b = 0, a = 255u;
                if (cmapBytesPerEntry == 2u) {
                    const uint16_t v = ReadU16LE(cp);
                    b = static_cast<uint8_t>(((v & 0x1Fu) * 255u) / 31u);
                    g = static_cast<uint8_t>((((v >> 5u) & 0x1Fu) * 255u) / 31u);
                    r = static_cast<uint8_t>((((v >> 10u) & 0x1Fu) * 255u) / 31u);
                    if (cmapDepth == 16u && (v & 0x8000u) != 0u) cmapHasNonZeroAlpha = true;
                    a = (cmapDepth == 16u) ? ((v & 0x8000u) ? 255u : 0u) : 255u;
                } else {
                    b = cp[0]; g = cp[1]; r = cp[2];
                    if (cmapBytesPerEntry == 4u) {
                        a = cp[3];
                        if (a != 0u) cmapHasNonZeroAlpha = true;
                    }
                }
                paletteRgba[idx * 4u + 0u] = r;
                paletteRgba[idx * 4u + 1u] = g;
                paletteRgba[idx * 4u + 2u] = b;
                paletteRgba[idx * 4u + 3u] = a;
            }
        }
        if ((cmapDepth == 16u || cmapDepth == 32u) && !cmapHasNonZeroAlpha) {
            for (size_t i = 0; i < 256u; ++i) paletteRgba[i * 4u + 3u] = 255u;
        }
        offset += static_cast<size_t>(cmapLen) * cmapBytesPerEntry;
    } else if (baseType == 1) {
        return false; // Color-mapped TGA requires a colormap
    }

    const size_t bytesPerPixel = (bpp == 15) ? 2u : (bpp / 8u);
    const size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.origWidth = width;
    outInfo.origHeight = height;
    outInfo.channels = static_cast<uint32_t>(bytesPerPixel);
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = isRle
        ? (L"TGA " + std::to_wstring(bpp) + L"-bit RLE")
        : (L"TGA " + std::to_wstring(bpp) + L"-bit");

    const bool topToBottom = (desc & 0x20u) != 0u;
    const bool rightToLeft = (desc & 0x10u) != 0u;

    // Decode into straight RGBA first so we can inspect attribute/alpha channel usage cleanly
    std::vector<uint8_t> straightRgba(totalPixels * 4u, 0u);
    bool anyNonZeroAlpha = false;
    bool anyAttrBit16Set = false;

    auto decodePixelRgba = [&](const uint8_t* src, uint8_t dstRgba[4]) {
        if (baseType == 1) {
            const uint8_t idx = src[0];
            std::memcpy(dstRgba, &paletteRgba[idx * 4u], 4);
            if (dstRgba[3] != 0u) anyNonZeroAlpha = true;
        } else if (baseType == 3) {
            const uint8_t gray = src[0];
            const uint8_t a = (bytesPerPixel == 2u) ? src[1] : 255u;
            dstRgba[0] = dstRgba[1] = dstRgba[2] = gray;
            dstRgba[3] = a;
            if (a != 0u) anyNonZeroAlpha = true;
        } else if (bytesPerPixel == 2u) {
            const uint16_t v = ReadU16LE(src);
            dstRgba[0] = static_cast<uint8_t>((((v >> 10u) & 0x1Fu) * 255u) / 31u);
            dstRgba[1] = static_cast<uint8_t>((((v >>  5u) & 0x1Fu) * 255u) / 31u);
            dstRgba[2] = static_cast<uint8_t>((( v         & 0x1Fu) * 255u) / 31u);
            if ((v & 0x8000u) != 0u) {
                anyAttrBit16Set = true;
                anyNonZeroAlpha = true;
            }
            dstRgba[3] = (bpp == 16 && alphaBits == 1u) ? ((v & 0x8000u) ? 255u : 0u) : 255u;
        } else if (bytesPerPixel == 3u) {
            dstRgba[0] = src[2];
            dstRgba[1] = src[1];
            dstRgba[2] = src[0];
            dstRgba[3] = 255u;
            anyNonZeroAlpha = true;
        } else { // 32-bit BGRX / BGRA
            dstRgba[0] = src[2];
            dstRgba[1] = src[1];
            dstRgba[2] = src[0];
            dstRgba[3] = src[3];
            if (src[3] != 0u) anyNonZeroAlpha = true;
        }
    };

    auto getDstRgba = [&](size_t linearIdx) -> uint8_t* {
        const uint32_t row  = static_cast<uint32_t>(linearIdx / width);
        const uint32_t col  = static_cast<uint32_t>(linearIdx % width);
        const uint32_t outY = topToBottom ? row : (height - 1u - row);
        const uint32_t outX = rightToLeft ? (width - 1u - col) : col;
        return straightRgba.data() + (static_cast<size_t>(outY) * width + outX) * 4u;
    };

    if (!isRle) {
        if (offset + totalPixels * bytesPerPixel > size) return false;
        for (size_t i = 0; i < totalPixels; ++i) {
            decodePixelRgba(data + offset + i * bytesPerPixel, getDstRgba(i));
        }
    } else {
        size_t pixIdx = 0;
        while (pixIdx < totalPixels && offset < size) {
            const uint8_t packetHdr = data[offset++];
            const uint32_t count = (packetHdr & 0x7Fu) + 1u;
            if (packetHdr & 0x80u) {
                if (offset + bytesPerPixel > size) break;
                uint8_t pxRgba[4] = {};
                decodePixelRgba(data + offset, pxRgba);
                offset += bytesPerPixel;
                for (uint32_t k = 0; k < count && pixIdx < totalPixels; ++k, ++pixIdx) {
                    std::memcpy(getDstRgba(pixIdx), pxRgba, 4);
                }
            } else {
                for (uint32_t k = 0; k < count && pixIdx < totalPixels; ++k, ++pixIdx) {
                    if (offset + bytesPerPixel > size) break;
                    decodePixelRgba(data + offset, getDstRgba(pixIdx));
                    offset += bytesPerPixel;
                }
            }
        }
        if (pixIdx == 0) return false;
    }

    // Standard TGA attribute-bit semantics:
    // - 32-bit BGRX with (desc & 0x0F) == 0: force opaque (a = 255) unless any pixel actually stores non-zero alpha.
    // - 16-bit ARGB1555 with (desc & 0x0F) == 1: honour bit 15 if used, or force opaque if all pixels cleared bit 15.
    const bool forceOpaque =
        (baseType == 2 && bytesPerPixel == 4u && alphaBits == 0u && !anyNonZeroAlpha) ||
        (baseType == 2 && bpp == 16u && alphaBits == 1u && !anyAttrBit16Set);

    outInfo.bgraPixels.resize(totalPixels * 4u);
    for (size_t i = 0; i < totalPixels; ++i) {
        const uint8_t* sp = &straightRgba[i * 4u];
        const uint8_t a = forceOpaque ? 255u : sp[3];
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], sp[0], sp[1], sp[2], a);
    }

    outInfo.valid = true;
    return true;
}
