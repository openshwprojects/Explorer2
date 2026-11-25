#pragma once
// AtariDegasImage.h — Atari ST DEGAS (.pi1, .pi2, .pi3) & DEGAS Elite RLE (.pc1, .pc2, .pc3) Planar Bitplane Decoder

namespace Formats {

inline bool DecodeAtariDegasImage(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 36u) return false;

    const uint16_t resWord = ByteIO::RdU16BE(data + 0);
    const bool isCompressed = (resWord & 0x8000u) != 0u;
    const uint16_t res = resWord & 0x7FFFu;
    if (res > 2u) return false;

    // Uncompressed DEGAS (.pi1/.pi2/.pi3) is 34B header + 32,000B screen RAM (+ optional 32B color cycling trailer = 32,066B)
    if (!isCompressed && (size < 32034u || size > 32128u)) return false;

    uint32_t w = 320u, h = 200u, numPlanes = 4u;
    if (res == 1u)      { w = 640u; h = 200u; numPlanes = 2u; }
    else if (res == 2u) { w = 640u; h = 400u; numPlanes = 1u; }

    // Detect Atari STe 12-bit (4-bit unscrambling) vs classic Atari ST 9-bit (3-bit) palette
    bool isSte12Bit = false;
    uint16_t rawPal[16] = {};
    for (int i = 0; i < 16; ++i) {
        rawPal[i] = ByteIO::RdU16BE(data + 2u + static_cast<size_t>(i) * 2u);
        if ((rawPal[i] & 0xF000u) != 0u) {
            // High nibble bits must be zero in valid Atari ST / STe color hardware registers
            if (!isCompressed) return false;
        }
        if ((rawPal[i] & 0x0888u) != 0u) isSte12Bit = true;
    }

    auto unpackNibble = [&](uint16_t nib) -> uint8_t {
        if (isSte12Bit) {
            const uint32_t v4 = ((nib & 0x7u) << 1u) | ((nib >> 3u) & 0x1u);
            return static_cast<uint8_t>(v4 * 17u);
        }
        return static_cast<uint8_t>((nib & 0x7u) * 255u / 7u);
    };

    uint8_t palRgb[16][3] = {};
    for (int i = 0; i < 16; ++i) {
        palRgb[i][0] = unpackNibble((rawPal[i] >> 8u) & 0xFu);
        palRgb[i][1] = unpackNibble((rawPal[i] >> 4u) & 0xFu);
        palRgb[i][2] = unpackNibble( rawPal[i]        & 0xFu);
    }

    // Atari ST high-res monochrome monitor (SM124, res == 2) uses bit 0 of color 0 for inversion:
    // standard SM124 displays index 0 as white and index 1 as black unless color 0 bit 0 == 0 & color 1 != color 0
    if (res == 2u) {
        const bool inv = ((rawPal[0] & 0x1u) == 0u && rawPal[0] == rawPal[1]);
        palRgb[0][0] = palRgb[0][1] = palRgb[0][2] = inv ? 0u   : 255u;
        palRgb[1][0] = palRgb[1][1] = palRgb[1][2] = inv ? 255u : 0u;
    }

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 3u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = isCompressed
        ? (L"Atari ST DEGAS Elite Compressed (.pc" + std::to_wstring(res + 1u) + L")")
        : (L"Atari ST DEGAS Planar Image (.pi" + std::to_wstring(res + 1u) + L")");
    outInfo.bgraPixels.resize(static_cast<size_t>(w) * h * 4u);

    if (!isCompressed) {
        // Atari ST Shifter screen memory: 16-pixel groups stored as `numPlanes` interleaved big-endian 16-bit words
        const uint8_t* vram = data + 34u;
        const size_t groupsPerRow = w / 16u;
        const size_t rowStrideBytes = groupsPerRow * numPlanes * 2u; // Always 160 bytes for res 0/1, 80 bytes for res 2

        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* srcRow = vram + static_cast<size_t>(y) * rowStrideBytes;
            uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * w * 4u];
            for (size_t g = 0; g < groupsPerRow; ++g) {
                uint16_t words[4] = {};
                for (uint32_t p = 0; p < numPlanes; ++p) {
                    words[p] = ByteIO::RdU16BE(srcRow + (g * numPlanes + p) * 2u);
                }
                for (uint32_t bit = 0; bit < 16u; ++bit) {
                    const uint16_t mask = static_cast<uint16_t>(0x8000u >> bit);
                    uint8_t idx = 0;
                    for (uint32_t p = 0; p < numPlanes; ++p) {
                        if ((words[p] & mask) != 0u) idx |= static_cast<uint8_t>(1u << p);
                    }
                    const size_t x = g * 16u + bit;
                    WritePremulBGRA(dstRow + x * 4u, palRgb[idx][0], palRgb[idx][1], palRgb[idx][2], 255u);
                }
            }
        }
    } else {
        // DEGAS Elite (.pc1/.pc2/.pc3): per-scanline, per-plane PackBits RLE (planeRowBytes = w / 8)
        const size_t planeRowBytes = w / 8u;
        size_t pos = 34u;
        std::vector<uint8_t> planeRows[4];

        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t p = 0; p < numPlanes; ++p) {
                size_t consumed = 0;
                if (!::Explorer2::StreamCodecs::DecompressPackBits(
                        data + pos, size - pos, planeRows[p], planeRowBytes, &consumed))
                {
                    return false;
                }
                pos += consumed;
            }
            uint8_t* dstRow = &outInfo.bgraPixels[static_cast<size_t>(y) * w * 4u];
            for (uint32_t x = 0; x < w; ++x) {
                const size_t byteOff = x >> 3u;
                const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7u));
                uint8_t idx = 0;
                for (uint32_t p = 0; p < numPlanes; ++p) {
                    if ((planeRows[p][byteOff] & mask) != 0u) idx |= static_cast<uint8_t>(1u << p);
                }
                WritePremulBGRA(dstRow + x * 4u, palRgb[idx][0], palRgb[idx][1], palRgb[idx][2], 255u);
            }
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
