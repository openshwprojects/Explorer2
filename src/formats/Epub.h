#pragma once

static bool ParseEpubDocument(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::EpubBook;
    out.isSpreadsheet = false;
    out.formatTitle = L"EPUB eBook (.epub)";

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, entries)) return false;

    // 1. Extract metadata from .opf if present
    for (const auto& e : entries) {
        std::wstring lp = PathUtils::ToLower(e.pathInArchive);
        if (lp.size() > 4 && lp.substr(lp.size() - 4) == L".opf") {
            std::vector<uint8_t> opfBytes;
            if (ZipDeflate::ExtractZipEntryFromFile(fullPath, e, opfBytes) && !opfBytes.empty()) {
                std::string_view sv(reinterpret_cast<const char*>(opfBytes.data()), opfBytes.size());
                auto extractTag = [&](std::string_view tagOpen, std::string_view tagClose) -> std::wstring {
                    size_t p = sv.find(tagOpen);
                    if (p == std::string_view::npos) return {};
                    size_t gt = sv.find('>', p);
                    size_t end = sv.find(tagClose, gt);
                    if (gt == std::string_view::npos || end == std::string_view::npos) return {};
                    return StripXmlHtmlTags(sv.substr(gt + 1, end - (gt + 1)));
                };
                std::wstring title = extractTag("<dc:title", "</dc:title>");
                std::wstring creator = extractTag("<dc:creator", "</dc:creator>");
                if (!title.empty()) AddDocParagraph(out, title, 1, false);
                if (!creator.empty()) AddDocParagraph(out, L"Autor / Author: " + creator, 2, false);
            }
            break;
        }
    }

    // 2. Extract XHTML / HTML chapter bodies
    int chaptersLoaded = 0;
    for (const auto& e : entries) {
        if (e.isDirectory) continue;
        std::wstring lp = PathUtils::ToLower(e.pathInArchive);
        if ((lp.size() > 6 && lp.substr(lp.size() - 6) == L".xhtml") ||
            (lp.size() > 5 && lp.substr(lp.size() - 5) == L".html") ||
            (lp.size() > 4 && lp.substr(lp.size() - 4) == L".htm")) {
            std::vector<uint8_t> htmlBytes;
            if (!ZipDeflate::ExtractZipEntryFromFile(fullPath, e, htmlBytes) || htmlBytes.empty()) continue;
            std::string_view sv(reinterpret_cast<const char*>(htmlBytes.data()), htmlBytes.size());

            // Extract <h1..h3>, <p>, <li> blocks
            size_t pos = 0;
            while (pos < sv.size()) {
                size_t lt = sv.find('<', pos);
                if (lt == std::string_view::npos) break;
                if (lt + 3 >= sv.size()) break;
                std::string tag;
                size_t k = lt + 1;
                while (k < sv.size() && ((sv[k] >= 'a' && sv[k] <= 'z') || (sv[k] >= 'A' && sv[k] <= 'Z') || (sv[k] >= '0' && sv[k] <= '9'))) {
                    tag.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(sv[k]))));
                    ++k;
                }
                if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "p" || tag == "li") {
                    size_t gt = sv.find('>', lt);
                    if (gt == std::string_view::npos) break;
                    if (sv[gt - 1] == '/') { pos = gt + 1; continue; }
                    std::string closeTag = "</" + tag + ">";
                    size_t endTag = sv.find(closeTag, gt + 1);
                    if (endTag == std::string_view::npos) { pos = gt + 1; continue; }
                    int hLevel = (tag == "h1") ? 1 : ((tag == "h2") ? 2 : ((tag == "h3") ? 3 : 0));
                    bool bullet = (tag == "li");
                    AddDocParagraph(out, StripXmlHtmlTags(sv.substr(gt + 1, endTag - (gt + 1))), hLevel, bullet);
                    pos = endTag + closeTag.size();
                } else {
                    pos = lt + 1;
                }
            }
            if (++chaptersLoaded >= 40) break;
        }
    }

    out.valid = !out.paragraphs.empty();
    return out.valid;
}
