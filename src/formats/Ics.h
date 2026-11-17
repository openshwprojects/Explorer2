#pragma once
#include "ContentLines.h"
// Ics.h — iCalendar (RFC 5545, .ics / .ical): VEVENT, VTODO and VJOURNAL components read from unfolded
// content lines (long lines folded at 75 octets by Google / Outlook exports are joined again), with
// TEXT escapes removed, TZID / all-day dates shown, recurrence rules, organizer and attendee counts.

namespace Formats {

inline bool ParseICalendar(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace CL = ContentLines;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::vector<CL::Line> lines = CL::Parse(sv);
    if (lines.empty() || lines[0].name != "BEGIN" || CL::ToUpperAscii(lines[0].value) != "VCALENDAR") return false;

    SpreadsheetSheet sheet;
    sheet.name = L"Events";
    sheet.rows.push_back({ L"Type", L"Summary", L"Start", L"End / Due", L"Location", L"Status", L"Recurrence",
                           L"Organizer", L"Attendees", L"Description" });
    SpreadsheetSheet summary;
    summary.name = L"Calendar";
    summary.rows.push_back({ L"Property", L"Value" });

    struct Item {
        std::wstring type, summ, start, end, loc, status, rrule, org, desc;
        size_t attendees = 0;
    };
    std::vector<std::string> stack;
    Item cur;
    bool inItem = false;
    size_t events = 0, todos = 0, journals = 0, timezones = 0, alarms = 0;
    std::wstring firstStart, lastStart; // lexical order of the raw values (YYYYMMDD...) is chronological
    std::string firstRaw, lastRaw;

    for (const auto& l : lines) {
        if (l.name == "BEGIN") {
            std::string comp = CL::ToUpperAscii(l.value);
            stack.push_back(comp);
            if (stack.size() > 16) return false;
            if (comp == "VEVENT" || comp == "VTODO" || comp == "VJOURNAL") {
                inItem = true;
                cur = Item{};
                cur.type = (comp == "VEVENT") ? L"Event" : (comp == "VTODO" ? L"To-do" : L"Journal");
            } else if (comp == "VTIMEZONE") {
                ++timezones;
            } else if (comp == "VALARM") {
                ++alarms;
            }
            continue;
        }
        if (l.name == "END") {
            std::string comp = CL::ToUpperAscii(l.value);
            if (stack.empty() || stack.back() != comp) continue; // tolerate stray END lines
            stack.pop_back();
            if (inItem && (comp == "VEVENT" || comp == "VTODO" || comp == "VJOURNAL")) {
                inItem = false;
                if (comp == "VEVENT") ++events; else if (comp == "VTODO") ++todos; else ++journals;
                if (sheet.rows.size() <= 10000) {
                    std::wstring desc = cur.desc;
                    for (auto& ch : desc) if (ch == L'\n' || ch == L'\r') ch = L' ';
                    if (desc.size() > 400) desc = desc.substr(0, 400) + L"...";
                    sheet.rows.push_back({ cur.type, cur.summ, cur.start, cur.end, cur.loc, cur.status, cur.rrule, cur.org,
                                           cur.attendees ? std::to_wstring(cur.attendees) : std::wstring(), desc });
                }
            }
            continue;
        }
        // Properties of the item itself (not of a nested VALARM)
        if (inItem && !stack.empty() && (stack.back() == "VEVENT" || stack.back() == "VTODO" || stack.back() == "VJOURNAL")) {
            if (l.name == "SUMMARY") cur.summ = CL::TextValue(l);
            else if (l.name == "DTSTART") {
                cur.start = CL::FormatDateTime(l);
                if (firstRaw.empty() || l.value < firstRaw) { firstRaw = l.value; firstStart = cur.start; }
                if (lastRaw.empty() || l.value > lastRaw) { lastRaw = l.value; lastStart = cur.start; }
            }
            else if (l.name == "DTEND" || l.name == "DUE") cur.end = CL::FormatDateTime(l);
            else if (l.name == "DURATION" && cur.end.empty()) cur.end = L"+" + PathUtils::Utf8ToWide(l.value);
            else if (l.name == "LOCATION") cur.loc = CL::TextValue(l);
            else if (l.name == "STATUS") cur.status = PathUtils::Utf8ToWide(l.value);
            else if (l.name == "RRULE") cur.rrule = PathUtils::Utf8ToWide(l.value);
            else if (l.name == "DESCRIPTION") cur.desc = CL::TextValue(l);
            else if (l.name == "ORGANIZER") {
                std::string cn = l.ParamValue("CN");
                std::string addr = l.value;
                if (addr.size() > 7 && CL::ToUpperAscii(addr.substr(0, 7)) == "MAILTO:") addr = addr.substr(7);
                cur.org = PathUtils::Utf8ToWide(cn.empty() ? addr : (cn + " <" + addr + ">"));
            }
            else if (l.name == "ATTENDEE") ++cur.attendees;
            continue;
        }
        if (stack.size() == 1) { // calendar-level properties
            if (l.name == "PRODID") summary.rows.push_back({ L"Producer", CL::TextValue(l) });
            else if (l.name == "VERSION") summary.rows.push_back({ L"Version", PathUtils::Utf8ToWide(l.value) });
            else if (l.name == "METHOD") summary.rows.push_back({ L"Method", PathUtils::Utf8ToWide(l.value) });
            else if (l.name == "X-WR-CALNAME") summary.rows.push_back({ L"Calendar name", CL::TextValue(l) });
            else if (l.name == "X-WR-TIMEZONE") summary.rows.push_back({ L"Time zone", CL::TextValue(l) });
        }
    }
    if (events + todos + journals == 0 && timezones == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"iCalendar Events (.ics)";
    summary.rows.push_back({ L"Events", std::to_wstring(events) });
    if (todos) summary.rows.push_back({ L"To-dos", std::to_wstring(todos) });
    if (journals) summary.rows.push_back({ L"Journal entries", std::to_wstring(journals) });
    if (alarms) summary.rows.push_back({ L"Alarms", std::to_wstring(alarms) });
    if (timezones) summary.rows.push_back({ L"Time zone definitions", std::to_wstring(timezones) });
    if (!firstStart.empty()) summary.rows.push_back({ L"First start", firstStart });
    if (!lastStart.empty()) summary.rows.push_back({ L"Last start", lastStart });
    sheet.maxCols = 10;
    summary.maxCols = 2;
    out.sheets.push_back(std::move(sheet));
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
