#pragma once
// SubViewerSub.h — SubViewer Timestamped Text Subtitle (.sub) Reader

namespace Formats {
namespace DetailMedia100 {

inline bool ParseSubViewerSub(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // SubViewer HH:MM:SS.cc,HH:MM:SS.cc
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    SpreadsheetSheet sh;
    sh.name = L"Subtitles";
    sh.rows.push_back({ L"#", L"Start", L"End", L"Dialogue Text" });

    size_t ls = 0;
    size_t cueIdx = 0;
    std::wstring pendingStart, pendingEnd;
    while (ls < sv.size() && sh.rows.size() < 4096) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty()) continue;

        // SubViewer timestamp line: 00:01:23.45,00:01:25.67
        if (ln.size() >= 23 && ln[2] == ':' && ln[5] == ':' && ln[11] == ',' && ln[14] == ':' && ln[17] == ':') {
            pendingStart = PathUtils::Utf8ToWide(ln.substr(0, 11));
            pendingEnd   = PathUtils::Utf8ToWide(ln.substr(12, 11));
            continue;
        }
        if (!pendingStart.empty()) {
            std::string txt(ln);
            size_t br = 0;
            while ((br = txt.find("[br]", br)) != std::string::npos) { txt.replace(br, 4, " "); }
            sh.rows.push_back({ std::to_wstring(++cueIdx), pendingStart, pendingEnd, PathUtils::Utf8ToWide(txt) });
            pendingStart.clear();
            pendingEnd.clear();
        }
    }

    if (cueIdx == 0) return false;
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SubViewer Subtitle (.sub)";
    FinalizeSheetCols(sh);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseSubViewerSubDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    return DetailMedia100::ParseSubViewerSub(raw, out);
}

} // namespace Formats
