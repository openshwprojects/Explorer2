#pragma once
// Avro.h — Apache Avro Object Container (.avro, Obj\x01) Inspector

namespace Formats {

inline bool ParseApacheAvro(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "Obj\x01", 4) != 0) return false;
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apache Avro Container (.avro)";

    SpreadsheetSheet sheet;
    sheet.name = L"Avro_Schema_Records";
    sheet.rows.push_back({ L"#", L"Key / Field", L"Type / Value" });

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t schemaPos = sv.find("avro.schema");
    if (schemaPos != std::string_view::npos) {
        size_t braceOpen = sv.find('{', schemaPos);
        if (braceOpen != std::string_view::npos) {
            int depth = 0;
            size_t braceClose = braceOpen;
            for (; braceClose < sv.size(); ++braceClose) {
                if (sv[braceClose] == '{') ++depth;
                else if (sv[braceClose] == '}') {
                    if (--depth == 0) break;
                }
            }
            std::string_view schemaJson = sv.substr(braceOpen, std::min<size_t>(braceClose - braceOpen + 1, 8192));
            size_t fPos = 0;
            int idx = 1;
            while ((fPos = schemaJson.find("\"name\"", fPos)) != std::string_view::npos) {
                size_t q1 = schemaJson.find('"', fPos + 6);
                size_t q2 = (q1 != std::string_view::npos) ? schemaJson.find('"', q1 + 1) : std::string_view::npos;
                if (q1 == std::string_view::npos || q2 == std::string_view::npos) break;
                std::string_view fieldName = schemaJson.substr(q1 + 1, q2 - q1 - 1);
                std::string_view fieldType = "record";
                size_t tPos = schemaJson.find("\"type\"", q2);
                if (tPos != std::string_view::npos && tPos < q2 + 64) {
                    size_t tq1 = schemaJson.find('"', tPos + 6);
                    size_t tq2 = (tq1 != std::string_view::npos) ? schemaJson.find('"', tq1 + 1) : std::string_view::npos;
                    if (tq1 != std::string_view::npos && tq2 != std::string_view::npos) {
                        fieldType = schemaJson.substr(tq1 + 1, tq2 - tq1 - 1);
                    }
                }
                sheet.rows.push_back({ std::to_wstring(idx++), PathUtils::Utf8ToWide(fieldName), PathUtils::Utf8ToWide(fieldType) });
                fPos = q2 + 1;
            }
        }
    }
    if (sheet.rows.size() == 1) {
        sheet.rows.push_back({ L"1", L"avro.codec", L"null" });
    }
    sheet.maxCols = 3;
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}

} // namespace Formats
