#pragma once
// ApplePlist.h — Zero-dependency C++20 Apple Binary ("bplist00" + NSKeyedArchiver) & XML Property List (.plist) reader.

namespace Formats {

namespace DetailPlist {

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

inline uint64_t RdBeUintN(const uint8_t* p, uint8_t nBytes) {
    uint64_t v = 0;
    for (uint8_t i = 0; i < nBytes; ++i) v = (v << 8) | p[i];
    return v;
}

struct BplistDoc {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint8_t offsetIntSize = 0;
    uint8_t objectRefSize = 0;
    uint64_t numObjects = 0;
    uint64_t topObject = 0;
    uint64_t offsetTableOff = 0;
    std::vector<uint64_t> offsets;

    // NSKeyedArchiver support
    bool isKeyedArchive = false;
    std::vector<uint64_t> kaObjects; // object indices inside $objects NSArray

    bool ReadLengthCount(size_t& pos, uint8_t lowNibble, uint64_t& outCount) const {
        if (lowNibble != 0x0Fu) {
            outCount = lowNibble;
            return true;
        }
        if (pos >= size) return false;
        uint8_t intMarker = data[pos++];
        if ((intMarker & 0xF0u) != 0x10u) return false;
        uint8_t shift = intMarker & 0x0Fu;
        if (shift > 3u) return false;
        uint8_t nBytes = static_cast<uint8_t>(1u << shift);
        if (nBytes > size - pos) return false;
        outCount = RdBeUintN(data + pos, nBytes);
        pos += nBytes;
        return true;
    }

    bool ReadScalarAsString(uint64_t objIdx, std::wstring& outType, std::wstring& outVal, uint64_t* outUidVal = nullptr) const {
        if (objIdx >= offsets.size()) return false;
        size_t pos = static_cast<size_t>(offsets[objIdx]);
        if (pos >= size) return false;
        uint8_t marker = data[pos++];
        uint8_t hi = (marker >> 4) & 0x0Fu;
        uint8_t lo = marker & 0x0Fu;

        if (hi == 0x00u) {
            if (lo == 0x00u) { outType = L"Null"; outVal = L"null"; return true; }
            if (lo == 0x08u) { outType = L"Boolean"; outVal = L"false"; return true; }
            if (lo == 0x09u) { outType = L"Boolean"; outVal = L"true"; return true; }
            if (lo == 0x0Eu && pos + 16 <= size) {
                const uint8_t* u = data + pos;
                wchar_t ubuf[64] = {};
                swprintf_s(ubuf, L"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                    u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
                outType = L"UUID";
                outVal = ubuf;
                return true;
            }
            if (lo == 0x0Fu) { outType = L"Fill"; outVal = L""; return true; }
            return false;
        }
        if (hi == 0x01u) { // Integer (1, 2, 4, 8, 16 bytes BE)
            if (lo > 4u) return false;
            size_t nBytes = size_t(1u) << lo;
            if (nBytes > size - pos) return false;
            outType = L"Integer";
            if (nBytes < 8u) {
                outVal = std::to_wstring(RdBeUintN(data + pos, static_cast<uint8_t>(nBytes)));
            } else if (nBytes == 8u) {
                outVal = std::to_wstring(DetailPlist::RdI64BE(data + pos));
            } else { // 16-byte integer
                uint64_t hi64 = DetailPlist::RdU64BE(data + pos);
                uint64_t lo64 = DetailPlist::RdU64BE(data + pos + 8);
                if (hi64 == 0) outVal = std::to_wstring(lo64);
                else if (hi64 == UINT64_MAX) outVal = std::to_wstring(static_cast<int64_t>(lo64));
                else outVal = L"0x" + PathUtils::HexBytes(data + pos, 16, 0);
            }
            return true;
        }
        if (hi == 0x02u) { // Real
            outType = L"Real";
            double dv = 0.0;
            if (lo == 2u && pos + 4 <= size) dv = DetailPlist::RdF32BE(data + pos);
            else if (lo == 3u && pos + 8 <= size) dv = DetailPlist::RdF64BE(data + pos);
            else return false;
            wchar_t b[64] = {};
            swprintf_s(b, L"%.14g", dv);
            outVal = b;
            return true;
        }
        if (marker == 0x33u) { // NSDate: float64 BE seconds since 2001-01-01 00:00:00 UTC (+978307200 Unix sec)
            if (pos + 8 > size) return false;
            double sec2001 = DetailPlist::RdF64BE(data + pos);
            outType = L"Date";
            if (!std::isfinite(sec2001) || std::fabs(sec2001) > 3.0e11) {
                outVal = L"(invalid date)";
            } else {
                int64_t unixSec = static_cast<int64_t>(std::floor(sec2001 + 0.5)) + 978307200LL;
                outVal = DetailPlist::FormatUnixUtc(unixSec);
            }
            return true;
        }
        if (hi == 0x04u) { // NSData
            uint64_t cnt = 0;
            if (!ReadLengthCount(pos, lo, cnt) || cnt > size - pos) return false;
            outType = L"Data";
            outVal = L"[" + std::to_wstring(cnt) + L" B] " + PathUtils::HexBytes(data + pos, static_cast<size_t>(cnt), L' ', 20);
            return true;
        }
        if (hi == 0x05u || hi == 0x07u) { // ASCII (0x50) or UTF-8 (0x70) String
            uint64_t cnt = 0;
            if (!ReadLengthCount(pos, lo, cnt) || cnt > size - pos) return false;
            outType = L"String";
            size_t show = static_cast<size_t>(std::min<uint64_t>(cnt, 2048ULL));
            if (hi == 0x07u) {
                outVal = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(data + pos), show));
            } else {
                outVal.clear();
                outVal.reserve(show);
                for (size_t i = 0; i < show; ++i) outVal.push_back(static_cast<wchar_t>(data[pos + i]));
            }
            return true;
        }
        if (hi == 0x06u) { // UTF-16BE String (cnt = 16-bit code units)
            uint64_t cnt = 0;
            if (!ReadLengthCount(pos, lo, cnt) || cnt > (size - pos) / 2u) return false;
            outType = L"String";
            outVal = DetailPlist::DecodeUtf16Span(data + pos, static_cast<size_t>(std::min<uint64_t>(cnt, 2048ULL)), true);
            return true;
        }
        if (hi == 0x08u) { // CF$UID ((lo + 1) bytes BE)
            uint8_t nBytes = static_cast<uint8_t>(lo + 1u);
            if (nBytes > 8u || nBytes > size - pos) return false;
            uint64_t uid = RdBeUintN(data + pos, nBytes);
            if (outUidVal) *outUidVal = uid;
            outType = L"CF$UID";
            outVal = L"UID(" + std::to_wstring(uid) + L")";
            return true;
        }
        return false;
    }

    bool ReadContainerChildren(uint64_t objIdx, uint8_t& outKindHi,
                               std::vector<uint64_t>& outKeys, std::vector<uint64_t>& outVals) const {
        outKeys.clear();
        outVals.clear();
        if (objIdx >= offsets.size()) return false;
        size_t pos = static_cast<size_t>(offsets[objIdx]);
        if (pos >= size) return false;
        uint8_t marker = data[pos++];
        outKindHi = (marker >> 4) & 0x0Fu;
        uint8_t lo = marker & 0x0Fu;
        if (outKindHi != 0x0Au && outKindHi != 0x0Bu && outKindHi != 0x0Cu && outKindHi != 0x0Du) return false;

        uint64_t cnt = 0;
        if (!ReadLengthCount(pos, lo, cnt)) return false;
        uint64_t totalRefs = (outKindHi == 0x0Du) ? (cnt * 2ULL) : cnt;
        if (cnt > 250000ULL || totalRefs > (size - pos) / objectRefSize) return false;

        if (outKindHi == 0x0Du) {
            outKeys.resize(static_cast<size_t>(cnt));
            outVals.resize(static_cast<size_t>(cnt));
            for (size_t i = 0; i < static_cast<size_t>(cnt); ++i) {
                outKeys[i] = RdBeUintN(data + pos + i * objectRefSize, objectRefSize);
                if (outKeys[i] >= numObjects) return false;
            }
            size_t valBase = pos + static_cast<size_t>(cnt) * objectRefSize;
            for (size_t i = 0; i < static_cast<size_t>(cnt); ++i) {
                outVals[i] = RdBeUintN(data + valBase + i * objectRefSize, objectRefSize);
                if (outVals[i] >= numObjects) return false;
            }
        } else {
            outVals.resize(static_cast<size_t>(cnt));
            for (size_t i = 0; i < static_cast<size_t>(cnt); ++i) {
                outVals[i] = RdBeUintN(data + pos + i * objectRefSize, objectRefSize);
                if (outVals[i] >= numObjects) return false;
            }
        }
        return true;
    }

    void WalkObject(uint64_t objIdx, const std::wstring& path, int depth,
                    std::vector<bool>& activeStack, SpreadsheetSheet& sh) const {
        if (depth > 32 || sh.rows.size() > 5000 || objIdx >= numObjects) return;
        if (activeStack[static_cast<size_t>(objIdx)]) {
            sh.rows.push_back({ path.empty() ? L"(root)" : path, L"CycleRef", L"<Cycle -> #" + std::to_wstring(objIdx) + L">" });
            return;
        }

        std::wstring sType, sVal;
        uint64_t uidVal = UINT64_MAX;
        if (ReadScalarAsString(objIdx, sType, sVal, &uidVal)) {
            if (isKeyedArchive && sType == L"CF$UID" && uidVal < kaObjects.size()) {
                uint64_t resolvedIdx = kaObjects[static_cast<size_t>(uidVal)];
                activeStack[static_cast<size_t>(objIdx)] = true;
                WalkObject(resolvedIdx, path, depth + 1, activeStack, sh);
                activeStack[static_cast<size_t>(objIdx)] = false;
                return;
            }
            sh.rows.push_back({ path.empty() ? L"(root)" : path, sType, sVal });
            return;
        }

        uint8_t kindHi = 0;
        std::vector<uint64_t> keys, vals;
        if (!ReadContainerChildren(objIdx, kindHi, keys, vals)) return;

        activeStack[static_cast<size_t>(objIdx)] = true;
        if (kindHi == 0x0Du) { // Dict
            if (vals.empty()) {
                sh.rows.push_back({ path.empty() ? L"(root)" : path, L"Dictionary", L"{0 entries}" });
            }
            for (size_t i = 0; i < vals.size() && sh.rows.size() <= 5000; ++i) {
                std::wstring kt, kv;
                if (!ReadScalarAsString(keys[i], kt, kv)) kv = L"#" + std::to_wstring(keys[i]);
                if (isKeyedArchive && kv == L"$class") continue;
                std::wstring childPath = path.empty() ? kv : (path + L"." + kv);
                WalkObject(vals[i], childPath, depth + 1, activeStack, sh);
            }
        } else { // Array / Set
            const wchar_t* cLabel = (kindHi == 0x0Au) ? L"Array" : L"Set";
            if (vals.empty()) {
                sh.rows.push_back({ path.empty() ? L"(root)" : path, cLabel, L"[0 items]" });
            }
            for (size_t i = 0; i < vals.size() && sh.rows.size() <= 5000; ++i) {
                std::wstring childPath = (path.empty() ? L"" : path) + L"[" + std::to_wstring(i) + L"]";
                WalkObject(vals[i], childPath, depth + 1, activeStack, sh);
            }
        }
        activeStack[static_cast<size_t>(objIdx)] = false;
    }
};

inline bool ParseBinaryPlist(const uint8_t* data, size_t size, ParsedOfficeDocument& out) {
    if (size < 40 || std::memcmp(data, "bplist", 6) != 0) return false;
    if (data[6] < '0' || data[6] > '9' || data[7] < '0' || data[7] > '9') return false;
    std::string verTag(reinterpret_cast<const char*>(data), 8);

    const uint8_t* tr = data + size - 32;
    BplistDoc bp;
    bp.data           = data;
    bp.size           = size;
    bp.offsetIntSize  = tr[6];
    bp.objectRefSize  = tr[7];
    bp.numObjects     = DetailPlist::RdU64BE(tr + 8);
    bp.topObject      = DetailPlist::RdU64BE(tr + 16);
    bp.offsetTableOff = DetailPlist::RdU64BE(tr + 24);

    auto validPow2 = [](uint8_t v) { return v == 1u || v == 2u || v == 4u || v == 8u; };
    if (!validPow2(bp.offsetIntSize) || !validPow2(bp.objectRefSize)) return false;
    if (bp.numObjects == 0 || bp.numObjects > 500000ULL || bp.topObject >= bp.numObjects) return false;
    if (bp.offsetTableOff < 8ULL || bp.offsetTableOff > size - 32ULL) return false;
    if (bp.numObjects > (size - 32ULL - bp.offsetTableOff) / bp.offsetIntSize) return false;

    bp.offsets.resize(static_cast<size_t>(bp.numObjects));
    for (size_t i = 0; i < bp.offsets.size(); ++i) {
        uint64_t off = RdBeUintN(data + bp.offsetTableOff + i * bp.offsetIntSize, bp.offsetIntSize);
        if (off < 8ULL || off >= size - 32ULL) return false;
        bp.offsets[i] = off;
    }

    // Detect NSKeyedArchiver ($archiver == "NSKeyedArchiver" + $objects + $top)
    uint64_t startObj = bp.topObject;
    {
        uint8_t rootKind = 0;
        std::vector<uint64_t> rKeys, rVals;
        if (bp.ReadContainerChildren(bp.topObject, rootKind, rKeys, rVals) && rootKind == 0x0Du) {
            uint64_t objsRef = UINT64_MAX, topRef = UINT64_MAX;
            bool archMatch = false;
            for (size_t i = 0; i < rKeys.size(); ++i) {
                std::wstring kt, kv;
                if (!bp.ReadScalarAsString(rKeys[i], kt, kv)) continue;
                if (kv == L"$archiver") {
                    std::wstring vt, vv;
                    if (bp.ReadScalarAsString(rVals[i], vt, vv) && vv == L"NSKeyedArchiver") archMatch = true;
                } else if (kv == L"$objects") {
                    objsRef = rVals[i];
                } else if (kv == L"$top") {
                    topRef = rVals[i];
                }
            }
            uint8_t arrKind = 0;
            std::vector<uint64_t> dummyKeys;
            if (archMatch && objsRef < bp.numObjects && topRef < bp.numObjects &&
                bp.ReadContainerChildren(objsRef, arrKind, dummyKeys, bp.kaObjects) && arrKind == 0x0Au) {
                bp.isKeyedArchive = true;
                startObj = topRef;
            }
        }
    }

    SpreadsheetSheet sh;
    sh.name = L"Plist_Entries";
    sh.rows.push_back({ L"Key Path", L"Type", L"Value" });

    std::vector<bool> activeStack(static_cast<size_t>(bp.numObjects), false);
    bp.WalkObject(startObj, L"", 0, activeStack, sh);
    if (sh.rows.size() <= 1) return false;

    SpreadsheetSheet sumSh;
    sumSh.name = L"Plist_Summary";
    sumSh.rows.push_back({ L"Property", L"Value" });
    sumSh.rows.push_back({ L"Container Encoding", L"Apple Binary Property List (" + PathUtils::Utf8ToWide(verTag) + L")" });
    sumSh.rows.push_back({ L"Object Count", std::to_wstring(bp.numObjects) + L" (top=#" + std::to_wstring(bp.topObject) + L")" });
    sumSh.rows.push_back({ L"Offset / Ref Sizes", std::to_wstring(bp.offsetIntSize) + L" B / " + std::to_wstring(bp.objectRefSize) + L" B" });
    if (bp.isKeyedArchive) sumSh.rows.push_back({ L"Archiver", L"NSKeyedArchiver (CF$UID resolved)" });

    FinalizeSheetCols(sh);
    FinalizeSheetCols(sumSh);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = bp.isKeyedArchive
        ? L"Apple Binary Plist / NSKeyedArchiver (.plist)"
        : L"Apple Binary Property List (.plist)";
    out.sheets.push_back(std::move(sh));
    out.sheets.push_back(std::move(sumSh));
    out.valid = true;
    return true;
}

inline bool WalkXmlPlistValue(std::string_view sv, size_t& pos, const std::wstring& path, int depth, SpreadsheetSheet& sh) {
    if (depth > 32 || sh.rows.size() > 5000) return false;
    ::Explorer2::XmlTagScan::XmlTag t;
    if (!::Explorer2::XmlTagScan::NextTag(sv, pos, t) || t.isClosing) return false;

    const std::wstring dispPath = path.empty() ? L"(root)" : path;
    if (t.localName == "true" || t.localName == "false") {
        if (!t.isSelfClosing) {
            std::string_view dummy; size_t ac = pos;
            if (::Explorer2::XmlTagScan::FindElementBody(sv, pos, t.localName, dummy, ac)) pos = ac;
        }
        sh.rows.push_back({ dispPath, L"Boolean", t.localName == "true" ? L"true" : L"false" });
        return true;
    }
    if (t.localName == "string" || t.localName == "integer" || t.localName == "real" || t.localName == "date" || t.localName == "data") {
        std::string text;
        if (!t.isSelfClosing) {
            std::string_view body; size_t ac = pos;
            if (!::Explorer2::XmlTagScan::FindElementBody(sv, pos, t.localName, body, ac)) return false;
            pos = ac;
            text = ::Explorer2::XmlTagScan::DecodeText(body);
        }
        if (t.localName == "data") {
            std::vector<uint8_t> bin;
            ::Explorer2::Base64::Decode(text, bin);
            sh.rows.push_back({ dispPath, L"Data", L"[" + std::to_wstring(bin.size()) + L" B] " + PathUtils::HexBytes(bin.data(), bin.size(), L' ', 20) });
        } else {
            const wchar_t* typeLbl = (t.localName == "integer") ? L"Integer" :
                                     (t.localName == "real")    ? L"Real" :
                                     (t.localName == "date")    ? L"Date" : L"String";
            std::wstring valW = PathUtils::Utf8ToWide(text);
            if (t.localName != "string") valW = PathUtils::Trim(valW);
            sh.rows.push_back({ dispPath, typeLbl, std::move(valW) });
        }
        return true;
    }
    if (t.localName == "dict") {
        if (t.isSelfClosing) {
            sh.rows.push_back({ dispPath, L"Dictionary", L"{0 entries}" });
            return true;
        }
        std::string_view dictBody; size_t afterDict = pos;
        if (!::Explorer2::XmlTagScan::FindElementBody(sv, pos, "dict", dictBody, afterDict)) return false;
        pos = afterDict;

        size_t dp = 0;
        size_t count = 0;
        while (dp < dictBody.size() && sh.rows.size() <= 5000) {
            ::Explorer2::XmlTagScan::XmlTag kt;
            if (!::Explorer2::XmlTagScan::NextTag(dictBody, dp, kt)) break;
            if (kt.isClosing || kt.localName != "key") return false;
            std::string keyStr;
            if (!kt.isSelfClosing) {
                std::string_view kb; size_t ak = dp;
                if (!::Explorer2::XmlTagScan::FindElementBody(dictBody, dp, "key", kb, ak)) return false;
                dp = ak;
                keyStr = ::Explorer2::XmlTagScan::DecodeText(kb);
            }
            std::wstring keyW = PathUtils::Utf8ToWide(keyStr);
            std::wstring childPath = path.empty() ? keyW : (path + L"." + keyW);
            if (!WalkXmlPlistValue(dictBody, dp, childPath, depth + 1, sh)) return false;
            ++count;
        }
        if (count == 0) sh.rows.push_back({ dispPath, L"Dictionary", L"{0 entries}" });
        return true;
    }
    if (t.localName == "array") {
        if (t.isSelfClosing) {
            sh.rows.push_back({ dispPath, L"Array", L"[0 items]" });
            return true;
        }
        std::string_view arrBody; size_t afterArr = pos;
        if (!::Explorer2::XmlTagScan::FindElementBody(sv, pos, "array", arrBody, afterArr)) return false;
        pos = afterArr;

        size_t ap = 0;
        size_t idx = 0;
        while (ap < arrBody.size() && sh.rows.size() <= 5000) {
            size_t peek = ap;
            ::Explorer2::XmlTagScan::XmlTag peekTag;
            if (!::Explorer2::XmlTagScan::NextTag(arrBody, peek, peekTag)) break;
            std::wstring childPath = (path.empty() ? L"" : path) + L"[" + std::to_wstring(idx++) + L"]";
            if (!WalkXmlPlistValue(arrBody, ap, childPath, depth + 1, sh)) return false;
        }
        if (idx == 0) sh.rows.push_back({ dispPath, L"Array", L"[0 items]" });
        return true;
    }
    return false;
}

} // namespace DetailPlist

inline bool ParseApplePlist(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8) return false;

    // Branch A: Binary Plist ("bplist00" .. "bplist15")
    if (std::memcmp(raw.data(), "bplist", 6) == 0) {
        if (out.sourceFileSize > raw.size()) {
            SourceBytes sb(raw, out);
            if (sb.Size() <= 64u * 1024u * 1024u) {
                std::vector<uint8_t> whole;
                const uint8_t* wp = sb.Read(0, static_cast<size_t>(sb.Size()), whole);
                if (wp) {
                    bool ok = DetailPlist::ParseBinaryPlist(wp, static_cast<size_t>(sb.Size()), out);
                    if (ok) out.sourceReadFully = true;
                    return ok;
                }
            }
            return false;
        }
        return DetailPlist::ParseBinaryPlist(raw.data(), raw.size(), out);
    }

    // Branch B: Apple XML Property List (<!DOCTYPE plist ...> / <plist version="1.0">)
    std::string_view sv = ::Explorer2::Json::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    size_t pos = 0;
    ::Explorer2::XmlTagScan::XmlTag rootTag;
    if (!::Explorer2::XmlTagScan::NextTag(sv, pos, rootTag) || rootTag.isClosing || rootTag.localName != "plist") {
        return false;
    }
    std::wstring plistVer = PathUtils::Utf8ToWide(::Explorer2::XmlTagScan::GetAttr(rootTag.attrs, "version"));
    std::string_view plistBody;
    size_t afterPlist = pos;
    if (!rootTag.isSelfClosing) {
        if (!::Explorer2::XmlTagScan::FindElementBody(sv, pos, "plist", plistBody, afterPlist)) return false;
    }

    SpreadsheetSheet sh;
    sh.name = L"Plist_Entries";
    sh.rows.push_back({ L"Key Path", L"Type", L"Value" });

    size_t bp = 0;
    if (!plistBody.empty() && !DetailPlist::WalkXmlPlistValue(plistBody, bp, L"", 0, sh)) {
        return false;
    }
    if (sh.rows.size() <= 1) return false;

    SpreadsheetSheet sumSh;
    sumSh.name = L"Plist_Summary";
    sumSh.rows.push_back({ L"Property", L"Value" });
    sumSh.rows.push_back({ L"Container Encoding", L"Apple XML Property List" + (plistVer.empty() ? L"" : (L" (v" + plistVer + L")")) });
    sumSh.rows.push_back({ L"Decoded Entries", std::to_wstring(sh.rows.size() - 1) });

    FinalizeSheetCols(sh);
    FinalizeSheetCols(sumSh);
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Apple XML Property List (.plist)";
    out.sheets.push_back(std::move(sh));
    out.sheets.push_back(std::move(sumSh));
    out.valid = true;
    return true;
}

} // namespace Formats
