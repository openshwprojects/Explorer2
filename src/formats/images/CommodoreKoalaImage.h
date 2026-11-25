#pragma once
// CommodoreKoalaImage.h — Commodore 64 KoalaPainter Multicolor Bitmap (.koa, .kla) Decoder
// Exact 10,003-byte format: [0x00, 0x60] PRG load addr + 8,000B bitmap + 1,000B screen RAM + 1,000B color RAM + 1B bg color.

namespace Formats {

inline bool DecodeCommodoreKoalaImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size != 10003u) return false;

    const uint16_t loadAddr = ByteIO::RdU16LE(data + 0);
    // Standard KoalaPainter load address is $6000 ([0x00, 0x60]); also accept $4000 / $2000 C64 multicolor dumps
    if (loadAddr != 0x6000u && loadAddr != 0x4000u && loadAddr != 0x2000u) return false;

    // Authentic Colodore VIC-II 16-color sRGB palette (Pepto / Philip Timmermann)
    static constexpr uint8_t kVicIIPalette[16][3] = {
        {   0,   0,   0 }, //  0 Black
        { 255, 255, 255 }, //  1 White
        { 129,  51,  56 }, //  2 Red
        { 117, 206, 200 }, //  3 Cyan
        { 142,  60, 151 }, //  4 Purple
        {  86, 172,  77 }, //  5 Green
        {  46,  44, 155 }, //  6 Blue
        { 237, 241, 113 }, //  7 Yellow
        { 142,  80,  41 }, //  8 Orange
        {  85,  56,   0 }, //  9 Brown
        { 196, 108, 113 }, // 10 Light Red
        {  74,  74,  74 }, // 11 Dark Grey
        { 123, 123, 123 }, // 12 Medium Grey
        { 169, 255, 159 }, // 13 Light Green
        { 112, 109, 235 }, // 14 Light Blue
        { 178, 178, 178 }  // 15 Light Grey
    };

    const uint8_t* bitmap    = data + 2u;     // 8,000 bytes (40 x 25 cells of 8 bytes)
    const uint8_t* screenRam = data + 8002u;  // 1,000 bytes (high nibble = %01, low nibble = %10)
    const uint8_t* colorRam  = data + 9002u;  // 1,000 bytes (low nibble = %11)
    const uint8_t  bgColIdx  = data[10002u] & 0x0Fu;

    constexpr uint32_t kOutW = 320u; // 160 double-wide multicolor pixels -> 320x200 aspect-accurate output
    constexpr uint32_t kOutH = 200u;

    outInfo = {};
    outInfo.width = kOutW;
    outInfo.height = kOutH;
    outInfo.origWidth = kOutW;
    outInfo.origHeight = kOutH;
    outInfo.channels = 3u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Commodore 64 KoalaPainter Multicolor (.koa)";
    outInfo.bgraPixels.resize(static_cast<size_t>(kOutW) * kOutH * 4u);

    for (uint32_t y = 0; y < kOutH; ++y) {
        const uint32_t cellRow = y >> 3u;
        const uint32_t inCellY = y & 7u;
        uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * kOutW * 4u];

        for (uint32_t mx = 0; mx < 160u; ++mx) {
            const uint32_t cellCol = mx >> 2u;
            const size_t cellIdx = static_cast<size_t>(cellRow) * 40u + cellCol;
            const uint8_t byteVal = bitmap[cellIdx * 8u + inCellY];
            const uint8_t shift = static_cast<uint8_t>(6u - ((mx & 3u) * 2u));
            const uint8_t crumb = (byteVal >> shift) & 0x03u;

            uint8_t colIdx = bgColIdx;
            if (crumb == 1u)      colIdx = (screenRam[cellIdx] >> 4u) & 0x0Fu;
            else if (crumb == 2u) colIdx =  screenRam[cellIdx]        & 0x0Fu;
            else if (crumb == 3u) colIdx =  colorRam[cellIdx]         & 0x0Fu;

            const uint8_t r = kVicIIPalette[colIdx][0];
            const uint8_t g = kVicIIPalette[colIdx][1];
            const uint8_t b = kVicIIPalette[colIdx][2];
            WritePremulBGRA(dstRow + (mx * 2u + 0u) * 4u, r, g, b, 255u);
            WritePremulBGRA(dstRow + (mx * 2u + 1u) * 4u, r, g, b, 255u);
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
