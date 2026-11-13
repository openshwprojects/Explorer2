#pragma once
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
