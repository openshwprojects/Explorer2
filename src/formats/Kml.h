#pragma once
// Kml.h — Keyhole Markup Language Geographic XML (.kml) Parser

namespace Formats {

inline bool ParseKmlGeography(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("<kml") == std::string_view::npos && sv.find("<Placemark") == std::string_view::npos) {
        return false;
    }

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Google Earth KML (.kml)";

    SpreadsheetSheet sheet;
    sheet.name = L"Placemarks";
    sheet.rows.push_back({ L"#", L"Placemark Name", L"Description", L"Coordinates (Lon,Lat,Alt)" });

    auto extractTag = [](std::string_view blk, const char* openT, const char* closeT) -> std::wstring {
        size_t p1 = blk.find(openT);
        if (p1 == std::string_view::npos) return L"";
        p1 += std::strlen(openT);
        size_t p2 = blk.find(closeT, p1);
        if (p2 == std::string_view::npos) return L"";
        std::string_view v = blk.substr(p1, p2 - p1);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\r' || v.front() == '\n')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.remove_suffix(1);
        return PathUtils::Utf8ToWide(v);
    };

    int idx = 1;
    size_t pos = 0;
    while ((pos = sv.find("<Placemark", pos)) != std::string_view::npos && idx <= 5000) {
        size_t endPm = sv.find("</Placemark>", pos);
        if (endPm == std::string_view::npos) break;
        std::string_view blk = sv.substr(pos, endPm - pos);
        std::wstring nm = extractTag(blk, "<name>", "</name>");
        std::wstring desc = extractTag(blk, "<description>", "</description>");
        std::wstring coords = extractTag(blk, "<coordinates>", "</coordinates>");
        sheet.rows.push_back({ std::to_wstring(idx++), nm, desc, coords });
        pos = endPm + 12;
    }

    sheet.maxCols = 4;
    out.sheets.push_back(std::move(sheet));
    out.valid = (out.sheets[0].rows.size() >= 2);
    return out.valid;
}

} // namespace Formats
