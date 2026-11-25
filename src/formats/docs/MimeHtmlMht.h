#pragma once
// MimeHtmlMht.h — Zero-dependency C++20 RFC 2557 MIME HTML Web Archive (.mht / .mhtml) reader.

namespace Formats {

namespace DetailMhtml {

struct MhtmlResource {
    std::wstring location;
    std::wstring contentType;
    std::wstring encoding;
    size_t decodedSize = 0;
};

struct MhtmlParsed {
    std::string startCid;
    std::wstring primaryHtml;
    std::wstring fallbackPlain;
    std::vector<MhtmlResource> resources;
};

inline std::string DetectHtmlMetaCharset(std::string_view htmlBytes) {
    std::string head = PathUtils::ToLowerAscii(htmlBytes.substr(0, std::min<size_t>(htmlBytes.size(), 4096u)));
    size_t cs = head.find("charset=");
    if (cs == std::string::npos) return {};
    size_t a = cs + 8;
    if (a < head.size() && (head[a] == '"' || head[a] == '\'')) ++a;
    size_t b = a;
    while (b < head.size() && head[b] != '"' && head[b] != '\'' && head[b] != ';' && head[b] != '>' && head[b] != ' ') ++b;
    return (b > a) ? head.substr(a, b - a) : std::string{};
}

inline void WalkMhtmlMime(const std::vector<std::pair<std::string, std::string>>& headers,
                          std::string_view body, int depth, MhtmlParsed& mp) {
    if (depth > 8) return;
    const std::string ctype = DetailMediaAndScoreDoc::MimeHeader(headers, "content-type");
    std::string media = PathUtils::ToLowerAscii(PathUtils::TrimAscii(std::string_view(ctype).substr(0, ctype.find(';'))));
    if (media.empty()) media = (depth == 0) ? "text/html" : "text/plain";

    if (media.rfind("multipart/", 0) == 0) {
        std::string startP = PathUtils::WideToUtf8(DetailMediaAndScoreDoc::MimeParam(ctype, "start"));
        if (!startP.empty() && mp.startCid.empty()) mp.startCid = startP;
        const std::string boundary = PathUtils::WideToUtf8(DetailMediaAndScoreDoc::MimeParam(ctype, "boundary"));
        if (boundary.empty()) return;
        const std::string delim = "--" + boundary;
        auto nextDelim = [&](size_t from) {
            size_t p = body.find(delim, from);
            while (p != std::string_view::npos && p > 0 && body[p - 1] != '\n') p = body.find(delim, p + 1);
            return p;
        };
        size_t pos = nextDelim(0);
        while (pos != std::string_view::npos) {
            size_t after = pos + delim.size();
            if (body.compare(after, 2, "--") == 0) break;
            size_t partStart = body.find('\n', after);
            if (partStart == std::string_view::npos) break;
            ++partStart;
            size_t next = nextDelim(partStart);
            size_t partEnd = (next == std::string_view::npos) ? body.size() : next;
            if (partEnd > partStart && body[partEnd - 1] == '\n') --partEnd;
            if (partEnd > partStart && body[partEnd - 1] == '\r') --partEnd;
            std::string_view ph, pb;
            DetailMediaAndScoreDoc::SplitMimeEntity(body.substr(partStart, partEnd - partStart), ph, pb);
            WalkMhtmlMime(DetailMediaAndScoreDoc::ParseMimeHeaders(ph), pb, depth + 1, mp);
            pos = next;
        }
        return;
    }

    const std::string cteRaw = std::string(PathUtils::TrimAscii(DetailMediaAndScoreDoc::MimeHeader(headers, "content-transfer-encoding")));
    const std::string cte = PathUtils::ToLowerAscii(cteRaw);
    const std::string cLoc = DetailMediaAndScoreDoc::MimeHeader(headers, "content-location");
    const std::string cId  = DetailMediaAndScoreDoc::MimeHeader(headers, "content-id");

    std::string decodedBytes;
    if (cte == "base64") {
        std::vector<uint8_t> bin;
        ::Explorer2::Base64::Decode(body, bin, ::Explorer2::Base64::Mode::Lenient, 32u * 1024u * 1024u);
        decodedBytes.assign(reinterpret_cast<const char*>(bin.data()), bin.size());
    } else if (cte == "quoted-printable") {
        decodedBytes = DetailMediaAndScoreDoc::DecodeQuotedPrintable(body);
    } else {
        decodedBytes.assign(body.data(), body.size());
    }

    bool matchesStart = (!mp.startCid.empty() && !cId.empty() && cId == mp.startCid);
    if (media == "text/html" && (mp.primaryHtml.empty() || matchesStart)) {
        std::string cs = PathUtils::WideToUtf8(DetailMediaAndScoreDoc::MimeParam(ctype, "charset"));
        if (cs.empty()) cs = DetectHtmlMetaCharset(decodedBytes);
        mp.primaryHtml = DetailMediaAndScoreDoc::DecodeCharsetText(decodedBytes, cs);
        if (depth > 0 && mp.resources.size() < 512) {
            std::string loc = !cLoc.empty() ? cLoc : (!cId.empty() ? cId : "(primary document)");
            mp.resources.push_back({ PathUtils::Utf8ToWide(loc), PathUtils::Utf8ToWide(media),
                                     PathUtils::Utf8ToWide(cte.empty() ? "8bit" : cte), decodedBytes.size() });
        }
        return;
    }
    if (media == "text/plain" && mp.fallbackPlain.empty()) {
        std::string cs = PathUtils::WideToUtf8(DetailMediaAndScoreDoc::MimeParam(ctype, "charset"));
        mp.fallbackPlain = DetailMediaAndScoreDoc::DecodeCharsetText(decodedBytes, cs);
    }

    if (mp.resources.size() < 512) {
        std::string loc = !cLoc.empty() ? cLoc : (!cId.empty() ? cId : "");
        if (loc.empty()) loc = PathUtils::WideToUtf8(DetailMediaAndScoreDoc::MimeParam(ctype, "name"));
        if (loc.empty()) loc = "(inline " + media + ")";
        mp.resources.push_back({
            PathUtils::Utf8ToWide(loc),
            PathUtils::Utf8ToWide(media),
            PathUtils::Utf8ToWide(cte.empty() ? "binary" : cte),
            decodedBytes.size()
        });
    }
}

inline void ExtractHtmlDomAndTables(std::wstring_view html, std::wstring& outTitle,
                                    std::vector<DocParagraph>& outParas,
                                    std::vector<std::vector<std::wstring>>& outTableRows) {
    outTitle.clear();
    std::wstring curText;
    int curHeading = 0;
    bool curBullet = false;
    bool inTable = false, inCell = false;
    std::vector<std::wstring> curTr;
    std::wstring curCell;

    auto cleanWs = [](std::wstring_view raw) -> std::wstring {
        std::wstring dec = ::Explorer2::XmlTagScan::DecodeEntitiesWide(raw);
        std::wstring res;
        res.reserve(dec.size());
        bool prevSp = false;
        for (wchar_t ch : dec) {
            bool sp = (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == 0x00A0);
            if (sp) {
                if (!prevSp && !res.empty()) res.push_back(L' ');
                prevSp = true;
            } else {
                res.push_back(ch);
                prevSp = false;
            }
        }
        return PathUtils::Trim(res);
    };

    auto flushPara = [&]() {
        std::wstring clean = cleanWs(curText);
        if (!clean.empty() && outParas.size() < 2000) {
            outParas.push_back({ std::move(clean), curHeading, curBullet, false });
        }
        curText.clear();
        curHeading = 0;
        curBullet = false;
    };

    bool skipBlock = false;
    std::wstring skipEnd;
    bool inTitle = false;
    std::wstring rawTitle;

    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == L'<') {
            if (i + 4 <= html.size() && html.substr(i, 4) == L"<!--") {
                size_t ec = html.find(L"-->", i + 4);
                i = (ec == std::wstring_view::npos) ? html.size() - 1 : (ec + 2);
                continue;
            }
            size_t gt = html.find(L'>', i + 1);
            if (gt == std::wstring_view::npos) break;
            std::wstring tagInner = PathUtils::ToLower(html.substr(i + 1, std::min<size_t>(gt - i - 1, 48u)));
            bool selfClose = (!tagInner.empty() && tagInner.back() == L'/');
            size_t sp = tagInner.find_first_of(L" \t\r\n/");
            std::wstring tname = (tagInner.size() > 0 && tagInner[0] == L'/')
                ? tagInner.substr(0, tagInner.find_first_of(L" \t\r\n", 1))
                : ((sp != std::wstring::npos) ? tagInner.substr(0, sp) : tagInner);

            if (skipBlock) {
                if (tname == skipEnd) skipBlock = false;
                i = gt;
                continue;
            }
            if (tname == L"script" || tname == L"style" || tname == L"noscript" || tname == L"svg" || tname == L"template") {
                if (!selfClose) { skipBlock = true; skipEnd = L"/" + tname; }
                i = gt;
                continue;
            }
            if (tname == L"title") { inTitle = true; i = gt; continue; }
            if (tname == L"/title") { inTitle = false; if (outTitle.empty()) outTitle = cleanWs(rawTitle); i = gt; continue; }

            if (tname == L"table") { flushPara(); inTable = true; curTr.clear(); }
            else if (tname == L"/table") { inTable = false; inCell = false; }
            else if (inTable && tname == L"tr") { curTr.clear(); inCell = false; }
            else if (inTable && (tname == L"td" || tname == L"th")) { inCell = true; curCell.clear(); }
            else if (inTable && (tname == L"/td" || tname == L"/th")) {
                curTr.push_back(cleanWs(curCell));
                curCell.clear();
                inCell = false;
            } else if (inTable && tname == L"/tr") {
                if (!curTr.empty()) {
                    std::wstring rowLine;
                    for (size_t ci = 0; ci < curTr.size(); ++ci) {
                        if (ci) rowLine += L" | ";
                        rowLine += curTr[ci];
                    }
                    if (!rowLine.empty() && outParas.size() < 2000) {
                        outParas.push_back({ rowLine, 0, false, true });
                    }
                    if (outTableRows.size() < 1000) outTableRows.push_back(std::move(curTr));
                    curTr.clear();
                }
            } else if (tname == L"h1" || tname == L"h2" || tname == L"h3" || tname == L"h4") {
                flushPara();
                curHeading = (tname == L"h1") ? 1 : ((tname == L"h2") ? 2 : 3);
            } else if (tname == L"/h1" || tname == L"/h2" || tname == L"/h3" || tname == L"/h4") {
                flushPara();
            } else if (tname == L"li") {
                flushPara();
                curBullet = true;
            } else if (tname == L"p" || tname == L"/p" || tname == L"div" || tname == L"/div" ||
                       tname == L"section" || tname == L"article" || tname == L"blockquote" || tname == L"br" || tname == L"/li") {
                if (inCell) curCell.push_back(L' ');
                else flushPara();
            }
            i = gt;
            continue;
        }
        if (inTitle) rawTitle.push_back(html[i]);
        else if (inTable && inCell) curCell.push_back(html[i]);
        else if (!inTable) curText.push_back(html[i]);
    }
    flushPara();
}

} // namespace DetailMhtml

inline bool ParseMimeHtmlMht(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 16) return false;
    std::string_view sv = ::Explorer2::Json::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));

    std::string_view hdrBlock, bodyBlock;
    DetailMediaAndScoreDoc::SplitMimeEntity(sv, hdrBlock, bodyBlock);
    auto headers = DetailMediaAndScoreDoc::ParseMimeHeaders(hdrBlock);
    if (headers.empty()) return false;

    const std::string mimeVer = DetailMediaAndScoreDoc::MimeHeader(headers, "mime-version");
    const std::string ctype   = DetailMediaAndScoreDoc::MimeHeader(headers, "content-type");
    const std::string snapLoc = DetailMediaAndScoreDoc::MimeHeader(headers, "snapshot-content-location");
    const std::string contLoc = DetailMediaAndScoreDoc::MimeHeader(headers, "content-location");
    const std::string fromHdr = DetailMediaAndScoreDoc::MimeHeader(headers, "from");
    const std::string subjHdr = DetailMediaAndScoreDoc::MimeHeader(headers, "subject");
    const std::string dateHdr = DetailMediaAndScoreDoc::MimeHeader(headers, "date");

    std::string lowCtype = PathUtils::ToLowerAscii(ctype);
    if (mimeVer.empty() && lowCtype.find("multipart/") == std::string::npos &&
        lowCtype.find("text/html") == std::string::npos && snapLoc.empty() && contLoc.empty()) {
        return false;
    }

    DetailMhtml::MhtmlParsed mp;
    DetailMhtml::WalkMhtmlMime(headers, bodyBlock, 0, mp);
    if (mp.primaryHtml.empty() && mp.fallbackPlain.empty()) return false;

    std::wstring pageTitle;
    std::vector<std::vector<std::wstring>> htmlTables;
    if (!mp.primaryHtml.empty()) {
        DetailMhtml::ExtractHtmlDomAndTables(mp.primaryHtml, pageTitle, out.paragraphs, htmlTables);
    } else {
        out.paragraphs.push_back({ PathUtils::Trim(mp.fallbackPlain), 0, false, false });
    }

    std::wstring subjW = DetailMediaAndScoreDoc::DecodeRfc2047Header(subjHdr);
    std::wstring fromW = DetailMediaAndScoreDoc::DecodeRfc2047Header(fromHdr);
    std::wstring urlW  = PathUtils::Utf8ToWide(!snapLoc.empty() ? snapLoc : contLoc);

    SpreadsheetSheet sumSh;
    sumSh.name = L"MHTML_Summary";
    sumSh.rows.push_back({ L"Header / Property", L"Value" });
    if (!pageTitle.empty()) sumSh.rows.push_back({ L"HTML <title>", pageTitle });
    if (!subjW.empty())     sumSh.rows.push_back({ L"Subject", subjW });
    if (!urlW.empty())      sumSh.rows.push_back({ L"Source URL", urlW });
    if (!fromW.empty())     sumSh.rows.push_back({ L"Saved By (From)", fromW });
    if (!dateHdr.empty())   sumSh.rows.push_back({ L"Snapshot Date", PathUtils::Utf8ToWide(dateHdr) });
    sumSh.rows.push_back({ L"MIME Parts / Resources", std::to_wstring(mp.resources.size()) });
    sumSh.rows.push_back({ L"Extracted Paragraphs", std::to_wstring(out.paragraphs.size()) });

    SpreadsheetSheet pageSh;
    pageSh.name = L"Page_Content";
    pageSh.rows.push_back({ L"#", L"Kind", L"Text" });
    for (size_t i = 0; i < out.paragraphs.size(); ++i) {
        const auto& p = out.paragraphs[i];
        std::wstring kind = p.isTableRow ? L"Table Row" : (p.headingLevel > 0 ? (L"H" + std::to_wstring(p.headingLevel)) : (p.isBullet ? L"Bullet" : L"Paragraph"));
        pageSh.rows.push_back({ std::to_wstring(i + 1), kind, p.text });
    }

    SpreadsheetSheet resSh;
    resSh.name = L"Embedded_Resources";
    resSh.rows.push_back({ L"#", L"Content-Location / ID", L"Content-Type", L"Transfer-Encoding", L"Decoded Size" });
    for (size_t i = 0; i < mp.resources.size(); ++i) {
        const auto& r = mp.resources[i];
        resSh.rows.push_back({
            std::to_wstring(i + 1),
            r.location,
            r.contentType,
            r.encoding,
            PathUtils::FormatByteSize(r.decodedSize) + L" (" + std::to_wstring(r.decodedSize) + L" B)"
        });
    }

    RecountDocText(out);
    FinalizeSheetCols(pageSh);
    FinalizeSheetCols(sumSh);
    FinalizeSheetCols(resSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MIME HTML Web Archive (" + extLower + L")";
    if (pageSh.rows.size() > 1) out.sheets.push_back(std::move(pageSh));
    out.sheets.push_back(std::move(sumSh));
    if (resSh.rows.size() > 1) out.sheets.push_back(std::move(resSh));
    if (!htmlTables.empty()) {
        SpreadsheetSheet tblSh;
        tblSh.name = L"HTML_Tables";
        tblSh.rows = std::move(htmlTables);
        FinalizeSheetCols(tblSh);
        out.sheets.push_back(std::move(tblSh));
    }
    out.valid = true;
    return true;
}

} // namespace Formats
