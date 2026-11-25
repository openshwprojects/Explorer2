#pragma once
// NintendoTplImage.h — Nintendo GameCube / Wii Texture Palette Library (.tpl, magic 0x0020AF30)
// Decodes big-endian TPL archives via shared NintendoGxTileCommon.h (direct GX tiles, CMPR, and CI4/CI8/CI14X2 TLUTs).

namespace Formats {

inline bool DecodeNintendoTpl(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 0x38u) return false;
    if (ByteIO::RdU32BE(data + 0x00) != 0x0020AF30u) return false;

    const uint32_t numImages   = ByteIO::RdU32BE(data + 0x04);
    const uint32_t imgTableOff = ByteIO::RdU32BE(data + 0x08);
    if (numImages == 0u || numImages > 4096u || imgTableOff < 0x0Cu) return false;
    if (static_cast<uint64_t>(imgTableOff) + static_cast<uint64_t>(numImages) * 8ULL > size) {
        return false;
    }

    // Decode first texture entry in TPL table
    const uint32_t imgHdrOff = ByteIO::RdU32BE(data + imgTableOff + 0x00);
    const uint32_t palHdrOff = ByteIO::RdU32BE(data + imgTableOff + 0x04);
    if (imgHdrOff < 0x14u || static_cast<uint64_t>(imgHdrOff) + 36ULL > size) {
        return false;
    }

    const uint8_t* ih       = data + imgHdrOff;
    const uint16_t height   = ByteIO::RdU16BE(ih + 0x00);
    const uint16_t width    = ByteIO::RdU16BE(ih + 0x02);
    const uint32_t gxFmt    = ByteIO::RdU32BE(ih + 0x04);
    const uint32_t pxOff    = ByteIO::RdU32BE(ih + 0x08);
    if (width == 0u || height == 0u || pxOff == 0u || pxOff >= size) {
        return false;
    }

    uint32_t tw = 0, th = 0, tb = 0;
    bool needsPal = false;
    if (!NintendoGx::GetGxTileMetrics(gxFmt, tw, th, tb, needsPal)) {
        return false;
    }

    std::vector<std::array<uint8_t, 4>> palette;
    if (needsPal) {
        if (palHdrOff == 0u || static_cast<uint64_t>(palHdrOff) + 12ULL > size) {
            return false;
        }
        const uint8_t* ph      = data + palHdrOff;
        const uint16_t palCnt  = ByteIO::RdU16BE(ph + 0x00);
        const uint32_t tlutFmt = ByteIO::RdU32BE(ph + 0x04);
        const uint32_t palOff  = ByteIO::RdU32BE(ph + 0x08);
        if (palCnt == 0u || palOff == 0u || palOff >= size) {
            return false;
        }
        if (!NintendoGx::DecodeGxTlut(data + palOff, size - palOff, palCnt, tlutFmt, palette)) {
            return false;
        }
    }

    std::vector<uint8_t> bgra;
    if (!NintendoGx::DecodeGxSurfaceToPbgra(
            data + pxOff, size - pxOff, width, height, gxFmt,
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
    outInfo.formatName = std::wstring(L"Nintendo GameCube/Wii TPL (") + NintendoGx::GxFormatName(gxFmt) + L")";
    if (numImages > 1u) {
        outInfo.formatName += L" [" + std::to_wstring(numImages) + L" textures]";
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
