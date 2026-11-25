#pragma once
// Ktx.h — Khronos Texture 1.0 (.ktx) Decoder (S3TC, RGTC, BPTC BC6H/BC7, ETC1/ETC2/EAC, ASTC, Half/Float & Uncompressed)
#include "Astc.h"

namespace Formats {

inline bool DecodeKTX(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    static const uint8_t kMagic[12] = { 0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    if (!data || size < 68 || std::memcmp(data, kMagic, 12) != 0) return false;

    const bool le = (data[12] == 0x01 && data[13] == 0x02 && data[14] == 0x03 && data[15] == 0x04);
    const bool be = (data[12] == 0x04 && data[13] == 0x03 && data[14] == 0x02 && data[15] == 0x01);
    if (!le && !be) return false;

    auto rd16 = [&](const uint8_t* p) -> uint16_t {
        return be ? ReadU16BE(p) : ReadU16LE(p);
    };
    auto rd32 = [&](size_t off) -> uint32_t {
        return be ? ReadU32BE(data + off) : ReadU32LE(data + off);
    };
    auto rdF32 = [&](const uint8_t* p) -> float {
        uint32_t u = be ? ReadU32BE(p) : ReadU32LE(p);
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return f;
    };

    const uint32_t glType           = rd32(16);
    const uint32_t glFormat         = rd32(24);
    const uint32_t glInternalFormat = rd32(28);
    const uint32_t w                = rd32(36);
    const uint32_t h                = std::max(1u, rd32(40));
    const uint32_t bytesOfKeyValue  = rd32(60);
    if (w == 0 || w > 8192 || h > 8192) return false;
    if (64u + static_cast<size_t>(bytesOfKeyValue) + 4u > size) return false;

    const size_t imgSizeOff = 64u + static_cast<size_t>(bytesOfKeyValue);
    const uint32_t imageSize = rd32(imgSizeOff);
    const size_t payloadOff = imgSizeOff + 4u;
    if (imageSize == 0 || payloadOff > size || imageSize > size - payloadOff) return false;

    const uint8_t* src = data + payloadOff;
    const size_t pixelCount = static_cast<size_t>(w) * h;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"Khronos Texture (.ktx)";

    // 1. ASTC 2D LDR / sRGB formats (0x93B0..0x93BD & 0x93D0..0x93DD)
    if ((glInternalFormat >= 0x93B0u && glInternalFormat <= 0x93BDu) ||
        (glInternalFormat >= 0x93D0u && glInternalFormat <= 0x93DDu))
    {
        static constexpr uint8_t kAstcDims[14][2] = {
            { 4, 4 },  { 5, 4 },  { 5, 5 },  { 6, 5 },  { 6, 6 },  { 8, 5 },  { 8, 6 },
            { 8, 8 },  { 10, 5 }, { 10, 6 }, { 10, 8 }, { 10, 10 },{ 12, 10 },{ 12, 12 }
        };
        const uint32_t idx = (glInternalFormat >= 0x93D0u) ? (glInternalFormat - 0x93D0u) : (glInternalFormat - 0x93B0u);
        const uint32_t bx = kAstcDims[idx][0];
        const uint32_t by = kAstcDims[idx][1];
        if (!DecodeAstcSurface(src, imageSize, w, h, bx, by, outInfo.bgraPixels)) return false;
        outInfo.valid = true;
        return true;
    }

    // 2. BC6H HDR block formats (0x8E8E = SF16 signed, 0x8E8F = UF16 unsigned)
    if (glInternalFormat == 0x8E8Eu || glInternalFormat == 0x8E8Fu) {
        const bool isSigned = (glInternalFormat == 0x8E8Eu);
        const uint32_t bw = (w + 3u) / 4u;
        const uint32_t bh = (h + 3u) / 4u;
        if (imageSize < static_cast<size_t>(bw) * bh * 16u) return false;
        std::vector<float> hdrRgba(pixelCount * 4u, 1.0f);
        float blkF[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                GpuTex::DecodeBC6HBlock(bp, blkF, isSigned);
                bp += 16u;
                for (uint32_t py = 0; py < 4u && by * 4u + py < h; ++py) {
                    for (uint32_t px = 0; px < 4u && bx * 4u + px < w; ++px) {
                        std::memcpy(
                            &hdrRgba[(static_cast<size_t>(by * 4u + py) * w + (bx * 4u + px)) * 4u],
                            &blkF[(py * 4u + px) * 4u],
                            4u * sizeof(float));
                    }
                }
            }
        }
        outInfo.channels = 3u;
        outInfo.bitsPerChannel = 16u;
        GpuTex::ConvertFloatRgbaToBgra(hdrRgba.data(), w, h, false, outInfo.bgraPixels);
        outInfo.valid = true;
        return true;
    }

    // 3. Compressed 4x4 LDR GPU block formats (S3TC, RGTC, BC7, ETC1, ETC2, EAC R11/RG11)
    const bool isCompressed = (glType == 0 || glFormat == 0 ||
        (glInternalFormat >= 0x83F0u && glInternalFormat <= 0x83F3u) ||
        (glInternalFormat >= 0x8C4Cu && glInternalFormat <= 0x8C4Fu) ||
        (glInternalFormat >= 0x8DBBu && glInternalFormat <= 0x8DBEu) ||
        glInternalFormat == 0x8E8Cu || glInternalFormat == 0x8E8Du ||
        glInternalFormat == 0x8D64u ||
        (glInternalFormat >= 0x9270u && glInternalFormat <= 0x9279u));

    if (isCompressed) {
        const uint32_t bw = (w + 3u) / 4u;
        const uint32_t bh = (h + 3u) / 4u;
        size_t blockStride = 0;
        switch (glInternalFormat) {
        case 0x83F0: case 0x8C4C: // DXT1 RGB
        case 0x83F1: case 0x8C4D: // DXT1 RGBA
        case 0x8DBB: case 0x8DBC: // RGTC1 / BC4
        case 0x8D64:              // ETC1_RGB8_OES
        case 0x9270: case 0x9271: // EAC R11 / SIGNED_R11
        case 0x9274: case 0x9275: // ETC2 RGB8 / SRGB8
        case 0x9276: case 0x9277: // ETC2 RGB8_PUNCHTHROUGH_ALPHA1
            blockStride = 8;
            break;
        case 0x83F2: case 0x8C4E: // DXT3
        case 0x83F3: case 0x8C4F: // DXT5
        case 0x8DBD: case 0x8DBE: // RGTC2 / BC5
        case 0x8E8C: case 0x8E8D: // BPTC / BC7
        case 0x9272: case 0x9273: // EAC RG11 / SIGNED_RG11
        case 0x9278: case 0x9279: // ETC2 RGBA8 EAC
            blockStride = 16;
            break;
        default:
            return false;
        }
        const size_t needed = static_cast<size_t>(bw) * bh * blockStride;
        if (imageSize < needed) return false;

        if (glInternalFormat == 0x9270u || glInternalFormat == 0x9271u) {
            outInfo.channels = 1u;
            outInfo.bitsPerChannel = 11u;
        } else if (glInternalFormat == 0x9272u || glInternalFormat == 0x9273u) {
            outInfo.channels = 2u;
            outInfo.bitsPerChannel = 11u;
        }

        outInfo.bgraPixels.resize(pixelCount * 4u);
        uint8_t blk[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                switch (glInternalFormat) {
                case 0x83F0: case 0x8C4C: GpuTex::DecodeBC1Block(bp, blk, true, true); break;
                case 0x83F1: case 0x8C4D: GpuTex::DecodeBC1Block(bp, blk, true, false); break;
                case 0x83F2: case 0x8C4E: GpuTex::DecodeBC2Block(bp, blk); break;
                case 0x83F3: case 0x8C4F: GpuTex::DecodeBC3Block(bp, blk); break;
                case 0x8DBB:              GpuTex::DecodeBC4Block(bp, blk, false); break;
                case 0x8DBC:              GpuTex::DecodeBC4Block(bp, blk, true); break;
                case 0x8DBD:              GpuTex::DecodeBC5Block(bp, blk, false); break;
                case 0x8DBE:              GpuTex::DecodeBC5Block(bp, blk, true); break;
                case 0x8E8C: case 0x8E8D: GpuTex::DecodeBC7Block(bp, blk); break;
                case 0x8D64:              GpuTex::DecodeETC1Block(bp, blk); break;
                case 0x9270:              GpuTex::DecodeEacR11Block(bp, false, blk); break;
                case 0x9271:              GpuTex::DecodeEacR11Block(bp, true, blk); break;
                case 0x9272:              GpuTex::DecodeEacRG11Block(bp, false, blk); break;
                case 0x9273:              GpuTex::DecodeEacRG11Block(bp, true, blk); break;
                case 0x9274: case 0x9275: GpuTex::DecodeETC2Block(bp, blk, false); break;
                case 0x9276: case 0x9277: GpuTex::DecodeETC2RgbA1Block(bp, blk); break;
                case 0x9278: case 0x9279: GpuTex::DecodeETC2Block(bp, blk, true); break;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                bp += blockStride;
            }
        }
        outInfo.valid = true;
        return true;
    }

    // 4. Half-float (GL_RGBA16F / GL_RGB16F) & Float32 (GL_RGBA32F / GL_RGB32F)
    if (glInternalFormat == 0x881A || glInternalFormat == 0x881B || glType == 0x140B ||
        glInternalFormat == 0x8814 || glInternalFormat == 0x8815 || glType == 0x1406)
    {
        const bool isHalf = (glInternalFormat == 0x881A || glInternalFormat == 0x881B || glType == 0x140B);
        uint32_t ch = 4;
        if (glInternalFormat == 0x881B || glInternalFormat == 0x8815 || glFormat == 0x1907) ch = 3;
        else if (glFormat == 0x8227) ch = 2;
        else if (glFormat == 0x1903 || glFormat == 0x1909) ch = 1;

        const size_t elemBytes = isHalf ? 2u : 4u;
        const size_t rowStride = ((static_cast<size_t>(w) * ch * elemBytes + 3u) & ~size_t(3u));
        if (rowStride * h > imageSize) return false;

        outInfo.channels = ch;
        outInfo.bitsPerChannel = isHalf ? 16 : 32;
        std::vector<float> rgba(pixelCount * 4, 1.0f);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* row = src + static_cast<size_t>(y) * rowStride;
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t* px = row + static_cast<size_t>(x) * ch * elemBytes;
                auto sample = [&](uint32_t c) -> float {
                    return isHalf ? GpuTex::HalfToFloat(rd16(px + c * 2)) : rdF32(px + c * 4);
                };
                float r = sample(0);
                float g = (ch >= 2) ? sample(1) : r;
                float b = (ch >= 3) ? sample(2) : ((ch == 2) ? 0.0f : r);
                float a = (ch == 4) ? sample(3) : 1.0f;
                size_t di = (static_cast<size_t>(y) * w + x) * 4;
                rgba[di + 0] = r; rgba[di + 1] = g; rgba[di + 2] = b; rgba[di + 3] = a;
            }
        }
        GpuTex::ConvertFloatRgbaToBgra(rgba.data(), w, h, false, outInfo.bgraPixels);
        outInfo.valid = true;
        return true;
    }

    // 5. Uncompressed 8-bit formats
    uint32_t bpp = 4;
    bool bgraOrder = false;
    if (glFormat == 0x80E1) { bpp = 4; bgraOrder = true; } // GL_BGRA
    else if (glFormat == 0x80E0) { bpp = 3; bgraOrder = true; } // GL_BGR
    else if (glFormat == 0x1908 || glInternalFormat == 0x8058 || glInternalFormat == 0x8C43) { bpp = 4; }
    else if (glFormat == 0x1907 || glInternalFormat == 0x8051 || glInternalFormat == 0x8C41) { bpp = 3; }
    else if (glFormat == 0x8227 || glFormat == 0x190A || glInternalFormat == 0x822B) { bpp = 2; }
    else if (glFormat == 0x1903 || glFormat == 0x1909 || glInternalFormat == 0x8229) { bpp = 1; }
    else return false;

    const size_t rowStride = ((static_cast<size_t>(w) * bpp + 3u) & ~size_t(3u));
    if (rowStride * h > imageSize) return false;

    outInfo.channels = bpp;
    outInfo.bgraPixels.resize(pixelCount * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * rowStride;
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* px = row + static_cast<size_t>(x) * bpp;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (bpp == 4) {
                r = bgraOrder ? px[2] : px[0];
                g = px[1];
                b = bgraOrder ? px[0] : px[2];
                a = px[3];
            } else if (bpp == 3) {
                r = bgraOrder ? px[2] : px[0];
                g = px[1];
                b = bgraOrder ? px[0] : px[2];
            } else if (bpp == 2) {
                if (glFormat == 0x190A) { r = g = b = px[0]; a = px[1]; }
                else { r = px[0]; g = px[1]; b = 0; }
            } else {
                r = g = b = px[0];
            }
            WritePremulBGRA(&outInfo.bgraPixels[(static_cast<size_t>(y) * w + x) * 4], r, g, b, a);
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
