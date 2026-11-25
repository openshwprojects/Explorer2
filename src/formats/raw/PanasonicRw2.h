#pragma once
// PanasonicRw2.h — Panasonic Lumix RW2 12-bit 14-px-block Sensor & Preview RAW Decoder (.rw2)

namespace Formats {

namespace DetailVectorAndRawImg {

// ============================================================================
// 4. Panasonic Lumix RW2 12-bit 14-Pixel-per-16-Byte Sensor Unpacker
// ============================================================================
inline bool UnpackPanasonicRw2Sensor(
    const uint8_t* src, size_t srcLen, uint32_t width, uint32_t height,
    std::vector<uint16_t>& outSamples)
{
    if (!src || width < 2 || height == 0) return false;
    size_t totalPixels = static_cast<size_t>(width) * height;
    size_t minBlocks = (totalPixels + 13u) / 14u;
    if (srcLen < minBlocks * 16u) return false;

    outSamples.assign(totalPixels, 0u);
    size_t bytePos = 0;
    uint32_t bitBuf = 0;
    int bitCnt = 0;

    auto panaBits = [&](int n) -> int32_t {
        while (bitCnt < n) {
            uint8_t b = (bytePos < srcLen) ? src[bytePos++] : 0u;
            bitBuf |= static_cast<uint32_t>(b) << bitCnt;
            bitCnt += 8;
        }
        int32_t res = static_cast<int32_t>(bitBuf & ((1u << n) - 1u));
        bitBuf >>= n;
        bitCnt -= n;
        return res;
    };

    for (uint32_t y = 0; y < height; ++y) {
        int32_t pred[2] = { 0, 0 };
        int32_t nonz[2] = { 0, 0 };
        int sh = 0;
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t i = x % 14u;
            if (i == 0u) {
                pred[0] = nonz[0] = pred[1] = nonz[1] = 0;
            }
            if (i % 3u == 2u) {
                sh = 4 >> (3 - panaBits(2));
            }
            int pIdx = static_cast<int>(x & 1u);
            if (nonz[pIdx] != 0) {
                int32_t j = panaBits(8);
                if (j != 0) {
                    pred[pIdx] -= (0x80 << sh);
                    if (pred[pIdx] < 0 || sh == 4) pred[pIdx] &= ~(-1 << sh);
                    pred[pIdx] += (j << sh);
                }
            } else {
                nonz[pIdx] = panaBits(8);
                if (nonz[pIdx] != 0 || i > 11u) {
                    pred[pIdx] = (nonz[pIdx] << 4) | panaBits(4);
                }
            }
            outSamples[static_cast<size_t>(y) * width + x] = static_cast<uint16_t>(std::clamp<int32_t>(pred[pIdx], 0, 4095));
        }
    }
    return true;
}

} // namespace DetailVectorAndRawImg

inline bool DecodePanasonicRw2(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailVectorAndRawImg::ParseTiffRawIfds(data, size, L"Panasonic Lumix RW2 RAW (.rw2)", outInfo);
}

} // namespace Formats
