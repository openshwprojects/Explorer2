#pragma once
// CorelDrawCdr.h — CorelDRAW X4+ ZIP & Classic v3-X3 RIFF DISP Preview Decoder (.cdr)

namespace Formats {

inline bool DecodeCorelDrawCdr(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 8) return false;
        std::wstring label = L"CorelDRAW Artwork (.cdr)";

        // 1. Modern CorelDRAW X4+ ZIP container (metadata/thumbnails/thumbnail.bmp, previews/thumbnail.png, previews/page1.png, ...)
        if (data[0] == 'P' && data[1] == 'K' && (data[2] == 3 || data[2] == 5)) {
            return DetailImg200::TryDecodeBestZipThumbnail(data, size, label, outInfo);
        }

        // 2. Classic CorelDRAW v3–X3 RIFF container ('RIFF'....'CDR*' -> walk RIFF & LIST chunks for 'DISP' DIB)
        if (size >= 16 && std::memcmp(data, "RIFF", 4) == 0 &&
            (std::memcmp(data + 8, "CDR", 3) == 0 || std::memcmp(data + 8, "cdr", 3) == 0))
        {
            uint32_t riffLen = DetailImg100::RdU32LE(data + 4);
            size_t riffEnd = std::min<size_t>(size, 8u + static_cast<size_t>(riffLen));
            if (riffEnd < 16) riffEnd = size;
            if (DetailImg200::WalkRiffCdrForDisp(data, size, 12, riffEnd, 0, label, outInfo)) {
                return true;
            }
            if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 12)) {
                return true;
            }
        }
        return false;
}

} // namespace Formats
