#pragma once
#include "../JsonDom.h"
// GeoJson.h — RFC 7946 GeoJSON (.geojson): FeatureCollection / Feature / bare Geometry documents.
// Parsed with the shared JSON reader, features streamed one at a time (large collections, or documents cut
// short by the read limit, still show every feature up to that point). Every feature becomes a table row
// (geometry type, vertex count, first position, bounding box, properties as columns) and its vertices feed
// the map view.

namespace Formats {

namespace DetailGeoJson {

using JVal = ::Explorer2::Json::Value;

inline bool IsGeometryType(const std::string& t) {
    return t == "Point" || t == "MultiPoint" || t == "LineString" || t == "MultiLineString" ||
           t == "Polygon" || t == "MultiPolygon" || t == "GeometryCollection";
}

// A position is [lon, lat] or [lon, lat, alt] (RFC 7946 3.1.1)
inline bool ReadPosition(const JVal& v, double& lon, double& lat, double& alt, bool& hasAlt) {
    if (!v.IsArray() || v.arrVal.size() < 2 || !v.arrVal[0].IsNumber() || !v.arrVal[1].IsNumber()) return false;
    lon = v.arrVal[0].numVal;
    lat = v.arrVal[1].numVal;
    hasAlt = v.arrVal.size() >= 3 && v.arrVal[2].IsNumber();
    alt = hasAlt ? v.arrVal[2].numVal : 0.0;
    return true;
}

struct GeomStats {
    size_t vertices = 0;
    bool hasFirst = false;
    double firstLon = 0.0, firstLat = 0.0, firstAlt = 0.0;
    bool firstHasAlt = false;
    double minLon = 0.0, minLat = 0.0, maxLon = 0.0, maxLat = 0.0;
};

struct Walker {
    ParsedOfficeDocument& doc;
    int rowIndex = 0;
    std::wstring label;
    size_t maxMapPoints = 50000;

    void AddPosition(const JVal& pos, GeomStats& st) {
        double lon = 0.0, lat = 0.0, alt = 0.0;
        bool hasAlt = false;
        if (!ReadPosition(pos, lon, lat, alt, hasAlt)) return;
        if (!std::isfinite(lon) || !std::isfinite(lat)) return;
        if (st.vertices == 0) {
            st.hasFirst = true;
            st.firstLon = lon; st.firstLat = lat; st.firstAlt = alt; st.firstHasAlt = hasAlt;
            st.minLon = st.maxLon = lon;
            st.minLat = st.maxLat = lat;
        } else {
            st.minLon = std::min(st.minLon, lon); st.maxLon = std::max(st.maxLon, lon);
            st.minLat = std::min(st.minLat, lat); st.maxLat = std::max(st.maxLat, lat);
        }
        ++st.vertices;
        if (doc.geoPoints.size() < maxMapPoints && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0) {
            GpsTrackPoint pt;
            pt.lat = lat;
            pt.lon = lon;
            pt.elevM = std::isfinite(alt) ? static_cast<float>(alt) : 0.0f;
            pt.rowIndex = rowIndex;
            pt.label = label;
            doc.geoPoints.push_back(std::move(pt));
        }
    }

    // `depth` = how many array levels lie above a single position for this geometry type
    void AddCoordinates(const JVal& coords, int depth, GeomStats& st) {
        if (!coords.IsArray()) return;
        if (depth == 0) {
            AddPosition(coords, st);
            return;
        }
        for (const auto& c : coords.arrVal) AddCoordinates(c, depth - 1, st);
    }

    // Returns the geometry type name ("" when the object is not a geometry)
    std::string AddGeometry(const JVal& g, GeomStats& st, int nesting = 0) {
        if (!g.IsObject() || nesting > 8) return {};
        std::string type = g.GetString("type");
        const JVal* coords = g.Find("coordinates");
        if (type == "GeometryCollection") {
            if (const JVal* geoms = g.Find("geometries"); geoms && geoms->IsArray()) {
                for (const auto& sub : geoms->arrVal) AddGeometry(sub, st, nesting + 1);
            }
            return type;
        }
        if (!coords) return IsGeometryType(type) ? type : std::string();
        int depth = -1;
        if (type == "Point") depth = 0;
        else if (type == "MultiPoint" || type == "LineString") depth = 1;
        else if (type == "MultiLineString" || type == "Polygon") depth = 2;
        else if (type == "MultiPolygon") depth = 3;
        if (depth < 0) return {};
        AddCoordinates(*coords, depth, st);
        return type;
    }
};

inline std::wstring FormatCoord(double v) {
    wchar_t buf[48];
    swprintf_s(buf, L"%.6f", v);
    std::wstring s = buf;
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

inline std::wstring CellText(const JVal& v) {
    if (v.IsString()) return PathUtils::Utf8ToWide(v.strVal);
    return PathUtils::Utf8ToWide(::Explorer2::Json::Compact(v, 160));
}

} // namespace DetailGeoJson

inline bool ParseGeoJsonDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailGeoJson;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));

    // Root members are kept (they are small); the features array is read one feature at a time, so a large
    // collection never becomes one tree and a document cut short still shows the features before the cut
    struct Row {
        std::string type;
        size_t vertices = 0;
        std::wstring coords, bounds, id;
        std::vector<std::pair<std::string, std::wstring>> props;
    };
    std::vector<Row> rows;
    constexpr size_t kMaxRows = 5000;
    size_t featureCount = 0, totalVertices = 0;
    std::map<std::string, size_t> typeCounts;
    bool anyBounds = false;
    double bMinLon = 0.0, bMinLat = 0.0, bMaxLon = 0.0, bMaxLat = 0.0;
    out.geoPoints.clear();

    auto handleFeature = [&](const JVal& f) {
        if (!f.IsObject()) return;
        ++featureCount;
        bool isFeature = f.GetString("type") == "Feature";
        const JVal* geom = isFeature ? f.Find("geometry") : &f;
        const JVal* props = isFeature ? f.Find("properties") : nullptr;
        Walker w{ out };
        w.rowIndex = static_cast<int>(std::min(rows.size(), kMaxRows) + 1);
        if (props && props->IsObject()) {
            for (const char* nameKey : { "name", "Name", "NAME", "title", "label" }) {
                if (const JVal* nv = props->Find(nameKey); nv && nv->IsString()) {
                    w.label = PathUtils::Utf8ToWide(nv->strVal);
                    break;
                }
            }
        }
        if (rows.size() >= kMaxRows) w.maxMapPoints = out.geoPoints.size(); // counted, not mapped
        GeomStats st;
        std::string gtype = (geom && geom->IsObject()) ? w.AddGeometry(*geom, st) : std::string();
        ++typeCounts[gtype.empty() ? std::string("(none)") : gtype];
        totalVertices += st.vertices;
        if (st.hasFirst) {
            if (!anyBounds) {
                bMinLon = st.minLon; bMinLat = st.minLat; bMaxLon = st.maxLon; bMaxLat = st.maxLat;
                anyBounds = true;
            } else {
                bMinLon = std::min(bMinLon, st.minLon); bMinLat = std::min(bMinLat, st.minLat);
                bMaxLon = std::max(bMaxLon, st.maxLon); bMaxLat = std::max(bMaxLat, st.maxLat);
            }
        }
        if (rows.size() >= kMaxRows) return;
        Row r;
        r.type = gtype;
        r.vertices = st.vertices;
        if (st.hasFirst) {
            r.coords = L"[" + FormatCoord(st.firstLon) + L", " + FormatCoord(st.firstLat);
            if (st.firstHasAlt) r.coords += L", " + FormatCoord(st.firstAlt);
            r.coords += L"]";
            if (st.vertices > 1) {
                r.bounds = FormatCoord(st.minLon) + L", " + FormatCoord(st.minLat) + L" .. " +
                           FormatCoord(st.maxLon) + L", " + FormatCoord(st.maxLat);
            }
        }
        if (const JVal* id = isFeature ? f.Find("id") : nullptr) r.id = CellText(*id);
        if (props && props->IsObject()) {
            for (const auto& kv : props->objVal) {
                if (r.props.size() >= 256) break;
                if (!kv.second.IsNull()) r.props.emplace_back(kv.first, CellText(kv.second));
            }
        }
        rows.push_back(std::move(r));
    };

    ::Explorer2::Json::Limits limits;
    limits.maxNodes = 4000000; // per feature (the stream resets it), and for the root members
    ::Explorer2::Json::Parser parser(text, limits);
    JVal rootObj;
    rootObj.kind = JVal::Kind::Object;
    bool sawFeatures = false;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "features" && !sawFeatures && parser.Peek() == '[') {
            sawFeatures = true;
            return parser.StreamArray([&](JVal&& f) { handleFeature(f); });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        rootObj.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();

    std::string rootType = rootObj.GetString("type");
    if (sawFeatures) {
        // A complete document must say it is a FeatureCollection; one cut short may have lost its "type"
        // member (written after the features), but it must have delivered at least one feature
        if (complete ? rootType != "FeatureCollection" : (!rootType.empty() && rootType != "FeatureCollection")) return false;
        if (!complete && featureCount == 0) return false;
    } else if (complete && (rootType == "Feature" || IsGeometryType(rootType))) {
        handleFeature(rootObj); // a bare Feature / Geometry is a one-row collection
    } else {
        return false;
    }

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"RFC 7946 GeoJSON Spatial Features (.geojson)";

    // Property columns: keys in order of first appearance (capped)
    std::vector<std::string> propKeys;
    for (const auto& r : rows) {
        for (const auto& kv : r.props) {
            if (propKeys.size() >= 24) break;
            if (std::find(propKeys.begin(), propKeys.end(), kv.first) == propKeys.end()) propKeys.push_back(kv.first);
        }
    }
    SpreadsheetSheet sheet;
    sheet.name = L"GeoJSON_Features";
    std::vector<std::wstring> header = { L"#", L"Geometry Type", L"Vertices", L"Coordinates", L"Bounds (lon/lat)", L"ID" };
    for (const auto& k : propKeys) header.push_back(PathUtils::Utf8ToWide(k));
    sheet.rows.push_back(header);
    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        std::vector<std::wstring> row = { std::to_wstring(i + 1), r.type.empty() ? L"(no geometry)" : PathUtils::Utf8ToWide(r.type),
                                          std::to_wstring(r.vertices), r.coords, r.bounds, r.id };
        for (const auto& k : propKeys) {
            std::wstring cell;
            for (const auto& kv : r.props) {
                if (kv.first == k) { cell = kv.second; break; }
            }
            row.push_back(std::move(cell));
        }
        sheet.rows.push_back(std::move(row));
    }
    sheet.maxCols = header.size();
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet summary;
    summary.name = L"GeoJSON_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Root type", PathUtils::Utf8ToWide(rootType.empty() ? std::string("FeatureCollection") : rootType) });
    summary.rows.push_back({ L"Features", std::to_wstring(featureCount) + (featureCount > rows.size() ? L" (first " + std::to_wstring(rows.size()) + L" listed)" : L"") });
    summary.rows.push_back({ L"Vertices", std::to_wstring(totalVertices) });
    for (const auto& tc : typeCounts) {
        summary.rows.push_back({ L"Geometry: " + PathUtils::Utf8ToWide(tc.first), std::to_wstring(tc.second) });
    }
    if (anyBounds) {
        summary.rows.push_back({ L"Bounds (lon/lat)", FormatCoord(bMinLon) + L", " + FormatCoord(bMinLat) + L" .. " +
                                                     FormatCoord(bMaxLon) + L", " + FormatCoord(bMaxLat) });
    }
    if (const JVal* bbox = rootObj.Find("bbox")) summary.rows.push_back({ L"Declared bbox", CellText(*bbox) });
    if (const JVal* crs = rootObj.Find("crs")) summary.rows.push_back({ L"CRS (pre-RFC 7946)", CellText(*crs) });
    if (!complete) summary.rows.push_back({ L"Document", L"incomplete (cut short or damaged); features before that point are shown" });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
