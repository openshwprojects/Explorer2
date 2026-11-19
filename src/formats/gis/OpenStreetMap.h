#pragma once
#include "../common/GpsTrackMap.h"
// OpenStreetMap.h — OSM XML 0.6 (.osm): nodes, ways and relations read element by element. Way geometry is
// resolved through the node table, so ways (roads, buildings, rivers) reach the map, not just loose nodes.
// Tagged nodes (points of interest), ways and relations are listed with their name and main feature tag.

namespace Formats {

inline bool ParseOpenStreetMapDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    X::Element osm;
    if (!X::FindChild(sv, "osm", osm, false) || osm.selfClosing) return false;
    const std::string_view doc = osm.body;

    struct Tagged {
        std::string name, feature, all;
        size_t count = 0;
    };
    static const char* kFeatureKeys[] = { "highway", "building", "amenity", "shop", "tourism", "leisure", "landuse",
                                          "natural", "waterway", "railway", "aeroway", "place", "boundary", "man_made",
                                          "power", "public_transport", "route", "type" };
    auto readTags = [](std::string_view body) {
        Tagged t;
        X::ForEachElement(body, "tag", [&](const X::Element& e) {
            std::string k = X::AttrText(e.attrs, "k");
            std::string v = X::AttrText(e.attrs, "v");
            if (k.empty()) return true;
            ++t.count;
            if (k == "name") t.name = v;
            if (t.feature.empty()) {
                for (const char* fk : kFeatureKeys) {
                    if (k == fk) { t.feature = k + "=" + v; break; }
                }
            }
            if (t.all.size() < 400) {
                if (!t.all.empty()) t.all += "; ";
                t.all += k + "=" + v;
            }
            return true;
        });
        return t;
    };

    SpreadsheetSheet sheet;
    sheet.name = L"OSM_Elements";
    sheet.rows.push_back({ L"Element", L"ID", L"Name", L"Feature", L"Latitude", L"Longitude", L"Nodes / Members", L"Tags" });
    constexpr size_t kMaxRows = 20000;
    out.geoPoints.clear();

    // 1. Nodes: positions for way geometry; tagged nodes are listed (untagged ones are just vertices)
    std::unordered_map<int64_t, std::pair<double, double>> nodePos;
    size_t nodes = 0, taggedNodes = 0, ways = 0, relations = 0, missingRefs = 0;
    auto parseId = [](const std::string& s, int64_t& id) {
        auto res = std::from_chars(s.data(), s.data() + s.size(), id);
        return res.ec == std::errc{} && res.ptr == s.data() + s.size();
    };
    X::ForEachElement(doc, "node", [&](const X::Element& e) {
        ++nodes;
        int64_t id = 0;
        double lat = 0.0, lon = 0.0;
        std::string idS = X::AttrText(e.attrs, "id");
        std::string latS = X::AttrText(e.attrs, "lat");
        std::string lonS = X::AttrText(e.attrs, "lon");
        bool hasPos = ParseGeoNumber(latS, lat) && ParseGeoNumber(lonS, lon);
        if (parseId(idS, id) && hasPos) nodePos[id] = { lat, lon };
        if (!e.selfClosing) {
            Tagged t = readTags(e.body);
            if (t.count > 0) {
                ++taggedNodes;
                if (sheet.rows.size() <= kMaxRows) {
                    int row = static_cast<int>(sheet.rows.size());
                    std::wstring nameW = PathUtils::Utf8ToWide(t.name);
                    sheet.rows.push_back({ L"node", PathUtils::Utf8ToWide(idS), nameW, PathUtils::Utf8ToWide(t.feature),
                                           PathUtils::Utf8ToWide(latS), PathUtils::Utf8ToWide(lonS), L"", PathUtils::Utf8ToWide(t.all) });
                    if (hasPos) AddGeoPoint(out, lat, lon, 0.0, row, nameW);
                }
            }
        }
        return true;
    });

    // 2. Ways: node references resolved to coordinates
    X::ForEachElement(doc, "way", [&](const X::Element& e) {
        ++ways;
        std::string idS = X::AttrText(e.attrs, "id");
        std::vector<std::pair<double, double>> pts;
        size_t refs = 0;
        if (!e.selfClosing) {
            X::ForEachElement(e.body, "nd", [&](const X::Element& nd) {
                ++refs;
                int64_t ref = 0;
                auto it = parseId(X::AttrText(nd.attrs, "ref"), ref) ? nodePos.find(ref) : nodePos.end();
                if (it == nodePos.end()) ++missingRefs;
                else pts.push_back(it->second);
                return true;
            });
        }
        Tagged t = e.selfClosing ? Tagged{} : readTags(e.body);
        if (sheet.rows.size() <= kMaxRows) {
            int row = static_cast<int>(sheet.rows.size());
            std::wstring nameW = PathUtils::Utf8ToWide(t.name);
            std::wstring latW, lonW;
            if (!pts.empty()) {
                wchar_t b1[32], b2[32];
                swprintf_s(b1, L"%.7f", pts[0].first);
                swprintf_s(b2, L"%.7f", pts[0].second);
                latW = b1;
                lonW = b2;
            }
            sheet.rows.push_back({ L"way", PathUtils::Utf8ToWide(idS), nameW, PathUtils::Utf8ToWide(t.feature), latW, lonW,
                                   std::to_wstring(refs), PathUtils::Utf8ToWide(t.all) });
            for (const auto& pt : pts) AddGeoPoint(out, pt.first, pt.second, 0.0, row, nameW);
        }
        return true;
    });

    // 3. Relations: members and tags (routes, multipolygons, boundaries)
    X::ForEachElement(doc, "relation", [&](const X::Element& e) {
        ++relations;
        size_t members = 0;
        if (!e.selfClosing) {
            X::ForEachElement(e.body, "member", [&](const X::Element&) { ++members; return true; });
        }
        Tagged t = e.selfClosing ? Tagged{} : readTags(e.body);
        if (sheet.rows.size() <= kMaxRows) {
            sheet.rows.push_back({ L"relation", PathUtils::Utf8ToWide(X::AttrText(e.attrs, "id")), PathUtils::Utf8ToWide(t.name),
                                   PathUtils::Utf8ToWide(t.feature), L"", L"", std::to_wstring(members), PathUtils::Utf8ToWide(t.all) });
        }
        return true;
    });
    if (nodes + ways + relations == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"OpenStreetMap Vector Data (.osm)";
    sheet.maxCols = 8;
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet summary;
    summary.name = L"OSM_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"OSM version", PathUtils::Utf8ToWide(X::AttrText(osm.attrs, "version")) });
    summary.rows.push_back({ L"Generator", PathUtils::Utf8ToWide(X::AttrText(osm.attrs, "generator")) });
    X::Element bounds;
    if (X::FindChild(doc, "bounds", bounds)) {
        summary.rows.push_back({ L"Bounds (lat/lon)", PathUtils::Utf8ToWide(X::AttrText(bounds.attrs, "minlat") + ", " +
            X::AttrText(bounds.attrs, "minlon") + " .. " + X::AttrText(bounds.attrs, "maxlat") + ", " + X::AttrText(bounds.attrs, "maxlon")) });
    }
    summary.rows.push_back({ L"Nodes", std::to_wstring(nodes) + L" (" + std::to_wstring(taggedNodes) + L" tagged)" });
    summary.rows.push_back({ L"Ways", std::to_wstring(ways) });
    summary.rows.push_back({ L"Relations", std::to_wstring(relations) });
    if (missingRefs) summary.rows.push_back({ L"Way node references outside this file", std::to_wstring(missingRefs) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
