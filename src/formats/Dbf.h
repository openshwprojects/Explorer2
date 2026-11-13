#pragma once

static bool ParseDbfDatabase(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 64) return false;
    uint32_t numRecords = static_cast<uint32_t>(raw[4]) |
                          (static_cast<uint32_t>(raw[5]) << 8) |
                          (static_cast<uint32_t>(raw[6]) << 16) |
                          (static_cast<uint32_t>(raw[7]) << 24);
    uint16_t headerBytes = static_cast<uint16_t>(raw[8]) | (static_cast<uint16_t>(raw[9]) << 8);
    uint16_t recordBytes = static_cast<uint16_t>(raw[10]) | (static_cast<uint16_t>(raw[11]) << 8);
    if (headerBytes < 33 || headerBytes > raw.size() || recordBytes < 2) return false;

    struct DbfField {
        std::wstring name;
        char type = 'C';
        uint8_t length = 0;
    };
    std::vector<DbfField> fields;
    size_t pos = 32;
    while (pos + 32 <= headerBytes && raw[pos] != 0x0D) {
        char nameBuf[12] = {};
        std::memcpy(nameBuf, &raw[pos], 11);
        DbfField f;
        f.name = PathUtils::Utf8ToWide(nameBuf);
        f.type = static_cast<char>(raw[pos + 11]);
        f.length = raw[pos + 16];
        if (!f.name.empty() && f.length > 0) {
            fields.push_back(std::move(f));
        }
        pos += 32;
    }
    if (fields.empty()) return false;

    out.kind = OfficeFileKind::DbfTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"dBASE / FoxPro Database Table (.dbf)";

    SpreadsheetSheet sheet;
    sheet.name = L"DBF_Table";
    sheet.maxCols = fields.size();

    std::vector<std::wstring> hdrRow;
    hdrRow.reserve(fields.size());
    for (const auto& f : fields) hdrRow.push_back(f.name);
    sheet.rows.push_back(std::move(hdrRow));

    size_t recOffset = headerBytes;
    uint32_t maxRecs = std::min<uint32_t>(numRecords, 10000);
    for (uint32_t r = 0; r < maxRecs && recOffset + recordBytes <= raw.size(); ++r, recOffset += recordBytes) {
        uint8_t delFlag = raw[recOffset];
        if (delFlag == '*') continue; // Deleted record
        size_t fOff = recOffset + 1;
        std::vector<std::wstring> row;
        row.reserve(fields.size());
        for (const auto& f : fields) {
            if (fOff + f.length > recOffset + recordBytes) break;
            std::string_view fv(reinterpret_cast<const char*>(&raw[fOff]), f.length);
            while (!fv.empty() && fv.front() == ' ') fv.remove_prefix(1);
            while (!fv.empty() && fv.back() == ' ') fv.remove_suffix(1);
            row.push_back(PathUtils::Utf8ToWide(fv));
            fOff += f.length;
        }
        sheet.rows.push_back(std::move(row));
    }

    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}
