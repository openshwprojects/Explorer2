#pragma once
// SiliconGraphicsAliasPix.h — Alias|Wavefront PIX 24-bit & 8-bit RLE Raster Decoder (.pix, .als)

namespace Formats {

inline bool DecodeSiliconGraphicsAliasPix(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    // Header is 10 big-endian bytes: [width:u16, height:u16, xoff:u16, yoff:u16, bpp:u16]
    if (!data || size < 12u) return false;

    // Quickly reject common colliding formats (.als Ableton Live gzip/XML, RIFF, PNG, ZIP, IFF)
    if ((data[0] == 0x1Fu && data[1] == 0x8Bu) ||
        data[0] == '<' ||
        (data[0] == 'P' && data[1] == 'K') ||
        std::memcmp(data, "RIFF", 4) == 0 ||
        std::memcmp(data, "FORM", 4) == 0 ||
        std::memcmp(data, "\x89PNG", 4) == 0)
    {
        return false;
    }

    const uint16_t w    = ByteIO::RdU16BE(data + 0);
    const uint16_t h    = ByteIO::RdU16BE(data + 2);
    const uint16_t xoff = ByteIO::RdU16BE(data + 4);
    const uint16_t yoff = ByteIO::RdU16BE(data + 6);
    const uint16_t bpp  = ByteIO::RdU16BE(data + 8);

    if (w == 0 || h == 0 || w > 8192u || h > 8192u || xoff > 8192u || yoff > 8192u) return false;
    if (bpp != 24u && bpp != 8u) return false;

    const size_t pktSize = (bpp == 24u) ? 4u : 2u;
    if (size < 10u + pktSize) return false;

    const size_t totalPixels = static_cast<size_t>(w) * h;
    if (totalPixels > 64ull * 1024ull * 1024ull) return false;

    // Worst-case RLE has 1 pixel per packet; best-case has ceil(totalPixels / 255) packets
    const size_t minPackets = (totalPixels + 254u) / 255u;
    if ((size - 10u) / pktSize < minPackets) return false;

    std::vector<uint8_t> bgra(totalPixels * 4u, 255u);
    size_t pos = 10u;
    size_t decodedPx = 0u;

    while (decodedPx < totalPixels && pos + pktSize <= size) {
        const uint8_t count = data[pos];
        if (count == 0u) return false; // Run count 0 is illegal in Alias PIX and cleanly rejects arbitrary binary data
        if (decodedPx + count > totalPixels) return false;

        uint8_t r = 0, g = 0, b = 0;
        if (bpp == 24u) {
            // Alias PIX 24-bit packet order is [count, B, G, R]
            b = data[pos + 1u];
            g = data[pos + 2u];
            r = data[pos + 3u];
        } else {
            r = g = b = data[pos + 1u];
        }
        pos += pktSize;

        for (uint8_t k = 0; k < count; ++k) {
            uint8_t* dst = &bgra[decodedPx * 4u];
            dst[0] = b;
            dst[1] = g;
            dst[2] = r;
            dst[3] = 255u;
            ++decodedPx;
        }
    }

    // Require exact full-frame decode and no huge trailing garbage
    if (decodedPx != totalPixels) return false;
    if (size - pos > 16u) return false;

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = (bpp == 24u) ? 3u : 1u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = (bpp == 24u)
        ? L"Alias|Wavefront PIX Raster (24-bit .pix)"
        : L"Alias|Wavefront Matte PIX (8-bit .pix)";
    outInfo.bgraPixels = std::move(bgra);
    outInfo.valid = true;
    return true;
}

} // namespace Formats
