#pragma once

static bool ParseSylkSpreadsheet(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::DifSylkTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"SYLK Symbolic Link Spreadsheet (.slk)";

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    SpreadsheetSheet sheet;
    sheet.name = L"SYLK";

    int curY = 1, curX = 1;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        pos = eol + 1;

        if (ln.rfind("C;", 0) == 0) {
            size_t p = 2;
            std::string_view kVal;
            bool hasVal = false;
            while (p < ln.size()) {
                size_t semi = ln.find(';', p);
                if (semi == std::string_view::npos) semi = ln.size();
                std::string_view field = ln.substr(p, semi - p);
                if (!field.empty()) {
                    if (field[0] == 'Y') curY = std::max(1, std::atoi(std::string(field.substr(1)).c_str()));
                    else if (field[0] == 'X') curX = std::max(1, std::atoi(std::string(field.substr(1)).c_str()));
                    else if (field[0] == 'K') {
                        kVal = field.substr(1);
                        if (kVal.size() >= 2 && kVal.front() == '"' && kVal.back() == '"') {
                            kVal = kVal.substr(1, kVal.size() - 2);
                        }
                        hasVal = true;
                    }
                }
                p = semi + 1;
            }
            if (hasVal && curY <= 5000 && curX <= 256) {
                if (static_cast<size_t>(curY) > sheet.rows.size()) {
                    sheet.rows.resize(static_cast<size_t>(curY));
                }
                auto& r = sheet.rows[curY - 1];
                if (static_cast<size_t>(curX) > r.size()) {
                    r.resize(static_cast<size_t>(curX));
                }
                r[curX - 1] = PathUtils::Utf8ToWide(kVal);
                sheet.maxCols = std::max(sheet.maxCols, r.size());
            }
        }
    }
    if (sheet.rows.empty()) return false;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}
