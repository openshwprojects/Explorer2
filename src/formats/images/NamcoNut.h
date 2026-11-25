#pragma once
// NamcoNut.h — Bandai Namco Texture Container Decoder (.nut — NTP3 / NTWU GX2 / NTWD)

namespace Formats {

// ============================================================================
// 4. Bandai Namco Texture Container (.nut — "NTP3" / "NTWU" / "NTWD")
// ============================================================================
inline bool DecodeNamcoNut(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64) return false;
    bool isBE = false;
    const bool isWiiU = (std::memcmp(data, "NTWU", 4) == 0);
    if (std::memcmp(data, "NTP3", 4) == 0 || isWiiU) isBE = true;
    else if (std::memcmp(data, "NTWD", 4) == 0) isBE = false;
    else return false;

    auto rd16 = [&](const uint8_t* p) { return isBE ? ConsoleTexDetail::RdU16BE(p) : ConsoleTexDetail::RdU16LE(p); };
    auto rd32 = [&](const uint8_t* p) { return isBE ? ConsoleTexDetail::RdU32BE(p) : ConsoleTexDetail::RdU32LE(p); };

    const uint16_t numTex = rd16(data + 6);
    if (numTex == 0u || numTex > 2048u) return false;

    const uint8_t* th      = data + 16;
    const uint32_t totalSz = rd32(th + 0);
    const uint32_t dataSz  = rd32(th + 8);
    const uint16_t hdrSz   = rd16(th + 12);
    const uint8_t  pixFmt  = th[19];
    const uint16_t w       = rd16(th + 20);
    const uint16_t h       = rd16(th + 22);

    if (hdrSz < 48u || w == 0 || h == 0 || w > 8192u || h > 8192u || 16ull + hdrSz > size) return false;

    // NTWU stores an explicit data offset inside its extended header when multi-texture / GX2 blocks precede pixel data
    size_t pixOff = 16u + hdrSz;
    if (isWiiU && hdrSz >= 0x38u) {
        const uint32_t relDataOff = ConsoleTexDetail::RdU32BE(th + 0x20u);
        if (relDataOff >= hdrSz && 16ull + relDataOff < size) {
            pixOff = 16u + static_cast<size_t>(relDataOff);
        }
    }

    size_t avail = size - pixOff;
    if (dataSz > 0 && dataSz <= avail) avail = dataSz;
    else if (totalSz > hdrSz && (totalSz - hdrSz) <= avail) avail = totalSz - hdrSz;

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (pixFmt) {
        case 0:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 1:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 2:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 8:  codec = ConsoleTexDetail::GpuSurfaceCodec::RGB565; break;
        case 14: codec = isBE ? ConsoleTexDetail::GpuSurfaceCodec::ARGB8 : ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 17: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 21: codec = ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 22: codec = ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 23: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        default: return false;
    }

    const uint8_t* surfacePtr = data + pixOff;
    size_t surfaceLen = avail;
    std::vector<uint8_t> gx2Linear;

    // Inspect Wii U NTWU extended header for GX2Surface tileMode & swizzle ("GX2\0" / "GX2 " subblock or embedded GX2Surface)
    if (isWiiU) {
        uint32_t tileMode = 0;
        uint32_t swizzle  = 0;
        for (size_t s = 24u; s + 16u <= hdrSz; s += 4u) {
            if (std::memcmp(th + s, "GX2\0", 4) == 0 || std::memcmp(th + s, "GX2 ", 4) == 0) {
                // Standard 16-byte "GX2\0" subheader followed by GX2Surface:
                //   GX2Surface: [dim:4][width:4][height:4][depth:4][numMips:4][format:4][aa:4][use:4]
                //               [imageSize:4][imagePtr:4][mipSize:4][mipPtr:4][tileMode:4][swizzle:4]
                for (size_t surfBase : { s + 16u, s + 8u, s + 4u }) {
                    if (surfBase + 0x38u <= hdrSz &&
                        ConsoleTexDetail::RdU32BE(th + surfBase + 4u) == w &&
                        ConsoleTexDetail::RdU32BE(th + surfBase + 8u) == h)
                    {
                        tileMode = ConsoleTexDetail::RdU32BE(th + surfBase + 0x30u);
                        swizzle  = ConsoleTexDetail::RdU32BE(th + surfBase + 0x34u);
                        break;
                    }
                }
                if (tileMode == 0 && s + 24u <= hdrSz) {
                    // Compact GX2 subblock variant: ["GX2\0":4][size:4][hdrSz:4][pad:4][tileMode:4][swizzle:4]
                    uint32_t candTm = ConsoleTexDetail::RdU32BE(th + s + 16u);
                    if (candTm >= 1u && candTm <= 15u) {
                        tileMode = candTm;
                        swizzle  = ConsoleTexDetail::RdU32BE(th + s + 20u);
                    }
                }
                break;
            }
        }

        if (tileMode >= 1u && tileMode <= 15u) {
            uint32_t blkW = 1, blkH = 1, bpb = 4;
            ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
            const uint32_t elemW = (w + blkW - 1u) / blkW;
            const uint32_t elemH = (h + blkH - 1u) / blkH;
            const uint32_t swzBits = (swizzle >= 0x100u) ? ((swizzle >> 8u) & 7u) : (swizzle & 7u);
            const uint32_t pipeSwiz = swzBits & 1u;
            const uint32_t bankSwiz = (swzBits >> 1u) & 3u;
            if (!GpuTex::DeswizzleWiiUGx2Surface(
                    data + pixOff, size - pixOff, elemW, elemH, bpb,
                    tileMode, pipeSwiz, bankSwiz, gx2Linear))
            {
                return false;
            }
            surfacePtr = gx2Linear.data();
            surfaceLen = gx2Linear.size();
        }
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(surfacePtr, surfaceLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Bandai Namco NUT Texture (.nut)";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
