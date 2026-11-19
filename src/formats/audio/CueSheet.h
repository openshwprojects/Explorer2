#pragma once
// CueSheet.h — CDRWIN cue sheets (.cue): disc-level TITLE / PERFORMER / CATALOG / REM fields, FILE blocks,
// TRACK entries with TITLE, PERFORMER, ISRC, pregap (INDEX 00) and start (INDEX 01). Track lengths come from
// consecutive INDEX 01 positions (mm:ss:ff, 75 frames per second) inside the same file. Legacy 8-bit
// encodings decode through PathUtils::Utf8ToWide's ANSI fallback.

namespace Formats {

namespace DetailCue {

inline std::string_view TrimSv(std::string_view s) { return PathUtils::TrimAscii(s); }

// Argument after the command word: "quoted text" or the next bare word
inline std::string Arg(std::string_view rest) {
    rest = TrimSv(rest);
    if (!rest.empty() && rest.front() == '"') {
        size_t q = rest.find('"', 1);
        return std::string(rest.substr(1, (q == std::string_view::npos ? rest.size() : q) - 1));
    }
    return std::string(rest);
}

// mm:ss:ff -> frames (75 per second)
inline bool ParseMsf(std::string_view s, int64_t& frames) {
    s = TrimSv(s);
    int v[3] = {};
    int n = 0;
    size_t i = 0;
    while (n < 3 && i < s.size()) {
        size_t st = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i == st || i - st > 4) return false;
        std::from_chars(s.data() + st, s.data() + i, v[n++]);
        if (n < 3) {
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
        }
    }
    if (n != 3 || i != s.size() || v[1] > 59 || v[2] > 74) return false;
    frames = (static_cast<int64_t>(v[0]) * 60 + v[1]) * 75 + v[2];
    return true;
}

// frames -> mm:ss:ff, the notation cue sheets use
inline std::wstring FormatFrames(int64_t f) {
    wchar_t buf[32];
    swprintf_s(buf, L"%02lld:%02lld:%02lld", f / (75 * 60), (f / 75) % 60, f % 75);
    return buf;
}

} // namespace DetailCue

inline bool ParseCueSheetDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailCue;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF && static_cast<uint8_t>(sv[1]) == 0xBB &&
        static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };

    struct Track {
        std::string number, mode, title, performer, isrc, file;
        int64_t index0 = -1, index1 = -1;
    };
    std::vector<Track> tracks;
    std::vector<std::pair<std::wstring, std::wstring>> disc;
    std::string curFile, discPerformer;
    size_t files = 0;

    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = TrimSv(sv.substr(pos, eol - pos));
        pos = eol + 1;
        if (ln.empty()) continue;
        size_t sp = ln.find_first_of(" \t");
        std::string cmd(ln.substr(0, sp));
        for (char& c : cmd) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string_view rest = (sp == std::string_view::npos) ? std::string_view{} : ln.substr(sp + 1);
        Track* cur = tracks.empty() ? nullptr : &tracks.back();

        if (cmd == "FILE") {
            curFile = Arg(rest);
            ++files;
        } else if (cmd == "TRACK") {
            Track t;
            std::string_view r = TrimSv(rest);
            size_t s2 = r.find_first_of(" \t");
            t.number = std::string(r.substr(0, s2));
            if (s2 != std::string_view::npos) t.mode = std::string(TrimSv(r.substr(s2 + 1)));
            t.file = curFile;
            t.performer = discPerformer;
            if (tracks.size() < 200) tracks.push_back(std::move(t));
        } else if (cmd == "TITLE") {
            if (cur) cur->title = Arg(rest); else disc.emplace_back(L"Title", w(Arg(rest)));
        } else if (cmd == "PERFORMER") {
            if (cur) cur->performer = Arg(rest);
            else { discPerformer = Arg(rest); disc.emplace_back(L"Performer", w(discPerformer)); }
        } else if (cmd == "SONGWRITER" && !cur) {
            disc.emplace_back(L"Songwriter", w(Arg(rest)));
        } else if (cmd == "CATALOG") {
            disc.emplace_back(L"Catalog (UPC/EAN)", w(Arg(rest)));
        } else if (cmd == "ISRC" && cur) {
            cur->isrc = Arg(rest);
        } else if (cmd == "INDEX" && cur) {
            std::string_view r = TrimSv(rest);
            size_t s2 = r.find_first_of(" \t");
            if (s2 == std::string_view::npos) continue;
            int idx = -1;
            std::from_chars(r.data(), r.data() + s2, idx);
            int64_t f = 0;
            if (!ParseMsf(r.substr(s2 + 1), f)) continue;
            if (idx == 0) cur->index0 = f;
            else if (idx == 1) cur->index1 = f;
        } else if (cmd == "REM" && !cur) {
            std::string_view r = TrimSv(rest);
            size_t s2 = r.find_first_of(" \t");
            std::string key(r.substr(0, s2));
            std::string val = (s2 == std::string_view::npos) ? std::string() : Arg(r.substr(s2 + 1));
            if (!key.empty() && disc.size() < 32) disc.emplace_back(L"REM " + w(key), w(val));
        }
    }
    if (tracks.empty()) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"CUE_Tracks";
    sheet.rows.push_back({ L"Track #", L"Mode", L"Title", L"Performer", L"Index 01 Time", L"Length", L"Pregap",
                           L"Source File", L"ISRC" });
    for (size_t i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        std::wstring len;
        if (t.index1 >= 0 && i + 1 < tracks.size() && tracks[i + 1].file == t.file) {
            int64_t next = tracks[i + 1].index0 >= 0 ? tracks[i + 1].index0 : tracks[i + 1].index1;
            if (next > t.index1) len = FormatFrames(next - t.index1);
        }
        std::wstring pregap;
        if (t.index0 >= 0 && t.index1 > t.index0) pregap = FormatFrames(t.index1 - t.index0);
        sheet.rows.push_back({ w(t.number), w(t.mode), w(t.title), w(t.performer),
                               t.index1 >= 0 ? FormatFrames(t.index1) : std::wstring(), len, pregap, w(t.file), w(t.isrc) });
    }
    sheet.maxCols = 9;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"CDRWIN Audio Disc Sheet (.cue)";
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"CUE_Disc";
    summary.rows.push_back({ L"Property", L"Value" });
    for (auto& kv : disc) summary.rows.push_back({ kv.first, kv.second });
    summary.rows.push_back({ L"Tracks", std::to_wstring(tracks.size()) });
    summary.rows.push_back({ L"Files", std::to_wstring(files) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
