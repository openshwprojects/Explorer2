#pragma once
#include "GpsTrackMap.h"
// Gpx.h — GPS Exchange Format 1.0 / 1.1 (.gpx): waypoints, routes and tracks read element by element.
// Every <wpt>, <rtept> and <trkpt> becomes a table row (with its route / track name and segment) and a
// map point; the summary sheet holds the metadata and per-track statistics.

namespace Formats {

inline bool ParseGpxTrack(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    X::Element gpx;
    if (!X::FindChild(sv, "gpx", gpx, false) || gpx.selfClosing) return false;
    const std::string_view doc = gpx.body;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"GPS Exchange Track (.gpx)";
    out.geoPoints.clear();

    SpreadsheetSheet sheet;
    sheet.name = L"GPS_Points";
    sheet.rows.push_back({ L"#", L"Kind", L"Track / Route", L"Segment", L"Latitude", L"Longitude",
                           L"Elevation (m)", L"Timestamp", L"Name" });
    constexpr size_t kMaxRows = 20000;
    size_t totalPoints = 0, badPoints = 0;
    double minEle = 0.0, maxEle = 0.0;
    bool anyEle = false;
    std::string firstTime, lastTime;

    // One <wpt>/<rtept>/<trkpt>: lat/lon attributes, optional ele/time/name children. Every point reaches the map
    // (thinned evenly past its cap), the first kMaxRows the table too.
    std::string eleScratch, timeScratch, nameScratch;
    auto addPoint = [&](const X::Element& pt, const wchar_t* kind, const std::wstring& owner, size_t segment) {
        ++totalPoints;
        double lat = 0.0, lon = 0.0;
        if (!ParseGeoNumber(X::GetAttr(pt.attrs, "lat"), lat) || !ParseGeoNumber(X::GetAttr(pt.attrs, "lon"), lon) ||
            std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) {
            ++badPoints;
            return;
        }
        // One pass over the point's children (the first of each kind counts)
        std::string_view eleText, timeText, nameText;
        bool haveEle = false, haveTime = false, haveName = false;
        X::ForEachChild(pt.body, [&](std::string_view tag, const X::Element& c) {
            if (tag == "ele" && !haveEle) { eleText = X::TextView(c.body, eleScratch); haveEle = true; }
            else if (tag == "time" && !haveTime) { timeText = X::TextView(c.body, timeScratch); haveTime = true; }
            else if (tag == "name" && !haveName) { nameText = X::TextView(c.body, nameScratch); haveName = true; }
            return !(haveEle && haveTime && haveName);
        });
        double ele = 0.0;
        bool hasEle = ParseGeoNumber(eleText, ele);
        if (hasEle) {
            if (!anyEle) { minEle = maxEle = ele; anyEle = true; }
            else { minEle = std::min(minEle, ele); maxEle = std::max(maxEle, ele); }
        }
        if (!timeText.empty()) {
            if (firstTime.empty()) firstTime = timeText;
            lastTime = timeText;
        }
        const bool tableRow = sheet.rows.size() <= kMaxRows;
        const int rowIndex = tableRow ? static_cast<int>(sheet.rows.size()) : -1;
        std::wstring nameW = nameText.empty() ? std::wstring() : PathUtils::Utf8ToWide(nameText);
        if (tableRow) {
            sheet.rows.push_back({ std::to_wstring(rowIndex), kind, owner, segment ? std::to_wstring(segment) : std::wstring(),
                                   PathUtils::Utf8ToWide(X::AttrText(pt.attrs, "lat")),
                                   PathUtils::Utf8ToWide(X::AttrText(pt.attrs, "lon")),
                                   hasEle ? PathUtils::Utf8ToWide(eleText) : std::wstring(),
                                   PathUtils::Utf8ToWide(timeText), nameW });
        }
        AddGeoPoint(out, lat, lon, hasEle ? ele : 0.0, rowIndex, nameW.empty() ? owner : nameW);
    };

    SpreadsheetSheet summary;
    summary.name = L"GPX_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"GPX version", PathUtils::Utf8ToWide(X::AttrText(gpx.attrs, "version")) });
    summary.rows.push_back({ L"Creator", PathUtils::Utf8ToWide(X::AttrText(gpx.attrs, "creator")) });
    X::Element meta;
    if (X::FindChild(doc, "metadata", meta)) {
        std::string mName = X::ChildText(meta.body, "name");
        std::string mDesc = X::ChildText(meta.body, "desc");
        std::string mTime = X::ChildText(meta.body, "time");
        if (!mName.empty()) summary.rows.push_back({ L"Name", PathUtils::Utf8ToWide(mName) });
        if (!mDesc.empty()) summary.rows.push_back({ L"Description", PathUtils::Utf8ToWide(mDesc) });
        if (!mTime.empty()) summary.rows.push_back({ L"Created", PathUtils::Utf8ToWide(mTime) });
        X::Element author;
        if (X::FindChild(meta.body, "author", author)) {
            std::string aName = X::ChildText(author.body, "name");
            if (!aName.empty()) summary.rows.push_back({ L"Author", PathUtils::Utf8ToWide(aName) });
        }
    }

    size_t waypoints = 0, routes = 0, tracks = 0, segments = 0;
    // Tracks first, then routes, then waypoints, so the map line follows the recorded track
    X::ForEachElement(doc, "trk", [&](const X::Element& trk) {
        ++tracks;
        std::wstring tName = PathUtils::Utf8ToWide(X::ChildText(trk.body, "name"));
        if (tName.empty()) tName = L"Track " + std::to_wstring(tracks);
        size_t before = totalPoints, seg = 0;
        X::ForEachElement(trk.body, "trkseg", [&](const X::Element& s) {
            ++seg;
            ++segments;
            X::ForEachElement(s.body, "trkpt", [&](const X::Element& e) {
                addPoint(e, L"Track point", tName, seg);
                return true;
            });
            return true;
        });
        summary.rows.push_back({ L"Track: " + tName, std::to_wstring(seg) + L" segments, " +
                                 std::to_wstring(totalPoints - before) + L" points" });
        return true;
    });

    X::ForEachElement(doc, "rte", [&](const X::Element& rte) {
        ++routes;
        std::wstring rName = PathUtils::Utf8ToWide(X::ChildText(rte.body, "name"));
        if (rName.empty()) rName = L"Route " + std::to_wstring(routes);
        size_t before = totalPoints;
        X::ForEachElement(rte.body, "rtept", [&](const X::Element& e) {
            addPoint(e, L"Route point", rName, 0);
            return true;
        });
        summary.rows.push_back({ L"Route: " + rName, std::to_wstring(totalPoints - before) + L" points" });
        return true;
    });
    X::ForEachElement(doc, "wpt", [&](const X::Element& e) {
        ++waypoints;
        addPoint(e, L"Waypoint", L"", 0);
        return true;
    });
    if (totalPoints == 0 && waypoints == 0 && routes == 0 && tracks == 0) return false;
    summary.rows.push_back({ L"Waypoints", std::to_wstring(waypoints) });
    summary.rows.push_back({ L"Routes", std::to_wstring(routes) });
    summary.rows.push_back({ L"Tracks / segments", std::to_wstring(tracks) + L" / " + std::to_wstring(segments) });
    summary.rows.push_back({ L"Points", std::to_wstring(totalPoints) + (badPoints ? L" (" + std::to_wstring(badPoints) + L" without valid lat/lon)" : L"") });
    if (anyEle) {
        wchar_t buf[96];
        swprintf_s(buf, L"%.1f .. %.1f m", minEle, maxEle);
        summary.rows.push_back({ L"Elevation range", buf });
    }
    if (!firstTime.empty()) summary.rows.push_back({ L"Time span", PathUtils::Utf8ToWide(firstTime + " .. " + lastTime) });
    summary.maxCols = 2;

    sheet.maxCols = 9;
    out.sheets.push_back(std::move(sheet));
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
