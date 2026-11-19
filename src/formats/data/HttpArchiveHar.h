#pragma once
#include "../../JsonDom.h"
// HttpArchiveHar.h — W3C HTTP Archive 1.1/1.2 (.har): log.entries[] streamed from the JSON document.
// One row per request (start time, method, status, URL, MIME type, body size, total time, server IP)
// plus a summary sheet (creator, browser, pages, totals, status classes).

namespace Formats {

inline bool ParseHttpArchiveHarDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    using JVal = ::Explorer2::Json::Value;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));

    auto w = [](const std::string& s) { return PathUtils::Utf8ToWide(s); };
    auto num = [](const JVal* v, double& outVal) {
        if (!v || !v->IsNumber() || !std::isfinite(v->numVal)) return false;
        outVal = v->numVal;
        return true;
    };
    auto fmtMs = [](double ms) {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", ms);
        return std::wstring(buf);
    };

    SpreadsheetSheet sheet;
    sheet.name = L"HAR_Requests";
    sheet.rows.push_back({ L"#", L"Started", L"Method", L"Status", L"URL", L"MIME Type", L"Size (bytes)", L"Time (ms)", L"Server IP" });
    size_t entryCount = 0;
    double totalMs = 0.0, totalBytes = 0.0;
    size_t statusClass[6] = {};

    auto handleEntry = [&](const JVal& e) {
        if (!e.IsObject()) return;
        ++entryCount;
        const JVal* req = e.Find("request");
        const JVal* resp = e.Find("response");
        std::wstring method, url, status, mime, size, timeStr;
        if (req && req->IsObject()) {
            method = w(req->GetString("method"));
            url = w(req->GetString("url"));
        }
        if (resp && resp->IsObject()) {
            double st = 0.0;
            if (num(resp->Find("status"), st)) {
                status = std::to_wstring(static_cast<long long>(st));
                std::string stText = resp->GetString("statusText");
                if (!stText.empty()) status += L" " + w(stText);
                int cls = static_cast<int>(st) / 100;
                statusClass[(cls >= 1 && cls <= 5) ? cls : 0]++;
            }
            if (const JVal* content = resp->Find("content"); content && content->IsObject()) {
                mime = w(content->GetString("mimeType"));
                double sz = 0.0;
                if (num(content->Find("size"), sz) && sz >= 0.0) {
                    size = std::to_wstring(static_cast<long long>(sz));
                    totalBytes += sz;
                }
            }
        }
        double t = 0.0;
        if (num(e.Find("time"), t) && t >= 0.0) {
            timeStr = fmtMs(t);
            totalMs += t;
        }
        if (sheet.rows.size() <= 5000) {
            sheet.rows.push_back({ std::to_wstring(entryCount), w(e.GetString("startedDateTime")), method, status, url, mime,
                                   size, timeStr, w(e.GetString("serverIPAddress")) });
        }
    };

    // log.entries is read one entry at a time (a HAR with response bodies can be hundreds of MB, and one cut
    // short by the read limit still lists every request before the cut); the other log members are kept
    ::Explorer2::Json::Limits limits;
    limits.maxNodes = 4000000; // per entry (the stream resets it), and for the other members
    ::Explorer2::Json::Parser parser(text, limits);
    JVal logObj;
    logObj.kind = JVal::Kind::Object;
    bool sawLog = false, sawEntries = false;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "log" && !sawLog && parser.Peek() == '{') {
            sawLog = true;
            return parser.StreamObject([&](const std::string& logKey) {
                if (logKey == "entries" && !sawEntries && parser.Peek() == '[') {
                    sawEntries = true;
                    return parser.StreamArray([&](JVal&& e) { handleEntry(e); });
                }
                JVal v;
                if (!parser.ParseValue(v)) return false;
                logObj.objVal.emplace_back(logKey, std::move(v));
                return true;
            }, 1);
        }
        JVal skipped;
        return parser.ParseValue(skipped);
    }) && parser.AtEnd();
    if (!sawEntries || (!complete && entryCount == 0)) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"HTTP Archive Network Log (.har)";
    sheet.maxCols = 9;
    out.sheets.push_back(std::move(sheet));

    SpreadsheetSheet summary;
    summary.name = L"HAR_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"HAR version", w(logObj.GetString("version")) });
    for (const char* who : { "creator", "browser" }) {
        if (const JVal* c = logObj.Find(who); c && c->IsObject()) {
            std::wstring label = (who[0] == 'c') ? L"Creator" : L"Browser";
            summary.rows.push_back({ label, w(c->GetString("name")) + L" " + w(c->GetString("version")) });
        }
    }
    if (const JVal* pages = logObj.Find("pages"); pages && pages->IsArray()) {
        summary.rows.push_back({ L"Pages", std::to_wstring(pages->arrVal.size()) });
        for (size_t i = 0; i < pages->arrVal.size() && i < 20; ++i) {
            const JVal& pg = pages->arrVal[i];
            if (pg.IsObject()) summary.rows.push_back({ L"Page " + std::to_wstring(i + 1), w(pg.GetString("title")) });
        }
    }
    summary.rows.push_back({ L"Requests", std::to_wstring(entryCount) + (entryCount > 5000 ? L" (first 5000 listed)" : L"") });
    summary.rows.push_back({ L"Total time (ms)", fmtMs(totalMs) });
    summary.rows.push_back({ L"Total body size (bytes)", std::to_wstring(static_cast<long long>(totalBytes)) });
    const wchar_t* clsNames[6] = { L"Other status", L"1xx", L"2xx", L"3xx", L"4xx", L"5xx" };
    for (int c = 0; c < 6; ++c) {
        if (statusClass[c]) summary.rows.push_back({ clsNames[c], std::to_wstring(statusClass[c]) });
    }
    if (!complete) summary.rows.push_back({ L"Document", L"incomplete (cut short or damaged); requests before that point are shown" });
    summary.maxCols = 2;
    out.sheets.push_back(std::move(summary));

    out.valid = true; // a log with zero entries is still a valid (empty) archive
    return true;
}

} // namespace Formats
