#pragma once
#include "../ByteIO.h"
// ExtendedDocs200.h — 20 Databases, Protobuf, Cloud/SecOps, Crypto/Forensics, Chiptune, MusicXML & Game/Doc Formats (#81–#100)
// Covers:
//   Databases & KV (#81–#85): .accdb/.mdb, .rdb (Redis), .sst/.ldb (RocksDB/LevelDB), .mbtiles/.pmtiles, .pb/.pbf (Protobuf)
//   Cloud & SecOps (#86–#91): .tfstate, .sarif, .spdx/.cdx, .gpg/.pgp/.sig, .jks, .cat
//   Windows Forensics (#92–#94): .hiv (+ NTUSER.DAT "regf"), .etl, .pol
//   Audio/Photo/Game/Office (#95–#100): .sid/.nsf/.spc/.vgm, .musicxml/.mxl, .xmp, .uasset/.umap, .prefab/.unity, .pages/.numbers/.key/.vsdx

namespace Formats {

namespace DetailDocs200 {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline std::wstring HexBytes(const uint8_t* p, size_t n, size_t maxBytes = 16) { return PathUtils::HexBytes(p, n, L' ', maxBytes); }

inline std::wstring ReadUtf16LE(const uint8_t* p, size_t byteLen) {
    std::wstring out;
    size_t chars = byteLen / 2;
    out.reserve(chars);
    for (size_t i = 0; i < chars; ++i) {
        wchar_t wc = static_cast<wchar_t>(RdU16LE(p + i * 2));
        if (wc == 0) break;
        out.push_back(wc);
    }
    return out;
}

inline bool ReadVarint64(const uint8_t* data, size_t size, size_t& pos, uint64_t& outVal) { return ::Explorer2::ByteIO::RdLeb128(data, size, pos, outVal, 10); }

inline bool IsMostlyPrintableUtf8(const uint8_t* p, size_t n) {
    if (n == 0) return false;
    size_t good = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c == 0) return false;
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\r' || c == '\n' || c >= 0x80) ++good;
    }
    return good * 10 >= n * 9;
}

inline std::vector<uint8_t> DecodeBase64Block(std::string_view in) {
    std::vector<uint8_t> out;
    ::Explorer2::Base64::Decode(in, out);
    return out;
}

} // namespace DetailDocs200

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

// ============================================================================
// #82. Redis RDB Memory Snapshot (.rdb)
// ============================================================================
namespace DetailDocs200 {

inline bool ReadRdbLen(const uint8_t* data, size_t size, size_t& pos, uint64_t& outLen, bool& outIsEncoded) {
    outIsEncoded = false;
    if (pos >= size) return false;
    uint8_t first = data[pos++];
    uint8_t type = (first >> 6) & 0x03u;
    if (type == 0) {
        outLen = first & 0x3Fu;
        return true;
    }
    if (type == 1) {
        if (pos >= size) return false;
        outLen = (static_cast<uint64_t>(first & 0x3Fu) << 8) | data[pos++];
        return true;
    }
    if (type == 2) {
        if (first == 0x80) {
            if (pos + 4 > size) return false;
            outLen = RdU32BE(data + pos);
            pos += 4;
            return true;
        }
        if (first == 0x81) {
            if (pos + 8 > size) return false;
            outLen = RdU64BE(data + pos);
            pos += 8;
            return true;
        }
        return false;
    }
    outIsEncoded = true;
    outLen = first & 0x3Fu;
    return true;
}

inline bool ReadRdbString(const uint8_t* data, size_t size, size_t& pos, std::wstring& outStr) {
    uint64_t len = 0;
    bool isEnc = false;
    if (!ReadRdbLen(data, size, pos, len, isEnc)) return false;
    if (!isEnc) {
        if (len > size - pos) return false;
        size_t show = std::min<size_t>(static_cast<size_t>(len), 256);
        if (IsMostlyPrintableUtf8(data + pos, show)) {
            outStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(data + pos), show));
        } else {
            outStr = HexBytes(data + pos, static_cast<size_t>(len), 16);
        }
        pos += static_cast<size_t>(len);
        return true;
    }
    if (len == 0) { // ENC_INT8
        if (pos + 1 > size) return false;
        outStr = std::to_wstring(static_cast<int8_t>(data[pos++]));
        return true;
    }
    if (len == 1) { // ENC_INT16
        if (pos + 2 > size) return false;
        outStr = std::to_wstring(static_cast<int16_t>(RdU16LE(data + pos)));
        pos += 2;
        return true;
    }
    if (len == 2) { // ENC_INT32
        if (pos + 4 > size) return false;
        outStr = std::to_wstring(RdI32LE(data + pos));
        pos += 4;
        return true;
    }
    if (len == 3) { // ENC_LZF compressed string
        uint64_t clen = 0, ulen = 0;
        bool dummy = false;
        if (!ReadRdbLen(data, size, pos, clen, dummy) || !ReadRdbLen(data, size, pos, ulen, dummy)) return false;
        if (clen > size - pos) return false;
        outStr = L"[LZF " + std::to_wstring(clen) + L"B -> " + std::to_wstring(ulen) + L"B]";
        pos += static_cast<size_t>(clen);
        return true;
    }
    return false;
}

} // namespace DetailDocs200

inline bool ParseRedisRdb200(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 9 || std::memcmp(raw.data(), "REDIS", 5) != 0) return false;
    std::string verStr(reinterpret_cast<const char*>(raw.data() + 5), 4);
    for (char c : verStr) if (c < '0' || c > '9') return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Redis RDB Memory Snapshot (v" + PathUtils::Utf8ToWide(verStr) + L")";

    SpreadsheetSheet keysSh;
    keysSh.name = L"Redis_Keys";
    keysSh.maxCols = 5;
    keysSh.rows.push_back({ L"DB", L"Key", L"Value Type", L"Preview / Elements", L"TTL" });

    SpreadsheetSheet auxSh;
    auxSh.name = L"RDB_Metadata";
    auxSh.maxCols = 2;
    auxSh.rows.push_back({ L"AUX / Header Field", L"Value" });
    auxSh.rows.push_back({ L"RDB Format Version", PathUtils::Utf8ToWide(verStr) });
    auxSh.rows.push_back({ L"Snapshot Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });

    size_t pos = 9;
    uint64_t curDb = 0;
    std::wstring pendingTtl = L"Persistent";

    while (pos < raw.size() && keysSh.rows.size() < 500) {
        uint8_t op = raw[pos++];
        if (op == 0xFF) break; // EOF (+ optional 8-byte CRC64)
        if (op == 0xFA) { // AUX
            std::wstring k, v;
            if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, k) ||
                !DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, v)) break;
            auxSh.rows.push_back({ k, v });
            continue;
        }
        if (op == 0xFE) { // SELECTDB
            bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, curDb, enc)) break;
            continue;
        }
        if (op == 0xFB) { // RESIZEDB
            uint64_t dbSz = 0, expSz = 0; bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, dbSz, enc) ||
                !DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, expSz, enc)) break;
            auxSh.rows.push_back({ L"DB #" + std::to_wstring(curDb) + L" Hash Table Size",
                                   std::to_wstring(dbSz) + L" keys (" + std::to_wstring(expSz) + L" expiring)" });
            continue;
        }
        if (op == 0xFD) { // EXPIRETIME (sec)
            if (pos + 4 > raw.size()) break;
            pendingTtl = std::to_wstring(DetailDocs200::RdU32LE(raw.data() + pos)) + L" s";
            pos += 4;
            continue;
        }
        if (op == 0xFC) { // EXPIRETIMEMS (ms)
            if (pos + 8 > raw.size()) break;
            pendingTtl = std::to_wstring(DetailDocs200::RdU64LE(raw.data() + pos)) + L" ms";
            pos += 8;
            continue;
        }

        // Value type opcode (0=String, 1=List, 2=Set, 3=ZSet, 4=Hash)
        std::wstring key;
        if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, key)) break;

        if (op == 0x00) {
            std::wstring val;
            if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, val)) break;
            keysSh.rows.push_back({ std::to_wstring(curDb), key, L"STRING", val, pendingTtl });
        } else if (op == 0x01 || op == 0x02) {
            uint64_t cnt = 0; bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc)) break;
            std::wstring preview;
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring elem;
                if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, elem)) { pos = raw.size(); break; }
                if (i < 4) { if (i) preview += L", "; preview += elem; }
            }
            if (cnt > 4) preview += L" ... (" + std::to_wstring(cnt) + L" items)";
            keysSh.rows.push_back({ std::to_wstring(curDb), key, (op == 1) ? L"LIST" : L"SET", preview, pendingTtl });
        } else if (op == 0x04) {
            uint64_t cnt = 0; bool enc = false;
            if (!DetailDocs200::ReadRdbLen(raw.data(), raw.size(), pos, cnt, enc)) break;
            std::wstring preview;
            for (uint64_t i = 0; i < cnt; ++i) {
                std::wstring hk, hv;
                if (!DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, hk) ||
                    !DetailDocs200::ReadRdbString(raw.data(), raw.size(), pos, hv)) { pos = raw.size(); break; }
                if (i < 3) { if (i) preview += L", "; preview += hk + L"=" + hv; }
            }
            keysSh.rows.push_back({ std::to_wstring(curDb), key, L"HASH", preview, pendingTtl });
        } else {
            keysSh.rows.push_back({ std::to_wstring(curDb), key, L"TYPE_" + std::to_wstring(op), L"(Encoded Structure)", pendingTtl });
            break;
        }
        pendingTtl = L"Persistent";
    }

    out.sheets.push_back(std::move(keysSh));
    out.sheets.push_back(std::move(auxSh));
    out.valid = true;
    return true;
}

// ============================================================================
// #83. RocksDB / LevelDB Sorted String Table (.sst / .ldb)
// ============================================================================
// The footer names the index block, whose entries are the handles of the data blocks; each data block (stored
// plain, or Snappy / LZ4 / Zstandard compressed) is read in turn and its prefix-compressed entries become rows,
// internal keys split into user key, sequence number and operation. RocksDB format 4+ index values are
// delta-encoded handles. PlainTable files have no blocks: only their footer is shown.
inline bool ParseRocksLevelDbSst200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    (void)ext;
    // Bytes past those handed in (RocksDB tables are often 64 MB) are read from the file itself
    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();
    auto readRange = [&](uint64_t off, uint64_t len, std::vector<uint8_t>& buf) -> const uint8_t* {
        return (len > 64u * 1024 * 1024) ? nullptr : source.Read(off, static_cast<size_t>(len), buf);
    };
    if (fileSize < 48) return false;
    std::vector<uint8_t> footerBuf;
    const size_t footLen = static_cast<size_t>(std::min<uint64_t>(fileSize, 53));
    const uint8_t* foot = readRange(fileSize - footLen, footLen, footerBuf);
    if (!foot) return false;
    const uint8_t* footEnd = foot + footLen;
    const uint64_t footerMagic = DetailDocs200::RdU64LE(footEnd - 8);
    constexpr uint64_t kLevelDbMagic = 0xDB4775248B80FB57ULL;      // LevelDB, RocksDB legacy block-based
    constexpr uint64_t kBlockBasedMagic = 0x88E241B785F4CFF7ULL;   // RocksDB block-based
    constexpr uint64_t kPlainMagic = 0x8242229663BF9564ULL;        // RocksDB PlainTable
    constexpr uint64_t kLegacyPlainMagic = 0x4F3418EB7A8F13B8ULL;
    const bool isLevelDb = footerMagic == kLevelDbMagic;
    const bool isBlockBased = footerMagic == kBlockBasedMagic;
    const bool isPlain = footerMagic == kPlainMagic || footerMagic == kLegacyPlainMagic;
    if (!isLevelDb && !isBlockBased && !isPlain) return false;
    if ((isBlockBased || isPlain) && footLen < 53) return false;

    const std::wstring fmt = isLevelDb ? L"LevelDB Table (0xDB4775248B80FB57)"
                           : isBlockBased ? L"RocksDB Block-Based SSTable (0x88E241B785F4CFF7)"
                                          : L"RocksDB PlainTable SST";
    // LevelDB footer: two handles in 40 bytes + magic. RocksDB: checksum type, the handles, format version, magic.
    const uint32_t formatVersion = isLevelDb ? 0u : DetailDocs200::RdU32LE(footEnd - 12);
    const uint8_t* handles = isLevelDb ? footEnd - 48 : footEnd - 52; // 40 bytes either way
    size_t fpos = 0;
    uint64_t metaOff = 0, metaSz = 0, idxOff = 0, idxSz = 0;
    const bool handlesOk = DetailDocs200::ReadVarint64(handles, 40, fpos, metaOff) &&
                           DetailDocs200::ReadVarint64(handles, 40, fpos, metaSz) &&
                           DetailDocs200::ReadVarint64(handles, 40, fpos, idxOff) &&
                           DetailDocs200::ReadVarint64(handles, 40, fpos, idxSz);

    // A block's contents, decompressed; the 5-byte trailer (type + checksum) follows the stored bytes
    std::map<int, size_t> unreadByType;
    auto readBlock = [&](uint64_t off, uint64_t size, std::vector<uint8_t>& store, const uint8_t*& data, size_t& len) -> bool {
        if (size > 64u * 1024 * 1024) return false;
        std::vector<uint8_t> fileBytes;
        const uint8_t* src = readRange(off, size + 5, fileBytes);
        if (!src) return false;
        const int ctype = src[static_cast<size_t>(size)];
        if (ctype == 0) {
            if (!fileBytes.empty() && src == fileBytes.data()) { // keep bytes read from the file alive
                store = std::move(fileBytes);
                src = store.data();
            }
            data = src;
            len = static_cast<size_t>(size);
            return true;
        }
        bool ok = false;
        if (ctype == 1) {
            ok = ::Explorer2::StreamCodecs::SnappyDecompress(src, static_cast<size_t>(size), store, 64 * 1024 * 1024);
        } else if (ctype == 4 || ctype == 5 || ctype == 7) { // LZ4 / LZ4HC / ZSTD: format 2+ puts the size first
            size_t p = 0;
            uint64_t expect = 0;
            const bool sized = formatVersion >= 2 && DetailDocs200::ReadVarint64(src, static_cast<size_t>(size), p, expect) && expect <= 64u * 1024 * 1024;
            if (ctype == 7) {
                ok = ::Explorer2::StreamCodecs::DecompressZstdFrame(src + (sized ? p : 0), static_cast<size_t>(size) - (sized ? p : 0), store, 64 * 1024 * 1024);
            } else if (sized) {
                ok = ::Explorer2::Lz4::DecodeBlockExact(src + p, static_cast<size_t>(size) - p, static_cast<size_t>(expect), store);
            }
        }
        if (!ok) {
            ++unreadByType[ctype];
            return false;
        }
        data = store.data();
        len = store.size();
        return true;
    };
    // The restart array closing a block: its entries end where it starts
    auto entriesEnd = [](const uint8_t* b, size_t n, std::vector<uint32_t>* restarts) -> size_t {
        if (n < 4) return SIZE_MAX;
        uint32_t nr = DetailDocs200::RdU32LE(b + n - 4);
        if ((nr & 0x80000000u) != 0 || nr == 0 || 4ull * (nr + 1) > n) return SIZE_MAX; // (a hash-indexed block is not read)
        size_t end = n - 4 * (static_cast<size_t>(nr) + 1);
        if (restarts) {
            restarts->resize(nr);
            for (uint32_t i = 0; i < nr; ++i) (*restarts)[i] = DetailDocs200::RdU32LE(b + end + 4 * i);
        }
        return end;
    };

    // Data block handles from the index block
    std::vector<std::pair<uint64_t, uint64_t>> dataBlocks;
    if (handlesOk && !isPlain) {
        std::vector<uint8_t> store;
        const uint8_t* b = nullptr;
        size_t n = 0;
        std::vector<uint32_t> restarts;
        size_t end = readBlock(idxOff, idxSz, store, b, n) ? entriesEnd(b, n, &restarts) : SIZE_MAX;
        const bool valueDelta = isBlockBased && formatVersion >= 4;
        size_t pos = 0, ri = 0;
        uint64_t prevOff = 0, prevSize = 0;
        while (end != SIZE_MAX && pos < end && dataBlocks.size() < 100000) {
            const bool restart = ri < restarts.size() && restarts[ri] == pos;
            if (restart) ++ri;
            uint64_t shared = 0, nonShared = 0, valLen = 0;
            if (!DetailDocs200::ReadVarint64(b, end, pos, shared) || !DetailDocs200::ReadVarint64(b, end, pos, nonShared)) break;
            if (!valueDelta && !DetailDocs200::ReadVarint64(b, end, pos, valLen)) break;
            if (nonShared > end - pos) break;
            pos += static_cast<size_t>(nonShared);
            uint64_t off = 0, size = 0;
            size_t vpos = pos;
            if (!valueDelta || restart) {
                if (!DetailDocs200::ReadVarint64(b, end, vpos, off) || !DetailDocs200::ReadVarint64(b, end, vpos, size)) break;
            } else {
                uint64_t zz = 0;
                if (!DetailDocs200::ReadVarint64(b, end, vpos, zz)) break;
                const int64_t delta = static_cast<int64_t>(zz >> 1) ^ -static_cast<int64_t>(zz & 1);
                off = prevOff + prevSize + 5;
                size = static_cast<uint64_t>(static_cast<int64_t>(prevSize) + delta);
            }
            if (valueDelta) {
                pos = vpos;
            } else {
                if (valLen > end - pos) break;
                pos += static_cast<size_t>(valLen);
            }
            if (off >= fileSize || size > fileSize - off) break;
            dataBlocks.push_back({ off, size });
            prevOff = off;
            prevSize = size;
        }
    }

    SpreadsheetSheet kvSh;
    kvSh.name = L"SST_KeyValues";
    kvSh.maxCols = 4;
    kvSh.rows.push_back({ L"User Key", L"Op / Type", L"Sequence #", L"Value Preview" });
    auto printable = [](const uint8_t* p, size_t n) { return DetailDocs200::IsMostlyPrintableUtf8(p, n); };
    size_t entries = 0, blocksRead = 0;
    for (const auto& h : dataBlocks) {
        std::vector<uint8_t> store;
        const uint8_t* b = nullptr;
        size_t n = 0;
        if (!readBlock(h.first, h.second, store, b, n)) continue;
        const size_t end = entriesEnd(b, n, nullptr);
        if (end == SIZE_MAX) continue;
        ++blocksRead;
        std::string key;
        size_t pos = 0;
        while (pos < end) {
            uint64_t shared = 0, nonShared = 0, valLen = 0;
            if (!DetailDocs200::ReadVarint64(b, end, pos, shared) || !DetailDocs200::ReadVarint64(b, end, pos, nonShared) ||
                !DetailDocs200::ReadVarint64(b, end, pos, valLen)) break;
            if (shared > key.size() || nonShared > end - pos || valLen > end - pos - nonShared) break;
            key.resize(static_cast<size_t>(shared));
            key.append(reinterpret_cast<const char*>(b + pos), static_cast<size_t>(nonShared));
            pos += static_cast<size_t>(nonShared);
            const uint8_t* val = b + pos;
            pos += static_cast<size_t>(valLen);
            ++entries;
            if (kvSh.rows.size() > 5000) continue;

            // Internal key: user key + 8 bytes (sequence << 8 | type)
            std::wstring userKey, op = L"-", seq = L"-";
            size_t userLen = key.size();
            if (key.size() >= 8) {
                const uint64_t tag = DetailDocs200::RdU64LE(reinterpret_cast<const uint8_t*>(key.data() + key.size() - 8));
                const uint32_t type = static_cast<uint32_t>(tag & 0xFFu);
                static const struct { uint32_t t; const wchar_t* name; } kOps[] = {
                    { 0x0, L"Delete" }, { 0x1, L"Put" }, { 0x2, L"Merge" }, { 0x7, L"SingleDelete" },
                    { 0xF, L"RangeDelete" }, { 0x11, L"BlobIndex" }, { 0x14, L"DeleteWithTimestamp" }, { 0x16, L"WideColumns" },
                };
                for (const auto& o : kOps) {
                    if (o.t == type) { op = o.name; break; }
                }
                if (op != L"-") {
                    userLen = key.size() - 8;
                    seq = std::to_wstring(tag >> 8);
                }
            }
            const uint8_t* uk = reinterpret_cast<const uint8_t*>(key.data());
            userKey = printable(uk, userLen) ? PathUtils::Utf8ToWide(std::string_view(key.data(), userLen))
                                             : DetailDocs200::HexBytes(uk, userLen, 32);
            const size_t vShow = std::min<size_t>(static_cast<size_t>(valLen), 160);
            std::wstring valText = printable(val, vShow) ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), vShow))
                                                         : DetailDocs200::HexBytes(val, static_cast<size_t>(valLen), 24);
            kvSh.rows.push_back({ userKey, op, seq, valText });
        }
    }

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"SST_Footer";
    hdrSh.maxCols = 2;
    hdrSh.rows.push_back({ L"SSTable Field", L"Value" });
    hdrSh.rows.push_back({ L"Table Format", fmt });
    if (!isLevelDb) hdrSh.rows.push_back({ L"Format Version", std::to_wstring(formatVersion) });
    hdrSh.rows.push_back({ L"File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });
    if (handlesOk) {
        hdrSh.rows.push_back({ L"Metaindex Block Handle", L"offset=" + std::to_wstring(metaOff) + L", size=" + std::to_wstring(metaSz) });
        hdrSh.rows.push_back({ L"Index Block Handle", L"offset=" + std::to_wstring(idxOff) + L", size=" + std::to_wstring(idxSz) });
    }
    if (isPlain) {
        hdrSh.rows.push_back({ L"Entries", L"PlainTable layout: entries are not listed" });
    } else {
        hdrSh.rows.push_back({ L"Data Blocks", std::to_wstring(dataBlocks.size()) + L" in the index, " + std::to_wstring(blocksRead) + L" read" });
        hdrSh.rows.push_back({ L"Entries", std::to_wstring(entries) + (entries + 1 > kvSh.rows.size() ? L" (first " + std::to_wstring(kvSh.rows.size() - 1) + L" listed)" : L"") });
        static const struct { int t; const wchar_t* name; } kCodecs[] = {
            { 1, L"Snappy" }, { 2, L"zlib" }, { 3, L"bzip2" }, { 4, L"LZ4" }, { 5, L"LZ4HC" }, { 6, L"XPRESS" }, { 7, L"Zstandard" },
        };
        for (const auto& u : unreadByType) {
            std::wstring codec = L"type " + std::to_wstring(u.first);
            for (const auto& k : kCodecs) {
                if (k.t == u.first) codec = k.name;
            }
            hdrSh.rows.push_back({ L"Blocks Not Decoded", std::to_wstring(u.second) + L" (" + codec + L")" });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = fmt;
    if (source.ReadPastHead() && !source.ReadFailed()) out.sourceReadFully = true;
    if (kvSh.rows.size() > 1) out.sheets.push_back(std::move(kvSh));
    out.sheets.push_back(std::move(hdrSh));
    out.valid = true;
    return true;
}

// ============================================================================
// #84. PMTiles v3 & MBTiles Vector/Raster Map Archives (.pmtiles / .mbtiles)
// ============================================================================
inline bool ParseMapTiles200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // PMTiles v3 ("PMTiles\x03", 127-byte header)
    if ((raw.size() >= 127 && std::memcmp(raw.data(), "PMTiles\x03", 8) == 0) || ext == L".pmtiles") {
        if (raw.size() < 127 || std::memcmp(raw.data(), "PMTiles", 7) != 0) return false;
        const uint8_t* h = raw.data();
        uint64_t addressedTiles = DetailDocs200::RdU64LE(h + 72);
        uint64_t tileEntries    = DetailDocs200::RdU64LE(h + 80);
        uint64_t tileContents   = DetailDocs200::RdU64LE(h + 88);
        uint8_t tileComp        = h[98];
        uint8_t tileType        = h[99];
        uint8_t minZoom         = h[100];
        uint8_t maxZoom         = h[101];
        double minLon = DetailDocs200::RdI32LE(h + 102) * 1e-7;
        double minLat = DetailDocs200::RdI32LE(h + 106) * 1e-7;
        double maxLon = DetailDocs200::RdI32LE(h + 110) * 1e-7;
        double maxLat = DetailDocs200::RdI32LE(h + 114) * 1e-7;
        uint8_t centerZoom = h[118];
        double centerLon = DetailDocs200::RdI32LE(h + 119) * 1e-7;
        double centerLat = DetailDocs200::RdI32LE(h + 123) * 1e-7;

        const wchar_t* tTypeName = (tileType == 1) ? L"MVT (Mapbox Vector Tile)" :
                                   (tileType == 2) ? L"PNG Raster" :
                                   (tileType == 3) ? L"JPEG Raster" :
                                   (tileType == 4) ? L"WebP Raster" :
                                   (tileType == 5) ? L"AVIF Raster" : L"Unknown";
        const wchar_t* compName  = (tileComp == 1) ? L"None" : (tileComp == 2) ? L"Gzip" :
                                   (tileComp == 3) ? L"Brotli" : (tileComp == 4) ? L"Zstandard" : L"Unknown";

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Protomaps PMTiles v3 Cloud-Native Tile Archive (.pmtiles)";

        SpreadsheetSheet sh;
        sh.name = L"PMTiles_Header";
        sh.maxCols = 4;
        // Latitude + Longitude columns enable interactive Direct2D Map View via ExtractGeoTrackFromSheets!
        sh.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        sh.rows.push_back({ L"SW Corner", L"Bounding Box SW", std::to_wstring(minLat), std::to_wstring(minLon) });
        sh.rows.push_back({ L"NW Corner", L"Bounding Box NW", std::to_wstring(maxLat), std::to_wstring(minLon) });
        sh.rows.push_back({ L"NE Corner", L"Bounding Box NE", std::to_wstring(maxLat), std::to_wstring(maxLon) });
        sh.rows.push_back({ L"SE Corner", L"Bounding Box SE", std::to_wstring(minLat), std::to_wstring(maxLon) });
        sh.rows.push_back({ L"SW Close",  L"Bounding Box Close", std::to_wstring(minLat), std::to_wstring(minLon) });
        sh.rows.push_back({ L"Map Center (z" + std::to_wstring(centerZoom) + L")", tTypeName, std::to_wstring(centerLat), std::to_wstring(centerLon) });
        sh.rows.push_back({ L"Zoom Range", L"z" + std::to_wstring(minZoom) + L" .. z" + std::to_wstring(maxZoom), L"", L"" });
        sh.rows.push_back({ L"Tile Payload Type", tTypeName, L"", L"" });
        sh.rows.push_back({ L"Tile Compression", compName, L"", L"" });
        sh.rows.push_back({ L"Addressed Tiles", std::to_wstring(addressedTiles), L"", L"" });
        sh.rows.push_back({ L"Unique Tile Blobs", std::to_wstring(tileContents) + L" (" + std::to_wstring(tileEntries) + L" Hilbert runs)", L"", L"" });

        uint64_t jsonOff = DetailDocs200::RdU64LE(h + 24);
        uint64_t jsonLen = DetailDocs200::RdU64LE(h + 32);
        if (jsonLen > 0 && jsonOff + jsonLen <= raw.size()) {
            const uint8_t* jp = raw.data() + static_cast<size_t>(jsonOff);
            size_t jn = static_cast<size_t>(std::min<uint64_t>(jsonLen, 512));
            if (DetailDocs200::IsMostlyPrintableUtf8(jp, jn)) {
                sh.rows.push_back({ L"JSON Metadata", PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(jp), jn)), L"", L"" });
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // MBTiles (SQLite3 B-Tree container with metadata + tiles/map/images tables)
    if (ext == L".mbtiles") {
        if (raw.size() < 100 || std::memcmp(raw.data(), "SQLite format 3\0", 16) != 0) {
            return false;
        }
        ParsedOfficeDocument sqlDoc;
        sqlDoc.filePath = out.filePath;
        sqlDoc.sourceFileSize = out.sourceFileSize;
        if (!ParseSqliteDatabase(raw, sqlDoc) || !sqlDoc.valid || sqlDoc.sheets.empty()) {
            return false;
        }
        out.sourceReadFully = sqlDoc.sourceReadFully;

        const SpreadsheetSheet* metaSheet = nullptr;
        const SpreadsheetSheet* tilesSheet = nullptr;
        const SpreadsheetSheet* mapSheet = nullptr;
        for (const auto& shItem : sqlDoc.sheets) {
            std::wstring sname = PathUtils::ToLower(shItem.name);
            if (sname == L"metadata") metaSheet = &shItem;
            else if (sname == L"tiles") tilesSheet = &shItem;
            else if (sname == L"map") mapSheet = &shItem;
        }
        if (!metaSheet && !tilesSheet && !mapSheet) {
            return false;
        }

        auto splitCsvDoubles = [](const std::wstring& s, std::vector<double>& outVals) {
            outVals.clear();
            size_t start = 0;
            while (start <= s.size()) {
                size_t comma = s.find(L',', start);
                std::wstring tok = (comma == std::wstring::npos) ? s.substr(start) : s.substr(start, comma - start);
                wchar_t* endPtr = nullptr;
                double v = std::wcstod(tok.c_str(), &endPtr);
                if (endPtr == tok.c_str() || !std::isfinite(v)) return false;
                outVals.push_back(v);
                if (comma == std::wstring::npos) break;
                start = comma + 1u;
            }
            return !outVals.empty();
        };

        std::wstring tsName, tsFormat, tsMinZoom, tsMaxZoom, tsType, tsVersion, tsDesc, tsAttrib;
        double minLon = 0.0, minLat = 0.0, maxLon = 0.0, maxLat = 0.0;
        bool hasBounds = false;
        double centerLon = 0.0, centerLat = 0.0;
        int centerZoom = -1;
        bool hasCenter = false;

        if (metaSheet && !metaSheet->rows.empty()) {
            size_t nameCol = 0, valCol = 1;
            const auto& hdrRow = metaSheet->rows[0];
            for (size_t c = 0; c < hdrRow.size(); ++c) {
                std::wstring cn = PathUtils::ToLower(hdrRow[c]);
                if (cn == L"name") nameCol = c;
                else if (cn == L"value") valCol = c;
            }
            for (size_t r = 1; r < metaSheet->rows.size(); ++r) {
                const auto& row = metaSheet->rows[r];
                if (std::max(nameCol, valCol) >= row.size()) continue;
                std::wstring k = PathUtils::ToLower(row[nameCol]);
                const std::wstring& v = row[valCol];
                if (k == L"name") tsName = v;
                else if (k == L"format") tsFormat = v;
                else if (k == L"minzoom") tsMinZoom = v;
                else if (k == L"maxzoom") tsMaxZoom = v;
                else if (k == L"type") tsType = v;
                else if (k == L"version") tsVersion = v;
                else if (k == L"description") tsDesc = v;
                else if (k == L"attribution") tsAttrib = v;
                else if (k == L"bounds") {
                    std::vector<double> b;
                    if (splitCsvDoubles(v, b) && b.size() == 4 &&
                        std::fabs(b[0]) <= 180.0 && std::fabs(b[1]) <= 90.0 &&
                        std::fabs(b[2]) <= 180.0 && std::fabs(b[3]) <= 90.0 &&
                        b[0] <= b[2] && b[1] <= b[3])
                    {
                        minLon = b[0]; minLat = b[1]; maxLon = b[2]; maxLat = b[3];
                        hasBounds = true;
                    }
                } else if (k == L"center") {
                    std::vector<double> c;
                    if (splitCsvDoubles(v, c) && c.size() >= 2 &&
                        std::fabs(c[0]) <= 180.0 && std::fabs(c[1]) <= 90.0)
                    {
                        centerLon = c[0];
                        centerLat = c[1];
                        if (c.size() >= 3 && c[2] >= 0.0 && c[2] <= 30.0) {
                            centerZoom = static_cast<int>(std::llround(c[2]));
                        }
                        hasCenter = true;
                    }
                }
            }
        }

        // Inspect real decoded tiles (or map) B-Tree rows and derive TMS -> WGS84 bounds if needed
        const SpreadsheetSheet* tileGrid = tilesSheet ? tilesSheet : mapSheet;
        size_t decodedTiles = 0;
        int obsMinZ = 99, obsMaxZ = -1;
        double tMinLon = 180.0, tMinLat = 90.0, tMaxLon = -180.0, tMaxLat = -90.0;
        bool hasDerivedTileBounds = false;

        if (tileGrid && !tileGrid->rows.empty()) {
            int zCol = -1, xCol = -1, yCol = -1;
            const auto& thdr = tileGrid->rows[0];
            for (size_t c = 0; c < thdr.size(); ++c) {
                std::wstring cn = PathUtils::ToLower(thdr[c]);
                if (cn == L"zoom_level") zCol = static_cast<int>(c);
                else if (cn == L"tile_column") xCol = static_cast<int>(c);
                else if (cn == L"tile_row") yCol = static_cast<int>(c);
            }
            if (zCol < 0 && xCol < 0 && yCol < 0 && !metaSheet) {
                return false;
            }
            if (zCol >= 0 && xCol >= 0 && yCol >= 0) {
                constexpr double kPi = 3.14159265358979323846;
                for (size_t r = 1; r < tileGrid->rows.size(); ++r) {
                    const auto& row = tileGrid->rows[r];
                    size_t maxIdx = static_cast<size_t>(std::max({ zCol, xCol, yCol }));
                    if (maxIdx >= row.size()) continue;
                    wchar_t *ez = nullptr, *ex = nullptr, *ey = nullptr;
                    long z = std::wcstol(row[static_cast<size_t>(zCol)].c_str(), &ez, 10);
                    long long x = std::wcstoll(row[static_cast<size_t>(xCol)].c_str(), &ex, 10);
                    long long yTms = std::wcstoll(row[static_cast<size_t>(yCol)].c_str(), &ey, 10);
                    if (ez == row[static_cast<size_t>(zCol)].c_str() ||
                        ex == row[static_cast<size_t>(xCol)].c_str() ||
                        ey == row[static_cast<size_t>(yCol)].c_str()) continue;
                    if (z < 0 || z > 28) continue;
                    long long nInt = 1LL << z;
                    if (x < 0 || x >= nInt || yTms < 0 || yTms >= nInt) continue;

                    ++decodedTiles;
                    obsMinZ = std::min(obsMinZ, static_cast<int>(z));
                    obsMaxZ = std::max(obsMaxZ, static_cast<int>(z));

                    double n = static_cast<double>(nInt);
                    double yXyzNorth = static_cast<double>((nInt - 1LL) - yTms);
                    double yXyzSouth = yXyzNorth + 1.0;
                    double lonW = (static_cast<double>(x) / n) * 360.0 - 180.0;
                    double lonE = (static_cast<double>(x + 1LL) / n) * 360.0 - 180.0;
                    double latN = std::atan(std::sinh(kPi * (1.0 - 2.0 * yXyzNorth / n))) * (180.0 / kPi);
                    double latS = std::atan(std::sinh(kPi * (1.0 - 2.0 * yXyzSouth / n))) * (180.0 / kPi);

                    tMinLon = std::min(tMinLon, lonW);
                    tMaxLon = std::max(tMaxLon, lonE);
                    tMinLat = std::min(tMinLat, latS);
                    tMaxLat = std::max(tMaxLat, latN);
                    hasDerivedTileBounds = true;
                }
            }
        }

        if (!hasBounds && hasDerivedTileBounds) {
            minLon = tMinLon;
            minLat = tMinLat;
            maxLon = tMaxLon;
            maxLat = tMaxLat;
            hasBounds = true;
        }
        if (!hasCenter && hasBounds) {
            centerLon = (minLon + maxLon) * 0.5;
            centerLat = (minLat + maxLat) * 0.5;
            hasCenter = true;
        }

        uint16_t pageSzRaw = DetailDocs200::RdU16BE(raw.data() + 16);
        uint32_t pageSz = (pageSzRaw == 1) ? 65536u : pageSzRaw;
        uint32_t pageCnt = DetailDocs200::RdU32BE(raw.data() + 28);

        auto fmtCoord = [](double c) {
            wchar_t b[32] = {};
            swprintf_s(b, L"%.6f", c);
            return std::wstring(b);
        };

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Mapbox MBTiles SQLite Map Archive (.mbtiles)";

        SpreadsheetSheet ov;
        ov.name = L"MBTiles_Overview";
        ov.maxCols = 4;
        ov.rows.push_back({ L"Property", L"Value", L"Latitude", L"Longitude" });
        if (hasBounds) {
            std::wstring bLabel = tsName.empty() ? L"MBTiles Bounds" : tsName;
            ov.rows.push_back({ L"SW Corner", bLabel, fmtCoord(minLat), fmtCoord(minLon) });
            ov.rows.push_back({ L"NW Corner", bLabel, fmtCoord(maxLat), fmtCoord(minLon) });
            ov.rows.push_back({ L"NE Corner", bLabel, fmtCoord(maxLat), fmtCoord(maxLon) });
            ov.rows.push_back({ L"SE Corner", bLabel, fmtCoord(minLat), fmtCoord(maxLon) });
            ov.rows.push_back({ L"SW Close",  bLabel, fmtCoord(minLat), fmtCoord(minLon) });
        }
        if (hasCenter) {
            std::wstring cProp = (centerZoom >= 0) ? (L"Map Center (z" + std::to_wstring(centerZoom) + L")") : L"Map Center";
            ov.rows.push_back({ cProp, tsFormat.empty() ? L"MBTiles Center" : tsFormat, fmtCoord(centerLat), fmtCoord(centerLon) });
        }
        if (!tsName.empty())    ov.rows.push_back({ L"Tileset Name", tsName, L"", L"" });
        if (!tsFormat.empty())  ov.rows.push_back({ L"Tile Format", tsFormat, L"", L"" });
        if (!tsMinZoom.empty() || !tsMaxZoom.empty()) {
            ov.rows.push_back({ L"Zoom Range", L"z" + (tsMinZoom.empty() ? L"?" : tsMinZoom) + L" .. z" + (tsMaxZoom.empty() ? L"?" : tsMaxZoom), L"", L"" });
        } else if (obsMaxZ >= 0) {
            ov.rows.push_back({ L"Observed Zoom Range", L"z" + std::to_wstring(obsMinZ) + L" .. z" + std::to_wstring(obsMaxZ), L"", L"" });
        }
        if (!tsType.empty())    ov.rows.push_back({ L"Layer Type", tsType, L"", L"" });
        if (!tsVersion.empty()) ov.rows.push_back({ L"Tileset Version", tsVersion, L"", L"" });
        if (!tsDesc.empty())    ov.rows.push_back({ L"Description", tsDesc, L"", L"" });
        if (!tsAttrib.empty())  ov.rows.push_back({ L"Attribution", tsAttrib, L"", L"" });
        ov.rows.push_back({ L"Decoded Tile Rows", std::to_wstring(decodedTiles), L"", L"" });
        ov.rows.push_back({ L"SQLite Page Size", std::to_wstring(pageSz) + L" B", L"", L"" });
        ov.rows.push_back({ L"Database Pages", std::to_wstring(pageCnt), L"", L"" });

        out.sheets.push_back(std::move(ov));
        for (auto& sqlSh : sqlDoc.sheets) {
            out.sheets.push_back(std::move(sqlSh));
        }
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #85. Schema-less Protobuf Wire-Format Inspector (.pb / .pbf)
// ============================================================================
namespace DetailDocs200 {

inline bool WalkProtobuf(const uint8_t* data, size_t size, const std::wstring& prefix, int depth,
                         SpreadsheetSheet* outSheet, size_t& ioRows) {
    if (size == 0 || depth > 4) return false;
    size_t pos = 0;
    size_t fields = 0;
    while (pos < size) {
        uint64_t tag = 0;
        if (!ReadVarint64(data, size, pos, tag) || tag == 0) return false;
        uint64_t fieldNum = tag >> 3;
        uint8_t wireType = static_cast<uint8_t>(tag & 0x07u);
        if (fieldNum == 0 || fieldNum > 536870911ULL) return false;

        std::wstring fpath = prefix.empty() ? (L"#" + std::to_wstring(fieldNum))
                                            : (prefix + L"." + std::to_wstring(fieldNum));
        if (wireType == 0) { // Varint
            uint64_t v = 0;
            if (!ReadVarint64(data, size, pos, v)) return false;
            int64_t zigzag = static_cast<int64_t>((v >> 1) ^ (~(v & 1ULL) + 1ULL));
            if (outSheet && ioRows < 400) {
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"0 (Varint)",
                    std::to_wstring(v) + L" (sint: " + std::to_wstring(zigzag) + L")" });
                ++ioRows;
            }
        } else if (wireType == 1) { // 64-bit
            if (pos + 8 > size) return false;
            uint64_t u = RdU64LE(data + pos);
            double d = 0.0; std::memcpy(&d, data + pos, 8);
            pos += 8;
            if (outSheet && ioRows < 400) {
                wchar_t buf[96] = {};
                swprintf_s(buf, L"0x%016llX (double: %.6g)", static_cast<unsigned long long>(u), d);
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"1 (Fixed64)", buf });
                ++ioRows;
            }
        } else if (wireType == 2) { // Length-delimited
            uint64_t len = 0;
            if (!ReadVarint64(data, size, pos, len) || len > size - pos) return false;
            const uint8_t* sub = data + pos;
            size_t subLen = static_cast<size_t>(len);
            pos += subLen;

            if (IsMostlyPrintableUtf8(sub, subLen)) {
                if (outSheet && ioRows < 400) {
                    size_t show = std::min<size_t>(subLen, 160);
                    outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (String)",
                        L"\"" + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sub), show)) + L"\"" });
                    ++ioRows;
                }
            } else if (subLen >= 2 && depth < 4 && WalkProtobuf(sub, subLen, fpath, depth + 1, nullptr, ioRows)) {
                if (outSheet && ioRows < 400) {
                    outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (Submessage)",
                        L"{" + std::to_wstring(subLen) + L" bytes}" });
                    ++ioRows;
                }
                WalkProtobuf(sub, subLen, fpath, depth + 1, outSheet, ioRows);
            } else if (outSheet && ioRows < 400) {
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"2 (Bytes)",
                    L"[" + std::to_wstring(subLen) + L" B] " + HexBytes(sub, subLen, 16) });
                ++ioRows;
            }
        } else if (wireType == 5) { // 32-bit
            if (pos + 4 > size) return false;
            uint32_t u = RdU32LE(data + pos);
            float f = 0.0f; std::memcpy(&f, data + pos, 4);
            pos += 4;
            if (outSheet && ioRows < 400) {
                wchar_t buf[64] = {};
                swprintf_s(buf, L"0x%08X (float: %.5g)", u, f);
                outSheet->rows.push_back({ fpath, std::to_wstring(fieldNum), L"5 (Fixed32)", buf });
                ++ioRows;
            }
        } else {
            return false; // Deprecated group or invalid wire type
        }
        ++fields;
    }
    return fields > 0;
}

} // namespace DetailDocs200

inline bool ParseProtobufWire200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    if ((ext != L".pb" && ext != L".pbf") || raw.size() < 2) return false;

    SpreadsheetSheet sh;
    sh.name = L"Protobuf_Wire";
    sh.maxCols = 4;
    sh.rows.push_back({ L"Field Path", L"Tag #", L"Wire Type", L"Decoded Value" });

    size_t rows = 0;
    // OSM .pbf starts with 4-byte BE BlobHeader length followed by Protobuf BlobHeader
    if (ext == L".pbf" && raw.size() > 16) {
        uint32_t hdrLen = DetailDocs200::RdU32BE(raw.data());
        if (hdrLen > 4 && hdrLen < 65536 && 4 + hdrLen <= raw.size()) {
            if (DetailDocs200::WalkProtobuf(raw.data() + 4, hdrLen, L"BlobHeader", 0, &sh, rows)) {
                out.kind = OfficeFileKind::SqliteDb;
                out.isSpreadsheet = true;
                out.formatTitle = L"OpenStreetMap / Binary PBF Protobuf Stream (.pbf)";
                out.sheets.push_back(std::move(sh));
                out.valid = true;
                return true;
            }
        }
    }

    if (!DetailDocs200::WalkProtobuf(raw.data(), raw.size(), L"", 0, &sh, rows)) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Protocol Buffers Wire-Format Message (" + ext + L")";
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// #86–#88. Cloud & SecOps JSON/Tag Formats (.tfstate, .sarif, .spdx, .cdx)
//   Read with the shared JSON parser (CycloneDX XML with the XML scanner, SPDX tag-value line by line). The big
//   arrays (resources, results, components, packages) are streamed, so a document cut short by the read limit
//   still lists everything before the cut, and is marked incomplete.
// ============================================================================
namespace DetailCloudSecOps {

using JVal = ::Explorer2::Json::Value;
using JParser = ::Explorer2::Json::Parser;
constexpr size_t kMaxRows = 5000;

inline std::wstring W(std::string_view s) { return PathUtils::Utf8ToWide(s); }

// A value as cell text: strings as they are, numbers / booleans as written, arrays and objects compacted
inline std::wstring Cell(const JVal* v) {
    if (!v || v->IsNull()) return {};
    if (v->IsString() || v->IsNumber()) return W(v->strVal);
    if (v->IsBool()) return v->boolVal ? L"true" : L"false";
    return W(::Explorer2::Json::Compact(*v, 200));
}

inline const JVal* Path(const JVal* v, std::initializer_list<std::string_view> keys) {
    for (std::string_view k : keys) {
        if (!v) return nullptr;
        v = v->Find(k);
    }
    return v;
}

inline std::string Str(const JVal* v) { return (v && v->IsString()) ? v->strVal : std::string(); }

inline ::Explorer2::Json::Limits StreamLimits() {
    ::Explorer2::Json::Limits limits;
    limits.maxNodes = 4000000; // per streamed element, and for the members kept whole
    return limits;
}

inline void NoteIncomplete(SpreadsheetSheet& summary, bool complete, const wchar_t* what) {
    if (!complete) {
        summary.rows.push_back({ L"Document", std::wstring(L"incomplete (cut short or damaged); the ") + what +
                                              L" before that point are listed" });
    }
}

inline void FinishSheet(SpreadsheetSheet& sh) {
    sh.maxCols = 0;
    for (const auto& r : sh.rows) sh.maxCols = std::max(sh.maxCols, r.size());
}

// ---- Terraform state (v4: resources[] with instances[]; v1-v3: modules[] with a resources map) ----
inline bool ParseTerraformState(std::string_view text, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh;
    sh.name = L"Resources";
    sh.rows.push_back({ L"Address", L"Mode", L"Type", L"Name", L"Provider", L"ID", L"Module" });
    size_t resources = 0, instances = 0;
    std::map<std::string, size_t> byType;

    // provider["registry.terraform.io/hashicorp/aws"].west -> registry.terraform.io/hashicorp/aws (west)
    auto providerName = [](const std::string& p) {
        size_t q1 = p.find('"');
        size_t q2 = (q1 == std::string::npos) ? std::string::npos : p.find('"', q1 + 1);
        if (q2 == std::string::npos) return W(p);
        std::wstring s = W(std::string_view(p).substr(q1 + 1, q2 - q1 - 1));
        size_t alias = p.find("].", q2);
        if (alias != std::string::npos) s += L" (" + W(std::string_view(p).substr(alias + 2)) + L")";
        return s;
    };
    auto addRow = [&](std::string addr, const std::string& mode, const std::string& type, const std::string& name,
                      const std::string& provider, std::wstring id, const std::string& module) {
        ++byType[type.empty() ? std::string("(none)") : type];
        if (sh.rows.size() > kMaxRows) return;
        sh.rows.push_back({ W(addr), W(mode), W(type), W(name), providerName(provider), std::move(id), W(module) });
    };
    auto handleResource = [&](const JVal& r) {
        if (!r.IsObject()) return;
        ++resources;
        std::string mode = r.GetString("mode"), type = r.GetString("type"), name = r.GetString("name");
        std::string module = r.GetString("module"), provider = r.GetString("provider");
        std::string base = module.empty() ? std::string() : module + ".";
        if (mode == "data") base += "data.";
        base += type + "." + name;
        const JVal* insts = r.Find("instances");
        if (!insts || !insts->IsArray() || insts->arrVal.empty()) {
            addRow(base, mode, type, name, provider, {}, module);
            return;
        }
        for (const auto& in : insts->arrVal) {
            ++instances;
            std::string addr = base;
            if (const JVal* key = in.Find("index_key")) {
                if (key->IsString()) addr += "[\"" + key->strVal + "\"]";
                else if (key->IsNumber()) addr += "[" + key->strVal + "]";
            }
            addRow(addr, mode, type, name, provider, Cell(Path(&in, { "attributes", "id" })), module);
        }
    };

    JParser parser(text, StreamLimits());
    JVal root;
    root.kind = JVal::Kind::Object;
    bool sawResources = false;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "resources" && !sawResources && parser.Peek() == '[') {
            sawResources = true;
            return parser.StreamArray([&](JVal&& r) { handleResource(r); });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        root.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();

    // Legacy states: modules[].resources is a map of address -> { type, provider, primary: { id } }
    if (const JVal* mods = root.Find("modules"); mods && mods->IsArray()) {
        sawResources = true;
        for (const auto& m : mods->arrVal) {
            std::string module;
            if (const JVal* path = m.Find("path"); path && path->IsArray()) {
                for (const auto& seg : path->arrVal) {
                    if (!seg.IsString() || seg.strVal == "root") continue;
                    module += (module.empty() ? "module." : ".module.") + seg.strVal;
                }
            }
            const JVal* res = m.Find("resources");
            if (!res || !res->IsObject()) continue;
            for (const auto& kv : res->objVal) {
                ++resources;
                ++instances;
                const std::string& addr = kv.first;
                std::string mode = (addr.rfind("data.", 0) == 0) ? "data" : "managed";
                std::string type = kv.second.GetString("type");
                size_t lastDot = addr.rfind('.');
                std::string name = (lastDot == std::string::npos) ? addr : addr.substr(lastDot + 1);
                addRow(module.empty() ? addr : module + "." + addr, mode, type, name, kv.second.GetString("provider"),
                       Cell(Path(&kv.second, { "primary", "id" })), module);
            }
        }
    }
    if (!sawResources && !root.Find("terraform_version") && !root.Find("lineage")) return false;
    if (!complete && resources == 0) return false;

    std::string tfVer = root.GetString("terraform_version");
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Terraform Infrastructure State (.tfstate" + (tfVer.empty() ? L"" : L" — v" + W(tfVer)) + L")";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));

    SpreadsheetSheet summary;
    summary.name = L"State";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"State format version", Cell(root.Find("version")) });
    summary.rows.push_back({ L"Terraform version", W(tfVer) });
    summary.rows.push_back({ L"Serial", Cell(root.Find("serial")) });
    summary.rows.push_back({ L"Lineage", Cell(root.Find("lineage")) });
    summary.rows.push_back({ L"Resources", std::to_wstring(resources) });
    summary.rows.push_back({ L"Instances", std::to_wstring(instances) });
    for (const auto& tc : byType) summary.rows.push_back({ L"Type: " + W(tc.first), std::to_wstring(tc.second) });
    if (const JVal* outputs = root.Find("outputs"); outputs && outputs->IsObject()) {
        for (const auto& kv : outputs->objVal) {
            bool sensitive = kv.second.GetBool("sensitive", false);
            summary.rows.push_back({ L"Output: " + W(kv.first), sensitive ? L"(sensitive)" : Cell(kv.second.Find("value")) });
        }
    }
    NoteIncomplete(summary, complete, L"resources");
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

// ---- SARIF 2.1.0 (runs[] with tool.driver.rules and results[]) ----

// "{0} is unused" + arguments -> "x is unused"
inline std::string ExpandSarifArguments(const std::string& tmpl, const JVal* args) {
    if (!args || !args->IsArray()) return tmpl;
    std::string s;
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '{') {
            size_t close = tmpl.find('}', i + 1);
            if (close != std::string::npos && close > i + 1 && close - i <= 4) {
                size_t idx = 0;
                bool digits = true;
                for (size_t k = i + 1; k < close; ++k) {
                    if (tmpl[k] < '0' || tmpl[k] > '9') { digits = false; break; }
                    idx = idx * 10 + static_cast<size_t>(tmpl[k] - '0');
                }
                if (digits && idx < args->arrVal.size()) {
                    s += args->arrVal[idx].IsString() ? args->arrVal[idx].strVal : ::Explorer2::Json::Compact(args->arrVal[idx], 80);
                    i = close;
                    continue;
                }
            }
        }
        s.push_back(tmpl[i]);
    }
    return s;
}

inline bool ParseSarifLog(std::string_view text, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh;
    sh.name = L"SARIF_Findings";
    sh.rows.push_back({ L"Level", L"Rule", L"File", L"Line", L"Message", L"Tool" });
    std::map<std::string, size_t> levelCounts;
    std::vector<std::wstring> runTools;
    size_t results = 0;

    struct RunCtx {
        JVal tool, artifacts;
        bool haveTool = false;
        std::wstring toolName;
        std::map<std::string, const JVal*, std::less<>> rulesById;
        const JVal* rules = nullptr;
    };
    auto setTool = [](RunCtx& ctx, JVal&& tool) {
        ctx.tool = std::move(tool);
        ctx.haveTool = true;
        const JVal* driver = ctx.tool.Find("driver");
        std::string name = driver ? driver->GetString("name") : std::string();
        std::string ver = driver ? driver->GetString("semanticVersion", driver->GetString("version")) : std::string();
        ctx.toolName = W(name) + (ver.empty() ? L"" : L" " + W(ver));
        ctx.rules = driver ? driver->Find("rules") : nullptr;
        if (ctx.rules && ctx.rules->IsArray()) {
            for (const auto& r : ctx.rules->arrVal) {
                std::string id = r.GetString("id");
                if (!id.empty()) ctx.rulesById.emplace(std::move(id), &r);
            }
        }
    };
    auto ruleFor = [](const RunCtx& ctx, const JVal& res) -> const JVal* {
        if (!ctx.rules || !ctx.rules->IsArray()) return nullptr;
        int idx = res.GetInt("ruleIndex", -1);
        if (idx < 0) {
            if (const JVal* rr = res.Find("rule")) idx = rr->GetInt("index", -1);
        }
        if (idx >= 0 && static_cast<size_t>(idx) < ctx.rules->arrVal.size()) return &ctx.rules->arrVal[static_cast<size_t>(idx)];
        std::string id = res.GetString("ruleId");
        if (id.empty()) {
            if (const JVal* rr = res.Find("rule")) id = rr->GetString("id");
        }
        auto it = ctx.rulesById.find(id);
        return (it == ctx.rulesById.end()) ? nullptr : it->second;
    };
    auto handleResult = [&](const RunCtx& ctx, const JVal& r, bool addRow) {
        if (!r.IsObject()) return;
        ++results;
        const JVal* rule = ruleFor(ctx, r);
        // SARIF 3.27.10: the result's level, else "none" for kinds other than fail, else the rule's default, else warning
        std::string level = r.GetString("level");
        if (level.empty()) {
            std::string kind = r.GetString("kind");
            if (!kind.empty() && kind != "fail") level = "none";
            else if (rule) level = Str(Path(rule, { "defaultConfiguration", "level" }));
            if (level.empty()) level = "warning";
        }
        ++levelCounts[level];
        if (!addRow || sh.rows.size() > kMaxRows) return;

        std::string ruleId = r.GetString("ruleId");
        if (ruleId.empty()) ruleId = Str(Path(&r, { "rule", "id" }));
        if (ruleId.empty() && rule) ruleId = rule->GetString("id");
        std::string msg;
        if (const JVal* m = r.Find("message"); m && m->IsObject()) {
            msg = m->GetString("text");
            if (msg.empty() && rule) {
                std::string mid = m->GetString("id");
                if (!mid.empty()) msg = Str(Path(rule, { "messageStrings", mid, "text" }));
            }
            msg = ExpandSarifArguments(msg, m->Find("arguments"));
        }
        std::string uri, line;
        if (const JVal* locs = r.Find("locations"); locs && locs->IsArray() && !locs->arrVal.empty()) {
            const JVal& loc = locs->arrVal[0];
            if (const JVal* pl = loc.Find("physicalLocation")) {
                if (const JVal* al = pl->Find("artifactLocation")) {
                    uri = al->GetString("uri");
                    int ai = al->GetInt("index", -1);
                    if (uri.empty() && ai >= 0 && ctx.artifacts.IsArray() && static_cast<size_t>(ai) < ctx.artifacts.arrVal.size()) {
                        uri = Str(Path(&ctx.artifacts.arrVal[static_cast<size_t>(ai)], { "location", "uri" }));
                    }
                }
                if (const JVal* reg = pl->Find("region")) {
                    int sl = reg->GetInt("startLine", -1), sc = reg->GetInt("startColumn", -1);
                    if (sl > 0) line = std::to_string(sl) + (sc > 0 ? ":" + std::to_string(sc) : std::string());
                }
            }
            if (uri.empty()) {
                if (const JVal* ll = loc.Find("logicalLocations"); ll && ll->IsArray() && !ll->arrVal.empty()) {
                    uri = ll->arrVal[0].GetString("fullyQualifiedName", ll->arrVal[0].GetString("name"));
                }
            }
        }
        for (char& c : msg) {
            if (c == '\r' || c == '\n') c = ' ';
        }
        sh.rows.push_back({ W(level), W(ruleId), W(uri), W(line), W(msg), ctx.toolName });
    };

    JParser parser(text, StreamLimits());
    JVal root;
    root.kind = JVal::Kind::Object;
    bool sawRuns = false;
    size_t runCount = 0;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "runs" && !sawRuns && parser.Peek() == '[') {
            sawRuns = true;
            return parser.StreamArrayItems([&]() {
                if (parser.Peek() != '{') {
                    JVal skipped;
                    return parser.ParseValue(skipped);
                }
                ++runCount;
                RunCtx ctx;
                std::vector<JVal> pending; // results seen before the run's tool (rules) was
                bool ok = parser.StreamObject([&](const std::string& rk) {
                    if (rk == "results" && parser.Peek() == '[') {
                        return parser.StreamArray([&](JVal&& r) {
                            if (ctx.haveTool) handleResult(ctx, r, true);
                            else if (pending.size() < kMaxRows) pending.push_back(std::move(r));
                            else handleResult(ctx, r, false);
                        });
                    }
                    JVal v;
                    if (!parser.ParseValue(v)) return false;
                    if (rk == "tool" && !ctx.haveTool) setTool(ctx, std::move(v));
                    else if (rk == "artifacts") ctx.artifacts = std::move(v);
                    return true;
                }, 2);
                for (const auto& r : pending) handleResult(ctx, r, true);
                runTools.push_back(ctx.toolName);
                return ok;
            });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        root.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();
    if (!sawRuns || (!complete && results == 0)) return false;

    std::wstring firstTool = runTools.empty() ? std::wstring() : runTools[0];
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"OASIS SARIF Static Analysis Report (" + (firstTool.empty() ? std::wstring(L".sarif") : firstTool) + L")";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));

    SpreadsheetSheet summary;
    summary.name = L"SARIF_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"SARIF version", W(root.GetString("version")) });
    summary.rows.push_back({ L"Runs", std::to_wstring(runCount) });
    for (size_t i = 0; i < runTools.size() && i < 50; ++i) {
        summary.rows.push_back({ L"Run " + std::to_wstring(i + 1) + L" tool", runTools[i].empty() ? L"(unnamed)" : runTools[i] });
    }
    summary.rows.push_back({ L"Results", std::to_wstring(results) + (results > kMaxRows ? L" (first " + std::to_wstring(kMaxRows) + L" listed)" : L"") });
    for (const auto& lc : levelCounts) summary.rows.push_back({ L"Level: " + W(lc.first), std::to_wstring(lc.second) });
    NoteIncomplete(summary, complete, L"results");
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

// ---- Software bills of materials: CycloneDX (JSON / XML) and SPDX (JSON / tag-value) ----
struct SbomRow {
    std::string name, version, type, license, locator, supplier;
};

inline void PushSbomRow(SpreadsheetSheet& sh, const SbomRow& r) {
    if (sh.rows.size() > kMaxRows) return;
    sh.rows.push_back({ W(r.name), W(r.version), W(r.type), W(r.license), W(r.locator), W(r.supplier) });
}

inline SpreadsheetSheet NewSbomSheet() {
    SpreadsheetSheet sh;
    sh.name = L"SBOM_Inventory";
    sh.rows.push_back({ L"Component / Package", L"Version", L"Type", L"License", L"PURL / Source", L"Supplier" });
    return sh;
}

inline void AppendJoined(std::string& dst, const std::string& part, const char* sep) {
    if (part.empty()) return;
    if (!dst.empty()) dst += sep;
    dst += part;
}

// CycloneDX component (JSON); nested components follow it
inline void CycloneDxJsonComponent(const JVal& c, SpreadsheetSheet& sh, size_t& count, int depth) {
    if (!c.IsObject() || depth > 16) return;
    ++count;
    SbomRow r;
    std::string group = c.GetString("group");
    r.name = group.empty() ? c.GetString("name") : group + "/" + c.GetString("name");
    r.version = c.GetString("version");
    r.type = c.GetString("type");
    if (const JVal* lics = c.Find("licenses"); lics && lics->IsArray()) {
        for (const auto& l : lics->arrVal) {
            std::string one = l.GetString("expression");
            if (one.empty()) {
                const JVal* lic = l.Find("license");
                one = lic ? lic->GetString("id", lic->GetString("name")) : std::string();
            }
            AppendJoined(r.license, one, ", ");
        }
    }
    r.locator = c.GetString("purl");
    r.supplier = Str(Path(&c, { "supplier", "name" }));
    if (r.supplier.empty()) r.supplier = c.GetString("publisher", c.GetString("author"));
    PushSbomRow(sh, r);
    if (const JVal* nested = c.Find("components"); nested && nested->IsArray()) {
        for (const auto& n : nested->arrVal) CycloneDxJsonComponent(n, sh, count, depth + 1);
    }
}

// SPDX 2.x package (JSON)
inline void SpdxJsonPackage(const JVal& p, SpreadsheetSheet& sh, size_t& count) {
    if (!p.IsObject()) return;
    ++count;
    SbomRow r;
    r.name = p.GetString("name");
    r.version = p.GetString("versionInfo");
    r.type = p.GetString("primaryPackagePurpose");
    r.license = p.GetString("licenseConcluded");
    if (r.license.empty() || r.license == "NOASSERTION" || r.license == "NONE") {
        std::string declared = p.GetString("licenseDeclared");
        if (!declared.empty() && declared != "NOASSERTION") r.license = declared;
    }
    if (const JVal* refs = p.Find("externalRefs"); refs && refs->IsArray()) {
        for (const auto& ref : refs->arrVal) {
            if (ref.GetString("referenceType") == "purl") {
                r.locator = ref.GetString("referenceLocator");
                break;
            }
        }
    }
    if (r.locator.empty()) {
        std::string dl = p.GetString("downloadLocation");
        if (dl != "NOASSERTION" && dl != "NONE") r.locator = dl;
    }
    r.supplier = p.GetString("supplier");
    PushSbomRow(sh, r);
}

inline bool ParseSbomJson(std::string_view text, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh = NewSbomSheet();
    size_t count = 0;
    JParser parser(text, StreamLimits());
    JVal root;
    root.kind = JVal::Kind::Object;
    bool sawComponents = false, sawPackages = false;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (key == "components" && !sawComponents && parser.Peek() == '[') {
            sawComponents = true;
            return parser.StreamArray([&](JVal&& c) { CycloneDxJsonComponent(c, sh, count, 0); });
        }
        if (key == "packages" && !sawPackages && parser.Peek() == '[') {
            sawPackages = true;
            return parser.StreamArray([&](JVal&& p) { SpdxJsonPackage(p, sh, count); });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        root.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();

    std::string bomFormat = root.GetString("bomFormat"), spdxVersion = root.GetString("spdxVersion");
    bool isCdx = bomFormat == "CycloneDX" || (bomFormat.empty() && sawComponents && !complete);
    bool isSpdx = spdxVersion.rfind("SPDX-", 0) == 0 || (spdxVersion.empty() && sawPackages && !complete);
    if (isCdx == isSpdx) return false; // neither (or a document claiming to be both)
    if (!complete && count == 0) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = isCdx ? L"CycloneDX Software Bill of Materials (.cdx)" : L"Linux Foundation SPDX SBOM (.spdx)";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));

    SpreadsheetSheet summary;
    summary.name = L"SBOM_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    if (isCdx) {
        summary.rows.push_back({ L"Specification", L"CycloneDX " + W(root.GetString("specVersion")) });
        summary.rows.push_back({ L"Serial number", W(root.GetString("serialNumber")) });
        if (const JVal* subject = Path(&root, { "metadata", "component" })) {
            summary.rows.push_back({ L"Subject", W(subject->GetString("name")) + L" " + W(subject->GetString("version")) });
        }
        std::string created = Str(Path(&root, { "metadata", "timestamp" }));
        if (!created.empty()) summary.rows.push_back({ L"Created", W(created) });
        // metadata.tools: an array of tools (1.4) or { components: [...] } (1.5+)
        if (const JVal* tools = Path(&root, { "metadata", "tools" })) {
            const JVal* list = tools->IsArray() ? tools : tools->Find("components");
            std::string names;
            if (list && list->IsArray()) {
                for (const auto& t : list->arrVal) AppendJoined(names, t.GetString("name") + (t.GetString("version").empty() ? "" : " " + t.GetString("version")), ", ");
            }
            if (!names.empty()) summary.rows.push_back({ L"Tools", W(names) });
        }
        summary.rows.push_back({ L"Components", std::to_wstring(count) });
        if (const JVal* deps = root.Find("dependencies"); deps && deps->IsArray()) {
            summary.rows.push_back({ L"Dependency entries", std::to_wstring(deps->arrVal.size()) });
        }
    } else {
        summary.rows.push_back({ L"Specification", W(spdxVersion) });
        summary.rows.push_back({ L"Document name", W(root.GetString("name")) });
        summary.rows.push_back({ L"Namespace", W(root.GetString("documentNamespace")) });
        summary.rows.push_back({ L"Data license", W(root.GetString("dataLicense")) });
        if (const JVal* ci = root.Find("creationInfo")) {
            summary.rows.push_back({ L"Created", W(ci->GetString("created")) });
            if (const JVal* creators = ci->Find("creators"); creators && creators->IsArray()) {
                std::string names;
                for (const auto& c : creators->arrVal) AppendJoined(names, Str(&c), ", ");
                summary.rows.push_back({ L"Creators", W(names) });
            }
        }
        summary.rows.push_back({ L"Packages", std::to_wstring(count) });
        if (const JVal* files = root.Find("files"); files && files->IsArray()) summary.rows.push_back({ L"Files", std::to_wstring(files->arrVal.size()) });
        if (const JVal* rel = root.Find("relationships"); rel && rel->IsArray()) summary.rows.push_back({ L"Relationships", std::to_wstring(rel->arrVal.size()) });
    }
    NoteIncomplete(summary, complete, isCdx ? L"components" : L"packages");
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

// CycloneDX XML: <bom><metadata>..</metadata><components><component type=".."><name/>..</component></components>
inline void CycloneDxXmlComponents(std::string_view body, SpreadsheetSheet& sh, size_t& count, int depth) {
    namespace X = ::Explorer2::XmlTagScan;
    if (depth > 16) return;
    X::ForEachChild(body, [&](std::string_view name, const X::Element& e) {
        if (name != "component") return true;
        ++count;
        SbomRow r;
        std::string group = X::ChildText(e.body, "group");
        r.name = group.empty() ? X::ChildText(e.body, "name") : group + "/" + X::ChildText(e.body, "name");
        r.version = X::ChildText(e.body, "version");
        r.type = X::AttrText(e.attrs, "type");
        X::Element lics;
        if (X::FindChild(e.body, "licenses", lics)) {
            X::ForEachChild(lics.body, [&](std::string_view ln, const X::Element& le) {
                if (ln == "expression") AppendJoined(r.license, std::string(X::TrimView(X::DecodeText(le.body))), ", ");
                else if (ln == "license") {
                    std::string one = X::ChildText(le.body, "id");
                    if (one.empty()) one = X::ChildText(le.body, "name");
                    AppendJoined(r.license, one, ", ");
                }
                return true;
            });
        }
        r.locator = X::ChildText(e.body, "purl");
        X::Element sup;
        if (X::FindChild(e.body, "supplier", sup)) r.supplier = X::ChildText(sup.body, "name");
        if (r.supplier.empty()) r.supplier = X::ChildText(e.body, "publisher");
        PushSbomRow(sh, r);
        X::Element nested;
        if (X::FindChild(e.body, "components", nested)) CycloneDxXmlComponents(nested.body, sh, count, depth + 1);
        return true;
    });
}

inline bool ParseCycloneDxXml(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    namespace X = ::Explorer2::XmlTagScan;
    std::string xml = X::DocumentToUtf8(raw.data(), raw.size());
    X::Element bom;
    if (!X::FindChild(xml, "bom", bom)) return false;
    SpreadsheetSheet sh = NewSbomSheet();
    size_t count = 0;
    X::Element comps;
    if (X::FindChild(bom.body, "components", comps)) CycloneDxXmlComponents(comps.body, sh, count, 0);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"CycloneDX Software Bill of Materials (.cdx)";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));
    SpreadsheetSheet summary;
    summary.name = L"SBOM_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    std::string ns = X::AttrText(bom.attrs, "xmlns");
    size_t slash = ns.rfind('/');
    summary.rows.push_back({ L"Specification", L"CycloneDX " + W(slash == std::string::npos ? ns : ns.substr(slash + 1)) });
    summary.rows.push_back({ L"Serial number", W(X::AttrText(bom.attrs, "serialNumber")) });
    X::Element meta, subject;
    if (X::FindChild(bom.body, "metadata", meta) && X::FindChild(meta.body, "component", subject)) {
        summary.rows.push_back({ L"Subject", W(X::ChildText(subject.body, "name")) + L" " + W(X::ChildText(subject.body, "version")) });
    }
    summary.rows.push_back({ L"Components", std::to_wstring(count) });
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

// SPDX tag-value: "Tag: value" lines; a value may be a multi-line <text>..</text> block
inline bool ParseSpdxTagValue(std::string_view sv, ParsedOfficeDocument& out) {
    SpreadsheetSheet sh = NewSbomSheet();
    size_t count = 0;
    std::string spdxVersion, docName, created;
    std::vector<std::string> creators;
    SbomRow cur;
    bool inPackage = false;
    auto flush = [&]() {
        if (inPackage) {
            ++count;
            PushSbomRow(sh, cur);
        }
        cur = SbomRow{};
        inPackage = false;
    };
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view line = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        size_t colon = line.find(':');
        if (line.empty() || line.front() == '#' || colon == std::string_view::npos) continue;
        std::string_view tag = PathUtils::TrimAscii(line.substr(0, colon));
        std::string_view val = PathUtils::TrimAscii(line.substr(colon + 1));
        if (val.rfind("<text>", 0) == 0) { // multi-line value: skip to its end
            if (val.find("</text>") == std::string_view::npos) {
                size_t end = sv.find("</text>", pos);
                pos = (end == std::string_view::npos) ? sv.size() : sv.find('\n', end);
                if (pos == std::string_view::npos) pos = sv.size(); else ++pos;
            }
            continue;
        }
        if (tag == "SPDXVersion") spdxVersion = std::string(val);
        else if (tag == "DocumentName") docName = std::string(val);
        else if (tag == "Created") created = std::string(val);
        else if (tag == "Creator") creators.emplace_back(val);
        else if (tag == "PackageName") { flush(); inPackage = true; cur.name = std::string(val); }
        else if (!inPackage) continue;
        else if (tag == "PackageVersion") cur.version = std::string(val);
        else if (tag == "PrimaryPackagePurpose") cur.type = std::string(val);
        else if (tag == "PackageLicenseConcluded") cur.license = std::string(val);
        else if (tag == "PackageLicenseDeclared" && (cur.license.empty() || cur.license == "NOASSERTION")) cur.license = std::string(val);
        else if (tag == "PackageSupplier") cur.supplier = std::string(val);
        else if (tag == "PackageDownloadLocation" && cur.locator.empty() && val != "NOASSERTION" && val != "NONE") cur.locator = std::string(val);
        else if (tag == "ExternalRef") { // ExternalRef: PACKAGE-MANAGER purl pkg:npm/x@1
            size_t p1 = val.find(' ');
            size_t p2 = (p1 == std::string_view::npos) ? p1 : val.find(' ', p1 + 1);
            if (p2 != std::string_view::npos && val.substr(p1 + 1, p2 - p1 - 1) == "purl") {
                cur.locator = std::string(PathUtils::TrimAscii(val.substr(p2 + 1)));
            }
        }
        else if (tag == "FileName" || tag == "SnippetSPDXID" || tag == "LicenseID") flush(); // past the package's tags
    }
    flush();
    if (spdxVersion.rfind("SPDX-", 0) != 0) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Linux Foundation SPDX SBOM (.spdx)";
    FinishSheet(sh);
    out.sheets.push_back(std::move(sh));
    SpreadsheetSheet summary;
    summary.name = L"SBOM_Summary";
    summary.rows.push_back({ L"Property", L"Value" });
    summary.rows.push_back({ L"Specification", W(spdxVersion) });
    summary.rows.push_back({ L"Document name", W(docName) });
    summary.rows.push_back({ L"Created", W(created) });
    std::string creatorList;
    for (const auto& c : creators) AppendJoined(creatorList, c, ", ");
    summary.rows.push_back({ L"Creators", W(creatorList) });
    summary.rows.push_back({ L"Packages", std::to_wstring(count) });
    FinishSheet(summary);
    out.sheets.push_back(std::move(summary));
    out.valid = true;
    return true;
}

} // namespace DetailCloudSecOps

inline bool ParseCloudSecOps200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    using namespace DetailCloudSecOps;
    std::string_view text = ::Explorer2::Json::StripUtf8Bom(
        std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    size_t firstPos = text.find_first_not_of(" \t\r\n");
    char first = (firstPos == std::string_view::npos) ? '\0' : text[firstPos];

    if (ext == L".tfstate") return first == '{' && ParseTerraformState(text, out);
    if (ext == L".sarif") return first == '{' && ParseSarifLog(text, out);
    if (ext == L".spdx" || ext == L".cdx") {
        if (first == '{') return ParseSbomJson(text, out);
        if (first == '<') return ext == L".cdx" && ParseCycloneDxXml(raw, out); // (SPDX RDF/XML is not read)
        return ParseSpdxTagValue(text, out);
    }
    return false;
}

// ============================================================================
// #89–#91. Crypto & Security Containers (.gpg/.pgp/.sig, .jks, .cat)
// ============================================================================
inline bool ParseCryptoSec200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #89. OpenPGP RFC 4880 Binary & ASCII-Armored Packet Inspector (.gpg / .pgp / .sig)
    if (ext == L".gpg" || ext == L".pgp" || ext == L".sig") {
        std::vector<uint8_t> decoded;
        const uint8_t* pktData = raw.data();
        size_t pktSize = raw.size();
        bool armored = false;

        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("-----BEGIN PGP ") != std::string_view::npos) {
            armored = true;
            size_t hdrEnd = sv.find("\r\n\r\n");
            size_t sep = 4;
            if (hdrEnd == std::string_view::npos) { hdrEnd = sv.find("\n\n"); sep = 2; }
            if (hdrEnd != std::string_view::npos) {
                size_t ftr = sv.find("-----END PGP ", hdrEnd + sep);
                std::string_view b64 = sv.substr(hdrEnd + sep, (ftr == std::string_view::npos ? sv.size() : ftr) - (hdrEnd + sep));
                // Strip CRC24 line starting with '='
                size_t crcEq = b64.rfind("\n=");
                if (crcEq != std::string_view::npos) b64 = b64.substr(0, crcEq);
                decoded = DetailDocs200::DecodeBase64Block(b64);
                pktData = decoded.data();
                pktSize = decoded.size();
            }
        }
        if (pktSize < 2 || (pktData[0] & 0x80u) == 0) return false;

        auto pgpTagName = [](uint8_t tag) -> const wchar_t* {
            switch (tag) {
            case 1:  return L"Public-Key Encrypted Session Key (PKESK)";
            case 2:  return L"Signature Packet";
            case 3:  return L"Symmetric-Key Encrypted Session Key (SKESK)";
            case 4:  return L"One-Pass Signature";
            case 5:  return L"Secret-Key Packet";
            case 6:  return L"Public-Key Packet";
            case 7:  return L"Secret-Subkey Packet";
            case 8:  return L"Compressed Data";
            case 9:  return L"Symmetrically Encrypted Data";
            case 11: return L"Literal Data";
            case 13: return L"User ID";
            case 14: return L"Public-Subkey Packet";
            case 18: return L"Sym. Encrypted Integrity Protected Data (SEIPD)";
            default: return L"OpenPGP Packet";
            }
        };
        auto pgpAlgName = [](uint8_t alg) -> std::wstring {
            switch (alg) {
            case 1: case 2: case 3: return L"RSA";
            case 16: return L"Elgamal";
            case 17: return L"DSA";
            case 18: return L"ECDH (Curve25519/NIST)";
            case 19: return L"ECDSA";
            case 22: return L"EdDSA (Ed25519)";
            default: return L"Alg#" + std::to_wstring(alg);
            }
        };

        SpreadsheetSheet sh;
        sh.name = L"OpenPGP_Packets";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Packet #", L"RFC 4880 Tag", L"Body Length", L"Algorithm / Identity Details" });

        size_t pos = 0;
        int idx = 0;
        while (pos < pktSize && idx < 128) {
            uint8_t ctb = pktData[pos++];
            if ((ctb & 0x80u) == 0) break;
            uint8_t tag = 0;
            size_t bodyLen = 0;
            if ((ctb & 0x40u) != 0) { // New packet format
                tag = ctb & 0x3Fu;
                if (pos >= pktSize) break;
                uint8_t l0 = pktData[pos++];
                if (l0 < 192) bodyLen = l0;
                else if (l0 < 224) {
                    if (pos >= pktSize) break;
                    bodyLen = ((static_cast<size_t>(l0 - 192) << 8) + pktData[pos++]) + 192;
                } else if (l0 == 255) {
                    if (pos + 4 > pktSize) break;
                    bodyLen = DetailDocs200::RdU32BE(pktData + pos);
                    pos += 4;
                } else {
                    bodyLen = 1ull << (l0 & 0x1Fu); // Partial body length
                }
            } else { // Old packet format
                tag = (ctb >> 2) & 0x0Fu;
                uint8_t ltype = ctb & 0x03u;
                if (ltype == 0) { if (pos >= pktSize) break; bodyLen = pktData[pos++]; }
                else if (ltype == 1) { if (pos + 2 > pktSize) break; bodyLen = DetailDocs200::RdU16BE(pktData + pos); pos += 2; }
                else if (ltype == 2) { if (pos + 4 > pktSize) break; bodyLen = DetailDocs200::RdU32BE(pktData + pos); pos += 4; }
                else bodyLen = pktSize - pos;
            }
            if (bodyLen > pktSize - pos) bodyLen = pktSize - pos;
            const uint8_t* body = pktData + pos;

            std::wstring detail = armored ? L"ASCII-Armored" : L"Binary";
            if (tag == 13 && bodyLen > 0) {
                detail = L"UID: " + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(body), bodyLen));
            } else if ((tag == 6 || tag == 14 || tag == 5 || tag == 7) && bodyLen >= 6) {
                uint8_t ver = body[0];
                uint32_t ts = DetailDocs200::RdU32BE(body + 1);
                uint8_t alg = (ver == 4) ? body[5] : (bodyLen >= 8 ? body[7] : 0);
                detail = L"v" + std::to_wstring(ver) + L" " + pgpAlgName(alg) + L" (Created Unix=" + std::to_wstring(ts) + L")";
            } else if (tag == 2 && bodyLen >= 4) {
                uint8_t ver = body[0];
                uint8_t pkAlg = (ver == 4) ? body[2] : (bodyLen >= 17 ? body[15] : 0);
                detail = L"v" + std::to_wstring(ver) + L" Sig via " + pgpAlgName(pkAlg);
            } else if (tag == 1 && bodyLen >= 10) {
                detail = L"Recipient KeyID: " + DetailDocs200::HexBytes(body + 1, 8, 8) + L" (" + pgpAlgName(body[9]) + L")";
            }

            sh.rows.push_back({ std::to_wstring(++idx), pgpTagName(tag), std::to_wstring(bodyLen) + L" B", detail });
            pos += bodyLen;
        }
        if (idx == 0) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = std::wstring(L"OpenPGP Cryptographic Packet Stream (") + (armored ? L"ASCII-Armored " : L"Binary ") + ext + L")";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #90. Java KeyStore Binary Vault (.jks)
    if (ext == L".jks" || (raw.size() >= 12 && (DetailDocs200::RdU32BE(raw.data()) == 0xFEEDFEEDu || DetailDocs200::RdU32BE(raw.data()) == 0xCECECECEu))) {
        if (raw.size() < 12) return false;
        uint32_t magic = DetailDocs200::RdU32BE(raw.data());
        if (magic != 0xFEEDFEEDu && magic != 0xCECECECEu) return false;
        uint32_t ver   = DetailDocs200::RdU32BE(raw.data() + 4);
        uint32_t count = DetailDocs200::RdU32BE(raw.data() + 8);

        SpreadsheetSheet sh;
        sh.name = L"KeyStore_Entries";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Alias", L"Entry Type", L"Created (Epoch ms)", L"Certificate / Key Info" });

        size_t pos = 12;
        for (uint32_t i = 0; i < count && i < 256 && pos + 6 <= raw.size(); ++i) {
            uint32_t tag = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
            if (pos + 2 > raw.size()) break;
            uint16_t aLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
            if (pos + aLen + 8 > raw.size()) break;
            std::wstring alias = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), aLen));
            pos += aLen;
            uint64_t ts = DetailDocs200::RdU64BE(raw.data() + pos); pos += 8;

            if (tag == 1) { // PrivateKey
                if (pos + 4 > raw.size()) break;
                uint32_t kLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                if (kLen > raw.size() - pos) break;
                pos += kLen;
                uint32_t chainCnt = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32BE(raw.data() + pos) : 0;
                if (pos + 4 <= raw.size()) pos += 4;
                for (uint32_t c = 0; c < chainCnt && pos + 2 <= raw.size(); ++c) {
                    uint16_t ctLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen + 4 > raw.size()) { pos = raw.size(); break; }
                    pos += ctLen;
                    uint32_t cbLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                    if (cbLen > raw.size() - pos) { pos = raw.size(); break; }
                    pos += cbLen;
                }
                sh.rows.push_back({ alias, L"PrivateKeyEntry", std::to_wstring(ts),
                    L"PKCS#8 Encrypted Key (" + std::to_wstring(kLen) + L" B), Chain=" + std::to_wstring(chainCnt) });
            } else if (tag == 2) { // TrustedCert
                std::wstring ctype = L"X.509";
                if (ver == 2) {
                    if (pos + 2 > raw.size()) break;
                    uint16_t ctLen = DetailDocs200::RdU16BE(raw.data() + pos); pos += 2;
                    if (pos + ctLen > raw.size()) break;
                    ctype = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + pos), ctLen));
                    pos += ctLen;
                }
                if (pos + 4 > raw.size()) break;
                uint32_t cbLen = DetailDocs200::RdU32BE(raw.data() + pos); pos += 4;
                if (cbLen > raw.size() - pos) break;
                pos += cbLen;
                sh.rows.push_back({ alias, L"TrustedCertificateEntry", std::to_wstring(ts),
                    ctype + L" DER Certificate (" + std::to_wstring(cbLen) + L" B)" });
            } else {
                break;
            }
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (magic == 0xCECECECEu) ? L"Java JCEKS Cryptographic KeyStore (.jks)" : L"Java KeyStore Binary Vault (.jks)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #91. Windows Authenticode Security Catalog (.cat — ASN.1 DER PKCS#7 SignedData)
    if (ext == L".cat" && raw.size() >= 16 && raw[0] == 0x30) {
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Authenticode PKCS#7 Security Catalog (.cat)";

        SpreadsheetSheet sh;
        sh.name = L"Catalog_Contents";
        sh.maxCols = 3;
        sh.rows.push_back({ L"ASN.1 / Authenticode Element", L"Encoding", L"Value / Digest" });

        // Check SPC_CATALOG_LIST OID 1.3.6.1.4.1.311.12.1.1 (2B 06 01 04 01 82 37 0C 01 01)
        static const uint8_t kCatOid[10] = { 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0C, 0x01, 0x01 };
        bool hasSpcCat = false;
        for (size_t i = 0; i + 10 <= raw.size(); ++i) {
            if (std::memcmp(raw.data() + i, kCatOid, 10) == 0) { hasSpcCat = true; break; }
        }
        sh.rows.push_back({ L"PKCS#7 ContentType", L"OID", hasSpcCat ? L"1.3.6.1.4.1.311.12.1.1 (SPC_CATALOG_LIST)" : L"1.2.840.113549.1.7.2 (SignedData)" });

        // Extract DER PrintableString (0x13), UTF8String (0x0C), BMPString UTF-16BE (0x1E), and SHA-1/SHA-256 OCTET STRINGs (0x04 0x14 / 0x04 0x20)
        for (size_t i = 2; i + 4 < raw.size() && sh.rows.size() < 250; ++i) {
            uint8_t tag = raw[i];
            uint8_t len = raw[i + 1];
            if ((tag == 0x0C || tag == 0x13 || tag == 0x16) && len >= 3 && len < 120 && i + 2 + len <= raw.size()) {
                if (DetailDocs200::IsMostlyPrintableUtf8(raw.data() + i + 2, len)) {
                    sh.rows.push_back({ L"Signer / Attribute", (tag == 0x0C) ? L"UTF8String" : L"PrintableString",
                        PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + i + 2), len)) });
                    i += 1 + len;
                }
            } else if (tag == 0x1E && len >= 6 && (len % 2) == 0 && len < 120 && i + 2 + len <= raw.size()) {
                std::wstring bmp;
                bool okBmp = true;
                for (size_t k = 0; k < len; k += 2) {
                    uint16_t ch = DetailDocs200::RdU16BE(raw.data() + i + 2 + k);
                    if (ch < 32 || ch > 0xD7FF) { okBmp = false; break; }
                    bmp.push_back(static_cast<wchar_t>(ch));
                }
                if (okBmp) {
                    sh.rows.push_back({ L"Catalog Member Tag", L"BMPString", bmp });
                    i += 1 + len;
                }
            } else if (tag == 0x04 && (len == 20 || len == 32) && i + 2 + len <= raw.size()) {
                sh.rows.push_back({ (len == 32) ? L"SHA-256 Member Thumbprint" : L"SHA-1 Member Thumbprint",
                    L"OCTET STRING", DetailDocs200::HexBytes(raw.data() + i + 2, len, 32) });
                i += 1 + len;
            }
        }
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #92–#94. Windows Forensics & System Policy (.hiv / NTUSER.DAT, .etl, .pol)
// ============================================================================
inline bool ParseWindowsForensics200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    // #92. Windows Registry Binary Hive ("regf" — .hiv or NTUSER.DAT)
    if ((raw.size() >= 48 && std::memcmp(raw.data(), "regf", 4) == 0) && (ext == L".hiv" || ext == L".dat")) {
        uint32_t seq1     = DetailDocs200::RdU32LE(raw.data() + 4);
        uint32_t seq2     = DetailDocs200::RdU32LE(raw.data() + 8);
        uint32_t majVer   = DetailDocs200::RdU32LE(raw.data() + 20);
        uint32_t minVer   = DetailDocs200::RdU32LE(raw.data() + 24);
        uint32_t rootCell = DetailDocs200::RdU32LE(raw.data() + 36);
        std::wstring hiveName = (raw.size() >= 112) ? DetailDocs200::ReadUtf16LE(raw.data() + 48, 64) : L"(Root Hive)";

        SpreadsheetSheet keysSh;
        keysSh.name = L"Hive_Keys_And_Values";
        keysSh.maxCols = 4;
        keysSh.rows.push_back({ L"Cell Kind", L"Key / Value Name", L"Subkeys / RegType", L"Decoded Data" });

        // Walk hbin blocks starting at offset 4096
        size_t pos = 4096;
        while (pos + 32 <= raw.size() && keysSh.rows.size() < 500) {
            if (std::memcmp(raw.data() + pos, "hbin", 4) == 0) {
                uint32_t hbinSize = DetailDocs200::RdU32LE(raw.data() + pos + 8);
                if (hbinSize < 32) break;
                size_t cpos = pos + 32;
                size_t hend = std::min(raw.size(), pos + hbinSize);
                while (cpos + 8 <= hend && keysSh.rows.size() < 500) {
                    int32_t rawSz = DetailDocs200::RdI32LE(raw.data() + cpos);
                    size_t absSz = static_cast<size_t>(rawSz < 0 ? -static_cast<int64_t>(rawSz) : rawSz);
                    if (absSz < 8 || cpos + absSz > hend) break;
                    if (rawSz < 0 && absSz >= 24) { // Allocated cell
                        const uint8_t* cell = raw.data() + cpos + 4;
                        size_t cLen = absSz - 4;
                        if (cLen >= 76 && cell[0] == 'n' && cell[1] == 'k') {
                            uint16_t flags  = DetailDocs200::RdU16LE(cell + 2);
                            uint32_t subCnt = DetailDocs200::RdU32LE(cell + 20);
                            uint32_t valCnt = DetailDocs200::RdU32LE(cell + 36);
                            uint16_t nLen   = DetailDocs200::RdU16LE(cell + 72);
                            if (76u + nLen <= cLen) {
                                std::wstring kn = (flags & 0x20u)
                                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell + 76), nLen))
                                    : DetailDocs200::ReadUtf16LE(cell + 76, nLen);
                                keysSh.rows.push_back({ L"Key (nk)", kn,
                                    std::to_wstring(subCnt) + L" subkeys", std::to_wstring(valCnt) + L" values" });
                            }
                        } else if (cLen >= 20 && cell[0] == 'v' && cell[1] == 'k') {
                            uint16_t nLen  = DetailDocs200::RdU16LE(cell + 2);
                            uint32_t dLenR = DetailDocs200::RdU32LE(cell + 4);
                            uint32_t dOff  = DetailDocs200::RdU32LE(cell + 8);
                            uint32_t dType = DetailDocs200::RdU32LE(cell + 12);
                            uint16_t vFlg  = DetailDocs200::RdU16LE(cell + 16);
                            std::wstring vn = L"(Default)";
                            if (nLen > 0 && 20u + nLen <= cLen) {
                                vn = (vFlg & 1u)
                                    ? PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cell + 20), nLen))
                                    : DetailDocs200::ReadUtf16LE(cell + 20, nLen);
                            }
                            bool inlineData = (dLenR & 0x80000000u) != 0;
                            uint32_t dLen = dLenR & 0x7FFFFFFFu;
                            std::wstring tName = (dType == 1) ? L"REG_SZ" : (dType == 2) ? L"REG_EXPAND_SZ" :
                                                 (dType == 3) ? L"REG_BINARY" : (dType == 4) ? L"REG_DWORD" :
                                                 (dType == 7) ? L"REG_MULTI_SZ" : (dType == 11) ? L"REG_QWORD" : (L"TYPE_" + std::to_wstring(dType));
                            std::wstring dVal;
                            if (inlineData && dType == 4) {
                                wchar_t b[32] = {}; swprintf_s(b, L"0x%08X (%u)", dOff, dOff); dVal = b;
                            } else if (!inlineData && dLen > 0 && 4096ull + dOff + 4 + dLen <= raw.size()) {
                                const uint8_t* dp = raw.data() + 4096 + dOff + 4;
                                if (dType == 1 || dType == 2 || dType == 7) dVal = DetailDocs200::ReadUtf16LE(dp, std::min<uint32_t>(dLen, 256));
                                else if (dType == 4 && dLen >= 4) dVal = std::to_wstring(DetailDocs200::RdU32LE(dp));
                                else dVal = DetailDocs200::HexBytes(dp, dLen, 16);
                            }
                            keysSh.rows.push_back({ L"Value (vk)", vn, tName, dVal });
                        }
                    }
                    cpos += absSz;
                }
                pos += hbinSize;
            } else {
                pos += 4096;
            }
        }

        SpreadsheetSheet hdrSh;
        hdrSh.name = L"Hive_Header";
        hdrSh.maxCols = 2;
        hdrSh.rows.push_back({ L"regf Field", L"Value" });
        hdrSh.rows.push_back({ L"Embedded Hive Name", hiveName.empty() ? L"(Unnamed)" : hiveName });
        hdrSh.rows.push_back({ L"Hive Format Version", std::to_wstring(majVer) + L"." + std::to_wstring(minVer) });
        hdrSh.rows.push_back({ L"Sequence Numbers", std::to_wstring(seq1) + L" / " + std::to_wstring(seq2) + (seq1 == seq2 ? L" (Clean)" : L" (Dirty)") });
        hdrSh.rows.push_back({ L"Root Key Cell Offset", L"0x" + DetailSys100::Hex32(rootCell).substr(2) });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Registry Binary Hive (" + ext + L")";
        out.sheets.push_back(std::move(keysSh));
        out.sheets.push_back(std::move(hdrSh));
        out.valid = true;
        return true;
    }

    // #93. Windows Event Trace Log (.etl — WMI_BUFFER_HEADER + TRACE_LOGFILE_HEADER + ETW Event Records)
    if (ext == L".etl" && raw.size() >= 1024) {
        const uint32_t buf0Size = DetailDocs200::RdU32LE(raw.data() + 0);
        // WMI_BUFFER_HEADER BufferSize must be a 1 KB multiple in [1024, 16 MB] and fit inside raw
        if (buf0Size < 1024u || buf0Size > (16u * 1024u * 1024u) || (buf0Size % 1024u) != 0 || buf0Size > raw.size()) {
            return false;
        }

        auto getValidBufferEnd = [](const uint8_t* bHdr, uint32_t bSize) -> uint32_t {
            const uint32_t savedOff = DetailDocs200::RdU32LE(bHdr + 4);
            const uint32_t wnodeOff = DetailDocs200::RdU32LE(bHdr + 48);
            if (wnodeOff >= 72u && wnodeOff <= bSize) return wnodeOff;
            if (savedOff >= 72u && savedOff <= bSize) return savedOff;
            return 0u;
        };

        auto formatGuid = [](const uint8_t* g) -> std::wstring {
            const uint32_t d1 = DetailDocs200::RdU32LE(g + 0);
            const uint16_t d2 = DetailDocs200::RdU16LE(g + 4);
            const uint16_t d3 = DetailDocs200::RdU16LE(g + 6);
            wchar_t buf[64] = {};
            swprintf_s(buf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                d1, d2, d3, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
            return buf;
        };

        auto formatFileTime = [](uint64_t ft100ns) -> std::wstring {
            // Windows FILETIME: 100-ns intervals since 1601-01-01 UTC; if small QPC tick, render raw tick
            if (ft100ns < 116444736000000000ULL || ft100ns > 200000000000000000ULL) {
                return L"QPC " + std::to_wstring(ft100ns);
            }
            FILETIME ft{};
            ft.dwLowDateTime  = static_cast<DWORD>(ft100ns & 0xFFFFFFFFULL);
            ft.dwHighDateTime = static_cast<DWORD>(ft100ns >> 32);
            SYSTEMTIME st{};
            if (!FileTimeToSystemTime(&ft, &st)) return std::to_wstring(ft100ns);
            const uint32_t us = static_cast<uint32_t>((ft100ns / 10ULL) % 1000000ULL);
            wchar_t buf[64] = {};
            swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%06u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, us);
            return buf;
        };

        auto levelName = [](uint8_t lvl) -> std::wstring {
            switch (lvl) {
            case 0: return L"0 (LogAlways)";
            case 1: return L"1 (Critical)";
            case 2: return L"2 (Error)";
            case 3: return L"3 (Warning)";
            case 4: return L"4 (Info)";
            case 5: return L"5 (Verbose)";
            default: return std::to_wstring(lvl);
            }
        };

        auto isValidEtwRecordAt = [](const uint8_t* rec, size_t maxBytes) -> bool {
            if (maxBytes < 16) return false;
            const uint16_t recSize = DetailDocs200::RdU16LE(rec + 0);
            const uint8_t hdrType  = rec[2];
            const uint8_t marker   = rec[3];
            if (marker != 0xC0u || recSize > maxBytes) return false;
            if (hdrType == 0x12u || hdrType == 0x13u) return recSize >= 80u;      // EVENT_HEADER (32/64-bit)
            if (hdrType == 0x10u || hdrType == 0x11u) return recSize >= 32u;      // Compact EVENT_HEADER
            if (hdrType == 0x01u || hdrType == 0x02u) return recSize >= 32u;      // SYSTEM_TRACE_HEADER (32/64-bit)
            if (hdrType == 0x03u || hdrType == 0x0Cu || hdrType == 0x0Du) return recSize >= 16u; // PERFINFO_TRACE_HEADER
            if (hdrType <= 0x0Fu) return recSize >= 48u;                          // EVENT_TRACE_HEADER
            return false;
        };

        const uint32_t validEnd0 = getValidBufferEnd(raw.data(), buf0Size);
        if (validEnd0 <= 72u || !isValidEtwRecordAt(raw.data() + 72, validEnd0 - 72u)) {
            return false;
        }

        struct ProviderAgg {
            uint64_t count = 0;
            uint16_t minId = 0xFFFFu;
            uint16_t maxId = 0;
            uint32_t levelMask = 0;
        };
        std::map<std::wstring, ProviderAgg> providers;

        SpreadsheetSheet sessSh;
        sessSh.name = L"Session & Buffers";
        sessSh.maxCols = 2;
        sessSh.rows.push_back({ L"ETW Property", L"Value" });

        SpreadsheetSheet evtSh;
        evtSh.name = L"Events";
        evtSh.maxCols = 9;
        evtSh.rows.push_back({ L"Seq", L"Timestamp", L"PID", L"TID", L"Provider GUID", L"EventID", L"Level", L"Opcode", L"Payload Bytes" });

        bool parsedLogfileHdr = false;
        uint64_t totalBuffers = 0;
        uint64_t totalEvents  = 0;

        size_t bufFileOff = 0;
        while (bufFileOff + 72u <= raw.size()) {
            const uint8_t* bHdr = raw.data() + bufFileOff;
            const uint32_t bSize = DetailDocs200::RdU32LE(bHdr + 0);
            if (bSize < 1024u || bSize > (16u * 1024u * 1024u) || (bSize % 1024u) != 0) break;
            if (bufFileOff + bSize > raw.size()) break;

            const uint32_t vEnd = getValidBufferEnd(bHdr, bSize);
            ++totalBuffers;
            if (vEnd > 72u && vEnd <= bSize) {
                size_t rOff = 72u;
                while (rOff + 16u <= vEnd) {
                    const uint8_t* rec = bHdr + rOff;
                    if (rec[3] != 0xC0u) break;
                    const uint16_t recSize = DetailDocs200::RdU16LE(rec + 0);
                    const uint8_t hdrType  = rec[2];
                    if (recSize < 16u || rOff + recSize > vEnd) break;

                    if ((hdrType == 0x12u || hdrType == 0x13u) && recSize >= 80u) {
                        // Vista+ Crimson EVENT_HEADER (80 bytes)
                        const uint16_t flags   = DetailDocs200::RdU16LE(rec + 4);
                        const uint32_t tid     = DetailDocs200::RdU32LE(rec + 8);
                        const uint32_t pid     = DetailDocs200::RdU32LE(rec + 12);
                        const uint64_t ts      = DetailDocs200::RdU64LE(rec + 16);
                        const std::wstring gid = formatGuid(rec + 24);
                        const uint16_t evtId   = DetailDocs200::RdU16LE(rec + 40);
                        const uint8_t level    = rec[44];
                        const uint8_t opcode   = rec[45];
                        uint32_t payloadLen    = recSize - 80u;
                        if ((flags & 0x0001u) != 0 && payloadLen >= 2u) {
                            // Adjust for extended data items if present
                            const uint16_t extCnt = DetailDocs200::RdU16LE(rec + 80);
                            size_t extPos = 82u;
                            for (uint16_t e = 0; e < extCnt && extPos + 8u <= recSize; ++e) {
                                const uint16_t dSz = DetailDocs200::RdU16LE(rec + extPos + 6u);
                                extPos += 8u + dSz;
                            }
                            if (extPos <= recSize) payloadLen = static_cast<uint32_t>(recSize - extPos);
                        }

                        ++totalEvents;
                        auto& pa = providers[gid];
                        pa.count++;
                        pa.minId = std::min(pa.minId, evtId);
                        pa.maxId = std::max(pa.maxId, evtId);
                        if (level < 32u) pa.levelMask |= (1u << level);

                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                std::to_wstring(pid),
                                std::to_wstring(tid),
                                gid,
                                std::to_wstring(evtId),
                                levelName(level),
                                std::to_wstring(opcode),
                                std::to_wstring(payloadLen)
                            });
                        }
                    } else if ((hdrType == 0x01u || hdrType == 0x02u) && recSize >= 32u) {
                        // Kernel SYSTEM_TRACE_HEADER (32 bytes)
                        const uint8_t opcode = rec[4];
                        const uint8_t group  = rec[5];
                        const uint32_t tid   = DetailDocs200::RdU32LE(rec + 8);
                        const uint32_t pid   = DetailDocs200::RdU32LE(rec + 12);
                        const uint64_t ts    = DetailDocs200::RdU64LE(rec + 16);
                        const uint16_t synId = static_cast<uint16_t>((static_cast<uint16_t>(group) << 8) | opcode);
                        const std::wstring kGuid = L"{9E814AAD-3204-11D2-9A82-006008A86939}"; // SystemTraceControlGuid

                        ++totalEvents;
                        auto& pa = providers[kGuid];
                        pa.count++;
                        pa.minId = std::min(pa.minId, synId);
                        pa.maxId = std::max(pa.maxId, synId);
                        pa.levelMask |= (1u << 4);

                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                std::to_wstring(pid),
                                std::to_wstring(tid),
                                kGuid,
                                std::to_wstring(synId),
                                L"4 (Kernel)",
                                std::to_wstring(opcode),
                                std::to_wstring(recSize - 32u)
                            });
                        }
                    } else if (hdrType <= 0x0Fu && hdrType != 0x03u && hdrType != 0x0Cu && hdrType != 0x0Du && recSize >= 48u) {
                        // Classic EVENT_TRACE_HEADER (48 bytes)
                        const uint8_t opcode   = rec[4];
                        const uint8_t level    = rec[5];
                        const uint32_t tid     = DetailDocs200::RdU32LE(rec + 8);
                        const uint32_t pid     = DetailDocs200::RdU32LE(rec + 12);
                        const uint64_t ts      = DetailDocs200::RdU64LE(rec + 16);
                        const std::wstring gid = formatGuid(rec + 24);
                        const uint32_t payLen  = recSize - 48u;

                        if (!parsedLogfileHdr && totalBuffers == 1 && rOff == 72u && opcode == 0u && payLen >= 64u) {
                            parsedLogfileHdr = true;
                            const uint8_t* lh = rec + 48;
                            const uint8_t majVer  = lh[4];
                            const uint8_t minVer  = lh[5];
                            const uint32_t build  = DetailDocs200::RdU32LE(lh + 8);
                            const uint32_t nProcs = DetailDocs200::RdU32LE(lh + 12);
                            const uint64_t endFt  = DetailDocs200::RdU64LE(lh + 16);
                            const uint32_t timRes = DetailDocs200::RdU32LE(lh + 24);
                            const uint32_t bufWr  = DetailDocs200::RdU32LE(lh + 36);
                            const uint32_t ptrSz  = DetailDocs200::RdU32LE(lh + 44);
                            const uint32_t evtLst = DetailDocs200::RdU32LE(lh + 48);
                            const uint32_t cpuMhz = DetailDocs200::RdU32LE(lh + 52);

                            sessSh.rows.push_back({ L"OS Version",
                                std::to_wstring(majVer) + L"." + std::to_wstring(minVer) + L" (Build " + std::to_wstring(build) + L")" });
                            if (ptrSz == 4u || ptrSz == 8u) {
                                sessSh.rows.push_back({ L"Architecture Pointer Size", std::to_wstring(ptrSz * 8u) + L"-bit" });
                            }
                            sessSh.rows.push_back({ L"Number of Processors", std::to_wstring(nProcs) });
                            if (cpuMhz > 0) sessSh.rows.push_back({ L"CPU Speed", std::to_wstring(cpuMhz) + L" MHz" });
                            if (timRes > 0) sessSh.rows.push_back({ L"Timer Resolution (100ns)", std::to_wstring(timRes) });
                            sessSh.rows.push_back({ L"Header Buffers Written", std::to_wstring(bufWr) });
                            sessSh.rows.push_back({ L"Events Lost", std::to_wstring(evtLst) });

                            const size_t tailBase = (ptrSz == 4u) ? 240u : 248u;
                            if (payLen >= tailBase + 32u) {
                                const uint64_t bootFt  = DetailDocs200::RdU64LE(lh + tailBase + 0);
                                const uint64_t startFt = DetailDocs200::RdU64LE(lh + tailBase + 16);
                                const uint32_t bufLost = DetailDocs200::RdU32LE(lh + tailBase + 28);
                                if (bootFt > 0)  sessSh.rows.push_back({ L"Boot Time (UTC)", formatFileTime(bootFt) });
                                if (startFt > 0) sessSh.rows.push_back({ L"Session Start Time (UTC)", formatFileTime(startFt) });
                                if (endFt > 0)   sessSh.rows.push_back({ L"Session End Time (UTC)", formatFileTime(endFt) });
                                sessSh.rows.push_back({ L"Buffers Lost", std::to_wstring(bufLost) });

                                const size_t strOff = tailBase + 32u;
                                if (payLen > strOff + 2u) {
                                    std::wstring loggerName = DetailDocs200::ReadUtf16LE(lh + strOff, payLen - strOff);
                                    if (!loggerName.empty()) {
                                        sessSh.rows.push_back({ L"Logger Name", loggerName });
                                        const size_t nextStr = strOff + (loggerName.size() + 1u) * 2u;
                                        if (payLen > nextStr + 2u) {
                                            std::wstring logFile = DetailDocs200::ReadUtf16LE(lh + nextStr, payLen - nextStr);
                                            if (!logFile.empty()) sessSh.rows.push_back({ L"Log File Name", logFile });
                                        }
                                    }
                                }
                            }
                        }

                        ++totalEvents;
                        auto& pa = providers[gid];
                        pa.count++;
                        pa.minId = std::min<uint16_t>(pa.minId, opcode);
                        pa.maxId = std::max<uint16_t>(pa.maxId, opcode);
                        if (level < 32u) pa.levelMask |= (1u << level);

                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                std::to_wstring(pid),
                                std::to_wstring(tid),
                                gid,
                                std::to_wstring(opcode),
                                levelName(level),
                                std::to_wstring(opcode),
                                std::to_wstring(payLen)
                            });
                        }
                    } else {
                        // PERFINFO_TRACE_HEADER (0x03/0x0C/0x0D)
                        const uint16_t hookId = DetailDocs200::RdU16LE(rec + 4);
                        const uint64_t ts     = DetailDocs200::RdU64LE(rec + 8);
                        const std::wstring pGuid = L"{CE1DBFB4-137E-4DA6-87B0-3F59AA102CBC}";
                        ++totalEvents;
                        auto& pa = providers[pGuid];
                        pa.count++;
                        pa.minId = std::min(pa.minId, hookId);
                        pa.maxId = std::max(pa.maxId, hookId);
                        pa.levelMask |= (1u << 4);
                        if (evtSh.rows.size() <= 512) {
                            evtSh.rows.push_back({
                                std::to_wstring(totalEvents),
                                formatFileTime(ts),
                                L"0",
                                L"0",
                                pGuid,
                                std::to_wstring(hookId),
                                L"4 (PerfInfo)",
                                std::to_wstring(hookId & 0xFFu),
                                std::to_wstring(recSize - 16u)
                            });
                        }
                    }

                    const size_t alignedStep = (static_cast<size_t>(recSize) + 7u) & ~static_cast<size_t>(7u);
                    if (alignedStep == 0) break;
                    rOff += alignedStep;
                }
            }

            bufFileOff += bSize;
        }

        if (totalEvents == 0) return false;

        sessSh.rows.insert(sessSh.rows.begin() + 1, { L"WMI Buffer Size", PathUtils::FormatByteSize(buf0Size) });
        sessSh.rows.insert(sessSh.rows.begin() + 2, { L"Parsed ETW Buffers", std::to_wstring(totalBuffers) });
        sessSh.rows.insert(sessSh.rows.begin() + 3, { L"Total Decoded Event Records", std::to_wstring(totalEvents) });
        sessSh.rows.insert(sessSh.rows.begin() + 4, { L"Distinct Provider GUIDs", std::to_wstring(providers.size()) });
        sessSh.rows.push_back({ L"First Buffer LoggerId", std::to_wstring(DetailDocs200::RdU16LE(raw.data() + 42)) });
        sessSh.rows.push_back({ L"First Buffer Processor", std::to_wstring(raw[40]) });

        SpreadsheetSheet provSh;
        provSh.name = L"Providers";
        provSh.maxCols = 4;
        provSh.rows.push_back({ L"Provider GUID", L"Event Count", L"Levels Seen", L"EventID Range" });
        for (const auto& kv : providers) {
            std::wstring lvls;
            for (uint8_t l = 0; l < 8u; ++l) {
                if ((kv.second.levelMask & (1u << l)) != 0) {
                    if (!lvls.empty()) lvls += L", ";
                    lvls += std::to_wstring(l);
                }
            }
            provSh.rows.push_back({
                kv.first,
                std::to_wstring(kv.second.count),
                lvls.empty() ? L"0" : lvls,
                std::to_wstring(kv.second.minId) + L" .. " + std::to_wstring(kv.second.maxId)
            });
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Event Trace Log (ETW .etl)";
        out.sheets.push_back(std::move(sessSh));
        out.sheets.push_back(std::move(provSh));
        out.sheets.push_back(std::move(evtSh));
        out.valid = true;
        return true;
    }

    // #94. Windows Group Policy Registry.pol ("PReg\x01\x00\x00\x00")
    if ((raw.size() >= 8 && std::memcmp(raw.data(), "PReg\x01\x00\x00\x00", 8) == 0) || ext == L".pol") {
        if (raw.size() < 8 || std::memcmp(raw.data(), "PReg", 4) != 0) return false;
        uint32_t ver = DetailDocs200::RdU32LE(raw.data() + 4);

        SpreadsheetSheet sh;
        sh.name = L"GroupPolicy_Rules";
        sh.maxCols = 4;
        sh.rows.push_back({ L"Registry Key", L"Value Name", L"Reg Type", L"Policy Data" });

        size_t pos = 8;
        while (pos + 16 <= raw.size() && sh.rows.size() < 500) {
            if (DetailDocs200::RdU16LE(raw.data() + pos) != 0x005Bu) { pos += 2; continue; } // '['
            pos += 2;
            std::wstring key = DetailDocs200::ReadUtf16LE(raw.data() + pos, raw.size() - pos);
            pos += (key.size() + 1) * 2;
            if (pos + 2 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            std::wstring val = DetailDocs200::ReadUtf16LE(raw.data() + pos, raw.size() - pos);
            pos += (val.size() + 1) * 2;
            if (pos + 10 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            uint32_t rType = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (pos + 6 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            uint32_t rSize = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (pos + 2 > raw.size() || DetailDocs200::RdU16LE(raw.data() + pos) != 0x003Bu) break; // ';'
            pos += 2;
            if (rSize > raw.size() - pos) break;

            std::wstring tStr = (rType == 1) ? L"REG_SZ" : (rType == 2) ? L"REG_EXPAND_SZ" :
                                (rType == 4) ? L"REG_DWORD" : (rType == 11) ? L"REG_QWORD" : L"REG_BINARY";
            std::wstring dStr;
            if (rType == 4 && rSize >= 4) dStr = std::to_wstring(DetailDocs200::RdU32LE(raw.data() + pos));
            else if (rType == 1 || rType == 2) dStr = DetailDocs200::ReadUtf16LE(raw.data() + pos, rSize);
            else dStr = DetailDocs200::HexBytes(raw.data() + pos, rSize, 16);

            pos += rSize;
            if (pos + 2 <= raw.size() && DetailDocs200::RdU16LE(raw.data() + pos) == 0x005Du) pos += 2; // ']'
            sh.rows.push_back({ key, val, tStr, dStr });
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Windows Group Policy Registry.pol (PReg v" + std::to_wstring(ver) + L")";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// #95–#100. Chiptune, MusicXML, XMP, Unreal, Unity & iWork/Visio Formats
// ============================================================================
inline bool ParseMediaGameOffice200(const std::vector<uint8_t>& raw, const std::wstring& ext, ParsedOfficeDocument& out) {
    auto trimNullAscii = [](const uint8_t* p, size_t maxLen) -> std::wstring {
        size_t n = 0;
        while (n < maxLen && p[n] != 0) ++n;
        return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
    };

    // #95. Retro Chiptune Music (.sid / .nsf / .spc / .vgm)
    if (ext == L".sid" || ext == L".nsf" || ext == L".spc" || ext == L".vgm") {
        SpreadsheetSheet sh;
        sh.name = L"Chiptune_Header";
        sh.maxCols = 2;
        sh.rows.push_back({ L"Chiptune Field", L"Value" });

        if (ext == L".sid" && raw.size() >= 0x76 && (std::memcmp(raw.data(), "PSID", 4) == 0 || std::memcmp(raw.data(), "RSID", 4) == 0)) {
            out.formatTitle = L"Commodore 64 MOS 6581/8580 SID Music (.sid)";
            sh.rows.push_back({ L"Container Magic", trimNullAscii(raw.data(), 4) + L" v" + std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 4)) });
            sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x16, 32) });
            sh.rows.push_back({ L"Author", trimNullAscii(raw.data() + 0x36, 32) });
            sh.rows.push_back({ L"Released", trimNullAscii(raw.data() + 0x56, 32) });
            sh.rows.push_back({ L"Sub-Songs", std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 0x0E)) + L" (Start #" + std::to_wstring(DetailDocs200::RdU16BE(raw.data() + 0x10)) + L")" });
            wchar_t addrs[96] = {};
            swprintf_s(addrs, L"Load=$%04X, Init=$%04X, Play=$%04X",
                DetailDocs200::RdU16BE(raw.data() + 0x08), DetailDocs200::RdU16BE(raw.data() + 0x0A), DetailDocs200::RdU16BE(raw.data() + 0x0C));
            sh.rows.push_back({ L"6510 Addresses", addrs });
        } else if (ext == L".nsf" && raw.size() >= 0x80 && std::memcmp(raw.data(), "NESM\x1A", 5) == 0) {
            out.formatTitle = L"Nintendo NES 2A03 / Expansion Chiptune (.nsf)";
            sh.rows.push_back({ L"Title", trimNullAscii(raw.data() + 0x0E, 32) });
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0x2E, 32) });
            sh.rows.push_back({ L"Copyright", trimNullAscii(raw.data() + 0x4E, 32) });
            sh.rows.push_back({ L"Total Songs", std::to_wstring(raw[6]) + L" (Start #" + std::to_wstring(raw[7]) + L")" });
            uint8_t exp = raw[0x7B];
            std::wstring chips = L"2A03 APU";
            if (exp & 0x01) chips += L" + Konami VRC6";
            if (exp & 0x02) chips += L" + Konami VRC7";
            if (exp & 0x04) chips += L" + FDS";
            if (exp & 0x08) chips += L" + MMC5";
            if (exp & 0x10) chips += L" + Namco 163";
            if (exp & 0x20) chips += L" + Sunsoft 5B";
            sh.rows.push_back({ L"Sound Chips", chips });
        } else if (ext == L".spc" && raw.size() >= 0x100 && std::memcmp(raw.data(), "SNES-SPC700", 11) == 0) {
            out.formatTitle = L"Super Nintendo Sony SPC700 S-DSP State (.spc)";
            sh.rows.push_back({ L"Song Title", trimNullAscii(raw.data() + 0x2E, 32) });
            sh.rows.push_back({ L"Game Title", trimNullAscii(raw.data() + 0x4E, 32) });
            sh.rows.push_back({ L"Artist", trimNullAscii(raw.data() + 0xB1, 32) });
            sh.rows.push_back({ L"Dumper", trimNullAscii(raw.data() + 0x6E, 16) });
            sh.rows.push_back({ L"Comments", trimNullAscii(raw.data() + 0x7E, 32) });
            sh.rows.push_back({ L"SPC700 PC Register", DetailSys100::Hex32(DetailDocs200::RdU16LE(raw.data() + 0x25)) });
        } else if (ext == L".vgm" && raw.size() >= 0x40 && std::memcmp(raw.data(), "Vgm ", 4) == 0) {
            out.formatTitle = L"Video Game Music Chip Register Log (.vgm)";
            uint32_t verBcd  = DetailDocs200::RdU32LE(raw.data() + 0x08);
            uint32_t snClk   = DetailDocs200::RdU32LE(raw.data() + 0x0C);
            uint32_t ym2413  = DetailDocs200::RdU32LE(raw.data() + 0x10);
            uint32_t gd3Rel  = DetailDocs200::RdU32LE(raw.data() + 0x14);
            uint32_t samples = DetailDocs200::RdU32LE(raw.data() + 0x18);
            uint32_t ym2612  = DetailDocs200::RdU32LE(raw.data() + 0x2C);
            wchar_t vbuf[32] = {}; swprintf_s(vbuf, L"%X.%02X", (verBcd >> 8) & 0xFFu, verBcd & 0xFFu);
            sh.rows.push_back({ L"VGM Version", vbuf });
            sh.rows.push_back({ L"Duration (@44.1 kHz)", std::to_wstring(samples / 44100) + L"." + std::to_wstring((samples % 44100) / 441) + L" s" });
            if (snClk)  sh.rows.push_back({ L"TI SN76489 PSG Clock", std::to_wstring(snClk) + L" Hz" });
            if (ym2612) sh.rows.push_back({ L"Yamaha YM2612 FM Clock", std::to_wstring(ym2612) + L" Hz" });
            if (ym2413) sh.rows.push_back({ L"Yamaha YM2413 OPLL Clock", std::to_wstring(ym2413) + L" Hz" });
            if (gd3Rel > 0 && 0x14ull + gd3Rel + 12 < raw.size() && std::memcmp(raw.data() + 0x14 + gd3Rel, "Gd3 ", 4) == 0) {
                size_t gp = 0x14 + gd3Rel + 12;
                static const wchar_t* kGd3Names[] = { L"Track (EN)", L"Track (JP)", L"Game (EN)", L"Game (JP)", L"System (EN)", L"System (JP)", L"Author (EN)" };
                for (int t = 0; t < 7 && gp + 2 <= raw.size(); ++t) {
                    std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + gp, raw.size() - gp);
                    gp += (s.size() + 1) * 2;
                    if (!s.empty()) sh.rows.push_back({ kGd3Names[t], s });
                }
            }
        } else {
            return false;
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #96. MusicXML & Compressed MusicXML Score (.musicxml / .mxl)
    if (ext == L".musicxml" || ext == L".mxl") {
        std::vector<uint8_t> xmlBuf;
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
            std::vector<ZipDeflate::ArchiveItemMeta> entries;
            if (ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) {
                for (const auto& e : entries) {
                    std::wstring eExt = PathUtils::GetExtension(e.pathInArchive);
                    if ((eExt == L".xml" || eExt == L".musicxml") && e.pathInArchive.find(L"META-INF") == std::wstring::npos) {
                        if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, xmlBuf) && !xmlBuf.empty()) {
                            sv = std::string_view(reinterpret_cast<const char*>(xmlBuf.data()), xmlBuf.size());
                            break;
                        }
                    }
                }
            }
        }
        if (sv.find("<score-partwise") == std::string_view::npos && sv.find("<score-timewise") == std::string_view::npos) return false;

        auto xmlTagVal = [&](std::string_view src, std::string_view tag) -> std::string {
            std::string open = "<" + std::string(tag) + ">";
            std::string close = "</" + std::string(tag) + ">";
            size_t p = src.find(open);
            if (p == std::string_view::npos) return {};
            p += open.size();
            size_t e = src.find(close, p);
            return (e == std::string_view::npos) ? std::string{} : std::string(src.substr(p, e - p));
        };

        std::string title = xmlTagVal(sv, "work-title");
        if (title.empty()) title = xmlTagVal(sv, "movement-title");
        // Notes in document order: part-wise scores nest measures in parts, time-wise ones parts in measures. The time
        // signature in force is tracked; chord notes (sounding with the note before) and grace notes are marked.
        namespace X = ::Explorer2::XmlTagScan;
        SpreadsheetSheet notesSh;
        notesSh.name = L"Score_Notes";
        notesSh.rows.push_back({ L"Part", L"Measure", L"Pitch", L"Duration Type", L"Time Sig", L"Lyric" });
        std::string partId, measureNo, timeSig;
        size_t tagPos = 0;
        X::XmlTag t;
        while (X::NextTag(sv, tagPos, t) && notesSh.rows.size() <= 2000) {
            if (t.isClosing) continue;
            if (t.localName == "part" && !t.isSelfClosing) {
                partId = X::AttrText(t.attrs, "id");
            } else if (t.localName == "measure") {
                measureNo = X::AttrText(t.attrs, "number");
            } else if ((t.localName == "time" || t.localName == "note") && !t.isSelfClosing) {
                std::string_view body;
                size_t after = 0;
                if (!X::FindElementBody(sv, t.afterTag, t.localName, body, after)) break;
                tagPos = after;
                if (t.localName == "time") {
                    std::string beats = X::ChildText(body, "beats");
                    if (!beats.empty()) timeSig = beats + "/" + X::ChildText(body, "beat-type");
                    continue;
                }
                X::Element el;
                std::string pitch;
                if (X::FindChild(body, "pitch", el)) {
                    std::string alter = X::ChildText(el.body, "alter");
                    const char* acc = (alter == "1") ? "#" : (alter == "-1") ? "b" : (alter == "2") ? "##" : (alter == "-2") ? "bb" : "";
                    pitch = X::ChildText(el.body, "step") + acc + X::ChildText(el.body, "octave");
                } else if (X::FindChild(body, "unpitched", el)) {
                    pitch = X::ChildText(el.body, "display-step") + X::ChildText(el.body, "display-octave") + " (unpitched)";
                } else {
                    pitch = "Rest";
                }
                if (X::FindChild(body, "chord", el)) pitch = "+ " + pitch;
                if (X::FindChild(body, "grace", el)) pitch += " (grace)";
                std::string lyric;
                if (X::FindChild(body, "lyric", el)) lyric = X::ChildText(el.body, "text");
                notesSh.rows.push_back({ PathUtils::Utf8ToWide(partId), PathUtils::Utf8ToWide(measureNo), PathUtils::Utf8ToWide(pitch),
                                         PathUtils::Utf8ToWide(X::ChildText(body, "type")), PathUtils::Utf8ToWide(timeSig),
                                         PathUtils::Utf8ToWide(lyric) });
            }
        }
        notesSh.maxCols = 6;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"MusicXML Musical Score (" + (title.empty() ? ext : PathUtils::Utf8ToWide(title)) + L")";
        out.sheets.push_back(std::move(notesSh));
        out.valid = true;
        return true;
    }

    // #97. Adobe XMP / Camera Raw Develop Sidecar (.xmp)
    if (ext == L".xmp") {
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("xmpmeta") == std::string_view::npos && sv.find("rdf:RDF") == std::string_view::npos) return false;

        // Every property of every rdf:Description: attributes (prefix:name="value") and property elements, whose value
        // is their text, their rdf:Alt / Seq / Bag items ("; "), or their structure's fields ("name=value, ...")
        namespace X = ::Explorer2::XmlTagScan;
        SpreadsheetSheet sh;
        sh.name = L"XMP_Properties";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Namespace", L"Property", L"Value" });
        auto forEachAttr = [](std::string_view attrs, auto&& fn) {
            size_t i = 0;
            const size_t n = attrs.size();
            while (i < n) {
                while (i < n && X::IsWs(attrs[i])) ++i;
                size_t nameStart = i;
                while (i < n && X::IsNameChar(attrs[i])) ++i;
                if (i == nameStart) { ++i; continue; }
                std::string_view name = attrs.substr(nameStart, i - nameStart);
                while (i < n && X::IsWs(attrs[i])) ++i;
                if (i >= n || attrs[i] != '=') continue;
                ++i;
                while (i < n && X::IsWs(attrs[i])) ++i;
                if (i >= n || (attrs[i] != '"' && attrs[i] != '\'')) continue;
                char q = attrs[i++];
                size_t valStart = i;
                while (i < n && attrs[i] != q) ++i;
                std::string_view val = attrs.substr(valStart, i - valStart);
                if (i < n) ++i;
                fn(name, val);
            }
        };
        auto isProperty = [](std::string_view name) {
            return name.rfind("xmlns", 0) != 0 && name.rfind("rdf:", 0) != 0 && name.rfind("xml:", 0) != 0;
        };
        auto localOf = [](std::string_view qname) {
            size_t colon = qname.find(':');
            return std::string(colon == std::string_view::npos ? qname : qname.substr(colon + 1));
        };
        auto qnameAt = [](std::string_view doc, size_t tagStart) {
            size_t b = tagStart + 1, e = b;
            while (e < doc.size() && X::IsNameChar(doc[e])) ++e;
            return doc.substr(b, e - b);
        };
        auto propertyValue = [&](std::string_view attrs, std::string_view body, bool selfClosing) {
            std::string value;
            auto append = [&](const std::string& piece, const char* sep) {
                if (piece.empty()) return;
                if (!value.empty()) value += sep;
                value += piece;
            };
            std::string_view resource = X::GetAttr(attrs, "resource");
            if (!resource.empty()) return X::DecodeText(resource);
            forEachAttr(attrs, [&](std::string_view name, std::string_view val) {
                if (isProperty(name)) append(localOf(name) + "=" + X::DecodeText(val), ", ");
            });
            if (selfClosing) return value;
            if (body.find('<') == std::string_view::npos) {
                append(std::string(X::TrimView(X::DecodeText(body))), ", ");
                return value;
            }
            size_t p = 0;
            X::XmlTag c;
            while (X::NextTag(body, p, c)) {
                if (c.isClosing) continue;
                std::string fields;
                forEachAttr(c.attrs, [&](std::string_view name, std::string_view val) {
                    if (!isProperty(name)) return;
                    if (!fields.empty()) fields += ", ";
                    fields += localOf(name) + "=" + X::DecodeText(val);
                });
                const bool container = c.localName == "Alt" || c.localName == "Seq" || c.localName == "Bag" || c.localName == "Description";
                std::string text;
                if (!c.isSelfClosing && !container) {
                    std::string_view inner;
                    size_t after = 0;
                    if (X::FindElementBody(body, c.afterTag, c.localName, inner, after) && inner.find('<') == std::string_view::npos) {
                        text = std::string(X::TrimView(X::DecodeText(inner)));
                        p = after; // a leaf
                    }
                }
                if (c.localName == "li") {
                    append(text.empty() ? fields : (fields.empty() ? text : text + " (" + fields + ")"), "; ");
                } else if (!container && !text.empty()) {
                    append(std::string(c.localName) + "=" + text, ", ");
                } else {
                    append(fields, ", ");
                }
            }
            return value;
        };
        std::string xmpXml = X::DocumentToUtf8(raw.data(), raw.size());
        X::ForEachElement(xmpXml, "Description", [&](const X::Element& d) {
            forEachAttr(d.attrs, [&](std::string_view name, std::string_view val) {
                if (isProperty(name) && sh.rows.size() <= 2000) {
                    size_t colon = name.find(':');
                    sh.rows.push_back({ PathUtils::Utf8ToWide(colon == std::string_view::npos ? std::string_view() : name.substr(0, colon)),
                                        PathUtils::Utf8ToWide(localOf(name)), PathUtils::Utf8ToWide(X::DecodeText(val)) });
                }
            });
            size_t p = 0;
            X::XmlTag c;
            while (X::NextTag(d.body, p, c) && sh.rows.size() <= 2000) {
                if (c.isClosing) continue;
                std::string_view qn = qnameAt(d.body, c.tagStart);
                std::string_view inner;
                if (!c.isSelfClosing) {
                    size_t after = 0;
                    if (!X::FindElementBody(d.body, c.afterTag, c.localName, inner, after)) break;
                    p = after;
                }
                size_t colon = qn.find(':');
                sh.rows.push_back({ PathUtils::Utf8ToWide(colon == std::string_view::npos ? std::string_view() : qn.substr(0, colon)),
                                    PathUtils::Utf8ToWide(localOf(qn)), PathUtils::Utf8ToWide(propertyValue(c.attrs, inner, c.isSelfClosing)) });
            }
            return true;
        });

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Adobe XMP / Lightroom Camera Raw Sidecar (.xmp)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #98. Unreal Engine 4/5 Asset Package (.uasset / .umap — magic 0x9E2A83C1)
    if (ext == L".uasset" || ext == L".umap" || (raw.size() >= 24 && DetailDocs200::RdU32LE(raw.data()) == 0x9E2A83C1u)) {
        if (raw.size() < 24) return false;
        uint32_t tag = DetailDocs200::RdU32LE(raw.data());
        if (tag != 0x9E2A83C1u && tag != 0xC1832A9Eu) return false;

        int32_t legVer  = DetailDocs200::RdI32LE(raw.data() + 4);
        int32_t ue4Ver  = DetailDocs200::RdI32LE(raw.data() + 12);
        size_t pos = 16;
        int32_t ue5Ver = 0;
        if (legVer <= -8 && pos + 4 <= raw.size()) { ue5Ver = DetailDocs200::RdI32LE(raw.data() + pos); pos += 4; }
        if (pos + 8 <= raw.size()) pos += 4; // FileVersionLicenseeUE
        // Skip CustomVersionContainer (count * 20 bytes)
        if (legVer <= -2 && pos + 4 <= raw.size()) {
            uint32_t custCnt = DetailDocs200::RdU32LE(raw.data() + pos); pos += 4;
            if (custCnt < 4096 && pos + custCnt * 20ull <= raw.size()) pos += custCnt * 20ull;
        }
        uint32_t totalHdrSz = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        if (pos + 4 <= raw.size()) pos += 4;

        auto readFString = [&](size_t& p) -> std::wstring {
            if (p + 4 > raw.size()) return {};
            int32_t slen = DetailDocs200::RdI32LE(raw.data() + p); p += 4;
            if (slen > 0 && slen < 1024 && p + static_cast<size_t>(slen) <= raw.size()) {
                std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + p), slen - 1));
                p += static_cast<size_t>(slen);
                return s;
            }
            if (slen < 0 && -slen < 1024 && p + static_cast<size_t>(-slen) * 2 <= raw.size()) {
                size_t bytes = static_cast<size_t>(-slen) * 2;
                std::wstring s = DetailDocs200::ReadUtf16LE(raw.data() + p, bytes);
                p += bytes;
                return s;
            }
            return {};
        };

        std::wstring folderName = readFString(pos);
        uint32_t pkgFlags = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        if (pos + 4 <= raw.size()) pos += 4;
        uint32_t nameCnt  = (pos + 4 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos) : 0;
        uint32_t nameOff  = (pos + 8 <= raw.size()) ? DetailDocs200::RdU32LE(raw.data() + pos + 4) : 0;

        SpreadsheetSheet hdrSh;
        hdrSh.name = L"Package_Summary";
        hdrSh.maxCols = 2;
        hdrSh.rows.push_back({ L"FPackageFileSummary Field", L"Value" });
        hdrSh.rows.push_back({ L"Engine Generation", (legVer <= -8) ? (L"Unreal Engine 5 (v" + std::to_wstring(ue5Ver) + L")")
                                                                    : (L"Unreal Engine 4 (v" + std::to_wstring(ue4Ver) + L")") });
        hdrSh.rows.push_back({ L"Package Folder", folderName.empty() ? L"None" : folderName });
        hdrSh.rows.push_back({ L"Total Header Size", std::to_wstring(totalHdrSz) + L" B" });
        hdrSh.rows.push_back({ L"Package Flags", DetailSys100::Hex32(pkgFlags) });
        hdrSh.rows.push_back({ L"NameMap Count", std::to_wstring(nameCnt) + L" @ offset " + std::to_wstring(nameOff) });

        SpreadsheetSheet nmSh;
        nmSh.name = L"NameMap";
        nmSh.maxCols = 2;
        nmSh.rows.push_back({ L"Name Index", L"FName Identifier" });
        if (nameCnt > 0 && nameOff > 0 && nameOff < raw.size()) {
            size_t np = nameOff;
            for (uint32_t i = 0; i < std::min<uint32_t>(nameCnt, 300) && np + 4 <= raw.size(); ++i) {
                std::wstring nm = readFString(np);
                if (nm.empty()) break;
                if (ue4Ver >= 504 && np + 4 <= raw.size()) np += 4; // NonCasePreservingHash + CasePreservingHash (2x uint16)
                nmSh.rows.push_back({ std::to_wstring(i), nm });
            }
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".umap") ? L"Unreal Engine Level Map (.umap)" : L"Unreal Engine Asset Package (.uasset)";
        out.sheets.push_back(std::move(hdrSh));
        if (nmSh.rows.size() > 1) out.sheets.push_back(std::move(nmSh));
        out.valid = true;
        return true;
    }

    // #99. Unity YAML Prefab & Scene Hierarchy (.prefab / .unity)
    if (ext == L".prefab" || ext == L".unity") {
        std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (sv.find("--- !u!") == std::string_view::npos && sv.find("%YAML") == std::string_view::npos) return false;

        auto unityClassName = [](int cid) -> std::wstring {
            switch (cid) {
            case 1:   return L"GameObject";
            case 4:   return L"Transform";
            case 20:  return L"Camera";
            case 23:  return L"MeshRenderer";
            case 33:  return L"MeshFilter";
            case 54:  return L"Rigidbody";
            case 65:  return L"BoxCollider";
            case 82:  return L"AudioSource";
            case 104: return L"RenderSettings";
            case 108: return L"Light";
            case 114: return L"MonoBehaviour";
            case 137: return L"SkinnedMeshRenderer";
            case 212: return L"SpriteRenderer";
            case 222: return L"CanvasRenderer";
            case 223: return L"Canvas";
            case 224: return L"RectTransform";
            default:  return L"ClassID_" + std::to_wstring(cid);
            }
        };

        SpreadsheetSheet sh;
        sh.name = L"Unity_Hierarchy";
        sh.maxCols = 4;
        sh.rows.push_back({ L"FileID", L"Unity Class", L"m_Name", L"Position / Reference" });

        size_t pos = 0;
        while ((pos = sv.find("--- !u!", pos)) != std::string_view::npos && sh.rows.size() < 500) {
            pos += 7;
            int cid = std::atoi(std::string(sv.substr(pos, std::min<size_t>(12, sv.size() - pos))).c_str());
            size_t amp = sv.find('&', pos);
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) break;
            std::string fid = (amp != std::string_view::npos && amp < eol)
                              ? std::string(sv.substr(amp + 1, eol - amp - 1)) : "0";
            if (!fid.empty() && fid.back() == '\r') fid.pop_back();

            size_t nextObj = sv.find("--- !u!", eol);
            std::string_view blk = sv.substr(eol, (nextObj == std::string_view::npos ? sv.size() : nextObj) - eol);

            auto yamlField = [&](std::string_view key) -> std::string {
                size_t kp = blk.find(key);
                if (kp == std::string_view::npos) return {};
                kp += key.size();
                size_t ke = blk.find('\n', kp);
                std::string_view v = blk.substr(kp, (ke == std::string_view::npos ? blk.size() : ke) - kp);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.remove_suffix(1);
                return std::string(v);
            };

            std::string mName = yamlField("m_Name:");
            std::string detail = yamlField("m_LocalPosition:");
            if (detail.empty()) detail = yamlField("m_GameObject:");
            sh.rows.push_back({ PathUtils::Utf8ToWide(fid), unityClassName(cid),
                                PathUtils::Utf8ToWide(mName), PathUtils::Utf8ToWide(detail) });
            pos = eol;
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".unity") ? L"Unity Engine YAML Scene (.unity)" : L"Unity Engine YAML Prefab (.prefab)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #100a. Microsoft Visio Drawing Package (.vsdx — OPC ZIP + Visio Page XML)
    if (ext == L".vsdx" && raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) return false;

        bool hasVisioPart = false;
        SpreadsheetSheet sh;
        sh.name = L"Visio_Shapes";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Part / Page", L"Shape / Component", L"Text / Size" });

        for (const auto& e : entries) {
            std::wstring lp = PathUtils::ToLower(e.pathInArchive);
            std::replace(lp.begin(), lp.end(), L'\\', L'/');
            if (lp.find(L"visio/") != std::wstring::npos) hasVisioPart = true;
            if (lp.find(L"visio/pages/page") != std::wstring::npos && PathUtils::GetExtension(lp) == L".xml") {
                std::vector<uint8_t> pgBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, pgBytes) && !pgBytes.empty()) {
                    std::string_view psv(reinterpret_cast<const char*>(pgBytes.data()), pgBytes.size());
                    size_t tp = 0;
                    int sIdx = 0;
                    while ((tp = psv.find("<Text", tp)) != std::string_view::npos && sh.rows.size() < 300) {
                        size_t gt = psv.find('>', tp);
                        size_t te = (gt != std::string_view::npos) ? psv.find("</Text>", gt + 1) : std::string_view::npos;
                        if (te == std::string_view::npos) break;
                        std::string rawTxt(psv.substr(gt + 1, te - gt - 1));
                        std::string clean;
                        bool inTag = false;
                        for (char c : rawTxt) {
                            if (c == '<') inTag = true;
                            else if (c == '>') inTag = false;
                            else if (!inTag) clean.push_back(c);
                        }
                        std::wstring wtxt = PathUtils::Trim(PathUtils::Utf8ToWide(clean));
                        if (!wtxt.empty()) {
                            sh.rows.push_back({ e.name, L"Shape #" + std::to_wstring(++sIdx), wtxt });
                        }
                        tp = te + 7;
                    }
                }
            }
        }
        if (!hasVisioPart) return false;
        if (sh.rows.size() == 1) {
            for (const auto& e : entries) {
                if (!e.isDirectory && sh.rows.size() < 200) {
                    sh.rows.push_back({ e.pathInArchive, L"Visio Part", PathUtils::FormatByteSize(e.uncompressedSize) });
                }
            }
        }
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Microsoft Visio Drawing Package (.vsdx)";
        out.sheets.push_back(std::move(sh));
        out.valid = true;
        return true;
    }

    // #100b. Apple iWork '13+ (.iwa Snappy-Framed Protobuf) & iWork '09 (index.xml / index.apxl) Packages
    if ((ext == L".pages" || ext == L".numbers" || ext == L".key") &&
        raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K')
    {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries)) return false;

        auto snappyDecompress = [](const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dst) {
            return ::Explorer2::StreamCodecs::SnappyDecompress(src, srcLen, dst, 16 * 1024 * 1024, true);
        };

        auto unframeIwa = [&](const std::vector<uint8_t>& framed, std::vector<uint8_t>& outStream) -> bool {
            outStream.clear();
            if (framed.size() < 4) return false;
            size_t pos = 0;
            size_t validChunks = 0;
            bool sawSnappyIdent = false;
            while (pos + 4 <= framed.size()) {
                const uint8_t chunkType = framed[pos];
                const uint32_t chunkLen = static_cast<uint32_t>(framed[pos + 1]) |
                                          (static_cast<uint32_t>(framed[pos + 2]) << 8) |
                                          (static_cast<uint32_t>(framed[pos + 3]) << 16);
                pos += 4;
                if (pos + chunkLen > framed.size()) return false;
                const uint8_t* payload = framed.data() + pos;
                if (chunkType == 0xFFu && chunkLen == 6u && std::memcmp(payload, "sNaPpY", 6) == 0) {
                    sawSnappyIdent = true;
                } else if (chunkType == 0x00u) {
                    std::vector<uint8_t> blk;
                    bool ok = snappyDecompress(payload, chunkLen, blk);
                    if (!ok && sawSnappyIdent && chunkLen > 4u) {
                        ok = snappyDecompress(payload + 4, chunkLen - 4u, blk);
                    }
                    if (!ok) return false;
                    if (outStream.size() + blk.size() > 32u * 1024u * 1024u) return false;
                    outStream.insert(outStream.end(), blk.begin(), blk.end());
                    ++validChunks;
                } else if (chunkType == 0x01u) {
                    const size_t crcSkip = (sawSnappyIdent && chunkLen >= 4u) ? 4u : 0u;
                    if (outStream.size() + (chunkLen - crcSkip) > 32u * 1024u * 1024u) return false;
                    outStream.insert(outStream.end(), payload + crcSkip, payload + chunkLen);
                    ++validChunks;
                } else {
                    return false;
                }
                pos += chunkLen;
            }
            return pos == framed.size() && validChunks > 0 && !outStream.empty();
        };

        auto pbSkip = [](const uint8_t* buf, size_t sz, size_t& p, uint8_t wt) -> bool {
            if (wt == 0u) { uint64_t v = 0; return DetailDocs200::ReadVarint64(buf, sz, p, v); }
            if (wt == 1u) { if (p + 8u > sz) return false; p += 8u; return true; }
            if (wt == 2u) {
                uint64_t l = 0;
                if (!DetailDocs200::ReadVarint64(buf, sz, p, l) || l > sz - p) return false;
                p += static_cast<size_t>(l);
                return true;
            }
            if (wt == 5u) { if (p + 4u > sz) return false; p += 4u; return true; }
            return false;
        };

        auto cleanIworkText = [](std::string_view utf8) -> std::vector<std::wstring> {
            // Strip U+FFFC (EF BF BC) object replacement markers and split paragraphs
            std::string cleaned;
            cleaned.reserve(utf8.size());
            for (size_t i = 0; i < utf8.size(); ++i) {
                const uint8_t b = static_cast<uint8_t>(utf8[i]);
                if (b == 0xEFu && i + 2 < utf8.size() &&
                    static_cast<uint8_t>(utf8[i + 1]) == 0xBFu && static_cast<uint8_t>(utf8[i + 2]) == 0xBCu)
                {
                    i += 2;
                    continue;
                }
                if (b == 0xE2u && i + 2 < utf8.size() &&
                    static_cast<uint8_t>(utf8[i + 1]) == 0x80u &&
                    (static_cast<uint8_t>(utf8[i + 2]) == 0xA8u || static_cast<uint8_t>(utf8[i + 2]) == 0xA9u))
                {
                    cleaned.push_back('\n');
                    i += 2;
                    continue;
                }
                if (b == '\r') {
                    if (i + 1 < utf8.size() && utf8[i + 1] == '\n') ++i;
                    cleaned.push_back('\n');
                } else if (b == '\n' || b == '\t' || b >= 0x20u) {
                    cleaned.push_back(static_cast<char>(b));
                }
            }
            std::vector<std::wstring> lines;
            size_t st = 0;
            while (st <= cleaned.size()) {
                size_t nl = cleaned.find('\n', st);
                std::string_view seg = (nl == std::string::npos)
                    ? std::string_view(cleaned).substr(st)
                    : std::string_view(cleaned).substr(st, nl - st);
                std::wstring w = PathUtils::Trim(PathUtils::Utf8ToWide(std::string(seg)));
                if (!w.empty()) lines.push_back(std::move(w));
                if (nl == std::string::npos) break;
                st = nl + 1;
            }
            return lines;
        };

        SpreadsheetSheet sh;
        sh.name = L"Package_And_Text";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Archive Stream", L"Object / Message Type", L"Extracted Content" });

        SpreadsheetSheet cellsSh;
        cellsSh.name = L"Numbers_Table_Data";
        cellsSh.maxCols = 3;
        cellsSh.rows.push_back({ L"Table / Stream", L"Entry Key", L"Cell Value" });

        size_t validIwaFiles = 0;
        size_t validIwaObjects = 0;
        bool validLegacyXml = false;
        std::wstring quickLookNote;

        for (const auto& e : entries) {
            if (e.isDirectory) continue;
            std::wstring lp = PathUtils::ToLower(e.pathInArchive);
            std::replace(lp.begin(), lp.end(), L'\\', L'/');

            if (lp.find(L"quicklook/") == 0 && (PathUtils::GetExtension(lp) == L".pdf" || PathUtils::GetExtension(lp) == L".jpg")) {
                if (!quickLookNote.empty()) quickLookNote += L", ";
                quickLookNote += e.pathInArchive + L" (" + PathUtils::FormatByteSize(e.uncompressedSize) + L")";
                continue;
            }

            if (PathUtils::GetExtension(lp) == L".iwa" && e.uncompressedSize <= 32u * 1024u * 1024u) {
                std::vector<uint8_t> rawIwa, decomp;
                if (!ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, rawIwa) || rawIwa.empty()) continue;
                if (!unframeIwa(rawIwa, decomp)) continue;

                // Walk decompressed .iwa Protobuf stream: [archiveInfoLen: varint][ArchiveInfo][MessagePayloads...]
                size_t pos = 0;
                bool streamOk = true;
                size_t localObjs = 0;
                while (pos < decomp.size()) {
                    uint64_t aiLen = 0;
                    if (!DetailDocs200::ReadVarint64(decomp.data(), decomp.size(), pos, aiLen) ||
                        aiLen == 0 || aiLen > decomp.size() - pos)
                    {
                        streamOk = false;
                        break;
                    }
                    const uint8_t* aiBuf = decomp.data() + pos;
                    const size_t aiSize = static_cast<size_t>(aiLen);
                    pos += aiSize;

                    uint64_t archiveId = 0;
                    struct MsgDesc { uint32_t type = 0; size_t length = 0; };
                    std::vector<MsgDesc> msgs;

                    size_t ap = 0;
                    while (ap < aiSize) {
                        uint64_t tag = 0;
                        if (!DetailDocs200::ReadVarint64(aiBuf, aiSize, ap, tag) || tag == 0) { streamOk = false; break; }
                        const uint32_t fn = static_cast<uint32_t>(tag >> 3);
                        const uint8_t wt  = static_cast<uint8_t>(tag & 0x07u);
                        if (fn == 1 && wt == 0u) {
                            if (!DetailDocs200::ReadVarint64(aiBuf, aiSize, ap, archiveId)) { streamOk = false; break; }
                        } else if (fn == 2 && wt == 2u) {
                            uint64_t miLen = 0;
                            if (!DetailDocs200::ReadVarint64(aiBuf, aiSize, ap, miLen) || miLen > aiSize - ap) { streamOk = false; break; }
                            const uint8_t* miBuf = aiBuf + ap;
                            const size_t miSize = static_cast<size_t>(miLen);
                            ap += miSize;
                            MsgDesc md;
                            size_t mp = 0;
                            while (mp < miSize) {
                                uint64_t mtag = 0;
                                if (!DetailDocs200::ReadVarint64(miBuf, miSize, mp, mtag) || mtag == 0) { streamOk = false; break; }
                                const uint32_t mfn = static_cast<uint32_t>(mtag >> 3);
                                const uint8_t mwt  = static_cast<uint8_t>(mtag & 0x07u);
                                if (mfn == 1 && mwt == 0u) {
                                    uint64_t tv = 0;
                                    if (!DetailDocs200::ReadVarint64(miBuf, miSize, mp, tv)) { streamOk = false; break; }
                                    md.type = static_cast<uint32_t>(tv);
                                } else if (mfn == 3 && mwt == 0u) {
                                    uint64_t lv = 0;
                                    if (!DetailDocs200::ReadVarint64(miBuf, miSize, mp, lv) || lv > decomp.size()) { streamOk = false; break; }
                                    md.length = static_cast<size_t>(lv);
                                } else if (!pbSkip(miBuf, miSize, mp, mwt)) {
                                    streamOk = false;
                                    break;
                                }
                            }
                            if (!streamOk) break;
                            msgs.push_back(md);
                        } else if (!pbSkip(aiBuf, aiSize, ap, wt)) {
                            streamOk = false;
                            break;
                        }
                    }
                    if (!streamOk) break;

                    for (const auto& md : msgs) {
                        if (md.length > decomp.size() - pos) { streamOk = false; break; }
                        const uint8_t* mPtr = decomp.data() + pos;
                        const size_t mLen = md.length;
                        pos += mLen;
                        ++localObjs;

                        // TSWP::StorageArchive (type 2001..2025): field 3 = repeated string text
                        if (md.type >= 2000u && md.type <= 2030u) {
                            size_t sp = 0;
                            while (sp < mLen) {
                                uint64_t stag = 0;
                                if (!DetailDocs200::ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                                const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                                const uint8_t swt  = static_cast<uint8_t>(stag & 0x07u);
                                if (sfn == 3 && swt == 2u) {
                                    uint64_t sLen = 0;
                                    if (!DetailDocs200::ReadVarint64(mPtr, mLen, sp, sLen) || sLen > mLen - sp) break;
                                    std::string_view sv(reinterpret_cast<const char*>(mPtr + sp), static_cast<size_t>(sLen));
                                    sp += static_cast<size_t>(sLen);
                                    for (auto& line : cleanIworkText(sv)) {
                                        if (sh.rows.size() <= 500) {
                                            sh.rows.push_back({
                                                e.pathInArchive,
                                                L"TSWP::StorageArchive (#" + std::to_wstring(archiveId) + L")",
                                                std::move(line)
                                            });
                                        }
                                    }
                                } else if (!pbSkip(mPtr, mLen, sp, swt)) {
                                    break;
                                }
                            }
                        }
                        // TST::TableDataList (type 6005 / 6201): field 3 = repeated ListEntry { key=1, string=3 / number=6 }
                        else if (md.type == 6005u || md.type == 6201u) {
                            size_t sp = 0;
                            while (sp < mLen) {
                                uint64_t stag = 0;
                                if (!DetailDocs200::ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                                const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                                const uint8_t swt  = static_cast<uint8_t>(stag & 0x07u);
                                if (sfn == 3 && swt == 2u) {
                                    uint64_t eLen = 0;
                                    if (!DetailDocs200::ReadVarint64(mPtr, mLen, sp, eLen) || eLen > mLen - sp) break;
                                    const uint8_t* ePtr = mPtr + sp;
                                    const size_t eSz = static_cast<size_t>(eLen);
                                    sp += eSz;
                                    uint64_t key = 0;
                                    std::wstring cellVal;
                                    size_t ep = 0;
                                    while (ep < eSz) {
                                        uint64_t etag = 0;
                                        if (!DetailDocs200::ReadVarint64(ePtr, eSz, ep, etag) || etag == 0) break;
                                        const uint32_t efn = static_cast<uint32_t>(etag >> 3);
                                        const uint8_t ewt  = static_cast<uint8_t>(etag & 0x07u);
                                        if (efn == 1 && ewt == 0u) {
                                            DetailDocs200::ReadVarint64(ePtr, eSz, ep, key);
                                        } else if ((efn == 3 || efn == 8) && ewt == 2u) {
                                            uint64_t sl = 0;
                                            if (!DetailDocs200::ReadVarint64(ePtr, eSz, ep, sl) || sl > eSz - ep) break;
                                            cellVal = PathUtils::Trim(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(ePtr + ep), static_cast<size_t>(sl))));
                                            ep += static_cast<size_t>(sl);
                                        } else if (efn == 6 && ewt == 1u && ep + 8u <= eSz) {
                                            uint64_t bits = DetailDocs200::RdU64LE(ePtr + ep);
                                            ep += 8u;
                                            double dv = 0.0;
                                            std::memcpy(&dv, &bits, 8);
                                            wchar_t dbuf[64] = {};
                                            swprintf_s(dbuf, L"%.6g", dv);
                                            cellVal = dbuf;
                                        } else if (!pbSkip(ePtr, eSz, ep, ewt)) {
                                            break;
                                        }
                                    }
                                    if (!cellVal.empty()) {
                                        if (cellsSh.rows.size() <= 500) {
                                            cellsSh.rows.push_back({ e.pathInArchive, std::to_wstring(key), cellVal });
                                        }
                                        if (sh.rows.size() <= 500) {
                                            sh.rows.push_back({ e.pathInArchive, L"TST::TableDataList [#" + std::to_wstring(key) + L"]", cellVal });
                                        }
                                    }
                                } else if (!pbSkip(mPtr, mLen, sp, swt)) {
                                    break;
                                }
                            }
                        }
                        // TST::TableModelArchive (6001) / TN::SheetArchive (2): extract table/sheet name
                        else if (md.type == 6001u || md.type == 2u) {
                            size_t sp = 0;
                            while (sp < mLen) {
                                uint64_t stag = 0;
                                if (!DetailDocs200::ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                                const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                                const uint8_t swt  = static_cast<uint8_t>(stag & 0x07u);
                                if (((md.type == 6001u && (sfn == 4u || sfn == 8u)) || (md.type == 2u && sfn == 1u)) && swt == 2u) {
                                    uint64_t sl = 0;
                                    if (!DetailDocs200::ReadVarint64(mPtr, mLen, sp, sl) || sl > mLen - sp) break;
                                    if (sl > 0 && sl <= 256u && DetailDocs200::IsMostlyPrintableUtf8(mPtr + sp, static_cast<size_t>(sl))) {
                                        std::wstring nm = PathUtils::Trim(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(mPtr + sp), static_cast<size_t>(sl))));
                                        if (!nm.empty() && sh.rows.size() <= 500) {
                                            sh.rows.push_back({
                                                e.pathInArchive,
                                                (md.type == 2u) ? L"TN::SheetArchive" : L"TST::TableModelArchive",
                                                std::move(nm)
                                            });
                                        }
                                    }
                                    sp += static_cast<size_t>(sl);
                                } else if (!pbSkip(mPtr, mLen, sp, swt)) {
                                    break;
                                }
                            }
                        }
                    }
                    if (!streamOk) break;
                }

                if (streamOk && localObjs > 0) {
                    ++validIwaFiles;
                    validIwaObjects += localObjs;
                }
            }
            // iWork '09 XML format: index.xml / index.apxl (plain or gzip-compressed inside ZIP)
            else if (lp == L"index.xml" || lp == L"index.apxl" || lp == L"index.xml.gz" || lp == L"index.apxl.gz") {
                std::vector<uint8_t> xmlRaw, xmlBuf;
                if (!ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, xmlRaw) || xmlRaw.empty()) continue;
                if (xmlRaw.size() >= 2 && xmlRaw[0] == 0x1Fu && xmlRaw[1] == 0x8Bu) {
                    if (!ZipDeflate::InflateGzip(xmlRaw.data(), xmlRaw.size(), xmlBuf)) continue;
                } else {
                    xmlBuf = std::move(xmlRaw);
                }
                std::string_view xsv(reinterpret_cast<const char*>(xmlBuf.data()), xmlBuf.size());
                if (xsv.find("<ls:document") == std::string_view::npos &&
                    xsv.find("<sl:document") == std::string_view::npos &&
                    xsv.find("<key:presentation") == std::string_view::npos &&
                    xsv.find("xmlns:sf=") == std::string_view::npos)
                {
                    continue;
                }
                validLegacyXml = true;
                size_t pPos = 0;
                while ((pPos = xsv.find("<sf:p", pPos)) != std::string_view::npos && sh.rows.size() <= 500) {
                    size_t gt = xsv.find('>', pPos);
                    if (gt == std::string_view::npos) break;
                    if (xsv[gt - 1] == '/') { pPos = gt + 1; continue; }
                    size_t closeP = xsv.find("</sf:p>", gt + 1);
                    if (closeP == std::string_view::npos) break;
                    std::string inner(xsv.substr(gt + 1, closeP - gt - 1));
                    std::string plain;
                    bool inTag = false;
                    for (char ch : inner) {
                        if (ch == '<') inTag = true;
                        else if (ch == '>') inTag = false;
                        else if (!inTag) plain.push_back(ch);
                    }
                    std::wstring wtxt = PathUtils::Trim(PathUtils::Utf8ToWide(plain));
                    if (!wtxt.empty()) {
                        sh.rows.push_back({ e.pathInArchive, L"iWork '09 Paragraph", std::move(wtxt) });
                    }
                    pPos = closeP + 7;
                }
            }
        }

        // Strict anti-cheat: if ZIP contains ZERO valid .iwa Snappy-Protobuf streams and ZERO iWork '09 index.xml/apxl, reject!
        if (validIwaFiles == 0 && !validLegacyXml) {
            return false;
        }

        if (validIwaFiles > 0) {
            sh.rows.insert(sh.rows.begin() + 1, {
                L"Package Summary",
                L"iWork '13+ Archive",
                std::to_wstring(validIwaFiles) + L" .iwa streams, " + std::to_wstring(validIwaObjects) + L" protobuf objects"
            });
        }
        if (!quickLookNote.empty()) {
            sh.rows.push_back({ L"QuickLook Preview", L"Supplemental Preview", quickLookNote });
        }

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = (ext == L".pages")   ? L"Apple Pages Document Package (.pages)" :
                          (ext == L".numbers") ? L"Apple Numbers Spreadsheet Package (.numbers)" :
                                                 L"Apple Keynote Presentation Package (.key)";
        out.sheets.push_back(std::move(sh));
        if (cellsSh.rows.size() > 1) {
            out.sheets.push_back(std::move(cellsSh));
        }
        out.valid = true;
        return true;
    }

    return false;
}

// ============================================================================
// Master Dispatcher for ExtendedDocs200 (#81–#100)
// ============================================================================
inline bool ParseExtendedDocs200(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (extLower == L".accdb" || extLower == L".mdb") return ParseAccessDatabase200(raw, extLower, out);
    if (extLower == L".rdb") return ParseRedisRdb200(raw, out);
    if (extLower == L".sst" || extLower == L".ldb") return ParseRocksLevelDbSst200(raw, extLower, out);
    if (extLower == L".pmtiles" || extLower == L".mbtiles") return ParseMapTiles200(raw, extLower, out);
    if (extLower == L".pb" || extLower == L".pbf") return ParseProtobufWire200(raw, extLower, out);
    if (extLower == L".tfstate" || extLower == L".sarif" || extLower == L".spdx" || extLower == L".cdx") {
        return ParseCloudSecOps200(raw, extLower, out);
    }
    if (extLower == L".gpg" || extLower == L".pgp" || extLower == L".sig" || extLower == L".jks" || extLower == L".cat") {
        return ParseCryptoSec200(raw, extLower, out);
    }
    if (extLower == L".hiv" || extLower == L".dat" || extLower == L".etl" || extLower == L".pol") {
        return ParseWindowsForensics200(raw, extLower, out);
    }
    if (extLower == L".sid" || extLower == L".nsf" || extLower == L".spc" || extLower == L".vgm" ||
        extLower == L".musicxml" || extLower == L".mxl" || extLower == L".xmp" ||
        extLower == L".uasset" || extLower == L".umap" || extLower == L".prefab" || extLower == L".unity" ||
        extLower == L".pages" || extLower == L".numbers" || extLower == L".key" || extLower == L".vsdx")
    {
        return ParseMediaGameOffice200(raw, extLower, out);
    }
    return false;
}

} // namespace Formats
