#pragma once
#include "ContentLines.h"
// Vcf.h — vCard 2.1 / 3.0 / 4.0 (.vcf / .vcard): every card read from unfolded content lines with
// escapes, QUOTED-PRINTABLE and CHARSET decoded (vCard 2.1 phone exports), structured N / ADR / ORG
// values, and all phone numbers / e-mail addresses with their types.

namespace Formats {

inline bool ParseVCard(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace CL = ContentLines;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::vector<CL::Line> lines = CL::Parse(sv);
    if (lines.empty() || lines[0].name != "BEGIN" || CL::ToUpperAscii(lines[0].value) != "VCARD") return false;

    SpreadsheetSheet sheet;
    sheet.name = L"Contacts";
    sheet.rows.push_back({ L"Full Name (FN)", L"Organization (ORG)", L"Title", L"Phone (TEL)", L"Email",
                           L"Address", L"Birthday", L"URL", L"Note", L"Version" });

    struct Card {
        std::wstring fn, nameFromN, org, title, tel, email, adr, bday, url, note, version;
    };
    Card cur;
    int depth = 0;
    size_t cards = 0, photos = 0;
    auto joinNonEmpty = [](const std::vector<std::string>& parts, const char* sep) {
        std::string r;
        for (const auto& p : parts) {
            if (p.empty()) continue;
            if (!r.empty()) r += sep;
            r += p;
        }
        return r;
    };
    auto typeLabel = [](const CL::Line& l) {
        std::wstring t;
        for (const char* k : { "CELL", "WORK", "HOME", "FAX", "VOICE", "PREF", "INTERNET", "MAIN" }) {
            if (l.HasType(k)) {
                if (!t.empty()) t += L",";
                std::string low(k);
                for (char& c : low) c = static_cast<char>(c - 'A' + 'a');
                t += PathUtils::Utf8ToWide(low);
            }
        }
        return t;
    };
    // Structured value: quoted-printable decoded first, then split at ';', non-empty parts joined with ", ",
    // then CHARSET applied
    auto components = [](const CL::Line& l) {
        std::string v = l.value;
        std::string enc = CL::ToUpperAscii(l.ParamValue("ENCODING"));
        if (enc == "QUOTED-PRINTABLE" || l.HasType("QUOTED-PRINTABLE")) v = CL::DecodeQuotedPrintable(v);
        return CL::SplitComponents(v);
    };
    auto withCharset = [](const CL::Line& l, std::string decoded) {
        CL::Line tmp = l;
        tmp.value = std::move(decoded);
        tmp.params.erase(std::remove_if(tmp.params.begin(), tmp.params.end(),
            [](const CL::Param& p) { return p.name == "ENCODING" || p.name == "QUOTED-PRINTABLE"; }), tmp.params.end());
        return CL::TextValue(tmp, false);
    };
    auto structured = [&](const CL::Line& l) { return withCharset(l, joinNonEmpty(components(l), ", ")); };
    auto appendMulti = [](std::wstring& dst, const std::wstring& v, const std::wstring& type) {
        if (v.empty()) return;
        if (!dst.empty()) dst += L"; ";
        dst += v;
        if (!type.empty()) dst += L" (" + type + L")";
    };

    for (const auto& l : lines) {
        if (l.name == "BEGIN" && CL::ToUpperAscii(l.value) == "VCARD") {
            if (++depth == 1) cur = Card{};
            if (depth > 4) return false;
            continue;
        }
        if (l.name == "END" && CL::ToUpperAscii(l.value) == "VCARD") {
            if (depth == 0) continue;
            if (--depth == 0) {
                ++cards;
                std::wstring name = !cur.fn.empty() ? cur.fn : cur.nameFromN;
                if (sheet.rows.size() <= 20000) {
                    std::wstring note = cur.note;
                    for (auto& ch : note) if (ch == L'\n' || ch == L'\r') ch = L' ';
                    sheet.rows.push_back({ name, cur.org, cur.title, cur.tel, cur.email, cur.adr, cur.bday, cur.url, note, cur.version });
                }
            }
            continue;
        }
        if (depth != 1) continue; // nested AGENT cards are counted with their parent only
        if (l.name == "FN") cur.fn = CL::TextValue(l);
        else if (l.name == "N") {
            // Family;Given;Additional;Prefix;Suffix -> "Prefix Given Additional Family Suffix"
            auto p = components(l);
            p.resize(5);
            cur.nameFromN = withCharset(l, joinNonEmpty({ p[3], p[1], p[2], p[0], p[4] }, " "));
        }
        else if (l.name == "ORG") cur.org = structured(l);  // organization;unit;sub-unit
        else if (l.name == "TITLE") cur.title = CL::TextValue(l);
        else if (l.name == "TEL") appendMulti(cur.tel, CL::TextValue(l), typeLabel(l));
        else if (l.name == "EMAIL") appendMulti(cur.email, CL::TextValue(l), typeLabel(l));
        else if (l.name == "ADR") appendMulti(cur.adr, structured(l), typeLabel(l)); // PO box;ext;street;city;region;code;country
        else if (l.name == "BDAY") cur.bday = PathUtils::Utf8ToWide(l.value);
        else if (l.name == "URL") cur.url = CL::TextValue(l, false);
        else if (l.name == "NOTE") cur.note = CL::TextValue(l);
        else if (l.name == "VERSION") cur.version = PathUtils::Utf8ToWide(l.value);
        else if (l.name == "PHOTO" || l.name == "LOGO") ++photos;
    }
    if (cards == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"vCard Contacts (.vcf)";
    sheet.maxCols = 10;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"vCard_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Contacts", std::to_wstring(cards) });
    if (photos) summary.rows.push_back({ L"Embedded photos / logos", std::to_wstring(photos) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
