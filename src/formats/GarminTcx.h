#pragma once
// GarminTcx.h — Garmin Training Center XML (.tcx) Fitness Telemetry & GPS Trackpoint Parser

namespace Formats {

inline bool ParseGarminTcxDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("<TrainingCenterDatabase") == std::string_view::npos && sv.find("<Trackpoint>") == std::string_view::npos) {
        return false;
    }

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Garmin Training Center Telemetry (.tcx)";

    SpreadsheetSheet sheet;
    sheet.name = L"TCX_Trackpoints";
    sheet.rows.push_back({ L"#", L"Timestamp", L"Latitude", L"Longitude", L"Altitude (m)", L"Heart Rate (bpm)" });

    auto getTag = [](std::string_view blk, const char* openT, const char* closeT) -> std::wstring {
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
        size_t p1 = sv.find("<Trackpoint>", pos);
        if (p1 == std::string_view::npos) break;
        size_t p2 = sv.find("</Trackpoint>", p1);
        if (p2 == std::string_view::npos) break;
        std::string_view blk = sv.substr(p1, p2 - p1);
        pos = p2 + 13;

        std::wstring tm  = getTag(blk, "<Time>", "</Time>");
        std::wstring lat = getTag(blk, "<LatitudeDegrees>", "</LatitudeDegrees>");
        std::wstring lon = getTag(blk, "<LongitudeDegrees>", "</LongitudeDegrees>");
        std::wstring alt = getTag(blk, "<AltitudeMeters>", "</AltitudeMeters>");
        std::wstring hr  = getTag(blk, "<Value>", "</Value>");
        sheet.rows.push_back({ std::to_wstring(idx++), tm, lat, lon, alt, hr });
    }

    sheet.maxCols = 6;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
