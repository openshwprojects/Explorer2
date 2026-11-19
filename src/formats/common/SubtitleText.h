#pragma once
// SubtitleText.h — shared pieces of the subtitle readers (SubRip, WebVTT, SubStation Alpha):
// text decoding (UTF-8 / UTF-16 with BOM / legacy ANSI), line splitting, cue timestamps and
// removal of styling markup from cue text.

namespace Formats {

namespace SubtitleText {

// Whole file -> UTF-16 text. UTF-16 LE/BE byte-order marks are honored; otherwise UTF-8, falling back to
// the ANSI code page for legacy 8-bit files (PathUtils::Utf8ToWide).
inline std::wstring DecodeFile(const std::vector<uint8_t>& raw) {
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        std::wstring w((raw.size() - 2) / 2, L'\0');
        for (size_t i = 0; i < w.size(); ++i) w[i] = static_cast<wchar_t>(raw[2 + 2 * i] | (raw[3 + 2 * i] << 8));
        return w;
    }
    if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        std::wstring w((raw.size() - 2) / 2, L'\0');
        for (size_t i = 0; i < w.size(); ++i) w[i] = static_cast<wchar_t>((raw[2 + 2 * i] << 8) | raw[3 + 2 * i]);
        return w;
    }
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF && static_cast<uint8_t>(sv[1]) == 0xBB &&
        static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    return PathUtils::Utf8ToWide(sv);
}

// Lines without their CR / LF; trailing spaces removed
inline std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring ln = text.substr(pos, eol - pos);
        while (!ln.empty() && (ln.back() == L'\r' || ln.back() == L' ' || ln.back() == L'\t')) ln.pop_back();
        lines.push_back(std::move(ln));
        if (eol == text.size()) break;
        pos = eol + 1;
    }
    return lines;
}

// "01:02:03,456" / "01:02:03.456" / "02:03.456" (WebVTT short form) / "1:02:03.45" (SSA centiseconds)
inline bool ParseTimestampMs(std::wstring_view s, int64_t& outMs) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.remove_suffix(1);
    int64_t fields[3] = {};
    int nf = 0;
    size_t i = 0;
    while (i < s.size() && nf < 3) {
        size_t st = i;
        int64_t v = 0;
        while (i < s.size() && s[i] >= L'0' && s[i] <= L'9' && i - st < 9) v = v * 10 + (s[i++] - L'0');
        if (i == st) return false;
        fields[nf++] = v;
        if (i < s.size() && s[i] == L':') { ++i; continue; }
        break;
    }
    if (nf < 2) return false;
    int64_t frac = 0;
    if (i < s.size() && (s[i] == L',' || s[i] == L'.')) {
        ++i;
        int digits = 0;
        int64_t f = 0;
        while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') {
            if (digits < 3) { f = f * 10 + (s[i] - L'0'); ++digits; }
            ++i;
        }
        while (digits < 3) { f *= 10; ++digits; }
        frac = f;
    }
    if (i != s.size()) return false;
    int64_t h = (nf == 3) ? fields[0] : 0;
    int64_t m = (nf == 3) ? fields[1] : fields[0];
    int64_t sec = (nf == 3) ? fields[2] : fields[1];
    if (m > 59 || sec > 59) return false;
    outMs = ((h * 60 + m) * 60 + sec) * 1000 + frac;
    return true;
}

inline std::wstring FormatMs(int64_t ms) {
    if (ms < 0) ms = 0;
    wchar_t buf[48];
    swprintf_s(buf, L"%02lld:%02lld:%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    return buf;
}

inline std::wstring FormatSeconds(int64_t ms) {
    wchar_t buf[32];
    swprintf_s(buf, L"%.3f", static_cast<double>(ms) / 1000.0);
    return buf;
}

// Removes HTML-like tags (<i>, <font color=...>, <v Speaker>, <00:01.000> karaoke times) and
// {\...} override blocks; \N / \n become line breaks and \h a space (SSA); entities &amp; &lt; &gt; decoded.
inline std::wstring StripMarkup(std::wstring_view s, bool ssaEscapes) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'<') {
            size_t e = s.find(L'>', i + 1);
            if (e != std::wstring_view::npos && e - i < 200) { i = e; continue; }
        } else if (c == L'{') {
            size_t e = s.find(L'}', i + 1);
            if (e != std::wstring_view::npos && (ssaEscapes || (i + 1 < s.size() && s[i + 1] == L'\\'))) { i = e; continue; }
        } else if (c == L'\\' && ssaEscapes && i + 1 < s.size()) {
            wchar_t n = s[i + 1];
            if (n == L'N' || n == L'n') { out.push_back(L'\n'); ++i; continue; }
            if (n == L'h') { out.push_back(L' '); ++i; continue; }
        } else if (c == L'&') {
            if (s.substr(i, 5) == L"&amp;") { out.push_back(L'&'); i += 4; continue; }
            if (s.substr(i, 4) == L"&lt;") { out.push_back(L'<'); i += 3; continue; }
            if (s.substr(i, 4) == L"&gt;") { out.push_back(L'>'); i += 3; continue; }
            if (s.substr(i, 6) == L"&nbsp;") { out.push_back(L' '); i += 5; continue; }
        }
        out.push_back(c);
    }
    return out;
}

// Multi-line cue text in one table cell
inline std::wstring JoinCueLines(const std::vector<std::wstring>& lines) {
    std::wstring r;
    for (const auto& l : lines) {
        if (l.empty()) continue;
        if (!r.empty()) r += L" / ";
        r += l;
    }
    return r;
}

} // namespace SubtitleText

} // namespace Formats
