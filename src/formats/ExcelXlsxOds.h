#pragma once

namespace DetailExcelOds {

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

inline bool EndsWithWideCI(std::wstring_view s, std::wstring_view suffix) {
    if (s.size() < suffix.size()) return false;
    size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        wchar_t ca = s[off + i];
        wchar_t cb = suffix[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca + (L'a' - L'A'));
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb + (L'a' - L'A'));
        if (ca != cb) return false;
    }
    return true;
}

inline std::string_view TrimAsciiSV(std::string_view s) { return ::Explorer2::PathUtils::TrimAscii(s); }

inline int HexValChar(wchar_t c) {
    if (c >= L'0' && c <= L'9') return static_cast<int>(c - L'0');
    if (c >= L'A' && c <= L'F') return static_cast<int>(c - L'A' + 10);
    if (c >= L'a' && c <= L'f') return static_cast<int>(c - L'a' + 10);
    return -1;
}

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

inline void CivilFromDays1970(int64_t z, int& outY, int& outM, int& outD) {
    z += 719468LL;
    const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097LL);
    const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400LL;
    const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const uint32_t mp = (5u * doy + 2u) / 153u;
    const uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    const uint32_t m = (mp < 10u) ? (mp + 3u) : (mp - 9u);
    outY = static_cast<int>(y + (m <= 2u ? 1 : 0));
    outM = static_cast<int>(m);
    outD = static_cast<int>(d);
}

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

static bool ParseExcelXlsx(const std::wstring& fullPath, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::ExcelXlsx;
    out.isSpreadsheet = true;
    out.formatTitle = DetailExcelOds::EndsWithWideCI(fullPath, L".xlsm")
        ? L"Microsoft Excel Macro-Enabled Workbook (.xlsm)"
        : L"Microsoft Excel Workbook (.xlsx)";

    std::vector<ZipDeflate::ArchiveItemMeta> zipEntries;
    if (!ZipDeflate::ListZipEntriesFromFile(fullPath, zipEntries) || zipEntries.empty()) {
        return false;
    }

    // ------------------------------------------------------------------------
    // 1. Parse xl/_rels/workbook.xml.rels (Relationship Id -> Target + Type)
    // ------------------------------------------------------------------------
    struct OoxmlRelEntry {
        std::string id;
        std::wstring target;
        std::string type;
    };
    std::vector<OoxmlRelEntry> wbRels;
    std::wstring sharedStringsPath = L"xl/sharedStrings.xml";
    std::wstring stylesPath = L"xl/styles.xml";

    std::vector<uint8_t> relsBytes;
    if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"xl/_rels/workbook.xml.rels", relsBytes)) {
        std::string_view rsv(reinterpret_cast<const char*>(relsBytes.data()), relsBytes.size());
        size_t pos = 0;
        size_t tStart = 0, tGt = 0;
        bool selfClose = false;
        while (DetailExcelOds::FindXmlElemOpen(rsv, "Relationship", pos, tStart, tGt, selfClose)) {
            std::string_view tag = rsv.substr(tStart, tGt - tStart + 1);
            std::string_view idSv, tgtSv, typeSv;
            if (DetailExcelOds::GetXmlAttr(tag, "Id", idSv) && DetailExcelOds::GetXmlAttr(tag, "Target", tgtSv)) {
                DetailExcelOds::GetXmlAttr(tag, "Type", typeSv);
                std::wstring resolved = DetailExcelOds::NormalizeOoxmlTarget(tgtSv, "xl");
                if (typeSv.find("/sharedStrings") != std::string_view::npos && !resolved.empty()) {
                    sharedStringsPath = resolved;
                } else if (typeSv.find("/styles") != std::string_view::npos && !resolved.empty()) {
                    stylesPath = resolved;
                }
                wbRels.push_back({ std::string(idSv), std::move(resolved), std::string(typeSv) });
            }
            pos = tGt + 1;
        }
    }

    // ------------------------------------------------------------------------
    // 2. Parse xl/workbook.xml (date1904 + ordered <sheet name="..." r:id="..."/>)
    // ------------------------------------------------------------------------
    struct WorkbookSheetRef {
        std::wstring name;
        std::wstring zipPath;
        bool isNonWorksheet = false;
    };
    std::vector<WorkbookSheetRef> sheetRefs;
    bool is1904DateSystem = false;
    bool hasWorkbookXml = false;

    std::vector<uint8_t> wbBytes;
    if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"xl/workbook.xml", wbBytes)) {
        hasWorkbookXml = true;
        std::string_view wbsv(reinterpret_cast<const char*>(wbBytes.data()), wbBytes.size());

        size_t prStart = 0, prGt = 0;
        bool prSelf = false;
        if (DetailExcelOds::FindXmlElemOpen(wbsv, "workbookPr", 0, prStart, prGt, prSelf)) {
            std::string_view prTag = wbsv.substr(prStart, prGt - prStart + 1);
            std::string_view d1904Sv;
            if (DetailExcelOds::GetXmlAttr(prTag, "date1904", d1904Sv)) {
                d1904Sv = DetailExcelOds::TrimAsciiSV(d1904Sv);
                is1904DateSystem = (d1904Sv == "1" || DetailExcelOds::AsciiEqCI(d1904Sv, "true"));
            }
        }

        size_t pos = 0, sStart = 0, sGt = 0;
        bool sSelf = false;
        int ordIdx = 1;
        while (DetailExcelOds::FindXmlElemOpen(wbsv, "sheet", pos, sStart, sGt, sSelf) && sheetRefs.size() < 128) {
            std::string_view sTag = wbsv.substr(sStart, sGt - sStart + 1);
            std::string_view nameSv, ridSv;
            std::wstring shName = DetailExcelOds::GetXmlAttr(sTag, "name", nameSv)
                ? DetailExcelOds::DecodeSpreadsheetXml(nameSv, true)
                : (L"Sheet" + std::to_wstring(ordIdx));

            bool gotRid = DetailExcelOds::GetXmlAttr(sTag, "r:id", ridSv);
            if (!gotRid) gotRid = DetailExcelOds::GetXmlAttr(sTag, "id", ridSv, true);

            std::wstring targetPath;
            bool nonWs = false;
            if (gotRid && !ridSv.empty()) {
                for (const auto& rel : wbRels) {
                    if (rel.id == ridSv) {
                        targetPath = rel.target;
                        if (rel.type.find("chartsheet") != std::string::npos ||
                            rel.type.find("macrosheet") != std::string::npos ||
                            rel.type.find("dialogsheet") != std::string::npos)
                        {
                            nonWs = true;
                        }
                        break;
                    }
                }
            }
            if (targetPath.empty()) {
                targetPath = L"xl/worksheets/sheet" + std::to_wstring(ordIdx) + L".xml";
            }
            sheetRefs.push_back({ std::move(shName), std::move(targetPath), nonWs });
            ++ordIdx;
            pos = sGt + 1;
        }
    }

    if (!hasWorkbookXml || sheetRefs.empty()) {
        // Fallback ONLY if workbook.xml wasn't present or listed zero sheets
        for (int sIdx = 1; sIdx <= 16; ++sIdx) {
            std::wstring fbPath = L"xl/worksheets/sheet" + std::to_wstring(sIdx) + L".xml";
            std::vector<uint8_t> probe;
            if (!DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, fbPath, probe)) {
                if (sIdx == 1) continue;
                break;
            }
            sheetRefs.push_back({ L"Sheet" + std::to_wstring(sIdx), std::move(fbPath), false });
        }
    }

    // ------------------------------------------------------------------------
    // 3. Parse Shared Strings & Styles if OpenXML sheets are present
    // ------------------------------------------------------------------------
    if (!sheetRefs.empty()) {
        std::vector<std::wstring> sharedStrings;
        std::vector<uint8_t> ssBytes;
        if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, sharedStringsPath, ssBytes)) {
            std::string_view ssv(reinterpret_cast<const char*>(ssBytes.data()), ssBytes.size());
            size_t pos = 0, siStart = 0, siGt = 0;
            bool siSelf = false;
            while (DetailExcelOds::FindXmlElemOpen(ssv, "si", pos, siStart, siGt, siSelf)) {
                if (siSelf) {
                    sharedStrings.emplace_back();
                    pos = siGt + 1;
                } else {
                    size_t bodyEnd = 0, afterClose = 0;
                    if (!DetailExcelOds::FindXmlElemClose(ssv, "si", siGt + 1, bodyEnd, afterClose)) break;
                    sharedStrings.push_back(DetailExcelOds::ExtractOoxmlStringItemText(ssv.substr(siGt + 1, bodyEnd - (siGt + 1))));
                    pos = afterClose;
                }
            }
        }

        // Parse xl/styles.xml (<numFmts>, <cellStyleXfs>, <cellXfs>)
        struct CustomNumFmt {
            uint32_t id = 0;
            std::string code;
        };
        std::vector<CustomNumFmt> customFmts;
        std::vector<uint32_t> styleXfFmtIds;
        std::vector<DetailExcelOds::ExcelStyleFmt> cellXfs;

        std::vector<uint8_t> stBytes;
        if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, stylesPath, stBytes)) {
            std::string_view stsv(reinterpret_cast<const char*>(stBytes.data()), stBytes.size());

            size_t nfOpen = 0, nfGt = 0;
            bool nfSelf = false;
            if (DetailExcelOds::FindXmlElemOpen(stsv, "numFmts", 0, nfOpen, nfGt, nfSelf) && !nfSelf) {
                size_t nfEnd = 0, nfAfter = 0;
                if (DetailExcelOds::FindXmlElemClose(stsv, "numFmts", nfGt + 1, nfEnd, nfAfter)) {
                    std::string_view nfBlk = stsv.substr(nfGt + 1, nfEnd - (nfGt + 1));
                    size_t p = 0, fOpen = 0, fGt = 0;
                    bool fSelf = false;
                    while (DetailExcelOds::FindXmlElemOpen(nfBlk, "numFmt", p, fOpen, fGt, fSelf)) {
                        std::string_view fTag = nfBlk.substr(fOpen, fGt - fOpen + 1);
                        std::string_view idSv, codeSv;
                        if (DetailExcelOds::GetXmlAttr(fTag, "numFmtId", idSv) && DetailExcelOds::GetXmlAttr(fTag, "formatCode", codeSv)) {
                            uint32_t id = static_cast<uint32_t>(DetailExcelOds::ParseIntSV(idSv, 0));
                            customFmts.push_back({ id, std::string(codeSv) });
                        }
                        p = fGt + 1;
                    }
                }
            }

            size_t csxOpen = 0, csxGt = 0;
            bool csxSelf = false;
            if (DetailExcelOds::FindXmlElemOpen(stsv, "cellStyleXfs", 0, csxOpen, csxGt, csxSelf) && !csxSelf) {
                size_t csxEnd = 0, csxAfter = 0;
                if (DetailExcelOds::FindXmlElemClose(stsv, "cellStyleXfs", csxGt + 1, csxEnd, csxAfter)) {
                    std::string_view csxBlk = stsv.substr(csxGt + 1, csxEnd - (csxGt + 1));
                    size_t p = 0, xOpen = 0, xGt = 0;
                    bool xSelf = false;
                    while (DetailExcelOds::FindXmlElemOpen(csxBlk, "xf", p, xOpen, xGt, xSelf)) {
                        std::string_view xTag = csxBlk.substr(xOpen, xGt - xOpen + 1);
                        std::string_view idSv;
                        uint32_t fid = DetailExcelOds::GetXmlAttr(xTag, "numFmtId", idSv)
                            ? static_cast<uint32_t>(DetailExcelOds::ParseIntSV(idSv, 0)) : 0u;
                        styleXfFmtIds.push_back(fid);
                        if (xSelf) {
                            p = xGt + 1;
                        } else {
                            size_t xe = 0, xa = 0;
                            p = DetailExcelOds::FindXmlElemClose(csxBlk, "xf", xGt + 1, xe, xa) ? xa : (xGt + 1);
                        }
                    }
                }
            }

            size_t cxOpen = 0, cxGt = 0;
            bool cxSelf = false;
            if (DetailExcelOds::FindXmlElemOpen(stsv, "cellXfs", 0, cxOpen, cxGt, cxSelf) && !cxSelf) {
                size_t cxEnd = 0, cxAfter = 0;
                if (DetailExcelOds::FindXmlElemClose(stsv, "cellXfs", cxGt + 1, cxEnd, cxAfter)) {
                    std::string_view cxBlk = stsv.substr(cxGt + 1, cxEnd - (cxGt + 1));
                    size_t p = 0, xOpen = 0, xGt = 0;
                    bool xSelf = false;
                    while (DetailExcelOds::FindXmlElemOpen(cxBlk, "xf", p, xOpen, xGt, xSelf)) {
                        std::string_view xTag = cxBlk.substr(xOpen, xGt - xOpen + 1);
                        std::string_view idSv, applySv, xfIdSv;
                        uint32_t fid = DetailExcelOds::GetXmlAttr(xTag, "numFmtId", idSv)
                            ? static_cast<uint32_t>(DetailExcelOds::ParseIntSV(idSv, 0)) : 0u;
                        if (DetailExcelOds::GetXmlAttr(xTag, "applyNumberFormat", applySv)) {
                            std::string_view ap = DetailExcelOds::TrimAsciiSV(applySv);
                            if ((ap == "0" || DetailExcelOds::AsciiEqCI(ap, "false")) && fid == 0u &&
                                DetailExcelOds::GetXmlAttr(xTag, "xfId", xfIdSv))
                            {
                                size_t baseIdx = static_cast<size_t>(DetailExcelOds::ParseIntSV(xfIdSv, -1));
                                if (baseIdx < styleXfFmtIds.size()) fid = styleXfFmtIds[baseIdx];
                            }
                        }
                        std::string_view customCode;
                        bool hasCustom = false;
                        for (const auto& cf : customFmts) {
                            if (cf.id == fid) {
                                customCode = cf.code;
                                hasCustom = true;
                                break;
                            }
                        }
                        cellXfs.push_back(DetailExcelOds::ClassifyExcelNumberFormat(fid, customCode, hasCustom));
                        if (xSelf) {
                            p = xGt + 1;
                        } else {
                            size_t xe = 0, xa = 0;
                            p = DetailExcelOds::FindXmlElemClose(cxBlk, "xf", xGt + 1, xe, xa) ? xa : (xGt + 1);
                        }
                    }
                }
            }
        }

        // --------------------------------------------------------------------
        // 4. Parse Worksheets
        // --------------------------------------------------------------------
        constexpr int kMaxExcelCols = 512;
        constexpr size_t kMaxSheetRows = 10000;
        constexpr int64_t kMaxGapRowsPerJump = 64;

        for (const auto& sRef : sheetRefs) {
            std::vector<uint8_t> shBytes;
            if (!DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, sRef.zipPath, shBytes)) {
                continue;
            }
            if (sRef.isNonWorksheet && shBytes.empty()) continue;

            SpreadsheetSheet sheet;
            sheet.name = sRef.name;

            std::string_view sv(reinterpret_cast<const char*>(shBytes.data()), shBytes.size());
            size_t rSearch = 0, rStart = 0, rGt = 0;
            bool rSelf = false;
            int64_t expectedNextRow1 = 1;

            while (DetailExcelOds::FindXmlElemOpen(sv, "row", rSearch, rStart, rGt, rSelf)) {
                std::string_view rTag = sv.substr(rStart, rGt - rStart + 1);
                std::string_view rowNumSv;
                int64_t rowNum1 = expectedNextRow1;
                if (DetailExcelOds::GetXmlAttr(rTag, "r", rowNumSv)) {
                    int64_t parsedR = DetailExcelOds::ParseIntSV(rowNumSv, expectedNextRow1);
                    if (parsedR >= 1) rowNum1 = parsedR;
                }

                if (rSelf) {
                    rSearch = rGt + 1;
                    continue;
                }
                size_t endRow = 0, afterRow = 0;
                if (!DetailExcelOds::FindXmlElemClose(sv, "row", rGt + 1, endRow, afterRow)) break;
                std::string_view rowBlk = sv.substr(rGt + 1, endRow - (rGt + 1));

                std::vector<std::wstring> rowCells;
                size_t cSearch = 0, cStart = 0, cGt = 0;
                bool cSelf = false;
                int nextSequentialCol = 0;

                while (DetailExcelOds::FindXmlElemOpen(rowBlk, "c", cSearch, cStart, cGt, cSelf)) {
                    std::string_view cTag = rowBlk.substr(cStart, cGt - cStart + 1);
                    size_t endC = cGt, afterC = cGt + 1;
                    if (!cSelf && !DetailExcelOds::FindXmlElemClose(rowBlk, "c", cGt + 1, endC, afterC)) break;
                    std::string_view cBody = cSelf ? std::string_view{} : rowBlk.substr(cGt + 1, endC - (cGt + 1));

                    int colIdx = nextSequentialCol;
                    std::string_view refSv;
                    if (DetailExcelOds::GetXmlAttr(cTag, "r", refSv) && !refSv.empty()) {
                        // Strip '$' absolute markers if present before passing to ExcelCellRefToColIndex
                        std::string cleanRef;
                        cleanRef.reserve(refSv.size());
                        for (char rc : refSv) {
                            if (rc != '$') cleanRef.push_back(rc);
                        }
                        if (!cleanRef.empty()) {
                            colIdx = ExcelCellRefToColIndex(cleanRef);
                        }
                    }
                    nextSequentialCol = colIdx + 1;

                    std::string_view tSv, sSv;
                    DetailExcelOds::GetXmlAttr(cTag, "t", tSv);
                    tSv = DetailExcelOds::TrimAsciiSV(tSv);
                    int64_t styleIdx = DetailExcelOds::GetXmlAttr(cTag, "s", sSv)
                        ? DetailExcelOds::ParseIntSV(sSv, -1) : -1;

                    std::wstring cellVal;
                    bool hasValueElem = false;

                    if (tSv == "inlineStr") {
                        size_t isOpen = 0, isGt = 0;
                        bool isSelf = false;
                        if (DetailExcelOds::FindXmlElemOpen(cBody, "is", 0, isOpen, isGt, isSelf) && !isSelf) {
                            size_t isEnd = 0, isAfter = 0;
                            if (DetailExcelOds::FindXmlElemClose(cBody, "is", isGt + 1, isEnd, isAfter)) {
                                cellVal = DetailExcelOds::ExtractOoxmlStringItemText(cBody.substr(isGt + 1, isEnd - (isGt + 1)));
                                hasValueElem = true;
                            }
                        } else {
                            cellVal = DetailExcelOds::ExtractOoxmlStringItemText(cBody);
                            hasValueElem = !cellVal.empty();
                        }
                    } else {
                        size_t vOpen = 0, vGt = 0;
                        bool vSelf = false;
                        if (DetailExcelOds::FindXmlElemOpen(cBody, "v", 0, vOpen, vGt, vSelf) && !vSelf) {
                            size_t vEnd = 0, vAfter = 0;
                            if (DetailExcelOds::FindXmlElemClose(cBody, "v", vGt + 1, vEnd, vAfter)) {
                                hasValueElem = true;
                                std::string_view rawV = cBody.substr(vGt + 1, vEnd - (vGt + 1));
                                if (tSv == "s") {
                                    int64_t ssIdx = DetailExcelOds::ParseIntSV(rawV, -1);
                                    if (ssIdx >= 0 && static_cast<size_t>(ssIdx) < sharedStrings.size()) {
                                        cellVal = sharedStrings[static_cast<size_t>(ssIdx)];
                                    }
                                } else if (tSv == "str") {
                                    cellVal = DetailExcelOds::DecodeSpreadsheetXml(rawV, true);
                                } else if (tSv == "b") {
                                    std::string_view bv = DetailExcelOds::TrimAsciiSV(rawV);
                                    cellVal = (bv == "1" || DetailExcelOds::AsciiEqCI(bv, "true")) ? L"TRUE" : L"FALSE";
                                } else if (tSv == "e") {
                                    cellVal = DetailExcelOds::DecodeSpreadsheetXml(DetailExcelOds::TrimAsciiSV(rawV), false);
                                } else if (tSv == "d") {
                                    cellVal = DetailExcelOds::DecodeSpreadsheetXml(DetailExcelOds::TrimAsciiSV(rawV), false);
                                } else {
                                    // t="n" or omitted t -> numeric cell formatted via styles.xml
                                    DetailExcelOds::ExcelStyleFmt fmt{};
                                    if (styleIdx >= 0 && static_cast<size_t>(styleIdx) < cellXfs.size()) {
                                        fmt = cellXfs[static_cast<size_t>(styleIdx)];
                                    }
                                    cellVal = DetailExcelOds::FormatExcelNumericCell(rawV, fmt, is1904DateSystem);
                                }
                            }
                        }
                    }

                    // Formula fallback when <v> is absent on a formula cell (<f>...</f>)
                    if (!hasValueElem && cellVal.empty() && !cBody.empty()) {
                        size_t fOpen = 0, fGt = 0;
                        bool fSelf = false;
                        if (DetailExcelOds::FindXmlElemOpen(cBody, "f", 0, fOpen, fGt, fSelf) && !fSelf) {
                            size_t fEnd = 0, fAfter = 0;
                            if (DetailExcelOds::FindXmlElemClose(cBody, "f", fGt + 1, fEnd, fAfter)) {
                                std::string_view fRaw = DetailExcelOds::TrimAsciiSV(cBody.substr(fGt + 1, fEnd - (fGt + 1)));
                                if (!fRaw.empty()) {
                                    std::wstring fDec = DetailExcelOds::DecodeSpreadsheetXml(fRaw, false);
                                    if (!fDec.empty()) {
                                        cellVal = (fDec.front() == L'=') ? std::move(fDec) : (L"=" + fDec);
                                    }
                                }
                            }
                        }
                    }

                    if (!cellVal.empty() && colIdx >= 0 && colIdx < kMaxExcelCols) {
                        if (static_cast<size_t>(colIdx) >= rowCells.size()) {
                            rowCells.resize(static_cast<size_t>(colIdx) + 1);
                        }
                        rowCells[static_cast<size_t>(colIdx)] = std::move(cellVal);
                    }
                    cSearch = afterC;
                }

                if (!rowCells.empty()) {
                    if (rowNum1 > expectedNextRow1) {
                        int64_t gap = rowNum1 - expectedNextRow1;
                        if (gap > kMaxGapRowsPerJump) gap = kMaxGapRowsPerJump;
                        for (int64_t g = 0; g < gap && sheet.rows.size() < kMaxSheetRows; ++g) {
                            sheet.rows.emplace_back();
                        }
                    }
                    if (sheet.rows.size() < kMaxSheetRows) {
                        sheet.maxCols = std::max(sheet.maxCols, rowCells.size());
                        sheet.rows.push_back(std::move(rowCells));
                    }
                    expectedNextRow1 = rowNum1 + 1;
                }

                rSearch = afterRow;
                if (sheet.rows.size() >= kMaxSheetRows) break;
            }

            if (!sheet.rows.empty()) {
                out.sheets.push_back(std::move(sheet));
            }
        }
    }

    // ------------------------------------------------------------------------
    // 5. OASIS OpenDocument Spreadsheet (.ods) via content.xml
    // ------------------------------------------------------------------------
    if (out.sheets.empty()) {
        std::vector<uint8_t> odsBytes;
        if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"content.xml", odsBytes)) {
            std::string_view sv(reinterpret_cast<const char*>(odsBytes.data()), odsBytes.size());

            // Guard against non-spreadsheet ODF documents (e.g. .odt text documents containing a table)
            bool hasOdfSpreadsheet = (sv.find("<office:spreadsheet") != std::string_view::npos ||
                                      sv.find(":spreadsheet") != std::string_view::npos);
            bool hasOdfTextBody = (sv.find("<office:text>") != std::string_view::npos ||
                                   sv.find("<office:text ") != std::string_view::npos);
            if (hasOdfSpreadsheet || !hasOdfTextBody) {
                std::vector<uint8_t> mimeBytes;
                if (DetailExcelOds::ExtractZipEntryFlex(fullPath, zipEntries, L"mimetype", mimeBytes)) {
                    std::string_view msv = DetailExcelOds::TrimAsciiSV(
                        std::string_view(reinterpret_cast<const char*>(mimeBytes.data()), mimeBytes.size()));
                    if (!msv.empty() && msv.find("opendocument.spreadsheet") == std::string_view::npos) {
                        out.valid = false;
                        return false;
                    }
                }

                out.formatTitle = L"OpenDocument Spreadsheet (.ods)";
                constexpr size_t kMaxOdsCols = 512;
                constexpr size_t kMaxOdsRows = 10000;
                constexpr size_t kMaxOdsGapRows = 64;
                constexpr size_t kMaxNonEmptyRowRepeat = 256;

                size_t tSearch = 0, tStart = 0, tGt = 0;
                bool tSelf = false;
                int tblIdx = 1;

                while (DetailExcelOds::FindXmlElemOpen(sv, "table", tSearch, tStart, tGt, tSelf) && out.sheets.size() < 128) {
                    std::string_view tblTag = sv.substr(tStart, tGt - tStart + 1);
                    if (tSelf) {
                        tSearch = tGt + 1;
                        continue;
                    }
                    size_t endTbl = 0, afterTbl = 0;
                    if (!DetailExcelOds::FindXmlElemClose(sv, "table", tGt + 1, endTbl, afterTbl)) break;
                    std::string_view tblBlk = sv.substr(tGt + 1, endTbl - (tGt + 1));

                    SpreadsheetSheet sheet;
                    std::string_view nmSv;
                    sheet.name = DetailExcelOds::GetXmlAttr(tblTag, "table:name", nmSv, true)
                        ? DetailExcelOds::DecodeSpreadsheetXml(nmSv, false)
                        : (L"Sheet" + std::to_wstring(tblIdx));
                    ++tblIdx;

                    size_t pendingEmptyRows = 0;
                    size_t rSearch = 0, rStart = 0, rGt = 0;
                    bool rSelf = false;

                    while (DetailExcelOds::FindXmlElemOpen(tblBlk, "table-row", rSearch, rStart, rGt, rSelf)) {
                        std::string_view rTag = tblBlk.substr(rStart, rGt - rStart + 1);
                        std::string_view rRepSv;
                        size_t rowRepeat = 1;
                        if (DetailExcelOds::GetXmlAttr(rTag, "table:number-rows-repeated", rRepSv, true)) {
                            int64_t rr = DetailExcelOds::ParseIntSV(rRepSv, 1);
                            if (rr > 1) rowRepeat = static_cast<size_t>(rr > 1048576LL ? 1048576LL : rr);
                        }

                        if (rSelf) {
                            pendingEmptyRows = std::min<size_t>(pendingEmptyRows + rowRepeat, 1048576u);
                            rSearch = rGt + 1;
                            continue;
                        }
                        size_t endRow = 0, afterRow = 0;
                        if (!DetailExcelOds::FindXmlElemClose(tblBlk, "table-row", rGt + 1, endRow, afterRow)) break;
                        std::string_view rowBlk = tblBlk.substr(rGt + 1, endRow - (rGt + 1));

                        std::vector<std::wstring> rowCells;
                        size_t pendingEmptyCols = 0;

                        // Walk <table:table-cell> and <table:covered-table-cell> in exact document order
                        size_t cPos = 0;
                        while (cPos < rowBlk.size()) {
                            size_t lt = rowBlk.find('<', cPos);
                            if (lt == std::string_view::npos || lt + 1 >= rowBlk.size()) break;
                            char next = rowBlk[lt + 1];
                            if (next == '!' || next == '?' || next == '/') {
                                if (next == '!' && lt + 3 < rowBlk.size() && rowBlk[lt + 2] == '-' && rowBlk[lt + 3] == '-') {
                                    size_t endCmt = rowBlk.find("-->", lt + 4);
                                    if (endCmt == std::string_view::npos) break;
                                    cPos = endCmt + 3;
                                    continue;
                                }
                                size_t gt = rowBlk.find('>', lt + 1);
                                if (gt == std::string_view::npos) break;
                                cPos = gt + 1;
                                continue;
                            }

                            size_t nameEnd = lt + 1;
                            while (nameEnd < rowBlk.size() && !DetailExcelOds::IsTagNameStop(rowBlk[nameEnd])) ++nameEnd;
                            std::string_view local = DetailExcelOds::StripXmlNsPrefix(rowBlk.substr(lt + 1, nameEnd - (lt + 1)));
                            size_t cGt = DetailExcelOds::FindTagCloseGt(rowBlk, lt);
                            if (cGt == std::string_view::npos) break;

                            if (local != "table-cell" && local != "covered-table-cell") {
                                cPos = cGt + 1;
                                continue;
                            }

                            std::string_view cTag = rowBlk.substr(lt, cGt - lt + 1);
                            size_t prev = cGt;
                            while (prev > lt + 1 && (rowBlk[prev - 1] == ' ' || rowBlk[prev - 1] == '\t' || rowBlk[prev - 1] == '\r' || rowBlk[prev - 1] == '\n')) --prev;
                            bool cSelf = (prev > lt + 1 && rowBlk[prev - 1] == '/');

                            size_t endC = cGt, afterC = cGt + 1;
                            if (!cSelf && !DetailExcelOds::FindXmlElemClose(rowBlk, local, cGt + 1, endC, afterC)) break;
                            std::string_view cBody = cSelf ? std::string_view{} : rowBlk.substr(cGt + 1, endC - (cGt + 1));

                            std::string_view cRepSv;
                            size_t colRepeat = 1;
                            if (DetailExcelOds::GetXmlAttr(cTag, "table:number-columns-repeated", cRepSv, true)) {
                                int64_t cr = DetailExcelOds::ParseIntSV(cRepSv, 1);
                                if (cr > 1) colRepeat = static_cast<size_t>(cr > 65536LL ? 65536LL : cr);
                            }

                            std::wstring cellText;
                            if (local == "table-cell") {
                                cellText = DetailExcelOds::ExtractOdfCellText(cTag, cBody);
                            }

                            // CRITICAL TRAILING-REPEAT GUARD:
                            // Only flush pendingEmptyCols when a subsequent non-empty cell appears in the same row!
                            if (cellText.empty()) {
                                pendingEmptyCols = std::min<size_t>(pendingEmptyCols + colRepeat, 65536u);
                            } else {
                                while (pendingEmptyCols > 0 && rowCells.size() < kMaxOdsCols) {
                                    rowCells.emplace_back();
                                    --pendingEmptyCols;
                                }
                                pendingEmptyCols = 0;
                                size_t avail = (rowCells.size() < kMaxOdsCols) ? (kMaxOdsCols - rowCells.size()) : 0u;
                                size_t emitCnt = std::min<size_t>(colRepeat, avail);
                                for (size_t k = 0; k < emitCnt; ++k) {
                                    rowCells.push_back(cellText);
                                }
                            }
                            cPos = afterC;
                        }

                        // CRITICAL TRAILING-ROW-REPEAT GUARD:
                        // Trailing empty rows (e.g. LibreOffice Calc number-rows-repeated="1048575") stay in pendingEmptyRows
                        // and are discarded at </table:table> unless a non-empty row follows!
                        if (rowCells.empty()) {
                            pendingEmptyRows = std::min<size_t>(pendingEmptyRows + rowRepeat, 1048576u);
                        } else {
                            size_t gapToFlush = std::min<size_t>(pendingEmptyRows, kMaxOdsGapRows);
                            for (size_t g = 0; g < gapToFlush && sheet.rows.size() < kMaxOdsRows; ++g) {
                                sheet.rows.emplace_back();
                            }
                            pendingEmptyRows = 0;

                            sheet.maxCols = std::max(sheet.maxCols, rowCells.size());
                            size_t emitRows = std::min<size_t>(rowRepeat, kMaxNonEmptyRowRepeat);
                            for (size_t k = 0; k < emitRows && sheet.rows.size() < kMaxOdsRows; ++k) {
                                if (k + 1 == emitRows || sheet.rows.size() + 1 >= kMaxOdsRows) {
                                    sheet.rows.push_back(std::move(rowCells));
                                    break;
                                } else {
                                    sheet.rows.push_back(rowCells);
                                }
                            }
                        }

                        rSearch = afterRow;
                        if (sheet.rows.size() >= kMaxOdsRows) break;
                    }

                    if (!sheet.rows.empty()) {
                        out.sheets.push_back(std::move(sheet));
                    }
                    tSearch = afterTbl;
                }
            }
        }
    }

    out.valid = !out.sheets.empty();
    return out.valid;
}
