#pragma once
// Vcf.h — vCard Contacts Book (.vcf / .vcard) Parser

namespace Formats {

inline bool ParseVCard(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("BEGIN:VCARD") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"vCard Contacts (.vcf)";

    SpreadsheetSheet sheet;
    sheet.name = L"Contacts";
    sheet.rows.push_back({ L"Full Name (FN)", L"Organization (ORG)", L"Title", L"Phone (TEL)", L"Email" });

    bool inCard = false;
    std::wstring fn, org, title, tel, email;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln == "BEGIN:VCARD") {
            inCard = true;
            fn.clear(); org.clear(); title.clear(); tel.clear(); email.clear();
        } else if (ln == "END:VCARD") {
            if (inCard) {
                sheet.rows.push_back({ fn, org, title, tel, email });
            }
            inCard = false;
        } else if (inCard) {
            size_t colon = ln.find(':');
            if (colon != std::string_view::npos) {
                std::string_view k = ln.substr(0, colon);
                std::wstring v = PathUtils::Utf8ToWide(ln.substr(colon + 1));
                if (k.rfind("FN", 0) == 0) fn = v;
                else if (k.rfind("ORG", 0) == 0) org = v;
                else if (k.rfind("TITLE", 0) == 0) title = v;
                else if (k.rfind("TEL", 0) == 0) tel = v;
                else if (k.rfind("EMAIL", 0) == 0) email = v;
            }
        }
    }
    sheet.maxCols = 5;
    out.sheets.push_back(std::move(sheet));
    out.valid = (out.sheets[0].rows.size() >= 2);
    return out.valid;
}

} // namespace Formats
