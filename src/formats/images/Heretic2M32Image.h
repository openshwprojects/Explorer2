#pragma once
// Heretic2M32Image.h — Raven Software Heretic II / Soldier of Fortune 32-bit RGBA MipTexture (.m32, version == 4)
// Reuses shared QuakeMipTexCommon.h & FormatPrologueCommon.h.

namespace Formats {

inline bool DecodeHeretic2M32(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    constexpr size_t kM32HeaderBytes = 968u;
    if (!data || size <= kM32HeaderBytes || ByteIO::RdI32LE(data) != 4) return false;

    uint32_t w0 = 0u, h0 = 0u, off0 = 0u;
    if (!QuakeMipCommon::Validate16LevelMipChain(
            data, size, kM32HeaderBytes, 0x204u, 0x244u, 0x284u, 4u, w0, h0, off0)) {
        return false;
    }

    std::string texName;
    if (!FormatPrologue::ReadPrintableFixedAscii(data + 4u, 128u, texName)) return false;

    const size_t pxCount = static_cast<size_t>(w0) * h0;
    outInfo = {};
    outInfo.width = w0;
    outInfo.height = h0;
    outInfo.origWidth = w0;
    outInfo.origHeight = h0;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.bgraPixels.resize(pxCount * 4u);

    const uint8_t* src = data + off0;
    for (size_t i = 0u; i < pxCount; ++i) {
        WritePremulBGRA(&outInfo.bgraPixels[i * 4u], src[i * 4u + 0u], src[i * 4u + 1u], src[i * 4u + 2u], src[i * 4u + 3u]);
    }

    outInfo.formatName = L"Raven Heretic II / SoF M32 (RGBA32)";
    if (!texName.empty()) outInfo.formatName += L" — " + PathUtils::Utf8ToWide(texName);
    outInfo.valid = true;
    return true;
}

} // namespace Formats
