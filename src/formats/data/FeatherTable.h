#pragma once
// FeatherTable.h — Apache Arrow Feather v1 ("FEA1") & Feather v2 ("ARROW1") (.feather) Reader

namespace Formats {
namespace AiDataDetail {

inline const wchar_t* FeatherV1TypeName(int8_t ptype, uint8_t metaType) {
    if (metaType == 1) return L"CATEGORY";
    if (metaType == 2) return L"TIMESTAMP";
    if (metaType == 3) return L"DATE";
    if (metaType == 4) return L"TIME";
    switch (ptype) {
        case 0:  return L"BOOL";
        case 1:  return L"INT8";
        case 2:  return L"INT16";
        case 3:  return L"INT32";
        case 4:  return L"INT64";
        case 5:  return L"UINT8";
        case 6:  return L"UINT16";
        case 7:  return L"UINT32";
        case 8:  return L"UINT64";
        case 9:  return L"FLOAT";
        case 10: return L"DOUBLE";
        case 11: return L"UTF8";
        case 12: return L"BINARY";
        case 13: return L"CATEGORY";
        case 14: return L"TIMESTAMP";
        case 15: return L"DATE";
        case 16: return L"TIME";
        case 17: return L"LARGE_UTF8";
        case 18: return L"LARGE_BINARY";
        default: return L"UNKNOWN";
    }
}

inline std::wstring FormatFeatherDate32(int32_t daysSinceEpoch) {
    return GeoSciDetail::FormatUtcEpoch(static_cast<int64_t>(daysSinceEpoch) * 86400LL).substr(0, 10);
}

inline std::wstring FormatFeatherTimeOfDay(int64_t val, int8_t unit) {
    int64_t sec = 0, frac = 0;
    int fracDigits = 0;
    if (unit == 0) { sec = val; }
    else if (unit == 1) { sec = val / 1000LL; frac = std::llabs(val % 1000LL); fracDigits = 3; }
    else if (unit == 2) { sec = val / 1000000LL; frac = std::llabs(val % 1000000LL); fracDigits = 6; }
    else { sec = val / 1000000000LL; frac = std::llabs(val % 1000000000LL); fracDigits = 9; }
    if (sec < 0) sec = 0;
    int hr = static_cast<int>((sec / 3600LL) % 24LL);
    int mn = static_cast<int>((sec % 3600LL) / 60LL);
    int sc = static_cast<int>(sec % 60LL);
    wchar_t buf[64] = {};
    if (fracDigits > 0 && frac > 0) {
        swprintf_s(buf, L"%02d:%02d:%02d.%0*lld", hr, mn, sc, fracDigits, frac);
    } else {
        swprintf_s(buf, L"%02d:%02d:%02d", hr, mn, sc);
    }
    return buf;
}

inline std::wstring FormatFeatherTimestamp(int64_t val, int8_t unit, const std::string& tz) {
    int64_t sec = 0, frac = 0;
    int fracDigits = 0;
    if (unit == 0) { sec = val; }
    else if (unit == 1) { sec = (val >= 0) ? (val / 1000LL) : ((val - 999LL) / 1000LL); frac = val - sec * 1000LL; fracDigits = 3; }
    else if (unit == 2) { sec = (val >= 0) ? (val / 1000000LL) : ((val - 999999LL) / 1000000LL); frac = val - sec * 1000000LL; fracDigits = 6; }
    else { sec = (val >= 0) ? (val / 1000000000LL) : ((val - 999999999LL) / 1000000000LL); frac = val - sec * 1000000000LL; fracDigits = 9; }
    std::wstring base = GeoSciDetail::FormatUtcEpoch(sec); // "YYYY-MM-DD HH:MM:SS UTC"
    if (base.size() >= 19 && fracDigits > 0 && frac > 0) {
        wchar_t fbuf[32] = {};
        swprintf_s(fbuf, L".%0*lld", fracDigits, frac);
        base.insert(19, fbuf);
    }
    if (!tz.empty() && tz != "UTC" && base.size() >= 4 && base.substr(base.size() - 4) == L" UTC") {
        base.replace(base.size() - 3, 3, PathUtils::Utf8ToWide(tz));
    }
    return base;
}

template <typename ReadFn>
inline bool DecodeFeatherV1PrimitiveLevels(
    const FbTable& pa,
    uint64_t fileSize,
    ReadFn&& readSlice,
    size_t maxItems,
    std::vector<std::wstring>& outVals)
{
    outVals.clear();
    int8_t ptype = pa.GetI8(0, 0);
    int64_t dataOff = pa.GetI64(2, 0);
    int64_t vlen = pa.GetI64(3, 0);
    int64_t nulls = pa.GetI64(4, 0);
    int64_t totalBytes = pa.GetI64(5, 0);
    if (dataOff < 0 || vlen <= 0 || static_cast<uint64_t>(dataOff) > fileSize) return false;

    size_t rows = static_cast<size_t>(std::min<int64_t>(vlen, static_cast<int64_t>(maxItems)));
    uint64_t curPos = static_cast<uint64_t>(dataOff);
    uint64_t nullBytesPadded = 0;
    std::vector<uint8_t> nullBmp;
    if (nulls > 0) {
        nullBytesPadded = (((static_cast<uint64_t>(vlen) + 7ULL) / 8ULL) + 7ULL) & ~uint64_t(7);
        size_t needBmp = (rows + 7u) / 8u;
        if (curPos + needBmp > fileSize) return false;
        nullBmp.resize(needBmp);
        if (!readSlice(curPos, nullBmp.data(), needBmp)) return false;
        curPos += nullBytesPadded;
    }

    auto isRowNull = [&](size_t r) -> bool {
        if (nulls <= 0 || (r >> 3) >= nullBmp.size()) return false;
        return ((nullBmp[r >> 3] >> (r & 7)) & 1u) == 0;
    };

    outVals.resize(rows);
    if (ptype == 11 || ptype == 12) { // UTF8 / BINARY (int32 offsets + bytes)
        uint64_t rawOffBytes = (static_cast<uint64_t>(vlen) + 1ULL) * 4ULL;
        uint64_t padOffBytes = (rawOffBytes + 7ULL) & ~uint64_t(7);
        size_t needOffBytes = (rows + 1u) * 4u;
        if (curPos + needOffBytes > fileSize) return false;
        std::vector<uint8_t> offBuf(needOffBytes);
        if (!readSlice(curPos, offBuf.data(), needOffBytes)) return false;

        uint64_t strBase = curPos + padOffBytes;
        if (padOffBytes != rawOffBytes && totalBytes > 0 && curPos + rawOffBytes <= fileSize) {
            uint8_t lastOff4[4] = {};
            if (readSlice(curPos + static_cast<uint64_t>(vlen) * 4ULL, lastOff4, 4)) {
                int32_t endOff = RdI32LE(lastOff4);
                if (endOff >= 0 && nullBytesPadded + rawOffBytes + static_cast<uint64_t>(endOff) == static_cast<uint64_t>(totalBytes)) {
                    strBase = curPos + rawOffBytes;
                }
            }
        }

        int32_t minOff = RdI32LE(offBuf.data());
        int32_t maxOff = RdI32LE(offBuf.data() + rows * 4u);
        if (minOff < 0 || maxOff < minOff || maxOff - minOff > 32 * 1024 * 1024) return false;
        size_t spanLen = static_cast<size_t>(maxOff - minOff);
        std::vector<uint8_t> valBuf(spanLen);
        if (spanLen > 0) {
            if (strBase + static_cast<uint64_t>(maxOff) > fileSize) return false;
            if (!readSlice(strBase + static_cast<uint64_t>(minOff), valBuf.data(), spanLen)) return false;
        }
        for (size_t r = 0; r < rows; ++r) {
            if (isRowNull(r)) { outVals[r] = L"NULL"; continue; }
            int32_t o0 = RdI32LE(offBuf.data() + r * 4u);
            int32_t o1 = RdI32LE(offBuf.data() + (r + 1u) * 4u);
            if (o0 >= minOff && o1 >= o0 && static_cast<size_t>(o1 - minOff) <= valBuf.size()) {
                const uint8_t* sp = valBuf.data() + (o0 - minOff);
                size_t sl = static_cast<size_t>(o1 - o0);
                if (ptype == 11) {
                    outVals[r] = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp), sl));
                } else {
                    outVals[r] = DetailStructuredDataDoc::HexBytes(sp, sl, 24);
                }
            }
        }
        return true;
    }

    if (ptype == 17 || ptype == 18) { // LARGE_UTF8 / LARGE_BINARY (int64 offsets + bytes)
        uint64_t rawOffBytes = (static_cast<uint64_t>(vlen) + 1ULL) * 8ULL;
        size_t needOffBytes = (rows + 1u) * 8u;
        if (curPos + needOffBytes > fileSize) return false;
        std::vector<uint8_t> offBuf(needOffBytes);
        if (!readSlice(curPos, offBuf.data(), needOffBytes)) return false;
        uint64_t strBase = curPos + rawOffBytes;
        int64_t minOff = RdI64LE(offBuf.data());
        int64_t maxOff = RdI64LE(offBuf.data() + rows * 8u);
        if (minOff < 0 || maxOff < minOff || maxOff - minOff > 32LL * 1024LL * 1024LL) return false;
        size_t spanLen = static_cast<size_t>(maxOff - minOff);
        std::vector<uint8_t> valBuf(spanLen);
        if (spanLen > 0) {
            if (strBase + static_cast<uint64_t>(maxOff) > fileSize) return false;
            if (!readSlice(strBase + static_cast<uint64_t>(minOff), valBuf.data(), spanLen)) return false;
        }
        for (size_t r = 0; r < rows; ++r) {
            if (isRowNull(r)) { outVals[r] = L"NULL"; continue; }
            int64_t o0 = RdI64LE(offBuf.data() + r * 8u);
            int64_t o1 = RdI64LE(offBuf.data() + (r + 1u) * 8u);
            if (o0 >= minOff && o1 >= o0 && static_cast<size_t>(o1 - minOff) <= valBuf.size()) {
                const uint8_t* sp = valBuf.data() + static_cast<size_t>(o0 - minOff);
                size_t sl = static_cast<size_t>(o1 - o0);
                outVals[r] = (ptype == 17)
                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp), sl))
                    : DetailStructuredDataDoc::HexBytes(sp, sl, 24);
            }
        }
        return true;
    }

    if (ptype == 0) { // BOOL (bit-packed LSB-first)
        size_t needBytes = (rows + 7u) / 8u;
        if (curPos + needBytes > fileSize) return false;
        std::vector<uint8_t> valBuf(needBytes);
        if (needBytes > 0 && !readSlice(curPos, valBuf.data(), needBytes)) return false;
        for (size_t r = 0; r < rows; ++r) {
            if (isRowNull(r)) { outVals[r] = L"NULL"; continue; }
            outVals[r] = (((valBuf[r >> 3] >> (r & 7)) & 1u) != 0) ? L"true" : L"false";
        }
        return true;
    }

    size_t elemSz = 0;
    if (ptype == 1 || ptype == 5) elemSz = 1;
    else if (ptype == 2 || ptype == 6) elemSz = 2;
    else if (ptype == 3 || ptype == 7 || ptype == 9 || ptype == 13 || ptype == 15) elemSz = 4;
    else if (ptype == 4 || ptype == 8 || ptype == 10 || ptype == 14 || ptype == 16) elemSz = 8;
    if (elemSz == 0 || curPos + rows * elemSz > fileSize) return false;

    std::vector<uint8_t> valBuf(rows * elemSz);
    if (!valBuf.empty() && !readSlice(curPos, valBuf.data(), valBuf.size())) return false;
    for (size_t r = 0; r < rows; ++r) {
        if (isRowNull(r)) { outVals[r] = L"NULL"; continue; }
        const uint8_t* p = valBuf.data() + r * elemSz;
        switch (ptype) {
            case 1:  outVals[r] = std::to_wstring(static_cast<int>(static_cast<int8_t>(p[0]))); break;
            case 5:  outVals[r] = std::to_wstring(static_cast<unsigned>(p[0])); break;
            case 2:  outVals[r] = std::to_wstring(RdI16LE(p)); break;
            case 6:  outVals[r] = std::to_wstring(RdU16LE(p)); break;
            case 3:
            case 13: outVals[r] = std::to_wstring(RdI32LE(p)); break;
            case 7:  outVals[r] = std::to_wstring(RdU32LE(p)); break;
            case 4:
            case 14:
            case 16: outVals[r] = std::to_wstring(RdI64LE(p)); break;
            case 8:  outVals[r] = std::to_wstring(RdU64LE(p)); break;
            case 9:  outVals[r] = FmtDouble(RdF32LE(p)); break;
            case 10: outVals[r] = FmtDouble(RdF64LE(p)); break;
            case 15: outVals[r] = FormatFeatherDate32(RdI32LE(p)); break;
            default: break;
        }
    }
    return true;
}

template <typename ReadFn>
inline bool ParseFeatherV1Common(
    const uint8_t* metaBuf,
    uint32_t metaLen,
    uint64_t fileSize,
    ReadFn&& readSlice,
    const std::wstring& extLower,
    ParsedOfficeDocument& out)
{
    FbTable ctable;
    if (!GetFbRootTable(metaBuf, metaLen, ctable)) return false;

    int64_t numRows = ctable.GetI64(1, 0);
    uint32_t colCnt = 0;
    size_t colVec = ctable.GetVector(2, colCnt);
    if (!colVec || colCnt == 0) return false;
    int32_t fver = ctable.GetI32(3, 2);

    SpreadsheetSheet schemaSh;
    schemaSh.name = L"Feather_Schema";
    schemaSh.rows.push_back({ L"#", L"Column Name", L"Feather Type", L"Values", L"Nulls" });
    schemaSh.rows.push_back({
        L"(meta)",
        L"Feather v1 (CTable v" + std::to_wstring(fver) + L")",
        L"Rows: " + std::to_wstring(numRows),
        L"Columns: " + std::to_wstring(colCnt),
        PathUtils::Utf8ToWide(ctable.GetString(0))
    });

    const uint32_t takeCols = std::min<uint32_t>(colCnt, 128u);
    const size_t previewRows = static_cast<size_t>(std::min<int64_t>(std::max<int64_t>(0, numRows), 500));
    std::vector<std::wstring> hdrRow;
    std::vector<std::vector<std::wstring>> grid(previewRows);
    for (auto& r : grid) r.reserve(takeCols);

    for (uint32_t c = 0; c < takeCols && colVec + (c + 1u) * 4u <= ctable.len; ++c) {
        size_t cElem = colVec + c * 4u;
        uint32_t cOff = RdU32LE(ctable.buf + cElem);
        FbTable ct;
        if (!cOff || cElem + cOff > ctable.len || !ct.Init(ctable.buf, ctable.len, cElem + cOff)) continue;

        std::wstring cname = PathUtils::Utf8ToWide(ct.GetString(0));
        if (cname.empty()) cname = L"col_" + std::to_wstring(c);
        size_t outColIdx = hdrRow.size();
        hdrRow.push_back(cname);
        for (auto& r : grid) r.emplace_back();

        FbTable pa;
        int8_t ptype = 0;
        int64_t vlen = 0, nulls = 0;
        uint8_t metaType = ct.GetU8(2, 0);
        FbTable metaTbl;
        bool hasMetaTbl = (metaType != 0) && ct.GetTable(3, metaTbl);

        if (ct.GetTable(1, pa)) {
            ptype = pa.GetI8(0, 0);
            vlen = pa.GetI64(3, 0);
            nulls = pa.GetI64(4, 0);
        }

        std::wstring typeLabel = FeatherV1TypeName(ptype, metaType);
        int8_t timeUnit = 0;
        std::string tz;
        std::vector<std::wstring> catLevels;
        if (hasMetaTbl) {
            if (metaType == 1) { // CategoryMetadata
                FbTable lvlPa;
                if (metaTbl.GetTable(0, lvlPa)) {
                    DecodeFeatherV1PrimitiveLevels(lvlPa, fileSize, readSlice, 4096, catLevels);
                }
            } else if (metaType == 2) { // TimestampMetadata
                timeUnit = metaTbl.GetI8(0, 0);
                tz = metaTbl.GetString(1);
            } else if (metaType == 4) { // TimeMetadata
                timeUnit = metaTbl.GetI8(0, 0);
            }
        }

        schemaSh.rows.push_back({
            std::to_wstring(c),
            cname,
            typeLabel,
            std::to_wstring(vlen),
            std::to_wstring(nulls)
        });

        if (previewRows > 0 && ct.GetTable(1, pa)) {
            std::vector<std::wstring> colVals;
            if (DecodeFeatherV1PrimitiveLevels(pa, fileSize, readSlice, previewRows, colVals)) {
                size_t n = std::min(previewRows, colVals.size());
                for (size_t r = 0; r < n; ++r) {
                    if (colVals[r] == L"NULL") {
                        grid[r][outColIdx] = L"NULL";
                    } else if ((metaType == 2 || ptype == 14) && (ptype == 4 || ptype == 14)) {
                        int64_t rawTs = std::stoll(colVals[r]);
                        grid[r][outColIdx] = FormatFeatherTimestamp(rawTs, timeUnit, tz);
                    } else if (metaType == 3 && ptype == 3) {
                        int32_t rawDays = static_cast<int32_t>(std::stol(colVals[r]));
                        grid[r][outColIdx] = FormatFeatherDate32(rawDays);
                    } else if ((metaType == 4 || ptype == 16) && (ptype == 3 || ptype == 4 || ptype == 16)) {
                        int64_t rawTm = std::stoll(colVals[r]);
                        grid[r][outColIdx] = FormatFeatherTimeOfDay(rawTm, timeUnit);
                    } else if (!catLevels.empty()) {
                        long long idx = std::stoll(colVals[r]);
                        if (idx >= 0 && static_cast<size_t>(idx) < catLevels.size()) {
                            grid[r][outColIdx] = catLevels[static_cast<size_t>(idx)];
                        } else {
                            grid[r][outColIdx] = std::move(colVals[r]);
                        }
                    } else {
                        grid[r][outColIdx] = std::move(colVals[r]);
                    }
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
    out.valid = true;
    return true;
}

inline bool ParseFeatherTable(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Feather v1 ("FEA1" ... "FEA1")
    if (raw.size() >= 16 && std::memcmp(raw.data(), "FEA1", 4) == 0 &&
        std::memcmp(raw.data() + raw.size() - 4, "FEA1", 4) == 0)
    {
        uint32_t metaLen = RdU32LE(raw.data() + raw.size() - 8);
        if (metaLen == 0 || metaLen > raw.size() - 12) return false;
        size_t metaOff = raw.size() - 8 - metaLen;
        auto readMem = [&](uint64_t off, uint8_t* dst, size_t len) -> bool {
            if (off > raw.size() || len > raw.size() - static_cast<size_t>(off)) return false;
            if (len > 0) std::memcpy(dst, raw.data() + static_cast<size_t>(off), len);
            return true;
        };
        return ParseFeatherV1Common(raw.data() + metaOff, metaLen, raw.size(), readMem, extLower, out);
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
        out.sourceReadFully = true;
        return ParseFeatherTable(raw, extLower, out);
    }

    uint8_t head8[8] = {};
    if (!rf.ReadAt(0, head8, 8)) return false;
    if (std::memcmp(head8, "ARROW1", 6) == 0) {
        out.sourceReadFully = true;
        return ParseArrowRandomAccessFile(rf, extLower, L"Apache Arrow Feather v2 Table", out);
    }
    if (std::memcmp(head8, "FEA1", 4) == 0) {
        uint8_t tail8[8] = {};
        if (!rf.ReadAt(rf.fileSize - 8, tail8, 8) || std::memcmp(tail8 + 4, "FEA1", 4) != 0) return false;
        uint32_t metaLen = RdU32LE(tail8);
        if (metaLen == 0 || metaLen > 32u * 1024u * 1024u || static_cast<uint64_t>(metaLen) + 12ULL > rf.fileSize) return false;
        std::vector<uint8_t> metaBuf(metaLen);
        if (!rf.ReadAt(rf.fileSize - 8ULL - metaLen, metaBuf.data(), metaLen)) return false;
        auto readFile = [&](uint64_t off, uint8_t* dst, size_t len) -> bool {
            return len == 0 || rf.ReadAt(off, dst, len);
        };
        if (!ParseFeatherV1Common(metaBuf.data(), metaLen, rf.fileSize, readFile, extLower, out)) return false;
        out.sourceReadFully = true;
        return true;
    }
    return false;
}

} // namespace AiDataDetail

inline bool ParseFeatherTable(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseFeatherTable(raw, extLower, out); }
inline bool ParseFeatherTableFile(const std::wstring& fullPath, const std::wstring& extLower, ParsedOfficeDocument& out) { return AiDataDetail::ParseFeatherTableFile(fullPath, extLower, out); }

} // namespace Formats
