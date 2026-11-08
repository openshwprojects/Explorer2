#include "ImageFormatLoader.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace Explorer2 {

namespace {

constexpr uint32_t kMaxRasterDimension = 16384;

inline uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint16_t ReadU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline int16_t ReadI16BE(const uint8_t* p) {
    return static_cast<int16_t>(ReadU16BE(p));
}

inline uint32_t ReadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline int32_t ReadI32BE(const uint8_t* p) {
    return static_cast<int32_t>(ReadU32BE(p));
}

inline uint64_t ReadU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(ReadU32BE(p)) << 32) | ReadU32BE(p + 4);
}

inline void WritePremulBGRA(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 255) {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = 255;
    } else if (a == 0) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
    } else {
        dst[0] = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
        dst[1] = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
        dst[2] = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
        dst[3] = a;
    }
}

void DownscaleIfRequested(uint32_t maxDimension, ExtendedImageInfo& info) {
    info.origWidth = info.width;
    info.origHeight = info.height;
    if (maxDimension == 0 || (info.width <= maxDimension && info.height <= maxDimension)) {
        return;
    }
    if (info.width == 0 || info.height == 0 || info.bgraPixels.size() < static_cast<size_t>(info.width) * info.height * 4) {
        return;
    }

    uint32_t dstW = info.width;
    uint32_t dstH = info.height;
    if (info.width >= info.height) {
        dstW = maxDimension;
        dstH = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(info.height) * maxDimension) / info.width));
    } else {
        dstH = maxDimension;
        dstW = std::max(1u, static_cast<uint32_t>((static_cast<uint64_t>(info.width) * maxDimension) / info.height));
    }

    std::vector<uint8_t> scaled(static_cast<size_t>(dstW) * dstH * 4);
    for (uint32_t y = 0; y < dstH; ++y) {
        uint32_t srcY = std::min(info.height - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * info.height) / dstH));
        const uint8_t* srcRow = info.bgraPixels.data() + static_cast<size_t>(srcY) * info.width * 4;
        uint8_t* dstRow = scaled.data() + static_cast<size_t>(y) * dstW * 4;
        for (uint32_t x = 0; x < dstW; ++x) {
            uint32_t srcX = std::min(info.width - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * info.width) / dstW));
            const uint8_t* sp = srcRow + srcX * 4;
            uint8_t* dp = dstRow + x * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    info.width = dstW;
    info.height = dstH;
    info.bgraPixels = std::move(scaled);
}

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

// ============================================================================
// 2. ADOBE PHOTOSHOP PSD / PSB (.psd, .psb) — Layers Metadata + Merged Composite
// ============================================================================
bool DecodePSD(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 26 || std::memcmp(data, "8BPS", 4) != 0) return false;

    uint16_t version   = ReadU16BE(data + 4);
    if (version != 1 && version != 2) return false;
    bool isPsb = (version == 2);

    uint16_t channels  = ReadU16BE(data + 12);
    uint32_t height    = ReadU32BE(data + 14);
    uint32_t width     = ReadU32BE(data + 18);
    uint16_t depth     = ReadU16BE(data + 22);
    uint16_t colorMode = ReadU16BE(data + 24);

    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (channels == 0 || channels > 64) return false;
    if (depth != 8 && depth != 16 && depth != 1) return false;

    size_t pos = 26;
    if (pos + 4 > size) return false;

    // 1. Color Mode Data Section
    uint32_t colorModeLen = ReadU32BE(data + pos);
    pos += 4;
    if (pos + colorModeLen > size) return false;
    const uint8_t* indexedPalette = (colorMode == 2 && colorModeLen >= 768) ? (data + pos) : nullptr;
    pos += colorModeLen;

    // 2. Image Resources Section
    if (pos + 4 > size) return false;
    uint32_t imgResLen = ReadU32BE(data + pos);
    pos += 4;
    if (pos + imgResLen > size) return false;
    pos += imgResLen;

    // 3. Layer and Mask Information Section (extract layer names!)
    if (pos + (isPsb ? 8u : 4u) > size) return false;
    uint64_t layerMaskLen = isPsb ? ReadU64BE(data + pos) : ReadU32BE(data + pos);
    pos += (isPsb ? 8 : 4);
    size_t compositePos = pos + static_cast<size_t>(layerMaskLen);
    if (compositePos > size) return false;

    if (layerMaskLen >= (isPsb ? 10u : 6u)) {
        size_t lpos = pos + (isPsb ? 8 : 4); // skip Layer info length
        if (lpos + 2 <= compositePos) {
            int16_t rawLayerCount = ReadI16BE(data + lpos);
            lpos += 2;
            int layerCount = std::abs(static_cast<int>(rawLayerCount));
            for (int i = 0; i < layerCount && i < 256 && lpos + 18 <= compositePos; ++i) {
                lpos += 16; // top, left, bottom, right (4 * int32)
                uint16_t numCh = ReadU16BE(data + lpos);
                lpos += 2;
                size_t chEntryLen = isPsb ? 10 : 6;
                if (lpos + numCh * chEntryLen + 12 > compositePos) break;
                lpos += numCh * chEntryLen;
                if (std::memcmp(data + lpos, "8BIM", 4) != 0) break;
                lpos += 4; // '8BIM'
                lpos += 4; // blend mode key
                lpos += 4; // opacity, clipping, flags, filler
                uint32_t extraLen = ReadU32BE(data + lpos);
                lpos += 4;
                size_t nextLayerPos = lpos + extraLen;
                if (nextLayerPos > compositePos) break;

                // Inside extra data: Layer mask data (uint32 len) + Layer blending ranges (uint32 len) + Pascal name
                size_t epos = lpos;
                if (epos + 4 <= nextLayerPos) {
                    uint32_t maskDataLen = ReadU32BE(data + epos);
                    epos += 4 + maskDataLen;
                    if (epos + 4 <= nextLayerPos) {
                        uint32_t blendRangesLen = ReadU32BE(data + epos);
                        epos += 4 + blendRangesLen;
                        if (epos < nextLayerPos) {
                            uint8_t nameLen = data[epos];
                            if (epos + 1 + nameLen <= nextLayerPos && nameLen > 0) {
                                std::string lname(reinterpret_cast<const char*>(data + epos + 1), nameLen);
                                outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
                            }
                        }
                    }
                }
                lpos = nextLayerPos;
            }
        }
    }

    // 4. Merged Composite Image Data Section
    outInfo.width = width;
    outInfo.height = height;
    outInfo.channels = channels;
    outInfo.bitsPerChannel = depth;
    outInfo.formatName = isPsb ? L"Photoshop PSB" : L"Photoshop PSD";
    if (!outInfo.layerNames.empty()) {
        outInfo.formatName += L" (" + std::to_wstring(outInfo.layerNames.size()) + L" layers)";
    }

    size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.bgraPixels.assign(totalPixels * 4, 255);

    if (compositePos + 2 > size) {
        return false;
    }

    uint16_t compression = ReadU16BE(data + compositePos);
    size_t cpos = compositePos + 2;
    size_t bytesPerSample = (depth == 16) ? 2 : 1;
    size_t rowBytes = (depth == 1) ? ((width + 7) / 8) : (static_cast<size_t>(width) * bytesPerSample);
    uint32_t readChannels = std::min<uint32_t>(channels, 4u);

    std::vector<std::vector<uint8_t>> planar(readChannels, std::vector<uint8_t>(totalPixels, 255));

    if (compression == 0) {
        // Uncompressed planar data
        for (uint32_t ch = 0; ch < readChannels; ++ch) {
            for (uint32_t y = 0; y < height; ++y) {
                if (cpos + rowBytes > size) break;
                uint8_t* dstRow = planar[ch].data() + static_cast<size_t>(y) * width;
                if (depth == 8) {
                    std::memcpy(dstRow, data + cpos, width);
                } else if (depth == 16) {
                    for (uint32_t x = 0; x < width; ++x) {
                        dstRow[x] = data[cpos + x * 2]; // high byte of big-endian 16-bit sample
                    }
                } else if (depth == 1) {
                    for (uint32_t x = 0; x < width; ++x) {
                        bool bit = (data[cpos + (x >> 3)] & (0x80 >> (x & 7))) != 0;
                        dstRow[x] = bit ? 0 : 255;
                    }
                }
                cpos += rowBytes;
            }
        }
    } else if (compression == 1) {
        // PackBits RLE planar data
        size_t countEntrySize = isPsb ? 4 : 2;
        size_t totalScanlines = static_cast<size_t>(channels) * height;
        if (cpos + totalScanlines * countEntrySize > size) return false;
        size_t rleDataPos = cpos + totalScanlines * countEntrySize;

        for (uint32_t ch = 0; ch < channels; ++ch) {
            for (uint32_t y = 0; y < height; ++y) {
                size_t idx = static_cast<size_t>(ch) * height + y;
                uint32_t lineCompressedBytes = isPsb
                    ? ReadU32BE(data + cpos + idx * 4)
                    : ReadU16BE(data + cpos + idx * 2);
                if (rleDataPos + lineCompressedBytes > size) break;

                if (ch < readChannels) {
                    uint8_t* dstRow = planar[ch].data() + static_cast<size_t>(y) * width;
                    size_t rp = rleDataPos;
                    size_t rEnd = rleDataPos + lineCompressedBytes;
                    size_t outX = 0;
                    while (rp < rEnd && outX < rowBytes) {
                        int8_t hdr = static_cast<int8_t>(data[rp++]);
                        if (hdr >= 0) {
                            size_t count = static_cast<size_t>(hdr) + 1;
                            for (size_t k = 0; k < count && rp < rEnd && outX < rowBytes; ++k, ++rp, ++outX) {
                                if (depth == 8) dstRow[outX] = data[rp];
                                else if (depth == 16 && (outX & 1) == 0 && (outX >> 1) < width) dstRow[outX >> 1] = data[rp];
                            }
                        } else if (hdr != -128) {
                            size_t count = static_cast<size_t>(1 - hdr);
                            if (rp >= rEnd) break;
                            uint8_t val = data[rp++];
                            for (size_t k = 0; k < count && outX < rowBytes; ++k, ++outX) {
                                if (depth == 8) dstRow[outX] = val;
                                else if (depth == 16 && (outX & 1) == 0 && (outX >> 1) < width) dstRow[outX >> 1] = val;
                            }
                        }
                    }
                }
                rleDataPos += lineCompressedBytes;
            }
        }
    } else {
        return false;
    }

    // Interleave planar channels into PBGRA
    for (size_t i = 0; i < totalPixels; ++i) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (colorMode == 1 || colorMode == 0) {
            r = g = b = planar[0][i];
            if (readChannels >= 2) a = planar[1][i];
        } else if (colorMode == 2 && indexedPalette) {
            uint8_t idx = planar[0][i];
            r = indexedPalette[idx];
            g = indexedPalette[256 + idx];
            b = indexedPalette[512 + idx];
        } else if (colorMode == 4 && readChannels >= 4) {
            // Photoshop CMYK (0 = full ink, 255 = no ink)
            uint32_t c = planar[0][i], m = planar[1][i], y = planar[2][i], k = planar[3][i];
            r = static_cast<uint8_t>((c * k) / 255);
            g = static_cast<uint8_t>((m * k) / 255);
            b = static_cast<uint8_t>((y * k) / 255);
        } else {
            r = planar[0][i];
            g = (readChannels >= 2) ? planar[1][i] : r;
            b = (readChannels >= 3) ? planar[2][i] : r;
            a = (readChannels >= 4) ? planar[3][i] : 255;
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// 3. GIMP XCF (.xcf) — Canvas + Layer Hierarchy + 64x64 RLE Tile Compositor
// ============================================================================
bool DecodeXcfTileRle(
    const uint8_t* tileData,
    size_t tileBytes,
    uint32_t tileW,
    uint32_t tileH,
    uint32_t bpp,
    std::vector<uint8_t>& outInterleaved)
{
    size_t numPixels = static_cast<size_t>(tileW) * tileH;
    outInterleaved.assign(numPixels * bpp, 255);
    size_t pos = 0;

    for (uint32_t ch = 0; ch < bpp; ++ch) {
        size_t px = 0;
        while (px < numPixels && pos < tileBytes) {
            uint8_t n = tileData[pos++];
            if (n <= 126) {
                // Run of (n + 1) identical bytes
                size_t runLen = static_cast<size_t>(n) + 1;
                if (pos >= tileBytes) break;
                uint8_t val = tileData[pos++];
                for (size_t k = 0; k < runLen && px < numPixels; ++k, ++px) {
                    outInterleaved[px * bpp + ch] = val;
                }
            } else if (n == 127) {
                // Long run of identical bytes
                if (pos + 3 > tileBytes) break;
                size_t runLen = (static_cast<size_t>(tileData[pos]) << 8) | tileData[pos + 1];
                uint8_t val = tileData[pos + 2];
                pos += 3;
                for (size_t k = 0; k < runLen && px < numPixels; ++k, ++px) {
                    outInterleaved[px * bpp + ch] = val;
                }
            } else if (n == 128) {
                // Long literal sequence
                if (pos + 2 > tileBytes) break;
                size_t litLen = (static_cast<size_t>(tileData[pos]) << 8) | tileData[pos + 1];
                pos += 2;
                for (size_t k = 0; k < litLen && px < numPixels && pos < tileBytes; ++k, ++px, ++pos) {
                    outInterleaved[px * bpp + ch] = tileData[pos];
                }
            } else {
                // Short literal sequence of (256 - n) bytes
                size_t litLen = 256 - static_cast<size_t>(n);
                for (size_t k = 0; k < litLen && px < numPixels && pos < tileBytes; ++k, ++px, ++pos) {
                    outInterleaved[px * bpp + ch] = tileData[pos];
                }
            }
        }
    }
    return true;
}

bool DecodeXCF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 26 || std::memcmp(data, "gimp xcf ", 9) != 0) return false;

    int xcfVer = 0;
    if (std::memcmp(data + 9, "file", 4) == 0) {
        xcfVer = 0;
    } else if (data[9] == 'v') {
        xcfVer = std::atoi(reinterpret_cast<const char*>(data + 10));
    }
    bool use64BitOffsets = (xcfVer >= 11);

    size_t pos = 14;
    if (pos + 12 > size) return false;
    uint32_t canvasW = ReadU32BE(data + pos);
    uint32_t canvasH = ReadU32BE(data + pos + 4);
    uint32_t baseType = ReadU32BE(data + pos + 8);
    pos += 12;

    if (canvasW == 0 || canvasH == 0 || canvasW > kMaxRasterDimension || canvasH > kMaxRasterDimension) return false;
    if (xcfVer >= 4) {
        if (pos + 4 > size) return false;
        pos += 4; // precision field in XCF v004+
    }

    uint8_t compression = 1; // 0 = None, 1 = RLE
    // Read image property list until PROP_END (0)
    while (pos + 8 <= size) {
        uint32_t propType = ReadU32BE(data + pos);
        uint32_t propLen  = ReadU32BE(data + pos + 4);
        pos += 8;
        if (pos + propLen > size) return false;
        if (propType == 17 && propLen >= 1) { // PROP_COMPRESSION
            compression = data[pos];
        }
        pos += propLen;
        if (propType == 0) break;
    }

    auto readOffset = [&](size_t& p) -> uint64_t {
        if (use64BitOffsets) {
            if (p + 8 > size) return 0;
            uint64_t v = ReadU64BE(data + p);
            p += 8;
            return v;
        } else {
            if (p + 4 > size) return 0;
            uint64_t v = ReadU32BE(data + p);
            p += 4;
            return v;
        }
    };

    std::vector<uint64_t> layerOffsets;
    while (pos < size) {
        uint64_t loff = readOffset(pos);
        if (loff == 0) break;
        if (loff < size) layerOffsets.push_back(loff);
        if (layerOffsets.size() >= 256) break;
    }

    outInfo.width = canvasW;
    outInfo.height = canvasH;
    outInfo.channels = (baseType == 1) ? 2 : 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"GIMP XCF v" + std::to_wstring(xcfVer);

    size_t totalCanvasPixels = static_cast<size_t>(canvasW) * canvasH;
    // Initialize canvas with subtle checkerboard / white base
    outInfo.bgraPixels.assign(totalCanvasPixels * 4, 0);

    // Composite layers from bottom to top (XCF stores top-most layer first in layerOffsets)
    for (int li = static_cast<int>(layerOffsets.size()) - 1; li >= 0; --li) {
        size_t lp = static_cast<size_t>(layerOffsets[li]);
        if (lp + 16 > size) continue;
        uint32_t lw = ReadU32BE(data + lp);
        uint32_t lh = ReadU32BE(data + lp + 4);
        uint32_t ltype = ReadU32BE(data + lp + 8);
        uint32_t nameLen = ReadU32BE(data + lp + 12);
        lp += 16;
        if (lp + nameLen > size) continue;
        if (nameLen > 1) {
            std::string lname(reinterpret_cast<const char*>(data + lp), nameLen - 1);
            outInfo.layerNames.push_back(PathUtils::Utf8ToWide(lname));
        }
        lp += nameLen;

        int32_t offX = 0, offY = 0;
        uint32_t opacity = 255;
        bool visible = true;
        while (lp + 8 <= size) {
            uint32_t pType = ReadU32BE(data + lp);
            uint32_t pLen  = ReadU32BE(data + lp + 4);
            lp += 8;
            if (lp + pLen > size) break;
            if (pType == 6 && pLen >= 4) { // PROP_OPACITY
                opacity = std::min<uint32_t>(255u, ReadU32BE(data + lp));
            } else if (pType == 8 && pLen >= 4) { // PROP_VISIBLE
                visible = (ReadU32BE(data + lp) != 0);
            } else if (pType == 15 && pLen >= 8) { // PROP_OFFSETS
                offX = ReadI32BE(data + lp);
                offY = ReadI32BE(data + lp + 4);
            }
            lp += pLen;
            if (pType == 0) break;
        }
        if (!visible || lw == 0 || lh == 0 || lw > kMaxRasterDimension || lh > kMaxRasterDimension) continue;

        uint64_t hierOff = readOffset(lp);
        if (hierOff == 0 || hierOff + 12 > size) continue;
        size_t hp = static_cast<size_t>(hierOff);
        uint32_t hw = ReadU32BE(data + hp);
        uint32_t hh = ReadU32BE(data + hp + 4);
        uint32_t bpp = ReadU32BE(data + hp + 8);
        hp += 12;
        if (bpp == 0 || bpp > 4 || hw != lw || hh != lh) continue;

        uint64_t levelOff = readOffset(hp);
        if (levelOff == 0 || levelOff + 8 > size) continue;
        size_t lvp = static_cast<size_t>(levelOff);
        uint32_t lvw = ReadU32BE(data + lvp);
        uint32_t lvh = ReadU32BE(data + lvp + 4);
        lvp += 8;
        if (lvw != lw || lvh != lh) continue;

        uint32_t tilesX = (lw + 63) / 64;
        uint32_t tilesY = (lh + 63) / 64;
        uint32_t totalTiles = tilesX * tilesY;
        std::vector<uint64_t> tileOffsets;
        for (uint32_t t = 0; t < totalTiles; ++t) {
            uint64_t toff = readOffset(lvp);
            if (toff == 0) break;
            tileOffsets.push_back(toff);
        }

        std::vector<uint8_t> tileBuf;
        for (size_t t = 0; t < tileOffsets.size(); ++t) {
            uint32_t tx = static_cast<uint32_t>(t % tilesX);
            uint32_t ty = static_cast<uint32_t>(t / tilesX);
            uint32_t tw = std::min(64u, lw - tx * 64);
            uint32_t th = std::min(64u, lh - ty * 64);

            size_t tStart = static_cast<size_t>(tileOffsets[t]);
            if (tStart >= size) continue;
            size_t tEnd = (t + 1 < tileOffsets.size() && tileOffsets[t + 1] > tileOffsets[t] && tileOffsets[t + 1] <= size)
                ? static_cast<size_t>(tileOffsets[t + 1])
                : std::min(size, tStart + static_cast<size_t>(tw) * th * bpp * 2);
            size_t tBytes = tEnd - tStart;

            if (compression == 0) {
                size_t rawLen = static_cast<size_t>(tw) * th * bpp;
                if (tStart + rawLen > size) continue;
                tileBuf.assign(data + tStart, data + tStart + rawLen);
            } else {
                DecodeXcfTileRle(data + tStart, tBytes, tw, th, bpp, tileBuf);
            }

            // Alpha-blend tile pixels onto canvas
            for (uint32_t py = 0; py < th; ++py) {
                int32_t cy = offY + static_cast<int32_t>(ty * 64 + py);
                if (cy < 0 || cy >= static_cast<int32_t>(canvasH)) continue;
                for (uint32_t px = 0; px < tw; ++px) {
                    int32_t cx = offX + static_cast<int32_t>(tx * 64 + px);
                    if (cx < 0 || cx >= static_cast<int32_t>(canvasW)) continue;

                    const uint8_t* sp = tileBuf.data() + (static_cast<size_t>(py) * tw + px) * bpp;
                    uint8_t sr = 0, sg = 0, sb = 0, sa = 255;
                    if (ltype == 0 || bpp == 3) {
                        sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = 255;
                    } else if (ltype == 1 || bpp == 4) {
                        sr = sp[0]; sg = sp[1]; sb = sp[2]; sa = sp[3];
                    } else if (bpp == 1) {
                        sr = sg = sb = sp[0]; sa = 255;
                    } else if (bpp == 2) {
                        sr = sg = sb = sp[0]; sa = sp[1];
                    }
                    sa = static_cast<uint8_t>((sa * opacity) / 255);
                    if (sa == 0) continue;

                    uint8_t* dp = outInfo.bgraPixels.data() + (static_cast<size_t>(cy) * canvasW + cx) * 4;
                    if (sa == 255 || dp[3] == 0) {
                        WritePremulBGRA(dp, sr, sg, sb, sa);
                    } else {
                        uint32_t invA = 255 - sa;
                        uint8_t outB = static_cast<uint8_t>((static_cast<uint32_t>(sb) * sa + static_cast<uint32_t>(dp[0]) * invA) / 255);
                        uint8_t outG = static_cast<uint8_t>((static_cast<uint32_t>(sg) * sa + static_cast<uint32_t>(dp[1]) * invA) / 255);
                        uint8_t outR = static_cast<uint8_t>((static_cast<uint32_t>(sr) * sa + static_cast<uint32_t>(dp[2]) * invA) / 255);
                        uint8_t outA = static_cast<uint8_t>(sa + (dp[3] * invA) / 255);
                        dp[0] = outB; dp[1] = outG; dp[2] = outR; dp[3] = outA;
                    }
                }
            }
        }
    }

    if (!outInfo.layerNames.empty()) {
        outInfo.formatName += L" (" + std::to_wstring(outInfo.layerNames.size()) + L" layers)";
    }
    outInfo.valid = true;
    return true;
}

// ============================================================================
// 4. RADIANCE HDR (.hdr, .pic) — RGBE Scanline Decoder + Reinhard Tone-Mapping
// ============================================================================
bool DecodeHDR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 16) return false;
    if (std::memcmp(data, "#?RADIANCE", 10) != 0 &&
        std::memcmp(data, "#?RGBE", 6) != 0 &&
        std::memcmp(data, "FORMAT=32-bit_rle_rgbe", 22) != 0) {
        return false;
    }

    size_t pos = 0;
    // Skip header lines until blank line
    while (pos < size) {
        size_t lineStart = pos;
        while (pos < size && data[pos] != '\n') ++pos;
        size_t lineLen = pos - lineStart;
        if (lineLen > 0 && data[lineStart + lineLen - 1] == '\r') --lineLen;
        if (pos < size) ++pos; // skip '\n'
        if (lineLen == 0) break;
    }

    // Read resolution line, e.g. "-Y 512 +X 1024"
    size_t resStart = pos;
    while (pos < size && data[pos] != '\n') ++pos;
    std::string resLine(reinterpret_cast<const char*>(data + resStart), pos - resStart);
    if (pos < size) ++pos;

    int height = 0, width = 0;
    const char* yPtr = std::strstr(resLine.c_str(), "Y ");
    const char* xPtr = std::strstr(resLine.c_str(), "X ");
    if (yPtr) height = std::atoi(yPtr + 2);
    if (xPtr) width  = std::atoi(xPtr + 2);
    if (width <= 0 || height <= 0 || width > static_cast<int>(kMaxRasterDimension) || height > static_cast<int>(kMaxRasterDimension)) {
        return false;
    }

    uint32_t uW = static_cast<uint32_t>(width);
    uint32_t uH = static_cast<uint32_t>(height);
    size_t totalPixels = static_cast<size_t>(uW) * uH;
    std::vector<uint8_t> rgbe(totalPixels * 4, 0);

    for (uint32_t y = 0; y < uH && pos < size; ++y) {
        uint8_t* scanline = rgbe.data() + static_cast<size_t>(y) * uW * 4;
        if (uW >= 8 && uW <= 0x7FFF && pos + 4 <= size &&
            data[pos] == 2 && data[pos + 1] == 2 &&
            ((static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3]) == uW)
        {
            // Adaptive per-channel RLE scanline
            pos += 4;
            for (int ch = 0; ch < 4; ++ch) {
                uint32_t x = 0;
                while (x < uW && pos < size) {
                    uint8_t code = data[pos++];
                    if (code > 128) {
                        uint32_t run = code - 128;
                        if (pos >= size) break;
                        uint8_t val = data[pos++];
                        for (uint32_t k = 0; k < run && x < uW; ++k, ++x) {
                            scanline[x * 4 + ch] = val;
                        }
                    } else {
                        uint32_t lit = code;
                        for (uint32_t k = 0; k < lit && x < uW && pos < size; ++k, ++x, ++pos) {
                            scanline[x * 4 + ch] = data[pos];
                        }
                    }
                }
            }
        } else {
            // Uncompressed RGBE scanline
            size_t rowBytes = static_cast<size_t>(uW) * 4;
            size_t avail = std::min(rowBytes, size - pos);
            std::memcpy(scanline, data + pos, avail);
            pos += avail;
        }
    }

    // Tone-map RGBE -> 8-bit sRGB PBGRA
    outInfo.width = uW;
    outInfo.height = uH;
    outInfo.channels = 3;
    outInfo.bitsPerChannel = 32;
    outInfo.formatName = L"Radiance HDR (RGBE)";
    outInfo.bgraPixels.resize(totalPixels * 4);

    auto toneMapChannel = [](float v) -> uint8_t {
        if (v <= 0.0f) return 0;
        float mapped = v / (1.0f + v); // Reinhard operator
        float srgb = std::pow(mapped, 1.0f / 2.2f);
        int iv = static_cast<int>(srgb * 255.0f + 0.5f);
        return static_cast<uint8_t>(std::clamp(iv, 0, 255));
    };

    for (size_t i = 0; i < totalPixels; ++i) {
        const uint8_t* sp = rgbe.data() + i * 4;
        uint8_t e = sp[3];
        uint8_t r = 0, g = 0, b = 0;
        if (e != 0) {
            float scale = std::ldexp(1.0f, static_cast<int>(e) - (128 + 8));
            r = toneMapChannel((sp[0] + 0.5f) * scale);
            g = toneMapChannel((sp[1] + 0.5f) * scale);
            b = toneMapChannel((sp[2] + 0.5f) * scale);
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, 255);
    }

    outInfo.valid = true;
    return true;
}

// ============================================================================
// 5. NETPBM (.ppm, .pgm, .pbm, .pnm) — P1/P2/P3 ASCII & P4/P5/P6 Binary
// ============================================================================
bool DecodeNetpbm(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 7 || data[0] != 'P' || data[1] < '1' || data[1] > '6') return false;
    int magic = data[1] - '0';
    size_t pos = 2;

    auto nextToken = [&]() -> std::string {
        while (pos < size) {
            if (data[pos] == '#') {
                while (pos < size && data[pos] != '\n') ++pos;
            } else if (data[pos] <= ' ') {
                ++pos;
            } else {
                break;
            }
        }
        size_t start = pos;
        while (pos < size && data[pos] > ' ' && data[pos] != '#') ++pos;
        return std::string(reinterpret_cast<const char*>(data + start), pos - start);
    };

    int width = std::atoi(nextToken().c_str());
    int height = std::atoi(nextToken().c_str());
    int maxVal = (magic == 1 || magic == 4) ? 1 : std::atoi(nextToken().c_str());
    if (width <= 0 || height <= 0 || width > static_cast<int>(kMaxRasterDimension) || height > static_cast<int>(kMaxRasterDimension) || maxVal <= 0) {
        return false;
    }

    // Skip single whitespace character before binary data
    if (pos < size && data[pos] == '\r' && pos + 1 < size && data[pos + 1] == '\n') pos += 2;
    else if (pos < size && data[pos] <= ' ') ++pos;

    uint32_t uW = static_cast<uint32_t>(width);
    uint32_t uH = static_cast<uint32_t>(height);
    size_t totalPixels = static_cast<size_t>(uW) * uH;
    outInfo.width = uW;
    outInfo.height = uH;
    outInfo.channels = (magic == 3 || magic == 6) ? 3 : 1;
    outInfo.bitsPerChannel = (maxVal > 255) ? 16 : 8;
    outInfo.formatName = L"Netpbm P" + std::to_wstring(magic);
    outInfo.bgraPixels.resize(totalPixels * 4, 255);

    auto scaleSample = [&](int v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp((v * 255) / maxVal, 0, 255));
    };

    if (magic == 1) {
        for (size_t i = 0; i < totalPixels; ++i) {
            std::string tok = nextToken();
            uint8_t c = (!tok.empty() && tok[0] == '1') ? 0 : 255;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], c, c, c, 255);
        }
    } else if (magic == 2) {
        for (size_t i = 0; i < totalPixels; ++i) {
            uint8_t c = scaleSample(std::atoi(nextToken().c_str()));
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], c, c, c, 255);
        }
    } else if (magic == 3) {
        for (size_t i = 0; i < totalPixels; ++i) {
            uint8_t r = scaleSample(std::atoi(nextToken().c_str()));
            uint8_t g = scaleSample(std::atoi(nextToken().c_str()));
            uint8_t b = scaleSample(std::atoi(nextToken().c_str()));
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, 255);
        }
    } else if (magic == 4) {
        size_t rowBytes = (uW + 7) / 8;
        for (uint32_t y = 0; y < uH && pos + rowBytes <= size; ++y) {
            for (uint32_t x = 0; x < uW; ++x) {
                bool bit = (data[pos + (x >> 3)] & (0x80 >> (x & 7))) != 0;
                uint8_t c = bit ? 0 : 255;
                WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * uW + x) * 4], c, c, c, 255);
            }
            pos += rowBytes;
        }
    } else if (magic == 5) {
        size_t bps = (maxVal > 255) ? 2 : 1;
        for (size_t i = 0; i < totalPixels && pos + bps <= size; ++i, pos += bps) {
            int raw = (bps == 2) ? ReadU16BE(data + pos) : data[pos];
            uint8_t c = scaleSample(raw);
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], c, c, c, 255);
        }
    } else if (magic == 6) {
        size_t bps = (maxVal > 255) ? 2 : 1;
        for (size_t i = 0; i < totalPixels && pos + bps * 3 <= size; ++i, pos += bps * 3) {
            uint8_t r = scaleSample((bps == 2) ? ReadU16BE(data + pos) : data[pos]);
            uint8_t g = scaleSample((bps == 2) ? ReadU16BE(data + pos + bps) : data[pos + 1]);
            uint8_t b = scaleSample((bps == 2) ? ReadU16BE(data + pos + bps * 2) : data[pos + 2]);
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, 255);
        }
    }

    outInfo.valid = true;
    return true;
}

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

// ============================================================================
// 7. QUITE OK IMAGE (.qoi) — Full Specification Decoder
// ============================================================================
bool DecodeQOI(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 22 || std::memcmp(data, "qoif", 4) != 0) return false;

    uint32_t width    = ReadU32BE(data + 4);
    uint32_t height   = ReadU32BE(data + 8);
    uint8_t  channels = data[12];
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;
    if (channels != 3 && channels != 4) return false;

    size_t totalPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.channels = channels;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Quite OK Image (QOI)";
    outInfo.bgraPixels.resize(totalPixels * 4);

    struct QoiPx { uint8_t r = 0, g = 0, b = 0, a = 0; };
    QoiPx index[64] = {};
    QoiPx px = { 0, 0, 0, 255 };

    size_t pos = 14;
    size_t endPos = (size >= 8) ? (size - 8) : size;
    uint32_t run = 0;

    for (size_t i = 0; i < totalPixels; ++i) {
        if (run > 0) {
            --run;
        } else if (pos < endPos) {
            uint8_t b1 = data[pos++];
            if (b1 == 0xFE) { // QOI_OP_RGB
                if (pos + 3 <= size) {
                    px.r = data[pos++];
                    px.g = data[pos++];
                    px.b = data[pos++];
                }
            } else if (b1 == 0xFF) { // QOI_OP_RGBA
                if (pos + 4 <= size) {
                    px.r = data[pos++];
                    px.g = data[pos++];
                    px.b = data[pos++];
                    px.a = data[pos++];
                }
            } else if ((b1 & 0xC0) == 0x00) { // QOI_OP_INDEX
                px = index[b1 & 0x3F];
            } else if ((b1 & 0xC0) == 0x40) { // QOI_OP_DIFF
                px.r = static_cast<uint8_t>(px.r + ((b1 >> 4) & 0x03) - 2);
                px.g = static_cast<uint8_t>(px.g + ((b1 >> 2) & 0x03) - 2);
                px.b = static_cast<uint8_t>(px.b + (b1 & 0x03) - 2);
            } else if ((b1 & 0xC0) == 0x80) { // QOI_OP_LUMA
                if (pos < size) {
                    uint8_t b2 = data[pos++];
                    int vg = (b1 & 0x3F) - 32;
                    px.r = static_cast<uint8_t>(px.r + vg - 8 + ((b2 >> 4) & 0x0F));
                    px.g = static_cast<uint8_t>(px.g + vg);
                    px.b = static_cast<uint8_t>(px.b + vg - 8 + (b2 & 0x0F));
                }
            } else if ((b1 & 0xC0) == 0xC0) { // QOI_OP_RUN
                run = (b1 & 0x3F);
            }
            uint32_t hash = (static_cast<uint32_t>(px.r) * 3 +
                             static_cast<uint32_t>(px.g) * 5 +
                             static_cast<uint32_t>(px.b) * 7 +
                             static_cast<uint32_t>(px.a) * 11) & 63u;
            index[hash] = px;
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], px.r, px.g, px.b, px.a);
    }

    outInfo.valid = true;
    return true;
}

} // namespace

bool ImageFormatLoader::IsExtendedImageExtension(const std::wstring& extLower) {
    return extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst" ||
           extLower == L".psd" || extLower == L".psb" ||
           extLower == L".xcf" ||
           extLower == L".hdr" || extLower == L".pic" ||
           extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm" ||
           extLower == L".pcx" ||
           extLower == L".qoi";
}

bool ImageFormatLoader::DecodeImageMemory(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    outInfo = {};
    if (!data || size < 6) return false;

    bool ok = false;
    // Check magic signatures first so even misnamed files decode reliably
    if (size >= 4 && std::memcmp(data, "8BPS", 4) == 0) {
        ok = DecodePSD(data, size, outInfo);
    } else if (size >= 9 && std::memcmp(data, "gimp xcf ", 9) == 0) {
        ok = DecodeXCF(data, size, outInfo);
    } else if (size >= 4 && std::memcmp(data, "qoif", 4) == 0) {
        ok = DecodeQOI(data, size, outInfo);
    } else if (size >= 10 && (std::memcmp(data, "#?RADIANCE", 10) == 0 || std::memcmp(data, "#?RGBE", 6) == 0)) {
        ok = DecodeHDR(data, size, outInfo);
    } else if (extLower == L".ppm" || extLower == L".pgm" || extLower == L".pbm" || extLower == L".pnm") {
        ok = DecodeNetpbm(data, size, outInfo);
    } else if (extLower == L".pcx") {
        ok = DecodePCX(data, size, outInfo);
    } else if (extLower == L".tga" || extLower == L".icb" || extLower == L".vda" || extLower == L".vst") {
        ok = DecodeTGA(data, size, outInfo);
    } else if (extLower == L".hdr" || extLower == L".pic") {
        ok = DecodeHDR(data, size, outInfo);
    } else if (extLower == L".psd" || extLower == L".psb") {
        ok = DecodePSD(data, size, outInfo);
    } else if (extLower == L".xcf") {
        ok = DecodeXCF(data, size, outInfo);
    } else if (extLower == L".qoi") {
        ok = DecodeQOI(data, size, outInfo);
    }

    if (ok && outInfo.valid) {
        DownscaleIfRequested(maxDimension, outInfo);
        return true;
    }
    return false;
}

bool ImageFormatLoader::DecodeImageFile(
    const std::wstring& fullPath,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    outInfo = {};
    std::vector<uint8_t> bytes;
    if (!PathUtils::ReadFileBytes(fullPath, 256ULL * 1024 * 1024, bytes) || bytes.empty()) {
        return false;
    }
    std::wstring ext = PathUtils::GetExtension(fullPath);
    return DecodeImageMemory(bytes.data(), bytes.size(), ext, maxDimension, outInfo);
}

} // namespace Explorer2
