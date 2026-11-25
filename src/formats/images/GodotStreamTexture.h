#pragma once
// GodotStreamTexture.h — Godot Engine 3 ("GDST" .stex) & Godot Engine 4 ("GST2" .ctex) Compiled Stream Texture Decoder
// Supports uncompressed L8/LA8/R8/RG8/RGB8/RGBA8/RGBA4444/RGB565/Float/Half, S3TC/RGTC/BPTC, ETC/ETC2/EAC, ASTC, and embedded PNG/WebP.
#include "Astc.h"

namespace Formats {

namespace DetailGodotTex {

inline bool DecodeGodotRawSurface(
    const uint8_t* src,
    size_t srcLen,
    uint32_t w,
    uint32_t h,
    uint32_t godotFmt,
    bool isGodot4,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    if (!src || w == 0 || h == 0 || w > 8192u || h > 8192u) return false;
    const size_t nPx = static_cast<size_t>(w) * h;
    if (nPx > 64ull * 1024ull * 1024ull) return false;

    outInfo = {};
    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = label;

    // 1. Uncompressed integer formats (0..7 — identical in Godot 3 & 4 except 7 = RGBA5551 in v3, RGB565 in v4)
    if (godotFmt <= 7u) {
        static constexpr size_t kBpp[8] = { 1, 2, 1, 2, 3, 4, 2, 2 };
        const size_t bpp = kBpp[godotFmt];
        if (srcLen < nPx * bpp) return false;
        outInfo.channels = (godotFmt == 0u || godotFmt == 2u) ? 1u :
                           (godotFmt == 1u || godotFmt == 3u) ? 2u :
                           (godotFmt == 4u || (godotFmt == 7u && isGodot4)) ? 3u : 4u;
        outInfo.bgraPixels.resize(nPx * 4u);
        for (size_t i = 0; i < nPx; ++i) {
            const uint8_t* p = src + i * bpp;
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            switch (godotFmt) {
                case 0u: r = g = b = p[0]; break;                         // FORMAT_L8
                case 1u: r = g = b = p[0]; a = p[1]; break;               // FORMAT_LA8
                case 2u: r = p[0]; g = 0; b = 0; break;                   // FORMAT_R8
                case 3u: r = p[0]; g = p[1]; b = 0; break;                // FORMAT_RG8
                case 4u: r = p[0]; g = p[1]; b = p[2]; break;             // FORMAT_RGB8
                case 5u: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;   // FORMAT_RGBA8
                case 6u: {                                                // FORMAT_RGBA4444
                    const uint16_t v = ByteIO::RdU16LE(p);
                    r = static_cast<uint8_t>(((v >> 12u) & 0x0Fu) * 17u);
                    g = static_cast<uint8_t>(((v >>  8u) & 0x0Fu) * 17u);
                    b = static_cast<uint8_t>(((v >>  4u) & 0x0Fu) * 17u);
                    a = static_cast<uint8_t>(( v         & 0x0Fu) * 17u);
                    break;
                }
                case 7u: {
                    const uint16_t v = ByteIO::RdU16LE(p);
                    if (isGodot4) {                                       // Godot 4: FORMAT_RGB565
                        GpuTex::UnpackRgb565(v, r, g, b);
                    } else {                                              // Godot 3: FORMAT_RGBA5551
                        r = static_cast<uint8_t>(((v >> 11u) & 0x1Fu) * 255u / 31u);
                        g = static_cast<uint8_t>(((v >>  6u) & 0x1Fu) * 255u / 31u);
                        b = static_cast<uint8_t>(((v >>  1u) & 0x1Fu) * 255u / 31u);
                        a = (v & 1u) ? 255u : 0u;
                    }
                    break;
                }
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
        outInfo.valid = true;
        return true;
    }

    // 2. Uncompressed Float32 (8..11) & Float16 (12..15)
    if (godotFmt >= 8u && godotFmt <= 15u) {
        const bool isHalf = (godotFmt >= 12u);
        const uint32_t ch = isHalf ? (godotFmt - 11u) : (godotFmt - 7u);
        const size_t elemBytes = isHalf ? 2u : 4u;
        if (srcLen < nPx * ch * elemBytes) return false;
        std::vector<float> rgba(nPx * 4u, 1.0f);
        for (size_t i = 0; i < nPx; ++i) {
            const uint8_t* p = src + i * ch * elemBytes;
            auto rdC = [&](uint32_t c) -> float {
                return isHalf ? GpuTex::HalfToFloat(ByteIO::RdU16LE(p + c * 2u)) : GpuTex::ReadF32LE(p + c * 4u);
            };
            rgba[i * 4u + 0u] = rdC(0u);
            rgba[i * 4u + 1u] = (ch >= 2u) ? rdC(1u) : 0.0f;
            rgba[i * 4u + 2u] = (ch >= 3u) ? rdC(2u) : 0.0f;
            rgba[i * 4u + 3u] = (ch == 4u) ? rdC(3u) : 1.0f;
        }
        outInfo.channels = ch;
        outInfo.bitsPerChannel = isHalf ? 16u : 32u;
        GpuTex::ConvertFloatRgbaToBgra(rgba.data(), w, h, false, outInfo.bgraPixels);
        outInfo.valid = true;
        return true;
    }

    // 3. Shared Desktop GPU Block Formats (17..24: DXT1, DXT3, DXT5, RGTC_R, RGTC_RG, BPTC_RGBA, BPTC_RGBF, BPTC_RGBFU)
    if (godotFmt >= 17u && godotFmt <= 24u) {
        const uint32_t bw = (w + 3u) / 4u;
        const uint32_t bh = (h + 3u) / 4u;
        const size_t blkBytes = (godotFmt == 17u || godotFmt == 20u) ? 8u : 16u;
        if (srcLen < static_cast<size_t>(bw) * bh * blkBytes) return false;

        if (godotFmt == 23u || godotFmt == 24u) { // BC6H signed / unsigned
            const bool isSigned = (godotFmt == 23u);
            std::vector<float> hdr(nPx * 4u, 1.0f);
            float blkF[64];
            const uint8_t* bp = src;
            for (uint32_t by = 0; by < bh; ++by) {
                for (uint32_t bx = 0; bx < bw; ++bx) {
                    GpuTex::DecodeBC6HBlock(bp, blkF, isSigned);
                    bp += 16u;
                    for (uint32_t py = 0; py < 4u && by * 4u + py < h; ++py) {
                        for (uint32_t px = 0; px < 4u && bx * 4u + px < w; ++px) {
                            std::memcpy(&hdr[(static_cast<size_t>(by * 4u + py) * w + (bx * 4u + px)) * 4u],
                                        &blkF[(py * 4u + px) * 4u], 4u * sizeof(float));
                        }
                    }
                }
            }
            outInfo.bitsPerChannel = 16u;
            GpuTex::ConvertFloatRgbaToBgra(hdr.data(), w, h, false, outInfo.bgraPixels);
            outInfo.valid = true;
            return true;
        }

        outInfo.bgraPixels.resize(nPx * 4u);
        uint8_t blk[64];
        const uint8_t* bp = src;
        for (uint32_t by = 0; by < bh; ++by) {
            for (uint32_t bx = 0; bx < bw; ++bx) {
                switch (godotFmt) {
                    case 17u: GpuTex::DecodeBC1Block(bp, blk, true, false); break;
                    case 18u: GpuTex::DecodeBC2Block(bp, blk); break;
                    case 19u: GpuTex::DecodeBC3Block(bp, blk); break;
                    case 20u: GpuTex::DecodeBC4Block(bp, blk, false); break;
                    case 21u: GpuTex::DecodeBC5Block(bp, blk, false); break;
                    case 22u: GpuTex::DecodeBC7Block(bp, blk); break;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                bp += blkBytes;
            }
        }
        outInfo.valid = true;
        return true;
    }

    // 4. Mobile GPU Formats (PVRTC in Godot 3; ETC / ETC2 / ASTC in Godot 3 & 4)
    if (!isGodot4 && godotFmt >= 25u && godotFmt <= 28u) {
        const bool is2bpp = (godotFmt == 25u || godotFmt == 26u);
        const bool opaqueOnly = (godotFmt == 25u || godotFmt == 27u);
        if (!GpuTex::DecodePvrtcSurface(src, srcLen, w, h, is2bpp, opaqueOnly, outInfo.bgraPixels)) return false;
        outInfo.valid = true;
        return true;
    }

    // Normalize ETC/ETC2 enum index (Godot 3 starts ETC at 29; Godot 4 removed PVRTC so ETC starts at 25)
    const int etcIdx = isGodot4 ? (static_cast<int>(godotFmt) - 25) : (static_cast<int>(godotFmt) - 29);
    if (etcIdx >= 0 && etcIdx <= 9) {
        if (isGodot4 && etcIdx == 9) { // FORMAT_DXT5_RA_AS_RG (34 in Godot 4)
            const uint32_t bw = (w + 3u) / 4u, bh = (h + 3u) / 4u;
            if (srcLen < static_cast<size_t>(bw) * bh * 16u) return false;
            outInfo.bgraPixels.resize(nPx * 4u);
            uint8_t blk[64];
            const uint8_t* bp = src;
            for (uint32_t by = 0; by < bh; ++by) {
                for (uint32_t bx = 0; bx < bw; ++bx) {
                    GpuTex::DecodeBC3Block(bp, blk);
                    for (int k = 0; k < 16; ++k) { blk[k * 4 + 1] = blk[k * 4 + 3]; blk[k * 4 + 2] = 0; blk[k * 4 + 3] = 255; }
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                    bp += 16u;
                }
            }
            outInfo.valid = true;
            return true;
        }
        if (etcIdx <= 8) {
            const uint32_t bw = (w + 3u) / 4u;
            const uint32_t bh = (h + 3u) / 4u;
            const size_t blkBytes = (etcIdx == 3 || etcIdx == 4 || etcIdx == 6 || etcIdx == 8) ? 16u : 8u;
            if (srcLen < static_cast<size_t>(bw) * bh * blkBytes) return false;
            outInfo.bgraPixels.resize(nPx * 4u);
            uint8_t blk[64];
            const uint8_t* bp = src;
            for (uint32_t by = 0; by < bh; ++by) {
                for (uint32_t bx = 0; bx < bw; ++bx) {
                    switch (etcIdx) {
                        case 0: GpuTex::DecodeETC1Block(bp, blk); break;                 // ETC
                        case 1: GpuTex::DecodeEacR11Block(bp, false, blk); break;        // ETC2_R11
                        case 2: GpuTex::DecodeEacR11Block(bp, true, blk); break;         // ETC2_R11S
                        case 3: GpuTex::DecodeEacRG11Block(bp, false, blk); break;       // ETC2_RG11
                        case 4: GpuTex::DecodeEacRG11Block(bp, true, blk); break;        // ETC2_RG11S
                        case 5: GpuTex::DecodeETC2Block(bp, blk, false); break;          // ETC2_RGB8
                        case 6: GpuTex::DecodeETC2Block(bp, blk, true); break;           // ETC2_RGBA8
                        case 7: GpuTex::DecodeETC2RgbA1Block(bp, blk); break;            // ETC2_RGB8A1
                        case 8: {                                                        // ETC2_RA_AS_RG
                            GpuTex::DecodeETC2Block(bp, blk, true);
                            for (int k = 0; k < 16; ++k) { blk[k * 4 + 1] = blk[k * 4 + 3]; blk[k * 4 + 2] = 0; blk[k * 4 + 3] = 255; }
                            break;
                        }
                    }
                    GpuTex::BlitBlock4x4RgbaToPremulBgra(blk, bx, by, w, h, outInfo.bgraPixels);
                    bp += blkBytes;
                }
            }
            outInfo.valid = true;
            return true;
        }
    }

    // 5. Godot 4 ASTC 4x4 (35/36) & ASTC 8x8 (37/38)
    if (isGodot4 && godotFmt >= 35u && godotFmt <= 38u) {
        const uint32_t blkDim = (godotFmt <= 36u) ? 4u : 8u;
        if (!DecodeAstcSurface(src, srcLen, w, h, blkDim, blkDim, outInfo.bgraPixels)) return false;
        outInfo.valid = true;
        return true;
    }

    return false;
}

inline bool DecodeGodotLosslessChunk(
    const uint8_t* pay,
    size_t payLen,
    uint32_t origW,
    uint32_t origH,
    const std::wstring& label,
    ExtendedImageInfo& outInfo)
{
    if (!pay || payLen < 12u) return false;
    // Godot sometimes prepends a 4-byte ASCII tag ("PNG " / "WEBP") inside lossless mip chunks
    if (payLen > 16u && (std::memcmp(pay, "PNG ", 4) == 0 || std::memcmp(pay, "WEBP", 4) == 0)) {
        pay += 4u;
        payLen -= 4u;
    }

    if (payLen >= 16u && std::memcmp(pay, "\x89PNG\r\n\x1A\n", 8) == 0) {
        if (!GpuTex::DecodePngToImageInfo(pay, payLen, label, outInfo)) return false;
        if (origW > 0 && origH > 0) { outInfo.origWidth = origW; outInfo.origHeight = origH; }
        return true;
    }
    if (payLen >= 16u && std::memcmp(pay, "RIFF", 4) == 0 && std::memcmp(pay + 8u, "WEBP", 4) == 0) {
        if (!ImageFormatLoader::DecodeImageMemory(pay, payLen, L".webp", 0, outInfo)) return false;
        outInfo.formatName = label;
        if (origW > 0 && origH > 0) { outInfo.origWidth = origW; outInfo.origHeight = origH; }
        return true;
    }
    return false;
}

} // namespace DetailGodotTex

inline bool DecodeGodotStreamTexture(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    using namespace DetailGodotTex;
    if (!data || size < 20u) return false;

    // 1. Godot 3 StreamTexture ("GDST", .stex):
    //    [magic:"GDST"][tw:u16][twCustom:u16][th:u16][thCustom:u16][flags:u32][dataFormat:u32]
    if (std::memcmp(data, "GDST", 4) == 0) {
        const uint16_t tw  = ByteIO::RdU16LE(data + 4);
        const uint16_t twc = ByteIO::RdU16LE(data + 6);
        const uint16_t th  = ByteIO::RdU16LE(data + 8);
        const uint16_t thc = ByteIO::RdU16LE(data + 10);
        const uint32_t df  = ByteIO::RdU32LE(data + 16);

        const uint32_t dispW = (twc > 0u) ? twc : tw;
        const uint32_t dispH = (thc > 0u) ? thc : th;
        const uint32_t rawW  = (tw > 0u)  ? tw  : twc;
        const uint32_t rawH  = (th > 0u)  ? th  : thc;
        if (rawW == 0 || rawH == 0 || rawW > 8192u || rawH > 8192u) return false;

        const std::wstring label = L"Godot 3 StreamTexture (.stex)";
        // Bit 20 = FORMAT_BIT_PNG, Bit 21 = FORMAT_BIT_WEBP
        if ((df & (0x00100000u | 0x00200000u)) != 0u) {
            if (size < 28u) return false;
            const uint32_t numMips = ByteIO::RdU32LE(data + 20);
            (void)numMips;
            const uint32_t chunkLen = ByteIO::RdU32LE(data + 24);
            if (chunkLen == 0 || chunkLen > size - 28u) return false;
            return DecodeGodotLosslessChunk(data + 28u, chunkLen, dispW, dispH, label, outInfo);
        }

        const uint32_t imgFmt = df & 0x000FFFFFu;
        if (!DecodeGodotRawSurface(data + 20u, size - 20u, rawW, rawH, imgFmt, false, label, outInfo)) {
            return false;
        }
        outInfo.origWidth = dispW;
        outInfo.origHeight = dispH;
        return true;
    }

    // 2. Godot 4 CompressedTexture2D ("GST2", .ctex):
    //    [0..3] "GST2", [4..7] ver(1), [8..11] w, [12..15] h, [16..19] flags, [20..23] mipLimit,
    //    [24..35] reserved[3], [36..39] dataFormat (0=IMAGE, 1=PNG, 2=WEBP, 3=BASIS),
    //    [40..41] imgW, [42..43] imgH, [44..47] mipCount, [48..51] imgFormat, [52..] payload
    if (size >= 52u && std::memcmp(data, "GST2", 4) == 0) {
        const uint32_t ver    = ByteIO::RdU32LE(data + 4);
        const uint32_t dispW  = ByteIO::RdU32LE(data + 8);
        const uint32_t dispH  = ByteIO::RdU32LE(data + 12);
        const uint32_t dfMode = ByteIO::RdU32LE(data + 36);
        const uint16_t imgW   = ByteIO::RdU16LE(data + 40);
        const uint16_t imgH   = ByteIO::RdU16LE(data + 42);
        const uint32_t imgFmt = ByteIO::RdU32LE(data + 48);

        if (ver != 1u) return false;
        const uint32_t effW = (imgW > 0u) ? imgW : dispW;
        const uint32_t effH = (imgH > 0u) ? imgH : dispH;
        if (effW == 0 || effH == 0 || effW > 8192u || effH > 8192u) return false;

        const std::wstring label = L"Godot 4 CompressedTexture2D (.ctex)";
        if (dfMode == 1u || dfMode == 2u) { // PNG / WebP
            if (size < 56u) return false;
            const uint32_t chunkLen = ByteIO::RdU32LE(data + 52);
            if (chunkLen == 0 || chunkLen > size - 56u) return false;
            return DecodeGodotLosslessChunk(data + 56u, chunkLen, dispW ? dispW : effW, dispH ? dispH : effH, label, outInfo);
        }
        if (dfMode == 3u) { // Basis Universal
            if (size < 56u) return false;
            const uint32_t chunkLen = ByteIO::RdU32LE(data + 52);
            const uint8_t* basisPtr = (chunkLen > 0 && chunkLen <= size - 56u) ? (data + 56u) : (data + 52u);
            const size_t basisLen   = (chunkLen > 0 && chunkLen <= size - 56u) ? chunkLen : (size - 52u);
            if (!ImageFormatLoader::DecodeImageMemory(basisPtr, basisLen, L".basis", 0, outInfo)) return false;
            outInfo.formatName = label;
            return true;
        }
        if (dfMode == 0u) { // DATA_FORMAT_IMAGE
            if (!DecodeGodotRawSurface(data + 52u, size - 52u, effW, effH, imgFmt, true, label, outInfo)) {
                return false;
            }
            if (dispW > 0 && dispH > 0) { outInfo.origWidth = dispW; outInfo.origHeight = dispH; }
            return true;
        }
    }

    return false;
}

} // namespace Formats
