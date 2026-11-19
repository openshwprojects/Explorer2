#pragma once
#include "../common/GpsTrackMap.h"
// IgcFlightLog.h — FAI IGC flight recorder files (.igc): H header records (date, pilot, glider, registration,
// recorder, datum) and every B fix record (time, position, validity, pressure and GNSS altitude in their fixed
// columns). Valid 3-D fixes go to the map with GNSS altitude (pressure altitude when GNSS altitude is 0).

namespace Formats {

inline bool ParseIgcFlightLogDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.empty() || (sv[0] != 'A' && sv.find("\nB") == std::string_view::npos)) return false;

    auto digits = [](std::string_view s) {
        if (s.empty()) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    };
    auto toInt = [](std::string_view s, int& v) {
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        return res.ec == std::errc{} && res.ptr == s.data() + s.size();
    };

    SpreadsheetSheet sheet;
    sheet.name = L"IGC_Flight_Track";
    sheet.rows.push_back({ L"#", L"Latitude", L"Longitude", L"Elevation (m)", L"UTC Time", L"Pressure Alt (m)", L"Fix" });
    std::vector<std::pair<std::wstring, std::wstring>> header;
    size_t fixes = 0, invalid = 0, malformed = 0;
    int maxAlt = 0, minAlt = 0;
    std::string firstTime, lastTime;
    out.geoPoints.clear();

    static const struct { const char* code; const wchar_t* label; } kHeaders[] = {
        { "DTE", L"Date (DDMMYY)" }, { "PLT", L"Pilot" }, { "CM2", L"Second crew" }, { "GTY", L"Glider type" },
        { "GID", L"Glider registration" }, { "CID", L"Competition ID" }, { "CCL", L"Competition class" },
        { "FTY", L"Flight recorder" }, { "RFW", L"Firmware" }, { "RHW", L"Hardware" }, { "DTM", L"GPS datum" },
        { "GPS", L"GPS receiver" }, { "PRS", L"Pressure sensor" }, { "SIT", L"Site" },
    };

    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;

        if (ln[0] == 'H' && ln.size() >= 5) {
            // HFDTE..., HFPLTPILOTINCHARGE:Name, HFGTYGLIDERTYPE:ASK 21
            std::string_view code = ln.substr(2, 3);
            for (const auto& h : kHeaders) {
                if (code == h.code) {
                    std::string_view val = ln.substr(5);
                    size_t colon = val.find(':');
                    if (colon != std::string_view::npos) val = val.substr(colon + 1);
                    if (code == "DTE" && val.rfind("DATE", 0) == 0) val = val.substr(4);
                    while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
                    if (!val.empty() && header.size() < 32) header.emplace_back(h.label, PathUtils::Utf8ToWide(val));
                    break;
                }
            }
            continue;
        }
        if (ln[0] != 'B') continue;
        // B HHMMSS DDMMmmm[N|S] DDDMMmmm[E|W] [A|V] PPPPP GGGGG (35 characters, extensions may follow)
        int latDeg = 0, latMin = 0, lonDeg = 0, lonMin = 0, pAlt = 0, gAlt = 0;
        if (ln.size() < 35 || !digits(ln.substr(1, 6)) || !toInt(ln.substr(7, 2), latDeg) || !toInt(ln.substr(9, 5), latMin) ||
            (ln[14] != 'N' && ln[14] != 'S') || !toInt(ln.substr(15, 3), lonDeg) || !toInt(ln.substr(18, 5), lonMin) ||
            (ln[23] != 'E' && ln[23] != 'W') || !toInt(ln.substr(25, 5), pAlt) || !toInt(ln.substr(30, 5), gAlt) ||
            latDeg > 90 || lonDeg > 180 || latMin >= 60000 || lonMin >= 60000) {
            ++malformed;
            continue;
        }
        ++fixes;
        bool valid3d = (ln[24] == 'A');
        if (!valid3d) ++invalid;
        double lat = latDeg + latMin / 60000.0;
        double lon = lonDeg + lonMin / 60000.0;
        if (ln[14] == 'S') lat = -lat;
        if (ln[23] == 'W') lon = -lon;
        int alt = (gAlt != 0 || pAlt == 0) ? gAlt : pAlt;
        maxAlt = (fixes == 1) ? alt : std::max(maxAlt, alt);
        minAlt = (fixes == 1) ? alt : std::min(minAlt, alt);
        std::string hhmmss(ln.substr(1, 6));
        std::string tm = hhmmss.substr(0, 2) + ":" + hhmmss.substr(2, 2) + ":" + hhmmss.substr(4, 2);
        if (firstTime.empty()) firstTime = tm;
        lastTime = tm;
        if (sheet.rows.size() > 30000) continue;
        int row = static_cast<int>(sheet.rows.size());
        wchar_t latB[32] = {}, lonB[32] = {};
        swprintf_s(latB, L"%.6f", lat);
        swprintf_s(lonB, L"%.6f", lon);
        sheet.rows.push_back({ std::to_wstring(row), latB, lonB, std::to_wstring(gAlt), PathUtils::Utf8ToWide(tm) + L" UTC",
                               std::to_wstring(pAlt), valid3d ? L"3D" : L"2D / invalid" });
        if (valid3d) AddGeoPoint(out, lat, lon, alt, row, PathUtils::Utf8ToWide(tm));
    }
    if (fixes == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"FAI IGC Flight Recorder Log (.igc)";
    sheet.maxCols = 7;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"IGC_Flight";
    summary.rows.push_back({ L"Property", L"Value" });
    for (auto& h : header) summary.rows.push_back({ h.first, h.second });
    summary.rows.push_back({ L"Fixes", std::to_wstring(fixes) + (invalid ? L" (" + std::to_wstring(invalid) + L" not 3D-valid)" : L"") });
    if (malformed) summary.rows.push_back({ L"Malformed B records (skipped)", std::to_wstring(malformed) });
    summary.rows.push_back({ L"Time span (UTC)", PathUtils::Utf8ToWide(firstTime + " .. " + lastTime) });
    summary.rows.push_back({ L"Altitude range (m)", std::to_wstring(minAlt) + L" .. " + std::to_wstring(maxAlt) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
