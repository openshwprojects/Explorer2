#pragma once

static bool ParseDifSpreadsheet(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::DifSylkTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"DIF Data Interchange Spreadsheet (.dif)";

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::vector<std::string_view> lines;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        lines.push_back(ln);
        pos = eol + 1;
    }

    SpreadsheetSheet sheet;
    sheet.name = L"DIF";
    bool inData = false;
    std::vector<std::wstring> curRow;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string_view ln = lines[i];
        if (!inData) {
            if (ln == "DATA") {
                inData = true;
                if (i + 2 < lines.size()) i += 2;
            }
            continue;
        }
        if (ln == "EOD") break;
        if (ln == "-1,0") {
            if (i + 1 < lines.size() && lines[i + 1] == "BOT") {
                if (!curRow.empty()) {
                    sheet.maxCols = std::max(sheet.maxCols, curRow.size());
                    sheet.rows.push_back(std::move(curRow));
                    curRow.clear();
                }
                ++i;
            } else if (i + 1 < lines.size() && lines[i + 1] == "EOD") {
                break;
            }
        } else if (ln.rfind("1,0", 0) == 0 && i + 1 < lines.size()) {
            std::string_view sVal = lines[++i];
            if (sVal.size() >= 2 && sVal.front() == '"' && sVal.back() == '"') {
                sVal = sVal.substr(1, sVal.size() - 2);
            }
            curRow.push_back(PathUtils::Utf8ToWide(sVal));
        } else if (ln.rfind("0,", 0) == 0 && i + 1 < lines.size()) {
            std::string_view numPart = ln.substr(2);
            ++i; // Skip "V" line
            curRow.push_back(PathUtils::Utf8ToWide(numPart));
        }
    }
    if (!curRow.empty()) {
        sheet.maxCols = std::max(sheet.maxCols, curRow.size());
        sheet.rows.push_back(std::move(curRow));
    }
    if (sheet.rows.empty()) return false;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}
