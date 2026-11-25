#pragma once
// NintendoNw4rTex0.h — Nintendo Wii NW4R Binary Texture Resource (.tex0 — "TEX0")
// Reuses shared NintendoGxTileCommon.h (`I4`/`I8`/`IA4`/`IA8`/`RGB565`/`RGB5A3`/`RGBA8`/`CMPR`).

namespace Formats {

inline bool DecodeNintendoNw4rTex0(
    const uint8_t* data,
    size_t size,
    uint32_t maxDimension,
    ExtendedImageInfo& outInfo)
{
    if (!data || size < 0x40u || size > (64u << 20)) return false;
    if (std::memcmp(data, "TEX0", 4) != 0) return false;

    const uint32_t declSize   = ByteIO::RdU32BE(data + 0x04u);
    const uint32_t version    = ByteIO::RdU32BE(data + 0x08u);
    const uint32_t texDataOff = ByteIO::RdU32BE(data + 0x10u);
    const uint32_t hasCiFlag  = ByteIO::RdU32BE(data + 0x18u);
    const uint16_t width      = ByteIO::RdU16BE(data + 0x1Cu);
    const uint16_t height     = ByteIO::RdU16BE(data + 0x1Eu);
    const uint32_t gxFmt      = ByteIO::RdU32BE(data + 0x20u);
    const uint32_t mipCount   = ByteIO::RdU32BE(data + 0x24u);

    if ((version != 1u && version != 2u && version != 3u) || declSize < 0x40u || declSize > size) return false;
    if (width == 0u || height == 0u || width > 4096u || height > 4096u) return false;
    if (mipCount == 0u || mipCount > 16u || hasCiFlag > 1u) return false;
    if (texDataOff < 0x40u || texDataOff >= declSize) return false;

    uint32_t tw = 0, th = 0, tb = 0;
    bool needsPal = false;
    if (!NintendoGx::GetGxTileMetrics(gxFmt, tw, th, tb, needsPal) || needsPal) {
        return false; // Standalone TEX0 without external PLT0 decodes direct/CMPR GX formats
    }

    uint64_t level0Bytes = 0ULL;
    if (!NintendoGx::ComputeGxLevel0Size(width, height, gxFmt, level0Bytes)) return false;
    if (level0Bytes > declSize - texDataOff) return false;

    std::vector<uint8_t> bgra;
    if (!NintendoGx::DecodeGxSurfaceToPbgra(data + texDataOff, declSize - texDataOff, width, height, gxFmt, nullptr, bgra)) {
        return false;
    }

    std::wstring fmtTitle = std::wstring(L"Nintendo Wii NW4R TEX0 (") + NintendoGx::GxFormatName(gxFmt) + L")";
    return ConsolePx::FinalizeConsoleImageInfo(outInfo, width, height, std::move(bgra), std::move(fmtTitle), maxDimension);
}

} // namespace Formats
