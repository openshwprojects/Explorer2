#pragma once
// OpenDocumentOdp.h — OASIS OpenDocument Presentation (.odp / .otp) Reader

namespace DetailPptxOdp {

// ============================================================================
// OpenDocument Presentation (.odp / .otp) Full XML Parser
// ============================================================================
inline bool ParseOdpPresentationArchive(
    const std::wstring& fullPath,
    const std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    const std::unordered_map<std::string, size_t>& entryMap,
    ParsedOfficeDocument& out)
{
    auto readZipUtf8 = [&](const std::string& normName, std::string& outStr) -> bool {
        auto it = entryMap.find(normName);
        if (it == entryMap.end()) return false;
        std::vector<uint8_t> buf;
        if (!ZipDeflate::ExtractZipEntryFromFile(fullPath, entries[it->second], buf) || buf.empty()) return false;
        outStr = XmlBytesToUtf8(buf);
        return !outStr.empty();
    };

    // 1. Validate ODF mimetype if present (reject .ods / .odt / .odg renamed to .odp!)
    {
        auto itMime = entryMap.find("mimetype");
        if (itMime != entryMap.end() && entries[itMime->second].uncompressedSize > 0) {
            std::vector<uint8_t> mBytes;
            if (ZipDeflate::ExtractZipEntryFromFile(fullPath, entries[itMime->second], mBytes) && !mBytes.empty()) {
                std::string mLow = LowerAsciiStr(std::string_view(reinterpret_cast<const char*>(mBytes.data()), mBytes.size()));
                if (mLow.find("opendocument") != std::string::npos &&
                    mLow.find("presentation") == std::string::npos)
                {
                    return false;
                }
            }
        }
    }

    std::string contentUtf8;
    if (!readZipUtf8("content.xml", contentUtf8)) return false;

    XmlNode doc;
    if (!ParseXmlTreePptx(contentUtf8, doc)) return false;

    // Locate <office:presentation>
    const XmlNode* presNode = nullptr;
    auto findPres = [&](auto& self, const XmlNode& cur) -> void {
        if (presNode) return;
        for (const auto& ch : cur.children) {
            if (ch.isText) continue;
            if (ch.fullTag == "office:presentation" || ch.localTag == "presentation") {
                presNode = &ch;
                return;
            }
            self(self, ch);
        }
    };
    findPres(findPres, doc);
    if (!presNode) return false;

    out.kind = OfficeFileKind::PowerPointPptx;
    out.isSpreadsheet = false;

    // 2. Optional ODF meta.xml metadata (title / creator)
    std::wstring docTitle, docCreator;
    {
        std::string metaUtf8;
        if (readZipUtf8("meta.xml", metaUtf8)) {
            XmlNode mDoc;
            if (ParseXmlTreePptx(metaUtf8, mDoc)) {
                auto scanMeta = [&](auto& self, const XmlNode& cur) -> void {
                    for (const auto& ch : cur.children) {
                        if (ch.isText) continue;
                        if (ch.localTag == "title" && docTitle.empty()) docTitle = GetChildTextTrimmed(cur, "title");
                        else if ((ch.localTag == "creator" || ch.localTag == "initial-creator") && docCreator.empty()) {
                            std::wstring w; CollectAllTextW(ch, w); docCreator = TrimWideWs(w);
                        } else {
                            self(self, ch);
                        }
                    }
                };
                scanMeta(scanMeta, mDoc);
            }
        }
    }
    if (!docTitle.empty() || !docCreator.empty()) {
        std::wstring hdr = docTitle;
        if (!docCreator.empty()) {
            if (!hdr.empty()) hdr += L" — ";
            hdr += docCreator;
        }
        DetailWordOdt::EmitDocParagraph(out, { std::move(hdr), 1, false, false });
    }

    // Collect <presentation:footer-decl> / <presentation:header-decl> declarations
    std::unordered_map<std::string, std::wstring> footerDecls;
    for (const auto& ch : presNode->children) {
        if (ch.isText) continue;
        if (ch.localTag == "footer-decl" || ch.localTag == "header-decl") {
            std::string nm(ch.Attr("name"));
            std::wstring val;
            CollectAllTextW(ch, val);
            val = TrimWideWs(val);
            if (!nm.empty() && !val.empty()) footerDecls[nm] = std::move(val);
        }
    }

    // Helper to evaluate inline ODF <text:p> / <text:h> mixed content (<text:span>, <text:s>, <text:tab>, <text:line-break>, <text:a>)
    auto evalOdpInlineText = [&](auto& self, const XmlNode& node, std::wstring& acc) -> void {
        if (node.isText) {
            acc += PathUtils::Utf8ToWide(node.text);
            return;
        }
        if (node.localTag == "annotation") return;
        if (node.localTag == "s") {
            int cnt = std::min(256, DetailWordOdt::ParsePositiveIntAttr(node.Attr("c"), 1));
            acc.append(static_cast<size_t>(cnt), L' ');
            return;
        }
        if (node.localTag == "tab") {
            acc.push_back(L'\t');
            return;
        }
        if (node.localTag == "line-break") {
            acc.push_back(L'\n');
            return;
        }
        size_t beforeLen = acc.size();
        for (const auto& c : node.children) {
            self(self, c, acc);
        }
        if (node.localTag == "a") {
            std::string_view href = node.Attr("href");
            if (!href.empty() && href.front() != '#') {
                std::wstring wh = PathUtils::Utf8ToWide(href);
                if (acc.substr(beforeLen).find(wh) == std::wstring::npos) {
                    acc += L" [" + wh + L"]";
                }
            }
        }
    };

    // 3. Walk <draw:page> slides (strictly matching localTag == "page", NEVER "page-thumbnail"!)
    size_t slideCount = 0;
    for (const auto& page : presNode->children) {
        if (page.isText || page.localTag != "page") continue;
        ++slideCount;

        std::string rawPageName(page.Attr("name"));
        bool isHidden = (LowerAsciiStr(page.Attr("visibility")) == "hidden");
        std::string useFooter(page.Attr("use-footer-name"));

        std::wstring slideTitle;
        std::vector<DocParagraph> slideParas;
        std::vector<DocParagraph> noteParas;
        size_t shapeObjects = 0;
        size_t imageObjects = 0;

        auto parseOdpTable = [&](const XmlNode& tblNode, std::vector<DocParagraph>& dst) {
            for (const auto& tr : tblNode.children) {
                if (tr.isText || tr.localTag != "table-row") continue;
                std::vector<std::wstring> rowCells;
                bool anyText = false;
                for (const auto& tc : tr.children) {
                    if (tc.isText) continue;
                    if (tc.localTag == "covered-table-cell") continue;
                    if (tc.localTag != "table-cell") continue;
                    int rep = std::min(16, DetailWordOdt::ParsePositiveIntAttr(tc.Attr("number-columns-repeated"), 1));
                    std::wstring cellStr;
                    for (const auto& cp : tc.children) {
                        if (cp.isText) continue;
                        if (cp.localTag == "p" || cp.localTag == "h") {
                            std::wstring pt;
                            evalOdpInlineText(evalOdpInlineText, cp, pt);
                            pt = TrimWideWs(pt);
                            if (!pt.empty()) {
                                if (!cellStr.empty()) cellStr += L" ";
                                cellStr += pt;
                            }
                        }
                    }
                    if (!cellStr.empty()) anyText = true;
                    int emitRep = cellStr.empty() ? std::min(rep, 4) : rep;
                    for (int r = 0; r < emitRep; ++r) rowCells.push_back(cellStr);
                }
                while (!rowCells.empty() && rowCells.back().empty()) rowCells.pop_back();
                if (anyText && !rowCells.empty()) {
                    std::wstring joined;
                    for (size_t ci = 0; ci < rowCells.size(); ++ci) {
                        if (ci > 0) joined += L" | ";
                        joined += rowCells[ci];
                    }
                    dst.push_back({ std::move(joined), 0, false, true });
                } else if (!rowCells.empty()) {
                    ++shapeObjects;
                }
            }
        };

        auto walkOdpSubtree = [&](auto& self, const XmlNode& cur, int listDepth, std::string_view frameClass, bool inNotes) -> void {
            for (const auto& ch : cur.children) {
                if (ch.isText) continue;
                if (ch.localTag == "page-thumbnail" || ch.localTag == "forms" || ch.localTag == "enhanced-geometry") continue;

                if (ch.localTag == "notes") {
                    self(self, ch, 0, "notes", true);
                    continue;
                }
                if (ch.localTag == "annotation") {
                    std::wstring cmt;
                    for (const auto& ac : ch.children) {
                        if (!ac.isText && ac.localTag == "p") {
                            std::wstring pt;
                            evalOdpInlineText(evalOdpInlineText, ac, pt);
                            pt = TrimWideWs(pt);
                            if (!pt.empty()) {
                                if (!cmt.empty()) cmt += L" ";
                                cmt += pt;
                            }
                        }
                    }
                    if (!cmt.empty()) slideParas.push_back({ L"[Comment] " + cmt, 0, false, false });
                    continue;
                }
                if (ch.localTag == "table") {
                    parseOdpTable(ch, inNotes ? noteParas : slideParas);
                    continue;
                }
                if (ch.localTag == "frame") {
                    std::string fc = LowerAsciiStr(ch.Attr("class"));
                    if (fc == "page-number" || fc == "date-time" || fc == "header" || fc == "footer") continue;
                    self(self, ch, listDepth, fc.empty() ? frameClass : std::string_view(fc), inNotes);
                    continue;
                }
                if (ch.localTag == "image") {
                    if (!inNotes) ++imageObjects;
                    continue;
                }
                if (ch.localTag == "custom-shape" || ch.localTag == "rect" || ch.localTag == "circle" ||
                    ch.localTag == "ellipse" || ch.localTag == "line" || ch.localTag == "polygon" ||
                    ch.localTag == "polyline" || ch.localTag == "path" || ch.localTag == "connector")
                {
                    if (!inNotes) ++shapeObjects;
                    self(self, ch, listDepth, frameClass, inNotes);
                    continue;
                }
                if (ch.localTag == "list") {
                    self(self, ch, listDepth + 1, frameClass, inNotes);
                    continue;
                }
                if (ch.localTag == "p" || ch.localTag == "h") {
                    std::wstring pt;
                    evalOdpInlineText(evalOdpInlineText, ch, pt);
                    pt = TrimWideWs(pt);
                    if (pt.empty()) continue;

                    if (inNotes) {
                        noteParas.push_back({ L"[Notes] " + pt, 0, false, false });
                    } else if (frameClass == "title" && slideTitle.empty()) {
                        slideTitle = std::move(pt);
                    } else {
                        DocParagraph dp;
                        if (ch.localTag == "h") {
                            dp.headingLevel = std::clamp(DetailWordOdt::ParsePositiveIntAttr(ch.Attr("outline-level"), 2), 1, 3);
                        } else if (frameClass == "subtitle") {
                            dp.headingLevel = 3;
                        } else if (listDepth > 0 || frameClass == "outline") {
                            dp.isBullet = true;
                            if (listDepth > 1) {
                                pt.insert(0, static_cast<size_t>((listDepth - 1) * 2), L' ');
                            }
                        }
                        dp.text = std::move(pt);
                        slideParas.push_back(std::move(dp));
                    }
                    continue;
                }
                self(self, ch, listDepth, frameClass, inNotes);
            }
        };

        walkOdpSubtree(walkOdpSubtree, page, 0, "", false);

        if (slideTitle.empty() && !useFooter.empty()) {
            auto itF = footerDecls.find(useFooter);
            if (itF != footerDecls.end() && slideCount == 1) {
                slideTitle = itF->second;
            }
        }

        // Build Slide Heading
        std::wstring slideHdr = L"Slajd " + std::to_wstring(slideCount) + L" / Slide " + std::to_wstring(slideCount);
        std::wstring wPageName = PathUtils::Utf8ToWide(rawPageName);
        std::wstring defName1 = L"page" + std::to_wstring(slideCount);
        std::wstring defName2 = L"Slide " + std::to_wstring(slideCount);
        if (!slideTitle.empty()) {
            slideHdr += L": " + slideTitle;
        } else if (!wPageName.empty() && _wcsicmp(wPageName.c_str(), defName1.c_str()) != 0 && _wcsicmp(wPageName.c_str(), defName2.c_str()) != 0) {
            slideHdr += L" (" + wPageName + L")";
        }
        if (isHidden) slideHdr += L" [Hidden]";
        DetailWordOdt::EmitDocParagraph(out, { std::move(slideHdr), 1, false, false });

        bool hadSlideBody = false;
        for (auto& sp : slideParas) {
            hadSlideBody = true;
            DetailWordOdt::EmitDocParagraph(out, std::move(sp));
        }
        for (auto& np : noteParas) {
            hadSlideBody = true;
            DetailWordOdt::EmitDocParagraph(out, std::move(np));
        }
        if (!hadSlideBody && slideTitle.empty() && (shapeObjects > 0 || imageObjects > 0)) {
            std::wstring summary = L"[Vector/Graphic Slide: " + std::to_wstring(shapeObjects) + L" shape(s)";
            if (imageObjects > 0) summary += L", " + std::to_wstring(imageObjects) + L" image(s)";
            summary += L"]";
            DetailWordOdt::EmitDocParagraph(out, { std::move(summary), 0, false, false });
        }
    }

    if (slideCount == 0 || out.paragraphs.empty()) {
        out = ParsedOfficeDocument{};
        return false;
    }

    out.formatTitle = L"OpenDocument Presentation (.odp — " + std::to_wstring(slideCount) +
                      (slideCount == 1 ? L" slide)" : L" slides)");
    out.valid = true;
    return true;
}

} // namespace DetailPptxOdp

namespace Formats {

inline bool ParseOpenDocumentOdp(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    using namespace DetailPptxOdp;

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, entries) || entries.empty()) {
        return false;
    }

    std::unordered_map<std::string, size_t> entryMap;
    entryMap.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].isDirectory) continue;
        std::string norm = NormalizeZipPath("", PathUtils::WideToUtf8(entries[i].pathInArchive));
        if (!norm.empty()) entryMap[norm] = i;
    }

    return ParseOdpPresentationArchive(fullPath, entries, entryMap, out);
}

} // namespace Formats
