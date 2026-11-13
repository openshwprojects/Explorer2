#pragma once
// BibTex.h — BibTeX (.bib) LaTeX Academic Bibliography Database Parser

namespace Formats {

inline bool ParseBibTexDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find('@') == std::string_view::npos || sv.find('{') == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"BibTeX Bibliography Database (.bib)";

    SpreadsheetSheet sheet;
    sheet.name = L"BibTeX_Entries";
    sheet.rows.push_back({ L"Type", L"Cite Key", L"Title", L"Author", L"Year" });

    auto getField = [](std::string_view blk, const char* fname) -> std::wstring {
        size_t p = 0;
        while (p < blk.size()) {
            size_t eq = blk.find('=', p);
            if (eq == std::string_view::npos) break;
            size_t kStart = eq;
            while (kStart > p && (blk[kStart - 1] == ' ' || blk[kStart - 1] == '\t')) --kStart;
            size_t kBegin = kStart;
            while (kBegin > p && std::isalnum(static_cast<unsigned char>(blk[kBegin - 1]))) --kBegin;
            std::string key(blk.substr(kBegin, kStart - kBegin));
            for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (key == fname) {
                size_t vStart = eq + 1;
                while (vStart < blk.size() && (blk[vStart] == ' ' || blk[vStart] == '\t')) ++vStart;
                if (vStart < blk.size() && (blk[vStart] == '{' || blk[vStart] == '"')) {
                    char closeCh = (blk[vStart] == '{') ? '}' : '"';
                    size_t vEnd = blk.find(closeCh, vStart + 1);
                    if (vEnd != std::string_view::npos) {
                        return PathUtils::Utf8ToWide(blk.substr(vStart + 1, vEnd - vStart - 1));
                    }
                }
            }
            p = eq + 1;
        }
        return L"";
    };

    size_t pos = 0;
    while (pos < sv.size() && sheet.rows.size() < 5000) {
        size_t at = sv.find('@', pos);
        if (at == std::string_view::npos) break;
        size_t b1 = sv.find('{', at);
        if (b1 == std::string_view::npos) break;
        size_t nextAt = sv.find("\n@", b1);
        std::string_view entry = sv.substr(at + 1, ((nextAt != std::string_view::npos) ? nextAt : sv.size()) - (at + 1));

        std::wstring etype = PathUtils::Utf8ToWide(sv.substr(at + 1, b1 - (at + 1)));
        size_t comma = sv.find(',', b1);
        std::wstring ckey = (comma != std::string_view::npos) ? PathUtils::Utf8ToWide(sv.substr(b1 + 1, comma - (b1 + 1))) : L"";
        sheet.rows.push_back({
            etype, ckey,
            getField(entry, "title"),
            getField(entry, "author"),
            getField(entry, "year")
        });
        pos = (nextAt != std::string_view::npos) ? (nextAt + 1) : sv.size();
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
