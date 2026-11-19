#pragma once
// AccessDatabaseMdb.h — Microsoft Access Jet3 & Jet4/ACE Database (.accdb / .mdb) Reader

namespace Formats {

// ============================================================================
// #81. Microsoft Access Database (.accdb / .mdb) — Real Jet3 & Jet4/ACE Engine
// ============================================================================
namespace DetailDocs200 {

struct JetColDef {
    uint8_t colType = 0;
    uint16_t colId = 0;
    uint16_t varIdx = 0;
    uint16_t displayIdx = 0;
    uint8_t precision = 0;
    uint8_t scale = 0;
    bool isFixed = false;
    uint16_t fixedOff = 0;
    uint16_t colLen = 0;
    std::wstring name;
};

struct JetTableDef {
    uint32_t tdefPage = 0;
    uint32_t numRowsHdr = 0;
    uint8_t tableType = 0;
    uint16_t numVarCols = 0;
    bool isSystem = false;
    std::wstring name;
    std::vector<JetColDef> cols;
};

inline std::wstring DecodeJetText(const uint8_t* p, size_t len, bool isJet3) {
    if (len == 0) return {};
    if (isJet3) {
        if (IsMostlyPrintableUtf8(p, len)) {
            return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), len));
        }
        std::wstring s;
        s.reserve(len);
        for (size_t i = 0; i < len && p[i] != 0; ++i) {
            s.push_back(static_cast<wchar_t>(p[i]));
        }
        return s;
    }
    // Jet4 / ACE Compressed Unicode starts with 0xFF 0xFE; 0x00 toggles 1-byte Latin-1 vs 2-byte UTF-16LE
    if (len >= 2 && p[0] == 0xFFu && p[1] == 0xFEu) {
        std::wstring out;
        out.reserve(len);
        bool compressed = true;
        size_t i = 2;
        while (i < len) {
            if (p[i] == 0x00u) {
                compressed = !compressed;
                ++i;
            } else if (compressed) {
                out.push_back(static_cast<wchar_t>(p[i++]));
            } else {
                if (i + 1 >= len) break;
                wchar_t wc = static_cast<wchar_t>(RdU16LE(p + i));
                i += 2;
                if (wc != 0) out.push_back(wc);
            }
        }
        return out;
    }
    return ReadUtf16LE(p, len);
}

inline std::wstring FormatOleDate(double oleDays) {
    if (!std::isfinite(oleDays) || oleDays < -657434.0 || oleDays > 2958465.0) {
        wchar_t b[48] = {};
        swprintf_s(b, L"%.12g", oleDays);
        return b;
    }
    // OLE Automation Epoch: 1899-12-30 (25569 days before 1970-01-01)
    double totalSec = std::round((oleDays - 25569.0) * 86400.0);
    int64_t sec = static_cast<int64_t>(totalSec);
    int64_t days = sec / 86400;
    int64_t rem = sec % 86400;
    if (rem < 0) { rem += 86400; --days; }
    // Howard Hinnant civil_from_days algorithm
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = static_cast<int64_t>(yoe) + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    wchar_t buf[48] = {};
    if (rem == 0) {
        swprintf_s(buf, L"%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    } else {
        uint32_t hh = static_cast<uint32_t>(rem / 3600);
        uint32_t mm = static_cast<uint32_t>((rem % 3600) / 60);
        uint32_t ss = static_cast<uint32_t>(rem % 60);
        swprintf_s(buf, L"%04lld-%02u-%02u %02u:%02u:%02u", static_cast<long long>(y), m, d, hh, mm, ss);
    }
    return buf;
}

inline std::wstring DecodeJetLvalSingle(const std::vector<uint8_t>& raw, size_t pageSize, bool isJet3, uint32_t pgRow) {
    uint32_t lvalPg = pgRow >> 8;
    uint32_t lvalRow = pgRow & 0xFFu;
    size_t numPages = raw.size() / pageSize;
    if (lvalPg == 0 || lvalPg >= numPages) return {};
    const uint8_t* pg = raw.data() + static_cast<size_t>(lvalPg) * pageSize;
    if (pg[0] != 0x01u) return {};
    size_t hdrLen = isJet3 ? 10u : 14u;
    uint16_t mask = isJet3 ? 0x0FFFu : 0x1FFFu;
    uint16_t nRows = RdU16LE(pg + (isJet3 ? 8u : 12u));
    if (lvalRow >= nRows || hdrLen + static_cast<size_t>(nRows) * 2u > pageSize) return {};
    uint16_t rawOff = RdU16LE(pg + hdrLen + lvalRow * 2u);
    if ((rawOff & 0x8000u) != 0) return {};
    size_t rStart = static_cast<size_t>(rawOff & mask);
    size_t rEnd = (lvalRow == 0) ? pageSize : static_cast<size_t>(RdU16LE(pg + hdrLen + (lvalRow - 1u) * 2u) & mask);
    if (rStart >= rEnd || rEnd > pageSize) return {};
    return DecodeJetText(pg + rStart, rEnd - rStart, isJet3);
}

inline std::wstring FormatJetCellValue(const JetColDef& col, const uint8_t* val, size_t len,
                                       bool isJet3, const std::vector<uint8_t>& rawDb, size_t pageSize)
{
    wchar_t numBuf[64] = {};
    switch (col.colType) {
        case 0x02u: // BYTE
            return (len >= 1) ? std::to_wstring(val[0]) : L"";
        case 0x03u: // INT16
            return (len >= 2) ? std::to_wstring(static_cast<int16_t>(RdU16LE(val))) : L"";
        case 0x04u: // LONG32
            return (len >= 4) ? std::to_wstring(RdI32LE(val)) : L"";
        case 0x05u: { // MONEY / CURRENCY (int64 scaled by 10000)
            if (len < 8) return L"";
            int64_t ticks = static_cast<int64_t>(RdU64LE(val));
            double cur = static_cast<double>(ticks) / 10000.0;
            swprintf_s(numBuf, L"%.4f", cur);
            std::wstring s = numBuf;
            while (s.size() > 1 && s.find(L'.') != std::wstring::npos && s.back() == L'0') s.pop_back();
            if (!s.empty() && s.back() == L'.') s.pop_back();
            return s;
        }
        case 0x06u: { // FLOAT32
            if (len < 4) return L"";
            uint32_t u = RdU32LE(val);
            float f = 0.0f;
            std::memcpy(&f, &u, 4);
            if (!std::isfinite(f)) return L"NaN";
            swprintf_s(numBuf, L"%.7g", static_cast<double>(f));
            return numBuf;
        }
        case 0x07u: { // DOUBLE64
            if (len < 8) return L"";
            uint64_t u = RdU64LE(val);
            double d = 0.0;
            std::memcpy(&d, &u, 8);
            if (!std::isfinite(d)) return L"NaN";
            swprintf_s(numBuf, L"%.12g", d);
            return numBuf;
        }
        case 0x08u: { // DATETIME (OLE DATE double)
            if (len < 8) return L"";
            uint64_t u = RdU64LE(val);
            double d = 0.0;
            std::memcpy(&d, &u, 8);
            return FormatOleDate(d);
        }
        case 0x0Au: // TEXT
            return DecodeJetText(val, len, isJet3);
        case 0x0Cu: { // MEMO (Long Text)
            if (len < 12) return DecodeJetText(val, len, isJet3);
            uint32_t memoHdr = RdU32LE(val);
            uint32_t memoBytes = memoHdr & 0x3FFFFFFFu;
            if ((memoHdr & 0x80000000u) != 0) {
                size_t inlineLen = std::min<size_t>(len - 12u, memoBytes);
                return DecodeJetText(val + 12u, inlineLen, isJet3);
            }
            if ((memoHdr & 0x40000000u) != 0) {
                uint32_t pgRow = RdU32LE(val + 4u);
                return DecodeJetLvalSingle(rawDb, pageSize, isJet3, pgRow);
            }
            return L"[Multi-Page MEMO " + std::to_wstring(memoBytes) + L" B]";
        }
        case 0x0Fu: { // GUID / REPID (16 bytes)
            if (len < 16) return HexBytes(val, len, 16);
            swprintf_s(numBuf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                       RdU32LE(val), RdU16LE(val + 4), RdU16LE(val + 6),
                       val[8], val[9], val[10], val[11], val[12], val[13], val[14], val[15]);
            return numBuf;
        }
        case 0x10u: { // NUMERIC (17 bytes: sign + 4x32-bit BE groups)
            if (len < 17) return HexBytes(val, len, 17);
            bool neg = (val[0] == 0x80u);
            uint64_t lo = (static_cast<uint64_t>(RdU32LE(val + 9)) << 32) | RdU32LE(val + 13);
            double v = static_cast<double>(lo);
            for (uint8_t s = 0; s < col.scale && s < 28; ++s) v /= 10.0;
            if (neg) v = -v;
            swprintf_s(numBuf, L"%.12g", v);
            return numBuf;
        }
        default:
            return HexBytes(val, len, 16);
    }
}

inline bool ParseJetTdefChain(const std::vector<uint8_t>& raw, size_t pageSize, bool isJet3,
                              uint32_t startPg, JetTableDef& outTdef)
{
    size_t numPages = raw.size() / pageSize;
    if (startPg == 0 || startPg >= numPages) return false;

    std::vector<uint8_t> tdefBuf;
    tdefBuf.reserve(pageSize);
    uint32_t curPg = startPg;
    for (int hop = 0; hop < 64 && curPg != 0 && curPg < numPages; ++hop) {
        const uint8_t* pg = raw.data() + static_cast<size_t>(curPg) * pageSize;
        if (pg[0] != 0x02u) return false;
        if (hop == 0) {
            tdefBuf.insert(tdefBuf.end(), pg, pg + pageSize);
        } else {
            tdefBuf.insert(tdefBuf.end(), pg + 8u, pg + pageSize);
        }
        uint32_t nextPg = RdU32LE(pg + 4u);
        if (nextPg == curPg) break;
        curPg = nextPg;
    }

    const uint8_t* b = tdefBuf.data();
    size_t bLen = tdefBuf.size();

    uint32_t numRows = 0;
    uint8_t tableType = 0;
    uint16_t numVarCols = 0;
    uint16_t numCols = 0;
    uint32_t numRealIdx = 0;
    size_t colOff = 0;
    size_t colStride = isJet3 ? 18u : 25u;

    if (isJet3) {
        if (bLen < 43u) return false;
        numRows = RdU32LE(b + 12u);
        tableType = b[20u];
        numVarCols = RdU16LE(b + 23u);
        numCols = RdU16LE(b + 25u);
        numRealIdx = RdU32LE(b + 31u);
        if (numCols == 0u || numCols > 255u || numRealIdx > 256u) return false;
        colOff = 43u + static_cast<size_t>(numRealIdx) * 8u;
    } else {
        if (bLen < 63u) return false;
        numRows = RdU32LE(b + 16u);
        tableType = b[40u];
        numVarCols = RdU16LE(b + 43u);
        numCols = RdU16LE(b + 45u);
        numRealIdx = RdU32LE(b + 51u);
        if (numCols == 0u || numCols > 255u || numRealIdx > 256u) return false;
        colOff = 63u + static_cast<size_t>(numRealIdx) * 12u;
    }

    size_t nameOff = colOff + static_cast<size_t>(numCols) * colStride;
    if (nameOff >= bLen) return false;

    std::vector<JetColDef> cols(numCols);
    for (uint16_t i = 0; i < numCols; ++i) {
        const uint8_t* cb = b + colOff + static_cast<size_t>(i) * colStride;
        JetColDef& c = cols[i];
        c.colType = cb[0];
        if (c.colType == 0u || c.colType > 0x14u) return false;
        if (isJet3) {
            c.colId = RdU16LE(cb + 1u);
            c.varIdx = RdU16LE(cb + 3u);
            c.displayIdx = RdU16LE(cb + 5u);
            c.precision = cb[11u];
            c.scale = cb[12u];
            c.isFixed = (cb[13u] & 0x01u) != 0u;
            c.fixedOff = RdU16LE(cb + 14u);
            c.colLen = RdU16LE(cb + 16u);
        } else {
            c.colId = RdU16LE(cb + 5u);
            c.varIdx = RdU16LE(cb + 7u);
            c.displayIdx = RdU16LE(cb + 9u);
            c.precision = cb[11u];
            c.scale = cb[12u];
            c.isFixed = (cb[15u] & 0x01u) != 0u;
            c.fixedOff = RdU16LE(cb + 21u);
            c.colLen = RdU16LE(cb + 23u);
        }
    }

    size_t pos = nameOff;
    for (uint16_t i = 0; i < numCols; ++i) {
        if (isJet3) {
            if (pos + 1u > bLen) return false;
            uint8_t nlen = b[pos++];
            if (nlen == 0u || nlen > 128u || pos + nlen > bLen) return false;
            cols[i].name = DecodeJetText(b + pos, nlen, true);
            pos += nlen;
        } else {
            if (pos + 2u > bLen) return false;
            uint16_t nByteLen = RdU16LE(b + pos);
            pos += 2u;
            if (nByteLen < 2u || (nByteLen & 1u) != 0u || nByteLen > 256u || pos + nByteLen > bLen) return false;
            cols[i].name = ReadUtf16LE(b + pos, nByteLen);
            pos += nByteLen;
        }
        if (cols[i].name.empty()) return false;
    }

    std::sort(cols.begin(), cols.end(), [](const JetColDef& a, const JetColDef& bCol) {
        if (a.displayIdx != bCol.displayIdx) return a.displayIdx < bCol.displayIdx;
        return a.colId < bCol.colId;
    });

    outTdef.tdefPage = startPg;
    outTdef.numRowsHdr = numRows;
    outTdef.tableType = tableType;
    outTdef.numVarCols = numVarCols;
    outTdef.isSystem = (startPg == 2u || tableType == 0x4Eu);
    outTdef.cols = std::move(cols);
    return true;
}

inline bool DecodeSingleJetRow(const JetTableDef& tdef, const uint8_t* row, size_t rowLen,
                               bool isJet3, const std::vector<uint8_t>& rawDb, size_t pageSize,
                               std::vector<std::wstring>& outCells)
{
    size_t fixedBase = isJet3 ? 1u : 2u;
    if (rowLen <= fixedBase) return false;

    uint16_t numColsInRow = isJet3 ? static_cast<uint16_t>(row[0]) : static_cast<uint16_t>(RdU16LE(row) & 0x01FFu);
    if (numColsInRow == 0u || numColsInRow > 512u) return false;

    size_t nullLen = (static_cast<size_t>(numColsInRow) + 7u) / 8u;
    if (fixedBase + nullLen > rowLen) return false;
    const uint8_t* nullBmp = row + rowLen - nullLen;
    size_t dataLimit = rowLen - nullLen;

    uint16_t numVarInRow = 0u;
    size_t varCntPos = 0u;
    size_t jumpCnt = 0u;
    if (tdef.numVarCols > 0u) {
        if (isJet3) {
            jumpCnt = (rowLen - 1u) / 256u;
            if (dataLimit >= fixedBase + 1u + jumpCnt) {
                varCntPos = dataLimit - 1u;
                numVarInRow = row[varCntPos];
                if (varCntPos < fixedBase + jumpCnt + static_cast<size_t>(numVarInRow) + 1u) {
                    numVarInRow = 0u;
                }
            }
        } else {
            if (dataLimit >= fixedBase + 2u) {
                varCntPos = dataLimit - 2u;
                numVarInRow = RdU16LE(row + varCntPos) & 0x01FFu;
                if (varCntPos < fixedBase + static_cast<size_t>(numVarInRow + 1u) * 2u) {
                    numVarInRow = 0u;
                }
            }
        }
    }

    auto getJet3VarOffset = [&](uint16_t idx) -> size_t {
        size_t tblStart = varCntPos - jumpCnt - 1u; // index 0 offset byte
        size_t rawByte = row[tblStart - idx];
        size_t hiPage = 0u;
        for (size_t j = 0; j < jumpCnt; ++j) {
            if (row[varCntPos - 1u - j] <= idx) ++hiPage;
        }
        return (hiPage << 8) + rawByte;
    };

    outCells.clear();
    outCells.reserve(tdef.cols.size());
    for (const auto& col : tdef.cols) {
        if (col.colId >= numColsInRow) {
            outCells.push_back((col.colType == 0x01u) ? L"FALSE" : L"NULL");
            continue;
        }
        bool notNull = ((nullBmp[col.colId >> 3] >> (col.colId & 7u)) & 1u) != 0u;
        if (col.colType == 0x01u) {
            outCells.push_back(notNull ? L"TRUE" : L"FALSE");
            continue;
        }
        if (!notNull) {
            outCells.push_back(L"NULL");
            continue;
        }

        if (col.isFixed) {
            size_t fOff = fixedBase + col.fixedOff;
            if (fOff >= dataLimit) {
                outCells.push_back(L"");
                continue;
            }
            size_t avail = std::min<size_t>(col.colLen, dataLimit - fOff);
            outCells.push_back(FormatJetCellValue(col, row + fOff, avail, isJet3, rawDb, pageSize));
        } else {
            if (col.varIdx >= numVarInRow) {
                outCells.push_back(L"");
                continue;
            }
            size_t vStart = 0u, vEnd = 0u;
            if (isJet3) {
                vStart = getJet3VarOffset(col.varIdx);
                vEnd = getJet3VarOffset(static_cast<uint16_t>(col.varIdx + 1u));
            } else {
                vStart = static_cast<size_t>(RdU16LE(row + varCntPos - 2u * (col.varIdx + 1u)) & 0x1FFFu);
                vEnd = static_cast<size_t>(RdU16LE(row + varCntPos - 2u * (col.varIdx + 2u)) & 0x1FFFu);
            }
            if (vStart > vEnd || vEnd > varCntPos) {
                outCells.push_back(L"");
                continue;
            }
            outCells.push_back(FormatJetCellValue(col, row + vStart, vEnd - vStart, isJet3, rawDb, pageSize));
        }
    }
    return true;
}

inline void CollectJetTableRows(const std::vector<uint8_t>& raw, size_t pageSize, bool isJet3,
                                const JetTableDef& tdef, const std::vector<uint32_t>& dataPages,
                                std::vector<std::vector<std::wstring>>& outRows, size_t maxRows = 500)
{
    const size_t numPages = raw.size() / pageSize;
    const size_t hdrLen = isJet3 ? 10u : 14u;
    const uint16_t offMask = isJet3 ? 0x0FFFu : 0x1FFFu;

    for (uint32_t pgIdx : dataPages) {
        if (pgIdx >= numPages || outRows.size() >= maxRows) break;
        const uint8_t* pg = raw.data() + static_cast<size_t>(pgIdx) * pageSize;
        if (pg[0] != 0x01u) continue;
        uint16_t nRows = RdU16LE(pg + (isJet3 ? 8u : 12u));
        if (hdrLen + static_cast<size_t>(nRows) * 2u > pageSize) continue;
        size_t minRowDataOff = hdrLen + static_cast<size_t>(nRows) * 2u;

        for (uint16_t r = 0; r < nRows && outRows.size() < maxRows; ++r) {
            uint16_t rawOff = RdU16LE(pg + hdrLen + static_cast<size_t>(r) * 2u);
            if ((rawOff & 0x8000u) != 0u) continue; // Deleted row
            size_t rStart = static_cast<size_t>(rawOff & offMask);
            size_t rEnd = (r == 0u) ? pageSize : static_cast<size_t>(RdU16LE(pg + hdrLen + static_cast<size_t>(r - 1u) * 2u) & offMask);
            if (rStart < minRowDataOff || rStart >= rEnd || rEnd > pageSize) continue;

            const uint8_t* rowPtr = pg + rStart;
            size_t rowLen = rEnd - rStart;

            // Follow single-hop overflow row pointer (0x4000)
            if ((rawOff & 0x4000u) != 0u) {
                if (rowLen != 4u) continue;
                uint32_t ovPgRow = RdU32LE(rowPtr);
                uint32_t ovPg = ovPgRow >> 8;
                uint32_t ovRow = ovPgRow & 0xFFu;
                if (ovPg == 0u || ovPg >= numPages) continue;
                const uint8_t* opg = raw.data() + static_cast<size_t>(ovPg) * pageSize;
                if (opg[0] != 0x01u) continue;
                uint16_t onRows = RdU16LE(opg + (isJet3 ? 8u : 12u));
                if (ovRow >= onRows || hdrLen + static_cast<size_t>(onRows) * 2u > pageSize) continue;
                uint16_t orawOff = RdU16LE(opg + hdrLen + ovRow * 2u);
                if ((orawOff & 0xC000u) != 0u) continue;
                size_t os = static_cast<size_t>(orawOff & offMask);
                size_t oe = (ovRow == 0u) ? pageSize : static_cast<size_t>(RdU16LE(opg + hdrLen + (ovRow - 1u) * 2u) & offMask);
                if (os >= oe || oe > pageSize) continue;
                rowPtr = opg + os;
                rowLen = oe - os;
            }

            std::vector<std::wstring> cells;
            if (DecodeSingleJetRow(tdef, rowPtr, rowLen, isJet3, raw, pageSize, cells)) {
                outRows.push_back(std::move(cells));
            }
        }
    }
}

} // namespace DetailDocs200

inline bool ParseAccessDatabase200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if (raw.size() < 32) return false;
    bool isAce = (std::memcmp(raw.data() + 4, "Standard ACE DB", 15) == 0);
    bool isJet = (std::memcmp(raw.data() + 4, "Standard Jet DB", 15) == 0);
    if (!isAce && !isJet) return false;

    uint32_t verCode = DetailDocs200::RdU32LE(raw.data() + 0x14);
    bool isJet3 = (isJet && verCode == 0);
    size_t pageSize = isJet3 ? 2048u : 4096u;
    if (raw.size() < pageSize * 2u) return false;

    size_t numPages = raw.size() / pageSize;
    std::wstring eng = isAce ? (L"Access ACE Engine (v" + std::to_wstring(verCode) + L", 4 KB Pages)")
                             : (isJet3 ? L"Jet 3.5 (Access 97, 2 KB Pages)" : L"Jet 4.0 (Access 2000-2003, 4 KB Pages)");

    // Identify continuation TDEF pages and group DATA pages by owning TDEF page
    std::vector<bool> isContTdef(numPages, false);
    std::vector<std::pair<uint32_t, uint32_t>> ownerDataPairs;
    for (size_t pg = 1; pg < numPages; ++pg) {
        const uint8_t* p = raw.data() + pg * pageSize;
        if (p[0] == 0x02u) {
            uint32_t nextPg = DetailDocs200::RdU32LE(p + 4u);
            if (nextPg > 0u && nextPg < numPages && nextPg != pg) {
                isContTdef[nextPg] = true;
            }
        } else if (p[0] == 0x01u && std::memcmp(p + 4u, "LVAL", 4) != 0) {
            uint32_t ownerTdef = DetailDocs200::RdU32LE(p + 4u);
            if (ownerTdef > 0u && ownerTdef < numPages) {
                ownerDataPairs.push_back({ ownerTdef, static_cast<uint32_t>(pg) });
            }
        }
    }

    std::vector<DetailDocs200::JetTableDef> tables;
    int msysIdx = -1;
    for (uint32_t pg = 1; pg < static_cast<uint32_t>(numPages); ++pg) {
        if (raw[static_cast<size_t>(pg) * pageSize] != 0x02u || isContTdef[pg]) continue;
        DetailDocs200::JetTableDef td;
        if (DetailDocs200::ParseJetTdefChain(raw, pageSize, isJet3, pg, td)) {
            if (pg == 2u) {
                td.name = L"MSysObjects";
                td.isSystem = true;
                msysIdx = static_cast<int>(tables.size());
            }
            tables.push_back(std::move(td));
        }
    }

    // Reject header-only, encrypted, or corrupt databases that have zero valid TDEF schemas
    if (tables.empty()) return false;

    auto getDataPagesFor = [&](uint32_t tdefPg) {
        std::vector<uint32_t> pgs;
        for (const auto& kv : ownerDataPairs) {
            if (kv.first == tdefPg) pgs.push_back(kv.second);
        }
        return pgs;
    };

    // Read MSysObjects (TDEF page 2) to resolve real user & system table names
    if (msysIdx >= 0) {
        const auto& msys = tables[static_cast<size_t>(msysIdx)];
        int idCol = -1, nameCol = -1, typeCol = -1, flagsCol = -1;
        for (size_t ci = 0; ci < msys.cols.size(); ++ci) {
            std::wstring cname = PathUtils::ToLower(msys.cols[ci].name);
            if (cname == L"id") idCol = static_cast<int>(ci);
            else if (cname == L"name") nameCol = static_cast<int>(ci);
            else if (cname == L"type") typeCol = static_cast<int>(ci);
            else if (cname == L"flags") flagsCol = static_cast<int>(ci);
        }
        if (idCol >= 0 && nameCol >= 0) {
            std::vector<std::vector<std::wstring>> catRows;
            DetailDocs200::CollectJetTableRows(raw, pageSize, isJet3, msys, getDataPagesFor(2u), catRows, 1000);
            for (const auto& r : catRows) {
                if (static_cast<size_t>(std::max(idCol, nameCol)) >= r.size()) continue;
                const std::wstring& nm = r[static_cast<size_t>(nameCol)];
                if (nm.empty() || nm == L"NULL") continue;
                if (typeCol >= 0 && static_cast<size_t>(typeCol) < r.size()) {
                    wchar_t* eptr = nullptr;
                    long tp = std::wcstol(r[static_cast<size_t>(typeCol)].c_str(), &eptr, 10);
                    if (eptr != r[static_cast<size_t>(typeCol)].c_str() && tp != 1) continue;
                }
                wchar_t* idEnd = nullptr;
                long long rawId = std::wcstoll(r[static_cast<size_t>(idCol)].c_str(), &idEnd, 10);
                if (idEnd == r[static_cast<size_t>(idCol)].c_str()) continue;
                uint32_t tdefPg = static_cast<uint32_t>(rawId) & 0x00FFFFFFu;

                uint32_t flg = 0u;
                if (flagsCol >= 0 && static_cast<size_t>(flagsCol) < r.size()) {
                    wchar_t* fEnd = nullptr;
                    flg = static_cast<uint32_t>(std::wcstoll(r[static_cast<size_t>(flagsCol)].c_str(), &fEnd, 10));
                }

                for (auto& td : tables) {
                    if (td.tdefPage == tdefPg) {
                        td.name = nm;
                        td.isSystem = (tdefPg == 2u || nm.rfind(L"MSys", 0) == 0 ||
                                       nm.rfind(L"USys", 0) == 0 || (flg & 0x80000002u) != 0u);
                        break;
                    }
                }
            }
        }
    }

    for (auto& td : tables) {
        if (td.name.empty()) {
            td.name = (td.isSystem ? L"SysTable_Page" : L"Table_Page") + std::to_wstring(td.tdefPage);
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Access Database (" + ext + L")";

    SpreadsheetSheet schemaSh;
    schemaSh.name = L"Access_Schema";
    schemaSh.maxCols = 5;
    schemaSh.rows.push_back({ L"Table Name", L"Kind", L"TDEF Page", L"Columns", L"Decoded Rows" });

    size_t userSheetsEmitted = 0;
    for (const auto& td : tables) {
        std::vector<std::vector<std::wstring>> dataRows;
        DetailDocs200::CollectJetTableRows(raw, pageSize, isJet3, td, getDataPagesFor(td.tdefPage), dataRows, 500);

        schemaSh.rows.push_back({
            td.name,
            td.isSystem ? L"System Table" : L"User Table",
            std::to_wstring(td.tdefPage),
            std::to_wstring(td.cols.size()),
            std::to_wstring(dataRows.size())
        });

        if (!td.isSystem) {
            SpreadsheetSheet tsh;
            tsh.name = td.name;
            tsh.maxCols = std::max<size_t>(1u, td.cols.size());
            std::vector<std::wstring> hdrRow;
            hdrRow.reserve(td.cols.size());
            for (const auto& c : td.cols) hdrRow.push_back(c.name);
            tsh.rows.push_back(std::move(hdrRow));
            for (auto& dr : dataRows) tsh.rows.push_back(std::move(dr));
            out.sheets.push_back(std::move(tsh));
            ++userSheetsEmitted;
        }
    }

    // If database only contains system catalog tables (blank DB), expose MSysObjects too
    if (userSheetsEmitted == 0 && msysIdx >= 0) {
        const auto& msys = tables[static_cast<size_t>(msysIdx)];
        SpreadsheetSheet msh;
        msh.name = msys.name;
        msh.maxCols = std::max<size_t>(1u, msys.cols.size());
        std::vector<std::wstring> hdrRow;
        for (const auto& c : msys.cols) hdrRow.push_back(c.name);
        msh.rows.push_back(std::move(hdrRow));
        std::vector<std::vector<std::wstring>> catRows;
        DetailDocs200::CollectJetTableRows(raw, pageSize, isJet3, msys, getDataPagesFor(2u), catRows, 200);
        for (auto& dr : catRows) msh.rows.push_back(std::move(dr));
        out.sheets.push_back(std::move(msh));
    }

    schemaSh.rows.push_back({ L"[Engine]", eng, std::to_wstring(pageSize) + L" B/page", std::to_wstring(numPages) + L" pages", L"" });
    out.sheets.push_back(std::move(schemaSh));
    out.valid = true;
    return true;
}


} // namespace Formats
