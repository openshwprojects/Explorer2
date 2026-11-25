#pragma once
// BioWare2daTable.h — BioWare Aurora / Odyssey 2D Array Table (.2da — "2DA V2.0" ASCII & "2DA V2.b" Binary)
// Used across Neverwinter Nights 1 & 2, Star Wars: KotOR I & II, Jade Empire, and The Witcher.

namespace Formats {

namespace DetailBioWare2da {

inline bool Next2daAsciiTokens(std::string_view line, std::vector<std::string>& outToks) {
    outToks.clear();
    size_t i = 0;
    const size_t n = line.size();
    while (i < n) {
        while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= n) break;
        if (line[i] == '"') {
            ++i;
            std::string tok;
            bool closed = false;
            while (i < n) {
                if (line[i] == '"') {
                    ++i;
                    closed = true;
                    break;
                }
                tok.push_back(line[i++]);
            }
            if (!closed) return false; // Unterminated quote
            outToks.push_back(std::move(tok));
        } else {
            size_t s = i;
            while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
            outToks.emplace_back(line.substr(s, i - s));
        }
    }
    return true;
}

inline bool Parse2daBinaryV2b(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 18u) return false;
    if (raw[8] != '\n' && raw[8] != '\r') return false;

    size_t pos = 9u;
    if (raw[8] == '\r' && pos < raw.size() && raw[pos] == '\n') ++pos;

    // 1. Tab-separated column names terminated by NUL
    const size_t colStart = pos;
    while (pos < raw.size() && raw[pos] != 0u) ++pos;
    if (pos >= raw.size()) return false;
    std::string_view colBlob(reinterpret_cast<const char*>(raw.data() + colStart), pos - colStart);
    ++pos; // skip NUL

    std::vector<std::wstring> cols;
    cols.push_back(L"Row");
    {
        size_t s = 0;
        while (s < colBlob.size()) {
            size_t tab = colBlob.find('\t', s);
            std::string_view seg = (tab == std::string_view::npos) ? colBlob.substr(s) : colBlob.substr(s, tab - s);
            if (!seg.empty()) {
                cols.push_back(BioWareCommon::DecodeBioWareString(seg));
            }
            if (tab == std::string_view::npos) break;
            s = tab + 1u;
        }
    }
    const size_t numCols = cols.size() - 1u;
    if (numCols == 0u || numCols > 1024u) return false;

    // 2. uint32 rowCount
    if (raw.size() - pos < 4u) return false;
    const uint32_t rowCount = ByteIO::RdU32LE(raw.data() + pos);
    pos += 4u;
    if (rowCount == 0u || rowCount > 65536u) return false;
    if (static_cast<uint64_t>(rowCount) * numCols > 2000000ULL) return false;

    // 3. `rowCount` tab-terminated row labels
    std::vector<std::wstring> rowLabels;
    rowLabels.reserve(rowCount);
    for (uint32_t r = 0; r < rowCount; ++r) {
        const size_t rs = pos;
        while (pos < raw.size() && raw[pos] != '\t' && raw[pos] != 0u) ++pos;
        if (pos >= raw.size() || raw[pos] != '\t') return false;
        std::string_view rl(reinterpret_cast<const char*>(raw.data() + rs), pos - rs);
        rowLabels.push_back(BioWareCommon::DecodeBioWareString(rl));
        ++pos; // skip '\t'
    }

    // 4. `rowCount * numCols` uint16 cell offsets + uint16 dataSize + string pool
    const uint64_t cellCount = static_cast<uint64_t>(rowCount) * numCols;
    const uint64_t offBytes  = cellCount * 2ULL;
    if (offBytes + 2ULL > raw.size() - pos) return false;

    const size_t offsetsBase = pos;
    pos += static_cast<size_t>(offBytes);
    const uint16_t dataSize = ByteIO::RdU16LE(raw.data() + pos);
    pos += 2u;
    if (dataSize > raw.size() - pos) return false;
    const size_t poolBase = pos;
    const size_t poolEnd  = poolBase + dataSize;
    if (dataSize > 0u && raw[poolEnd - 1u] != 0u) return false;

    SpreadsheetSheet sheet;
    sheet.name = L"2DA_V2b";
    sheet.rows.reserve(static_cast<size_t>(rowCount) + 1u);
    sheet.rows.push_back(std::move(cols));

    for (uint32_t r = 0; r < rowCount; ++r) {
        std::vector<std::wstring> row;
        row.reserve(numCols + 1u);
        row.push_back(rowLabels[r]);
        for (size_t c = 0; c < numCols; ++c) {
            const size_t idx = static_cast<size_t>(r) * numCols + c;
            const uint16_t relOff = ByteIO::RdU16LE(raw.data() + offsetsBase + idx * 2u);
            if (relOff >= dataSize) {
                if (dataSize == 0u && relOff == 0u) {
                    row.emplace_back();
                    continue;
                }
                return false;
            }
            const char* sPtr = reinterpret_cast<const char*>(raw.data() + poolBase + relOff);
            const size_t maxLen = dataSize - relOff;
            const size_t slen = strnlen(sPtr, maxLen);
            if (slen >= maxLen) return false;
            row.push_back(BioWareCommon::DecodeBioWareString(std::string_view(sPtr, slen)));
        }
        sheet.rows.push_back(std::move(row));
    }

    wchar_t title[160] = {};
    swprintf_s(title, L"BioWare Binary 2DA Table (2DA V2.b, %u rows x %zu cols)", rowCount, numCols);
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

inline bool Parse2daAsciiV20(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(raw.data(), raw.size(), sv, 12u, (32u << 20))) return false;

    size_t linePos = 0u;
    std::string_view rawLine;
    if (!ReaderText::NextLineSv(sv, linePos, rawLine)) return false;
    std::string_view hdrLine = ReaderText::TrimSv(rawLine);
    if (hdrLine.size() < 8u || hdrLine.size() > 16u || !ReaderText::StartsWithCI(hdrLine, "2DA V2.0")) return false;

    std::string defaultVal;
    std::vector<std::string> colToks;
    bool gotCols = false;

    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view tr = ReaderText::TrimSv(rawLine);
        if (tr.empty()) continue;
        if (!gotCols && ReaderText::StartsWithCI(tr, "DEFAULT:")) {
            defaultVal = std::string(ReaderText::TrimSv(tr.substr(8)));
            continue;
        }
        if (!Next2daAsciiTokens(tr, colToks) || colToks.empty()) return false;
        gotCols = true;
        break;
    }
    if (!gotCols || colToks.empty() || colToks.size() > 1024u) return false;

    const size_t numCols = colToks.size();
    SpreadsheetSheet sheet;
    sheet.name = L"2DA_V20";
    {
        std::vector<std::wstring> hdrRow;
        hdrRow.reserve(numCols + 1u);
        hdrRow.push_back(L"Row");
        for (const auto& ct : colToks) hdrRow.push_back(BioWareCommon::DecodeBioWareString(ct));
        sheet.rows.push_back(std::move(hdrRow));
    }

    std::vector<std::string> rowToks;
    size_t dataRows = 0u;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view tr = ReaderText::TrimSv(rawLine);
        if (tr.empty()) continue;
        if (!Next2daAsciiTokens(tr, rowToks)) return false;
        // Strict column count check: every data row must have 1 (row index) + numCols tokens!
        if (rowToks.size() != numCols + 1u) return false;

        std::vector<std::wstring> row;
        row.reserve(numCols + 1u);
        row.push_back(BioWareCommon::DecodeBioWareString(rowToks[0]));
        for (size_t c = 1u; c <= numCols; ++c) {
            const std::string& cell = rowToks[c];
            if (cell == "****" && !defaultVal.empty()) {
                row.push_back(BioWareCommon::DecodeBioWareString(defaultVal));
            } else {
                row.push_back(BioWareCommon::DecodeBioWareString(cell));
            }
        }
        sheet.rows.push_back(std::move(row));
        if (++dataRows >= 65536u) break;
    }

    if (dataRows == 0u) return false;
    wchar_t title[160] = {};
    swprintf_s(title, L"BioWare ASCII 2DA Table (2DA V2.0, %zu rows x %zu cols)", dataRows, numCols);
    return BioWareCommon::EmitSpreadsheetDocument(outDoc, title, std::move(sheet));
}

} // namespace DetailBioWare2da

inline bool ParseBioWare2daTable(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    if (raw.size() < 12u || raw.size() > (32u << 20)) return false;
    if (std::memcmp(raw.data(), "2DA V2.b", 8) == 0) {
        return DetailBioWare2da::Parse2daBinaryV2b(raw, outDoc);
    }
    return DetailBioWare2da::Parse2daAsciiV20(raw, outDoc);
}

} // namespace Formats
