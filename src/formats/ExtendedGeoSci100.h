#pragma once
// ExtendedGeoSci100.h — Binary Decoders for GIS Vector, Garmin FIT, NetCDF, WMO GRIB & NIfTI Volumes
// Covers: .shp/.shx, .gpkg, .fit (Garmin), .kmz, .nc/.cdf, .grib/.grib2/.grb, .nii/.nii.gz

namespace Formats {

namespace GeoSciDetail {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint16_t RdU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline int16_t RdI16LE(const uint8_t* p) { return static_cast<int16_t>(RdU16LE(p)); }
inline int16_t RdI16BE(const uint8_t* p) { return static_cast<int16_t>(RdU16BE(p)); }

inline uint32_t RdU24BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
}
inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t RdU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}
inline int32_t RdI32LE(const uint8_t* p) { return static_cast<int32_t>(RdU32LE(p)); }
inline int32_t RdI32BE(const uint8_t* p) { return static_cast<int32_t>(RdU32BE(p)); }

inline uint64_t RdU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(RdU32LE(p)) | (static_cast<uint64_t>(RdU32LE(p + 4)) << 32);
}
inline uint64_t RdU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(RdU32BE(p)) << 32) | static_cast<uint64_t>(RdU32BE(p + 4));
}
inline int64_t RdI64LE(const uint8_t* p) { return static_cast<int64_t>(RdU64LE(p)); }
inline int64_t RdI64BE(const uint8_t* p) { return static_cast<int64_t>(RdU64BE(p)); }

inline float RdF32LE(const uint8_t* p) { uint32_t u = RdU32LE(p); float f = 0.0f; std::memcpy(&f, &u, 4); return f; }
inline float RdF32BE(const uint8_t* p) { uint32_t u = RdU32BE(p); float f = 0.0f; std::memcpy(&f, &u, 4); return f; }
inline double RdF64LE(const uint8_t* p) { uint64_t u = RdU64LE(p); double d = 0.0; std::memcpy(&d, &u, 8); return d; }
inline double RdF64BE(const uint8_t* p) { uint64_t u = RdU64BE(p); double d = 0.0; std::memcpy(&d, &u, 8); return d; }

// WMO GRIB sign-magnitude integer decoders (MSB = sign bit, remaining bits = magnitude)
inline int16_t RdGribSignMag16(const uint8_t* p) {
    uint16_t u = RdU16BE(p);
    int16_t mag = static_cast<int16_t>(u & 0x7FFFu);
    return (u & 0x8000u) ? static_cast<int16_t>(-mag) : mag;
}
inline int32_t RdGribSignMag24(const uint8_t* p) {
    uint32_t u = RdU24BE(p);
    int32_t mag = static_cast<int32_t>(u & 0x7FFFFFu);
    return (u & 0x800000u) ? -mag : mag;
}
inline int32_t RdGribSignMag32(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    int32_t mag = static_cast<int32_t>(u & 0x7FFFFFFFu);
    return (u & 0x80000000u) ? -mag : mag;
}

// IBM System/360 32-bit hexadecimal floating-point (used by GRIB1 BDS reference value)
inline double RdIbmFloat32(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    if ((u & 0x7FFFFFFFu) == 0) return 0.0;
    double sign = (u & 0x80000000u) ? -1.0 : 1.0;
    int exp16 = static_cast<int>((u >> 24) & 0x7Fu) - 64;
    uint32_t mant = u & 0x00FFFFFFu;
    return sign * static_cast<double>(mant) * std::ldexp(1.0, 4 * exp16 - 24);
}

inline std::wstring FmtNum(double v, int prec = 6) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.*g", prec, v);
    return buf;
}

inline std::wstring FmtCoord(double v) {
    if (!std::isfinite(v)) return L"0";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.6f", v);
    return buf;
}

inline std::wstring FormatUtcEpoch(int64_t unixSec) {
    int64_t days = unixSec / 86400;
    int64_t rem  = unixSec % 86400;
    if (rem < 0) { rem += 86400; --days; }
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = static_cast<int64_t>(yoe) + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2 ? 1 : 0);
    int hr = static_cast<int>(rem / 3600);
    int mn = static_cast<int>((rem % 3600) / 60);
    int sc = static_cast<int>(rem % 60);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04lld-%02u-%02u %02d:%02d:%02d UTC", y, m, d, hr, mn, sc);
    return buf;
}

inline void FinalizeCols(SpreadsheetSheet& sh) {
    size_t mc = 0;
    for (const auto& r : sh.rows) mc = std::max(mc, r.size());
    sh.maxCols = mc;
}

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

// ============================================================================
// 2. Garmin / ANT+ FIT Telemetry Activity (.fit)
// ============================================================================
struct FitFieldDef {
    uint8_t num = 0;
    uint8_t size = 0;
    uint8_t baseType = 0;
};

struct FitLocalDef {
    bool active = false;
    bool bigEndian = false;
    uint16_t globalMsgNum = 0;
    std::vector<FitFieldDef> fields;
    std::vector<FitFieldDef> devFields;
};

inline bool ReadFitScalar(const uint8_t* p, uint8_t size, uint8_t baseType, bool be, int64_t& outInt, double& outDbl, bool& isSigned) {
    uint8_t bt = baseType & 0x1Fu;
    isSigned = false;
    outInt = 0;
    outDbl = 0.0;
    switch (bt) {
        case 0: // enum
        case 2: // uint8
        case 10: // uint8z
        case 13: // byte
            if (size < 1) return false;
            if ((bt == 0 || bt == 2 || bt == 13) && p[0] == 0xFFu) return false;
            if (bt == 10 && p[0] == 0x00u) return false;
            outInt = p[0]; outDbl = static_cast<double>(p[0]); return true;
        case 1: // sint8
            if (size < 1 || p[0] == 0x7Fu) return false;
            isSigned = true; outInt = static_cast<int8_t>(p[0]); outDbl = static_cast<double>(outInt); return true;
        case 3: // sint16
            if (size < 2) return false;
            { int16_t v = be ? RdI16BE(p) : RdI16LE(p); if (v == 0x7FFF) return false; isSigned = true; outInt = v; outDbl = v; return true; }
        case 4: // uint16
        case 11: // uint16z
            if (size < 2) return false;
            { uint16_t v = be ? RdU16BE(p) : RdU16LE(p); if ((bt == 4 && v == 0xFFFFu) || (bt == 11 && v == 0)) return false; outInt = v; outDbl = v; return true; }
        case 5: // sint32
            if (size < 4) return false;
            { int32_t v = be ? RdI32BE(p) : RdI32LE(p); if (v == 0x7FFFFFFF) return false; isSigned = true; outInt = v; outDbl = v; return true; }
        case 6: // uint32
        case 12: // uint32z
            if (size < 4) return false;
            { uint32_t v = be ? RdU32BE(p) : RdU32LE(p); if ((bt == 6 && v == 0xFFFFFFFFu) || (bt == 12 && v == 0)) return false; outInt = v; outDbl = v; return true; }
        case 8: // float32
            if (size < 4) return false;
            { float f = be ? RdF32BE(p) : RdF32LE(p); if (!std::isfinite(f)) return false; outDbl = f; outInt = static_cast<int64_t>(f); return true; }
        case 9: // float64
            if (size < 8) return false;
            { double d = be ? RdF64BE(p) : RdF64LE(p); if (!std::isfinite(d)) return false; outDbl = d; outInt = static_cast<int64_t>(d); return true; }
        case 14: // sint64
            if (size < 8) return false;
            { int64_t v = be ? RdI64BE(p) : RdI64LE(p); if (v == std::numeric_limits<int64_t>::max()) return false; isSigned = true; outInt = v; outDbl = static_cast<double>(v); return true; }
        case 15: // uint64
        case 16: // uint64z
            if (size < 8) return false;
            { uint64_t v = be ? RdU64BE(p) : RdU64LE(p); if ((bt == 15 && v == UINT64_MAX) || (bt == 16 && v == 0)) return false; outInt = static_cast<int64_t>(v); outDbl = static_cast<double>(v); return true; }
        default:
            return false;
    }
}

inline bool ParseGarminFit(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 14) return false;
    uint8_t hdrSize = raw[0];
    if ((hdrSize != 12 && hdrSize != 14) || hdrSize > raw.size()) return false;
    if (std::memcmp(raw.data() + 8, ".FIT", 4) != 0) return false;

    uint8_t protoVer = raw[1];
    uint16_t profileVer = RdU16LE(raw.data() + 2);
    uint32_t dataLen = RdU32LE(raw.data() + 4);
    size_t endPos = std::min<size_t>(raw.size(), static_cast<size_t>(hdrSize) + static_cast<size_t>(dataLen));

    FitLocalDef defs[16] = {};
    uint32_t lastTimestamp = 0;

    SpreadsheetSheet trackSh;
    trackSh.name = L"FIT_Track_Records";
    trackSh.rows.push_back({
        L"Timestamp (UTC)", L"Latitude", L"Longitude", L"Altitude (m)",
        L"Heart Rate (bpm)", L"Cadence (rpm)", L"Distance (m)", L"Speed (m/s)", L"Power (W)"
    });

    SpreadsheetSheet sumSh;
    sumSh.name = L"FIT_Activity_Summary";
    sumSh.rows.push_back({ L"Message / Metric", L"Field", L"Value" });
    sumSh.rows.push_back({ L"FIT Header", L"Protocol / Profile", std::to_wstring(protoVer >> 4) + L"." + std::to_wstring(protoVer & 0x0Fu) + L" / " + std::to_wstring(profileVer) });
    sumSh.rows.push_back({ L"FIT Header", L"Data Payload Size", PathUtils::FormatByteSize(dataLen) });

    constexpr int64_t kGarminEpochOffset = 631065600LL; // 1989-12-31 00:00:00 UTC
    constexpr double kSemicircleToDeg = 180.0 / 2147483648.0;

    size_t pos = hdrSize;
    size_t totalDataMsgs = 0;
    while (pos < endPos) {
        uint8_t hdr = raw[pos++];
        bool isCompTs = (hdr & 0x80u) != 0;
        if (!isCompTs && (hdr & 0x40u) != 0) {
            // Definition Message
            bool hasDev = (hdr & 0x20u) != 0;
            uint8_t ltype = hdr & 0x0Fu;
            if (pos + 5 > endPos) break;
            bool be = (raw[pos + 1] != 0);
            uint16_t gmsg = be ? RdU16BE(raw.data() + pos + 2) : RdU16LE(raw.data() + pos + 2);
            uint8_t nFields = raw[pos + 4];
            pos += 5;
            if (static_cast<size_t>(nFields) * 3 > endPos - pos) break;

            FitLocalDef d;
            d.active = true;
            d.bigEndian = be;
            d.globalMsgNum = gmsg;
            d.fields.resize(nFields);
            for (uint8_t i = 0; i < nFields; ++i) {
                d.fields[i] = { raw[pos], raw[pos + 1], raw[pos + 2] };
                pos += 3;
            }
            if (hasDev) {
                if (pos >= endPos) break;
                uint8_t nDev = raw[pos++];
                if (static_cast<size_t>(nDev) * 3 > endPos - pos) break;
                d.devFields.resize(nDev);
                for (uint8_t i = 0; i < nDev; ++i) {
                    d.devFields[i] = { raw[pos], raw[pos + 1], raw[pos + 2] };
                    pos += 3;
                }
            }
            defs[ltype] = std::move(d);
            continue;
        }

        // Data Message (Normal or Compressed Timestamp)
        uint8_t ltype = isCompTs ? ((hdr >> 5) & 0x03u) : (hdr & 0x0Fu);
        if (isCompTs) {
            uint32_t offs = hdr & 0x1Fu;
            if (offs >= (lastTimestamp & 0x1Fu)) lastTimestamp = (lastTimestamp & ~0x1Fu) + offs;
            else lastTimestamp = (lastTimestamp & ~0x1Fu) + offs + 0x20u;
        }
        const FitLocalDef& def = defs[ltype];
        if (!def.active) break;

        size_t msgBytes = 0;
        for (const auto& f : def.fields) msgBytes += f.size;
        for (const auto& df : def.devFields) msgBytes += df.size;
        if (msgBytes > endPos - pos) break;

        const uint8_t* fp = raw.data() + pos;
        pos += msgBytes;
        ++totalDataMsgs;

        if (def.globalMsgNum == 20) { // record
            uint32_t ts = lastTimestamp;
            bool hasTs = isCompTs, hasLat = false, hasLon = false, hasAlt = false;
            double lat = 0.0, lon = 0.0, altM = 0.0;
            std::wstring hrStr, cadStr, distStr, spdStr, pwrStr;

            for (const auto& f : def.fields) {
                int64_t iv = 0; double dv = 0.0; bool sgn = false;
                if (ReadFitScalar(fp, f.size, f.baseType, def.bigEndian, iv, dv, sgn)) {
                    switch (f.num) {
                        case 253: ts = static_cast<uint32_t>(iv); lastTimestamp = ts; hasTs = true; break;
                        case 0:   lat = static_cast<double>(static_cast<int32_t>(iv)) * kSemicircleToDeg; hasLat = true; break;
                        case 1:   lon = static_cast<double>(static_cast<int32_t>(iv)) * kSemicircleToDeg; hasLon = true; break;
                        case 2:   if (!hasAlt) { altM = dv / 5.0 - 500.0; hasAlt = true; } break;
                        case 78:  altM = dv / 5.0 - 500.0; hasAlt = true; break; // enhanced_altitude
                        case 3:   hrStr = std::to_wstring(iv); break;
                        case 4:   cadStr = std::to_wstring(iv); break;
                        case 5:   distStr = FmtNum(dv / 100.0, 6); break;
                        case 6:   if (spdStr.empty()) spdStr = FmtNum(dv / 1000.0, 4); break;
                        case 73:  spdStr = FmtNum(dv / 1000.0, 4); break; // enhanced_speed
                        case 7:   pwrStr = std::to_wstring(iv); break;
                    }
                }
                fp += f.size;
            }

            std::wstring tsStr = hasTs ? FormatUtcEpoch(static_cast<int64_t>(ts) + kGarminEpochOffset) : L"";
            int rowIdx = static_cast<int>(trackSh.rows.size());
            if (hasLat && hasLon && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0 && out.geoPoints.size() < 20000) {
                GpsTrackPoint gp;
                gp.lat = lat;
                gp.lon = lon;
                gp.elevM = hasAlt ? static_cast<float>(altM) : 0.0f;
                gp.rowIndex = rowIdx;
                gp.label = tsStr + (hrStr.empty() ? L"" : (L" | " + hrStr + L" bpm"));
                out.geoPoints.push_back(std::move(gp));
            }
            if (trackSh.rows.size() <= 2000) {
                trackSh.rows.push_back({
                    tsStr,
                    hasLat ? FmtCoord(lat) : L"",
                    hasLon ? FmtCoord(lon) : L"",
                    hasAlt ? FmtNum(altM, 5) : L"",
                    hrStr, cadStr, distStr, spdStr, pwrStr
                });
            }
        } else if (def.globalMsgNum == 18 || def.globalMsgNum == 19 || def.globalMsgNum == 0) { // session / lap / file_id
            std::wstring msgLbl = (def.globalMsgNum == 18) ? L"Session" : (def.globalMsgNum == 19 ? L"Lap" : L"File ID");
            for (const auto& f : def.fields) {
                int64_t iv = 0; double dv = 0.0; bool sgn = false;
                if (sumSh.rows.size() < 200 && ReadFitScalar(fp, f.size, f.baseType, def.bigEndian, iv, dv, sgn)) {
                    if (def.globalMsgNum == 0 && f.num == 1) sumSh.rows.push_back({ msgLbl, L"Manufacturer ID", std::to_wstring(iv) });
                    else if (def.globalMsgNum == 0 && f.num == 4) sumSh.rows.push_back({ msgLbl, L"Time Created", FormatUtcEpoch(iv + kGarminEpochOffset) });
                    else if (def.globalMsgNum == 18 && f.num == 5) sumSh.rows.push_back({ msgLbl, L"Sport Enum", std::to_wstring(iv) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 7) sumSh.rows.push_back({ msgLbl, L"Total Elapsed Time (s)", FmtNum(dv / 1000.0) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 9) sumSh.rows.push_back({ msgLbl, L"Total Distance (m)", FmtNum(dv / 100.0) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 16) sumSh.rows.push_back({ msgLbl, L"Avg Heart Rate (bpm)", std::to_wstring(iv) });
                    else if ((def.globalMsgNum == 18 || def.globalMsgNum == 19) && f.num == 17) sumSh.rows.push_back({ msgLbl, L"Max Heart Rate (bpm)", std::to_wstring(iv) });
                }
                fp += f.size;
            }
        }
    }

    sumSh.rows.push_back({ L"FIT Stream", L"Decoded Data Messages", std::to_wstring(totalDataMsgs) });
    FinalizeCols(trackSh);
    FinalizeCols(sumSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Garmin FIT Telemetry Activity (.fit)";
    if (trackSh.rows.size() > 1) {
        out.sheets.push_back(std::move(trackSh));
        out.sheets.push_back(std::move(sumSh));
    } else {
        out.sheets.push_back(std::move(sumSh));
    }
    out.valid = true;
    return true;
}

// ============================================================================
// 3. NetCDF Classic (CDF\x01, CDF\x02, CDF\x05) & NetCDF-4 (HDF5)
// ============================================================================
struct NcReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    uint8_t ver = 1; // 1=32bit, 2=64bit-offset, 5=64bit-data

    bool RdU32(uint32_t& v) {
        if (pos + 4 > size) return false;
        v = RdU32BE(data + pos);
        pos += 4;
        return true;
    }
    bool RdNonNeg(uint64_t& v) {
        if (ver == 5) {
            if (pos + 8 > size) return false;
            v = RdU64BE(data + pos);
            pos += 8;
            return true;
        }
        uint32_t u = 0;
        if (!RdU32(u)) return false;
        v = u;
        return true;
    }
    bool RdOffset(uint64_t& v) {
        if (ver == 2 || ver == 5) {
            if (pos + 8 > size) return false;
            v = RdU64BE(data + pos);
            pos += 8;
            return true;
        }
        uint32_t u = 0;
        if (!RdU32(u)) return false;
        v = u;
        return true;
    }
    bool RdName(std::string& out) {
        uint64_t n = 0;
        if (!RdNonNeg(n) || n > size - pos || n > 4096) return false;
        out.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(n));
        size_t padded = (static_cast<size_t>(n) + 3u) & ~size_t(3u);
        if (padded > size - pos) return false;
        pos += padded;
        return true;
    }
};

inline size_t NcTypeSize(uint32_t ncType) {
    switch (ncType) {
        case 1: case 2: case 7: return 1; // BYTE, CHAR, UBYTE
        case 3: case 8:         return 2; // SHORT, USHORT
        case 4: case 5: case 9: return 4; // INT, FLOAT, UINT
        case 6: case 10: case 11: return 8; // DOUBLE, INT64, UINT64
        default: return 0;
    }
}

inline const wchar_t* NcTypeName(uint32_t ncType) {
    switch (ncType) {
        case 1: return L"byte";   case 2: return L"char";   case 3: return L"short";
        case 4: return L"int";    case 5: return L"float";  case 6: return L"double";
        case 7: return L"ubyte";  case 8: return L"ushort"; case 9: return L"uint";
        case 10: return L"int64"; case 11: return L"uint64";
        default: return L"unknown";
    }
}

inline bool ReadNcAttrList(NcReader& r, std::vector<std::pair<std::string, std::wstring>>& outAttrs) {
    uint32_t tag = 0;
    uint64_t nelems = 0;
    if (!r.RdU32(tag) || !r.RdNonNeg(nelems)) return false;
    if (tag == 0 && nelems == 0) return true;
    if (tag != 0x0000000Cu || nelems > 4096) return false;

    for (uint64_t i = 0; i < nelems; ++i) {
        std::string name;
        uint32_t ncType = 0;
        uint64_t nvals = 0;
        if (!r.RdName(name) || !r.RdU32(ncType) || !r.RdNonNeg(nvals)) return false;
        size_t esz = NcTypeSize(ncType);
        if (esz == 0 || nvals > (r.size - r.pos) / esz) return false;
        size_t rawBytes = static_cast<size_t>(nvals) * esz;
        size_t padded = (rawBytes + 3u) & ~size_t(3u);
        if (padded > r.size - r.pos) return false;

        const uint8_t* vp = r.data + r.pos;
        std::wstring valStr;
        if (ncType == 2) {
            std::string s(reinterpret_cast<const char*>(vp), rawBytes);
            while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
            valStr = PathUtils::Utf8ToWide(s);
        } else {
            for (uint64_t k = 0; k < std::min<uint64_t>(nvals, 6); ++k) {
                if (k > 0) valStr += L", ";
                const uint8_t* p = vp + static_cast<size_t>(k) * esz;
                if (ncType == 1) valStr += std::to_wstring(static_cast<int8_t>(p[0]));
                else if (ncType == 3) valStr += std::to_wstring(RdI16BE(p));
                else if (ncType == 4) valStr += std::to_wstring(RdI32BE(p));
                else if (ncType == 5) valStr += FmtNum(RdF32BE(p));
                else if (ncType == 6) valStr += FmtNum(RdF64BE(p));
                else valStr += std::to_wstring(p[0]);
            }
        }
        outAttrs.emplace_back(std::move(name), std::move(valStr));
        r.pos += padded;
    }
    return true;
}

inline bool ParseNetCdf(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // NetCDF-4 uses HDF5 container
    if ((raw.size() >= 36 && std::memcmp(raw.data(), "\x89HDF\r\n\x1A\n", 8) == 0) ||
        (raw.size() >= 548 && std::memcmp(raw.data() + 512, "\x89HDF\r\n\x1A\n", 8) == 0))
    {
        return ParseHdf5Doc(raw, extLower, out);
    }

    if (raw.size() < 16 || std::memcmp(raw.data(), "CDF", 3) != 0) return false;
    uint8_t ver = raw[3];
    if (ver != 1 && ver != 2 && ver != 5) return false;

    NcReader r{ raw.data(), raw.size(), 4, ver };
    uint64_t numRecs = 0;
    if (!r.RdNonNeg(numRecs)) return false;
    if (numRecs == 0xFFFFFFFFULL || numRecs == UINT64_MAX) numRecs = 0;

    // 1. Dimensions
    struct NcDim { std::string name; uint64_t len = 0; bool isUnlimited = false; };
    std::vector<NcDim> dims;
    int recDimId = -1;
    {
        uint32_t tag = 0; uint64_t nd = 0;
        if (!r.RdU32(tag) || !r.RdNonNeg(nd)) return false;
        if (tag == 0x0000000Au) {
            if (nd > 4096) return false;
            for (uint64_t i = 0; i < nd; ++i) {
                NcDim d;
                if (!r.RdName(d.name) || !r.RdNonNeg(d.len)) return false;
                if (d.len == 0) { d.isUnlimited = true; d.len = numRecs; recDimId = static_cast<int>(i); }
                dims.push_back(std::move(d));
            }
        } else if (tag != 0 || nd != 0) {
            return false;
        }
    }

    // 2. Global Attributes
    std::vector<std::pair<std::string, std::wstring>> globalAttrs;
    if (!ReadNcAttrList(r, globalAttrs)) return false;

    // 3. Variables
    struct NcVar {
        std::string name;
        std::vector<uint64_t> dimIds;
        std::vector<std::pair<std::string, std::wstring>> attrs;
        uint32_t ncType = 0;
        uint64_t vsize = 0;
        uint64_t begin = 0;
        bool isRecordVar = false;
    };
    std::vector<NcVar> vars;
    {
        uint32_t tag = 0; uint64_t nv = 0;
        if (!r.RdU32(tag) || !r.RdNonNeg(nv)) return false;
        if (tag == 0x0000000Bu) {
            if (nv > 4096) return false;
            for (uint64_t i = 0; i < nv; ++i) {
                NcVar v;
                uint64_t vndims = 0;
                if (!r.RdName(v.name) || !r.RdNonNeg(vndims) || vndims > 64) return false;
                for (uint64_t d = 0; d < vndims; ++d) {
                    uint64_t did = 0;
                    if (!r.RdNonNeg(did)) return false;
                    v.dimIds.push_back(did);
                }
                if (!v.dimIds.empty() && recDimId >= 0 && v.dimIds[0] == static_cast<uint64_t>(recDimId)) {
                    v.isRecordVar = true;
                }
                if (!ReadNcAttrList(r, v.attrs)) return false;
                if (!r.RdU32(v.ncType) || !r.RdNonNeg(v.vsize) || !r.RdOffset(v.begin)) return false;
                if (NcTypeSize(v.ncType) == 0) return false;
                vars.push_back(std::move(v));
            }
        } else if (tag != 0 || nv != 0) {
            return false;
        }
    }

    uint64_t recSlabSize = 0;
    for (const auto& v : vars) {
        if (v.isRecordVar) recSlabSize += v.vsize;
    }

    auto readVarDoubles = [&](const NcVar& v, size_t maxCount, std::vector<double>& outVals) {
        outVals.clear();
        size_t esz = NcTypeSize(v.ncType);
        if (esz == 0 || v.ncType == 2) return;
        uint64_t elemCnt = 1;
        for (uint64_t did : v.dimIds) {
            if (did < dims.size()) elemCnt *= dims[static_cast<size_t>(did)].len;
        }
        size_t want = static_cast<size_t>(std::min<uint64_t>(elemCnt, maxCount));
        if (want == 0) return;

        auto rdOne = [&](size_t byteOff) -> double {
            const uint8_t* p = raw.data() + byteOff;
            switch (v.ncType) {
                case 1:  return static_cast<int8_t>(p[0]);
                case 3:  return RdI16BE(p);
                case 4:  return RdI32BE(p);
                case 5:  return RdF32BE(p);
                case 6:  return RdF64BE(p);
                case 7:  return p[0];
                case 8:  return RdU16BE(p);
                case 9:  return RdU32BE(p);
                case 10: return static_cast<double>(RdI64BE(p));
                case 11: return static_cast<double>(RdU64BE(p));
                default: return 0.0;
            }
        };

        if (!v.isRecordVar) {
            for (size_t i = 0; i < want && v.begin + (i + 1) * esz <= raw.size(); ++i) {
                outVals.push_back(rdOne(static_cast<size_t>(v.begin) + i * esz));
            }
        } else {
            uint64_t perRec = 1;
            for (size_t d = 1; d < v.dimIds.size(); ++d) {
                if (v.dimIds[d] < dims.size()) perRec *= dims[static_cast<size_t>(v.dimIds[d])].len;
            }
            for (uint64_t rec = 0; rec < numRecs && outVals.size() < want; ++rec) {
                uint64_t recBase = v.begin + rec * recSlabSize;
                for (uint64_t j = 0; j < perRec && outVals.size() < want; ++j) {
                    uint64_t boff = recBase + j * esz;
                    if (boff + esz > raw.size()) return;
                    outVals.push_back(rdOne(static_cast<size_t>(boff)));
                }
            }
        }
    };

    SpreadsheetSheet schemaSh;
    schemaSh.name = L"NetCDF_Variables";
    schemaSh.rows.push_back({ L"Variable / Attribute", L"Type", L"Dimensions", L"Units / Value", L"Statistics / Preview" });
    schemaSh.rows.push_back({ L"(format)", ver == 5 ? L"CDF-5 (64-bit data)" : (ver == 2 ? L"CDF-2 (64-bit offset)" : L"CDF-1 (Classic)"),
        L"numrecs=" + std::to_wstring(numRecs), L"dims=" + std::to_wstring(dims.size()), L"vars=" + std::to_wstring(vars.size()) });

    for (const auto& ga : globalAttrs) {
        schemaSh.rows.push_back({ L"@" + PathUtils::Utf8ToWide(ga.first), L"Global Attr", L"", ga.second, L"" });
    }

    int latVarIdx = -1, lonVarIdx = -1;
    std::vector<std::vector<double>> varSamples(vars.size());

    for (size_t i = 0; i < vars.size(); ++i) {
        const auto& v = vars[i];
        std::string low = PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"lat" || PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"latitude" ? "lat" :
                          (PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"lon" || PathUtils::ToLower(PathUtils::Utf8ToWide(v.name)) == L"longitude" ? "lon" : "");
        if (low == "lat") latVarIdx = static_cast<int>(i);
        if (low == "lon") lonVarIdx = static_cast<int>(i);

        std::wstring dstr = L"(";
        for (size_t d = 0; d < v.dimIds.size(); ++d) {
            if (d > 0) dstr += L", ";
            uint64_t did = v.dimIds[d];
            if (did < dims.size()) dstr += PathUtils::Utf8ToWide(dims[static_cast<size_t>(did)].name) + L"=" + std::to_wstring(dims[static_cast<size_t>(did)].len);
            else dstr += std::to_wstring(did);
        }
        dstr += L")";

        std::wstring units;
        for (const auto& va : v.attrs) {
            if (va.first == "units") units = va.second;
            else if (va.first == "long_name" && units.empty()) units = va.second;
        }

        readVarDoubles(v, 512, varSamples[i]);
        std::wstring statStr;
        if (!varSamples[i].empty()) {
            double mn = varSamples[i][0], mx = varSamples[i][0];
            for (double dv : varSamples[i]) {
                if (std::isfinite(dv)) { mn = std::min(mn, dv); mx = std::max(mx, dv); }
            }
            statStr = L"min=" + FmtNum(mn) + L", max=" + FmtNum(mx) + L" [e.g. " + FmtNum(varSamples[i][0]) + L"]";
        }
        schemaSh.rows.push_back({ PathUtils::Utf8ToWide(v.name), NcTypeName(v.ncType), dstr, units, statStr });
    }
    FinalizeCols(schemaSh);

    // Populate geoPoints if lat & lon arrays exist
    if (latVarIdx >= 0 && lonVarIdx >= 0) {
        const auto& lats = varSamples[static_cast<size_t>(latVarIdx)];
        const auto& lons = varSamples[static_cast<size_t>(lonVarIdx)];
        if (lats.size() == lons.size()) {
            for (size_t i = 0; i < lats.size() && out.geoPoints.size() < 5000; ++i) {
                double lonNorm = (lons[i] > 180.0 && lons[i] <= 360.0) ? (lons[i] - 360.0) : lons[i];
                if (std::fabs(lats[i]) <= 90.0 && std::fabs(lonNorm) <= 180.0) {
                    GpsTrackPoint gp; gp.lat = lats[i]; gp.lon = lonNorm; gp.rowIndex = static_cast<int>(i + 1);
                    out.geoPoints.push_back(std::move(gp));
                }
            }
        }
    }

    SpreadsheetSheet dataSh;
    dataSh.name = L"NetCDF_Data_Preview";
    std::vector<std::wstring> dhdr = { L"Index" };
    size_t maxLen = 0;
    for (size_t i = 0; i < vars.size() && dhdr.size() <= 32; ++i) {
        if (!varSamples[i].empty()) {
            dhdr.push_back(PathUtils::Utf8ToWide(vars[i].name));
            maxLen = std::max(maxLen, varSamples[i].size());
        }
    }
    if (maxLen > 0) {
        dataSh.rows.push_back(std::move(dhdr));
        for (size_t idx = 0; idx < std::min<size_t>(maxLen, 500); ++idx) {
            std::vector<std::wstring> row = { std::to_wstring(idx) };
            for (size_t i = 0; i < vars.size() && row.size() <= 32; ++i) {
                if (varSamples[i].empty()) continue;
                row.push_back(idx < varSamples[i].size() ? FmtNum(varSamples[i][idx]) : L"");
            }
            dataSh.rows.push_back(std::move(row));
        }
        FinalizeCols(dataSh);
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"NetCDF Scientific Array Dataset (" + extLower + L")";
    out.sheets.push_back(std::move(schemaSh));
    if (dataSh.rows.size() > 1) out.sheets.push_back(std::move(dataSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 4. WMO GRIB Edition 1 & Edition 2 Meteorological Grids (.grib / .grib2 / .grb)
// ============================================================================
inline uint32_t ReadBitsBE(const uint8_t* buf, size_t bufBytes, size_t& bitPos, uint8_t nBits) {
    if (nBits == 0) return 0;
    uint32_t val = 0;
    for (uint8_t b = 0; b < nBits; ++b) {
        size_t byteIdx = bitPos >> 3;
        if (byteIdx >= bufBytes) return val;
        uint8_t bitVal = (buf[byteIdx] >> (7 - (bitPos & 7))) & 1u;
        val = (val << 1) | bitVal;
        ++bitPos;
    }
    return val;
}

inline std::wstring Grib2ParamName(uint8_t disc, uint8_t cat, uint8_t num) {
    if (disc == 0) {
        if (cat == 0 && num == 0) return L"Temperature (K)";
        if (cat == 0 && num == 2) return L"Potential Temperature (K)";
        if (cat == 1 && num == 0) return L"Specific Humidity (kg/kg)";
        if (cat == 1 && num == 1) return L"Relative Humidity (%)";
        if (cat == 1 && num == 8) return L"Total Precipitation (kg/m2)";
        if (cat == 2 && num == 2) return L"U-Component of Wind (m/s)";
        if (cat == 2 && num == 3) return L"V-Component of Wind (m/s)";
        if (cat == 3 && num == 0) return L"Pressure (Pa)";
        if (cat == 3 && num == 1) return L"Pressure Reduced to MSL (Pa)";
        if (cat == 3 && num == 5) return L"Geopotential Height (gpm)";
    }
    return L"D" + std::to_wstring(disc) + L":C" + std::to_wstring(cat) + L":P" + std::to_wstring(num);
}

inline bool ParseWmoGrib(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16) return false;

    SpreadsheetSheet msgSh;
    msgSh.name = L"GRIB_Messages";
    msgSh.rows.push_back({
        L"Msg #", L"Edition", L"Reference Time (UTC)", L"Parameter",
        L"Grid (Ni x Nj)", L"Domain Bounds", L"Min", L"Max", L"Mean"
    });

    SpreadsheetSheet gridSh;
    gridSh.name = L"GRIB_Grid_Preview";

    size_t pos = 0;
    size_t msgCount = 0;
    while (pos + 16 <= raw.size() && msgCount < 256) {
        // Locate "GRIB" indicator
        if (std::memcmp(raw.data() + pos, "GRIB", 4) != 0) {
            bool foundNext = false;
            for (size_t scan = pos + 1; scan + 16 <= raw.size() && scan < pos + 4096; ++scan) {
                if (std::memcmp(raw.data() + scan, "GRIB", 4) == 0 && (raw[scan + 7] == 1 || raw[scan + 7] == 2)) {
                    pos = scan;
                    foundNext = true;
                    break;
                }
            }
            if (!foundNext) break;
        }

        uint8_t edition = raw[pos + 7];
        if (edition == 2) {
            uint8_t discipline = raw[pos + 6];
            uint64_t msgLen = RdU64BE(raw.data() + pos + 8);
            if (msgLen < 20 || msgLen > raw.size() - pos) break;
            size_t msgEnd = pos + static_cast<size_t>(msgLen);
            if (std::memcmp(raw.data() + msgEnd - 4, "7777", 4) != 0) break;

            uint16_t year = 0; uint8_t month = 0, day = 0, hour = 0, minute = 0;
            uint32_t ni = 0, nj = 0, numVals = 0;
            double la1 = 0.0, lo1 = 0.0, la2 = 0.0, lo2 = 0.0;
            bool hasBounds = false;
            uint8_t pCat = 255, pNum = 255;
            uint16_t reprTmpl = 0xFFFFu;
            double refR = 0.0;
            int16_t binE = 0, decD = 0;
            uint8_t bitsPerVal = 0;
            std::vector<double> unpacked;

            size_t spos = pos + 16;
            while (spos + 5 <= msgEnd - 4) {
                uint32_t secLen = RdU32BE(raw.data() + spos);
                uint8_t secNum  = raw[spos + 4];
                if (secLen < 5 || secLen > (msgEnd - 4) - spos) break;
                const uint8_t* sp = raw.data() + spos;

                if (secNum == 1 && secLen >= 19) {
                    year = RdU16BE(sp + 12); month = sp[14]; day = sp[15]; hour = sp[16]; minute = sp[17];
                } else if (secNum == 3 && secLen >= 14) {
                    numVals = RdU32BE(sp + 6);
                    uint16_t gtmpl = RdU16BE(sp + 12);
                    if (gtmpl == 0 && secLen >= 63) {
                        ni = RdU32BE(sp + 30);
                        nj = RdU32BE(sp + 34);
                        la1 = RdGribSignMag32(sp + 46) / 1e6;
                        lo1 = RdGribSignMag32(sp + 50) / 1e6;
                        la2 = RdGribSignMag32(sp + 55) / 1e6;
                        lo2 = RdGribSignMag32(sp + 59) / 1e6;
                        hasBounds = true;
                    }
                } else if (secNum == 4 && secLen >= 11) {
                    pCat = sp[9];
                    pNum = sp[10];
                } else if (secNum == 5 && secLen >= 11) {
                    numVals = RdU32BE(sp + 5);
                    reprTmpl = RdU16BE(sp + 9);
                    if (reprTmpl == 0 && secLen >= 20) {
                        refR = RdF32BE(sp + 11);
                        binE = RdGribSignMag16(sp + 15);
                        decD = RdGribSignMag16(sp + 17);
                        bitsPerVal = sp[19];
                    }
                } else if (secNum == 7 && reprTmpl == 0 && bitsPerVal <= 32) {
                    double scale2E = std::ldexp(1.0, binE);
                    double inv10D  = std::pow(10.0, -static_cast<double>(decD));
                    size_t cap = std::min<size_t>(numVals > 0 ? numVals : 1, 20000);
                    size_t bitPos = 0;
                    size_t avail = secLen - 5;
                    for (size_t i = 0; i < cap; ++i) {
                        if (bitsPerVal > 0 && (bitPos + bitsPerVal) > avail * 8) break;
                        uint32_t x = ReadBitsBE(sp + 5, avail, bitPos, bitsPerVal);
                        unpacked.push_back((refR + static_cast<double>(x) * scale2E) * inv10D);
                    }
                }
                spos += secLen;
            }

            ++msgCount;
            wchar_t tBuf[64] = {};
            swprintf_s(tBuf, L"%04u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
            std::wstring bStr = hasBounds ? (FmtNum(la1, 4) + L"," + FmtNum(lo1, 4) + L" .. " + FmtNum(la2, 4) + L"," + FmtNum(lo2, 4)) : L"";
            std::wstring mnS, mxS, avS;
            if (!unpacked.empty()) {
                double mn = unpacked[0], mx = unpacked[0], sm = 0.0;
                for (double v : unpacked) { mn = std::min(mn, v); mx = std::max(mx, v); sm += v; }
                mnS = FmtNum(mn); mxS = FmtNum(mx); avS = FmtNum(sm / static_cast<double>(unpacked.size()));

                if (gridSh.rows.empty()) {
                    uint32_t cols = (ni > 0 && nj > 0) ? std::min<uint32_t>(ni, 32) : 1;
                    uint32_t rows = (ni > 0 && nj > 0) ? std::min<uint32_t>(nj, 128) : static_cast<uint32_t>(std::min<size_t>(unpacked.size(), 256));
                    if (cols > 1) {
                        std::vector<std::wstring> hr = { L"Lat \\ LonIdx" };
                        for (uint32_t c = 0; c < cols; ++c) hr.push_back(L"[" + std::to_wstring(c) + L"]");
                        gridSh.rows.push_back(std::move(hr));
                        for (uint32_t r = 0; r < rows; ++r) {
                            std::vector<std::wstring> rv = { L"Row " + std::to_wstring(r) };
                            for (uint32_t c = 0; c < cols; ++c) {
                                size_t idx = static_cast<size_t>(r) * ni + c;
                                rv.push_back(idx < unpacked.size() ? FmtNum(unpacked[idx]) : L"");
                            }
                            gridSh.rows.push_back(std::move(rv));
                        }
                    } else {
                        gridSh.rows.push_back({ L"Grid Point #", Grib2ParamName(discipline, pCat, pNum) });
                        for (size_t i = 0; i < std::min<size_t>(unpacked.size(), 256); ++i) {
                            gridSh.rows.push_back({ std::to_wstring(i), FmtNum(unpacked[i]) });
                        }
                    }
                    FinalizeCols(gridSh);
                }
            }
            msgSh.rows.push_back({
                std::to_wstring(msgCount), L"GRIB2", tBuf, Grib2ParamName(discipline, pCat, pNum),
                (ni > 0 && nj > 0) ? (std::to_wstring(ni) + L"x" + std::to_wstring(nj)) : std::to_wstring(numVals),
                bStr, mnS, mxS, avS
            });
            pos = msgEnd;
        } else if (edition == 1) {
            uint32_t msgLen = RdU24BE(raw.data() + pos + 4);
            if (msgLen < 40 || msgLen > raw.size() - pos) break;
            size_t msgEnd = pos + msgLen;
            if (std::memcmp(raw.data() + msgEnd - 4, "7777", 4) != 0) break;

            size_t spos = pos + 8;
            if (spos + 28 > msgEnd - 4) break;
            uint32_t pdsLen = RdU24BE(raw.data() + spos);
            if (pdsLen < 28 || pdsLen > (msgEnd - 4) - spos) break;
            const uint8_t* pds = raw.data() + spos;
            uint8_t flags = pds[7];
            uint8_t paramId = pds[8];
            uint16_t year = static_cast<uint16_t>((pds[24] > 0 ? (pds[24] - 1) * 100 : 1900) + pds[12]);
            uint8_t month = pds[13], day = pds[14], hour = pds[15], minute = pds[16];
            int16_t decD = RdGribSignMag16(pds + 26);
            spos += pdsLen;

            uint16_t ni = 0, nj = 0;
            if ((flags & 0x80u) != 0 && spos + 16 <= msgEnd - 4) { // GDS
                uint32_t gdsLen = RdU24BE(raw.data() + spos);
                if (gdsLen < 6 || gdsLen > (msgEnd - 4) - spos) break;
                if (gdsLen >= 10) {
                    ni = RdU16BE(raw.data() + spos + 6);
                    nj = RdU16BE(raw.data() + spos + 8);
                }
                spos += gdsLen;
            }
            if ((flags & 0x40u) != 0 && spos + 6 <= msgEnd - 4) { // BMS
                uint32_t bmsLen = RdU24BE(raw.data() + spos);
                if (bmsLen < 6 || bmsLen > (msgEnd - 4) - spos) break;
                spos += bmsLen;
            }

            std::vector<double> unpacked;
            if (spos + 11 <= msgEnd - 4) { // BDS
                uint32_t bdsLen = RdU24BE(raw.data() + spos);
                if (bdsLen >= 11 && bdsLen <= (msgEnd - 4) - spos) {
                    const uint8_t* bds = raw.data() + spos;
                    int16_t binE = RdGribSignMag16(bds + 4);
                    double refR  = RdIbmFloat32(bds + 6);
                    uint8_t bpv  = bds[10];
                    double scale2E = std::ldexp(1.0, binE);
                    double inv10D  = std::pow(10.0, -static_cast<double>(decD));
                    size_t avail = bdsLen - 11;
                    size_t nPts = (ni > 0 && nj > 0) ? std::min<size_t>(static_cast<size_t>(ni) * nj, 20000)
                                                     : (bpv > 0 ? std::min<size_t>((avail * 8) / bpv, 20000) : 1);
                    size_t bitPos = 0;
                    for (size_t i = 0; i < nPts && bpv <= 32; ++i) {
                        if (bpv > 0 && bitPos + bpv > avail * 8) break;
                        uint32_t x = ReadBitsBE(bds + 11, avail, bitPos, bpv);
                        unpacked.push_back((refR + static_cast<double>(x) * scale2E) * inv10D);
                    }
                }
            }

            ++msgCount;
            wchar_t tBuf[64] = {};
            swprintf_s(tBuf, L"%04u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
            std::wstring mnS, mxS, avS;
            if (!unpacked.empty()) {
                double mn = unpacked[0], mx = unpacked[0], sm = 0.0;
                for (double v : unpacked) { mn = std::min(mn, v); mx = std::max(mx, v); sm += v; }
                mnS = FmtNum(mn); mxS = FmtNum(mx); avS = FmtNum(sm / static_cast<double>(unpacked.size()));
            }
            msgSh.rows.push_back({
                std::to_wstring(msgCount), L"GRIB1", tBuf, L"Param #" + std::to_wstring(paramId),
                (ni > 0 && nj > 0) ? (std::to_wstring(ni) + L"x" + std::to_wstring(nj)) : L"",
                L"", mnS, mxS, avS
            });
            pos = msgEnd;
        } else {
            break;
        }
    }

    if (msgCount == 0) return false;
    FinalizeCols(msgSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"WMO GRIB Meteorological Forecast Grid";
    out.sheets.push_back(std::move(msgSh));
    if (!gridSh.rows.empty()) out.sheets.push_back(std::move(gridSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 5. NIfTI-1 & NIfTI-2 3D/4D Neuroimaging Volume (.nii / .nii.gz)
// ============================================================================
inline const wchar_t* NiftiDatatypeName(int16_t dt) {
    switch (dt) {
        case 2:    return L"UINT8";
        case 4:    return L"INT16";
        case 8:    return L"INT32";
        case 16:   return L"FLOAT32";
        case 32:   return L"COMPLEX64";
        case 64:   return L"FLOAT64";
        case 128:  return L"RGB24";
        case 256:  return L"INT8";
        case 512:  return L"UINT16";
        case 768:  return L"UINT32";
        case 1024: return L"INT64";
        case 1280: return L"UINT64";
        default:   return L"VOXEL";
    }
}

inline bool ParseNiftiVolume(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> uncomp;
    const uint8_t* ptr = raw.data();
    size_t len = raw.size();
    bool wasGzip = false;

    if (len >= 10 && ptr[0] == 0x1Fu && ptr[1] == 0x8Bu) {
        if (!ZipDeflate::InflateGzip(ptr, len, uncomp, 128 * 1024 * 1024) || uncomp.empty()) return false;
        ptr = uncomp.data();
        len = uncomp.size();
        wasGzip = true;
    }
    if (len < 348) return false;

    int32_t hdrLE = RdI32LE(ptr);
    int32_t hdrBE = RdI32BE(ptr);
    bool nifti2 = false;
    bool be = false;
    if (hdrLE == 348) { nifti2 = false; be = false; }
    else if (hdrBE == 348) { nifti2 = false; be = true; }
    else if (hdrLE == 540 && len >= 540) { nifti2 = true; be = false; }
    else if (hdrBE == 540 && len >= 540) { nifti2 = true; be = true; }
    else return false;

    auto rdI16 = [&](size_t o) { return be ? RdI16BE(ptr + o) : RdI16LE(ptr + o); };
    auto rdI32 = [&](size_t o) { return be ? RdI32BE(ptr + o) : RdI32LE(ptr + o); };
    auto rdI64 = [&](size_t o) { return be ? RdI64BE(ptr + o) : RdI64LE(ptr + o); };
    auto rdF32 = [&](size_t o) { return be ? RdF32BE(ptr + o) : RdF32LE(ptr + o); };
    auto rdF64 = [&](size_t o) { return be ? RdF64BE(ptr + o) : RdF64LE(ptr + o); };

    int64_t dim[8] = {};
    double pixdim[8] = {};
    int16_t datatype = 0, bitpix = 0;
    int64_t voxOffset = 0;
    double sclSlope = 1.0, sclInter = 0.0;
    uint8_t xyztUnits = 0;
    std::string descrip;
    int32_t qformCode = 0, sformCode = 0;
    double srowX[4] = {}, srowY[4] = {}, srowZ[4] = {};

    if (!nifti2) {
        for (int i = 0; i < 8; ++i) dim[i] = rdI16(40 + static_cast<size_t>(i) * 2);
        if (dim[0] < 1 || dim[0] > 7) return false;
        datatype = rdI16(70);
        bitpix   = rdI16(72);
        for (int i = 0; i < 8; ++i) pixdim[i] = rdF32(76 + static_cast<size_t>(i) * 4);
        voxOffset = static_cast<int64_t>(rdF32(108));
        sclSlope  = rdF32(112);
        sclInter  = rdF32(116);
        xyztUnits = ptr[123];
        descrip.assign(reinterpret_cast<const char*>(ptr + 148), 80);
        qformCode = rdI16(252);
        sformCode = rdI16(254);
        for (int i = 0; i < 4; ++i) {
            srowX[i] = rdF32(280 + static_cast<size_t>(i) * 4);
            srowY[i] = rdF32(296 + static_cast<size_t>(i) * 4);
            srowZ[i] = rdF32(312 + static_cast<size_t>(i) * 4);
        }
    } else {
        if (std::memcmp(ptr + 4, "n+2", 3) != 0 && std::memcmp(ptr + 4, "ni2", 3) != 0) return false;
        datatype = rdI16(12);
        bitpix   = rdI16(14);
        for (int i = 0; i < 8; ++i) dim[i] = rdI64(16 + static_cast<size_t>(i) * 8);
        if (dim[0] < 1 || dim[0] > 7) return false;
        for (int i = 0; i < 8; ++i) pixdim[i] = rdF64(104 + static_cast<size_t>(i) * 8);
        voxOffset = rdI64(168);
        sclSlope  = rdF64(176);
        sclInter  = rdF64(184);
        descrip.assign(reinterpret_cast<const char*>(ptr + 240), 80);
        qformCode = rdI32(344);
        sformCode = rdI32(348);
        for (int i = 0; i < 4; ++i) {
            srowX[i] = rdF64(400 + static_cast<size_t>(i) * 8);
            srowY[i] = rdF64(432 + static_cast<size_t>(i) * 8);
            srowZ[i] = rdF64(464 + static_cast<size_t>(i) * 8);
        }
        xyztUnits = static_cast<uint8_t>(rdI32(500) & 0xFF);
    }

    while (!descrip.empty() && (descrip.back() == '\0' || descrip.back() == ' ')) descrip.pop_back();
    if (sclSlope == 0.0 || !std::isfinite(sclSlope)) { sclSlope = 1.0; }
    if (!std::isfinite(sclInter)) { sclInter = 0.0; }

    int64_t nx = std::max<int64_t>(1, dim[1]);
    int64_t ny = std::max<int64_t>(1, dim[2]);
    int64_t nz = std::max<int64_t>(1, dim[0] >= 3 ? dim[3] : 1);
    int64_t nt = std::max<int64_t>(1, dim[0] >= 4 ? dim[4] : 1);

    const wchar_t* spaceUnit = ((xyztUnits & 0x07u) == 1) ? L"m" : (((xyztUnits & 0x07u) == 2) ? L"mm" : (((xyztUnits & 0x07u) == 3) ? L"um" : L"vox"));

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"NIfTI_Header_And_Stats";
    hdrSh.rows.push_back({ L"Neuroimaging Attribute", L"Value" });
    hdrSh.rows.push_back({ L"Format", std::wstring(nifti2 ? L"NIfTI-2 (540B)" : L"NIfTI-1 (348B)") + (wasGzip ? L" [GZip Compressed]" : L"") });
    hdrSh.rows.push_back({ L"Voxel Grid Dimensions", std::to_wstring(nx) + L" \xD7 " + std::to_wstring(ny) + L" \xD7 " + std::to_wstring(nz) + (nt > 1 ? (L" \xD7 " + std::to_wstring(nt)) : L"") + L" (" + std::to_wstring(dim[0]) + L"D)" });
    hdrSh.rows.push_back({ L"Voxel Spacing (pixdim)", FmtNum(pixdim[1]) + L" \xD7 " + FmtNum(pixdim[2]) + L" \xD7 " + FmtNum(pixdim[3]) + L" " + spaceUnit });
    hdrSh.rows.push_back({ L"Voxel Data Type", std::wstring(NiftiDatatypeName(datatype)) + L" (" + std::to_wstring(bitpix) + L" bits/voxel)" });
    hdrSh.rows.push_back({ L"Voxel Buffer Offset", std::to_wstring(voxOffset) + L" B" });
    hdrSh.rows.push_back({ L"Intensity Scaling", L"slope=" + FmtNum(sclSlope) + L", inter=" + FmtNum(sclInter) });
    if (!descrip.empty()) hdrSh.rows.push_back({ L"Description", PathUtils::Utf8ToWide(descrip) });
    hdrSh.rows.push_back({ L"Coordinate Codes", L"qform=" + std::to_wstring(qformCode) + L", sform=" + std::to_wstring(sformCode) });
    if (sformCode > 0) {
        hdrSh.rows.push_back({ L"Affine srow_x", L"[" + FmtNum(srowX[0]) + L", " + FmtNum(srowX[1]) + L", " + FmtNum(srowX[2]) + L", " + FmtNum(srowX[3]) + L"]" });
        hdrSh.rows.push_back({ L"Affine srow_y", L"[" + FmtNum(srowY[0]) + L", " + FmtNum(srowY[1]) + L", " + FmtNum(srowY[2]) + L", " + FmtNum(srowY[3]) + L"]" });
        hdrSh.rows.push_back({ L"Affine srow_z", L"[" + FmtNum(srowZ[0]) + L", " + FmtNum(srowZ[1]) + L", " + FmtNum(srowZ[2]) + L", " + FmtNum(srowZ[3]) + L"]" });
    }

    SpreadsheetSheet sliceSh;
    sliceSh.name = L"NIfTI_Center_Slice";

    size_t hdrMin = nifti2 ? 540 : 348;
    size_t vOff = (voxOffset >= static_cast<int64_t>(hdrMin)) ? static_cast<size_t>(voxOffset) : hdrMin;
    size_t bytesPerVox = (datatype == 2 || datatype == 256) ? 1 :
                         (datatype == 4 || datatype == 512) ? 2 :
                         (datatype == 8 || datatype == 16 || datatype == 768) ? 4 :
                         (datatype == 64 || datatype == 1024 || datatype == 1280) ? 8 :
                         (datatype == 128) ? 3 : 0;

    if (bytesPerVox > 0 && vOff + bytesPerVox <= len) {
        auto readVox = [&](size_t idx) -> double {
            const uint8_t* p = ptr + vOff + idx * bytesPerVox;
            double rv = 0.0;
            switch (datatype) {
                case 2:    rv = p[0]; break;
                case 256:  rv = static_cast<int8_t>(p[0]); break;
                case 4:    rv = be ? RdI16BE(p) : RdI16LE(p); break;
                case 512:  rv = be ? RdU16BE(p) : RdU16LE(p); break;
                case 8:    rv = be ? RdI32BE(p) : RdI32LE(p); break;
                case 768:  rv = be ? RdU32BE(p) : RdU32LE(p); break;
                case 16:   rv = be ? RdF32BE(p) : RdF32LE(p); break;
                case 64:   rv = be ? RdF64BE(p) : RdF64LE(p); break;
                case 1024: rv = static_cast<double>(be ? RdI64BE(p) : RdI64LE(p)); break;
                case 1280: rv = static_cast<double>(be ? RdU64BE(p) : RdU64LE(p)); break;
                case 128:  rv = (static_cast<double>(p[0]) + p[1] + p[2]) / 3.0; break;
                default:   rv = 0.0; break;
            }
            return rv * sclSlope + sclInter;
        };

        size_t availVoxels = (len - vOff) / bytesPerVox;
        uint64_t declaredVox = static_cast<uint64_t>(nx) * static_cast<uint64_t>(ny) * static_cast<uint64_t>(nz) * static_cast<uint64_t>(nt);
        size_t statCap = static_cast<size_t>(std::min<uint64_t>({ static_cast<uint64_t>(availVoxels), declaredVox, 500000ULL }));

        if (statCap > 0) {
            double mn = 0.0, mx = 0.0, sm = 0.0;
            size_t finCnt = 0, nzCnt = 0;
            for (size_t i = 0; i < statCap; ++i) {
                double v = readVox(i);
                if (std::isfinite(v)) {
                    if (finCnt == 0) { mn = v; mx = v; }
                    else { mn = std::min(mn, v); mx = std::max(mx, v); }
                    sm += v;
                    ++finCnt;
                    if (std::fabs(v) > 1e-9) ++nzCnt;
                }
            }
            if (finCnt > 0) {
                hdrSh.rows.push_back({ L"Voxel Min Intensity", FmtNum(mn) });
                hdrSh.rows.push_back({ L"Voxel Max Intensity", FmtNum(mx) });
                hdrSh.rows.push_back({ L"Voxel Mean Intensity", FmtNum(sm / static_cast<double>(finCnt)) });
                hdrSh.rows.push_back({ L"Non-Zero Voxels Sampled", std::to_wstring(nzCnt) + L" / " + std::to_wstring(finCnt) });
            }

            // Center axial slice Z = nz / 2
            size_t zMid = static_cast<size_t>(nz / 2);
            size_t cols = static_cast<size_t>(std::min<int64_t>(nx, 48));
            size_t rows = static_cast<size_t>(std::min<int64_t>(ny, 64));
            size_t stepX = std::max<size_t>(1, static_cast<size_t>(nx) / cols);
            size_t stepY = std::max<size_t>(1, static_cast<size_t>(ny) / rows);

            std::vector<std::wstring> hr = { L"Y \\ X (Z=" + std::to_wstring(zMid) + L")" };
            for (size_t x = 0; x < cols; ++x) hr.push_back(L"X=" + std::to_wstring(x * stepX));
            sliceSh.rows.push_back(std::move(hr));

            for (size_t y = 0; y < rows; ++y) {
                size_t yy = y * stepY;
                std::vector<std::wstring> rv = { L"Y=" + std::to_wstring(yy) };
                for (size_t x = 0; x < cols; ++x) {
                    size_t xx = x * stepX;
                    uint64_t idx = xx + yy * static_cast<uint64_t>(nx) + zMid * static_cast<uint64_t>(nx) * static_cast<uint64_t>(ny);
                    rv.push_back(idx < availVoxels ? FmtNum(readVox(static_cast<size_t>(idx)), 4) : L"");
                }
                sliceSh.rows.push_back(std::move(rv));
            }
            FinalizeCols(sliceSh);
        }
    }
    FinalizeCols(hdrSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = nifti2 ? L"NIfTI-2 Neuroimaging Volume (.nii)" : L"NIfTI-1 Neuroimaging Volume (.nii)";
    out.sheets.push_back(std::move(hdrSh));
    if (sliceSh.rows.size() > 1) out.sheets.push_back(std::move(sliceSh));
    out.valid = true;
    return true;
}

} // namespace GeoSciDetail

inline bool ParseExtendedGeoSciDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. ESRI Shapefile (.shp / .shx — magic big-endian 0x0000270A = 9994)
    if (extLower == L".shp" || extLower == L".shx" ||
        (raw.size() >= 100 && GeoSciDetail::RdU32BE(raw.data()) == 9994u))
    {
        return GeoSciDetail::ParseShapefile(raw, extLower, out);
    }

    // 2. OGC GeoPackage (.gpkg — SQLite 3 Spatial Database)
    if (extLower == L".gpkg") {
        if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) return false;
        return ParseSqliteDatabase(raw, out, /*isGeoPackage=*/true);
    }

    // 3. Garmin FIT Activity (.fit — ".FIT" at offset 8)
    if (extLower == L".fit" || (raw.size() >= 14 && (raw[0] == 12 || raw[0] == 14) && std::memcmp(raw.data() + 8, ".FIT", 4) == 0)) {
        return GeoSciDetail::ParseGarminFit(raw, out);
    }

    // 4. Zipped Keyhole Markup Language (.kmz)
    if (extLower == L".kmz") {
        if (raw.size() < 22 || raw[0] != 'P' || raw[1] != 'K') return false;
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) return false;
        for (const auto& e : entries) {
            if (PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) == L".kml") {
                std::vector<uint8_t> kmlBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, kmlBytes) && !kmlBytes.empty()) {
                    bool ok = ParseKmlGeography(kmlBytes, out);
                    std::string_view sv(reinterpret_cast<const char*>(kmlBytes.data()), kmlBytes.size());
                    // Extract all multi-vertex <coordinates>lon,lat[,alt] ...</coordinates> & <gx:coord>lon lat [alt]</gx:coord>
                    size_t pos = 0;
                    int rowIdx = 1;
                    while ((pos = sv.find("<coordinates>", pos)) != std::string_view::npos && out.geoPoints.size() < 20000) {
                        pos += 13;
                        size_t endC = sv.find("</coordinates>", pos);
                        if (endC == std::string_view::npos) break;
                        std::string_view blk = sv.substr(pos, endC - pos);
                        size_t k = 0;
                        while (k < blk.size() && out.geoPoints.size() < 20000) {
                            while (k < blk.size() && (blk[k] == ' ' || blk[k] == '\t' || blk[k] == '\r' || blk[k] == '\n')) ++k;
                            if (k >= blk.size()) break;
                            size_t tokEnd = k;
                            while (tokEnd < blk.size() && blk[tokEnd] != ' ' && blk[tokEnd] != '\t' && blk[tokEnd] != '\r' && blk[tokEnd] != '\n') ++tokEnd;
                            std::string tok(blk.substr(k, tokEnd - k));
                            double lon = 0.0, lat = 0.0, alt = 0.0;
                            int n = sscanf_s(tok.c_str(), "%lf,%lf,%lf", &lon, &lat, &alt);
                            if (n >= 2 && std::isfinite(lat) && std::isfinite(lon) && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0) {
                                GpsTrackPoint pt;
                                pt.lat = lat;
                                pt.lon = lon;
                                pt.elevM = (n >= 3 && std::isfinite(alt)) ? static_cast<float>(alt) : 0.0f;
                                pt.rowIndex = rowIdx;
                                out.geoPoints.push_back(std::move(pt));
                            }
                            k = tokEnd;
                        }
                        ++rowIdx;
                        pos = endC + 14;
                    }
                    pos = 0;
                    while ((pos = sv.find("<gx:coord>", pos)) != std::string_view::npos && out.geoPoints.size() < 20000) {
                        pos += 10;
                        size_t endG = sv.find("</gx:coord>", pos);
                        if (endG == std::string_view::npos) break;
                        std::string tok(sv.substr(pos, endG - pos));
                        double lon = 0.0, lat = 0.0, alt = 0.0;
                        int n = sscanf_s(tok.c_str(), "%lf %lf %lf", &lon, &lat, &alt);
                        if (n >= 2 && std::isfinite(lat) && std::isfinite(lon) && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0) {
                            GpsTrackPoint pt;
                            pt.lat = lat;
                            pt.lon = lon;
                            pt.elevM = (n >= 3 && std::isfinite(alt)) ? static_cast<float>(alt) : 0.0f;
                            pt.rowIndex = 1;
                            out.geoPoints.push_back(std::move(pt));
                        }
                        pos = endG + 11;
                    }
                    if (!ok && !out.geoPoints.empty()) {
                        out.kind = OfficeFileKind::CsvTable;
                        out.isSpreadsheet = true;
                        SpreadsheetSheet sh;
                        sh.name = L"KMZ_Track";
                        sh.rows.push_back({ L"#", L"Latitude", L"Longitude", L"Elevation (m)" });
                        for (size_t i = 0; i < std::min<size_t>(out.geoPoints.size(), 500); ++i) {
                            sh.rows.push_back({
                                std::to_wstring(i + 1),
                                GeoSciDetail::FmtNum(out.geoPoints[i].lat, 6),
                                GeoSciDetail::FmtNum(out.geoPoints[i].lon, 6),
                                GeoSciDetail::FmtNum(out.geoPoints[i].elevM, 2)
                            });
                        }
                        sh.maxCols = 4;
                        out.sheets.push_back(std::move(sh));
                        out.valid = true;
                        ok = true;
                    }
                    if (ok) {
                        out.formatTitle = L"Google Earth Zipped KMZ (.kmz)";
                        out.hasGeoTrack = (out.geoPoints.size() >= 2);
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // 5. NetCDF (.nc / .cdf)
    if (extLower == L".nc" || extLower == L".cdf" ||
        (raw.size() >= 16 && (std::memcmp(raw.data(), "CDF\x01", 4) == 0 ||
                              std::memcmp(raw.data(), "CDF\x02", 4) == 0 ||
                              std::memcmp(raw.data(), "CDF\x05", 4) == 0)))
    {
        return GeoSciDetail::ParseNetCdf(raw, extLower, out);
    }

    // 6. WMO GRIB Edition 1 & Edition 2 (.grib / .grib2 / .grb)
    if (extLower == L".grib" || extLower == L".grib2" || extLower == L".grb" ||
        (raw.size() >= 16 && std::memcmp(raw.data(), "GRIB", 4) == 0 && (raw[7] == 1 || raw[7] == 2)))
    {
        return GeoSciDetail::ParseWmoGrib(raw, out);
    }

    // 7. NIfTI-1 & NIfTI-2 Neuroimaging Volume (.nii / .nii.gz)
    if (extLower == L".nii" || extLower == L".nii.gz") {
        return GeoSciDetail::ParseNiftiVolume(raw, out);
    }

    return false;
}

} // namespace Formats
