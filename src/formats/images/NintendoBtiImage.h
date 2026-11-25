#pragma once
// NintendoBtiImage.h — Nintendo GameCube / Wii JSystem Binary Texture (.bti — ResTIMG)
// Used across Wind Waker, Twilight Princess, Super Mario Sunshine, Super Mario Galaxy, and Double Dash.
// Reuses shared NintendoGxTileCommon.h with strict GX hardware enum & offset invariants.

namespace Formats {

inline bool DecodeNintendoBti(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 0x40u) return false;

    const uint8_t  gxFmt       = data[0x00];
    const uint8_t  alphaMode   = data[0x01];
    const uint16_t width       = ByteIO::RdU16BE(data + 0x02);
    const uint16_t height      = ByteIO::RdU16BE(data + 0x04);
    const uint8_t  wrapS       = data[0x06];
    const uint8_t  wrapT       = data[0x07];
    const uint8_t  indexTex    = data[0x08];
    const uint8_t  tlutFmt     = data[0x09];
    const uint16_t numColors   = ByteIO::RdU16BE(data + 0x0A);
    const uint32_t palOff      = ByteIO::RdU32BE(data + 0x0C);
    const uint8_t  mipEn       = data[0x10];
    const uint8_t  edgeLod     = data[0x11];
    const uint8_t  biasClamp   = data[0x12];
    const uint8_t  maxAniso    = data[0x13];
    const uint8_t  minFilter   = data[0x14];
    const uint8_t  magFilter   = data[0x15];
    const uint8_t  mipCount    = data[0x18];
    const uint32_t imgOff      = ByteIO::RdU32BE(data + 0x1C);

    // Strict ResTIMG GX hardware enum validation (since BTI has no ASCII magic)
    if (width == 0u || height == 0u || width > 4096u || height > 4096u) return false;
    if (!(alphaMode <= 4u || alphaMode == 0xCCu)) return false;
    if (wrapS > 2u || wrapT > 2u || indexTex > 1u || tlutFmt > 2u) return false;
    if (mipEn > 1u || edgeLod > 1u || biasClamp > 1u || maxAniso > 2u) return false;
    if (minFilter > 5u || magFilter > 1u || mipCount > 16u) return false;
    if (imgOff < 0x20u || imgOff >= size) return false;

    uint32_t tw = 0, th = 0, tb = 0;
    bool needsPal = false;
    if (!NintendoGx::GetGxTileMetrics(gxFmt, tw, th, tb, needsPal)) return false;

    uint64_t level0Bytes = 0;
    if (!NintendoGx::ComputeGxLevel0Size(width, height, gxFmt, level0Bytes)) return false;
    if (static_cast<uint64_t>(imgOff) + level0Bytes > size) return false;

    std::vector<std::array<uint8_t, 4>> palette;
    if (needsPal) {
        if (numColors == 0u || palOff < 0x20u || palOff >= size) return false;
        if (!NintendoGx::DecodeGxTlut(data + palOff, size - palOff, numColors, tlutFmt, palette)) {
            return false;
        }
    }

    std::vector<uint8_t> bgra;
    if (!NintendoGx::DecodeGxSurfaceToPbgra(
            data + imgOff, size - imgOff, width, height, gxFmt,
            needsPal ? &palette : nullptr, bgra)) {
        return false;
    }

    outInfo = {};
    outInfo.width = width;
    outInfo.height = height;
    outInfo.origWidth = width;
    outInfo.origHeight = height;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels = std::move(bgra);
    outInfo.formatName = std::wstring(L"Nintendo JSystem BTI (") + NintendoGx::GxFormatName(gxFmt) + L")";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
