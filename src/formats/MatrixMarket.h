#pragma once
// MatrixMarket.h — NIST Matrix Market (.mtx) Sparse & Dense Matrix Exchange Format Parser

namespace Formats {

inline bool ParseMatrixMarketDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.rfind("%%MatrixMarket", 0) != 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"NIST Matrix Market Sparse Matrix (.mtx)";

    SpreadsheetSheet sheet;
    sheet.name = L"Matrix_Entries";
    sheet.rows.push_back({ L"Entry #", L"Row (i)", L"Column (j)", L"Value (A[i,j])" });

    bool gotDims = false;
    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '%') continue;

        if (!gotDims) {
            gotDims = true;
            continue;
        }
        char r[32] = {}, c[32] = {}, v[64] = "1.0";
        if (sscanf_s(std::string(ln).c_str(), "%31s %31s %63s",
            r, (unsigned)_countof(r), c, (unsigned)_countof(c), v, (unsigned)_countof(v)) >= 2)
        {
            sheet.rows.push_back({
                std::to_wstring(idx++),
                PathUtils::Utf8ToWide(r),
                PathUtils::Utf8ToWide(c),
                PathUtils::Utf8ToWide(v)
            });
        }
    }

    sheet.maxCols = 4;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
