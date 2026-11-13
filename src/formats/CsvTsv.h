#pragma once
static bool ParseCsvOrTsv(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;

    std::wstring text;
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t wlen = (raw.size() - 2) / 2;
        text.resize(wlen);
        std::memcpy(text.data(), raw.data() + 2, wlen * 2);
    } else {
        size_t start = (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        text = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + start), raw.size() - start));
    }

    // Auto-detect delimiter from first line outside quotes
    wchar_t delim = (extLower == L".tsv") ? L'\t' : L',';
    if (extLower != L".tsv") {
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

    out.formatTitle = (delim == L'\t') ? L"TSV Table (Tab-Separated)"
                    : ((delim == L';') ? L"CSV Table (Semicolon ;)" : L"CSV Table (Comma ,)");

    SpreadsheetSheet sheet;
    sheet.name = L"CSV";
    std::vector<std::wstring> curRow;
    std::wstring curCell;
    bool inQuotes = false;

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (inQuotes) {
            if (c == L'"') {
                if (i + 1 < text.size() && text[i + 1] == L'"') {
                    curCell.push_back(L'"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                curCell.push_back(c);
            }
        } else {
            if (c == L'"' && curCell.empty()) {
                inQuotes = true;
            } else if (c == delim) {
                curRow.push_back(std::move(curCell));
                curCell.clear();
            } else if (c == L'\r' || c == L'\n') {
                if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                curRow.push_back(std::move(curCell));
                curCell.clear();
                if (!curRow.empty() && !(curRow.size() == 1 && curRow[0].empty())) {
                    sheet.maxCols = std::max(sheet.maxCols, curRow.size());
                    sheet.rows.push_back(std::move(curRow));
                }
                curRow.clear();
                if (sheet.rows.size() >= 10000) break;
            } else {
                curCell.push_back(c);
            }
        }
    }
    if (!curCell.empty() || !curRow.empty()) {
        curRow.push_back(std::move(curCell));
        sheet.maxCols = std::max(sheet.maxCols, curRow.size());
        sheet.rows.push_back(std::move(curRow));
    }

    if (sheet.rows.empty()) {
        sheet.rows.push_back({ L"" });
        sheet.maxCols = 1;
    }
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}
