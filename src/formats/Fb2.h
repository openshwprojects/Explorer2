#pragma once

static bool ParseFb2Document(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::EpubBook;
    out.isSpreadsheet = false;
    out.formatTitle = L"FictionBook 2 eBook (.fb2)";

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    auto extractTag = [&](std::string_view tagOpen, std::string_view tagClose) -> std::wstring {
        size_t p = sv.find(tagOpen);
        if (p == std::string_view::npos) return {};
        size_t gt = sv.find('>', p);
        size_t end = sv.find(tagClose, gt);
        if (gt == std::string_view::npos || end == std::string_view::npos) return {};
        return StripXmlHtmlTags(sv.substr(gt + 1, end - (gt + 1)));
    };

    std::wstring bookTitle = extractTag("<book-title", "</book-title>");
    if (!bookTitle.empty()) AddDocParagraph(out, bookTitle, 1, false);

    std::wstring firstName = extractTag("<first-name", "</first-name>");
    std::wstring lastName  = extractTag("<last-name", "</last-name>");
    if (!firstName.empty() || !lastName.empty()) {
        AddDocParagraph(out, L"Autor / Author: " + firstName + (firstName.empty() ? L"" : L" ") + lastName, 2, false);
    }

    size_t pos = sv.find("<body");
    if (pos == std::string_view::npos) pos = 0;
    while (pos < sv.size() && out.paragraphs.size() < 5000) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos || lt + 2 >= sv.size()) break;
        if (sv.substr(lt, 6) == "<title") {
            size_t gt = sv.find('>', lt);
            size_t end = sv.find("</title>", gt);
            if (gt != std::string_view::npos && end != std::string_view::npos) {
                AddDocParagraph(out, StripXmlHtmlTags(sv.substr(gt + 1, end - (gt + 1))), 2, false);
                pos = end + 8;
                continue;
            }
        } else if (sv.substr(lt, 2) == "<p" && (sv[lt + 2] == '>' || sv[lt + 2] == ' ')) {
            size_t gt = sv.find('>', lt);
            size_t end = sv.find("</p>", gt);
            if (gt != std::string_view::npos && end != std::string_view::npos) {
                AddDocParagraph(out, StripXmlHtmlTags(sv.substr(gt + 1, end - (gt + 1))), 0, false);
                pos = end + 4;
                continue;
            }
        }
        pos = lt + 1;
    }

    out.valid = !out.paragraphs.empty();
    return out.valid;
}
