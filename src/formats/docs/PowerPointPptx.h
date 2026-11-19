#pragma once
// PowerPointPptx.h — Microsoft PowerPoint OOXML PresentationML (.pptx / .ppsx / .pptm / .potx) Reader

namespace DetailPptxOdp {

// Extract text from a DrawingML <a:p> node (handling <a:r>, <a:fld>, <a:br/>, <a:tab/>, <a:hlinkClick>, bullets/levels)
inline std::wstring ExtractDrawingMlPara(
    const XmlNode& pNode,
    const std::unordered_map<std::string, std::string>& extLinks,
    bool skipSlideNumAndDateFields,
    bool& outIsBullet,
    int& outIndentLvl)
{
    outIsBullet = false;
    outIndentLvl = 0;
    bool explicitBuNone = false;

    if (const XmlNode* pPr = pNode.FindFirstLocal("ppr")) {
        std::string_view lvlSv = pPr->Attr("lvl");
        if (!lvlSv.empty()) {
            outIndentLvl = std::clamp(std::atoi(std::string(lvlSv).c_str()), 0, 6);
            if (outIndentLvl > 0) outIsBullet = true;
        }
        for (const auto& pp : pPr->children) {
            if (pp.isText) continue;
            if (pp.localTag == "bunone") explicitBuNone = true;
            else if (pp.localTag == "buchar" || pp.localTag == "buautonum" || pp.localTag == "bublip") outIsBullet = true;
        }
    }
    if (explicitBuNone) outIsBullet = false;

    std::wstring text;
    std::vector<std::wstring> urlsToAppend;

    auto checkRunHlink = [&](const XmlNode& rNode) {
        if (const XmlNode* rPr = rNode.FindFirstLocal("rpr")) {
            if (const XmlNode* hl = rPr->FindFirstLocal("hlinkclick")) {
                std::string rid(hl->Attr("r:id"));
                if (rid.empty()) rid = std::string(hl->Attr("id"));
                auto it = extLinks.find(rid);
                if (it != extLinks.end() && !it->second.empty()) {
                    std::wstring wurl = PathUtils::Utf8ToWide(it->second);
                    if (std::find(urlsToAppend.begin(), urlsToAppend.end(), wurl) == urlsToAppend.end()) {
                        urlsToAppend.push_back(std::move(wurl));
                    }
                }
            }
        }
    };

    for (const auto& ch : pNode.children) {
        if (ch.isText) continue;
        if (ch.fullTag == "mc:fallback") continue;
        if (ch.localTag == "r") {
            checkRunHlink(ch);
            for (const auto& rc : ch.children) {
                if (!rc.isText && rc.localTag == "t") {
                    CollectAllTextW(rc, text);
                } else if (!rc.isText && rc.localTag == "br") {
                    text.push_back(L'\n');
                } else if (!rc.isText && rc.localTag == "tab") {
                    text.push_back(L'\t');
                }
            }
        } else if (ch.localTag == "fld") {
            std::string fType = LowerAsciiStr(ch.Attr("type"));
            if (skipSlideNumAndDateFields &&
                (fType.find("slidenum") != std::string::npos || fType.find("datetime") != std::string::npos))
            {
                continue;
            }
            checkRunHlink(ch);
            for (const auto& fc : ch.children) {
                if (!fc.isText && fc.localTag == "t") CollectAllTextW(fc, text);
            }
        } else if (ch.localTag == "br") {
            text.push_back(L'\n');
        } else if (ch.localTag == "tab") {
            text.push_back(L'\t');
        } else if (ch.localTag == "t") {
            CollectAllTextW(ch, text);
        } else if (ch.localTag == "m" || ch.localTag == "omath" || ch.localTag == "omathpara") {
            CollectAllTextW(ch, text);
        }
    }

    text = TrimWideWs(text);
    if (!text.empty()) {
        for (const auto& u : urlsToAppend) {
            if (text.find(u) == std::wstring::npos) {
                text += L" [" + u + L"]";
            }
        }
        if (outIndentLvl > 0) {
            text.insert(0, static_cast<size_t>(outIndentLvl * 2), L' ');
        }
    }
    return text;
}

// Parse DrawingML <a:tbl> into pipe-separated table rows with isTableRow = true
inline void ParseDrawingMlTable(
    const XmlNode& tblNode,
    const std::unordered_map<std::string, std::string>& extLinks,
    std::vector<DocParagraph>& outParas)
{
    for (const auto& tr : tblNode.children) {
        if (tr.isText || tr.localTag != "tr") continue;
        std::vector<std::wstring> cells;
        bool anyNonEmpty = false;
        for (const auto& tc : tr.children) {
            if (tc.isText || tc.localTag != "tc") continue;
            // Skip horizontal/vertical merged shadow cells if completely empty
            std::string_view hm = tc.Attr("hmerge");
            std::string_view vm = tc.Attr("vmerge");
            bool isMergedShadow = (hm == "1" || hm == "true" || vm == "1" || vm == "true");

            std::wstring cellTxt;
            if (const XmlNode* txBody = tc.FindFirstLocal("txbody")) {
                for (const auto& p : txBody->children) {
                    if (p.isText || p.localTag != "p") continue;
                    bool dummyBul = false;
                    int dummyLvl = 0;
                    std::wstring pt = ExtractDrawingMlPara(p, extLinks, false, dummyBul, dummyLvl);
                    if (!pt.empty()) {
                        if (!cellTxt.empty()) cellTxt += L" ";
                        cellTxt += pt;
                    }
                }
            }
            if (isMergedShadow && cellTxt.empty()) {
                continue;
            }
            if (!cellTxt.empty()) anyNonEmpty = true;
            cells.push_back(std::move(cellTxt));
        }
        if (anyNonEmpty && !cells.empty()) {
            std::wstring rowLine;
            for (size_t i = 0; i < cells.size(); ++i) {
                if (i > 0) rowLine += L" | ";
                rowLine += cells[i];
            }
            DocParagraph dp;
            dp.text = std::move(rowLine);
            dp.isTableRow = true;
            outParas.push_back(std::move(dp));
        }
    }
}

struct ExtractedSlideContent {
    std::wstring title;
    std::wstring subtitle;
    std::vector<DocParagraph> bodyParas;
    std::vector<std::string> chartRelIds;
    std::vector<std::string> diagramRelIds;
    size_t shapeCount = 0;
    size_t pictureCount = 0;
};

inline void WalkPptxShapeTree(
    const XmlNode& container,
    const std::unordered_map<std::string, std::string>& extLinks,
    bool isNotesSlide,
    ExtractedSlideContent& sc)
{
    for (const auto& ch : container.children) {
        if (ch.isText) continue;
        if (ch.fullTag == "mc:fallback") continue;

        if (ch.localTag == "alternatecontent" || ch.localTag == "choice") {
            WalkPptxShapeTree(ch, extLinks, isNotesSlide, sc);
            continue;
        }
        if (ch.localTag == "grpsp" || ch.localTag == "sptree" || ch.localTag == "csld") {
            WalkPptxShapeTree(ch, extLinks, isNotesSlide, sc);
            continue;
        }
        if (ch.localTag == "pic" || ch.localTag == "blipfill") {
            ++sc.pictureCount;
            continue;
        }
        if (ch.localTag == "cxnsp") {
            ++sc.shapeCount;
            continue;
        }
        if (ch.localTag == "graphicframe") {
            // Inspect <a:graphic><a:graphicData> for <a:tbl>, <c:chart>, or <dgm:relIds>
            auto walkGraphic = [&](auto& self, const XmlNode& gn) -> void {
                for (const auto& gc : gn.children) {
                    if (gc.isText || gc.fullTag == "mc:fallback") continue;
                    if (gc.localTag == "tbl") {
                        ParseDrawingMlTable(gc, extLinks, sc.bodyParas);
                    } else if (gc.localTag == "chart") {
                        std::string rid(gc.Attr("r:id"));
                        if (rid.empty()) rid = std::string(gc.Attr("id"));
                        if (!rid.empty()) sc.chartRelIds.push_back(rid);
                    } else if (gc.localTag == "relids") {
                        std::string dm(gc.Attr("r:dm"));
                        if (dm.empty()) dm = std::string(gc.Attr("dm"));
                        if (!dm.empty()) sc.diagramRelIds.push_back(dm);
                    } else {
                        self(self, gc);
                    }
                }
            };
            walkGraphic(walkGraphic, ch);
            continue;
        }
        if (ch.localTag == "sp") {
            // Determine placeholder type (<p:nvSpPr><p:nvPr><p:ph type="..."/>)
            std::string phType;
            bool hasPh = false;
            if (const XmlNode* nvSpPr = ch.FindFirstLocal("nvsppr")) {
                if (const XmlNode* nvPr = nvSpPr->FindFirstLocal("nvpr")) {
                    if (const XmlNode* ph = nvPr->FindFirstLocal("ph")) {
                        hasPh = true;
                        phType = LowerAsciiStr(ph->Attr("type"));
                    }
                }
            }
            if (const XmlNode* spPr = ch.FindFirstLocal("sppr")) {
                if (spPr->FindFirstLocal("prstgeom") || spPr->FindFirstLocal("custgeom")) {
                    if (!hasPh) ++sc.shapeCount;
                }
            }

            // Skip slide-number / date-time / header / footer boilerplate placeholders
            if (phType == "sldnum" || phType == "dt" || phType == "hdr" || (isNotesSlide && phType == "sldimg")) {
                continue;
            }
            if (isNotesSlide && phType == "ftr") {
                continue;
            }

            const bool isTitlePh = (!isNotesSlide && (phType == "title" || phType == "ctrtitle"));
            const bool isSubTitlePh = (!isNotesSlide && phType == "subtitle");
            const bool isBodyPh = (!isNotesSlide && (phType == "body" || phType == "obj" || (hasPh && phType.empty())));

            if (const XmlNode* txBody = ch.FindFirstLocal("txbody")) {
                for (const auto& p : txBody->children) {
                    if (p.isText || p.localTag != "p") continue;
                    bool isBul = false;
                    int lvl = 0;
                    std::wstring pTxt = ExtractDrawingMlPara(p, extLinks, true, isBul, lvl);
                    if (pTxt.empty()) continue;

                    if (isTitlePh && sc.title.empty()) {
                        sc.title = std::move(pTxt);
                    } else if (isSubTitlePh && sc.subtitle.empty()) {
                        sc.subtitle = std::move(pTxt);
                    } else {
                        DocParagraph dp;
                        dp.text = std::move(pTxt);
                        dp.headingLevel = isTitlePh ? 2 : (isSubTitlePh ? 3 : 0);
                        dp.isBullet = (!isNotesSlide && (isBul || isBodyPh));
                        sc.bodyParas.push_back(std::move(dp));
                    }
                }
            }
        }
    }
}

// Extract SmartArt text points from ppt/diagrams/dataN.xml (<dgm:dataModel><dgm:ptLst><dgm:pt>)
inline void ParseSmartArtDataXml(std::string_view xmlUtf8, std::vector<DocParagraph>& outParas) {
    XmlNode doc;
    if (!ParseXmlTreePptx(xmlUtf8, doc)) return;
    const XmlNode* dm = doc.FindFirstLocal("datamodel");
    if (!dm) return;
    const XmlNode* ptLst = dm->FindFirstLocal("ptlst");
    if (!ptLst) return;

    std::unordered_map<std::string, std::string> noLinks;
    std::vector<std::wstring> items;
    for (const auto& pt : ptLst->children) {
        if (pt.isText || pt.localTag != "pt") continue;
        std::string ptType = LowerAsciiStr(pt.Attr("type"));
        if (ptType == "partrans" || ptType == "sibtrans" || ptType == "pres") continue;

        if (const XmlNode* tNode = pt.FindFirstLocal("t")) {
            std::wstring nodeText;
            for (const auto& p : tNode->children) {
                if (p.isText || p.localTag != "p") continue;
                bool bul = false; int lvl = 0;
                std::wstring s = ExtractDrawingMlPara(p, noLinks, false, bul, lvl);
                if (!s.empty() && s != L"[Text]") {
                    if (!nodeText.empty()) nodeText += L" ";
                    nodeText += s;
                }
            }
            if (!nodeText.empty()) {
                items.push_back(std::move(nodeText));
            }
        }
    }
    for (auto& it : items) {
        DocParagraph dp;
        dp.text = L"[SmartArt] " + std::move(it);
        dp.isBullet = true;
        outParas.push_back(std::move(dp));
    }
}

// Extract embedded OOXML Chart title, series, categories & cached values from ppt/charts/chartN.xml
inline void ParseChartSpaceXml(std::string_view xmlUtf8, std::vector<DocParagraph>& outParas) {
    XmlNode doc;
    if (!ParseXmlTreePptx(xmlUtf8, doc)) return;
    const XmlNode* cs = doc.FindFirstLocal("chartspace");
    if (!cs) return;
    const XmlNode* chart = cs->FindFirstLocal("chart");
    if (!chart) return;

    auto extractCachePts = [](const XmlNode* parentNode, std::vector<std::wstring>& vals) {
        vals.clear();
        if (!parentNode) return;
        auto walk = [&](auto& self, const XmlNode& cur) -> void {
            for (const auto& ch : cur.children) {
                if (ch.isText || ch.fullTag == "mc:fallback") continue;
                if (ch.localTag == "pt") {
                    std::wstring v = GetChildTextTrimmed(ch, "v");
                    if (!v.empty()) vals.push_back(std::move(v));
                } else {
                    self(self, ch);
                }
            }
        };
        walk(walk, *parentNode);
    };

    // Optional chart title
    if (const XmlNode* titleNode = chart->FindFirstLocal("title")) {
        std::wstring tStr;
        if (const XmlNode* tx = titleNode->FindFirstLocal("tx")) {
            CollectAllTextW(*tx, tStr);
            tStr = TrimWideWs(tStr);
        }
        if (!tStr.empty()) {
            DocParagraph dp;
            dp.text = L"[Chart] " + tStr;
            dp.headingLevel = 3;
            outParas.push_back(std::move(dp));
        }
    }

    const XmlNode* plotArea = chart->FindFirstLocal("plotarea");
    if (!plotArea) return;

    for (const auto& pc : plotArea->children) {
        if (pc.isText || pc.localTag.find("chart") == std::string::npos) continue;
        for (const auto& ser : pc.children) {
            if (ser.isText || ser.localTag != "ser") continue;
            std::vector<std::wstring> serNames, cats, vals;
            extractCachePts(ser.FindFirstLocal("tx"), serNames);
            extractCachePts(ser.FindFirstLocal("cat"), cats);
            if (cats.empty()) extractCachePts(ser.FindFirstLocal("xval"), cats);
            extractCachePts(ser.FindFirstLocal("val"), vals);
            if (vals.empty()) extractCachePts(ser.FindFirstLocal("yval"), vals);

            std::wstring sName = serNames.empty() ? L"Series" : serNames.front();
            std::wstring summary = L"[Chart: " + sName + L"]";
            size_t nPts = std::min<size_t>(std::max(cats.size(), vals.size()), 12u);
            for (size_t i = 0; i < nPts; ++i) {
                summary += (i == 0) ? L" " : L" | ";
                if (i < cats.size() && i < vals.size()) {
                    // Trim excessive float precision like "8.200000000000001"
                    double dv = 0.0;
                    wchar_t* endp = nullptr;
                    dv = std::wcstod(vals[i].c_str(), &endp);
                    if (endp && *endp == 0 && vals[i].size() > 8) {
                        wchar_t numBuf[48] = {};
                        swprintf_s(numBuf, L"%.4g", dv);
                        summary += cats[i] + L": " + numBuf;
                    } else {
                        summary += cats[i] + L": " + vals[i];
                    }
                } else if (i < vals.size()) {
                    summary += vals[i];
                } else if (i < cats.size()) {
                    summary += cats[i];
                }
            }
            if (nPts > 0 || !serNames.empty()) {
                DocParagraph dp;
                dp.text = std::move(summary);
                dp.isTableRow = true;
                outParas.push_back(std::move(dp));
            }
        }
    }
}

// Extract Slide Comments from ppt/comments/commentN.xml
inline void ParseSlideCommentsXml(
    std::string_view xmlUtf8,
    const std::unordered_map<std::string, std::wstring>& authorMap,
    std::vector<DocParagraph>& outParas)
{
    XmlNode doc;
    if (!ParseXmlTreePptx(xmlUtf8, doc)) return;
    const XmlNode* cmLst = doc.FindFirstLocal("cmlst");
    if (!cmLst) return;
    for (const auto& cm : cmLst->children) {
        if (cm.isText || cm.localTag != "cm") continue;
        std::wstring txt = GetChildTextTrimmed(cm, "text");
        if (txt.empty()) continue;
        std::string authId(cm.Attr("authorid"));
        auto it = authorMap.find(authId);
        std::wstring prefix = (it != authorMap.end() && !it->second.empty())
            ? (L"[Comment — " + it->second + L"] ")
            : L"[Comment] ";
        DocParagraph dp;
        dp.text = prefix + txt;
        outParas.push_back(std::move(dp));
    }
}

} // namespace DetailPptxOdp

namespace Formats {

inline bool ParsePowerPointPptx(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
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

    auto readPartUtf8 = [&](const std::string& normPath, std::string& outStr) -> bool {
        auto it = entryMap.find(normPath);
        if (it == entryMap.end()) return false;
        std::vector<uint8_t> buf;
        if (!ZipDeflate::ExtractZipEntryFromFile(fullPath, entries[it->second], buf) || buf.empty()) return false;
        outStr = XmlBytesToUtf8(buf);
        return !outStr.empty();
    };

    // 1. Strict OOXML PresentationML validation:
    //    Reject non-PowerPoint ZIPs (e.g. arbitrary .zip, .docx, .xlsx, .jar, .apk renamed to .pptx)!
    {
        std::string ctXml;
        if (readPartUtf8("[content_types].xml", ctXml)) {
            std::string lowCt = LowerAsciiStr(ctXml);
            if ((lowCt.find("wordprocessingml.document.main") != std::string::npos ||
                 lowCt.find("spreadsheetml.sheet.main") != std::string::npos) &&
                lowCt.find("presentationml") == std::string::npos)
            {
                return false;
            }
        }
    }

    std::string presXml;
    if (!readPartUtf8("ppt/presentation.xml", presXml)) {
        return false;
    }
    XmlNode presDoc;
    if (!ParseXmlTreePptx(presXml, presDoc)) {
        return false;
    }
    const XmlNode* presRoot = presDoc.FindFirstLocal("presentation");
    if (!presRoot) {
        return false;
    }

    // 2. Resolve slide order from <p:sldIdLst> + ppt/_rels/presentation.xml.rels
    std::unordered_map<std::string, std::string> presRelTargets;
    {
        std::string presRelsXml;
        if (readPartUtf8("ppt/_rels/presentation.xml.rels", presRelsXml)) {
            std::vector<OoxmlRel> rels;
            ParseOoxmlRelsXml(presRelsXml, rels);
            for (const auto& r : rels) {
                if (!r.external) {
                    presRelTargets[r.id] = NormalizeZipPath("ppt", r.target);
                }
            }
        }
    }

    struct OrderedSlide {
        std::string zipPath;
        bool hidden = false;
    };
    std::vector<OrderedSlide> orderedSlides;
    if (const XmlNode* sldIdLst = presRoot->FindFirstLocal("sldidlst")) {
        for (const auto& sldId : sldIdLst->children) {
            if (sldId.isText || sldId.localTag != "sldid") continue;
            std::string rid(sldId.Attr("r:id"));
            if (rid.empty()) rid = std::string(sldId.Attr("id"));
            bool hidden = (sldId.Attr("show") == "0" || LowerAsciiStr(sldId.Attr("show")) == "false");
            auto it = presRelTargets.find(rid);
            if (it != presRelTargets.end() && entryMap.find(it->second) != entryMap.end()) {
                orderedSlides.push_back({ it->second, hidden });
            }
        }
    }
    // Fallback to ppt/slides/slideN.xml ONLY if presentation.xml was valid and sldIdLst/rels omitted entries
    if (orderedSlides.empty()) {
        std::vector<std::pair<int, std::string>> fallbackSlides;
        for (const auto& [k, idx] : entryMap) {
            if (k.rfind("ppt/slides/slide", 0) == 0 && k.size() > 20 && k.substr(k.size() - 4) == ".xml") {
                int num = std::atoi(k.substr(16, k.size() - 20).c_str());
                if (num > 0) fallbackSlides.emplace_back(num, k);
            }
        }
        std::sort(fallbackSlides.begin(), fallbackSlides.end());
        for (auto& fs : fallbackSlides) orderedSlides.push_back({ std::move(fs.second), false });
    }

    if (orderedSlides.empty()) {
        return false; // Zero-slide shell rejected cleanly (no fake placeholder!)
    }

    out.kind = OfficeFileKind::PowerPointPptx;
    out.isSpreadsheet = false;

    // 3. Optional Comment Authors (ppt/commentAuthors.xml) & Core Metadata (docProps/core.xml)
    std::unordered_map<std::string, std::wstring> commentAuthors;
    {
        std::string caXml;
        if (readPartUtf8("ppt/commentauthors.xml", caXml)) {
            XmlNode caDoc;
            if (ParseXmlTreePptx(caXml, caDoc)) {
                if (const XmlNode* caLst = caDoc.FindFirstLocal("cmauthorlst")) {
                    for (const auto& ca : caLst->children) {
                        if (ca.isText || ca.localTag != "cmauthor") continue;
                        std::string id(ca.Attr("id"));
                        std::wstring nm = PathUtils::Utf8ToWide(ca.Attr("name"));
                        if (!id.empty() && !nm.empty()) commentAuthors[id] = std::move(nm);
                    }
                }
            }
        }
    }

    std::wstring coreTitle, coreCreator;
    {
        std::string coreXml;
        if (readPartUtf8("docprops/core.xml", coreXml)) {
            XmlNode cDoc;
            if (ParseXmlTreePptx(coreXml, cDoc)) {
                if (const XmlNode* cp = cDoc.FindFirstLocal("coreproperties")) {
                    coreTitle   = GetChildTextTrimmed(*cp, "title");
                    coreCreator = GetChildTextTrimmed(*cp, "creator");
                }
            }
        }
    }
    if (!coreCreator.empty() || (!coreTitle.empty() && _wcsicmp(coreTitle.c_str(), L"PowerPoint Presentation") != 0)) {
        std::wstring deckBanner;
        if (!coreTitle.empty() && _wcsicmp(coreTitle.c_str(), L"PowerPoint Presentation") != 0) {
            deckBanner = coreTitle;
        }
        if (!coreCreator.empty()) {
            if (!deckBanner.empty()) deckBanner += L" — ";
            deckBanner += L"Author: " + coreCreator;
        }
        if (!deckBanner.empty()) {
            DetailWordOdt::EmitDocParagraph(out, { std::move(deckBanner), 2, false, false });
        }
    }

    // 4. Parse each slide + its .rels (hyperlinks, SmartArt diagrams, embedded charts, speaker notes, comments)
    size_t validSlidesParsed = 0;
    for (size_t si = 0; si < orderedSlides.size() && si < 500; ++si) {
        const std::string& slidePath = orderedSlides[si].zipPath;
        std::string slideXml;
        if (!readPartUtf8(slidePath, slideXml)) continue;

        XmlNode sldDoc;
        if (!ParseXmlTreePptx(slideXml, sldDoc)) continue;
        const XmlNode* sldRoot = sldDoc.FindFirstLocal("sld");
        if (!sldRoot) continue;
        ++validSlidesParsed;

        bool isHidden = orderedSlides[si].hidden ||
                        (sldRoot->Attr("show") == "0" || LowerAsciiStr(sldRoot->Attr("show")) == "false");

        // Parse ppt/slides/_rels/slideN.xml.rels
        std::unordered_map<std::string, std::string> extLinks;
        std::unordered_map<std::string, std::string> intTargets;
        std::vector<std::string> notesParts, commentParts, diagramPartsFallback, chartPartsFallback;
        {
            std::string relsXml;
            std::string slideDir = DirOfPart(slidePath);
            if (readPartUtf8(RelsPathForPart(slidePath), relsXml)) {
                std::vector<OoxmlRel> sRels;
                ParseOoxmlRelsXml(relsXml, sRels);
                for (const auto& r : sRels) {
                    if (r.external) {
                        extLinks[r.id] = r.target;
                    } else {
                        std::string normT = NormalizeZipPath(slideDir, r.target);
                        intTargets[r.id] = normT;
                        if (r.type.find("/notesslide") != std::string::npos) {
                            notesParts.push_back(normT);
                        } else if (r.type.find("/comments") != std::string::npos) {
                            commentParts.push_back(normT);
                        } else if (r.type.find("/diagramdata") != std::string::npos) {
                            diagramPartsFallback.push_back(normT);
                        } else if (r.type.find("/chart") != std::string::npos && r.type.find("chartuser") == std::string::npos) {
                            chartPartsFallback.push_back(normT);
                        }
                    }
                }
            }
        }

        ExtractedSlideContent sc;
        WalkPptxShapeTree(*sldRoot, extLinks, false, sc);

        // Slide Heading
        const size_t dispNum = validSlidesParsed;
        std::wstring slideHdr = L"Slajd " + std::to_wstring(dispNum) + L" / Slide " + std::to_wstring(dispNum);
        if (!sc.title.empty()) slideHdr += L": " + sc.title;
        if (isHidden) slideHdr += L" [Hidden]";
        DetailWordOdt::EmitDocParagraph(out, { std::move(slideHdr), 1, false, false });

        bool emittedSlideBody = !sc.title.empty();
        if (!sc.subtitle.empty()) {
            emittedSlideBody = true;
            DetailWordOdt::EmitDocParagraph(out, { std::move(sc.subtitle), 2, false, false });
        }
        for (auto& bp : sc.bodyParas) {
            emittedSlideBody = true;
            DetailWordOdt::EmitDocParagraph(out, std::move(bp));
        }

        // SmartArt Diagrams linked from <dgm:relIds r:dm="..."> (or slide rels diagramData)
        std::vector<std::string> diagTargets;
        for (const auto& dmId : sc.diagramRelIds) {
            auto it = intTargets.find(dmId);
            if (it != intTargets.end()) diagTargets.push_back(it->second);
        }
        if (diagTargets.empty()) diagTargets = diagramPartsFallback;
        for (const auto& dpPath : diagTargets) {
            std::string dXml;
            if (readPartUtf8(dpPath, dXml)) {
                std::vector<DocParagraph> dParas;
                ParseSmartArtDataXml(dXml, dParas);
                for (auto& dp : dParas) {
                    emittedSlideBody = true;
                    DetailWordOdt::EmitDocParagraph(out, std::move(dp));
                }
            }
        }

        // Embedded Charts linked from <c:chart r:id="..."> (or slide rels chart)
        std::vector<std::string> chartTargets;
        for (const auto& chId : sc.chartRelIds) {
            auto it = intTargets.find(chId);
            if (it != intTargets.end()) chartTargets.push_back(it->second);
        }
        if (chartTargets.empty()) chartTargets = chartPartsFallback;
        for (const auto& cpPath : chartTargets) {
            std::string cXml;
            if (readPartUtf8(cpPath, cXml)) {
                std::vector<DocParagraph> cParas;
                ParseChartSpaceXml(cXml, cParas);
                for (auto& cp : cParas) {
                    emittedSlideBody = true;
                    DetailWordOdt::EmitDocParagraph(out, std::move(cp));
                }
            }
        }

        // Speaker Notes (ppt/notesSlides/notesSlideK.xml)
        for (const auto& npPath : notesParts) {
            std::string nXml;
            if (readPartUtf8(npPath, nXml)) {
                XmlNode nDoc;
                if (ParseXmlTreePptx(nXml, nDoc)) {
                    if (const XmlNode* notesRoot = nDoc.FindFirstLocal("notes")) {
                        std::unordered_map<std::string, std::string> noExt;
                        ExtractedSlideContent nsc;
                        WalkPptxShapeTree(*notesRoot, noExt, true, nsc);
                        for (auto& np : nsc.bodyParas) {
                            emittedSlideBody = true;
                            DetailWordOdt::EmitDocParagraph(out, { L"[Notes] " + np.text, 0, false, false });
                        }
                    }
                }
            }
        }

        // Slide Comments (ppt/comments/commentK.xml)
        for (const auto& cmPath : commentParts) {
            std::string cmXml;
            if (readPartUtf8(cmPath, cmXml)) {
                std::vector<DocParagraph> cmParas;
                ParseSlideCommentsXml(cmXml, commentAuthors, cmParas);
                for (auto& cmp : cmParas) {
                    emittedSlideBody = true;
                    DetailWordOdt::EmitDocParagraph(out, std::move(cmp));
                }
            }
        }

        if (!emittedSlideBody && (sc.shapeCount > 0 || sc.pictureCount > 0)) {
            std::wstring gSummary = L"[Visual Slide: " + std::to_wstring(sc.shapeCount) + L" shape(s)";
            if (sc.pictureCount > 0) gSummary += L", " + std::to_wstring(sc.pictureCount) + L" image(s)";
            gSummary += L"]";
            DetailWordOdt::EmitDocParagraph(out, { std::move(gSummary), 0, false, false });
        }
    }

    if (validSlidesParsed == 0 || out.paragraphs.empty()) {
        out = ParsedOfficeDocument{};
        return false;
    }

    const wchar_t* kindLabel = (extLower == L".ppsx" || extLower == L".ppsm")
        ? L"Microsoft PowerPoint Slide Show ("
        : L"Microsoft PowerPoint Presentation (";
    out.formatTitle = std::wstring(kindLabel) + extLower + L" — " +
                      std::to_wstring(validSlidesParsed) +
                      (validSlidesParsed == 1 ? L" slide)" : L" slides)");
    out.valid = true;
    return true;
}


} // namespace Formats
