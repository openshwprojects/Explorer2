#pragma once
// CueSheet.h — CDRWIN Audio Disc CUE Sheet (.cue) Tracklist & Index Table Parser

namespace Formats {

inline bool ParseCueSheetDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("TRACK ") == std::string_view::npos && sv.find("FILE ") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"CDRWIN Audio Disc Sheet (.cue)";

    SpreadsheetSheet sheet;
    sheet.name = L"CUE_Tracks";
    sheet.rows.push_back({ L"Track #", L"Mode", L"Title", L"Performer", L"Index 01 Time", L"Source File" });

    std::wstring curFile, curTrack, curMode, curTitle, curPerf, curIdx;
    auto flushTrack = [&]() {
        if (!curTrack.empty()) {
            sheet.rows.push_back({ curTrack, curMode, curTitle, curPerf, curIdx, curFile });
            curTrack.clear(); curMode.clear(); curTitle.clear(); curIdx.clear();
        }
    };
    auto getQuoted = [](std::string_view ln) -> std::wstring {
        size_t q1 = ln.find('"');
        size_t q2 = (q1 != std::string_view::npos) ? ln.find('"', q1 + 1) : std::string_view::npos;
        if (q1 != std::string_view::npos && q2 != std::string_view::npos) {
            return PathUtils::Utf8ToWide(ln.substr(q1 + 1, q2 - q1 - 1));
        }
        return L"";
    };

    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln.rfind("FILE ", 0) == 0) {
            curFile = getQuoted(ln);
        } else if (ln.rfind("TRACK ", 0) == 0) {
            flushTrack();
            char trk[32] = {}, mode[32] = {};
            if (sscanf_s(std::string(ln.substr(6)).c_str(), "%31s %31s", trk, (unsigned)_countof(trk), mode, (unsigned)_countof(mode)) >= 1) {
                curTrack = PathUtils::Utf8ToWide(trk);
                curMode  = PathUtils::Utf8ToWide(mode);
            }
        } else if (ln.rfind("TITLE ", 0) == 0) {
            curTitle = getQuoted(ln);
        } else if (ln.rfind("PERFORMER ", 0) == 0) {
            curPerf = getQuoted(ln);
        } else if (ln.rfind("INDEX 01 ", 0) == 0) {
            curIdx = PathUtils::Utf8ToWide(ln.substr(9));
        }
    }
    flushTrack();

    sheet.maxCols = 6;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
