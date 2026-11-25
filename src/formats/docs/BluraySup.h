#pragma once
// BluraySup.h — Blu-ray PGS Subtitle Stream (.sup) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseBluraySup(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 13 || raw[0] != 'P' || raw[1] != 'G') return false;

    uint16_t videoW = 0, videoH = 0;
    size_t pcsCount = 0, odsCount = 0, pdsCount = 0, endCount = 0;

    SpreadsheetSheet segs;
    segs.name = L"PGS_Segments";
    segs.rows.push_back({ L"#", L"PTS", L"Segment Type", L"Size (B)", L"Details" });

    size_t pos = 0;
    size_t segIdx = 0;
    while (pos + 13 <= raw.size()) {
        if (raw[pos] != 'P' || raw[pos + 1] != 'G') break;
        uint32_t pts = DetailBinarySysDoc::RdU32BE(raw.data() + pos + 2);
        uint8_t sType = raw[pos + 10];
        uint16_t sSize = DetailBinarySysDoc::RdU16BE(raw.data() + pos + 11);
        if (pos + 13 + sSize > raw.size()) break;
        const uint8_t* p = raw.data() + pos + 13;

        std::wstring typeName = DetailBinarySysDoc::Hex32(sType);
        std::wstring detail;
        if (sType == 0x16) {
            typeName = L"PCS (Presentation)";
            ++pcsCount;
            if (sSize >= 11) {
                videoW = DetailBinarySysDoc::RdU16BE(p);
                videoH = DetailBinarySysDoc::RdU16BE(p + 2);
                uint16_t compNum = DetailBinarySysDoc::RdU16BE(p + 5);
                uint8_t nObjs = p[10];
                detail = std::to_wstring(videoW) + L"x" + std::to_wstring(videoH) +
                         L", comp#" + std::to_wstring(compNum) + L", objs=" + std::to_wstring(nObjs);
            }
        } else if (sType == 0x17) {
            typeName = L"WDS (Window)";
        } else if (sType == 0x14) {
            typeName = L"PDS (Palette)";
            ++pdsCount;
            if (sSize >= 2) detail = std::to_wstring((sSize - 2) / 5) + L" entries";
        } else if (sType == 0x15) {
            typeName = L"ODS (Bitmap Object)";
            ++odsCount;
            if (sSize >= 11 && (p[3] & 0x80u) != 0) {
                uint16_t bw = DetailBinarySysDoc::RdU16BE(p + 7);
                uint16_t bh = DetailBinarySysDoc::RdU16BE(p + 9);
                detail = L"Bitmap " + std::to_wstring(bw) + L"x" + std::to_wstring(bh);
            }
        } else if (sType == 0x80) {
            typeName = L"END (Display Set)";
            ++endCount;
        }

        if (segs.rows.size() < 2048) {
            segs.rows.push_back({
                std::to_wstring(++segIdx), FormatPts90kHz(pts), typeName, std::to_wstring(sSize), detail
            });
        } else {
            ++segIdx;
        }
        pos += 13ULL + sSize;
    }

    if (segIdx == 0) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Blu-ray PGS Subtitle Stream (.sup)";

    SpreadsheetSheet sum;
    sum.name = L"PGS_Summary";
    sum.rows.push_back({ L"Property", L"Value" });
    if (videoW > 0) sum.rows.push_back({ L"Video Resolution", std::to_wstring(videoW) + L"x" + std::to_wstring(videoH) });
    sum.rows.push_back({ L"Total Segments", std::to_wstring(segIdx) });
    sum.rows.push_back({ L"PCS / ODS / PDS / END",
        std::to_wstring(pcsCount) + L" / " + std::to_wstring(odsCount) + L" / " + std::to_wstring(pdsCount) + L" / " + std::to_wstring(endCount) });
    FinalizeSheetCols(sum);
    FinalizeSheetCols(segs);
    out.sheets.push_back(std::move(sum));
    out.sheets.push_back(std::move(segs));
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseBluraySupDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseBluraySup(raw, out); }

} // namespace Formats
