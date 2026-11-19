#pragma once
// ContentLines.h — the "content line" syntax shared by iCalendar (RFC 5545) and vCard (RFC 2425 / 6350,
// vCard 2.1): line unfolding (CRLF + space / tab), NAME;PARAM=value;...:VALUE splitting with quoted
// parameter values, property groups, value unescaping, and vCard 2.1 QUOTED-PRINTABLE / CHARSET.

namespace Formats {

namespace ContentLines {

struct Param {
    std::string name;  // upper case
    std::string value; // quotes removed; several values stay comma-separated
};

struct Line {
    std::string group; // "item1" in item1.TEL:...
    std::string name;  // upper case
    std::vector<Param> params;
    std::string value; // raw value (escapes and encodings not yet applied)

    const Param* FindParam(std::string_view n) const {
        for (const auto& p : params) {
            if (p.name == n) return &p;
        }
        return nullptr;
    }
    std::string ParamValue(std::string_view n) const {
        const Param* p = FindParam(n);
        return p ? p->value : std::string();
    }
    // vCard 2.1 bare types (TEL;CELL;HOME:...) and TYPE=a,b
    bool HasType(std::string_view t) const {
        for (const auto& p : params) {
            auto eqCI = [](std::string_view a, std::string_view b) {
                if (a.size() != b.size()) return false;
                for (size_t i = 0; i < a.size(); ++i) {
                    char x = a[i], y = b[i];
                    if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 32);
                    if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 32);
                    if (x != y) return false;
                }
                return true;
            };
            if (p.value.empty() && eqCI(p.name, t)) return true;
            if (p.name == "TYPE") {
                size_t s = 0;
                while (s <= p.value.size()) {
                    size_t c = p.value.find(',', s);
                    if (c == std::string::npos) c = p.value.size();
                    if (eqCI(std::string_view(p.value).substr(s, c - s), t)) return true;
                    s = c + 1;
                }
            }
        }
        return false;
    }
};

inline std::string ToUpperAscii(std::string_view s) {
    std::string r(s);
    for (char& c : r) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }
    return r;
}

// =XX hex escapes and '=' soft line breaks (already joined by Parse)
inline std::string DecodeQuotedPrintable(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '=' && i + 1 < v.size() && v[i + 1] == '\n') { ++i; continue; }                         // soft line break
        if (v[i] == '=' && i + 2 < v.size() && v[i + 1] == '\r' && v[i + 2] == '\n') { i += 2; continue; }
        if (v[i] == '=' && i + 2 < v.size()) {
            int h = hex(v[i + 1]);
            int l = hex(v[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(v[i]);
    }
    return out;
}

// RFC 5545 / 6350 TEXT escapes: \n \N \, \; \\ (unknown escapes keep the character)
inline std::string Unescape(std::string_view v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            if (n == 'n' || n == 'N') out.push_back('\n');
            else out.push_back(n);
            continue;
        }
        out.push_back(v[i]);
    }
    return out;
}

// Structured values (N, ADR, ORG): split at unescaped separators, then unescape each part
inline std::vector<std::string> SplitComponents(std::string_view v, char sep = ';') {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            cur.push_back(v[i]);
            cur.push_back(v[++i]);
            continue;
        }
        if (v[i] == sep) {
            parts.push_back(Unescape(cur));
            cur.clear();
            continue;
        }
        cur.push_back(v[i]);
    }
    parts.push_back(Unescape(cur));
    return parts;
}

// Splits text into unfolded content lines. Lines that do not have the NAME...:VALUE shape are skipped.
inline std::vector<Line> Parse(std::string_view text, size_t maxLines = 200000) {
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEFu && static_cast<uint8_t>(text[1]) == 0xBBu &&
        static_cast<uint8_t>(text[2]) == 0xBFu) {
        text.remove_prefix(3);
    }
    // 1. Physical lines -> logical lines (folding; vCard 2.1 quoted-printable soft breaks)
    std::vector<std::string> logical;
    size_t pos = 0;
    while (pos < text.size() && logical.size() < maxLines) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view ln = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        if (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t') && !logical.empty()) {
            logical.back().append(ln.data() + 1, ln.size() - 1); // folded continuation
            continue;
        }
        if (ln.empty()) continue;
        // QUOTED-PRINTABLE value ending in '=' continues on the next physical line
        if (!logical.empty()) {
            const std::string& prev = logical.back();
            if (!prev.empty() && prev.back() == '=' &&
                ToUpperAscii(prev.substr(0, prev.find(':'))).find("QUOTED-PRINTABLE") != std::string::npos) {
                logical.back().pop_back();
                logical.back().append(ln.data(), ln.size());
                continue;
            }
        }
        logical.emplace_back(ln);
    }

    // 2. NAME *(;PARAM[=VALUE]) : VALUE
    std::vector<Line> out;
    out.reserve(logical.size());
    for (const std::string& l : logical) {
        size_t i = 0;
        const size_t n = l.size();
        size_t nameEnd = 0;
        while (nameEnd < n && l[nameEnd] != ';' && l[nameEnd] != ':') ++nameEnd;
        if (nameEnd == 0 || nameEnd >= n) continue;
        Line line;
        std::string_view fullName(l.data(), nameEnd);
        size_t dot = fullName.find('.');
        if (dot != std::string_view::npos) {
            line.group = std::string(fullName.substr(0, dot));
            fullName.remove_prefix(dot + 1);
        }
        line.name = ToUpperAscii(fullName);
        i = nameEnd;
        bool ok = true;
        while (i < n && l[i] == ';') {
            ++i;
            size_t ps = i;
            while (i < n && l[i] != '=' && l[i] != ';' && l[i] != ':') ++i;
            Param p;
            p.name = ToUpperAscii(std::string_view(l.data() + ps, i - ps));
            if (i < n && l[i] == '=') {
                ++i;
                std::string val;
                while (i < n && l[i] != ';' && l[i] != ':') {
                    if (l[i] == '"') {
                        size_t q = l.find('"', i + 1);
                        if (q == std::string::npos) { ok = false; break; }
                        val.append(l, i + 1, q - i - 1);
                        i = q + 1;
                    } else {
                        val.push_back(l[i++]);
                    }
                }
                p.value = std::move(val);
            }
            if (!ok) break;
            line.params.push_back(std::move(p));
        }
        if (!ok || i >= n || l[i] != ':') continue;
        line.value = l.substr(i + 1);
        out.push_back(std::move(line));
    }
    return out;
}

// The value of a TEXT property in UTF-8: quoted-printable and legacy CHARSET handled, escapes removed
inline std::wstring TextValue(const Line& line, bool unescape = true) {
    std::string v = line.value;
    std::string enc = ToUpperAscii(line.ParamValue("ENCODING"));
    if (enc == "QUOTED-PRINTABLE" || line.HasType("QUOTED-PRINTABLE")) v = DecodeQuotedPrintable(v);
    if (unescape) v = Unescape(v);
    std::string cs = ToUpperAscii(line.ParamValue("CHARSET"));
    if (!cs.empty() && cs != "UTF-8" && cs != "US-ASCII") {
        UINT cp = 0;
        if (cs == "WINDOWS-1250" || cs == "CP1250") cp = 1250;
        else if (cs == "WINDOWS-1251" || cs == "CP1251") cp = 1251;
        else if (cs == "WINDOWS-1252" || cs == "CP1252") cp = 1252;
        else if (cs == "ISO-8859-1") cp = 28591;
        else if (cs == "ISO-8859-2") cp = 28592;
        else if (cs == "ISO-8859-15") cp = 28605;
        else if (cs == "SHIFT_JIS") cp = 932;
        else if (cs == "GB2312" || cs == "GBK") cp = 936;
        else if (cs == "BIG5") cp = 950;
        if (cp != 0 && !v.empty()) {
            int wl = MultiByteToWideChar(cp, 0, v.data(), static_cast<int>(v.size()), nullptr, 0);
            if (wl > 0) {
                std::wstring w(static_cast<size_t>(wl), L'\0');
                MultiByteToWideChar(cp, 0, v.data(), static_cast<int>(v.size()), w.data(), wl);
                return w;
            }
        }
    }
    return PathUtils::Utf8ToWide(v);
}

// "20261117T090000Z" -> "2026-11-17 09:00:00 UTC", "20261117" -> "2026-11-17"; TZID shown in parentheses
inline std::wstring FormatDateTime(const Line& line) {
    std::string v = line.value;
    std::wstring tz = PathUtils::Utf8ToWide(line.ParamValue("TZID"));
    auto digits = [&](size_t at, size_t cnt) {
        if (at + cnt > v.size()) return false;
        for (size_t k = 0; k < cnt; ++k) {
            if (v[at + k] < '0' || v[at + k] > '9') return false;
        }
        return true;
    };
    std::wstring r;
    if (v.size() >= 8 && digits(0, 8)) {
        r = PathUtils::Utf8ToWide(v.substr(0, 4) + "-" + v.substr(4, 2) + "-" + v.substr(6, 2));
        if (v.size() >= 15 && v[8] == 'T' && digits(9, 6)) {
            r += PathUtils::Utf8ToWide(" " + v.substr(9, 2) + ":" + v.substr(11, 2) + ":" + v.substr(13, 2));
            if (v.size() >= 16 && (v[15] == 'Z' || v[15] == 'z')) r += L" UTC";
        }
    } else {
        r = PathUtils::Utf8ToWide(v);
    }
    if (!tz.empty()) r += L" (" + tz + L")";
    return r;
}

} // namespace ContentLines

} // namespace Formats
