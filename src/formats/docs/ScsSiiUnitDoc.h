#pragma once
// ScsSiiUnitDoc.h — SCS Software Euro Truck Simulator 2 / American Truck Simulator Unit File (.sii)
// Supports plain-text `"SiiNunit { ... }"` and zlib-compressed `.sii` streams, parsing
// `<unit_class> : <unit_name> { <attr>: <val> }` definitions with strict `{}` balance checking.

namespace Formats {

inline bool ParseScsSiiUnitDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 12u || raw.size() > (32u << 20)) return false;

    std::vector<uint8_t> inflated;
    std::string_view sv;
    bool wasCompressed = false;

    // Support RFC 1950 zlib-compressed SiiNunit blobs as well as UTF-8 text SiiNunit
    if (raw.size() >= 16u && raw[0] == 0x78u &&
        ZipDeflate::InflateZlib(raw.data(), raw.size(), inflated, 32u << 20) && !inflated.empty()) {
        if (!FormatPrologue::PrepareUtf8TextNoNul(inflated.data(), inflated.size(), sv, 12u, 32u << 20)) {
            return false;
        }
        wasCompressed = true;
    } else {
        if (!FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 12u, 32u << 20)) {
            return false;
        }
    }

    // Strip `#`, `//`, and `/*...*/` comments while checking balanced quotes and braces
    std::string clean;
    clean.reserve(sv.size());
    bool inQuote = false;
    int braceDepth = 0;
    for (size_t i = 0u; i < sv.size();) {
        const char c = sv[i];
        if (inQuote) {
            if (c == '\\' && i + 1u < sv.size()) {
                clean.push_back(c);
                clean.push_back(sv[i + 1u]);
                i += 2u;
                continue;
            }
            if (c == '"') inQuote = false;
            clean.push_back(c);
            ++i;
            continue;
        }
        if (c == '"') {
            inQuote = true;
            clean.push_back(c);
            ++i;
            continue;
        }
        if (c == '#' || (c == '/' && i + 1u < sv.size() && sv[i + 1u] == '/')) {
            while (i < sv.size() && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1u < sv.size() && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < sv.size()) {
                if (sv[i] == '*' && sv[i + 1u] == '/') { i += 2u; closed = true; break; }
                ++i;
            }
            if (!closed) return false;
            continue;
        }
        if (c == '{') ++braceDepth;
        else if (c == '}') { if (--braceDepth < 0) return false; }
        clean.push_back(c);
        ++i;
    }
    if (inQuote || braceDepth != 0) return false;

    std::string_view csv = ReaderText::TrimSv(clean);
    if (!ReaderText::StartsWithCI(csv, "SiiNunit")) return false;
    csv = ReaderText::TrimSv(csv.substr(8u));
    if (csv.size() < 2u || csv.front() != '{' || csv.back() != '}') return false;
    csv = csv.substr(1u, csv.size() - 2u);

    SpreadsheetSheet sheet;
    sheet.name = L"SiiNunit";
    sheet.rows.push_back({ L"UnitClass", L"UnitName", L"Attribute", L"Value" });

    size_t unitCount = 0u;
    size_t pos = 0u;
    while (pos < csv.size()) {
        const size_t lb = csv.find('{', pos);
        if (lb == std::string_view::npos) break;
        std::string_view hdr = ReaderText::TrimSv(csv.substr(pos, lb - pos));
        const size_t rb = csv.find('}', lb + 1u);
        if (rb == std::string_view::npos) return false;
        std::string_view body = csv.substr(lb + 1u, rb - lb - 1u);
        pos = rb + 1u;

        // Unit header: `<unit_class> : <unit_name>`
        const size_t colon = hdr.find(':');
        if (colon == std::string_view::npos) continue;
        std::string_view uClass = ReaderText::TrimSv(hdr.substr(0u, colon));
        std::string_view uName  = ReaderText::TrimSv(hdr.substr(colon + 1u));
        if (uClass.empty() || uName.empty()) continue;
        ++unitCount;

        size_t lp = 0u;
        std::string_view rawLine;
        while (ReaderText::NextLineSv(body, lp, rawLine)) {
            std::string_view ln = ReaderText::TrimSv(rawLine);
            if (ln.empty() || ln.front() == '@') continue;
            const size_t kvColon = ln.find(':');
            if (kvColon == std::string_view::npos) continue;
            std::string_view k = ReaderText::TrimSv(ln.substr(0u, kvColon));
            std::string_view v = ReaderText::TrimSv(ln.substr(kvColon + 1u));
            if (v.size() >= 2u && v.front() == '"' && v.back() == '"') {
                v = v.substr(1u, v.size() - 2u);
            }
            if (!k.empty() && sheet.rows.size() < 16385u) {
                sheet.rows.push_back({
                    PathUtils::Utf8ToWide(uClass),
                    PathUtils::Utf8ToWide(uName),
                    PathUtils::Utf8ToWide(k),
                    PathUtils::Utf8ToWide(v)
                });
            }
        }
    }

    if (unitCount == 0u || sheet.rows.size() <= 1u) return false;

    wchar_t title[160] = {};
    swprintf_s(title, L"SCS Software SiiNunit Mod Definition (.sii%ls, %zu units, %zu attrs)",
               wasCompressed ? L" zlib" : L"", unitCount, sheet.rows.size() - 1u);
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

} // namespace Formats
