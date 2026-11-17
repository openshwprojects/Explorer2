#pragma once
// Sylk.h — Microsoft SYLK (.slk / .sylk): ID record first, then C (cell) and F (format) records whose X / Y
// fields move the current cell, B (bounds) and E (end). Inside a record ";;" is a literal semicolon, and a
// quoted K value doubles its embedded quotes; unquoted K values are numbers, TRUE / FALSE or #errors.

static bool ParseSylkSpreadsheet(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() < 2 || sv.substr(0, 2) != "ID") return false;

    // One record's fields, with ";;" unescaped
    auto splitFields = [](std::string_view rec, std::vector<std::string>& fields) {
        fields.clear();
        std::string cur;
        for (size_t i = 0; i < rec.size(); ++i) {
            if (rec[i] == ';') {
                if (i + 1 < rec.size() && rec[i + 1] == ';') { cur.push_back(';'); ++i; continue; }
                fields.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(rec[i]);
            }
        }
        fields.push_back(std::move(cur));
    };
    auto parseIndex = [](std::string_view digits, int& outVal) {
        int v = 0;
        if (digits.empty()) return false;
        for (char c : digits) {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
            if (v > 1000000) return false;
        }
        if (v < 1) return false;
        outVal = v;
        return true;
    };

    constexpr int kMaxRows = 50000, kMaxCols = 512;
    SpreadsheetSheet sheet;
    sheet.name = L"SYLK";
    int curY = 1, curX = 1;
    size_t cells = 0;
    std::vector<std::string> fields;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        pos = eol + 1;
        if (ln.empty()) continue;
        splitFields(ln, fields);
        const std::string& type = fields[0];
        if (type == "E") break;
        if (type != "C" && type != "F") continue;

        std::string value;
        bool hasValue = false;
        for (size_t f = 1; f < fields.size(); ++f) {
            const std::string& fld = fields[f];
            if (fld.empty()) continue;
            std::string_view arg = std::string_view(fld).substr(1);
            if (fld[0] == 'Y') parseIndex(arg, curY);
            else if (fld[0] == 'X') parseIndex(arg, curX);
            else if (fld[0] == 'K' && type == "C") {
                hasValue = true;
                if (arg.size() >= 1 && arg.front() == '"') {
                    arg.remove_prefix(1);
                    if (!arg.empty() && arg.back() == '"') arg.remove_suffix(1);
                    value.clear();
                    for (size_t i = 0; i < arg.size(); ++i) {
                        value.push_back(arg[i]);
                        if (arg[i] == '"' && i + 1 < arg.size() && arg[i + 1] == '"') ++i;
                    }
                } else {
                    value.assign(arg.begin(), arg.end());
                }
            }
        }
        if (!hasValue || curY > kMaxRows || curX > kMaxCols) continue;
        if (static_cast<size_t>(curY) > sheet.rows.size()) sheet.rows.resize(static_cast<size_t>(curY));
        auto& row = sheet.rows[static_cast<size_t>(curY) - 1];
        if (static_cast<size_t>(curX) > row.size()) row.resize(static_cast<size_t>(curX));
        row[static_cast<size_t>(curX) - 1] = PathUtils::Utf8ToWide(value);
        sheet.maxCols = std::max(sheet.maxCols, row.size());
        ++cells;
    }
    if (cells == 0) return false;

    out.kind = OfficeFileKind::DifSylkTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"SYLK Symbolic Link Spreadsheet (.slk)";
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}
