#pragma once
// ZxSpectrumScrImage.h — Sinclair ZX Spectrum Screen Dump (.scr — exact 6,912 bytes) Decoder

namespace Formats {

inline bool DecodeZxSpectrumScrImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    // ZX Spectrum VRAM dump is strictly 6,144B bitmap + 768B attributes = 6,912 bytes.
    // Reject Windows PE screensavers ("MZ") or text files that happen to match size.
    if (!data || size != 6912u) return false;
    if (data[0] == 'M' && data[1] == 'Z') return false;

    constexpr uint32_t kW = 256u;
    constexpr uint32_t kH = 192u;

    outInfo = {};
    outInfo.width = kW;
    outInfo.height = kH;
    outInfo.origWidth = kW;
    outInfo.origHeight = kH;
    outInfo.channels = 3u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Sinclair ZX Spectrum Screen (.scr)";
    outInfo.bgraPixels.resize(static_cast<size_t>(kW) * kH * 4u);

    for (uint32_t y = 0; y < kH; ++y) {
        uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * kW * 4u];
        for (uint32_t x = 0; x < kW; ++x) {
            // Non-linear 3-third ZX Spectrum ULA address mapping:
            //   bits 12..11 = third (y[7:6]), bits 10..8 = pixel row in char (y[2:0]),
            //   bits  7..5  = char row in third (y[5:3]), bits 4..0 = byte column (x[7:3])
            const size_t vramOff = ((static_cast<size_t>(y) & 0xC0u) << 5u) |
                                   ((static_cast<size_t>(y) & 0x07u) << 8u) |
                                   ((static_cast<size_t>(y) & 0x38u) << 2u) |
                                   (static_cast<size_t>(x) >> 3u);
            const bool isInk = ((data[vramOff] >> (7u - (x & 7u))) & 1u) != 0u;

            const uint8_t attr = data[6144u + (y >> 3u) * 32u + (x >> 3u)];
            const uint8_t col3 = isInk ? (attr & 0x07u) : ((attr >> 3u) & 0x07u);
            const uint8_t lvl  = ((attr & 0x40u) != 0u) ? 255u : 215u; // BRIGHT vs normal ULA voltage

            // ZX Spectrum 3-bit color bits: bit 0 = Blue, bit 1 = Red, bit 2 = Green
            const uint8_t b = (col3 & 0x01u) ? lvl : 0u;
            const uint8_t r = (col3 & 0x02u) ? lvl : 0u;
            const uint8_t g = (col3 & 0x04u) ? lvl : 0u;
            WritePremulBGRA(dstRow + x * 4u, r, g, b, 255u);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
