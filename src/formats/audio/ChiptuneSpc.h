#pragma once
// ChiptuneSpc.h — Super Nintendo Sony SPC700 S-DSP State (.spc) Inspector

namespace Formats {

inline bool ParseChiptuneSpcDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 0x100 || std::memcmp(raw.data(), "SNES-SPC700", 11) != 0) return false;
    auto trimNullAscii = DetailDocs200::TrimNullAscii;
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

            out.formatTitle = L"Super Nintendo Sony SPC700 S-DSP State (.spc)";
            sh.rows.push_back({ L"Song Title", trimNullAscii(raw.data() + 0x2E, 32) });
            sh.rows.push_back({ L"Game Title", trimNullAscii(raw.data() + 0x4E, 32) });
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0xB1, 32) });
            sh.rows.push_back({ L"Dumper", trimNullAscii(raw.data() + 0x6E, 16) });
            sh.rows.push_back({ L"Comments", trimNullAscii(raw.data() + 0x7E, 32) });
            sh.rows.push_back({ L"SPC700 PC Register", DetailSys100::Hex32(DetailDocs200::RdU16LE(raw.data() + 0x25)) });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;

}

} // namespace Formats
