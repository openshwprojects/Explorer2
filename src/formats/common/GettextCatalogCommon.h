#pragma once
// GettextCatalogCommon.h — Shared DRY Localization Catalog Helpers for GNU Gettext (.mo & .po/.pot)

namespace Formats::GettextCommon {

struct CatalogEntry {
    std::wstring context;
    std::wstring msgid;
    std::wstring msgstr;
    std::wstring notes;
};

struct CatalogHeaderMeta {
    std::wstring project;
    std::wstring language;
    std::wstring pluralForms;
};

inline void ParseHeaderMetadataBlock(std::string_view rawHdr, CatalogHeaderMeta& outMeta) {
    size_t pos = 0u;
    std::string_view line;
    while (ReaderText::NextLineSv(rawHdr, pos, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string key(ReaderText::TrimSv(line.substr(0, colon)));
        for (char& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        std::string_view val = ReaderText::TrimSv(line.substr(colon + 1u));
        if (key == "project-id-version") outMeta.project = PathUtils::Utf8ToWide(val);
        else if (key == "language")      outMeta.language = PathUtils::Utf8ToWide(val);
        else if (key == "plural-forms")  outMeta.pluralForms = PathUtils::Utf8ToWide(val);
    }
}

inline std::wstring JoinNulSeparatedForms(std::string_view blob) {
    std::wstring out;
    size_t pos = 0u;
    size_t idx = 0u;
    const bool hasMultiple = (blob.find('\0') != std::string_view::npos);
    while (pos <= blob.size()) {
        const size_t nul = blob.find('\0', pos);
        std::string_view part = (nul == std::string_view::npos) ? blob.substr(pos) : blob.substr(pos, nul - pos);
        if (hasMultiple) {
            if (!out.empty()) out += L" | ";
            out += L"[" + std::to_wstring(idx++) + L"] " + PathUtils::Utf8ToWide(part);
        } else {
            out = PathUtils::Utf8ToWide(part);
        }
        if (nul == std::string_view::npos) break;
        pos = nul + 1u;
    }
    return out;
}

inline bool EmitCatalogDocument(
    ParsedOfficeDocument& outDoc,
    const wchar_t* formatPrefix,
    const CatalogHeaderMeta& meta,
    const std::vector<CatalogEntry>& entries)
{
    if (entries.empty() && meta.project.empty() && meta.language.empty()) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"Translations";
    sheet.rows.push_back({ L"#", L"Context", L"Source (msgid)", L"Translation (msgstr)", L"Flags / Notes" });

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        sheet.rows.push_back({
            std::to_wstring(i + 1u),
            e.context,
            e.msgid,
            e.msgstr,
            e.notes
        });
    }
    FinalizeSheetCols(sheet);

    std::wstring title = formatPrefix;
    if (!meta.language.empty()) title += L" [" + meta.language + L"]";
    if (!meta.project.empty())  title += L" — " + meta.project;
    title += L" (" + std::to_wstring(entries.size()) + L" messages)";

    outDoc.formatTitle = std::move(title);
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.sheets.clear();
    outDoc.paragraphs.clear();
    outDoc.sheets.push_back(std::move(sheet));
    outDoc.valid = true;
    return true;
}

} // namespace Formats::GettextCommon
