#pragma once
// XmlTagScan.h — small allocation-free XML scanner shared by the XML-based readers (AMF, 3MF, COLLADA,
// GPX, KML, TCX). It walks start/end/empty-element tags of an in-memory document, skipping comments,
// CDATA, processing instructions and DOCTYPE; names are matched without their namespace prefix.
// Text helpers decode character data (entities, numeric references, CDATA sections).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PlatformCompat.h"   // <windows.h> on Windows
#include "PathUtils.h"

namespace Explorer2::XmlTagScan {

struct XmlTag {
    std::string_view localName;
    std::string_view attrs;
    size_t tagStart = 0;
    size_t afterTag = 0;
    bool isClosing = false;
    bool isSelfClosing = false;
};

inline bool IsWs(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool IsNameChar(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
           (u >= '0' && u <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
}

inline std::string_view TrimWs(std::string_view s) {
    while (!s.empty() && IsWs(s.front())) s.remove_prefix(1);
    while (!s.empty() && IsWs(s.back())) s.remove_suffix(1);
    return s;
}

inline bool NextTag(std::string_view sv, size_t& pos, XmlTag& out) {
    const size_t n = sv.size();
    while (pos < n) {
        size_t lt = sv.find('<', pos);
        if (lt == std::string_view::npos || lt + 1 >= n) {
            pos = n;
            return false;
        }
        if (lt + 4 <= n && sv.substr(lt, 4) == "<!--") {
            size_t endC = sv.find("-->", lt + 4);
            pos = (endC == std::string_view::npos) ? n : (endC + 3);
            continue;
        }
        if (lt + 9 <= n && sv.substr(lt, 9) == "<![CDATA[") {
            size_t endCd = sv.find("]]>", lt + 9);
            pos = (endCd == std::string_view::npos) ? n : (endCd + 3);
            continue;
        }
        if (sv[lt + 1] == '?') {
            size_t endPi = sv.find("?>", lt + 2);
            pos = (endPi == std::string_view::npos) ? n : (endPi + 2);
            continue;
        }
        if (sv[lt + 1] == '!') {
            size_t gt = sv.find('>', lt + 2);
            pos = (gt == std::string_view::npos) ? n : (gt + 1);
            continue;
        }

        size_t gt = lt + 1;
        char quote = 0;
        while (gt < n) {
            char c = sv[gt];
            if (quote != 0) {
                if (c == quote) quote = 0;
            } else {
                if (c == '"' || c == '\'') quote = c;
                else if (c == '>') break;
            }
            ++gt;
        }
        if (gt >= n) {
            pos = n;
            return false;
        }

        size_t cur = lt + 1;
        bool closing = false;
        if (cur < gt && sv[cur] == '/') {
            closing = true;
            ++cur;
        }
        while (cur < gt && IsWs(sv[cur])) ++cur;
        size_t nameStart = cur;
        while (cur < gt && IsNameChar(sv[cur])) ++cur;
        if (cur == nameStart) {
            pos = gt + 1;
            continue;
        }

        std::string_view rawName = sv.substr(nameStart, cur - nameStart);
        size_t colon = rawName.rfind(':');
        std::string_view localName = (colon == std::string_view::npos) ? rawName : rawName.substr(colon + 1);

        size_t attrEnd = gt;
        bool selfClosing = false;
        if (!closing) {
            size_t back = gt;
            while (back > cur && IsWs(sv[back - 1])) --back;
            if (back > cur && sv[back - 1] == '/') {
                selfClosing = true;
                attrEnd = back - 1;
            }
        }

        out.localName = localName;
        out.attrs = (cur <= attrEnd) ? sv.substr(cur, attrEnd - cur) : std::string_view{};
        out.tagStart = lt;
        out.afterTag = gt + 1;
        out.isClosing = closing;
        out.isSelfClosing = selfClosing;
        pos = gt + 1;
        return true;
    }
    return false;
}

inline bool FindElementBody(
    std::string_view sv,
    size_t afterOpenPos,
    std::string_view localName,
    std::string_view& bodyOut,
    size_t& afterClosePos)
{
    size_t cur = afterOpenPos;
    int depth = 1;
    XmlTag t;
    while (NextTag(sv, cur, t)) {
        if (t.localName == localName) {
            if (t.isClosing) {
                --depth;
                if (depth == 0) {
                    bodyOut = sv.substr(afterOpenPos, t.tagStart - afterOpenPos);
                    afterClosePos = cur;
                    return true;
                }
            } else if (!t.isSelfClosing) {
                ++depth;
            }
        }
    }
    return false;
}

// The raw value (entities not decoded) of attribute `name` in a start tag, or in the attribute text of one: `out` is
// set and true returned when the attribute is there. An exact name wins; with byLocalName an attribute whose name has
// a namespace prefix ("w:val") also answers to its local name ("val"), and with ignoreCase names compare without
// regard to ASCII case. Unquoted values (as HTML allows) are accepted.
inline bool FindAttr(std::string_view attrs, std::string_view name, std::string_view& out, bool byLocalName = true,
                     bool ignoreCase = false) {
    auto same = [ignoreCase](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        if (!ignoreCase) return a == b;
        for (size_t k = 0; k < a.size(); ++k) {
            char x = a[k], y = b[k];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + ('a' - 'A'));
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + ('a' - 'A'));
            if (x != y) return false;
        }
        return true;
    };
    bool haveLocal = false;
    size_t i = 0;
    const size_t n = attrs.size();
    while (i < n) {
        while (i < n && IsWs(attrs[i])) ++i;
        const size_t nameStart = i;
        while (i < n && IsNameChar(attrs[i])) ++i;
        if (i == nameStart) { ++i; continue; }
        const std::string_view attrName = attrs.substr(nameStart, i - nameStart);
        while (i < n && IsWs(attrs[i])) ++i;
        if (i >= n || attrs[i] != '=') continue; // the element's own name, or a name without a value
        ++i;
        while (i < n && IsWs(attrs[i])) ++i;
        if (i >= n) break;
        std::string_view val;
        if (attrs[i] == '"' || attrs[i] == '\'') {
            const char q = attrs[i++];
            const size_t valStart = i;
            while (i < n && attrs[i] != q) ++i;
            val = attrs.substr(valStart, i - valStart);
            if (i < n) ++i;
        } else {
            const size_t valStart = i;
            while (i < n && !IsWs(attrs[i]) && attrs[i] != '>' && !(attrs[i] == '/' && i + 1 < n && attrs[i + 1] == '>')) ++i;
            val = attrs.substr(valStart, i - valStart);
        }
        if (same(attrName, name)) {
            out = val;
            return true;
        }
        if (byLocalName && !haveLocal) {
            const size_t colon = attrName.rfind(':');
            if (colon != std::string_view::npos && same(attrName.substr(colon + 1), name)) {
                out = val;
                haveLocal = true;
            }
        }
    }
    return haveLocal;
}

inline std::string_view GetAttr(std::string_view attrs, std::string_view wantName) {
    std::string_view v;
    return FindAttr(attrs, wantName, v) ? v : std::string_view();
}


// Common HTML named entities (beyond XML's five) that XHTML e-books and hand-written XML use
inline uint32_t HtmlEntityCodePoint(std::string_view name) {
    static const struct { const char* n; uint32_t cp; } kEntities[] = {
        { "nbsp", 0x00A0 }, { "shy", 0x00AD }, { "copy", 0x00A9 }, { "reg", 0x00AE }, { "trade", 0x2122 },
        { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "bull", 0x2022 }, { "middot", 0x00B7 },
        { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "sbquo", 0x201A }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
        { "bdquo", 0x201E }, { "laquo", 0x00AB }, { "raquo", 0x00BB }, { "deg", 0x00B0 }, { "times", 0x00D7 },
        { "divide", 0x00F7 }, { "euro", 0x20AC }, { "pound", 0x00A3 }, { "sect", 0x00A7 }, { "para", 0x00B6 },
        { "frac12", 0x00BD }, { "frac14", 0x00BC }, { "frac34", 0x00BE }, { "plusmn", 0x00B1 }, { "micro", 0x00B5 },
        { "dagger", 0x2020 }, { "prime", 0x2032 }, { "larr", 0x2190 }, { "rarr", 0x2192 }, { "zwnj", 0x200C },
        { "zwj", 0x200D }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "thinsp", 0x2009 },
    };
    for (const auto& e : kEntities) {
        if (name == e.n) return e.cp;
    }
    return 0;
}

// Character data of an element body: entity / character references decoded, CDATA kept verbatim,
// markup (child tags, comments, processing instructions) dropped.
inline std::string DecodeText(std::string_view raw) {
    if (raw.find_first_of("<&") == std::string_view::npos) return std::string(raw); // nothing to decode
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    const size_t n = raw.size();
    auto appendUtf8 = [&out](uint32_t cp) {
        if (cp == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0xFFFDu;
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    };
    while (i < n) {
        char c = raw[i];
        if (c == '<') {
            if (raw.compare(i, 9, "<![CDATA[") == 0) {
                size_t e = raw.find("]]>", i + 9);
                size_t stop = (e == std::string_view::npos) ? n : e;
                out.append(raw.data() + i + 9, stop - (i + 9));
                i = (e == std::string_view::npos) ? n : e + 3;
            } else if (raw.compare(i, 4, "<!--") == 0) {
                size_t e = raw.find("-->", i + 4);
                i = (e == std::string_view::npos) ? n : e + 3;
            } else {
                size_t e = raw.find('>', i + 1);
                i = (e == std::string_view::npos) ? n : e + 1;
            }
            continue;
        }
        if (c == '&') {
            size_t semi = raw.find(';', i + 1);
            if (semi != std::string_view::npos && semi - i <= 12) {
                std::string_view ent = raw.substr(i + 1, semi - i - 1);
                bool done = true;
                if (ent == "lt") out.push_back('<');
                else if (ent == "gt") out.push_back('>');
                else if (ent == "amp") out.push_back('&');
                else if (ent == "quot") out.push_back('"');
                else if (ent == "apos") out.push_back('\'');
                else if (uint32_t named = HtmlEntityCodePoint(ent)) appendUtf8(named);
                else if (ent.size() >= 2 && ent[0] == '#') {
                    uint32_t cp = 0;
                    bool hex = (ent[1] == 'x' || ent[1] == 'X');
                    size_t k = hex ? 2 : 1;
                    if (k >= ent.size()) done = false;
                    for (; done && k < ent.size(); ++k) {
                        char d = ent[k];
                        uint32_t v = 0;
                        if (d >= '0' && d <= '9') v = static_cast<uint32_t>(d - '0');
                        else if (hex && d >= 'a' && d <= 'f') v = static_cast<uint32_t>(d - 'a' + 10);
                        else if (hex && d >= 'A' && d <= 'F') v = static_cast<uint32_t>(d - 'A' + 10);
                        else { done = false; break; }
                        cp = cp * (hex ? 16u : 10u) + v;
                        if (cp > 0x10FFFFu) cp = 0x110000u; // stays out of range, becomes U+FFFD
                    }
                    if (done) appendUtf8(cp);
                } else {
                    done = false;
                }
                if (done) {
                    i = semi + 1;
                    continue;
                }
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// The same references in text that is already wide (XML's five, the HTML names above, &#..; / &#x..;); unknown
// or malformed references stay as written, and a no-break space becomes a plain space for the text views.
inline std::wstring DecodeEntitiesWide(std::wstring_view in) {
    if (in.find(L'&') == std::wstring_view::npos) return std::wstring(in);
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'&') {
            size_t semi = in.find(L';', i + 1);
            if (semi != std::wstring_view::npos && semi > i + 1 && semi - i <= 12) {
                std::wstring_view ent = in.substr(i + 1, semi - i - 1);
                uint32_t cp = 0;
                if (ent[0] == L'#') {
                    bool hex = ent.size() > 1 && (ent[1] == L'x' || ent[1] == L'X');
                    size_t k = hex ? 2 : 1;
                    bool ok = k < ent.size();
                    for (; ok && k < ent.size(); ++k) {
                        wchar_t d = ent[k];
                        uint32_t v = 0;
                        if (d >= L'0' && d <= L'9') v = static_cast<uint32_t>(d - L'0');
                        else if (hex && d >= L'a' && d <= L'f') v = static_cast<uint32_t>(d - L'a' + 10);
                        else if (hex && d >= L'A' && d <= L'F') v = static_cast<uint32_t>(d - L'A' + 10);
                        else ok = false;
                        cp = cp * (hex ? 16u : 10u) + v;
                        if (cp > 0x10FFFFu) ok = false;
                    }
                    if (!ok || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0;
                } else {
                    std::string name;
                    for (wchar_t ch : ent) {
                        if (ch > 0x7F) { name.clear(); break; }
                        name.push_back(static_cast<char>(ch));
                    }
                    if (name == "amp") cp = '&';
                    else if (name == "lt") cp = '<';
                    else if (name == "gt") cp = '>';
                    else if (name == "quot") cp = '"';
                    else if (name == "apos") cp = '\'';
                    else if (name == "nbsp") cp = ' ';
                    else if (!name.empty()) cp = HtmlEntityCodePoint(name);
                }
                if (cp != 0) {
                    PathUtils::AppendUtf16(out, cp);
                    i = semi;
                    continue;
                }
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

inline std::string_view TrimView(std::string_view s) { return PathUtils::TrimAscii(s); }

// An element body's trimmed character data without copying it when it holds no markup or references (numbers, names,
// timestamps); otherwise decoded into scratch, which the view then points into
inline std::string_view TextView(std::string_view raw, std::string& scratch) {
    if (raw.find_first_of("<&") == std::string_view::npos) return TrimView(raw);
    scratch = DecodeText(raw);
    return TrimView(scratch);
}

// One element found by FindChild / ForEachElement
struct Element {
    std::string_view attrs;
    std::string_view body; // empty for <empty/> elements
    bool selfClosing = false;
};

// First element named `name` inside `body`; with directOnly, only immediate children count
inline bool FindChild(std::string_view body, std::string_view name, Element& out, bool directOnly = true) {
    size_t pos = 0;
    int depth = 0;
    XmlTag t;
    while (NextTag(body, pos, t)) {
        if (t.isClosing) {
            if (depth > 0) --depth;
            continue;
        }
        if ((!directOnly || depth == 0) && t.localName == name) {
            out.attrs = t.attrs;
            out.selfClosing = t.isSelfClosing;
            out.body = {};
            if (!t.isSelfClosing) {
                size_t after = 0;
                if (!FindElementBody(body, t.afterTag, name, out.body, after)) return false;
            }
            return true;
        }
        if (!t.isSelfClosing) ++depth;
    }
    return false;
}

// Attribute value with entity / character references decoded and surrounding whitespace removed
inline std::string AttrText(std::string_view attrs, std::string_view name) {
    return std::string(TrimView(DecodeText(GetAttr(attrs, name))));
}

// Decoded, trimmed text of the first (direct) child element named `name`; empty when absent
inline std::string ChildText(std::string_view body, std::string_view name, bool directOnly = true) {
    Element e;
    if (!FindChild(body, name, e, directOnly)) return {};
    return std::string(TrimView(DecodeText(e.body)));
}

// Calls fn(localName, const Element&) for every direct child element of `body`, in document order.
// Stops early when fn returns false or a child is not terminated.
template <class Fn>
inline void ForEachChild(std::string_view body, Fn&& fn) {
    size_t pos = 0;
    XmlTag t;
    while (NextTag(body, pos, t)) {
        if (t.isClosing) continue; // a stray end tag at this level
        Element e;
        e.attrs = t.attrs;
        e.selfClosing = t.isSelfClosing;
        if (!t.isSelfClosing) {
            size_t after = 0;
            if (!FindElementBody(body, t.afterTag, t.localName, e.body, after)) return;
            pos = after;
        }
        if (!fn(t.localName, static_cast<const Element&>(e))) return;
    }
}

// Calls fn(const Element&) for every element named `name` in `doc` (outermost match; its body is skipped).
// Stops early when fn returns false or an element is not terminated.
template <class Fn>
inline void ForEachElement(std::string_view doc, std::string_view name, Fn&& fn) {
    size_t pos = 0;
    XmlTag t;
    while (NextTag(doc, pos, t)) {
        if (t.isClosing || t.localName != name) continue;
        Element e;
        e.attrs = t.attrs;
        e.selfClosing = t.isSelfClosing;
        if (!t.isSelfClosing) {
            size_t after = 0;
            if (!FindElementBody(doc, t.afterTag, name, e.body, after)) return;
            pos = after;
        }
        if (!fn(static_cast<const Element&>(e))) return;
    }
}


// Windows code page of a charset / encoding name (lower case, as in XML declarations and MIME headers);
// 0 when it is not known. UTF-8 is 65001, US-ASCII 20127.
inline unsigned CodePageForCharset(std::string_view name) {
    static const struct { const char* name; unsigned cp; } kCodePages[] = {
        { "utf-8", 65001 }, { "utf8", 65001 }, { "us-ascii", 20127 }, { "ascii", 20127 },
        { "windows-1250", 1250 }, { "windows-1251", 1251 }, { "windows-1252", 1252 }, { "windows-1253", 1253 },
        { "windows-1254", 1254 }, { "windows-1255", 1255 }, { "windows-1256", 1256 }, { "windows-1257", 1257 },
        { "windows-1258", 1258 }, { "windows-874", 874 }, { "cp1250", 1250 }, { "cp1251", 1251 }, { "cp1252", 1252 },
        { "iso-8859-1", 28591 }, { "latin1", 28591 }, { "iso-8859-2", 28592 }, { "iso-8859-3", 28593 },
        { "iso-8859-4", 28594 }, { "iso-8859-5", 28595 }, { "iso-8859-6", 28596 }, { "iso-8859-7", 28597 },
        { "iso-8859-8", 28598 }, { "iso-8859-9", 28599 }, { "iso-8859-13", 28603 }, { "iso-8859-15", 28605 },
        { "koi8-r", 20866 }, { "koi8-u", 21866 }, { "cp866", 866 }, { "ibm866", 866 }, { "tis-620", 874 },
        { "gb2312", 936 }, { "gbk", 936 }, { "gb18030", 54936 }, { "big5", 950 }, { "shift_jis", 932 },
        { "euc-jp", 20932 }, { "iso-2022-jp", 50220 }, { "euc-kr", 51949 }, { "ks_c_5601-1987", 949 },
    };
    for (const auto& e : kCodePages) {
        if (name == e.name) return e.cp;
    }
    return 0;
}

// A whole XML document as UTF-8: UTF-16 byte-order marks and the encoding named in the XML declaration
// (windows-125x, iso-8859-x, koi8-r/u, cp866, gb2312/gbk, big5, shift_jis, euc-jp/kr) are honored;
// documents without a declaration are taken as UTF-8.
inline std::string DocumentToUtf8(const uint8_t* data, size_t size) {
    if (!data || size == 0) return std::string();
    auto wideToUtf8 = [](const wchar_t* w, int wn) {
        std::string out;
        if (wn <= 0) return out;
        int n = WideCharToMultiByte(CP_UTF8, 0, w, wn, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return out;
        out.resize(static_cast<size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, w, wn, out.data(), n, nullptr, nullptr);
        return out;
    };
    if (size >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        bool be = data[0] == 0xFE;
        std::wstring w((size - 2) / 2, L'\0');
        for (size_t i = 0; i < w.size(); ++i) {
            uint8_t a = data[2 + 2 * i], b = data[3 + 2 * i];
            w[i] = static_cast<wchar_t>(be ? ((a << 8) | b) : (a | (b << 8)));
        }
        return wideToUtf8(w.data(), static_cast<int>(w.size()));
    }
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        return std::string(reinterpret_cast<const char*>(data) + 3, size - 3);
    }
    std::string_view head(reinterpret_cast<const char*>(data), size < 256 ? size : 256);
    std::string enc;
    if (head.rfind("<?xml", 0) == 0) {
        size_t declEnd = head.find("?>");
        size_t e = head.find("encoding");
        if (e != std::string_view::npos && (declEnd == std::string_view::npos || e < declEnd)) {
            size_t q = head.find_first_of("\"'", e);
            if (q != std::string_view::npos) {
                size_t q2 = head.find(head[q], q + 1);
                if (q2 != std::string_view::npos) {
                    for (char c : head.substr(q + 1, q2 - q - 1)) enc.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
                }
            }
        }
    }
    const unsigned cp = CodePageForCharset(enc);
    if (cp != 0 && cp != 65001 && cp != 20127) {
        int wn = MultiByteToWideChar(cp, 0, reinterpret_cast<const char*>(data), static_cast<int>(size), nullptr, 0);
        if (wn > 0) {
            std::wstring w(static_cast<size_t>(wn), L'\0');
            MultiByteToWideChar(cp, 0, reinterpret_cast<const char*>(data), static_cast<int>(size), w.data(), wn);
            return wideToUtf8(w.data(), wn);
        }
    }
    return std::string(reinterpret_cast<const char*>(data), size);
}

} // namespace Explorer2::XmlTagScan
