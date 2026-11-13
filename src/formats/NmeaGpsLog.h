#pragma once
// NmeaGpsLog.h — NMEA 0183 Marine/Drone/Receiver GPS Log (.nmea / $GPGGA / $GPRMC .log) Parser

namespace Formats {

inline double ConvertNmeaDegMin(std::string_view dm, char hemi) {
    if (dm.empty()) return 0.0;
    double raw = std::strtod(std::string(dm).c_str(), nullptr);
    int deg = static_cast<int>(raw / 100.0);
    double mins = raw - deg * 100.0;
    double dec = deg + (mins / 60.0);
    if (hemi == 'S' || hemi == 'W' || hemi == 's' || hemi == 'w') dec = -dec;
    return dec;
}

inline bool ParseNmeaGpsLogDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("$GP") == std::string_view::npos && sv.find("$GN") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"NMEA 0183 GPS Receiver Log (.nmea)";

    SpreadsheetSheet sheet;
    sheet.name = L"NMEA_GPS_Fix";
    sheet.rows.push_back({ L"#", L"Latitude", L"Longitude", L"Elevation (m)", L"UTC Time / Sentence" });

    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.size() < 12 || ln[0] != '$') continue;

        std::vector<std::string_view> tok;
        size_t p = 0;
        while (p <= ln.size()) {
            size_t c = ln.find(',', p);
            if (c == std::string_view::npos) c = ln.size();
            tok.push_back(ln.substr(p, c - p));
            p = c + 1;
        }

        if (tok.size() >= 10 && (tok[0].find("GGA") != std::string_view::npos)) {
            double lat = ConvertNmeaDegMin(tok[2], tok[3].empty() ? 'N' : tok[3][0]);
            double lon = ConvertNmeaDegMin(tok[4], tok[5].empty() ? 'E' : tok[5][0]);
            std::wstring alt = PathUtils::Utf8ToWide(tok[9]);
            std::wstring utc = PathUtils::Utf8ToWide(tok[1]);
            wchar_t latB[32] = {}, lonB[32] = {};
            swprintf_s(latB, L"%.6f", lat);
            swprintf_s(lonB, L"%.6f", lon);
            sheet.rows.push_back({ std::to_wstring(idx++), latB, lonB, alt, L"GGA @" + utc });
        } else if (tok.size() >= 8 && (tok[0].find("RMC") != std::string_view::npos)) {
            double lat = ConvertNmeaDegMin(tok[3], tok[4].empty() ? 'N' : tok[4][0]);
            double lon = ConvertNmeaDegMin(tok[5], tok[6].empty() ? 'E' : tok[6][0]);
            std::wstring spd = PathUtils::Utf8ToWide(tok[7]);
            std::wstring utc = PathUtils::Utf8ToWide(tok[1]);
            wchar_t latB[32] = {}, lonB[32] = {};
            swprintf_s(latB, L"%.6f", lat);
            swprintf_s(lonB, L"%.6f", lon);
            sheet.rows.push_back({ std::to_wstring(idx++), latB, lonB, spd, L"RMC @" + utc });
        }
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
