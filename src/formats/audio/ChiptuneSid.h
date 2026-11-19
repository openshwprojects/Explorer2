#pragma once
// ChiptuneSid.h — Commodore 64 MOS 6581/8580 SID Music (.sid) Inspector

namespace Formats {

inline bool ParseChiptuneSidDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 0x76 || (std::memcmp(raw.data(), "PSID", 4) != 0 && std::memcmp(raw.data(), "RSID", 4) != 0)) return false;
    auto trimNullAscii = DetailDocs200::TrimNullAscii;
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

            out.formatTitle = L"Commodore 64 MOS 6581/8580 SID Music (.sid)";
            sh.rows.push_back({ L"Container Magic", trimNullAscii(raw.data(), 4) + L" v" + std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 4)) });
            sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x16, 32) });
            sh.rows.push_back({ L"Author", trimNullAscii(raw.data() + 0x36, 32) });
            sh.rows.push_back({ L"Released", trimNullAscii(raw.data() + 0x56, 32) });
            sh.rows.push_back({ L"Sub-Songs", std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 0x0E)) + L" (Start #" + std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 0x10)) + L")" });
            wchar_t addrs[96] = {};
            swprintf_s(addrs, L"Load=$%04X, Init=$%04X, Play=$%04X",
                DetailDocs200::RdU16BE(raw.data() + 0x08), DetailDocs200::RdU16BE(raw.data() + 0x0A), DetailDocs200::RdU16BE(raw.data() + 0x0C));
            sh.rows.push_back({ L"6510 Addresses", addrs });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;

}

} // namespace Formats
