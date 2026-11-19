#pragma once
#include "../common/GpsTrackMap.h"
// NmeaGpsLog.h — NMEA 0183 receiver logs (.nmea / .nma / .gps): GGA, RMC, GLL and VTG sentences from any
// talker ($GP, $GN, $GL, $GA, $GB, $BD ...). Checksums (*hh) are verified, void / no-fix sentences give no
// position, and GGA + RMC (+ VTG) reports of the same epoch merge into one fix with altitude, speed and course.

namespace Formats {

namespace DetailNmea {

// ddmm.mmmm / dddmm.mmmm + hemisphere -> signed decimal degrees
inline bool DegMin(std::string_view dm, std::string_view hemi, double& out) {
    double raw = 0.0;
    if (dm.empty() || !ParseGeoNumber(dm, raw) || raw < 0.0) return false;
    double deg = std::floor(raw / 100.0);
    double mins = raw - deg * 100.0;
    if (mins >= 60.0) return false;
    out = deg + mins / 60.0;
    if (!hemi.empty() && (hemi[0] == 'S' || hemi[0] == 'W')) out = -out;
    return true;
}

inline std::wstring Fmt(double v, int prec) {
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f", prec, v);
    return buf;
}

} // namespace DetailNmea

inline bool ParseNmeaGpsLogDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using namespace DetailNmea;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    struct Fix {
        std::string time, date;
        double lat = 0.0, lon = 0.0;
        bool hasPos = false;
        std::string alt, speedKmh, course, quality, sats, hdop;
    };
    std::vector<Fix> fixes;
    std::map<std::string, size_t> sentenceCounts;
    size_t badChecksum = 0, sentences = 0, noFix = 0;
    auto fixFor = [&](const std::string& time) -> Fix& {
        // GGA / RMC / VTG of the same second describe the same fix
        if (!fixes.empty() && !time.empty() && fixes.back().time == time) return fixes.back();
        fixes.emplace_back();
        fixes.back().time = time;
        return fixes.back();
    };

    for (size_t pos = 0; pos < sv.size() && fixes.size() < 100000;) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        size_t dollar = ln.find('$');
        if (dollar == std::string_view::npos) continue;
        ln = ln.substr(dollar);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.size() < 7) continue;
        // Checksum: XOR of the characters between '$' and '*'
        size_t star = ln.rfind('*');
        if (star != std::string_view::npos) {
            if (star + 3 > ln.size()) { ++badChecksum; continue; }
            uint8_t x = 0;
            for (size_t i = 1; i < star; ++i) x ^= static_cast<uint8_t>(ln[i]);
            unsigned want = 0;
            auto res = std::from_chars(ln.data() + star + 1, ln.data() + star + 3, want, 16);
            if (res.ec != std::errc{} || want != x) { ++badChecksum; continue; }
            ln = ln.substr(0, star);
        }
        std::vector<std::string_view> tok;
        for (size_t p = 0; p <= ln.size();) {
            size_t c = ln.find(',', p);
            if (c == std::string_view::npos) c = ln.size();
            tok.push_back(ln.substr(p, c - p));
            p = c + 1;
        }
        if (tok[0].size() < 6) continue;
        std::string type(tok[0].substr(3)); // $GPGGA -> GGA (talker ignored)
        ++sentences;
        ++sentenceCounts[type];
        auto t = [&](size_t i) -> std::string_view { return i < tok.size() ? tok[i] : std::string_view{}; };

        if (type == "GGA") {
            // time, lat, N/S, lon, E/W, quality, sats, hdop, alt, M, ...
            std::string quality(t(6));
            if (quality.empty() || quality == "0") { ++noFix; continue; }
            Fix& f = fixFor(std::string(t(1)));
            double lat = 0.0, lon = 0.0;
            if (DegMin(t(2), t(3), lat) && DegMin(t(4), t(5), lon)) { f.lat = lat; f.lon = lon; f.hasPos = true; }
            f.quality = quality;
            f.sats = std::string(t(7));
            f.hdop = std::string(t(8));
            f.alt = std::string(t(9));
        } else if (type == "RMC") {
            // time, status A/V, lat, N/S, lon, E/W, speed kn, course, date ddmmyy, ...
            if (t(2) != "A") { ++noFix; continue; }
            Fix& f = fixFor(std::string(t(1)));
            double lat = 0.0, lon = 0.0;
            if (!f.hasPos && DegMin(t(3), t(4), lat) && DegMin(t(5), t(6), lon)) { f.lat = lat; f.lon = lon; f.hasPos = true; }
            double kn = 0.0;
            if (ParseGeoNumber(t(7), kn)) f.speedKmh = std::string(PathUtils::WideToUtf8(Fmt(kn * 1.852, 1)));
            f.course = std::string(t(8));
            std::string_view d = t(9);
            if (d.size() == 6) {
                // ddmmyy; two-digit years: 80..99 -> 19xx (GPS started in 1980), else 20xx
                std::string yy(d.substr(4, 2));
                f.date = (yy >= "80" ? "19" : "20") + yy + "-" + std::string(d.substr(2, 2)) + "-" + std::string(d.substr(0, 2));
            }
        } else if (type == "GLL") {
            // lat, N/S, lon, E/W, time, status
            if (t(6) == "V") { ++noFix; continue; }
            Fix& f = fixFor(std::string(t(5)));
            double lat = 0.0, lon = 0.0;
            if (!f.hasPos && DegMin(t(1), t(2), lat) && DegMin(t(3), t(4), lon)) { f.lat = lat; f.lon = lon; f.hasPos = true; }
        } else if (type == "VTG" && !fixes.empty()) {
            // course true, T, course magnetic, M, speed kn, N, speed km/h, K
            Fix& f = fixes.back();
            if (f.speedKmh.empty() && !t(7).empty()) f.speedKmh = std::string(t(7));
            if (f.course.empty()) f.course = std::string(t(1));
        }
    }
    if (sentences == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"NMEA 0183 GPS Receiver Log (.nmea)";
    out.geoPoints.clear();
    SpreadsheetSheet sheet;
    sheet.name = L"NMEA_GPS_Fix";
    sheet.rows.push_back({ L"#", L"UTC Time", L"Date", L"Latitude", L"Longitude", L"Altitude (m)", L"Speed (km/h)",
                           L"Course (°)", L"Fix Quality", L"Satellites", L"HDOP" });
    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };
    std::string lastDate;
    for (auto& f : fixes) {
        if (!f.date.empty()) lastDate = f.date; else f.date = lastDate;
        if (sheet.rows.size() > 20000) break;
        int row = static_cast<int>(sheet.rows.size());
        std::string tm = f.time.size() >= 6 ? f.time.substr(0, 2) + ":" + f.time.substr(2, 2) + ":" + f.time.substr(4) : f.time;
        sheet.rows.push_back({ std::to_wstring(row), w(tm), w(f.date), f.hasPos ? Fmt(f.lat, 6) : L"", f.hasPos ? Fmt(f.lon, 6) : L"",
                               w(f.alt), w(f.speedKmh), w(f.course), w(f.quality), w(f.sats), w(f.hdop) });
        if (f.hasPos) {
            double alt = 0.0;
            ParseGeoNumber(f.alt, alt);
            AddGeoPoint(out, f.lat, f.lon, alt, row, w(tm));
        }
    }
    sheet.maxCols = 11;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"NMEA_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Sentences", std::to_wstring(sentences) });
    for (const auto& sc : sentenceCounts) summary.rows.push_back({ L"Sentence " + w(sc.first), std::to_wstring(sc.second) });
    summary.rows.push_back({ L"Fixes", std::to_wstring(fixes.size()) });
    if (noFix) summary.rows.push_back({ L"Sentences without a fix", std::to_wstring(noFix) });
    if (badChecksum) summary.rows.push_back({ L"Bad checksums (skipped)", std::to_wstring(badChecksum) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
