#pragma once
// PowerPointPptxOdp.h — Full OOXML PresentationML (.pptx/.ppsx/.pptm/.potx) & ODF Impress (.odp/.otp) Parser
// Supports:
//  - Strict package validation ([Content_Types].xml + ppt/presentation.xml for OOXML; mimetype + <office:presentation> for ODF)
//  - True presentation slide order & hidden-slide flags via ppt/presentation.xml <p:sldIdLst> + ppt/_rels/presentation.xml.rels
//  - Placeholder role classification (title/ctrTitle/subTitle/body, filtering sldNum/dt/hdr/ftr noise)
//  - Recursive group shapes (<p:grpSp>), multi-run paragraphs (<a:r>, <a:fld>, <a:br/>, <a:tab/>), bullets/levels, hyperlinks
//  - DrawingML & ODF tables (<a:tbl>/<a:tr>/<a:tc> with hMerge/vMerge; <table:table> with covered-table-cell & repeated cols)
//  - Per-slide relationships: SmartArt (<dgm:dataModel>), Charts (<c:chartSpace> series/categories/values), Speaker Notes, Comments
//  - Document metadata (docProps/core.xml & meta.xml) and vector-shape summaries on graphic-only slides.

namespace DetailPptxOdp {

struct XmlAttr {
    std::string name;      // full name (lowercase)
    std::string localName; // name after ':' (lowercase)
    std::string value;     // entity-decoded UTF-8
};

struct XmlNode {
    bool isText = false;
    std::string fullTag;   // lowercase e.g. "draw:page"
    std::string localTag;  // lowercase e.g. "page"
    std::string text;      // for isText == true (entity-decoded UTF-8)
    std::vector<XmlAttr> attrs;
    std::vector<XmlNode> children;

    std::string_view Attr(std::string_view exactOrLocal) const {
        for (const auto& a : attrs) {
            if (a.name == exactOrLocal || a.localName == exactOrLocal) return a.value;
        }
        return {};
    }

    const XmlNode* FindFirstLocal(std::string_view loc) const {
        for (const auto& ch : children) {
            if (!ch.isText && ch.localTag == loc) return &ch;
        }
        return nullptr;
    }

};

inline std::string LowerAsciiStr(std::string_view sv) { return ::Explorer2::PathUtils::ToLowerAscii(sv); }

inline std::string LocalXmlName(const std::string& fullLower) {
    size_t col = fullLower.find(':');
    return (col == std::string::npos) ? fullLower : fullLower.substr(col + 1);
}

inline std::wstring TrimWideWs(std::wstring_view sv) { return ::Explorer2::PathUtils::Trim(sv); }

// BOM / UTF-16 / declared encoding handled by the shared XML helper
inline std::string XmlBytesToUtf8(const std::vector<uint8_t>& raw) {
    return ::Explorer2::XmlTagScan::DocumentToUtf8(raw.data(), raw.size());
}

inline std::string DecodeXmlUtf8(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) return std::string(raw);
    return ::Explorer2::XmlTagScan::DecodeText(raw);
}

// Fast iterative XML tree parser (preserves mixed-content text nodes & element hierarchy)
inline bool ParseXmlTreePptx(std::string_view sv, XmlNode& outRoot) {
    outRoot = XmlNode{};
    outRoot.fullTag = "#doc";
    outRoot.localTag = "#doc";

    std::vector<XmlNode*> stack;
    stack.reserve(32);
    stack.push_back(&outRoot);

    size_t pos = 0;
    size_t nodeCount = 0;
    constexpr size_t kMaxNodes = 250000;

    while (pos < sv.size() && nodeCount < kMaxNodes) {
        if (sv[pos] != '<') {
            size_t nextLt = sv.find('<', pos);
            if (nextLt == std::string_view::npos) nextLt = sv.size();
            std::string_view chunk = sv.substr(pos, nextLt - pos);
            pos = nextLt;
            if (stack.size() > 1 && !chunk.empty()) {
                // Keep text node if non-whitespace OR if inside an inline/run/text element (<a:t>, <text:span>, <text:p>, etc.)
                bool anyNonWs = false;
                for (unsigned char c : chunk) {
                    if (c > 0x20u) { anyNonWs = true; break; }
                }
                const std::string& pLoc = stack.back()->localTag;
                if (anyNonWs || pLoc == "t" || pLoc == "p" || pLoc == "h" || pLoc == "span" || pLoc == "a" || pLoc == "v" || pLoc == "text") {
                    XmlNode tn;
                    tn.isText = true;
                    tn.text = DecodeXmlUtf8(chunk);
                    stack.back()->children.push_back(std::move(tn));
                    ++nodeCount;
                }
            }
            continue;
        }

        if (pos + 4 <= sv.size() && sv.substr(pos, 4) == "<!--") {
            size_t endC = sv.find("-->", pos + 4);
            pos = (endC == std::string_view::npos) ? sv.size() : (endC + 3);
            continue;
        }
        if (pos + 9 <= sv.size() && sv.substr(pos, 9) == "<![CDATA[") {
            size_t endCd = sv.find("]]>", pos + 9);
            if (endCd == std::string_view::npos) break;
            if (stack.size() > 1) {
                XmlNode tn;
                tn.isText = true;
                tn.text = std::string(sv.substr(pos + 9, endCd - (pos + 9)));
                stack.back()->children.push_back(std::move(tn));
                ++nodeCount;
            }
            pos = endCd + 3;
            continue;
        }
        if (pos + 2 <= sv.size() && (sv[pos + 1] == '?' || sv[pos + 1] == '!')) {
            size_t gt = sv.find('>', pos + 2);
            pos = (gt == std::string_view::npos) ? sv.size() : (gt + 1);
            continue;
        }

        // Find closing '>' respecting quoted attribute strings
        size_t gt = pos + 1;
        char inQuote = 0;
        while (gt < sv.size()) {
            char ch = sv[gt];
            if (inQuote) {
                if (ch == inQuote) inQuote = 0;
            } else {
                if (ch == '"' || ch == '\'') inQuote = ch;
                else if (ch == '>') break;
            }
            ++gt;
        }
        if (gt >= sv.size()) break;

        std::string_view tagBody = sv.substr(pos + 1, gt - (pos + 1));
        pos = gt + 1;
        while (!tagBody.empty() && static_cast<unsigned char>(tagBody.front()) <= 0x20u) tagBody.remove_prefix(1);
        while (!tagBody.empty() && static_cast<unsigned char>(tagBody.back()) <= 0x20u) tagBody.remove_suffix(1);
        if (tagBody.empty()) continue;

        bool isClosing = (tagBody.front() == '/');
        if (isClosing) {
            tagBody.remove_prefix(1);
            while (!tagBody.empty() && static_cast<unsigned char>(tagBody.front()) <= 0x20u) tagBody.remove_prefix(1);
            size_t ne = 0;
            while (ne < tagBody.size() && !DetailWordOdt::IsXmlTagDelim(tagBody[ne])) ++ne;
            std::string closeFull = LowerAsciiStr(tagBody.substr(0, ne));
            std::string closeLoc  = LocalXmlName(closeFull);
            for (size_t s = stack.size(); s > 1; --s) {
                if (stack[s - 1]->fullTag == closeFull || stack[s - 1]->localTag == closeLoc) {
                    stack.resize(s - 1);
                    break;
                }
            }
            continue;
        }

        bool selfClose = (!tagBody.empty() && tagBody.back() == '/');
        if (selfClose) tagBody.remove_suffix(1);

        size_t ne = 0;
        while (ne < tagBody.size() && !DetailWordOdt::IsXmlTagDelim(tagBody[ne])) ++ne;
        if (ne == 0) continue;

        XmlNode elem;
        elem.fullTag  = LowerAsciiStr(tagBody.substr(0, ne));
        elem.localTag = LocalXmlName(elem.fullTag);

        std::string_view attrSv = tagBody.substr(ne);
        size_t ap = 0;
        while (ap < attrSv.size()) {
            while (ap < attrSv.size() && (static_cast<unsigned char>(attrSv[ap]) <= 0x20u || attrSv[ap] == '/')) ++ap;
            if (ap >= attrSv.size()) break;
            size_t as = ap;
            while (ap < attrSv.size() && attrSv[ap] != '=' && static_cast<unsigned char>(attrSv[ap]) > 0x20u) ++ap;
            std::string_view aName = attrSv.substr(as, ap - as);
            while (ap < attrSv.size() && static_cast<unsigned char>(attrSv[ap]) <= 0x20u) ++ap;
            std::string aVal;
            if (ap < attrSv.size() && attrSv[ap] == '=') {
                ++ap;
                while (ap < attrSv.size() && static_cast<unsigned char>(attrSv[ap]) <= 0x20u) ++ap;
                if (ap < attrSv.size() && (attrSv[ap] == '"' || attrSv[ap] == '\'')) {
                    char q = attrSv[ap++];
                    size_t qe = attrSv.find(q, ap);
                    if (qe != std::string_view::npos) {
                        aVal = DecodeXmlUtf8(attrSv.substr(ap, qe - ap));
                        ap = qe + 1;
                    } else {
                        ap = attrSv.size();
                    }
                }
            }
            if (!aName.empty()) {
                std::string lowA = LowerAsciiStr(aName);
                elem.attrs.push_back({ lowA, LocalXmlName(lowA), std::move(aVal) });
            }
        }

        if (stack.size() >= 64) selfClose = true;
        stack.back()->children.push_back(std::move(elem));
        ++nodeCount;
        if (!selfClose) {
            stack.push_back(&stack.back()->children.back());
        }
    }

    return !outRoot.children.empty();
}

inline void CollectAllTextW(const XmlNode& n, std::wstring& out) {
    if (n.isText) {
        out += PathUtils::Utf8ToWide(n.text);
        return;
    }
    if (n.fullTag == "mc:fallback") return;
    for (const auto& ch : n.children) {
        CollectAllTextW(ch, out);
    }
}

inline std::wstring GetChildTextTrimmed(const XmlNode& parent, std::string_view localTag) {
    for (const auto& ch : parent.children) {
        if (!ch.isText && ch.localTag == localTag) {
            std::wstring w;
            CollectAllTextW(ch, w);
            return TrimWideWs(w);
        }
    }
    return {};
}

// Normalize OOXML archive paths (resolving '.', '..', leading '/', and backslashes)
inline std::string NormalizeZipPath(std::string_view baseDir, std::string_view target) {
    std::string combined;
    if (!target.empty() && (target.front() == '/' || target.front() == '\\')) {
        combined = std::string(target.substr(1));
    } else if (baseDir.empty()) {
        combined = std::string(target);
    } else {
        combined = std::string(baseDir) + "/" + std::string(target);
    }
    for (char& c : combined) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    std::vector<std::string_view> segs;
    size_t p = 0;
    while (p < combined.size()) {
        size_t sl = combined.find('/', p);
        if (sl == std::string::npos) sl = combined.size();
        std::string_view part(combined.data() + p, sl - p);
        p = sl + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!segs.empty()) segs.pop_back();
        } else {
            segs.push_back(part);
        }
    }
    std::string out;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i > 0) out.push_back('/');
        out.append(segs[i]);
    }
    return out;
}

inline std::string RelsPathForPart(const std::string& partPath) {
    size_t sl = partPath.rfind('/');
    if (sl == std::string::npos) return "_rels/" + partPath + ".rels";
    return partPath.substr(0, sl) + "/_rels/" + partPath.substr(sl + 1) + ".rels";
}

inline std::string DirOfPart(const std::string& partPath) {
    size_t sl = partPath.rfind('/');
    return (sl == std::string::npos) ? std::string() : partPath.substr(0, sl);
}

struct OoxmlRel {
    std::string id;
    std::string type;
    std::string target;
    bool external = false;
};

inline void ParseOoxmlRelsXml(std::string_view xmlUtf8, std::vector<OoxmlRel>& outRels) {
    outRels.clear();
    XmlNode doc;
    if (!ParseXmlTreePptx(xmlUtf8, doc)) return;
    const XmlNode* root = doc.FindFirstLocal("relationships");
    if (!root) return;
    for (const auto& ch : root->children) {
        if (ch.isText || ch.localTag != "relationship") continue;
        OoxmlRel r;
        r.id = std::string(ch.Attr("id"));
        r.type = LowerAsciiStr(ch.Attr("type"));
        r.target = std::string(ch.Attr("target"));
        r.external = (LowerAsciiStr(ch.Attr("targetmode")) == "external");
        if (!r.id.empty() && !r.target.empty()) {
            outRels.push_back(std::move(r));
        }
    }
}

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

static bool ParsePowerPointPptx(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
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

    if (extLower == L".odp" || extLower == L".otp") {
        return ParseOdpPresentationArchive(fullPath, entries, entryMap, out);
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
