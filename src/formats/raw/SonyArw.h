#pragma once
// SonyArw.h — Sony Alpha ARW / ARW2 Delta-Compressed CFA RAW Decoder (.arw)

namespace Formats {

namespace DetailImg200 {

// ============================================================================
// 2. Sony Alpha ARW2 Delta-Compressed Raw Unpacker (Compression == 32767)
// ============================================================================
inline bool UnpackSonyArw2Sensor(
    const uint8_t* src, size_t srcLen, uint32_t width, uint32_t height,
    const uint16_t* optCurveLut4096,
    std::vector<uint16_t>& outSamples)
{
    if (!src || width < 32 || (width & 31u) != 0u || height == 0) return false;
    size_t neededBytes = static_cast<size_t>(width) * height;
    if (srcLen < neededBytes) return false;

    outSamples.assign(neededBytes, 0u);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* rowSrc = src + static_cast<size_t>(y) * width;
        uint16_t* rowDst = &outSamples[static_cast<size_t>(y) * width];
        for (uint32_t x0 = 0; x0 < width; x0 += 32u) {
            for (uint32_t parity = 0; parity < 2u; ++parity) {
                const uint8_t* blk = rowSrc + x0 + parity * 16u;
                size_t bitPos = 0;
                auto rdBits = [&](int n) -> uint32_t {
                    uint32_t v = 0;
                    for (int k = 0; k < n; ++k) {
                        size_t bp = bitPos++;
                        v |= static_cast<uint32_t>((blk[bp >> 3u] >> (bp & 7u)) & 1u) << k;
                    }
                    return v;
                };
                uint32_t maxV = rdBits(11);
                uint32_t minV = rdBits(11);
                uint32_t imax = rdBits(4);
                uint32_t imin = rdBits(4);
                if (maxV < minV) return false;
                int sh = 0;
                while (sh < 4 && (0x80u << sh) <= (maxV - minV)) ++sh;

                for (uint32_t i = 0; i < 16u; ++i) {
                    uint32_t pix11 = 0u;
                    if (i == imax) pix11 = maxV;
                    else if (i == imin) pix11 = minV;
                    else pix11 = std::min<uint32_t>(2047u, (rdBits(7) << sh) + minV);

                    uint16_t v12 = static_cast<uint16_t>(pix11 << 1u);
                    if (optCurveLut4096) v12 = optCurveLut4096[std::min<uint32_t>(v12, 4095u)];
                    rowDst[x0 + i * 2u + parity] = v12;
                }
            }
        }
    }
    return true;
}

} // namespace DetailImg200

inline bool DecodeSonyArw(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailImg200::ParseTiffRawIfds(data, size, L"Sony Alpha ARW RAW (.arw)", outInfo);
}

} // namespace Formats
