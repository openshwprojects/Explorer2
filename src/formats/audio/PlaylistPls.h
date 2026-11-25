#pragma once
// PlaylistPls.h — Winamp / SHOUTcast PLS Media Playlist ([playlist] File<n>= / Title<n>= / Length<n>=) Reader

namespace Formats {

inline bool ParsePlaylistPlsDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF &&
        static_cast<uint8_t>(sv[1]) == 0xBB && static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }

    std::vector<std::string_view> lines;
    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = PathUtils::TrimAscii(sv.substr(pos, eol - pos));
        if (!ln.empty() && ln[0] != ';') lines.push_back(ln);
        pos = eol + 1;
    }
    if (lines.empty() || lines[0].size() < 10 || _strnicmp(lines[0].data(), "[playlist]", 10) != 0) {
        return false;
    }

    auto formatDuration = [](double sec) -> std::wstring {
        if (!(sec >= 0.0)) return L"";
        wchar_t buf[48] = {};
        long long total = static_cast<long long>(sec + 0.5);
        if (total >= 3600) swprintf_s(buf, L"%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
        else swprintf_s(buf, L"%lld:%02lld", total / 60, total % 60);
        return buf;
    };

    std::map<int, std::array<std::string, 3>> items; // n -> File, Title, Length
    std::string declared;
    std::string plsVersion;
    for (size_t i = 1; i < lines.size(); ++i) {
        std::string_view ln = lines[i];
        size_t eq = ln.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key(PathUtils::TrimAscii(ln.substr(0, eq)));
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string val(PathUtils::TrimAscii(ln.substr(eq + 1)));
        int slot = -1;
        size_t digits = 0;
        if (key.rfind("file", 0) == 0) { slot = 0; digits = 4; }
        else if (key.rfind("title", 0) == 0) { slot = 1; digits = 5; }
        else if (key.rfind("length", 0) == 0) { slot = 2; digits = 6; }
        else if (key == "numberofentries") { declared = val; continue; }
        else if (key == "version") { plsVersion = val; continue; }
        if (slot < 0 || digits >= key.size()) continue;
        int n = 0;
        auto res = std::from_chars(key.data() + digits, key.data() + key.size(), n);
        if (res.ec != std::errc{} || res.ptr != key.data() + key.size() || n <= 0) continue;
        items[n][static_cast<size_t>(slot)] = std::move(val);
    }
    if (items.empty()) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"Playlist_Tracks";
    sheet.rows.push_back({ L"#", L"Duration", L"Title", L"URI / Path" });

    SpreadsheetSheet summary;
    summary.name = L"Playlist_Summary";
    summary.rows.push_back({ L"Property", L"Value" });

    double totalSec = 0.0;
    size_t entries = 0;
    bool unknownDurations = false;
    constexpr size_t kMaxRows = 20000;

    for (const auto& kv : items) {
        if (kv.second[0].empty()) continue;
        ++entries;
        double len = -1.0;
        if (!kv.second[2].empty()) {
            std::from_chars(kv.second[2].data(), kv.second[2].data() + kv.second[2].size(), len);
        }
        if (len >= 0.0) totalSec += len;
        else unknownDurations = true;
        if (sheet.rows.size() <= kMaxRows) {
            sheet.rows.push_back({
                std::to_wstring(kv.first),
                len >= 0.0 ? formatDuration(len) : L"(stream)",
                PathUtils::Utf8ToWide(kv.second[1]),
                PathUtils::Utf8ToWide(kv.second[0])
            });
        }
    }
    if (entries == 0) return false;

    if (!declared.empty()) summary.rows.push_back({ L"Declared entries", PathUtils::Utf8ToWide(declared) });
    if (!plsVersion.empty()) summary.rows.push_back({ L"PLS version", PathUtils::Utf8ToWide(plsVersion) });
    summary.rows.push_back({ L"Entries", std::to_wstring(entries) });
    if (totalSec > 0.0) {
        summary.rows.push_back({ L"Total duration", formatDuration(totalSec) + (unknownDurations ? L" (+ entries of unknown length)" : L"") });
    }

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"PLS Media Playlist (.pls)";
    sheet.maxCols = sheet.rows[0].size();
    summary.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
