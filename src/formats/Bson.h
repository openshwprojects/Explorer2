#pragma once
// Bson.h — Binary JSON Document (.bson) Inspector

namespace Formats {

inline bool ParseBsonDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 5) return false;
    uint32_t docLen = 0;
    std::memcpy(&docLen, raw.data(), 4);
    if (docLen < 5 || docLen > raw.size() || raw[docLen - 1] != 0) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Binary JSON Document (.bson)";

    SpreadsheetSheet sheet;
    sheet.name = L"BSON_Fields";
    sheet.rows.push_back({ L"Key", L"BSON Type", L"Value" });

    size_t pos = 4;
    while (pos + 1 < docLen) {
        uint8_t etype = raw[pos++];
        if (etype == 0) break;
        std::string key;
        while (pos < docLen && raw[pos] != 0) key.push_back(static_cast<char>(raw[pos++]));
        if (pos < docLen) ++pos; // skip nul

        std::wstring typeName = L"unknown";
        std::wstring valStr;
        if (etype == 0x01 && pos + 8 <= docLen) { // double
            double d = 0.0; std::memcpy(&d, raw.data() + pos, 8); pos += 8;
            typeName = L"double";
            wchar_t b[64] = {}; swprintf_s(b, L"%.6g", d); valStr = b;
        } else if (etype == 0x02 && pos + 4 <= docLen) { // UTF-8 string
            uint32_t slen = 0; std::memcpy(&slen, raw.data() + pos, 4); pos += 4;
            if (slen == 0 || pos + slen > docLen) break;
            typeName = L"string";
            valStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), slen - 1));
            pos += slen;
        } else if ((etype == 0x03 || etype == 0x04) && pos + 4 <= docLen) { // embedded doc or array
            uint32_t subLen = 0; std::memcpy(&subLen, raw.data() + pos, 4);
            if (subLen < 5 || pos + subLen > docLen) break;
            typeName = (etype == 0x03) ? L"object" : L"array";
            valStr = L"(" + std::to_wstring(subLen) + L" bytes)";
            pos += subLen;
        } else if (etype == 0x08 && pos + 1 <= docLen) { // bool
            typeName = L"bool";
            valStr = raw[pos++] ? L"true" : L"false";
        } else if (etype == 0x0A) { // null
            typeName = L"null"; valStr = L"null";
        } else if (etype == 0x10 && pos + 4 <= docLen) { // int32
            int32_t v = 0; std::memcpy(&v, raw.data() + pos, 4); pos += 4;
            typeName = L"int32"; valStr = std::to_wstring(v);
        } else if ((etype == 0x12 || etype == 0x09) && pos + 8 <= docLen) { // int64 / datetime
            int64_t v = 0; std::memcpy(&v, raw.data() + pos, 8); pos += 8;
            typeName = (etype == 0x09) ? L"datetime" : L"int64";
            valStr = std::to_wstring(v);
        } else {
            break;
        }
        sheet.rows.push_back({ PathUtils::Utf8ToWide(key), typeName, valStr });
    }

    sheet.maxCols = 3;
    out.valid = (sheet.rows.size() >= 2);
    out.sheets.push_back(std::move(sheet));
    return out.valid;
}

} // namespace Formats
