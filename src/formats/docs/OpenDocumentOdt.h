#pragma once
// OpenDocumentOdt.h — OASIS OpenDocument Text (.odt) Reader

namespace DetailWordOdt {

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

namespace Formats {

inline bool ParseOpenDocumentOdt(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    std::vector<uint8_t> docBytes;
    if (!ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", docBytes) || docBytes.empty()) return false;
    std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
    return DetailWordOdt::ParseOdfWriterXml(sv, out);
}

} // namespace Formats
