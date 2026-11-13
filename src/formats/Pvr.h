#pragma once
// Pvr.h — PowerVR GPU Texture Container (.pvr, PVR\x03) Decoder

namespace Formats {

inline bool DecodePVR(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (size < 52 || std::memcmp(data, "PVR\x03", 4) != 0) return false;
    uint32_t h = 0, w = 0, metaLen = 0;
    std::memcpy(&h, data + 24, 4);
    std::memcpy(&w, data + 28, 4);
    std::memcpy(&metaLen, data + 48, 4);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || 52 + metaLen > size) return false;

    size_t payloadOff = 52 + metaLen;
    size_t pixelCount = static_cast<size_t>(w) * h;
    if (payloadOff + pixelCount * 4 > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"PowerVR Texture v3 (.pvr)";
    outInfo.bgraPixels.resize(pixelCount * 4);

    const uint8_t* src = data + payloadOff;
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t r = src[i * 4 + 0], g = src[i * 4 + 1], b = src[i * 4 + 2], a = src[i * 4 + 3];
        uint8_t* dst = &outInfo.bgraPixels[i * 4];
        dst[0] = static_cast<uint8_t>((b * a) / 255);
        dst[1] = static_cast<uint8_t>((g * a) / 255);
        dst[2] = static_cast<uint8_t>((r * a) / 255);
        dst[3] = a;
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
