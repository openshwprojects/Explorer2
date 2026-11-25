#pragma once
// AppleBinaryCookies.h — Apple Safari / CFNetwork Binary Cookies ("cook", .binarycookies) Reader

namespace Formats {
namespace DetailAppleCookies {

inline std::wstring FormatCfAbsoluteTimeUtc(double cfSec) {
    if (!std::isfinite(cfSec) || cfSec < -3153600000.0 || cfSec > 6307200000.0) return L"-";
    // Mac CFAbsoluteTime epoch is 2001-01-01 00:00:00 UTC (+978307200 seconds after Unix epoch)
    int64_t unixSec = static_cast<int64_t>(std::llround(cfSec)) + 978307200LL;
    return GeoSciDetail::FormatUtcEpoch(unixSec);
}

inline std::wstring ReadCookieStr(const uint8_t* rec, uint32_t recSize, uint32_t off) {
    if (off < 40 || off >= recSize) return L"";
    size_t maxLen = recSize - off;
    size_t n = 0;
    while (n < maxLen && rec[off + n] != 0) ++n;
    return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(rec + off), std::min<size_t>(n, 2048)));
}

inline std::wstring DescribeCookieFlags(uint32_t flags) {
    std::wstring s;
    if (flags & 0x01u) s += L"Secure";
    if (flags & 0x04u) { if (!s.empty()) s += L", "; s += L"HttpOnly"; }
    uint32_t ss = (flags >> 3) & 0x07u;
    if (ss == 3 || ss == 4 || (flags & 0x20u)) { if (!s.empty()) s += L", "; s += L"SameSite=Strict"; }
    else if (ss == 2) { if (!s.empty()) s += L", "; s += L"SameSite=Lax"; }
    else if (ss == 1) { if (!s.empty()) s += L", "; s += L"SameSite=None"; }
    return s.empty() ? L"None" : s;
}

} // namespace DetailAppleCookies

inline bool ParseAppleBinaryCookies(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "cook", 4) != 0) return false;

    const uint32_t numPages = ::Explorer2::ByteIO::RdU32BE(raw.data() + 4);
    if (numPages > 65536 || 8ULL + static_cast<uint64_t>(numPages) * 4ULL > raw.size()) return false;

    std::vector<uint32_t> pageSizes(numPages);
    size_t curPos = 8 + static_cast<size_t>(numPages) * 4u;
    uint64_t totalPageBytes = 0;
    for (uint32_t i = 0; i < numPages; ++i) {
        pageSizes[i] = ::Explorer2::ByteIO::RdU32BE(raw.data() + 8 + i * 4u);
        totalPageBytes += pageSizes[i];
    }
    if (totalPageBytes > raw.size() - curPos) return false;

    SpreadsheetSheet cookSh;
    cookSh.name = L"Safari_Cookies";
    cookSh.rows.push_back({
        L"Page", L"Domain / URL", L"Name", L"Path", L"Flags",
        L"Created (UTC)", L"Expires (UTC)", L"Value", L"Comment"
    });

    size_t totalCookies = 0;
    for (uint32_t p = 0; p < numPages; ++p) {
        const uint32_t pSize = pageSizes[p];
        if (pSize < 8 || curPos + pSize > raw.size()) return false;
        const uint8_t* page = raw.data() + curPos;
        curPos += pSize;

        // Page header magic: 00 00 01 00 (0x00000100 BE)
        if (::Explorer2::ByteIO::RdU32BE(page) != 0x00000100u) return false;
        const uint32_t nCookies = ::Explorer2::ByteIO::RdU32LE(page + 4);
        if (8ULL + static_cast<uint64_t>(nCookies) * 4ULL > pSize) return false;

        for (uint32_t c = 0; c < nCookies; ++c) {
            const uint32_t cOff = ::Explorer2::ByteIO::RdU32LE(page + 8 + c * 4u);
            if (static_cast<uint64_t>(cOff) + 56ULL > pSize) continue;
            const uint8_t* rec = page + cOff;
            const uint32_t cSize = ::Explorer2::ByteIO::RdU32LE(rec + 0);
            if (cSize < 56 || static_cast<uint64_t>(cOff) + static_cast<uint64_t>(cSize) > pSize) continue;

            const uint32_t flags      = ::Explorer2::ByteIO::RdU32LE(rec + 8);
            const uint32_t hasPort    = ::Explorer2::ByteIO::RdU32LE(rec + 12);
            const uint32_t urlOff     = ::Explorer2::ByteIO::RdU32LE(rec + 16);
            const uint32_t nameOff    = ::Explorer2::ByteIO::RdU32LE(rec + 20);
            const uint32_t pathOff    = ::Explorer2::ByteIO::RdU32LE(rec + 24);
            const uint32_t valOff     = ::Explorer2::ByteIO::RdU32LE(rec + 28);
            const uint32_t commentOff = ::Explorer2::ByteIO::RdU32LE(rec + 32);
            const double   expCf      = ::Explorer2::ByteIO::RdF64LE(rec + 40);
            const double   creCf      = ::Explorer2::ByteIO::RdF64LE(rec + 48);

            std::wstring domain = DetailAppleCookies::ReadCookieStr(rec, cSize, urlOff);
            if (hasPort != 0 && 56 + 2 <= cSize && urlOff >= 58) {
                uint16_t port = ::Explorer2::ByteIO::RdU16BE(rec + 56);
                if (port == 0) port = ::Explorer2::ByteIO::RdU16LE(rec + 56);
                if (port > 0) domain += L":" + std::to_wstring(port);
            }

            ++totalCookies;
            if (cookSh.rows.size() <= 5000) {
                cookSh.rows.push_back({
                    std::to_wstring(p + 1),
                    std::move(domain),
                    DetailAppleCookies::ReadCookieStr(rec, cSize, nameOff),
                    DetailAppleCookies::ReadCookieStr(rec, cSize, pathOff),
                    DetailAppleCookies::DescribeCookieFlags(flags),
                    DetailAppleCookies::FormatCfAbsoluteTimeUtc(creCf),
                    DetailAppleCookies::FormatCfAbsoluteTimeUtc(expCf),
                    DetailAppleCookies::ReadCookieStr(rec, cSize, valOff),
                    commentOff ? DetailAppleCookies::ReadCookieStr(rec, cSize, commentOff) : L""
                });
            }
        }
    }

    uint32_t checksum = (curPos + 8 <= raw.size()) ? ::Explorer2::ByteIO::RdU32BE(raw.data() + curPos + 4) : 0;
    size_t tailMetaLen = (curPos + 8 < raw.size()) ? (raw.size() - curPos - 8) : 0;

    SpreadsheetSheet sumSh;
    sumSh.name = L"BinaryCookies_Summary";
    sumSh.rows.push_back({ L"Property", L"Value" });
    sumSh.rows.push_back({ L"Container Magic", L"cook (Apple CFNetwork BinaryCookies)" });
    sumSh.rows.push_back({ L"Cookie Pages", std::to_wstring(numPages) });
    sumSh.rows.push_back({ L"Total Decoded Cookies", std::to_wstring(totalCookies) });
    if (checksum != 0) sumSh.rows.push_back({ L"Container Checksum", DetailBinarySysDoc::Hex32(checksum) });
    if (tailMetaLen > 0) sumSh.rows.push_back({ L"Trailing Metadata Plist", PathUtils::FormatByteSize(tailMetaLen) });
    FinalizeSheetCols(sumSh);
    FinalizeSheetCols(cookSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apple Safari Binary Cookies (.binarycookies — " + std::to_wstring(totalCookies) + L" cookie(s))";
    out.sheets.push_back(std::move(cookSh));
    out.sheets.push_back(std::move(sumSh));
    out.valid = true;
    return true;
}

} // namespace Formats
