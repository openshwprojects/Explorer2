#pragma once
// MicroDvdSub.h — MicroDVD Frame-Based Text Subtitle (.sub) Reader

namespace Formats {
namespace DetailMedia100 {

inline bool ParseMicroDvdSub(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // MicroDVD {startFrame}{endFrame}Text
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    SpreadsheetSheet sh;
    sh.name = L"Subtitles";
    sh.rows.push_back({ L"#", L"Start", L"End", L"Dialogue Text" });

    size_t ls = 0;
    size_t cueIdx = 0;
    while (ls < sv.size() && sh.rows.size() < 4096) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty()) continue;

        if (ln[0] == '{') {
            size_t b1 = ln.find('}', 1);
            if (b1 != std::string_view::npos && b1 + 1 < ln.size() && ln[b1 + 1] == '{') {
                size_t b2 = ln.find('}', b1 + 2);
                if (b2 != std::string_view::npos) {
                    bool okDigits = (b1 > 1 && b2 > b1 + 2);
                    for (size_t k = 1; k < b1 && okDigits; ++k) if (!std::isdigit(static_cast<unsigned char>(ln[k]))) okDigits = false;
                    for (size_t k = b1 + 2; k < b2 && okDigits; ++k) if (!std::isdigit(static_cast<unsigned char>(ln[k]))) okDigits = false;
                    if (okDigits) {
                        std::string txt(ln.substr(b2 + 1));
                        for (size_t k = 0; k < txt.size(); ++k) if (txt[k] == '|') txt[k] = ' ';
                        sh.rows.push_back({
                            std::to_wstring(++cueIdx),
                            PathUtils::Utf8ToWide(ln.substr(1, b1 - 1)),
                            PathUtils::Utf8ToWide(ln.substr(b1 + 2, b2 - b1 - 2)),
                            PathUtils::Utf8ToWide(txt)
                        });
                        continue;
                    }
                }
            }
        }
    }

    if (cueIdx == 0) return false;
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MicroDVD Subtitle (.sub)";
    FinalizeSheetCols(sh);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseMicroDvdSubDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailMedia100::ParseMicroDvdSub(raw, out);
}

} // namespace Formats
