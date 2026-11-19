#pragma once
// Source2VtexC.h — Valve Source 2 Compiled Texture Resource Decoder (.vtex_c)

namespace Formats {

// ============================================================================
// 6. Valve Source 2 Compiled Texture (.vtex_c — ResourceHeader v12 + "DATA")
// ============================================================================
inline bool DecodeSource2VtexC(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 68) return false;
    uint32_t hdrFileSize  = ConsoleTexDetail::RdU32LE(data + 0);
    uint16_t hdrVersion   = ConsoleTexDetail::RdU16LE(data + 4);
    uint32_t blockRelOff  = ConsoleTexDetail::RdU32LE(data + 8);
    uint32_t blockCount   = ConsoleTexDetail::RdU32LE(data + 12);
    (void)hdrFileSize;

    if (hdrVersion != 12u || blockCount == 0u || blockCount > 64u) return false;
    uint64_t blockDirOff = 8ull + blockRelOff;
    if (blockDirOff > size || static_cast<uint64_t>(blockCount) * 12ull > size - blockDirOff) return false;

    size_t dataBlkOff = SIZE_MAX, dataBlkSz = 0, maxBlkEnd = static_cast<size_t>(blockDirOff + blockCount * 12u);
    for (uint32_t i = 0; i < blockCount; ++i) {
        size_t entPos = static_cast<size_t>(blockDirOff) + static_cast<size_t>(i) * 12u;
        int32_t rel   = ConsoleTexDetail::RdI32LE(data + entPos + 4);
        uint32_t bsz  = ConsoleTexDetail::RdU32LE(data + entPos + 8);
        int64_t absOff = static_cast<int64_t>(entPos + 4u) + rel;
        if (absOff < 16 || static_cast<uint64_t>(absOff) > size || bsz > size - static_cast<size_t>(absOff)) {
            return false;
        }
        maxBlkEnd = std::max(maxBlkEnd, static_cast<size_t>(absOff) + bsz);
        if (std::memcmp(data + entPos, "DATA", 4) == 0) {
            dataBlkOff = static_cast<size_t>(absOff);
            dataBlkSz  = bsz;
        }
    }
    if (dataBlkOff == SIZE_MAX || dataBlkSz < 40u) return false;

    const uint8_t* vtex = data + dataBlkOff;
    uint16_t vtexVer    = ConsoleTexDetail::RdU16LE(vtex + 0);
    uint16_t w          = ConsoleTexDetail::RdU16LE(vtex + 20);
    uint16_t h          = ConsoleTexDetail::RdU16LE(vtex + 22);
    uint8_t  vtexFmt    = vtex[26];
    uint8_t  numMips    = vtex[27];
    if (vtexVer != 1u || w == 0 || h == 0 || w > 8192 || h > 8192 || numMips == 0 || numMips > 16) return false;

    size_t streamOff = maxBlkEnd;
    if (streamOff >= size) {
        // Fallback when pixel stream is appended inside the DATA block tail after 40-byte VTex_t header
        if (dataBlkOff + 40u < size) streamOff = dataBlkOff + 40u;
        else return false;
    }

    // Embedded PNG formats (16 = PNG_RGBA8888, 18 = PNG_DXT5)
    if (vtexFmt == 16u || vtexFmt == 18u) {
        for (size_t p = streamOff; p + 24 <= size; ++p) {
            if (std::memcmp(data + p, "\x89PNG\r\n\x1A\n", 8) == 0) {
                if (DetailImg100::DecodeEmbeddedWicBuffer(data + p, size - p, L"Valve Source 2 Texture (.vtex_c)", outInfo)) {
                    outInfo.origWidth = outInfo.width;
                    outInfo.origHeight = outInfo.height;
                    return true;
                }
            }
        }
        return false;
    }

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (vtexFmt) {
        case 1:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 2:  codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 3:  codec = ConsoleTexDetail::GpuSurfaceCodec::R8; break;
        case 4:  codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 11: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA16F; break;
        case 20: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        case 21: codec = ConsoleTexDetail::GpuSurfaceCodec::BC6H_U; break;
        case 22: codec = ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 23: codec = ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 27: codec = ConsoleTexDetail::GpuSurfaceCodec::ETC2_RGB; break;
        case 28: codec = ConsoleTexDetail::GpuSurfaceCodec::ETC2_RGBA; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint64_t mip0Bytes = static_cast<uint64_t>((w + blkW - 1u) / blkW) * ((h + blkH - 1u) / blkH) * bpb;
    if (mip0Bytes == 0 || mip0Bytes > size - streamOff) return false;

    // Source 2 stores mips smallest-to-largest at the end of the file! Mip 0 is the final mip0Bytes slice.
    size_t mip0Pos = size - static_cast<size_t>(mip0Bytes);
    if (mip0Pos < streamOff) mip0Pos = streamOff;

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(data + mip0Pos, static_cast<size_t>(mip0Bytes), w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_U) ? 16u : 8u;
    outInfo.formatName = L"Valve Source 2 Compiled Texture (.vtex_c)";
    outInfo.valid = true;
    return true;
}


} // namespace Formats
