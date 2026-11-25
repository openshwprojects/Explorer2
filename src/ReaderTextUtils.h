#pragma once
// ReaderTextUtils.h — shared text, token, numeric, case-insensitive, UTF-16 span, calendar, and
// relative-path safety helpers used across all format readers (3D, EDA, images, documents, archives).
#include "ByteIO.h"
#include "CharConvCompat.h"
#include "PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <string_view>
#include <system_error>

namespace Explorer2::ReaderText {

inline std::string_view TrimSv(std::string_view s) {
    return PathUtils::TrimAscii(s);
}

inline std::string_view TrimCrTabSpace(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

inline std::string_view StripUtf8Bom(std::string_view s) {
    if (s.size() >= 3 &&
        static_cast<uint8_t>(s[0]) == 0xEFu &&
        static_cast<uint8_t>(s[1]) == 0xBBu &&
        static_cast<uint8_t>(s[2]) == 0xBFu) {
        s.remove_prefix(3);
    }
    return s;
}

inline std::string ToLowerAscii(std::string_view s) {
    return PathUtils::ToLowerAscii(s);
}

inline std::string ToUpperAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

inline bool AsciiEqCI(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + ('a' - 'A'));
        if (ca != cb) return false;
    }
    return true;
}

inline bool StartsWithCI(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && AsciiEqCI(s.substr(0, prefix.size()), prefix);
}

inline bool EndsWithCI(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && AsciiEqCI(s.substr(s.size() - suffix.size()), suffix);
}

inline bool EndsWithWideCI(std::wstring_view s, std::wstring_view suffix) {
    if (s.size() < suffix.size()) return false;
    const size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        wchar_t ca = s[off + i];
        wchar_t cb = suffix[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca + (L'a' - L'A'));
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb + (L'a' - L'A'));
        if (ca != cb) return false;
    }
    return true;
}

inline int HexVal(char c) {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
    return -1;
}

inline int HexValWide(wchar_t c) {
    if (c >= L'0' && c <= L'9') return static_cast<int>(c - L'0');
    if (c >= L'A' && c <= L'F') return static_cast<int>(c - L'A' + 10);
    if (c >= L'a' && c <= L'f') return static_cast<int>(c - L'a' + 10);
    return -1;
}

inline bool NextTokenSv(std::string_view& line, std::string_view& outTok) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.remove_prefix(1);
    if (line.empty()) return false;
    size_t i = 0;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
    outTok = line.substr(0, i);
    line.remove_prefix(i);
    return true;
}

inline bool NextLineSv(std::string_view sv, size_t& pos, std::string_view& outLine) {
    if (pos >= sv.size()) return false;
    size_t eol = sv.find('\n', pos);
    if (eol == std::string_view::npos) {
        outLine = sv.substr(pos);
        pos = sv.size();
    } else {
        outLine = sv.substr(pos, eol - pos);
        pos = eol + 1;
    }
    if (!outLine.empty() && outLine.back() == '\r') outLine.remove_suffix(1);
    return true;
}

inline bool ParseFloatSv(std::string_view tok, float& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    float v = 0.0f;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline bool ParseDoubleSv(std::string_view tok, double& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    double v = 0.0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size() || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline bool ParseIntSv(std::string_view tok, int& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.size() >= 3 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        uint32_t u = 0;
        auto res = std::from_chars(tok.data() + 2, tok.data() + tok.size(), u, 16);
        if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
        outVal = static_cast<int>(u);
        return true;
    }
    int v = 0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v, 10);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
    outVal = v;
    return true;
}

inline bool ParseInt64Sv(std::string_view tok, int64_t& outVal) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (tok.size() >= 3 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        uint64_t u = 0;
        auto res = std::from_chars(tok.data() + 2, tok.data() + tok.size(), u, 16);
        if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
        outVal = static_cast<int64_t>(u);
        return true;
    }
    int64_t v = 0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v, 10);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
    outVal = v;
    return true;
}

inline bool ParseUint64Sv(std::string_view tok, uint64_t& outVal, int base = 10) {
    tok = TrimSv(tok);
    if (tok.empty()) return false;
    if (tok.front() == '+') tok.remove_prefix(1);
    if (base == 16 && tok.size() >= 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) tok.remove_prefix(2);
    if (tok.empty()) return false;
    uint64_t v = 0;
    auto res = std::from_chars(tok.data(), tok.data() + tok.size(), v, base);
    if (res.ec != std::errc{} || res.ptr != tok.data() + tok.size()) return false;
    outVal = v;
    return true;
}

inline bool IsSafeRelativeSubFile(std::string_view rel) {
    if (rel.empty() || rel.size() > 260) return false;
    if (rel.front() == '/' || rel.front() == '\\') return false;
    if (rel.find(':') != std::string_view::npos) return false;
    if (rel.find("..") != std::string_view::npos) return false;
    for (unsigned char c : rel) {
        if (c < 0x20u) return false;
    }
    return true;
}

inline std::wstring DecodeUtf16Span(const uint8_t* p, size_t charCount, bool bigEndian) {
    std::wstring out;
    out.reserve(std::min<size_t>(charCount, 65536u));
    for (size_t i = 0; i < charCount; ++i) {
        uint16_t u = bigEndian ? ByteIO::RdU16BE(p + i * 2u) : ByteIO::RdU16LE(p + i * 2u);
        if (u == 0) break;
        if (u >= 0xD800u && u <= 0xDBFFu && i + 1u < charCount) {
            uint16_t lo = bigEndian ? ByteIO::RdU16BE(p + (i + 1u) * 2u) : ByteIO::RdU16LE(p + (i + 1u) * 2u);
            if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                PathUtils::AppendUtf16(out, 0x10000u + (((static_cast<uint32_t>(u) - 0xD800u) << 10) | (lo - 0xDC00u)));
                ++i;
                continue;
            }
        }
        PathUtils::AppendUtf16(out, u);
    }
    return out;
}

// Howard Hinnant's civil-from-days algorithm (days since 1970-01-01 -> Gregorian Y-M-D)
inline void CivilFromDays1970(int64_t z, int& outY, int& outM, int& outD) {
    z += 719468LL;
    const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097LL);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400LL;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;
    outD = static_cast<int>(doy - (153u * mp + 2u) / 5u + 1u);
    outM = static_cast<int>((mp < 10u) ? (mp + 3u) : (mp - 9u));
    outY = static_cast<int>(y + (outM <= 2 ? 1 : 0));
}

inline std::wstring FormatUnixUtc(int64_t unixSec) {
    int64_t days = (unixSec >= 0) ? (unixSec / 86400LL) : ((unixSec - 86399LL) / 86400LL);
    int64_t sod = unixSec - days * 86400LL;
    int y = 1970, m = 1, d = 1;
    CivilFromDays1970(days, y, m, d);
    int hh = static_cast<int>(sod / 3600LL);
    int mm = static_cast<int>((sod % 3600LL) / 60LL);
    int ss = static_cast<int>(sod % 60LL);
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d UTC", y, m, d, hh, mm, ss);
    return buf;
}

inline void CalendarToDosDateTime(
    int year, int month, int day,
    int hour, int minute, int second,
    uint16_t& outDate, uint16_t& outTime)
{
    outDate = 0;
    outTime = 0;
    if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 || day > 31) return;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return;
    if (second == 60) second = 59;
    outDate = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
    outTime = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
}

} // namespace Explorer2::ReaderText
