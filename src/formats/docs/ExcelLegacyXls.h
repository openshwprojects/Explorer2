#pragma once
// ExcelLegacyXls.h — Microsoft Excel BIFF2-BIFF8 (.xls) OLE2 Workbook & Shared String Table Reader

namespace Formats {

namespace BiffXlsDetail {

// little-endian loads (ByteIO.h)
inline uint16_t Rd16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t Rd32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline double RdF64(const uint8_t* p) { return ::Explorer2::ByteIO::RdF64LE(p); }

inline double DecodeBiffRk(uint32_t rk) {
    double v = 0.0;
    if ((rk & 0x02u) != 0) {
        int32_t iv = static_cast<int32_t>(rk) >> 2;
        v = static_cast<double>(iv);
    } else {
        uint64_t bits = static_cast<uint64_t>(rk & ~3u) << 34;
        std::memcpy(&v, &bits, 8);
    }
    if ((rk & 0x01u) != 0) {
        v /= 100.0;
    }
    return v;
}

inline std::wstring FormatXlsNumber(double v) {
    if (std::isnan(v)) return L"#NUM!";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    if (v == 0.0) return L"0";
    wchar_t buf[64] = {};
    double av = std::fabs(v);
    if (av >= 1e-4 && av < 1e12 && std::floor(v) == v) {
        swprintf_s(buf, L"%.0f", v);
    } else {
        swprintf_s(buf, L"%.12g", v);
    }
    return buf;
}

inline std::wstring BiffErrCodeToStr(uint8_t err) {
    switch (err) {
        case 0x00: return L"#NULL!";
        case 0x07: return L"#DIV/0!";
        case 0x0F: return L"#VALUE!";
        case 0x17: return L"#REF!";
        case 0x1D: return L"#NAME?";
        case 0x24: return L"#NUM!";
        case 0x2A: return L"#N/A";
        default:   return L"#ERR!";
    }
}

struct BiffChunk {
    const uint8_t* ptr = nullptr;
    size_t len = 0;
};

// MS-XLS 2.5.293 XLUnicodeRichExtendedString cursor spanning SST + CONTINUE record boundaries
struct SstStreamCursor {
    const std::vector<BiffChunk>& chunks;
    size_t cIdx = 0;
    size_t off  = 0;

    explicit SstStreamCursor(const std::vector<BiffChunk>& c) : chunks(c) {}

    bool EnsureAvail() {
        while (cIdx < chunks.size() && off >= chunks[cIdx].len) {
            ++cIdx;
            off = 0;
        }
        return cIdx < chunks.size();
    }

    bool ReadRawBytes(uint8_t* dst, size_t count) {
        size_t got = 0;
        while (got < count) {
            if (!EnsureAvail()) return false;
            size_t avail = chunks[cIdx].len - off;
            size_t take = std::min(count - got, avail);
            std::memcpy(dst + got, chunks[cIdx].ptr + off, take);
            off += take;
            got += take;
        }
        return true;
    }

    bool SkipRawBytes(size_t count) {
        size_t rem = count;
        while (rem > 0) {
            if (!EnsureAvail()) return false;
            size_t avail = chunks[cIdx].len - off;
            size_t take = std::min(rem, avail);
            off += take;
            rem -= take;
        }
        return true;
    }

    bool ReadRichExtendedString(std::wstring& out) {
        out.clear();
        uint8_t hdr[3] = {};
        if (!ReadRawBytes(hdr, 3)) return false;
        uint16_t cch = Rd16(hdr);
        uint8_t flags = hdr[2];
        bool fHighByte = (flags & 0x01u) != 0;
        bool fExtSt    = (flags & 0x04u) != 0;
        bool fRichSt   = (flags & 0x08u) != 0;

        uint16_t cRun = 0;
        if (fRichSt) {
            uint8_t rb[2] = {};
            if (!ReadRawBytes(rb, 2)) return false;
            cRun = Rd16(rb);
        }
        uint32_t cbExtRst = 0;
        if (fExtSt) {
            uint8_t eb[4] = {};
            if (!ReadRawBytes(eb, 4)) return false;
            cbExtRst = Rd32(eb);
        }

        out.reserve(cch);
        size_t remChars = cch;
        while (remChars > 0) {
            if (cIdx >= chunks.size()) return false;
            if (off >= chunks[cIdx].len) {
                // Crossing into a CONTINUE record mid-string: first byte of CONTINUE gives new fHighByte!
                ++cIdx;
                off = 0;
                while (cIdx < chunks.size() && chunks[cIdx].len == 0) ++cIdx;
                if (cIdx >= chunks.size()) return false;
                uint8_t contFlags = chunks[cIdx].ptr[off++];
                fHighByte = (contFlags & 0x01u) != 0;
            }
            size_t availBytes = chunks[cIdx].len - off;
            if (fHighByte) {
                if (availBytes < 2) {
                    uint8_t b2[2] = {};
                    if (!ReadRawBytes(b2, 2)) return false;
                    out.push_back(static_cast<wchar_t>(Rd16(b2)));
                    --remChars;
                    continue;
                }
                size_t takeChars = std::min(remChars, availBytes / 2);
                const uint8_t* p = chunks[cIdx].ptr + off;
                for (size_t i = 0; i < takeChars; ++i) {
                    out.push_back(static_cast<wchar_t>(Rd16(p + i * 2)));
                }
                off += takeChars * 2;
                remChars -= takeChars;
            } else {
                size_t takeChars = std::min(remChars, availBytes);
                const uint8_t* p = chunks[cIdx].ptr + off;
                for (size_t i = 0; i < takeChars; ++i) {
                    out.push_back(static_cast<wchar_t>(p[i]));
                }
                off += takeChars;
                remChars -= takeChars;
            }
        }

        if (cRun > 0 && !SkipRawBytes(static_cast<size_t>(cRun) * 4)) return false;
        if (cbExtRst > 0 && !SkipRawBytes(cbExtRst)) return false;
        return true;
    }
};

inline std::wstring ReadInlineBiffString(const uint8_t* p, size_t avail, bool isBiff8, bool lenIs16Bit) {
    if (avail == 0) return {};
    size_t pos = 0;
    uint16_t cch = 0;
    if (lenIs16Bit) {
        if (avail < 2) return {};
        cch = Rd16(p);
        pos = 2;
    } else {
        cch = p[0];
        pos = 1;
    }
    if (cch == 0) return {};

    if (!isBiff8) {
        size_t take = std::min<size_t>(cch, avail - pos);
        std::wstring out;
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) out.push_back(static_cast<wchar_t>(p[pos + i]));
        return out;
    }

    if (pos >= avail) return {};
    uint8_t flags = p[pos++];
    bool fHighByte = (flags & 0x01u) != 0;
    bool fExtSt    = (flags & 0x04u) != 0;
    bool fRichSt   = (flags & 0x08u) != 0;
    if (fRichSt) { if (pos + 2 > avail) return {}; pos += 2; }
    if (fExtSt)  { if (pos + 4 > avail) return {}; pos += 4; }

    std::wstring out;
    if (fHighByte) {
        size_t take = std::min<size_t>(cch, (avail - pos) / 2);
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) out.push_back(static_cast<wchar_t>(Rd16(p + pos + i * 2)));
    } else {
        size_t take = std::min<size_t>(cch, avail - pos);
        out.reserve(take);
        for (size_t i = 0; i < take; ++i) out.push_back(static_cast<wchar_t>(p[pos + i]));
    }
    return out;
}

inline bool ParseBiffWorkbook(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    std::vector<uint8_t> streamBuf;
    const uint8_t* biff = nullptr;
    size_t biffSize = 0;

    Ole2::CompoundFile cf;
    if (Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) {
        const Ole2::DirEntry* wb = Ole2::FindStreamCaseInsensitive(cf, L"Workbook");
        if (!wb) wb = Ole2::FindStreamCaseInsensitive(cf, L"Book");
        if (!wb || !Ole2::ExtractStream(cf, *wb, streamBuf) || streamBuf.size() < 8) {
            return false;
        }
        biff = streamBuf.data();
        biffSize = streamBuf.size();
    } else {
        if (raw.size() < 8) return false;
        uint16_t rec0 = Rd16(raw.data());
        uint16_t len0 = Rd16(raw.data() + 2);
        if ((rec0 != 0x0809 && rec0 != 0x0409 && rec0 != 0x0209 && rec0 != 0x0009) || len0 < 4 || len0 > 64 || 4u + len0 > raw.size()) {
            return false;
        }
        biff = raw.data();
        biffSize = raw.size();
    }

    uint16_t firstRec = Rd16(biff);
    uint16_t firstLen = Rd16(biff + 2);
    if ((firstRec != 0x0809 && firstRec != 0x0409 && firstRec != 0x0209 && firstRec != 0x0009) || firstLen < 4 || 4u + firstLen > biffSize) {
        return false;
    }

    uint16_t biffVer = Rd16(biff + 4);
    uint16_t firstSubType = Rd16(biff + 6);
    bool isBiff8 = (firstRec == 0x0809 && biffVer >= 0x0600);

    struct BoundSheetMeta {
        uint32_t bofOffset = 0;
        uint8_t visibility = 0;
        uint8_t sheetType  = 0;
        std::wstring name;
    };
    std::vector<BoundSheetMeta> boundSheets;
    std::vector<std::wstring> sst;

    size_t pos = 0;
    if (firstSubType == 0x0005) {
        while (pos + 4 <= biffSize) {
            uint16_t recId  = Rd16(biff + pos);
            uint16_t recLen = Rd16(biff + pos + 2);
            pos += 4;
            if (recLen > biffSize - pos) break;
            const uint8_t* p = biff + pos;
            size_t nextPos = pos + recLen;

            if (recId == 0x000A) {
                pos = nextPos;
                break;
            }
            if (recId == 0x0085 && recLen >= 7) {
                BoundSheetMeta bs;
                bs.bofOffset  = Rd32(p);
                bs.visibility = p[4];
                bs.sheetType  = p[5];
                bs.name       = ReadInlineBiffString(p + 6, recLen - 6, isBiff8, false);
                if (bs.name.empty()) bs.name = L"Sheet" + std::to_wstring(boundSheets.size() + 1);
                boundSheets.push_back(std::move(bs));
            } else if (recId == 0x00FC && recLen >= 8) {
                uint32_t cstUnique = Rd32(p + 4);
                std::vector<BiffChunk> chunks;
                chunks.push_back({ p + 8, static_cast<size_t>(recLen - 8) });

                while (nextPos + 4 <= biffSize && Rd16(biff + nextPos) == 0x003C) {
                    uint16_t contLen = Rd16(biff + nextPos + 2);
                    if (contLen > biffSize - (nextPos + 4)) break;
                    chunks.push_back({ biff + nextPos + 4, contLen });
                    nextPos += 4 + contLen;
                }

                uint32_t capUnique = std::min<uint32_t>(cstUnique, 500000u);
                sst.reserve(std::min<uint32_t>(capUnique, 4096u));
                SstStreamCursor cur(chunks);
                for (uint32_t i = 0; i < capUnique; ++i) {
                    std::wstring s;
                    if (!cur.ReadRichExtendedString(s)) break;
                    sst.push_back(std::move(s));
                }
            }
            pos = nextPos;
        }
    }

    auto parseWorksheetAt = [&](size_t startOff, const std::wstring& sheetName, size_t& outEndOff) -> bool {
        outEndOff = startOff;
        if (startOff + 8 > biffSize) return false;
        uint16_t bofId  = Rd16(biff + startOff);
        uint16_t bofLen = Rd16(biff + startOff + 2);
        if ((bofId != 0x0809 && bofId != 0x0409 && bofId != 0x0209 && bofId != 0x0009) ||
            bofLen < 4 || startOff + 4 + bofLen > biffSize) {
            return false;
        }
        uint16_t subVer  = Rd16(biff + startOff + 4);
        uint16_t subType = Rd16(biff + startOff + 6);
        bool subBiff8 = (bofId == 0x0809 && subVer >= 0x0600) || isBiff8;
        if (subType != 0x0010 && subType != 0x0040 && bofId == 0x0809) {
            size_t cur = startOff + 4 + bofLen;
            int bofDepth = 1;
            while (cur + 4 <= biffSize && bofDepth > 0) {
                uint16_t rid = Rd16(biff + cur);
                uint16_t rln = Rd16(biff + cur + 2);
                cur += 4;
                if (rln > biffSize - cur) break;
                if (rid == 0x0809 || rid == 0x0409 || rid == 0x0209 || rid == 0x0009) ++bofDepth;
                else if (rid == 0x000A) --bofDepth;
                cur += rln;
            }
            outEndOff = cur;
            return false;
        }

        SpreadsheetSheet sheet;
        sheet.name = sheetName;

        auto setCell = [&](uint16_t r, uint16_t c, std::wstring val) {
            if (r >= 16384 || c >= 512) return;
            if (sheet.rows.size() <= r) sheet.rows.resize(static_cast<size_t>(r) + 1);
            if (sheet.rows[r].size() <= c) sheet.rows[r].resize(static_cast<size_t>(c) + 1);
            sheet.rows[r][c] = std::move(val);
            sheet.maxCols = std::max(sheet.maxCols, static_cast<size_t>(c) + 1);
        };

        bool hasPendingFormulaStr = false;
        uint16_t pendingFormulaRow = 0;
        uint16_t pendingFormulaCol = 0;

        size_t cur = startOff + 4 + bofLen;
        int nestedBof = 0;
        while (cur + 4 <= biffSize) {
            uint16_t rid = Rd16(biff + cur);
            uint16_t rln = Rd16(biff + cur + 2);
            cur += 4;
            if (rln > biffSize - cur) break;
            const uint8_t* rp = biff + cur;
            size_t nextCur = cur + rln;

            if (rid == 0x0809 || rid == 0x0409 || rid == 0x0209 || rid == 0x0009) {
                ++nestedBof;
                cur = nextCur;
                continue;
            }
            if (rid == 0x000A) {
                if (nestedBof > 0) {
                    --nestedBof;
                    cur = nextCur;
                    continue;
                }
                outEndOff = nextCur;
                break;
            }
            if (nestedBof > 0) {
                cur = nextCur;
                continue;
            }

            switch (rid) {
                case 0x00FD: // LABELSST
                    if (rln >= 10) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        uint32_t idx = Rd32(rp + 6);
                        if (idx < sst.size()) setCell(row, col, sst[idx]);
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0204: // LABEL
                case 0x00D6: // RSTRING
                    if (rln >= 8) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, ReadInlineBiffString(rp + 6, rln - 6, subBiff8, true));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0004: // BIFF2 LABEL
                    if (rln >= 8) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, ReadInlineBiffString(rp + 7, rln - 7, false, false));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0203: // NUMBER
                    if (rln >= 14) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, FormatXlsNumber(RdF64(rp + 6)));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x027E: // RK
                case 0x007E:
                    if (rln >= 10) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        setCell(row, col, FormatXlsNumber(DecodeBiffRk(Rd32(rp + 6))));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x00BD: // MULRK
                    if (rln >= 6) {
                        uint16_t row = Rd16(rp);
                        uint16_t colFirst = Rd16(rp + 2);
                        uint16_t colLast  = Rd16(rp + rln - 2);
                        size_t count = (colLast >= colFirst) ? static_cast<size_t>(colLast - colFirst + 1) : 0;
                        for (size_t i = 0; i < count && 4 + (i + 1) * 6 + 2 <= rln; ++i) {
                            uint32_t rk = Rd32(rp + 4 + i * 6 + 2);
                            setCell(row, static_cast<uint16_t>(colFirst + i), FormatXlsNumber(DecodeBiffRk(rk)));
                        }
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0205: // BOOLERR
                    if (rln >= 8) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        if (rp[7] == 0) setCell(row, col, rp[6] != 0 ? L"TRUE" : L"FALSE");
                        else setCell(row, col, BiffErrCodeToStr(rp[6]));
                    }
                    hasPendingFormulaStr = false;
                    break;

                case 0x0006: // FORMULA
                case 0x0406:
                case 0x0206:
                    hasPendingFormulaStr = false;
                    if (rln >= 14) {
                        uint16_t row = Rd16(rp);
                        uint16_t col = Rd16(rp + 2);
                        if (Rd16(rp + 12) != 0xFFFFu) {
                            setCell(row, col, FormatXlsNumber(RdF64(rp + 6)));
                        } else {
                            uint8_t resType = rp[6];
                            if (resType == 0x00) {
                                hasPendingFormulaStr = true;
                                pendingFormulaRow = row;
                                pendingFormulaCol = col;
                            } else if (resType == 0x01) {
                                setCell(row, col, rp[8] != 0 ? L"TRUE" : L"FALSE");
                            } else if (resType == 0x02) {
                                setCell(row, col, BiffErrCodeToStr(rp[8]));
                            } else if (resType == 0x03) {
                                setCell(row, col, L"");
                            }
                        }
                    }
                    break;

                case 0x0207: // STRING
                    if (hasPendingFormulaStr && rln >= 2) {
                        std::vector<BiffChunk> schunks;
                        schunks.push_back({ rp, rln });
                        while (nextCur + 4 <= biffSize && Rd16(biff + nextCur) == 0x003C) {
                            uint16_t cl = Rd16(biff + nextCur + 2);
                            if (cl > biffSize - (nextCur + 4)) break;
                            schunks.push_back({ biff + nextCur + 4, cl });
                            nextCur += 4 + cl;
                        }
                        if (subBiff8) {
                            SstStreamCursor scur(schunks);
                            std::wstring s;
                            if (scur.ReadRichExtendedString(s)) setCell(pendingFormulaRow, pendingFormulaCol, std::move(s));
                        } else {
                            setCell(pendingFormulaRow, pendingFormulaCol, ReadInlineBiffString(rp, rln, false, true));
                        }
                        hasPendingFormulaStr = false;
                    }
                    break;

                case 0x04BC: // SHRFMLA
                case 0x0221: // ARRAY
                case 0x0236: // TABLE
                    break;

                default:
                    hasPendingFormulaStr = false;
                    break;
            }
            cur = nextCur;
        }

        if (sheet.rows.empty()) {
            sheet.rows.push_back({ L"" });
            sheet.maxCols = 1;
        }
        outDoc.sheets.push_back(std::move(sheet));
        outEndOff = cur;
        return true;
    };

    if (!boundSheets.empty()) {
        for (const auto& bs : boundSheets) {
            if (bs.sheetType == 0x00 || bs.sheetType == 0x01) {
                size_t endOff = 0;
                parseWorksheetAt(bs.bofOffset, bs.name, endOff);
            }
        }
    } else {
        size_t scan = (firstSubType == 0x0005) ? pos : 0;
        size_t idx = 1;
        while (scan + 8 <= biffSize && outDoc.sheets.size() < 32) {
            uint16_t rid = Rd16(biff + scan);
            uint16_t rln = Rd16(biff + scan + 2);
            if (rid == 0x0809 || rid == 0x0409 || rid == 0x0209 || rid == 0x0009) {
                size_t endOff = scan;
                parseWorksheetAt(scan, L"Sheet" + std::to_wstring(idx++), endOff);
                if (endOff <= scan) break;
                scan = endOff;
            } else {
                if (4u + rln > biffSize - scan) break;
                scan += 4 + rln;
            }
        }
    }

    if (outDoc.sheets.empty()) return false;
    outDoc.kind = OfficeFileKind::ExcelXls;
    outDoc.isSpreadsheet = true;
    outDoc.formatTitle = isBiff8 ? L"Microsoft Excel 97-2003 BIFF8 (.xls)" : L"Microsoft Excel BIFF5 Workbook (.xls)";
    outDoc.valid = true;
    return true;
}

} // namespace BiffXlsDetail

inline bool ParseExcelLegacyXls(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    return BiffXlsDetail::ParseBiffWorkbook(raw, outDoc);
}

} // namespace Formats
