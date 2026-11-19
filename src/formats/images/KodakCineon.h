#pragma once
// KodakCineon.h — Kodak Cineon 10-bit Printing-Density Film Scan Decoder (.cin)

namespace Formats {

inline bool DecodeKodakCineon(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 1024) return false;
    const bool isCinBE = (DetailImg100::RdU32BE(data) == 0x802A5FD7u);
    const bool isCinLE = (DetailImg100::RdU32LE(data) == 0x802A5FD7u);
    if (!isCinBE && !isCinLE) return false;
    const bool isLittle = isCinLE;
    auto rd32 = [&](size_t off) -> uint32_t {
        if (off + 4 > size) return 0;
        return isLittle ? DetailImg100::RdU32LE(data + off) : DetailImg100::RdU32BE(data + off);
    };
    using DpxElemSpec = DetailDpxCin::DpxElemSpec;
    uint32_t dataOff = rd32(4);
    uint32_t w = 0, h = 0;
    uint16_t orientation = 0;
    std::vector<DpxElemSpec> elems;
        w = rd32(200);
        h = rd32(204);
        if (w == 0 || w > 8192 || h == 0 || h > 8192) return false;
        if (dataOff < 716 || dataOff >= size) return false;

        // Image information header (192): orientation, channel count, then 28-byte channel descriptors
        // (bits per pixel at +2 of the first one = 198). Data format header (680): interleave, packing.
        const uint8_t numCh = data[193];
        const uint8_t bpp = data[198];
        if (numCh < 1 || numCh > 8) return false;
        if (bpp != 8 && bpp != 10 && bpp != 12 && bpp != 16) return false;
        orientation = data[192];

        // Cineon packing codes mapped to the DPX element packing used below
        uint16_t packing = 0;
        switch (data[681]) {
            case 0: packing = 0; break;                   // every bit used
            case 1: case 2:                               // samples on byte boundaries
                if (bpp != 8) return false;
                packing = 0;
                break;
            case 3: case 4:                               // samples on 16-bit boundaries, left / right justified
                if (bpp != 12 && bpp != 16) return false;
                packing = (data[681] == 3) ? 1 : 2;
                break;
            case 5: packing = 1; break;                   // 32-bit words, left justified (DPX method A)
            case 6: packing = 2; break;                   // 32-bit words, right justified (DPX method B)
            default: return false;
        }

        DpxElemSpec el{};
        el.descriptor = (numCh == 1) ? 6 : ((numCh >= 4) ? 51 : 50);
        el.transfer   = 1; // Cineon Printing-Density Log
        el.bitSize    = bpp;
        el.packing    = packing;
        el.encoding   = 0;
        el.elemOff    = dataOff;
        elems.push_back(el);
    return DetailDpxCin::RenderDpxOrCineonElements(data, size, isLittle, true, w, h, orientation, elems, outInfo);
}

} // namespace Formats
