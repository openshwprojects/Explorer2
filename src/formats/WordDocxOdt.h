#pragma once

static bool ParseWordDocx(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = L"Microsoft Word Document (.docx)";

    std::vector<uint8_t> docBytes;
    if (!ZipDeflate::ExtractZipFileByName(fullPath, L"word/document.xml", docBytes) &&
        !ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", docBytes)) {
        return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(docBytes.data()), docBytes.size());
    size_t pPos = 0;
    while ((pPos = sv.find("<w:p", pPos)) != std::string_view::npos) {
        char after = (pPos + 4 < sv.size()) ? sv[pPos + 4] : ' ';
        if (after != '>' && after != ' ' && after != '/') {
            pPos += 4;
            continue;
        }
        size_t pGt = sv.find('>', pPos);
        if (pGt == std::string_view::npos) break;
        if (sv[pGt - 1] == '/') { pPos = pGt + 1; continue; }
        size_t endP = sv.find("</w:p>", pGt);
        if (endP == std::string_view::npos) break;
        std::string_view pBlk = sv.substr(pGt + 1, endP - (pGt + 1));

        DocParagraph para;
        if (pBlk.find("Heading1") != std::string_view::npos || pBlk.find("heading 1") != std::string_view::npos || pBlk.find("Title") != std::string_view::npos) {
            para.headingLevel = 1;
        } else if (pBlk.find("Heading2") != std::string_view::npos || pBlk.find("heading 2") != std::string_view::npos) {
            para.headingLevel = 2;
        } else if (pBlk.find("Heading3") != std::string_view::npos) {
            para.headingLevel = 3;
        }
        if (pBlk.find("<w:numPr>") != std::string_view::npos) {
            para.isBullet = true;
        }

        size_t tPos = 0;
        while ((tPos = pBlk.find("<w:t", tPos)) != std::string_view::npos) {
            size_t tGt = pBlk.find('>', tPos);
            if (tGt == std::string_view::npos) break;
            if (pBlk[tGt - 1] == '/') { tPos = tGt + 1; continue; }
            size_t endT = pBlk.find("</w:t>", tGt);
            if (endT == std::string_view::npos) break;
            para.text += DecodeXmlEntities(PathUtils::Utf8ToWide(pBlk.substr(tGt + 1, endT - (tGt + 1))));
            tPos = endT + 6;
        }

        if (!para.text.empty()) {
            out.charCount += para.text.size();
            bool inWord = false;
            for (wchar_t ch : para.text) {
                if (ch > L' ') {
                    if (!inWord) { ++out.wordCount; inWord = true; }
                } else {
                    inWord = false;
                }
            }
            out.paragraphs.push_back(std::move(para));
        }
        pPos = endP + 6;
    }

    if (out.paragraphs.empty()) {
        out.paragraphs.push_back({ L"(Pusty dokument Word .docx / Empty Word document)", 0, false, false });
    }
    out.valid = true;
    return true;
}
