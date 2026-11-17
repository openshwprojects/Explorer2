#pragma once

namespace DetailWordOdt {

inline bool IsXmlTagDelim(char c) {
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline std::string_view GetXmlAttr(std::string_view tagBody, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tagBody, attrName, v, false) ? v : std::string_view();
}

inline int ParsePositiveIntAttr(std::string_view val, int defVal) {
    if (val.empty()) return defVal;
    int acc = 0;
    for (char c : val) {
        if (c < '0' || c > '9') return defVal;
        acc = acc * 10 + (c - '0');
        if (acc > 10000) return 10000;
    }
    return (acc > 0) ? acc : defVal;
}

inline void EmitDocParagraph(ParsedOfficeDocument& out, DocParagraph&& para) {
    if (para.text.empty()) return;
    CountDocText(out, para.text);
    out.paragraphs.push_back(std::move(para));
}

// ---- WordprocessingML (.docx): word/document.xml, with word/styles.xml for heading levels and lists ----

struct WordStyle {
    int outline = -1;        // w:outlineLvl of the style (0 = Heading 1 .. 8, 9 = body text), -1 = not set
    int builtinHeading = -1; // outline implied by the built-in style name ("heading 1", "Title", "Subtitle")
    int numbered = -1;       // w:numPr/w:numId of the style: 1 = numbered, 0 = numbering turned off, -1 = not set
    std::string basedOn;
};
using WordStyleMap = std::map<std::string, WordStyle, std::less<>>;

// Outline level implied by a built-in style name or id: "heading 1".."heading 9" ("Heading1"), "Title", "Subtitle"
inline int BuiltinHeadingOutline(std::string_view name) {
    std::string low;
    for (char c : name) {
        if (c != ' ') low.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c));
    }
    if (low == "title") return 0;
    if (low == "subtitle") return 1;
    if (low.size() == 8 && low.compare(0, 7, "heading") == 0 && low[7] >= '1' && low[7] <= '9') return low[7] - '1';
    return -1;
}

inline int ParseOutlineLevel(std::string_view val) {
    return (val.size() == 1 && val[0] >= '0' && val[0] <= '9') ? val[0] - '0' : -1;
}

inline WordStyleMap ParseWordStyles(std::string_view xml) {
    namespace X = ::Explorer2::XmlTagScan;
    WordStyleMap styles;
    X::ForEachElement(xml, "style", [&](const X::Element& e) {
        if (X::GetAttr(e.attrs, "type") != "paragraph") return true;
        std::string id(X::GetAttr(e.attrs, "styleId"));
        if (id.empty() || styles.size() >= 4096) return true;
        WordStyle st;
        X::Element child, pPr, numPr;
        if (X::FindChild(e.body, "name", child)) st.builtinHeading = BuiltinHeadingOutline(X::GetAttr(child.attrs, "val"));
        if (X::FindChild(e.body, "basedOn", child)) st.basedOn = std::string(X::GetAttr(child.attrs, "val"));
        if (X::FindChild(e.body, "pPr", pPr)) {
            if (X::FindChild(pPr.body, "outlineLvl", child)) st.outline = ParseOutlineLevel(X::GetAttr(child.attrs, "val"));
            if (X::FindChild(pPr.body, "numPr", numPr) && X::FindChild(numPr.body, "numId", child)) {
                st.numbered = (X::GetAttr(child.attrs, "val") == "0") ? 0 : 1;
            }
        }
        styles.emplace(std::move(id), std::move(st));
        return true;
    });
    return styles;
}

// Outline level (0-8) a paragraph style gives through its basedOn chain, -1 for none. Without a styles part
// (or for an id it does not define) the built-in ids ("Heading1", "Title", ...) still count.
inline int StyleOutline(const WordStyleMap& styles, std::string_view styleId) {
    std::string_view id = styleId;
    for (int hop = 0; hop < 16 && !id.empty(); ++hop) {
        auto it = styles.find(id);
        if (it == styles.end()) return (hop == 0) ? BuiltinHeadingOutline(id) : -1;
        const WordStyle& st = it->second;
        if (st.outline >= 0) return (st.outline <= 8) ? st.outline : -1;
        if (st.builtinHeading >= 0) return st.builtinHeading;
        id = st.basedOn;
    }
    return -1;
}

inline bool StyleNumbered(const WordStyleMap& styles, std::string_view styleId) {
    std::string_view id = styleId;
    for (int hop = 0; hop < 16 && !id.empty(); ++hop) {
        auto it = styles.find(id);
        if (it == styles.end()) return false;
        if (it->second.numbered >= 0) return it->second.numbered == 1;
        id = it->second.basedOn;
    }
    return false;
}

// Walks the body element by element. Paragraphs may nest (a text box's paragraphs sit inside the paragraph that
// anchors it); table rows become one "cell | cell" paragraph each; deleted or moved-away text, the fallback copy
// of alternate content and ruby guide text are not part of what Word shows, so they are skipped.
inline bool ParseOoxmlWordXml(std::string_view sv, const WordStyleMap& styles, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    {
        size_t probe = 0;
        X::XmlTag t;
        bool sawRoot = false, sawBody = false;
        while (!sawBody && X::NextTag(sv, probe, t)) {
            if (t.isClosing) continue;
            if (!sawRoot) {
                if (t.localName != "document") return false;
                sawRoot = true;
            } else if (t.localName == "body") {
                sawBody = true;
            }
        }
        if (!sawBody) return false;
    }

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Microsoft Word Document (.docx)";

    struct OpenPara {
        DocParagraph para;
        std::string styleId;
        int outline = -1;  // the paragraph's own w:outlineLvl
        int numbered = -1; // the paragraph's own w:numPr/w:numId (0 = numbering turned off)
    };
    std::vector<OpenPara> paras;
    int skipDepth = 0, pPrDepth = 0, tableDepth = 0, deepParas = 0; // deepParas: nested past the 64 kept open
    bool inCell = false;
    std::vector<std::wstring> rowCells;
    std::wstring cellText;

    auto isSkipped = [](std::string_view n) {
        return n == "del" || n == "moveFrom" || n == "Fallback" || n == "pPrChange" || n == "rPrChange" || n == "rt";
    };
    auto finishParagraph = [&](OpenPara&& op) {
        int outline = op.outline;
        if (outline < 0 && !op.styleId.empty()) outline = StyleOutline(styles, op.styleId);
        if (outline >= 0 && outline <= 8) op.para.headingLevel = std::min(outline + 1, 3);
        bool numbered = (op.numbered >= 0) ? (op.numbered == 1) : (!op.styleId.empty() && StyleNumbered(styles, op.styleId));
        op.para.isBullet = numbered && op.para.headingLevel == 0;
        if (tableDepth > 0 && inCell) {
            if (!op.para.text.empty()) {
                if (!cellText.empty()) cellText.push_back(L' ');
                cellText += op.para.text;
            }
        } else {
            EmitDocParagraph(out, std::move(op.para));
        }
    };

    size_t pos = 0;
    X::XmlTag t;
    while (X::NextTag(sv, pos, t)) {
        const std::string_view n = t.localName;
        if (isSkipped(n)) {
            if (t.isClosing) { if (skipDepth > 0) --skipDepth; }
            else if (!t.isSelfClosing) ++skipDepth;
            continue;
        }
        if (skipDepth > 0) continue;

        if (n == "p") {
            if (t.isClosing) {
                if (deepParas > 0) {
                    --deepParas; // its text went to the paragraph enclosing it
                } else if (!paras.empty()) {
                    OpenPara op = std::move(paras.back());
                    paras.pop_back();
                    pPrDepth = 0;
                    finishParagraph(std::move(op));
                }
            } else if (!t.isSelfClosing) {
                if (paras.size() < 64) paras.emplace_back();
                else ++deepParas;
                pPrDepth = 0;
            }
            continue;
        }
        if (n == "pPr") {
            if (t.isClosing) { if (pPrDepth > 0) --pPrDepth; }
            else if (!t.isSelfClosing && !paras.empty()) ++pPrDepth;
            continue;
        }
        if (pPrDepth > 0) { // paragraph properties: style, outline level, numbering (tab stops here are not text)
            if (t.isClosing || paras.empty()) continue;
            OpenPara& op = paras.back();
            if (n == "pStyle") op.styleId = std::string(X::GetAttr(t.attrs, "val"));
            else if (n == "outlineLvl") op.outline = ParseOutlineLevel(X::GetAttr(t.attrs, "val"));
            else if (n == "numId") op.numbered = (X::GetAttr(t.attrs, "val") == "0") ? 0 : 1;
            continue;
        }

        if (n == "tbl") {
            if (t.isClosing) {
                if (tableDepth > 0 && --tableDepth == 0) inCell = false;
            } else if (!t.isSelfClosing) {
                ++tableDepth;
            }
            continue;
        }
        if (tableDepth == 1 && n == "tr") {
            if (t.isClosing) {
                while (!rowCells.empty() && rowCells.back().empty()) rowCells.pop_back();
                if (!rowCells.empty()) {
                    DocParagraph row;
                    for (size_t i = 0; i < rowCells.size(); ++i) {
                        if (i > 0) row.text += L" | ";
                        row.text += rowCells[i];
                    }
                    row.isTableRow = true;
                    EmitDocParagraph(out, std::move(row));
                }
            }
            rowCells.clear();
            inCell = false;
            continue;
        }
        if (tableDepth == 1 && n == "tc") {
            if (t.isClosing) {
                rowCells.push_back(std::move(cellText));
                inCell = false;
            } else if (t.isSelfClosing) {
                rowCells.emplace_back();
            } else {
                inCell = true;
            }
            cellText.clear();
            continue;
        }

        if (t.isClosing || paras.empty()) continue;
        std::wstring& text = paras.back().para.text;
        if (n == "t") {
            if (t.isSelfClosing) continue;
            std::string_view body;
            size_t after = 0;
            if (!X::FindElementBody(sv, t.afterTag, "t", body, after)) break;
            pos = after;
            std::string decoded = X::DecodeText(body);
            std::string_view run = decoded;
            if (X::GetAttr(t.attrs, "space") != "preserve") run = X::TrimWs(run); // Word drops it without xml:space
            text += PathUtils::Utf8ToWide(run);
        } else if (n == "tab" || n == "ptab") {
            text.push_back(L'\t');
        } else if (n == "br" || n == "cr") {
            text.push_back(L'\n');
        } else if (n == "noBreakHyphen") {
            text.push_back(L'-');
        }
    }
    while (!paras.empty()) { // a document cut short: keep what its open paragraphs hold
        OpenPara op = std::move(paras.back());
        paras.pop_back();
        finishParagraph(std::move(op));
    }

    if (out.paragraphs.empty()) {
        out.paragraphs.push_back({ L"(Pusty dokument Word .docx / Empty Word document)", 0, false, false });
    }
    out.valid = true;
    return true;
}

// Resolves an OPC relationship: the target of the first relationship whose type ends with `typeSuffix` in the
// .rels part belonging to `partName` ("" = the package itself), as a part name relative to the package root
inline std::wstring FindRelatedPart(const std::wstring& zipPath, const std::string& partName, std::string_view typeSuffix) {
    namespace X = ::Explorer2::XmlTagScan;
    size_t slash = partName.rfind('/');
    std::string folder = (slash == std::string::npos) ? std::string() : partName.substr(0, slash + 1);
    std::string fileName = (slash == std::string::npos) ? partName : partName.substr(slash + 1);
    std::string relsName = folder + "_rels/" + fileName + ".rels";
    std::vector<uint8_t> rels;
    if (!ZipDeflate::ExtractZipFileByName(zipPath, PathUtils::Utf8ToWide(relsName), rels) || rels.empty()) return {};
    std::string xml = X::DocumentToUtf8(rels.data(), rels.size());
    std::string target;
    X::ForEachElement(xml, "Relationship", [&](const X::Element& e) {
        std::string_view type = X::GetAttr(e.attrs, "Type");
        if (type.size() < typeSuffix.size() || type.substr(type.size() - typeSuffix.size()) != typeSuffix) return true;
        if (X::GetAttr(e.attrs, "TargetMode") == "External") return true;
        target = X::AttrText(e.attrs, "Target");
        return false;
    });
    if (target.empty()) return {};
    std::string resolved = (target.front() == '/') ? std::string() : folder;
    size_t i = (target.front() == '/') ? 1 : 0;
    while (i < target.size()) {
        size_t next = target.find('/', i);
        std::string seg = target.substr(i, (next == std::string::npos ? target.size() : next) - i);
        i = (next == std::string::npos) ? target.size() : next + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!resolved.empty()) resolved.pop_back(); // the trailing '/'
            size_t cut = resolved.rfind('/');
            resolved = (cut == std::string::npos) ? std::string() : resolved.substr(0, cut + 1);
            continue;
        }
        resolved += seg;
        if (next != std::string::npos) resolved.push_back('/');
    }
    return PathUtils::Utf8ToWide(resolved);
}

inline bool ParseWordprocessingPackage(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    std::wstring mainPart = FindRelatedPart(fullPath, std::string(), "/officeDocument");
    if (mainPart.empty()) mainPart = L"word/document.xml";
    std::vector<uint8_t> docBytes;
    if (!ZipDeflate::ExtractZipFileByName(fullPath, mainPart, docBytes) || docBytes.empty()) return false;
    std::string docXml = ::Explorer2::XmlTagScan::DocumentToUtf8(docBytes.data(), docBytes.size());
    docBytes.clear();
    docBytes.shrink_to_fit();

    WordStyleMap styles;
    std::wstring stylesPart = FindRelatedPart(fullPath, PathUtils::WideToUtf8(mainPart), "/styles");
    if (stylesPart.empty()) stylesPart = L"word/styles.xml";
    std::vector<uint8_t> stylesBytes;
    if (ZipDeflate::ExtractZipFileByName(fullPath, stylesPart, stylesBytes) && !stylesBytes.empty()) {
        styles = ParseWordStyles(::Explorer2::XmlTagScan::DocumentToUtf8(stylesBytes.data(), stylesBytes.size()));
    }
    return ParseOoxmlWordXml(docXml, styles, out);
}

inline bool ParseOdfWriterXml(std::string_view sv, ParsedOfficeDocument& out) {
    if (sv.find("<office:document-content") == std::string_view::npos &&
        sv.find("<office:body") == std::string_view::npos &&
        sv.find("<office:text") == std::string_view::npos) {
        return false;
    }

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"OpenDocument Text (.odt)";

    std::string_view bodySv = sv;
    size_t otStart = sv.find("<office:text");
    if (otStart != std::string_view::npos) {
        size_t otGt = sv.find('>', otStart);
        size_t otEnd = sv.find("</office:text>", otStart);
        if (otGt != std::string_view::npos && otEnd != std::string_view::npos && otEnd > otGt) {
            bodySv = sv.substr(otGt + 1, otEnd - (otGt + 1));
        }
    }

    int skipDepth = 0;
    int listDepth = 0;
    int tableDepth = 0;
    bool inTableRow = false;
    bool inTableCell = false;
    int blockDepth = 0;

    DocParagraph curPara;
    std::vector<std::wstring> rowCells;
    std::wstring cellText;

    size_t pos = 0;
    while (pos < bodySv.size()) {
        if (bodySv[pos] != '<') {
            size_t nextLt = bodySv.find('<', pos);
            if (nextLt == std::string_view::npos) nextLt = bodySv.size();
            if (skipDepth == 0 && blockDepth > 0) {
                std::wstring decoded = ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(bodySv.substr(pos, nextLt - pos)));
                if (inTableCell) cellText += decoded;
                else curPara.text += decoded;
            }
            pos = nextLt;
            continue;
        }

        // XML Comments / CDATA / Processing Instructions
        if (pos + 4 <= bodySv.size() && bodySv.substr(pos, 4) == "<!--") {
            size_t endC = bodySv.find("-->", pos + 4);
            pos = (endC == std::string_view::npos) ? bodySv.size() : (endC + 3);
            continue;
        }
        if (pos + 9 <= bodySv.size() && bodySv.substr(pos, 9) == "<![CDATA[") {
            size_t endCd = bodySv.find("]]>", pos + 9);
            if (endCd == std::string_view::npos) break;
            if (skipDepth == 0 && blockDepth > 0) {
                std::wstring w = PathUtils::Utf8ToWide(bodySv.substr(pos + 9, endCd - (pos + 9)));
                if (inTableCell) cellText += w;
                else curPara.text += w;
            }
            pos = endCd + 3;
            continue;
        }
        if (pos + 2 <= bodySv.size() && (bodySv[pos + 1] == '?' || bodySv[pos + 1] == '!')) {
            size_t gt = bodySv.find('>', pos + 2);
            pos = (gt == std::string_view::npos) ? bodySv.size() : (gt + 1);
            continue;
        }

        size_t gt = bodySv.find('>', pos + 1);
        if (gt == std::string_view::npos) break;
        std::string_view tagContent = bodySv.substr(pos + 1, gt - (pos + 1));
        pos = gt + 1;
        if (tagContent.empty()) continue;

        bool isClosing = (tagContent.front() == '/');
        if (isClosing) tagContent.remove_prefix(1);
        bool isSelfClosing = (!isClosing && !tagContent.empty() && tagContent.back() == '/');
        if (isSelfClosing) tagContent.remove_suffix(1);

        size_t nmEnd = 0;
        while (nmEnd < tagContent.size() && !IsXmlTagDelim(tagContent[nmEnd])) ++nmEnd;
        std::string_view tagName = tagContent.substr(0, nmEnd);
        std::string_view attrPart = tagContent.substr(nmEnd);

        // Skip margin annotations and tracked-change metadata blocks
        if (tagName == "office:annotation" || tagName == "text:tracked-changes") {
            if (isClosing) { if (skipDepth > 0) --skipDepth; }
            else if (!isSelfClosing) ++skipDepth;
            continue;
        }
        if (skipDepth > 0) continue;

        if (tagName == "text:list-item") {
            if (isClosing) { if (listDepth > 0) --listDepth; }
            else if (!isSelfClosing) ++listDepth;
            continue;
        }

        if (tagName == "table:table") {
            if (isClosing) {
                if (tableDepth > 0) --tableDepth;
                inTableRow = false;
                inTableCell = false;
            } else if (!isSelfClosing) {
                if (blockDepth > 0 && tableDepth == 0) {
                    EmitDocParagraph(out, std::move(curPara));
                    curPara = DocParagraph{};
                    blockDepth = 0;
                }
                ++tableDepth;
            }
            continue;
        }

        if (tableDepth > 0) {
            if (tagName == "table:table-row") {
                if (isClosing) {
                    while (!rowCells.empty() && rowCells.back().empty()) rowCells.pop_back();
                    if (!rowCells.empty()) {
                        std::wstring joined;
                        for (size_t ci = 0; ci < rowCells.size(); ++ci) {
                            if (ci > 0) joined += L" | ";
                            joined += rowCells[ci];
                        }
                        DocParagraph rowPara;
                        rowPara.text = std::move(joined);
                        rowPara.isTableRow = true;
                        EmitDocParagraph(out, std::move(rowPara));
                    }
                    rowCells.clear();
                    inTableRow = false;
                    inTableCell = false;
                    blockDepth = 0;
                } else if (!isSelfClosing) {
                    inTableRow = true;
                    inTableCell = false;
                    rowCells.clear();
                    blockDepth = 0;
                }
                continue;
            }

            if (inTableRow && tagName == "table:table-cell") {
                if (isClosing) {
                    rowCells.push_back(std::move(cellText));
                    cellText.clear();
                    inTableCell = false;
                    blockDepth = 0;
                } else if (isSelfClosing) {
                    int rep = ParsePositiveIntAttr(GetXmlAttr(attrPart, "table:number-columns-repeated"), 1);
                    if (rep <= 16) {
                        for (int r = 0; r < rep; ++r) rowCells.emplace_back();
                    }
                } else {
                    inTableCell = true;
                    cellText.clear();
                    blockDepth = 0;
                }
                continue;
            }

            if (inTableCell && (tagName == "text:p" || tagName == "text:h")) {
                if (isClosing) {
                    if (blockDepth > 0) --blockDepth;
                } else if (!isSelfClosing) {
                    if (blockDepth == 0 && !cellText.empty()) cellText += L" ";
                    ++blockDepth;
                }
                continue;
            }
        } else {
            if (tagName == "text:p" || tagName == "text:h") {
                if (isClosing) {
                    if (blockDepth > 0) {
                        --blockDepth;
                        if (blockDepth == 0) {
                            EmitDocParagraph(out, std::move(curPara));
                            curPara = DocParagraph{};
                        }
                    }
                } else if (!isSelfClosing) {
                    if (blockDepth == 0) {
                        curPara = DocParagraph{};
                        if (tagName == "text:h") {
                            int lvl = ParsePositiveIntAttr(GetXmlAttr(attrPart, "text:outline-level"), 1);
                            curPara.headingLevel = std::clamp(lvl, 1, 3);
                        } else if (listDepth > 0) {
                            curPara.isBullet = true;
                        }
                    }
                    ++blockDepth;
                }
                continue;
            }
        }

        // Inline whitespace elements (<text:s/>, <text:tab/>, <text:line-break/>)
        if (blockDepth > 0 && !isClosing) {
            std::wstring& dst = inTableCell ? cellText : curPara.text;
            if (tagName == "text:s") {
                int count = std::min(256, ParsePositiveIntAttr(GetXmlAttr(attrPart, "text:c"), 1));
                dst.append(static_cast<size_t>(count), L' ');
            } else if (tagName == "text:tab") {
                dst.push_back(L'\t');
            } else if (tagName == "text:line-break") {
                dst.push_back(L'\n');
            }
        }
    }

    if (blockDepth > 0 && !inTableCell && !curPara.text.empty()) {
        EmitDocParagraph(out, std::move(curPara));
    }

    if (out.paragraphs.empty()) {
        out.paragraphs.push_back({ L"(Empty OpenDocument Text .odt)", 0, false, false });
    }
    out.valid = true;
    return true;
}

} // namespace DetailWordOdt

static bool ParseWordDocx(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    std::wstring ext = PathUtils::ToLower(fullPath);
    size_t dotPos = ext.find_last_of(L".\\/");
    if (dotPos != std::wstring::npos && ext[dotPos] == L'.') {
        ext = ext.substr(dotPos);
    } else {
        ext.clear();
    }

    auto parseOdt = [&]() {
        std::vector<uint8_t> docBytes;
        if (!ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", docBytes) || docBytes.empty()) return false;
        std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
        return DetailWordOdt::ParseOdfWriterXml(sv, out);
    };
    if (ext == L".odt" || ext == L".ott") return parseOdt();
    if (ext == L".docx" || ext == L".docm" || ext == L".dotx" || ext == L".dotm") {
        return DetailWordOdt::ParseWordprocessingPackage(fullPath, out);
    }
    return DetailWordOdt::ParseWordprocessingPackage(fullPath, out) || parseOdt();
}
