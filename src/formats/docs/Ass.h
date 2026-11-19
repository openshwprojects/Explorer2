#pragma once
#include "../common/SubtitleText.h"
// Ass.h — SubStation Alpha v4 / Advanced SubStation Alpha v4+ (.ssa / .ass): [Script Info] properties,
// [V4 Styles] / [V4+ Styles] style names, and [Events] Dialogue / Comment lines whose columns follow the
// section's own "Format:" line (the Text field keeps its commas). Override blocks {\...} are removed and
// \N / \h become a line break / space.

namespace Formats {

inline bool ParseSubStationAss(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace ST = SubtitleText;
    std::vector<std::wstring> lines = ST::SplitLines(ST::DecodeFile(raw));

    auto trim = [](std::wstring s) {
        size_t a = s.find_first_not_of(L" \t");
        size_t b = s.find_last_not_of(L" \t");
        return (a == std::wstring::npos) ? std::wstring() : s.substr(a, b - a + 1);
    };
    auto lowerOf = [](std::wstring s) {
        for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
        return s;
    };

    std::wstring section;
    std::vector<std::wstring> eventFormat; // lower-case field names
    std::vector<std::pair<std::wstring, std::wstring>> info;
    std::vector<std::wstring> styleNames;
    bool sawScriptInfo = false;

    SpreadsheetSheet sheet;
    sheet.name = L"Events_ASS";
    sheet.rows.push_back({ L"Start", L"End", L"Style", L"Actor", L"Dialogue Text", L"Kind", L"Layer", L"Effect" });
    size_t dialogues = 0, comments = 0;
    int64_t maxEnd = 0;

    for (const auto& ln : lines) {
        if (ln.empty() || ln[0] == L';') continue;
        if (ln[0] == L'[') {
            section = lowerOf(trim(ln));
            if (section == L"[script info]") sawScriptInfo = true;
            continue;
        }
        size_t colon = ln.find(L':');
        if (colon == std::wstring::npos) continue;
        std::wstring key = trim(ln.substr(0, colon));
        std::wstring val = trim(ln.substr(colon + 1));
        std::wstring keyLow = lowerOf(key);

        if (section == L"[script info]") {
            if (info.size() < 64) info.emplace_back(key, val);
        } else if (section == L"[v4+ styles]" || section == L"[v4 styles]") {
            if (keyLow == L"style") {
                size_t c = val.find(L',');
                if (styleNames.size() < 256) styleNames.push_back(trim(val.substr(0, c)));
            }
        } else if (section == L"[events]") {
            if (keyLow == L"format") {
                eventFormat.clear();
                size_t s = 0;
                while (s <= val.size()) {
                    size_t c = val.find(L',', s);
                    if (c == std::wstring::npos) c = val.size();
                    eventFormat.push_back(lowerOf(trim(val.substr(s, c - s))));
                    s = c + 1;
                }
                continue;
            }
            bool isDialogue = (keyLow == L"dialogue");
            if (!isDialogue && keyLow != L"comment") continue;
            // Default v4+ order when the file has no Format line
            std::vector<std::wstring> fmt = eventFormat.empty()
                ? std::vector<std::wstring>{ L"layer", L"start", L"end", L"style", L"name", L"marginl", L"marginr", L"marginv", L"effect", L"text" }
                : eventFormat;
            std::vector<std::wstring> fields;
            size_t s = 0;
            for (size_t f = 0; f + 1 < fmt.size(); ++f) {
                size_t c = val.find(L',', s);
                if (c == std::wstring::npos) break;
                fields.push_back(trim(val.substr(s, c - s)));
                s = c + 1;
            }
            if (fields.size() + 1 != fmt.size()) continue; // malformed line
            fields.push_back(val.substr(std::min(s, val.size()))); // Text keeps its commas
            auto field = [&](const wchar_t* name) -> std::wstring {
                for (size_t f = 0; f < fmt.size(); ++f) {
                    if (fmt[f] == name) return fields[f];
                }
                return {};
            };
            int64_t a = 0, b = 0;
            bool okA = ST::ParseTimestampMs(field(L"start"), a);
            bool okB = ST::ParseTimestampMs(field(L"end"), b);
            if (okB) maxEnd = std::max(maxEnd, b);
            std::wstring text = ST::StripMarkup(field(L"text"), true);
            for (auto& ch : text) if (ch == L'\n') ch = L' ';
            if (isDialogue) ++dialogues; else ++comments;
            if (sheet.rows.size() <= 20000) {
                std::wstring actor = field(L"name");
                if (actor.empty()) actor = field(L"actor");
                sheet.rows.push_back({ okA ? ST::FormatMs(a) : field(L"start"), okB ? ST::FormatMs(b) : field(L"end"),
                                       field(L"style"), actor, text, isDialogue ? L"Dialogue" : L"Comment",
                                       field(L"layer"), field(L"effect") });
            }
        }
    }
    if (!sawScriptInfo && dialogues + comments == 0) return false;
    if (dialogues + comments == 0 && info.empty()) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"SubStation Alpha Subtitles (.ass)";
    sheet.maxCols = 8;
    out.sheets.push_back(std::move(sheet));
    SpreadsheetSheet summary;
    summary.name = L"Script_Info";
    summary.rows.push_back({ L"Property", L"Value" });
    for (const auto& kv : info) summary.rows.push_back({ kv.first, kv.second });
    summary.rows.push_back({ L"Dialogue lines", std::to_wstring(dialogues) });
    if (comments) summary.rows.push_back({ L"Comment lines", std::to_wstring(comments) });
    summary.rows.push_back({ L"Running time", ST::FormatMs(maxEnd) });
    if (!styleNames.empty()) {
        std::wstring joined;
        for (const auto& n : styleNames) joined += (joined.empty() ? L"" : L", ") + n;
        summary.rows.push_back({ L"Styles", joined });
    }
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace Formats
