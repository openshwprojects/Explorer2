#pragma once
// RisCitation.h — Research Information Systems (.ris) Bibliographic Citation Format Parser

namespace Formats {

inline bool ParseRisCitationDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("TY  -") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"RIS Bibliographic Citations (.ris)";

    SpreadsheetSheet sheet;
    sheet.name = L"RIS_Citations";
    sheet.rows.push_back({ L"Type (TY)", L"Title (TI/T1)", L"Authors (AU)", L"Year (PY/Y1)", L"Journal / Source (JO)" });

    std::wstring ty, ti, au, py, jo;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.size() < 5 || ln.substr(2, 3) != "  -") continue;

        std::string_view tag = ln.substr(0, 2);
        std::string_view val = (ln.size() > 6) ? ln.substr(6) : std::string_view{};
        std::wstring wval = PathUtils::Utf8ToWide(val);

        if (tag == "TY") {
            ty = wval; ti.clear(); au.clear(); py.clear(); jo.clear();
        } else if (tag == "TI" || tag == "T1") {
            ti = wval;
        } else if (tag == "AU" || tag == "A1") {
            if (!au.empty()) au += L"; ";
            au += wval;
        } else if (tag == "PY" || tag == "Y1") {
            py = wval;
        } else if (tag == "JO" || tag == "JF" || tag == "T2") {
            jo = wval;
        } else if (tag == "ER") {
            sheet.rows.push_back({ ty, ti, au, py, jo });
            ty.clear();
        }
    }
    if (!ty.empty()) {
        sheet.rows.push_back({ ty, ti, au, py, jo });
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
