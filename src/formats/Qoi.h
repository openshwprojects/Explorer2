#pragma once
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
