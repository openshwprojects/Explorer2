#pragma once
// Sas7bdat.h — SAS7BDAT Binary Dataset (.sas7bdat) Reader (Uncompressed + SASYZCRL RLE + SASYZCR2 RDC)

namespace Formats {
namespace AiDataDetail {

// SAS CHAR / RLE ("SASYZCRL") row decompressor
inline bool DecompressSasRle(const uint8_t* in, size_t inLen, size_t expectedLen, std::vector<uint8_t>& out) {
    out.clear();
    if (!in || expectedLen == 0 || expectedLen > 16u * 1024u * 1024u) return false;
    out.reserve(expectedLen);
    size_t ip = 0;
    while (ip < inLen && out.size() < expectedLen) {
        const uint8_t ctrl = in[ip++];
        const uint8_t cmd  = (ctrl >> 4) & 0x0Fu;
        const size_t  low4 = ctrl & 0x0Fu;
        size_t count = 0;
        switch (cmd) {
            case 0x0: // COPY64
                if (ip >= inLen) return false;
                count = (low4 << 8) + in[ip++] + 64u;
                if (count > inLen - ip || count > expectedLen - out.size()) return false;
                out.insert(out.end(), in + ip, in + ip + count);
                ip += count;
                break;
            case 0x1: // COPY64_PLUS_4096
                if (ip >= inLen) return false;
                count = (low4 << 8) + in[ip++] + 4160u;
                if (count > inLen - ip || count > expectedLen - out.size()) return false;
                out.insert(out.end(), in + ip, in + ip + count);
                ip += count;
                break;
            case 0x2: // COPY96
                count = low4 + 96u;
                if (count > inLen - ip || count > expectedLen - out.size()) return false;
                out.insert(out.end(), in + ip, in + ip + count);
                ip += count;
                break;
            case 0x4: { // INSERT_BYTE18
                if (ip + 2 > inLen) return false;
                count = (low4 << 8) + in[ip] + 18u;
                uint8_t b = in[ip + 1];
                ip += 2;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, b);
                break;
            }
            case 0x5: // INSERT_AT17
                if (ip >= inLen) return false;
                count = (low4 << 8) + in[ip++] + 17u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, static_cast<uint8_t>('@'));
                break;
            case 0x6: // INSERT_BLANK17
                if (ip >= inLen) return false;
                count = (low4 << 8) + in[ip++] + 17u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, static_cast<uint8_t>(' '));
                break;
            case 0x7: // INSERT_ZERO17
                if (ip >= inLen) return false;
                count = (low4 << 8) + in[ip++] + 17u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, 0u);
                break;
            case 0x8: // COPY1
            case 0x9: // COPY17
            case 0xA: // COPY33
            case 0xB: // COPY49
                count = low4 + 1u + static_cast<size_t>(cmd - 0x8u) * 16u;
                if (count > inLen - ip || count > expectedLen - out.size()) return false;
                out.insert(out.end(), in + ip, in + ip + count);
                ip += count;
                break;
            case 0xC: { // INSERT_BYTE3
                if (ip >= inLen) return false;
                uint8_t b = in[ip++];
                count = low4 + 3u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, b);
                break;
            }
            case 0xD: // INSERT_AT2
                count = low4 + 2u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, static_cast<uint8_t>('@'));
                break;
            case 0xE: // INSERT_BLANK2
                count = low4 + 2u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, static_cast<uint8_t>(' '));
                break;
            case 0xF: // INSERT_ZERO2
                count = low4 + 2u;
                if (count > expectedLen - out.size()) return false;
                out.insert(out.end(), count, 0u);
                break;
            default:
                return false;
        }
    }
    if (out.size() < expectedLen) out.resize(expectedLen, 0u);
    return true;
}

// SAS BINARY / Ross Data Compression ("SASYZCR2") row decompressor
inline bool DecompressSasRdc(const uint8_t* in, size_t inLen, size_t expectedLen, std::vector<uint8_t>& out) {
    out.clear();
    if (!in || expectedLen == 0 || expectedLen > 16u * 1024u * 1024u) return false;
    out.reserve(expectedLen);
    size_t ip = 0;
    uint16_t ctrlBits = 0;
    uint16_t ctrlMask = 0;
    while (ip < inLen && out.size() < expectedLen) {
        if (ctrlMask == 0) {
            if (ip + 2 > inLen) return false;
            ctrlBits = static_cast<uint16_t>((static_cast<uint16_t>(in[ip]) << 8) | in[ip + 1]);
            ip += 2;
            ctrlMask = 0x8000u;
        }
        const bool isCmd = (ctrlBits & ctrlMask) != 0;
        ctrlMask >>= 1;
        if (!isCmd) {
            if (ip >= inLen) return false;
            out.push_back(in[ip++]);
            continue;
        }
        if (ip >= inLen) return false;
        const uint8_t b   = in[ip++];
        const uint8_t cmd = (b >> 4) & 0x0Fu;
        const size_t  cnt = b & 0x0Fu;
        if (cmd == 0) { // Short RLE
            if (ip >= inLen) return false;
            size_t run = cnt + 3u;
            uint8_t val = in[ip++];
            if (run > expectedLen - out.size()) return false;
            out.insert(out.end(), run, val);
        } else if (cmd == 1) { // Long RLE
            if (ip + 2 > inLen) return false;
            size_t run = cnt + (static_cast<size_t>(in[ip]) << 4) + 19u;
            uint8_t val = in[ip + 1];
            ip += 2;
            if (run > expectedLen - out.size()) return false;
            out.insert(out.end(), run, val);
        } else if (cmd == 2) { // Long LZ77 pattern
            if (ip + 2 > inLen) return false;
            size_t ofs = cnt + 3u + (static_cast<size_t>(in[ip]) << 4);
            size_t run = static_cast<size_t>(in[ip + 1]) + 16u;
            ip += 2;
            if (ofs == 0 || ofs > out.size() || run > expectedLen - out.size()) return false;
            for (size_t k = 0; k < run; ++k) out.push_back(out[out.size() - ofs]);
        } else { // cmd 3..15: Short LZ77 pattern
            if (ip >= inLen) return false;
            size_t ofs = cnt + 3u + (static_cast<size_t>(in[ip++]) << 4);
            size_t run = cmd;
            if (ofs == 0 || ofs > out.size() || run > expectedLen - out.size()) return false;
            for (size_t k = 0; k < run; ++k) out.push_back(out[out.size() - ofs]);
        }
    }
    return out.size() == expectedLen;
}

// ============================================================================
// 13. SAS7BDAT Binary Dataset (.sas7bdat)
// ============================================================================
inline bool ParseSas7bdat(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    static const uint8_t kSasMagic[32] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0xC2,0xEA,0x81,0x60,
        0xB3,0x14,0x11,0xCF,0xBD,0x92,0x08,0x00, 0x09,0xC7,0x31,0x8C,0x18,0x1F,0x10,0x11
    };
    if (raw.size() < 288 || std::memcmp(raw.data(), kSasMagic, 32) != 0) return false;

    SourceBytes source(raw, out);
    const uint64_t fileSize = source.Size();

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
    if (headerLen < 256 || pageSize < 256 || pageSize > 16u * 1024u * 1024u || headerLen > fileSize) return false;

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
    int compMode = 0; // 0 = None, 1 = SASYZCRL (RLE), 2 = SASYZCR2 (RDC)

    std::vector<std::vector<std::wstring>> decodedRows;
    std::vector<std::vector<uint8_t>> pendingRowBlobs; // in case compressed rows precede ColumnAttrs on a page
    size_t intSize = u64 ? 8 : 4;
    size_t pageHdrOff = u64 ? 32 : 16;
    size_t subPtrSize = u64 ? 24 : 12;

    auto decodeRowBytes = [&](const uint8_t* rp) {
        if (decodedRows.size() >= 500 || cols.empty() || rowLength == 0) return;
        if (totalRows > 0 && decodedRows.size() >= totalRows) return;
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
    };

    auto isKnownMetaSig = [](uint32_t sig) -> bool {
        return sig == 0xF7F7F7F7u || sig == 0x000000F7u ||
               sig == 0xF6F6F6F6u || sig == 0x000000F6u ||
               sig == 0xFDFFFFFFu || sig == 0xFFFFFFFDu ||
               sig == 0xFFFFFFFFu ||
               sig == 0xFCFFFFFFu || sig == 0xFFFFFFFCu ||
               sig == 0xFBFFFFFFu || sig == 0xFFFFFFFBu ||
               sig == 0xFAFFFFFFu || sig == 0xFFFFFFFAu ||
               sig == 0xFEFBFFFFu || sig == 0xFFFFFBFEu;
    };

    size_t maxPages = static_cast<size_t>(std::min<uint64_t>(pageCount > 0 ? pageCount : 128, 128));
    std::vector<uint8_t> pageBuf;
    std::vector<uint8_t> decompRow;
    for (size_t pg = 0; pg < maxPages; ++pg) {
        uint64_t pgPos = static_cast<uint64_t>(headerLen) + static_cast<uint64_t>(pg) * pageSize;
        if (pgPos + pageHdrOff + 8 > fileSize) break;
        size_t availPg = 0;
        const uint8_t* pp = source.ReadSome(pgPos, pageSize, pageSize, pageBuf, availPg);
        if (!pp || availPg < pageHdrOff + 8) break;

        uint16_t pageType   = rdU16(pp + pageHdrOff);
        uint16_t blockCount = rdU16(pp + pageHdrOff + 2);
        uint16_t subCount   = rdU16(pp + pageHdrOff + 4);

        const bool hasSubheaders = (pageType == 0 || pageType == 512 || pageType == 640 ||
                                    pageType == 1024 || pageType == 36864); // 36864 = 0x9000 COMP

        if (hasSubheaders) {
            for (uint16_t s = 0; s < subCount; ++s) {
                size_t ptrOff = pageHdrOff + 8 + static_cast<size_t>(s) * subPtrSize;
                if (ptrOff + subPtrSize > availPg) break;
                uint64_t shOff = rdInt(pp + ptrOff);
                uint64_t shLen = rdInt(pp + ptrOff + intSize);
                uint8_t compFlag = pp[ptrOff + 2 * intSize];
                uint8_t typeFlag = pp[ptrOff + 2 * intSize + 1];
                if (shLen == 0 || compFlag == 1 || shOff + shLen > availPg) continue;

                const uint8_t* sp = pp + static_cast<size_t>(shOff);

                // Compressed row subheader (compFlag == 4) or uncompressed row stored in subheader (compFlag == 0 && typeFlag == 1)
                if (compFlag == 4 && rowLength > 0 && (decodedRows.size() + pendingRowBlobs.size()) < 500) {
                    bool okDec = false;
                    if (compMode == 2) {
                        okDec = DecompressSasRdc(sp, static_cast<size_t>(shLen), static_cast<size_t>(rowLength), decompRow);
                    } else {
                        okDec = DecompressSasRle(sp, static_cast<size_t>(shLen), static_cast<size_t>(rowLength), decompRow);
                        if (!okDec) okDec = DecompressSasRdc(sp, static_cast<size_t>(shLen), static_cast<size_t>(rowLength), decompRow);
                    }
                    if (okDec && decompRow.size() == rowLength) {
                        if (!cols.empty()) decodeRowBytes(decompRow.data());
                        else pendingRowBlobs.push_back(decompRow);
                    }
                    continue;
                }

                if (shLen >= 4) {
                    uint32_t sig = RdU32LE(sp);
                    if (u64 && shLen >= 8) {
                        uint32_t sigHi = RdU32LE(sp + 4);
                        if (sig == 0xFFFFFFFFu && sigHi != 0xFFFFFFFFu) sig = sigHi;
                    }

                    if ((sig == 0xF7F7F7F7u || sig == 0x000000F7u) && shLen >= 12 * intSize) { // RowSize
                        rowLength   = rdInt(sp + 5 * intSize);
                        totalRows   = rdInt(sp + 6 * intSize);
                        mixRowCount = rdInt(sp + ((16 * intSize <= shLen) ? (15 * intSize) : (6 * intSize)));
                        continue;
                    }
                    if ((sig == 0xF6F6F6F6u || sig == 0x000000F6u) && shLen >= 2 * intSize) { // ColumnSize
                        colCount = rdInt(sp + intSize);
                        if (colCount <= 4096 && cols.size() < colCount) cols.resize(static_cast<size_t>(colCount));
                        continue;
                    }
                    if (sig == 0xFDFFFFFFu || sig == 0xFFFFFFFDu) { // ColumnText
                        size_t txtHdr = intSize + 2;
                        if (shLen > txtHdr) {
                            std::string blob(reinterpret_cast<const char*>(sp + intSize), static_cast<size_t>(shLen - intSize));
                            if (blob.find("SASYZCR2") != std::string::npos) compMode = 2;
                            else if (blob.find("SASYZCRL") != std::string::npos && compMode == 0) compMode = 1;
                            colTexts.push_back(std::move(blob));
                        }
                        continue;
                    }
                    if (sig == 0xFFFFFFFFu && shLen > intSize + 8 && typeFlag == 0) { // ColumnName
                        size_t entryLen = 8;
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
                        continue;
                    }
                    if ((sig == 0xFCFFFFFFu || sig == 0xFFFFFFFCu) && shLen > intSize + 8) { // ColumnAttrs
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
                        if (!pendingRowBlobs.empty()) {
                            for (const auto& rb : pendingRowBlobs) {
                                if (rb.size() == rowLength) decodeRowBytes(rb.data());
                            }
                            pendingRowBlobs.clear();
                        }
                        continue;
                    }

                    // Uncompressed data row stored inside subheader table on compressed datasets (compFlag == 0, typeFlag == 1)
                    if (compFlag == 0 && typeFlag == 1 && rowLength > 0 && shLen == rowLength && !isKnownMetaSig(sig)) {
                        if (!cols.empty()) decodeRowBytes(sp);
                        else if (pendingRowBlobs.size() < 500) pendingRowBlobs.emplace_back(sp, sp + static_cast<size_t>(shLen));
                        continue;
                    }
                }
            }
        }

        // Decode uncompressed fixed rows on DATA (256) or uncompressed MIX (512/640) pages
        if ((pageType == 256 || (compMode == 0 && (pageType == 512 || pageType == 640))) &&
            rowLength > 0 && !cols.empty() && decodedRows.size() < 500)
        {
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
                decodeRowBytes(pp + dataOff);
            }
        }
    }

    const wchar_t* compName = (compMode == 2) ? L"SASYZCR2 (RDC)" : (compMode == 1 ? L"SASYZCRL (RLE)" : L"Uncompressed");

    SpreadsheetSheet metaSh;
    metaSh.name = L"SAS_Dataset_Info";
    metaSh.rows.push_back({ L"Property / Column #", L"Name", L"Type", L"Width / Value" });
    metaSh.rows.push_back({ L"(dataset)", dsName.empty() ? L"SAS7BDAT" : dsName, u64 ? L"64-bit" : L"32-bit",
        L"Rows=" + std::to_wstring(totalRows) + L", Cols=" + std::to_wstring(cols.size()) +
        L", PageSize=" + std::to_wstring(pageSize) + L", Compression=" + compName });
    for (size_t i = 0; i < cols.size(); ++i) {
        std::wstring cname = cols[i].name.empty() ? (L"COL_" + std::to_wstring(i + 1)) : cols[i].name;
        metaSh.rows.push_back({ std::to_wstring(i + 1), cname, cols[i].type == 2 ? L"Character" : L"Numeric", std::to_wstring(cols[i].width) + L" B" });
    }
    FinalizeSheetCols(metaSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SAS Binary Dataset (.sas7bdat)";
    if (source.ReadPastHead() && !source.ReadFailed()) out.sourceReadFully = true;
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
