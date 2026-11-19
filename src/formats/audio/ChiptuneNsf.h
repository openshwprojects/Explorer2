#pragma once
// ChiptuneNsf.h — Nintendo NES 2A03 / Expansion Chiptune (.nsf) Inspector

namespace Formats {

inline bool ParseChiptuneNsfDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 0x80 || std::memcmp(raw.data(), "NESM\x1A", 5) != 0) return false;
    auto trimNullAscii = DetailDocs200::TrimNullAscii;
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

            out.formatTitle = L"Nintendo NES 2A03 / Expansion Chiptune (.nsf)";
            sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x0E, 32) });
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0x2E, 32) });
            sh.rows.push_back({ L"Copyright", trimNullAscii(raw.data() + 0x4E, 32) });
            sh.rows.push_back({ L"Total Songs", std::to_wstring(raw[6]) + L" (Start #" + std::to_wstring(raw[7]) + L")" });
            uint8_t exp = raw[0x7B];
            std::wstring chips = L"2A03 APU";
            if (exp & 0x01) chips += L" + Konami VRC6";
            if (exp & 0x02) chips += L" + Konami VRC7";
            if (exp & 0x04) chips += L" + FDS";
            if (exp & 0x08) chips += L" + MMC5";
            if (exp & 0x10) chips += L" + Namco 163";
            if (exp & 0x20) chips += L" + Sunsoft 5B";
            sh.rows.push_back({ L"Sound Chips", chips });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;

}

} // namespace Formats
