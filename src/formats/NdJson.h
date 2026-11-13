#pragma once
// NdJson.h — Newline-Delimited JSON / JSON Lines (.ndjson / .jsonl) Stream Table Parser

namespace Formats {

inline bool ParseNdJsonDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find('{') == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Newline-Delimited JSON Stream (.ndjson / .jsonl)";

    SpreadsheetSheet sheet;
    sheet.name = L"JSON_Lines";
    std::vector<std::wstring> cols;

    size_t pos = 0;
    while (pos < sv.size() && sheet.rows.size() < 5000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty() || ln.front() != '{') continue;

        std::vector<std::pair<std::wstring, std::wstring>> kv;
        size_t p = 1;
        while (p < ln.size()) {
            size_t q1 = ln.find('"', p);
            if (q1 == std::string_view::npos) break;
            size_t q2 = ln.find('"', q1 + 1);
            if (q2 == std::string_view::npos) break;
            std::wstring key = PathUtils::Utf8ToWide(ln.substr(q1 + 1, q2 - q1 - 1));
            size_t colon = ln.find(':', q2 + 1);
            if (colon == std::string_view::npos) break;
            size_t vs = colon + 1;
            while (vs < ln.size() && (ln[vs] == ' ' || ln[vs] == '\t')) ++vs;
            std::wstring val;
            if (vs < ln.size() && ln[vs] == '"') {
                size_t ve = ln.find('"', vs + 1);
                if (ve == std::string_view::npos) break;
                val = PathUtils::Utf8ToWide(ln.substr(vs + 1, ve - vs - 1));
                p = ve + 1;
            } else {
                size_t ve = vs;
                while (ve < ln.size() && ln[ve] != ',' && ln[ve] != '}') ++ve;
                val = PathUtils::Utf8ToWide(ln.substr(vs, ve - vs));
                p = ve + 1;
            }
            kv.push_back({ key, val });
            if (std::find(cols.begin(), cols.end(), key) == cols.end() && cols.size() < 24) {
                cols.push_back(key);
            }
        }

        if (!kv.empty()) {
            if (sheet.rows.empty()) {
                sheet.rows.push_back(cols);
            }
            std::vector<std::wstring> row(cols.size(), L"");
            for (const auto& pair : kv) {
                auto it = std::find(cols.begin(), cols.end(), pair.first);
                if (it != cols.end()) {
                    row[static_cast<size_t>(it - cols.begin())] = pair.second;
                }
            }
            sheet.rows.push_back(std::move(row));
        }
    }

    if (!sheet.rows.empty() && !cols.empty()) {
        sheet.rows[0] = cols;
    }
    sheet.maxCols = static_cast<int>(cols.size());
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
