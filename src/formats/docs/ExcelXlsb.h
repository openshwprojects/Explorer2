#pragma once
// ExcelXlsb.h — Zero-dependency C++20 Microsoft Excel Binary Workbook (.xlsb, [MS-XLSB] BIFF12) reader.

namespace Formats {

namespace DetailXlsb {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdI64LE;
using ::Explorer2::ByteIO::RdI64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdF64BE;


using ::Explorer2::ReaderText::DecodeUtf16Span;
using ::Explorer2::ReaderText::CivilFromDays1970;
using ::Explorer2::ReaderText::FormatUnixUtc;

inline std::wstring NormalizeArchiveKey(std::wstring_view sv) { return Formats::NormalizeZipArchiveKey(sv); }
using ZipPackageReader = Formats::ZipPackageReader;

enum class XlsbFmtKind : uint8_t { General, Date, Time, DateTime, Percent };

struct XlsbStyle {
    XlsbFmtKind kind = XlsbFmtKind::General;
    int pctDecimals = 2;
};

inline XlsbStyle ClassifyXlsbFmt(uint16_t ifmt, const std::wstring& customCode) {
    if (!customCode.empty()) {
        bool hasDate = false, hasTime = false, hasM = false, hasPct = false, hasNum = false, seenDot = false;
        int decs = 0;
        for (size_t i = 0; i < customCode.size(); ++i) {
            wchar_t c = customCode[i];
            if (c == L';') break;
            if (c == L'"') {
                ++i;
                while (i < customCode.size() && customCode[i] != L'"') ++i;
                continue;
            }
            if (c == L'\\' || c == L'_' || c == L'*') {
                if (i + 1 < customCode.size()) ++i;
                continue;
            }
            if (c == L'[') {
                size_t cb = customCode.find(L']', i + 1);
                if (cb == std::wstring::npos) break;
                std::wstring_view brk(customCode.data() + i + 1, cb - (i + 1));
                if (!brk.empty()) {
                    wchar_t f0 = static_cast<wchar_t>(towlower(brk[0]));
                    if (f0 == L'h' || f0 == L's') hasTime = true;
                }
                i = cb;
                continue;
            }
            wchar_t lc = static_cast<wchar_t>(towlower(c));
            if (lc == L'y' || lc == L'd') hasDate = true;
            else if (lc == L'h' || lc == L's') hasTime = true;
            else if (lc == L'm') hasM = true;
            else if (c == L'%') hasPct = true;
            else if (c == L'.') seenDot = true;
            else if (c == L'#' || c == L'0' || c == L'?') {
                hasNum = true;
                if (seenDot) ++decs;
            }
        }
        if (!hasNum && (hasDate || hasTime || hasM)) {
            if (hasDate && hasTime) return { XlsbFmtKind::DateTime, 0 };
            if (hasTime) return { XlsbFmtKind::Time, 0 };
            return { XlsbFmtKind::Date, 0 };
        }
        if (hasPct) return { XlsbFmtKind::Percent, seenDot ? std::min(decs, 6) : 0 };
    }
    if (ifmt == 9u) return { XlsbFmtKind::Percent, 0 };
    if (ifmt == 10u) return { XlsbFmtKind::Percent, 2 };
    if ((ifmt >= 14u && ifmt <= 17u) || (ifmt >= 27u && ifmt <= 36u) || (ifmt >= 50u && ifmt <= 58u)) {
        return { XlsbFmtKind::Date, 0 };
    }
    if ((ifmt >= 18u && ifmt <= 21u) || (ifmt >= 45u && ifmt <= 47u)) {
        return { XlsbFmtKind::Time, 0 };
    }
    if (ifmt == 22u) return { XlsbFmtKind::DateTime, 0 };
    return { XlsbFmtKind::General, 0 };
}

inline std::wstring FormatXlsbNumber(double val, const XlsbStyle& st, bool is1904) {
    if (std::isnan(val)) return L"#NUM!";
    if (std::isinf(val)) return val < 0 ? L"-Inf" : L"Inf";

    if (st.kind == XlsbFmtKind::Percent) {
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%.*f%%", st.pctDecimals, val * 100.0);
        return buf;
    }
    if ((st.kind == XlsbFmtKind::Date || st.kind == XlsbFmtKind::Time || st.kind == XlsbFmtKind::DateTime) &&
        val > -700000.0 && val < 3000000.0) {
        int64_t wholeDays = static_cast<int64_t>(val);
        if (val < 0.0 && static_cast<double>(wholeDays) != val) --wholeDays;
        double frac = std::max(0.0, val - static_cast<double>(wholeDays));
        int64_t secOfDay = static_cast<int64_t>(frac * 86400.0 + 0.5);
        if (secOfDay >= 86400LL) { secOfDay -= 86400LL; ++wholeDays; }
        int hh = static_cast<int>(secOfDay / 3600LL);
        int mm = static_cast<int>((secOfDay % 3600LL) / 60LL);
        int ss = static_cast<int>(secOfDay % 60LL);
        if (st.kind == XlsbFmtKind::Time) {
            wchar_t tbuf[32] = {};
            swprintf_s(tbuf, L"%02d:%02d:%02d", hh, mm, ss);
            return tbuf;
        }
        int yr = 1900, mo = 1, dy = 1;
        if (!is1904) {
            if (wholeDays == 60LL) { yr = 1900; mo = 2; dy = 29; }
            else CivilFromDays1970((wholeDays > 60LL) ? (wholeDays - 25569LL) : (wholeDays - 25568LL), yr, mo, dy);
        } else {
            CivilFromDays1970(wholeDays - 24107LL, yr, mo, dy);
        }
        wchar_t dbuf[64] = {};
        if (st.kind == XlsbFmtKind::Date && secOfDay == 0LL) swprintf_s(dbuf, L"%04d-%02d-%02d", yr, mo, dy);
        else if (ss == 0) swprintf_s(dbuf, L"%04d-%02d-%02d %02d:%02d", yr, mo, dy, hh, mm);
        else swprintf_s(dbuf, L"%04d-%02d-%02d %02d:%02d:%02d", yr, mo, dy, hh, mm, ss);
        return dbuf;
    }

    if (val == 0.0) return L"0";
    wchar_t buf[64] = {};
    double av = std::fabs(val);
    if (av >= 1e-4 && av < 1e12 && std::floor(val) == val) swprintf_s(buf, L"%.0f", val);
    else swprintf_s(buf, L"%.14g", val);
    return buf;
}

inline double DecodeXlsbRk(uint32_t rk) {
    double v = 0.0;
    if ((rk & 0x02u) != 0) {
        int32_t iv = static_cast<int32_t>(rk) >> 2;
        v = static_cast<double>(iv);
    } else {
        uint64_t bits = static_cast<uint64_t>(rk & ~3u) << 34;
        std::memcpy(&v, &bits, 8);
    }
    if ((rk & 0x01u) != 0) v /= 100.0;
    return v;
}

inline std::wstring XlsbErrStr(uint8_t err) {
    switch (err) {
        case 0x00: return L"#NULL!";
        case 0x07: return L"#DIV/0!";
        case 0x0F: return L"#VALUE!";
        case 0x17: return L"#REF!";
        case 0x1D: return L"#NAME?";
        case 0x24: return L"#NUM!";
        case 0x2A: return L"#N/A";
        case 0x2B: return L"#GETTING_DATA";
        default:   return L"#ERR!";
    }
}

// [MS-XLSB] §2.1.4: 1-4 byte variable-length record ID + 1-4 byte variable-length byte count
inline bool NextBiff12Record(const uint8_t* data, size_t size, size_t& pos, uint32_t& outId, const uint8_t*& outPay, size_t& outLen) {
    if (pos >= size) return false;
    outId = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        outId |= static_cast<uint32_t>(b & 0x7Fu) << (7 * i);
        if ((b & 0x80u) == 0) break;
    }
    uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= size) return false;
        uint8_t b = data[pos++];
        len |= static_cast<uint32_t>(b & 0x7Fu) << (7 * i);
        if ((b & 0x80u) == 0) break;
    }
    if (len > size - pos) return false;
    outPay = data + pos;
    outLen = len;
    pos += len;
    return true;
}

inline bool ReadXLWideString(const uint8_t* p, size_t len, size_t& off, std::wstring& out, bool nullable = false) {
    out.clear();
    if (off + 4 > len) return false;
    uint32_t cch = RdU32LE(p + off);
    off += 4;
    if (nullable && cch == 0xFFFFFFFFu) return true;
    if (cch > (len - off) / 2u) return false;
    out = DecodeUtf16Span(p + off, cch, false);
    off += static_cast<size_t>(cch) * 2u;
    return true;
}

inline std::wstring ResolveOoxmlRelTarget(std::string_view target, std::string_view baseDir) {
    std::string norm;
    norm.reserve(target.size());
    for (char c : target) norm.push_back(c == '\\' ? '/' : c);
    while (!norm.empty() && norm.front() == ' ') norm.erase(norm.begin());
    std::string combined;
    if (!norm.empty() && norm.front() == '/') combined = norm.substr(1);
    else if (baseDir.empty() || norm.rfind(std::string(baseDir) + "/", 0) == 0) combined = norm;
    else combined = std::string(baseDir) + "/" + norm;

    std::vector<std::string_view> segs;
    size_t p = 0;
    while (p <= combined.size()) {
        size_t sl = combined.find('/', p);
        if (sl == std::string::npos) sl = combined.size();
        std::string_view seg(combined.data() + p, sl - p);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") { if (!segs.empty()) segs.pop_back(); }
            else segs.push_back(seg);
        }
        p = sl + 1;
    }
    std::string clean;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) clean.push_back('/');
        clean.append(segs[i].data(), segs[i].size());
    }
    return PathUtils::Utf8ToWide(clean);
}

inline void ParseRelsXml(const std::vector<uint8_t>& xmlBytes, std::string_view baseDir,
                         std::unordered_map<std::wstring, std::wstring>& outMap) {
    std::string_view sv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
    size_t pos = 0;
    ::Explorer2::XmlTagScan::XmlTag tag;
    while (::Explorer2::XmlTagScan::NextTag(sv, pos, tag)) {
        if (!tag.isClosing && tag.localName == "Relationship") {
            std::string_view idSv = ::Explorer2::XmlTagScan::GetAttr(tag.attrs, "Id");
            std::string_view tgtSv = ::Explorer2::XmlTagScan::GetAttr(tag.attrs, "Target");
            if (!idSv.empty() && !tgtSv.empty()) {
                outMap[PathUtils::Utf8ToWide(idSv)] = ResolveOoxmlRelTarget(tgtSv, baseDir);
            }
        }
    }
}


} // namespace DetailXlsb

inline bool ParseExcelXlsbFile(const std::wstring& fullPath, const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    DetailXlsb::ZipPackageReader zip;
    if (!zip.Open(fullPath, raw)) return false;

    std::wstring wbPath = L"xl/workbook.bin";
    if (!zip.FindEntry(wbPath)) {
        std::vector<uint8_t> rootRels;
        if (zip.ExtractByPath(L"_rels/.rels", rootRels)) {
            std::unordered_map<std::wstring, std::wstring> rmap;
            DetailXlsb::ParseRelsXml(rootRels, "", rmap);
            for (const auto& [k, v] : rmap) {
                if (DetailXlsb::NormalizeArchiveKey(v).find(L".bin") != std::wstring::npos && zip.FindEntry(v)) {
                    wbPath = v;
                    break;
                }
            }
        }
    }

    std::vector<uint8_t> wbBytes;
    if (!zip.ExtractByPath(wbPath, wbBytes) || wbBytes.size() < 4) return false;

    // Determine workbook base directory & parse workbook.bin.rels
    std::wstring wbNorm = DetailXlsb::NormalizeArchiveKey(wbPath);
    size_t lastSl = wbNorm.rfind(L'/');
    std::string wbDirUtf8 = (lastSl != std::wstring::npos) ? PathUtils::WideToUtf8(wbNorm.substr(0, lastSl)) : "";
    std::wstring wbLeaf = (lastSl != std::wstring::npos) ? wbNorm.substr(lastSl + 1) : wbNorm;
    std::wstring wbRelsPath = (lastSl != std::wstring::npos ? wbNorm.substr(0, lastSl) + L"/_rels/" : L"_rels/") + wbLeaf + L".rels";

    std::unordered_map<std::wstring, std::wstring> relTargets;
    std::vector<uint8_t> wbRelsBytes;
    if (zip.ExtractByPath(wbRelsPath, wbRelsBytes)) {
        DetailXlsb::ParseRelsXml(wbRelsBytes, wbDirUtf8, relTargets);
    }

    // Parse xl/workbook.bin: BrtBeginBook (0x0083), BrtWbProp (0x0099), BrtBundleSh (0x009C)
    struct XlsbSheetEntry {
        std::wstring name;
        std::wstring rId;
        uint32_t hsState = 0;
    };
    std::vector<XlsbSheetEntry> bundleSheets;
    bool is1904 = false;
    bool sawBookBegin = false;
    {
        size_t pos = 0;
        uint32_t recId = 0;
        const uint8_t* pay = nullptr;
        size_t len = 0;
        while (DetailXlsb::NextBiff12Record(wbBytes.data(), wbBytes.size(), pos, recId, pay, len)) {
            if (recId == 0x0083u) { // BrtBeginBook
                sawBookBegin = true;
            } else if (recId == 0x0099u && len >= 1) { // BrtWbProp
                is1904 = (pay[0] & 0x01u) != 0;
            } else if (recId == 0x009Cu && len >= 12) { // BrtBundleSh
                XlsbSheetEntry se;
                se.hsState = DetailXlsb::RdU32LE(pay + 0);
                size_t off = 8; // skip hsState (4B) + iTabID (4B)
                if (DetailXlsb::ReadXLWideString(pay, len, off, se.rId, true) &&
                    DetailXlsb::ReadXLWideString(pay, len, off, se.name, false)) {
                    if (se.name.empty()) se.name = L"Sheet" + std::to_wstring(bundleSheets.size() + 1);
                    bundleSheets.push_back(std::move(se));
                }
            }
        }
    }
    if (!sawBookBegin && bundleSheets.empty()) return false;

    // Parse xl/sharedStrings.bin (BrtSSTItem = 0x0013)
    std::vector<std::wstring> sst;
    {
        std::wstring sstPath = L"xl/sharedStrings.bin";
        for (const auto& [rid, tgt] : relTargets) {
            if (DetailXlsb::NormalizeArchiveKey(tgt).find(L"sharedstrings.bin") != std::wstring::npos) {
                sstPath = tgt;
                break;
            }
        }
        std::vector<uint8_t> sstBytes;
        if (zip.ExtractByPath(sstPath, sstBytes)) {
            size_t pos = 0;
            uint32_t recId = 0;
            const uint8_t* pay = nullptr;
            size_t len = 0;
            while (DetailXlsb::NextBiff12Record(sstBytes.data(), sstBytes.size(), pos, recId, pay, len) && sst.size() < 1000000) {
                if (recId == 0x0013u && len >= 5) { // BrtSSTItem: flags (1B) + XLWideString
                    size_t off = 1;
                    std::wstring val;
                    if (DetailXlsb::ReadXLWideString(pay, len, off, val, false)) {
                        sst.push_back(std::move(val));
                    }
                }
            }
        }
    }

    // Parse xl/styles.bin (BrtFmt = 0x002C, BrtBeginCellXFs = 0x0269..0x026A, BrtXf = 0x002F)
    std::vector<DetailXlsb::XlsbStyle> cellStyles;
    {
        std::wstring stylesPath = L"xl/styles.bin";
        for (const auto& [rid, tgt] : relTargets) {
            if (DetailXlsb::NormalizeArchiveKey(tgt).find(L"styles.bin") != std::wstring::npos) {
                stylesPath = tgt;
                break;
            }
        }
        std::vector<uint8_t> stBytes;
        if (zip.ExtractByPath(stylesPath, stBytes)) {
            std::unordered_map<uint16_t, std::wstring> customFmts;
            std::vector<uint16_t> xfFmtIds, fallbackXfIds;
            bool inCellXfs = false;
            bool sawCellXfsBlock = false;
            size_t pos = 0;
            uint32_t recId = 0;
            const uint8_t* pay = nullptr;
            size_t len = 0;
            while (DetailXlsb::NextBiff12Record(stBytes.data(), stBytes.size(), pos, recId, pay, len)) {
                if (recId == 0x002Cu && len >= 6) { // BrtFmt: ifmt (2B) + XLWideString
                    uint16_t ifmt = DetailXlsb::RdU16LE(pay);
                    size_t off = 2;
                    std::wstring code;
                    if (DetailXlsb::ReadXLWideString(pay, len, off, code, false)) {
                        customFmts[ifmt] = std::move(code);
                    }
                } else if (recId == 0x0269u) { // BrtBeginCellXFs
                    inCellXfs = true;
                    sawCellXfsBlock = true;
                } else if (recId == 0x026Au) { // BrtEndCellXFs
                    inCellXfs = false;
                } else if (recId == 0x002Fu && len >= 4) { // BrtXf: ixfeParent (2B) + ifmt (2B)
                    uint16_t ifmt = DetailXlsb::RdU16LE(pay + 2);
                    if (inCellXfs) xfFmtIds.push_back(ifmt);
                    else if (!sawCellXfsBlock) fallbackXfIds.push_back(ifmt);
                }
            }
            const auto& activeXfs = sawCellXfsBlock ? xfFmtIds : fallbackXfIds;
            cellStyles.reserve(activeXfs.size());
            for (uint16_t fid : activeXfs) {
                auto it = customFmts.find(fid);
                cellStyles.push_back(DetailXlsb::ClassifyXlsbFmt(fid, it != customFmts.end() ? it->second : L""));
            }
        }
    }

    // Parse each worksheet .bin
    for (size_t si = 0; si < bundleSheets.size() && out.sheets.size() < 64; ++si) {
        const auto& bs = bundleSheets[si];
        std::wstring wsPath;
        auto rit = relTargets.find(bs.rId);
        if (rit != relTargets.end()) wsPath = rit->second;
        if (wsPath.empty() || !zip.FindEntry(wsPath)) {
            wsPath = L"xl/worksheets/sheet" + std::to_wstring(si + 1) + L".bin";
        }
        std::vector<uint8_t> wsBytes;
        if (!zip.ExtractByPath(wsPath, wsBytes) || wsBytes.empty()) continue;

        // Skip chartsheets / dialogsheets that have zero BrtBeginSheetData (0x0091) unless worksheet records exist
        SpreadsheetSheet sheet;
        sheet.name = bs.name + (bs.hsState != 0 ? L" [Hidden]" : L"");

        constexpr uint32_t kMaxRows = 20000;
        constexpr uint32_t kMaxCols = 512;
        uint32_t curRow = 0;
        uint32_t lastCol = 0;
        bool sawWsData = false;

        auto setCell = [&](uint32_t r, uint32_t c, std::wstring val) {
            if (r >= kMaxRows || c >= kMaxCols) return;
            if (sheet.rows.size() <= r) sheet.rows.resize(static_cast<size_t>(r) + 1);
            if (sheet.rows[r].size() <= c) sheet.rows[r].resize(static_cast<size_t>(c) + 1);
            sheet.rows[r][c] = std::move(val);
            sheet.maxCols = std::max(sheet.maxCols, static_cast<size_t>(c) + 1);
        };
        auto styleFor = [&](uint32_t styleField) -> DetailXlsb::XlsbStyle {
            uint32_t xfIdx = styleField & 0x00FFFFFFu;
            return (xfIdx < cellStyles.size()) ? cellStyles[xfIdx] : DetailXlsb::XlsbStyle{};
        };

        size_t pos = 0;
        uint32_t recId = 0;
        const uint8_t* pay = nullptr;
        size_t len = 0;
        while (DetailXlsb::NextBiff12Record(wsBytes.data(), wsBytes.size(), pos, recId, pay, len)) {
            if (recId == 0x0081u || recId == 0x0091u) { // BrtBeginWs / BrtBeginSheetData
                sawWsData = true;
                continue;
            }
            if (recId == 0x0000u && len >= 4) { // BrtRowHdr
                curRow = DetailXlsb::RdU32LE(pay);
                lastCol = 0;
                sawWsData = true;
                continue;
            }
            if (len < 8) continue;
            uint32_t rawCol = DetailXlsb::RdU32LE(pay);
            uint32_t col = (rawCol == 0xFFFFFFFFu) ? (lastCol + 1u) : rawCol;
            lastCol = col;
            uint32_t stBits = DetailXlsb::RdU32LE(pay + 4);

            switch (recId) {
                case 0x0001u: // BrtCellBlank
                    break;
                case 0x0002u: // BrtCellRk (8B Cell + 4B RK)
                    if (len >= 12) {
                        double v = DetailXlsb::DecodeXlsbRk(DetailXlsb::RdU32LE(pay + 8));
                        setCell(curRow, col, DetailXlsb::FormatXlsbNumber(v, styleFor(stBits), is1904));
                    }
                    break;
                case 0x0003u: // BrtCellError
                case 0x000Bu: // BrtFmlaError
                    if (len >= 9) setCell(curRow, col, DetailXlsb::XlsbErrStr(pay[8]));
                    break;
                case 0x0004u: // BrtCellBool
                case 0x000Au: // BrtFmlaBool
                    if (len >= 9) setCell(curRow, col, pay[8] != 0 ? L"TRUE" : L"FALSE");
                    break;
                case 0x0005u: // BrtCellReal
                case 0x0009u: // BrtFmlaNum
                    if (len >= 16) {
                        double v = DetailXlsb::RdF64LE(pay + 8);
                        setCell(curRow, col, DetailXlsb::FormatXlsbNumber(v, styleFor(stBits), is1904));
                    }
                    break;
                case 0x0006u: // BrtCellSt
                case 0x0008u: // BrtFmlaString
                    if (len >= 12) {
                        size_t off = 8;
                        std::wstring s;
                        if (DetailXlsb::ReadXLWideString(pay, len, off, s, false)) {
                            setCell(curRow, col, std::move(s));
                        }
                    }
                    break;
                case 0x0007u: // BrtCellIsst
                    if (len >= 12) {
                        uint32_t idx = DetailXlsb::RdU32LE(pay + 8);
                        if (idx < sst.size()) setCell(curRow, col, sst[idx]);
                    }
                    break;
                case 0x003Eu: // BrtCellRString (8B Cell + 1B flags + XLWideString)
                    if (len >= 13) {
                        size_t off = 9;
                        std::wstring s;
                        if (DetailXlsb::ReadXLWideString(pay, len, off, s, false)) {
                            setCell(curRow, col, std::move(s));
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        if (!sawWsData && sheet.rows.empty()) continue;
        if (sheet.rows.empty()) {
            sheet.rows.push_back({ L"" });
            sheet.maxCols = 1;
        }
        FinalizeSheetCols(sheet);
        out.sheets.push_back(std::move(sheet));
    }

    if (out.sheets.empty()) return false;
    out.kind = OfficeFileKind::ExcelXlsx;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Excel Binary Workbook (.xlsb)";
    if (zip.fromDisk) out.sourceReadFully = true;
    out.valid = true;
    return true;
}

} // namespace Formats
