#pragma once
// EsriShapefileDoc.h — ESRI 2D/Attribute Shapefile (.shp / .shx) Reader

namespace Formats {
namespace GeoSciDetail {

// ============================================================================
// 1. ESRI Shapefile (.shp / .shx) + Optional Sibling .dbf Attribute Join
// ============================================================================
inline const wchar_t* ShpTypeName(int32_t st) {
    switch (st) {
        case 0:  return L"NullShape";
        case 1:  return L"Point";
        case 3:  return L"PolyLine";
        case 5:  return L"Polygon";
        case 8:  return L"MultiPoint";
        case 11: return L"PointZ";
        case 13: return L"PolyLineZ";
        case 15: return L"PolygonZ";
        case 18: return L"MultiPointZ";
        case 21: return L"PointM";
        case 23: return L"PolyLineM";
        case 25: return L"PolygonM";
        case 28: return L"MultiPointM";
        case 31: return L"MultiPatch";
        default: return L"Geometry";
    }
}

inline bool ParseShapefile(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 100 || RdU32BE(raw.data()) != 9994u) return false;

    uint32_t fileLenWords = RdU32BE(raw.data() + 24);
    int32_t shapeType = RdI32LE(raw.data() + 32);
    double xmin = RdF64LE(raw.data() + 36);
    double ymin = RdF64LE(raw.data() + 44);
    double xmax = RdF64LE(raw.data() + 52);
    double ymax = RdF64LE(raw.data() + 60);
    double zmin = RdF64LE(raw.data() + 68);
    double zmax = RdF64LE(raw.data() + 76);

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"Shapefile_Header";
    hdrSh.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
    hdrSh.rows.push_back({ L"File Code", L"9994 (0x0000270A)", FmtCoord(ymin), FmtCoord(xmin) });
    hdrSh.rows.push_back({ L"Shape Geometry Type", std::wstring(ShpTypeName(shapeType)) + L" (" + std::to_wstring(shapeType) + L")", FmtCoord(ymax), FmtCoord(xmax) });
    hdrSh.rows.push_back({ L"Declared Length", PathUtils::FormatByteSize(static_cast<uint64_t>(fileLenWords) * 2ULL), L"", L"" });
    if (zmin != 0.0 || zmax != 0.0) {
        hdrSh.rows.push_back({ L"Z Elevation Range", FmtNum(zmin) + L" .. " + FmtNum(zmax), L"", L"" });
    }
    FinalizeCols(hdrSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = (extLower == L".shx") ? L"ESRI Shapefile Spatial Index (.shx)" : L"ESRI Vector Shapefile (.shp)";

    // Keep 100-byte header-only bounding-box compatibility (Stage12.Test13)
    if (raw.size() == 100) {
        out.sheets.push_back(std::move(hdrSh));
        out.valid = true;
        return true;
    }

    if (extLower == L".shx") {
        SpreadsheetSheet idxSh;
        idxSh.name = L"SHX_Index";
        idxSh.rows.push_back({ L"Record #", L"Byte Offset (.shp)", L"Record Length (Bytes)" });
        size_t pos = 100;
        size_t recIdx = 1;
        while (pos + 8 <= raw.size() && idxSh.rows.size() <= 1000) {
            uint32_t offWords = RdU32BE(raw.data() + pos);
            uint32_t lenWords = RdU32BE(raw.data() + pos + 4);
            idxSh.rows.push_back({
                std::to_wstring(recIdx++),
                std::to_wstring(static_cast<uint64_t>(offWords) * 2ULL),
                std::to_wstring(static_cast<uint64_t>(lenWords) * 2ULL)
            });
            pos += 8;
        }
        FinalizeCols(idxSh);
        out.sheets.push_back(std::move(idxSh));
        out.sheets.push_back(std::move(hdrSh));
        out.valid = true;
        return true;
    }

    // Check for sibling <stem>.dbf attribute table on disk
    ParsedOfficeDocument dbfDoc;
    bool hasDbf = false;
    if (out.filePath.size() > 4) {
        std::wstring dbfPath = out.filePath.substr(0, out.filePath.size() - 4) + L".dbf";
        std::vector<uint8_t> dbfBytes;
        if (PathUtils::ReadFileBytes(dbfPath, 16 * 1024 * 1024, dbfBytes) && !dbfBytes.empty()) {
            hasDbf = ParseDbfDatabase(dbfBytes, dbfDoc) && !dbfDoc.sheets.empty() && !dbfDoc.sheets[0].rows.empty();
        }
    }

    SpreadsheetSheet featSh;
    featSh.name = L"Shape_Features";
    std::vector<std::wstring> fhdr = { L"Record #", L"Geometry", L"Parts", L"Vertices", L"Latitude", L"Longitude", L"Elevation / Bounds" };
    if (hasDbf) {
        for (const auto& colName : dbfDoc.sheets[0].rows[0]) fhdr.push_back(colName);
    }
    featSh.rows.push_back(std::move(fhdr));

    auto pushShpGeoPoint = [&](double x, double y, double z, int rowIdx, const std::wstring& lbl) -> std::pair<double, double> {
        double lat = y, lon = x;
        // Convert Web Mercator EPSG:3857 meters to WGS84 degrees if within Mercator world bounds
        if ((std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) &&
            std::fabs(x) <= 20037508.35 && std::fabs(y) <= 20037508.35 &&
            (std::fabs(x) > 180.0 || std::fabs(y) > 90.0))
        {
            constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
            lon = (x / 20037508.342789244) * 180.0;
            lat = (2.0 * std::atan(std::exp((y / 20037508.342789244) * 3.14159265358979323846)) - 1.5707963267948966) * kRadToDeg;
        }
        if (out.geoPoints.size() < 10000 && std::isfinite(lat) && std::isfinite(lon) &&
            std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0)
        {
            GpsTrackPoint gp;
            gp.lat = lat;
            gp.lon = lon;
            gp.elevM = std::isfinite(z) ? static_cast<float>(z) : 0.0f;
            gp.rowIndex = rowIdx;
            gp.label = lbl;
            out.geoPoints.push_back(std::move(gp));
        }
        return { lat, lon };
    };

    size_t pos = 100;
    size_t recIdx = 0;
    while (pos + 8 <= raw.size()) {
        uint32_t recNum = RdU32BE(raw.data() + pos);
        uint32_t contentWords = RdU32BE(raw.data() + pos + 4);
        pos += 8;
        size_t recBytes = static_cast<size_t>(contentWords) * 2;
        if (recBytes < 4 || recBytes > raw.size() - pos) break;

        const uint8_t* rp = raw.data() + pos;
        int32_t rType = RdI32LE(rp);
        ++recIdx;
        int sheetRowIdx = static_cast<int>(featSh.rows.size());
        std::wstring recLabel = L"Feature #" + std::to_wstring(recNum);
        if (hasDbf && recIdx < dbfDoc.sheets[0].rows.size() && !dbfDoc.sheets[0].rows[recIdx].empty()) {
            recLabel = dbfDoc.sheets[0].rows[recIdx][0];
        }

        std::vector<std::wstring> frow;
        if (rType == 0) {
            frow = { std::to_wstring(recNum), L"NullShape", L"0", L"0", L"", L"", L"" };
        } else if ((rType == 1 || rType == 11 || rType == 21) && recBytes >= 20) {
            double x = RdF64LE(rp + 4);
            double y = RdF64LE(rp + 12);
            double z = (rType == 11 && recBytes >= 28) ? RdF64LE(rp + 20) : 0.0;
            auto [lat, lon] = pushShpGeoPoint(x, y, z, sheetRowIdx, recLabel);
            frow = {
                std::to_wstring(recNum), ShpTypeName(rType), L"1", L"1",
                FmtCoord(lat), FmtCoord(lon),
                (rType == 11) ? (FmtNum(z) + L" m") : L""
            };
        } else if ((rType == 8 || rType == 18 || rType == 28) && recBytes >= 40) { // MultiPoint
            int32_t numPts = RdI32LE(rp + 36);
            if (numPts > 0 && 40 + static_cast<uint64_t>(numPts) * 16 <= recBytes) {
                double sumLat = 0.0, sumLon = 0.0;
                for (int32_t p = 0; p < numPts; ++p) {
                    double px = RdF64LE(rp + 40 + static_cast<size_t>(p) * 16);
                    double py = RdF64LE(rp + 48 + static_cast<size_t>(p) * 16);
                    auto [lat, lon] = pushShpGeoPoint(px, py, 0.0, sheetRowIdx, recLabel);
                    sumLat += lat; sumLon += lon;
                }
                frow = {
                    std::to_wstring(recNum), ShpTypeName(rType), L"1", std::to_wstring(numPts),
                    FmtCoord(sumLat / numPts), FmtCoord(sumLon / numPts), L""
                };
            }
        } else if ((rType == 3 || rType == 5 || rType == 13 || rType == 15 || rType == 23 || rType == 25) && recBytes >= 44) {
            double bxMin = RdF64LE(rp + 4), byMin = RdF64LE(rp + 12), bxMax = RdF64LE(rp + 20), byMax = RdF64LE(rp + 28);
            int32_t numParts = RdI32LE(rp + 36);
            int32_t numPts   = RdI32LE(rp + 40);
            if (numParts >= 0 && numPts > 0) {
                uint64_t ptsOff = 44ULL + static_cast<uint64_t>(numParts) * 4ULL;
                uint64_t zOff   = ptsOff + static_cast<uint64_t>(numPts) * 16ULL + 16ULL;
                if (ptsOff + static_cast<uint64_t>(numPts) * 16ULL <= recBytes) {
                    double sumLat = 0.0, sumLon = 0.0;
                    for (int32_t p = 0; p < numPts; ++p) {
                        double px = RdF64LE(rp + static_cast<size_t>(ptsOff) + static_cast<size_t>(p) * 16);
                        double py = RdF64LE(rp + static_cast<size_t>(ptsOff) + static_cast<size_t>(p) * 16 + 8);
                        double pz = ((rType == 13 || rType == 15) && zOff + static_cast<uint64_t>(p + 1) * 8ULL <= recBytes)
                            ? RdF64LE(rp + static_cast<size_t>(zOff) + static_cast<size_t>(p) * 8) : 0.0;
                        auto [lat, lon] = pushShpGeoPoint(px, py, pz, sheetRowIdx, recLabel);
                        sumLat += lat; sumLon += lon;
                    }
                    std::wstring bStr = L"[" + FmtNum(bxMin) + L"," + FmtNum(byMin) + L" .. " + FmtNum(bxMax) + L"," + FmtNum(byMax) + L"]";
                    frow = {
                        std::to_wstring(recNum), ShpTypeName(rType), std::to_wstring(numParts), std::to_wstring(numPts),
                        FmtCoord(sumLat / numPts), FmtCoord(sumLon / numPts), bStr
                    };
                }
            }
        }

        if (!frow.empty() && featSh.rows.size() <= 1000) {
            if (hasDbf && recIdx < dbfDoc.sheets[0].rows.size()) {
                for (const auto& cell : dbfDoc.sheets[0].rows[recIdx]) frow.push_back(cell);
            }
            featSh.rows.push_back(std::move(frow));
        }
        pos += recBytes;
    }

    FinalizeCols(featSh);
    if (featSh.rows.size() > 1) {
        out.sheets.push_back(std::move(featSh));
    }
    out.sheets.push_back(std::move(hdrSh));
    out.valid = true;
    return true;
}

} // namespace GeoSciDetail

inline bool ParseEsriShapefileDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return GeoSciDetail::ParseShapefile(raw, extLower, out); }

} // namespace Formats
