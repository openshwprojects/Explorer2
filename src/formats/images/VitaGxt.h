#pragma once
// VitaGxt.h — PlayStation Vita SceGxm Texture + Morton Z-Order Deswizzler (.gxt)

namespace Formats {

// ============================================================================
// 2. PlayStation Vita SceGxm Texture (.gxt — "GXT\0")
// ============================================================================
inline bool DecodeVitaGxt(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64 || std::memcmp(data, "GXT\0", 4) != 0) return false;
    uint32_t version = ConsoleTexDetail::RdU32LE(data + 4);
    uint32_t numTex  = ConsoleTexDetail::RdU32LE(data + 8);
    if ((version & 0xFFFF0000u) != 0x10000000u || numTex == 0 || numTex > 1024) return false;

    const uint8_t* ent = data + 32;
    uint32_t dataOff   = ConsoleTexDetail::RdU32LE(ent + 0);
    uint32_t dataSz    = ConsoleTexDetail::RdU32LE(ent + 4);
    uint32_t texType   = ConsoleTexDetail::RdU32LE(ent + 16); // 0x00000000 = Swizzled, 0x60000000 = Linear
    uint32_t texFormat = ConsoleTexDetail::RdU32LE(ent + 20);
    uint16_t w         = ConsoleTexDetail::RdU16LE(ent + 24);
    uint16_t h         = ConsoleTexDetail::RdU16LE(ent + 26);

    if (w == 0 || h == 0 || w > 8192 || h > 8192 || dataOff < 64u || dataOff > size || dataSz == 0 || dataSz > size - dataOff) {
        return false;
    }

    uint32_t baseFmt = texFormat & 0xFF000000u;
    uint32_t swizFmt = texFormat & 0x0000F000u;
    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (baseFmt) {
        case 0x0C000000u:
            if (swizFmt == 0x1000u)      codec = ConsoleTexDetail::GpuSurfaceCodec::BGRA8; // U8U8U8U8_ARGB in LE memory = B,G,R,A
            else if (swizFmt == 0x2000u) codec = ConsoleTexDetail::GpuSurfaceCodec::ABGR8;
            else if (swizFmt == 0x3000u) codec = ConsoleTexDetail::GpuSurfaceCodec::ARGB8;
            else                         codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; // U8U8U8U8_ABGR in LE memory = R,G,B,A
            break;
        case 0x85000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 0x86000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 0x87000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;

    std::vector<uint8_t> linearBuf;
    const uint8_t* srcPtr = data + dataOff;
    size_t srcLen = dataSz;

    if ((texType & 0xF0000000u) == 0x00000000u) { // SceGxm Morton Swizzled!
        if (!ConsoleTexDetail::DeswizzleMortonSurface(srcPtr, srcLen, gridW, gridH, bpb, true, linearBuf)) {
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
    outInfo.formatName = L"PlayStation Vita SceGxm Texture (.gxt)";
    outInfo.valid = true;
    return true;
}


} // namespace Formats
