#pragma once

namespace DetailWordOdt {

inline void AppendCodePointUtf16(std::wstring& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return;
    if (cp <= 0xFFFFu) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000u;
        out.push_back(static_cast<wchar_t>(0xD800u + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00u + (cp & 0x3FFu)));
    }
}

inline std::wstring DecodeRichXmlEntities(std::wstring_view in) {
    std::wstring out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (in[i] == L'&') {
            size_t semi = in.find(L';', i + 1);
            if (semi != std::wstring_view::npos && semi - i <= 12) {
                std::wstring_view ent = in.substr(i + 1, semi - (i + 1));
                if (ent == L"amp")  { out.push_back(L'&');  i = semi + 1; continue; }
                if (ent == L"lt")   { out.push_back(L'<');  i = semi + 1; continue; }
                if (ent == L"gt")   { out.push_back(L'>');  i = semi + 1; continue; }
                if (ent == L"quot") { out.push_back(L'"');  i = semi + 1; continue; }
                if (ent == L"apos") { out.push_back(L'\''); i = semi + 1; continue; }
                if (ent == L"nbsp") { out.push_back(L' ');  i = semi + 1; continue; }
                if (ent.size() >= 2 && ent[0] == L'#') {
                    uint32_t cp = 0;
                    bool ok = true;
                    if (ent[1] == L'x' || ent[1] == L'X') {
                        if (ent.size() < 3) ok = false;
                        for (size_t k = 2; k < ent.size() && ok; ++k) {
                            wchar_t c = ent[k];
                            uint32_t d = 0;
                            if (c >= L'0' && c <= L'9') d = static_cast<uint32_t>(c - L'0');
                            else if (c >= L'a' && c <= L'f') d = static_cast<uint32_t>(c - L'a' + 10);
                            else if (c >= L'A' && c <= L'F') d = static_cast<uint32_t>(c - L'A' + 10);
                            else ok = false;
                            cp = (cp << 4) | d;
                            if (cp > 0x10FFFFu) ok = false;
                        }
                    } else {
                        for (size_t k = 1; k < ent.size() && ok; ++k) {
                            wchar_t c = ent[k];
                            if (c >= L'0' && c <= L'9') {
                                cp = cp * 10u + static_cast<uint32_t>(c - L'0');
                                if (cp > 0x10FFFFu) ok = false;
                            } else {
                                ok = false;
                            }
                        }
                    }
                    if (ok && cp > 0u) {
                        AppendCodePointUtf16(out, cp);
                        i = semi + 1;
                        continue;
                    }
                }
            }
        }
        out.push_back(in[i++]);
    }
    return out;
}

inline bool IsXmlTagDelim(char c) {
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline std::string_view GetXmlAttr(std::string_view tagBody, std::string_view attrName) {
    size_t pos = 0;
    while ((pos = tagBody.find(attrName, pos)) != std::string_view::npos) {
        bool validStart = (pos == 0) || tagBody[pos - 1] == ' ' || tagBody[pos - 1] == '\t' ||
                          tagBody[pos - 1] == '\r' || tagBody[pos - 1] == '\n';
        size_t eq = pos + attrName.size();
        while (eq < tagBody.size() && (tagBody[eq] == ' ' || tagBody[eq] == '\t')) ++eq;
        if (validStart && eq < tagBody.size() && tagBody[eq] == '=') {
            size_t q = eq + 1;
            while (q < tagBody.size() && (tagBody[q] == ' ' || tagBody[q] == '\t')) ++q;
            if (q < tagBody.size() && (tagBody[q] == '"' || tagBody[q] == '\'')) {
                char quote = tagBody[q];
                size_t endQ = tagBody.find(quote, q + 1);
                if (endQ != std::string_view::npos) {
                    return tagBody.substr(q + 1, endQ - (q + 1));
                }
            }
        }
        pos += attrName.size();
    }
    return {};
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
    out.charCount += para.text.size();
    bool inWord = false;
    for (wchar_t ch : para.text) {
        if (ch > L' ') {
            if (!inWord) {
                ++out.wordCount;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }
    out.paragraphs.push_back(std::move(para));
}

inline bool ParseOoxmlWordXml(std::string_view sv, ParsedOfficeDocument& out) {
    if (sv.find("<w:document") == std::string_view::npos &&
        sv.find("<w:body") == std::string_view::npos &&
        sv.find("<w:p") == std::string_view::npos) {
        return false;
    }

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Microsoft Word Document (.docx)";

    size_t pPos = 0;
    while ((pPos = sv.find("<w:p", pPos)) != std::string_view::npos) {
        char after = (pPos + 4 < sv.size()) ? sv[pPos + 4] : ' ';
        if (!IsXmlTagDelim(after)) {
            pPos += 4;
            continue;
        }
        size_t pGt = sv.find('>', pPos);
        if (pGt == std::string_view::npos) break;
        if (sv[pGt - 1] == '/') {
            pPos = pGt + 1;
            continue;
        }
        size_t endP = sv.find("</w:p>", pGt);
        if (endP == std::string_view::npos) break;
        std::string_view pBlk = sv.substr(pGt + 1, endP - (pGt + 1));

        DocParagraph para;
        if (pBlk.find("Heading1") != std::string_view::npos ||
            pBlk.find("heading 1") != std::string_view::npos ||
            pBlk.find("Title") != std::string_view::npos) {
            para.headingLevel = 1;
        } else if (pBlk.find("Heading2") != std::string_view::npos ||
                   pBlk.find("heading 2") != std::string_view::npos ||
                   pBlk.find("Subtitle") != std::string_view::npos) {
            para.headingLevel = 2;
        } else if (pBlk.find("Heading3") != std::string_view::npos ||
                   pBlk.find("heading 3") != std::string_view::npos) {
            para.headingLevel = 3;
        }
        if (pBlk.find("<w:numPr") != std::string_view::npos) {
            para.isBullet = true;
        }

        size_t cur = 0;
        while ((cur = pBlk.find('<', cur)) != std::string_view::npos) {
            size_t gt = pBlk.find('>', cur + 1);
            if (gt == std::string_view::npos) break;
            std::string_view tag = pBlk.substr(cur + 1, gt - (cur + 1));
            bool selfClose = (!tag.empty() && tag.back() == '/');

            if (tag.size() >= 3 && tag.substr(0, 3) == "w:t" &&
                (tag.size() == 3 || IsXmlTagDelim(tag[3])))
            {
                if (selfClose) {
                    cur = gt + 1;
                    continue;
                }
                size_t endT = pBlk.find("</w:t>", gt + 1);
                if (endT == std::string_view::npos) break;
                std::wstring rawW = PathUtils::Utf8ToWide(pBlk.substr(gt + 1, endT - (gt + 1)));
                para.text += DecodeRichXmlEntities(rawW);
                cur = endT + 6;
                continue;
            }
            if (tag.size() >= 5 && tag.substr(0, 5) == "w:tab" &&
                (tag.size() == 5 || IsXmlTagDelim(tag[5])))
            {
                para.text.push_back(L'\t');
                cur = gt + 1;
                continue;
            }
            if (tag.size() >= 4 && (tag.substr(0, 4) == "w:br" || tag.substr(0, 4) == "w:cr") &&
                (tag.size() == 4 || IsXmlTagDelim(tag[4])))
            {
                para.text.push_back(L'\n');
                cur = gt + 1;
                continue;
            }
            cur = gt + 1;
        }

        EmitDocParagraph(out, std::move(para));
        pPos = endP + 6;
    }

    if (out.paragraphs.empty()) {
        out.paragraphs.push_back({ L"(Pusty dokument Word .docx / Empty Word document)", 0, false, false });
    }
    out.valid = true;
    return true;
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
                std::wstring decoded = DecodeRichXmlEntities(PathUtils::Utf8ToWide(bodySv.substr(pos, nextLt - pos)));
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

    std::vector<uint8_t> docBytes;
    if (ext == L".odt" || ext == L".ott") {
        if (!ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", docBytes) || docBytes.empty()) {
            return false;
        }
        std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
        return DetailWordOdt::ParseOdfWriterXml(sv, out);
    }

    if (ext == L".docx" || ext == L".docm" || ext == L".dotx" || ext == L".dotm") {
        if (!ZipDeflate::ExtractZipFileByName(fullPath, L"word/document.xml", docBytes) || docBytes.empty()) {
            return false;
        }
        std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
        return DetailWordOdt::ParseOoxmlWordXml(sv, out);
    }

    if (ZipDeflate::ExtractZipFileByName(fullPath, L"word/document.xml", docBytes) && !docBytes.empty()) {
        std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
        return DetailWordOdt::ParseOoxmlWordXml(sv, out);
    }
    if (ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", docBytes) && !docBytes.empty()) {
        std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
        return DetailWordOdt::ParseOdfWriterXml(sv, out);
    }
    return false;
}
