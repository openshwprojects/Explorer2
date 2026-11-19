#pragma once
// AffinityImage.h — Affinity Photo / Designer / Publisher Preview & Thmb Decoder (.afphoto, .afdesign, .afpub)

namespace Formats {

inline bool DecodeAffinityImage(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size < 8) return false;
    const bool hasAffinityMagic = (data[0] == 0x00 && data[1] == 0xFF && data[2] == 0x4B && data[3] == 0x41);
    const bool hasZipMagic      = (data[0] == 'P' && data[1] == 'K' && (data[2] == 3 || data[2] == 5));
    if (extLower != L".afphoto" && extLower != L".afdesign" && extLower != L".afpub" && !hasAffinityMagic) {
        return false;
    }
    if (!hasAffinityMagic && !hasZipMagic) {
        return false;
    }

    std::wstring label = (extLower == L".afdesign") ? L"Affinity Designer (.afdesign)" :
                         (extLower == L".afpub")    ? L"Affinity Publisher (.afpub)" :
                                                      L"Affinity Photo (.afphoto)";

    if (hasZipMagic) {
        if (DetailImg200::TryDecodeBestZipThumbnail(data, size, label, outInfo)) return true;
        return false;
    }

    // 1. Scan raw embedded PNG (89 50 4E 47 ... IEND) or JPEG (FF D8 FF ... SOF ... FF D9)
    if (DetailImg200::FindAndDecodeBestEmbeddedRaster(data, size, label, outInfo, 4)) {
        return true;
    }

    // 2. Scan Zlib-compressed 'Thmb' / 'bmhT' blocks inside Affinity container
    for (size_t i = 4; i + 12 < size; ++i) {
        bool isThmbTag = (std::memcmp(data + i, "Thmb", 4) == 0 || std::memcmp(data + i, "bmhT", 4) == 0);
        if (!isThmbTag) continue;

        size_t probeLimit = std::min<size_t>(size, i + 64u);
        for (size_t z = i + 4; z + 6 < probeLimit; ++z) {
            uint8_t cmf = data[z], flg = data[z + 1];
            bool looksLikeZlib = ((cmf & 0x0Fu) == 8u && (cmf >> 4) <= 7u && ((static_cast<uint32_t>(cmf) * 256u + flg) % 31u) == 0u);
            if (!looksLikeZlib) continue;

            size_t maxComp = std::min<size_t>(size - z, 4u * 1024u * 1024u);
            std::vector<uint8_t> inflated;
            if (DetailImg200::CallInflateZlib(data + z, maxComp, inflated, 16u * 1024u * 1024u) && inflated.size() >= 32) {
                if (DetailImg100::DecodeEmbeddedWicBuffer(inflated.data(), inflated.size(), label, outInfo) && outInfo.valid) {
                    return true;
                }
                if (DetailImg200::FindAndDecodeBestEmbeddedRaster(inflated.data(), inflated.size(), label, outInfo, 0)) {
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace Formats
