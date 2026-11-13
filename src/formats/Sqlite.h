#pragma once

static bool ParseSqliteDatabase(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) {
        return false;
    }
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SQLite 3 Database (.sqlite / .db)";

    auto readBE16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>((static_cast<uint16_t>(raw[off]) << 8) | raw[off + 1]);
    };
    auto readBE32 = [&](size_t off) -> uint32_t {
        return (static_cast<uint32_t>(raw[off]) << 24) |
               (static_cast<uint32_t>(raw[off + 1]) << 16) |
               (static_cast<uint32_t>(raw[off + 2]) << 8) |
               static_cast<uint32_t>(raw[off + 3]);
    };

    uint32_t pageSize = readBE16(16);
    if (pageSize == 1) pageSize = 65536;
    uint32_t pageCount = readBE32(28);
    if (pageCount == 0 && pageSize > 0) pageCount = static_cast<uint32_t>(raw.size() / pageSize);
    uint32_t schemaCookie = readBE32(40);
    uint32_t textEnc = readBE32(56);
    uint32_t sqliteVer = readBE32(96);

    SpreadsheetSheet schemaSheet;
    schemaSheet.name = L"SQLite_Schema";
    schemaSheet.maxCols = 3;
    schemaSheet.rows.push_back({ L"Object / Metric", L"Type", L"SQL DDL / Value" });
    schemaSheet.rows.push_back({ L"Page Size (Bytes)", L"Header", std::to_wstring(pageSize) });
    schemaSheet.rows.push_back({ L"Total Pages", L"Header", std::to_wstring(pageCount) });
    schemaSheet.rows.push_back({ L"Text Encoding", L"Header", textEnc == 2 ? L"UTF-16LE" : (textEnc == 3 ? L"UTF-16BE" : L"UTF-8") });
    schemaSheet.rows.push_back({ L"Schema Cookie", L"Header", std::to_wstring(schemaCookie) });
    if (sqliteVer > 0) {
        wchar_t vBuf[64] = {};
        swprintf_s(vBuf, L"%u.%u.%u", sqliteVer / 1000000, (sqliteVer / 1000) % 1000, sqliteVer % 1000);
        schemaSheet.rows.push_back({ L"SQLite Version", L"Header", vBuf });
    }

    // Scan for CREATE TABLE / CREATE INDEX / CREATE VIEW / CREATE TRIGGER statements
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t pos = 0;
    while ((pos = sv.find("CREATE ", pos)) != std::string_view::npos) {
        size_t endPos = pos;
        int parenDepth = 0;
        bool seenParen = false;
        while (endPos < sv.size() && endPos - pos < 4096) {
            char c = sv[endPos];
            if (c < 9 || (c > 13 && c < 32) || static_cast<unsigned char>(c) >= 127) break;
            if (c == '(') { ++parenDepth; seenParen = true; }
            else if (c == ')') {
                --parenDepth;
                if (seenParen && parenDepth <= 0) { ++endPos; break; }
            }
            ++endPos;
        }
        std::string ddl(sv.substr(pos, endPos - pos));
        for (char& ch : ddl) { if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' '; }
        if (ddl.size() > 14) {
            std::wstring wDdl = PathUtils::Utf8ToWide(ddl);
            std::wstring objType = L"SQL";
            if (wDdl.rfind(L"CREATE TABLE", 0) == 0 || wDdl.rfind(L"CREATE TEMP TABLE", 0) == 0) objType = L"TABLE";
            else if (wDdl.rfind(L"CREATE INDEX", 0) == 0 || wDdl.rfind(L"CREATE UNIQUE INDEX", 0) == 0) objType = L"INDEX";
            else if (wDdl.rfind(L"CREATE VIEW", 0) == 0) objType = L"VIEW";
            else if (wDdl.rfind(L"CREATE TRIGGER", 0) == 0) objType = L"TRIGGER";

            // Extract identifier before '('
            std::wstring objName = objType;
            size_t openP = wDdl.find(L'(');
            if (openP != std::wstring::npos && openP > 13) {
                size_t nameEnd = openP;
                while (nameEnd > 0 && wDdl[nameEnd - 1] == L' ') --nameEnd;
                size_t nameStart = nameEnd;
                while (nameStart > 0 && wDdl[nameStart - 1] != L' ') --nameStart;
                if (nameEnd > nameStart) objName = wDdl.substr(nameStart, nameEnd - nameStart);
            }
            schemaSheet.rows.push_back({ objName, objType, wDdl });
        }
        pos = endPos;
    }
    out.sheets.push_back(std::move(schemaSheet));

    // Sheet 2: Extracted Record Strings from B-Tree Table Pages
    SpreadsheetSheet dataSheet;
    dataSheet.name = L"Records";
    dataSheet.rows.push_back({ L"Offset", L"Col 1", L"Col 2", L"Col 3", L"Col 4", L"Col 5" });
    dataSheet.maxCols = 6;

    std::vector<std::wstring> curRow;
    std::string curStr;
    size_t strStartOff = 0;
    for (size_t i = 100; i < raw.size() && dataSheet.rows.size() < 2000; ++i) {
        uint8_t b = raw[i];
        if (b >= 32 && b <= 126) {
            if (curStr.empty()) strStartOff = i;
            curStr.push_back(static_cast<char>(b));
        } else {
            if (curStr.size() >= 2 && curStr.rfind("CREATE ", 0) != 0) {
                if (curRow.empty()) {
                    wchar_t offBuf[32] = {};
                    swprintf_s(offBuf, L"0x%06zX", strStartOff);
                    curRow.push_back(offBuf);
                }
                curRow.push_back(PathUtils::Utf8ToWide(curStr));
                if (curRow.size() >= 6) {
                    dataSheet.rows.push_back(std::move(curRow));
                    curRow.clear();
                }
            }
            curStr.clear();
        }
    }
    if (curRow.size() > 1) {
        dataSheet.rows.push_back(std::move(curRow));
    }
    if (dataSheet.rows.size() > 1) {
        out.sheets.push_back(std::move(dataSheet));
    }

    out.valid = true;
    return true;
}
