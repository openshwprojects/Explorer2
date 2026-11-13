#pragma once

// ============================================================================
// 7. DirectDraw Surface (.dds) — Uncompressed RGBA/BGRA/RGB/L8 + DXT1/DXT3/DXT5
// ============================================================================
bool DecodeDDS(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 128 || std::memcmp(data, "DDS ", 4) != 0) return false;
    uint32_t hdrSize = ReadU32BE(data + 4);
    (void)hdrSize;
    auto readU32LE = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    };
    if (readU32LE(data + 4) != 124) return false;

    uint32_t height = readU32LE(data + 12);
    uint32_t width  = readU32LE(data + 16);
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;

    uint32_t pfFlags = readU32LE(data + 80);
    uint32_t fourCC  = readU32LE(data + 84);
    uint32_t rgbBits = readU32LE(data + 88);
    uint32_t rMask   = readU32LE(data + 92);
    uint32_t gMask   = readU32LE(data + 96);
    uint32_t bMask   = readU32LE(data + 100);
    uint32_t aMask   = readU32LE(data + 104);

    size_t offset = 128;
    uint32_t dxgiFormat = 0;
    if ((pfFlags & 0x4) && fourCC == 0x30315844 /* 'DX10' */) {
        if (size < 148) return false;
        dxgiFormat = readU32LE(data + 128);
        offset = 148;
        if (dxgiFormat >= 70 && dxgiFormat <= 72) fourCC = 0x31545844;      // BC1 -> DXT1
        else if (dxgiFormat >= 73 && dxgiFormat <= 75) fourCC = 0x33545844; // BC2 -> DXT3
        else if (dxgiFormat >= 76 && dxgiFormat <= 78) fourCC = 0x35545844; // BC3 -> DXT5
        else if (dxgiFormat >= 27 && dxgiFormat <= 32) {
            pfFlags &= ~0x4u; rgbBits = 32; rMask = 0x000000FFu; aMask = 0xFF000000u;
        } else if (dxgiFormat >= 87 && dxgiFormat <= 91) {
            pfFlags &= ~0x4u; rgbBits = 32; rMask = 0x00FF0000u; aMask = (dxgiFormat == 88) ? 0u : 0xFF000000u;
        } else if (dxgiFormat >= 60 && dxgiFormat <= 62) {
            pfFlags &= ~0x4u; rgbBits = 8;
        }
    }

    outInfo.width = width;
    outInfo.height = height;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.bgraPixels.assign(static_cast<size_t>(width) * height * 4, 0);

    auto unpack565 = [](uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
        r = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
        g = static_cast<uint8_t>(((c >> 5)  & 63) * 255 / 63);
        b = static_cast<uint8_t>((c         & 31) * 255 / 31);
    };

    // FourCC compressed (DXT1, DXT3, DXT5)
    if (pfFlags & 0x4 /* DDPF_FOURCC */) {
        bool isDxt1 = (fourCC == 0x31545844); // 'DXT1'
        bool isDxt3 = (fourCC == 0x33545844); // 'DXT3'
        bool isDxt5 = (fourCC == 0x35545844); // 'DXT5'
        if (!isDxt1 && !isDxt3 && !isDxt5) return false;

        outInfo.formatName = isDxt1 ? L"DDS (DXT1 / BC1)" : (isDxt3 ? L"DDS (DXT3 / BC2)" : L"DDS (DXT5 / BC3)");
        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;
        size_t blockBytes = isDxt1 ? 8 : 16;
        if (offset + static_cast<size_t>(blocksX) * blocksY * blockBytes > size) return false;

        const uint8_t* ptr = data + offset;
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t alphas[16];
                std::memset(alphas, 255, 16);
                const uint8_t* colorBlock = ptr;

                if (isDxt3) {
                    for (int i = 0; i < 8; ++i) {
                        uint8_t byteVal = ptr[i];
                        alphas[i * 2 + 0] = static_cast<uint8_t>((byteVal & 0x0F) * 17);
                        alphas[i * 2 + 1] = static_cast<uint8_t>((byteVal >> 4) * 17);
                    }
                    colorBlock = ptr + 8;
                } else if (isDxt5) {
                    uint8_t a0 = ptr[0];
                    uint8_t a1 = ptr[1];
                    uint8_t aLut[8];
                    aLut[0] = a0;
                    aLut[1] = a1;
                    if (a0 > a1) {
                        for (int i = 1; i <= 6; ++i) aLut[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                    } else {
                        for (int i = 1; i <= 4; ++i) aLut[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                        aLut[6] = 0;
                        aLut[7] = 255;
                    }
                    uint64_t aBits = 0;
                    for (int i = 0; i < 6; ++i) aBits |= (static_cast<uint64_t>(ptr[2 + i]) << (8 * i));
                    for (int i = 0; i < 16; ++i) {
                        alphas[i] = aLut[(aBits >> (3 * i)) & 7];
                    }
                    colorBlock = ptr + 8;
                }

                uint16_t c0 = ReadU16LE(colorBlock);
                uint16_t c1 = ReadU16LE(colorBlock + 2);
                uint32_t cBits = readU32LE(colorBlock + 4);

                uint8_t r[4], g[4], b[4], a[4] = { 255, 255, 255, 255 };
                unpack565(c0, r[0], g[0], b[0]);
                unpack565(c1, r[1], g[1], b[1]);
                if (c0 > c1 || !isDxt1) {
                    r[2] = static_cast<uint8_t>((2 * r[0] + r[1]) / 3);
                    g[2] = static_cast<uint8_t>((2 * g[0] + g[1]) / 3);
                    b[2] = static_cast<uint8_t>((2 * b[0] + b[1]) / 3);
                    r[3] = static_cast<uint8_t>((r[0] + 2 * r[1]) / 3);
                    g[3] = static_cast<uint8_t>((g[0] + 2 * g[1]) / 3);
                    b[3] = static_cast<uint8_t>((b[0] + 2 * b[1]) / 3);
                } else {
                    r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2);
                    g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2);
                    b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2);
                    r[3] = g[3] = b[3] = a[3] = 0;
                }

                for (uint32_t py = 0; py < 4; ++py) {
                    uint32_t dstY = by * 4 + py;
                    if (dstY >= height) continue;
                    for (uint32_t px = 0; px < 4; ++px) {
                        uint32_t dstX = bx * 4 + px;
                        if (dstX >= width) continue;
                        uint32_t idx = py * 4 + px;
                        uint32_t code = (cBits >> (2 * idx)) & 3;
                        uint8_t finalA = isDxt1 ? a[code] : alphas[idx];
                        WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(dstY) * width + dstX) * 4], r[code], g[code], b[code], finalA);
                    }
                }
                ptr += blockBytes;
            }
        }
        outInfo.valid = true;
        return true;
    }

    // Uncompressed RGB / RGBA / Luminance
    uint32_t bytesPerPx = (rgbBits >= 8) ? (rgbBits / 8) : 0;
    if (bytesPerPx == 0 || bytesPerPx > 4) return false;
    if (offset + static_cast<size_t>(width) * height * bytesPerPx > size) return false;

    outInfo.formatName = L"DDS (Uncompressed " + std::to_wstring(rgbBits) + L"-bit)";
    const uint8_t* ptr = data + offset;
    bool swapRB = (rMask == 0x000000FF);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (bytesPerPx == 4) {
            b = swapRB ? ptr[2] : ptr[0];
            g = ptr[1];
            r = swapRB ? ptr[0] : ptr[2];
            a = (aMask != 0) ? ptr[3] : 255;
        } else if (bytesPerPx == 3) {
            b = swapRB ? ptr[2] : ptr[0];
            g = ptr[1];
            r = swapRB ? ptr[0] : ptr[2];
        } else if (bytesPerPx == 2) {
            uint16_t v16 = ReadU16LE(ptr);
            if (gMask == 0x07E0) {
                unpack565(v16, r, g, b);
            } else {
                r = static_cast<uint8_t>(((v16 >> 10) & 31) * 255 / 31);
                g = static_cast<uint8_t>(((v16 >> 5)  & 31) * 255 / 31);
                b = static_cast<uint8_t>((v16         & 31) * 255 / 31);
                if (aMask != 0) a = (v16 & 0x8000) ? 255 : 0;
            }
        } else if (bytesPerPx == 1) {
            r = g = b = ptr[0];
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        ptr += bytesPerPx;
    }
    (void)gMask; (void)bMask;
    outInfo.valid = true;
    return true;
}

