#pragma once
// TrackerIt.h — Impulse Tracker Module (.it) Inspector

namespace Formats {
namespace DetailMedia100 {

inline bool ParseTrackerIt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 192 || std::memcmp(raw.data(), "IMPM", 4) != 0) return false;

    std::wstring songName = FixedAsciiField(raw.data() + 4, 26);
    uint16_t ordNum = DetailSys100::RdU16LE(raw.data() + 32);
    uint16_t insNum = DetailSys100::RdU16LE(raw.data() + 34);
    uint16_t smpNum = DetailSys100::RdU16LE(raw.data() + 36);
    uint16_t patNum = DetailSys100::RdU16LE(raw.data() + 38);
    uint16_t cwtv   = DetailSys100::RdU16LE(raw.data() + 40);
    uint8_t initSpd = raw[50];
    uint8_t initTmp = raw[51];

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Impulse Tracker Module (.it)";

    SpreadsheetSheet hdr;
    hdr.name = L"IT_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Song Name", songName.empty() ? L"(Untitled)" : songName });
    hdr.rows.push_back({ L"Tracker CWTV", DetailSys100::Hex32(cwtv) });
    hdr.rows.push_back({ L"Orders / Instruments / Samples / Patterns",
        std::to_wstring(ordNum) + L" / " + std::to_wstring(insNum) + L" / " + std::to_wstring(smpNum) + L" / " + std::to_wstring(patNum) });
    hdr.rows.push_back({ L"Initial Speed / Tempo", std::to_wstring(initSpd) + L" / " + std::to_wstring(initTmp) + L" BPM" });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet tbl;
    tbl.name = L"Instruments_&_Samples";
    tbl.rows.push_back({ L"Kind", L"#", L"Name", L"DOS Filename", L"Length", L"C5Speed (Hz)" });

    size_t insTab = 192ULL + ordNum;
    for (uint16_t i = 0; i < insNum && insTab + static_cast<size_t>(i + 1) * 4 <= raw.size(); ++i) {
        uint32_t off = DetailSys100::RdU32LE(raw.data() + insTab + i * 4);
        if (off + 60 <= raw.size() && std::memcmp(raw.data() + off, "IMPI", 4) == 0) {
            tbl.rows.push_back({
                L"Instrument", std::to_wstring(i + 1),
                FixedAsciiField(raw.data() + off + 32, 26),
                FixedAsciiField(raw.data() + off + 4, 12), L"-", L"-"
            });
        }
    }
    size_t smpTab = insTab + static_cast<size_t>(insNum) * 4;
    for (uint16_t s = 0; s < smpNum && smpTab + static_cast<size_t>(s + 1) * 4 <= raw.size(); ++s) {
        uint32_t off = DetailSys100::RdU32LE(raw.data() + smpTab + s * 4);
        if (off + 64 <= raw.size() && std::memcmp(raw.data() + off, "IMPS", 4) == 0) {
            tbl.rows.push_back({
                L"Sample", std::to_wstring(s + 1),
                FixedAsciiField(raw.data() + off + 20, 26),
                FixedAsciiField(raw.data() + off + 4, 12),
                std::to_wstring(DetailSys100::RdU32LE(raw.data() + off + 48)),
                std::to_wstring(DetailSys100::RdU32LE(raw.data() + off + 60))
            });
        }
    }
    FinalizeSheetCols(tbl);

    out.sheets.push_back(std::move(hdr));
    if (tbl.rows.size() > 1) out.sheets.push_back(std::move(tbl));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseTrackerItDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMedia100::ParseTrackerIt(raw, out); }

} // namespace Formats
