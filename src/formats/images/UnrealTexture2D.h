#pragma once
// UnrealTexture2D.h — Unreal Engine 4/5 Texture2D asset decoder (.uasset).
#include "../common/UnrealPackageCommon.h"

namespace Formats {
inline bool DecodeUnrealTexture2D(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    ExtendedImageInfo& outInfo)
{
    using namespace UnrealPkgDetail;
    ParsedUnrealSummary sum;
    if (!ParseUnrealPackageSummary(data, size, sum)) return false;

    enum class UePixFmt { Unknown, DXT1, DXT3, DXT5, BC4, BC5, BGRA8, RGBA8, G8, FloatRGBA };
    UePixFmt fmt = UePixFmt::Unknown;
    std::wstring fmtLabel;
    for (const auto& nm : sum.names) {
        if (EqualsAsciiCI(nm, "PF_DXT1"))           { fmt = UePixFmt::DXT1;      fmtLabel = L"PF_DXT1"; break; }
        if (EqualsAsciiCI(nm, "PF_DXT3"))           { fmt = UePixFmt::DXT3;      fmtLabel = L"PF_DXT3"; break; }
        if (EqualsAsciiCI(nm, "PF_DXT5"))           { fmt = UePixFmt::DXT5;      fmtLabel = L"PF_DXT5"; break; }
        if (EqualsAsciiCI(nm, "PF_BC4"))            { fmt = UePixFmt::BC4;       fmtLabel = L"PF_BC4"; break; }
        if (EqualsAsciiCI(nm, "PF_BC5"))            { fmt = UePixFmt::BC5;       fmtLabel = L"PF_BC5"; break; }
        if (EqualsAsciiCI(nm, "PF_B8G8R8A8"))       { fmt = UePixFmt::BGRA8;     fmtLabel = L"PF_B8G8R8A8"; break; }
        if (EqualsAsciiCI(nm, "PF_R8G8B8A8"))       { fmt = UePixFmt::RGBA8;     fmtLabel = L"PF_R8G8B8A8"; break; }
        if (EqualsAsciiCI(nm, "PF_G8"))             { fmt = UePixFmt::G8;        fmtLabel = L"PF_G8"; break; }
        if (EqualsAsciiCI(nm, "PF_FloatRGBA"))      { fmt = UePixFmt::FloatRGBA; fmtLabel = L"PF_FloatRGBA"; break; }
    }
    if (fmt == UePixFmt::Unknown) return false;

    std::vector<uint8_t> combined, ubulk;
    LoadCombinedUnrealStreams(data, size, sum, fullPath, combined, ubulk);
    const size_t bodyStart = (sum.totalHeaderSize > 0 && static_cast<size_t>(sum.totalHeaderSize) < combined.size())
        ? static_cast<size_t>(sum.totalHeaderSize)
        : 64u;
    if (bodyStart + 28 > combined.size()) return false;

    auto computeExpectedMipBytes = [&](uint32_t w, uint32_t h) -> uint64_t {
        uint64_t bx = (w + 3u) / 4u, by = (h + 3u) / 4u;
        switch (fmt) {
        case UePixFmt::DXT1:
        case UePixFmt::BC4:       return bx * by * 8ULL;
        case UePixFmt::DXT3:
        case UePixFmt::DXT5:
        case UePixFmt::BC5:       return bx * by * 16ULL;
        case UePixFmt::BGRA8:
        case UePixFmt::RGBA8:     return static_cast<uint64_t>(w) * h * 4ULL;
        case UePixFmt::G8:        return static_cast<uint64_t>(w) * h;
        case UePixFmt::FloatRGBA: return static_cast<uint64_t>(w) * h * 8ULL;
        default:                  return 0;
        }
    };

    // Scan FTexturePlatformData -> Mip[0] FByteBulkData:
    //   FByteBulkData (20B): uint32 BulkDataFlags, int32 ElementCount, int32 SizeOnDisk, int64 OffsetInFile
    //   followed (for inline) or preceded by int32 SizeX, int32 SizeY (and int32 SizeZ=1 in UE4.20+)!
    const uint8_t* buf = combined.data();
    const size_t totalSz = combined.size();
    uint32_t imgW = 0, imgH = 0;
    std::vector<uint8_t> mipBytes;

    for (size_t pos = bodyStart; pos + 28 <= totalSz; pos += 4) {
        uint32_t bFlags  = UeU32(buf + pos + 0);
        int32_t elemCnt  = UeI32(buf + pos + 4);
        int32_t diskSz   = UeI32(buf + pos + 8);
        int64_t bulkOff  = UeI64(buf + pos + 12);
        if (elemCnt <= 0 || diskSz <= 0 || elemCnt > 128 * 1024 * 1024) continue;
        if ((bFlags & 0xFFFF0000u) != 0) continue; // Plausible EBulkDataFlags check

        const bool inSeparateUbulk = (bFlags & 0x0100u) != 0; // BULKDATA_PayloadInSeperateFile
        const bool isZlib          = (bFlags & 0x0002u) != 0; // BULKDATA_SerializeCompressedZLIB
        if (!isZlib && diskSz != elemCnt) continue;

        // Look right after inline payload (where UE4/UE5 FTexture2DMipMap stores int32 SizeX, int32 SizeY, [int32 SizeZ])
        // OR right before FByteBulkData (where FTexturePlatformData stores int32 SizeX, int32 SizeY)!
        size_t afterInline = inSeparateUbulk ? (pos + 20) : (pos + 20 + static_cast<size_t>(diskSz));
        if (afterInline > totalSz) continue;

        auto checkDimsAt = [&](size_t dPos, uint32_t& outW, uint32_t& outH) -> bool {
            if (dPos + 8 > totalSz) return false;
            int32_t sx = UeI32(buf + dPos + 0);
            int32_t sy = UeI32(buf + dPos + 4);
            if (sx <= 0 || sy <= 0 || sx > 16384 || sy > 16384) return false;
            return computeExpectedMipBytes(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy)) == static_cast<uint64_t>(elemCnt);
        };

        uint32_t candW = 0, candH = 0;
        if (!checkDimsAt(afterInline, candW, candH)) {
            bool foundBefore = false;
            size_t lookBack = (pos >= bodyStart + 64) ? (pos - 64) : bodyStart;
            for (size_t bp = lookBack; bp + 8 <= pos; bp += 4) {
                if (checkDimsAt(bp, candW, candH)) { foundBefore = true; break; }
            }
            if (!foundBefore) continue;
        } else {
            candW = static_cast<uint32_t>(UeI32(buf + afterInline + 0));
            candH = static_cast<uint32_t>(UeI32(buf + afterInline + 4));
        }
        if (candW == 0 || candH == 0) {
            // Populate candW/candH if found via lookBack
            size_t lookBack = (pos >= bodyStart + 64) ? (pos - 64) : bodyStart;
            for (size_t bp = lookBack; bp + 8 <= pos; bp += 4) {
                int32_t sx = UeI32(buf + bp + 0), sy = UeI32(buf + bp + 4);
                if (sx > 0 && sy > 0 && sx <= 16384 && sy <= 16384 &&
                    computeExpectedMipBytes(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy)) == static_cast<uint64_t>(elemCnt)) {
                    candW = static_cast<uint32_t>(sx);
                    candH = static_cast<uint32_t>(sy);
                    break;
                }
            }
        }
        if (candW == 0 || candH == 0) continue;

        const uint8_t* rawSrc = nullptr;
        if (inSeparateUbulk) {
            if (ubulk.empty()) continue;
            uint64_t uOff = (bulkOff >= 0) ? static_cast<uint64_t>(bulkOff) : 0ULL;
            if (uOff + static_cast<uint64_t>(diskSz) > ubulk.size()) {
                // In UE4.16+, BulkDataOffsetInFile can be biased by negative BulkDataStartOffset -> try 0 when single mip fits
                if (static_cast<size_t>(diskSz) <= ubulk.size()) uOff = 0;
                else continue;
            }
            rawSrc = ubulk.data() + static_cast<size_t>(uOff);
        } else {
            if (pos + 20 + static_cast<size_t>(diskSz) > totalSz) continue;
            rawSrc = buf + pos + 20;
        }

        if (isZlib) {
            if (!ZipDeflate::InflateZlib(rawSrc, static_cast<size_t>(diskSz), mipBytes, static_cast<size_t>(elemCnt) + 64) ||
                mipBytes.size() != static_cast<size_t>(elemCnt)) {
                mipBytes.clear();
                continue;
            }
        } else {
            mipBytes.assign(rawSrc, rawSrc + static_cast<size_t>(elemCnt));
        }
        imgW = candW;
        imgH = candH;
        break;
    }

    if (imgW == 0 || imgH == 0 || mipBytes.empty()) return false;

    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4, 0);

    auto decodeBc4BlockToChannel = [](const uint8_t* blk, uint8_t out16[16]) {
        uint8_t r0 = blk[0], r1 = blk[1];
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= (static_cast<uint64_t>(blk[2 + i]) << (i * 8));
        uint8_t lut[8] = { r0, r1, 0, 0, 0, 0, 0, 255 };
        if (r0 > r1) {
            for (int k = 1; k <= 6; ++k) lut[k + 1] = static_cast<uint8_t>(((7 - k) * r0 + k * r1 + 3) / 7);
        } else {
            for (int k = 1; k <= 4; ++k) lut[k + 1] = static_cast<uint8_t>(((5 - k) * r0 + k * r1 + 2) / 5);
        }
        for (int i = 0; i < 16; ++i) out16[i] = lut[(bits >> (i * 3)) & 7u];
    };

    const uint32_t blocksX = (imgW + 3u) / 4u;
    const uint32_t blocksY = (imgH + 3u) / 4u;
    uint8_t blockRgba[64] = {};

    if (fmt == UePixFmt::DXT1 || fmt == UePixFmt::DXT3 || fmt == UePixFmt::DXT5) {
        size_t bSz = (fmt == UePixFmt::DXT1) ? 8u : 16u;
        size_t off = 0;
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                if (fmt == UePixFmt::DXT1) GpuTex::DecodeBC1Block(mipBytes.data() + off, blockRgba, true);
                else if (fmt == UePixFmt::DXT3) GpuTex::DecodeBC2Block(mipBytes.data() + off, blockRgba);
                else GpuTex::DecodeBC3Block(mipBytes.data() + off, blockRgba);
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, imgW, imgH, outInfo.bgraPixels);
                off += bSz;
            }
        }
    } else if (fmt == UePixFmt::BC4 || fmt == UePixFmt::BC5) {
        size_t bSz = (fmt == UePixFmt::BC4) ? 8u : 16u;
        size_t off = 0;
        uint8_t chR[16] = {}, chG[16] = {};
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                decodeBc4BlockToChannel(mipBytes.data() + off, chR);
                if (fmt == UePixFmt::BC5) decodeBc4BlockToChannel(mipBytes.data() + off + 8, chG);
                for (int i = 0; i < 16; ++i) {
                    if (fmt == UePixFmt::BC4) {
                        blockRgba[i * 4 + 0] = chR[i];
                        blockRgba[i * 4 + 1] = chR[i];
                        blockRgba[i * 4 + 2] = chR[i];
                    } else {
                        // Reconstruct normal map Z from X,Y in BC5!
                        float nx = (static_cast<float>(chR[i]) / 127.5f) - 1.0f;
                        float ny = (static_cast<float>(chG[i]) / 127.5f) - 1.0f;
                        float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                        blockRgba[i * 4 + 0] = chR[i];
                        blockRgba[i * 4 + 1] = chG[i];
                        blockRgba[i * 4 + 2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    }
                    blockRgba[i * 4 + 3] = 255;
                }
                GpuTex::BlitBlock4x4RgbaToPremulBgra(blockRgba, bx, by, imgW, imgH, outInfo.bgraPixels);
                off += bSz;
            }
        }
    } else if (fmt == UePixFmt::BGRA8 || fmt == UePixFmt::RGBA8) {
        const size_t nPix = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < nPix; ++i) {
            uint8_t r = (fmt == UePixFmt::BGRA8) ? mipBytes[i * 4 + 2] : mipBytes[i * 4 + 0];
            uint8_t g = mipBytes[i * 4 + 1];
            uint8_t b = (fmt == UePixFmt::BGRA8) ? mipBytes[i * 4 + 0] : mipBytes[i * 4 + 2];
            uint8_t a = mipBytes[i * 4 + 3];
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], r, g, b, a);
        }
    } else if (fmt == UePixFmt::G8) {
        const size_t nPix = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < nPix; ++i) {
            uint8_t l = mipBytes[i];
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], l, l, l, 255);
        }
    } else if (fmt == UePixFmt::FloatRGBA) {
        const size_t nPix = static_cast<size_t>(imgW) * imgH;
        for (size_t i = 0; i < nPix; ++i) {
            float rf = HalfToFloat(UeU16(mipBytes.data() + i * 8 + 0));
            float gf = HalfToFloat(UeU16(mipBytes.data() + i * 8 + 2));
            float bf = HalfToFloat(UeU16(mipBytes.data() + i * 8 + 4));
            float af = HalfToFloat(UeU16(mipBytes.data() + i * 8 + 6));
            auto toneMap = [](float c) -> uint8_t {
                if (!std::isfinite(c) || c <= 0.0f) return 0;
                float mapped = c / (1.0f + c);
                float srgb = std::pow(mapped, 1.0f / 2.2f);
                return static_cast<uint8_t>(std::clamp(srgb * 255.0f + 0.5f, 0.0f, 255.0f));
            };
            uint8_t a8 = std::isfinite(af) ? static_cast<uint8_t>(std::clamp(af * 255.0f + 0.5f, 0.0f, 255.0f)) : 255;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4], toneMap(rf), toneMap(gf), toneMap(bf), a8);
        }
    }

    outInfo.bitsPerChannel = (fmt == UePixFmt::FloatRGBA) ? 16 : 8;
    outInfo.channels = 4;
    outInfo.formatName = L"Unreal UTexture2D (" + fmtLabel + L")";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
