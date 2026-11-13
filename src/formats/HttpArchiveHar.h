#pragma once
// HttpArchiveHar.h — W3C HTTP Archive (.har) Network Traffic Waterfall Table Parser

namespace Formats {

inline bool ParseHttpArchiveHarDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("\"entries\"") == std::string_view::npos && sv.find("\"request\"") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"HTTP Archive Network Log (.har)";

    SpreadsheetSheet sheet;
    sheet.name = L"HAR_Requests";
    sheet.rows.push_back({ L"#", L"Method", L"Status", L"URL", L"MIME Type", L"Time (ms)" });

    auto findJsonStr = [](std::string_view blk, const char* key) -> std::wstring {
        size_t p = blk.find(key);
        if (p == std::string_view::npos) return L"";
        size_t colon = blk.find(':', p + std::strlen(key));
        if (colon == std::string_view::npos) return L"";
        size_t q1 = blk.find('"', colon + 1);
        size_t q2 = (q1 != std::string_view::npos) ? blk.find('"', q1 + 1) : std::string_view::npos;
        if (q1 == std::string_view::npos || q2 == std::string_view::npos) return L"";
        return PathUtils::Utf8ToWide(blk.substr(q1 + 1, q2 - q1 - 1));
    };
    auto findJsonNum = [](std::string_view blk, const char* key) -> std::wstring {
        size_t p = blk.find(key);
        if (p == std::string_view::npos) return L"";
        size_t colon = blk.find(':', p + std::strlen(key));
        if (colon == std::string_view::npos) return L"";
        size_t vs = colon + 1;
        while (vs < blk.size() && (blk[vs] == ' ' || blk[vs] == '\t')) ++vs;
        size_t ve = vs;
        while (ve < blk.size() && (std::isdigit(static_cast<unsigned char>(blk[ve])) || blk[ve] == '.')) ++ve;
        return PathUtils::Utf8ToWide(blk.substr(vs, ve - vs));
    };

    int idx = 1;
    size_t pos = 0;
    while (pos < sv.size() && idx <= 5000) {
        size_t req = sv.find("\"request\"", pos);
        if (req == std::string_view::npos) break;
        size_t nextReq = sv.find("\"request\"", req + 9);
        std::string_view blk = sv.substr(req, ((nextReq != std::string_view::npos) ? nextReq : sv.size()) - req);

        std::wstring method = findJsonStr(blk, "\"method\"");
        std::wstring url    = findJsonStr(blk, "\"url\"");
        std::wstring status = findJsonNum(blk, "\"status\"");
        std::wstring mime   = findJsonStr(blk, "\"mimeType\"");
        std::wstring timeMs = findJsonNum(blk, "\"time\"");
        sheet.rows.push_back({ std::to_wstring(idx++), method, status, url, mime, timeMs });
        pos = (nextReq != std::string_view::npos) ? nextReq : sv.size();
    }

    sheet.maxCols = 6;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
