#pragma once
// GeoJson.h — RFC 7946 GeoJSON (.geojson) Spatial FeatureCollection Table Parser

namespace Formats {

inline bool ParseGeoJsonDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("Feature") == std::string_view::npos && sv.find("coordinates") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"RFC 7946 GeoJSON Spatial Features (.geojson)";

    SpreadsheetSheet sheet;
    sheet.name = L"GeoJSON_Features";
    sheet.rows.push_back({ L"#", L"Geometry Type", L"Coordinates", L"Name / Properties" });

    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t fp = sv.find("\"Feature\"", pos);
        if (fp == std::string_view::npos) break;
        size_t nextFp = sv.find("\"Feature\"", fp + 9);
        std::string_view blk = sv.substr(fp, (nextFp != std::string_view::npos) ? (nextFp - fp) : (sv.size() - fp));

        std::wstring geomType = L"Point";
        for (const char* gt : { "Point", "LineString", "Polygon", "MultiPoint", "MultiPolygon" }) {
            if (blk.find(gt) != std::string_view::npos) {
                geomType = PathUtils::Utf8ToWide(gt);
                break;
            }
        }

        std::wstring coordsStr;
        size_t cp = blk.find("\"coordinates\"");
        if (cp != std::string_view::npos) {
            size_t b1 = blk.find('[', cp);
            size_t b2 = (b1 != std::string_view::npos) ? blk.find(']', b1) : std::string_view::npos;
            if (b1 != std::string_view::npos && b2 != std::string_view::npos) {
                coordsStr = PathUtils::Utf8ToWide(blk.substr(b1, std::min<size_t>(64, b2 - b1 + 1)));
            }
        }

        std::wstring propStr;
        size_t np = blk.find("\"name\"");
        if (np != std::string_view::npos) {
            size_t q1 = blk.find('"', np + 6);
            size_t q2 = (q1 != std::string_view::npos) ? blk.find('"', q1 + 1) : std::string_view::npos;
            if (q1 != std::string_view::npos && q2 != std::string_view::npos) {
                propStr = PathUtils::Utf8ToWide(blk.substr(q1 + 1, q2 - q1 - 1));
            }
        }
        sheet.rows.push_back({ std::to_wstring(idx++), geomType, coordsStr, propStr });
        pos = (nextFp != std::string_view::npos) ? nextFp : sv.size();
    }

    sheet.maxCols = 4;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
