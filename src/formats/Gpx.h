#pragma once
// Gpx.h — GPS Exchange Format XML (.gpx) Track & Waypoint Parser

namespace Formats {

inline bool ParseGpxTrack(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("<gpx") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"GPS Exchange Track (.gpx)";

    SpreadsheetSheet sheet;
    sheet.name = L"GPS_Points";
    sheet.rows.push_back({ L"#", L"Latitude", L"Longitude", L"Elevation (m)", L"Timestamp / Name" });

    auto extractAttr = [](std::string_view tag, const char* attr) -> std::wstring {
        size_t p = tag.find(attr);
        if (p == std::string_view::npos) return L"";
        p += std::strlen(attr);
        size_t q = tag.find('"', p);
        if (q == std::string_view::npos) return L"";
        return PathUtils::Utf8ToWide(tag.substr(p, q - p));
    };
    auto extractTag = [](std::string_view blk, const char* openT, const char* closeT) -> std::wstring {
        size_t p1 = blk.find(openT);
        if (p1 == std::string_view::npos) return L"";
        p1 += std::strlen(openT);
        size_t p2 = blk.find(closeT, p1);
        if (p2 == std::string_view::npos) return L"";
        return PathUtils::Utf8ToWide(blk.substr(p1, p2 - p1));
    };

    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t trk = sv.find("<trkpt ", pos);
        size_t wpt = sv.find("<wpt ", pos);
        size_t next = std::min(trk, wpt);
        if (next == std::string_view::npos) break;
        bool isWpt = (next == wpt);
        const char* closeTag = isWpt ? "</wpt>" : "</trkpt>";
        size_t gt = sv.find('>', next);
        size_t endPt = (gt != std::string_view::npos) ? sv.find(closeTag, gt) : std::string_view::npos;
        if (gt == std::string_view::npos || endPt == std::string_view::npos) break;

        std::string_view openBlk = sv.substr(next, gt - next);
        std::string_view innerBlk = sv.substr(gt + 1, endPt - (gt + 1));
        std::wstring lat = extractAttr(openBlk, "lat=\"");
        std::wstring lon = extractAttr(openBlk, "lon=\"");
        std::wstring ele = extractTag(innerBlk, "<ele>", "</ele>");
        std::wstring tm  = extractTag(innerBlk, "<time>", "</time>");
        if (tm.empty()) tm = extractTag(innerBlk, "<name>", "</name>");
        sheet.rows.push_back({ std::to_wstring(idx++), lat, lon, ele, tm });
        pos = endPt + std::strlen(closeTag);
    }

    sheet.maxCols = 5;
    out.sheets.push_back(std::move(sheet));
    out.valid = (out.sheets[0].rows.size() >= 2);
    return out.valid;
}

} // namespace Formats
