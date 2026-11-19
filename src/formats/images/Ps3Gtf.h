#pragma once
// Ps3Gtf.h — PlayStation 3 Cell GCM Texture Decoder (.gtf)

namespace Formats {

// ============================================================================
// 3. PlayStation 3 Cell GCM Texture (.gtf — Big-Endian Header + CellGcmTexture)
// ============================================================================
inline bool DecodePs3Gtf(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 44) return false;
    uint32_t version = ConsoleTexDetail::RdU32BE(data + 0);
    uint32_t numTex  = ConsoleTexDetail::RdU32BE(data + 8);
    uint8_t  verMajor = static_cast<uint8_t>((version >> 24) & 0xFFu);
    if ((verMajor != 1u && verMajor != 2u) || (version & 0xFFFFu) != 0u || numTex == 0u || numTex > 256u) {
        return false;
    }

    // Entry 0 starts at +12 (12-byte GTF header: version[4], size[4], numTex[4])
    // Note: some GTF packers pad header to 16 bytes when id==0 at +12 and offset is at +16; check +16 vs +20!
    size_t entOff = 12u;
    uint32_t texOff = ConsoleTexDetail::RdU32BE(data + entOff + 4);
    uint32_t texSz  = ConsoleTexDetail::RdU32BE(data + entOff + 8);
    if (texOff < 36u || texOff > size || texSz == 0u || texSz > size - texOff) {
        entOff = 16u;
        if (entOff + 28u > size) return false;
        texOff = ConsoleTexDetail::RdU32BE(data + entOff + 4);
        texSz  = ConsoleTexDetail::RdU32BE(data + entOff + 8);
        if (texOff < 40u || texOff > size || texSz == 0u || texSz > size - texOff) return false;
    }

    const uint8_t* attr = data + entOff + 12u; // CellGcmTexture (24 bytes)
    uint8_t rawFmt = attr[0];
    uint8_t dim    = attr[2];
    uint16_t w     = ConsoleTexDetail::RdU16BE(attr + 8);
    uint16_t h     = ConsoleTexDetail::RdU16BE(attr + 10);
    if ((dim != 1u && dim != 2u && dim != 3u) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    bool isLinear = (rawFmt & 0x20u) != 0u;
    uint8_t baseFmt = static_cast<uint8_t>(rawFmt & ~(0x20u | 0x40u));

    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (baseFmt) {
        case 0x81u: codec = ConsoleTexDetail::GpuSurfaceCodec::R8;    break;
        case 0x85u: codec = ConsoleTexDetail::GpuSurfaceCodec::ARGB8; break; // CELL_GCM_TEXTURE_A8R8G8B8
        case 0x86u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1;   break; // CELL_GCM_TEXTURE_COMPRESSED_DXT1
        case 0x87u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2;   break; // CELL_GCM_TEXTURE_COMPRESSED_DXT23
        case 0x88u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3;   break; // CELL_GCM_TEXTURE_COMPRESSED_DXT45
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;

    std::vector<uint8_t> linearBuf;
    const uint8_t* srcPtr = data + texOff;
    size_t srcLen = texSz;

    if (!isLinear && blkW == 1u) { // PS3 CellGcm SZ Morton swizzle on uncompressed texels
        if (!ConsoleTexDetail::DeswizzleMortonSurface(srcPtr, srcLen, gridW, gridH, bpb, false, linearBuf)) {
            return false;
        }
        srcPtr = linearBuf.data();
        srcLen = linearBuf.size();
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(srcPtr, srcLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"PlayStation 3 Cell GCM Texture (.gtf)";
    outInfo.valid = true;
    return true;
}


} // namespace Formats
