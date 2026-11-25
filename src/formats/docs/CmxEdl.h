#pragma once
// CmxEdl.h — CMX 3600 Edit Decision List (.edl) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

// ============================================================================
// 7. NLE Video Timelines (.edl, .fcpxml, .otio)
// ============================================================================
inline bool ParseCmxEdl(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::wstring edlTitle, fcmMode;

    struct EdlEvent {
        std::wstring num, reel, chan, trans, srcIn, srcOut, recIn, recOut, clipName;
    };
    std::vector<EdlEvent> events;

    auto isTimecode = [](std::string_view t) {
        return t.size() == 11 &&
               (t[2] == ':' || t[2] == ';') && (t[5] == ':' || t[5] == ';') && (t[8] == ':' || t[8] == ';');
    };

    size_t ls = 0;
    while (ls < sv.size()) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty()) continue;

        if (ln.size() >= 6 && PathUtils::ToLowerAscii(ln.substr(0, 6)) == "title:") {
            edlTitle = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(6)));
            continue;
        }
        if (ln.size() >= 4 && PathUtils::ToLowerAscii(ln.substr(0, 4)) == "fcm:") {
            fcmMode = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(4)));
            continue;
        }
        if (ln[0] == '*' && !events.empty()) {
            std::string low = PathUtils::ToLowerAscii(ln);
            size_t cn = low.find("clip name:");
            if (cn != std::string::npos) {
                events.back().clipName = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(cn + 10)));
            }
            continue;
        }

        // Tokenize potential CMX 3600 event row
        std::vector<std::string_view> toks;
        size_t i = 0;
        while (i < ln.size()) {
            while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
            size_t s = i;
            while (i < ln.size() && ln[i] != ' ' && ln[i] != '\t') ++i;
            if (i > s) toks.push_back(ln.substr(s, i - s));
        }
        if (toks.size() >= 8 && std::isdigit(static_cast<unsigned char>(toks[0][0]))) {
            size_t tcIdx = toks.size() - 4;
            if (isTimecode(toks[tcIdx]) && isTimecode(toks[tcIdx + 1]) &&
                isTimecode(toks[tcIdx + 2]) && isTimecode(toks[tcIdx + 3]))
            {
                EdlEvent ev;
                ev.num    = PathUtils::Utf8ToWide(toks[0]);
                ev.reel   = PathUtils::Utf8ToWide(toks[1]);
                ev.chan   = PathUtils::Utf8ToWide(toks[2]);
                ev.trans  = PathUtils::Utf8ToWide(toks[3]);
                ev.srcIn  = PathUtils::Utf8ToWide(toks[tcIdx]);
                ev.srcOut = PathUtils::Utf8ToWide(toks[tcIdx + 1]);
                ev.recIn  = PathUtils::Utf8ToWide(toks[tcIdx + 2]);
                ev.recOut = PathUtils::Utf8ToWide(toks[tcIdx + 3]);
                events.push_back(std::move(ev));
            }
        }
    }

    if (events.empty() && edlTitle.empty()) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"CMX 3600 Edit Decision List (.edl)";

    SpreadsheetSheet sh;
    sh.name = L"EDL_Events";
    sh.rows.push_back({ L"Event #", L"Reel", L"Channel", L"Trans", L"Src In", L"Src Out", L"Rec In", L"Rec Out", L"Clip Name" });
    if (!edlTitle.empty() || !fcmMode.empty()) {
        sh.rows.push_back({ L"TITLE", edlTitle, fcmMode, L"", L"", L"", L"", L"", L"" });
    }
    for (const auto& ev : events) {
        sh.rows.push_back({ ev.num, ev.reel, ev.chan, ev.trans, ev.srcIn, ev.srcOut, ev.recIn, ev.recOut, ev.clipName });
    }
    FinalizeSheetCols(sh);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

} // namespace DetailMediaAndScoreDoc

inline bool ParseCmxEdlDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseCmxEdl(raw, out); }

} // namespace Formats
