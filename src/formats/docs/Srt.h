#pragma once
#include "../common/SubtitleText.h"
// Srt.h — SubRip (.srt): numbered cues "start --> end" with one or more text lines. Timestamps are parsed
// (durations, overlaps and the total running time are reported), styling tags are removed from the text,
// and UTF-16 / legacy ANSI files decode correctly.

namespace Formats {

inline bool ParseSubRipSrt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace ST = SubtitleText;
    std::vector<std::wstring> lines = ST::SplitLines(ST::DecodeFile(raw));

    SpreadsheetSheet sheet;
    sheet.name = L"Subtitles_SRT";
    sheet.rows.push_back({ L"#", L"Start Time", L"End Time", L"Duration (s)", L"Dialogue Text" });

    size_t cues = 0, badTimings = 0, overlaps = 0, styled = 0;
    int64_t lastEnd = 0, maxEnd = 0;
    size_t i = 0;
    while (i < lines.size()) {
        // Find the next timing line; the line before it (if a number) is the cue index
        size_t arrowLine = i;
        while (arrowLine < lines.size() && lines[arrowLine].find(L"-->") == std::wstring::npos) ++arrowLine;
        if (arrowLine >= lines.size()) break;
        std::wstring idx;
        if (arrowLine > i) {
            const std::wstring& prev = lines[arrowLine - 1];
            if (!prev.empty() && prev.find_first_not_of(L"0123456789") == std::wstring::npos) idx = prev;
        }
        const std::wstring& timing = lines[arrowLine];
        size_t arrow = timing.find(L"-->");
        std::wstring startS = timing.substr(0, arrow);
        std::wstring endS = timing.substr(arrow + 3);
        // Optional "X1:... X2:..." position after the end time
        size_t sp = endS.find_first_not_of(L" \t");
        if (sp != std::wstring::npos) {
            size_t sp2 = endS.find_first_of(L" \t", sp);
            endS = endS.substr(sp, (sp2 == std::wstring::npos ? endS.size() : sp2) - sp);
        }
        int64_t startMs = 0, endMs = 0;
        bool okTime = ST::ParseTimestampMs(startS, startMs) && ST::ParseTimestampMs(endS, endMs) && endMs >= startMs;
        // Text: following lines up to a blank line
        std::vector<std::wstring> text;
        size_t j = arrowLine + 1;
        while (j < lines.size() && !lines[j].empty()) {
            // A new cue starting without a blank line in between ("12" then a timing line)
            if (j + 1 < lines.size() && lines[j + 1].find(L"-->") != std::wstring::npos &&
                lines[j].find_first_not_of(L"0123456789") == std::wstring::npos) break;
            if (lines[j].find(L"-->") != std::wstring::npos) break;
            std::wstring plain = ST::StripMarkup(lines[j], false);
            if (plain != lines[j]) ++styled;
            text.push_back(plain);
            ++j;
        }
        i = j;
        if (!okTime) {
            ++badTimings;
            continue;
        }
        ++cues;
        if (startMs < lastEnd) ++overlaps;
        lastEnd = endMs;
        maxEnd = std::max(maxEnd, endMs);
        if (sheet.rows.size() <= 20000) {
            sheet.rows.push_back({ idx.empty() ? std::to_wstring(cues) : idx, ST::FormatMs(startMs), ST::FormatMs(endMs),
                                   ST::FormatSeconds(endMs - startMs), ST::JoinCueLines(text) });
        }
    }
    if (cues == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"SubRip Video Subtitles (.srt)";
    sheet.maxCols = 5;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"SRT_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Cues", std::to_wstring(cues) });
    summary.rows.push_back({ L"Running time", ST::FormatMs(maxEnd) });
    if (overlaps) summary.rows.push_back({ L"Cues starting before the previous one ends", std::to_wstring(overlaps) });
    if (badTimings) summary.rows.push_back({ L"Cues with unreadable timing (skipped)", std::to_wstring(badTimings) });
    if (styled) summary.rows.push_back({ L"Lines with styling tags (removed)", std::to_wstring(styled) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
