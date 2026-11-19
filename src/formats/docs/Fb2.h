#pragma once
// Fb2.h — FictionBook 2 e-books (.fb2): the declared encoding is honored (windows-1251 / koi8-r books read
// correctly), title-info gives title, authors, genres, language, series and annotation, and every <body>
// (main text, then notes) is walked structurally: section titles as headings, paragraphs, subtitles,
// poems verse by verse, epigraphs, citations and text authors.

namespace DetailFb2 {

namespace X = ::Explorer2::XmlTagScan;

inline std::wstring Text(std::string_view body) {
    std::string t = X::DecodeText(body);
    for (char& c : t) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    return PathUtils::Utf8ToWide(t);
}

inline std::string PersonName(std::string_view body) {
    std::string n;
    for (const char* part : { "first-name", "middle-name", "last-name" }) {
        std::string v = X::ChildText(body, part);
        if (!v.empty()) n += (n.empty() ? "" : " ") + v;
    }
    if (n.empty()) n = X::ChildText(body, "nickname");
    return n;
}

inline void WalkFlow(std::string_view body, ParsedOfficeDocument& out, int sectionDepth, int depth) {
    if (depth > 24 || out.paragraphs.size() >= 20000) return;
    X::ForEachChild(body, [&](std::string_view name, const X::Element& e) {
        if (out.paragraphs.size() >= 20000) return false;
        if (name == "section") {
            WalkFlow(e.body, out, sectionDepth + 1, depth + 1);
        } else if (name == "title") {
            // A title holds one or more <p>; each becomes a heading line
            int level = std::min(3, std::max(1, sectionDepth + 1));
            bool any = false;
            X::ForEachElement(e.body, "p", [&](const X::Element& p) {
                AddDocParagraph(out, Text(p.body), level, false);
                any = true;
                return true;
            });
            if (!any) AddDocParagraph(out, Text(e.body), level, false);
        } else if (name == "p" || name == "v" || name == "text-author") {
            AddDocParagraph(out, (name == "text-author" ? L"— " : L"") + Text(e.body), 0, false);
        } else if (name == "subtitle") {
            AddDocParagraph(out, Text(e.body), 3, false);
        } else if (name == "poem" || name == "stanza" || name == "epigraph" || name == "cite") {
            WalkFlow(e.body, out, sectionDepth, depth + 1);
        } else if (name == "image") {
            AddDocParagraph(out, L"[image]", 0, false);
        } else if (name == "table") {
            X::ForEachElement(e.body, "tr", [&](const X::Element& tr) {
                std::wstring row;
                X::ForEachChild(tr.body, [&](std::string_view, const X::Element& cell) {
                    row += (row.empty() ? L"" : L" | ") + Text(cell.body);
                    return true;
                });
                AddDocParagraph(out, row, 0, true);
                return true;
            });
        }
        return true;
    });
}

} // namespace DetailFb2

static bool ParseFb2Document(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    using namespace DetailFb2;
    out.kind = OfficeFileKind::EpubBook;
    out.isSpreadsheet = false;
    out.formatTitle = L"FictionBook 2 eBook (.fb2)";

    std::string doc = X::DocumentToUtf8(raw.data(), raw.size());
    X::Element fb;
    if (!X::FindChild(doc, "FictionBook", fb, false) || fb.selfClosing) return false;

    X::Element desc, ti;
    if (X::FindChild(fb.body, "description", desc) && X::FindChild(desc.body, "title-info", ti)) {
        std::string title = X::ChildText(ti.body, "book-title");
        if (!title.empty()) AddDocParagraph(out, PathUtils::Utf8ToWide(title), 1, false);
        std::string authors;
        X::ForEachChild(ti.body, [&](std::string_view name, const X::Element& e) {
            if (name == "author") {
                std::string n = PersonName(e.body);
                if (!n.empty()) authors += (authors.empty() ? "" : ", ") + n;
            }
            return true;
        });
        if (!authors.empty()) AddDocParagraph(out, L"Autor / Author: " + PathUtils::Utf8ToWide(authors), 2, false);
        std::string info;
        std::string genres;
        X::ForEachChild(ti.body, [&](std::string_view name, const X::Element& e) {
            if (name == "genre") genres += (genres.empty() ? "" : ", ") + std::string(X::TrimView(X::DecodeText(e.body)));
            return true;
        });
        if (!genres.empty()) info += "genre: " + genres;
        std::string lang = X::ChildText(ti.body, "lang");
        if (!lang.empty()) info += (info.empty() ? "" : " | ") + std::string("lang: ") + lang;
        X::Element seq;
        if (X::FindChild(ti.body, "sequence", seq)) {
            std::string sname = X::AttrText(seq.attrs, "name");
            std::string snum = X::AttrText(seq.attrs, "number");
            if (!sname.empty()) info += (info.empty() ? "" : " | ") + std::string("series: ") + sname + (snum.empty() ? "" : " #" + snum);
        }
        if (!info.empty()) AddDocParagraph(out, PathUtils::Utf8ToWide(info), 3, false);
        X::Element ann;
        if (X::FindChild(ti.body, "annotation", ann)) WalkFlow(ann.body, out, 2, 0);
    }

    // Main body first, then named bodies (notes, comments)
    std::vector<X::Element> bodies;
    X::ForEachChild(fb.body, [&](std::string_view name, const X::Element& e) {
        if (name == "body") bodies.push_back(e);
        return true;
    });
    for (const auto& b : bodies) {
        std::string bname = X::AttrText(b.attrs, "name");
        if (!bname.empty()) AddDocParagraph(out, PathUtils::Utf8ToWide(bname), 1, false);
        WalkFlow(b.body, out, 0, 0);
    }
    out.valid = !out.paragraphs.empty();
    return out.valid;
}
