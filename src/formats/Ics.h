#pragma once
// Ics.h — iCalendar Schedule & Events (.ics / .ical) Parser

namespace Formats {

inline bool ParseICalendar(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("BEGIN:VCALENDAR") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"iCalendar Events (.ics)";

    SpreadsheetSheet sheet;
    sheet.name = L"Events";
    sheet.rows.push_back({ L"Summary", L"Start (DTSTART)", L"End (DTEND)", L"Location", L"Description" });

    bool inEvent = false;
    std::wstring summary, dtStart, dtEnd, location, desc;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln == "BEGIN:VEVENT") {
            inEvent = true;
            summary.clear(); dtStart.clear(); dtEnd.clear(); location.clear(); desc.clear();
        } else if (ln == "END:VEVENT") {
            if (inEvent) {
                sheet.rows.push_back({ summary, dtStart, dtEnd, location, desc });
            }
            inEvent = false;
        } else if (inEvent) {
            size_t colon = ln.find(':');
            if (colon != std::string_view::npos) {
                std::string_view k = ln.substr(0, colon);
                std::wstring v = PathUtils::Utf8ToWide(ln.substr(colon + 1));
                if (k.rfind("SUMMARY", 0) == 0) summary = v;
                else if (k.rfind("DTSTART", 0) == 0) dtStart = v;
                else if (k.rfind("DTEND", 0) == 0) dtEnd = v;
                else if (k.rfind("LOCATION", 0) == 0) location = v;
                else if (k.rfind("DESCRIPTION", 0) == 0) desc = v;
            }
        }
    }
    sheet.maxCols = 5;
    out.sheets.push_back(std::move(sheet));
    out.valid = (out.sheets[0].rows.size() >= 2);
    return out.valid;
}

} // namespace Formats
