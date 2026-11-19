#pragma once
// AdobeIdml.h — Zero-dependency C++20 Adobe InDesign Markup Language Package (.idml, UCF/OPC + Stories/Spreads/Styles) reader.

namespace Formats {

namespace DetailIdml {

inline std::wstring NormalizeArchiveKey(std::wstring_view sv) {
    std::wstring r;
    r.reserve(sv.size());
    for (wchar_t ch : sv) {
        if (ch == L'\\') ch = L'/';
        if (r.empty() && ch == L'/') continue;
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        r.push_back(ch);
    }
    return r;
}

struct ZipPackageReader {
    std::wstring filePath;
    const uint8_t* memData = nullptr;
    size_t memLen = 0;
    bool fromDisk = false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;

    bool Open(const std::wstring& fullPath, const std::vector<uint8_t>& raw) {
        entries.clear();
        if (!fullPath.empty() && ZipDeflate::ListZipEntriesFromFile(fullPath, entries) && !entries.empty()) {
            filePath = fullPath;
            fromDisk = true;
            return true;
        }
        if (!raw.empty() && ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries) && !entries.empty()) {
            memData = raw.data();
            memLen = raw.size();
            fromDisk = false;
            return true;
        }
        return false;
    }

    const ZipDeflate::ArchiveItemMeta* FindEntry(std::wstring_view wantNormPath) const {
        std::wstring want = NormalizeArchiveKey(wantNormPath);
        for (const auto& e : entries) {
            if (!e.isDirectory && NormalizeArchiveKey(e.pathInArchive) == want) return &e;
        }
        return nullptr;
    }

    bool ExtractEntry(const ZipDeflate::ArchiveItemMeta& e, std::vector<uint8_t>& outBytes, size_t maxBytes = 64u * 1024u * 1024u) const {
        outBytes.clear();
        if (fromDisk) return ZipDeflate::ExtractZipEntryFromFile(filePath, e, outBytes, maxBytes);
        return ZipDeflate::ExtractZipEntryFromMemory(memData, memLen, e, outBytes, maxBytes);
    }

    bool ExtractByPath(std::wstring_view wantPath, std::vector<uint8_t>& outBytes, size_t maxBytes = 64u * 1024u * 1024u) const {
        const auto* e = FindEntry(wantPath);
        return e ? ExtractEntry(*e, outBytes, maxBytes) : false;
    }
};

} // namespace DetailIdml

inline bool ParseAdobeIdml(const std::wstring& fullPath, const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    DetailIdml::ZipPackageReader zip;
    if (!zip.Open(fullPath, raw)) return false;

    std::vector<uint8_t> designMapBytes;
    if (!zip.ExtractByPath(L"designmap.xml", designMapBytes) || designMapBytes.empty()) {
        return false;
    }
    std::string_view dmSv(reinterpret_cast<const char*>(designMapBytes.data()), designMapBytes.size());
    if (dmSv.find("<Document") == std::string_view::npos && dmSv.find(":Document") == std::string_view::npos) {
        return false;
    }

    // Parse designmap.xml: Document attributes, StoryList order, idPkg:Story & idPkg:Spread references
    std::wstring docName, domVersion;
    std::vector<std::string> storyOrderIds;
    std::vector<std::wstring> pkgStoryPaths, pkgSpreadPaths;
    {
        size_t pos = 0;
        ::Explorer2::XmlTagScan::XmlTag t;
        while (::Explorer2::XmlTagScan::NextTag(dmSv, pos, t)) {
            if (t.isClosing) continue;
            if (t.localName == "Document") {
                domVersion = PathUtils::Utf8ToWide(::Explorer2::XmlTagScan::GetAttr(t.attrs, "DOMVersion"));
                docName    = PathUtils::Utf8ToWide(::Explorer2::XmlTagScan::GetAttr(t.attrs, "Name"));
                std::string_view sl = ::Explorer2::XmlTagScan::GetAttr(t.attrs, "StoryList");
                size_t sp = 0;
                while (sp < sl.size()) {
                    while (sp < sl.size() && sl[sp] == ' ') ++sp;
                    size_t se = sp;
                    while (se < sl.size() && sl[se] != ' ') ++se;
                    if (se > sp) storyOrderIds.emplace_back(sl.substr(sp, se - sp));
                    sp = se;
                }
            } else if (t.localName == "Story") {
                std::string_view srcAttr = ::Explorer2::XmlTagScan::GetAttr(t.attrs, "src");
                if (!srcAttr.empty()) pkgStoryPaths.push_back(PathUtils::Utf8ToWide(srcAttr));
            } else if (t.localName == "Spread") {
                std::string_view srcAttr = ::Explorer2::XmlTagScan::GetAttr(t.attrs, "src");
                if (!srcAttr.empty()) pkgSpreadPaths.push_back(PathUtils::Utf8ToWide(srcAttr));
            }
        }
    }

    // Parse Resources/Styles.xml for ParagraphStyle Self -> Name mapping
    std::unordered_map<std::string, std::string> styleNames;
    {
        std::vector<uint8_t> stBytes;
        if (zip.ExtractByPath(L"Resources/Styles.xml", stBytes)) {
            std::string_view sv(reinterpret_cast<const char*>(stBytes.data()), stBytes.size());
            size_t pos = 0;
            ::Explorer2::XmlTagScan::XmlTag t;
            while (::Explorer2::XmlTagScan::NextTag(sv, pos, t)) {
                if (!t.isClosing && t.localName == "ParagraphStyle") {
                    std::string selfId(::Explorer2::XmlTagScan::GetAttr(t.attrs, "Self"));
                    std::string sname(::Explorer2::XmlTagScan::GetAttr(t.attrs, "Name"));
                    if (!selfId.empty() && !sname.empty()) styleNames[selfId] = sname;
                }
            }
        }
    }

    // Parse META-INF/metadata.xml (XMP Dublin Core title/creator if present)
    std::wstring xmpCreatorTool;
    {
        std::vector<uint8_t> metaBytes;
        if (zip.ExtractByPath(L"META-INF/metadata.xml", metaBytes)) {
            std::string_view msv(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size());
            xmpCreatorTool = DetailMedia100::ExtractFirstXmlTagText(msv, "xmp:CreatorTool");
        }
    }

    // Parse Spreads/Spread_*.xml -> Spreads_&_Frames sheet
    SpreadsheetSheet sprSh;
    sprSh.name = L"Spreads_&_Frames";
    sprSh.rows.push_back({ L"Spread File", L"Pages", L"Text Frames", L"Graphic Frames", L"Page Names" });
    if (pkgSpreadPaths.empty()) {
        for (const auto& e : zip.entries) {
            std::wstring lp = DetailIdml::NormalizeArchiveKey(e.pathInArchive);
            if (lp.rfind(L"spreads/spread_", 0) == 0 && lp.size() > 4 && lp.substr(lp.size() - 4) == L".xml") {
                pkgSpreadPaths.push_back(e.pathInArchive);
            }
        }
    }
    size_t totalPages = 0;
    for (const auto& spPath : pkgSpreadPaths) {
        std::vector<uint8_t> spBytes;
        if (!zip.ExtractByPath(spPath, spBytes)) continue;
        std::string_view sv(reinterpret_cast<const char*>(spBytes.data()), spBytes.size());
        size_t pages = 0, txtFrames = 0, gfxFrames = 0;
        std::wstring pageNames;
        size_t pos = 0;
        ::Explorer2::XmlTagScan::XmlTag t;
        while (::Explorer2::XmlTagScan::NextTag(sv, pos, t)) {
            if (t.isClosing) continue;
            if (t.localName == "Page") {
                ++pages;
                std::wstring pn = PathUtils::Utf8ToWide(::Explorer2::XmlTagScan::GetAttr(t.attrs, "Name"));
                if (!pn.empty() && pageNames.size() < 80) {
                    if (!pageNames.empty()) pageNames += L", ";
                    pageNames += pn;
                }
            } else if (t.localName == "TextFrame") {
                ++txtFrames;
            } else if (t.localName == "Rectangle" || t.localName == "Oval" || t.localName == "Polygon") {
                ++gfxFrames;
            }
        }
        totalPages += pages;
        sprSh.rows.push_back({ spPath, std::to_wstring(pages), std::to_wstring(txtFrames), std::to_wstring(gfxFrames), pageNames });
    }

    // Order story paths according to StoryList from designmap.xml, then append any remaining Stories/Story_*.xml
    std::vector<std::wstring> orderedStories;
    std::set<std::wstring> seenStories;
    auto addStoryPath = [&](const std::wstring& p) {
        std::wstring key = DetailIdml::NormalizeArchiveKey(p);
        if (zip.FindEntry(key) && seenStories.insert(key).second) orderedStories.push_back(p);
    };
    for (const auto& sid : storyOrderIds) {
        addStoryPath(L"Stories/Story_" + PathUtils::Utf8ToWide(sid) + L".xml");
    }
    for (const auto& sp : pkgStoryPaths) addStoryPath(sp);
    for (const auto& e : zip.entries) {
        std::wstring lp = DetailIdml::NormalizeArchiveKey(e.pathInArchive);
        if (lp.rfind(L"stories/story_", 0) == 0 && lp.size() > 4 && lp.substr(lp.size() - 4) == L".xml") {
            addStoryPath(e.pathInArchive);
        }
    }

    auto classifyParaStyle = [&](std::string_view rawStyle) -> std::pair<int, bool> {
        std::string s(rawStyle);
        auto it = styleNames.find(s);
        if (it != styleNames.end()) s = it->second;
        std::string low = PathUtils::ToLowerAscii(s);
        size_t sl = low.rfind('/');
        if (sl != std::string::npos) low = low.substr(sl + 1);
        bool bullet = (low.find("bullet") != std::string::npos || low.find("list") != std::string::npos);
        int h = 0;
        if (low.find("title") != std::string::npos && low.find("subtitle") == std::string::npos) h = 1;
        else if (low.find("heading 1") != std::string::npos || low.find("heading1") != std::string::npos || low == "h1" || low.find("chapter") != std::string::npos) h = 1;
        else if (low.find("heading 2") != std::string::npos || low.find("heading2") != std::string::npos || low == "h2" || low.find("subtitle") != std::string::npos) h = 2;
        else if (low.find("heading 3") != std::string::npos || low.find("heading3") != std::string::npos || low == "h3") h = 3;
        return { h, bullet };
    };

    SpreadsheetSheet txtSh;
    txtSh.name = L"Story_Text";
    txtSh.rows.push_back({ L"#", L"Story", L"Style", L"Paragraph Text" });

    SpreadsheetSheet tblSh;
    tblSh.name = L"Tables";

    for (const auto& stPath : orderedStories) {
        std::vector<uint8_t> sBytes;
        if (!zip.ExtractByPath(stPath, sBytes) || sBytes.empty()) continue;
        std::string_view sv(reinterpret_cast<const char*>(sBytes.data()), sBytes.size());

        std::wstring curPara;
        std::wstring curStyleLabel = L"Normal";
        int curHeading = 0;
        bool curBullet = false;

        auto flushPara = [&]() {
            // Split on U+2028/U+2029/\n
            size_t ls = 0;
            while (ls <= curPara.size()) {
                size_t le = ls;
                while (le < curPara.size() && curPara[le] != L'\n' && curPara[le] != 0x2028 && curPara[le] != 0x2029) ++le;
                std::wstring line = PathUtils::Trim(std::wstring_view(curPara).substr(ls, le - ls));
                if (!line.empty() && out.paragraphs.size() < 3000) {
                    out.paragraphs.push_back({ line, curHeading, curBullet, false });
                    txtSh.rows.push_back({ std::to_wstring(txtSh.rows.size()), PathUtils::GetFileName(stPath), curStyleLabel, line });
                }
                if (le >= curPara.size()) break;
                ls = le + 1;
            }
            curPara.clear();
        };

        size_t pos = 0;
        ::Explorer2::XmlTagScan::XmlTag t;
        while (::Explorer2::XmlTagScan::NextTag(sv, pos, t)) {
            if (t.localName == "ParagraphStyleRange") {
                flushPara();
                if (!t.isClosing && !t.isSelfClosing) {
                    std::string_view ps = ::Explorer2::XmlTagScan::GetAttr(t.attrs, "AppliedParagraphStyle");
                    auto [hl, bul] = classifyParaStyle(ps);
                    curHeading = hl;
                    curBullet = bul;
                    size_t sl = ps.rfind('/');
                    curStyleLabel = PathUtils::Utf8ToWide(sl != std::string_view::npos ? ps.substr(sl + 1) : ps);
                }
            } else if (!t.isClosing && t.localName == "Br") {
                flushPara();
            } else if (!t.isClosing && !t.isSelfClosing && t.localName == "Content") {
                std::string_view bodySv;
                size_t afterClose = pos;
                if (::Explorer2::XmlTagScan::FindElementBody(sv, pos, "Content", bodySv, afterClose)) {
                    std::string dec = ::Explorer2::XmlTagScan::DecodeText(bodySv);
                    curPara += PathUtils::Utf8ToWide(dec);
                    pos = afterClose;
                }
            } else if (!t.isClosing && !t.isSelfClosing && t.localName == "Table") {
                flushPara();
                std::string_view tblBody;
                size_t afterTbl = pos;
                int colCount = std::max(1, std::atoi(std::string(::Explorer2::XmlTagScan::GetAttr(t.attrs, "ColumnCount")).c_str()));
                if (::Explorer2::XmlTagScan::FindElementBody(sv, pos, "Table", tblBody, afterTbl)) {
                    pos = afterTbl;
                    std::vector<std::vector<std::wstring>> grid;
                    size_t tp = 0;
                    ::Explorer2::XmlTagScan::XmlTag ct;
                    size_t seqIdx = 0;
                    while (::Explorer2::XmlTagScan::NextTag(tblBody, tp, ct)) {
                        if (!ct.isClosing && !ct.isSelfClosing && ct.localName == "Cell") {
                            std::string_view cellName = ::Explorer2::XmlTagScan::GetAttr(ct.attrs, "Name");
                            int cCol = static_cast<int>(seqIdx % static_cast<size_t>(colCount));
                            int cRow = static_cast<int>(seqIdx / static_cast<size_t>(colCount));
                            size_t colon = cellName.find(':');
                            if (colon != std::string_view::npos) {
                                cCol = std::max(0, std::atoi(std::string(cellName.substr(0, colon)).c_str()));
                                cRow = std::max(0, std::atoi(std::string(cellName.substr(colon + 1)).c_str()));
                            }
                            ++seqIdx;

                            std::string_view cellBody;
                            size_t afterCell = tp;
                            if (::Explorer2::XmlTagScan::FindElementBody(tblBody, tp, "Cell", cellBody, afterCell)) {
                                tp = afterCell;
                                std::wstring cellTxt;
                                size_t cp2 = 0;
                                ::Explorer2::XmlTagScan::XmlTag innerT;
                                while (::Explorer2::XmlTagScan::NextTag(cellBody, cp2, innerT)) {
                                    if (!innerT.isClosing && !innerT.isSelfClosing && innerT.localName == "Content") {
                                        std::string_view cb;
                                        size_t ac = cp2;
                                        if (::Explorer2::XmlTagScan::FindElementBody(cellBody, cp2, "Content", cb, ac)) {
                                            if (!cellTxt.empty()) cellTxt += L" ";
                                            cellTxt += PathUtils::Trim(PathUtils::Utf8ToWide(::Explorer2::XmlTagScan::DecodeText(cb)));
                                            cp2 = ac;
                                        }
                                    }
                                }
                                if (cRow < 1000 && cCol < 128) {
                                    if (grid.size() <= static_cast<size_t>(cRow)) grid.resize(static_cast<size_t>(cRow) + 1);
                                    if (grid[static_cast<size_t>(cRow)].size() <= static_cast<size_t>(cCol)) {
                                        grid[static_cast<size_t>(cRow)].resize(static_cast<size_t>(cCol) + 1);
                                    }
                                    grid[static_cast<size_t>(cRow)][static_cast<size_t>(cCol)] = std::move(cellTxt);
                                }
                            }
                        }
                    }
                    for (auto& gr : grid) {
                        std::wstring joined;
                        for (size_t ci = 0; ci < gr.size(); ++ci) {
                            if (ci) joined += L" | ";
                            joined += gr[ci];
                        }
                        if (!joined.empty() && out.paragraphs.size() < 3000) {
                            out.paragraphs.push_back({ joined, 0, false, true });
                            txtSh.rows.push_back({ std::to_wstring(txtSh.rows.size()), PathUtils::GetFileName(stPath), L"[Table Row]", joined });
                        }
                        if (tblSh.rows.size() < 1000) tblSh.rows.push_back(std::move(gr));
                    }
                }
            }
        }
        flushPara();
    }

    SpreadsheetSheet sumSh;
    sumSh.name = L"Document_Summary";
    sumSh.rows.push_back({ L"Property", L"Value" });
    if (!docName.empty())        sumSh.rows.push_back({ L"Document Name", docName });
    if (!domVersion.empty())     sumSh.rows.push_back({ L"IDML DOM Version", domVersion });
    if (!xmpCreatorTool.empty()) sumSh.rows.push_back({ L"Creator Tool", xmpCreatorTool });
    sumSh.rows.push_back({ L"Stories", std::to_wstring(orderedStories.size()) });
    sumSh.rows.push_back({ L"Spreads / Pages", std::to_wstring(sprSh.rows.size() - 1) + L" / " + std::to_wstring(totalPages) });
    sumSh.rows.push_back({ L"Extracted Paragraphs", std::to_wstring(out.paragraphs.size()) });

    RecountDocText(out);
    FinalizeSheetCols(txtSh);
    FinalizeSheetCols(sumSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Adobe InDesign Markup Language Package (.idml)";
    if (txtSh.rows.size() > 1) out.sheets.push_back(std::move(txtSh));
    out.sheets.push_back(std::move(sumSh));
    if (sprSh.rows.size() > 1) { FinalizeSheetCols(sprSh); out.sheets.push_back(std::move(sprSh)); }
    if (!tblSh.rows.empty())   { FinalizeSheetCols(tblSh); out.sheets.push_back(std::move(tblSh)); }
    if (zip.fromDisk) out.sourceReadFully = true;
    out.valid = true;
    return true;
}

} // namespace Formats
