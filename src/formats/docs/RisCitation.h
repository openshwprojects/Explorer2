#pragma once
// RisCitation.h — RIS citation files (.ris; EndNote / Zotero / Mendeley / RefWorks exports): "TAG  - value"
// records from TY to ER with all authors, title, year (from PY / Y1 / DA dates), source, volume, issue,
// pages, publisher, DOI / URL and keywords; reference type codes are named.

namespace Formats {

inline bool ParseRisCitationDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF && static_cast<uint8_t>(sv[1]) == 0xBB &&
        static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    static const struct { const char* code; const wchar_t* name; } kTypes[] = {
        { "JOUR", L"Journal article" }, { "JFULL", L"Journal (full)" }, { "BOOK", L"Book" }, { "CHAP", L"Book chapter" },
        { "EBOOK", L"E-book" }, { "ECHAP", L"E-book chapter" }, { "CONF", L"Conference proceedings" },
        { "CPAPER", L"Conference paper" }, { "THES", L"Thesis" }, { "RPRT", L"Report" }, { "ELEC", L"Web page" },
        { "PAT", L"Patent" }, { "NEWS", L"Newspaper" }, { "MGZN", L"Magazine" }, { "GEN", L"Generic" },
        { "DATA", L"Dataset" }, { "COMP", L"Software" }, { "STAND", L"Standard" }, { "UNPB", L"Unpublished" },
    };

    struct Ref {
        std::string type, title, authors, year, source, volume, issue, sp, ep, publisher, doi, url, keywords;
    };
    std::vector<Ref> refs;
    Ref cur;
    bool inRef = false;
    std::string lastTag;
    auto appendList = [](std::string& dst, std::string_view v, const char* sep) {
        if (v.empty()) return;
        if (!dst.empty()) dst += sep;
        dst += v;
    };

    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;
        // "TY  - JOUR": two-character tag, two spaces, hyphen
        bool tagged = ln.size() >= 5 && ln[2] == ' ' && ln[3] == ' ' && ln[4] == '-' &&
                      std::isalnum(static_cast<unsigned char>(ln[0])) && std::isalnum(static_cast<unsigned char>(ln[1]));
        if (!tagged) {
            // Continuation of a long value (abstracts, titles wrapped by some exporters)
            if (inRef && (lastTag == "TI" || lastTag == "T1")) appendList(cur.title, ln, " ");
            continue;
        }
        std::string tag(ln.substr(0, 2));
        std::string_view val = ln.size() > 6 ? ln.substr(6) : std::string_view{};
        while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
        lastTag = tag;
        if (tag == "TY") {
            if (inRef) refs.push_back(std::move(cur));
            cur = Ref{};
            cur.type = std::string(val);
            inRef = true;
            continue;
        }
        if (!inRef) continue;
        if (tag == "ER") {
            refs.push_back(std::move(cur));
            cur = Ref{};
            inRef = false;
        } else if (tag == "TI" || tag == "T1") { if (cur.title.empty()) cur.title = std::string(val); }
        else if (tag == "AU" || tag == "A1" || tag == "A2" || tag == "A3" || tag == "A4") appendList(cur.authors, val, "; ");
        else if (tag == "PY" || tag == "Y1" || tag == "DA") {
            if (cur.year.empty() && val.size() >= 4 && std::isdigit(static_cast<unsigned char>(val[0]))) cur.year = std::string(val.substr(0, 4));
        }
        else if (tag == "JO" || tag == "JF" || tag == "T2" || tag == "BT" || tag == "JA" || tag == "J2") { if (cur.source.empty()) cur.source = std::string(val); }
        else if (tag == "VL") cur.volume = std::string(val);
        else if (tag == "IS") cur.issue = std::string(val);
        else if (tag == "SP") cur.sp = std::string(val);
        else if (tag == "EP") cur.ep = std::string(val);
        else if (tag == "PB") cur.publisher = std::string(val);
        else if (tag == "DO") cur.doi = std::string(val);
        else if (tag == "UR" || tag == "L2") { if (cur.url.empty()) cur.url = std::string(val); }
        else if (tag == "KW") appendList(cur.keywords, val, "; ");
        if (refs.size() >= 20000) break;
    }
    if (inRef) refs.push_back(std::move(cur));
    if (refs.empty()) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"RIS Bibliographic Citations (.ris)";
    SpreadsheetSheet sheet;
    sheet.name = L"RIS_Citations";
    sheet.rows.push_back({ L"Type (TY)", L"Title (TI/T1)", L"Authors (AU)", L"Year (PY/Y1)", L"Journal / Source (JO)",
                           L"Volume", L"Issue", L"Pages", L"Publisher", L"DOI / URL", L"Keywords" });
    std::map<std::wstring, size_t> typeCounts;
    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };
    for (const auto& r : refs) {
        std::wstring type = w(r.type);
        for (const auto& t : kTypes) {
            if (r.type == t.code) { type = std::wstring(t.name) + L" (" + w(r.type) + L")"; break; }
        }
        ++typeCounts[type];
        std::string pages = r.sp + (r.ep.empty() ? "" : "-" + r.ep);
        sheet.rows.push_back({ type, w(r.title), w(r.authors), w(r.year), w(r.source), w(r.volume), w(r.issue), w(pages),
                               w(r.publisher), w(r.doi.empty() ? r.url : r.doi), w(r.keywords) });
    }
    sheet.maxCols = 11;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"RIS_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"References", std::to_wstring(refs.size()) });
    for (const auto& tc : typeCounts) summary.rows.push_back({ tc.first, std::to_wstring(tc.second) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
