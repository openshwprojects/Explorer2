#pragma once
// VobSubMpegSub.h — DVD VobSub MPEG-2 PS SPU Binary Stream (.sub) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseVobSubSpu(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // 1. VobSub Binary MPEG-2 PS (.sub starting with 0x000001BA)
    if (raw.size() >= 14 && DetailBinarySysDoc::RdU32BE(raw.data()) == 0x000001BAu) {
        SpreadsheetSheet spu;
        spu.name = L"VobSub_Packets";
        spu.rows.push_back({ L"#", L"Substream ID", L"PTS", L"PES Length" });
        size_t pos = 0;
        size_t pktIdx = 0;
        while (pos + 6 <= raw.size() && spu.rows.size() < 2048) {
            if (raw[pos] == 0 && raw[pos + 1] == 0 && raw[pos + 2] == 1) {
                uint8_t code = raw[pos + 3];
                if (code == 0xBA) {
                    if (pos + 14 > raw.size()) break;
                    size_t stuff = raw[pos + 13] & 7u;
                    pos += 14 + stuff;
                    continue;
                }
                uint16_t pesLen = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 4);
                if (code == 0xBD && pos + 9 <= raw.size() && pesLen >= 4) {
                    uint8_t flags2 = raw[pos + 7];
                    uint8_t hdrDataLen = raw[pos + 8];
                    uint64_t pts = 0;
                    if ((flags2 & 0x80u) != 0 && hdrDataLen >= 5 && pos + 14 <= raw.size()) {
                        const uint8_t* pt = raw.data() + pos + 9;
                        pts = (static_cast<uint64_t>((pt[0] >> 1) & 7u) << 30) |
                              (static_cast<uint64_t>(pt[1]) << 22) |
                              (static_cast<uint64_t>((pt[2] >> 1) & 0x7Fu) << 15) |
                              (static_cast<uint64_t>(pt[3]) << 7) |
                              static_cast<uint64_t>((pt[4] >> 1) & 0x7Fu);
                    }
                    size_t subIdOff = pos + 9 + hdrDataLen;
                    uint8_t subId = (subIdOff < raw.size()) ? raw[subIdOff] : 0;
                    spu.rows.push_back({
                        std::to_wstring(++pktIdx),
                        DetailBinarySysDoc::Hex32(subId),
                        FormatPts90kHz(pts),
                        std::to_wstring(pesLen)
                    });
                }
                pos += 6ULL + pesLen;
                continue;
            }
            ++pos;
        }
        if (pktIdx == 0) return false;
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"DVD VobSub SPU Stream (.sub)";
        FinalizeSheetCols(spu);
        out.sheets.push_back(std::move(spu));
        out.valid = true;
        return true;
    }

    return false;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseVobSubSpuDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailMediaAndScoreDoc::ParseVobSubSpu(raw, out);
}

} // namespace Formats
