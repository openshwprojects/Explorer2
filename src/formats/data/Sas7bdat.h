#pragma once
// Sas7bdat.h — SAS7BDAT Binary Dataset (.sas7bdat) Reader

namespace Formats {
namespace AiDataDetail {

// ============================================================================
// 13. SAS7BDAT Binary Dataset (.sas7bdat)
// ============================================================================
inline bool ParseSas7bdat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    static const uint8_t kSasMagic[32] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0xC2,0xEA,0x81,0x60,
        0xB3,0x14,0x11,0xCF,0xBD,0x92,0x08,0x00, 0x09,0xC7,0x31,0x8C,0x18,0x1F,0x10,0x11
    };
    if (raw.size() < 288 || std::memcmp(raw.data(), kSasMagic, 32) != 0) return false;

    bool u64 = (raw[32] == 0x33);
    size_t align1 = (raw[32] == 0x33) ? 4 : 0;
    size_t align2 = (raw[35] == 0x33) ? 4 : 0;
    bool le = (raw[37] == 0x01);

    auto rdU16 = [&](const uint8_t* p) { return le ? RdU16LE(p) : RdU16BE(p); };
    auto rdU32 = [&](const uint8_t* p) { return le ? RdU32LE(p) : RdU32BE(p); };
    auto rdInt = [&](const uint8_t* p) -> uint64_t {
        return u64 ? (le ? RdU64LE(p) : RdU64BE(p)) : rdU32(p);
    };

    std::wstring dsName = CleanAsciiFixed(raw.data() + 92, 32);
    size_t hdrLenOff = 196 + align1;
    if (hdrLenOff + 16 > raw.size()) return false;
    uint32_t headerLen = rdU32(raw.data() + hdrLenOff);
    uint32_t pageSize  = rdU32(raw.data() + hdrLenOff + 4);
    uint64_t pageCount = u64 ? (le ? RdU64LE(raw.data() + hdrLenOff + 8) : RdU64BE(raw.data() + hdrLenOff + 8))
                             : rdU32(raw.data() + hdrLenOff + 8);
    (void)align2;
    if (headerLen < 256 || pageSize < 256 || headerLen > raw.size()) return false;

    struct SasCol {
        std::wstring name;
        uint64_t offset = 0;
        uint32_t width = 0;
        uint8_t type = 1; // 1 = Numeric, 2 = Character
    };
    std::vector<SasCol> cols;
    std::vector<std::string> colTexts;
    uint64_t rowLength = 0;
    uint64_t totalRows = 0;
    uint64_t colCount = 0;
    uint64_t mixRowCount = 0;

    std::vector<std::vector<std::wstring>> decodedRows;
    size_t intSize = u64 ? 8 : 4;
    size_t pageHdrOff = u64 ? 32 : 16;
    size_t subPtrSize = u64 ? 24 : 12;

    size_t maxPages = static_cast<size_t>(std::min<uint64_t>(pageCount > 0 ? pageCount : 64, 64));
    for (size_t pg = 0; pg < maxPages; ++pg) {
        size_t pgPos = headerLen + pg * static_cast<size_t>(pageSize);
        if (pgPos + pageHdrOff + 8 > raw.size()) break;
        size_t availPg = std::min<size_t>(pageSize, raw.size() - pgPos);
        const uint8_t* pp = raw.data() + pgPos;

        uint16_t pageType   = rdU16(pp + pageHdrOff);
        uint16_t blockCount = rdU16(pp + pageHdrOff + 2);
        uint16_t subCount   = rdU16(pp + pageHdrOff + 4);

        // Parse metadata subheaders on META (0), MIX (512/640), or AMD (1024) pages
        if (pageType == 0 || pageType == 512 || pageType == 640 || pageType == 1024) {
            for (uint16_t s = 0; s < subCount; ++s) {
                size_t ptrOff = pageHdrOff + 8 + static_cast<size_t>(s) * subPtrSize;
                if (ptrOff + subPtrSize > availPg) break;
                uint64_t shOff = rdInt(pp + ptrOff);
                uint64_t shLen = rdInt(pp + ptrOff + intSize);
                uint8_t compFlag = pp[ptrOff + 2 * intSize];
                if (shLen < 4 || compFlag == 1 || shOff + shLen > availPg) continue;

                const uint8_t* sp = pp + static_cast<size_t>(shOff);
                uint32_t sig = RdU32LE(sp);
                if (u64 && shLen >= 8) {
                    uint32_t sigHi = RdU32LE(sp + 4);
                    if (sig == 0xFFFFFFFFu && sigHi != 0xFFFFFFFFu) sig = sigHi;
                }

                if ((sig == 0xF7F7F7F7u || sig == 0x000000F7u) && shLen >= 12 * intSize) { // RowSize
                    rowLength   = rdInt(sp + 5 * intSize);
                    totalRows   = rdInt(sp + 6 * intSize);
                    mixRowCount = rdInt(sp + ((16 * intSize <= shLen) ? (15 * intSize) : (6 * intSize)));
                } else if ((sig == 0xF6F6F6F6u || sig == 0x000000F6u) && shLen >= 2 * intSize) { // ColumnSize
                    colCount = rdInt(sp + intSize);
                    if (colCount <= 4096 && cols.size() < colCount) cols.resize(static_cast<size_t>(colCount));
                } else if (sig == 0xFDFFFFFFu || sig == 0xFFFFFFFDu) { // ColumnText
                    size_t txtHdr = intSize + 2;
                    if (shLen > txtHdr) {
                        colTexts.emplace_back(reinterpret_cast<const char*>(sp + intSize), static_cast<size_t>(shLen - intSize));
                    }
                } else if (sig == 0xFFFFFFFFu && shLen > intSize + 8) { // ColumnName
                    size_t entryLen = u64 ? 8 : 8;
                    size_t cur = intSize + 8;
                    size_t cIdx = 0;
                    while (cur + entryLen <= shLen && cIdx < 4096) {
                        uint16_t txtIdx  = rdU16(sp + cur);
                        uint16_t nameOff = rdU16(sp + cur + 2);
                        uint16_t nameLen = rdU16(sp + cur + 4);
                        if (cols.size() <= cIdx) cols.resize(cIdx + 1);
                        if (txtIdx < colTexts.size() && static_cast<size_t>(nameOff) + nameLen <= colTexts[txtIdx].size()) {
                            std::string nm = colTexts[txtIdx].substr(nameOff, nameLen);
                            cols[cIdx].name = PathUtils::Utf8ToWide(nm);
                        }
                        ++cIdx;
                        cur += entryLen;
                    }
                } else if ((sig == 0xFCFFFFFFu || sig == 0xFFFFFFFCu) && shLen > intSize + 8) { // ColumnAttrs
                    size_t entryLen = u64 ? 16 : 12;
                    size_t cur = intSize + 8;
                    size_t cIdx = 0;
                    while (cur + entryLen <= shLen && cIdx < 4096) {
                        if (cols.size() <= cIdx) cols.resize(cIdx + 1);
                        cols[cIdx].offset = rdInt(sp + cur);
                        cols[cIdx].width  = rdU32(sp + cur + intSize);
                        cols[cIdx].type   = sp[cur + intSize + 6];
                        ++cIdx;
                        cur += entryLen;
                    }
                }
            }
        }

        // Decode uncompressed rows on MIX (512/640) or DATA (256) pages
        if ((pageType == 256 || pageType == 512 || pageType == 640) && rowLength > 0 && !cols.empty() && decodedRows.size() < 500) {
            size_t dataOff = pageHdrOff + 8;
            size_t rowsOnPage = 0;
            if (pageType == 256) {
                rowsOnPage = blockCount;
            } else {
                dataOff = pageHdrOff + 8 + static_cast<size_t>(subCount) * subPtrSize;
                dataOff = (dataOff + 7u) & ~size_t(7u);
                rowsOnPage = static_cast<size_t>(std::min<uint64_t>(mixRowCount > 0 ? mixRowCount : totalRows, 500));
            }
            for (size_t r = 0; r < rowsOnPage && decodedRows.size() < 500 && dataOff + rowLength <= availPg; ++r, dataOff += static_cast<size_t>(rowLength)) {
                if (totalRows > 0 && decodedRows.size() >= totalRows) break;
                const uint8_t* rp = pp + dataOff;
                std::vector<std::wstring> rvec(cols.size());
                for (size_t c = 0; c < cols.size(); ++c) {
                    if (cols[c].width == 0 || cols[c].offset + cols[c].width > rowLength) continue;
                    const uint8_t* cp = rp + static_cast<size_t>(cols[c].offset);
                    if (cols[c].type == 2) {
                        rvec[c] = PathUtils::Trim(CleanAsciiFixed(cp, cols[c].width));
                    } else if (cols[c].width <= 8) {
                        uint8_t dblBytes[8] = {};
                        if (le) {
                            std::memcpy(dblBytes + (8 - cols[c].width), cp, cols[c].width);
                            double dv = RdF64LE(dblBytes);
                            rvec[c] = std::isfinite(dv) ? FmtDouble(dv) : L".";
                        } else {
                            std::memcpy(dblBytes, cp, cols[c].width);
                            double dv = RdF64BE(dblBytes);
                            rvec[c] = std::isfinite(dv) ? FmtDouble(dv) : L".";
                        }
                    }
                }
                decodedRows.push_back(std::move(rvec));
            }
        }
    }

    SpreadsheetSheet metaSh;
    metaSh.name = L"SAS_Dataset_Info";
    metaSh.rows.push_back({ L"Property / Column #", L"Name", L"Type", L"Width / Value" });
    metaSh.rows.push_back({ L"(dataset)", dsName.empty() ? L"SAS7BDAT" : dsName, u64 ? L"64-bit" : L"32-bit",
        L"Rows=" + std::to_wstring(totalRows) + L", Cols=" + std::to_wstring(cols.size()) + L", PageSize=" + std::to_wstring(pageSize) });
    for (size_t i = 0; i < cols.size(); ++i) {
        std::wstring cname = cols[i].name.empty() ? (L"COL_" + std::to_wstring(i + 1)) : cols[i].name;
        metaSh.rows.push_back({ std::to_wstring(i + 1), cname, cols[i].type == 2 ? L"Character" : L"Numeric", std::to_wstring(cols[i].width) + L" B" });
    }
    FinalizeSheetCols(metaSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SAS Binary Dataset (.sas7bdat)";
    if (!decodedRows.empty()) {
        SpreadsheetSheet dataSh;
        dataSh.name = dsName.empty() ? L"SAS_Data" : dsName;
        std::vector<std::wstring> hdr;
        for (size_t i = 0; i < cols.size(); ++i) hdr.push_back(cols[i].name.empty() ? (L"COL_" + std::to_wstring(i + 1)) : cols[i].name);
        dataSh.rows.push_back(std::move(hdr));
        for (auto& r : decodedRows) dataSh.rows.push_back(std::move(r));
        FinalizeSheetCols(dataSh);
        out.sheets.push_back(std::move(dataSh));
    }
    out.sheets.push_back(std::move(metaSh));
    out.valid = true;
    return true;
}

} // namespace AiDataDetail

inline bool ParseSas7bdat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return AiDataDetail::ParseSas7bdat(raw, out); }

} // namespace Formats
