#pragma once
// IgcFlightLog.h — FAI Glider / Drone IGC Flight Recorder Log (.igc) GPS & Barometric Altitude Parser

namespace Formats {

inline bool ParseIgcFlightLogDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.empty() || (sv[0] != 'A' && sv.find("\nB") == std::string_view::npos)) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"FAI IGC Flight Recorder Log (.igc)";

    SpreadsheetSheet sheet;
    sheet.name = L"IGC_Flight_Track";
    sheet.rows.push_back({ L"#", L"Latitude", L"Longitude", L"Elevation (m)", L"UTC Time" });

    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        // B-record: B HHMMSS DDMMmmmN DDDMMmmmE A PPPPP GGGGG
        if (ln.size() >= 35 && ln[0] == 'B') {
            std::string_view hhmmss = ln.substr(1, 6);
            int latDeg = std::atoi(std::string(ln.substr(7, 2)).c_str());
            double latMin = std::atof(std::string(ln.substr(9, 5)).c_str()) / 1000.0;
            double lat = latDeg + latMin / 60.0;
            if (ln[14] == 'S') lat = -lat;

            int lonDeg = std::atoi(std::string(ln.substr(15, 3)).c_str());
            double lonMin = std::atof(std::string(ln.substr(18, 5)).c_str()) / 1000.0;
            double lon = lonDeg + lonMin / 60.0;
            if (ln[23] == 'W') lon = -lon;

            int gpsAlt = std::atoi(std::string(ln.substr(30, 5)).c_str());
            wchar_t latB[32] = {}, lonB[32] = {}, timeB[32] = {};
            swprintf_s(latB, L"%.6f", lat);
            swprintf_s(lonB, L"%.6f", lon);
            swprintf_s(timeB, L"%c%c:%c%c:%c%c UTC", hhmmss[0], hhmmss[1], hhmmss[2], hhmmss[3], hhmmss[4], hhmmss[5]);
            sheet.rows.push_back({ std::to_wstring(idx++), latB, lonB, std::to_wstring(gpsAlt), timeB });
        }
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
