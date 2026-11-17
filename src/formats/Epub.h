#pragma once
// Epub.h — EPUB 2 / 3 e-books (.epub): META-INF/container.xml names the package document (OPF); its
// metadata (title, creators, language, publisher, date, identifier, subjects, description) is shown first and
// the chapters follow in the OPF spine's reading order (not the ZIP's file order). Chapter XHTML is read as
// blocks (headings, paragraphs, list items, preformatted text, definition terms) with entities decoded.

namespace DetailEpub {

namespace X = ::Explorer2::XmlTagScan;

// Joins an OPF-relative href to the OPF's folder, resolving "." / ".." and %XX escapes
inline std::wstring ResolveHref(const std::wstring& opfDir, std::string href) {
    size_t hash = href.find('#');
    if (hash != std::string::npos) href.resize(hash);
    std::string dec;
    for (size_t i = 0; i < href.size(); ++i) {
        if (href[i] == '%' && i + 2 < href.size() && std::isxdigit(static_cast<unsigned char>(href[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(href[i + 2]))) {
            dec.push_back(static_cast<char>(std::stoi(href.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            dec.push_back(href[i]);
        }
    }
    std::wstring joined = opfDir.empty() ? PathUtils::Utf8ToWide(dec) : opfDir + L"/" + PathUtils::Utf8ToWide(dec);
    std::vector<std::wstring> parts;
    size_t s = 0;
    while (s <= joined.size()) {
        size_t e = joined.find_first_of(L"/\\", s);
        if (e == std::wstring::npos) e = joined.size();
        std::wstring seg = joined.substr(s, e - s);
        if (seg == L"..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != L".") parts.push_back(seg);
        s = e + 1;
    }
    std::wstring out;
    for (const auto& p : parts) out += (out.empty() ? L"" : L"/") + p;
    return out;
}

inline const ZipDeflate::ArchiveItemMeta* FindEntry(const std::vector<ZipDeflate::ArchiveItemMeta>& entries, const std::wstring& path) {
    for (const auto& e : entries) {
        if (_wcsicmp(e.pathInArchive.c_str(), path.c_str()) == 0) return &e;
    }
    return nullptr;
}

inline bool IsBlockTag(std::string_view n) {
    return n == "p" || n == "li" || n == "pre" || n == "dt" || n == "dd" || n == "figcaption" ||
           (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6');
}

// Block-level text of one XHTML chapter; a block's nested blocks are part of its text (containers such
// as <blockquote> / <div> are descended into, so their paragraphs stay separate)
inline void AppendChapterBlocks(std::string_view xhtml, ParsedOfficeDocument& out, size_t maxParagraphs) {
    X::Element body;
    std::string_view sv = X::FindChild(xhtml, "body", body, false) ? body.body : xhtml;
    size_t pos = 0;
    X::XmlTag t;
    while (X::NextTag(sv, pos, t) && out.paragraphs.size() < maxParagraphs) {
        if (t.isClosing || t.isSelfClosing || !IsBlockTag(t.localName)) continue;
        std::string_view inner;
        size_t after = 0;
        if (!X::FindElementBody(sv, t.afterTag, t.localName, inner, after)) continue;
        std::string text = X::DecodeText(inner);
        for (char& c : text) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        int level = 0;
        if (t.localName.size() == 2 && t.localName[0] == 'h') level = std::min(3, t.localName[1] - '0');
        AddDocParagraph(out, PathUtils::Utf8ToWide(text), level, t.localName == "li" || t.localName == "dd");
        pos = after;
    }
}

} // namespace DetailEpub

static bool ParseEpubDocument(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    using namespace DetailEpub;
    out.kind = OfficeFileKind::EpubBook;
    out.isSpreadsheet = false;
    out.formatTitle = L"EPUB eBook (.epub)";

    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, entries)) return false;
    auto readEntry = [&](const ZipDeflate::ArchiveItemMeta* e, std::string& text) {
        std::vector<uint8_t> bytes;
        if (!e || !ZipDeflate::ExtractZipEntryFromFile(fullPath, *e, bytes, 32 * 1024 * 1024) || bytes.empty()) return false;
        text = X::DocumentToUtf8(bytes.data(), bytes.size());
        return true;
    };

    // 1. container.xml -> package document; fall back to the first .opf in the archive
    std::wstring opfPath;
    std::string container;
    if (readEntry(FindEntry(entries, L"META-INF/container.xml"), container)) {
        X::Element rf;
        if (X::FindChild(container, "rootfile", rf, false)) opfPath = PathUtils::Utf8ToWide(X::AttrText(rf.attrs, "full-path"));
    }
    if (opfPath.empty()) {
        for (const auto& e : entries) {
            if (PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive)) == L".opf") { opfPath = e.pathInArchive; break; }
        }
    }
    std::string opf;
    if (opfPath.empty() || !readEntry(FindEntry(entries, opfPath), opf)) return false;
    size_t slash = opfPath.find_last_of(L'/');
    std::wstring opfDir = (slash == std::wstring::npos) ? L"" : opfPath.substr(0, slash);

    // 2. Metadata
    X::Element meta;
    if (X::FindChild(opf, "metadata", meta, false)) {
        std::string title = X::ChildText(meta.body, "title", false);
        if (!title.empty()) AddDocParagraph(out, PathUtils::Utf8ToWide(title), 1, false);
        std::string creators;
        X::ForEachElement(meta.body, "creator", [&](const X::Element& c) {
            std::string n = std::string(X::TrimView(X::DecodeText(c.body)));
            if (!n.empty()) creators += (creators.empty() ? "" : ", ") + n;
            return true;
        });
        if (!creators.empty()) AddDocParagraph(out, L"Autor / Author: " + PathUtils::Utf8ToWide(creators), 2, false);
        std::string info;
        for (const char* key : { "language", "publisher", "date", "identifier" }) {
            std::string v = X::ChildText(meta.body, key, false);
            if (!v.empty()) info += (info.empty() ? "" : " | ") + std::string(key) + ": " + v;
        }
        std::string subjects;
        X::ForEachElement(meta.body, "subject", [&](const X::Element& c) {
            std::string n = std::string(X::TrimView(X::DecodeText(c.body)));
            if (!n.empty()) subjects += (subjects.empty() ? "" : ", ") + n;
            return true;
        });
        if (!subjects.empty()) info += (info.empty() ? "" : " | ") + std::string("subject: ") + subjects;
        if (!info.empty()) AddDocParagraph(out, PathUtils::Utf8ToWide(info), 3, false);
        std::string desc = X::ChildText(meta.body, "description", false);
        if (!desc.empty()) AddDocParagraph(out, PathUtils::Utf8ToWide(X::DecodeText(desc)), 0, false);
    }

    // 3. Manifest id -> href, spine reading order
    std::map<std::string, std::string> manifest;
    X::Element man;
    if (X::FindChild(opf, "manifest", man, false)) {
        X::ForEachElement(man.body, "item", [&](const X::Element& it) {
            manifest[X::AttrText(it.attrs, "id")] = X::AttrText(it.attrs, "href");
            return true;
        });
    }
    std::vector<std::wstring> chapters;
    X::Element spine;
    if (X::FindChild(opf, "spine", spine, false)) {
        X::ForEachElement(spine.body, "itemref", [&](const X::Element& ir) {
            auto it = manifest.find(X::AttrText(ir.attrs, "idref"));
            if (it != manifest.end() && !it->second.empty()) chapters.push_back(ResolveHref(opfDir, it->second));
            return true;
        });
    }
    if (chapters.empty()) {
        // No usable spine: (X)HTML files in archive order, as before
        for (const auto& e : entries) {
            std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive));
            if (!e.isDirectory && (ext == L".xhtml" || ext == L".html" || ext == L".htm")) chapters.push_back(e.pathInArchive);
        }
    }

    // 4. Chapters in reading order
    size_t read = 0;
    for (const auto& ch : chapters) {
        if (out.paragraphs.size() >= 20000 || read >= 400) break;
        std::string xhtml;
        if (!readEntry(FindEntry(entries, ch), xhtml)) continue;
        AppendChapterBlocks(xhtml, out, 20000);
        ++read;
    }
    out.valid = !out.paragraphs.empty();
    return out.valid;
}
