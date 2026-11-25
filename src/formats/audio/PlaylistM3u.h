#pragma once
// PlaylistM3u.h — media playlists: extended M3U / M3U8 (#EXTINF with IPTV-style attributes) and HTTP Live
// Streaming (master playlists with #EXT-X-STREAM-INF variants, media playlists with segments).

namespace Formats {

namespace DetailPlaylist {

inline std::string_view TrimSv(std::string_view s) { return PathUtils::TrimAscii(s); }

// KEY=VALUE,KEY="quoted, value" attribute lists (#EXT-X-STREAM-INF, #EXTINF IPTV attributes)
inline std::vector<std::pair<std::string, std::string>> ParseAttrList(std::string_view s, char sep) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == sep)) ++i;
        size_t ks = i;
        while (i < s.size() && s[i] != '=' && s[i] != sep && s[i] != ' ') ++i;
        std::string key(s.substr(ks, i - ks));
        if (i >= s.size() || s[i] != '=') {
            if (!key.empty()) out.emplace_back(key, std::string());
            continue;
        }
        ++i;
        std::string val;
        if (i < s.size() && s[i] == '"') {
            size_t q = s.find('"', i + 1);
            if (q == std::string_view::npos) q = s.size();
            val = std::string(s.substr(i + 1, q - i - 1));
            i = (q < s.size()) ? q + 1 : q;
        } else {
            size_t vs = i;
            while (i < s.size() && s[i] != sep && s[i] != ' ') ++i;
            val = std::string(s.substr(vs, i - vs));
        }
        if (!key.empty()) out.emplace_back(std::move(key), std::move(val));
    }
    return out;
}

inline std::wstring FormatDuration(double sec) {
    if (!(sec >= 0.0)) return L"";
    wchar_t buf[48];
    long long total = static_cast<long long>(sec + 0.5);
    if (total >= 3600) swprintf_s(buf, L"%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
    else swprintf_s(buf, L"%lld:%02lld", total / 60, total % 60);
    return buf;
}

} // namespace DetailPlaylist

inline bool ParsePlaylistM3uDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailPlaylist;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF && static_cast<uint8_t>(sv[1]) == 0xBB &&
        static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    std::vector<std::string_view> lines;
    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = TrimSv(sv.substr(pos, eol - pos));
        if (!ln.empty()) lines.push_back(ln);
        pos = eol + 1;
    }
    if (lines.empty()) return false;
    auto w = [](std::string_view s) { return PathUtils::Utf8ToWide(s); };

    SpreadsheetSheet sheet;
    sheet.name = L"Playlist_Tracks";
    SpreadsheetSheet summary;
    summary.name = L"Playlist_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    double totalSec = 0.0;
    size_t entries = 0;
    bool unknownDurations = false;
    constexpr size_t kMaxRows = 20000;

    if (lines[0].size() >= 10 && _strnicmp(lines[0].data(), "[playlist]", 10) == 0) {
        return false; // PLS is handled by PlaylistPls.h
    }
    {
        // --- M3U / M3U8 / HLS ---
        bool extended = (lines[0].rfind("#EXTM3U", 0) == 0);
        bool hlsMaster = false, hlsMedia = false;
        for (auto ln : lines) {
            if (ln.rfind("#EXT-X-STREAM-INF", 0) == 0) hlsMaster = true;
            if (ln.rfind("#EXT-X-TARGETDURATION", 0) == 0 || ln.rfind("#EXT-X-MEDIA-SEQUENCE", 0) == 0) hlsMedia = true;
        }
        if (hlsMaster) {
            sheet.rows.push_back({ L"#", L"Bandwidth (bit/s)", L"Resolution", L"Codecs", L"URI" });
        } else {
            sheet.rows.push_back({ L"#", L"Duration", L"Title", L"URI / Path", L"Attributes" });
        }
        std::string pendingTitle, pendingAttrs, pendingStreamInf;
        double pendingDur = -1.0;
        bool havePending = false;
        size_t nonUriText = 0;
        for (size_t i = (extended ? 1 : 0); i < lines.size(); ++i) {
            std::string_view ln = lines[i];
            if (ln.rfind("#EXTINF:", 0) == 0) {
                // #EXTINF:<duration> [attr="v" ...],<title>
                std::string_view rest = ln.substr(8);
                size_t comma = std::string_view::npos;
                bool inQuote = false;
                for (size_t k = 0; k < rest.size(); ++k) {
                    if (rest[k] == '"') inQuote = !inQuote;
                    else if (rest[k] == ',' && !inQuote) { comma = k; break; }
                }
                std::string_view head = (comma == std::string_view::npos) ? rest : rest.substr(0, comma);
                pendingTitle = (comma == std::string_view::npos) ? std::string() : std::string(TrimSv(rest.substr(comma + 1)));
                size_t sp = head.find_first_of(" \t");
                std::string_view durS = TrimSv(head.substr(0, sp));
                pendingDur = -1.0;
                std::from_chars(durS.data(), durS.data() + durS.size(), pendingDur);
                pendingAttrs.clear();
                if (sp != std::string_view::npos) {
                    for (const auto& kv : ParseAttrList(head.substr(sp + 1), ' ')) {
                        if (!pendingAttrs.empty()) pendingAttrs += "; ";
                        pendingAttrs += kv.first + "=" + kv.second;
                    }
                }
                havePending = true;
                continue;
            }
            if (ln.rfind("#EXT-X-STREAM-INF:", 0) == 0) {
                pendingStreamInf = std::string(ln.substr(18));
                continue;
            }
            if (ln[0] == '#') {
                if (ln.rfind("#EXT-X-VERSION:", 0) == 0) summary.rows.push_back({ L"HLS version", w(ln.substr(15)) });
                else if (ln.rfind("#EXT-X-TARGETDURATION:", 0) == 0) summary.rows.push_back({ L"Target duration (s)", w(ln.substr(22)) });
                else if (ln.rfind("#EXT-X-PLAYLIST-TYPE:", 0) == 0) summary.rows.push_back({ L"Playlist type", w(ln.substr(21)) });
                else if (ln.rfind("#EXT-X-ENDLIST", 0) == 0) summary.rows.push_back({ L"End of list", L"yes (VOD / complete)" });
                else if (ln.rfind("#EXT-X-KEY:", 0) == 0) summary.rows.push_back({ L"Encryption", w(ln.substr(11)) });
                else if (ln.rfind("#PLAYLIST:", 0) == 0) summary.rows.push_back({ L"Playlist name", w(ln.substr(10)) });
                continue;
            }
            // A URI / path line
            bool looksLikeUri = ln.find("://") != std::string_view::npos || ln.find('/') != std::string_view::npos ||
                                ln.find('\\') != std::string_view::npos || ln.find('.') != std::string_view::npos;
            if (!looksLikeUri && !havePending && pendingStreamInf.empty()) { ++nonUriText; continue; }
            ++entries;
            if (hlsMaster && !pendingStreamInf.empty()) {
                std::string bw, res, codecs;
                for (const auto& kv : ParseAttrList(pendingStreamInf, ',')) {
                    if (kv.first == "BANDWIDTH") bw = kv.second;
                    else if (kv.first == "RESOLUTION") res = kv.second;
                    else if (kv.first == "CODECS") codecs = kv.second;
                }
                if (sheet.rows.size() <= kMaxRows) {
                    sheet.rows.push_back({ std::to_wstring(entries), w(bw), w(res), w(codecs), w(ln) });
                }
            } else {
                if (havePending && pendingDur >= 0.0) totalSec += pendingDur;
                else unknownDurations = true;
                if (sheet.rows.size() <= kMaxRows) {
                    sheet.rows.push_back({ std::to_wstring(entries),
                                           (havePending && pendingDur >= 0.0) ? FormatDuration(pendingDur) : (havePending ? L"(stream)" : L""),
                                           w(pendingTitle), w(ln), w(pendingAttrs) });
                }
            }
            pendingTitle.clear();
            pendingAttrs.clear();
            pendingStreamInf.clear();
            pendingDur = -1.0;
            havePending = false;
        }
        // Plain text that is not a list of paths / URLs is not a playlist
        if (entries == 0 || (!extended && nonUriText > entries)) return false;
        out.formatTitle = hlsMaster ? L"HTTP Live Streaming Master Playlist (.m3u8)"
                        : hlsMedia ? L"HTTP Live Streaming Media Playlist (.m3u8)"
                        : L"Media Playlist (.m3u / .m3u8)";
        if (hlsMaster) summary.rows.push_back({ L"Variant streams", std::to_wstring(entries) });
    }
    if (entries == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    sheet.maxCols = sheet.rows[0].size();
    summary.rows.push_back({ L"Entries", std::to_wstring(entries) });
    if (totalSec > 0.0) {
        summary.rows.push_back({ L"Total duration", FormatDuration(totalSec) + (unknownDurations ? L" (+ entries of unknown length)" : L"") });
    }
    summary.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
