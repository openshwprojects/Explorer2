#pragma once
// FeatherTable.h — Apache Arrow Feather v1 ("FEA1") & Feather v2 ("ARROW1") (.feather) Reader

namespace Formats {
namespace AiDataDetail {

inline bool ParseFeatherTable(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Feather v1 ("FEA1" ... "FEA1")
    if (raw.size() >= 16 && std::memcmp(raw.data(), "FEA1", 4) == 0 &&
        std::memcmp(raw.data() + raw.size() - 4, "FEA1", 4) == 0)
    {
        uint32_t metaLen = RdU32LE(raw.data() + raw.size() - 8);
        if (metaLen == 0 || metaLen > raw.size() - 12) return false;
        size_t metaOff = raw.size() - 8 - metaLen;
        FbTable ctable;
        if (!GetFbRootTable(raw.data() + metaOff, metaLen, ctable)) return false;

        int64_t numRows = ctable.GetI64(1, 0);
        uint32_t colCnt = 0;
        size_t colVec = ctable.GetVector(2, colCnt);
        if (!colVec || colCnt == 0) return false;

        SpreadsheetSheet schemaSh;
        schemaSh.name = L"Feather_Schema";
        schemaSh.rows.push_back({ L"#", L"Column Name", L"Feather Type", L"Values", L"Nulls" });

        std::vector<std::wstring> hdrRow;
        size_t previewRows = static_cast<size_t>(std::min<int64_t>(std::max<int64_t>(0, numRows), 500));
        std::vector<std::vector<std::wstring>> grid(previewRows, std::vector<std::wstring>(std::min<uint32_t>(colCnt, 128)));

        for (uint32_t c = 0; c < std::min<uint32_t>(colCnt, 128) && colVec + (c + 1) * 4 <= ctable.len; ++c) {
            size_t cElem = colVec + c * 4;
            uint32_t cOff = RdU32LE(ctable.buf + cElem);
            FbTable ct;
            if (!cOff || cElem + cOff > ctable.len || !ct.Init(ctable.buf, ctable.len, cElem + cOff)) continue;
            std::wstring cname = PathUtils::Utf8ToWide(ct.GetString(0));
            hdrRow.push_back(cname);

            FbTable pa;
            int8_t ptype = 0;
            int64_t dataOff = 0, vlen = 0, nulls = 0;
            if (ct.GetTable(1, pa)) {
                ptype = pa.GetI8(0, 0);
                dataOff = pa.GetI64(2, 0);
                vlen = pa.GetI64(3, 0);
                nulls = pa.GetI64(4, 0);
            }
            schemaSh.rows.push_back({ std::to_wstring(c), cname, L"Type(" + std::to_wstring(ptype) + L")", std::to_wstring(vlen), std::to_wstring(nulls) });

            size_t valPos = static_cast<size_t>(dataOff);
            if (nulls > 0) valPos += ((static_cast<size_t>(vlen) + 7) / 8 + 7) & ~size_t(7);
            for (size_t r = 0; r < previewRows; ++r) {
                if (ptype == 11 && valPos + (r + 1) * 4 <= raw.size()) grid[r][c] = FmtDouble(RdF32LE(raw.data() + valPos + r * 4));
                else if (ptype == 12 && valPos + (r + 1) * 8 <= raw.size()) grid[r][c] = FmtDouble(RdF64LE(raw.data() + valPos + r * 8));
                else if (ptype == 3 && valPos + (r + 1) * 4 <= raw.size()) grid[r][c] = std::to_wstring(RdI32LE(raw.data() + valPos + r * 4));
                else if (ptype == 4 && valPos + (r + 1) * 8 <= raw.size()) grid[r][c] = std::to_wstring(RdI64LE(raw.data() + valPos + r * 8));
            }
        }
        FinalizeSheetCols(schemaSh);
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Apache Arrow Feather v1 Table (" + extLower + L")";
        out.sheets.push_back(std::move(schemaSh));
        if (!hdrRow.empty()) {
            SpreadsheetSheet dataSh;
            dataSh.name = L"Feather_Data";
            dataSh.rows.push_back(std::move(hdrRow));
            for (auto& r : grid) dataSh.rows.push_back(std::move(r));
            FinalizeSheetCols(dataSh);
            out.sheets.push_back(std::move(dataSh));
        }
        out.valid = true;
        return true;
    }

    // 2. Feather v2 (Arrow IPC Random-Access File "ARROW1" ... "ARROW1")
    if (raw.size() >= 18 && std::memcmp(raw.data(), "ARROW1", 6) == 0) {
        return ParseArrowRandomAccessMemory(raw, extLower, L"Apache Arrow Feather v2 Table", out);
    }
    return false;
}

inline bool ParseFeatherTableFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) {
    ParquetDetail::WinParquetRandomFile rf;
    if (!rf.Open(fullPath) || rf.fileSize < 16) return false;

    if (rf.fileSize <= 16ULL * 1024ULL * 1024ULL) {
        std::vector<uint8_t> raw(static_cast<size_t>(rf.fileSize));
        if (!rf.ReadAt(0, raw.data(), raw.size())) return false;
        return ParseFeatherTable(raw, extLower, out);
    }

    uint8_t head8[8] = {};
    if (!rf.ReadAt(0, head8, 8)) return false;
    if (std::memcmp(head8, "ARROW1", 6) == 0) {
        return ParseArrowRandomAccessFile(rf, extLower, L"Apache Arrow Feather v2 Table", out);
    }
    if (std::memcmp(head8, "FEA1", 4) == 0) {
        uint8_t tail8[8] = {};
        if (!rf.ReadAt(rf.fileSize - 8, tail8, 8) || std::memcmp(tail8 + 4, "FEA1", 4) != 0) return false;
        uint32_t metaLen = RdU32LE(tail8);
        if (metaLen == 0 || metaLen > 32u * 1024u * 1024u || static_cast<uint64_t>(metaLen) + 12ULL > rf.fileSize) return false;
        std::vector<uint8_t> metaBuf(metaLen);
        if (!rf.ReadAt(rf.fileSize - 8ULL - metaLen, metaBuf.data(), metaLen)) return false;
        FbTable ctable;
        if (!GetFbRootTable(metaBuf.data(), metaLen, ctable)) return false;
        int64_t numRows = ctable.GetI64(1, 0);
        uint32_t colCnt = 0;
        size_t colVec = ctable.GetVector(2, colCnt);
        if (!colVec || colCnt == 0) return false;
        SpreadsheetSheet schemaSh;
        schemaSh.name = L"Feather_Schema";
        schemaSh.rows.push_back({ L"#", L"Column Name", L"Feather Type", L"Values", L"Nulls" });
        std::vector<std::wstring> hdrRow;
        size_t previewRows = static_cast<size_t>(std::min<int64_t>(std::max<int64_t>(0, numRows), 500));
        std::vector<std::vector<std::wstring>> grid(previewRows, std::vector<std::wstring>(std::min<uint32_t>(colCnt, 128)));
        std::vector<uint8_t> colSlice;
        for (uint32_t c = 0; c < std::min<uint32_t>(colCnt, 128) && colVec + (c + 1) * 4 <= ctable.len; ++c) {
            size_t cElem = colVec + c * 4;
            uint32_t cOff = RdU32LE(ctable.buf + cElem);
            FbTable ct;
            if (!cOff || cElem + cOff > ctable.len || !ct.Init(ctable.buf, ctable.len, cElem + cOff)) continue;
            std::wstring cname = PathUtils::Utf8ToWide(ct.GetString(0));
            hdrRow.push_back(cname);
            FbTable pa;
            int8_t ptype = 0;
            int64_t dataOff = 0, vlen = 0, nulls = 0;
            if (ct.GetTable(1, pa)) { ptype = pa.GetI8(0, 0); dataOff = pa.GetI64(2, 0); vlen = pa.GetI64(3, 0); nulls = pa.GetI64(4, 0); }
            schemaSh.rows.push_back({ std::to_wstring(c), cname, L"Type(" + std::to_wstring(ptype) + L")", std::to_wstring(vlen), std::to_wstring(nulls) });
            uint64_t valPos = static_cast<uint64_t>(std::max<int64_t>(0, dataOff));
            if (nulls > 0) valPos += ((static_cast<uint64_t>(std::max<int64_t>(0, vlen)) + 7ULL) / 8ULL + 7ULL) & ~uint64_t(7);
            size_t elemSz = (ptype == 11 || ptype == 3) ? 4 : ((ptype == 12 || ptype == 4) ? 8 : 0);
            if (elemSz > 0 && previewRows > 0 && valPos + previewRows * elemSz <= rf.fileSize) {
                colSlice.resize(previewRows * elemSz);
                if (rf.ReadAt(valPos, colSlice.data(), colSlice.size())) {
                    for (size_t r = 0; r < previewRows; ++r) {
                        if (ptype == 11) grid[r][c] = FmtDouble(RdF32LE(colSlice.data() + r * 4));
                        else if (ptype == 12) grid[r][c] = FmtDouble(RdF64LE(colSlice.data() + r * 8));
                        else if (ptype == 3) grid[r][c] = std::to_wstring(RdI32LE(colSlice.data() + r * 4));
                        else if (ptype == 4) grid[r][c] = std::to_wstring(RdI64LE(colSlice.data() + r * 8));
                    }
                }
            }
        }
        FinalizeSheetCols(schemaSh);
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Apache Arrow Feather v1 Table (" + extLower + L")";
        out.sheets.push_back(std::move(schemaSh));
        if (!hdrRow.empty()) {
            SpreadsheetSheet dataSh;
            dataSh.name = L"Feather_Data";
            dataSh.rows.push_back(std::move(hdrRow));
            for (auto& r : grid) dataSh.rows.push_back(std::move(r));
            FinalizeSheetCols(dataSh);
            out.sheets.push_back(std::move(dataSh));
        }
        out.sourceReadFully = true;
        out.valid = true;
        return true;
    }
    return false;
}

} // namespace AiDataDetail

inline bool ParseFeatherTable(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseFeatherTable(raw, extLower, out); }
inline bool ParseFeatherTableFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseFeatherTableFile(fullPath, extLower, out); }

} // namespace Formats
