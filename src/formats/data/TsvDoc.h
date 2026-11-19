#pragma once
// TsvDoc.h — Tab-Separated Values (.tsv) Reader

namespace Formats {

inline bool ParseTsvDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::CsvTable;
    out.isSpreadsheet = true;

    std::wstring text;
    wchar_t delim = L'\t';
    bool delimFromHint = false;
    DetailCsvTsv::DecodeDelimitedTextAndSepHint(raw, L'\t', text, delim, delimFromHint);

    out.formatTitle = DetailCsvTsv::FormatDelimitedTitle(delim);
    return DetailCsvTsv::ParseDelimitedRowsToSheet(text, delim, L"TSV", out);
}

} // namespace Formats
