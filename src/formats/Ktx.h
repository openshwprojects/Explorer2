#pragma once
// Ktx.h — Khronos Texture 1.0 (.ktx) Decoder

namespace Formats {

inline bool DecodeKTX(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    static const uint8_t kMagic[12] = { 0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    if (size < 68 || std::memcmp(data, kMagic, 12) != 0) return false;
    bool bigEndian = (data[12] == 0x04 && data[13] == 0x03 && data[14] == 0x02 && data[15] == 0x01);
    auto rd32 = [&](size_t off) -> uint32_t {
        const uint8_t* p = data + off;
        return bigEndian
            ? ((static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3])
            : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    };

    uint32_t glFormat         = rd32(24);
    uint32_t glInternalFormat = rd32(28);
    uint32_t w                = rd32(36);
    uint32_t h                = std::max(1u, rd32(40));
    uint32_t bytesOfKeyValue  = rd32(60);
    if (w == 0 || w > 8192 || h > 8192 || 64 + bytesOfKeyValue + 4 > size) return false;

    size_t imgSizeOff = 64 + bytesOfKeyValue;
    uint32_t imageSize = rd32(imgSizeOff);
    size_t payloadOff = imgSizeOff + 4;
    if (payloadOff + imageSize > size) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Khronos Texture (.ktx)";
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4, 255);

    const uint8_t* src = data + payloadOff;
    uint32_t bpp = 4;
    if (glFormat == 0x1907 || glInternalFormat == 0x8051) bpp = 3; // GL_RGB / GL_RGB8
    else if (glFormat == 0x1903 || glFormat == 0x1909 || glInternalFormat == 0x8229) bpp = 1; // GL_RED / GL_LUMINANCE / GL_R8

    size_t rowStride = ((static_cast<size_t>(w) * bpp + 3) & ~size_t(3));
    if (rowStride * h <= imageSize) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* row = src + y * rowStride;
            for (uint32_t x = 0; x < w; ++x) {
                uint8_t r = row[x * bpp];
                uint8_t g = (bpp >= 3) ? row[x * bpp + 1] : r;
                uint8_t b = (bpp >= 3) ? row[x * bpp + 2] : r;
                uint8_t a = (bpp == 4) ? row[x * bpp + 3] : 255;
                uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4];
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
