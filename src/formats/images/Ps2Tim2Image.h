#pragma once
// Ps2Tim2Image.h — Sony PlayStation 2 "TIM2" Texture (.tm2 / .tim2)
// Supports TIM2 v3/v4 with 16-byte (FormatId=0) and 128-byte (FormatId=1) alignment,
// direct RGBA32 / RGB24 / ABGR1555, and paletted IDTEX8 / IDTEX4 with PS2 GS CSM1
// 32-entry CLUT bank unswizzling and automatic PS2 GS 0x80->0xFF alpha scaling.

namespace Formats {

namespace DetailPs2Tim2 {

inline size_t Csm1ClutOffset(size_t logicalIdx) {
    // PS2 GS CSM1 swaps banks [8..15] and [16..23] inside every 32-color block
    const size_t sub = logicalIdx & 0x18u;
    if (sub == 0x08u || sub == 0x10u) {
        return logicalIdx ^ 0x18u;
    }
    return logicalIdx;
}

} // namespace DetailPs2Tim2

inline bool DecodePs2Tim2(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64) return false;
    if (std::memcmp(data, "TIM2", 4) != 0) return false;

    const uint8_t fmtVer      = data[4];
    const uint8_t fmtAlignId  = data[5];
    const uint16_t numPics    = ByteIO::RdU16LE(data + 6);
    if ((fmtVer != 3u && fmtVer != 4u) || fmtAlignId > 1u || numPics == 0u) {
        return false;
    }

    const size_t picOff = (fmtAlignId == 1u) ? 128u : 16u;
    if (size < picOff + 48u) return false;

    const uint8_t* ph          = data + picOff;
    const uint32_t totalSize   = ByteIO::RdU32LE(ph + 0x00);
    const uint32_t clutSize    = ByteIO::RdU32LE(ph + 0x04);
    const uint32_t imageSize   = ByteIO::RdU32LE(ph + 0x08);
    const uint16_t headerSize  = ByteIO::RdU16LE(ph + 0x0C);
    const uint16_t clutColors  = ByteIO::RdU16LE(ph + 0x0E);
    const uint8_t  mipCount    = ph[0x11];
    const uint8_t  clutType    = ph[0x12];
    const uint8_t  imageType   = ph[0x13];
    const uint16_t width       = ByteIO::RdU16LE(ph + 0x14);
    const uint16_t height      = ByteIO::RdU16LE(ph + 0x16);

    if (headerSize < 48u || width == 0u || height == 0u ||
        width > kMaxRasterDimension || height > kMaxRasterDimension || mipCount == 0u) {
        return false;
    }
    if (static_cast<uint64_t>(width) * height > 64ULL * 1024ULL * 1024ULL) {
        return false;
    }

    const uint64_t imgStart64  = static_cast<uint64_t>(picOff) + headerSize;
    const uint64_t clutStart64 = imgStart64 + imageSize;
    const uint64_t endOff64    = clutStart64 + clutSize;
    if (imgStart64 > size || clutStart64 > size || endOff64 > size) {
        return false;
    }
    if (totalSize != 0u && static_cast<uint64_t>(headerSize) + imageSize + clutSize > totalSize) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    uint64_t mip0Bytes = 0;
    const wchar_t* typeTag = L"RGBA32";
    switch (imageType) {
    case 1u: mip0Bytes = static_cast<uint64_t>(pixelCount) * 2ULL; typeTag = L"ABGR1555"; break;
    case 2u: mip0Bytes = static_cast<uint64_t>(pixelCount) * 3ULL; typeTag = L"RGB24";    break;
    case 3u: mip0Bytes = static_cast<uint64_t>(pixelCount) * 4ULL; typeTag = L"RGBA32";   break;
    case 4u: mip0Bytes = (static_cast<uint64_t>(pixelCount) + 1ULL) / 2ULL; typeTag = L"IDTEX4"; break;
    case 5u: mip0Bytes = static_cast<uint64_t>(pixelCount);        typeTag = L"IDTEX8";   break;
    default: return false;
    }
    if (mip0Bytes == 0 || mip0Bytes > imageSize) return false;

    const uint8_t* imgPtr  = data + static_cast<size_t>(imgStart64);
    const uint8_t* clutPtr = data + static_cast<size_t>(clutStart64);

    // Decode CLUT if indexed format (IDTEX4 / IDTEX8)
    std::array<std::array<uint8_t, 4>, 256> pal{};
    if (imageType == 4u || imageType == 5u) {
        const uint8_t clutFmt = static_cast<uint8_t>(clutType & 0x3Fu);
        const bool isCsm2     = (clutType & 0x40u) != 0u;
        size_t entryBytes = 0;
        if (clutFmt == 1u) entryBytes = 2u;
        else if (clutFmt == 2u) entryBytes = 3u;
        else if (clutFmt == 3u) entryBytes = 4u;
        else return false;

        const size_t maxEntries = (imageType == 4u) ? 16u : 256u;
        const size_t availEntries = std::min<size_t>(
            maxEntries,
            clutColors > 0 ? std::min<size_t>(clutColors, clutSize / entryBytes) : (clutSize / entryBytes));
        if (availEntries == 0) return false;

        uint8_t maxClutA = 0;
        for (size_t i = 0; i < availEntries; ++i) {
            const size_t rawIdx = (imageType == 5u && !isCsm2) ? DetailPs2Tim2::Csm1ClutOffset(i) : i;
            if ((rawIdx + 1u) * entryBytes > clutSize) continue;
            const uint8_t* ce = clutPtr + rawIdx * entryBytes;
            if (clutFmt == 1u) {
                ConsolePx::UnpackAbgr1555(ByteIO::RdU16LE(ce), pal[i][0], pal[i][1], pal[i][2], pal[i][3], /*zeroIsTransparentOnly=*/true);
            } else if (clutFmt == 2u) {
                pal[i] = { ce[0], ce[1], ce[2], 255u };
            } else {
                pal[i] = { ce[0], ce[1], ce[2], ce[3] };
                maxClutA = std::max(maxClutA, ce[3]);
            }
        }
        if (clutFmt == 3u && maxClutA > 0u && maxClutA <= 0x80u) {
            for (size_t i = 0; i < availEntries; ++i) {
                pal[i][3] = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(pal[i][3]) * 255u + 64u) / 128u));
            }
        }
    }

    outInfo = {};
    outInfo.width = width;
    outInfo.height = height;
    outInfo.origWidth = width;
    outInfo.origHeight = height;
    outInfo.channels = (imageType == 2u) ? 3u : 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels.resize(pixelCount * 4u);

    if (imageType == 1u) {
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            ConsolePx::UnpackAbgr1555(ByteIO::RdU16LE(imgPtr + i * 2u), r, g, b, a, /*zeroIsTransparentOnly=*/true);
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], r, g, b, a);
        }
    } else if (imageType == 2u) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t* s = imgPtr + i * 3u;
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], s[0], s[1], s[2], 255u);
        }
    } else if (imageType == 3u) {
        uint8_t maxA = 0;
        for (size_t i = 0; i < pixelCount; ++i) {
            maxA = std::max(maxA, imgPtr[i * 4u + 3u]);
        }
        const bool gsHalfAlpha = (maxA > 0u && maxA <= 0x80u);
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t* s = imgPtr + i * 4u;
            uint8_t a = s[3];
            if (gsHalfAlpha) {
                a = static_cast<uint8_t>(std::min<uint32_t>(255u, (static_cast<uint32_t>(a) * 255u + 64u) / 128u));
            }
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], s[0], s[1], s[2], a);
        }
    } else if (imageType == 4u) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const uint8_t byte = imgPtr[i >> 1];
            const uint8_t nib  = ((i & 1u) == 0u) ? (byte & 0x0Fu) : ((byte >> 4) & 0x0Fu);
            const auto& c = pal[nib];
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], c[0], c[1], c[2], c[3]);
        }
    } else if (imageType == 5u) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const auto& c = pal[imgPtr[i]];
            WritePremulBGRA(&outInfo.bgraPixels[i * 4u], c[0], c[1], c[2], c[3]);
        }
    }

    outInfo.formatName = std::wstring(L"Sony PS2 TIM2 (") + typeTag + L")";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
