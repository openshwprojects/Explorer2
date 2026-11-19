#pragma once
// AppleIwa.h — Apple iWork '13+ Snappy-Framed Protobuf Archive Stream (.iwa) Reader

namespace Formats {

inline bool ParseAppleIwaDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> decomp;
    if (!DetailDocs200::UnframeIwaStream(raw, decomp)) return false;

    SpreadsheetSheet sh;
    sh.name = L"Package_And_Text";
    sh.maxCols = 3;
    sh.rows.push_back({ L"Archive Stream", L"Object / Message Type", L"Extracted Content" });

    SpreadsheetSheet cellsSh;
    cellsSh.name = L"Numbers_Table_Data";
    cellsSh.maxCols = 3;
    cellsSh.rows.push_back({ L"Table / Stream", L"Entry Key", L"Cell Value" });

    size_t validIwaFiles = 0;
    size_t validIwaObjects = 0;
    if (!DetailDocs200::ParseIwaProtobufStream(decomp, L"Document.iwa", sh, cellsSh, validIwaFiles, validIwaObjects) || validIwaObjects == 0) {
        return false;
    }

    sh.rows.insert(sh.rows.begin() + 1, {
        L"Stream Summary",
        L"iWork '13+ .iwa Stream",
        std::to_wstring(validIwaObjects) + L" protobuf objects (" + PathUtils::FormatByteSize(decomp.size()) + L" unframed)"
    });

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apple iWork Archive Stream (.iwa)";
    out.sheets.push_back(std::move(sh));
    if (cellsSh.rows.size() > 1) {
        out.sheets.push_back(std::move(cellsSh));
    }
    out.valid = true;
    return true;
}

} // namespace Formats
