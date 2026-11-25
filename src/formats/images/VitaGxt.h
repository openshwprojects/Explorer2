#pragma once
// VitaGxt.h — PlayStation Vita SceGxm Texture + Morton Deswizzler + PVRTC + P4/P8 + 16-bit (.gxt)

namespace Formats {

// ============================================================================
// 2. PlayStation Vita SceGxm Texture (.gxt — "GXT\0")
// ============================================================================
inline bool DecodeVitaGxt(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64 || std::memcmp(data, "GXT\0", 4) != 0) return false;
    const uint32_t version     = ConsoleTexDetail::RdU32LE(data + 4);
    const uint32_t numTex      = ConsoleTexDetail::RdU32LE(data + 8);
    const uint32_t texDataBase = ConsoleTexDetail::RdU32LE(data + 12);
    const uint32_t texDataSize = ConsoleTexDetail::RdU32LE(data + 16);
    const uint32_t numP4Pal    = ConsoleTexDetail::RdU32LE(data + 20);
    const uint32_t numP8Pal    = ConsoleTexDetail::RdU32LE(data + 24);
    if ((version & 0xFFFF0000u) != 0x10000000u || numTex == 0 || numTex > 1024 || numP4Pal > 256 || numP8Pal > 256) {
        return false;
    }

    const uint8_t* ent       = data + 32;
    const uint32_t dataOff   = ConsoleTexDetail::RdU32LE(ent + 0);
    const uint32_t dataSz    = ConsoleTexDetail::RdU32LE(ent + 4);
    const int32_t  palIdx    = ConsoleTexDetail::RdI32LE(ent + 12);
    const uint32_t texType   = ConsoleTexDetail::RdU32LE(ent + 16); // 0x00000000 = Swizzled, 0x60000000 = Linear
    const uint32_t texFormat = ConsoleTexDetail::RdU32LE(ent + 20);
    const uint16_t w         = ConsoleTexDetail::RdU16LE(ent + 24);
    const uint16_t h         = ConsoleTexDetail::RdU16LE(ent + 26);

    if (w == 0 || h == 0 || w > 8192 || h > 8192 || dataOff < 64u || dataOff > size || dataSz == 0 || dataSz > size - dataOff) {
        return false;
    }

    const uint32_t baseFmt = texFormat & 0xFF000000u;
    const uint32_t swizFmt = texFormat & 0x0000F000u;
    const bool isSwizzled  = ((texType & 0xF0000000u) == 0x00000000u);

    auto finishVitaInfo = [&]() -> bool {
        outInfo.width = w;
        outInfo.height = h;
        outInfo.origWidth = w;
        outInfo.origHeight = h;
        outInfo.channels = 4u;
        outInfo.bitsPerChannel = 8u;
        outInfo.formatName = L"PlayStation Vita SceGxm Texture (.gxt)";
        outInfo.valid = true;
        return true;
    };

    // 1. PowerVR PVRTC / PVRTC2 (0x80000000 = 2bpp, 0x81000000 = 4bpp, 0x82000000 = II 2bpp, 0x83000000 = II 4bpp)
    if (baseFmt >= 0x80000000u && baseFmt <= 0x83000000u) {
        const bool is2bpp = (baseFmt == 0x80000000u || baseFmt == 0x82000000u);
        if (!GpuTex::DecodePvrtcSurface(data + dataOff, dataSz, w, h, is2bpp, false, outInfo.bgraPixels)) {
            return false;
        }
        return finishVitaInfo();
    }

    // 2. Paletted P4 (0x94000000) & P8 (0x95000000)
    if (baseFmt == 0x94000000u || baseFmt == 0x95000000u) {
        const bool isP4 = (baseFmt == 0x94000000u);
        const uint64_t totalP4Bytes = static_cast<uint64_t>(numP4Pal) * 64ull;
        const uint64_t totalP8Bytes = static_cast<uint64_t>(numP8Pal) * 1024ull;
        const uint64_t totalPalBytes = totalP4Bytes + totalP8Bytes;
        if (totalPalBytes == 0 || totalPalBytes > size) return false;

        // SceGxm GXT stores P4 palettes (64B each) then P8 palettes (1024B each) at the end of the data section / file
        size_t palBase = size - static_cast<size_t>(totalPalBytes);
        if (texDataBase >= 64u && static_cast<uint64_t>(texDataBase) + texDataSize <= size &&
            texDataSize >= totalPalBytes && (texDataBase + texDataSize - totalPalBytes) >= dataOff + dataSz)
        {
            palBase = static_cast<size_t>(texDataBase + texDataSize - totalPalBytes);
        }

        const uint32_t effPalIdx = (palIdx >= 0) ? static_cast<uint32_t>(palIdx) : 0u;
        size_t curPalOff = 0;
        size_t numColors = 0;
        if (isP4) {
            if (effPalIdx >= numP4Pal) return false;
            curPalOff = palBase + static_cast<size_t>(effPalIdx) * 64u;
            numColors = 16u;
        } else {
            if (effPalIdx >= numP8Pal) return false;
            curPalOff = palBase + static_cast<size_t>(totalP4Bytes) + static_cast<size_t>(effPalIdx) * 1024u;
            numColors = 256u;
        }
        if (curPalOff + numColors * 4u > size) return false;

        uint8_t lutRgba[256][4] = {};
        for (size_t c = 0; c < numColors; ++c) {
            const uint8_t* p = data + curPalOff + c * 4u;
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            switch (swizFmt) {
                case 0x1000u: b = p[0]; g = p[1]; r = p[2]; a = p[3]; break; // ARGB (LE memory: B,G,R,A)
                case 0x2000u: a = p[0]; b = p[1]; g = p[2]; r = p[3]; break; // RGBA
                case 0x3000u: a = p[0]; r = p[1]; g = p[2]; b = p[3]; break; // BGRA
                case 0x4000u: r = p[0]; g = p[1]; b = p[2]; a = 255u; break; // 1BGR
                case 0x5000u: b = p[0]; g = p[1]; r = p[2]; a = 255u; break; // 1RGB
                default:      r = p[0]; g = p[1]; b = p[2]; a = p[3]; break; // ABGR (LE memory: R,G,B,A)
            }
            lutRgba[c][0] = r; lutRgba[c][1] = g; lutRgba[c][2] = b; lutRgba[c][3] = a;
        }

        const size_t nPx = static_cast<size_t>(w) * h;
        std::vector<uint8_t> rawIndices;
        if (isP4) {
            const size_t needBytes = (nPx + 1u) / 2u;
            if (dataSz < needBytes) return false;
            rawIndices.resize(needBytes * 2u);
            for (size_t i = 0; i < needBytes; ++i) {
                const uint8_t byte = data[dataOff + i];
                rawIndices[i * 2u + 0u] = byte & 0x0Fu;
                rawIndices[i * 2u + 1u] = (byte >> 4u) & 0x0Fu;
            }
        } else {
            if (dataSz < nPx) return false;
            rawIndices.assign(data + dataOff, data + dataOff + dataSz);
        }

        std::vector<uint8_t> linearIdx;
        const uint8_t* idxPtr = rawIndices.data();
        if (isSwizzled) {
            if (!ConsoleTexDetail::DeswizzleMortonSurface(rawIndices.data(), rawIndices.size(), w, h, 1u, true, linearIdx)) {
                return false;
            }
            idxPtr = linearIdx.data();
        }

        outInfo.bgraPixels.resize(nPx * 4u);
        for (size_t i = 0; i < nPx; ++i) {
            const uint8_t ci = idxPtr[i] & (isP4 ? 0x0Fu : 0xFFu);
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], lutRgba[ci][0], lutRgba[ci][1], lutRgba[ci][2], lutRgba[ci][3]);
        }
        return finishVitaInfo();
    }

    // 3. 16-bit Direct Color Formats: 0x04000000 (U4U4U4U4), 0x05000000 (U1U5U5U5), 0x06000000 (U5U6U5)
    if (baseFmt == 0x04000000u || baseFmt == 0x05000000u || baseFmt == 0x06000000u) {
        const size_t nPx = static_cast<size_t>(w) * h;
        if (dataSz < nPx * 2u) return false;
        std::vector<uint8_t> linear16;
        const uint8_t* src16 = data + dataOff;
        if (isSwizzled) {
            if (!ConsoleTexDetail::DeswizzleMortonSurface(src16, dataSz, w, h, 2u, true, linear16)) return false;
            src16 = linear16.data();
        }
        outInfo.bgraPixels.resize(nPx * 4u);
        for (size_t i = 0; i < nPx; ++i) {
            const uint16_t v = ConsoleTexDetail::RdU16LE(src16 + i * 2u);
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            if (baseFmt == 0x06000000u) { // U5U6U5 (0x0000 = BGR, 0x1000 = RGB)
                uint8_t c0 = static_cast<uint8_t>(((v >> 11u) & 0x1Fu) * 255u / 31u);
                uint8_t c1 = static_cast<uint8_t>(((v >>  5u) & 0x3Fu) * 255u / 63u);
                uint8_t c2 = static_cast<uint8_t>(( v         & 0x1Fu) * 255u / 31u);
                if (swizFmt == 0x1000u) { r = c0; g = c1; b = c2; }
                else                    { b = c0; g = c1; r = c2; }
            } else if (baseFmt == 0x05000000u) { // U1U5U5U5
                if (swizFmt == 0x2000u || swizFmt == 0x3000u) { // RGBA / BGRA (alpha in bit 0)
                    uint8_t c0 = static_cast<uint8_t>(((v >> 11u) & 0x1Fu) * 255u / 31u);
                    uint8_t c1 = static_cast<uint8_t>(((v >>  6u) & 0x1Fu) * 255u / 31u);
                    uint8_t c2 = static_cast<uint8_t>(((v >>  1u) & 0x1Fu) * 255u / 31u);
                    a = (v & 1u) ? 255u : 0u;
                    if (swizFmt == 0x2000u) { r = c0; g = c1; b = c2; }
                    else                    { b = c0; g = c1; r = c2; }
                } else { // ABGR (0x0000) / ARGB (0x1000) / 1BGR / 1RGB (alpha in bit 15)
                    a = (swizFmt >= 0x4000u) ? 255u : ((v & 0x8000u) ? 255u : 0u);
                    uint8_t c0 = static_cast<uint8_t>(((v >> 10u) & 0x1Fu) * 255u / 31u);
                    uint8_t c1 = static_cast<uint8_t>(((v >>  5u) & 0x1Fu) * 255u / 31u);
                    uint8_t c2 = static_cast<uint8_t>(( v         & 0x1Fu) * 255u / 31u);
                    if (swizFmt == 0x1000u || swizFmt == 0x5000u) { r = c0; g = c1; b = c2; }
                    else                                          { b = c0; g = c1; r = c2; }
                }
            } else { // 0x04000000: U4U4U4U4
                uint8_t n3 = static_cast<uint8_t>(((v >> 12u) & 0x0Fu) * 17u);
                uint8_t n2 = static_cast<uint8_t>(((v >>  8u) & 0x0Fu) * 17u);
                uint8_t n1 = static_cast<uint8_t>(((v >>  4u) & 0x0Fu) * 17u);
                uint8_t n0 = static_cast<uint8_t>(( v         & 0x0Fu) * 17u);
                switch (swizFmt) {
                    case 0x1000u: a = n3; r = n2; g = n1; b = n0; break; // ARGB
                    case 0x2000u: r = n3; g = n2; b = n1; a = n0; break; // RGBA
                    case 0x3000u: b = n3; g = n2; r = n1; a = n0; break; // BGRA
                    case 0x5000u: a = 255u; r = n2; g = n1; b = n0; break;
                    default:      a = (swizFmt == 0x4000u) ? 255u : n3; b = n2; g = n1; r = n0; break; // ABGR
                }
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
        return finishVitaInfo();
    }

    // 4. 32-bit RGBA/BGRA & S3TC DXT1/DXT3/DXT5
    ConsoleTexDetail::GpuSurfaceCodec codec = ConsoleTexDetail::GpuSurfaceCodec::None;
    switch (baseFmt) {
        case 0x0C000000u:
            if (swizFmt == 0x1000u)      codec = ConsoleTexDetail::GpuSurfaceCodec::BGRA8;
            else if (swizFmt == 0x2000u) codec = ConsoleTexDetail::GpuSurfaceCodec::ABGR8;
            else if (swizFmt == 0x3000u) codec = ConsoleTexDetail::GpuSurfaceCodec::ARGB8;
            else                         codec = ConsoleTexDetail::GpuSurfaceCodec::RGBA8;
            break;
        case 0x85000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC1; break;
        case 0x86000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC2; break;
        case 0x87000000u: codec = ConsoleTexDetail::GpuSurfaceCodec::BC3; break;
        default: return false;
    }

    uint32_t blkW = 1, blkH = 1, bpb = 4;
    ConsoleTexDetail::GetCodecBlockLayout(codec, blkW, blkH, bpb);
    const uint32_t gridW = (w + blkW - 1u) / blkW;
    const uint32_t gridH = (h + blkH - 1u) / blkH;

    std::vector<uint8_t> linearBuf;
    const uint8_t* srcPtr = data + dataOff;
    size_t srcLen = dataSz;

    if (isSwizzled) {
        if (!ConsoleTexDetail::DeswizzleMortonSurface(srcPtr, srcLen, gridW, gridH, bpb, true, linearBuf)) {
            return false;
        }
        srcPtr = linearBuf.data();
        srcLen = linearBuf.size();
    }

    if (!ConsoleTexDetail::DecodeLinearGpuSurface(srcPtr, srcLen, w, h, codec, outInfo.bgraPixels)) {
        return false;
    }
    return finishVitaInfo();
}

} // namespace Formats
