#pragma once
// Tim.h — Sony PlayStation 1 TIM Texture (.tim) Decoder

namespace Formats {

inline bool DecodeTIM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 20 || data[0] != 0x10 || data[1] != 0x00 || data[2] != 0x00 || data[3] != 0x00) return false;
    uint32_t flags = ByteIO::RdU32LE(data + 4);
    uint32_t pmode = flags & 0x7u; // 0=4bpp CLUT, 1=8bpp CLUT, 2=16bpp BGR555, 3=24bpp RGB888
    if (pmode > 3u || (flags >> 4u) != 0u) return false;
    bool hasClut = (flags & 0x8u) != 0u;
    if ((pmode == 0u || pmode == 1u) && !hasClut) return false;

    size_t pos = 8;
    uint16_t clut[256] = {};
    size_t nCols = 0;
    if (hasClut) {
        if (pos + 12 > size) return false;
        uint32_t clutLen = ByteIO::RdU32LE(data + pos);
        if (clutLen < 12 || clutLen > size - pos) return false;
        nCols = std::min<size_t>(256, (static_cast<size_t>(clutLen) - 12u) / 2u);
        for (size_t i = 0; i < nCols; ++i) {
            clut[i] = ByteIO::RdU16LE(data + pos + 12u + i * 2u);
        }
        pos += clutLen;
    }

    if (pos + 12 > size) return false;
    uint32_t imgLen = ByteIO::RdU32LE(data + pos);
    uint16_t wWords = ByteIO::RdU16LE(data + pos + 8);
    uint16_t h      = ByteIO::RdU16LE(data + pos + 10);
    if (imgLen < 12 || imgLen > size - pos) return false;
    size_t imgDataLen = static_cast<size_t>(imgLen) - 12u;
    pos += 12;

    const size_t rowStrideBytes = static_cast<size_t>(wWords) * 2u;
    uint32_t w = wWords;
    if (pmode == 0u)      w = static_cast<uint32_t>(wWords) * 4u;
    else if (pmode == 1u) w = static_cast<uint32_t>(wWords) * 2u;
    else if (pmode == 3u) w = (static_cast<uint32_t>(wWords) * 2u) / 3u;

    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;
    if (rowStrideBytes * static_cast<size_t>(h) > imgDataLen || pos + imgDataLen > size) return false;

    const uint8_t* src = data + pos;

    // PS1 GPU STP (0x8000) rule: 0x0000 (R=G=B=0, STP=0) is transparent, while 0x8000 (R=G=B=0, STP=1) is opaque black.
    // Guard against degenerate all-zero files by enabling 0x0000 transparency when non-zero colors / STP bits are present.
    bool zeroIsTransparent = false;
    if (pmode <= 1u) {
        for (size_t i = 0; i < nCols; ++i) {
            if (clut[i] != 0u) { zeroIsTransparent = true; break; }
        }
    } else if (pmode == 2u) {
        for (uint32_t y = 0; y < h && !zeroIsTransparent; ++y) {
            const uint8_t* rp = src + static_cast<size_t>(y) * rowStrideBytes;
            for (uint32_t x = 0; x < w; ++x) {
                if (ByteIO::RdU16LE(rp + x * 2u) != 0u) { zeroIsTransparent = true; break; }
            }
        }
    }

    auto unpackPs1_16 = [zeroIsTransparent](uint16_t c, uint8_t* dst) {
        if (c == 0x0000u && zeroIsTransparent) {
            dst[0] = 0; dst[1] = 0; dst[2] = 0; dst[3] = 0;
            return;
        }
        uint8_t r = static_cast<uint8_t>(((c & 31u) * 255u + 15u) / 31u);
        uint8_t g = static_cast<uint8_t>((((c >> 5u) & 31u) * 255u + 15u) / 31u);
        uint8_t b = static_cast<uint8_t>((((c >> 10u) & 31u) * 255u + 15u) / 31u);
        dst[0] = b; dst[1] = g; dst[2] = r; dst[3] = 255u;
    };

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = (pmode == 3u || !zeroIsTransparent) ? 3u : 4u;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"PlayStation 1 TIM (.tim)";
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* rowPtr = src + static_cast<size_t>(y) * rowStrideBytes;
        uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * w * 4u];
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* dst = dstRow + static_cast<size_t>(x) * 4u;
            if (pmode == 3u) {
                dst[0] = rowPtr[x * 3u + 2u];
                dst[1] = rowPtr[x * 3u + 1u];
                dst[2] = rowPtr[x * 3u + 0u];
                dst[3] = 255u;
            } else if (pmode == 2u) {
                unpackPs1_16(ByteIO::RdU16LE(rowPtr + x * 2u), dst);
            } else if (pmode == 1u) {
                unpackPs1_16(clut[rowPtr[x]], dst);
            } else {
                uint8_t byte = rowPtr[x >> 1u];
                uint8_t nib = (x & 1u) ? (byte >> 4u) : (byte & 0x0Fu);
                unpackPs1_16(clut[nib], dst);
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
