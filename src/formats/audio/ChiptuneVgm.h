#pragma once
// ChiptuneVgm.h — Video Game Music Chip Register Log (.vgm) Inspector

namespace Formats {

inline bool ParseChiptuneVgmDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 0x40 || std::memcmp(raw.data(), "Vgm ", 4) != 0) return false;
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

            out.formatTitle = L"Video Game Music Chip Register Log (.vgm)";
            uint32_t verBcd  = DetailDocs200::RdU32LE(raw.data() + 0x08);
            uint32_t snClk   = DetailDocs200::RdU32LE(raw.data() + 0x0C);
            uint32_t ym2413  = DetailDocs200::RdU32LE(raw.data() + 0x10);
            uint32_t gd3Rel  = DetailDocs200::RdU32LE(raw.data() + 0x14);
            uint32_t samples = DetailDocs200::RdU32LE(raw.data() + 0x18);
            uint32_t ym2612  = DetailDocs200::RdU32LE(raw.data() + 0x2C);
            wchar_t vbuf[32] = {}; swprintf_s(vbuf, L"%X.%02X", (verBcd >> 8) & 0xFFu, verBcd & 0xFFu);
            sh.rows.push_back({ L"VGM Version", vbuf });
            sh.rows.push_back({ L"Duration (@44.1 kHz)", std::to_wstring(samples / 44100) + L"." + std::to_wstring((samples % 44100) / 441) + L" s" });
            if (snClk)  sh.rows.push_back({ L"TI SN76489 PSG Clock", std::to_wstring(snClk) + L" Hz" });
            if (ym2612) sh.rows.push_back({ L"Yamaha YM2612 FM Clock", std::to_wstring(ym2612) + L" Hz" });
            if (ym2413) sh.rows.push_back({ L"Yamaha YM2413 OPLL Clock", std::to_wstring(ym2413) + L" Hz" });
            if (gd3Rel > 0 && 0x14ull + gd3Rel + 12 < raw.size() && std::memcmp(raw.data() + 0x14 + gd3Rel, "Gd3 ", 4) == 0) {
                size_t gp = 0x14 + gd3Rel + 12;
                static const wchar_t* kGd3Names[] = { L"Track (EN)", L"Track (JP)", L"Game (EN)", L"Game (JP)", L"System (EN)", L"System (JP)", L"Author (EN)" };
                for (int t = 0; t < 7 && gp + 2 <= raw.size(); ++t) {
                    std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + gp, raw.size() - gp);
                    gp += (s.size() + 1) * 2;
                    if (!s.empty()) sh.rows.push_back({ kGd3Names[t], s });
                }
            }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;

}

} // namespace Formats
