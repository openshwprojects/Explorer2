#pragma once
// Dif.h — Data Interchange Format (.dif): a header of three-line items (topic / "vector,value" / "string")
// starting with TABLE and ending with DATA, then two-line data values: "-1,0" + BOT / EOD (tuple start, end of
// data), "0,<number>" + V / TRUE / FALSE / NA / ERROR, "1,0" + a quoted string (embedded quotes doubled).
// Every tuple is one row, empty tuples included, so rows keep their positions.

static bool ParseDifSpreadsheet(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t pos = 0;
    auto nextLine = [&](std::string_view& ln) {
        if (pos >= sv.size()) return false;
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        ln = sv.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        pos = eol + 1;
        return true;
    };
    auto unquote = [](std::string_view s) {
        s = PathUtils::TrimAscii(s);
        std::string v;
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
            for (size_t i = 0; i < s.size(); ++i) {
                v.push_back(s[i]);
                if (s[i] == '"' && i + 1 < s.size() && s[i + 1] == '"') ++i;
            }
        } else {
            v.assign(s.begin(), s.end());
        }
        return v;
    };

    // Header: TABLE first, items until DATA
    std::string_view topic, nums, str;
    if (!nextLine(topic) || PathUtils::TrimAscii(topic) != "TABLE") return false;
    if (!nextLine(nums) || !nextLine(str)) return false;
    bool sawData = false;
    for (int item = 0; item < 256 && !sawData; ++item) {
        if (!nextLine(topic) || !nextLine(nums) || !nextLine(str)) return false;
        sawData = (PathUtils::TrimAscii(topic) == "DATA");
    }
    if (!sawData) return false;

    constexpr size_t kMaxRows = 50000, kMaxCols = 512;
    SpreadsheetSheet sheet;
    sheet.name = L"DIF";
    bool inTuple = false, ended = false;
    std::vector<std::wstring> row;
    size_t values = 0;
    while (!ended && sheet.rows.size() < kMaxRows) {
        std::string_view head, body;
        if (!nextLine(head) || !nextLine(body)) break;
        head = PathUtils::TrimAscii(head);
        size_t comma = head.find(',');
        if (comma == std::string_view::npos) break; // damaged: keep the rows before it
        std::string_view type = head.substr(0, comma), number = head.substr(comma + 1);
        std::string_view indicator = PathUtils::TrimAscii(body);
        if (type == "-1") {
            if (indicator == "BOT") {
                if (inTuple) sheet.rows.push_back(std::move(row));
                row.clear();
                inTuple = true;
            } else if (indicator == "EOD") {
                ended = true;
            }
            continue;
        }
        std::string cell;
        if (type == "0") {
            if (indicator == "V") cell.assign(number.begin(), number.end());
            else if (indicator == "TRUE" || indicator == "FALSE") cell.assign(indicator.begin(), indicator.end());
            else if (indicator == "NA") cell = "#N/A";
            else if (indicator == "ERROR") cell = "#ERROR";
            else cell.assign(number.begin(), number.end());
        } else if (type == "1") {
            cell = unquote(body);
        } else {
            break;
        }
        if (inTuple && row.size() < kMaxCols) row.push_back(PathUtils::Utf8ToWide(cell));
        ++values;
    }
    if (inTuple) sheet.rows.push_back(std::move(row));
    while (!sheet.rows.empty() && sheet.rows.back().empty()) sheet.rows.pop_back();
    if (values == 0 || sheet.rows.empty()) return false;
    for (const auto& r : sheet.rows) sheet.maxCols = std::max(sheet.maxCols, r.size());

    out.kind = OfficeFileKind::DifSylkTable;
    out.isSpreadsheet = true;
    out.formatTitle = L"DIF Data Interchange Spreadsheet (.dif)";
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}
