#pragma once
// XWindowDumpXwd.h — X11 Window Dump v7 (.xwd — XWDFileHeader) Decoder
// Supports XYBitmap, XYPixmap & ZPixmap across depths 1/2/4/8/15/16/24/32, LSBFirst/MSBFirst, and XWDColor LUTs.

namespace Formats {

namespace DetailXwd {

inline uint8_t ExtractMaskedChannel(uint32_t pixel, uint32_t mask) noexcept {
    if (mask == 0u) return 0u;
    uint32_t shift = 0u;
    while (((mask >> shift) & 1u) == 0u && shift < 32u) ++shift;
    const uint32_t maxVal = mask >> shift;
    const uint32_t val = (pixel & mask) >> shift;
    if (maxVal == 255u) return static_cast<uint8_t>(val);
    if (maxVal == 0u) return 0u;
    return static_cast<uint8_t>((static_cast<uint64_t>(val) * 255ull + (maxVal / 2u)) / maxVal);
}

inline uint8_t SampleXwdPlaneBit(
    const uint8_t* rowPtr,
    uint32_t x,
    uint32_t byteOrder,
    uint32_t bitmapUnit,
    uint32_t bitmapBitOrder) noexcept
{
    const uint32_t unitBits = (bitmapUnit == 16u || bitmapUnit == 32u) ? bitmapUnit : 8u;
    const uint32_t unitBytes = unitBits / 8u;
    const size_t unitOff = static_cast<size_t>(x / unitBits) * unitBytes;
    const uint32_t bitInUnit = x % unitBits;
    // Map bit position inside the unit to logical byte within the unit, then apply byte_order swap if needed
    const uint32_t logicalByteFromMsb = (bitmapBitOrder == 1u) ? (bitInUnit / 8u) : ((unitBits - 1u - bitInUnit) / 8u);
    const size_t physByte = (byteOrder == 1u) ? logicalByteFromMsb : (unitBytes - 1u - logicalByteFromMsb);
    const uint8_t b = rowPtr[unitOff + physByte];
    const uint32_t bitInByte = bitInUnit & 7u;
    return (bitmapBitOrder == 1u) ? ((b >> (7u - bitInByte)) & 1u) : ((b >> bitInByte) & 1u);
}

} // namespace DetailXwd

inline bool DecodeXWindowDumpXwd(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailXwd;
    // XWDFileHeader is 25 big-endian CARD32 fields = 100 bytes minimum
    if (!data || size < 100u) return false;

    const uint32_t headerSize     = ByteIO::RdU32BE(data + 0);
    const uint32_t fileVersion    = ByteIO::RdU32BE(data + 4);
    const uint32_t pixmapFormat   = ByteIO::RdU32BE(data + 8);  // 0=XYBitmap, 1=XYPixmap, 2=ZPixmap
    const uint32_t pixmapDepth    = ByteIO::RdU32BE(data + 12);
    const uint32_t w              = ByteIO::RdU32BE(data + 16);
    const uint32_t h              = ByteIO::RdU32BE(data + 20);
    const uint32_t xoffset        = ByteIO::RdU32BE(data + 24);
    const uint32_t byteOrder      = ByteIO::RdU32BE(data + 28); // 0=LSBFirst, 1=MSBFirst
    const uint32_t bitmapUnit     = ByteIO::RdU32BE(data + 32);
    const uint32_t bitmapBitOrder = ByteIO::RdU32BE(data + 36); // 0=LSBFirst, 1=MSBFirst
    const uint32_t bitsPerPixel   = ByteIO::RdU32BE(data + 44);
    const uint32_t bytesPerLine   = ByteIO::RdU32BE(data + 48);
    const uint32_t visualClass    = ByteIO::RdU32BE(data + 52); // 0..5
    const uint32_t redMask        = ByteIO::RdU32BE(data + 56);
    const uint32_t greenMask      = ByteIO::RdU32BE(data + 60);
    const uint32_t blueMask       = ByteIO::RdU32BE(data + 64);
    const uint32_t ncolors        = ByteIO::RdU32BE(data + 76);

    if (fileVersion != 7u || headerSize < 100u || headerSize > 65536u || headerSize > size) return false;
    if (pixmapFormat > 2u || visualClass > 5u || byteOrder > 1u || bitmapBitOrder > 1u) return false;
    if (w == 0 || h == 0 || w > 8192u || h > 8192u || bytesPerLine == 0 || ncolors > 65536u) return false;
    if (pixmapDepth == 0 || pixmapDepth > 32u || bitsPerPixel == 0 || bitsPerPixel > 32u) return false;

    const uint64_t colorsBytes = static_cast<uint64_t>(ncolors) * 12ull;
    if (colorsBytes > size - headerSize) return false;

    // Parse ncolors x 12-byte XWDColor table: [pixel:u32BE, red:u16BE, green:u16BE, blue:u16BE, flags:u8, pad:u8]
    std::vector<std::array<uint8_t, 3>> cmap(std::max<uint32_t>(256u, ncolors), { 0, 0, 0 });
    for (uint32_t i = 0; i < 256u; ++i) {
        cmap[i] = { static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i) };
    }
    if (pixmapDepth == 1u && ncolors == 0u) {
        cmap[0] = { 255, 255, 255 };
        cmap[1] = { 0, 0, 0 };
    }

    const uint8_t* colBase = data + headerSize;
    for (uint32_t i = 0; i < ncolors; ++i) {
        const uint8_t* cp = colBase + static_cast<size_t>(i) * 12u;
        const uint32_t pixVal = ByteIO::RdU32BE(cp + 0);
        const uint8_t r8 = static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16BE(cp + 4)) * 255u + 32767u) / 65535u);
        const uint8_t g8 = static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16BE(cp + 6)) * 255u + 32767u) / 65535u);
        const uint8_t b8 = static_cast<uint8_t>((static_cast<uint32_t>(ByteIO::RdU16BE(cp + 8)) * 255u + 32767u) / 65535u);
        const size_t slot = (pixVal < cmap.size()) ? static_cast<size_t>(pixVal) : static_cast<size_t>(i);
        cmap[slot] = { r8, g8, b8 };
        if (i < cmap.size() && pixVal >= cmap.size()) cmap[i] = { r8, g8, b8 };
    }

    const size_t pixOff = headerSize + static_cast<size_t>(colorsBytes);
    const size_t avail  = size - pixOff;
    const uint8_t* pixBase = data + pixOff;

    const bool useTrueMasks = (visualClass == 4u || visualClass == 5u || (redMask | greenMask | blueMask) != 0u) &&
                              (pixmapDepth > 8u || (visualClass == 4u && (redMask | greenMask | blueMask) != 0u));

    auto pixelValueToRgb = [&](uint32_t pv, uint8_t& r, uint8_t& g, uint8_t& b) {
        if (useTrueMasks) {
            r = ExtractMaskedChannel(pv, redMask);
            g = ExtractMaskedChannel(pv, greenMask);
            b = ExtractMaskedChannel(pv, blueMask);
            if (visualClass == 5u && ncolors >= 256u) { // DirectColor maps extracted R,G,B indices through XWDColor LUT
                r = cmap[r][0];
                g = cmap[g][1];
                b = cmap[b][2];
            }
        } else {
            const size_t idx = (pv < cmap.size()) ? static_cast<size_t>(pv) : (pv & 0xFFu);
            r = cmap[idx][0];
            g = cmap[idx][1];
            b = cmap[idx][2];
        }
    };

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = (pixmapDepth <= 8u && ncolors == 0u && visualClass <= 1u) ? 1u : 3u;
    outInfo.bitsPerChannel = std::min<uint32_t>(8u, pixmapDepth);
    outInfo.formatName = L"X11 Window Dump v7 (.xwd)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    if (pixmapFormat == 0u || pixmapFormat == 1u) {
        // XYBitmap (1 plane) / XYPixmap (`pixmapDepth` sequential bitplanes, MSB plane first per X11 protocol)
        const uint32_t planes = (pixmapFormat == 0u) ? 1u : pixmapDepth;
        const uint64_t planeSize = static_cast<uint64_t>(bytesPerLine) * h;
        if (planeSize * planes > avail) return false;
        if (((xoffset + w + 7u) / 8u) > bytesPerLine) return false;

        for (uint32_t y = 0; y < h; ++y) {
            uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * w * 4u];
            for (uint32_t x = 0; x < w; ++x) {
                uint32_t pv = 0u;
                for (uint32_t p = 0; p < planes; ++p) {
                    const uint8_t* rowPtr = pixBase + static_cast<size_t>(p * planeSize) + static_cast<size_t>(y) * bytesPerLine;
                    const uint32_t bit = SampleXwdPlaneBit(rowPtr, xoffset + x, byteOrder, bitmapUnit, bitmapBitOrder);
                    pv = (pv << 1u) | bit;
                }
                uint8_t r = 0, g = 0, b = 0;
                pixelValueToRgb(pv, r, g, b);
                WritePremulBGRA(dstRow + x * 4u, r, g, b, 255u);
            }
        }
    } else {
        // ZPixmap (packed scanlines)
        const uint64_t totalBytes = static_cast<uint64_t>(bytesPerLine) * h;
        if (totalBytes > avail) return false;
        if (((static_cast<uint64_t>(xoffset + w) * bitsPerPixel + 7ull) / 8ull) > bytesPerLine) return false;

        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* rowPtr = pixBase + static_cast<size_t>(y) * bytesPerLine;
            uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * w * 4u];
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t lx = xoffset + x;
                uint32_t pv = 0u;
                if (bitsPerPixel == 1u) {
                    pv = SampleXwdPlaneBit(rowPtr, lx, byteOrder, bitmapUnit, bitmapBitOrder);
                } else if (bitsPerPixel == 2u) {
                    const uint8_t b = rowPtr[lx >> 2u];
                    const uint32_t shift = (bitmapBitOrder == 1u) ? (6u - (lx & 3u) * 2u) : ((lx & 3u) * 2u);
                    pv = (b >> shift) & 0x03u;
                } else if (bitsPerPixel == 4u) {
                    const uint8_t b = rowPtr[lx >> 1u];
                    pv = (bitmapBitOrder == 1u)
                        ? (((lx & 1u) == 0u) ? (b >> 4u) : (b & 0x0Fu))
                        : (((lx & 1u) == 0u) ? (b & 0x0Fu) : (b >> 4u));
                } else if (bitsPerPixel == 8u) {
                    pv = rowPtr[lx];
                } else if (bitsPerPixel == 16u) {
                    const uint8_t* p = rowPtr + static_cast<size_t>(lx) * 2u;
                    pv = (byteOrder == 1u) ? ByteIO::RdU16BE(p) : ByteIO::RdU16LE(p);
                } else if (bitsPerPixel == 24u) {
                    const uint8_t* p = rowPtr + static_cast<size_t>(lx) * 3u;
                    pv = (byteOrder == 1u)
                        ? ((static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2])
                        : ((static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0]);
                } else if (bitsPerPixel == 32u) {
                    const uint8_t* p = rowPtr + static_cast<size_t>(lx) * 4u;
                    pv = (byteOrder == 1u) ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p);
                } else {
                    return false;
                }

                uint8_t r = 0, g = 0, b = 0;
                pixelValueToRgb(pv, r, g, b);
                WritePremulBGRA(dstRow + x * 4u, r, g, b, 255u);
            }
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
