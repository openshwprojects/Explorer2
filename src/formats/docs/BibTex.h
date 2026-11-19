#pragma once
// BibTex.h — BibTeX (.bib): @type{key, field = value, ...} entries parsed with brace / quote nesting,
// "#" concatenation, @string macros (plus the standard month names), @preamble / @comment skipped, and
// common LaTeX accents ({\"o}, \'e, \l, \k{a}, ...) turned into Unicode for display.

namespace Formats {

namespace DetailBib {

struct Parser {
    std::string_view s;
    size_t p = 0;
    std::map<std::string, std::string> macros;

    void SkipWs() {
        while (p < s.size()) {
            char c = s[p];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++p;
            else if (c == '%') { while (p < s.size() && s[p] != '\n') ++p; } // TeX comment
            else break;
        }
    }
    static std::string Lower(std::string_view v) {
        std::string r(v);
        for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    }
    std::string Ident() {
        size_t st = p;
        while (p < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[p]);
            if (std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == '.' || c == '+' || c == '/' || c >= 0x80u) ++p;
            else break;
        }
        return std::string(s.substr(st, p - st));
    }
    // {...} with nesting; returns the inside
    bool Braced(std::string& out) {
        if (p >= s.size() || s[p] != '{') return false;
        int depth = 0;
        size_t st = p + 1;
        for (; p < s.size(); ++p) {
            if (s[p] == '{') ++depth;
            else if (s[p] == '}' && --depth == 0) {
                out.assign(s.data() + st, p - st);
                ++p;
                return true;
            } else if (s[p] == '\\' && p + 1 < s.size()) {
                ++p; // \{ and \} do not count
            }
        }
        return false;
    }
    // "..." where braces may nest and protect quotes
    bool Quoted(std::string& out) {
        if (p >= s.size() || s[p] != '"') return false;
        int depth = 0;
        size_t st = ++p;
        for (; p < s.size(); ++p) {
            char c = s[p];
            if (c == '\\' && p + 1 < s.size()) { ++p; continue; }
            if (c == '{') ++depth;
            else if (c == '}') --depth;
            else if (c == '"' && depth == 0) {
                out.assign(s.data() + st, p - st);
                ++p;
                return true;
            }
        }
        return false;
    }
    // value := part ("#" part)*, part := {..} | ".." | number | macro
    bool Value(std::string& out) {
        out.clear();
        while (true) {
            SkipWs();
            std::string part;
            if (p < s.size() && s[p] == '{') { if (!Braced(part)) return false; }
            else if (p < s.size() && s[p] == '"') { if (!Quoted(part)) return false; }
            else {
                std::string id = Ident();
                if (id.empty()) return false;
                bool numeric = id.find_first_not_of("0123456789") == std::string::npos;
                if (numeric) part = id;
                else {
                    auto it = macros.find(Lower(id));
                    part = (it != macros.end()) ? it->second : id;
                }
            }
            out += part;
            SkipWs();
            if (p < s.size() && s[p] == '#') { ++p; continue; }
            return true;
        }
    }
};

// LaTeX accents and a few commands -> UTF-8, braces removed
inline std::string CleanTex(std::string_view v) {
    static const struct { const char* cmd; char base; const char* utf8; } kAccents[] = {
        { "\"", 'a', "\xC3\xA4" }, { "\"", 'o', "\xC3\xB6" }, { "\"", 'u', "\xC3\xBC" }, { "\"", 'A', "\xC3\x84" },
        { "\"", 'O', "\xC3\x96" }, { "\"", 'U', "\xC3\x9C" }, { "\"", 'e', "\xC3\xAB" }, { "\"", 'i', "\xC3\xAF" },
        { "'", 'a', "\xC3\xA1" }, { "'", 'e', "\xC3\xA9" }, { "'", 'i', "\xC3\xAD" }, { "'", 'o', "\xC3\xB3" },
        { "'", 'u', "\xC3\xBA" }, { "'", 'y', "\xC3\xBD" }, { "'", 'E', "\xC3\x89" }, { "'", 'O', "\xC3\x93" },
        { "'", 'c', "\xC4\x87" }, { "'", 'n', "\xC5\x84" }, { "'", 's', "\xC5\x9B" }, { "'", 'z', "\xC5\xBA" },
        { "'", 'C', "\xC4\x86" }, { "'", 'N', "\xC5\x83" }, { "'", 'S', "\xC5\x9A" }, { "'", 'Z', "\xC5\xB9" },
        { "`", 'a', "\xC3\xA0" }, { "`", 'e', "\xC3\xA8" }, { "`", 'o', "\xC3\xB2" }, { "`", 'u', "\xC3\xB9" },
        { "^", 'a', "\xC3\xA2" }, { "^", 'e', "\xC3\xAA" }, { "^", 'o', "\xC3\xB4" }, { "^", 'i', "\xC3\xAE" },
        { "~", 'n', "\xC3\xB1" }, { "~", 'a', "\xC3\xA3" }, { "~", 'o', "\xC3\xB5" }, { "~", 'N', "\xC3\x91" },
        { ".", 'z', "\xC5\xBC" }, { ".", 'Z', "\xC5\xBB" }, { "c", 'c', "\xC3\xA7" }, { "c", 'C', "\xC3\x87" },
        { "k", 'a', "\xC4\x85" }, { "k", 'e', "\xC4\x99" }, { "k", 'A', "\xC4\x84" }, { "k", 'E', "\xC4\x98" },
        { "v", 'c', "\xC4\x8D" }, { "v", 's', "\xC5\xA1" }, { "v", 'z', "\xC5\xBE" }, { "v", 'r', "\xC5\x99" },
        { "v", 'e', "\xC4\x9B" }, { "v", 'C', "\xC4\x8C" }, { "v", 'S', "\xC5\xA0" }, { "v", 'Z', "\xC5\xBD" },
    };
    static const struct { const char* cmd; const char* utf8; } kLetters[] = {
        { "l", "\xC5\x82" }, { "L", "\xC5\x81" }, { "o", "\xC3\xB8" }, { "O", "\xC3\x98" }, { "ss", "\xC3\x9F" },
        { "aa", "\xC3\xA5" }, { "AA", "\xC3\x85" }, { "ae", "\xC3\xA6" }, { "AE", "\xC3\x86" }, { "i", "i" },
        { "&", "&" }, { "%", "%" }, { "_", "_" }, { "$", "$" }, { "#", "#" }, { "TeX", "TeX" }, { "LaTeX", "LaTeX" },
    };
    std::string out;
    size_t i = 0;
    while (i < v.size()) {
        char c = v[i];
        if (c == '{' || c == '}') { ++i; continue; }
        if (c == '~') { out.push_back(' '); ++i; continue; } // tie
        if (c == '\\' && i + 1 < v.size()) {
            size_t j = i + 1;
            std::string cmd;
            if (std::isalpha(static_cast<unsigned char>(v[j]))) {
                while (j < v.size() && std::isalpha(static_cast<unsigned char>(v[j]))) cmd.push_back(v[j++]);
            } else {
                cmd.push_back(v[j++]);
            }
            // argument: optional spaces/braces, then one letter
            size_t k = j;
            while (k < v.size() && (v[k] == ' ' || v[k] == '{')) ++k;
            bool done = false;
            if (k < v.size()) {
                for (const auto& a : kAccents) {
                    if (cmd == a.cmd && v[k] == a.base) {
                        out += a.utf8;
                        i = k + 1;
                        done = true;
                        break;
                    }
                }
            }
            if (!done) {
                for (const auto& l : kLetters) {
                    if (cmd == l.cmd) {
                        out += l.utf8;
                        i = j;
                        if (i < v.size() && v[i] == ' ' && std::isalpha(static_cast<unsigned char>(cmd[0]))) ++i;
                        done = true;
                        break;
                    }
                }
            }
            if (!done) {
                // Unknown command (\emph, \textit, ...): drop the command name, keep its argument text
                i = j;
            }
            continue;
        }
        if (c == '\r' || c == '\n' || c == '\t') { if (!out.empty() && out.back() != ' ') out.push_back(' '); ++i; continue; }
        if (c == ' ' && !out.empty() && out.back() == ' ') { ++i; continue; }
        out.push_back(c);
        ++i;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    size_t lead = out.find_first_not_of(' ');
    return lead == std::string::npos ? std::string() : out.substr(lead);
}

} // namespace DetailBib

inline bool ParseBibTexDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailBib;
    Parser ps;
    ps.s = std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (ps.s.size() >= 3 && static_cast<uint8_t>(ps.s[0]) == 0xEF && static_cast<uint8_t>(ps.s[1]) == 0xBB &&
        static_cast<uint8_t>(ps.s[2]) == 0xBF) {
        ps.p = 3;
    }
    static const char* kMonths[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
    static const char* kMonthNames[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                         "September", "October", "November", "December" };
    for (int m = 0; m < 12; ++m) ps.macros[kMonths[m]] = kMonthNames[m];

    SpreadsheetSheet sheet;
    sheet.name = L"BibTeX_Entries";
    sheet.rows.push_back({ L"Type", L"Cite Key", L"Title", L"Author", L"Year", L"Journal / Booktitle", L"Volume",
                           L"Pages", L"Publisher", L"DOI / URL" });
    std::map<std::string, size_t> typeCounts;
    size_t entries = 0, strings = 0, broken = 0;

    while (ps.p < ps.s.size()) {
        size_t at = ps.s.find('@', ps.p);
        if (at == std::string_view::npos) break;
        ps.p = at + 1;
        ps.SkipWs();
        std::string type = Parser::Lower(ps.Ident());
        ps.SkipWs();
        if (type.empty() || ps.p >= ps.s.size() || (ps.s[ps.p] != '{' && ps.s[ps.p] != '(')) continue;
        char close = (ps.s[ps.p] == '{') ? '}' : ')';
        if (type == "comment") {
            std::string skip;
            if (close == '}') ps.Braced(skip); else ++ps.p;
            continue;
        }
        ++ps.p;
        if (type == "preamble") {
            std::string skip;
            ps.Value(skip);
            ps.SkipWs();
            if (ps.p < ps.s.size() && ps.s[ps.p] == close) ++ps.p;
            continue;
        }
        if (type == "string") {
            ps.SkipWs();
            std::string name = Parser::Lower(ps.Ident());
            ps.SkipWs();
            std::string val;
            if (!name.empty() && ps.p < ps.s.size() && ps.s[ps.p] == '=') {
                ++ps.p;
                if (ps.Value(val)) {
                    ps.macros[name] = val;
                    ++strings;
                }
            }
            ps.SkipWs();
            if (ps.p < ps.s.size() && ps.s[ps.p] == close) ++ps.p;
            continue;
        }
        // Regular entry: key, then field = value pairs
        ps.SkipWs();
        size_t keyStart = ps.p;
        while (ps.p < ps.s.size() && ps.s[ps.p] != ',' && ps.s[ps.p] != close && ps.s[ps.p] != '\n') ++ps.p;
        std::string key(ps.s.substr(keyStart, ps.p - keyStart));
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
        std::map<std::string, std::string> fields;
        bool ok = true;
        while (ps.p < ps.s.size()) {
            ps.SkipWs();
            if (ps.p < ps.s.size() && ps.s[ps.p] == ',') { ++ps.p; ps.SkipWs(); }
            if (ps.p < ps.s.size() && ps.s[ps.p] == close) { ++ps.p; break; }
            std::string fname = Parser::Lower(ps.Ident());
            ps.SkipWs();
            if (fname.empty() || ps.p >= ps.s.size() || ps.s[ps.p] != '=') { ok = false; break; }
            ++ps.p;
            std::string val;
            if (!ps.Value(val)) { ok = false; break; }
            if (!fields.count(fname)) {
                // Identifiers keep '~', '%' and '_' as written; text fields get the TeX clean-up
                bool raw = (fname == "url" || fname == "doi" || fname == "eprint" || fname == "file" || fname == "isbn");
                fields[fname] = raw ? val : CleanTex(val);
            }
        }
        if (!ok && fields.empty()) {
            ++broken;
            continue; // resynchronize at the next '@'
        }
        ++entries;
        ++typeCounts[type];
        if (sheet.rows.size() > 20000) continue;
        auto f = [&](const char* name) -> std::wstring {
            auto it = fields.find(name);
            return it == fields.end() ? std::wstring() : PathUtils::Utf8ToWide(it->second);
        };
        std::wstring venue = f("journal");
        if (venue.empty()) venue = f("booktitle");
        if (venue.empty()) venue = f("school");
        if (venue.empty()) venue = f("institution");
        std::wstring link = f("doi");
        if (link.empty()) link = f("url");
        std::wstring authors = f("author");
        if (authors.empty()) authors = f("editor");
        // "A and B and C" -> "A; B; C"
        for (size_t pos = authors.find(L" and "); pos != std::wstring::npos; pos = authors.find(L" and ", pos + 2)) {
            authors.replace(pos, 5, L"; ");
        }
        sheet.rows.push_back({ PathUtils::Utf8ToWide(type), PathUtils::Utf8ToWide(key), f("title"), authors, f("year"),
                               venue, f("volume"), f("pages"), f("publisher"), link });
    }
    if (entries == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"BibTeX Bibliography Database (.bib)";
    sheet.maxCols = 10;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"BibTeX_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Entries", std::to_wstring(entries) });
    for (const auto& tc : typeCounts) summary.rows.push_back({ L"@" + PathUtils::Utf8ToWide(tc.first), std::to_wstring(tc.second) });
    if (strings) summary.rows.push_back({ L"@string macros", std::to_wstring(strings) });
    if (broken) summary.rows.push_back({ L"Unreadable entries (skipped)", std::to_wstring(broken) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
