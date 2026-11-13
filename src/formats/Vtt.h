#pragma once
// Vtt.h — WebVTT Video Subtitles (.vtt) Parser

namespace Formats {

inline bool ParseWebVtt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("WEBVTT") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"WebVTT Video Subtitles (.vtt)";

    SpreadsheetSheet sheet;
    sheet.name = L"Subtitles_VTT";
    sheet.rows.push_back({ L"Cue", L"Start Time", L"End Time", L"Caption Text" });

    int cueNum = 1;
    std::wstring curStart, curEnd, curText;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.rfind("WEBVTT", 0) == 0) continue;

        if (ln.empty()) {
            if (!curStart.empty() && !curText.empty()) {
                sheet.rows.push_back({ std::to_wstring(cueNum++), curStart, curEnd, curText });
                curStart.clear(); curEnd.clear(); curText.clear();
            }
            continue;
        }
        size_t arrow = ln.find("-->");
        if (arrow != std::string_view::npos) {
            std::string_view s0 = ln.substr(0, arrow);
            std::string_view s1 = ln.substr(arrow + 3);
            while (!s0.empty() && s0.back() == ' ') s0.remove_suffix(1);
            while (!s1.empty() && s1.front() == ' ') s1.remove_prefix(1);
            curStart = PathUtils::Utf8ToWide(s0);
            curEnd = PathUtils::Utf8ToWide(s1);
        } else if (!curStart.empty()) {
            if (!curText.empty()) curText += L" ";
            curText += PathUtils::Utf8ToWide(ln);
        }
    }
    if (!curStart.empty() && !curText.empty()) {
        sheet.rows.push_back({ std::to_wstring(cueNum++), curStart, curEnd, curText });
    }
    sheet.maxCols = 4;
    out.sheets.push_back(std::move(sheet));
    out.valid = (out.sheets[0].rows.size() >= 2);
    return out.valid;
}

} // namespace Formats
