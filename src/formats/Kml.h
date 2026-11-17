#pragma once
#include "GpsTrackMap.h"
// Kml.h — OGC KML 2.2 (.kml; also the doc.kml inside .kmz): Placemarks with their Document / Folder path,
// geometry type (Point, LineString, LinearRing, Polygon, MultiGeometry, gx:Track, gx:MultiTrack) and every
// vertex of <coordinates> / <gx:coord>, read element by element. All vertices go to the map.

namespace Formats {

namespace DetailKml {

namespace X = ::Explorer2::XmlTagScan;

struct Coord {
    double lon = 0.0, lat = 0.0, alt = 0.0;
    bool hasAlt = false;
};

// <coordinates>: "lon,lat[,alt]" tuples separated by whitespace (spaces next to commas are tolerated)
inline void ParseCoordinates(std::string_view s, std::vector<Coord>& out, size_t cap) {
    size_t i = 0;
    const size_t n = s.size();
    auto isWs = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (i < n && out.size() < cap) {
        while (i < n && isWs(s[i])) ++i;
        if (i >= n) break;
        double vals[3] = {};
        int count = 0;
        bool bad = false;
        while (i < n && count < 3) {
            size_t st = i;
            while (i < n && s[i] != ',' && !isWs(s[i])) ++i;
            double v = 0.0;
            if (!ParseGeoNumber(s.substr(st, i - st), v)) { bad = true; break; }
            vals[count++] = v;
            size_t j = i;
            while (j < n && isWs(s[j])) ++j;
            if (j < n && s[j] == ',') {
                i = j + 1;
                while (i < n && isWs(s[i])) ++i;
                continue;
            }
            break; // whitespace not followed by a comma ends the tuple
        }
        if (bad) {
            while (i < n && !isWs(s[i])) ++i; // skip the damaged tuple
            continue;
        }
        if (count >= 2) out.push_back({ vals[0], vals[1], vals[2], count >= 3 });
    }
}

// <gx:coord>: "lon lat [alt]"
inline bool ParseGxCoord(std::string_view s, Coord& c) {
    double v[3] = {};
    int count = 0;
    size_t i = 0;
    while (i < s.size() && count < 3) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        size_t st = i;
        while (i < s.size() && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        if (st == i) break;
        if (!ParseGeoNumber(s.substr(st, i - st), v[count])) return false;
        ++count;
    }
    if (count < 2) return false;
    c = { v[0], v[1], v[2], count >= 3 };
    return true;
}

inline std::string StripMarkup(std::string text, size_t maxLen) {
    std::string out;
    out.reserve(std::min(text.size(), maxLen));
    bool inTag = false, lastSpace = false;
    for (char c : text) {
        if (c == '<') { inTag = true; continue; }
        if (inTag) { if (c == '>') inTag = false; continue; }
        bool ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (ws) {
            if (!lastSpace && !out.empty()) out.push_back(' ');
            lastSpace = true;
        } else {
            out.push_back(c);
            lastSpace = false;
        }
        if (out.size() >= maxLen) break;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

inline bool IsGeometryName(std::string_view n) {
    return n == "Point" || n == "LineString" || n == "LinearRing" || n == "Polygon" || n == "MultiGeometry" ||
           n == "Track" || n == "MultiTrack" || n == "Model";
}

} // namespace DetailKml

inline bool ParseKmlGeography(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailKml;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    X::Element root;
    std::string_view top = sv;
    if (X::FindChild(sv, "kml", root, false)) {
        if (root.selfClosing) return false;
        top = root.body;
    }

    SpreadsheetSheet sheet;
    sheet.name = L"Placemarks";
    sheet.rows.push_back({ L"#", L"Placemark Name", L"Folder", L"Geometry", L"Vertices",
                           L"Coordinates (Lon,Lat,Alt)", L"Description" });
    constexpr size_t kMaxRows = 10000;
    out.geoPoints.clear();
    std::map<std::string, size_t> geomCounts;
    size_t placemarks = 0, folders = 0, vertices = 0;
    std::string docName;

    auto fmt = [](double v) {
        wchar_t buf[40];
        swprintf_s(buf, L"%.6f", v);
        std::wstring s = buf;
        while (!s.empty() && s.back() == L'0') s.pop_back();
        if (!s.empty() && s.back() == L'.') s.pop_back();
        return s;
    };

    auto processPlacemark = [&](const X::Element& pm, const std::string& folder) {
        ++placemarks;
        std::string name = X::ChildText(pm.body, "name");
        std::string desc = StripMarkup(X::ChildText(pm.body, "description"), 300);

        // Geometry type: the first geometry element inside the placemark
        std::string geom;
        {
            size_t pos = 0;
            X::XmlTag t;
            while (X::NextTag(pm.body, pos, t)) {
                if (!t.isClosing && IsGeometryName(t.localName)) { geom = std::string(t.localName); break; }
            }
        }
        std::vector<Coord> coords;
        X::ForEachElement(pm.body, "coordinates", [&](const X::Element& c) {
            ParseCoordinates(X::DecodeText(c.body), coords, 200000);
            return true;
        });
        X::ForEachElement(pm.body, "coord", [&](const X::Element& c) { // gx:Track samples
            Coord k;
            if (ParseGxCoord(X::DecodeText(c.body), k)) coords.push_back(k);
            return coords.size() < 200000;
        });
        vertices += coords.size();
        ++geomCounts[geom.empty() ? std::string("(none)") : (geom == "Track" ? std::string("gx:Track") :
                                                              geom == "MultiTrack" ? std::string("gx:MultiTrack") : geom)];

        if (sheet.rows.size() > kMaxRows) return;
        int rowIndex = static_cast<int>(sheet.rows.size());
        std::wstring nameW = PathUtils::Utf8ToWide(name);
        std::wstring first;
        if (!coords.empty()) {
            first = fmt(coords[0].lon) + L"," + fmt(coords[0].lat);
            if (coords[0].hasAlt) first += L"," + fmt(coords[0].alt);
        }
        sheet.rows.push_back({ std::to_wstring(rowIndex), nameW, PathUtils::Utf8ToWide(folder),
                               geom.empty() ? L"(none)" : PathUtils::Utf8ToWide(geom),
                               std::to_wstring(coords.size()), first, PathUtils::Utf8ToWide(desc) });
        for (const auto& c : coords) AddGeoPoint(out, c.lat, c.lon, c.alt, rowIndex, nameW);
    };

    // Documents and folders nest; placemarks are listed with their container path
    auto walk = [&](auto& self, std::string_view body, const std::string& path, int depth) -> void {
        if (depth > 32) return;
        X::ForEachChild(body, [&](std::string_view name, const X::Element& e) {
            if (name == "Document" || name == "Folder") {
                ++folders;
                std::string fname = X::ChildText(e.body, "name");
                if (name == "Document" && docName.empty()) docName = fname;
                std::string sub = path.empty() ? fname : (fname.empty() ? path : path + " / " + fname);
                self(self, e.body, sub, depth + 1);
            } else if (name == "Placemark") {
                processPlacemark(e, path);
            }
            return true;
        });
    };
    walk(walk, top, std::string(), 0);
    if (placemarks == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Google Earth KML (.kml)";
    sheet.maxCols = 7;
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet summary;
    summary.name = L"KML_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    if (!docName.empty()) summary.rows.push_back({ L"Document", PathUtils::Utf8ToWide(docName) });
    summary.rows.push_back({ L"Placemarks", std::to_wstring(placemarks) });
    summary.rows.push_back({ L"Documents / folders", std::to_wstring(folders) });
    summary.rows.push_back({ L"Vertices", std::to_wstring(vertices) });
    for (const auto& gc : geomCounts) {
        summary.rows.push_back({ L"Geometry: " + PathUtils::Utf8ToWide(gc.first), std::to_wstring(gc.second) });
    }
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
