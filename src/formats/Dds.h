#pragma once

// ============================================================================
// 7. DirectDraw Surface (.dds) — Full BC1..BC7 + DX10 Float16/Float32/HDR + Uncompressed
// ============================================================================
bool DecodeDDS(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 128 || std::memcmp(data, "DDS ", 4) != 0) return false;
    if (ReadU32LE(data + 4) != 124) return false;

    uint32_t height = ReadU32LE(data + 12);
    uint32_t width  = ReadU32LE(data + 16);
    if (width == 0 || height == 0 || width > kMaxRasterDimension || height > kMaxRasterDimension) return false;

    uint32_t pfSize  = ReadU32LE(data + 76);
    uint32_t pfFlags = ReadU32LE(data + 80);
    uint32_t fourCC  = ReadU32LE(data + 84);
    uint32_t rgbBits = ReadU32LE(data + 88);
    uint32_t rMask   = ReadU32LE(data + 92);
    uint32_t gMask   = ReadU32LE(data + 96);
    uint32_t bMask   = ReadU32LE(data + 100);
    uint32_t aMask   = ReadU32LE(data + 104);
    if (pfSize != 32) return false;

    enum class DdsCodec {
        None,
        BC1, BC2, BC3,
        BC4_U, BC4_S,
        BC5_U, BC5_S,
        BC6H_U, BC6H_S,
        BC7,
        R32G32B32A32_F, R32G32B32_F, R32G32_F, R32_F,
        R16G16B16A16_F, R16G16_F, R16_F,
        R16G16B16A16_U, R16G16B16A16_S, R16G16_U, R16_U,
        R11G11B10_F, R9G9B9E5, R10G10B10A2_U,
        R8G8_U, A8_U, L8_U, L8A8_U,
        UncompressedInt
    };

    DdsCodec codec = DdsCodec::None;
    std::wstring fmtLabel;
    size_t offset = 128;

    if ((pfFlags & 0x4u) != 0 && fourCC == 0x30315844u /* 'DX10' */) {
        if (size < 148) return false;
        uint32_t dxgi = ReadU32LE(data + 128);
        uint32_t resDim = ReadU32LE(data + 132);
        if (resDim != 0 && (resDim < 2 || resDim > 4)) return false;
        offset = 148;

        switch (dxgi) {
            case 1: case 2:   codec = DdsCodec::R32G32B32A32_F; fmtLabel = L"DDS DX10 (R32G32B32A32_FLOAT HDR)"; break;
            case 5: case 6:   codec = DdsCodec::R32G32B32_F;    fmtLabel = L"DDS DX10 (R32G32B32_FLOAT HDR)"; break;
            case 9: case 10:  codec = DdsCodec::R16G16B16A16_F; fmtLabel = L"DDS DX10 (R16G16B16A16_FLOAT HDR)"; break;
            case 11:          codec = DdsCodec::R16G16B16A16_U; fmtLabel = L"DDS DX10 (R16G16B16A16_UNORM)"; break;
            case 13:          codec = DdsCodec::R16G16B16A16_S; fmtLabel = L"DDS DX10 (R16G16B16A16_SNORM)"; break;
            case 15: case 16: codec = DdsCodec::R32G32_F;       fmtLabel = L"DDS DX10 (R32G32_FLOAT)"; break;
            case 23: case 24: codec = DdsCodec::R10G10B10A2_U;  fmtLabel = L"DDS DX10 (R10G10B10A2_UNORM)"; break;
            case 26:          codec = DdsCodec::R11G11B10_F;    fmtLabel = L"DDS DX10 (R11G11B10_FLOAT HDR)"; break;
            case 27: case 28: case 29: case 30: case 31: case 32:
                codec = DdsCodec::UncompressedInt; rgbBits = 32;
                rMask = 0x000000FFu; gMask = 0x0000FF00u; bMask = 0x00FF0000u; aMask = 0xFF000000u;
                fmtLabel = L"DDS DX10 (R8G8B8A8)";
                break;
            case 33: case 34: codec = DdsCodec::R16G16_F;       fmtLabel = L"DDS DX10 (R16G16_FLOAT)"; break;
            case 35:          codec = DdsCodec::R16G16_U;       fmtLabel = L"DDS DX10 (R16G16_UNORM)"; break;
            case 39: case 40: case 41: codec = DdsCodec::R32_F; fmtLabel = L"DDS DX10 (R32_FLOAT)"; break;
            case 48: case 49: codec = DdsCodec::R8G8_U;         fmtLabel = L"DDS DX10 (R8G8_UNORM)"; break;
            case 53: case 54: codec = DdsCodec::R16_F;          fmtLabel = L"DDS DX10 (R16_FLOAT)"; break;
            case 55: case 56: codec = DdsCodec::R16_U;          fmtLabel = L"DDS DX10 (R16_UNORM)"; break;
            case 60: case 61: case 62: codec = DdsCodec::L8_U;  fmtLabel = L"DDS DX10 (R8_UNORM)"; break;
            case 65:          codec = DdsCodec::A8_U;           fmtLabel = L"DDS DX10 (A8_UNORM)"; break;
            case 67:          codec = DdsCodec::R9G9B9E5;       fmtLabel = L"DDS DX10 (R9G9B9E5_SHAREDEXP HDR)"; break;
            case 70: case 71: case 72: codec = DdsCodec::BC1;   fmtLabel = L"DDS DX10 (BC1 / DXT1)"; break;
            case 73: case 74: case 75: codec = DdsCodec::BC2;   fmtLabel = L"DDS DX10 (BC2 / DXT3)"; break;
            case 76: case 77: case 78: codec = DdsCodec::BC3;   fmtLabel = L"DDS DX10 (BC3 / DXT5)"; break;
            case 79: case 80: codec = DdsCodec::BC4_U;          fmtLabel = L"DDS DX10 (BC4_UNORM)"; break;
            case 81:          codec = DdsCodec::BC4_S;          fmtLabel = L"DDS DX10 (BC4_SNORM)"; break;
            case 82: case 83: codec = DdsCodec::BC5_U;          fmtLabel = L"DDS DX10 (BC5_UNORM Normal)"; break;
            case 84:          codec = DdsCodec::BC5_S;          fmtLabel = L"DDS DX10 (BC5_SNORM Normal)"; break;
            case 85:
                codec = DdsCodec::UncompressedInt; rgbBits = 16;
                rMask = 0xF800u; gMask = 0x07E0u; bMask = 0x001Fu; aMask = 0u;
                fmtLabel = L"DDS DX10 (B5G6R5_UNORM)";
                break;
            case 86:
                codec = DdsCodec::UncompressedInt; rgbBits = 16;
                rMask = 0x7C00u; gMask = 0x03E0u; bMask = 0x001Fu; aMask = 0x8000u;
                fmtLabel = L"DDS DX10 (B5G5R5A1_UNORM)";
                break;
            case 87: case 90: case 91:
                codec = DdsCodec::UncompressedInt; rgbBits = 32;
                rMask = 0x00FF0000u; gMask = 0x0000FF00u; bMask = 0x000000FFu; aMask = 0xFF000000u;
                fmtLabel = L"DDS DX10 (B8G8R8A8)";
                break;
            case 88: case 92: case 93:
                codec = DdsCodec::UncompressedInt; rgbBits = 32;
                rMask = 0x00FF0000u; gMask = 0x0000FF00u; bMask = 0x000000FFu; aMask = 0u;
                fmtLabel = L"DDS DX10 (B8G8R8X8)";
                break;
            case 94: case 95: codec = DdsCodec::BC6H_U;         fmtLabel = L"DDS DX10 (BC6H_UF16 HDR)"; break;
            case 96:          codec = DdsCodec::BC6H_S;         fmtLabel = L"DDS DX10 (BC6H_SF16 HDR)"; break;
            case 97: case 98: case 99: codec = DdsCodec::BC7;   fmtLabel = L"DDS DX10 (BC7_UNORM)"; break;
            default: return false;
        }
    } else if ((pfFlags & 0x4u) != 0) {
        switch (fourCC) {
            case 0x31545844u: codec = DdsCodec::BC1;            fmtLabel = L"DDS (DXT1 / BC1)"; break;
            case 0x32545844u:
            case 0x33545844u: codec = DdsCodec::BC2;            fmtLabel = L"DDS (DXT3 / BC2)"; break;
            case 0x34545844u:
            case 0x35545844u: codec = DdsCodec::BC3;            fmtLabel = L"DDS (DXT5 / BC3)"; break;
            case 0x31495441u: // 'ATI1'
            case 0x55344342u: codec = DdsCodec::BC4_U;          fmtLabel = L"DDS (ATI1 / BC4_UNORM)"; break;
            case 0x53344342u: codec = DdsCodec::BC4_S;          fmtLabel = L"DDS (BC4_SNORM)"; break;
            case 0x32495441u: // 'ATI2'
            case 0x55354342u: codec = DdsCodec::BC5_U;          fmtLabel = L"DDS (ATI2 / BC5_UNORM Normal)"; break;
            case 0x53354342u: codec = DdsCodec::BC5_S;          fmtLabel = L"DDS (BC5_SNORM Normal)"; break;
            case 36u:         codec = DdsCodec::R16G16B16A16_U; fmtLabel = L"DDS (A16B16G16R16_UNORM)"; break;
            case 110u:        codec = DdsCodec::R16G16B16A16_S; fmtLabel = L"DDS (Q16W16V16U16_SNORM)"; break;
            case 111u:        codec = DdsCodec::R16_F;          fmtLabel = L"DDS (R16F)"; break;
            case 112u:        codec = DdsCodec::R16G16_F;       fmtLabel = L"DDS (G16R16F)"; break;
            case 113u:        codec = DdsCodec::R16G16B16A16_F; fmtLabel = L"DDS (A16B16G16R16F HDR)"; break;
            case 114u:        codec = DdsCodec::R32_F;          fmtLabel = L"DDS (R32F)"; break;
            case 115u:        codec = DdsCodec::R32G32_F;       fmtLabel = L"DDS (G32R32F)"; break;
            case 116u:        codec = DdsCodec::R32G32B32A32_F; fmtLabel = L"DDS (A32B32G32R32F HDR)"; break;
            default: return false;
        }
    } else if ((pfFlags & 0x20000u) != 0) { // DDPF_LUMINANCE
        if (rgbBits == 8) {
            codec = DdsCodec::L8_U; fmtLabel = L"DDS (L8 Luminance)";
        } else if (rgbBits == 16 && (pfFlags & 0x1u) != 0) {
            codec = DdsCodec::L8A8_U; fmtLabel = L"DDS (L8A8 Luminance+Alpha)";
        } else if (rgbBits == 16) {
            codec = DdsCodec::R16_U; fmtLabel = L"DDS (L16 Luminance)";
        } else {
            return false;
        }
    } else if ((pfFlags & 0x2u) != 0 && rgbBits == 8) { // DDPF_ALPHA
        codec = DdsCodec::A8_U; fmtLabel = L"DDS (A8 Alpha)";
    } else {
        if (rgbBits == 32 && rMask == 0x3FF00000u && gMask == 0x000FFC00u && bMask == 0x000003FFu) {
            codec = DdsCodec::R10G10B10A2_U; fmtLabel = L"DDS (A2R10G10B10)";
        } else if (rgbBits == 32 && rMask == 0x000003FFu && gMask == 0x000FFC00u && bMask == 0x3FF00000u) {
            codec = DdsCodec::R10G10B10A2_U; fmtLabel = L"DDS (A2B10G10R10)";
        } else {
            codec = DdsCodec::UncompressedInt;
            fmtLabel = L"DDS (Uncompressed " + std::to_wstring(rgbBits) + L"-bit)";
        }
    }

    size_t numPixels = static_cast<size_t>(width) * height;
    outInfo.width = width;
    outInfo.height = height;
    outInfo.origWidth = width;
    outInfo.origHeight = height;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = fmtLabel;

    // 1. Block-compressed LDR / Normal formats (BC1..BC5, BC7)
    if (codec == DdsCodec::BC1 || codec == DdsCodec::BC2 || codec == DdsCodec::BC3 ||
        codec == DdsCodec::BC4_U || codec == DdsCodec::BC4_S ||
        codec == DdsCodec::BC5_U || codec == DdsCodec::BC5_S || codec == DdsCodec::BC7)
    {
        uint32_t blocksX = (width + 3u) / 4u;
        uint32_t blocksY = (height + 3u) / 4u;
        size_t blockBytes = (codec == DdsCodec::BC1 || codec == DdsCodec::BC4_U || codec == DdsCodec::BC4_S) ? 8u : 16u;
        size_t totalBytes = static_cast<size_t>(blocksX) * blocksY * blockBytes;
        if (offset > size || totalBytes > size - offset) return false;

        outInfo.bgraPixels.assign(numPixels * 4, 0);
        const uint8_t* ptr = data + offset;
        uint8_t blockRgba[64];
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                switch (codec) {
                    case DdsCodec::BC1:   GpuTex::DecodeBC1Block(ptr, blockRgba, true, false); break;
                    case DdsCodec::BC2:   GpuTex::DecodeBC2Block(ptr, blockRgba); break;
                    case DdsCodec::BC3:   GpuTex::DecodeBC3Block(ptr, blockRgba); break;
                    case DdsCodec::BC4_U: GpuTex::DecodeBC4Block(ptr, blockRgba, false); break;
                    case DdsCodec::BC4_S: GpuTex::DecodeBC4Block(ptr, blockRgba, true); break;
                    case DdsCodec::BC5_U: GpuTex::DecodeBC5Block(ptr, blockRgba, false); break;
                    case DdsCodec::BC5_S: GpuTex::DecodeBC5Block(ptr, blockRgba, true); break;
                    case DdsCodec::BC7:   GpuTex::DecodeBC7Block(ptr, blockRgba); break;
                    default: break;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, width, height, outInfo.bgraPixels);
                ptr += blockBytes;
            }
        }
        outInfo.valid = true;
        return true;
    }

    // 2. Block-compressed HDR BC6H (UF16 / SF16)
    if (codec == DdsCodec::BC6H_U || codec == DdsCodec::BC6H_S) {
        uint32_t blocksX = (width + 3u) / 4u;
        uint32_t blocksY = (height + 3u) / 4u;
        size_t totalBytes = static_cast<size_t>(blocksX) * blocksY * 16u;
        if (offset > size || totalBytes > size - offset) return false;

        std::vector<float> hdrRgba(numPixels * 4, 0.0f);
        const uint8_t* ptr = data + offset;
        float blockFloat[64];
        bool isSigned = (codec == DdsCodec::BC6H_S);
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                GpuTex::DecodeBC6HBlock(ptr, blockFloat, isSigned);
                for (uint32_t py = 0; py < 4; ++py) {
                    uint32_t dy = by * 4 + py;
                    if (dy >= height) continue;
                    for (uint32_t px = 0; px < 4; ++px) {
                        uint32_t dx = bx * 4 + px;
                        if (dx >= width) continue;
                        std::memcpy(&hdrRgba[(static_cast<size_t>(dy) * width + dx) * 4],
                                    &blockFloat[(py * 4 + px) * 4], 4 * sizeof(float));
                    }
                }
                ptr += 16;
            }
        }
        outInfo.bitsPerChannel = 16;
        GpuTex::ConvertFloatRgbaToBgra(hdrRgba.data(), width, height, false, outInfo.bgraPixels);
        outInfo.valid = true;
        return true;
    }

    // 3. Floating-point / Shared-Exponent HDR formats
    if (codec == DdsCodec::R32G32B32A32_F || codec == DdsCodec::R32G32B32_F ||
        codec == DdsCodec::R32G32_F       || codec == DdsCodec::R32_F       ||
        codec == DdsCodec::R16G16B16A16_F || codec == DdsCodec::R16G16_F    ||
        codec == DdsCodec::R16_F          || codec == DdsCodec::R11G11B10_F ||
        codec == DdsCodec::R9G9B9E5)
    {
        size_t bpp = 0;
        switch (codec) {
            case DdsCodec::R32G32B32A32_F: bpp = 16; outInfo.bitsPerChannel = 32; break;
            case DdsCodec::R32G32B32_F:    bpp = 12; outInfo.bitsPerChannel = 32; break;
            case DdsCodec::R32G32_F:       bpp = 8;  outInfo.bitsPerChannel = 32; break;
            case DdsCodec::R32_F:          bpp = 4;  outInfo.bitsPerChannel = 32; break;
            case DdsCodec::R16G16B16A16_F: bpp = 8;  outInfo.bitsPerChannel = 16; break;
            case DdsCodec::R16G16_F:       bpp = 4;  outInfo.bitsPerChannel = 16; break;
            case DdsCodec::R16_F:          bpp = 2;  outInfo.bitsPerChannel = 16; break;
            case DdsCodec::R11G11B10_F:
            case DdsCodec::R9G9B9E5:       bpp = 4;  outInfo.bitsPerChannel = 16; break;
            default: break;
        }
        if (offset > size || numPixels * bpp > size - offset) return false;

        std::vector<float> hdrRgba(numPixels * 4, 0.0f);
        const uint8_t* ptr = data + offset;
        for (size_t i = 0; i < numPixels; ++i, ptr += bpp) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
            switch (codec) {
                case DdsCodec::R32G32B32A32_F:
                    r = GpuTex::ReadF32LE(ptr + 0); g = GpuTex::ReadF32LE(ptr + 4);
                    b = GpuTex::ReadF32LE(ptr + 8); a = GpuTex::ReadF32LE(ptr + 12);
                    break;
                case DdsCodec::R32G32B32_F:
                    r = GpuTex::ReadF32LE(ptr + 0); g = GpuTex::ReadF32LE(ptr + 4);
                    b = GpuTex::ReadF32LE(ptr + 8);
                    break;
                case DdsCodec::R32G32_F:
                    r = GpuTex::ReadF32LE(ptr + 0); g = GpuTex::ReadF32LE(ptr + 4);
                    break;
                case DdsCodec::R32_F:
                    r = g = b = GpuTex::ReadF32LE(ptr);
                    break;
                case DdsCodec::R16G16B16A16_F:
                    r = GpuTex::HalfToFloat(ReadU16LE(ptr + 0)); g = GpuTex::HalfToFloat(ReadU16LE(ptr + 2));
                    b = GpuTex::HalfToFloat(ReadU16LE(ptr + 4)); a = GpuTex::HalfToFloat(ReadU16LE(ptr + 6));
                    break;
                case DdsCodec::R16G16_F:
                    r = GpuTex::HalfToFloat(ReadU16LE(ptr + 0)); g = GpuTex::HalfToFloat(ReadU16LE(ptr + 2));
                    break;
                case DdsCodec::R16_F:
                    r = g = b = GpuTex::HalfToFloat(ReadU16LE(ptr));
                    break;
                case DdsCodec::R11G11B10_F:
                    GpuTex::UnpackR11G11B10Float(ReadU32LE(ptr), r, g, b);
                    break;
                case DdsCodec::R9G9B9E5:
                    GpuTex::UnpackRgb9E5(ReadU32LE(ptr), r, g, b);
                    break;
                default: break;
            }
            hdrRgba[i * 4 + 0] = r; hdrRgba[i * 4 + 1] = g;
            hdrRgba[i * 4 + 2] = b; hdrRgba[i * 4 + 3] = a;
        }
        GpuTex::ConvertFloatRgbaToBgra(hdrRgba.data(), width, height, false, outInfo.bgraPixels);
        outInfo.valid = true;
        return true;
    }

    // 4. Special 16-bit / 10-10-10-2 / 2-channel integer formats
    if (codec != DdsCodec::UncompressedInt) {
        size_t bpp = 0;
        switch (codec) {
            case DdsCodec::R16G16B16A16_U:
            case DdsCodec::R16G16B16A16_S: bpp = 8; outInfo.bitsPerChannel = 16; break;
            case DdsCodec::R16G16_U:
            case DdsCodec::R10G10B10A2_U:  bpp = 4; outInfo.bitsPerChannel = (codec == DdsCodec::R16G16_U) ? 16 : 10; break;
            case DdsCodec::R16_U:
            case DdsCodec::R8G8_U:
            case DdsCodec::L8A8_U:         bpp = 2; outInfo.bitsPerChannel = (codec == DdsCodec::R16_U) ? 16 : 8; break;
            case DdsCodec::A8_U:
            case DdsCodec::L8_U:           bpp = 1; break;
            default: return false;
        }
        if (offset > size || numPixels * bpp > size - offset) return false;

        outInfo.bgraPixels.resize(numPixels * 4);
        const uint8_t* ptr = data + offset;
        bool swap10 = (rMask == 0x3FF00000u);
        for (size_t i = 0; i < numPixels; ++i, ptr += bpp) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (codec) {
                case DdsCodec::R16G16B16A16_U:
                    r = static_cast<uint8_t>((ReadU16LE(ptr + 0) + 128u) / 257u);
                    g = static_cast<uint8_t>((ReadU16LE(ptr + 2) + 128u) / 257u);
                    b = static_cast<uint8_t>((ReadU16LE(ptr + 4) + 128u) / 257u);
                    a = static_cast<uint8_t>((ReadU16LE(ptr + 6) + 128u) / 257u);
                    break;
                case DdsCodec::R16G16B16A16_S: {
                    auto sn16 = [](uint16_t raw) -> uint8_t {
                        int s = std::clamp(static_cast<int>(static_cast<int16_t>(raw)), -32767, 32767);
                        return static_cast<uint8_t>(((s + 32767) * 255 + 32767) / 65534);
                    };
                    r = sn16(ReadU16LE(ptr + 0)); g = sn16(ReadU16LE(ptr + 2));
                    b = sn16(ReadU16LE(ptr + 4)); a = sn16(ReadU16LE(ptr + 6));
                    break;
                }
                case DdsCodec::R16G16_U:
                    r = static_cast<uint8_t>((ReadU16LE(ptr + 0) + 128u) / 257u);
                    g = static_cast<uint8_t>((ReadU16LE(ptr + 2) + 128u) / 257u);
                    break;
                case DdsCodec::R10G10B10A2_U: {
                    uint32_t v = ReadU32LE(ptr);
                    uint8_t c0 = static_cast<uint8_t>(((v & 0x3FFu) * 255u + 511u) / 1023u);
                    uint8_t c1 = static_cast<uint8_t>((((v >> 10) & 0x3FFu) * 255u + 511u) / 1023u);
                    uint8_t c2 = static_cast<uint8_t>((((v >> 20) & 0x3FFu) * 255u + 511u) / 1023u);
                    r = swap10 ? c2 : c0; g = c1; b = swap10 ? c0 : c2;
                    a = static_cast<uint8_t>(((v >> 30) & 3u) * 85u);
                    break;
                }
                case DdsCodec::R16_U:
                    r = g = b = static_cast<uint8_t>((ReadU16LE(ptr) + 128u) / 257u);
                    break;
                case DdsCodec::R8G8_U:
                    r = ptr[0]; g = ptr[1];
                    break;
                case DdsCodec::L8A8_U:
                    r = g = b = ptr[0]; a = ptr[1];
                    break;
                case DdsCodec::L8_U:
                    r = g = b = ptr[0];
                    break;
                case DdsCodec::A8_U:
                    r = g = b = 255; a = ptr[0];
                    break;
                default: break;
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        }
        outInfo.valid = true;
        return true;
    }

    // 5. Standard uncompressed RGB / RGBA / Luminance (8, 16, 24, 32-bit)
    uint32_t bytesPerPx = (rgbBits >= 8 && (rgbBits % 8) == 0) ? (rgbBits / 8) : 0;
    if (bytesPerPx == 0 || bytesPerPx > 4) return false;
    if (offset > size || numPixels * bytesPerPx > size - offset) return false;

    outInfo.bgraPixels.resize(numPixels * 4);
    const uint8_t* ptr = data + offset;
    bool swapRB = (rMask == 0x000000FFu);
    for (size_t i = 0; i < numPixels; ++i) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        if (bytesPerPx == 4) {
            b = swapRB ? ptr[2] : ptr[0];
            g = ptr[1];
            r = swapRB ? ptr[0] : ptr[2];
            a = (aMask != 0) ? ptr[3] : 255;
        } else if (bytesPerPx == 3) {
            b = swapRB ? ptr[2] : ptr[0];
            g = ptr[1];
            r = swapRB ? ptr[0] : ptr[2];
        } else if (bytesPerPx == 2) {
            uint16_t v16 = ReadU16LE(ptr);
            if (gMask == 0x07E0u) {
                GpuTex::UnpackRgb565(v16, r, g, b);
            } else if (rMask == 0x0F00u && gMask == 0x00F0u && bMask == 0x000Fu) {
                r = static_cast<uint8_t>(((v16 >> 8) & 0xFu) * 17u);
                g = static_cast<uint8_t>(((v16 >> 4) & 0xFu) * 17u);
                b = static_cast<uint8_t>((v16 & 0xFu) * 17u);
                if (aMask != 0) a = static_cast<uint8_t>(((v16 >> 12) & 0xFu) * 17u);
            } else {
                r = static_cast<uint8_t>(((v16 >> 10) & 31u) * 255u / 31u);
                g = static_cast<uint8_t>(((v16 >> 5)  & 31u) * 255u / 31u);
                b = static_cast<uint8_t>((v16         & 31u) * 255u / 31u);
                if (aMask != 0) a = (v16 & 0x8000u) ? 255 : 0;
            }
        } else if (bytesPerPx == 1) {
            r = g = b = ptr[0];
        }
        WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        ptr += bytesPerPx;
    }
    outInfo.valid = true;
    return true;
}
