#pragma once
// TrackerS3m.h — Scream Tracker 3 Module (.s3m) Inspector

namespace Formats {
namespace DetailMediaAndScoreDoc {

inline bool ParseTrackerS3m(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 96 || std::memcmp(raw.data() + 44, "SCRM", 4) != 0) return false;

    std::wstring title = FixedAsciiField(raw.data(), 28);
    uint16_t ordNum = DetailBinarySysDoc::RdU16LE(raw.data() + 32);
    uint16_t insNum = DetailBinarySysDoc::RdU16LE(raw.data() + 34);
    uint16_t patNum = DetailBinarySysDoc::RdU16LE(raw.data() + 36);
    uint16_t cwtv   = DetailBinarySysDoc::RdU16LE(raw.data() + 40);
    uint8_t initSpd = raw[49];
    uint8_t initTmp = raw[50];

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ScreamTracker 3 Module (.s3m)";

    SpreadsheetSheet hdr;
    hdr.name = L"S3M_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Title", title.empty() ? L"(Untitled)" : title });
    hdr.rows.push_back({ L"Tracker Version", DetailBinarySysDoc::Hex32(cwtv) });
    hdr.rows.push_back({ L"Orders / Instruments / Patterns",
        std::to_wstring(ordNum) + L" / " + std::to_wstring(insNum) + L" / " + std::to_wstring(patNum) });
    hdr.rows.push_back({ L"Initial Speed / Tempo", std::to_wstring(initSpd) + L" / " + std::to_wstring(initTmp) + L" BPM" });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet ins;
    ins.name = L"Instruments";
    ins.rows.push_back({ L"#", L"Instrument Name", L"DOS Filename", L"Length", L"C2Spd (Hz)", L"Volume" });
    size_t ptrTable = 96ULL + ordNum;
    for (uint16_t i = 0; i < insNum && ptrTable + static_cast<size_t>(i + 1) * 2 <= raw.size(); ++i) {
        size_t iOff = static_cast<size_t>(DetailBinarySysDoc::RdU16LE(raw.data() + ptrTable + i * 2)) * 16ULL;
        if (iOff == 0 || iOff + 80 > raw.size()) continue;
        std::wstring dosFn = FixedAsciiField(raw.data() + iOff + 1, 12);
        uint32_t len = DetailBinarySysDoc::RdU32LE(raw.data() + iOff + 16);
        uint8_t vol = raw[iOff + 28];
        uint32_t c2spd = DetailBinarySysDoc::RdU32LE(raw.data() + iOff + 32);
        std::wstring sName = FixedAsciiField(raw.data() + iOff + 48, 28);
        ins.rows.push_back({ std::to_wstring(i + 1), sName, dosFn, std::to_wstring(len), std::to_wstring(c2spd), std::to_wstring(vol) });
    }
    FinalizeSheetCols(ins);

    out.sheets.push_back(std::move(hdr));
    if (ins.rows.size() > 1) out.sheets.push_back(std::move(ins));
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseTrackerS3mDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseTrackerS3m(raw, out); }

} // namespace Formats
