#pragma once
// BioWareTableCommon.h — Shared DRY helpers for BioWare Aurora / Odyssey / Electron formats
// Used by BioWare2daTable.h (.2da), BioWareTlkTable.h (.tlk), and BioWareGffStruct.h (.gff).

namespace Formats::BioWareCommon {

inline const wchar_t* LanguageIdName(uint32_t langId) {
    switch (langId) {
    case 0u:   return L"English";
    case 1u:   return L"French";
    case 2u:   return L"German";
    case 3u:   return L"Italian";
    case 4u:   return L"Spanish";
    case 5u:   return L"Polish";
    case 128u: return L"Korean";
    case 129u: return L"Chinese (Trad)";
    case 130u: return L"Chinese (Simp)";
    case 131u: return L"Japanese";
    default:   return L"Custom";
    }
}

inline std::wstring DecodeBioWareString(std::string_view sv) {
    if (sv.empty()) return {};
    std::wstring w = PathUtils::Utf8ToWide(sv);
    if (!w.empty()) return w;
    w.reserve(sv.size());
    for (unsigned char c : sv) {
        if (c >= 0x20u || c == '\t' || c == '\n') {
            w.push_back(static_cast<wchar_t>(c));
        }
    }
    return w;
}

inline bool EmitSpreadsheetDocument(
    ParsedOfficeDocument& outDoc,
    std::wstring title,
    SpreadsheetSheet&& sheet)
{
    if (sheet.rows.empty()) return false;
    FinalizeSheetCols(sheet);
    outDoc.formatTitle = std::move(title);
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.sheets.clear();
    outDoc.paragraphs.clear();
    outDoc.sheets.push_back(std::move(sheet));
    outDoc.valid = true;
    return true;
}

} // namespace Formats::BioWareCommon
