#pragma once
// GimpPat.h — GIMP ('GPAT') Seamless Pattern Decoder (.pat)

namespace Formats {

inline bool DecodeGimpPat(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 24) return false;
        if (size >= 24 && std::memcmp(data + 20, "GPAT", 4) == 0) {
            uint32_t hdrSize = DetailImg100::RdU32BE(data + 0);
            uint32_t pw      = DetailImg100::RdU32BE(data + 8);
            uint32_t ph      = DetailImg100::RdU32BE(data + 12);
            uint32_t bpp     = DetailImg100::RdU32BE(data + 16);
            if (pw == 0 || ph == 0 || pw > 4096 || ph > 4096 || bpp < 1 || bpp > 4) return false;
            if (hdrSize < 24 || hdrSize + static_cast<size_t>(pw) * ph * bpp > size) return false;

            // Tile 2x2 so seamless pattern tiling is immediately visible!
            uint32_t tileX = (pw <= 256) ? 2 : 1;
            uint32_t tileY = (ph <= 256) ? 2 : 1;
            uint32_t outW = pw * tileX, outH = ph * tileY;
            outInfo.width = outW; outInfo.height = outH;
            outInfo.origWidth = pw; outInfo.origHeight = ph;
            outInfo.channels = 4; outInfo.bitsPerChannel = 8;
            outInfo.formatName = L"GIMP Seamless Pattern (.pat)";
            outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4);

            const uint8_t* src = data + hdrSize;
            for (uint32_t y = 0; y < outH; ++y) {
                uint32_t sy = y % ph;
                for (uint32_t x = 0; x < outW; ++x) {
                    uint32_t sx = x % pw;
                    const uint8_t* p = src + (static_cast<size_t>(sy) * pw + sx) * bpp;
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    if (bpp == 1) { r = g = b = p[0]; }
                    else if (bpp == 2) { r = g = b = p[0]; a = p[1]; }
                    else if (bpp == 3) { r = p[0]; g = p[1]; b = p[2]; }
                    else { r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * outW + x) * 4];
                    dst[0] = static_cast<uint8_t>((b * a) / 255);
                    dst[1] = static_cast<uint8_t>((g * a) / 255);
                    dst[2] = static_cast<uint8_t>((r * a) / 255);
                    dst[3] = a;
                }
            }
            outInfo.valid = true;
            return true;
        }
    return false;
}

} // namespace Formats
