#pragma once
// SwitchBntx.h — Nintendo Switch Binary Texture + Tegra X1 Block-Linear Deswizzler (.bntx)

namespace Formats {

// ============================================================================
// 1. Nintendo Switch Binary Texture (.bntx — "BNTX" + "NX  " + "BRTI")
// ============================================================================
inline bool DecodeSwitchBntx(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 144 || std::memcmp(data, "BNTX", 4) != 0) return false;
    uint16_t bom = ConsoleTexDetail::RdU16LE(data + 12);
    if (bom != 0xFEFFu) return false; // Switch NX is little-endian

    // Locate first "BRTI" block (via NX infoPtrArray at +40, or bounded scan)
    size_t brtiOff = SIZE_MAX;
    if (size >= 56 && std::memcmp(data + 32, "NX  ", 4) == 0) {
        uint32_t texCount = ConsoleTexDetail::RdU32LE(data + 36);
        uint64_t ptrArr   = ConsoleTexDetail::RdU64LE(data + 40);
        if (texCount > 0 && ptrArr >= 32 && ptrArr + 8ull <= size) {
            uint64_t cand = ConsoleTexDetail::RdU64LE(data + static_cast<size_t>(ptrArr));
            if (cand + 120ull <= size && std::memcmp(data + static_cast<size_t>(cand), "BRTI", 4) == 0) {
                brtiOff = static_cast<size_t>(cand);
            }
        }
    }
    if (brtiOff == SIZE_MAX) {
        for (size_t p = 32; p + 120 <= std::min<size_t>(size, 8192u); p += 4) {
            if (std::memcmp(data + p, "BRTI", 4) == 0) { brtiOff = p; break; }
        }
    }
    if (brtiOff == SIZE_MAX || brtiOff + 120 > size) return false;

    const uint8_t* brti   = data + brtiOff;
    uint16_t tileMode     = ConsoleTexDetail::RdU16LE(brti + 18); // 0 = Tegra GOB Block-Linear, 1 = Linear
    uint32_t bntxFmt      = ConsoleTexDetail::RdU32LE(brti + 28);
    uint32_t w            = ConsoleTexDetail::RdU32LE(brti + 36);
    uint32_t h            = ConsoleTexDetail::RdU32LE(brti + 40);
    uint32_t blkHeightLog2= ConsoleTexDetail::RdU32LE(brti + 52);
    uint32_t imageSize    = ConsoleTexDetail::RdU32LE(brti + 72);
    uint64_t namePtr      = ConsoleTexDetail::RdU64LE(brti + 96);
    uint64_t mipArrPtr    = ConsoleTexDetail::RdU64LE(brti + 112);

    if (w == 0 || h == 0 || w > 8192 || h > 8192 || mipArrPtr == 0 || mipArrPtr + 8ull > size) return false;
    uint64_t mip0Off = ConsoleTexDetail::RdU64LE(data + static_cast<size_t>(mipArrPtr));
    if (mip0Off == 0 || mip0Off >= size) return false;

    std::wstring texName;
    if (namePtr >= 32 && namePtr + 2ull < size) {
        uint16_t nLen = ConsoleTexDetail::RdU16LE(data + static_cast<size_t>(namePtr));
        if (nLen > 0 && nLen <= 256 && namePtr + 2ull + nLen <= size) {
            std::string ns(reinterpret_cast<const char*>(data + static_cast<size_t>(namePtr) + 2), nLen);
            texName = PathUtils::Utf8ToWide(ns);
            outInfo.layerNames.push_back(texName);
        }
    }

    uint32_t fmtKind = (bntxFmt >> 8) & 0xFFu;
    uint32_t fmtType = bntxFmt & 0xFFu;
    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (fmtKind) {
        case 0x02: codec = ConsoleTexDetail::GpuSurfaceCodec::R8; break;
        case 0x07: codec = ConsoleTexDetail::GpuSurfaceCodec::RGB565; break;
        case 0x0B: codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8; break;
        case 0x0C: codec = ConsoleTexDetail::GpuSurfaceCodec::BGRA8; break;
        case 0x1A: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 0x1B: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 0x1C: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        case 0x1D: codec = (fmtType == 2u) ? ConsoleTexDetail::GpuSurfaceCodec::BC4_S : ConsoleTexDetail::GpuSurfaceCodec::BC4_U; break;
        case 0x1E: codec = (fmtType == 2u) ? ConsoleTexDetail::GpuSurfaceCodec::BC5_S : ConsoleTexDetail::GpuSurfaceCodec::BC5_U; break;
        case 0x1F: codec = (fmtType == 5u) ? ConsoleTexDetail::GpuSurfaceCodec::BC6H_S : ConsoleTexDetail::GpuSurfaceCodec::BC6H_U; break;
        case 0x20: codec = ConsoleTexDetail::GpuSurfaceCodec::BC7; break;
        case 0x2D: codec = ConsoleTexDetail::GpuSurfaceCodec::ASTC_4x4; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    uint32_t gridW = (w + blkW - 1u) / blkW;
    uint32_t gridH = (h + blkH - 1u) / blkH;
    size_t avail = size - static_cast<size_t>(mip0Off);
    if (imageSize > 0 && imageSize < avail) avail = imageSize;
    const uint8_t* mip0Ptr = data + static_cast<size_t>(mip0Off);

    std::vector<uint8_t> linearBuf;
    const uint8_t* linearPtr = mip0Ptr;
    size_t linearLen = avail;

    if (tileMode == 0u) { // Tegra X1 Block-Linear GOB swizzled!
        if (!ConsoleTexDetail::DeswizzleTegraBlockLinear(mip0Ptr, avail, gridW, gridH, bpb, blkHeightLog2, linearBuf)) {
            return false;
        }
        linearPtr = linearBuf.data();
        linearLen = linearBuf.size();
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(linearPtr, linearLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Nintendo Switch BNTX Texture (.bntx" + (texName.empty() ? L"" : (L", " + texName)) + L")";
    outInfo.valid = true;
    return true;
}


} // namespace Formats
