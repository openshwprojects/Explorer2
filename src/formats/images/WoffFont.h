#pragma once
// WoffFont.h — W3C Web Open Font Format 1.0 ("wOFF" .woff) Zlib Table Unpacker & Specimen Decoder

namespace Formats {

inline bool ReconstructSfntFromWoff1(const uint8_t* data, size_t size, std::vector<uint8_t>& outSfnt) {
    using namespace FontOutline;
    if (!data || size < 44u || std::memcmp(data, "wOFF", 4) != 0) return false;
    uint32_t flavor = DetailLayeredAndHdrImg::RdU32BE(data + 4u);
    uint16_t numTables = DetailLayeredAndHdrImg::RdU16BE(data + 12u);
    if (numTables == 0 || numTables > 256u) return false;
    if (44ull + static_cast<size_t>(numTables) * 20ull > size) return false;

    struct WoffRec {
        uint32_t tag = 0;
        uint32_t woffOffset = 0;
        uint32_t compLength = 0;
        uint32_t origLength = 0;
        uint32_t origChecksum = 0;
        std::vector<uint8_t> rawBytes;
    };
    std::vector<WoffRec> recs(numTables);
    size_t totalDataAligned = 0;

    for (uint16_t i = 0; i < numTables; ++i) {
        const uint8_t* ent = data + 44u + static_cast<size_t>(i) * 20u;
        WoffRec& r = recs[i];
        r.tag = DetailLayeredAndHdrImg::RdU32BE(ent + 0u);
        r.woffOffset = DetailLayeredAndHdrImg::RdU32BE(ent + 4u);
        r.compLength = DetailLayeredAndHdrImg::RdU32BE(ent + 8u);
        r.origLength = DetailLayeredAndHdrImg::RdU32BE(ent + 12u);
        r.origChecksum = DetailLayeredAndHdrImg::RdU32BE(ent + 16u);

        if (r.woffOffset > size || r.compLength > size - r.woffOffset) return false;
        if (r.compLength > r.origLength || r.origLength > 64u * 1024u * 1024u) return false;

        if (r.origLength == 0) {
            r.rawBytes.clear();
        } else if (r.compLength < r.origLength) {
            if (!ZipDeflate::InflateZlib(data + r.woffOffset, r.compLength, r.rawBytes, static_cast<size_t>(r.origLength) + 64u) ||
                r.rawBytes.size() != r.origLength) {
                return false;
            }
        } else {
            r.rawBytes.assign(data + r.woffOffset, data + r.woffOffset + r.origLength);
        }
        if (r.origChecksum == 0 && !r.rawBytes.empty()) {
            r.origChecksum = CalcSfntChecksum(r.rawBytes.data(), r.rawBytes.size());
        }
        totalDataAligned += (static_cast<size_t>(r.origLength) + 3u) & ~size_t(3u);
    }

    std::sort(recs.begin(), recs.end(), [](const WoffRec& a, const WoffRec& b) {
        return a.tag < b.tag;
    });

    uint16_t entrySelector = 0;
    uint16_t maxPow2 = 1;
    while (static_cast<uint32_t>(maxPow2) * 2u <= numTables) {
        maxPow2 = static_cast<uint16_t>(maxPow2 * 2u);
        ++entrySelector;
    }
    uint16_t searchRange = static_cast<uint16_t>(maxPow2 * 16u);
    uint16_t rangeShift = static_cast<uint16_t>(numTables * 16u - searchRange);

    size_t dirBytes = 12u + static_cast<size_t>(numTables) * 16u;
    outSfnt.assign(dirBytes + totalDataAligned, 0u);

    WrU32BE(outSfnt.data() + 0u, flavor);
    WrU16BE(outSfnt.data() + 4u, numTables);
    WrU16BE(outSfnt.data() + 6u, searchRange);
    WrU16BE(outSfnt.data() + 8u, entrySelector);
    WrU16BE(outSfnt.data() + 10u, rangeShift);

    size_t curOff = dirBytes;
    for (uint16_t i = 0; i < numTables; ++i) {
        uint8_t* recPtr = outSfnt.data() + 12u + static_cast<size_t>(i) * 16u;
        WrU32BE(recPtr + 0u, recs[i].tag);
        WrU32BE(recPtr + 4u, recs[i].origChecksum);
        WrU32BE(recPtr + 8u, static_cast<uint32_t>(curOff));
        WrU32BE(recPtr + 12u, recs[i].origLength);
        if (!recs[i].rawBytes.empty()) {
            std::memcpy(outSfnt.data() + curOff, recs[i].rawBytes.data(), recs[i].rawBytes.size());
        }
        curOff += (static_cast<size_t>(recs[i].origLength) + 3u) & ~size_t(3u);
    }
    return true;
}

inline bool DecodeWoffFont(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    std::vector<uint8_t> sfnt;
    if (!ReconstructSfntFromWoff1(data, size, sfnt)) return false;
    return FontOutline::RenderSfntFontSpecimen(sfnt.data(), sfnt.size(), L"WOFF WebFont", outInfo);
}

} // namespace Formats
