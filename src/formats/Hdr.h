#pragma once
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

