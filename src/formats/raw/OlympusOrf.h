#pragma once
// OlympusOrf.h — Olympus / OM System Adaptive Golomb-Rice & TIFF CFA RAW Decoder (.orf)

namespace Formats {

namespace DetailVectorAndRawImg {

// ============================================================================
// 7. Olympus ORF 12/14-bit Adaptive Golomb-Rice Lossless Sensor Unpacker
// ============================================================================
inline bool UnpackOlympusOrfSensor(
    const uint8_t* src, size_t srcLen,
    uint32_t width, uint32_t height, uint16_t bps,
    std::vector<uint16_t>& outSamples)
{
    if (!src || width < 2 || (width & 1u) != 0u || height < 2 || (height & 1u) != 0u) return false;
    const uint16_t effBps = (bps == 14u) ? 14u : 12u;
    const size_t skipHdr = (effBps == 14u) ? 8u : 7u; // 7-byte Olympus stream header (8-byte for 14-bit)
    if (srcLen <= skipHdr + 4u) return false;

    static constexpr std::array<uint8_t, 4096> kNumLz12 = []() constexpr {
        std::array<uint8_t, 4096> lut{};
        lut[0] = 12u;
        for (uint32_t i = 1; i < 4096u; ++i) {
            uint8_t active = 0;
            uint32_t t = i;
            while (t > 0u) { ++active; t >>= 1u; }
            lut[i] = static_cast<uint8_t>(12u - active);
        }
        return lut;
    }();

    auto activeBits16 = [](uint16_t v) -> int {
        int n = 0;
        while (v > 0u) { ++n; v >>= 1u; }
        return n;
    };

    size_t bytePos = skipHdr;
    uint64_t bitBuf = 0;
    int bitCnt = 0;
    size_t zeroPadBytes = 0;

    auto fillBits = [&]() {
        while (bitCnt <= 32) {
            if (bytePos < srcLen) {
                bitBuf = (bitBuf << 8u) | src[bytePos++];
            } else {
                bitBuf <<= 8u;
                ++zeroPadBytes;
            }
            bitCnt += 8;
        }
    };

    auto peekBits = [&](int n) -> int {
        if (n <= 0) return 0;
        return static_cast<int>((bitBuf >> (bitCnt - n)) & ((1ull << n) - 1ull));
    };
    auto skipBits = [&](int n) { bitCnt -= n; };
    auto getBits = [&](int n) -> int {
        if (n <= 0) return 0;
        bitCnt -= n;
        return static_cast<int>((bitBuf >> bitCnt) & ((1ull << n) - 1ull));
    };

    const int32_t maxVal = (1 << effBps) - 1;
    outSamples.assign(static_cast<size_t>(width) * height, 0u);

    for (uint32_t row = 0; row < height; ++row) {
        int acarry[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
        for (uint32_t col = 0; col < width; ++col) {
            fillBits();
            if (zeroPadBytes > 16u) return false; // Reject truncated Olympus streams

            int* carry = acarry[col & 1u];
            int bias = (carry[2] < 3) ? 2 : 0;
            int nb = std::max(activeBits16(static_cast<uint16_t>(carry[0])) - bias, 2 + bias);
            if (nb > 14) return false;

            int b = peekBits(15);
            int sign = -(b >> 14);
            int low = (b >> 12) & 3;
            int lz = kNumLz12[b & 4095];

            int highBits = 0;
            if (lz != 12) {
                skipBits(lz + 4);
                highBits = lz;
            } else {
                skipBits(15);
                int nh = 15 - nb;
                highBits = peekBits(nh);
                skipBits(nh + 1);
            }

            carry[0] = (highBits << nb) | getBits(nb);
            int diff = (carry[0] ^ sign) + carry[1];
            carry[1] = (diff * 3 + carry[1]) >> 5;
            carry[2] = (carry[0] > 16) ? 0 : (carry[2] + 1);
            int fullDiff = (diff * 4) | low;

            int pred = 0;
            if (row < 2u && col < 2u) {
                pred = 0;
            } else if (row < 2u) {
                pred = outSamples[static_cast<size_t>(row) * width + (col - 2u)];
            } else if (col < 2u) {
                pred = outSamples[static_cast<size_t>(row - 2u) * width + col];
            } else {
                int left = outSamples[static_cast<size_t>(row) * width + (col - 2u)];
                int up   = outSamples[static_cast<size_t>(row - 2u) * width + col];
                int nw   = outSamples[static_cast<size_t>(row - 2u) * width + (col - 2u)];
                int lnw  = left - nw;
                int unw  = up - nw;
                if (((lnw < 0) != (unw < 0)) && lnw != 0 && unw != 0) {
                    pred = (std::abs(lnw) > 32 || std::abs(unw) > 32) ? (left + unw) : ((left + up) >> 1);
                } else {
                    pred = (std::abs(lnw) > std::abs(unw)) ? left : up;
                }
            }

            outSamples[static_cast<size_t>(row) * width + col] =
                static_cast<uint16_t>(std::clamp<int32_t>(pred + fullDiff, 0, maxVal));
        }
    }
    return true;
}

} // namespace DetailVectorAndRawImg

inline bool DecodeOlympusOrf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    return DetailVectorAndRawImg::ParseTiffRawIfds(data, size, L"Olympus ORF RAW (.orf)", outInfo);
}

} // namespace Formats
