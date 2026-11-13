#pragma once
// WebAssemblyWasm.h — W3C WebAssembly Binary Module (.wasm) Section & Export Table Inspector

namespace Formats {

inline bool ParseWebAssemblyWasmDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8 || raw[0] != 0x00 || raw[1] != 'a' || raw[2] != 's' || raw[3] != 'm') return false;

    uint32_t version = 0;
    std::memcpy(&version, raw.data() + 4, 4);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"W3C WebAssembly Binary Module (.wasm)";

    SpreadsheetSheet sheet;
    sheet.name = L"WASM_Sections";
    sheet.rows.push_back({ L"Section #", L"Section ID", L"Section Name", L"Offset", L"Payload Size (Bytes)" });
    sheet.rows.push_back({ L"Header", L"0x0061736D", L"\\0asm Magic", L"0x0000", L"Version " + std::to_wstring(version) });

    static const wchar_t* const kSecNames[13] = {
        L"Custom (0)", L"Type (1)", L"Import (2)", L"Function (3)",
        L"Table (4)", L"Memory (5)", L"Global (6)", L"Export (7)",
        L"Start (8)", L"Element (9)", L"Code (10)", L"Data (11)", L"DataCount (12)"
    };

    size_t pos = 8;
    int secIdx = 1;
    while (pos < raw.size() && secIdx <= 256) {
        uint8_t id = raw[pos++];
        uint32_t payloadLen = 0;
        int shift = 0;
        while (pos < raw.size() && shift < 35) {
            uint8_t b = raw[pos++];
            payloadLen |= static_cast<uint32_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        wchar_t offBuf[32] = {};
        swprintf_s(offBuf, L"0x%06zX", pos);
        const wchar_t* sname = (id < 13) ? kSecNames[id] : L"Unknown Section";
        sheet.rows.push_back({
            std::to_wstring(secIdx++),
            std::to_wstring(id),
            sname,
            offBuf,
            std::to_wstring(payloadLen) + L" B"
        });
        if (pos + payloadLen > raw.size()) break;
        pos += payloadLen;
    }

    sheet.maxCols = 5;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
