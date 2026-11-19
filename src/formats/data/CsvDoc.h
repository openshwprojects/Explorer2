#pragma once
// CsvDoc.h — Comma / Semicolon / Pipe-Separated Values (.csv, .psv) Reader

namespace Formats {

inline bool ParseCsvDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;

    std::wstring text;
    wchar_t delim = L',';
    bool delimFromHint = false;
    DetailCsvTsv::DecodeDelimitedTextAndSepHint(raw, L',', text, delim, delimFromHint);

    // Auto-detect delimiter from first line outside quotes
    if (!delimFromHint) {
        int commas = 0, semis = 0, tabs = 0, pipes = 0;
        bool inQ = false;
        for (size_t i = 0; i < std::min<size_t>(text.size(), 2048); ++i) {
            wchar_t c = text[i];
            if (c == L'"') inQ = !inQ;
            else if (!inQ) {
                if (c == L'\n') break;
                if (c == L',') ++commas;
                else if (c == L';') ++semis;
                else if (c == L'\t') ++tabs;
                else if (c == L'|') ++pipes;
            }
        }
        int best = commas;
        if (semis > best) { best = semis; delim = L';'; }
        if (tabs > best)  { best = tabs;  delim = L'\t'; }
        if (pipes > best) { best = pipes; delim = L'|'; }
    }

    out.formatTitle = DetailCsvTsv::FormatDelimitedTitle(delim);
    return DetailCsvTsv::ParseDelimitedRowsToSheet(text, delim, L"CSV", out);
}

} // namespace Formats
