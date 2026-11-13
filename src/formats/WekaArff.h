#pragma once
// WekaArff.h — Weka Attribute-Relation File Format (.arff) Machine Learning Dataset Parser

namespace Formats {

inline bool ParseWekaArffDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("@relation") == std::string_view::npos && sv.find("@RELATION") == std::string_view::npos &&
        sv.find("@attribute") == std::string_view::npos && sv.find("@ATTRIBUTE") == std::string_view::npos) {
        return false;
    }

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Weka ML Attribute-Relation Dataset (.arff)";

    SpreadsheetSheet sheet;
    sheet.name = L"ARFF_Data";
    std::vector<std::wstring> headers;
    bool inData = false;

    size_t pos = 0;
    while (pos < sv.size() && sheet.rows.size() < 5000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '%') continue;

        if (!inData) {
            if (ln.rfind("@attribute", 0) == 0 || ln.rfind("@ATTRIBUTE", 0) == 0) {
                char aname[64] = {};
                if (sscanf_s(std::string(ln.substr(10)).c_str(), "%63s", aname, (unsigned)_countof(aname)) == 1) {
                    headers.push_back(PathUtils::Utf8ToWide(aname));
                }
            } else if (ln.rfind("@data", 0) == 0 || ln.rfind("@DATA", 0) == 0) {
                inData = true;
                if (!headers.empty()) sheet.rows.push_back(headers);
            }
        } else {
            std::vector<std::wstring> row;
            size_t p = 0;
            while (p < ln.size()) {
                size_t c = ln.find(',', p);
                if (c == std::string_view::npos) c = ln.size();
                row.push_back(PathUtils::Utf8ToWide(ln.substr(p, c - p)));
                p = c + 1;
            }
            sheet.maxCols = std::max(sheet.maxCols, row.size());
            sheet.rows.push_back(std::move(row));
        }
    }

    sheet.maxCols = std::max(sheet.maxCols, headers.size());
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
