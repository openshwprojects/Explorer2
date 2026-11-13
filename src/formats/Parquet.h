#pragma once
// Parquet.h — Apache Parquet Columnar Storage (.parquet, PAR1) Inspector

namespace Formats {

inline bool ParseApacheParquet(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 12 || std::memcmp(raw.data(), "PAR1", 4) != 0 ||
        std::memcmp(raw.data() + raw.size() - 4, "PAR1", 4) != 0) {
        return false;
    }
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache Parquet Columnar (.parquet)";

    uint32_t footerLen = 0;
    std::memcpy(&footerLen, raw.data() + raw.size() - 8, 4);
    size_t footerStart = (footerLen + 8 <= raw.size()) ? (raw.size() - 8 - footerLen) : 4;

    SpreadsheetSheet sheet;
    sheet.name = L"Parquet_Schema";
    sheet.rows.push_back({ L"Idx", L"Field / Token", L"Offset", L"Segment" });

    // Extract printable ASCII identifiers from footer (column/schema names) and data page strings
    std::string cur;
    size_t startOff = 4;
    for (size_t i = 4; i < raw.size() - 8; ++i) {
        uint8_t b = raw[i];
        if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') ||
            b == '_' || b == '-' || b == '.' || b == ':' || b == '/') {
            if (cur.empty()) startOff = i;
            cur.push_back(static_cast<char>(b));
        } else {
            if (cur.size() >= 3 && cur.size() <= 64) {
                bool hasAlpha = false;
                for (char c : cur) if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { hasAlpha = true; break; }
                if (hasAlpha && sheet.rows.size() < 200) {
                    sheet.rows.push_back({
                        std::to_wstring(sheet.rows.size()),
                        PathUtils::Utf8ToWide(cur),
                        std::to_wstring(startOff),
                        (startOff >= footerStart) ? L"Thrift Footer Schema" : L"Column Data Page"
                    });
                }
            }
            cur.clear();
        }
    }
    if (sheet.rows.size() == 1) {
        sheet.rows.push_back({ L"1", L"PAR1 Columnar Footer", std::to_wstring(footerStart), std::to_wstring(footerLen) + L" bytes" });
    }
    sheet.maxCols = 4;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}

} // namespace Formats
