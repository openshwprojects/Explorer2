#pragma once

static bool ParsePowerPointPptx(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::PowerPointPptx;
    out.isSpreadsheet = false;
    out.formatTitle = (extLower == L".odp")
        ? L"OpenDocument Presentation (.odp)"
        : L"Microsoft PowerPoint Presentation (.pptx)";

    if (extLower == L".odp") {
        std::vector<uint8_t> xmlBytes;
        if (!ZipDeflate::ExtractZipFileByName(fullPath, L"content.xml", xmlBytes) || xmlBytes.empty()) {
            return false;
        }
        std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
        size_t pos = 0;
        int slideIdx = 1;
        while ((pos = sv.find("<draw:page", pos)) != std::string_view::npos) {
            size_t gt = sv.find('>', pos);
            if (gt == std::string_view::npos) break;
            size_t endPage = sv.find("</draw:page>", gt);
            if (endPage == std::string_view::npos) break;
            std::string_view pageBlk = sv.substr(gt + 1, endPage - (gt + 1));

            AddDocParagraph(out, L"Slajd " + std::to_wstring(slideIdx) + L" / Slide " + std::to_wstring(slideIdx), 1, false);
            size_t pPos = 0;
            bool firstPara = true;
            while ((pPos = pageBlk.find("<text:p", pPos)) != std::string_view::npos) {
                size_t pGt = pageBlk.find('>', pPos);
                if (pGt == std::string_view::npos) break;
                if (pageBlk[pGt - 1] == '/') { pPos = pGt + 1; continue; }
                size_t endP = pageBlk.find("</text:p>", pGt);
                if (endP == std::string_view::npos) break;
                std::wstring pTxt = StripXmlHtmlTags(pageBlk.substr(pGt + 1, endP - (pGt + 1)));
                if (!pTxt.empty()) {
                    AddDocParagraph(out, std::move(pTxt), firstPara ? 2 : 0, !firstPara);
                    firstPara = false;
                }
                pPos = endP + 9;
            }
            ++slideIdx;
            pos = endPage + 12;
        }
        out.valid = !out.paragraphs.empty();
        return out.valid;
    }

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, entries)) return false;

    struct SlideEntry {
        int slideNum = 0;
        ZipDeflate::ArchiveItemMeta meta;
    };
    std::vector<SlideEntry> slides;
    for (const auto& e : entries) {
        std::wstring lp = PathUtils::ToLower(e.pathInArchive);
        std::replace(lp.begin(), lp.end(), L'\\', L'/');
        if (lp.rfind(L"ppt/slides/slide", 0) == 0 && lp.size() > 20 && lp.substr(lp.size() - 4) == L".xml") {
            std::wstring numStr = lp.substr(16, lp.size() - 20);
            int num = _wtoi(numStr.c_str());
            if (num > 0) {
                slides.push_back({ num, e });
            }
        }
    }
    std::sort(slides.begin(), slides.end(), [](const SlideEntry& a, const SlideEntry& b) {
        return a.slideNum < b.slideNum;
    });

    for (const auto& s : slides) {
        std::vector<uint8_t> sBytes;
        if (!ZipDeflate::ExtractZipEntryFromFile(fullPath, s.meta, sBytes) || sBytes.empty()) continue;

        AddDocParagraph(out, L"Slajd " + std::to_wstring(s.slideNum) + L" / Slide " + std::to_wstring(s.slideNum), 1, false);
        std::string_view sv(reinterpret_cast<const char*>(sBytes.data()), sBytes.size());
        size_t pPos = 0;
        bool firstOnSlide = true;
        while ((pPos = sv.find("<a:p", pPos)) != std::string_view::npos) {
            char after = (pPos + 4 < sv.size()) ? sv[pPos + 4] : ' ';
            if (after != '>' && after != ' ' && after != '/') { pPos += 4; continue; }
            size_t pGt = sv.find('>', pPos);
            if (pGt == std::string_view::npos) break;
            if (sv[pGt - 1] == '/') { pPos = pGt + 1; continue; }
            size_t endP = sv.find("</a:p>", pGt);
            if (endP == std::string_view::npos) break;
            std::string_view pBlk = sv.substr(pGt + 1, endP - (pGt + 1));

            std::wstring paraText;
            size_t tPos = 0;
            while ((tPos = pBlk.find("<a:t", tPos)) != std::string_view::npos) {
                size_t tGt = pBlk.find('>', tPos);
                if (tGt == std::string_view::npos) break;
                if (pBlk[tGt - 1] == '/') { tPos = tGt + 1; continue; }
                size_t endT = pBlk.find("</a:t>", tGt);
                if (endT == std::string_view::npos) break;
                paraText += DecodeXmlEntities(PathUtils::Utf8ToWide(pBlk.substr(tGt + 1, endT - (tGt + 1))));
                tPos = endT + 6;
            }
            if (!paraText.empty()) {
                AddDocParagraph(out, std::move(paraText), firstOnSlide ? 2 : 0, !firstOnSlide);
                firstOnSlide = false;
            }
            pPos = endP + 6;
        }
    }

    if (out.paragraphs.empty()) {
        AddDocParagraph(out, L"(Pusta prezentacja PowerPoint / Empty PowerPoint presentation)", 0, false);
    }
    out.valid = true;
    return true;
}
