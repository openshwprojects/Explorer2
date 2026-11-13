#pragma once
// PlaylistM3u.h — Extended M3U / HLS / PLS Media Playlist (.m3u / .m3u8 / .pls) Track Table Parser

namespace Formats {

inline bool ParsePlaylistM3uDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.empty()) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Media Playlist (.m3u / .m3u8 / .pls)";

    SpreadsheetSheet sheet;
    sheet.name = L"Playlist_Tracks";
    sheet.rows.push_back({ L"#", L"Duration (s)", L"Title / Stream Info", L"URI / Path" });

    std::wstring pendingDur = L"-", pendingTitle = L"-";
    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("#EXTINF:", 0) == 0) {
            std::string_view rest = ln.substr(8);
            size_t comma = rest.find(',');
            if (comma != std::string_view::npos) {
                pendingDur   = PathUtils::Utf8ToWide(rest.substr(0, comma));
                pendingTitle = PathUtils::Utf8ToWide(rest.substr(comma + 1));
            } else {
                pendingDur = PathUtils::Utf8ToWide(rest);
            }
        } else if (ln[0] == '#') {
            continue;
        } else if (ln.rfind("[playlist]", 0) == 0 || ln.rfind("NumberOfEntries", 0) == 0 || ln.rfind("Version=", 0) == 0) {
            continue;
        } else if (ln.rfind("File", 0) == 0 && ln.find('=') != std::string_view::npos) {
            size_t eq = ln.find('=');
            sheet.rows.push_back({ std::to_wstring(idx++), pendingDur, pendingTitle, PathUtils::Utf8ToWide(ln.substr(eq + 1)) });
            pendingDur = L"-"; pendingTitle = L"-";
        } else {
            sheet.rows.push_back({ std::to_wstring(idx++), pendingDur, pendingTitle, PathUtils::Utf8ToWide(ln) });
            pendingDur = L"-"; pendingTitle = L"-";
        }
    }

    sheet.maxCols = 4;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
