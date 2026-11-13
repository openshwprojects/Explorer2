#pragma once
// Ass.h — Advanced SubStation Alpha Subtitles (.ass / .ssa) Parser

namespace Formats {

inline bool ParseSubStationAss(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("[Script Info]") == std::string_view::npos && sv.find("Dialogue:") == std::string_view::npos) {
        return false;
    }

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"SubStation Alpha Subtitles (.ass)";

    SpreadsheetSheet sheet;
    sheet.name = L"Events_ASS";
    sheet.rows.push_back({ L"Start", L"End", L"Style", L"Actor", L"Dialogue Text" });

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.rfind("Dialogue:", 0) == 0) {
            std::string_view body = ln.substr(9);
            while (!body.empty() && body.front() == ' ') body.remove_prefix(1);
            std::vector<std::string_view> parts;
            size_t p = 0;
            for (int comma = 0; comma < 9; ++comma) {
                size_t c = body.find(',', p);
                if (c == std::string_view::npos) break;
                parts.push_back(body.substr(p, c - p));
                p = c + 1;
            }
            if (parts.size() >= 9 && p <= body.size()) {
                std::string_view txt = body.substr(p);
                sheet.rows.push_back({
                    PathUtils::Utf8ToWide(parts[1]),
                    PathUtils::Utf8ToWide(parts[2]),
                    PathUtils::Utf8ToWide(parts[3]),
                    PathUtils::Utf8ToWide(parts[4]),
                    PathUtils::Utf8ToWide(txt)
                });
            }
        }
    }
    sheet.maxCols = 5;
    out.sheets.push_back(std::move(sheet));
    out.valid = (out.sheets[0].rows.size() >= 2);
    return out.valid;
}

} // namespace Formats
