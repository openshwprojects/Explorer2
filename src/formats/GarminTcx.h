#pragma once
#include "GpsTrackMap.h"
// GarminTcx.h — Garmin Training Center XML v2 (.tcx): Activities (sport, laps with totals) and Courses,
// every Trackpoint with time, position, altitude, distance, heart rate, cadence and the ActivityExtension
// speed / power, read element by element. Positions go to the map; laps go to the summary sheet.

namespace Formats {

inline bool ParseGarminTcxDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    X::Element root;
    if (!X::FindChild(sv, "TrainingCenterDatabase", root, false) || root.selfClosing) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Garmin Training Center Telemetry (.tcx)";
    out.geoPoints.clear();

    SpreadsheetSheet sheet;
    sheet.name = L"TCX_Trackpoints";
    sheet.rows.push_back({ L"#", L"Activity / Course", L"Lap", L"Timestamp", L"Latitude", L"Longitude", L"Altitude (m)",
                           L"Distance (m)", L"Heart Rate (bpm)", L"Cadence (rpm)", L"Speed (m/s)", L"Power (W)" });
    SpreadsheetSheet laps;
    laps.name = L"TCX_Laps";
    laps.rows.push_back({ L"Activity / Course", L"Lap", L"Start", L"Time (s)", L"Distance (m)", L"Calories",
                          L"Avg HR (bpm)", L"Max HR (bpm)", L"Max Speed (m/s)", L"Intensity" });

    constexpr size_t kMaxRows = 20000;
    size_t trackpoints = 0, activities = 0, courses = 0;
    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };
    // Child text that may sit inside a wrapper, e.g. <HeartRateBpm><Value>138</Value></HeartRateBpm>
    auto nestedValue = [](std::string_view body, std::string_view wrapper) -> std::string {
        X::Element e;
        if (!X::FindChild(body, wrapper, e)) return {};
        return X::ChildText(e.body, "Value");
    };

    auto addTrack = [&](std::string_view lapOrCourseBody, const std::wstring& owner, const std::wstring& lapLabel) {
        X::ForEachElement(lapOrCourseBody, "Trackpoint", [&](const X::Element& tp) {
            ++trackpoints;
            double lat = 0.0, lon = 0.0;
            bool hasPos = false;
            std::string latS, lonS;
            X::Element pos;
            if (X::FindChild(tp.body, "Position", pos)) {
                latS = X::ChildText(pos.body, "LatitudeDegrees");
                lonS = X::ChildText(pos.body, "LongitudeDegrees");
                hasPos = ParseGeoNumber(latS, lat) && ParseGeoNumber(lonS, lon);
            }
            std::string alt = X::ChildText(tp.body, "AltitudeMeters");
            int rowIndex = -1; // past the table's cap a point still reaches the map
            if (sheet.rows.size() <= kMaxRows) {
                std::string hr = nestedValue(tp.body, "HeartRateBpm");
                std::string cad = X::ChildText(tp.body, "Cadence");
                std::string speed, watts;
                X::Element ext;
                if (X::FindChild(tp.body, "Extensions", ext)) {
                    speed = X::ChildText(ext.body, "Speed", false);
                    watts = X::ChildText(ext.body, "Watts", false);
                    if (cad.empty()) cad = X::ChildText(ext.body, "RunCadence", false);
                }
                rowIndex = static_cast<int>(sheet.rows.size());
                sheet.rows.push_back({ std::to_wstring(rowIndex), owner, lapLabel, w(X::ChildText(tp.body, "Time")), w(latS), w(lonS),
                                       w(alt), w(X::ChildText(tp.body, "DistanceMeters")), w(hr), w(cad), w(speed), w(watts) });
            }
            if (hasPos) {
                double altV = 0.0;
                ParseGeoNumber(alt, altV);
                AddGeoPoint(out, lat, lon, altV, rowIndex, owner);
            }
            return true;
        });
    };

    auto addLapRow = [&](const X::Element& lap, const std::wstring& owner, const std::wstring& lapLabel) {
        if (laps.rows.size() > 2000) return;
        laps.rows.push_back({ owner, lapLabel, w(X::AttrText(lap.attrs, "StartTime")),
                              w(X::ChildText(lap.body, "TotalTimeSeconds")), w(X::ChildText(lap.body, "DistanceMeters")),
                              w(X::ChildText(lap.body, "Calories")), w(nestedValue(lap.body, "AverageHeartRateBpm")),
                              w(nestedValue(lap.body, "MaximumHeartRateBpm")), w(X::ChildText(lap.body, "MaximumSpeed")),
                              w(X::ChildText(lap.body, "Intensity")) });
    };

    X::Element acts;
    if (X::FindChild(root.body, "Activities", acts)) {
        X::ForEachElement(acts.body, "Activity", [&](const X::Element& act) {
            ++activities;
            std::wstring owner = w(X::AttrText(act.attrs, "Sport"));
            std::string id = X::ChildText(act.body, "Id");
            owner = (owner.empty() ? L"Activity" : owner) + (id.empty() ? L"" : L" " + w(id));
            size_t lapNo = 0;
            X::ForEachElement(act.body, "Lap", [&](const X::Element& lap) {
                std::wstring lapLabel = std::to_wstring(++lapNo);
                addLapRow(lap, owner, lapLabel);
                addTrack(lap.body, owner, lapLabel);
                return true;
            });
            return true;
        });
    }
    X::Element crs;
    if (X::FindChild(root.body, "Courses", crs)) {
        X::ForEachElement(crs.body, "Course", [&](const X::Element& course) {
            ++courses;
            std::wstring owner = L"Course " + w(X::ChildText(course.body, "Name"));
            size_t lapNo = 0;
            X::ForEachElement(course.body, "Lap", [&](const X::Element& lap) {
                addLapRow(lap, owner, std::to_wstring(++lapNo));
                return true;
            });
            addTrack(course.body, owner, L"");
            return true;
        });
    }
    if (activities == 0 && courses == 0) return false;

    sheet.maxCols = 12;
    laps.maxCols = 10;
    out.sheets.push_back(std::move(sheet));
    if (laps.rows.size() > 1) out.sheets.push_back(std::move(laps));

    SpreadsheetSheet summary;
    summary.name = L"TCX_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Activities", std::to_wstring(activities) });
    summary.rows.push_back({ L"Courses", std::to_wstring(courses) });
    summary.rows.push_back({ L"Trackpoints", std::to_wstring(trackpoints) });
    summary.rows.push_back({ L"Trackpoints with position", std::to_wstring(out.geoPoints.size()) });
    X::Element author;
    if (X::FindChild(root.body, "Author", author)) {
        std::string an = X::ChildText(author.body, "Name");
        if (!an.empty()) summary.rows.push_back({ L"Author", w(an) });
    }
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
