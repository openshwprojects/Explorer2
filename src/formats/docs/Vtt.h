#pragma once
#include "../common/SubtitleText.h"
// Vtt.h — WebVTT (.vtt, W3C): "WEBVTT" header, cues with optional identifiers and cue settings,
// NOTE / STYLE / REGION blocks skipped, <v Speaker> voice spans shown as the speaker column,
// other markup and karaoke timestamps removed from the text.

namespace Formats {

inline bool ParseWebVtt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace ST = SubtitleText;
    std::vector<std::wstring> lines = ST::SplitLines(ST::DecodeFile(raw));
    if (lines.empty() || lines[0].compare(0, 6, L"WEBVTT") != 0 ||
        (lines[0].size() > 6 && lines[0][6] != L' ' && lines[0][6] != L'\t')) {
        return false;
    }

    SpreadsheetSheet sheet;
    sheet.name = L"Subtitles_VTT";
    sheet.rows.push_back({ L"#", L"Cue ID", L"Start", L"End", L"Duration (s)", L"Speaker", L"Text", L"Settings" });
    size_t cues = 0, notes = 0, styles = 0, regions = 0;
    int64_t maxEnd = 0;
    std::wstring headerText = lines[0].size() > 7 ? lines[0].substr(7) : std::wstring();

    size_t i = 1;
    // Header metadata lines run until the first blank line
    while (i < lines.size() && !lines[i].empty()) ++i;
    while (i < lines.size()) {
        if (lines[i].empty()) { ++i; continue; }
        // Block: up to the next blank line
        size_t end = i;
        while (end < lines.size() && !lines[end].empty()) ++end;
        const std::wstring& first = lines[i];
        if (first.compare(0, 4, L"NOTE") == 0 && (first.size() == 4 || first[4] == L' ' || first[4] == L'\t')) {
            ++notes;
        } else if (first == L"STYLE") {
            ++styles;
        } else if (first == L"REGION") {
            ++regions;
        } else {
            size_t timingLine = i;
            std::wstring cueId;
            if (first.find(L"-->") == std::wstring::npos && i + 1 < end) {
                cueId = first;
                timingLine = i + 1;
            }
            const std::wstring& tl = lines[timingLine];
            size_t arrow = tl.find(L"-->");
            if (arrow != std::wstring::npos) {
                std::wstring startS = tl.substr(0, arrow);
                std::wstring rest = tl.substr(arrow + 3);
                size_t s0 = rest.find_first_not_of(L" \t");
                std::wstring endS, settings;
                if (s0 != std::wstring::npos) {
                    size_t s1 = rest.find_first_of(L" \t", s0);
                    endS = rest.substr(s0, (s1 == std::wstring::npos ? rest.size() : s1) - s0);
                    if (s1 != std::wstring::npos) {
                        size_t s2 = rest.find_first_not_of(L" \t", s1);
                        if (s2 != std::wstring::npos) settings = rest.substr(s2);
                    }
                }
                int64_t a = 0, b = 0;
                if (ST::ParseTimestampMs(startS, a) && ST::ParseTimestampMs(endS, b) && b >= a) {
                    ++cues;
                    maxEnd = std::max(maxEnd, b);
                    std::wstring speaker;
                    std::vector<std::wstring> text;
                    for (size_t k = timingLine + 1; k < end; ++k) {
                        const std::wstring& t = lines[k];
                        size_t v = t.find(L"<v");
                        if (speaker.empty() && v != std::wstring::npos && v + 2 < t.size() &&
                            (t[v + 2] == L' ' || t[v + 2] == L'.' || t[v + 2] == L'\t')) {
                            size_t nameStart = t.find(L' ', v);
                            size_t close = t.find(L'>', v);
                            if (nameStart != std::wstring::npos && close != std::wstring::npos && nameStart < close) {
                                speaker = t.substr(nameStart + 1, close - nameStart - 1);
                            }
                        }
                        text.push_back(ST::StripMarkup(t, false));
                    }
                    if (sheet.rows.size() <= 20000) {
                        sheet.rows.push_back({ std::to_wstring(cues), cueId, ST::FormatMs(a), ST::FormatMs(b),
                                               ST::FormatSeconds(b - a), speaker, ST::JoinCueLines(text), settings });
                    }
                }
            }
        }
        i = end;
    }
    if (cues == 0 && notes == 0) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"WebVTT Video Subtitles (.vtt)";
    sheet.maxCols = 8;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"VTT_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    if (!headerText.empty()) summary.rows.push_back({ L"Header text", headerText });
    summary.rows.push_back({ L"Cues", std::to_wstring(cues) });
    summary.rows.push_back({ L"Running time", ST::FormatMs(maxEnd) });
    if (notes) summary.rows.push_back({ L"NOTE blocks", std::to_wstring(notes) });
    if (styles) summary.rows.push_back({ L"STYLE blocks", std::to_wstring(styles) });
    if (regions) summary.rows.push_back({ L"REGION blocks", std::to_wstring(regions) });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
