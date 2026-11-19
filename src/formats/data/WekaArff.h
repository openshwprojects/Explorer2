#pragma once
// WekaArff.h — Weka Attribute-Relation File Format (.arff): case-insensitive @relation / @attribute / @data,
// quoted names and values (with commas inside), NUMERIC / STRING / DATE / nominal {a,b,c} types, sparse
// instances {index value, ...}, missing values "?", and % comments.

namespace Formats {

namespace DetailArff {

inline bool StartsWithCI(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

// Next token: 'quoted' / "quoted" (backslash escapes) or a bare word ending at whitespace / sep
inline bool NextToken(std::string_view s, size_t& i, std::string& out, char sep) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= s.size()) return false;
    out.clear();
    if (s[i] == '\'' || s[i] == '"') {
        char q = s[i++];
        while (i < s.size() && s[i] != q) {
            if (s[i] == '\\' && i + 1 < s.size()) ++i;
            out.push_back(s[i++]);
        }
        if (i < s.size()) ++i;
        return true;
    }
    while (i < s.size() && s[i] != sep && s[i] != ' ' && s[i] != '\t' && !(sep == ',' && (s[i] == '{' || s[i] == '}'))) {
        out.push_back(s[i++]);
    }
    return true;
}

// Comma-separated values of one data row (quotes respected)
inline std::vector<std::string> SplitRow(std::string_view s) {
    std::vector<std::string> vals;
    size_t i = 0;
    while (i <= s.size()) {
        std::string v;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i < s.size() && (s[i] == '\'' || s[i] == '"')) {
            NextToken(s, i, v, ',');
        } else {
            size_t st = i;
            while (i < s.size() && s[i] != ',') ++i;
            v = std::string(s.substr(st, i - st));
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
        }
        vals.push_back(std::move(v));
        while (i < s.size() && s[i] != ',') ++i;
        if (i >= s.size()) break;
        ++i;
    }
    return vals;
}

} // namespace DetailArff

inline bool ParseWekaArffDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailArff;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::string relation;
    std::vector<std::string> names, types;
    bool inData = false;
    std::vector<std::vector<std::string>> rows;
    size_t sparseRows = 0, missing = 0, dataRows = 0;

    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        if (ln.empty() || ln[0] == '%') continue;

        if (!inData) {
            if (StartsWithCI(ln, "@relation")) {
                size_t i = 9;
                NextToken(ln, i, relation, ' ');
            } else if (StartsWithCI(ln, "@attribute")) {
                size_t i = 10;
                std::string name;
                if (!NextToken(ln, i, name, ' ')) continue;
                while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
                names.push_back(name);
                types.push_back(std::string(ln.substr(i)));
            } else if (StartsWithCI(ln, "@data")) {
                inData = true;
            } else if (ln[0] != '@') {
                return false; // not an ARFF header
            }
            continue;
        }
        ++dataRows;
        if (rows.size() >= 20000) continue;
        std::vector<std::string> row(names.size());
        if (ln[0] == '{') {
            // Sparse: {index value, index value}; omitted attributes are 0
            ++sparseRows;
            for (auto& v : row) v = "0";
            std::string_view inner = ln.substr(1, (ln.back() == '}' ? ln.size() - 2 : ln.size() - 1));
            for (const auto& pair : SplitRow(inner)) {
                std::string_view pv(pair);
                size_t sp = pv.find_first_of(" \t");
                if (sp == std::string_view::npos) continue;
                size_t idx = 0;
                auto res = std::from_chars(pv.data(), pv.data() + sp, idx);
                if (res.ec != std::errc{} || idx >= row.size()) continue;
                size_t i = sp;
                std::string val;
                NextToken(pv, i, val, ',');
                row[idx] = val;
            }
        } else {
            std::vector<std::string> vals = SplitRow(ln);
            for (size_t c = 0; c < vals.size(); ++c) {
                if (c < row.size()) row[c] = vals[c];
                else row.push_back(vals[c]);
            }
        }
        for (const auto& v : row) if (v == "?") ++missing;
        rows.push_back(std::move(row));
    }
    if (!inData || names.empty()) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Weka ML Attribute-Relation Dataset (.arff)";
    SpreadsheetSheet sheet;
    sheet.name = L"ARFF_Data";
    std::vector<std::wstring> header;
    for (const auto& n : names) header.push_back(PathUtils::Utf8ToWide(n));
    sheet.rows.push_back(header);
    size_t maxCols = header.size();
    for (const auto& r : rows) {
        std::vector<std::wstring> wr;
        wr.reserve(r.size());
        for (const auto& v : r) wr.push_back(PathUtils::Utf8ToWide(v));
        maxCols = std::max(maxCols, wr.size());
        sheet.rows.push_back(std::move(wr));
    }
    sheet.maxCols = maxCols;
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet attrs;
    attrs.name = L"ARFF_Attributes";
    attrs.rows.push_back({ L"#", L"Attribute", L"Type" });
    for (size_t i = 0; i < names.size(); ++i) {
        attrs.rows.push_back({ std::to_wstring(i + 1), PathUtils::Utf8ToWide(names[i]), PathUtils::Utf8ToWide(types[i]) });
    }
    attrs.rows.push_back({ L"", L"Relation", PathUtils::Utf8ToWide(relation) });
    attrs.rows.push_back({ L"", L"Instances", std::to_wstring(dataRows) + (sparseRows ? L" (" + std::to_wstring(sparseRows) + L" sparse)" : L"") });
    if (missing) attrs.rows.push_back({ L"", L"Missing values (?)", std::to_wstring(missing) });
    attrs.maxCols = 3;
    out.sheets.push_back(std::move(attrs));
    out.valid = true;
    return true;
}

} // namespace Formats
