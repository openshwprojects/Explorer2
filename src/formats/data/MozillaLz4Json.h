#pragma once
// MozillaLz4Json.h — Mozilla Firefox LZ4-Compressed JSON (.jsonlz4, .mozlz4, .baklz4) Reader

namespace Formats {
namespace DetailMozLz4 {

using JVal = ::Explorer2::Json::Value;

inline std::wstring JStr(const JVal& obj, std::string_view key) {
    const JVal* p = obj.Find(key);
    if (!p) return L"";
    if (p->kind == JVal::Kind::String) return PathUtils::Utf8ToWide(p->strVal);
    if (p->kind == JVal::Kind::Number) return AiDataDetail::FmtDouble(p->numVal);
    if (p->kind == JVal::Kind::Bool)   return p->boolVal ? L"true" : L"false";
    return L"";
}

inline std::wstring FormatMozTimestamp(const JVal& obj, std::string_view key, bool isMicroseconds) {
    const JVal* p = obj.Find(key);
    int64_t v = 0;
    if (!p || !p->AsInt64(v) || v <= 0) return L"";
    int64_t sec = isMicroseconds ? (v / 1000000LL) : (v / 1000LL);
    if (sec <= 0 || sec > 4102444800LL) return std::to_wstring(v);
    return GeoSciDetail::FormatUtcEpoch(sec);
}

inline void WalkBookmarksNode(const JVal& node, const std::wstring& folderPath, SpreadsheetSheet& sh, int depth) {
    if (depth > 24 || sh.rows.size() > 3000 || node.kind != JVal::Kind::Object) return;
    std::wstring title = JStr(node, "title");
    std::wstring uri   = JStr(node, "uri");
    std::wstring guid  = JStr(node, "guid");
    std::wstring tags  = JStr(node, "tags");
    std::wstring kw    = JStr(node, "keyword");
    if (!kw.empty()) tags = tags.empty() ? (L"kw:" + kw) : (tags + L" (kw:" + kw + L")");

    if (!uri.empty()) {
        sh.rows.push_back({
            folderPath.empty() ? L"/" : folderPath,
            title,
            uri,
            tags,
            FormatMozTimestamp(node, "dateAdded", true),
            FormatMozTimestamp(node, "lastModified", true),
            guid
        });
    }

    const JVal* children = node.Find("children");
    if (children && children->kind == JVal::Kind::Array) {
        std::wstring nextPath = folderPath;
        if (!title.empty()) {
            nextPath = folderPath.empty() ? title : (folderPath + L" / " + title);
        }
        for (const auto& ch : children->arrVal) {
            WalkBookmarksNode(ch, nextPath, sh, depth + 1);
            if (sh.rows.size() > 3000) break;
        }
    }
}

inline void FlattenJsonFields(const JVal& v, const std::wstring& path, SpreadsheetSheet& sh, int depth) {
    if (depth > 20 || sh.rows.size() > 800) return;
    switch (v.kind) {
        case JVal::Kind::Null:
            sh.rows.push_back({ path.empty() ? L"(root)" : path, L"null", L"null" });
            break;
        case JVal::Kind::Bool:
            sh.rows.push_back({ path.empty() ? L"(root)" : path, L"bool", v.boolVal ? L"true" : L"false" });
            break;
        case JVal::Kind::Number:
            sh.rows.push_back({ path.empty() ? L"(root)" : path, L"number", AiDataDetail::FmtDouble(v.numVal) });
            break;
        case JVal::Kind::String:
            sh.rows.push_back({ path.empty() ? L"(root)" : path, L"string", PathUtils::Utf8ToWide(v.strVal.substr(0, 256)) });
            break;
        case JVal::Kind::Array:
            if (v.arrVal.empty()) {
                sh.rows.push_back({ path.empty() ? L"(root)" : path, L"array[0]", L"[]" });
            } else {
                for (size_t i = 0; i < v.arrVal.size() && sh.rows.size() <= 800; ++i) {
                    FlattenJsonFields(v.arrVal[i], path + L"[" + std::to_wstring(i) + L"]", sh, depth + 1);
                }
            }
            break;
        case JVal::Kind::Object:
            if (v.objVal.empty()) {
                sh.rows.push_back({ path.empty() ? L"(root)" : path, L"object{}", L"{}" });
            } else {
                for (const auto& kv : v.objVal) {
                    std::wstring kw = PathUtils::Utf8ToWide(kv.first);
                    FlattenJsonFields(kv.second, path.empty() ? kw : (path + L"." + kw), sh, depth + 1);
                    if (sh.rows.size() > 800) break;
                }
            }
            break;
    }
}

} // namespace DetailMozLz4

inline bool ParseMozillaLz4Json(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 12 || std::memcmp(raw.data(), "mozLz40\0", 8) != 0) return false;
    const uint32_t uncompLen = ::Explorer2::ByteIO::RdU32LE(raw.data() + 8);
    if (uncompLen > 128u * 1024u * 1024u) return false;

    std::vector<uint8_t> decoded;
    if (!::Explorer2::Lz4::DecodeBlockExact(raw.data() + 12, raw.size() - 12, uncompLen, decoded)) {
        return false;
    }

    std::string_view jsonSv(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    jsonSv = ReaderText::StripUtf8Bom(jsonSv);
    ::Explorer2::Json::Value root;
    ::Explorer2::Json::Limits lim;
    lim.maxDepth = 48;
    if (!::Explorer2::Json::ParseDocument(jsonSv, root, lim)) return false;

    std::wstring subKind = L"JSON";

    // 1. Firefox Bookmarks Backup (root.children)
    if (root.kind == ::Explorer2::Json::Value::Kind::Object && root.Find("children")) {
        SpreadsheetSheet bmSh;
        bmSh.name = L"Firefox_Bookmarks";
        bmSh.rows.push_back({ L"Folder", L"Title", L"URL", L"Tags / Keyword", L"Date Added", L"Last Modified", L"GUID" });
        DetailMozLz4::WalkBookmarksNode(root, L"", bmSh, 0);
        if (bmSh.rows.size() > 1) {
            FinalizeSheetCols(bmSh);
            out.sheets.push_back(std::move(bmSh));
            subKind = L"Firefox Bookmarks Backup";
        }
    }

    // 2. Firefox SessionStore (root.windows[].tabs[].entries[])
    if (const auto* wins = root.Find("windows"); wins && wins->kind == ::Explorer2::Json::Value::Kind::Array) {
        SpreadsheetSheet tabSh;
        tabSh.name = L"Firefox_Session_Tabs";
        tabSh.rows.push_back({ L"Window #", L"Tab #", L"Selected", L"Title", L"Active URL", L"History Depth", L"Last Accessed" });
        for (size_t w = 0; w < wins->arrVal.size() && tabSh.rows.size() <= 3000; ++w) {
            const auto& winObj = wins->arrVal[w];
            int64_t selTab = 0;
            if (const auto* st = winObj.Find("selected")) st->AsInt64(selTab);
            const auto* tabs = winObj.Find("tabs");
            if (!tabs || tabs->kind != ::Explorer2::Json::Value::Kind::Array) continue;
            for (size_t t = 0; t < tabs->arrVal.size() && tabSh.rows.size() <= 3000; ++t) {
                const auto& tabObj = tabs->arrVal[t];
                const auto* entries = tabObj.Find("entries");
                std::wstring title, url;
                size_t histCnt = 0;
                if (entries && entries->kind == ::Explorer2::Json::Value::Kind::Array && !entries->arrVal.empty()) {
                    histCnt = entries->arrVal.size();
                    int64_t idx1 = static_cast<int64_t>(histCnt);
                    if (const auto* ix = tabObj.Find("index")) ix->AsInt64(idx1);
                    size_t pick = (idx1 >= 1 && static_cast<size_t>(idx1) <= histCnt) ? static_cast<size_t>(idx1 - 1) : (histCnt - 1);
                    title = DetailMozLz4::JStr(entries->arrVal[pick], "title");
                    url   = DetailMozLz4::JStr(entries->arrVal[pick], "url");
                }
                tabSh.rows.push_back({
                    std::to_wstring(w + 1),
                    std::to_wstring(t + 1),
                    (static_cast<int64_t>(t + 1) == selTab) ? L"Yes" : L"No",
                    title,
                    url,
                    std::to_wstring(histCnt),
                    DetailMozLz4::FormatMozTimestamp(tabObj, "lastAccessed", false)
                });
            }
        }
        if (tabSh.rows.size() > 1) {
            FinalizeSheetCols(tabSh);
            out.sheets.push_back(std::move(tabSh));
            subKind = L"Firefox SessionStore";
        }
    }

    // 3. Firefox Search Engines (root.engines[])
    if (const auto* engs = root.Find("engines"); engs && engs->kind == ::Explorer2::Json::Value::Kind::Array && !engs->arrVal.empty()) {
        SpreadsheetSheet engSh;
        engSh.name = L"Firefox_Search_Engines";
        engSh.rows.push_back({ L"#", L"Engine Name", L"Short Name / ID", L"Load Path", L"Hidden" });
        for (size_t i = 0; i < engs->arrVal.size() && i < 500; ++i) {
            const auto& eo = engs->arrVal[i];
            std::wstring nm = DetailMozLz4::JStr(eo, "_name");
            if (nm.empty()) nm = DetailMozLz4::JStr(eo, "name");
            std::wstring sn = DetailMozLz4::JStr(eo, "_shortName");
            if (sn.empty()) sn = DetailMozLz4::JStr(eo, "id");
            std::wstring lp = DetailMozLz4::JStr(eo, "_loadPath");
            std::wstring hd = DetailMozLz4::JStr(eo, "_hidden");
            engSh.rows.push_back({ std::to_wstring(i + 1), nm, sn, lp, hd.empty() ? L"false" : hd });
        }
        FinalizeSheetCols(engSh);
        out.sheets.push_back(std::move(engSh));
        subKind = L"Firefox Search Configuration";
    }

    // 4. Generic top-level array of objects -> tabular grid
    if (out.sheets.empty() && root.kind == ::Explorer2::Json::Value::Kind::Array &&
        !root.arrVal.empty() && root.arrVal[0].kind == ::Explorer2::Json::Value::Kind::Object)
    {
        std::vector<std::string> colKeys;
        std::set<std::string> seen;
        for (const auto& item : root.arrVal) {
            if (item.kind != ::Explorer2::Json::Value::Kind::Object) continue;
            for (const auto& kv : item.objVal) {
                if (seen.insert(kv.first).second) colKeys.push_back(kv.first);
                if (colKeys.size() >= 48) break;
            }
            if (colKeys.size() >= 48) break;
        }
        if (!colKeys.empty()) {
            SpreadsheetSheet tbl;
            tbl.name = L"MozLz4_Table";
            std::vector<std::wstring> hdr;
            for (const auto& k : colKeys) hdr.push_back(PathUtils::Utf8ToWide(k));
            tbl.rows.push_back(std::move(hdr));
            for (size_t r = 0; r < std::min<size_t>(root.arrVal.size(), 500); ++r) {
                const auto& item = root.arrVal[r];
                if (item.kind != ::Explorer2::Json::Value::Kind::Object) continue;
                std::vector<std::wstring> row(colKeys.size());
                for (size_t c = 0; c < colKeys.size(); ++c) row[c] = DetailMozLz4::JStr(item, colKeys[c]);
                tbl.rows.push_back(std::move(row));
            }
            FinalizeSheetCols(tbl);
            out.sheets.push_back(std::move(tbl));
        }
    }

    SpreadsheetSheet fldSh;
    fldSh.name = L"MozLz4_Fields";
    fldSh.rows.push_back({ L"JSON Path / Property", L"Type", L"Value" });
    fldSh.rows.push_back({ L"(container)", L"mozLz40",
        L"Compressed=" + PathUtils::FormatByteSize(raw.size() - 12) + L" -> Uncompressed=" + PathUtils::FormatByteSize(uncompLen) });
    DetailMozLz4::FlattenJsonFields(root, L"", fldSh, 0);
    FinalizeSheetCols(fldSh);
    out.sheets.push_back(std::move(fldSh));

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Mozilla LZ4 JSON — " + subKind + L" (" + extLower + L")";
    out.valid = true;
    return true;
}

} // namespace Formats
