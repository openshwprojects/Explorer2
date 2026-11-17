#pragma once
#include "../JsonDom.h"
// NdJson.h — Newline-Delimited JSON / JSON Lines (.ndjson / .jsonl): one JSON value per line.
// Every line is parsed with the shared JSON reader. Objects become rows with one column per key
// (keys in order of first appearance); other values go to a "value" column. Lines that are not valid
// JSON are counted and listed in the summary sheet instead of being guessed at.

namespace Formats {

inline bool ParseNdJsonDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using JVal = ::Explorer2::Json::Value;
    std::string_view sv = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));

    constexpr size_t kMaxRows = 5000;
    constexpr size_t kMaxCols = 32;
    std::vector<std::string> cols;
    bool needValueCol = false;
    struct Row {
        size_t lineNo = 0;
        JVal value;
    };
    std::vector<Row> rows;
    size_t lineNo = 0, valueLines = 0, badLines = 0, firstBadLine = 0;
    auto addRecord = [&](size_t recLine, JVal&& v) {
        ++valueLines;
        if (rows.size() >= kMaxRows) return; // keep counting, stop storing
        if (v.IsObject()) {
            for (const auto& kv : v.objVal) {
                if (cols.size() >= kMaxCols) break;
                if (std::find(cols.begin(), cols.end(), kv.first) == cols.end()) cols.push_back(kv.first);
            }
        } else {
            needValueCol = true;
        }
        rows.push_back({ recLine, std::move(v) });
    };
    bool wholeDocument = false;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        if (ln.empty()) continue;

        JVal v;
        if (!::Explorer2::Json::ParseDocument(ln, v)) {
            if (valueLines == 0) {
                // First record does not parse on its own: maybe a pretty-printed JSON document saved as
                // .jsonl; an array of records is shown the same way, anything else is not JSON Lines.
                JVal doc;
                if (!::Explorer2::Json::ParseDocument(sv, doc) || !(doc.IsArray() || doc.IsObject())) return false;
                wholeDocument = true;
                lineNo = 0;
                if (doc.IsArray()) {
                    size_t n = 0;
                    for (auto& item : doc.arrVal) addRecord(++n, std::move(item));
                } else {
                    addRecord(1, std::move(doc));
                }
                break;
            }
            ++badLines;
            if (firstBadLine == 0) firstBadLine = lineNo;
            continue;
        }
        addRecord(lineNo, std::move(v));
    }
    if (rows.empty()) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"Newline-Delimited JSON Stream (.ndjson / .jsonl)";

    SpreadsheetSheet sheet;
    sheet.name = L"JSON_Lines";
    std::vector<std::wstring> header = { wholeDocument ? L"Record" : L"Line" };
    for (const auto& c : cols) header.push_back(PathUtils::Utf8ToWide(c));
    if (needValueCol) header.push_back(L"value");
    sheet.rows.push_back(header);
    for (const auto& r : rows) {
        std::vector<std::wstring> cells(header.size());
        cells[0] = std::to_wstring(r.lineNo);
        auto cellText = [](const JVal& v) {
            return v.IsString() ? PathUtils::Utf8ToWide(v.strVal)
                                : PathUtils::Utf8ToWide(::Explorer2::Json::Compact(v, 200));
        };
        if (r.value.IsObject()) {
            for (size_t c = 0; c < cols.size(); ++c) {
                if (const JVal* fv = r.value.Find(cols[c])) cells[c + 1] = cellText(*fv);
            }
        } else {
            cells.back() = cellText(r.value);
        }
        sheet.rows.push_back(std::move(cells));
    }
    sheet.maxCols = header.size();
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet summary;
    summary.name = L"JSON_Lines_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    if (wholeDocument) summary.rows.push_back({ L"Layout", L"Single JSON document (not one record per line)" });
    else summary.rows.push_back({ L"Lines", std::to_wstring(lineNo) });
    summary.rows.push_back({ L"JSON records", std::to_wstring(valueLines) });
    summary.rows.push_back({ L"Records shown", std::to_wstring(rows.size()) });
    summary.rows.push_back({ L"Columns", std::to_wstring(cols.size()) + (cols.size() >= kMaxCols ? L" (capped)" : L"") });
    summary.rows.push_back({ L"Invalid lines", std::to_wstring(badLines) +
                             (badLines ? L" (first at line " + std::to_wstring(firstBadLine) + L")" : L"") });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));

    out.valid = true;
    return true;
}

} // namespace Formats
