#pragma once
// Pkm.h — Ericsson Texture Compression (.pkm, PKM 10 / PKM 20) Decoder
// Covers: ETC1 (0), ETC2_RGB T/H/Planar (1), ETC2_RGBA EAC+ETC2 (2,3),
//         ETC2_RGB_A1 punchthrough (4), EAC_R11 (5,7), and EAC_RG11 (6,8).

namespace Formats {

inline bool DecodePKM(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 16 || std::memcmp(data, "PKM ", 4) != 0) return false;

    const bool isV1 = (data[4] == '1' && data[5] == '0');
    const bool isV2 = (data[4] == '2' && data[5] == '0');
    if (!isV1 && !isV2) return false;

    auto rdBE16 = [](const uint8_t* p) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    };

    const uint16_t formatType = rdBE16(data + 6);
    if (isV1 && formatType != 0u) return false;
    if (formatType > 8u) return false;

    const uint16_t extW  = rdBE16(data + 8);
    const uint16_t extH  = rdBE16(data + 10);
    const uint16_t origW = rdBE16(data + 12);
    const uint16_t origH = rdBE16(data + 14);

    const uint32_t w = (origW > 0) ? origW : extW;
    const uint32_t h = (origH > 0) ? origH : extH;
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return false;
    if ((extW > 0 && extW < w) || (extH > 0 && extH < h)) return false;

    const uint32_t padW = (extW >= w) ? extW : ((w + 3u) & ~3u);
    const uint32_t padH = (extH >= h) ? extH : ((h + 3u) & ~3u);
    const uint32_t bw = (padW + 3u) / 4u;
    const uint32_t bh = (padH + 3u) / 4u;
    if (bw == 0 || bh == 0) return false;

    size_t bytesPerBlock = 8u;
    uint32_t channels = 3u;
    uint32_t bitsPerCh = 8u;
    std::wstring fmtDesc;

    switch (formatType) {
        case 0:
            bytesPerBlock = 8u;  channels = 3u; bitsPerCh = 8u;
            fmtDesc = L"PKM v1/v2 ETC1 RGB8 (.pkm)";
            break;
        case 1:
            bytesPerBlock = 8u;  channels = 3u; bitsPerCh = 8u;
            fmtDesc = L"PKM v2 ETC2 RGB8 (.pkm)";
            break;
        case 2:
        case 3:
            bytesPerBlock = 16u; channels = 4u; bitsPerCh = 8u;
            fmtDesc = L"PKM v2 ETC2 RGBA8 + EAC (.pkm)";
            break;
        case 4:
            bytesPerBlock = 8u;  channels = 4u; bitsPerCh = 8u;
            fmtDesc = L"PKM v2 ETC2 RGB8A1 Punchthrough (.pkm)";
            break;
        case 5:
            bytesPerBlock = 8u;  channels = 1u; bitsPerCh = 11u;
            fmtDesc = L"PKM v2 EAC R11 UNORM (.pkm)";
            break;
        case 6:
            bytesPerBlock = 16u; channels = 2u; bitsPerCh = 11u;
            fmtDesc = L"PKM v2 EAC RG11 UNORM (.pkm)";
            break;
        case 7:
            bytesPerBlock = 8u;  channels = 1u; bitsPerCh = 11u;
            fmtDesc = L"PKM v2 EAC R11 SNORM (.pkm)";
            break;
        case 8:
            bytesPerBlock = 16u; channels = 2u; bitsPerCh = 11u;
            fmtDesc = L"PKM v2 EAC RG11 SNORM (.pkm)";
            break;
        default:
            return false;
    }

    const uint64_t requiredPayload = static_cast<uint64_t>(bw) * bh * bytesPerBlock;
    if (requiredPayload > size - 16u) return false;

    outInfo.width = w;
    outInfo.height = h;
    outInfo.origWidth = w;
    outInfo.origHeight = h;
    outInfo.channels = channels;
    outInfo.bitsPerChannel = bitsPerCh;
    outInfo.formatName = fmtDesc;
    outInfo.bgraPixels.assign(static_cast<size_t>(w) * h * 4u, 0u);

    const uint8_t* ptr = data + 16;
    uint8_t blkRgba[64];

    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            switch (formatType) {
                case 0: GpuTex::DecodeETC2RgbBlock(ptr, blkRgba, false); break;
                case 1: GpuTex::DecodeETC2RgbBlock(ptr, blkRgba, true);  break;
                case 2:
                case 3: GpuTex::DecodeETC2Block(ptr, blkRgba, true);     break;
                case 4: GpuTex::DecodeETC2RgbA1Block(ptr, blkRgba);      break;
                case 5: GpuTex::DecodeEacR11Block(ptr, false, blkRgba);  break;
                case 6: GpuTex::DecodeEacRG11Block(ptr, false, blkRgba); break;
                case 7: GpuTex::DecodeEacR11Block(ptr, true, blkRgba);   break;
                case 8: GpuTex::DecodeEacRG11Block(ptr, true, blkRgba);  break;
                default: return false;
            }
            GpuTex::BlitBlock4x4RgbaToPremulBgra(blkRgba, bx, by, w, h, outInfo.bgraPixels);
            ptr += bytesPerBlock;
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
