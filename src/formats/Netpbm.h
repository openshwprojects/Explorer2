#pragma once
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

