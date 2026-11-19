#pragma once
// NamcoNut.h — Bandai Namco Texture Container Decoder (.nut)

namespace Formats {

// ============================================================================
// 4. Bandai Namco Texture Container (.nut — "NTP3" / "NTWU" / "NTWD")
// ============================================================================
inline bool DecodeNamcoNut(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64) return false;
    bool isBE = false;
    if (std::memcmp(data, "NTP3", 4) == 0 || std::memcmp(data, "NTWU", 4) == 0) isBE = true;
    else if (std::memcmp(data, "NTWD", 4) == 0) isBE = false;
    else return false;

    auto rd16 = [&](const uint8_t* p) { return isBE ? ConsoleTexDetail::RdU16BE(p) : ConsoleTexDetail::RdU16LE(p); };
    auto rd32 = [&](const uint8_t* p) { return isBE ? ConsoleTexDetail::RdU32BE(p) : ConsoleTexDetail::RdU32LE(p); };

    uint16_t numTex = rd16(data + 6);
    if (numTex == 0u || numTex > 2048u) return false;

    const uint8_t* th  = data + 16;
    uint32_t totalSz   = rd32(th + 0);
    uint32_t dataSz    = rd32(th + 8);
    uint16_t hdrSz     = rd16(th + 12);
    uint8_t  pixFmt    = th[19];
    uint16_t w         = rd16(th + 20);
    uint16_t h         = rd16(th + 22);

    if (hdrSz < 48u || w == 0 || h == 0 || w > 8192 || h > 8192 || 16ull + hdrSz > size) return false;
    size_t pixOff = 16u + hdrSz;
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

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(data + pixOff, avail, w, h, codec, outInfo.bgraPixels)) {
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
