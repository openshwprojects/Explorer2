#pragma once
// SegaGvrImage.h — Sega GameCube / Wii "GVRT" (+ optional "GBIX"/"GCIX") Texture (.gvr)
// Used across Sonic Adventure DX, Sonic Adventure 2: Battle, PSO Ep I & II, and Super Monkey Ball.
// Reuses shared NintendoGxTileCommon.h for all Flipper/Hollywood GX tile & embedded TLUT formats.

namespace Formats {

inline bool DecodeSegaGvr(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 0x30u) return false;

    size_t gvrtOff = 0u;
    if (std::memcmp(data, "GBIX", 4) == 0 || std::memcmp(data, "GCIX", 4) == 0) {
        const uint32_t lenLE = ByteIO::RdU32LE(data + 4u);
        const uint32_t lenBE = ByteIO::RdU32BE(data + 4u);
        if (lenLE <= size - 12u && std::memcmp(data + 8u + lenLE, "GVRT", 4) == 0) {
            gvrtOff = 8u + lenLE;
        } else if (lenBE <= size - 12u && std::memcmp(data + 8u + lenBE, "GVRT", 4) == 0) {
            gvrtOff = 8u + lenBE;
        } else {
            return false;
        }
    }

    if (size - gvrtOff < 0x10u) return false;
    const uint8_t* gv = data + gvrtOff;
    if (std::memcmp(gv, "GVRT", 4) != 0) return false;

    const uint8_t  dataFlags = gv[0x0A];
    const uint8_t  gxFmt     = gv[0x0B];
    const uint16_t width     = ByteIO::RdU16BE(gv + 0x0C);
    const uint16_t height    = ByteIO::RdU16BE(gv + 0x0E);
    if (width == 0u || height == 0u || width > 4096u || height > 4096u) return false;

    uint32_t tw = 0, th = 0, tb = 0;
    bool needsPal = false;
    if (!NintendoGx::GetGxTileMetrics(gxFmt, tw, th, tb, needsPal)) return false;

    size_t curOff = gvrtOff + 0x10u;
    std::vector<std::array<uint8_t, 4>> palette;
    if (needsPal) {
        // Require embedded palette flag (bit 0x08) for standalone .gvr decoding
        if ((dataFlags & 0x08u) == 0u) return false;
        const uint32_t tlutFmt  = static_cast<uint32_t>((dataFlags >> 4u) & 0x0Fu);
        const uint32_t palCount = (gxFmt == NintendoGx::GX_TF_CI4) ? 16u : 256u;
        const size_t   palBytes = static_cast<size_t>(palCount) * 2u;
        if (size - curOff < palBytes) return false;
        if (!NintendoGx::DecodeGxTlut(data + curOff, size - curOff, palCount, tlutFmt, palette)) {
            return false;
        }
        curOff += palBytes;
    }

    std::vector<uint8_t> bgra;
    if (!NintendoGx::DecodeGxSurfaceToPbgra(
            data + curOff, size - curOff, width, height, gxFmt,
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
    outInfo.formatName = std::wstring(L"Sega GameCube/Wii GVR (") + NintendoGx::GxFormatName(gxFmt) + L")";
    outInfo.valid = true;
    return true;
}

} // namespace Formats
