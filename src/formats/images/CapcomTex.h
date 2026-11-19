#pragma once
// CapcomTex.h — Capcom RE Engine / MT Framework Texture Decoder (.tex)

namespace Formats {

// ============================================================================
// 5. Capcom RE Engine Texture (.tex — binary "TEX\0")
// ============================================================================
inline bool IsLikelyCapcomTexBinary(const uint8_t* data, size_t size) {
    return data && size >= 56 &&
           (std::memcmp(data, "TEX\0", 4) == 0 || std::memcmp(data, "\0XET", 4) == 0);
}

inline bool DecodeCapcomTex(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!IsLikelyCapcomTexBinary(data, size)) return false;
    if (std::memcmp(data, "TEX\0", 4) != 0) return false;

    uint32_t version = ConsoleTexDetail::RdU32LE(data + 4);
    uint16_t w       = ConsoleTexDetail::RdU16LE(data + 8);
    uint16_t h       = ConsoleTexDetail::RdU16LE(data + 10);
    uint32_t dxgiFmt = ConsoleTexDetail::RdU32LE(data + 16);
    if (version < 8u || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    // Mip 0 header (16 bytes at +40: uint64_t mipOffset, uint32_t pitch, uint32_t mipByteSize)
    if (size < 56u) return false;
    uint64_t mipOff  = ConsoleTexDetail::RdU64LE(data + 40);
    uint32_t mipSize = ConsoleTexDetail::RdU32LE(data + 52);
    if (mipOff < 56u || mipOff >= size) return false;
    size_t avail = size - static_cast<size_t>(mipOff);
    if (mipSize > 0u && mipSize <= avail) avail = mipSize;

    auto codec = ConsoleTexDetail::MapDxgiToSurfaceCodec(dxgiFmt);
    if (codec == ConsoleTexDetail::GpuSurfaceCodec::None) return false;

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(data + static_cast<size_t>(mipOff), avail, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = (codec == ConsoleTexDetail::GpuSurfaceCodec::RGBA16F ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_U ||
                              codec == ConsoleTexDetail::GpuSurfaceCodec::BC6H_S) ? 16u : 8u;
    outInfo.formatName = L"Capcom RE Engine Texture (.tex)";
    outInfo.valid = true;
    return true;
}


} // namespace Formats
