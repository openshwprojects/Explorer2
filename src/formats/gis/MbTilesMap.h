#pragma once
// MbTilesMap.h — Mapbox MBTiles SQLite Map Archive (.mbtiles) Reader

namespace Formats {

inline bool ParseMbTilesMapDoc(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // MBTiles (SQLite3 B-Tree container with metadata + tiles/map/images tables)
    if (ext == L".mbtiles") {
        if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) {
            return false;
        }
        ParsedOfficeDocument sqlDoc;
        sqlDoc.filePath = out.filePath;
        sqlDoc.sourceFileSize = out.sourceFileSize;
        if (!ParseSqliteDatabase(raw, sqlDoc) || !sqlDoc.valid || sqlDoc.sheets.empty()) {
            return false;
        }
        out.sourceReadFully = sqlDoc.sourceReadFully;

        const SpreadsheetSheet* metaSheet = nullptr;
        const SpreadsheetSheet* tilesSheet = nullptr;
        const SpreadsheetSheet* mapSheet = nullptr;
        for (const auto& shItem : sqlDoc.sheets) {
            std::wstring sname = PathUtils::ToLower(shItem.name);
            if (sname == L"metadata") metaSheet = &shItem;
            else if (sname == L"tiles") tilesSheet = &shItem;
            else if (sname == L"map") mapSheet = &shItem;
        }
        if (!metaSheet && !tilesSheet && !mapSheet) {
            return false;
        }

        auto splitCsvDoubles = [](const std::wstring& s, std::vector<double>& outVals) {
            outVals.clear();
            size_t start = 0;
            while (start <= s.size()) {
                size_t comma = s.find(L',', start);
                std::wstring tok = (comma == std::wstring::npos) ? s.substr(start) : s.substr(start, comma - start);
                wchar_t* endPtr = nullptr;
                double v = std::wcstod(tok.c_str(), &endPtr);
                if (endPtr == tok.c_str() || !std::isfinite(v)) return false;
                outVals.push_back(v);
                if (comma == std::wstring::npos) break;
                start = comma + 1u;
            }
            return !outVals.empty();
        };

        std::wstring tsName, tsFormat, tsMinZoom, tsMaxZoom, tsType, tsVersion, tsDesc, tsAttrib;
        double minLon = 0.0, minLat = 0.0, maxLon = 0.0, maxLat = 0.0;
        bool hasBounds = false;
        double centerLon = 0.0, centerLat = 0.0;
        int centerZoom = -1;
        bool hasCenter = false;

        if (metaSheet && !metaSheet->rows.empty()) {
            size_t nameCol = 0, valCol = 1;
            const auto& hdrRow = metaSheet->rows[0];
            for (size_t c = 0; c < hdrRow.size(); ++c) {
                std::wstring cn = PathUtils::ToLower(hdrRow[c]);
                if (cn == L"name") nameCol = c;
                else if (cn == L"value") valCol = c;
            }
            for (size_t r = 1; r < metaSheet->rows.size(); ++r) {
                const auto& row = metaSheet->rows[r];
                if (std::max(nameCol, valCol) >= row.size()) continue;
                std::wstring k = PathUtils::ToLower(row[nameCol]);
                const std::wstring& v = row[valCol];
                if (k == L"name") tsName = v;
                else if (k == L"format") tsFormat = v;
                else if (k == L"minzoom") tsMinZoom = v;
                else if (k == L"maxzoom") tsMaxZoom = v;
                else if (k == L"type") tsType = v;
                else if (k == L"version") tsVersion = v;
                else if (k == L"description") tsDesc = v;
                else if (k == L"attribution") tsAttrib = v;
                else if (k == L"bounds") {
                    std::vector<double> b;
                    if (splitCsvDoubles(v, b) && b.size() == 4 &&
                        std::fabs(b[0]) <= 180.0 && std::fabs(b[1]) <= 90.0 &&
                        std::fabs(b[2]) <= 180.0 && std::fabs(b[3]) <= 90.0 &&
                        b[0] <= b[2] && b[1] <= b[3])
                    {
                        minLon = b[0]; minLat = b[1]; maxLon = b[2]; maxLat = b[3];
                        hasBounds = true;
                    }
                } else if (k == L"center") {
                    std::vector<double> c;
                    if (splitCsvDoubles(v, c) && c.size() >= 2 &&
                        std::fabs(c[0]) <= 180.0 && std::fabs(c[1]) <= 90.0)
                    {
                        centerLon = c[0];
                        centerLat = c[1];
                        if (c.size() >= 3 && c[2] >= 0.0 && c[2] <= 30.0) {
                            centerZoom = static_cast<int>(std::llround(c[2]));
                        }
                        hasCenter = true;
                    }
                }
            }
        }

        // Inspect real decoded tiles (or map) B-Tree rows and derive TMS -> WGS84 bounds if needed
        const SpreadsheetSheet* tileGrid = tilesSheet ? tilesSheet : mapSheet;
        size_t decodedTiles = 0;
        int obsMinZ = 99, obsMaxZ = -1;
        double tMinLon = 180.0, tMinLat = 90.0, tMaxLon = -180.0, tMaxLat = -90.0;
        bool hasDerivedTileBounds = false;

        if (tileGrid && !tileGrid->rows.empty()) {
            int zCol = -1, xCol = -1, yCol = -1;
            const auto& thdr = tileGrid->rows[0];
            for (size_t c = 0; c < thdr.size(); ++c) {
                std::wstring cn = PathUtils::ToLower(thdr[c]);
                if (cn == L"zoom_level") zCol = static_cast<int>(c);
                else if (cn == L"tile_column") xCol = static_cast<int>(c);
                else if (cn == L"tile_row") yCol = static_cast<int>(c);
            }
            if (zCol < 0 && xCol < 0 && yCol < 0 && !metaSheet) {
                return false;
            }
            if (zCol >= 0 && xCol >= 0 && yCol >= 0) {
                constexpr double kPi = 3.14159265358979323846;
                for (size_t r = 1; r < tileGrid->rows.size(); ++r) {
                    const auto& row = tileGrid->rows[r];
                    size_t maxIdx = static_cast<size_t>(std::max({ zCol, xCol, yCol }));
                    if (maxIdx >= row.size()) continue;
                    wchar_t *ez = nullptr, *ex = nullptr, *ey = nullptr;
                    long z = std::wcstol(row[static_cast<size_t>(zCol)].c_str(), &ez, 10);
                    long long x = std::wcstoll(row[static_cast<size_t>(xCol)].c_str(), &ex, 10);
                    long long yTms = std::wcstoll(row[static_cast<size_t>(yCol)].c_str(), &ey, 10);
                    if (ez == row[static_cast<size_t>(zCol)].c_str() ||
                        ex == row[static_cast<size_t>(xCol)].c_str() ||
                        ey == row[static_cast<size_t>(yCol)].c_str()) continue;
                    if (z < 0 || z > 28) continue;
                    long long nInt = 1LL << z;
                    if (x < 0 || x >= nInt || yTms < 0 || yTms >= nInt) continue;

                    ++decodedTiles;
                    obsMinZ = std::min(obsMinZ, static_cast<int>(z));
                    obsMaxZ = std::max(obsMaxZ, static_cast<int>(z));

                    double n = static_cast<double>(nInt);
                    double yXyzNorth = static_cast<double>((nInt - 1LL) - yTms);
                    double yXyzSouth = yXyzNorth + 1.0;
                    double lonW = (static_cast<double>(x) / n) * 360.0 - 180.0;
                    double lonE = (static_cast<double>(x + 1LL) / n) * 360.0 - 180.0;
                    double latN = std::atan(std::sinh(kPi * (1.0 - 2.0 * yXyzNorth / n))) * (180.0 / kPi);
                    double latS = std::atan(std::sinh(kPi * (1.0 - 2.0 * yXyzSouth / n))) * (180.0 / kPi);

                    tMinLon = std::min(tMinLon, lonW);
                    tMaxLon = std::max(tMaxLon, lonE);
                    tMinLat = std::min(tMinLat, latS);
                    tMaxLat = std::max(tMaxLat, latN);
                    hasDerivedTileBounds = true;
                }
            }
        }

        if (!hasBounds && hasDerivedTileBounds) {
            minLon = tMinLon;
            minLat = tMinLat;
            maxLon = tMaxLon;
            maxLat = tMaxLat;
            hasBounds = true;
        }
        if (!hasCenter && hasBounds) {
            centerLon = (minLon + maxLon) * 0.5;
            centerLat = (minLat + maxLat) * 0.5;
            hasCenter = true;
        }

        uint16_t pageSzRaw = DetailDocs200::RdU16BE(raw.data() + 16);
        uint32_t pageSz = (pageSzRaw == 1) ? 65536u : pageSzRaw;
        uint32_t pageCnt = DetailDocs200::RdU32BE(raw.data() + 28);

        auto fmtCoord = [](double c) {
            wchar_t b[32] = {};
            swprintf_s(b, L"%.6f", c);
            return std::wstring(b);
        };

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Mapbox MBTiles SQLite Map Archive (.mbtiles)";

        SpreadsheetSheet ov;
        ov.name = L"MBTiles_Overview";
        ov.maxCols = 4;
        ov.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        if (hasBounds) {
            std::wstring bLabel = tsName.empty() ? L"MBTiles Bounds" : tsName;
            ov.rows.push_back({ L"SW Corner", bLabel, fmtCoord(minLat), fmtCoord(minLon) });
            ov.rows.push_back({ L"NW Corner", bLabel, fmtCoord(maxLat), fmtCoord(minLon) });
            ov.rows.push_back({ L"NE Corner", bLabel, fmtCoord(maxLat), fmtCoord(maxLon) });
            ov.rows.push_back({ L"SE Corner", bLabel, fmtCoord(minLat), fmtCoord(maxLon) });
            ov.rows.push_back({ L"SW Close",  bLabel, fmtCoord(minLat), fmtCoord(minLon) });
        }
        if (hasCenter) {
            std::wstring cProp = (centerZoom >= 0) ? (L"Map Center (z" + std::to_wstring(centerZoom) + L")") : L"Map Center";
            ov.rows.push_back({ cProp, tsFormat.empty() ? L"MBTiles Center" : tsFormat, fmtCoord(centerLat), fmtCoord(centerLon) });
        }
        if (!tsName.empty())    ov.rows.push_back({ L"Tileset Name", tsName, L"", L"" });
        if (!tsFormat.empty())  ov.rows.push_back({ L"Tile Format", tsFormat, L"", L"" });
        if (!tsMinZoom.empty() || !tsMaxZoom.empty()) {
            ov.rows.push_back({ L"Zoom Range", L"z" + (tsMinZoom.empty() ? L"?" : tsMinZoom) + L" .. z" + (tsMaxZoom.empty() ? L"?" : tsMaxZoom), L"", L"" });
        } else if (obsMaxZ >= 0) {
            ov.rows.push_back({ L"Observed Zoom Range", L"z" + std::to_wstring(obsMinZ) + L" .. z" + std::to_wstring(obsMaxZ), L"", L"" });
        }
        if (!tsType.empty())    ov.rows.push_back({ L"Layer Type", tsType, L"", L"" });
        if (!tsVersion.empty()) ov.rows.push_back({ L"Tileset Version", tsVersion, L"", L"" });
        if (!tsDesc.empty())    ov.rows.push_back({ L"Description", tsDesc, L"", L"" });
        if (!tsAttrib.empty())  ov.rows.push_back({ L"Attribution", tsAttrib, L"", L"" });
        ov.rows.push_back({ L"Decoded Tile Rows", std::to_wstring(decodedTiles), L"", L"" });
        ov.rows.push_back({ L"SQLite Page Size", std::to_wstring(pageSz) + L" B", L"", L"" });
        ov.rows.push_back({ L"Database Pages", std::to_wstring(pageCnt), L"", L"" });

        out.sheets.push_back(std::move(ov));
        for (auto& sqlSh : sqlDoc.sheets) {
            out.sheets.push_back(std::move(sqlSh));
        }
        out.valid = true;
        return true;
    }

    return false;
}


} // namespace Formats
