#pragma once
// DocFormatCommon.h — Shared detail namespaces for Document, AI/Data, GIS, Media & Security readers.
// Included inside namespace Explorer2 in DocReaders.cpp after Parquet.h, Kml.h, Sqlite.h, ContentLines.h, GpsTrackMap.h.

#ifndef E2_DOC_FORMAT_CORE_ONLY
#include "ContentLines.h"
namespace DetailExcelOds {

using ::Explorer2::ReaderText::AsciiEqCI;
using ::Explorer2::ReaderText::EndsWithWideCI;
inline std::string_view TrimAsciiSV(std::string_view s) { return ::Explorer2::ReaderText::TrimSv(s); }
inline int HexValChar(wchar_t c) { return ::Explorer2::ReaderText::HexValWide(c); }

// Single-pass XML entity + ECMA-376 ST_Xstring control escape decoder
inline std::wstring DecodeSpreadsheetXml(std::string_view utf8Raw, bool decodeOoxmlXstr = false) {
    if (utf8Raw.empty()) return {};
    const std::wstring w = ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Utf8ToWide(utf8Raw));
    std::wstring unent;
    unent.reserve(w.size());

    size_t i = 0;
    while (i < w.size()) {
        // OOXML ST_Xstring: _xHHHH_ stands for a character XML text cannot carry (tab, CR, LF)
        if (decodeOoxmlXstr && w[i] == L'_' && i + 6 < w.size() && (w[i + 1] == L'x' || w[i + 1] == L'X') && w[i + 6] == L'_') {
            int h0 = HexValChar(w[i + 2]), h1 = HexValChar(w[i + 3]), h2 = HexValChar(w[i + 4]), h3 = HexValChar(w[i + 5]);
            if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                uint32_t cp = static_cast<uint32_t>((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                if (cp == 0x0009u || cp == 0x000Au || cp == 0x000Du) {
                    unent.push_back(static_cast<wchar_t>(cp));
                    i += 7;
                    continue;
                }
            }
        }
        unent.push_back(w[i++]);
    }

    // Normalize CR/LF and lone CR to LF
    std::wstring norm;
    norm.reserve(unent.size());
    for (size_t k = 0; k < unent.size(); ++k) {
        if (unent[k] == L'\r') {
            norm.push_back(L'\n');
            if (k + 1 < unent.size() && unent[k + 1] == L'\n') ++k;
        } else {
            norm.push_back(unent[k]);
        }
    }
    return norm;
}

inline bool IsTagNameStop(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>' || c == '/';
}

inline std::string_view StripXmlNsPrefix(std::string_view tagName) {
    size_t colon = tagName.find(':');
    return (colon != std::string_view::npos) ? tagName.substr(colon + 1) : tagName;
}

// Quote-aware scan for closing '>' of an XML tag
inline size_t FindTagCloseGt(std::string_view xml, size_t tagStart) {
    char quote = 0;
    for (size_t i = tagStart + 1; i < xml.size(); ++i) {
        char c = xml[i];
        if (quote != 0) {
            if (c == quote) quote = 0;
        } else {
            if (c == '"' || c == '\'') quote = c;
            else if (c == '>') return i;
        }
    }
    return std::string_view::npos;
}

// Finds next opening tag whose local name (ignoring optional namespace prefix) equals `localName`
inline bool FindXmlElemOpen(
    std::string_view xml,
    std::string_view localName,
    size_t searchFrom,
    size_t& outTagStart,
    size_t& outTagGt,
    bool& outSelfClosing)
{
    size_t pos = searchFrom;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= xml.size()) return false;
        char next = xml[lt + 1];
        if (next == '!') {
            if (lt + 3 < xml.size() && xml[lt + 2] == '-' && xml[lt + 3] == '-') {
                size_t endCmt = xml.find("-->", lt + 4);
                if (endCmt == std::string_view::npos) return false;
                pos = endCmt + 3;
                continue;
            }
            if (lt + 8 < xml.size() && xml.substr(lt + 2, 7) == "[CDATA[") {
                size_t endCd = xml.find("]]>", lt + 9);
                if (endCd == std::string_view::npos) return false;
                pos = endCd + 3;
                continue;
            }
            size_t gt = xml.find('>', lt + 2);
            if (gt == std::string_view::npos) return false;
            pos = gt + 1;
            continue;
        }
        if (next == '?' || next == '/') {
            size_t gt = xml.find('>', lt + 2);
            if (gt == std::string_view::npos) return false;
            pos = gt + 1;
            continue;
        }

        size_t nameEnd = lt + 1;
        while (nameEnd < xml.size() && !IsTagNameStop(xml[nameEnd])) ++nameEnd;
        std::string_view fullName = xml.substr(lt + 1, nameEnd - (lt + 1));
        size_t gt = FindTagCloseGt(xml, lt);
        if (gt == std::string_view::npos) return false;

        if (StripXmlNsPrefix(fullName) == localName) {
            outTagStart = lt;
            outTagGt = gt;
            size_t prev = gt;
            while (prev > lt + 1 && (xml[prev - 1] == ' ' || xml[prev - 1] == '\t' || xml[prev - 1] == '\r' || xml[prev - 1] == '\n')) {
                --prev;
            }
            outSelfClosing = (prev > lt + 1 && xml[prev - 1] == '/');
            return true;
        }
        pos = gt + 1;
    }
    return false;
}

// Depth-aware close tag finder matching `</localName>` or `</prefix:localName>`
inline bool FindXmlElemClose(
    std::string_view xml,
    std::string_view localName,
    size_t afterOpenGt,
    size_t& outBodyEnd,
    size_t& outAfterCloseGt)
{
    int depth = 1;
    size_t pos = afterOpenGt;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= xml.size()) return false;
        char next = xml[lt + 1];
        if (next == '!') {
            if (lt + 3 < xml.size() && xml[lt + 2] == '-' && xml[lt + 3] == '-') {
                size_t endCmt = xml.find("-->", lt + 4);
                if (endCmt == std::string_view::npos) return false;
                pos = endCmt + 3;
                continue;
            }
            if (lt + 8 < xml.size() && xml.substr(lt + 2, 7) == "[CDATA[") {
                size_t endCd = xml.find("]]>", lt + 9);
                if (endCd == std::string_view::npos) return false;
                pos = endCd + 3;
                continue;
            }
            size_t gt = xml.find('>', lt + 2);
            if (gt == std::string_view::npos) return false;
            pos = gt + 1;
            continue;
        }
        if (next == '?') {
            size_t gt = xml.find('>', lt + 2);
            if (gt == std::string_view::npos) return false;
            pos = gt + 1;
            continue;
        }
        if (next == '/') {
            size_t nameEnd = lt + 2;
            while (nameEnd < xml.size() && !IsTagNameStop(xml[nameEnd])) ++nameEnd;
            std::string_view fullName = xml.substr(lt + 2, nameEnd - (lt + 2));
            size_t gt = xml.find('>', nameEnd);
            if (gt == std::string_view::npos) return false;
            if (StripXmlNsPrefix(fullName) == localName) {
                --depth;
                if (depth == 0) {
                    outBodyEnd = lt;
                    outAfterCloseGt = gt + 1;
                    return true;
                }
            }
            pos = gt + 1;
            continue;
        }

        size_t nameEnd = lt + 1;
        while (nameEnd < xml.size() && !IsTagNameStop(xml[nameEnd])) ++nameEnd;
        std::string_view fullName = xml.substr(lt + 1, nameEnd - (lt + 1));
        size_t gt = FindTagCloseGt(xml, lt);
        if (gt == std::string_view::npos) return false;
        if (StripXmlNsPrefix(fullName) == localName) {
            size_t prev = gt;
            while (prev > lt + 1 && (xml[prev - 1] == ' ' || xml[prev - 1] == '\t' || xml[prev - 1] == '\r' || xml[prev - 1] == '\n')) {
                --prev;
            }
            bool selfClose = (prev > lt + 1 && xml[prev - 1] == '/');
            if (!selfClose) ++depth;
        }
        pos = gt + 1;
    }
    return false;
}

// Tokenized XML attribute lookup (never matches substrings of other attributes!)
inline bool GetXmlAttr(std::string_view openTag, std::string_view attrName, std::string_view& outVal, bool allowAnyNsForLocal = false) {
    return ::Explorer2::XmlTagScan::FindAttr(openTag, attrName, outVal, allowAnyNsForLocal);
}

inline int64_t ParseIntSV(std::string_view s, int64_t defVal = 0) {
    s = TrimAsciiSV(s);
    if (s.empty()) return defVal;
    bool neg = false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') {
        neg = (s[0] == '-');
        i = 1;
    }
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return defVal;
    int64_t val = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        if (val <= 900000000000000000LL) {
            val = val * 10 + static_cast<int64_t>(s[i] - '0');
        }
    }
    return neg ? -val : val;
}

inline std::wstring NormalizeOoxmlTarget(std::string_view rawTarget, std::string_view baseDir = "xl") {
    std::string norm;
    norm.reserve(rawTarget.size() + baseDir.size() + 2);
    for (char c : rawTarget) norm.push_back(c == '\\' ? '/' : c);
    while (!norm.empty() && norm.front() == ' ') norm.erase(norm.begin());

    std::string combined;
    if (!norm.empty() && norm.front() == '/') {
        combined = norm.substr(1);
    } else if (norm.rfind("xl/", 0) == 0 || baseDir.empty()) {
        combined = norm;
    } else {
        combined = std::string(baseDir) + "/" + norm;
    }

    // Collapse '.' and '..' path segments
    std::vector<std::string_view> segs;
    size_t p = 0;
    while (p <= combined.size()) {
        size_t sl = combined.find('/', p);
        if (sl == std::string::npos) sl = combined.size();
        std::string_view seg(combined.data() + p, sl - p);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") {
                if (!segs.empty()) segs.pop_back();
            } else {
                segs.push_back(seg);
            }
        }
        p = sl + 1;
    }

    std::string clean;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i > 0) clean.push_back('/');
        clean.append(segs[i].data(), segs[i].size());
    }
    return PathUtils::Utf8ToWide(clean);
}

inline bool ExtractZipEntryFlex(
    const std::wstring& fullPath,
    const std::vector<ZipDeflate::ArchiveItemMeta>& zipEntries,
    const std::wstring& entryPath,
    std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (ZipDeflate::ExtractZipFileByName(fullPath, entryPath, outBytes) && !outBytes.empty()) {
        return true;
    }
    auto getMetaPath = [](const auto& item) -> std::wstring {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (MemberDetect::HasRelativePath<Item>::value) {
            if constexpr (std::is_constructible_v<std::wstring_view, decltype((item.relativePath))>) return std::wstring(std::wstring_view(item.relativePath));
            else return PathUtils::Utf8ToWide(item.relativePath);
        } else if constexpr (MemberDetect::HasName<Item>::value) {
            if constexpr (std::is_constructible_v<std::wstring_view, decltype((item.name))>) return std::wstring(std::wstring_view(item.name));
            else return PathUtils::Utf8ToWide(item.name);
        } else if constexpr (MemberDetect::HasPath<Item>::value) {
            if constexpr (std::is_constructible_v<std::wstring_view, decltype((item.path))>) return std::wstring(std::wstring_view(item.path));
            else return PathUtils::Utf8ToWide(item.path);
        } else {
            return {};
        }
    };
    auto normPathCI = [](std::wstring_view sv) {
        std::wstring r;
        r.reserve(sv.size());
        for (wchar_t ch : sv) {
            if (ch == L'\\') ch = L'/';
            if (r.empty() && ch == L'/') continue;
            if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
            r.push_back(ch);
        }
        return r;
    };
    const std::wstring want = normPathCI(entryPath);
    for (const auto& ze : zipEntries) {
        std::wstring cand = getMetaPath(ze);
        if (!cand.empty() && normPathCI(cand) == want) {
            if (ZipDeflate::ExtractZipFileByName(fullPath, cand, outBytes) && !outBytes.empty()) {
                return true;
            }
        }
    }
    return false;
}

// Extracts concatenated <t> text runs inside <si> or <is>, strictly skipping <rPh>...</rPh> phonetic furigana blocks!
inline std::wstring ExtractOoxmlStringItemText(std::string_view blk) {
    std::wstring combined;
    size_t pos = 0;
    while (pos < blk.size()) {
        size_t lt = blk.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= blk.size()) break;
        char next = blk[lt + 1];
        if (next == '!' || next == '?' || next == '/') {
            if (next == '!' && lt + 8 < blk.size() && blk.substr(lt + 2, 7) == "[CDATA[") {
                size_t endCd = blk.find("]]>", lt + 9);
                if (endCd == std::string_view::npos) break;
                pos = endCd + 3;
                continue;
            }
            size_t gt = blk.find('>', lt + 1);
            if (gt == std::string_view::npos) break;
            pos = gt + 1;
            continue;
        }

        size_t nameEnd = lt + 1;
        while (nameEnd < blk.size() && !IsTagNameStop(blk[nameEnd])) ++nameEnd;
        std::string_view local = StripXmlNsPrefix(blk.substr(lt + 1, nameEnd - (lt + 1)));
        size_t gt = FindTagCloseGt(blk, lt);
        if (gt == std::string_view::npos) break;

        size_t prev = gt;
        while (prev > lt + 1 && (blk[prev - 1] == ' ' || blk[prev - 1] == '\t' || blk[prev - 1] == '\r' || blk[prev - 1] == '\n')) --prev;
        bool selfClose = (prev > lt + 1 && blk[prev - 1] == '/');

        if (local == "rPh") {
            if (selfClose) {
                pos = gt + 1;
            } else {
                size_t bodyEnd = 0, afterClose = 0;
                if (!FindXmlElemClose(blk, "rPh", gt + 1, bodyEnd, afterClose)) break;
                pos = afterClose;
            }
            continue;
        }
        if (local == "t") {
            if (selfClose) {
                pos = gt + 1;
            } else {
                size_t bodyEnd = 0, afterClose = 0;
                if (!FindXmlElemClose(blk, "t", gt + 1, bodyEnd, afterClose)) break;
                combined += DecodeSpreadsheetXml(blk.substr(gt + 1, bodyEnd - (gt + 1)), true);
                pos = afterClose;
            }
            continue;
        }
        pos = gt + 1;
    }
    return combined;
}

enum class ExcelNumFmtKind {
    General,
    Date,
    Time,
    DateTime,
    Percent
};

struct ExcelStyleFmt {
    ExcelNumFmtKind kind = ExcelNumFmtKind::General;
    int percentDecimals = 2;
};

inline ExcelStyleFmt ClassifyExcelNumberFormat(uint32_t numFmtId, std::string_view rawFormatCode, bool hasCustomCode) {
    if (hasCustomCode && !rawFormatCode.empty()) {
        std::wstring decW = DecodeSpreadsheetXml(rawFormatCode, false);
        std::string code;
        code.reserve(decW.size());
        for (wchar_t ch : decW) {
            code.push_back((ch >= 0 && ch <= 127) ? static_cast<char>(ch) : ' ');
        }

        // Inspect first format section (positive numbers) up to unquoted ';'
        bool hasDateTok = false;
        bool hasTimeTok = false;
        bool hasMonthOrMin = false;
        bool hasPercent = false;
        bool hasNumPlaceholder = false;
        bool seenDecimalPt = false;
        int pctDecs = 0;

        for (size_t i = 0; i < code.size(); ++i) {
            char c = code[i];
            if (c == ';') break;
            if (c == '"') {
                ++i;
                while (i < code.size() && code[i] != '"') ++i;
                continue;
            }
            if (c == '\\' || c == '_' || c == '*') {
                if (i + 1 < code.size()) ++i;
                continue;
            }
            if (c == '[') {
                size_t closeB = code.find(']', i + 1);
                if (closeB == std::string::npos) break;
                std::string_view brk(code.data() + i + 1, closeB - (i + 1));
                brk = TrimAsciiSV(brk);
                if (!brk.empty()) {
                    bool allElapsedTime = true;
                    char firstLower = static_cast<char>(brk[0] | 0x20);
                    if (firstLower != 'h' && firstLower != 'm' && firstLower != 's') allElapsedTime = false;
                    for (char bc : brk) {
                        if ((bc | 0x20) != firstLower) { allElapsedTime = false; break; }
                    }
                    if (allElapsedTime) hasTimeTok = true;
                }
                i = closeB;
                continue;
            }

            char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
            if (lc == 'y' || lc == 'd') {
                hasDateTok = true;
            } else if (lc == 'h') {
                hasTimeTok = true;
            } else if (lc == 's') {
                hasTimeTok = true;
                // Skip optional fractional-second ".000" immediately after 's' so it isn't mistaken for numeric placeholder
                size_t k = i + 1;
                while (k < code.size() && (code[k] == 's' || code[k] == 'S')) ++k;
                if (k < code.size() && code[k] == '.') {
                    ++k;
                    while (k < code.size() && code[k] == '0') ++k;
                    i = k - 1;
                }
            } else if (lc == 'm') {
                hasMonthOrMin = true;
            } else if (c == '%') {
                hasPercent = true;
            } else if (c == '.') {
                seenDecimalPt = true;
            } else if (c == '#' || c == '0' || c == '?') {
                hasNumPlaceholder = true;
                if (seenDecimalPt) ++pctDecs;
            }
        }

        if (!hasNumPlaceholder && (hasDateTok || hasTimeTok || hasMonthOrMin)) {
            if (hasDateTok && hasTimeTok) return { ExcelNumFmtKind::DateTime, 0 };
            if (hasTimeTok) return { ExcelNumFmtKind::Time, 0 };
            return { ExcelNumFmtKind::Date, 0 };
        }
        if (hasPercent) {
            int decs = seenDecimalPt ? (pctDecs > 6 ? 6 : pctDecs) : 0;
            return { ExcelNumFmtKind::Percent, decs };
        }
    }

    if (numFmtId == 9u) return { ExcelNumFmtKind::Percent, 0 };
    if (numFmtId == 10u) return { ExcelNumFmtKind::Percent, 2 };
    if ((numFmtId >= 14u && numFmtId <= 17u) ||
        (numFmtId >= 27u && numFmtId <= 36u) ||
        (numFmtId >= 50u && numFmtId <= 58u))
    {
        return { ExcelNumFmtKind::Date, 0 };
    }
    if ((numFmtId >= 18u && numFmtId <= 21u) ||
        (numFmtId >= 45u && numFmtId <= 47u))
    {
        return { ExcelNumFmtKind::Time, 0 };
    }
    if (numFmtId == 22u) return { ExcelNumFmtKind::DateTime, 0 };
    return { ExcelNumFmtKind::General, 0 };
}

using ::Explorer2::ReaderText::CivilFromDays1970;

inline std::wstring FormatExcelNumericCell(
    std::string_view rawV,
    const ExcelStyleFmt& fmt,
    bool is1904DateSystem)
{
    std::string_view trimmed = TrimAsciiSV(rawV);
    if (trimmed.empty()) return {};

    std::string numStr(trimmed);
    char* endPtr = nullptr;
    double val = std::strtod(numStr.c_str(), &endPtr);
    if (endPtr == numStr.c_str()) {
        return DecodeSpreadsheetXml(trimmed, false);
    }

    if (fmt.kind == ExcelNumFmtKind::Percent) {
        double pct = val * 100.0;
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%.*f%%", fmt.percentDecimals, pct);
        return buf;
    }

    if (fmt.kind == ExcelNumFmtKind::Date ||
        fmt.kind == ExcelNumFmtKind::Time ||
        fmt.kind == ExcelNumFmtKind::DateTime)
    {
        if (val > -700000.0 && val < 3000000.0) {
            int64_t wholeDays = static_cast<int64_t>(val);
            if (val < 0.0 && static_cast<double>(wholeDays) != val) {
                --wholeDays;
            }
            double frac = val - static_cast<double>(wholeDays);
            if (frac < 0.0) frac = 0.0;
            int64_t secOfDay = static_cast<int64_t>(frac * 86400.0 + 0.5);
            if (secOfDay >= 86400LL) {
                secOfDay -= 86400LL;
                ++wholeDays;
            }
            int hh = static_cast<int>(secOfDay / 3600LL);
            int mm = static_cast<int>((secOfDay % 3600LL) / 60LL);
            int ss = static_cast<int>(secOfDay % 60LL);

            if (fmt.kind == ExcelNumFmtKind::Time) {
                wchar_t tbuf[32] = {};
                swprintf_s(tbuf, L"%02d:%02d:%02d", hh, mm, ss);
                return tbuf;
            }

            int yr = 1900, mo = 1, dy = 1;
            if (!is1904DateSystem) {
                if (wholeDays == 60LL) {
                    // Lotus 1-2-3 non-existent 1900-02-29 compatibility date
                    yr = 1900; mo = 2; dy = 29;
                } else {
                    int64_t daysFrom1970 = (wholeDays > 60LL) ? (wholeDays - 25569LL) : (wholeDays - 25568LL);
                    CivilFromDays1970(daysFrom1970, yr, mo, dy);
                }
            } else {
                // 1904 Date System: serial 0 == 1904-01-01 (24107 days before 1970-01-01)
                CivilFromDays1970(wholeDays - 24107LL, yr, mo, dy);
            }

            wchar_t dbuf[64] = {};
            if (fmt.kind == ExcelNumFmtKind::Date && secOfDay == 0LL) {
                swprintf_s(dbuf, L"%04d-%02d-%02d", yr, mo, dy);
            } else if (ss == 0) {
                swprintf_s(dbuf, L"%04d-%02d-%02d %02d:%02d", yr, mo, dy, hh, mm);
            } else {
                swprintf_s(dbuf, L"%04d-%02d-%02d %02d:%02d:%02d", yr, mo, dy, hh, mm, ss);
            }
            return dbuf;
        }
    }

    // General numeric: clean IEEE-754 representation artifacts like 12.300000000000001 while preserving exact integers
    bool hasDotOrExp = (trimmed.find('.') != std::string_view::npos ||
                        trimmed.find('e') != std::string_view::npos ||
                        trimmed.find('E') != std::string_view::npos);
    if (hasDotOrExp && trimmed.size() >= 15) {
        wchar_t gbuf[64] = {};
        swprintf_s(gbuf, L"%.14g", val);
        return gbuf;
    }
    return DecodeSpreadsheetXml(trimmed, false);
}

// Extracts rich text inside an ODF <table:table-cell>, preserving <text:s text:c="N"/>, <text:tab/>, <text:line-break/>,
// joining multiple <text:p> / <text:h> elements with '\n', and skipping <office:annotation> comments.
inline void AppendOdfParagraphInline(std::string_view inner, std::wstring& outParagraph) {
    size_t pos = 0;
    while (pos < inner.size()) {
        size_t lt = inner.find('<', pos);
        if (lt == std::string_view::npos) {
            outParagraph += DecodeSpreadsheetXml(inner.substr(pos), false);
            break;
        }
        if (lt > pos) {
            outParagraph += DecodeSpreadsheetXml(inner.substr(pos, lt - pos), false);
        }
        if (lt + 1 >= inner.size()) break;

        char next = inner[lt + 1];
        if (next == '!') {
            if (lt + 3 < inner.size() && inner[lt + 2] == '-' && inner[lt + 3] == '-') {
                size_t endCmt = inner.find("-->", lt + 4);
                if (endCmt == std::string_view::npos) break;
                pos = endCmt + 3;
                continue;
            }
            if (lt + 8 < inner.size() && inner.substr(lt + 2, 7) == "[CDATA[") {
                size_t endCd = inner.find("]]>", lt + 9);
                if (endCd == std::string_view::npos) break;
                outParagraph += PathUtils::Utf8ToWide(inner.substr(lt + 9, endCd - (lt + 9)));
                pos = endCd + 3;
                continue;
            }
        }
        size_t gt = FindTagCloseGt(inner, lt);
        if (gt == std::string_view::npos) break;

        if (next != '/' && next != '?' && next != '!') {
            size_t nameEnd = lt + 1;
            while (nameEnd < gt && !IsTagNameStop(inner[nameEnd])) ++nameEnd;
            std::string_view local = StripXmlNsPrefix(inner.substr(lt + 1, nameEnd - (lt + 1)));
            std::string_view openTag = inner.substr(lt, gt - lt + 1);

            if (local == "s") {
                std::string_view cntSv;
                int64_t count = 1;
                if (GetXmlAttr(openTag, "text:c", cntSv, true)) {
                    count = ParseIntSV(cntSv, 1);
                    if (count < 1) count = 1;
                    if (count > 256) count = 256;
                }
                outParagraph.append(static_cast<size_t>(count), L' ');
            } else if (local == "tab") {
                outParagraph.push_back(L'\t');
            } else if (local == "line-break") {
                outParagraph.push_back(L'\n');
            } else if (local == "annotation") {
                size_t prev = gt;
                while (prev > lt + 1 && (inner[prev - 1] == ' ' || inner[prev - 1] == '\t' || inner[prev - 1] == '\r' || inner[prev - 1] == '\n')) --prev;
                bool selfClose = (prev > lt + 1 && inner[prev - 1] == '/');
                if (!selfClose) {
                    size_t annEnd = 0, annAfter = 0;
                    if (FindXmlElemClose(inner, "annotation", gt + 1, annEnd, annAfter)) {
                        pos = annAfter;
                        continue;
                    }
                }
            }
        }
        pos = gt + 1;
    }
}

inline std::wstring ExtractOdfCellText(std::string_view cTag, std::string_view cBody) {
    std::wstring cellText;
    bool hasParagraph = false;

    size_t pos = 0;
    while (pos < cBody.size()) {
        size_t lt = cBody.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= cBody.size()) break;
        char next = cBody[lt + 1];
        if (next == '!' || next == '?' || next == '/') {
            if (next == '!' && lt + 3 < cBody.size() && cBody[lt + 2] == '-' && cBody[lt + 3] == '-') {
                size_t endCmt = cBody.find("-->", lt + 4);
                if (endCmt == std::string_view::npos) break;
                pos = endCmt + 3;
                continue;
            }
            size_t gt = cBody.find('>', lt + 1);
            if (gt == std::string_view::npos) break;
            pos = gt + 1;
            continue;
        }

        size_t nameEnd = lt + 1;
        while (nameEnd < cBody.size() && !IsTagNameStop(cBody[nameEnd])) ++nameEnd;
        std::string_view local = StripXmlNsPrefix(cBody.substr(lt + 1, nameEnd - (lt + 1)));
        size_t gt = FindTagCloseGt(cBody, lt);
        if (gt == std::string_view::npos) break;

        size_t prev = gt;
        while (prev > lt + 1 && (cBody[prev - 1] == ' ' || cBody[prev - 1] == '\t' || cBody[prev - 1] == '\r' || cBody[prev - 1] == '\n')) --prev;
        bool selfClose = (prev > lt + 1 && cBody[prev - 1] == '/');

        if (local == "annotation") {
            if (selfClose) {
                pos = gt + 1;
            } else {
                size_t bodyEnd = 0, afterClose = 0;
                if (!FindXmlElemClose(cBody, "annotation", gt + 1, bodyEnd, afterClose)) break;
                pos = afterClose;
            }
            continue;
        }

        if (local == "p" || local == "h") {
            std::wstring para;
            if (selfClose) {
                pos = gt + 1;
            } else {
                size_t bodyEnd = 0, afterClose = 0;
                if (!FindXmlElemClose(cBody, local, gt + 1, bodyEnd, afterClose)) break;
                AppendOdfParagraphInline(cBody.substr(gt + 1, bodyEnd - (gt + 1)), para);
                pos = afterClose;
            }
            if (hasParagraph) {
                cellText.push_back(L'\n');
            }
            cellText += para;
            hasParagraph = true;
            continue;
        }
        pos = gt + 1;
    }

    if (!cellText.empty()) {
        return cellText;
    }

    // Attribute fallbacks when <text:p> / <text:h> is absent or empty
    std::string_view attrSv;
    if (GetXmlAttr(cTag, "office:string-value", attrSv, true) && !attrSv.empty()) {
        return DecodeSpreadsheetXml(attrSv, false);
    }
    if (GetXmlAttr(cTag, "office:date-value", attrSv, true) && !attrSv.empty()) {
        return DecodeSpreadsheetXml(attrSv, false);
    }
    if (GetXmlAttr(cTag, "office:boolean-value", attrSv, true) && !attrSv.empty()) {
        std::string_view b = TrimAsciiSV(attrSv);
        if (AsciiEqCI(b, "true") || b == "1") return L"TRUE";
        if (AsciiEqCI(b, "false") || b == "0") return L"FALSE";
        return DecodeSpreadsheetXml(b, false);
    }
    if (GetXmlAttr(cTag, "office:value", attrSv, true) && !attrSv.empty()) {
        std::string_view vtSv;
        if (GetXmlAttr(cTag, "office:value-type", vtSv, true) && AsciiEqCI(TrimAsciiSV(vtSv), "percentage")) {
            return FormatExcelNumericCell(attrSv, { ExcelNumFmtKind::Percent, 2 }, false);
        }
        return FormatExcelNumericCell(attrSv, { ExcelNumFmtKind::General, 0 }, false);
    }
    return {};
}

} // namespace DetailExcelOds

namespace DetailWordOdt {

inline bool IsXmlTagDelim(char c) {
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline std::string_view GetXmlAttr(std::string_view tagBody, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tagBody, attrName, v, false) ? v : std::string_view();
}

inline int ParsePositiveIntAttr(std::string_view val, int defVal) {
    if (val.empty()) return defVal;
    int acc = 0;
    for (char c : val) {
        if (c < '0' || c > '9') return defVal;
        acc = acc * 10 + (c - '0');
        if (acc > 10000) return 10000;
    }
    return (acc > 0) ? acc : defVal;
}

inline void EmitDocParagraph(ParsedOfficeDocument& out, DocParagraph&& para) {
    if (para.text.empty()) return;
    CountDocText(out, para.text);
    out.paragraphs.push_back(std::move(para));
}


} // namespace DetailWordOdt

namespace DetailPptxOdp {

struct XmlAttr {
    std::string name;      // full name (lowercase)
    std::string localName; // name after ':' (lowercase)
    std::string value;     // entity-decoded UTF-8
};

struct XmlNode {
    bool isText = false;
    std::string fullTag;   // lowercase e.g. "draw:page"
    std::string localTag;  // lowercase e.g. "page"
    std::string text;      // for isText == true (entity-decoded UTF-8)
    std::vector<XmlAttr> attrs;
    std::vector<XmlNode> children;

    std::string_view Attr(std::string_view exactOrLocal) const {
        for (const auto& a : attrs) {
            if (a.name == exactOrLocal || a.localName == exactOrLocal) return a.value;
        }
        return {};
    }

    const XmlNode* FindFirstLocal(std::string_view loc) const {
        for (const auto& ch : children) {
            if (!ch.isText && ch.localTag == loc) return &ch;
        }
        return nullptr;
    }

};

inline std::string LowerAsciiStr(std::string_view sv) { return ::Explorer2::PathUtils::ToLowerAscii(sv); }

inline std::string LocalXmlName(const std::string& fullLower) {
    size_t col = fullLower.find(':');
    return (col == std::string::npos) ? fullLower : fullLower.substr(col + 1);
}

inline std::wstring TrimWideWs(std::wstring_view sv) { return ::Explorer2::PathUtils::Trim(sv); }

// BOM / UTF-16 / declared encoding handled by the shared XML helper
inline std::string XmlBytesToUtf8(const std::vector<uint8_t>& raw) {
    return ::Explorer2::XmlTagScan::DocumentToUtf8(raw.data(), raw.size());
}

inline std::string DecodeXmlUtf8(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) return std::string(raw);
    return ::Explorer2::XmlTagScan::DecodeText(raw);
}

// Fast iterative XML tree parser (preserves mixed-content text nodes & element hierarchy)
inline bool ParseXmlTreePptx(std::string_view sv, XmlNode& outRoot) {
    outRoot = XmlNode{};
    outRoot.fullTag = "#doc";
    outRoot.localTag = "#doc";

    std::vector<XmlNode*> stack;
    stack.reserve(32);
    stack.push_back(&outRoot);

    size_t pos = 0;
    size_t nodeCount = 0;
    constexpr size_t kMaxNodes = 250000;

    while (pos < sv.size() && nodeCount < kMaxNodes) {
        if (sv[pos] != '<') {
            size_t nextLt = sv.find('<', pos);
            if (nextLt == std::string_view::npos) nextLt = sv.size();
            std::string_view chunk = sv.substr(pos, nextLt - pos);
            pos = nextLt;
            if (stack.size() > 1 && !chunk.empty()) {
                // Keep text node if non-whitespace OR if inside an inline/run/text element (<a:t>, <text:span>, <text:p>, etc.)
                bool anyNonWs = false;
                for (unsigned char c : chunk) {
                    if (c > 0x20u) { anyNonWs = true; break; }
                }
                const std::string& pLoc = stack.back()->localTag;
                if (anyNonWs || pLoc == "t" || pLoc == "p" || pLoc == "h" || pLoc == "span" || pLoc == "a" || pLoc == "v" || pLoc == "text") {
                    XmlNode tn;
                    tn.isText = true;
                    tn.text = DecodeXmlUtf8(chunk);
                    stack.back()->children.push_back(std::move(tn));
                    ++nodeCount;
                }
            }
            continue;
        }

        if (pos + 4 <= sv.size() && sv.substr(pos, 4) == "<!--") {
            size_t endC = sv.find("-->", pos + 4);
            pos = (endC == std::string_view::npos) ? sv.size() : (endC + 3);
            continue;
        }
        if (pos + 9 <= sv.size() && sv.substr(pos, 9) == "<![CDATA[") {
            size_t endCd = sv.find("]]>", pos + 9);
            if (endCd == std::string_view::npos) break;
            if (stack.size() > 1) {
                XmlNode tn;
                tn.isText = true;
                tn.text = std::string(sv.substr(pos + 9, endCd - (pos + 9)));
                stack.back()->children.push_back(std::move(tn));
                ++nodeCount;
            }
            pos = endCd + 3;
            continue;
        }
        if (pos + 2 <= sv.size() && (sv[pos + 1] == '?' || sv[pos + 1] == '!')) {
            size_t gt = sv.find('>', pos + 2);
            pos = (gt == std::string_view::npos) ? sv.size() : (gt + 1);
            continue;
        }

        // Find closing '>' respecting quoted attribute strings
        size_t gt = pos + 1;
        char inQuote = 0;
        while (gt < sv.size()) {
            char ch = sv[gt];
            if (inQuote) {
                if (ch == inQuote) inQuote = 0;
            } else {
                if (ch == '"' || ch == '\'') inQuote = ch;
                else if (ch == '>') break;
            }
            ++gt;
        }
        if (gt >= sv.size()) break;

        std::string_view tagBody = sv.substr(pos + 1, gt - (pos + 1));
        pos = gt + 1;
        while (!tagBody.empty() && static_cast<unsigned char>(tagBody.front()) <= 0x20u) tagBody.remove_prefix(1);
        while (!tagBody.empty() && static_cast<unsigned char>(tagBody.back()) <= 0x20u) tagBody.remove_suffix(1);
        if (tagBody.empty()) continue;

        bool isClosing = (tagBody.front() == '/');
        if (isClosing) {
            tagBody.remove_prefix(1);
            while (!tagBody.empty() && static_cast<unsigned char>(tagBody.front()) <= 0x20u) tagBody.remove_prefix(1);
            size_t ne = 0;
            while (ne < tagBody.size() && !DetailWordOdt::IsXmlTagDelim(tagBody[ne])) ++ne;
            std::string closeFull = LowerAsciiStr(tagBody.substr(0, ne));
            std::string closeLoc  = LocalXmlName(closeFull);
            for (size_t s = stack.size(); s > 1; --s) {
                if (stack[s - 1]->fullTag == closeFull || stack[s - 1]->localTag == closeLoc) {
                    stack.resize(s - 1);
                    break;
                }
            }
            continue;
        }

        bool selfClose = (!tagBody.empty() && tagBody.back() == '/');
        if (selfClose) tagBody.remove_suffix(1);

        size_t ne = 0;
        while (ne < tagBody.size() && !DetailWordOdt::IsXmlTagDelim(tagBody[ne])) ++ne;
        if (ne == 0) continue;

        XmlNode elem;
        elem.fullTag  = LowerAsciiStr(tagBody.substr(0, ne));
        elem.localTag = LocalXmlName(elem.fullTag);

        std::string_view attrSv = tagBody.substr(ne);
        size_t ap = 0;
        while (ap < attrSv.size()) {
            while (ap < attrSv.size() && (static_cast<unsigned char>(attrSv[ap]) <= 0x20u || attrSv[ap] == '/')) ++ap;
            if (ap >= attrSv.size()) break;
            size_t as = ap;
            while (ap < attrSv.size() && attrSv[ap] != '=' && static_cast<unsigned char>(attrSv[ap]) > 0x20u) ++ap;
            std::string_view aName = attrSv.substr(as, ap - as);
            while (ap < attrSv.size() && static_cast<unsigned char>(attrSv[ap]) <= 0x20u) ++ap;
            std::string aVal;
            if (ap < attrSv.size() && attrSv[ap] == '=') {
                ++ap;
                while (ap < attrSv.size() && static_cast<unsigned char>(attrSv[ap]) <= 0x20u) ++ap;
                if (ap < attrSv.size() && (attrSv[ap] == '"' || attrSv[ap] == '\'')) {
                    char q = attrSv[ap++];
                    size_t qe = attrSv.find(q, ap);
                    if (qe != std::string_view::npos) {
                        aVal = DecodeXmlUtf8(attrSv.substr(ap, qe - ap));
                        ap = qe + 1;
                    } else {
                        ap = attrSv.size();
                    }
                }
            }
            if (!aName.empty()) {
                std::string lowA = LowerAsciiStr(aName);
                elem.attrs.push_back({ lowA, LocalXmlName(lowA), std::move(aVal) });
            }
        }

        if (stack.size() >= 64) selfClose = true;
        stack.back()->children.push_back(std::move(elem));
        ++nodeCount;
        if (!selfClose) {
            stack.push_back(&stack.back()->children.back());
        }
    }

    return !outRoot.children.empty();
}

inline void CollectAllTextW(const XmlNode& n, std::wstring& out) {
    if (n.isText) {
        out += PathUtils::Utf8ToWide(n.text);
        return;
    }
    if (n.fullTag == "mc:fallback") return;
    for (const auto& ch : n.children) {
        CollectAllTextW(ch, out);
    }
}

inline std::wstring GetChildTextTrimmed(const XmlNode& parent, std::string_view localTag) {
    for (const auto& ch : parent.children) {
        if (!ch.isText && ch.localTag == localTag) {
            std::wstring w;
            CollectAllTextW(ch, w);
            return TrimWideWs(w);
        }
    }
    return {};
}

// Normalize OOXML archive paths (resolving '.', '..', leading '/', and backslashes)
inline std::string NormalizeZipPath(std::string_view baseDir, std::string_view target) {
    std::string combined;
    if (!target.empty() && (target.front() == '/' || target.front() == '\\')) {
        combined = std::string(target.substr(1));
    } else if (baseDir.empty()) {
        combined = std::string(target);
    } else {
        combined = std::string(baseDir) + "/" + std::string(target);
    }
    for (char& c : combined) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    std::vector<std::string_view> segs;
    size_t p = 0;
    while (p < combined.size()) {
        size_t sl = combined.find('/', p);
        if (sl == std::string::npos) sl = combined.size();
        std::string_view part(combined.data() + p, sl - p);
        p = sl + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!segs.empty()) segs.pop_back();
        } else {
            segs.push_back(part);
        }
    }
    std::string out;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i > 0) out.push_back('/');
        out.append(segs[i]);
    }
    return out;
}

inline std::string RelsPathForPart(const std::string& partPath) {
    size_t sl = partPath.rfind('/');
    if (sl == std::string::npos) return "_rels/" + partPath + ".rels";
    return partPath.substr(0, sl) + "/_rels/" + partPath.substr(sl + 1) + ".rels";
}

inline std::string DirOfPart(const std::string& partPath) {
    size_t sl = partPath.rfind('/');
    return (sl == std::string::npos) ? std::string() : partPath.substr(0, sl);
}

struct OoxmlRel {
    std::string id;
    std::string type;
    std::string target;
    bool external = false;
};

inline void ParseOoxmlRelsXml(std::string_view xmlUtf8, std::vector<OoxmlRel>& outRels) {
    outRels.clear();
    XmlNode doc;
    if (!ParseXmlTreePptx(xmlUtf8, doc)) return;
    const XmlNode* root = doc.FindFirstLocal("relationships");
    if (!root) return;
    for (const auto& ch : root->children) {
        if (ch.isText || ch.localTag != "relationship") continue;
        OoxmlRel r;
        r.id = std::string(ch.Attr("id"));
        r.type = LowerAsciiStr(ch.Attr("type"));
        r.target = std::string(ch.Attr("target"));
        r.external = (LowerAsciiStr(ch.Attr("targetmode")) == "external");
        if (!r.id.empty() && !r.target.empty()) {
            outRels.push_back(std::move(r));
        }
    }
}


} // namespace DetailPptxOdp

#ifndef CERT_SHA256_HASH_PROP_ID
#define CERT_SHA256_HASH_PROP_ID 107
#endif
#endif // E2_DOC_FORMAT_CORE_ONLY

namespace Formats {

#ifndef E2_DOC_FORMAT_CORE_ONLY
namespace LegacyOfficeDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;

inline bool ReadStreamByName(const Ole2::CompoundFile& cf, std::wstring_view streamName, std::vector<uint8_t>& outBytes) {
    const Ole2::DirEntry* entry = Ole2::FindStreamCaseInsensitive(cf, streamName);
    if (!entry) return false;
    return Ole2::ExtractStream(cf, *entry, outBytes);
}

// Windows-1252 (CP1252) byte -> UTF-16 code unit, including 0x80..0x9F Word smart quotes, dashes, bullets
inline wchar_t Cp1252ToWide(uint8_t b) {
    static const uint16_t kWin1252_80_9F[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    if (b >= 0x80u && b <= 0x9Fu) {
        return static_cast<wchar_t>(kWin1252_80_9F[b - 0x80u]);
    }
    return static_cast<wchar_t>(b);
}

inline std::wstring TrimWideParagraph(std::wstring_view sv) { return ::Explorer2::PathUtils::Trim(sv); }

inline void PushOfficeParagraph(ParsedOfficeDocument& out, std::wstring text, int headingLevel, bool isBullet, bool isTableRow) {
    std::wstring clean = TrimWideParagraph(text);
    if (clean.empty()) return;
    CountDocText(out, clean);
    out.paragraphs.push_back({ std::move(clean), headingLevel, isBullet, isTableRow });
}


} // namespace LegacyOfficeDetail

namespace DetailAudio {

inline uint32_t Le32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline uint16_t Le16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t Be32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32BE(p); }
inline uint16_t Be16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16BE(p); }

// IEEE 754 80-bit extended (AIFF sample rate)
inline double Extended80(const uint8_t* p) {
    int exp = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) mant = (mant << 8) | p[2 + i];
    if (exp == 0 && mant == 0) return 0.0;
    double v = std::ldexp(static_cast<double>(mant), exp - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}

inline std::wstring Duration(double seconds) {
    if (!(seconds >= 0.0) || !std::isfinite(seconds)) return L"";
    long long ms = static_cast<long long>(seconds * 1000.0 + 0.5);
    wchar_t buf[64];
    if (ms >= 3600000) swprintf_s(buf, L"%lld:%02lld:%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    else swprintf_s(buf, L"%lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return buf;
}

inline std::wstring FourCC(const uint8_t* p) {
    std::wstring s;
    for (int i = 0; i < 4; ++i) s.push_back((p[i] >= 32 && p[i] < 127) ? static_cast<wchar_t>(p[i]) : L'?');
    return s;
}

inline std::wstring TextField(const uint8_t* p, size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
    return PathUtils::Utf8ToWide(s);
}

} // namespace DetailAudio
#endif // E2_DOC_FORMAT_CORE_ONLY

namespace DetailBinarySysDoc {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;

inline bool ReadSourceExact(SourceBytes& src, uint64_t off, uint8_t* dst, size_t len) {
    std::vector<uint8_t> tmp;
    const uint8_t* p = src.Read(off, len, tmp);
    if (!p) return false;
    if (len > 0) std::memcpy(dst, p, len);
    return true;
}

inline bool ReadSourceVec(SourceBytes& src, uint64_t off, size_t len, std::vector<uint8_t>& outVec) {
    outVec.clear();
    if (len == 0) return true;
    std::vector<uint8_t> tmp;
    const uint8_t* p = src.Read(off, len, tmp);
    if (!p) return false;
    if (!tmp.empty() && p == tmp.data()) outVec = std::move(tmp);
    else outVec.assign(p, p + len);
    return true;
}

inline std::wstring Hex32(uint32_t v) {
    wchar_t buf[32] = {};
    swprintf_s(buf, L"0x%08X", v);
    return buf;
}
inline std::wstring Hex64(uint64_t v) {
    wchar_t buf[32] = {};
    swprintf_s(buf, L"0x%016llX", static_cast<unsigned long long>(v));
    return buf;
}

inline std::wstring HexBytes(const uint8_t* p, size_t len, wchar_t sep = 0) { return PathUtils::HexBytes(p, len, sep); }

inline std::wstring FormatFileTimeUtc(uint64_t ftVal) {
    if (ftVal == 0 || ftVal >= 0x7FFFFFFFFFFFFFFFULL) return L"(not set)";
    FILETIME ft = {};
    ft.dwLowDateTime  = static_cast<DWORD>(ftVal & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ftVal >> 32);
    SYSTEMTIME st = {};
    if (!FileTimeToSystemTime(&ft, &st) || st.wYear < 1970 || st.wYear > 2200) return L"(invalid)";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u UTC",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

inline std::wstring FormatUnixTimeUtc(uint32_t unixSec) {
    if (unixSec == 0) return L"0 (not set)";
    const uint64_t ftVal = (static_cast<uint64_t>(unixSec) + 11644473600ULL) * 10000000ULL;
    return FormatFileTimeUtc(ftVal) + L" (" + std::to_wstring(unixSec) + L")";
}

inline std::wstring FormatGuidLE(const uint8_t* g) {
    wchar_t buf[64] = {};
    swprintf_s(buf, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        RdU32LE(g), RdU16LE(g + 4), RdU16LE(g + 6),
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return buf;
}

inline std::wstring ReadUtf16LEBounded(const uint8_t* p, size_t maxBytes, bool stopAtNul = true) {
    std::wstring out;
    const size_t nChars = maxBytes / 2;
    out.reserve(std::min<size_t>(nChars, 512));
    for (size_t i = 0; i < nChars; ++i) {
        const wchar_t wc = static_cast<wchar_t>(RdU16LE(p + i * 2));
        if (wc == 0 && stopAtNul) break;
        if (wc >= 0x20 || wc == L'\t') out.push_back(wc);
    }
    return out;
}

inline std::string ReadCStrBounded(const std::vector<uint8_t>& raw, size_t off, size_t maxLen = 512) {
    if (off >= raw.size()) return {};
    const size_t lim = std::min<size_t>(raw.size() - off, maxLen);
    size_t n = 0;
    while (n < lim && raw[off + n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(raw.data() + off), n);
}

} // namespace DetailBinarySysDoc

#ifndef E2_DOC_FORMAT_CORE_ONLY
namespace DetailX509 {

inline bool DecodeBase64Clean(std::string_view b64, std::vector<uint8_t>& outBytes) {
    ::Explorer2::Base64::Decode(b64, outBytes);
    return !outBytes.empty();
}

inline std::wstring MapKnownOid(const std::string& oid) {
    if (oid == "2.5.4.3")  return L"commonName (CN)";
    if (oid == "2.5.4.6")  return L"countryName (C)";
    if (oid == "2.5.4.7")  return L"localityName (L)";
    if (oid == "2.5.4.8")  return L"stateOrProvinceName (ST)";
    if (oid == "2.5.4.10") return L"organizationName (O)";
    if (oid == "2.5.4.11") return L"organizationalUnitName (OU)";
    if (oid == "1.2.840.113549.1.9.1")  return L"emailAddress";
    if (oid == "1.2.840.113549.1.1.1")  return L"rsaEncryption";
    if (oid == "1.2.840.113549.1.1.5")  return L"sha1WithRSAEncryption";
    if (oid == "1.2.840.113549.1.1.11") return L"sha256WithRSAEncryption";
    if (oid == "1.2.840.113549.1.1.12") return L"sha384WithRSAEncryption";
    if (oid == "1.2.840.113549.1.1.13") return L"sha512WithRSAEncryption";
    if (oid == "1.2.840.10045.2.1")     return L"ecPublicKey";
    if (oid == "1.2.840.10045.3.1.7")   return L"prime256v1 (secp256r1)";
    if (oid == "1.3.132.0.34")          return L"secp384r1";
    if (oid == "1.2.840.10045.4.3.2")   return L"ecdsa-with-SHA256";
    if (oid == "1.2.840.10045.4.3.3")   return L"ecdsa-with-SHA384";
    if (oid == "1.3.101.112")           return L"Ed25519";
    if (oid == "1.2.840.113549.1.7.1")  return L"pkcs7-data";
    if (oid == "1.2.840.113549.1.7.2")  return L"pkcs7-signedData";
    if (oid == "1.2.840.113549.1.7.6")  return L"pkcs7-encryptedData";
    if (oid == "1.2.840.113549.1.12.10.1.1") return L"keyBag";
    if (oid == "1.2.840.113549.1.12.10.1.2") return L"pkcs8ShroudedKeyBag";
    if (oid == "1.2.840.113549.1.12.10.1.3") return L"certBag";
    if (oid == "1.2.840.113549.1.12.10.1.4") return L"crlBag";
    if (oid == "1.2.840.113549.1.12.10.1.5") return L"secretBag";
    if (oid == "1.2.840.113549.1.12.10.1.6") return L"safeContentsBag";
    if (oid == "1.2.840.113549.1.12.1.1") return L"pbeWithSHAAnd128BitRC4";
    if (oid == "1.2.840.113549.1.12.1.3") return L"pbeWithSHAAnd3-KeyTripleDES-CBC";
    if (oid == "1.2.840.113549.1.12.1.6") return L"pbeWithSHAAnd40BitRC2-CBC";
    if (oid == "1.2.840.113549.1.5.12")  return L"PBKDF2";
    if (oid == "1.2.840.113549.1.5.13")  return L"PBES2";
    if (oid == "1.2.840.113549.2.7")     return L"hmacWithSHA1";
    if (oid == "1.2.840.113549.2.9")     return L"hmacWithSHA256";
    if (oid == "1.2.840.113549.2.10")    return L"hmacWithSHA384";
    if (oid == "1.2.840.113549.2.11")    return L"hmacWithSHA512";
    if (oid == "1.3.14.3.2.26")          return L"SHA-1";
    if (oid == "2.16.840.1.101.3.4.2.1") return L"SHA-256";
    if (oid == "2.16.840.1.101.3.4.2.2") return L"SHA-384";
    if (oid == "2.16.840.1.101.3.4.2.3") return L"SHA-512";
    if (oid == "2.16.840.1.101.3.4.1.2") return L"AES-128-CBC";
    if (oid == "2.16.840.1.101.3.4.1.42") return L"AES-256-CBC";
    if (oid == "1.2.840.113549.1.9.14")  return L"extensionRequest";
    if (oid == "1.2.840.113549.1.9.20")  return L"friendlyName";
    if (oid == "1.2.840.113549.1.9.21")  return L"localKeyId";
    if (oid == "1.2.840.113549.1.9.22.1") return L"x509Certificate";
    if (oid == "1.3.132.0.35")           return L"secp521r1";
    if (oid == "1.3.132.0.10")           return L"secp256k1";
    if (oid == "1.3.101.110")            return L"X25519";
    if (oid == "1.3.101.113")            return L"Ed448";
    if (oid == "2.5.29.17") return L"subjectAltName";
    if (oid == "2.5.29.19") return L"basicConstraints";
    if (oid == "2.5.29.15") return L"keyUsage";
    if (oid == "2.5.29.37") return L"extKeyUsage";
    return PathUtils::Utf8ToWide(oid);
}

inline std::string DecodeDerOid(const uint8_t* p, size_t len) {
    if (len == 0) return {};
    std::string out = std::to_string(p[0] / 40) + "." + std::to_string(p[0] % 40);
    uint64_t arc = 0;
    for (size_t i = 1; i < len; ++i) {
        arc = (arc << 7) | (p[i] & 0x7Fu);
        if ((p[i] & 0x80u) == 0) {
            out += "." + std::to_string(arc);
            arc = 0;
        }
    }
    return out;
}

inline bool ReadDerTlv(const uint8_t* p, size_t maxLen, uint8_t& tag, size_t& hdrLen, size_t& valLen) {
    if (maxLen < 2) return false;
    tag = p[0];
    if ((p[1] & 0x80u) == 0) {
        valLen = p[1];
        hdrLen = 2;
    } else {
        const size_t nBytes = p[1] & 0x7Fu;
        if (nBytes == 0 || nBytes > 4 || 2 + nBytes > maxLen) return false;
        valLen = 0;
        for (size_t i = 0; i < nBytes; ++i) valLen = (valLen << 8) | p[2 + i];
        hdrLen = 2 + nBytes;
    }
    return hdrLen + valLen <= maxLen;
}

inline void WalkAsn1Der(const uint8_t* p, size_t len, int depth, std::wstring& lastOidLabel, std::vector<std::vector<std::wstring>>& rows) {
    if (depth > 16 || rows.size() > 500) return;
    size_t pos = 0;
    while (pos < len && rows.size() <= 500) {
        uint8_t tag = 0;
        size_t hdrLen = 0, valLen = 0;
        if (!ReadDerTlv(p + pos, len - pos, tag, hdrLen, valLen)) break;
        const uint8_t* val = p + pos + hdrLen;

        if ((tag & 0x20u) != 0) { // Constructed (SEQUENCE / SET / explicit context tag)
            WalkAsn1Der(val, valLen, depth + 1, lastOidLabel, rows);
        } else if (tag == 0x06) { // OBJECT IDENTIFIER
            std::string oid = DecodeDerOid(val, valLen);
            std::wstring mapped = MapKnownOid(oid);
            lastOidLabel = mapped;
            rows.push_back({ L"ASN.1 OID", mapped + L" (" + PathUtils::Utf8ToWide(oid) + L")" });
        } else if (tag == 0x0C || tag == 0x13 || tag == 0x16 || tag == 0x14 || tag == 0x17 || tag == 0x18 ||
                   tag == 0x81 || tag == 0x82 || tag == 0x86) {
            bool printableOk = true;
            if ((tag & 0x80u) != 0) {
                if (valLen == 0 || valLen > 512) printableOk = false;
                for (size_t k = 0; k < valLen && printableOk; ++k) {
                    if (val[k] < 0x20u || val[k] > 0x7Eu) printableOk = false;
                }
            }
            if (printableOk) {
                std::wstring s = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), valLen));
                std::wstring key = lastOidLabel.empty() ? L"ASN.1 String" : lastOidLabel;
                if ((tag & 0x80u) == 0) lastOidLabel.clear();
                rows.push_back({ key, s });
            }
        } else if (tag == 0x1E && (valLen % 2 == 0)) { // BMPString (UTF-16BE)
            std::wstring s;
            for (size_t i = 0; i + 2 <= valLen; i += 2) {
                wchar_t wc = static_cast<wchar_t>(DetailBinarySysDoc::RdU16BE(val + i));
                if (wc != 0) s.push_back(wc);
            }
            rows.push_back({ lastOidLabel.empty() ? L"ASN.1 BMPString" : lastOidLabel, s });
            lastOidLabel.clear();
        } else if (tag == 0x02 && valLen > 0) { // INTEGER
            if (valLen <= 4) {
                uint32_t iv = 0;
                for (size_t i = 0; i < valLen; ++i) iv = (iv << 8) | val[i];
                rows.push_back({ L"ASN.1 Integer", std::to_wstring(iv) });
            } else {
                rows.push_back({ L"ASN.1 BigInteger (" + std::to_wstring(valLen * 8) + L"-bit)",
                    DetailBinarySysDoc::HexBytes(val, std::min<size_t>(valLen, 24), L':') + (valLen > 24 ? L"..." : L"") });
            }
        } else if (tag == 0x03 && valLen > 1) { // BIT STRING
            rows.push_back({ L"ASN.1 BitString", std::to_wstring((valLen - 1) * 8) + L" bits (" + DetailBinarySysDoc::HexBytes(val + 1, std::min<size_t>(valLen - 1, 16)) + L"...)" });
        } else if (tag == 0x04 && valLen > 0) { // OCTET STRING
            // Check if encapsulated ASN.1 SEQUENCE inside OCTET STRING
            uint8_t innerTag = 0; size_t ih = 0, iv = 0;
            if (val[0] == 0x30 && ReadDerTlv(val, valLen, innerTag, ih, iv) && ih + iv == valLen) {
                WalkAsn1Der(val, valLen, depth + 1, lastOidLabel, rows);
            } else {
                rows.push_back({ L"ASN.1 OctetString (" + std::to_wstring(valLen) + L" B)",
                    DetailBinarySysDoc::HexBytes(val, std::min<size_t>(valLen, 20), L':') + (valLen > 20 ? L"..." : L"") });
            }
        }
        pos += hdrLen + valLen;
    }
}

#ifdef _WIN32   // CryptoAPI: the Linux build of the readers shows certificates as their ASN.1 structure instead
inline void AppendCertContextRows(PCCERT_CONTEXT pCert, const std::wstring& prefix, std::vector<std::vector<std::wstring>>& rows) {
    if (!pCert || !pCert->pCertInfo) return;

    wchar_t subjBuf[512] = {}, issBuf[512] = {};
    CertNameToStrW(X509_ASN_ENCODING, &pCert->pCertInfo->Subject, CERT_X500_NAME_STR, subjBuf, 512);
    CertNameToStrW(X509_ASN_ENCODING, &pCert->pCertInfo->Issuer,  CERT_X500_NAME_STR, issBuf,  512);

    rows.push_back({ prefix + L"X.509 Version", L"v" + std::to_wstring(pCert->pCertInfo->dwVersion + 1) });
    rows.push_back({ prefix + L"Subject DN", subjBuf });
    rows.push_back({ prefix + L"Issuer DN", issBuf });

    // SerialNumber is stored little-endian in CERT_INFO
    std::vector<uint8_t> serRev(pCert->pCertInfo->SerialNumber.cbData);
    for (size_t i = 0; i < serRev.size(); ++i) {
        serRev[i] = pCert->pCertInfo->SerialNumber.pbData[serRev.size() - 1 - i];
    }
    rows.push_back({ prefix + L"Serial Number", DetailBinarySysDoc::HexBytes(serRev.data(), serRev.size(), L':') });

    if (pCert->pCertInfo->SignatureAlgorithm.pszObjId) {
        std::string sigOid = pCert->pCertInfo->SignatureAlgorithm.pszObjId;
        rows.push_back({ prefix + L"Signature Algorithm", MapKnownOid(sigOid) + L" (" + PathUtils::Utf8ToWide(sigOid) + L")" });
    }
    if (pCert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId) {
        std::string pkOid = pCert->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId;
        const DWORD keyBits = CertGetPublicKeyLength(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &pCert->pCertInfo->SubjectPublicKeyInfo);
        rows.push_back({ prefix + L"Public Key", MapKnownOid(pkOid) + L" (" + std::to_wstring(keyBits) + L"-bit)" });
    }

    uint64_t ftBefore = (static_cast<uint64_t>(pCert->pCertInfo->NotBefore.dwHighDateTime) << 32) | pCert->pCertInfo->NotBefore.dwLowDateTime;
    uint64_t ftAfter  = (static_cast<uint64_t>(pCert->pCertInfo->NotAfter.dwHighDateTime)  << 32) | pCert->pCertInfo->NotAfter.dwLowDateTime;
    rows.push_back({ prefix + L"Valid Not Before", DetailBinarySysDoc::FormatFileTimeUtc(ftBefore) });
    rows.push_back({ prefix + L"Valid Not After",  DetailBinarySysDoc::FormatFileTimeUtc(ftAfter) });

    // Subject Alternative Names (SAN)
    PCERT_EXTENSION pSanExt = CertFindExtension(szOID_SUBJECT_ALT_NAME2, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
    if (pSanExt) {
        CERT_ALT_NAME_INFO* pAltInfo = nullptr;
        DWORD cbAltInfo = 0;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2,
                pSanExt->Value.pbData, pSanExt->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &pAltInfo, &cbAltInfo) && pAltInfo)
        {
            std::wstring sanStr;
            for (DWORD i = 0; i < pAltInfo->cAltEntry; ++i) {
                const auto& e = pAltInfo->rgAltEntry[i];
                std::wstring item;
                if (e.dwAltNameChoice == CERT_ALT_NAME_DNS_NAME && e.pwszDNSName) item = std::wstring(L"DNS:") + e.pwszDNSName;
                else if (e.dwAltNameChoice == CERT_ALT_NAME_RFC822_NAME && e.pwszRfc822Name) item = std::wstring(L"Email:") + e.pwszRfc822Name;
                else if (e.dwAltNameChoice == CERT_ALT_NAME_URL && e.pwszURL) item = std::wstring(L"URI:") + e.pwszURL;
                else if (e.dwAltNameChoice == CERT_ALT_NAME_IP_ADDRESS && e.IPAddress.cbData == 4) {
                    wchar_t ipb[32] = {};
                    swprintf_s(ipb, L"IP:%u.%u.%u.%u", e.IPAddress.pbData[0], e.IPAddress.pbData[1], e.IPAddress.pbData[2], e.IPAddress.pbData[3]);
                    item = ipb;
                }
                if (!item.empty()) {
                    if (!sanStr.empty()) sanStr += L", ";
                    sanStr += item;
                }
            }
            if (!sanStr.empty()) rows.push_back({ prefix + L"Subject Alt Names (SAN)", sanStr });
            LocalFree(pAltInfo);
        }
    }

    uint8_t sha256[32] = {};
    DWORD cbSha256 = sizeof(sha256);
    if (CertGetCertificateContextProperty(pCert, CERT_SHA256_HASH_PROP_ID, sha256, &cbSha256) && cbSha256 == 32) {
        rows.push_back({ prefix + L"SHA-256 Thumbprint", DetailBinarySysDoc::HexBytes(sha256, 32, L':') });
    } else {
        uint8_t sha1[20] = {};
        DWORD cbSha1 = sizeof(sha1);
        if (CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID, sha1, &cbSha1) && cbSha1 == 20) {
            rows.push_back({ prefix + L"SHA-1 Thumbprint", DetailBinarySysDoc::HexBytes(sha1, 20, L':') });
        }
    }
}
#endif // _WIN32


struct DerBlock { std::wstring label; std::vector<uint8_t> der; };

inline bool ExtractPemOrDerBlocks(const std::vector<uint8_t>& raw, std::vector<DerBlock>& blocks, bool& isPem) {
    blocks.clear();
    isPem = false;
    if (raw.size() < 4) return false;
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t searchPos = 0;
    while (blocks.size() < 32) {
        const size_t bPos = sv.find("-----BEGIN ", searchPos);
        if (bPos == std::string_view::npos) break;
        const size_t bEnd = sv.find("-----", bPos + 11);
        if (bEnd == std::string_view::npos || bEnd - (bPos + 11) > 64) break;
        const std::string_view labelSv = sv.substr(bPos + 11, bEnd - (bPos + 11));
        const std::string endMarker = "-----END " + std::string(labelSv) + "-----";
        const size_t ePos = sv.find(endMarker, bEnd + 5);
        if (ePos == std::string_view::npos) break;

        std::string_view payloadSv = sv.substr(bEnd + 5, ePos - (bEnd + 5));
        // Strip optional RFC 1421 Proc-Type / DEK-Info armor headers before blank line
        if (payloadSv.find(':') != std::string_view::npos) {
            size_t blank = payloadSv.find("\r\n\r\n");
            if (blank != std::string_view::npos) payloadSv = payloadSv.substr(blank + 4);
            else {
                blank = payloadSv.find("\n\n");
                if (blank != std::string_view::npos) payloadSv = payloadSv.substr(blank + 2);
            }
        }

        std::vector<uint8_t> decoded;
#ifdef _WIN32
        DWORD cbDecoded = 0;
        if (CryptStringToBinaryA(payloadSv.data(), static_cast<DWORD>(payloadSv.size()), CRYPT_STRING_BASE64, nullptr, &cbDecoded, nullptr, nullptr) && cbDecoded > 0) {
            decoded.resize(cbDecoded);
            if (!CryptStringToBinaryA(payloadSv.data(), static_cast<DWORD>(payloadSv.size()), CRYPT_STRING_BASE64, decoded.data(), &cbDecoded, nullptr, nullptr)) {
                decoded.clear();
            } else {
                decoded.resize(cbDecoded);
            }
        }
#endif
        if (decoded.empty()) {
            DetailX509::DecodeBase64Clean(payloadSv, decoded);
        }
        if (!decoded.empty()) {
            isPem = true;
            blocks.push_back({ PathUtils::Utf8ToWide(labelSv), std::move(decoded) });
        }
        searchPos = ePos + endMarker.size();
    }

    if (!isPem) {
        uint8_t outerTag = 0;
        size_t hdrLen = 0, valLen = 0;
        if (raw[0] != 0x30 || !DetailX509::ReadDerTlv(raw.data(), raw.size(), outerTag, hdrLen, valLen) || valLen == 0) {
            return false;
        }
        blocks.push_back({ L"DER ASN.1 Binary", raw });
    }
    return !blocks.empty();
}

inline std::vector<std::vector<std::wstring>> InitPkiHeaderRows(bool isPem, size_t blockCount, const std::wstring& extLower) {
    return {
        { L"Certificate / PKI Field", L"Decoded Value" },
        { L"Container Encoding", isPem ? (L"PEM Base64 Armored (" + std::to_wstring(blockCount) + L" block(s))") : L"ASN.1 DER Binary" },
        { L"File Extension", extLower }
    };
}

inline bool FinalizePkiDoc(const std::wstring& title, std::vector<std::vector<std::wstring>>&& rows, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = title;
    SpreadsheetSheet sh;
    sh.name = L"X509_PKI_Info";
    sh.maxCols = 2;
    sh.rows = std::move(rows);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

inline std::wstring DecodeDerAnyString(uint8_t tag, const uint8_t* val, size_t valLen) {
    if (tag == 0x1E && (valLen % 2u) == 0u) { // BMPString (UTF-16BE)
        std::wstring s;
        s.reserve(valLen / 2u);
        for (size_t i = 0; i + 2 <= valLen; i += 2) {
            uint16_t cu = DetailBinarySysDoc::RdU16BE(val + i);
            if (cu != 0) s.push_back(static_cast<wchar_t>(cu));
        }
        return s;
    }
    if (tag == 0x1C && (valLen % 4u) == 0u) { // UniversalString (UTF-32BE)
        std::wstring s;
        for (size_t i = 0; i + 4 <= valLen; i += 4) {
            uint32_t cp = DetailBinarySysDoc::RdU32BE(val + i);
            if (cp == 0) continue;
            if (cp <= 0xFFFFu) {
                s.push_back(static_cast<wchar_t>(cp));
            } else if (cp <= 0x10FFFFu) {
                if constexpr (sizeof(wchar_t) == 2) {
                    uint32_t u = cp - 0x10000u;
                    s.push_back(static_cast<wchar_t>(0xD800u | (u >> 10)));
                    s.push_back(static_cast<wchar_t>(0xDC00u | (u & 0x3FFu)));
                } else {
                    s.push_back(static_cast<wchar_t>(cp));
                }
            }
        }
        return s;
    }
    return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(val), valLen));
}

inline std::wstring ParseDerTimeUtc(uint8_t tag, const uint8_t* val, size_t valLen) {
    std::string_view sv(reinterpret_cast<const char*>(val), valLen);
    auto digits = [&](size_t off, size_t n) -> bool {
        if (off + n > sv.size()) return false;
        for (size_t i = 0; i < n; ++i) {
            if (sv[off + i] < '0' || sv[off + i] > '9') return false;
        }
        return true;
    };
    if (tag == 0x17 && digits(0, 10)) { // UTCTime YYMMDDHHMM[SS]Z
        int yy = (sv[0] - '0') * 10 + (sv[1] - '0');
        int year = (yy >= 50) ? (1900 + yy) : (2000 + yy);
        std::string sec = digits(10, 2) ? std::string(sv.substr(10, 2)) : "00";
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%04d-%c%c-%c%c %c%c:%c%c:%c%c UTC",
            year, sv[2], sv[3], sv[4], sv[5], sv[6], sv[7], sv[8], sv[9], sec[0], sec[1]);
        return buf;
    }
    if (tag == 0x18 && digits(0, 12)) { // GeneralizedTime YYYYMMDDHHMM[SS]Z
        std::string sec = digits(12, 2) ? std::string(sv.substr(12, 2)) : "00";
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c UTC",
            sv[0], sv[1], sv[2], sv[3], sv[4], sv[5], sv[6], sv[7],
            sv[8], sv[9], sv[10], sv[11], sec[0], sec[1]);
        return buf;
    }
    return PathUtils::Utf8ToWide(sv);
}

inline std::wstring ParseDerDistinguishedName(const uint8_t* p, size_t len) {
    std::wstring dn;
    size_t pos = 0;
    while (pos < len) {
        uint8_t setTag = 0; size_t sh = 0, sv = 0;
        if (!ReadDerTlv(p + pos, len - pos, setTag, sh, sv)) break;
        const uint8_t* setVal = p + pos + sh;
        pos += sh + sv;
        if (setTag != 0x31) continue;
        size_t sp = 0;
        while (sp < sv) {
            uint8_t seqTag = 0; size_t qh = 0, qv = 0;
            if (!ReadDerTlv(setVal + sp, sv - sp, seqTag, qh, qv)) break;
            const uint8_t* seqVal = setVal + sp + qh;
            sp += qh + qv;
            if (seqTag != 0x30) continue;
            uint8_t oidTag = 0; size_t oh = 0, ov = 0;
            if (!ReadDerTlv(seqVal, qv, oidTag, oh, ov) || oidTag != 0x06 || oh + ov >= qv) continue;
            std::string oid = DecodeDerOid(seqVal + oh, ov);
            uint8_t valTag = 0; size_t vh = 0, vv = 0;
            if (!ReadDerTlv(seqVal + oh + ov, qv - (oh + ov), valTag, vh, vv)) continue;
            std::wstring attrVal = DecodeDerAnyString(valTag, seqVal + oh + ov + vh, vv);
            std::wstring shortKey;
            if (oid == "2.5.4.3") shortKey = L"CN";
            else if (oid == "2.5.4.6") shortKey = L"C";
            else if (oid == "2.5.4.7") shortKey = L"L";
            else if (oid == "2.5.4.8") shortKey = L"ST";
            else if (oid == "2.5.4.10") shortKey = L"O";
            else if (oid == "2.5.4.11") shortKey = L"OU";
            else if (oid == "2.5.4.5") shortKey = L"serialNumber";
            else if (oid == "1.2.840.113549.1.9.1") shortKey = L"emailAddress";
            else if (oid == "0.9.2342.19200300.100.1.25") shortKey = L"DC";
            else shortKey = PathUtils::Utf8ToWide(oid);
            if (!dn.empty()) dn += L", ";
            dn += shortKey + L"=" + attrVal;
        }
    }
    return dn;
}

inline bool ParseDerAlgorithmIdentifier(const uint8_t* p, size_t len, std::string& outOid, std::string& outParamOid, const uint8_t*& outParamsRaw, size_t& outParamsRawLen) {
    outOid.clear();
    outParamOid.clear();
    outParamsRaw = nullptr;
    outParamsRawLen = 0;
    uint8_t oidTag = 0; size_t oh = 0, ov = 0;
    if (!ReadDerTlv(p, len, oidTag, oh, ov) || oidTag != 0x06) return false;
    outOid = DecodeDerOid(p + oh, ov);
    if (oh + ov < len) {
        outParamsRaw = p + oh + ov;
        outParamsRawLen = len - (oh + ov);
        uint8_t pTag = 0; size_t ph = 0, pv = 0;
        if (ReadDerTlv(outParamsRaw, outParamsRawLen, pTag, ph, pv) && pTag == 0x06) {
            outParamOid = DecodeDerOid(outParamsRaw + ph, pv);
        }
    }
    return true;
}

inline bool ParseDerSubjectPublicKeyInfo(const uint8_t* spkiVal, size_t spkiLen, std::wstring& outAlgSummary, uint32_t& outKeyBits) {
    outAlgSummary.clear();
    outKeyBits = 0;
    uint8_t algTag = 0; size_t ah = 0, av = 0;
    if (!ReadDerTlv(spkiVal, spkiLen, algTag, ah, av) || algTag != 0x30 || ah + av >= spkiLen) return false;
    std::string algOid, paramOid;
    const uint8_t* pRaw = nullptr; size_t pRawLen = 0;
    if (!ParseDerAlgorithmIdentifier(spkiVal + ah, av, algOid, paramOid, pRaw, pRawLen)) return false;

    uint8_t bsTag = 0; size_t bh = 0, bv = 0;
    if (!ReadDerTlv(spkiVal + ah + av, spkiLen - (ah + av), bsTag, bh, bv) || bsTag != 0x03 || bv < 1) return false;
    const uint8_t unusedBits = spkiVal[ah + av + bh];
    const uint8_t* keyBytes = spkiVal + ah + av + bh + 1;
    const size_t keyLen = bv - 1;

    if (algOid == "1.2.840.113549.1.1.1") { // RSA: RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
        uint8_t rTag = 0; size_t rh = 0, rv = 0;
        if (ReadDerTlv(keyBytes, keyLen, rTag, rh, rv) && rTag == 0x30) {
            uint8_t mTag = 0; size_t mh = 0, mv = 0;
            if (ReadDerTlv(keyBytes + rh, rv, mTag, mh, mv) && mTag == 0x02 && mv > 0) {
                const uint8_t* modPtr = keyBytes + rh + mh;
                size_t modLen = mv;
                while (modLen > 1 && modPtr[0] == 0x00u) { ++modPtr; --modLen; }
                uint32_t topByte = modPtr[0];
                uint32_t leadBits = 0;
                while (topByte > 0) { ++leadBits; topByte >>= 1; }
                outKeyBits = static_cast<uint32_t>((modLen - 1) * 8u + leadBits);
            }
        }
        if (outKeyBits == 0) outKeyBits = static_cast<uint32_t>(keyLen * 8u);
        outAlgSummary = L"RSA (" + std::to_wstring(outKeyBits) + L"-bit)";
    } else if (algOid == "1.2.840.10045.2.1") { // ECDSA / ECDH
        if (paramOid == "1.2.840.10045.3.1.7" || paramOid == "1.3.132.0.10") outKeyBits = 256;
        else if (paramOid == "1.3.132.0.34") outKeyBits = 384;
        else if (paramOid == "1.3.132.0.35") outKeyBits = 521;
        else if (keyLen >= 1 && keyBytes[0] == 0x04u) outKeyBits = static_cast<uint32_t>(((keyLen - 1) / 2u) * 8u);
        std::wstring curve = paramOid.empty() ? L"EC" : MapKnownOid(paramOid);
        outAlgSummary = L"ECDSA / " + curve + (outKeyBits ? (L" (" + std::to_wstring(outKeyBits) + L"-bit)") : L"");
    } else if (algOid == "1.3.101.112" || algOid == "1.3.101.110") {
        outKeyBits = 256;
        outAlgSummary = MapKnownOid(algOid) + L" (256-bit)";
    } else if (algOid == "1.3.101.113") {
        outKeyBits = 448;
        outAlgSummary = L"Ed448 (448-bit)";
    } else {
        outKeyBits = static_cast<uint32_t>(keyLen * 8u - std::min<uint32_t>(unusedBits, 7u));
        outAlgSummary = MapKnownOid(algOid) + L" (" + std::to_wstring(outKeyBits) + L"-bit)";
    }
    return true;
}

inline void ParseDerX509Extensions(const uint8_t* extSeqVal, size_t extSeqLen, std::wstring& outSan, std::wstring& outKeyUsage, std::wstring& outBasicConstraints) {
    size_t pos = 0;
    while (pos < extSeqLen) {
        uint8_t eTag = 0; size_t eh = 0, ev = 0;
        if (!ReadDerTlv(extSeqVal + pos, extSeqLen - pos, eTag, eh, ev)) break;
        const uint8_t* eVal = extSeqVal + pos + eh;
        pos += eh + ev;
        if (eTag != 0x30) continue;
        size_t ep = 0;
        uint8_t oidTag = 0; size_t oh = 0, ov = 0;
        if (!ReadDerTlv(eVal, ev, oidTag, oh, ov) || oidTag != 0x06) continue;
        std::string oid = DecodeDerOid(eVal + oh, ov);
        ep = oh + ov;
        if (ep < ev && eVal[ep] == 0x01u) { // optional BOOLEAN critical
            uint8_t bTag = 0; size_t bh = 0, bv = 0;
            if (!ReadDerTlv(eVal + ep, ev - ep, bTag, bh, bv)) continue;
            ep += bh + bv;
        }
        uint8_t octTag = 0; size_t xh = 0, xv = 0;
        if (ep >= ev || !ReadDerTlv(eVal + ep, ev - ep, octTag, xh, xv) || octTag != 0x04) continue;
        const uint8_t* xVal = eVal + ep + xh;

        if (oid == "2.5.29.17") { // subjectAltName: GeneralNames ::= SEQUENCE OF GeneralName
            uint8_t gnTag = 0; size_t gh = 0, gv = 0;
            if (ReadDerTlv(xVal, xv, gnTag, gh, gv) && gnTag == 0x30) {
                size_t gp = 0;
                while (gp < gv) {
                    uint8_t nTag = 0; size_t nh = 0, nv = 0;
                    if (!ReadDerTlv(xVal + gh + gp, gv - gp, nTag, nh, nv)) break;
                    const uint8_t* nVal = xVal + gh + gp + nh;
                    gp += nh + nv;
                    std::wstring item;
                    if (nTag == 0x82u) item = L"DNS:" + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(nVal), nv));
                    else if (nTag == 0x81u) item = L"Email:" + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(nVal), nv));
                    else if (nTag == 0x86u) item = L"URI:" + PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(nVal), nv));
                    else if (nTag == 0x87u && nv == 4) {
                        wchar_t ipb[32] = {};
                        swprintf_s(ipb, L"IP:%u.%u.%u.%u", nVal[0], nVal[1], nVal[2], nVal[3]);
                        item = ipb;
                    } else if (nTag == 0x87u && nv == 16) {
                        item = L"IP6:" + DetailBinarySysDoc::HexBytes(nVal, 16, L':');
                    }
                    if (!item.empty()) {
                        if (!outSan.empty()) outSan += L", ";
                        outSan += item;
                    }
                }
            }
        } else if (oid == "2.5.29.15") { // keyUsage BIT STRING
            uint8_t kuTag = 0; size_t kh = 0, kv = 0;
            if (ReadDerTlv(xVal, xv, kuTag, kh, kv) && kuTag == 0x03 && kv >= 2) {
                uint16_t bits = static_cast<uint16_t>(xVal[kh + 1]) << 8;
                if (kv >= 3) bits |= xVal[kh + 2];
                static const wchar_t* const kNames[9] = {
                    L"DigitalSignature", L"NonRepudiation", L"KeyEncipherment", L"DataEncipherment",
                    L"KeyAgreement", L"KeyCertSign", L"CRLSign", L"EncipherOnly", L"DecipherOnly"
                };
                for (int b = 0; b < 9; ++b) {
                    if ((bits & (0x8000u >> b)) != 0) {
                        if (!outKeyUsage.empty()) outKeyUsage += L", ";
                        outKeyUsage += kNames[b];
                    }
                }
            }
        } else if (oid == "2.5.29.19") { // basicConstraints SEQUENCE { cA BOOLEAN OPTIONAL, pathLenConstraint INTEGER OPTIONAL }
            uint8_t bcTag = 0; size_t bh = 0, bv = 0;
            if (ReadDerTlv(xVal, xv, bcTag, bh, bv) && bcTag == 0x30) {
                bool isCa = false;
                if (bv >= 3 && xVal[bh] == 0x01u && xVal[bh + 1] == 0x01u && xVal[bh + 2] != 0) isCa = true;
                outBasicConstraints = isCa ? L"CA:TRUE" : L"CA:FALSE";
            }
        }
    }
}

struct ParsedX509CertSummary {
    uint32_t version = 1;
    std::wstring serialHex;
    std::wstring sigAlg;
    std::wstring issuerDn;
    std::wstring subjectDn;
    std::wstring notBefore;
    std::wstring notAfter;
    std::wstring pubKeySummary;
    std::wstring san;
    std::wstring keyUsage;
    std::wstring basicConstraints;
};

inline bool ParseDerX509CertificateSummary(const uint8_t* der, size_t len, ParsedX509CertSummary& out) {
    out = {};
    uint8_t outerTag = 0; size_t oh = 0, ov = 0;
    if (!ReadDerTlv(der, len, outerTag, oh, ov) || outerTag != 0x30) return false;
    const uint8_t* certVal = der + oh;
    uint8_t tbsTag = 0; size_t th = 0, tv = 0;
    if (!ReadDerTlv(certVal, ov, tbsTag, th, tv) || tbsTag != 0x30) return false;
    const uint8_t* tbs = certVal + th;
    size_t tp = 0;

    // 1. Optional [0] EXPLICIT Version
    if (tp < tv && tbs[tp] == 0xA0u) {
        uint8_t vTag = 0; size_t vh = 0, vv = 0;
        if (!ReadDerTlv(tbs + tp, tv - tp, vTag, vh, vv)) return false;
        uint8_t iTag = 0; size_t ih = 0, iv = 0;
        if (ReadDerTlv(tbs + tp + vh, vv, iTag, ih, iv) && iTag == 0x02 && iv == 1) {
            out.version = static_cast<uint32_t>(tbs[tp + vh + ih]) + 1u;
        }
        tp += vh + vv;
    }
    // 2. serialNumber INTEGER
    uint8_t sTag = 0; size_t sh = 0, sv = 0;
    if (tp >= tv || !ReadDerTlv(tbs + tp, tv - tp, sTag, sh, sv) || sTag != 0x02 || sv == 0) return false;
    out.serialHex = DetailBinarySysDoc::HexBytes(tbs + tp + sh, sv, L':');
    tp += sh + sv;

    // 3. signature AlgorithmIdentifier
    uint8_t sigTag = 0; size_t sigh = 0, sigv = 0;
    if (tp >= tv || !ReadDerTlv(tbs + tp, tv - tp, sigTag, sigh, sigv) || sigTag != 0x30) return false;
    {
        std::string oid, pOid; const uint8_t* pr = nullptr; size_t prl = 0;
        if (ParseDerAlgorithmIdentifier(tbs + tp + sigh, sigv, oid, pOid, pr, prl)) {
            out.sigAlg = MapKnownOid(oid);
        }
    }
    tp += sigh + sigv;

    // 4. issuer Name (SEQUENCE)
    uint8_t issTag = 0; size_t issh = 0, issv = 0;
    if (tp >= tv || !ReadDerTlv(tbs + tp, tv - tp, issTag, issh, issv) || issTag != 0x30) return false;
    out.issuerDn = ParseDerDistinguishedName(tbs + tp + issh, issv);
    tp += issh + issv;

    // 5. validity Validity ::= SEQUENCE { notBefore Time, notAfter Time }
    uint8_t valTag = 0; size_t valh = 0, valv = 0;
    if (tp >= tv || !ReadDerTlv(tbs + tp, tv - tp, valTag, valh, valv) || valTag != 0x30) return false;
    {
        const uint8_t* vPtr = tbs + tp + valh;
        uint8_t t1Tag = 0; size_t t1h = 0, t1v = 0;
        if (ReadDerTlv(vPtr, valv, t1Tag, t1h, t1v)) {
            out.notBefore = ParseDerTimeUtc(t1Tag, vPtr + t1h, t1v);
            uint8_t t2Tag = 0; size_t t2h = 0, t2v = 0;
            if (t1h + t1v < valv && ReadDerTlv(vPtr + t1h + t1v, valv - (t1h + t1v), t2Tag, t2h, t2v)) {
                out.notAfter = ParseDerTimeUtc(t2Tag, vPtr + t1h + t1v + t2h, t2v);
            }
        }
    }
    tp += valh + valv;

    // 6. subject Name (SEQUENCE)
    uint8_t subTag = 0; size_t subh = 0, subv = 0;
    if (tp >= tv || !ReadDerTlv(tbs + tp, tv - tp, subTag, subh, subv) || subTag != 0x30) return false;
    out.subjectDn = ParseDerDistinguishedName(tbs + tp + subh, subv);
    tp += subh + subv;

    // 7. subjectPublicKeyInfo SubjectPublicKeyInfo (SEQUENCE)
    uint8_t pkTag = 0; size_t pkh = 0, pkv = 0;
    if (tp >= tv || !ReadDerTlv(tbs + tp, tv - tp, pkTag, pkh, pkv) || pkTag != 0x30) return false;
    uint32_t pkBits = 0;
    ParseDerSubjectPublicKeyInfo(tbs + tp + pkh, pkv, out.pubKeySummary, pkBits);
    tp += pkh + pkv;

    // 8. Optional issuerUniqueID [1], subjectUniqueID [2], extensions [3] EXPLICIT Extensions
    while (tp < tv) {
        uint8_t oTag = 0; size_t xh = 0, xv = 0;
        if (!ReadDerTlv(tbs + tp, tv - tp, oTag, xh, xv)) break;
        if (oTag == 0xA3u) {
            uint8_t esTag = 0; size_t esh = 0, esv = 0;
            if (ReadDerTlv(tbs + tp + xh, xv, esTag, esh, esv) && esTag == 0x30) {
                ParseDerX509Extensions(tbs + tp + xh + esh, esv, out.san, out.keyUsage, out.basicConstraints);
            }
        }
        tp += xh + xv;
    }
    return true;
}

inline void AppendPortableX509Rows(const uint8_t* der, size_t len, const std::wstring& prefix, std::vector<std::vector<std::wstring>>& rows) {
#ifdef _WIN32
    PCCERT_CONTEXT pCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der, static_cast<DWORD>(len));
    if (pCert) {
        AppendCertContextRows(pCert, prefix, rows);
        CertFreeCertificateContext(pCert);
        return;
    }
#endif
    ParsedX509CertSummary s;
    if (ParseDerX509CertificateSummary(der, len, s)) {
        rows.push_back({ prefix + L"X.509 Version", L"v" + std::to_wstring(s.version) });
        if (!s.subjectDn.empty()) rows.push_back({ prefix + L"Subject DN", s.subjectDn });
        if (!s.issuerDn.empty())  rows.push_back({ prefix + L"Issuer DN", s.issuerDn });
        if (!s.serialHex.empty()) rows.push_back({ prefix + L"Serial Number", s.serialHex });
        if (!s.sigAlg.empty())    rows.push_back({ prefix + L"Signature Algorithm", s.sigAlg });
        if (!s.pubKeySummary.empty()) rows.push_back({ prefix + L"Public Key", s.pubKeySummary });
        if (!s.notBefore.empty()) rows.push_back({ prefix + L"Valid Not Before", s.notBefore });
        if (!s.notAfter.empty())  rows.push_back({ prefix + L"Valid Not After", s.notAfter });
        if (!s.san.empty())       rows.push_back({ prefix + L"Subject Alt Names (SAN)", s.san });
        if (!s.keyUsage.empty())  rows.push_back({ prefix + L"Key Usage", s.keyUsage });
    }
}
} // namespace DetailX509

namespace DetailPcap {

inline void DissectPacketL2L4(
    uint16_t linkType,
    const uint8_t* pkt,
    size_t capLen,
    std::wstring& outProto,
    std::wstring& outSrc,
    std::wstring& outDst,
    std::wstring& outInfo)
{
    outProto = L"RAW";
    outSrc   = L"-";
    outDst   = L"-";
    outInfo  = std::to_wstring(capLen) + L" bytes captured";

    size_t l3Off = 0;
    uint16_t ethType = 0;

    if (linkType == 1) { // Ethernet II
        if (capLen < 14) return;
        outSrc = DetailBinarySysDoc::HexBytes(pkt + 6, 6, L':');
        outDst = DetailBinarySysDoc::HexBytes(pkt + 0, 6, L':');
        ethType = DetailBinarySysDoc::RdU16BE(pkt + 12);
        l3Off = 14;
        if ((ethType == 0x8100 || ethType == 0x88A8) && capLen >= 18) { // 802.1Q VLAN
            ethType = DetailBinarySysDoc::RdU16BE(pkt + 16);
            l3Off = 18;
        }
        outProto = L"Ethernet";
    } else if (linkType == 113 && capLen >= 16) { // Linux SLL
        ethType = DetailBinarySysDoc::RdU16BE(pkt + 14);
        l3Off = 16;
    } else if (linkType == 101 || linkType == 228 || linkType == 229) { // Raw IP
        if (capLen < 1) return;
        const uint8_t v = pkt[0] >> 4;
        ethType = (v == 4) ? 0x0800 : ((v == 6) ? 0x86DD : 0);
        l3Off = 0;
    } else if (linkType == 0 && capLen >= 4) { // BSD Loopback
        const uint32_t af = DetailBinarySysDoc::RdU32LE(pkt);
        ethType = (af == 2) ? 0x0800 : 0x86DD;
        l3Off = 4;
    }

    if (ethType == 0x0806 && l3Off + 28 <= capLen) { // ARP
        const uint8_t* arp = pkt + l3Off;
        const uint16_t oper = DetailBinarySysDoc::RdU16BE(arp + 6);
        wchar_t sIp[32] = {}, tIp[32] = {};
        swprintf_s(sIp, L"%u.%u.%u.%u", arp[14], arp[15], arp[16], arp[17]);
        swprintf_s(tIp, L"%u.%u.%u.%u", arp[24], arp[25], arp[26], arp[27]);
        outProto = L"ARP";
        outSrc = sIp;
        outDst = tIp;
        if (oper == 1) outInfo = std::wstring(L"Who has ") + tIp + L"? Tell " + sIp;
        else if (oper == 2) outInfo = std::wstring(sIp) + L" is at " + DetailBinarySysDoc::HexBytes(arp + 8, 6, L':');
        return;
    }

    uint8_t ipProto = 0;
    size_t l4Off = 0;

    if (ethType == 0x0800 && l3Off + 20 <= capLen) { // IPv4
        const uint8_t* ip = pkt + l3Off;
        const size_t ihl = static_cast<size_t>(ip[0] & 0x0Fu) * 4;
        if (ihl < 20 || l3Off + ihl > capLen) return;
        ipProto = ip[9];
        wchar_t sBuf[32] = {}, dBuf[32] = {};
        swprintf_s(sBuf, L"%u.%u.%u.%u", ip[12], ip[13], ip[14], ip[15]);
        swprintf_s(dBuf, L"%u.%u.%u.%u", ip[16], ip[17], ip[18], ip[19]);
        outSrc = sBuf;
        outDst = dBuf;
        outProto = L"IPv4";
        l4Off = l3Off + ihl;
    } else if (ethType == 0x86DD && l3Off + 40 <= capLen) { // IPv6
        const uint8_t* ip6 = pkt + l3Off;
        ipProto = ip6[6];
        auto fmtIp6 = [](const uint8_t* a) -> std::wstring {
            wchar_t b[64] = {};
            swprintf_s(b, L"%x:%x:%x:%x:%x:%x:%x:%x",
                DetailBinarySysDoc::RdU16BE(a + 0), DetailBinarySysDoc::RdU16BE(a + 2),
                DetailBinarySysDoc::RdU16BE(a + 4), DetailBinarySysDoc::RdU16BE(a + 6),
                DetailBinarySysDoc::RdU16BE(a + 8), DetailBinarySysDoc::RdU16BE(a + 10),
                DetailBinarySysDoc::RdU16BE(a + 12), DetailBinarySysDoc::RdU16BE(a + 14));
            return b;
        };
        outSrc = fmtIp6(ip6 + 8);
        outDst = fmtIp6(ip6 + 24);
        outProto = L"IPv6";
        l4Off = l3Off + 40;
    } else {
        return;
    }

    if (ipProto == 6 && l4Off + 20 <= capLen) { // TCP
        const uint8_t* tcp = pkt + l4Off;
        const uint16_t sp = DetailBinarySysDoc::RdU16BE(tcp + 0);
        const uint16_t dp = DetailBinarySysDoc::RdU16BE(tcp + 2);
        const uint32_t seq = DetailBinarySysDoc::RdU32BE(tcp + 4);
        const size_t doff = static_cast<size_t>(tcp[12] >> 4) * 4;
        const uint8_t flg = tcp[13];
        const uint16_t win = DetailBinarySysDoc::RdU16BE(tcp + 14);

        outProto = L"TCP";
        std::wstring fstr;
        if (flg & 0x02u) fstr += L"SYN, ";
        if (flg & 0x10u) fstr += L"ACK, ";
        if (flg & 0x08u) fstr += L"PSH, ";
        if (flg & 0x01u) fstr += L"FIN, ";
        if (flg & 0x04u) fstr += L"RST, ";
        if (fstr.size() >= 2) fstr.resize(fstr.size() - 2);

        outInfo = std::to_wstring(sp) + L" -> " + std::to_wstring(dp) + L" [" + fstr + L"] Seq=" + std::to_wstring(seq) + L" Win=" + std::to_wstring(win);

        if (doff >= 20 && l4Off + doff < capLen) {
            const uint8_t* app = pkt + l4Off + doff;
            const size_t appLen = capLen - (l4Off + doff);
            if (appLen >= 5 && (std::memcmp(app, "GET ", 4) == 0 || std::memcmp(app, "POST ", 5) == 0 || std::memcmp(app, "HTTP/", 5) == 0)) {
                outProto = L"HTTP";
                size_t eol = 0;
                while (eol < appLen && eol < 96 && app[eol] != '\r' && app[eol] != '\n') ++eol;
                outInfo = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(app), eol));
            } else if (appLen >= 6 && app[0] == 0x16 && app[1] == 0x03) {
                outProto = L"TLS";
                outInfo += L" (TLS Handshake)";
            }
        }
    } else if (ipProto == 17 && l4Off + 8 <= capLen) { // UDP
        const uint8_t* udp = pkt + l4Off;
        const uint16_t sp = DetailBinarySysDoc::RdU16BE(udp + 0);
        const uint16_t dp = DetailBinarySysDoc::RdU16BE(udp + 2);
        const uint16_t ulen = DetailBinarySysDoc::RdU16BE(udp + 4);
        outProto = L"UDP";
        outInfo = std::to_wstring(sp) + L" -> " + std::to_wstring(dp) + L" Len=" + std::to_wstring(ulen);

        if ((sp == 53 || dp == 53 || sp == 5353 || dp == 5353) && l4Off + 8 + 12 <= capLen) {
            const uint8_t* dns = udp + 8;
            const size_t dnsLen = capLen - (l4Off + 8);
            const uint16_t txId  = DetailBinarySysDoc::RdU16BE(dns + 0);
            const uint16_t flags = DetailBinarySysDoc::RdU16BE(dns + 2);
            const uint16_t qdCnt = DetailBinarySysDoc::RdU16BE(dns + 4);
            outProto = (sp == 5353 || dp == 5353) ? L"mDNS" : L"DNS";
            std::string qname;
            if (qdCnt > 0 && dnsLen > 12) {
                size_t qp = 12;
                while (qp < dnsLen && dns[qp] != 0 && (dns[qp] & 0xC0u) == 0 && qname.size() < 200) {
                    const uint8_t lblLen = dns[qp++];
                    if (qp + lblLen > dnsLen) break;
                    if (!qname.empty()) qname.push_back('.');
                    qname.append(reinterpret_cast<const char*>(dns + qp), lblLen);
                    qp += lblLen;
                }
            }
            wchar_t txb[24] = {};
            swprintf_s(txb, L"0x%04X", txId);
            outInfo = std::wstring((flags & 0x8000u) ? L"Standard query response " : L"Standard query ") + txb +
                      (qname.empty() ? L"" : (L" " + PathUtils::Utf8ToWide(qname)));
        }
    } else if ((ipProto == 1 || ipProto == 58) && l4Off + 4 <= capLen) { // ICMP / ICMPv6
        const uint8_t icmpType = pkt[l4Off];
        const uint8_t icmpCode = pkt[l4Off + 1];
        outProto = (ipProto == 58) ? L"ICMPv6" : L"ICMP";
        if ((ipProto == 1 && icmpType == 8) || (ipProto == 58 && icmpType == 128)) outInfo = L"Echo (ping) request";
        else if ((ipProto == 1 && icmpType == 0) || (ipProto == 58 && icmpType == 129)) outInfo = L"Echo (ping) reply";
        else outInfo = L"Type=" + std::to_wstring(icmpType) + L" Code=" + std::to_wstring(icmpCode);
    }
}

} // namespace DetailPcap
#endif // E2_DOC_FORMAT_CORE_ONLY

namespace AiDataDetail {

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
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdF64BE;

using ::Explorer2::ByteIO::HalfToFloat;

using ::Explorer2::ByteIO::BFloat16ToFloat;

inline std::wstring FmtDouble(double v) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    double av = std::fabs(v);
    if (av == 0.0) return L"0";
    if (av >= 1e-4 && av < 1e9 && std::floor(v) == v) {
        swprintf_s(buf, L"%.0f", v);
    } else {
        swprintf_s(buf, L"%.6g", v);
    }
    return buf;
}

inline std::wstring CleanAsciiFixed(const uint8_t* p, size_t n) {
    size_t end = n;
    while (end > 0 && (p[end - 1] == 0 || p[end - 1] == ' ' || p[end - 1] == '\r' || p[end - 1] == '\n')) {
        --end;
    }
    std::string s;
    s.reserve(end);
    for (size_t i = 0; i < end; ++i) {
        if (p[i] == 0) break;
        s.push_back(p[i] >= 32 || p[i] == '\t' ? static_cast<char>(p[i]) : '?');
    }
    return PathUtils::Utf8ToWide(s);
}


// ============================================================================
// Minimal Protobuf Wire Reader (for .onnx and .orc)
// ============================================================================
struct PbReader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    PbReader(const uint8_t* d, size_t s) : data(d), size(s), pos(0) {}

    bool ReadVarint(uint64_t& out) {
        out = 0;
        int shift = 0;
        while (pos < size && shift < 70) {
            uint8_t b = data[pos++];
            out |= static_cast<uint64_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) return true;
            shift += 7;
        }
        return false;
    }

    bool ReadTag(uint32_t& fieldNum, uint8_t& wireType) {
        uint64_t tag = 0;
        if (!ReadVarint(tag) || tag == 0) return false;
        wireType = static_cast<uint8_t>(tag & 0x07u);
        fieldNum = static_cast<uint32_t>(tag >> 3);
        return fieldNum > 0;
    }

    bool ReadLengthDelimited(const uint8_t*& ptr, size_t& len) {
        uint64_t n = 0;
        if (!ReadVarint(n) || n > size - pos) return false;
        ptr = data + pos;
        len = static_cast<size_t>(n);
        pos += len;
        return true;
    }

    bool ReadString(std::string& out) {
        const uint8_t* p = nullptr;
        size_t n = 0;
        if (!ReadLengthDelimited(p, n)) return false;
        out.assign(reinterpret_cast<const char*>(p), n);
        return true;
    }

    bool SkipField(uint8_t wireType) {
        if (wireType == 0) {
            uint64_t v = 0;
            return ReadVarint(v);
        }
        if (wireType == 1) {
            if (pos + 8 > size) return false;
            pos += 8;
            return true;
        }
        if (wireType == 2) {
            const uint8_t* p = nullptr;
            size_t n = 0;
            return ReadLengthDelimited(p, n);
        }
        if (wireType == 5) {
            if (pos + 4 > size) return false;
            pos += 4;
            return true;
        }
        return false;
    }
};

// ============================================================================
// Minimal Bounds-Checked FlatBuffers Table Reader (for .tflite & .arrow/.feather)
// ============================================================================
struct FbTable {
    const uint8_t* buf = nullptr;
    size_t len = 0;
    size_t tablePos = 0;
    size_t vtablePos = 0;
    uint16_t vtableSize = 0;

    bool Init(const uint8_t* b, size_t l, size_t tPos) {
        buf = b;
        len = l;
        tablePos = tPos;
        if (!buf || tablePos + 4 > len) return false;
        int32_t vtRel = RdI32LE(buf + tablePos);
        int64_t vtAbs = static_cast<int64_t>(tablePos) - static_cast<int64_t>(vtRel);
        if (vtAbs < 0 || static_cast<size_t>(vtAbs) + 4 > len) return false;
        vtablePos = static_cast<size_t>(vtAbs);
        vtableSize = RdU16LE(buf + vtablePos);
        if (vtableSize < 4 || vtablePos + vtableSize > len) return false;
        return true;
    }

    size_t FieldPos(uint16_t slot) const {
        uint32_t vtOff = 4u + static_cast<uint32_t>(slot) * 2u;
        if (vtOff + 2 > vtableSize) return 0;
        uint16_t fieldRel = RdU16LE(buf + vtablePos + vtOff);
        if (fieldRel == 0 || tablePos + fieldRel > len) return 0;
        return tablePos + fieldRel;
    }

    uint8_t GetU8(uint16_t slot, uint8_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 1 <= len) ? buf[p] : def;
    }
    int8_t GetI8(uint16_t slot, int8_t def = 0) const {
        return static_cast<int8_t>(GetU8(slot, static_cast<uint8_t>(def)));
    }
    uint16_t GetU16(uint16_t slot, uint16_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 2 <= len) ? RdU16LE(buf + p) : def;
    }
    int16_t GetI16(uint16_t slot, int16_t def = 0) const {
        return static_cast<int16_t>(GetU16(slot, static_cast<uint16_t>(def)));
    }
    uint32_t GetU32(uint16_t slot, uint32_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 4 <= len) ? RdU32LE(buf + p) : def;
    }
    int32_t GetI32(uint16_t slot, int32_t def = 0) const {
        return static_cast<int32_t>(GetU32(slot, static_cast<uint32_t>(def)));
    }
    uint64_t GetU64(uint16_t slot, uint64_t def = 0) const {
        size_t p = FieldPos(slot);
        return (p && p + 8 <= len) ? RdU64LE(buf + p) : def;
    }
    int64_t GetI64(uint16_t slot, int64_t def = 0) const {
        return static_cast<int64_t>(GetU64(slot, static_cast<uint64_t>(def)));
    }

    size_t IndirectPos(uint16_t slot) const {
        size_t p = FieldPos(slot);
        if (!p || p + 4 > len) return 0;
        uint32_t uoff = RdU32LE(buf + p);
        if (uoff == 0 || uoff > len - p) return 0;
        return p + uoff;
    }

    std::string GetString(uint16_t slot) const {
        size_t ip = IndirectPos(slot);
        if (!ip || ip + 4 > len) return {};
        uint32_t slen = RdU32LE(buf + ip);
        if (slen > len - (ip + 4)) return {};
        return std::string(reinterpret_cast<const char*>(buf + ip + 4), slen);
    }

    size_t GetVector(uint16_t slot, uint32_t& outCount) const {
        outCount = 0;
        size_t ip = IndirectPos(slot);
        if (!ip || ip + 4 > len) return 0;
        outCount = RdU32LE(buf + ip);
        return ip + 4;
    }

    bool GetTable(uint16_t slot, FbTable& out) const {
        size_t ip = IndirectPos(slot);
        if (!ip) return false;
        return out.Init(buf, len, ip);
    }
};

inline bool GetFbRootTable(const uint8_t* buf, size_t len, FbTable& out) {
    if (!buf || len < 8) return false;
    uint32_t rootOff = RdU32LE(buf);
    if (rootOff < 4 || rootOff + 4 > len) return false;
    return out.Init(buf, len, rootOff);
}


// ============================================================================
// 3. Shared Tree Node for CBOR (RFC 8949) & MessagePack
// ============================================================================
struct BinTreeNode {
    enum Kind { Null, Bool, UInt, Int, Float, Str, Bytes, Array, Map } kind = Null;
    std::wstring typeLabel;
    bool bval = false;
    uint64_t uval = 0;
    int64_t ival = 0;
    double fval = 0.0;
    std::string str;
    std::vector<uint8_t> bytes;
    std::vector<BinTreeNode> children;
    std::vector<std::pair<BinTreeNode, BinTreeNode>> entries;

    std::wstring ScalarSummary() const {
        switch (kind) {
            case Null:  return typeLabel == L"undefined" ? L"undefined" : L"null";
            case Bool:  return bval ? L"true" : L"false";
            case UInt:  return std::to_wstring(uval);
            case Int:   return std::to_wstring(ival);
            case Float: return FmtDouble(fval);
            case Str:   return PathUtils::Utf8ToWide(str);
            case Bytes: {
                std::wstring h = L"0x";
                size_t cap = std::min<size_t>(bytes.size(), 24);
                wchar_t b[8] = {};
                for (size_t i = 0; i < cap; ++i) {
                    swprintf_s(b, L"%02X", bytes[i]);
                    h += b;
                }
                if (bytes.size() > cap) h += L"... (" + std::to_wstring(bytes.size()) + L" B)";
                return h;
            }
            case Array: return L"[" + std::to_wstring(children.size()) + L" items]";
            case Map:   return L"{" + std::to_wstring(entries.size()) + L" keys}";
        }
        return L"";
    }
};

inline void FlattenBinTree(const BinTreeNode& node, const std::wstring& path, SpreadsheetSheet& sh, int depth = 0) {
    if (sh.rows.size() >= 600 || depth > 24) return;
    if (node.kind == BinTreeNode::Map) {
        if (node.entries.empty()) {
            sh.rows.push_back({ path.empty() ? L"(root)" : path, node.typeLabel, L"{}" });
            return;
        }
        for (const auto& kv : node.entries) {
            std::wstring k = kv.first.ScalarSummary();
            std::wstring sub = path.empty() ? k : (path + L"." + k);
            FlattenBinTree(kv.second, sub, sh, depth + 1);
            if (sh.rows.size() >= 600) break;
        }
    } else if (node.kind == BinTreeNode::Array) {
        if (node.children.empty()) {
            sh.rows.push_back({ path.empty() ? L"(root)" : path, node.typeLabel, L"[]" });
            return;
        }
        for (size_t i = 0; i < node.children.size(); ++i) {
            std::wstring sub = (path.empty() ? L"" : path) + L"[" + std::to_wstring(i) + L"]";
            FlattenBinTree(node.children[i], sub, sh, depth + 1);
            if (sh.rows.size() >= 600) break;
        }
    } else {
        sh.rows.push_back({ path.empty() ? L"(root)" : path, node.typeLabel, node.ScalarSummary() });
    }
}

inline void PopulateSheetsFromBinTree(
    const std::vector<BinTreeNode>& roots,
    const std::wstring& prefix,
    std::vector<SpreadsheetSheet>& outSheets)
{
    // Check if root is an array of maps or multiple top-level maps -> tabular sheet
    const std::vector<BinTreeNode>* mapList = nullptr;
    if (roots.size() == 1 && roots[0].kind == BinTreeNode::Array && !roots[0].children.empty() &&
        roots[0].children[0].kind == BinTreeNode::Map)
    {
        mapList = &roots[0].children;
    } else if (roots.size() > 1 && roots[0].kind == BinTreeNode::Map) {
        mapList = &roots;
    }

    if (mapList) {
        std::vector<std::wstring> cols;
        std::set<std::wstring> seen;
        for (const auto& item : *mapList) {
            if (item.kind != BinTreeNode::Map) continue;
            for (const auto& kv : item.entries) {
                std::wstring k = kv.first.ScalarSummary();
                if (seen.insert(k).second) cols.push_back(k);
                if (cols.size() >= 48) break;
            }
            if (cols.size() >= 48) break;
        }
        if (!cols.empty()) {
            SpreadsheetSheet tbl;
            tbl.name = prefix + L"_Table";
            tbl.rows.push_back(cols);
            size_t rcap = std::min<size_t>(mapList->size(), 500);
            for (size_t r = 0; r < rcap; ++r) {
                const auto& item = (*mapList)[r];
                if (item.kind != BinTreeNode::Map) continue;
                std::vector<std::wstring> row(cols.size());
                for (const auto& kv : item.entries) {
                    std::wstring k = kv.first.ScalarSummary();
                    auto it = std::find(cols.begin(), cols.end(), k);
                    if (it != cols.end()) {
                        row[static_cast<size_t>(it - cols.begin())] = kv.second.ScalarSummary();
                    }
                }
                tbl.rows.push_back(std::move(row));
            }
            FinalizeSheetCols(tbl);
            outSheets.push_back(std::move(tbl));
        }
    }

    SpreadsheetSheet flat;
    flat.name = prefix + L"_Fields";
    flat.rows.push_back({ L"Field Path", L"Wire Type", L"Decoded Value" });
    if (roots.size() == 1) {
        FlattenBinTree(roots[0], L"", flat, 0);
    } else {
        for (size_t i = 0; i < roots.size() && flat.rows.size() < 600; ++i) {
            FlattenBinTree(roots[i], L"[" + std::to_wstring(i) + L"]", flat, 0);
        }
    }
    FinalizeSheetCols(flat);
    outSheets.push_back(std::move(flat));
}

#ifndef E2_DOC_FORMAT_CORE_ONLY
struct ArrowColSchema {
    std::wstring name;
    std::wstring typeLabel;
    bool nullable = true;
    uint8_t typeId = 0;    // Arrow Type union: 2=Int, 3=FloatingPoint, 5=Utf8, 6=Bool, 4=Binary, 20=LargeUtf8
    int32_t bitWidth = 32; // for Int / FloatingPoint (0=HALF,1=SINGLE,2=DOUBLE)
    bool isSigned = true;
    bool hasDict = false;
    int64_t dictId = 0;
    int32_t dictIdxBitWidth = 32;
    bool dictIdxSigned = true;
};

inline void ParseArrowSchemaFb(const FbTable& schemaTbl, std::vector<ArrowColSchema>& outCols) {
    uint32_t fCnt = 0;
    size_t fVec = schemaTbl.GetVector(1, fCnt);
    for (uint32_t i = 0; i < std::min<uint32_t>(fCnt, 256) && fVec && fVec + (i + 1) * 4 <= schemaTbl.len; ++i) {
        size_t fElem = fVec + i * 4;
        uint32_t fOff = RdU32LE(schemaTbl.buf + fElem);
        FbTable ft;
        if (!fOff || fElem + fOff > schemaTbl.len || !ft.Init(schemaTbl.buf, schemaTbl.len, fElem + fOff)) continue;

        ArrowColSchema col;
        col.name = PathUtils::Utf8ToWide(ft.GetString(0));
        col.nullable = ft.GetU8(1, 1) != 0;
        col.typeId = ft.GetU8(2, 0);
        FbTable typeTbl;
        bool hasTypeTbl = ft.GetTable(3, typeTbl);

        FbTable dictTbl;
        if (ft.GetTable(4, dictTbl)) {
            col.hasDict = true;
            col.dictId = dictTbl.GetI64(0, 0);
            FbTable idxIntTbl;
            if (dictTbl.GetTable(1, idxIntTbl)) {
                col.dictIdxBitWidth = idxIntTbl.GetI32(0, 32);
                col.dictIdxSigned   = idxIntTbl.GetU8(1, 1) != 0;
            }
        }

        switch (col.typeId) {
            case 1: col.typeLabel = L"Null"; break;
            case 2: // Int
                col.bitWidth = hasTypeTbl ? typeTbl.GetI32(0, 32) : 32;
                col.isSigned = hasTypeTbl ? (typeTbl.GetU8(1, 1) != 0) : true;
                col.typeLabel = (col.isSigned ? L"Int" : L"UInt") + std::to_wstring(col.bitWidth);
                break;
            case 3: { // FloatingPoint
                int16_t prec = hasTypeTbl ? typeTbl.GetI16(0, 1) : 1;
                col.bitWidth = (prec == 0) ? 16 : (prec == 2 ? 64 : 32);
                col.typeLabel = L"Float" + std::to_wstring(col.bitWidth);
                break;
            }
            case 4: col.typeLabel = L"Binary"; break;
            case 5: col.typeLabel = L"Utf8"; break;
            case 6: col.typeLabel = L"Bool"; break;
            case 7: col.typeLabel = L"Decimal"; break;
            case 8: col.typeLabel = L"Date"; col.bitWidth = (hasTypeTbl && typeTbl.GetI16(0, 1) == 0) ? 32 : 64; break;
            case 10: col.typeLabel = L"Timestamp"; col.bitWidth = 64; break;
            case 19: col.typeLabel = L"LargeBinary"; break;
            case 20: col.typeLabel = L"LargeUtf8"; break;
            default: col.typeLabel = L"ArrowType(" + std::to_wstring(col.typeId) + L")"; break;
        }
        if (col.hasDict) {
            col.typeLabel = L"Dict<" + col.typeLabel + L">";
        }
        outCols.push_back(std::move(col));
    }
}

inline bool DecodeArrowRecordBatch(
    const FbTable& rbTbl,
    const uint8_t* bodyPtr,
    size_t bodyLen,
    const std::vector<ArrowColSchema>& cols,
    const std::unordered_map<int64_t, std::vector<std::wstring>>& dictMap,
    std::vector<std::vector<std::wstring>>& outRows,
    int64_t& totalRowsAccum,
    std::wstring* outCompressionLabel = nullptr)
{
    int64_t batchRows = rbTbl.GetI64(0, 0);
    if (batchRows < 0) return false;
    if (batchRows == 0) return true;
    totalRowsAccum += batchRows;

    uint32_t nodeCnt = 0;
    size_t nodeVec = rbTbl.GetVector(1, nodeCnt);
    uint32_t bufCnt = 0;
    size_t bufVec = rbTbl.GetVector(2, bufCnt);
    if (!bufVec) return false;

    bool hasBodyComp = false;
    int8_t compCodec = 0;  // 0 = LZ4_FRAME, 1 = ZSTD
    int8_t compMethod = 0; // 0 = BUFFER
    FbTable compTbl;
    if (rbTbl.GetTable(3, compTbl)) {
        hasBodyComp = true;
        compCodec   = compTbl.GetI8(0, 0);
        compMethod  = compTbl.GetI8(1, 0);
        if (compMethod != 0 || (compCodec != 0 && compCodec != 1)) return false;
        if (outCompressionLabel) {
            *outCompressionLabel = (compCodec == 1) ? L"ZSTD" : L"LZ4_FRAME";
        }
    }

    size_t rowsToEmit = static_cast<size_t>(std::min<int64_t>(batchRows, static_cast<int64_t>(500 - std::min<size_t>(outRows.size(), 500))));
    if (rowsToEmit == 0) return true;

    std::vector<std::vector<uint8_t>> decompHolders;
    decompHolders.reserve(bufCnt);

    bool corruptComp = false;
    auto getBuf = [&](uint32_t idx, const uint8_t*& p, size_t& l) -> bool {
        p = nullptr; l = 0;
        if (idx >= bufCnt || bufVec + static_cast<size_t>(idx + 1) * 16 > rbTbl.len) return false;
        int64_t off = RdI64LE(rbTbl.buf + bufVec + static_cast<size_t>(idx) * 16);
        int64_t bl  = RdI64LE(rbTbl.buf + bufVec + static_cast<size_t>(idx) * 16 + 8);
        if (off < 0 || bl < 0 || static_cast<uint64_t>(off) + static_cast<uint64_t>(bl) > bodyLen) {
            corruptComp = true;
            return false;
        }
        if (bl == 0) return true;
        const uint8_t* rawP = bodyPtr + static_cast<size_t>(off);
        size_t rawL = static_cast<size_t>(bl);
        if (!hasBodyComp) {
            p = rawP;
            l = rawL;
            return true;
        }
        if (rawL < 8) { corruptComp = true; return false; }
        int64_t uncompLen = RdI64LE(rawP);
        if (uncompLen == -1) {
            p = rawP + 8;
            l = rawL - 8;
            return true;
        }
        if (uncompLen == 0) return true;
        if (uncompLen < -1 || uncompLen > 128LL * 1024LL * 1024LL) { corruptComp = true; return false; }
        std::vector<uint8_t> dec;
        size_t maxCap = static_cast<size_t>(uncompLen) + 64;
        bool okDec = (compCodec == 0)
            ? StreamCodecs::DecompressLz4Frame(rawP + 8, rawL - 8, dec, maxCap)
            : StreamCodecs::DecompressZstdFrame(rawP + 8, rawL - 8, dec, maxCap);
        if (!okDec || dec.size() != static_cast<size_t>(uncompLen)) {
            corruptComp = true;
            return false;
        }
        decompHolders.push_back(std::move(dec));
        p = decompHolders.back().data();
        l = decompHolders.back().size();
        return true;
    };

    size_t baseRow = outRows.size();
    outRows.resize(baseRow + rowsToEmit, std::vector<std::wstring>(cols.size()));

    uint32_t bCursor = 0;
    (void)nodeVec;
    (void)nodeCnt;
    for (size_t c = 0; c < cols.size(); ++c) {
        const auto& col = cols[c];
        const uint8_t* validBmp = nullptr; size_t validLen = 0;
        if (!getBuf(bCursor++, validBmp, validLen) && corruptComp) return false;

        auto isNull = [&](size_t r) -> bool {
            if (!validBmp || validLen == 0) return false;
            size_t byteIdx = r >> 3;
            if (byteIdx >= validLen) return false;
            return ((validBmp[byteIdx] >> (r & 7)) & 1u) == 0;
        };

        if (col.hasDict) {
            const uint8_t* idxBuf = nullptr; size_t idxLen = 0;
            if (!getBuf(bCursor++, idxBuf, idxLen) && corruptComp) return false;
            size_t bw = static_cast<size_t>(std::max(8, col.dictIdxBitWidth) / 8);
            auto itD = dictMap.find(col.dictId);
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (!idxBuf || (r + 1) * bw > idxLen) continue;
                const uint8_t* p = idxBuf + r * bw;
                int64_t dv = 0;
                if (col.dictIdxSigned) {
                    dv = (bw == 1) ? static_cast<int8_t>(p[0]) :
                         (bw == 2) ? RdI16LE(p) :
                         (bw == 4) ? RdI32LE(p) : RdI64LE(p);
                } else {
                    dv = static_cast<int64_t>((bw == 1) ? p[0] :
                         (bw == 2) ? RdU16LE(p) :
                         (bw == 4) ? RdU32LE(p) : RdU64LE(p));
                }
                if (itD != dictMap.end() && dv >= 0 && static_cast<size_t>(dv) < itD->second.size()) {
                    outRows[baseRow + r][c] = itD->second[static_cast<size_t>(dv)];
                } else {
                    outRows[baseRow + r][c] = std::to_wstring(dv);
                }
            }
            continue;
        }

        if (col.typeId == 5 || col.typeId == 4) { // Utf8 / Binary (validity, int32 offsets, data)
            const uint8_t* offBuf = nullptr; size_t offLen = 0;
            const uint8_t* valBuf = nullptr; size_t valLen = 0;
            if (!getBuf(bCursor++, offBuf, offLen) && corruptComp) return false;
            if (!getBuf(bCursor++, valBuf, valLen) && corruptComp) return false;
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (offBuf && (r + 2) * 4 <= offLen) {
                    int32_t o0 = RdI32LE(offBuf + r * 4);
                    int32_t o1 = RdI32LE(offBuf + (r + 1) * 4);
                    if (o0 >= 0 && o1 >= o0 && (o1 == o0 || (valBuf && static_cast<size_t>(o1) <= valLen))) {
                        std::string s = (o1 > o0) ? std::string(reinterpret_cast<const char*>(valBuf + o0), static_cast<size_t>(o1 - o0)) : std::string{};
                        outRows[baseRow + r][c] = PathUtils::Utf8ToWide(s);
                    }
                }
            }
        } else if (col.typeId == 20 || col.typeId == 19) { // LargeUtf8 / LargeBinary (validity, int64 offsets, data)
            const uint8_t* offBuf = nullptr; size_t offLen = 0;
            const uint8_t* valBuf = nullptr; size_t valLen = 0;
            if (!getBuf(bCursor++, offBuf, offLen) && corruptComp) return false;
            if (!getBuf(bCursor++, valBuf, valLen) && corruptComp) return false;
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (offBuf && (r + 2) * 8 <= offLen) {
                    int64_t o0 = RdI64LE(offBuf + r * 8);
                    int64_t o1 = RdI64LE(offBuf + (r + 1) * 8);
                    if (o0 >= 0 && o1 >= o0 && (o1 == o0 || (valBuf && static_cast<uint64_t>(o1) <= valLen))) {
                        std::string s = (o1 > o0) ? std::string(reinterpret_cast<const char*>(valBuf + o0), static_cast<size_t>(o1 - o0)) : std::string{};
                        outRows[baseRow + r][c] = PathUtils::Utf8ToWide(s);
                    }
                }
            }
        } else { // Fixed-width primitive (validity, data)
            const uint8_t* valBuf = nullptr; size_t valLen = 0;
            if (!getBuf(bCursor++, valBuf, valLen) && corruptComp) return false;
            for (size_t r = 0; r < rowsToEmit; ++r) {
                if (isNull(r)) { outRows[baseRow + r][c] = L"NULL"; continue; }
                if (!valBuf) continue;
                if (col.typeId == 6) { // Bool
                    if ((r >> 3) < valLen) {
                        outRows[baseRow + r][c] = (((valBuf[r >> 3] >> (r & 7)) & 1u) != 0) ? L"true" : L"false";
                    }
                } else if (col.typeId == 2 || col.typeId == 8 || col.typeId == 10) {
                    size_t bw = static_cast<size_t>(col.bitWidth / 8);
                    if (bw > 0 && (r + 1) * bw <= valLen) {
                        const uint8_t* p = valBuf + r * bw;
                        if (col.isSigned) {
                            int64_t v = (bw == 1) ? static_cast<int8_t>(p[0]) :
                                        (bw == 2) ? RdI16LE(p) :
                                        (bw == 4) ? RdI32LE(p) : RdI64LE(p);
                            outRows[baseRow + r][c] = std::to_wstring(v);
                        } else {
                            uint64_t v = (bw == 1) ? p[0] :
                                         (bw == 2) ? RdU16LE(p) :
                                         (bw == 4) ? RdU32LE(p) : RdU64LE(p);
                            outRows[baseRow + r][c] = std::to_wstring(v);
                        }
                    }
                } else if (col.typeId == 3) {
                    size_t bw = static_cast<size_t>(col.bitWidth / 8);
                    if (bw > 0 && (r + 1) * bw <= valLen) {
                        const uint8_t* p = valBuf + r * bw;
                        double v = (bw == 2) ? HalfToFloat(RdU16LE(p)) :
                                   (bw == 4) ? RdF32LE(p) : RdF64LE(p);
                        outRows[baseRow + r][c] = FmtDouble(v);
                    }
                }
            }
        }
    }
    return !corruptComp;
}

inline bool DecodeArrowDictionaryBatch(
    const FbTable& dbTbl,
    const uint8_t* bodyPtr,
    size_t bodyLen,
    const std::vector<ArrowColSchema>& cols,
    std::unordered_map<int64_t, std::vector<std::wstring>>& dictMap)
{
    int64_t dictId = dbTbl.GetI64(0, 0);
    FbTable rbTbl;
    if (!dbTbl.GetTable(1, rbTbl)) return false;
    bool isDelta = dbTbl.GetU8(2, 0) != 0;

    // Find a schema column using this dictId to determine the dictionary value type
    ArrowColSchema valCol;
    bool found = false;
    for (const auto& c : cols) {
        if (c.hasDict && c.dictId == dictId) {
            valCol = c;
            valCol.hasDict = false; // Decode physical dictionary values!
            found = true;
            break;
        }
    }
    if (!found) return true;

    std::vector<ArrowColSchema> singleCol = { valCol };
    std::vector<std::vector<std::wstring>> decRows;
    int64_t dummyAccum = 0;
    std::unordered_map<int64_t, std::vector<std::wstring>> emptyDict;
    if (!DecodeArrowRecordBatch(rbTbl, bodyPtr, bodyLen, singleCol, emptyDict, decRows, dummyAccum)) {
        return false;
    }
    auto& dst = dictMap[dictId];
    if (!isDelta) dst.clear();
    for (auto& r : decRows) {
        if (!r.empty()) dst.push_back(std::move(r[0]));
    }
    return true;
}

inline bool FinalizeArrowSheets(
    const std::wstring& extLower,
    const std::wstring& compLabel,
    size_t batchCount,
    int64_t totalRows,
    const std::vector<ArrowColSchema>& cols,
    std::vector<std::vector<std::wstring>>& dataRows,
    ParsedOfficeDocument& out,
    const wchar_t* titlePrefix = L"Apache Arrow Columnar IPC")
{
    if (cols.empty()) return false;
    if (totalRows > 0 && dataRows.empty()) return false; // Corrupt or undecodable record batch!

    SpreadsheetSheet schemaSh;
    schemaSh.name = L"Arrow_Schema";
    schemaSh.rows.push_back({ L"#", L"Field Name", L"Arrow Type", L"Nullable" });
    schemaSh.rows.push_back({
        L"(summary)",
        L"RecordBatches: " + std::to_wstring(batchCount),
        L"Total Rows: " + std::to_wstring(totalRows),
        compLabel.empty() ? L"UNCOMPRESSED" : compLabel
    });
    for (size_t i = 0; i < cols.size(); ++i) {
        schemaSh.rows.push_back({ std::to_wstring(i), cols[i].name, cols[i].typeLabel, cols[i].nullable ? L"true" : L"false" });
    }
    FinalizeSheetCols(schemaSh);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = std::wstring(titlePrefix) + L" (" + extLower +
        (compLabel.empty() ? L"" : (L", " + compLabel)) + L")";
    out.sheets.push_back(std::move(schemaSh));

    if (!dataRows.empty()) {
        SpreadsheetSheet dataSh;
        dataSh.name = L"Arrow_Data";
        std::vector<std::wstring> hdr;
        for (const auto& c : cols) hdr.push_back(c.name);
        dataSh.rows.push_back(std::move(hdr));
        for (auto& r : dataRows) dataSh.rows.push_back(std::move(r));
        FinalizeSheetCols(dataSh);
        out.sheets.push_back(std::move(dataSh));
    }
    out.valid = true;
    return true;
}

inline bool ParseArrowRandomAccessMemory(
    const std::vector<uint8_t>& raw,
    const std::wstring& extLower,
    const wchar_t* titlePrefix,
    ParsedOfficeDocument& out)
{
    std::vector<ArrowColSchema> cols;
    std::unordered_map<int64_t, std::vector<std::wstring>> dictMap;
    std::vector<std::vector<std::wstring>> dataRows;
    std::wstring compLabel;
    int64_t totalRows = 0;
    size_t batchCount = 0;


    if (raw.size() >= 18 && std::memcmp(raw.data(), "ARROW1", 6) == 0) {
        if (std::memcmp(raw.data() + raw.size() - 6, "ARROW1", 6) != 0) return false;
        uint32_t footerLen = RdU32LE(raw.data() + raw.size() - 10);
        if (footerLen == 0 || footerLen > raw.size() - 16) return false;
        size_t footerOff = raw.size() - 10 - footerLen;
        FbTable footer;
        if (!GetFbRootTable(raw.data() + footerOff, footerLen, footer)) return false;

        FbTable schemaTbl;
        if (!footer.GetTable(1, schemaTbl)) return false;
        ParseArrowSchemaFb(schemaTbl, cols);

        auto parseIpcBlockAt = [&](int64_t blkOff, int32_t metaLen, int64_t bodyLen, uint8_t expectedType) -> bool {
            if (blkOff < 0 || metaLen < 8 || bodyLen < 0) return false;
            if (static_cast<uint64_t>(blkOff) + static_cast<uint64_t>(metaLen) + static_cast<uint64_t>(bodyLen) > footerOff) return false;
            size_t msgPos = static_cast<size_t>(blkOff);
            uint32_t firstWord = RdU32LE(raw.data() + msgPos);
            size_t fbLen = (firstWord == 0xFFFFFFFFu) ? RdU32LE(raw.data() + msgPos + 4) : firstWord;
            size_t fbOff = (firstWord == 0xFFFFFFFFu) ? (msgPos + 8) : (msgPos + 4);
            if (fbLen == 0 || fbOff + fbLen > msgPos + static_cast<size_t>(metaLen)) return false;
            FbTable msg;
            if (!GetFbRootTable(raw.data() + fbOff, fbLen, msg) || msg.GetU8(1, 0) != expectedType) return false;
            const uint8_t* bodyPtr = raw.data() + static_cast<size_t>(blkOff) + static_cast<size_t>(metaLen);
            if (expectedType == 2) {
                FbTable db;
                return msg.GetTable(2, db) && DecodeArrowDictionaryBatch(db, bodyPtr, static_cast<size_t>(bodyLen), cols, dictMap);
            }
            FbTable rb;
            return msg.GetTable(2, rb) &&
                   DecodeArrowRecordBatch(rb, bodyPtr, static_cast<size_t>(bodyLen), cols, dictMap, dataRows, totalRows, &compLabel);
        };

        // Footer slot 2: dictionaries: [Block]
        uint32_t dictCnt = 0;
        size_t dictVec = footer.GetVector(2, dictCnt);
        for (uint32_t d = 0; d < dictCnt && dictVec && dictVec + (d + 1) * 24 <= footer.len; ++d) {
            if (!parseIpcBlockAt(RdI64LE(footer.buf + dictVec + d * 24),
                                 RdI32LE(footer.buf + dictVec + d * 24 + 8),
                                 RdI64LE(footer.buf + dictVec + d * 24 + 16), 2)) return false;
        }

        // Footer slot 3: recordBatches: [Block]
        uint32_t rbCnt = 0;
        size_t rbVec = footer.GetVector(3, rbCnt);
        batchCount = rbCnt;
        for (uint32_t b = 0; b < rbCnt && rbVec && rbVec + (b + 1) * 24 <= footer.len; ++b) {
            if (!parseIpcBlockAt(RdI64LE(footer.buf + rbVec + b * 24),
                                 RdI32LE(footer.buf + rbVec + b * 24 + 8),
                                 RdI64LE(footer.buf + rbVec + b * 24 + 16), 3)) return false;
        }
        return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out, titlePrefix);
    }
    return false;
}

template <typename RandomFileT>
inline bool ParseArrowRandomAccessFile(
    RandomFileT& rf,
    const std::wstring& extLower,
    const wchar_t* titlePrefix,
    ParsedOfficeDocument& out)
{
    std::vector<ArrowColSchema> cols;
    std::unordered_map<int64_t, std::vector<std::wstring>> dictMap;
    std::vector<std::vector<std::wstring>> dataRows;
    std::wstring compLabel;
    int64_t totalRows = 0;
    size_t batchCount = 0;
    {
        uint8_t tail10[10] = {};
        if (!rf.ReadAt(rf.fileSize - 10, tail10, 10) || std::memcmp(tail10 + 4, "ARROW1", 6) != 0) return false;
        uint32_t footerLen = RdU32LE(tail10);
        if (footerLen == 0 || footerLen > 64u * 1024u * 1024u || static_cast<uint64_t>(footerLen) + 16ULL > rf.fileSize) return false;
        uint64_t footerOff = rf.fileSize - 10ULL - footerLen;
        std::vector<uint8_t> footerBuf(footerLen);
        if (!rf.ReadAt(footerOff, footerBuf.data(), footerLen)) return false;

        FbTable footer;
        if (!GetFbRootTable(footerBuf.data(), footerBuf.size(), footer)) return false;
        FbTable schemaTbl;
        if (!footer.GetTable(1, schemaTbl)) return false;
        ParseArrowSchemaFb(schemaTbl, cols);

        std::vector<uint8_t> msgBuf, bodyBuf;
        auto readMsgTable = [&](uint64_t blkOff, int32_t metaLen, FbTable& outMsg) -> bool {
            if (metaLen < 8 || metaLen > 16 * 1024 * 1024 || blkOff + static_cast<uint64_t>(metaLen) > footerOff) return false;
            msgBuf.resize(static_cast<size_t>(metaLen));
            if (!rf.ReadAt(blkOff, msgBuf.data(), msgBuf.size())) return false;
            uint32_t w0 = RdU32LE(msgBuf.data());
            size_t fbLen = (w0 == 0xFFFFFFFFu) ? RdU32LE(msgBuf.data() + 4) : w0;
            size_t fbOff = (w0 == 0xFFFFFFFFu) ? 8 : 4;
            if (fbLen == 0 || fbOff + fbLen > msgBuf.size()) return false;
            return GetFbRootTable(msgBuf.data() + fbOff, fbLen, outMsg);
        };

        uint32_t dictCnt = 0;
        size_t dictVec = footer.GetVector(2, dictCnt);
        for (uint32_t d = 0; d < dictCnt && dictVec && dictVec + (d + 1) * 24 <= footer.len; ++d) {
            int64_t blkOff  = RdI64LE(footer.buf + dictVec + d * 24);
            int32_t metaLen = RdI32LE(footer.buf + dictVec + d * 24 + 8);
            int64_t bodyLen = RdI64LE(footer.buf + dictVec + d * 24 + 16);
            if (blkOff < 0 || bodyLen < 0 || bodyLen > 64LL * 1024LL * 1024LL) return false;
            FbTable msg;
            if (!readMsgTable(static_cast<uint64_t>(blkOff), metaLen, msg) || msg.GetU8(1, 0) != 2) return false;
            bodyBuf.resize(static_cast<size_t>(bodyLen));
            if (bodyLen > 0 && !rf.ReadAt(static_cast<uint64_t>(blkOff + metaLen), bodyBuf.data(), bodyBuf.size())) return false;
            FbTable db;
            if (!msg.GetTable(2, db) || !DecodeArrowDictionaryBatch(db, bodyBuf.data(), bodyBuf.size(), cols, dictMap)) return false;
        }

        uint32_t rbCnt = 0;
        size_t rbVec = footer.GetVector(3, rbCnt);
        batchCount = rbCnt;
        for (uint32_t b = 0; b < rbCnt && rbVec && rbVec + (b + 1) * 24 <= footer.len; ++b) {
            int64_t blkOff  = RdI64LE(footer.buf + rbVec + b * 24);
            int32_t metaLen = RdI32LE(footer.buf + rbVec + b * 24 + 8);
            int64_t bodyLen = RdI64LE(footer.buf + rbVec + b * 24 + 16);
            if (blkOff < 0 || bodyLen < 0 || static_cast<uint64_t>(blkOff + metaLen + bodyLen) > footerOff) return false;
            FbTable msg;
            if (!readMsgTable(static_cast<uint64_t>(blkOff), metaLen, msg) || msg.GetU8(1, 0) != 3) return false;
            FbTable rb;
            if (!msg.GetTable(2, rb)) return false;
            if (dataRows.size() < 500 && bodyLen <= 128LL * 1024LL * 1024LL) {
                bodyBuf.resize(static_cast<size_t>(bodyLen));
                if (bodyLen > 0 && !rf.ReadAt(static_cast<uint64_t>(blkOff + metaLen), bodyBuf.data(), bodyBuf.size())) return false;
                if (!DecodeArrowRecordBatch(rb, bodyBuf.data(), bodyBuf.size(), cols, dictMap, dataRows, totalRows, &compLabel)) {
                    return false;
                }
            } else {
                totalRows += std::max<int64_t>(0, rb.GetI64(0, 0));
            }
        }
        return FinalizeArrowSheets(extLower, compLabel, batchCount, totalRows, cols, dataRows, out, titlePrefix);
    }
}
#endif // E2_DOC_FORMAT_CORE_ONLY
} // namespace AiDataDetail

namespace GeoSciDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU24BE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdI64LE;
using ::Explorer2::ByteIO::RdI64BE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdF64BE;

// WMO GRIB sign-magnitude integer decoders (MSB = sign bit, remaining bits = magnitude)
inline int16_t RdGribSignMag16(const uint8_t* p) {
    uint16_t u = RdU16BE(p);
    int16_t mag = static_cast<int16_t>(u & 0x7FFFu);
    return (u & 0x8000u) ? static_cast<int16_t>(-mag) : mag;
}
inline int32_t RdGribSignMag32(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    int32_t mag = static_cast<int32_t>(u & 0x7FFFFFFFu);
    return (u & 0x80000000u) ? -mag : mag;
}

// IBM System/360 32-bit hexadecimal floating-point (used by GRIB1 BDS reference value)
inline double RdIbmFloat32(const uint8_t* p) {
    uint32_t u = RdU32BE(p);
    if ((u & 0x7FFFFFFFu) == 0) return 0.0;
    double sign = (u & 0x80000000u) ? -1.0 : 1.0;
    int exp16 = static_cast<int>((u >> 24) & 0x7Fu) - 64;
    uint32_t mant = u & 0x00FFFFFFu;
    return sign * static_cast<double>(mant) * std::ldexp(1.0, 4 * exp16 - 24);
}

inline std::wstring FmtNum(double v, int prec = 6) {
    if (std::isnan(v)) return L"NaN";
    if (std::isinf(v)) return v < 0 ? L"-Inf" : L"Inf";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.*g", prec, v);
    return buf;
}

inline std::wstring FmtCoord(double v) {
    if (!std::isfinite(v)) return L"0";
    wchar_t buf[64] = {};
    if (std::fabs(v) >= 1e12) swprintf_s(buf, L"%.6g", v);
    else swprintf_s(buf, L"%.6f", v);
    return buf;
}

inline std::wstring FormatUtcEpoch(int64_t unixSec) {
    return ::Explorer2::ReaderText::FormatUnixUtc(unixSec);
}

inline void FinalizeCols(SpreadsheetSheet& sh) {
    size_t mc = 0;
    for (const auto& r : sh.rows) mc = std::max(mc, r.size());
    sh.maxCols = mc;
}


} // namespace GeoSciDetail

namespace DetailMediaAndScoreDoc {

inline std::wstring Latin1OrCp1252ToWide(const uint8_t* p, size_t len) {
    static const uint16_t kCp1252_80_9F[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    std::wstring out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = p[i];
        if (b == 0) break;
        if (b >= 0x80 && b <= 0x9F) out.push_back(static_cast<wchar_t>(kCp1252_80_9F[b - 0x80]));
        else if (b >= 32 || b == '\t' || b == '\n' || b == '\r') out.push_back(static_cast<wchar_t>(b));
    }
    return PathUtils::Trim(out);
}

inline std::wstring FixedAsciiField(const uint8_t* p, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    std::string tmp;
    tmp.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = p[i];
        if (b >= 32 && b < 127) tmp.push_back(static_cast<char>(b));
        else if (b >= 128) tmp.push_back('?');
    }
    return PathUtils::Trim(PathUtils::Utf8ToWide(tmp));
}

inline std::wstring Utf16BytesToWide(const uint8_t* p, size_t len, bool bigEndian) {
    std::wstring out;
    out.reserve(len / 2);
    for (size_t i = 0; i + 2 <= len; i += 2) {
        uint16_t u = bigEndian ? DetailBinarySysDoc::RdU16BE(p + i) : DetailBinarySysDoc::RdU16LE(p + i);
        if (u == 0) break;
        if (u == 0xFEFF || u == 0xFFFE) continue;
        out.push_back(static_cast<wchar_t>(u));
    }
    return PathUtils::Trim(out);
}

#ifndef E2_DOC_FORMAT_CORE_ONLY

inline std::wstring FormatDurationSec(double sec) {
    if (!(sec >= 0.0) || sec > 3600000.0) return L"00:00";
    int totalMs = static_cast<int>(sec * 1000.0 + 0.5);
    int totalSec = totalMs / 1000;
    int ms = totalMs % 1000;
    int hrs = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    wchar_t buf[64] = {};
    if (hrs > 0) {
        swprintf_s(buf, L"%d:%02d:%02d", hrs, mins, secs);
    } else if (totalSec == 0 && ms > 0) {
        swprintf_s(buf, L"00:00.%03d", ms);
    } else {
        swprintf_s(buf, L"%02d:%02d", mins, secs);
    }
    return buf;
}

inline std::wstring FormatPts90kHz(uint64_t pts90k) {
    uint64_t totalMs = pts90k / 90ULL;
    uint64_t ms = totalMs % 1000ULL;
    uint64_t totalSec = totalMs / 1000ULL;
    uint64_t s = totalSec % 60ULL;
    uint64_t m = (totalSec / 60ULL) % 60ULL;
    uint64_t h = totalSec / 3600ULL;
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%02llu:%02llu:%02llu.%03llu",
        static_cast<unsigned long long>(h),
        static_cast<unsigned long long>(m),
        static_cast<unsigned long long>(s),
        static_cast<unsigned long long>(ms));
    return buf;
}


inline void ComputeDocCounts(ParsedOfficeDocument& out) { RecountDocText(out); }

inline std::vector<std::wstring> StripHtmlToParagraphs(std::wstring_view html, size_t maxParas = 600) {
    std::vector<std::wstring> paras;
    std::wstring cur;
    cur.reserve(256);

    auto flushCur = [&]() {
        std::wstring clean;
        clean.reserve(cur.size());
        bool prevSp = false;
        for (wchar_t ch : cur) {
            bool sp = (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n');
            if (sp) {
                if (!prevSp && !clean.empty()) clean.push_back(L' ');
                prevSp = true;
            } else {
                clean.push_back(ch);
                prevSp = false;
            }
        }
        clean = PathUtils::Trim(::Explorer2::XmlTagScan::DecodeEntitiesWide(clean));
        if (!clean.empty() && paras.size() < maxParas) {
            paras.push_back(std::move(clean));
        }
        cur.clear();
    };

    bool skipBlock = false;
    std::wstring skipEndTag;
    for (size_t i = 0; i < html.size() && paras.size() < maxParas; ++i) {
        if (html[i] == L'<') {
            size_t close = html.find(L'>', i + 1);
            if (close == std::wstring_view::npos) break;
            std::wstring tagLower = PathUtils::ToLower(html.substr(i + 1, std::min<size_t>(close - i - 1, 32)));
            size_t sp = tagLower.find_first_of(L" \t\r\n/");
            std::wstring tagName = (sp != std::wstring::npos && sp > 0) ? tagLower.substr(0, sp) : tagLower;
            if (!tagLower.empty() && tagLower[0] == L'/') {
                size_t sp2 = tagLower.find_first_of(L" \t\r\n", 1);
                tagName = (sp2 != std::wstring::npos) ? tagLower.substr(0, sp2) : tagLower;
            }
            if (skipBlock) {
                if (tagName == skipEndTag) skipBlock = false;
                i = close;
                continue;
            }
            if (tagName == L"style" || tagName == L"script" || tagName == L"head") {
                if (tagLower.back() != L'/') {
                    skipBlock = true;
                    skipEndTag = L"/" + tagName;
                }
                i = close;
                continue;
            }
            if (tagName == L"p" || tagName == L"/p" || tagName == L"br" || tagName == L"div" || tagName == L"/div" ||
                tagName == L"li" || tagName == L"tr" || tagName == L"h1" || tagName == L"h2" || tagName == L"h3" ||
                tagName == L"h4" || tagName == L"/h1" || tagName == L"/h2" || tagName == L"/h3" || tagName == L"mbp:pagebreak")
            {
                flushCur();
            }
            i = close;
            continue;
        }
        if (!skipBlock) {
            if (html[i] == L'\n') {
                if (cur.size() > 160) flushCur();
                else cur.push_back(L' ');
            } else {
                cur.push_back(html[i]);
            }
        }
    }
    flushCur();
    return paras;
}

inline std::vector<uint8_t> DecodeBase64Media(std::string_view in) {
    std::vector<uint8_t> out;
    ::Explorer2::Base64::Decode(in, out);
    return out;
}

inline std::string DecodeQuotedPrintable(std::string_view in) { return ContentLines::DecodeQuotedPrintable(in); }

// Bytes in a named charset (MIME / RFC 2047) as text; unknown names are read as UTF-8 (with the ANSI fallback)
inline std::wstring DecodeCharsetText(std::string_view bytes, std::string_view charsetName) {
    std::string cs = PathUtils::ToLowerAscii(PathUtils::TrimAscii(charsetName));
    if (cs.size() >= 2 && (cs.front() == '"' || cs.front() == '\'')) cs = cs.substr(1, cs.size() - 2);
    const unsigned cp = ::Explorer2::XmlTagScan::CodePageForCharset(cs);
    if (cp == 0 || cp == 65001 || cp == 20127 || bytes.empty()) return PathUtils::Utf8ToWide(bytes);
    int wn = MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wn <= 0) return PathUtils::Utf8ToWide(bytes);
    std::wstring w(static_cast<size_t>(wn), L'\0');
    MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), wn);
    return w;
}

inline std::wstring DecodeRfc2047Header(std::string_view rawHdr) {
    std::wstring result;
    size_t pos = 0;
    while (pos < rawHdr.size()) {
        size_t start = rawHdr.find("=?", pos);
        if (start == std::string_view::npos) {
            result += PathUtils::Utf8ToWide(rawHdr.substr(pos));
            break;
        }
        if (start > pos) {
            std::string_view pre = rawHdr.substr(pos, start - pos);
            bool onlyWs = true;
            for (char c : pre) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') { onlyWs = false; break; }
            if (!onlyWs || result.empty()) result += PathUtils::Utf8ToWide(pre);
        }
        size_t q1 = rawHdr.find('?', start + 2);
        size_t q2 = (q1 != std::string_view::npos) ? rawHdr.find('?', q1 + 1) : std::string_view::npos;
        size_t end = (q2 != std::string_view::npos) ? rawHdr.find("?=", q2 + 1) : std::string_view::npos;
        if (end == std::string_view::npos) {
            result += PathUtils::Utf8ToWide(rawHdr.substr(start));
            break;
        }
        std::string charset = PathUtils::ToLowerAscii(rawHdr.substr(start + 2, q1 - (start + 2)));
        char enc = rawHdr[q1 + 1];
        std::string_view payload = rawHdr.substr(q2 + 1, end - (q2 + 1));
        std::string decodedBytes;
        if (enc == 'B' || enc == 'b') {
            auto b = DecodeBase64Media(payload);
            decodedBytes.assign(reinterpret_cast<const char*>(b.data()), b.size());
        } else if (enc == 'Q' || enc == 'q') {
            std::string tmp(payload);
            for (char& c : tmp) if (c == '_') c = ' ';
            decodedBytes = DecodeQuotedPrintable(tmp);
        } else {
            decodedBytes.assign(payload.data(), payload.size());
        }
        result += DecodeCharsetText(decodedBytes, charset);
        pos = end + 2;
    }
    return PathUtils::Trim(result);
}

// Lightweight XML helpers for .mscz, .als, .gp, .fcpxml
inline std::string ExtractXmlAttr(std::string_view tag, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tag, attrName, v, false) ? std::string(v) : std::string();
}

inline std::wstring ExtractFirstXmlTagText(std::string_view xml, std::string_view tagName, size_t searchFrom = 0) {
    std::string open1 = "<" + std::string(tagName) + ">";
    std::string open2 = "<" + std::string(tagName) + " ";
    std::string closeTag = "</" + std::string(tagName) + ">";
    size_t p1 = xml.find(open1, searchFrom);
    size_t p2 = xml.find(open2, searchFrom);
    size_t p = std::min(p1, p2);
    if (p == std::string_view::npos) return {};
    size_t gt = xml.find('>', p);
    if (gt == std::string_view::npos || (gt > p && xml[gt - 1] == '/')) return {};
    size_t c = xml.find(closeTag, gt + 1);
    if (c == std::string_view::npos) return {};
    return ::Explorer2::XmlTagScan::DecodeEntitiesWide(PathUtils::Trim(PathUtils::Utf8ToWide(xml.substr(gt + 1, c - gt - 1))));
}

// ---- MIME (RFC 2045-2047, 2231) ----

// Header block / body of an entity, split at the first empty line
inline void SplitMimeEntity(std::string_view entity, std::string_view& headers, std::string_view& body) {
    size_t crlf = entity.find("\r\n\r\n"), lf = entity.find("\n\n");
    size_t end = crlf, sep = 4;
    if (lf != std::string_view::npos && (crlf == std::string_view::npos || lf < crlf)) { end = lf; sep = 2; }
    if (entity.rfind("\r\n", 0) == 0 || entity.rfind("\n", 0) == 0) { // no headers at all
        headers = {};
        body = entity.substr(entity[0] == '\r' ? 2 : 1);
        return;
    }
    headers = (end == std::string_view::npos) ? entity : entity.substr(0, end);
    body = (end == std::string_view::npos) ? std::string_view() : entity.substr(end + sep);
}

// Header fields (lower-case names), folded lines unfolded
inline std::vector<std::pair<std::string, std::string>> ParseMimeHeaders(std::string_view block) {
    std::vector<std::pair<std::string, std::string>> headers;
    size_t ls = 0;
    while (ls < block.size() && headers.size() < 512) {
        size_t le = block.find('\n', ls);
        if (le == std::string_view::npos) le = block.size();
        std::string_view line = block.substr(ls, le - ls);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        ls = (le < block.size()) ? (le + 1) : block.size();
        if (line.empty()) continue;
        if ((line[0] == ' ' || line[0] == '\t') && !headers.empty()) {
            headers.back().second.push_back(' ');
            size_t t = 0;
            while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) ++t;
            headers.back().second.append(line.substr(t));
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0 || colon > 64) continue;
        bool validKey = true;
        for (size_t k = 0; k < colon; ++k) {
            unsigned char ch = static_cast<unsigned char>(line[k]);
            if (ch <= 32 || ch >= 127) { validKey = false; break; }
        }
        if (!validKey) continue;
        std::string_view val = line.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
        headers.push_back({ PathUtils::ToLowerAscii(line.substr(0, colon)), std::string(val) });
    }
    return headers;
}

inline std::string MimeHeader(const std::vector<std::pair<std::string, std::string>>& headers, std::string_view name) {
    for (const auto& kv : headers) {
        if (kv.first == name) return kv.second;
    }
    return {};
}

// A parameter of a structured field ("text/plain; charset=utf-8; name=\"a b.txt\""); RFC 2231 "name*=utf-8''a%20b"
// values are percent-decoded and their charset applied. Empty when absent.
inline std::wstring MimeParam(std::string_view value, std::string_view name) {
    size_t i = 0;
    bool inQuote = false;
    // walk the ';'-separated parameters after the first item
    while (i < value.size()) {
        if (value[i] == '"') inQuote = !inQuote;
        if (value[i] != ';' || inQuote) { ++i; continue; }
        ++i;
        while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;
        size_t eq = value.find('=', i);
        if (eq == std::string_view::npos) break;
        std::string key = PathUtils::ToLowerAscii(PathUtils::TrimAscii(value.substr(i, eq - i)));
        size_t v = eq + 1;
        while (v < value.size() && (value[v] == ' ' || value[v] == '\t')) ++v;
        std::string val;
        if (v < value.size() && value[v] == '"') {
            ++v;
            while (v < value.size() && value[v] != '"') {
                if (value[v] == '\\' && v + 1 < value.size()) ++v;
                val.push_back(value[v++]);
            }
            if (v < value.size()) ++v;
        } else {
            while (v < value.size() && value[v] != ';' && value[v] != ' ' && value[v] != '\t') val.push_back(value[v++]);
        }
        i = v;
        if (key == name) return DecodeRfc2047Header(val); // (mailers often put encoded words in names)
        if (key.size() == name.size() + 1 && key.compare(0, name.size(), name) == 0 && key.back() == '*') {
            size_t q1 = val.find('\''), q2 = (q1 == std::string::npos) ? q1 : val.find('\'', q1 + 1);
            std::string charset = (q2 == std::string::npos) ? std::string() : val.substr(0, q1);
            std::string enc = (q2 == std::string::npos) ? val : val.substr(q2 + 1), bytes;
            auto hexv = [](char ch) {
                return (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
                     : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
            };
            for (size_t k = 0; k < enc.size(); ++k) {
                int h1 = (enc[k] == '%' && k + 2 < enc.size()) ? hexv(enc[k + 1]) : -1;
                int h2 = (h1 >= 0) ? hexv(enc[k + 2]) : -1;
                if (h1 >= 0 && h2 >= 0) { bytes.push_back(static_cast<char>(h1 * 16 + h2)); k += 2; }
                else bytes.push_back(enc[k]);
            }
            return DecodeCharsetText(bytes, charset);
        }
    }
    return {};
}

struct MimeContent {
    std::wstring plain, html;
    std::vector<std::wstring> attachments;
};

// One entity: a multipart's parts are walked (8 levels at most); inline text/plain and text/html parts are
// collected, everything else (and text marked as an attachment) is listed with its name and size
inline void WalkMimeEntity(const std::vector<std::pair<std::string, std::string>>& headers, std::string_view body,
                           int depth, MimeContent& mc) {
    const std::string ctype = MimeHeader(headers, "content-type");
    std::string media = PathUtils::ToLowerAscii(PathUtils::TrimAscii(std::string_view(ctype).substr(0, ctype.find(';'))));
    if (media.empty()) media = "text/plain";
    const std::string disp = MimeHeader(headers, "content-disposition");
    const bool asAttachment = PathUtils::ToLowerAscii(disp).rfind("attachment", 0) == 0;

    if (media.rfind("multipart/", 0) == 0 && depth < 8) {
        const std::string boundary = PathUtils::WideToUtf8(MimeParam(ctype, "boundary"));
        if (boundary.empty()) return;
        const std::string delim = "--" + boundary;
        // delimiter lines start a line; the closing one ends with "--"
        size_t pos = 0;
        auto nextDelim = [&](size_t from) {
            size_t p = body.find(delim, from);
            while (p != std::string_view::npos && p > 0 && body[p - 1] != '\n') p = body.find(delim, p + 1);
            return p;
        };
        pos = nextDelim(0);
        while (pos != std::string_view::npos) {
            size_t after = pos + delim.size();
            if (body.compare(after, 2, "--") == 0) break; // closing delimiter
            size_t partStart = body.find('\n', after);
            if (partStart == std::string_view::npos) break;
            ++partStart;
            size_t next = nextDelim(partStart);
            size_t partEnd = (next == std::string_view::npos) ? body.size() : next;
            // the line break before a delimiter belongs to the delimiter
            if (partEnd > partStart && body[partEnd - 1] == '\n') --partEnd;
            if (partEnd > partStart && body[partEnd - 1] == '\r') --partEnd;
            std::string_view partHeaders, partBody;
            SplitMimeEntity(body.substr(partStart, partEnd - partStart), partHeaders, partBody);
            WalkMimeEntity(ParseMimeHeaders(partHeaders), partBody, depth + 1, mc);
            pos = next;
        }
        return;
    }

    const std::string cte = PathUtils::ToLowerAscii(PathUtils::TrimAscii(MimeHeader(headers, "content-transfer-encoding")));
    auto decoded = [&]() {
        if (cte == "base64") {
            auto b = DecodeBase64Media(body);
            return std::string(reinterpret_cast<const char*>(b.data()), b.size());
        }
        if (cte == "quoted-printable") return DecodeQuotedPrintable(body);
        return std::string(body);
    };
    const bool isText = media == "text/plain" || media == "text/html";
    if (isText && !asAttachment) {
        std::wstring text = DecodeCharsetText(decoded(), PathUtils::WideToUtf8(MimeParam(ctype, "charset")));
        std::wstring& dst = (media == "text/plain") ? mc.plain : mc.html;
        if (media == "text/html") {
            std::wstring joined;
            for (const auto& p : StripHtmlToParagraphs(text, 400)) {
                if (!joined.empty()) joined += L"\n";
                joined += p;
            }
            text = std::move(joined);
        } else {
            text = PathUtils::Trim(text);
        }
        constexpr size_t kMaxPartChars = 1u << 20; // one paragraph in the view: keep it to the first million characters
        if (text.size() > kMaxPartChars) {
            text.resize(kMaxPartChars);
            text += L"\n[…]";
        }
        if (!text.empty() && dst.size() < 4 * kMaxPartChars) {
            if (!dst.empty()) dst += L"\n\n";
            dst += text;
        }
        return;
    }
    std::wstring name = MimeParam(disp, "filename");
    if (name.empty()) name = MimeParam(ctype, "name");
    if (media == "message/rfc822") {
        std::string_view innerHeaders, innerBody;
        SplitMimeEntity(body, innerHeaders, innerBody);
        std::wstring subject = DecodeRfc2047Header(MimeHeader(ParseMimeHeaders(innerHeaders), "subject"));
        if (name.empty()) name = L"(attached message) " + subject;
    }
    if (name.empty()) name = L"(unnamed " + PathUtils::Utf8ToWide(media) + L")";
    if (mc.attachments.size() < 256) mc.attachments.push_back(name + L" (" + PathUtils::FormatByteSize(decoded().size()) + L")");
}


inline bool ParseSingleEmailMessage(std::string_view msgSv, int msgIndex, ParsedOfficeDocument& out) {
        // Skip optional "From " mbox envelope line
        std::wstring mboxEnvelope;
        if (msgSv.size() >= 5 && msgSv.substr(0, 5) == "From ") {
            size_t nl = msgSv.find('\n');
            if (nl == std::string_view::npos) return false;
            mboxEnvelope = PathUtils::Trim(PathUtils::Utf8ToWide(msgSv.substr(0, nl)));
            msgSv.remove_prefix(nl + 1);
        }

        std::string_view hdrBlock, bodyBlock;
        SplitMimeEntity(msgSv, hdrBlock, bodyBlock);

        const std::vector<std::pair<std::string, std::string>> headers = ParseMimeHeaders(hdrBlock);

        if (headers.empty() && mboxEnvelope.empty()) return false;
        bool hasKnownHdr = !mboxEnvelope.empty();
        for (const auto& kv : headers) {
            if (kv.first == "from" || kv.first == "to" || kv.first == "subject" || kv.first == "date" ||
                kv.first == "message-id" || kv.first == "mime-version" || kv.first == "content-type" || kv.first == "received")
            {
                hasKnownHdr = true;
                break;
            }
        }
        if (!hasKnownHdr) return false;

        auto getHdr = [&](const char* k) -> std::string {
            for (const auto& kv : headers) if (kv.first == k) return kv.second;
            return {};
        };

        std::wstring subject = DecodeRfc2047Header(getHdr("subject"));
        std::wstring from    = DecodeRfc2047Header(getHdr("from"));
        std::wstring to      = DecodeRfc2047Header(getHdr("to"));
        std::wstring cc      = DecodeRfc2047Header(getHdr("cc"));
        std::wstring date    = DecodeRfc2047Header(getHdr("date"));
        std::wstring msgId   = DecodeRfc2047Header(getHdr("message-id"));

        if (msgIndex > 0) {
            out.paragraphs.push_back({ L"────────────────────────────────────────", 2, false, false });
        }
        if (!subject.empty()) out.paragraphs.push_back({ L"Subject: " + subject, 1, false, false });
        else if (!mboxEnvelope.empty()) out.paragraphs.push_back({ mboxEnvelope, 1, false, false });
        if (!from.empty())  out.paragraphs.push_back({ L"From: " + from, 2, false, false });
        if (!to.empty())    out.paragraphs.push_back({ L"To: " + to, 2, false, false });
        if (!cc.empty())    out.paragraphs.push_back({ L"Cc: " + cc, 2, false, false });
        if (!date.empty())  out.paragraphs.push_back({ L"Date: " + date, 2, false, false });
        if (!msgId.empty()) out.paragraphs.push_back({ L"Message-ID: " + msgId, 3, false, false });

        MimeContent mc;
        WalkMimeEntity(headers, bodyBlock, 0, mc);
        for (const auto& a : mc.attachments) out.paragraphs.push_back({ L"Attachment: " + a, 3, true, false });
        const std::wstring& text = !mc.plain.empty() ? mc.plain : mc.html;
        if (!text.empty()) out.paragraphs.push_back({ text, 0, false, false });
        return true;
}

// MS-OXRTFCP: PR_RTF_COMPRESSED back to RTF. "LZFu" is LZ77 over a 4 KB dictionary that starts with a fixed RTF
// preamble (control byte, then 8 tokens: a literal byte, or a big-endian offset:12 / length-2:4 reference; a
// reference to the write position ends the stream); "MELA" is stored uncompressed.
inline bool DecompressRtfLzfu(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    out.clear();
    if (in.size() < 16) return false;
    const uint32_t compSize = DetailBinarySysDoc::RdU32LE(in.data());
    const uint32_t rawSize = DetailBinarySysDoc::RdU32LE(in.data() + 4);
    const uint32_t compType = DetailBinarySysDoc::RdU32LE(in.data() + 8);
    if (rawSize > 64u * 1024 * 1024) return false;
    const size_t end = std::min<size_t>(in.size(), static_cast<size_t>(compSize) + 4);
    if (compType == 0x414C454Du) { // "MELA"
        out.assign(in.begin() + 16, in.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(in.size(), 16 + static_cast<size_t>(rawSize))));
        return !out.empty();
    }
    if (compType != 0x75465A4Cu) return false; // "LZFu"
    static const char kPreamble[] =
        "{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}{\\f0\\fnil \\froman \\fswiss \\fmodern \\fscript \\fdecor MS Sans "
        "SerifSymbolArialTimes New RomanCourier{\\colortbl\\red0\\green0\\blue0\r\n\\par \\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx";
    static_assert(sizeof(kPreamble) - 1 == 207, "MS-OXRTFCP dictionary preamble is 207 bytes");
    uint8_t dict[4096] = {};
    std::memcpy(dict, kPreamble, 207);
    size_t wpos = 207;
    out.reserve(rawSize);
    size_t pos = 16;
    while (pos < end && out.size() < rawSize) {
        const uint8_t control = in[pos++];
        for (int bit = 0; bit < 8 && out.size() < rawSize; ++bit) {
            if ((control & (1u << bit)) == 0) {
                if (pos >= end) return !out.empty();
                const uint8_t ch = in[pos++];
                out.push_back(ch);
                dict[wpos] = ch;
                wpos = (wpos + 1) & 0xFFFu;
            } else {
                if (pos + 2 > end) return !out.empty();
                const uint16_t token = static_cast<uint16_t>((in[pos] << 8) | in[pos + 1]);
                pos += 2;
                const size_t offset = token >> 4, length = static_cast<size_t>(token & 0xFu) + 2;
                if (offset == wpos) return true; // the end marker
                for (size_t k = 0; k < length && out.size() < rawSize; ++k) {
                    const uint8_t ch = dict[(offset + k) & 0xFFFu];
                    out.push_back(ch);
                    dict[wpos] = ch;
                    wpos = (wpos + 1) & 0xFFFu;
                }
            }
        }
    }
    return !out.empty();
}


struct OggStreamWalk {
    size_t pageCount = 0;
    size_t packetCount = 0;
    uint32_t serial = 0;
    int64_t maxGranule = 0;
    std::vector<std::vector<uint8_t>> headerPackets;
    std::set<uint32_t> streamSerials;
};

inline bool WalkOggBitstream(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& sourceTail, OggStreamWalk& res) {
    res = OggStreamWalk{};
    if (raw.size() < 27 || std::memcmp(raw.data(), "OggS", 4) != 0 || raw[4] != 0) return false;

    size_t pos = 0;
    size_t& pageCount = res.pageCount;
    size_t& packetCount = res.packetCount;
    uint32_t& serial = res.serial;
    serial = DetailBinarySysDoc::RdU32LE(raw.data() + 14);
    int64_t& maxGranule = res.maxGranule;

    std::vector<uint8_t> curPkt;
    auto& headerPackets = res.headerPackets;
    auto& streamSerials = res.streamSerials;
    while (pos + 27 <= raw.size()) {
        if (std::memcmp(raw.data() + pos, "OggS", 4) != 0 || raw[pos + 4] != 0) {
            size_t nextSync = pos + 1;
            bool resynced = false;
            while (nextSync + 27 <= raw.size() && nextSync < pos + 65536) {
                if (std::memcmp(raw.data() + nextSync, "OggS", 4) == 0 && raw[nextSync + 4] == 0) {
                    pos = nextSync;
                    resynced = true;
                    break;
                }
                ++nextSync;
            }
            if (!resynced) break;
        }
        // Only the first logical stream is read (pages of multiplexed streams, e.g. video, are skipped)
        const bool ours = DetailBinarySysDoc::RdU32LE(raw.data() + pos + 14) == serial;
        if ((raw[pos + 5] & 0x02u) != 0) streamSerials.insert(DetailBinarySysDoc::RdU32LE(raw.data() + pos + 14));
        uint64_t gpU = DetailBinarySysDoc::RdU64LE(raw.data() + pos + 6);
        if (ours && gpU != 0xFFFFFFFFFFFFFFFFULL) {
            int64_t gp = static_cast<int64_t>(gpU);
            if (gp > maxGranule) maxGranule = gp;
        }
        uint8_t numSegs = raw[pos + 26];
        if (pos + 27 + numSegs > raw.size()) break;
        const uint8_t* segTab = raw.data() + pos + 27;
        size_t dataPos = pos + 27 + numSegs;

        ++pageCount;
        for (uint8_t s = 0; s < numSegs; ++s) {
            uint8_t segLen = segTab[s];
            if (dataPos + segLen > raw.size()) { dataPos = raw.size(); break; }
            if (ours && headerPackets.size() < 3 && curPkt.size() < 262144) {
                curPkt.insert(curPkt.end(), raw.data() + dataPos, raw.data() + dataPos + segLen);
            }
            dataPos += segLen;
            if (segLen < 255 && ours) {
                ++packetCount;
                if (headerPackets.size() < 3) {
                    headerPackets.push_back(std::move(curPkt));
                    curPkt.clear();
                }
            }
        }
        pos = dataPos;
    }
    if (!curPkt.empty() && headerPackets.size() < 3) {
        ++packetCount;
        headerPackets.push_back(std::move(curPkt));
    }
    if (pageCount == 0 || headerPackets.empty()) return false;
    // The stream's last page carries its final granule position; for a file read only in part it is in the tail
    if (!sourceTail.empty()) {
        const auto& t = sourceTail;
        for (size_t p = 0; p + 27 <= t.size(); ++p) {
            if (t[p] != 'O' || std::memcmp(t.data() + p, "OggS", 4) != 0 || t[p + 4] != 0) continue;
            if (DetailBinarySysDoc::RdU32LE(t.data() + p + 14) != serial) continue;
            const uint64_t g = DetailBinarySysDoc::RdU64LE(t.data() + p + 6);
            if (g != 0xFFFFFFFFFFFFFFFFULL && static_cast<int64_t>(g) > maxGranule) maxGranule = static_cast<int64_t>(g);
        }
    }
    return true;
}

inline void ParseOggVorbisComments(
    const std::vector<uint8_t>& cpkt,
    size_t cOff,
    std::wstring& vendor,
    std::vector<std::pair<std::wstring, std::wstring>>& comments)
{
        if (cOff != SIZE_MAX && cOff + 4 <= cpkt.size()) {
            uint32_t vLen = DetailBinarySysDoc::RdU32LE(cpkt.data() + cOff);
            cOff += 4;
            if (vLen <= cpkt.size() - cOff) {
                vendor = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cpkt.data() + cOff), vLen)));
                cOff += vLen;
                if (cOff + 4 <= cpkt.size()) {
                    uint32_t numComments = DetailBinarySysDoc::RdU32LE(cpkt.data() + cOff);
                    cOff += 4;
                    for (uint32_t i = 0; i < numComments && i < 512 && cOff + 4 <= cpkt.size(); ++i) {
                        uint32_t clen = DetailBinarySysDoc::RdU32LE(cpkt.data() + cOff);
                        cOff += 4;
                        if (clen > cpkt.size() - cOff) break;
                        std::string_view sv(reinterpret_cast<const char*>(cpkt.data() + cOff), clen);
                        cOff += clen;
                        size_t eq = sv.find('=');
                        if (eq != std::string_view::npos) {
                            comments.push_back({
                                PathUtils::ToUpper(PathUtils::Utf8ToWide(sv.substr(0, eq))),
                                PathUtils::Utf8ToWide(sv.substr(eq + 1))
                            });
                        } else {
                            comments.push_back({ L"COMMENT", PathUtils::Utf8ToWide(sv) });
                        }
                    }
                }
            }
        }
}

inline bool FinalizeOggStreamDoc(
    const OggStreamWalk& walk,
    const std::wstring& formatTitle,
    const std::wstring& codec,
    uint32_t channels,
    uint32_t sampleRate,
    uint32_t granuleRate,
    uint16_t preSkip,
    int32_t nominalBitrate,
    const std::wstring& extraCodecInfo,
    const std::wstring& vendor,
    const std::vector<std::pair<std::wstring, std::wstring>>& comments,
    ParsedOfficeDocument& out)
{
    const uint32_t serial = walk.serial;
    const size_t pageCount = walk.pageCount;
    const size_t packetCount = walk.packetCount;
    const auto& streamSerials = walk.streamSerials;
    const int64_t maxGranule = walk.maxGranule;
    double durationSec = 0.0;
    if (granuleRate > 0 && maxGranule > preSkip) {
        durationSec = static_cast<double>(maxGranule - preSkip) / static_cast<double>(granuleRate);
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = formatTitle;
    SpreadsheetSheet info;
    info.name = L"Ogg_Stream";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"Codec", codec });
    info.rows.push_back({ L"Bitstream Serial", DetailBinarySysDoc::Hex32(serial) });
    info.rows.push_back({ L"Ogg Pages / Packets", std::to_wstring(pageCount) + L" / " + std::to_wstring(packetCount) });
    if (streamSerials.size() > 1) info.rows.push_back({ L"Logical Streams", std::to_wstring(streamSerials.size()) + L" (the first is described)" });
    if (channels > 0) info.rows.push_back({ L"Channels", std::to_wstring(channels) });
    if (sampleRate > 0) info.rows.push_back({ L"Sample Rate", std::to_wstring(sampleRate) + L" Hz" });
    if (nominalBitrate > 0) info.rows.push_back({ L"Nominal Bitrate", std::to_wstring(nominalBitrate / 1000) + L" kbps" });
    info.rows.push_back({ L"Max Granule Position", std::to_wstring(maxGranule) });
    info.rows.push_back({ L"Duration", FormatDurationSec(durationSec) });
    if (!extraCodecInfo.empty()) info.rows.push_back({ L"Codec Parameters", extraCodecInfo });
    if (!vendor.empty()) info.rows.push_back({ L"Encoder Vendor", vendor });
    for (const auto& kv : comments) {
        info.rows.push_back({ L"Tag: " + kv.first, kv.second });
    }
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));

    if (!comments.empty()) {
        SpreadsheetSheet csh;
        csh.name = L"Vorbis_Comments";
        csh.rows.push_back({ L"Key", L"Value" });
        for (const auto& kv : comments) csh.rows.push_back({ kv.first, kv.second });
        FinalizeSheetCols(csh);
        out.sheets.push_back(std::move(csh));
    }

    out.valid = true;
    return true;
}
#endif // E2_DOC_FORMAT_CORE_ONLY
} // namespace DetailMediaAndScoreDoc

namespace DetailStructuredDataDoc {

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


inline std::wstring TrimNullAscii(const uint8_t* p, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    return PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n));
}

#ifndef E2_DOC_FORMAT_CORE_ONLY
inline bool UnframeIwaStream(const std::vector<uint8_t>& framed, std::vector<uint8_t>& outStream) {
    auto snappyDecompress = [](const uint8_t* src, size_t srcLen, std::vector<uint8_t>& dst) {
        return ::Explorer2::StreamCodecs::SnappyDecompress(src, srcLen, dst, 16 * 1024 * 1024, true);
    };
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
}

inline bool ParseIwaProtobufStream(
    const std::vector<uint8_t>& decomp,
    const std::wstring& streamLabel,
    SpreadsheetSheet& sh,
    SpreadsheetSheet& cellsSh,
    size_t& validIwaFiles,
    size_t& validIwaObjects)
{
        auto pbSkip = [](const uint8_t* buf, size_t sz, size_t& p, uint8_t wt) -> bool {
            if (wt == 0u) { uint64_t v = 0; return DetailStructuredDataDoc::ReadVarint64(buf, sz, p, v); }
            if (wt == 1u) { if (p + 8u > sz) return false; p += 8u; return true; }
            if (wt == 2u) {
                uint64_t l = 0;
                if (!DetailStructuredDataDoc::ReadVarint64(buf, sz, p, l) || l > sz - p) return false;
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

                // Walk decompressed .iwa Protobuf stream: [archiveInfoLen: varint][ArchiveInfo][MessagePayloads...]
                size_t pos = 0;
                bool streamOk = true;
                size_t localObjs = 0;
                while (pos < decomp.size()) {
                    uint64_t aiLen = 0;
                    if (!DetailStructuredDataDoc::ReadVarint64(decomp.data(), decomp.size(), pos, aiLen) ||
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
                        if (!DetailStructuredDataDoc::ReadVarint64(aiBuf, aiSize, ap, tag) || tag == 0) { streamOk = false; break; }
                        const uint32_t fn = static_cast<uint32_t>(tag >> 3);
                        const uint8_t wt  = static_cast<uint8_t>(tag & 0x07u);
                        if (fn == 1 && wt == 0u) {
                            if (!DetailStructuredDataDoc::ReadVarint64(aiBuf, aiSize, ap, archiveId)) { streamOk = false; break; }
                        } else if (fn == 2 && wt == 2u) {
                            uint64_t miLen = 0;
                            if (!DetailStructuredDataDoc::ReadVarint64(aiBuf, aiSize, ap, miLen) || miLen > aiSize - ap) { streamOk = false; break; }
                            const uint8_t* miBuf = aiBuf + ap;
                            const size_t miSize = static_cast<size_t>(miLen);
                            ap += miSize;
                            MsgDesc md;
                            size_t mp = 0;
                            while (mp < miSize) {
                                uint64_t mtag = 0;
                                if (!DetailStructuredDataDoc::ReadVarint64(miBuf, miSize, mp, mtag) || mtag == 0) { streamOk = false; break; }
                                const uint32_t mfn = static_cast<uint32_t>(mtag >> 3);
                                const uint8_t mwt  = static_cast<uint8_t>(mtag & 0x07u);
                                if (mfn == 1 && mwt == 0u) {
                                    uint64_t tv = 0;
                                    if (!DetailStructuredDataDoc::ReadVarint64(miBuf, miSize, mp, tv)) { streamOk = false; break; }
                                    md.type = static_cast<uint32_t>(tv);
                                } else if (mfn == 3 && mwt == 0u) {
                                    uint64_t lv = 0;
                                    if (!DetailStructuredDataDoc::ReadVarint64(miBuf, miSize, mp, lv) || lv > decomp.size()) { streamOk = false; break; }
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
                                if (!DetailStructuredDataDoc::ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                                const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                                const uint8_t swt  = static_cast<uint8_t>(stag & 0x07u);
                                if (sfn == 3 && swt == 2u) {
                                    uint64_t sLen = 0;
                                    if (!DetailStructuredDataDoc::ReadVarint64(mPtr, mLen, sp, sLen) || sLen > mLen - sp) break;
                                    std::string_view sv(reinterpret_cast<const char*>(mPtr + sp), static_cast<size_t>(sLen));
                                    sp += static_cast<size_t>(sLen);
                                    for (auto& line : cleanIworkText(sv)) {
                                        if (sh.rows.size() <= 500) {
                                            sh.rows.push_back({
                                                streamLabel,
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
                                if (!DetailStructuredDataDoc::ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                                const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                                const uint8_t swt  = static_cast<uint8_t>(stag & 0x07u);
                                if (sfn == 3 && swt == 2u) {
                                    uint64_t eLen = 0;
                                    if (!DetailStructuredDataDoc::ReadVarint64(mPtr, mLen, sp, eLen) || eLen > mLen - sp) break;
                                    const uint8_t* ePtr = mPtr + sp;
                                    const size_t eSz = static_cast<size_t>(eLen);
                                    sp += eSz;
                                    uint64_t key = 0;
                                    std::wstring cellVal;
                                    size_t ep = 0;
                                    while (ep < eSz) {
                                        uint64_t etag = 0;
                                        if (!DetailStructuredDataDoc::ReadVarint64(ePtr, eSz, ep, etag) || etag == 0) break;
                                        const uint32_t efn = static_cast<uint32_t>(etag >> 3);
                                        const uint8_t ewt  = static_cast<uint8_t>(etag & 0x07u);
                                        if (efn == 1 && ewt == 0u) {
                                            DetailStructuredDataDoc::ReadVarint64(ePtr, eSz, ep, key);
                                        } else if ((efn == 3 || efn == 8) && ewt == 2u) {
                                            uint64_t sl = 0;
                                            if (!DetailStructuredDataDoc::ReadVarint64(ePtr, eSz, ep, sl) || sl > eSz - ep) break;
                                            cellVal = PathUtils::Trim(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(ePtr + ep), static_cast<size_t>(sl))));
                                            ep += static_cast<size_t>(sl);
                                        } else if (efn == 6 && ewt == 1u && ep + 8u <= eSz) {
                                            uint64_t bits = DetailStructuredDataDoc::RdU64LE(ePtr + ep);
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
                                            cellsSh.rows.push_back({ streamLabel, std::to_wstring(key), cellVal });
                                        }
                                        if (sh.rows.size() <= 500) {
                                            sh.rows.push_back({ streamLabel, L"TST::TableDataList [#" + std::to_wstring(key) + L"]", cellVal });
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
                                if (!DetailStructuredDataDoc::ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                                const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                                const uint8_t swt  = static_cast<uint8_t>(stag & 0x07u);
                                if (((md.type == 6001u && (sfn == 4u || sfn == 8u)) || (md.type == 2u && sfn == 1u)) && swt == 2u) {
                                    uint64_t sl = 0;
                                    if (!DetailStructuredDataDoc::ReadVarint64(mPtr, mLen, sp, sl) || sl > mLen - sp) break;
                                    if (sl > 0 && sl <= 256u && DetailStructuredDataDoc::IsMostlyPrintableUtf8(mPtr + sp, static_cast<size_t>(sl))) {
                                        std::wstring nm = PathUtils::Trim(PathUtils::Utf8ToWide(std::string(reinterpret_cast<const char*>(mPtr + sp), static_cast<size_t>(sl))));
                                        if (!nm.empty() && sh.rows.size() <= 500) {
                                            sh.rows.push_back({
                                                streamLabel,
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
    return streamOk && localObjs > 0;
}

inline bool ListZipPackage(
    const std::vector<uint8_t>& raw,
    ParsedOfficeDocument& out,
    std::vector<ZipDeflate::ArchiveItemMeta>& entries,
    bool& outUsedFile)
{
    entries.clear();
    outUsedFile = false;
    if (!out.filePath.empty() && ZipDeflate::ListZipEntriesFromFile(out.filePath, entries)) {
        outUsedFile = true;
        out.sourceReadFully = true;
        return true;
    }
    if (raw.size() >= 4 && raw[0] == 'P' && raw[1] == 'K') {
        return ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries);
    }
    return false;
}

inline bool ExtractZipPackageEntry(
    const std::vector<uint8_t>& raw,
    const ParsedOfficeDocument& out,
    bool usedFile,
    const ZipDeflate::ArchiveItemMeta& entry,
    std::vector<uint8_t>& outBytes)
{
    if (usedFile && !out.filePath.empty()) {
        return ZipDeflate::ExtractZipEntryFromFile(out.filePath, entry, outBytes);
    }
    return ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), entry, outBytes);
}

struct IWorkNumbersCtx {
    struct TileRowRec {
        uint32_t tileRowIndex = 0;
        uint32_t cellCount = 0;
        uint32_t storageVersion = 0;
        bool hasWideOffsets = false;
        std::vector<uint8_t> cellStorage;
        std::vector<uint8_t> cellOffsets;
    };
    struct TileObj {
        uint32_t maxColumn = 0;
        uint32_t maxRow = 0;
        uint32_t storageVersion = 0;
        std::vector<TileRowRec> rows;
    };
    struct TableModel {
        uint64_t archiveId = 0;
        std::wstring name;
        uint32_t numRows = 0;
        uint32_t numCols = 0;
        uint64_t stringTableId = 0;
        uint64_t formulaTableId = 0;
        uint64_t formatTableId = 0;
        uint64_t richTextTableId = 0;
        uint32_t tileSize = 256;
        std::vector<std::pair<uint32_t, uint64_t>> tiles; // (tileId, tileArchiveId)
        std::unordered_map<uint32_t, uint32_t> tileBaseRow;
        std::unordered_map<uint32_t, uint32_t> tileBaseCol;
    };

    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::wstring>> dataLists;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> dataListRefs;
    std::unordered_map<uint64_t, std::wstring> storageTexts;
    std::unordered_map<uint64_t, uint64_t> richPayloadToStorage;
    std::unordered_map<uint64_t, TileObj> tilesById;
    std::vector<TableModel> tables;
};

inline uint64_t ReadTspReferenceId(const uint8_t* p, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0;
        if (!ReadVarint64(p, len, pos, tag) || tag == 0) break;
        const uint32_t fn = static_cast<uint32_t>(tag >> 3);
        const uint8_t wt = static_cast<uint8_t>(tag & 0x07u);
        if (fn == 1 && wt == 0u) {
            uint64_t id = 0;
            if (ReadVarint64(p, len, pos, id)) return id;
            break;
        }
        if (wt == 0u) { uint64_t v = 0; if (!ReadVarint64(p, len, pos, v)) break; }
        else if (wt == 1u) { if (pos + 8 > len) break; pos += 8; }
        else if (wt == 2u) { uint64_t l = 0; if (!ReadVarint64(p, len, pos, l) || l > len - pos) break; pos += static_cast<size_t>(l); }
        else if (wt == 5u) { if (pos + 4 > len) break; pos += 4; }
        else break;
    }
    return 0;
}

inline std::wstring DecodeAppleBid128(const uint8_t* b16) {
    uint64_t lo = RdU64LE(b16);
    uint64_t hi = RdU64LE(b16 + 8);
    const bool neg = ((hi >> 63) & 1u) != 0;
    uint32_t comb = static_cast<uint32_t>((hi >> 49) & 0x3FFFu);
    int32_t exp = 0;
    uint64_t coeffHi = 0;
    if ((comb >> 12) == 3u) {
        if ((comb >> 10) == 0xFu) return L"NaN";
        if ((comb >> 11) == 0x7u) return neg ? L"-Inf" : L"Inf";
        exp = static_cast<int32_t>((hi >> 47) & 0x3FFFu) - 6176;
        coeffHi = (1ULL << 49) | (hi & ((1ULL << 47) - 1ULL));
    } else {
        exp = static_cast<int32_t>(comb) - 6176;
        coeffHi = hi & ((1ULL << 49) - 1ULL);
    }
    if (coeffHi == 0 && lo == 0) return L"0";
    if (coeffHi == 0 && exp >= -18 && exp <= 18) {
        double dv = static_cast<double>(lo) * std::pow(10.0, static_cast<double>(exp));
        if (neg) dv = -dv;
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%.12g", dv);
        return buf;
    }
    long double ldv = (static_cast<long double>(coeffHi) * 18446744073709551616.0L + static_cast<long double>(lo)) *
                      std::pow(10.0L, static_cast<long double>(exp));
    if (neg) ldv = -ldv;
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%.12g", static_cast<double>(ldv));
    return buf;
}

inline void CollectIWorkNumbersProtobuf(const std::vector<uint8_t>& decomp, IWorkNumbersCtx& ctx) {
    auto pbSkip = [](const uint8_t* buf, size_t sz, size_t& p, uint8_t wt) -> bool {
        if (wt == 0u) { uint64_t v = 0; return ReadVarint64(buf, sz, p, v); }
        if (wt == 1u) { if (p + 8u > sz) return false; p += 8u; return true; }
        if (wt == 2u) { uint64_t l = 0; if (!ReadVarint64(buf, sz, p, l) || l > sz - p) return false; p += static_cast<size_t>(l); return true; }
        if (wt == 5u) { if (p + 4u > sz) return false; p += 4u; return true; }
        return false;
    };

    size_t pos = 0;
    while (pos < decomp.size()) {
        uint64_t aiLen = 0;
        if (!ReadVarint64(decomp.data(), decomp.size(), pos, aiLen) || aiLen == 0 || aiLen > decomp.size() - pos) break;
        const uint8_t* aiBuf = decomp.data() + pos;
        const size_t aiSize = static_cast<size_t>(aiLen);
        pos += aiSize;

        uint64_t archiveId = 0;
        struct MsgDesc { uint32_t type = 0; size_t length = 0; };
        std::vector<MsgDesc> msgs;
        bool ok = true;
        size_t ap = 0;
        while (ap < aiSize) {
            uint64_t tag = 0;
            if (!ReadVarint64(aiBuf, aiSize, ap, tag) || tag == 0) { ok = false; break; }
            const uint32_t fn = static_cast<uint32_t>(tag >> 3);
            const uint8_t wt = static_cast<uint8_t>(tag & 0x07u);
            if (fn == 1 && wt == 0u) {
                if (!ReadVarint64(aiBuf, aiSize, ap, archiveId)) { ok = false; break; }
            } else if (fn == 2 && wt == 2u) {
                uint64_t miLen = 0;
                if (!ReadVarint64(aiBuf, aiSize, ap, miLen) || miLen > aiSize - ap) { ok = false; break; }
                const uint8_t* miBuf = aiBuf + ap;
                const size_t miSize = static_cast<size_t>(miLen);
                ap += miSize;
                MsgDesc md;
                size_t mp = 0;
                while (mp < miSize) {
                    uint64_t mtag = 0;
                    if (!ReadVarint64(miBuf, miSize, mp, mtag) || mtag == 0) { ok = false; break; }
                    const uint32_t mfn = static_cast<uint32_t>(mtag >> 3);
                    const uint8_t mwt = static_cast<uint8_t>(mtag & 0x07u);
                    if (mfn == 1 && mwt == 0u) {
                        uint64_t tv = 0;
                        if (!ReadVarint64(miBuf, miSize, mp, tv)) { ok = false; break; }
                        md.type = static_cast<uint32_t>(tv);
                    } else if (mfn == 3 && mwt == 0u) {
                        uint64_t lv = 0;
                        if (!ReadVarint64(miBuf, miSize, mp, lv) || lv > decomp.size()) { ok = false; break; }
                        md.length = static_cast<size_t>(lv);
                    } else if (!pbSkip(miBuf, miSize, mp, mwt)) {
                        ok = false; break;
                    }
                }
                if (!ok) break;
                msgs.push_back(md);
            } else if (!pbSkip(aiBuf, aiSize, ap, wt)) {
                ok = false; break;
            }
        }
        if (!ok) break;

        for (size_t mi = 0; mi < msgs.size(); ++mi) {
            const auto& md = msgs[mi];
            if (md.length > decomp.size() - pos) { ok = false; break; }
            const uint8_t* mPtr = decomp.data() + pos;
            const size_t mLen = md.length;
            pos += mLen;
            const uint64_t objId = (mi == 0) ? archiveId : 0;

            if (md.type >= 2000u && md.type <= 2030u && objId != 0) {
                size_t sp = 0;
                while (sp < mLen) {
                    uint64_t stag = 0;
                    if (!ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                    const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                    const uint8_t swt = static_cast<uint8_t>(stag & 0x07u);
                    if (sfn == 3 && swt == 2u) {
                        uint64_t sl = 0;
                        if (!ReadVarint64(mPtr, mLen, sp, sl) || sl > mLen - sp) break;
                        ctx.storageTexts[objId] = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(mPtr + sp), static_cast<size_t>(sl))));
                        sp += static_cast<size_t>(sl);
                    } else if (!pbSkip(mPtr, mLen, sp, swt)) break;
                }
            } else if ((md.type == 6003u || md.type == 6218u || md.type == 6220u) && objId != 0) {
                // RichTextPayloadArchive: field 1 = TSP::Reference storage
                size_t sp = 0;
                while (sp < mLen) {
                    uint64_t stag = 0;
                    if (!ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                    const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                    const uint8_t swt = static_cast<uint8_t>(stag & 0x07u);
                    if (sfn == 1 && swt == 2u) {
                        uint64_t rl = 0;
                        if (!ReadVarint64(mPtr, mLen, sp, rl) || rl > mLen - sp) break;
                        uint64_t refId = ReadTspReferenceId(mPtr + sp, static_cast<size_t>(rl));
                        if (refId != 0) ctx.richPayloadToStorage[objId] = refId;
                        sp += static_cast<size_t>(rl);
                    } else if (!pbSkip(mPtr, mLen, sp, swt)) break;
                }
            } else if ((md.type == 6005u || md.type == 6201u) && objId != 0) {
                auto& kvMap = ctx.dataLists[objId];
                auto& refMap = ctx.dataListRefs[objId];
                size_t sp = 0;
                while (sp < mLen) {
                    uint64_t stag = 0;
                    if (!ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                    const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                    const uint8_t swt = static_cast<uint8_t>(stag & 0x07u);
                    if (sfn == 3 && swt == 2u) {
                        uint64_t el = 0;
                        if (!ReadVarint64(mPtr, mLen, sp, el) || el > mLen - sp) break;
                        const uint8_t* epPtr = mPtr + sp;
                        const size_t epSz = static_cast<size_t>(el);
                        sp += epSz;
                        uint64_t key = 0, refTarget = 0;
                        std::wstring val;
                        size_t ep = 0;
                        while (ep < epSz) {
                            uint64_t etag = 0;
                            if (!ReadVarint64(epPtr, epSz, ep, etag) || etag == 0) break;
                            const uint32_t efn = static_cast<uint32_t>(etag >> 3);
                            const uint8_t ewt = static_cast<uint8_t>(etag & 0x07u);
                            if (efn == 1 && ewt == 0u) {
                                ReadVarint64(epPtr, epSz, ep, key);
                            } else if ((efn == 3 || efn == 8) && ewt == 2u) {
                                uint64_t sl = 0;
                                if (!ReadVarint64(epPtr, epSz, ep, sl) || sl > epSz - ep) break;
                                val = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(epPtr + ep), static_cast<size_t>(sl)));
                                ep += static_cast<size_t>(sl);
                            } else if ((efn == 4 || efn == 9) && ewt == 2u) {
                                uint64_t rl = 0;
                                if (!ReadVarint64(epPtr, epSz, ep, rl) || rl > epSz - ep) break;
                                refTarget = ReadTspReferenceId(epPtr + ep, static_cast<size_t>(rl));
                                ep += static_cast<size_t>(rl);
                            } else if (efn == 6 && ewt == 1u && ep + 8u <= epSz) {
                                uint64_t bits = RdU64LE(epPtr + ep); ep += 8u;
                                double dv = 0.0; std::memcpy(&dv, &bits, 8);
                                wchar_t dbuf[64] = {}; swprintf_s(dbuf, L"%.12g", dv);
                                val = dbuf;
                            } else if (!pbSkip(epPtr, epSz, ep, ewt)) break;
                        }
                        if (key != 0 || !val.empty() || refTarget != 0) {
                            if (!val.empty()) kvMap[key] = std::move(val);
                            if (refTarget != 0) refMap[key] = refTarget;
                        }
                    } else if (!pbSkip(mPtr, mLen, sp, swt)) break;
                }
            } else if (md.type == 6002u && objId != 0) {
                // TST::TileArchive
                IWorkNumbersCtx::TileObj tile;
                size_t sp = 0;
                while (sp < mLen) {
                    uint64_t stag = 0;
                    if (!ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                    const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                    const uint8_t swt = static_cast<uint8_t>(stag & 0x07u);
                    if (sfn == 1 && swt == 0u) { uint64_t v = 0; ReadVarint64(mPtr, mLen, sp, v); tile.maxColumn = static_cast<uint32_t>(v); }
                    else if (sfn == 2 && swt == 0u) { uint64_t v = 0; ReadVarint64(mPtr, mLen, sp, v); tile.maxRow = static_cast<uint32_t>(v); }
                    else if (sfn == 6 && swt == 0u) { uint64_t v = 0; ReadVarint64(mPtr, mLen, sp, v); tile.storageVersion = static_cast<uint32_t>(v); }
                    else if (sfn == 5 && swt == 2u) {
                        uint64_t rl = 0;
                        if (!ReadVarint64(mPtr, mLen, sp, rl) || rl > mLen - sp) break;
                        const uint8_t* rPtr = mPtr + sp;
                        const size_t rSz = static_cast<size_t>(rl);
                        sp += rSz;
                        IWorkNumbersCtx::TileRowRec rr;
                        std::vector<uint8_t> preStorage, preOffsets;
                        size_t rp = 0;
                        while (rp < rSz) {
                            uint64_t rtag = 0;
                            if (!ReadVarint64(rPtr, rSz, rp, rtag) || rtag == 0) break;
                            const uint32_t rfn = static_cast<uint32_t>(rtag >> 3);
                            const uint8_t rwt = static_cast<uint8_t>(rtag & 0x07u);
                            if (rfn == 1 && rwt == 0u) { uint64_t v = 0; ReadVarint64(rPtr, rSz, rp, v); rr.tileRowIndex = static_cast<uint32_t>(v); }
                            else if (rfn == 2 && rwt == 0u) { uint64_t v = 0; ReadVarint64(rPtr, rSz, rp, v); rr.cellCount = static_cast<uint32_t>(v); }
                            else if (rfn == 5 && rwt == 0u) { uint64_t v = 0; ReadVarint64(rPtr, rSz, rp, v); rr.storageVersion = static_cast<uint32_t>(v); }
                            else if (rfn == 8 && rwt == 0u) { uint64_t v = 0; ReadVarint64(rPtr, rSz, rp, v); rr.hasWideOffsets = (v != 0); }
                            else if ((rfn == 3 || rfn == 4 || rfn == 6 || rfn == 7) && rwt == 2u) {
                                uint64_t bl = 0;
                                if (!ReadVarint64(rPtr, rSz, rp, bl) || bl > rSz - rp) break;
                                std::vector<uint8_t> chunk(rPtr + rp, rPtr + rp + static_cast<size_t>(bl));
                                rp += static_cast<size_t>(bl);
                                if (rfn == 3) rr.cellStorage = std::move(chunk);
                                else if (rfn == 4) rr.cellOffsets = std::move(chunk);
                                else if (rfn == 6) preStorage = std::move(chunk);
                                else if (rfn == 7) preOffsets = std::move(chunk);
                            } else if (!pbSkip(rPtr, rSz, rp, rwt)) break;
                        }
                        if (rr.cellStorage.empty() && !preStorage.empty()) {
                            rr.cellStorage = std::move(preStorage);
                            rr.cellOffsets = std::move(preOffsets);
                        }
                        if (!rr.cellStorage.empty() && !rr.cellOffsets.empty()) {
                            tile.rows.push_back(std::move(rr));
                        }
                    } else if (!pbSkip(mPtr, mLen, sp, swt)) break;
                }
                ctx.tilesById[objId] = std::move(tile);
            } else if (md.type == 6001u) {
                // TST::TableModelArchive
                IWorkNumbersCtx::TableModel tm;
                tm.archiveId = objId;
                auto parseRbTree = [&](const uint8_t* tp, size_t tsz, std::unordered_map<uint32_t, uint32_t>& outMap) {
                    size_t p = 0;
                    while (p < tsz) {
                        uint64_t tag = 0;
                        if (!ReadVarint64(tp, tsz, p, tag) || tag == 0) break;
                        if ((tag >> 3) == 1 && (tag & 7u) == 2u) {
                            uint64_t nl = 0;
                            if (!ReadVarint64(tp, tsz, p, nl) || nl > tsz - p) break;
                            const uint8_t* np = tp + p; size_t nsz = static_cast<size_t>(nl); p += nsz;
                            uint64_t k = 0, v = 0; size_t q = 0;
                            while (q < nsz) {
                                uint64_t nt = 0;
                                if (!ReadVarint64(np, nsz, q, nt) || nt == 0) break;
                                if ((nt >> 3) == 1 && (nt & 7u) == 0u) ReadVarint64(np, nsz, q, k);
                                else if ((nt >> 3) == 2 && (nt & 7u) == 0u) ReadVarint64(np, nsz, q, v);
                                else if (!pbSkip(np, nsz, q, static_cast<uint8_t>(nt & 7u))) break;
                            }
                            outMap[static_cast<uint32_t>(v)] = static_cast<uint32_t>(k);
                        } else if (!pbSkip(tp, tsz, p, static_cast<uint8_t>(tag & 7u))) break;
                    }
                };

                size_t sp = 0;
                while (sp < mLen) {
                    uint64_t stag = 0;
                    if (!ReadVarint64(mPtr, mLen, sp, stag) || stag == 0) break;
                    const uint32_t sfn = static_cast<uint32_t>(stag >> 3);
                    const uint8_t swt = static_cast<uint8_t>(stag & 0x07u);
                    if (sfn == 6 && swt == 0u) { uint64_t v = 0; ReadVarint64(mPtr, mLen, sp, v); tm.numRows = static_cast<uint32_t>(v); }
                    else if (sfn == 7 && swt == 0u) { uint64_t v = 0; ReadVarint64(mPtr, mLen, sp, v); tm.numCols = static_cast<uint32_t>(v); }
                    else if (sfn == 8 && swt == 2u) {
                        uint64_t sl = 0;
                        if (!ReadVarint64(mPtr, mLen, sp, sl) || sl > mLen - sp) break;
                        tm.name = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(mPtr + sp), static_cast<size_t>(sl))));
                        sp += static_cast<size_t>(sl);
                    } else if (sfn == 4 && swt == 2u) {
                        // base_data_store (TST::TableDataStore)
                        uint64_t dl = 0;
                        if (!ReadVarint64(mPtr, mLen, sp, dl) || dl > mLen - sp) break;
                        const uint8_t* dsPtr = mPtr + sp;
                        const size_t dsSz = static_cast<size_t>(dl);
                        sp += dsSz;
                        size_t dp = 0;
                        while (dp < dsSz) {
                            uint64_t dtag = 0;
                            if (!ReadVarint64(dsPtr, dsSz, dp, dtag) || dtag == 0) break;
                            const uint32_t dfn = static_cast<uint32_t>(dtag >> 3);
                            const uint8_t dwt = static_cast<uint8_t>(dtag & 0x07u);
                            if (dwt == 2u && (dfn == 3 || dfn == 4 || dfn == 6 || dfn == 9 || dfn == 10 || dfn == 11 || dfn == 17)) {
                                uint64_t fl = 0;
                                if (!ReadVarint64(dsPtr, dsSz, dp, fl) || fl > dsSz - dp) break;
                                const uint8_t* fPtr = dsPtr + dp;
                                const size_t fSz = static_cast<size_t>(fl);
                                dp += fSz;
                                if (dfn == 4) tm.stringTableId = ReadTspReferenceId(fPtr, fSz);
                                else if (dfn == 6) tm.formulaTableId = ReadTspReferenceId(fPtr, fSz);
                                else if (dfn == 11) tm.formatTableId = ReadTspReferenceId(fPtr, fSz);
                                else if (dfn == 17) tm.richTextTableId = ReadTspReferenceId(fPtr, fSz);
                                else if (dfn == 9) parseRbTree(fPtr, fSz, tm.tileBaseRow);
                                else if (dfn == 10) parseRbTree(fPtr, fSz, tm.tileBaseCol);
                                else if (dfn == 3) {
                                    // TileStorage { repeated Tile tiles = 1; optional uint32 tile_size = 2; }
                                    size_t tsp = 0;
                                    while (tsp < fSz) {
                                        uint64_t tstag = 0;
                                        if (!ReadVarint64(fPtr, fSz, tsp, tstag) || tstag == 0) break;
                                        const uint32_t tsfn = static_cast<uint32_t>(tstag >> 3);
                                        const uint8_t tswt = static_cast<uint8_t>(tstag & 0x07u);
                                        if (tsfn == 2 && tswt == 0u) {
                                            uint64_t tszVal = 0; ReadVarint64(fPtr, fSz, tsp, tszVal);
                                            if (tszVal > 0 && tszVal <= 65536u) tm.tileSize = static_cast<uint32_t>(tszVal);
                                        } else if (tsfn == 1 && tswt == 2u) {
                                            uint64_t el = 0;
                                            if (!ReadVarint64(fPtr, fSz, tsp, el) || el > fSz - tsp) break;
                                            const uint8_t* tePtr = fPtr + tsp; size_t teSz = static_cast<size_t>(el); tsp += teSz;
                                            uint32_t tileId = 0; uint64_t tileRef = 0; size_t q = 0;
                                            while (q < teSz) {
                                                uint64_t tet = 0;
                                                if (!ReadVarint64(tePtr, teSz, q, tet) || tet == 0) break;
                                                if ((tet >> 3) == 1 && (tet & 7u) == 0u) { uint64_t v = 0; ReadVarint64(tePtr, teSz, q, v); tileId = static_cast<uint32_t>(v); }
                                                else if ((tet >> 3) == 2 && (tet & 7u) == 2u) {
                                                    uint64_t rl = 0;
                                                    if (!ReadVarint64(tePtr, teSz, q, rl) || rl > teSz - q) break;
                                                    tileRef = ReadTspReferenceId(tePtr + q, static_cast<size_t>(rl));
                                                    q += static_cast<size_t>(rl);
                                                } else if (!pbSkip(tePtr, teSz, q, static_cast<uint8_t>(tet & 7u))) break;
                                            }
                                            if (tileRef != 0) tm.tiles.push_back({ tileId, tileRef });
                                        } else if (!pbSkip(fPtr, fSz, tsp, tswt)) break;
                                    }
                                }
                            } else if (!pbSkip(dsPtr, dsSz, dp, dwt)) break;
                        }
                    } else if (!pbSkip(mPtr, mLen, sp, swt)) break;
                }
                if (!tm.tiles.empty() || !tm.name.empty()) ctx.tables.push_back(std::move(tm));
            }
        }
        if (!ok) break;
    }
}

inline void BuildIWorkNumbersGridSheets(const IWorkNumbersCtx& ctx, std::vector<SpreadsheetSheet>& outGridSheets) {
    constexpr int64_t kAppleEpoch2001 = 978307200LL;
    auto lookupString = [&](uint64_t tableId, uint32_t key) -> std::wstring {
        auto it = ctx.dataLists.find(tableId);
        if (it != ctx.dataLists.end()) {
            auto kit = it->second.find(key);
            if (kit != it->second.end()) return kit->second;
        }
        for (const auto& [id, mp] : ctx.dataLists) {
            (void)id;
            auto kit = mp.find(key);
            if (kit != mp.end()) return kit->second;
        }
        return {};
    };
    auto lookupRichText = [&](uint64_t tableId, uint32_t key) -> std::wstring {
        auto resolveRef = [&](uint64_t refId) -> std::wstring {
            auto sit = ctx.storageTexts.find(refId);
            if (sit != ctx.storageTexts.end()) return sit->second;
            auto pit = ctx.richPayloadToStorage.find(refId);
            if (pit != ctx.richPayloadToStorage.end()) {
                auto sit2 = ctx.storageTexts.find(pit->second);
                if (sit2 != ctx.storageTexts.end()) return sit2->second;
            }
            return {};
        };
        auto rit = ctx.dataListRefs.find(tableId);
        if (rit != ctx.dataListRefs.end()) {
            auto kit = rit->second.find(key);
            if (kit != rit->second.end()) {
                std::wstring s = resolveRef(kit->second);
                if (!s.empty()) return s;
            }
        }
        for (const auto& [id, mp] : ctx.dataListRefs) {
            (void)id;
            auto kit = mp.find(key);
            if (kit != mp.end()) {
                std::wstring s = resolveRef(kit->second);
                if (!s.empty()) return s;
            }
        }
        return lookupString(tableId, key);
    };

    size_t tblIdx = 0;
    for (const auto& tm : ctx.tables) {
        ++tblIdx;
        if (tm.tiles.empty()) continue;
        const uint32_t maxRowsCap = std::min<uint32_t>(tm.numRows > 0 ? tm.numRows : 2048u, 2048u);
        const uint32_t maxColsCap = std::min<uint32_t>(tm.numCols > 0 ? tm.numCols : 256u, 256u);
        std::vector<std::vector<std::wstring>> grid;
        size_t decodedCells = 0;

        for (size_t ti = 0; ti < tm.tiles.size(); ++ti) {
            const uint32_t tileId = tm.tiles[ti].first;
            const uint64_t tileRef = tm.tiles[ti].second;
            auto tit = ctx.tilesById.find(tileRef);
            if (tit == ctx.tilesById.end()) continue;
            const auto& tile = tit->second;

            uint32_t baseRow = 0, baseCol = 0;
            auto brit = tm.tileBaseRow.find(tileId);
            if (brit != tm.tileBaseRow.end()) baseRow = brit->second;
            else baseRow = static_cast<uint32_t>(ti) * tm.tileSize;
            auto bcit = tm.tileBaseCol.find(tileId);
            if (bcit != tm.tileBaseCol.end()) baseCol = bcit->second;

            for (const auto& rr : tile.rows) {
                const uint32_t absRow = baseRow + rr.tileRowIndex;
                if (absRow >= maxRowsCap) continue;
                const size_t nOffs = rr.cellOffsets.size() / 2u;
                const uint32_t rowVer = rr.storageVersion ? rr.storageVersion : tile.storageVersion;

                for (size_t ci = 0; ci < nOffs; ++ci) {
                    const uint32_t absCol = baseCol + static_cast<uint32_t>(ci);
                    if (absCol >= maxColsCap) break;
                    const uint16_t rawOff = RdU16LE(rr.cellOffsets.data() + ci * 2u);
                    if (rawOff == 0xFFFFu) continue;
                    const size_t byteOff = rr.hasWideOffsets ? (static_cast<size_t>(rawOff) * 4u) : static_cast<size_t>(rawOff);
                    if (byteOff + 8u > rr.cellStorage.size()) continue;

                    const uint8_t* c = rr.cellStorage.data() + byteOff;
                    const size_t rem = rr.cellStorage.size() - byteOff;
                    const uint8_t ver = c[0] ? c[0] : static_cast<uint8_t>(rowVer);
                    const uint8_t cellType = c[1];
                    std::wstring cellText;

                    if (ver == 5 && rem >= 12u) {
                        const uint32_t flags = RdU32LE(c + 4);
                        size_t p = 12u;
                        bool hasDec = false, hasDbl = false, hasDate = false, hasStr = false, hasRich = false;
                        std::wstring decStr;
                        double dblVal = 0.0, dateVal = 0.0;
                        uint32_t strKey = 0, richKey = 0;
                        if ((flags & 0x0001u) && p + 16u <= rem) { decStr = DecodeAppleBid128(c + p); p += 16u; hasDec = true; }
                        if ((flags & 0x0002u) && p + 8u <= rem)  { uint64_t b = RdU64LE(c + p); std::memcpy(&dblVal, &b, 8); p += 8u; hasDbl = true; }
                        if ((flags & 0x0004u) && p + 8u <= rem)  { uint64_t b = RdU64LE(c + p); std::memcpy(&dateVal, &b, 8); p += 8u; hasDate = true; }
                        if ((flags & 0x0008u) && p + 4u <= rem)  { strKey = RdU32LE(c + p); p += 4u; hasStr = true; }
                        if ((flags & 0x0010u) && p + 4u <= rem)  { richKey = RdU32LE(c + p); p += 4u; hasRich = true; }

                        if (cellType == 3 && hasStr) cellText = lookupString(tm.stringTableId, strKey);
                        else if (cellType == 9 && hasRich) cellText = lookupRichText(tm.richTextTableId, richKey);
                        else if (cellType == 6 && (hasDbl || hasDec)) cellText = (hasDbl ? (dblVal != 0.0) : (decStr != L"0")) ? L"TRUE" : L"FALSE";
                        else if (cellType == 5 && (hasDate || hasDbl)) {
                            double sec = hasDate ? dateVal : dblVal;
                            cellText = GeoSciDetail::FormatUtcEpoch(kAppleEpoch2001 + static_cast<int64_t>(std::llround(sec)));
                        } else if ((cellType == 2 || cellType == 10 || cellType == 7) && hasDec) {
                            cellText = decStr;
                        } else if (hasDbl) {
                            wchar_t dbuf[64] = {}; swprintf_s(dbuf, L"%.12g", dblVal); cellText = dbuf;
                        } else if (hasStr) {
                            cellText = lookupString(tm.stringTableId, strKey);
                        } else if (hasRich) {
                            cellText = lookupRichText(tm.richTextTableId, richKey);
                        }
                    } else if ((ver == 4 || ver == 3) && rem >= 8u) {
                        const uint8_t ct = (ver == 4) ? c[1] : ((c[1] == 3 && rem >= 12u) ? c[2] : c[1]);
                        const uint16_t flags = RdU16LE(c + ((c[1] == 3 && c[0] != 3) ? 4 : 2));
                        size_t p = 8u;
                        if (ver == 4) {
                            if (flags & 0x0002u) p += 4u; // cell style
                            if (flags & 0x0004u) p += 4u; // text style
                            if (flags & 0x0008u) p += 8u; // cond style + rule
                            if (flags & 0x0010u) p += 4u; // formula error
                            if (flags & 0x0020u) p += 4u; // formula key
                            double dblVal = 0.0, dateVal = 0.0;
                            uint32_t strKey = 0, richKey = 0;
                            bool hasDbl = false, hasStr = false, hasDate = false, hasRich = false;
                            if ((flags & 0x0040u) && p + 8u <= rem) { uint64_t b = RdU64LE(c + p); std::memcpy(&dblVal, &b, 8); p += 8u; hasDbl = true; }
                            if ((flags & 0x0080u) && p + 4u <= rem) { strKey = RdU32LE(c + p); p += 4u; hasStr = true; }
                            if ((flags & 0x0100u) && p + 8u <= rem) { uint64_t b = RdU64LE(c + p); std::memcpy(&dateVal, &b, 8); p += 8u; hasDate = true; }
                            if ((flags & 0x0200u) && p + 4u <= rem) { richKey = RdU32LE(c + p); p += 4u; hasRich = true; }

                            if (ct == 3 && hasStr) cellText = lookupString(tm.stringTableId, strKey);
                            else if (ct == 9 && hasRich) cellText = lookupRichText(tm.richTextTableId, richKey);
                            else if (ct == 6 && hasDbl) cellText = (dblVal != 0.0) ? L"TRUE" : L"FALSE";
                            else if (ct == 5 && (hasDate || hasDbl)) {
                                double sec = hasDate ? dateVal : dblVal;
                                cellText = GeoSciDetail::FormatUtcEpoch(kAppleEpoch2001 + static_cast<int64_t>(std::llround(sec)));
                            } else if (hasDbl) {
                                wchar_t dbuf[64] = {}; swprintf_s(dbuf, L"%.12g", dblVal); cellText = dbuf;
                            } else if (hasStr) {
                                cellText = lookupString(tm.stringTableId, strKey);
                            } else if (hasRich) {
                                cellText = lookupRichText(tm.richTextTableId, richKey);
                            }
                        } else if (rem >= 16u) {
                            // Classic v3 cell record fallback
                            if (ct == 3) cellText = lookupString(tm.stringTableId, RdU32LE(c + 12));
                            else if (ct == 9) cellText = lookupRichText(tm.richTextTableId, RdU32LE(c + 12));
                            else if (ct == 2 || ct == 6 || ct == 5) {
                                uint64_t b = RdU64LE(c + (rem >= 20u ? 12u : 8u));
                                double dv = 0.0; std::memcpy(&dv, &b, 8);
                                if (std::isfinite(dv)) {
                                    if (ct == 6) cellText = (dv != 0.0) ? L"TRUE" : L"FALSE";
                                    else if (ct == 5) cellText = GeoSciDetail::FormatUtcEpoch(kAppleEpoch2001 + static_cast<int64_t>(std::llround(dv)));
                                    else { wchar_t dbuf[64] = {}; swprintf_s(dbuf, L"%.12g", dv); cellText = dbuf; }
                                }
                            }
                        }
                    }

                    if (!cellText.empty()) {
                        if (grid.size() <= absRow) grid.resize(absRow + 1u);
                        if (grid[absRow].size() <= absCol) grid[absRow].resize(absCol + 1u);
                        grid[absRow][absCol] = std::move(cellText);
                        ++decodedCells;
                    }
                }
            }
        }

        if (decodedCells > 0 && !grid.empty()) {
            SpreadsheetSheet tsh;
            tsh.name = tm.name.empty() ? (L"Numbers_Table_" + std::to_wstring(tblIdx)) : tm.name;
            tsh.maxCols = 0;
            for (const auto& r : grid) tsh.maxCols = std::max(tsh.maxCols, r.size());
            tsh.rows = std::move(grid);
            outGridSheets.push_back(std::move(tsh));
        }
    }
}

inline bool ParseIWorkZipPackage(
    const std::vector<uint8_t>& raw,
    const wchar_t* formatTitle,
    ParsedOfficeDocument& out)
{
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    bool usedFile = false;
    if (!ListZipPackage(raw, out, entries, usedFile)) return false;

        SpreadsheetSheet sh;
        sh.name = L"Package_And_Text";
        sh.maxCols = 3;
        sh.rows.push_back({ L"Archive Stream", L"Object / Message Type", L"Extracted Content" });

        SpreadsheetSheet cellsSh;
        cellsSh.name = L"Numbers_Table_Data";
        cellsSh.maxCols = 3;
        cellsSh.rows.push_back({ L"Table / Stream", L"Entry Key", L"Cell Value" });

        IWorkNumbersCtx numCtx;
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
                if (!ExtractZipPackageEntry(raw, out, usedFile, e, rawIwa) || rawIwa.empty()) continue;
                if (!UnframeIwaStream(rawIwa, decomp)) continue;
                if (ParseIwaProtobufStream(decomp, e.pathInArchive, sh, cellsSh, validIwaFiles, validIwaObjects)) {
                    CollectIWorkNumbersProtobuf(decomp, numCtx);
                }
            }
            // iWork '09 XML format: index.xml / index.apxl (plain or gzip-compressed inside ZIP)
            else if (lp == L"index.xml" || lp == L"index.apxl" || lp == L"index.xml.gz" || lp == L"index.apxl.gz") {
                std::vector<uint8_t> xmlRaw, xmlBuf;
                if (!ExtractZipPackageEntry(raw, out, usedFile, e, xmlRaw) || xmlRaw.empty()) continue;
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

        std::vector<SpreadsheetSheet> gridSheets;
        BuildIWorkNumbersGridSheets(numCtx, gridSheets);

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = formatTitle;
        for (auto& gs : gridSheets) {
            out.sheets.push_back(std::move(gs));
        }
        out.sheets.push_back(std::move(sh));
        if (gridSheets.empty() && cellsSh.rows.size() > 1) {
            out.sheets.push_back(std::move(cellsSh));
        }
        out.valid = true;
        return true;
}
#endif // E2_DOC_FORMAT_CORE_ONLY
} // namespace DetailStructuredDataDoc

#ifndef E2_DOC_FORMAT_CORE_ONLY
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

inline bool ParseSbomJson(std::string_view text, ParsedOfficeDocument& out, bool wantCycloneDx) {
    SpreadsheetSheet sh = NewSbomSheet();
    size_t count = 0;
    JParser parser(text, StreamLimits());
    JVal root;
    root.kind = JVal::Kind::Object;
    bool sawComponents = false, sawPackages = false;
    bool complete = parser.StreamObject([&](const std::string& key) {
        if (wantCycloneDx && key == "components" && !sawComponents && parser.Peek() == '[') {
            sawComponents = true;
            return parser.StreamArray([&](JVal&& c) { CycloneDxJsonComponent(c, sh, count, 0); });
        }
        if (!wantCycloneDx && key == "packages" && !sawPackages && parser.Peek() == '[') {
            sawPackages = true;
            return parser.StreamArray([&](JVal&& p) { SpdxJsonPackage(p, sh, count); });
        }
        JVal v;
        if (!parser.ParseValue(v)) return false;
        root.objVal.emplace_back(key, std::move(v));
        return true;
    }) && parser.AtEnd();

    std::string bomFormat = root.GetString("bomFormat"), spdxVersion = root.GetString("spdxVersion");
    bool isCdx = bomFormat == "CycloneDX" || (bomFormat.empty() && spdxVersion.empty() && sawComponents && !complete);
    bool isSpdx = spdxVersion.rfind("SPDX-", 0) == 0 || (spdxVersion.empty() && bomFormat.empty() && sawPackages && !complete);
    if (wantCycloneDx) {
        if (!isCdx || isSpdx) return false;
    } else {
        if (!isSpdx || isCdx) return false;
    }
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

} // namespace DetailCloudSecOps


namespace DetailCsvTsv {

inline void DecodeDelimitedTextAndSepHint(
    const std::vector<uint8_t>& raw,
    wchar_t defaultDelim,
    std::wstring& text,
    wchar_t& delim,
    bool& delimFromHint)
{
    text.clear();
    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t wlen = (raw.size() - 2) / 2;
        text.resize(wlen);
        std::memcpy(text.data(), raw.data() + 2, wlen * 2);
    } else if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        size_t wlen = (raw.size() - 2) / 2;
        text.resize(wlen);
        for (size_t i = 0; i < wlen; ++i) text[i] = static_cast<wchar_t>((raw[2 + 2 * i] << 8) | raw[3 + 2 * i]);
    } else {
        size_t start = (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        text = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(raw.data() + start), raw.size() - start));
    }

    // Excel writes a "sep=;" first line to name the delimiter
    delim = defaultDelim;
    delimFromHint = false;
    if (text.size() >= 5 && (text.compare(0, 4, L"sep=") == 0 || text.compare(0, 4, L"SEP=") == 0) &&
        text[4] != L'\r' && text[4] != L'\n' &&
        (text.size() == 5 || text[5] == L'\r' || text[5] == L'\n')) {
        delim = text[4];
        delimFromHint = true;
        size_t eol = text.find(L'\n');
        text.erase(0, eol == std::wstring::npos ? text.size() : eol + 1);
    }
}

inline std::wstring FormatDelimitedTitle(wchar_t delim) {
    return (delim == L'\t') ? L"TSV Table (Tab-Separated)"
         : (delim == L';')  ? L"CSV Table (Semicolon ;)"
         : (delim == L',')  ? L"CSV Table (Comma ,)"
         : (delim == L'|')  ? L"CSV Table (Pipe |)"
         : L"CSV Table (Delimiter " + std::wstring(1, delim) + L")";
}

inline bool ParseDelimitedRowsToSheet(
    const std::wstring& text,
    wchar_t delim,
    const wchar_t* sheetName,
    ParsedOfficeDocument& out)
{
    SpreadsheetSheet sheet;
    sheet.name = sheetName;
    std::vector<std::wstring> curRow;
    std::wstring curCell;
    bool inQuotes = false;

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (inQuotes) {
            if (c == L'"') {
                if (i + 1 < text.size() && text[i + 1] == L'"') {
                    curCell.push_back(L'"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                curCell.push_back(c);
            }
        } else {
            if (c == L'"' && curCell.empty()) {
                inQuotes = true;
            } else if (c == delim) {
                curRow.push_back(std::move(curCell));
                curCell.clear();
            } else if (c == L'\r' || c == L'\n') {
                if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                curRow.push_back(std::move(curCell));
                curCell.clear();
                if (!curRow.empty() && !(curRow.size() == 1 && curRow[0].empty())) {
                    sheet.maxCols = std::max(sheet.maxCols, curRow.size());
                    sheet.rows.push_back(std::move(curRow));
                }
                curRow.clear();
                if (sheet.rows.size() >= 10000) break;
            } else {
                curCell.push_back(c);
            }
        }
    }
    if (!curCell.empty() || !curRow.empty()) {
        curRow.push_back(std::move(curCell));
        sheet.maxCols = std::max(sheet.maxCols, curRow.size());
        sheet.rows.push_back(std::move(curRow));
    }

    if (sheet.rows.empty()) {
        sheet.rows.push_back({ L"" });
        sheet.maxCols = 1;
    }
    out.sheets.push_back(std::move(sheet));
    out.valid = true;
    return true;
}

} // namespace DetailCsvTsv

inline std::wstring NormalizeZipArchiveKey(std::wstring_view sv) {
    std::wstring r;
    r.reserve(sv.size());
    for (wchar_t ch : sv) {
        if (ch == L'\\') ch = L'/';
        if (r.empty() && ch == L'/') continue;
        if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
        r.push_back(ch);
    }
    return r;
}

struct ZipPackageReader {
    std::wstring filePath;
    const uint8_t* memData = nullptr;
    size_t memLen = 0;
    bool fromDisk = false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;

    bool Open(const std::wstring& fullPath, const std::vector<uint8_t>& raw) {
        entries.clear();
        if (!fullPath.empty() && ZipDeflate::ListZipEntriesFromFile(fullPath, entries) && !entries.empty()) {
            filePath = fullPath;
            fromDisk = true;
            return true;
        }
        if (!raw.empty() && ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries) && !entries.empty()) {
            memData = raw.data();
            memLen = raw.size();
            fromDisk = false;
            return true;
        }
        return false;
    }

    const ZipDeflate::ArchiveItemMeta* FindEntry(std::wstring_view wantNormPath) const {
        std::wstring want = NormalizeZipArchiveKey(wantNormPath);
        for (const auto& e : entries) {
            if (!e.isDirectory && NormalizeZipArchiveKey(e.pathInArchive) == want) return &e;
        }
        return nullptr;
    }

    bool ExtractEntry(const ZipDeflate::ArchiveItemMeta& e, std::vector<uint8_t>& outBytes, size_t maxBytes = 64u * 1024u * 1024u) const {
        outBytes.clear();
        if (fromDisk) return ZipDeflate::ExtractZipEntryFromFile(filePath, e, outBytes, maxBytes);
        return ZipDeflate::ExtractZipEntryFromMemory(memData, memLen, e, outBytes, maxBytes);
    }

    bool ExtractByPath(std::wstring_view wantPath, std::vector<uint8_t>& outBytes, size_t maxBytes = 64u * 1024u * 1024u) const {
        const auto* e = FindEntry(wantPath);
        return e ? ExtractEntry(*e, outBytes, maxBytes) : false;
    }
};
#endif // E2_DOC_FORMAT_CORE_ONLY

} // namespace Formats
