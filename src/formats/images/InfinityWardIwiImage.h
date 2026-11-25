#pragma once
// InfinityWardIwiImage.h — Infinity Ward Call of Duty Texture (.iwi — "IWi" v5 / v6 / v8 / v13)
// Supports CoD2 (v5), CoD4/WaW (v6), MW2 (v8), and Black Ops (v13) headers, reverse-ordered mip chains
// (largest Level-0 mip stored at tail of file), and ARGB32 / RGB24 / GA16 / A8 / DXT1 / DXT3 / DXT5.

namespace Formats {

inline bool DecodeInfinityWardIwi(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 32u || size > (128u << 20)) return false;
    if (data[0] != 'I' || data[1] != 'W' || data[2] != 'i') return false;

    const uint8_t version = data[3];
    size_t hdrSize = 0u;
    uint8_t fmtCode = 0u;
    uint16_t width = 0u, height = 0u;
    uint32_t mipOff0 = 0u;

    if (version == 5u || version == 6u) {
        // v5/v6: [0..3]"IWi", [3]ver, [4]fmt, [5]usage, [6..8]w, [8..10]h, [10..12]depth, [12..28]mipOfs[4] (28 bytes)
        hdrSize = 28u;
        fmtCode = data[4];
        width   = ByteIO::RdU16LE(data + 6u);
        height  = ByteIO::RdU16LE(data + 8u);
        mipOff0 = ByteIO::RdU32LE(data + 12u);
    } else if (version == 8u) {
        // v8 (MW2): [0..3]"IWi", [3]ver, [4..8]flags, [8]fmt, [9]pad, [10..12]w, [12..14]h, [14..16]depth, [16..32]mipOfs[4] (32 bytes)
        hdrSize = 32u;
        fmtCode = data[8];
        width   = ByteIO::RdU16LE(data + 10u);
        height  = ByteIO::RdU16LE(data + 12u);
        mipOff0 = ByteIO::RdU32LE(data + 16u);
    } else if (version == 13u) {
        // v13 (BO1): 32B or 64B header; fmt at [8], w at [10], h at [12], mipOfs[0] at [32] (64B header)
        if (size < 64u) return false;
        hdrSize = 64u;
        fmtCode = data[8];
        width   = ByteIO::RdU16LE(data + 10u);
        height  = ByteIO::RdU16LE(data + 12u);
        mipOff0 = ByteIO::RdU32LE(data + 32u);
    } else {
        return false;
    }

    if (width == 0u || height == 0u || width > 8192u || height > 8192u || size <= hdrSize) return false;
    if (mipOff0 > size) return false;

    uint64_t level0Bytes = 0ULL;
    const wchar_t* fmtName = L"";
    int bcKind = 0;

    switch (fmtCode) {
    case 0x01u: level0Bytes = static_cast<uint64_t>(width) * height * 4ULL; fmtName = L"ARGB32"; break;
    case 0x02u: level0Bytes = static_cast<uint64_t>(width) * height * 3ULL; fmtName = L"RGB24";  break;
    case 0x03u: level0Bytes = static_cast<uint64_t>(width) * height * 2ULL; fmtName = L"GA16";   break;
    case 0x04u:
    case 0x05u: level0Bytes = static_cast<uint64_t>(width) * height * 1ULL; fmtName = L"A8/L8";  break;
    case 0x0Bu:
        bcKind = 1; fmtName = L"DXT1";
        level0Bytes = ((static_cast<uint64_t>(width) + 3ULL) / 4ULL) * ((static_cast<uint64_t>(height) + 3ULL) / 4ULL) * 8ULL;
        break;
    case 0x0Cu:
        bcKind = 2; fmtName = L"DXT3";
        level0Bytes = ((static_cast<uint64_t>(width) + 3ULL) / 4ULL) * ((static_cast<uint64_t>(height) + 3ULL) / 4ULL) * 16ULL;
        break;
    case 0x0Du:
        bcKind = 3; fmtName = L"DXT5";
        level0Bytes = ((static_cast<uint64_t>(width) + 3ULL) / 4ULL) * ((static_cast<uint64_t>(height) + 3ULL) / 4ULL) * 16ULL;
        break;
    default:
        return false;
    }

    if (level0Bytes == 0ULL || level0Bytes > size - hdrSize) return false;
    // IWi places Level 0 (largest mip) at the VERY END of the file (`size - level0Bytes`)!
    const size_t level0Off = size - static_cast<size_t>(level0Bytes);
    if (level0Off < hdrSize) return false;
    const uint8_t* src = data + level0Off;

    std::vector<uint8_t> bgra;
    if (bcKind > 0) {
        if (!ConsolePx::DecodeLinearS3tcToPbgra(src, static_cast<size_t>(level0Bytes), width, height, bcKind, bgra)) {
            return false;
        }
    } else {
        const size_t nPx = static_cast<size_t>(width) * height;
        bgra.assign(nPx * 4u, 0u);
        for (size_t i = 0u; i < nPx; ++i) {
            uint8_t r = 0, g = 0, b = 0, a = 255u;
            if (fmtCode == 0x01u) { // BGRA8888 in LE memory (`D3DFMT_A8R8G8B8`)
                b = src[i * 4u + 0u];
                g = src[i * 4u + 1u];
                r = src[i * 4u + 2u];
                a = src[i * 4u + 3u];
            } else if (fmtCode == 0x02u) {
                b = src[i * 3u + 0u];
                g = src[i * 3u + 1u];
                r = src[i * 3u + 2u];
            } else if (fmtCode == 0x03u) {
                r = g = b = src[i * 2u + 0u];
                a = src[i * 2u + 1u];
            } else if (fmtCode == 0x04u) {
                r = g = b = 255u;
                a = src[i];
            } else {
                r = g = b = src[i];
            }
            WritePremulBGRA(&bgra[i * 4u], r, g, b, a);
        }
    }

    wchar_t title[128] = {};
    swprintf_s(title, L"Infinity Ward CoD Texture (.iwi v%u %ls)", version, fmtName);
    return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), title, maxDimension);
}

} // namespace Formats
