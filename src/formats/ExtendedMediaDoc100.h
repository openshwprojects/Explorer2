#pragma once
// ExtendedMediaDoc100.h — Audio, Tracker, SoundFont, DAW/Score, NLE, Subtitle, Email, E-Book & Help Readers
// Covers: .eml/.mbox, .mp3, .ogg/.opus/.spx, .mod/.xm/.s3m/.it, .sf2/.sfz,
//         .mscz, .als, .flp, .gp/.gp3/.gp4/.gp5, .rpp, .edl, .fcpxml, .otio,
//         .idx/.sup/.sub, .msg, .mobi/.azw/.azw3/.prc, .djvu/.djv, .chm

namespace Formats {

namespace DetailMedia100 {

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
        uint16_t u = bigEndian ? DetailSys100::RdU16BE(p + i) : DetailSys100::RdU16LE(p + i);
        if (u == 0) break;
        if (u == 0xFEFF || u == 0xFFFE) continue;
        out.push_back(static_cast<wchar_t>(u));
    }
    return PathUtils::Trim(out);
}

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

inline void FinalizeSheetCols(SpreadsheetSheet& sh) {
    size_t mc = sh.maxCols;
    for (const auto& r : sh.rows) {
        if (r.size() > mc) mc = r.size();
    }
    sh.maxCols = mc;
}

inline void ComputeDocCounts(ParsedOfficeDocument& out) {
    size_t words = 0;
    size_t chars = 0;
    for (const auto& p : out.paragraphs) {
        chars += p.text.size();
        bool inW = false;
        for (wchar_t ch : p.text) {
            bool sp = (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n');
            if (!sp && !inW) { ++words; inW = true; }
            else if (sp) { inW = false; }
        }
    }
    out.wordCount = words;
    out.charCount = chars;
}

inline std::wstring DecodeHtmlEntitiesMedia(std::wstring_view in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'&') {
            size_t semi = in.find(L';', i + 1);
            if (semi != std::wstring_view::npos && semi - i <= 12) {
                std::wstring_view ent = in.substr(i + 1, semi - i - 1);
                if (ent == L"amp")   { out.push_back(L'&');  i = semi; continue; }
                if (ent == L"lt")    { out.push_back(L'<');  i = semi; continue; }
                if (ent == L"gt")    { out.push_back(L'>');  i = semi; continue; }
                if (ent == L"quot")  { out.push_back(L'"');  i = semi; continue; }
                if (ent == L"apos")  { out.push_back(L'\''); i = semi; continue; }
                if (ent == L"nbsp")  { out.push_back(L' ');  i = semi; continue; }
                if (ent == L"ndash") { out.push_back(0x2013); i = semi; continue; }
                if (ent == L"mdash") { out.push_back(0x2014); i = semi; continue; }
                if (ent == L"copy")  { out.push_back(0x00A9); i = semi; continue; }
                if (!ent.empty() && ent[0] == L'#') {
                    uint32_t cp = 0;
                    if (ent.size() > 2 && (ent[1] == L'x' || ent[1] == L'X')) {
                        for (size_t k = 2; k < ent.size(); ++k) {
                            wchar_t c = ent[k];
                            cp <<= 4;
                            if (c >= L'0' && c <= L'9') cp |= static_cast<uint32_t>(c - L'0');
                            else if (c >= L'a' && c <= L'f') cp |= static_cast<uint32_t>(c - L'a' + 10);
                            else if (c >= L'A' && c <= L'F') cp |= static_cast<uint32_t>(c - L'A' + 10);
                        }
                    } else {
                        for (size_t k = 1; k < ent.size(); ++k) {
                            if (ent[k] >= L'0' && ent[k] <= L'9') cp = cp * 10u + static_cast<uint32_t>(ent[k] - L'0');
                        }
                    }
                    if (cp >= 32 && cp <= 0xFFFF) {
                        out.push_back(static_cast<wchar_t>(cp));
                        i = semi;
                        continue;
                    }
                }
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

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
        clean = PathUtils::Trim(DecodeHtmlEntitiesMedia(clean));
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
    out.reserve((in.size() * 3) / 4 + 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        int v = -1;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+') v = 62;
        else if (ch == '/') v = 63;
        else if (ch == '=') break;
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
        }
    }
    return out;
}

inline std::string DecodeQuotedPrintable(std::string_view in) {
    auto hexNib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '=') {
            if (i + 1 < in.size() && in[i + 1] == '\n') { i += 1; continue; }
            if (i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n') { i += 2; continue; }
            if (i + 2 < in.size()) {
                int hi = hexNib(in[i + 1]);
                int lo = hexNib(in[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
        }
        out.push_back(in[i]);
    }
    return out;
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
        if (charset.find("8859-1") != std::string::npos || charset.find("1252") != std::string::npos || charset == "latin1") {
            result += Latin1OrCp1252ToWide(reinterpret_cast<const uint8_t*>(decodedBytes.data()), decodedBytes.size());
        } else {
            result += PathUtils::Utf8ToWide(decodedBytes);
        }
        pos = end + 2;
    }
    return PathUtils::Trim(result);
}

// Lightweight XML helpers for .mscz, .als, .gp, .fcpxml
inline std::string ExtractXmlAttr(std::string_view tag, std::string_view attrName) {
    std::string needle = std::string(attrName) + "=";
    size_t p = 0;
    while (p < tag.size()) {
        size_t hit = tag.find(needle, p);
        if (hit == std::string_view::npos) return {};
        if (hit == 0 || tag[hit - 1] == ' ' || tag[hit - 1] == '\t' || tag[hit - 1] == '\r' || tag[hit - 1] == '\n') {
            size_t vPos = hit + needle.size();
            if (vPos < tag.size() && (tag[vPos] == '"' || tag[vPos] == '\'')) {
                char q = tag[vPos++];
                size_t endQ = tag.find(q, vPos);
                if (endQ != std::string_view::npos) {
                    return std::string(tag.substr(vPos, endQ - vPos));
                }
            }
        }
        p = hit + needle.size();
    }
    return {};
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
    return DecodeHtmlEntitiesMedia(PathUtils::Trim(PathUtils::Utf8ToWide(xml.substr(gt + 1, c - gt - 1))));
}

// ============================================================================
// 1. RFC 5322 Email (.eml) & Unix Mailbox (.mbox)
// ============================================================================
inline bool ParseEmlOrMbox(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 8) return false;
    size_t checkLen = std::min<size_t>(raw.size(), 4096);
    size_t nulCount = 0;
    for (size_t i = 0; i < checkLen; ++i) if (raw[i] == 0) ++nulCount;
    if (nulCount * 50 > checkLen) return false;

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());

    auto parseSingleEmail = [&](std::string_view msgSv, int msgIndex) -> bool {
        // Skip optional "From " mbox envelope line
        std::wstring mboxEnvelope;
        if (msgSv.size() >= 5 && msgSv.substr(0, 5) == "From ") {
            size_t nl = msgSv.find('\n');
            if (nl == std::string_view::npos) return false;
            mboxEnvelope = PathUtils::Trim(PathUtils::Utf8ToWide(msgSv.substr(0, nl)));
            msgSv.remove_prefix(nl + 1);
        }

        size_t hdrEnd = msgSv.find("\r\n\r\n");
        size_t sepLen = 4;
        size_t altEnd = msgSv.find("\n\n");
        if (altEnd != std::string_view::npos && (hdrEnd == std::string_view::npos || altEnd < hdrEnd)) {
            hdrEnd = altEnd;
            sepLen = 2;
        }
        std::string_view hdrBlock = (hdrEnd != std::string_view::npos) ? msgSv.substr(0, hdrEnd) : msgSv;
        std::string_view bodyBlock = (hdrEnd != std::string_view::npos) ? msgSv.substr(hdrEnd + sepLen) : std::string_view{};

        // Unfold RFC 5322 header lines
        std::vector<std::pair<std::string, std::string>> headers;
        size_t ls = 0;
        while (ls < hdrBlock.size()) {
            size_t le = hdrBlock.find('\n', ls);
            if (le == std::string_view::npos) le = hdrBlock.size();
            std::string_view line = hdrBlock.substr(ls, le - ls);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            ls = (le < hdrBlock.size()) ? (le + 1) : hdrBlock.size();
            if (line.empty()) continue;

            if ((line[0] == ' ' || line[0] == '\t') && !headers.empty()) {
                headers.back().second.push_back(' ');
                size_t t = 0;
                while (t < line.size() && (line[t] == ' ' || line[t] == '\t')) ++t;
                headers.back().second.append(line.substr(t));
            } else {
                size_t colon = line.find(':');
                if (colon != std::string_view::npos && colon > 0 && colon <= 64) {
                    bool validKey = true;
                    for (size_t k = 0; k < colon; ++k) {
                        unsigned char ch = static_cast<unsigned char>(line[k]);
                        if (ch <= 32 || ch >= 127) { validKey = false; break; }
                    }
                    if (validKey) {
                        std::string key = PathUtils::ToLowerAscii(line.substr(0, colon));
                        std::string_view val = line.substr(colon + 1);
                        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
                        headers.push_back({ std::move(key), std::string(val) });
                    }
                }
            }
        }

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
        std::string ctype    = getHdr("content-type");
        std::string cte      = PathUtils::ToLowerAscii(getHdr("content-transfer-encoding"));

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

        auto decodePartBody = [](std::string_view rawPart, const std::string& partCte, const std::string& partCt) -> std::wstring {
            std::string bytes;
            if (partCte.find("quoted-printable") != std::string::npos) {
                bytes = DecodeQuotedPrintable(rawPart);
            } else if (partCte.find("base64") != std::string::npos) {
                auto b = DecodeBase64Media(rawPart);
                bytes.assign(reinterpret_cast<const char*>(b.data()), b.size());
            } else {
                bytes.assign(rawPart.data(), std::min<size_t>(rawPart.size(), 65536));
            }
            std::string ctLow = PathUtils::ToLowerAscii(partCt);
            std::wstring w = (ctLow.find("8859-1") != std::string::npos || ctLow.find("1252") != std::string::npos)
                ? Latin1OrCp1252ToWide(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size())
                : PathUtils::Utf8ToWide(bytes);
            if (ctLow.find("text/html") != std::string::npos) {
                auto ps = StripHtmlToParagraphs(w, 120);
                std::wstring joined;
                for (const auto& p : ps) {
                    if (!joined.empty()) joined += L"\n";
                    joined += p;
                }
                return joined;
            }
            return PathUtils::Trim(w);
        };

        std::string ctLower = PathUtils::ToLowerAscii(ctype);
        size_t bndPos = ctLower.find("boundary=");
        if (bndPos != std::string::npos && !bodyBlock.empty()) {
            std::string_view bVal = std::string_view(ctype).substr(bndPos + 9);
            if (!bVal.empty() && (bVal.front() == '"' || bVal.front() == '\'')) {
                char q = bVal.front();
                bVal.remove_prefix(1);
                size_t eq = bVal.find(q);
                if (eq != std::string_view::npos) bVal = bVal.substr(0, eq);
            } else {
                size_t semi = bVal.find_first_of("; \t\r\n");
                if (semi != std::string_view::npos) bVal = bVal.substr(0, semi);
            }
            std::string delim = "--" + std::string(bVal);
            size_t pPos = bodyBlock.find(delim);
            std::wstring plainFound;
            std::wstring htmlFallback;
            while (pPos != std::string_view::npos) {
                size_t afterDelim = pPos + delim.size();
                if (afterDelim + 2 <= bodyBlock.size() && bodyBlock.substr(afterDelim, 2) == "--") break;
                size_t partStart = bodyBlock.find('\n', afterDelim);
                if (partStart == std::string_view::npos) break;
                ++partStart;
                size_t nextDelim = bodyBlock.find(delim, partStart);
                std::string_view partSv = (nextDelim != std::string_view::npos)
                    ? bodyBlock.substr(partStart, nextDelim - partStart)
                    : bodyBlock.substr(partStart);

                size_t phEnd = partSv.find("\r\n\r\n");
                size_t pSep = 4;
                size_t phAlt = partSv.find("\n\n");
                if (phAlt != std::string_view::npos && (phEnd == std::string_view::npos || phAlt < phEnd)) {
                    phEnd = phAlt; pSep = 2;
                }
                std::string_view pHdr = (phEnd != std::string_view::npos) ? partSv.substr(0, phEnd) : std::string_view{};
                std::string_view pBody = (phEnd != std::string_view::npos) ? partSv.substr(phEnd + pSep) : partSv;

                std::string pHdrLow = PathUtils::ToLowerAscii(pHdr);
                std::string pCt = "text/plain";
                std::string pCte;
                std::string pDisp;
                {
                    size_t cti = pHdrLow.find("content-type:");
                    if (cti != std::string::npos) {
                        size_t e = pHdr.find('\n', cti);
                        pCt = std::string(pHdr.substr(cti + 13, (e == std::string_view::npos ? pHdr.size() : e) - (cti + 13)));
                    }
                    size_t ctei = pHdrLow.find("content-transfer-encoding:");
                    if (ctei != std::string::npos) {
                        size_t e = pHdrLow.find('\n', ctei);
                        pCte = pHdrLow.substr(ctei + 26, (e == std::string::npos ? pHdrLow.size() : e) - (ctei + 26));
                    }
                    size_t cdi = pHdrLow.find("content-disposition:");
                    if (cdi != std::string::npos) {
                        size_t e = pHdr.find('\n', cdi);
                        pDisp = std::string(pHdr.substr(cdi + 20, (e == std::string_view::npos ? pHdr.size() : e) - (cdi + 20)));
                    }
                }
                std::string pCtLow = PathUtils::ToLowerAscii(pCt);
                size_t fnPos = pHdrLow.find("filename=");
                if (fnPos == std::string::npos) fnPos = pHdrLow.find("name=");
                if (fnPos != std::string::npos || pDisp.find("attachment") != std::string::npos) {
                    std::wstring attName = L"(attachment)";
                    if (fnPos != std::string::npos) {
                        size_t eq = pHdr.find('=', fnPos);
                        std::string_view fnv = pHdr.substr(eq + 1);
                        size_t eol = fnv.find_first_of(";\r\n");
                        if (eol != std::string_view::npos) fnv = fnv.substr(0, eol);
                        while (!fnv.empty() && (fnv.front() == ' ' || fnv.front() == '"')) fnv.remove_prefix(1);
                        while (!fnv.empty() && (fnv.back() == ' ' || fnv.back() == '"')) fnv.remove_suffix(1);
                        attName = DecodeRfc2047Header(fnv);
                    }
                    out.paragraphs.push_back({ L"Attachment: " + attName + L" (" + PathUtils::FormatByteSize(pBody.size()) + L")", 3, true, false });
                } else if (pCtLow.find("text/plain") != std::string::npos) {
                    plainFound = decodePartBody(pBody, pCte, pCt);
                } else if (pCtLow.find("text/html") != std::string::npos) {
                    htmlFallback = decodePartBody(pBody, pCte, pCt);
                }
                pPos = nextDelim;
            }
            const std::wstring& chosen = !plainFound.empty() ? plainFound : htmlFallback;
            if (!chosen.empty()) {
                out.paragraphs.push_back({ chosen, 0, false, false });
            }
        } else if (!bodyBlock.empty()) {
            std::wstring bodyText = decodePartBody(bodyBlock, cte, ctype);
            if (!bodyText.empty()) {
                out.paragraphs.push_back({ bodyText, 0, false, false });
            }
        }
        return true;
    };

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = (extLower == L".mbox") ? L"Unix Mailbox Archive (.mbox)" : L"RFC 5322 Email Message (.eml)";

    int parsedCount = 0;
    if (extLower == L".mbox" && (sv.substr(0, 5) == "From " || sv.find("\nFrom ") != std::string_view::npos)) {
        size_t pos = 0;
        while (pos < sv.size() && parsedCount < 64) {
            size_t nextMsg = sv.find("\nFrom ", pos + 5);
            std::string_view slice = (nextMsg != std::string_view::npos)
                ? sv.substr(pos, nextMsg - pos)
                : sv.substr(pos);
            if (parseSingleEmail(slice, parsedCount)) ++parsedCount;
            if (nextMsg == std::string_view::npos) break;
            pos = nextMsg + 1;
        }
    } else {
        if (parseSingleEmail(sv, 0)) parsedCount = 1;
    }

    if (parsedCount == 0 || out.paragraphs.empty()) return false;
    ComputeDocCounts(out);
    out.valid = true;
    return true;
}

// ============================================================================
// 2. MPEG Audio + ID3v2.2 / v2.3 / v2.4 + ID3v1 + Xing/VBRI (.mp3)
// ============================================================================
inline std::wstring DecodeId3String(uint8_t enc, const uint8_t* p, size_t len) {
    if (len == 0) return {};
    if (enc == 0) {
        return Latin1OrCp1252ToWide(p, len);
    }
    if (enc == 1) {
        bool be = false;
        size_t off = 0;
        if (len >= 2) {
            if (p[0] == 0xFE && p[1] == 0xFF) { be = true; off = 2; }
            else if (p[0] == 0xFF && p[1] == 0xFE) { be = false; off = 2; }
        }
        return Utf16BytesToWide(p + off, len - off, be);
    }
    if (enc == 2) {
        return Utf16BytesToWide(p, len, true);
    }
    // enc == 3 (UTF-8) or fallback
    size_t actual = 0;
    while (actual < len && p[actual] != 0) ++actual;
    return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), actual)));
}

inline size_t FindId3Terminator(uint8_t enc, const uint8_t* p, size_t len, size_t& outTermBytes) {
    if (enc == 1 || enc == 2) {
        outTermBytes = 2;
        for (size_t i = 0; i + 2 <= len; i += 2) {
            if (p[i] == 0 && p[i + 1] == 0) return i;
        }
        outTermBytes = 0;
        return len;
    }
    outTermBytes = 1;
    for (size_t i = 0; i < len; ++i) {
        if (p[i] == 0) return i;
    }
    outTermBytes = 0;
    return len;
}

inline bool ParseMp3Audio(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 4) return false;

    bool hasId3v2 = false;
    uint8_t id3Major = 0;
    uint8_t id3Minor = 0;
    uint8_t id3Flags = 0;
    uint32_t id3Size = 0;
    size_t audioStart = 0;

    if (raw.size() >= 10 && std::memcmp(raw.data(), "ID3", 3) == 0 &&
        raw[3] >= 2 && raw[3] <= 4 && raw[4] < 0xFF &&
        (raw[6] & 0x80) == 0 && (raw[7] & 0x80) == 0 && (raw[8] & 0x80) == 0 && (raw[9] & 0x80) == 0)
    {
        hasId3v2 = true;
        id3Major = raw[3];
        id3Minor = raw[4];
        id3Flags = raw[5];
        id3Size = (static_cast<uint32_t>(raw[6] & 0x7F) << 21) |
                  (static_cast<uint32_t>(raw[7] & 0x7F) << 14) |
                  (static_cast<uint32_t>(raw[8] & 0x7F) << 7)  |
                  static_cast<uint32_t>(raw[9] & 0x7F);
        audioStart = std::min<size_t>(raw.size(), 10ULL + id3Size);
    }

    struct Id3TagRow { std::wstring id; std::wstring label; std::wstring value; };
    std::vector<Id3TagRow> tagRows;

    if (hasId3v2) {
        size_t pos = 10;
        const size_t tagEnd = std::min<size_t>(raw.size(), 10ULL + id3Size);
        if ((id3Flags & 0x40) != 0 && pos + 4 <= tagEnd) {
            uint32_t extSz = (id3Major == 4)
                ? ((static_cast<uint32_t>(raw[pos] & 0x7F) << 21) |
                   (static_cast<uint32_t>(raw[pos + 1] & 0x7F) << 14) |
                   (static_cast<uint32_t>(raw[pos + 2] & 0x7F) << 7) |
                   static_cast<uint32_t>(raw[pos + 3] & 0x7F))
                : DetailSys100::RdU32BE(raw.data() + pos);
            pos += (id3Major == 4) ? extSz : (4ULL + extSz);
        }

        const size_t hdrLen = (id3Major == 2) ? 6 : 10;
        while (pos + hdrLen <= tagEnd && tagRows.size() < 256) {
            const uint8_t* fp = raw.data() + pos;
            if (fp[0] == 0) break; // Padding

            size_t idLen = (id3Major == 2) ? 3 : 4;
            bool validId = true;
            for (size_t k = 0; k < idLen; ++k) {
                if (!((fp[k] >= 'A' && fp[k] <= 'Z') || (fp[k] >= '0' && fp[k] <= '9'))) { validId = false; break; }
            }
            if (!validId) break;

            std::string fid(reinterpret_cast<const char*>(fp), idLen);
            uint32_t fSize = 0;
            uint16_t fFlags = 0;
            if (id3Major == 2) {
                fSize = (static_cast<uint32_t>(fp[3]) << 16) | (static_cast<uint32_t>(fp[4]) << 8) | fp[5];
            } else if (id3Major == 4) {
                if ((fp[4] & 0x80) == 0 && (fp[5] & 0x80) == 0 && (fp[6] & 0x80) == 0 && (fp[7] & 0x80) == 0) {
                    fSize = (static_cast<uint32_t>(fp[4] & 0x7F) << 21) |
                            (static_cast<uint32_t>(fp[5] & 0x7F) << 14) |
                            (static_cast<uint32_t>(fp[6] & 0x7F) << 7)  |
                            static_cast<uint32_t>(fp[7] & 0x7F);
                } else {
                    fSize = DetailSys100::RdU32BE(fp + 4);
                }
                fFlags = DetailSys100::RdU16BE(fp + 8);
            } else {
                fSize = DetailSys100::RdU32BE(fp + 4);
                fFlags = DetailSys100::RdU16BE(fp + 8);
            }
            pos += hdrLen;
            if (fSize == 0 || pos + fSize > tagEnd) break;

            const uint8_t* payload = raw.data() + pos;
            size_t pLen = fSize;
            pos += fSize;

            // Skip ID3v2.4 data length indicator if set
            if (id3Major == 4 && (fFlags & 0x0001u) != 0 && pLen >= 4) {
                payload += 4;
                pLen -= 4;
            }
            if (pLen == 0) continue;

            auto mapFrameName = [](const std::string& id) -> std::wstring {
                if (id == "TIT2" || id == "TT2") return L"Title";
                if (id == "TPE1" || id == "TP1") return L"Artist";
                if (id == "TPE2" || id == "TP2") return L"Album Artist";
                if (id == "TALB" || id == "TAL") return L"Album";
                if (id == "TYER" || id == "TYE" || id == "TDRC") return L"Year / Date";
                if (id == "TCON" || id == "TCO") return L"Genre";
                if (id == "TRCK" || id == "TRK") return L"Track Number";
                if (id == "TPOS" || id == "TPA") return L"Disc Number";
                if (id == "TCOM" || id == "TCM") return L"Composer";
                if (id == "TBPM" || id == "TBP") return L"BPM";
                if (id == "TPUB" || id == "TPB") return L"Publisher";
                if (id == "TSSE" || id == "TSS") return L"Encoder";
                if (id == "COMM" || id == "COM") return L"Comment";
                if (id == "TXXX" || id == "TXX") return L"User Text";
                if (id == "APIC" || id == "PIC") return L"Cover Art";
                return PathUtils::Utf8ToWide(id);
            };

            std::wstring wId = PathUtils::Utf8ToWide(fid);
            std::wstring label = mapFrameName(fid);

            if (fid == "TXXX" || fid == "TXX") {
                uint8_t enc = payload[0];
                size_t termBytes = 0;
                size_t descLen = FindId3Terminator(enc, payload + 1, pLen - 1, termBytes);
                std::wstring desc = DecodeId3String(enc, payload + 1, descLen);
                size_t valOff = 1 + descLen + termBytes;
                std::wstring val = (valOff < pLen) ? DecodeId3String(enc, payload + valOff, pLen - valOff) : L"";
                tagRows.push_back({ wId, desc.empty() ? label : (L"TXXX:" + desc), val });
            } else if (fid == "COMM" || fid == "COM") {
                if (pLen >= 4) {
                    uint8_t enc = payload[0];
                    std::wstring lang = FixedAsciiField(payload + 1, 3);
                    size_t termBytes = 0;
                    size_t descLen = FindId3Terminator(enc, payload + 4, pLen - 4, termBytes);
                    std::wstring desc = DecodeId3String(enc, payload + 4, descLen);
                    size_t txtOff = 4 + descLen + termBytes;
                    std::wstring txt = (txtOff < pLen) ? DecodeId3String(enc, payload + txtOff, pLen - txtOff) : desc;
                    std::wstring lbl = label;
                    if (!lang.empty()) lbl += L" [" + lang + L"]";
                    if (!desc.empty() && txt != desc) lbl += L" (" + desc + L")";
                    tagRows.push_back({ wId, lbl, txt });
                }
            } else if (fid == "APIC" || fid == "PIC") {
                uint8_t enc = payload[0];
                std::wstring mime;
                size_t cursor = 1;
                if (fid == "PIC") {
                    if (pLen < 6) continue;
                    mime = L"image/" + PathUtils::ToLower(FixedAsciiField(payload + 1, 3));
                    cursor = 4;
                } else {
                    size_t nul = 0;
                    while (cursor + nul < pLen && payload[cursor + nul] != 0) ++nul;
                    mime = FixedAsciiField(payload + cursor, nul);
                    cursor += nul + ((cursor + nul < pLen) ? 1 : 0);
                }
                uint8_t picType = (cursor < pLen) ? payload[cursor++] : 0;
                size_t termBytes = 0;
                size_t descLen = (cursor < pLen) ? FindId3Terminator(enc, payload + cursor, pLen - cursor, termBytes) : 0;
                size_t imgOff = cursor + descLen + termBytes;
                size_t imgBytes = (imgOff <= pLen) ? (pLen - imgOff) : 0;
                tagRows.push_back({ wId, label,
                    (mime.empty() ? L"image/binary" : mime) + L", type=" + std::to_wstring(picType) + L", " + PathUtils::FormatByteSize(imgBytes) });
            } else if (fid[0] == 'T') {
                uint8_t enc = payload[0];
                std::wstring val = DecodeId3String(enc, payload + 1, pLen - 1);
                if (!val.empty()) tagRows.push_back({ wId, label, val });
            } else {
                tagRows.push_back({ wId, label, PathUtils::FormatByteSize(pLen) });
            }
        }
    }

    // Check trailing 128-byte ID3v1 tag
    bool hasId3v1 = false;
    if (raw.size() >= 128 && std::memcmp(raw.data() + raw.size() - 128, "TAG", 3) == 0) {
        hasId3v1 = true;
        const uint8_t* t1 = raw.data() + raw.size() - 128;
        std::wstring title1   = Latin1OrCp1252ToWide(t1 + 3, 30);
        std::wstring artist1  = Latin1OrCp1252ToWide(t1 + 33, 30);
        std::wstring album1   = Latin1OrCp1252ToWide(t1 + 63, 30);
        std::wstring year1    = Latin1OrCp1252ToWide(t1 + 93, 4);
        std::wstring comment1 = Latin1OrCp1252ToWide(t1 + 97, (t1[125] == 0 && t1[126] != 0) ? 28 : 30);
        if (!hasId3v2 || tagRows.empty()) {
            if (!title1.empty())   tagRows.push_back({ L"ID3v1", L"Title", title1 });
            if (!artist1.empty())  tagRows.push_back({ L"ID3v1", L"Artist", artist1 });
            if (!album1.empty())   tagRows.push_back({ L"ID3v1", L"Album", album1 });
            if (!year1.empty())    tagRows.push_back({ L"ID3v1", L"Year", year1 });
            if (!comment1.empty()) tagRows.push_back({ L"ID3v1", L"Comment", comment1 });
            if (t1[125] == 0 && t1[126] != 0) {
                tagRows.push_back({ L"ID3v1.1", L"Track Number", std::to_wstring(t1[126]) });
            }
            if (t1[127] < 192) {
                tagRows.push_back({ L"ID3v1", L"Genre ID", std::to_wstring(t1[127]) });
            }
        }
    }

    // Scan for MPEG Audio Frame Sync
    static const int kBitratesMpeg1[3][16] = {
        { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 }, // Layer I
        { 0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 }, // Layer II
        { 0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 }  // Layer III
    };
    static const int kBitratesMpeg2[3][16] = {
        { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },    // Layer I
        { 0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0 },    // Layer II
        { 0,  8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }     // Layer III
    };
    static const int kSampleRatesMpeg1[4] = { 44100, 48000, 32000, 0 };

    auto decodeFrameAt = [&](size_t off, int& outVerBits, int& outLayerNum, int& outKbps, int& outHz, int& outChanMode, size_t& outFrameLen) -> bool {
        if (off + 4 > raw.size()) return false;
        uint32_t h = DetailSys100::RdU32BE(raw.data() + off);
        if ((h & 0xFFE00000u) != 0xFFE00000u) return false;
        int verBits = static_cast<int>((h >> 19) & 3u);
        int layBits = static_cast<int>((h >> 17) & 3u);
        int brIdx   = static_cast<int>((h >> 12) & 0xFu);
        int srIdx   = static_cast<int>((h >> 10) & 3u);
        int pad     = static_cast<int>((h >> 9) & 1u);
        int chMode  = static_cast<int>((h >> 6) & 3u);
        if (verBits == 1 || layBits == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3) return false;

        int layerNum = (layBits == 3) ? 1 : ((layBits == 2) ? 2 : 3);
        int kbps = (verBits == 3) ? kBitratesMpeg1[layerNum - 1][brIdx] : kBitratesMpeg2[layerNum - 1][brIdx];
        int hz = kSampleRatesMpeg1[srIdx];
        if (verBits == 2) hz /= 2;
        else if (verBits == 0) hz /= 4;
        if (kbps <= 0 || hz <= 0) return false;

        size_t fLen = 0;
        if (layerNum == 1) {
            fLen = (static_cast<size_t>(12 * kbps * 1000 / hz) + static_cast<size_t>(pad)) * 4;
        } else {
            int coef = (layerNum == 3 && verBits != 3) ? 72 : 144;
            fLen = static_cast<size_t>(coef * kbps * 1000 / hz) + static_cast<size_t>(pad);
        }
        if (fLen < 24) return false;

        outVerBits = verBits;
        outLayerNum = layerNum;
        outKbps = kbps;
        outHz = hz;
        outChanMode = chMode;
        outFrameLen = fLen;
        return true;
    };

    bool foundFrame = false;
    size_t frameOff = 0;
    int mpegVerBits = 3, layerNum = 3, bitrateKbps = 0, sampleRateHz = 0, chanMode = 0;
    size_t firstFrameLen = 0;

    const size_t audioLimit = hasId3v1 && raw.size() >= 128 ? (raw.size() - 128) : raw.size();
    const size_t scanMax = std::min<size_t>(audioLimit, audioStart + 131072);
    size_t fallbackOff = 0;
    bool hasFallback = false;

    for (size_t off = audioStart; off + 4 <= scanMax; ++off) {
        if (raw[off] != 0xFF || (raw[off + 1] & 0xE0) != 0xE0) continue;
        int vb = 0, ln = 0, kb = 0, hz = 0, cm = 0;
        size_t fl = 0;
        if (!decodeFrameAt(off, vb, ln, kb, hz, cm, fl)) continue;
        if (!hasFallback) {
            hasFallback = true;
            fallbackOff = off;
        }
        // Confirm next frame sync if file has room; otherwise accept at EOF or when ID3 tag already verified MP3
        if (off + fl + 4 <= audioLimit) {
            int vb2 = 0, ln2 = 0, kb2 = 0, hz2 = 0, cm2 = 0;
            size_t fl2 = 0;
            if (decodeFrameAt(off + fl, vb2, ln2, kb2, hz2, cm2, fl2) && vb2 == vb && ln2 == ln && hz2 == hz) {
                foundFrame = true;
                frameOff = off;
                mpegVerBits = vb; layerNum = ln; bitrateKbps = kb; sampleRateHz = hz; chanMode = cm; firstFrameLen = fl;
                break;
            }
        } else {
            foundFrame = true;
            frameOff = off;
            mpegVerBits = vb; layerNum = ln; bitrateKbps = kb; sampleRateHz = hz; chanMode = cm; firstFrameLen = fl;
            break;
        }
    }
    if (!foundFrame && hasFallback && (hasId3v2 || hasId3v1)) {
        foundFrame = decodeFrameAt(fallbackOff, mpegVerBits, layerNum, bitrateKbps, sampleRateHz, chanMode, firstFrameLen);
        frameOff = fallbackOff;
    }

    if (!hasId3v2 && !foundFrame) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MPEG Audio / ID3 Inspector (.mp3)";

    SpreadsheetSheet info;
    info.name = L"Audio_&_Tags";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"File Size", PathUtils::FormatByteSize(raw.size()) });

    if (hasId3v2) {
        info.rows.push_back({ L"ID3v2 Tag", L"ID3v2." + std::to_wstring(id3Major) + L"." + std::to_wstring(id3Minor) +
            L" (" + PathUtils::FormatByteSize(id3Size) + L")" });
    }
    if (hasId3v1) {
        info.rows.push_back({ L"ID3v1 Footer", L"Present (128 B)" });
    }

    if (foundFrame) {
        std::wstring verStr = (mpegVerBits == 3) ? L"MPEG-1" : ((mpegVerBits == 2) ? L"MPEG-2" : L"MPEG-2.5");
        std::wstring layStr = (layerNum == 1) ? L"Layer I" : ((layerNum == 2) ? L"Layer II" : L"Layer III");
        std::wstring chStr  = (chanMode == 0) ? L"Stereo" : ((chanMode == 1) ? L"Joint Stereo" : ((chanMode == 2) ? L"Dual Channel" : L"Mono"));

        // Check Xing / Info / VBRI VBR header
        uint32_t vbrFrames = 0;
        uint32_t vbrBytes = 0;
        std::wstring rateMode = L"CBR";
        const size_t fEnd = std::min<size_t>(raw.size(), frameOff + firstFrameLen);
        for (size_t p = frameOff + 4; p + 16 <= fEnd && p <= frameOff + 64; ++p) {
            if (std::memcmp(raw.data() + p, "Xing", 4) == 0 || std::memcmp(raw.data() + p, "Info", 4) == 0) {
                rateMode = (std::memcmp(raw.data() + p, "Xing", 4) == 0) ? L"VBR (Xing)" : L"CBR (Info)";
                uint32_t xFlags = DetailSys100::RdU32BE(raw.data() + p + 4);
                size_t xp = p + 8;
                if ((xFlags & 1u) != 0 && xp + 4 <= fEnd) { vbrFrames = DetailSys100::RdU32BE(raw.data() + xp); xp += 4; }
                if ((xFlags & 2u) != 0 && xp + 4 <= fEnd) { vbrBytes = DetailSys100::RdU32BE(raw.data() + xp); }
                break;
            }
            if (std::memcmp(raw.data() + p, "VBRI", 4) == 0 && p + 18 <= fEnd) {
                rateMode = L"VBR (Fraunhofer VBRI)";
                vbrBytes  = DetailSys100::RdU32BE(raw.data() + p + 10);
                vbrFrames = DetailSys100::RdU32BE(raw.data() + p + 14);
                break;
            }
        }

        double durationSec = 0.0;
        int samplesPerFrame = (layerNum == 1) ? 384 : ((layerNum == 3 && mpegVerBits != 3) ? 576 : 1152);
        if (vbrFrames > 0 && sampleRateHz > 0) {
            durationSec = (static_cast<double>(vbrFrames) * samplesPerFrame) / static_cast<double>(sampleRateHz);
            if (durationSec > 0.0) {
                uint64_t streamBytes = (vbrBytes > 0) ? vbrBytes : (audioLimit > frameOff ? (audioLimit - frameOff) : 0);
                bitrateKbps = static_cast<int>((static_cast<double>(streamBytes) * 8.0) / (durationSec * 1000.0) + 0.5);
            }
        } else if (bitrateKbps > 0 && audioLimit > frameOff) {
            durationSec = (static_cast<double>(audioLimit - frameOff) * 8.0) / (static_cast<double>(bitrateKbps) * 1000.0);
        }

        info.rows.push_back({ L"Codec Profile", verStr + L" " + layStr });
        info.rows.push_back({ L"Bitrate", std::to_wstring(bitrateKbps) + L" kbps (" + rateMode + L")" });
        info.rows.push_back({ L"Sample Rate", std::to_wstring(sampleRateHz) + L" Hz" });
        info.rows.push_back({ L"Channel Mode", chStr });
        info.rows.push_back({ L"Duration", FormatDurationSec(durationSec) });
        info.rows.push_back({ L"First Frame Offset", std::to_wstring(frameOff) + L" B (len " + std::to_wstring(firstFrameLen) + L" B)" });
        if (vbrFrames > 0) {
            info.rows.push_back({ L"VBR Frame Count", std::to_wstring(vbrFrames) });
        }
    }

    for (const auto& tr : tagRows) {
        info.rows.push_back({ tr.label + L" [" + tr.id + L"]", tr.value });
    }
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));

    if (!tagRows.empty()) {
        SpreadsheetSheet frames;
        frames.name = L"ID3_Frames";
        frames.rows.push_back({ L"Frame ID", L"Field", L"Value" });
        for (const auto& tr : tagRows) {
            frames.rows.push_back({ tr.id, tr.label, tr.value });
        }
        FinalizeSheetCols(frames);
        out.sheets.push_back(std::move(frames));
    }

    out.valid = true;
    return true;
}

// ============================================================================
// 3. Ogg Container: Vorbis / Opus / Speex (.ogg / .opus / .spx)
// ============================================================================
inline bool ParseOggContainer(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 27 || std::memcmp(raw.data(), "OggS", 4) != 0 || raw[4] != 0) return false;

    size_t pos = 0;
    size_t pageCount = 0;
    size_t packetCount = 0;
    uint32_t serial = DetailSys100::RdU32LE(raw.data() + 14);
    int64_t maxGranule = 0;

    std::vector<uint8_t> curPkt;
    std::vector<std::vector<uint8_t>> headerPackets;

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
        uint64_t gpU = DetailSys100::RdU64LE(raw.data() + pos + 6);
        if (gpU != 0xFFFFFFFFFFFFFFFFULL) {
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
            if (headerPackets.size() < 3 && curPkt.size() < 262144) {
                curPkt.insert(curPkt.end(), raw.data() + dataPos, raw.data() + dataPos + segLen);
            }
            dataPos += segLen;
            if (segLen < 255) {
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

    std::wstring codec = L"Ogg Bitstream";
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t granuleRate = 0;
    uint16_t preSkip = 0;
    int32_t nominalBitrate = 0;
    std::wstring extraCodecInfo;

    const auto& bos = headerPackets[0];
    if (bos.size() >= 30 && bos[0] == 0x01 && std::memcmp(bos.data() + 1, "vorbis", 6) == 0) {
        codec = L"Xiph Vorbis";
        channels = bos[11];
        sampleRate = DetailSys100::RdU32LE(bos.data() + 12);
        granuleRate = sampleRate;
        nominalBitrate = static_cast<int32_t>(DetailSys100::RdU32LE(bos.data() + 20));
    } else if (bos.size() >= 19 && std::memcmp(bos.data(), "OpusHead", 8) == 0) {
        codec = L"IETF / Xiph Opus (v" + std::to_wstring(bos[8]) + L")";
        channels = bos[9];
        preSkip = DetailSys100::RdU16LE(bos.data() + 10);
        sampleRate = DetailSys100::RdU32LE(bos.data() + 12);
        granuleRate = 48000;
        int16_t gain = static_cast<int16_t>(DetailSys100::RdU16LE(bos.data() + 16));
        extraCodecInfo = L"PreSkip=" + std::to_wstring(preSkip) + L", Gain=" + std::to_wstring(gain) + L", MapFamily=" + std::to_wstring(bos[18]);
    } else if (bos.size() >= 80 && std::memcmp(bos.data(), "Speex   ", 8) == 0) {
        std::wstring ver = FixedAsciiField(bos.data() + 8, 20);
        codec = L"Xiph Speex " + ver;
        sampleRate = DetailSys100::RdU32LE(bos.data() + 36);
        granuleRate = sampleRate;
        uint32_t mode = DetailSys100::RdU32LE(bos.data() + 40);
        channels = DetailSys100::RdU32LE(bos.data() + 48);
        nominalBitrate = static_cast<int32_t>(DetailSys100::RdU32LE(bos.data() + 52));
        extraCodecInfo = (mode == 0) ? L"Narrowband" : ((mode == 1) ? L"Wideband" : L"Ultra-Wideband");
    } else if (bos.size() >= 8 && std::memcmp(bos.data(), "\x7F" "FLAC", 5) == 0) {
        codec = L"Ogg FLAC";
    } else if (bos.size() >= 7 && std::memcmp(bos.data(), "\x80theora", 7) == 0) {
        codec = L"Ogg Theora Video";
    }

    std::wstring vendor;
    std::vector<std::pair<std::wstring, std::wstring>> comments;
    if (headerPackets.size() >= 2) {
        const auto& cpkt = headerPackets[1];
        size_t cOff = SIZE_MAX;
        if (cpkt.size() >= 15 && cpkt[0] == 0x03 && std::memcmp(cpkt.data() + 1, "vorbis", 6) == 0) cOff = 7;
        else if (cpkt.size() >= 16 && std::memcmp(cpkt.data(), "OpusTags", 8) == 0) cOff = 8;
        else if (codec.find(L"Speex") != std::wstring::npos && cpkt.size() >= 8) cOff = 0;

        if (cOff != SIZE_MAX && cOff + 4 <= cpkt.size()) {
            uint32_t vLen = DetailSys100::RdU32LE(cpkt.data() + cOff);
            cOff += 4;
            if (vLen <= cpkt.size() - cOff) {
                vendor = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(cpkt.data() + cOff), vLen)));
                cOff += vLen;
                if (cOff + 4 <= cpkt.size()) {
                    uint32_t numComments = DetailSys100::RdU32LE(cpkt.data() + cOff);
                    cOff += 4;
                    for (uint32_t i = 0; i < numComments && i < 512 && cOff + 4 <= cpkt.size(); ++i) {
                        uint32_t clen = DetailSys100::RdU32LE(cpkt.data() + cOff);
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

    double durationSec = 0.0;
    if (granuleRate > 0 && maxGranule > preSkip) {
        durationSec = static_cast<double>(maxGranule - preSkip) / static_cast<double>(granuleRate);
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Xiph Ogg Audio Container (" + extLower + L")";

    SpreadsheetSheet info;
    info.name = L"Ogg_Stream";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"Codec", codec });
    info.rows.push_back({ L"Bitstream Serial", DetailSys100::Hex32(serial) });
    info.rows.push_back({ L"Ogg Pages / Packets", std::to_wstring(pageCount) + L" / " + std::to_wstring(packetCount) });
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

// ============================================================================
// 4. Demoscene Tracker Modules (.mod / .xm / .s3m / .it)
// ============================================================================
inline bool ParseTrackerMod(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 1084) return false;
    std::string sig(reinterpret_cast<const char*>(raw.data() + 1080), 4);
    int channels = 0;
    if (sig == "M.K." || sig == "M!K!" || sig == "M&K!" || sig == "FLT4") channels = 4;
    else if (sig == "FLT8" || sig == "OCTA" || sig == "CD81" || sig == "OKTA") channels = 8;
    else if (sig.size() == 4 && sig.substr(1, 3) == "CHN" && sig[0] >= '1' && sig[0] <= '9') channels = sig[0] - '0';
    else if (sig.size() == 4 && sig.substr(2, 2) == "CH" && sig[0] >= '0' && sig[0] <= '9' && sig[1] >= '0' && sig[1] <= '9')
        channels = (sig[0] - '0') * 10 + (sig[1] - '0');
    else if (sig.size() == 4 && sig.substr(0, 3) == "TDZ" && sig[3] >= '1' && sig[3] <= '3') channels = sig[3] - '0';
    if (channels <= 0 || channels > 64) return false;

    std::wstring songTitle = FixedAsciiField(raw.data(), 20);
    uint8_t songLength = raw[950];
    uint8_t restartPos = raw[951];
    uint8_t maxPat = 0;
    std::wstring orderPreview;
    for (int i = 0; i < 128; ++i) {
        if (raw[952 + i] > maxPat) maxPat = raw[952 + i];
        if (i < static_cast<int>(songLength) && i < 24) {
            if (!orderPreview.empty()) orderPreview += L", ";
            orderPreview += std::to_wstring(raw[952 + i]);
        }
    }
    int numPatterns = static_cast<int>(maxPat) + 1;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ProTracker / Amiga Module (.mod)";

    SpreadsheetSheet hdr;
    hdr.name = L"MOD_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Song Title", songTitle.empty() ? L"(Untitled)" : songTitle });
    hdr.rows.push_back({ L"Format Tag", PathUtils::Utf8ToWide(sig) });
    hdr.rows.push_back({ L"Channels", std::to_wstring(channels) });
    hdr.rows.push_back({ L"Song Length (Orders)", std::to_wstring(songLength) });
    hdr.rows.push_back({ L"Restart Position", std::to_wstring(restartPos) });
    hdr.rows.push_back({ L"Pattern Count", std::to_wstring(numPatterns) });
    hdr.rows.push_back({ L"Order Table", orderPreview });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet smp;
    smp.name = L"Samples";
    smp.rows.push_back({ L"#", L"Sample Name", L"Length (Bytes)", L"Finetune", L"Volume", L"Loop Start", L"Loop Length" });
    for (int i = 0; i < 31; ++i) {
        const uint8_t* sp = raw.data() + 20 + i * 30;
        std::wstring sName = FixedAsciiField(sp, 22);
        uint32_t lenBytes = static_cast<uint32_t>(DetailSys100::RdU16BE(sp + 22)) * 2u;
        int ft = sp[24] & 0x0F;
        if (ft >= 8) ft -= 16;
        uint8_t vol = sp[25];
        uint32_t loopStart = static_cast<uint32_t>(DetailSys100::RdU16BE(sp + 26)) * 2u;
        uint32_t loopLen   = static_cast<uint32_t>(DetailSys100::RdU16BE(sp + 28)) * 2u;
        if (!sName.empty() || lenBytes > 0) {
            smp.rows.push_back({
                std::to_wstring(i + 1), sName, std::to_wstring(lenBytes),
                std::to_wstring(ft), std::to_wstring(vol),
                std::to_wstring(loopStart), std::to_wstring(loopLen)
            });
        }
    }
    FinalizeSheetCols(smp);

    out.sheets.push_back(std::move(hdr));
    out.sheets.push_back(std::move(smp));
    out.valid = true;
    return true;
}

inline bool ParseTrackerXm(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 80 || std::memcmp(raw.data(), "Extended Module: ", 17) != 0 || raw[37] != 0x1A) return false;

    std::wstring modName     = FixedAsciiField(raw.data() + 17, 20);
    std::wstring trackerName = FixedAsciiField(raw.data() + 38, 20);
    uint16_t xmVer       = DetailSys100::RdU16LE(raw.data() + 58);
    uint32_t hdrSize     = DetailSys100::RdU32LE(raw.data() + 60);
    uint16_t songLen     = DetailSys100::RdU16LE(raw.data() + 64);
    uint16_t restartPos  = DetailSys100::RdU16LE(raw.data() + 66);
    uint16_t numChans    = DetailSys100::RdU16LE(raw.data() + 68);
    uint16_t numPats     = DetailSys100::RdU16LE(raw.data() + 70);
    uint16_t numInsts    = DetailSys100::RdU16LE(raw.data() + 72);
    uint16_t flags       = DetailSys100::RdU16LE(raw.data() + 74);
    uint16_t defTempo    = DetailSys100::RdU16LE(raw.data() + 76);
    uint16_t defBpm      = DetailSys100::RdU16LE(raw.data() + 78);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"FastTracker II Extended Module (.xm)";

    SpreadsheetSheet hdr;
    hdr.name = L"XM_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Module Title", modName.empty() ? L"(Untitled)" : modName });
    hdr.rows.push_back({ L"Tracker", trackerName });
    hdr.rows.push_back({ L"XM Version", std::to_wstring(xmVer >> 8) + L"." + std::to_wstring(xmVer & 0xFF) });
    hdr.rows.push_back({ L"Channels", std::to_wstring(numChans) });
    hdr.rows.push_back({ L"Song Length / Restart", std::to_wstring(songLen) + L" / " + std::to_wstring(restartPos) });
    hdr.rows.push_back({ L"Patterns / Instruments", std::to_wstring(numPats) + L" / " + std::to_wstring(numInsts) });
    hdr.rows.push_back({ L"Default Tempo / BPM", std::to_wstring(defTempo) + L" / " + std::to_wstring(defBpm) });
    hdr.rows.push_back({ L"Frequency Table", (flags & 1u) ? L"Linear" : L"Amiga" });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet patSh;
    patSh.name = L"Patterns";
    patSh.rows.push_back({ L"Pattern #", L"Rows", L"Packed Bytes" });

    size_t pos = 60ULL + hdrSize;
    for (uint16_t p = 0; p < numPats && pos + 9 <= raw.size(); ++p) {
        uint32_t pHdrLen = DetailSys100::RdU32LE(raw.data() + pos);
        uint16_t nRows   = DetailSys100::RdU16LE(raw.data() + pos + 5);
        uint16_t pkSize  = DetailSys100::RdU16LE(raw.data() + pos + 7);
        patSh.rows.push_back({ std::to_wstring(p), std::to_wstring(nRows), std::to_wstring(pkSize) });
        if (pHdrLen < 7 || pos + pHdrLen + pkSize > raw.size()) { pos = raw.size(); break; }
        pos += pHdrLen + pkSize;
    }
    FinalizeSheetCols(patSh);

    SpreadsheetSheet insSh;
    insSh.name = L"Instruments";
    insSh.rows.push_back({ L"Inst #", L"Instrument Name", L"Num Samples", L"Total Sample Bytes" });
    for (uint16_t i = 0; i < numInsts && pos + 29 <= raw.size(); ++i) {
        uint32_t iSize = DetailSys100::RdU32LE(raw.data() + pos);
        if (iSize < 29 || pos + iSize > raw.size()) break;
        std::wstring iName = FixedAsciiField(raw.data() + pos + 4, 22);
        uint16_t nSmp = DetailSys100::RdU16LE(raw.data() + pos + 27);
        uint32_t smpHdrSize = (nSmp > 0 && iSize >= 33) ? DetailSys100::RdU32LE(raw.data() + pos + 29) : 40;
        if (smpHdrSize == 0 || smpHdrSize > 4096) smpHdrSize = 40;
        pos += iSize;

        uint64_t totalSmpBytes = 0;
        for (uint16_t s = 0; s < nSmp && pos + smpHdrSize <= raw.size(); ++s) {
            if (smpHdrSize >= 4) totalSmpBytes += DetailSys100::RdU32LE(raw.data() + pos);
            pos += smpHdrSize;
        }
        insSh.rows.push_back({ std::to_wstring(i + 1), iName, std::to_wstring(nSmp), std::to_wstring(totalSmpBytes) });
        if (totalSmpBytes > raw.size() - pos) { pos = raw.size(); break; }
        pos += static_cast<size_t>(totalSmpBytes);
    }
    FinalizeSheetCols(insSh);

    out.sheets.push_back(std::move(hdr));
    if (patSh.rows.size() > 1) out.sheets.push_back(std::move(patSh));
    if (insSh.rows.size() > 1) out.sheets.push_back(std::move(insSh));
    out.valid = true;
    return true;
}

inline bool ParseTrackerS3m(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 96 || std::memcmp(raw.data() + 44, "SCRM", 4) != 0) return false;

    std::wstring title = FixedAsciiField(raw.data(), 28);
    uint16_t ordNum = DetailSys100::RdU16LE(raw.data() + 32);
    uint16_t insNum = DetailSys100::RdU16LE(raw.data() + 34);
    uint16_t patNum = DetailSys100::RdU16LE(raw.data() + 36);
    uint16_t cwtv   = DetailSys100::RdU16LE(raw.data() + 40);
    uint8_t initSpd = raw[49];
    uint8_t initTmp = raw[50];

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"ScreamTracker 3 Module (.s3m)";

    SpreadsheetSheet hdr;
    hdr.name = L"S3M_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Title", title.empty() ? L"(Untitled)" : title });
    hdr.rows.push_back({ L"Tracker Version", DetailSys100::Hex32(cwtv) });
    hdr.rows.push_back({ L"Orders / Instruments / Patterns",
        std::to_wstring(ordNum) + L" / " + std::to_wstring(insNum) + L" / " + std::to_wstring(patNum) });
    hdr.rows.push_back({ L"Initial Speed / Tempo", std::to_wstring(initSpd) + L" / " + std::to_wstring(initTmp) + L" BPM" });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet ins;
    ins.name = L"Instruments";
    ins.rows.push_back({ L"#", L"Instrument Name", L"DOS Filename", L"Length", L"C2Spd (Hz)", L"Volume" });
    size_t ptrTable = 96ULL + ordNum;
    for (uint16_t i = 0; i < insNum && ptrTable + static_cast<size_t>(i + 1) * 2 <= raw.size(); ++i) {
        size_t iOff = static_cast<size_t>(DetailSys100::RdU16LE(raw.data() + ptrTable + i * 2)) * 16ULL;
        if (iOff == 0 || iOff + 80 > raw.size()) continue;
        std::wstring dosFn = FixedAsciiField(raw.data() + iOff + 1, 12);
        uint32_t len = DetailSys100::RdU32LE(raw.data() + iOff + 16);
        uint8_t vol = raw[iOff + 28];
        uint32_t c2spd = DetailSys100::RdU32LE(raw.data() + iOff + 32);
        std::wstring sName = FixedAsciiField(raw.data() + iOff + 48, 28);
        ins.rows.push_back({ std::to_wstring(i + 1), sName, dosFn, std::to_wstring(len), std::to_wstring(c2spd), std::to_wstring(vol) });
    }
    FinalizeSheetCols(ins);

    out.sheets.push_back(std::move(hdr));
    if (ins.rows.size() > 1) out.sheets.push_back(std::move(ins));
    out.valid = true;
    return true;
}

inline bool ParseTrackerIt(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 192 || std::memcmp(raw.data(), "IMPM", 4) != 0) return false;

    std::wstring songName = FixedAsciiField(raw.data() + 4, 26);
    uint16_t ordNum = DetailSys100::RdU16LE(raw.data() + 32);
    uint16_t insNum = DetailSys100::RdU16LE(raw.data() + 34);
    uint16_t smpNum = DetailSys100::RdU16LE(raw.data() + 36);
    uint16_t patNum = DetailSys100::RdU16LE(raw.data() + 38);
    uint16_t cwtv   = DetailSys100::RdU16LE(raw.data() + 40);
    uint8_t initSpd = raw[50];
    uint8_t initTmp = raw[51];

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Impulse Tracker Module (.it)";

    SpreadsheetSheet hdr;
    hdr.name = L"IT_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"Song Name", songName.empty() ? L"(Untitled)" : songName });
    hdr.rows.push_back({ L"Tracker CWTV", DetailSys100::Hex32(cwtv) });
    hdr.rows.push_back({ L"Orders / Instruments / Samples / Patterns",
        std::to_wstring(ordNum) + L" / " + std::to_wstring(insNum) + L" / " + std::to_wstring(smpNum) + L" / " + std::to_wstring(patNum) });
    hdr.rows.push_back({ L"Initial Speed / Tempo", std::to_wstring(initSpd) + L" / " + std::to_wstring(initTmp) + L" BPM" });
    FinalizeSheetCols(hdr);

    SpreadsheetSheet tbl;
    tbl.name = L"Instruments_&_Samples";
    tbl.rows.push_back({ L"Kind", L"#", L"Name", L"DOS Filename", L"Length", L"C5Speed (Hz)" });

    size_t insTab = 192ULL + ordNum;
    for (uint16_t i = 0; i < insNum && insTab + static_cast<size_t>(i + 1) * 4 <= raw.size(); ++i) {
        uint32_t off = DetailSys100::RdU32LE(raw.data() + insTab + i * 4);
        if (off + 60 <= raw.size() && std::memcmp(raw.data() + off, "IMPI", 4) == 0) {
            tbl.rows.push_back({
                L"Instrument", std::to_wstring(i + 1),
                FixedAsciiField(raw.data() + off + 32, 26),
                FixedAsciiField(raw.data() + off + 4, 12), L"-", L"-"
            });
        }
    }
    size_t smpTab = insTab + static_cast<size_t>(insNum) * 4;
    for (uint16_t s = 0; s < smpNum && smpTab + static_cast<size_t>(s + 1) * 4 <= raw.size(); ++s) {
        uint32_t off = DetailSys100::RdU32LE(raw.data() + smpTab + s * 4);
        if (off + 64 <= raw.size() && std::memcmp(raw.data() + off, "IMPS", 4) == 0) {
            tbl.rows.push_back({
                L"Sample", std::to_wstring(s + 1),
                FixedAsciiField(raw.data() + off + 20, 26),
                FixedAsciiField(raw.data() + off + 4, 12),
                std::to_wstring(DetailSys100::RdU32LE(raw.data() + off + 48)),
                std::to_wstring(DetailSys100::RdU32LE(raw.data() + off + 60))
            });
        }
    }
    FinalizeSheetCols(tbl);

    out.sheets.push_back(std::move(hdr));
    if (tbl.rows.size() > 1) out.sheets.push_back(std::move(tbl));
    out.valid = true;
    return true;
}

// ============================================================================
// 5. SoundFont 2 (.sf2) & SFZ (.sfz)
// ============================================================================
inline bool ParseSoundFontSf2(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 12 || std::memcmp(raw.data(), "RIFF", 4) != 0 || std::memcmp(raw.data() + 8, "sfbk", 4) != 0) return false;

    SpreadsheetSheet info;
    info.name = L"SF2_Info";
    info.rows.push_back({ L"Chunk / Field", L"Value" });
    info.rows.push_back({ L"File Size", PathUtils::FormatByteSize(raw.size()) });

    SpreadsheetSheet presets;
    presets.name = L"Presets";
    presets.rows.push_back({ L"Bank", L"Preset #", L"Preset Name" });

    SpreadsheetSheet insts;
    insts.name = L"Instruments";
    insts.rows.push_back({ L"#", L"Instrument Name", L"Bag Index" });

    SpreadsheetSheet samples;
    samples.name = L"Samples";
    samples.rows.push_back({ L"Sample Name", L"Start", L"End", L"Sample Rate (Hz)", L"Root Pitch" });

    size_t pos = 12;
    while (pos + 8 <= raw.size()) {
        std::string cid(reinterpret_cast<const char*>(raw.data() + pos), 4);
        uint32_t csz = DetailSys100::RdU32LE(raw.data() + pos + 4);
        size_t bodyOff = pos + 8;
        size_t bodyEnd = std::min<size_t>(raw.size(), bodyOff + csz);

        if (cid == "LIST" && bodyOff + 4 <= bodyEnd) {
            std::string listType(reinterpret_cast<const char*>(raw.data() + bodyOff), 4);
            size_t sp = bodyOff + 4;
            while (sp + 8 <= bodyEnd) {
                std::string sid(reinterpret_cast<const char*>(raw.data() + sp), 4);
                uint32_t ssz = DetailSys100::RdU32LE(raw.data() + sp + 4);
                size_t sData = sp + 8;
                size_t sEnd = std::min<size_t>(bodyEnd, sData + ssz);
                const uint8_t* p = raw.data() + sData;
                size_t avail = sEnd - sData;

                if (listType == "INFO") {
                    if ((sid == "ifil" || sid == "iver") && avail >= 4) {
                        info.rows.push_back({ PathUtils::Utf8ToWide(sid),
                            std::to_wstring(DetailSys100::RdU16LE(p)) + L"." + std::to_wstring(DetailSys100::RdU16LE(p + 2)) });
                    } else if (avail > 0) {
                        std::wstring val = FixedAsciiField(p, avail);
                        if (!val.empty()) info.rows.push_back({ PathUtils::Utf8ToWide(sid), val });
                    }
                } else if (listType == "sdta" && sid == "smpl") {
                    info.rows.push_back({ L"PCM Sample Pool (smpl)", PathUtils::FormatByteSize(ssz) });
                } else if (listType == "pdta") {
                    if (sid == "phdr") {
                        size_t n = avail / 38;
                        for (size_t i = 0; i + 1 < n && presets.rows.size() < 1024; ++i) {
                            const uint8_t* r = p + i * 38;
                            presets.rows.push_back({
                                std::to_wstring(DetailSys100::RdU16LE(r + 22)),
                                std::to_wstring(DetailSys100::RdU16LE(r + 20)),
                                FixedAsciiField(r, 20)
                            });
                        }
                    } else if (sid == "inst") {
                        size_t n = avail / 22;
                        for (size_t i = 0; i + 1 < n && insts.rows.size() < 1024; ++i) {
                            const uint8_t* r = p + i * 22;
                            insts.rows.push_back({
                                std::to_wstring(i),
                                FixedAsciiField(r, 20),
                                std::to_wstring(DetailSys100::RdU16LE(r + 20))
                            });
                        }
                    } else if (sid == "shdr") {
                        size_t n = avail / 46;
                        for (size_t i = 0; i + 1 < n && samples.rows.size() < 2048; ++i) {
                            const uint8_t* r = p + i * 46;
                            samples.rows.push_back({
                                FixedAsciiField(r, 20),
                                std::to_wstring(DetailSys100::RdU32LE(r + 20)),
                                std::to_wstring(DetailSys100::RdU32LE(r + 24)),
                                std::to_wstring(DetailSys100::RdU32LE(r + 36)),
                                std::to_wstring(r[40])
                            });
                        }
                    }
                }
                sp = sData + ssz + (ssz & 1u);
            }
        }
        pos = bodyOff + csz + (csz & 1u);
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SoundFont 2 Sample Bank (.sf2)";
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));
    if (presets.rows.size() > 1) { FinalizeSheetCols(presets); out.sheets.push_back(std::move(presets)); }
    if (insts.rows.size() > 1)   { FinalizeSheetCols(insts);   out.sheets.push_back(std::move(insts)); }
    if (samples.rows.size() > 1) { FinalizeSheetCols(samples); out.sheets.push_back(std::move(samples)); }
    out.valid = true;
    return true;
}

inline bool ParseSoundFontSfz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 8) return false;
    for (size_t i = 0; i < std::min<size_t>(raw.size(), 2048); ++i) {
        if (raw[i] == 0) return false;
    }

    // Strip // and /* */ comments
    std::string clean;
    clean.reserve(raw.size());
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '/') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            clean.push_back('\n');
        } else if (i + 1 < sv.size() && sv[i] == '/' && sv[i + 1] == '*') {
            size_t endC = sv.find("*/", i + 2);
            if (endC == std::string_view::npos) break;
            i = endC + 1;
            clean.push_back(' ');
        } else {
            clean.push_back(sv[i]);
        }
    }

    std::string curSection;
    std::map<std::string, std::string> globalOps;
    std::map<std::string, std::string> groupOps;
    std::map<std::string, std::string> curOps;
    std::vector<std::map<std::string, std::string>> regions;
    int headerCount = 0;

    auto flushRegionIfActive = [&]() {
        if (curSection == "region" && !curOps.empty()) {
            std::map<std::string, std::string> merged = globalOps;
            for (const auto& kv : groupOps) merged[kv.first] = kv.second;
            for (const auto& kv : curOps) merged[kv.first] = kv.second;
            regions.push_back(std::move(merged));
        } else if (curSection == "global") {
            for (const auto& kv : curOps) globalOps[kv.first] = kv.second;
        } else if (curSection == "group") {
            groupOps = curOps;
        }
        curOps.clear();
    };

    size_t i = 0;
    while (i < clean.size()) {
        while (i < clean.size() && static_cast<unsigned char>(clean[i]) <= 32) ++i;
        if (i >= clean.size()) break;

        if (clean[i] == '<') {
            size_t gt = clean.find('>', i + 1);
            if (gt == std::string::npos) break;
            flushRegionIfActive();
            curSection = PathUtils::ToLowerAscii(std::string_view(clean).substr(i + 1, gt - i - 1));
            ++headerCount;
            i = gt + 1;
            continue;
        }

        size_t eq = clean.find('=', i);
        size_t nextLt = clean.find('<', i);
        if (eq == std::string::npos || (nextLt != std::string::npos && nextLt < eq)) {
            i = (nextLt != std::string::npos) ? nextLt : clean.size();
            continue;
        }
        std::string key = PathUtils::ToLowerAscii(std::string_view(clean).substr(i, eq - i));
        // Trim key whitespace (take last word if stray token preceded)
        size_t lastSp = key.find_last_of(" \t\r\n");
        if (lastSp != std::string::npos) key = key.substr(lastSp + 1);
        i = eq + 1;

        std::string val;
        if (key == "sample" || key == "default_path") {
            // Sample paths can contain spaces; read until newline, '<', or next key= token
            size_t lineEnd = clean.find_first_of("\r\n<", i);
            if (lineEnd == std::string::npos) lineEnd = clean.size();
            std::string_view rem(clean.data() + i, lineEnd - i);
            size_t nextEq = rem.find('=');
            if (nextEq != std::string_view::npos) {
                size_t spBefore = rem.rfind(' ', nextEq);
                if (spBefore != std::string_view::npos) rem = rem.substr(0, spBefore);
            }
            val = std::string(rem);
            i += rem.size();
        } else {
            size_t vEnd = i;
            while (vEnd < clean.size() && static_cast<unsigned char>(clean[vEnd]) > 32 && clean[vEnd] != '<') ++vEnd;
            val = clean.substr(i, vEnd - i);
            i = vEnd;
        }
        while (!val.empty() && (val.front() == ' ' || val.front() == '"')) val.erase(val.begin());
        while (!val.empty() && (val.back() == ' ' || val.back() == '"')) val.pop_back();
        if (!key.empty()) curOps[key] = val;
    }
    flushRegionIfActive();

    if (headerCount == 0 || (regions.empty() && globalOps.empty() && groupOps.empty())) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"SFZ Instrument Definition (.sfz)";

    SpreadsheetSheet regSh;
    regSh.name = L"SFZ_Regions";
    regSh.rows.push_back({ L"#", L"Sample", L"LoKey", L"HiKey", L"PitchCenter", L"LoVel", L"HiVel", L"Volume", L"Pan" });
    auto getOp = [](const std::map<std::string, std::string>& m, const char* k, const char* fallback = "") -> std::wstring {
        auto it = m.find(k);
        if (it != m.end()) return PathUtils::Utf8ToWide(it->second);
        if (fallback[0] != 0) {
            auto it2 = m.find(fallback);
            if (it2 != m.end()) return PathUtils::Utf8ToWide(it2->second);
        }
        return L"-";
    };
    for (size_t r = 0; r < regions.size(); ++r) {
        const auto& m = regions[r];
        regSh.rows.push_back({
            std::to_wstring(r + 1),
            getOp(m, "sample"),
            getOp(m, "lokey", "key"),
            getOp(m, "hikey", "key"),
            getOp(m, "pitch_keycenter", "key"),
            getOp(m, "lovel"),
            getOp(m, "hivel"),
            getOp(m, "volume"),
            getOp(m, "pan")
        });
    }
    if (regions.empty()) {
        for (const auto& kv : globalOps) {
            regSh.rows.push_back({ L"global", PathUtils::Utf8ToWide(kv.first), PathUtils::Utf8ToWide(kv.second) });
        }
    }
    FinalizeSheetCols(regSh);
    out.sheets.push_back(std::move(regSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 6. Score & DAW Formats (.mscz, .als, .flp, .gp/.gp3/.gp4/.gp5, .rpp)
// ============================================================================
inline bool ParseMuseScoreMscz(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(raw.data(), raw.size(), entries) || entries.empty()) return false;

    std::vector<uint8_t> mscxBytes;
    std::wstring mscxName;
    for (const auto& e : entries) {
        if (!e.isDirectory && PathUtils::GetExtension(e.pathInArchive) == L".mscx") {
            if (ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), e, mscxBytes) && !mscxBytes.empty()) {
                mscxName = e.pathInArchive;
                break;
            }
        }
    }
    if (mscxBytes.empty()) return false;

    std::string_view xml(reinterpret_cast<const char*>(mscxBytes.data()), mscxBytes.size());
    if (xml.find("<museScore") == std::string_view::npos) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MuseScore Compressed Score (.mscz)";

    SpreadsheetSheet meta;
    meta.name = L"Score_Metadata";
    meta.rows.push_back({ L"Field", L"Value" });
    meta.rows.push_back({ L"Score XML Entry", mscxName });

    size_t msPos = xml.find("<museScore");
    if (msPos != std::string_view::npos) {
        size_t gt = xml.find('>', msPos);
        if (gt != std::string_view::npos) {
            std::string ver = ExtractXmlAttr(xml.substr(msPos, gt - msPos), "version");
            if (!ver.empty()) meta.rows.push_back({ L"MuseScore Version", PathUtils::Utf8ToWide(ver) });
        }
    }

    // Parse <metaTag name="...">...</metaTag>
    size_t p = 0;
    while ((p = xml.find("<metaTag", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        size_t cl = (gt != std::string_view::npos) ? xml.find("</metaTag>", gt) : std::string_view::npos;
        if (gt == std::string_view::npos || cl == std::string_view::npos) break;
        std::string name = ExtractXmlAttr(xml.substr(p, gt - p), "name");
        std::wstring val = DecodeHtmlEntitiesMedia(PathUtils::Trim(PathUtils::Utf8ToWide(xml.substr(gt + 1, cl - gt - 1))));
        if (!name.empty() && !val.empty()) {
            meta.rows.push_back({ L"Meta: " + PathUtils::Utf8ToWide(name), val });
        }
        p = cl + 10;
    }

    // Parse <TimeSig> and <Tempo>
    size_t tsPos = xml.find("<TimeSig>");
    if (tsPos != std::string_view::npos) {
        size_t tsEnd = xml.find("</TimeSig>", tsPos);
        if (tsEnd != std::string_view::npos) {
            std::string_view tsBlk = xml.substr(tsPos, tsEnd - tsPos);
            std::wstring sigN = ExtractFirstXmlTagText(tsBlk, "sigN");
            std::wstring sigD = ExtractFirstXmlTagText(tsBlk, "sigD");
            if (!sigN.empty() && !sigD.empty()) meta.rows.push_back({ L"Time Signature", sigN + L"/" + sigD });
        }
    }
    size_t tmpPos = xml.find("<Tempo>");
    if (tmpPos != std::string_view::npos) {
        size_t tmpEnd = xml.find("</Tempo>", tmpPos);
        if (tmpEnd != std::string_view::npos) {
            std::wstring tVal = ExtractFirstXmlTagText(xml.substr(tmpPos, tmpEnd - tmpPos), "tempo");
            if (!tVal.empty()) {
                double bps = std::wcstod(tVal.c_str(), nullptr);
                int bpm = static_cast<int>(bps * 60.0 + 0.5);
                meta.rows.push_back({ L"Tempo", std::to_wstring(bpm) + L" BPM" });
            }
        }
    }

    SpreadsheetSheet parts;
    parts.name = L"Parts_&_Staves";
    parts.rows.push_back({ L"Item", L"Name / ID", L"Details" });
    p = 0;
    int partIdx = 0;
    while ((p = xml.find("<Part", p)) != std::string_view::npos) {
        if (p + 5 < xml.size() && xml[p + 5] != '>' && xml[p + 5] != ' ') { p += 5; continue; }
        size_t cl = xml.find("</Part>", p);
        if (cl == std::string_view::npos) break;
        std::string_view blk = xml.substr(p, cl - p);
        std::wstring tName = ExtractFirstXmlTagText(blk, "trackName");
        if (tName.empty()) tName = ExtractFirstXmlTagText(blk, "longName");
        std::wstring instId = ExtractFirstXmlTagText(blk, "instrumentId");
        parts.rows.push_back({ L"Part " + std::to_wstring(++partIdx), tName.empty() ? L"(Part)" : tName, instId });
        p = cl + 7;
    }

    p = 0;
    while ((p = xml.find("<Staff ", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        size_t cl = (gt != std::string_view::npos) ? xml.find("</Staff>", gt) : std::string_view::npos;
        if (gt == std::string_view::npos || cl == std::string_view::npos) break;
        std::string sid = ExtractXmlAttr(xml.substr(p, gt - p), "id");
        std::string_view blk = xml.substr(gt + 1, cl - gt - 1);
        size_t mCount = 0;
        size_t mp = 0;
        while ((mp = blk.find("<Measure", mp)) != std::string_view::npos) { ++mCount; mp += 8; }
        if (mCount > 0) {
            parts.rows.push_back({ L"Staff", L"ID " + PathUtils::Utf8ToWide(sid), std::to_wstring(mCount) + L" measures" });
        }
        p = cl + 8;
    }

    FinalizeSheetCols(meta);
    out.sheets.push_back(std::move(meta));
    if (parts.rows.size() > 1) { FinalizeSheetCols(parts); out.sheets.push_back(std::move(parts)); }
    out.valid = true;
    return true;
}

inline bool ParseAbletonAls(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::vector<uint8_t> uncomp;
    std::string_view xml;
    if (raw.size() >= 2 && raw[0] == 0x1F && raw[1] == 0x8B) {
        if (!ZipDeflate::InflateGzip(raw.data(), raw.size(), uncomp, 64 * 1024 * 1024) || uncomp.empty()) return false;
        xml = std::string_view(reinterpret_cast<const char*>(uncomp.data()), uncomp.size());
    } else {
        xml = std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size());
    }

    size_t abPos = xml.find("<Ableton");
    if (abPos == std::string_view::npos) return false;
    size_t abGt = xml.find('>', abPos);
    if (abGt == std::string_view::npos) return false;

    std::string_view rootTag = xml.substr(abPos, abGt - abPos);
    std::string creator  = ExtractXmlAttr(rootTag, "Creator");
    std::string majVer   = ExtractXmlAttr(rootTag, "MajorVersion");
    std::string minVer   = ExtractXmlAttr(rootTag, "MinorVersion");

    std::string tempoVal;
    size_t tPos = xml.find("<Tempo>");
    if (tPos != std::string_view::npos) {
        size_t mPos = xml.find("<Manual", tPos);
        size_t tEnd = xml.find("</Tempo>", tPos);
        if (mPos != std::string_view::npos && (tEnd == std::string_view::npos || mPos < tEnd)) {
            size_t mGt = xml.find('>', mPos);
            if (mGt != std::string_view::npos) tempoVal = ExtractXmlAttr(xml.substr(mPos, mGt - mPos), "Value");
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Ableton Live Set (.als)";

    SpreadsheetSheet info;
    info.name = L"Ableton_Project";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"Creator", creator.empty() ? L"Ableton Live" : DecodeHtmlEntitiesMedia(PathUtils::Utf8ToWide(creator)) });
    if (!majVer.empty()) info.rows.push_back({ L"Schema Version", PathUtils::Utf8ToWide(majVer + "." + minVer) });
    if (!tempoVal.empty()) info.rows.push_back({ L"Master Tempo", PathUtils::Utf8ToWide(tempoVal) + L" BPM" });
    info.rows.push_back({ L"Uncompressed XML Size", PathUtils::FormatByteSize(xml.size()) });

    SpreadsheetSheet tracks;
    tracks.name = L"Tracks";
    tracks.rows.push_back({ L"#", L"Track Type", L"Track Name", L"Clips" });

    static const char* kTrackTags[] = { "AudioTrack", "MidiTrack", "ReturnTrack", "GroupTrack" };
    int trkIdx = 0;
    for (const char* tTag : kTrackTags) {
        std::string openT = std::string("<") + tTag;
        std::string closeT = std::string("</") + tTag + ">";
        size_t p = 0;
        while ((p = xml.find(openT, p)) != std::string_view::npos) {
            char nextC = (p + openT.size() < xml.size()) ? xml[p + openT.size()] : 0;
            if (nextC != ' ' && nextC != '>') { p += openT.size(); continue; }
            size_t cl = xml.find(closeT, p);
            if (cl == std::string_view::npos) break;
            std::string_view blk = xml.substr(p, cl - p);

            std::string effName;
            size_t en = blk.find("<EffectiveName");
            if (en != std::string_view::npos) {
                size_t gt = blk.find('>', en);
                if (gt != std::string_view::npos) effName = ExtractXmlAttr(blk.substr(en, gt - en), "Value");
            }
            if (effName.empty()) {
                size_t un = blk.find("<UserName");
                if (un != std::string_view::npos) {
                    size_t gt = blk.find('>', un);
                    if (gt != std::string_view::npos) effName = ExtractXmlAttr(blk.substr(un, gt - un), "Value");
                }
            }
            size_t clips = 0;
            size_t cp = 0;
            while ((cp = blk.find("<AudioClip", cp)) != std::string_view::npos) { ++clips; cp += 10; }
            cp = 0;
            while ((cp = blk.find("<MidiClip", cp)) != std::string_view::npos) { ++clips; cp += 9; }

            tracks.rows.push_back({
                std::to_wstring(++trkIdx),
                PathUtils::Utf8ToWide(tTag),
                effName.empty() ? L"(Track)" : DecodeHtmlEntitiesMedia(PathUtils::Utf8ToWide(effName)),
                std::to_wstring(clips)
            });
            p = cl + closeT.size();
        }
    }
    info.rows.push_back({ L"Total Tracks", std::to_wstring(trkIdx) });
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));
    if (tracks.rows.size() > 1) { FinalizeSheetCols(tracks); out.sheets.push_back(std::move(tracks)); }
    out.valid = true;
    return true;
}

inline bool ParseFlStudioFlp(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 22 || std::memcmp(raw.data(), "FLhd", 4) != 0 ||
        DetailSys100::RdU32LE(raw.data() + 4) != 6 || std::memcmp(raw.data() + 14, "FLdt", 4) != 0)
    {
        return false;
    }

    int16_t flpFormat = static_cast<int16_t>(DetailSys100::RdU16LE(raw.data() + 8));
    uint16_t numChannels = DetailSys100::RdU16LE(raw.data() + 10);
    uint16_t ppq = DetailSys100::RdU16LE(raw.data() + 12);
    uint32_t dataLen = DetailSys100::RdU32LE(raw.data() + 18);

    size_t pos = 22;
    size_t end = std::min<size_t>(raw.size(), 22ULL + dataLen);
    bool isUnicode = false;
    std::wstring flVersion;
    std::wstring projTitle;
    std::wstring projAuthor;
    std::wstring projGenre;
    std::wstring projComment;
    double tempoBpm = 0.0;
    size_t eventCount = 0;

    std::vector<std::pair<std::wstring, std::wstring>> items;

    auto decodeFlpText = [&](const uint8_t* p, size_t len, bool forceAscii) -> std::wstring {
        if (len == 0) return {};
        if (!forceAscii && (isUnicode || (len >= 4 && (len % 2) == 0 && p[1] == 0 && p[len - 1] == 0))) {
            return Utf16BytesToWide(p, len, false);
        }
        size_t n = 0;
        while (n < len && p[n] != 0) ++n;
        return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n)));
    };

    while (pos < end) {
        uint8_t ev = raw[pos++];
        ++eventCount;
        if (ev < 64) {
            if (pos + 1 > end) break;
            pos += 1;
        } else if (ev < 128) {
            if (pos + 2 > end) break;
            uint16_t w = DetailSys100::RdU16LE(raw.data() + pos);
            pos += 2;
            if (ev == 66 && tempoBpm == 0.0) tempoBpm = static_cast<double>(w);
        } else if (ev < 192) {
            if (pos + 4 > end) break;
            uint32_t dw = DetailSys100::RdU32LE(raw.data() + pos);
            pos += 4;
            if (ev == 156) tempoBpm = static_cast<double>(dw) / 1000.0;
        } else {
            uint64_t vLen = 0;
            int shift = 0;
            while (pos < end && shift < 35) {
                uint8_t b = raw[pos++];
                vLen |= static_cast<uint64_t>(b & 0x7Fu) << shift;
                shift += 7;
                if ((b & 0x80u) == 0) break;
            }
            if (vLen > end - pos) break;
            const uint8_t* p = raw.data() + pos;
            size_t len = static_cast<size_t>(vLen);
            pos += len;

            if (ev == 199) {
                flVersion = decodeFlpText(p, len, true);
                int maj = std::wcstol(flVersion.c_str(), nullptr, 10);
                if (maj >= 12 || flVersion.find(L"11.5") == 0) isUnicode = true;
            } else if (ev == 194) {
                projTitle = decodeFlpText(p, len, false);
            } else if (ev == 195) {
                projComment = decodeFlpText(p, len, false);
            } else if (ev == 206) {
                projGenre = decodeFlpText(p, len, false);
            } else if (ev == 207) {
                projAuthor = decodeFlpText(p, len, false);
            } else if (ev == 192 || ev == 203) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Channel", nm });
            } else if (ev == 193) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Pattern", nm });
            } else if (ev == 196) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Sample File", nm });
            } else if (ev == 201) {
                std::wstring nm = decodeFlpText(p, len, false);
                if (!nm.empty() && items.size() < 512) items.push_back({ L"Plugin", nm });
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"FL Studio Project (.flp)";

    SpreadsheetSheet hdr;
    hdr.name = L"FLP_Project";
    hdr.rows.push_back({ L"Property", L"Value" });
    if (!flVersion.empty()) hdr.rows.push_back({ L"FL Studio Version", flVersion });
    if (!projTitle.empty()) hdr.rows.push_back({ L"Title", projTitle });
    if (!projAuthor.empty()) hdr.rows.push_back({ L"Author", projAuthor });
    if (!projGenre.empty()) hdr.rows.push_back({ L"Genre", projGenre });
    if (!projComment.empty()) hdr.rows.push_back({ L"Comment", projComment });
    hdr.rows.push_back({ L"Format / Channels / PPQ",
        std::to_wstring(flpFormat) + L" / " + std::to_wstring(numChannels) + L" / " + std::to_wstring(ppq) });
    if (tempoBpm > 0.0) {
        wchar_t tbuf[64] = {};
        swprintf_s(tbuf, L"%.3f BPM", tempoBpm);
        hdr.rows.push_back({ L"Tempo", tbuf });
    }
    hdr.rows.push_back({ L"Decoded TLV Events", std::to_wstring(eventCount) });
    FinalizeSheetCols(hdr);
    out.sheets.push_back(std::move(hdr));

    if (!items.empty()) {
        SpreadsheetSheet chSh;
        chSh.name = L"Channels_&_Plugins";
        chSh.rows.push_back({ L"Type", L"Name / Path" });
        for (const auto& it : items) chSh.rows.push_back({ it.first, it.second });
        FinalizeSheetCols(chSh);
        out.sheets.push_back(std::move(chSh));
    }

    out.valid = true;
    return true;
}

inline bool ParseGuitarPro(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // 1. Guitar Pro 7/8 (.gp ZIP with Content/score.gpif)
    if (raw.size() >= 4 && std::memcmp(raw.data(), "PK\x03\x04", 4) == 0) {
        std::vector<uint8_t> gpif;
        if (!ZipDeflate::ExtractZipEntryFromMemory(raw.data(), raw.size(), ".gpif", gpif) || gpif.empty()) return false;
        std::string_view xml(reinterpret_cast<const char*>(gpif.data()), gpif.size());
        if (xml.find("<GPIF") == std::string_view::npos) return false;

        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"Guitar Pro 7/8 Tablature (.gp)";

        SpreadsheetSheet info;
        info.name = L"Score_Info";
        info.rows.push_back({ L"Field", L"Value" });
        auto addTag = [&](const char* tag, const wchar_t* label) {
            std::wstring v = ExtractFirstXmlTagText(xml, tag);
            if (!v.empty()) {
                // Strip <![CDATA[ ... ]]>
                if (v.size() >= 12 && v.substr(0, 9) == L"<![CDATA[" && v.substr(v.size() - 3) == L"]]>") {
                    v = v.substr(9, v.size() - 12);
                }
                if (!v.empty()) info.rows.push_back({ label, v });
            }
        };
        addTag("GPVersion", L"GP Version");
        addTag("Title", L"Title");
        addTag("SubTitle", L"Subtitle");
        addTag("Artist", L"Artist");
        addTag("Album", L"Album");
        addTag("Words", L"Lyrics / Words");
        addTag("Music", L"Composer");
        addTag("Tabber", L"Tabber");

        size_t mbCount = 0;
        size_t p = 0;
        while ((p = xml.find("<MasterBar>", p)) != std::string_view::npos) { ++mbCount; p += 11; }
        p = 0;
        while ((p = xml.find("<MasterBar ", p)) != std::string_view::npos) { ++mbCount; p += 11; }
        info.rows.push_back({ L"Master Bars", std::to_wstring(mbCount) });

        SpreadsheetSheet trks;
        trks.name = L"Tracks";
        trks.rows.push_back({ L"Track ID", L"Name", L"Tuning Pitches" });
        p = 0;
        while ((p = xml.find("<Track ", p)) != std::string_view::npos) {
            size_t gt = xml.find('>', p);
            size_t cl = (gt != std::string_view::npos) ? xml.find("</Track>", gt) : std::string_view::npos;
            if (gt == std::string_view::npos || cl == std::string_view::npos) break;
            std::string tid = ExtractXmlAttr(xml.substr(p, gt - p), "id");
            std::string_view blk = xml.substr(gt + 1, cl - gt - 1);
            std::wstring nm = ExtractFirstXmlTagText(blk, "Name");
            if (nm.size() >= 12 && nm.substr(0, 9) == L"<![CDATA[" && nm.substr(nm.size() - 3) == L"]]>") {
                nm = nm.substr(9, nm.size() - 12);
            }
            std::wstring pitches = ExtractFirstXmlTagText(blk, "Pitches");
            trks.rows.push_back({ PathUtils::Utf8ToWide(tid), nm.empty() ? L"(Track)" : nm, pitches });
            p = cl + 8;
        }
        FinalizeSheetCols(info);
        out.sheets.push_back(std::move(info));
        if (trks.rows.size() > 1) { FinalizeSheetCols(trks); out.sheets.push_back(std::move(trks)); }
        out.valid = true;
        return true;
    }

    // 2. Legacy Guitar Pro v3 / v4 / v5 Binary ("FICHIER GUITAR PRO vX.XX")
    if (raw.size() < 35 || raw[0] < 18 || raw[0] > 30) return false;
    std::string verBanner(reinterpret_cast<const char*>(raw.data() + 1), raw[0]);
    if (verBanner.find("FICHIER GUITAR PRO") == std::string::npos) return false;

    int gpMaj = 5;
    int gpMin = 0;
    size_t vPos = verBanner.find('v');
    if (vPos != std::string::npos && vPos + 1 < verBanner.size()) {
        gpMaj = verBanner[vPos + 1] - '0';
        if (vPos + 3 < verBanner.size() && verBanner[vPos + 2] == '.') {
            gpMin = std::atoi(verBanner.c_str() + vPos + 3);
        }
    }

    size_t pos = 31;
    auto readIntByteStr = [&](std::wstring& outStr) -> bool {
        if (pos + 4 > raw.size()) return false;
        uint32_t blkLen = DetailSys100::RdU32LE(raw.data() + pos);
        pos += 4;
        if (blkLen == 0) {
            if (pos < raw.size() && raw[pos] == 0) ++pos;
            outStr.clear();
            return true;
        }
        if (blkLen > raw.size() - pos || blkLen > 65536) return false;
        uint8_t sLen = raw[pos];
        size_t take = std::min<size_t>(sLen, blkLen - 1);
        outStr = Latin1OrCp1252ToWide(raw.data() + pos + 1, take);
        pos += blkLen;
        return true;
    };

    std::wstring title, subtitle, artist, album, words, music, copyright, tabber, instructions;
    if (!readIntByteStr(title)) return false;
    readIntByteStr(subtitle);
    readIntByteStr(artist);
    readIntByteStr(album);
    readIntByteStr(words);
    if (gpMaj >= 5) readIntByteStr(music);
    readIntByteStr(copyright);
    readIntByteStr(tabber);
    readIntByteStr(instructions);

    std::vector<std::wstring> notices;
    if (pos + 4 <= raw.size()) {
        uint32_t nCount = DetailSys100::RdU32LE(raw.data() + pos);
        pos += 4;
        for (uint32_t i = 0; i < nCount && i < 64; ++i) {
            std::wstring nl;
            if (!readIntByteStr(nl)) break;
            if (!nl.empty()) notices.push_back(nl);
        }
    }

    if (gpMaj <= 4 && pos < raw.size()) {
        pos += 1; // tripletFeel
    }
    if (gpMaj >= 4 && pos + 4 <= raw.size()) {
        pos += 4; // lyricTrack
        for (int i = 0; i < 5 && pos + 8 <= raw.size(); ++i) {
            pos += 4; // startBar
            uint32_t lLen = DetailSys100::RdU32LE(raw.data() + pos);
            pos += 4;
            if (lLen > raw.size() - pos) { pos = raw.size(); break; }
            pos += lLen;
        }
    }
    if (gpMaj >= 5) {
        if (gpMin >= 10 && pos + 19 <= raw.size()) pos += 19; // RSE master effect
        if (pos + 30 <= raw.size()) {
            pos += 30; // PageSetup dimensions & flags
            for (int i = 0; i < 10; ++i) {
                std::wstring dummy;
                if (!readIntByteStr(dummy)) break;
            }
        }
        std::wstring tempoName;
        readIntByteStr(tempoName);
    }

    uint32_t tempo = 0;
    uint32_t measureCount = 0;
    uint32_t trackCount = 0;
    if (pos + 4 <= raw.size()) {
        tempo = DetailSys100::RdU32LE(raw.data() + pos);
        pos += 4;
        if (gpMaj >= 5 && gpMin >= 10 && pos < raw.size()) pos += 1; // hideTempo
        if (gpMaj >= 4) pos += 5; // key(1) + octave(4)
        else pos += 4;            // key(4)
        if (pos + 768 <= raw.size()) {
            pos += 768; // 64 MIDI channel ports (64 * 12B)
            if (gpMaj >= 5 && pos + 46 <= raw.size()) pos += 46; // musical directions(42) + master reverb(4)
            if (pos + 8 <= raw.size()) {
                measureCount = DetailSys100::RdU32LE(raw.data() + pos);
                trackCount   = DetailSys100::RdU32LE(raw.data() + pos + 4);
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Guitar Pro Tablature (" + extLower + L")";

    SpreadsheetSheet info;
    info.name = L"GP_Header";
    info.rows.push_back({ L"Property", L"Value" });
    info.rows.push_back({ L"Version", PathUtils::Utf8ToWide(verBanner) });
    if (!title.empty())        info.rows.push_back({ L"Title", title });
    if (!subtitle.empty())     info.rows.push_back({ L"Subtitle", subtitle });
    if (!artist.empty())       info.rows.push_back({ L"Artist", artist });
    if (!album.empty())        info.rows.push_back({ L"Album", album });
    if (!words.empty())        info.rows.push_back({ L"Words", words });
    if (!music.empty())        info.rows.push_back({ L"Music", music });
    if (!copyright.empty())    info.rows.push_back({ L"Copyright", copyright });
    if (!tabber.empty())       info.rows.push_back({ L"Tabber", tabber });
    if (!instructions.empty()) info.rows.push_back({ L"Instructions", instructions });
    if (tempo > 0 && tempo < 1000) info.rows.push_back({ L"Tempo", std::to_wstring(tempo) + L" BPM" });
    if (measureCount > 0 && measureCount < 100000) info.rows.push_back({ L"Measures", std::to_wstring(measureCount) });
    if (trackCount > 0 && trackCount < 256) info.rows.push_back({ L"Tracks", std::to_wstring(trackCount) });
    for (size_t i = 0; i < notices.size(); ++i) {
        info.rows.push_back({ L"Notice " + std::to_wstring(i + 1), notices[i] });
    }
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));
    out.valid = true;
    return true;
}

inline bool ParseReaperRpp(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEF && static_cast<uint8_t>(sv[1]) == 0xBB && static_cast<uint8_t>(sv[2]) == 0xBF) {
        sv.remove_prefix(3);
    }
    while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= 32) sv.remove_prefix(1);
    if (sv.size() < 15 || sv.substr(0, 15) != "<REAPER_PROJECT") return false;

    auto tokenizeLine = [](std::string_view ln) -> std::vector<std::string> {
        std::vector<std::string> toks;
        size_t i = 0;
        while (i < ln.size()) {
            while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
            if (i >= ln.size()) break;
            if (ln[i] == '"' || ln[i] == '\'' || ln[i] == '`') {
                char q = ln[i++];
                size_t s = i;
                while (i < ln.size() && ln[i] != q) ++i;
                toks.push_back( std::string(ln.substr(s, i - s)) );
                if (i < ln.size()) ++i;
            } else {
                size_t s = i;
                while (i < ln.size() && ln[i] != ' ' && ln[i] != '\t') ++i;
                toks.push_back( std::string(ln.substr(s, i - s)) );
            }
        }
        return toks;
    };

    std::wstring rppVer, tempoStr, srStr;
    struct RppTrack {
        std::wstring name;
        std::wstring volPan;
        size_t itemCount = 0;
        std::vector<std::wstring> fx;
        std::vector<std::wstring> sources;
    };
    std::vector<RppTrack> tracks;
    std::vector<std::string> blockStack;
    int trackDepth = -1;

    size_t ls = 0;
    while (ls < sv.size()) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();

        auto toks = tokenizeLine(ln);
        if (toks.empty()) continue;

        const std::string& head = toks[0];
        if (head == ">") {
            if (!blockStack.empty()) {
                if (static_cast<int>(blockStack.size()) - 1 == trackDepth) trackDepth = -1;
                blockStack.pop_back();
            }
            continue;
        }
        if (!head.empty() && head[0] == '<') {
            std::string bName = head.substr(1);
            if (bName == "REAPER_PROJECT" && toks.size() >= 3) {
                rppVer = PathUtils::Utf8ToWide(toks[2]);
            } else if (bName == "TRACK") {
                trackDepth = static_cast<int>(blockStack.size());
                tracks.push_back({});
            } else if (bName == "ITEM" && trackDepth >= 0 && !tracks.empty()) {
                tracks.back().itemCount++;
            } else if ((bName == "VST" || bName == "AU" || bName == "JS" || bName == "CLAP") && trackDepth >= 0 && !tracks.empty() && toks.size() >= 2) {
                tracks.back().fx.push_back(PathUtils::Utf8ToWide(toks[1]));
            }
            blockStack.push_back(std::move(bName));
            continue;
        }

        const std::string curBlk = blockStack.empty() ? "" : blockStack.back();
        if (curBlk == "REAPER_PROJECT") {
            if (head == "TEMPO" && toks.size() >= 4) {
                tempoStr = PathUtils::Utf8ToWide(toks[1] + " BPM (" + toks[2] + "/" + toks[3] + ")");
            } else if (head == "SAMPLERATE" && toks.size() >= 2) {
                srStr = PathUtils::Utf8ToWide(toks[1]) + L" Hz";
            }
        } else if (curBlk == "TRACK" && !tracks.empty()) {
            if (head == "NAME" && toks.size() >= 2) tracks.back().name = PathUtils::Utf8ToWide(toks[1]);
            else if (head == "VOLPAN" && toks.size() >= 3) tracks.back().volPan = L"vol=" + PathUtils::Utf8ToWide(toks[1]) + L", pan=" + PathUtils::Utf8ToWide(toks[2]);
        } else if (curBlk.rfind("SOURCE", 0) == 0 && head == "FILE" && toks.size() >= 2 && trackDepth >= 0 && !tracks.empty()) {
            tracks.back().sources.push_back(PathUtils::Utf8ToWide(toks[1]));
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Cockos REAPER Project (.rpp)";

    SpreadsheetSheet info;
    info.name = L"REAPER_Project";
    info.rows.push_back({ L"Property", L"Value" });
    if (!rppVer.empty()) info.rows.push_back({ L"REAPER Version", rppVer });
    if (!tempoStr.empty()) info.rows.push_back({ L"Tempo", tempoStr });
    if (!srStr.empty()) info.rows.push_back({ L"Sample Rate", srStr });
    info.rows.push_back({ L"Tracks", std::to_wstring(tracks.size()) });
    FinalizeSheetCols(info);
    out.sheets.push_back(std::move(info));

    if (!tracks.empty()) {
        SpreadsheetSheet tsh;
        tsh.name = L"Tracks";
        tsh.rows.push_back({ L"#", L"Track Name", L"Vol/Pan", L"Items", L"FX Plugins", L"Media Files" });
        for (size_t i = 0; i < tracks.size(); ++i) {
            auto joinVec = [](const std::vector<std::wstring>& v) {
                std::wstring s;
                for (size_t k = 0; k < v.size() && k < 6; ++k) {
                    if (k > 0) s += L"; ";
                    s += v[k];
                }
                return s;
            };
            tsh.rows.push_back({
                std::to_wstring(i + 1),
                tracks[i].name.empty() ? L"(Track)" : tracks[i].name,
                tracks[i].volPan,
                std::to_wstring(tracks[i].itemCount),
                joinVec(tracks[i].fx),
                joinVec(tracks[i].sources)
            });
        }
        FinalizeSheetCols(tsh);
        out.sheets.push_back(std::move(tsh));
    }
    out.valid = true;
    return true;
}

// ============================================================================
// 7. NLE Video Timelines (.edl, .fcpxml, .otio)
// ============================================================================
inline bool ParseCmxEdl(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::wstring edlTitle, fcmMode;

    struct EdlEvent {
        std::wstring num, reel, chan, trans, srcIn, srcOut, recIn, recOut, clipName;
    };
    std::vector<EdlEvent> events;

    auto isTimecode = [](std::string_view t) {
        return t.size() == 11 &&
               (t[2] == ':' || t[2] == ';') && (t[5] == ':' || t[5] == ';') && (t[8] == ':' || t[8] == ';');
    };

    size_t ls = 0;
    while (ls < sv.size()) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty()) continue;

        if (ln.size() >= 6 && PathUtils::ToLowerAscii(ln.substr(0, 6)) == "title:") {
            edlTitle = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(6)));
            continue;
        }
        if (ln.size() >= 4 && PathUtils::ToLowerAscii(ln.substr(0, 4)) == "fcm:") {
            fcmMode = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(4)));
            continue;
        }
        if (ln[0] == '*' && !events.empty()) {
            std::string low = PathUtils::ToLowerAscii(ln);
            size_t cn = low.find("clip name:");
            if (cn != std::string::npos) {
                events.back().clipName = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(cn + 10)));
            }
            continue;
        }

        // Tokenize potential CMX 3600 event row
        std::vector<std::string_view> toks;
        size_t i = 0;
        while (i < ln.size()) {
            while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
            size_t s = i;
            while (i < ln.size() && ln[i] != ' ' && ln[i] != '\t') ++i;
            if (i > s) toks.push_back(ln.substr(s, i - s));
        }
        if (toks.size() >= 8 && std::isdigit(static_cast<unsigned char>(toks[0][0]))) {
            size_t tcIdx = toks.size() - 4;
            if (isTimecode(toks[tcIdx]) && isTimecode(toks[tcIdx + 1]) &&
                isTimecode(toks[tcIdx + 2]) && isTimecode(toks[tcIdx + 3]))
            {
                EdlEvent ev;
                ev.num    = PathUtils::Utf8ToWide(toks[0]);
                ev.reel   = PathUtils::Utf8ToWide(toks[1]);
                ev.chan   = PathUtils::Utf8ToWide(toks[2]);
                ev.trans  = PathUtils::Utf8ToWide(toks[3]);
                ev.srcIn  = PathUtils::Utf8ToWide(toks[tcIdx]);
                ev.srcOut = PathUtils::Utf8ToWide(toks[tcIdx + 1]);
                ev.recIn  = PathUtils::Utf8ToWide(toks[tcIdx + 2]);
                ev.recOut = PathUtils::Utf8ToWide(toks[tcIdx + 3]);
                events.push_back(std::move(ev));
            }
        }
    }

    if (events.empty() && edlTitle.empty()) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"CMX 3600 Edit Decision List (.edl)";

    SpreadsheetSheet sh;
    sh.name = L"EDL_Events";
    sh.rows.push_back({ L"Event #", L"Reel", L"Channel", L"Trans", L"Src In", L"Src Out", L"Rec In", L"Rec Out", L"Clip Name" });
    if (!edlTitle.empty() || !fcmMode.empty()) {
        sh.rows.push_back({ L"TITLE", edlTitle, fcmMode, L"", L"", L"", L"", L"", L"" });
    }
    for (const auto& ev : events) {
        sh.rows.push_back({ ev.num, ev.reel, ev.chan, ev.trans, ev.srcIn, ev.srcOut, ev.recIn, ev.recOut, ev.clipName });
    }
    FinalizeSheetCols(sh);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

inline bool ParseFcpXml(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view xml(reinterpret_cast<const char*>(raw.data()), raw.size());
    size_t root = xml.find("<fcpxml");
    if (root == std::string_view::npos) return false;
    size_t rGt = xml.find('>', root);
    if (rGt == std::string_view::npos) return false;
    std::string ver = ExtractXmlAttr(xml.substr(root, rGt - root), "version");

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Final Cut Pro XML (.fcpxml v" + PathUtils::Utf8ToWide(ver) + L")";

    SpreadsheetSheet assets;
    assets.name = L"Assets_&_Formats";
    assets.rows.push_back({ L"Kind", L"ID", L"Name", L"Source / Resolution", L"Duration" });

    size_t p = 0;
    while ((p = xml.find("<format ", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xml.substr(p, gt - p);
        std::string w = ExtractXmlAttr(tag, "width");
        std::string h = ExtractXmlAttr(tag, "height");
        assets.rows.push_back({
            L"Format",
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "id")),
            DecodeHtmlEntitiesMedia(PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "name"))),
            PathUtils::Utf8ToWide(w + "x" + h),
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "frameDuration"))
        });
        p = gt + 1;
    }

    p = 0;
    while ((p = xml.find("<asset ", p)) != std::string_view::npos) {
        size_t gt = xml.find('>', p);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xml.substr(p, gt - p);
        std::string src = ExtractXmlAttr(tag, "src");
        if (src.empty()) {
            size_t cl = xml.find("</asset>", gt);
            if (cl != std::string_view::npos) {
                size_t mr = xml.substr(gt, cl - gt).find("<media-rep");
                if (mr != std::string_view::npos) {
                    size_t mrGt = xml.find('>', gt + mr);
                    if (mrGt != std::string_view::npos) src = ExtractXmlAttr(xml.substr(gt + mr, mrGt - (gt + mr)), "src");
                }
            }
        }
        assets.rows.push_back({
            L"Asset",
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "id")),
            DecodeHtmlEntitiesMedia(PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "name"))),
            DecodeHtmlEntitiesMedia(PathUtils::Utf8ToWide(src)),
            PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "duration"))
        });
        p = gt + 1;
    }

    SpreadsheetSheet clips;
    clips.name = L"Timeline_Clips";
    clips.rows.push_back({ L"Element", L"Name", L"Ref", L"Offset", L"Start", L"Duration" });
    static const char* kClipTags[] = { "asset-clip", "clip", "mc-clip", "video", "audio", "title" };
    for (const char* ct : kClipTags) {
        std::string openT = std::string("<") + ct + " ";
        size_t cp = 0;
        while ((cp = xml.find(openT, cp)) != std::string_view::npos && clips.rows.size() < 2048) {
            size_t gt = xml.find('>', cp);
            if (gt == std::string_view::npos) break;
            std::string_view tag = xml.substr(cp, gt - cp);
            clips.rows.push_back({
                PathUtils::Utf8ToWide(ct),
                DecodeHtmlEntitiesMedia(PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "name"))),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "ref")),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "offset")),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "start")),
                PathUtils::Utf8ToWide(ExtractXmlAttr(tag, "duration"))
            });
            cp = gt + 1;
        }
    }

    FinalizeSheetCols(assets);
    out.sheets.push_back(std::move(assets));
    if (clips.rows.size() > 1) { FinalizeSheetCols(clips); out.sheets.push_back(std::move(clips)); }
    out.valid = true;
    return true;
}

inline bool ParseOpenTimelineOtio(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view js(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (js.find("\"OTIO_SCHEMA\"") == std::string_view::npos) return false;

    // Minimal recursive JSON object walker tailored for OTIO schemas
    auto findJsonStrField = [](std::string_view obj, std::string_view key) -> std::string {
        std::string needle = "\"" + std::string(key) + "\"";
        size_t p = obj.find(needle);
        if (p == std::string_view::npos) return {};
        size_t col = obj.find(':', p + needle.size());
        if (col == std::string_view::npos) return {};
        size_t q1 = obj.find('"', col + 1);
        if (q1 == std::string_view::npos) return {};
        std::string res;
        for (size_t i = q1 + 1; i < obj.size(); ++i) {
            if (obj[i] == '\\' && i + 1 < obj.size()) { res.push_back(obj[++i]); continue; }
            if (obj[i] == '"') break;
            res.push_back(obj[i]);
        }
        return res;
    };
    auto findJsonNumField = [](std::string_view obj, std::string_view key) -> std::string {
        std::string needle = "\"" + std::string(key) + "\"";
        size_t p = obj.find(needle);
        if (p == std::string_view::npos) return {};
        size_t col = obj.find(':', p + needle.size());
        if (col == std::string_view::npos) return {};
        size_t s = col + 1;
        while (s < obj.size() && (obj[s] == ' ' || obj[s] == '\t' || obj[s] == '\r' || obj[s] == '\n')) ++s;
        size_t e = s;
        while (e < obj.size() && ((obj[e] >= '0' && obj[e] <= '9') || obj[e] == '.' || obj[e] == '-')) ++e;
        return std::string(obj.substr(s, e - s));
    };
    auto extractBalancedObj = [](std::string_view s, size_t openBrace) -> std::string_view {
        int depth = 0;
        bool inStr = false;
        for (size_t i = openBrace; i < s.size(); ++i) {
            char c = s[i];
            if (inStr) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0) return s.substr(openBrace, i - openBrace + 1);
            }
        }
        return s.substr(openBrace);
    };

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"OpenTimelineIO JSON (.otio)";

    SpreadsheetSheet tlSh;
    tlSh.name = L"OTIO_Timeline";
    tlSh.rows.push_back({ L"Schema", L"Name", L"Kind / URL", L"Start", L"Duration" });

    size_t p = 0;
    while ((p = js.find("\"OTIO_SCHEMA\"", p)) != std::string_view::npos && tlSh.rows.size() < 2048) {
        // Walk back to enclosing '{' at depth 0
        size_t brace = p;
        int d = 0;
        while (brace > 0) {
            --brace;
            if (js[brace] == '}') ++d;
            else if (js[brace] == '{') {
                if (d == 0) break;
                --d;
            }
        }
        std::string_view obj = extractBalancedObj(js, brace);
        // Header portion before nested "children" array so we read this object's own "name"
        size_t chIdx = obj.find("\"children\"");
        std::string_view headObj = (chIdx != std::string_view::npos) ? obj.substr(0, chIdx) : obj;

        std::string schema = findJsonStrField(headObj, "OTIO_SCHEMA");
        if (schema.empty()) schema = findJsonStrField(obj, "OTIO_SCHEMA");

        if (schema.rfind("Timeline.", 0) == 0 || schema.rfind("Track.", 0) == 0 ||
            schema.rfind("Clip.", 0) == 0 || schema.rfind("Gap.", 0) == 0 || schema.rfind("Transition.", 0) == 0)
        {
            std::string name = findJsonStrField(headObj, "name");
            if (name.empty()) name = findJsonStrField(obj, "name");
            std::string kind = findJsonStrField(headObj, "kind");
            std::string url  = findJsonStrField(obj, "target_url");
            std::string startStr, durStr;
            size_t srPos = obj.find("\"source_range\"");
            if (srPos != std::string_view::npos) {
                size_t b2 = obj.find('{', srPos);
                if (b2 != std::string_view::npos) {
                    std::string_view srObj = extractBalancedObj(obj, b2);
                    size_t stPos = srObj.find("\"start_time\"");
                    if (stPos != std::string_view::npos) {
                        size_t sb = srObj.find('{', stPos);
                        if (sb != std::string_view::npos) {
                            std::string_view stObj = extractBalancedObj(srObj, sb);
                            startStr = findJsonNumField(stObj, "value") + "@" + findJsonNumField(stObj, "rate");
                        }
                    }
                    size_t duPos = srObj.find("\"duration\"");
                    if (duPos != std::string_view::npos) {
                        size_t db = srObj.find('{', duPos);
                        if (db != std::string_view::npos) {
                            std::string_view duObj = extractBalancedObj(srObj, db);
                            durStr = findJsonNumField(duObj, "value") + "@" + findJsonNumField(duObj, "rate");
                        }
                    }
                }
            }
            tlSh.rows.push_back({
                PathUtils::Utf8ToWide(schema),
                PathUtils::Utf8ToWide(name),
                PathUtils::Utf8ToWide(!url.empty() ? url : kind),
                PathUtils::Utf8ToWide(startStr),
                PathUtils::Utf8ToWide(durStr)
            });
        }
        p += 13;
    }

    if (tlSh.rows.size() <= 1) return false;
    FinalizeSheetCols(tlSh);
    out.sheets.push_back(std::move(tlSh));
    out.valid = true;
    return true;
}

// ============================================================================
// 8. Subtitles: VobSub (.idx / .sub), MicroDVD / SubViewer (.sub), Blu-ray PGS (.sup)
// ============================================================================
inline bool ParseVobSubIdx(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (sv.find("VobSub index file") == std::string_view::npos &&
        (sv.find("timestamp:") == std::string_view::npos || sv.find("id:") == std::string_view::npos))
    {
        return false;
    }

    std::wstring resSize, langIdx, palette;
    std::wstring curLang = L"und";
    struct IdxCue { std::wstring lang, ts, filepos; };
    std::vector<IdxCue> cues;

    size_t ls = 0;
    while (ls < sv.size()) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty() || ln[0] == '#') continue;

        std::string low = PathUtils::ToLowerAscii(ln);
        if (low.rfind("size:", 0) == 0) resSize = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(5)));
        else if (low.rfind("langidx:", 0) == 0) langIdx = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(8)));
        else if (low.rfind("palette:", 0) == 0) palette = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(8)));
        else if (low.rfind("id:", 0) == 0) curLang = PathUtils::Trim(PathUtils::Utf8ToWide(ln.substr(3)));
        else if (low.rfind("timestamp:", 0) == 0 && cues.size() < 4096) {
            size_t fp = low.find("filepos:");
            std::string_view tsPart = (fp != std::string::npos) ? ln.substr(10, fp - 10) : ln.substr(10);
            while (!tsPart.empty() && (tsPart.back() == ',' || tsPart.back() == ' ')) tsPart.remove_suffix(1);
            std::string_view fpPart = (fp != std::string::npos) ? ln.substr(fp + 8) : std::string_view{};
            cues.push_back({ curLang, PathUtils::Trim(PathUtils::Utf8ToWide(tsPart)), PathUtils::Trim(PathUtils::Utf8ToWide(fpPart)) });
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"DVD VobSub Subtitle Index (.idx)";

    SpreadsheetSheet hdr;
    hdr.name = L"VobSub_Info";
    hdr.rows.push_back({ L"Directive", L"Value" });
    if (!resSize.empty()) hdr.rows.push_back({ L"Video Size", resSize });
    if (!langIdx.empty()) hdr.rows.push_back({ L"Default Language Index", langIdx });
    if (!palette.empty()) hdr.rows.push_back({ L"Palette", palette });
    hdr.rows.push_back({ L"Subtitle Cues", std::to_wstring(cues.size()) });
    FinalizeSheetCols(hdr);
    out.sheets.push_back(std::move(hdr));

    if (!cues.empty()) {
        SpreadsheetSheet csh;
        csh.name = L"Timestamps";
        csh.rows.push_back({ L"#", L"Stream ID", L"Timestamp", L"File Offset (Hex)" });
        for (size_t i = 0; i < cues.size(); ++i) {
            csh.rows.push_back({ std::to_wstring(i + 1), cues[i].lang, cues[i].ts, L"0x" + cues[i].filepos });
        }
        FinalizeSheetCols(csh);
        out.sheets.push_back(std::move(csh));
    }
    out.valid = true;
    return true;
}

inline bool ParseBluraySup(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 13 || raw[0] != 'P' || raw[1] != 'G') return false;

    uint16_t videoW = 0, videoH = 0;
    size_t pcsCount = 0, odsCount = 0, pdsCount = 0, endCount = 0;

    SpreadsheetSheet segs;
    segs.name = L"PGS_Segments";
    segs.rows.push_back({ L"#", L"PTS", L"Segment Type", L"Size (B)", L"Details" });

    size_t pos = 0;
    size_t segIdx = 0;
    while (pos + 13 <= raw.size()) {
        if (raw[pos] != 'P' || raw[pos + 1] != 'G') break;
        uint32_t pts = DetailSys100::RdU32BE(raw.data() + pos + 2);
        uint8_t sType = raw[pos + 10];
        uint16_t sSize = DetailSys100::RdU16BE(raw.data() + pos + 11);
        if (pos + 13 + sSize > raw.size()) break;
        const uint8_t* p = raw.data() + pos + 13;

        std::wstring typeName = DetailSys100::Hex32(sType);
        std::wstring detail;
        if (sType == 0x16) {
            typeName = L"PCS (Presentation)";
            ++pcsCount;
            if (sSize >= 11) {
                videoW = DetailSys100::RdU16BE(p);
                videoH = DetailSys100::RdU16BE(p + 2);
                uint16_t compNum = DetailSys100::RdU16BE(p + 5);
                uint8_t nObjs = p[10];
                detail = std::to_wstring(videoW) + L"x" + std::to_wstring(videoH) +
                         L", comp#" + std::to_wstring(compNum) + L", objs=" + std::to_wstring(nObjs);
            }
        } else if (sType == 0x17) {
            typeName = L"WDS (Window)";
        } else if (sType == 0x14) {
            typeName = L"PDS (Palette)";
            ++pdsCount;
            if (sSize >= 2) detail = std::to_wstring((sSize - 2) / 5) + L" entries";
        } else if (sType == 0x15) {
            typeName = L"ODS (Bitmap Object)";
            ++odsCount;
            if (sSize >= 11 && (p[3] & 0x80u) != 0) {
                uint16_t bw = DetailSys100::RdU16BE(p + 7);
                uint16_t bh = DetailSys100::RdU16BE(p + 9);
                detail = L"Bitmap " + std::to_wstring(bw) + L"x" + std::to_wstring(bh);
            }
        } else if (sType == 0x80) {
            typeName = L"END (Display Set)";
            ++endCount;
        }

        if (segs.rows.size() < 2048) {
            segs.rows.push_back({
                std::to_wstring(++segIdx), FormatPts90kHz(pts), typeName, std::to_wstring(sSize), detail
            });
        } else {
            ++segIdx;
        }
        pos += 13ULL + sSize;
    }

    if (segIdx == 0) return false;

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Blu-ray PGS Subtitle Stream (.sup)";

    SpreadsheetSheet sum;
    sum.name = L"PGS_Summary";
    sum.rows.push_back({ L"Property", L"Value" });
    if (videoW > 0) sum.rows.push_back({ L"Video Resolution", std::to_wstring(videoW) + L"x" + std::to_wstring(videoH) });
    sum.rows.push_back({ L"Total Segments", std::to_wstring(segIdx) });
    sum.rows.push_back({ L"PCS / ODS / PDS / END",
        std::to_wstring(pcsCount) + L" / " + std::to_wstring(odsCount) + L" / " + std::to_wstring(pdsCount) + L" / " + std::to_wstring(endCount) });
    FinalizeSheetCols(sum);
    FinalizeSheetCols(segs);
    out.sheets.push_back(std::move(sum));
    out.sheets.push_back(std::move(segs));
    out.valid = true;
    return true;
}

inline bool ParseSubSubtitle(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    // 1. VobSub Binary MPEG-2 PS (.sub starting with 0x000001BA)
    if (raw.size() >= 14 && DetailSys100::RdU32BE(raw.data()) == 0x000001BAu) {
        SpreadsheetSheet spu;
        spu.name = L"VobSub_Packets";
        spu.rows.push_back({ L"#", L"Substream ID", L"PTS", L"PES Length" });
        size_t pos = 0;
        size_t pktIdx = 0;
        while (pos + 6 <= raw.size() && spu.rows.size() < 2048) {
            if (raw[pos] == 0 && raw[pos + 1] == 0 && raw[pos + 2] == 1) {
                uint8_t code = raw[pos + 3];
                if (code == 0xBA) {
                    if (pos + 14 > raw.size()) break;
                    size_t stuff = raw[pos + 13] & 7u;
                    pos += 14 + stuff;
                    continue;
                }
                uint16_t pesLen = DetailSys100::RdU16BE(raw.data() + pos + 4);
                if (code == 0xBD && pos + 9 <= raw.size() && pesLen >= 4) {
                    uint8_t flags2 = raw[pos + 7];
                    uint8_t hdrDataLen = raw[pos + 8];
                    uint64_t pts = 0;
                    if ((flags2 & 0x80u) != 0 && hdrDataLen >= 5 && pos + 14 <= raw.size()) {
                        const uint8_t* pt = raw.data() + pos + 9;
                        pts = (static_cast<uint64_t>((pt[0] >> 1) & 7u) << 30) |
                              (static_cast<uint64_t>(pt[1]) << 22) |
                              (static_cast<uint64_t>((pt[2] >> 1) & 0x7Fu) << 15) |
                              (static_cast<uint64_t>(pt[3]) << 7) |
                              static_cast<uint64_t>((pt[4] >> 1) & 0x7Fu);
                    }
                    size_t subIdOff = pos + 9 + hdrDataLen;
                    uint8_t subId = (subIdOff < raw.size()) ? raw[subIdOff] : 0;
                    spu.rows.push_back({
                        std::to_wstring(++pktIdx),
                        DetailSys100::Hex32(subId),
                        FormatPts90kHz(pts),
                        std::to_wstring(pesLen)
                    });
                }
                pos += 6ULL + pesLen;
                continue;
            }
            ++pos;
        }
        if (pktIdx == 0) return false;
        out.kind = OfficeFileKind::SqliteDb;
        out.isSpreadsheet = true;
        out.formatTitle = L"DVD VobSub SPU Stream (.sub)";
        FinalizeSheetCols(spu);
        out.sheets.push_back(std::move(spu));
        out.valid = true;
        return true;
    }

    // 2. MicroDVD {startFrame}{endFrame}Text or SubViewer HH:MM:SS.cc,HH:MM:SS.cc
    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    SpreadsheetSheet sh;
    sh.name = L"Subtitles";
    sh.rows.push_back({ L"#", L"Start", L"End", L"Dialogue Text" });

    size_t ls = 0;
    size_t cueIdx = 0;
    std::wstring pendingStart, pendingEnd;
    while (ls < sv.size() && sh.rows.size() < 4096) {
        size_t le = sv.find('\n', ls);
        if (le == std::string_view::npos) le = sv.size();
        std::string_view ln = sv.substr(ls, le - ls);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        ls = (le < sv.size()) ? (le + 1) : sv.size();
        if (ln.empty()) continue;

        if (ln[0] == '{') {
            size_t b1 = ln.find('}', 1);
            if (b1 != std::string_view::npos && b1 + 1 < ln.size() && ln[b1 + 1] == '{') {
                size_t b2 = ln.find('}', b1 + 2);
                if (b2 != std::string_view::npos) {
                    bool okDigits = (b1 > 1 && b2 > b1 + 2);
                    for (size_t k = 1; k < b1 && okDigits; ++k) if (!std::isdigit(static_cast<unsigned char>(ln[k]))) okDigits = false;
                    for (size_t k = b1 + 2; k < b2 && okDigits; ++k) if (!std::isdigit(static_cast<unsigned char>(ln[k]))) okDigits = false;
                    if (okDigits) {
                        std::string txt(ln.substr(b2 + 1));
                        for (size_t k = 0; k < txt.size(); ++k) if (txt[k] == '|') txt[k] = ' ';
                        sh.rows.push_back({
                            std::to_wstring(++cueIdx),
                            PathUtils::Utf8ToWide(ln.substr(1, b1 - 1)),
                            PathUtils::Utf8ToWide(ln.substr(b1 + 2, b2 - b1 - 2)),
                            PathUtils::Utf8ToWide(txt)
                        });
                        continue;
                    }
                }
            }
        }
        // SubViewer timestamp line: 00:01:23.45,00:01:25.67
        if (ln.size() >= 23 && ln[2] == ':' && ln[5] == ':' && ln[11] == ',' && ln[14] == ':' && ln[17] == ':') {
            pendingStart = PathUtils::Utf8ToWide(ln.substr(0, 11));
            pendingEnd   = PathUtils::Utf8ToWide(ln.substr(12, 11));
            continue;
        }
        if (!pendingStart.empty()) {
            std::string txt(ln);
            size_t br = 0;
            while ((br = txt.find("[br]", br)) != std::string::npos) { txt.replace(br, 4, " "); }
            sh.rows.push_back({ std::to_wstring(++cueIdx), pendingStart, pendingEnd, PathUtils::Utf8ToWide(txt) });
            pendingStart.clear();
            pendingEnd.clear();
        }
    }

    if (cueIdx == 0) return false;
    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"MicroDVD / SubViewer Subtitle (.sub)";
    FinalizeSheetCols(sh);
    out.sheets.push_back(std::move(sh));
    out.valid = true;
    return true;
}

// ============================================================================
// 9. Microsoft Outlook MAPI Message (.msg — OLE2 CFB)
// ============================================================================
inline bool ParseOutlookMsg(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) return false;

    bool hasMapiMarker = false;
    for (const auto& e : cf.entries) {
        if (e.name.find(L"__substg1.0_") != std::wstring::npos ||
            e.name.find(L"__properties_version1.0") != std::wstring::npos ||
            e.name.find(L"__nameid_version1.0") != std::wstring::npos)
        {
            hasMapiMarker = true;
            break;
        }
    }
    if (!hasMapiMarker) return false;

    auto decodeSubstgStr = [&](const Ole2::DirEntry& e, const std::wstring& typeHex) -> std::wstring {
        std::vector<uint8_t> bytes;
        if (!Ole2::ExtractStream(cf, e, bytes, 4 * 1024 * 1024) || bytes.empty()) return {};
        if (typeHex == L"001f") {
            return Utf16BytesToWide(bytes.data(), bytes.size(), false);
        }
        if (typeHex == L"001e") {
            size_t n = bytes.size();
            while (n > 0 && bytes[n - 1] == 0) --n;
            return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(bytes.data()), n)));
        }
        return {};
    };

    std::map<std::wstring, std::wstring> rootProps;
    std::vector<uint8_t> htmlBodyBin;

    struct MsgRecip { std::wstring name, email; };
    struct MsgAttach { std::wstring filename, mime; uint64_t byteSize = 0; };
    std::map<std::wstring, MsgRecip> recips;
    std::map<std::wstring, MsgAttach> attachs;

    for (const auto& e : cf.entries) {
        if (e.objectType != 2 || e.fullPath.empty()) continue;
        std::wstring pathLow = PathUtils::ToLower(e.fullPath);
        size_t slash = pathLow.find(L'/');
        std::wstring parent = (slash != std::wstring::npos) ? pathLow.substr(0, slash) : L"";
        std::wstring leaf   = (slash != std::wstring::npos) ? pathLow.substr(slash + 1) : pathLow;

        if (leaf.size() == 20 && leaf.rfind(L"__substg1.0_", 0) == 0) {
            std::wstring propId   = leaf.substr(12, 4);
            std::wstring propType = leaf.substr(16, 4);

            if (parent.empty()) {
                if (propType == L"001f" || propType == L"001e") {
                    std::wstring val = decodeSubstgStr(e, propType);
                    if (!val.empty() && (propType == L"001f" || rootProps.find(propId) == rootProps.end())) {
                        rootProps[propId] = std::move(val);
                    }
                } else if (propId == L"1013" && propType == L"0102") {
                    Ole2::ExtractStream(cf, e, htmlBodyBin, 4 * 1024 * 1024);
                }
            } else if (parent.rfind(L"__recip_version1.0_", 0) == 0) {
                auto& r = recips[parent];
                if (propType == L"001f" || propType == L"001e") {
                    std::wstring val = decodeSubstgStr(e, propType);
                    if (propId == L"3001" && !val.empty()) r.name = val;
                    else if ((propId == L"39fe" || propId == L"3003") && !val.empty()) r.email = val;
                }
            } else if (parent.rfind(L"__attach_version1.0_", 0) == 0) {
                auto& a = attachs[parent];
                if (propType == L"001f" || propType == L"001e") {
                    std::wstring val = decodeSubstgStr(e, propType);
                    if (propId == L"3707" && !val.empty()) a.filename = val;
                    else if (propId == L"3704" && a.filename.empty() && !val.empty()) a.filename = val;
                    else if (propId == L"370e" && !val.empty()) a.mime = val;
                } else if (propId == L"3701" && propType == L"0102") {
                    a.byteSize = e.size;
                }
            }
        }
    }

    auto getP = [&](const wchar_t* k1, const wchar_t* k2 = nullptr) -> std::wstring {
        auto it = rootProps.find(k1);
        if (it != rootProps.end() && !it->second.empty()) return it->second;
        if (k2) {
            auto it2 = rootProps.find(k2);
            if (it2 != rootProps.end()) return it2->second;
        }
        return {};
    };

    std::wstring subject    = getP(L"0037");
    std::wstring msgClass   = getP(L"001a");
    std::wstring senderName = getP(L"0c1a", L"0042");
    std::wstring senderMail = getP(L"5d01", L"0c1f");
    if (senderMail.empty()) senderMail = getP(L"0065");
    std::wstring dispTo     = getP(L"0e04");
    std::wstring dispCc     = getP(L"0e03");
    std::wstring msgId      = getP(L"1035");
    std::wstring transHdrs  = getP(L"007d");
    std::wstring plainBody  = getP(L"1000");

    if (plainBody.empty()) {
        std::wstring htmlStr = getP(L"1013");
        if (htmlStr.empty() && !htmlBodyBin.empty()) {
            htmlStr = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(htmlBodyBin.data()), htmlBodyBin.size()));
        }
        if (!htmlStr.empty()) {
            auto ps = StripHtmlToParagraphs(htmlStr, 200);
            for (const auto& p : ps) {
                if (!plainBody.empty()) plainBody += L"\n";
                plainBody += p;
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Outlook MAPI Message (.msg)";

    SpreadsheetSheet hdrSh;
    hdrSh.name = L"Email_Headers";
    hdrSh.rows.push_back({ L"Header / Property", L"Value" });
    if (!msgClass.empty())   hdrSh.rows.push_back({ L"Message Class", msgClass });
    if (!subject.empty())    hdrSh.rows.push_back({ L"Subject", subject });
    if (!senderName.empty()) hdrSh.rows.push_back({ L"Sender Name", senderName });
    if (!senderMail.empty()) hdrSh.rows.push_back({ L"Sender Email", senderMail });
    if (!dispTo.empty())     hdrSh.rows.push_back({ L"To", dispTo });
    if (!dispCc.empty())     hdrSh.rows.push_back({ L"CC", dispCc });
    if (!msgId.empty())      hdrSh.rows.push_back({ L"Message-ID", msgId });
    hdrSh.rows.push_back({ L"Attachments", std::to_wstring(attachs.size()) });
    if (!transHdrs.empty())  hdrSh.rows.push_back({ L"Transport Headers", transHdrs.substr(0, 512) });
    FinalizeSheetCols(hdrSh);
    out.sheets.push_back(std::move(hdrSh));

    SpreadsheetSheet bodySh;
    bodySh.name = L"Email_Body";
    bodySh.rows.push_back({ L"Line #", L"Text" });
    if (!plainBody.empty()) {
        size_t ls = 0;
        size_t lineNo = 0;
        while (ls < plainBody.size() && lineNo < 2000) {
            size_t le = plainBody.find(L'\n', ls);
            if (le == std::wstring::npos) le = plainBody.size();
            std::wstring ln = PathUtils::Trim(std::wstring_view(plainBody).substr(ls, le - ls));
            ls = (le < plainBody.size()) ? (le + 1) : plainBody.size();
            if (!ln.empty()) bodySh.rows.push_back({ std::to_wstring(++lineNo), ln });
        }
    }
    FinalizeSheetCols(bodySh);
    out.sheets.push_back(std::move(bodySh));

    if (!recips.empty()) {
        SpreadsheetSheet rSh;
        rSh.name = L"Recipients";
        rSh.rows.push_back({ L"Display Name", L"Email Address" });
        for (const auto& kv : recips) rSh.rows.push_back({ kv.second.name, kv.second.email });
        FinalizeSheetCols(rSh);
        out.sheets.push_back(std::move(rSh));
    }

    if (!attachs.empty()) {
        SpreadsheetSheet aSh;
        aSh.name = L"Attachments";
        aSh.rows.push_back({ L"Filename", L"MIME Type", L"Byte Size" });
        for (const auto& kv : attachs) {
            aSh.rows.push_back({
                kv.second.filename.empty() ? L"(attachment)" : kv.second.filename,
                kv.second.mime,
                PathUtils::FormatByteSize(kv.second.byteSize)
            });
        }
        FinalizeSheetCols(aSh);
        out.sheets.push_back(std::move(aSh));
    }

    // Also populate paragraphs for unified document text inspection
    if (!subject.empty()) out.paragraphs.push_back({ L"Subject: " + subject, 1, false, false });
    if (!senderName.empty() || !senderMail.empty()) {
        out.paragraphs.push_back({ L"From: " + senderName + (senderMail.empty() ? L"" : (L" <" + senderMail + L">")), 2, false, false });
    }
    if (!plainBody.empty()) out.paragraphs.push_back({ plainBody, 0, false, false });
    ComputeDocCounts(out);

    out.valid = true;
    return true;
}

// ============================================================================
// 10. Mobipocket / Kindle E-Book (.mobi / .azw / .azw3 / .prc — PalmDB + LZ77)
// ============================================================================
inline bool ParseMobiPalmDb(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.size() < 78) return false;
    bool isBookMobi = (std::memcmp(raw.data() + 60, "BOOKMOBI", 8) == 0);
    bool isTextRead = (std::memcmp(raw.data() + 60, "TEXtREAd", 8) == 0);
    if (!isBookMobi && !isTextRead) return false;

    std::wstring pdbName = FixedAsciiField(raw.data(), 32);
    uint16_t numRecords = DetailSys100::RdU16BE(raw.data() + 76);
    if (numRecords == 0 || 78ULL + static_cast<size_t>(numRecords) * 8ULL > raw.size()) return false;

    std::vector<uint32_t> recOff(numRecords);
    for (uint16_t i = 0; i < numRecords; ++i) {
        recOff[i] = DetailSys100::RdU32BE(raw.data() + 78 + static_cast<size_t>(i) * 8);
    }
    auto getRecEnd = [&](uint16_t idx) -> size_t {
        uint32_t start = recOff[idx];
        size_t best = raw.size();
        for (uint16_t j = idx + 1; j < numRecords; ++j) {
            if (recOff[j] >= start && recOff[j] <= raw.size()) {
                best = recOff[j];
                break;
            }
        }
        return best;
    };

    if (recOff[0] >= raw.size()) return false;
    size_t r0End = getRecEnd(0);
    if (r0End <= recOff[0] || r0End - recOff[0] < 16) return false;
    const uint8_t* r0 = raw.data() + recOff[0];
    const size_t r0Len = r0End - recOff[0];

    uint16_t compression = DetailSys100::RdU16BE(r0);
    uint32_t textLength  = DetailSys100::RdU32BE(r0 + 4);
    uint16_t textRecCnt  = DetailSys100::RdU16BE(r0 + 8);
    uint16_t recSize     = DetailSys100::RdU16BE(r0 + 10);
    uint16_t encType     = DetailSys100::RdU16BE(r0 + 12);

    uint32_t textEncoding = 65001;
    uint32_t mobiVer = 0;
    uint16_t extraDataFlags = 0;
    std::wstring fullTitle = pdbName;
    std::vector<std::pair<std::wstring, std::wstring>> exthMeta;

    auto decodeMobiBytes = [&](const uint8_t* p, size_t n) -> std::wstring {
        if (textEncoding == 1252) return Latin1OrCp1252ToWide(p, n);
        return PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), n)));
    };

    if (r0Len >= 24 && std::memcmp(r0 + 16, "MOBI", 4) == 0) {
        uint32_t mobiHdrLen = DetailSys100::RdU32BE(r0 + 20);
        if (r0Len >= 40) {
            textEncoding = DetailSys100::RdU32BE(r0 + 28);
            mobiVer      = DetailSys100::RdU32BE(r0 + 36);
        }
        if (mobiHdrLen >= 76 && r0Len >= 92) {
            uint32_t fnOff = DetailSys100::RdU32BE(r0 + 84);
            uint32_t fnLen = DetailSys100::RdU32BE(r0 + 88);
            if (fnOff < r0Len && fnLen > 0 && fnLen <= r0Len - fnOff) {
                std::wstring ft = decodeMobiBytes(r0 + fnOff, fnLen);
                if (!ft.empty()) fullTitle = ft;
            }
        }
        uint32_t exthFlags = (mobiHdrLen >= 116 && r0Len >= 132) ? DetailSys100::RdU32BE(r0 + 128) : 0;
        if (mobiHdrLen >= 228 && r0Len >= 244) {
            extraDataFlags = DetailSys100::RdU16BE(r0 + 242);
        }

        size_t exthOff = 16ULL + mobiHdrLen;
        if ((exthFlags & 0x40u) != 0 && exthOff + 12 <= r0Len && std::memcmp(r0 + exthOff, "EXTH", 4) == 0) {
            uint32_t exthLen  = DetailSys100::RdU32BE(r0 + exthOff + 4);
            uint32_t exthCnt  = DetailSys100::RdU32BE(r0 + exthOff + 8);
            size_t ep = exthOff + 12;
            size_t eLimit = std::min<size_t>(r0Len, exthOff + exthLen);
            for (uint32_t i = 0; i < exthCnt && i < 256 && ep + 8 <= eLimit; ++i) {
                uint32_t rType = DetailSys100::RdU32BE(r0 + ep);
                uint32_t rLen  = DetailSys100::RdU32BE(r0 + ep + 4);
                if (rLen < 8 || ep + rLen > eLimit) break;
                const uint8_t* rp = r0 + ep + 8;
                size_t payLen = rLen - 8;
                ep += rLen;

                auto exthLabel = [](uint32_t t) -> const wchar_t* {
                    switch (t) {
                        case 100: return L"Author";
                        case 101: return L"Publisher";
                        case 103: return L"Description";
                        case 104: return L"ISBN";
                        case 105: return L"Subject";
                        case 106: return L"Publication Date";
                        case 108: return L"Contributor";
                        case 109: return L"Rights";
                        case 113: return L"ASIN";
                        case 503: return L"Updated Title";
                        case 524: return L"Language";
                        default:  return nullptr;
                    }
                };
                if (const wchar_t* lbl = exthLabel(rType)) {
                    std::wstring val = decodeMobiBytes(rp, payLen);
                    if (!val.empty()) {
                        exthMeta.push_back({ lbl, val });
                        if (rType == 503) fullTitle = val;
                    }
                }
            }
        }
    }

    // Decompress Records 1 .. min(textRecordCount, 32) using real PalmDOC LZ77
    std::vector<uint8_t> decomp;
    if (encType == 0 && (compression == 1 || compression == 2)) {
        uint16_t maxRec = std::min<uint16_t>(textRecCnt, std::min<uint16_t>(static_cast<uint16_t>(numRecords - 1), 32));
        decomp.reserve(static_cast<size_t>(maxRec) * 4096);
        const size_t maxTotalOut = 262144;

        for (uint16_t rIdx = 1; rIdx <= maxRec && decomp.size() < maxTotalOut; ++rIdx) {
            if (recOff[rIdx] >= raw.size()) continue;
            size_t rEnd = getRecEnd(rIdx);
            if (rEnd <= recOff[rIdx]) continue;
            const uint8_t* src = raw.data() + recOff[rIdx];
            size_t srcLen = rEnd - recOff[rIdx];

            // Trim trailing MOBI extra record entries if extraDataFlags set
            if (extraDataFlags != 0 && srcLen > 4) {
                for (int bit = 15; bit >= 1; --bit) {
                    if ((extraDataFlags & (1u << bit)) != 0 && srcLen > 0) {
                        size_t tailSz = 0;
                        int shift = 0;
                        size_t p = srcLen;
                        while (p > 0 && shift < 28) {
                            uint8_t b = src[--p];
                            tailSz |= static_cast<size_t>(b & 0x7Fu) << shift;
                            shift += 7;
                            if ((b & 0x80u) != 0) break; // Backward varint stop bit
                        }
                        if (tailSz > 0 && tailSz < srcLen) srcLen -= tailSz;
                    }
                }
                if ((extraDataFlags & 1u) != 0 && srcLen > 0) {
                    size_t mbTrim = (src[srcLen - 1] & 0x03u) + 1;
                    if (mbTrim < srcLen) srcLen -= mbTrim;
                }
            }

            if (compression == 1) {
                size_t take = std::min<size_t>(srcLen, maxTotalOut - decomp.size());
                decomp.insert(decomp.end(), src, src + take);
            } else if (compression == 2) {
                size_t i = 0;
                while (i < srcLen && decomp.size() < maxTotalOut) {
                    uint8_t c = src[i++];
                    if (c == 0x00 || (c >= 0x09 && c <= 0x7F)) {
                        decomp.push_back(c);
                    } else if (c >= 0x01 && c <= 0x08) {
                        for (uint8_t k = 0; k < c && i < srcLen && decomp.size() < maxTotalOut; ++k) {
                            decomp.push_back(src[i++]);
                        }
                    } else if (c >= 0x80 && c <= 0xBF) {
                        if (i >= srcLen) break;
                        uint16_t pair = (static_cast<uint16_t>(c) << 8) | src[i++];
                        size_t dist = (pair >> 3) & 0x07FFu;
                        size_t copyLen = (pair & 0x07u) + 3;
                        if (dist == 0 || dist > decomp.size()) continue;
                        for (size_t k = 0; k < copyLen && decomp.size() < maxTotalOut; ++k) {
                            uint8_t b = decomp[decomp.size() - dist];
                            decomp.push_back(b);
                        }
                    } else { // 0xC0..0xFF: ' ' + (c ^ 0x80)
                        decomp.push_back(' ');
                        if (decomp.size() < maxTotalOut) decomp.push_back(static_cast<uint8_t>(c ^ 0x80u));
                    }
                }
            }
        }
    }

    std::wstring rawMarkup = decodeMobiBytes(decomp.data(), decomp.size());
    std::vector<std::wstring> chapters = StripHtmlToParagraphs(rawMarkup, 500);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Kindle / Mobipocket E-Book (" + extLower + L")";

    SpreadsheetSheet meta;
    meta.name = L"Book_Metadata";
    meta.rows.push_back({ L"Property", L"Value" });
    meta.rows.push_back({ L"Title", fullTitle.empty() ? L"(Untitled)" : fullTitle });
    meta.rows.push_back({ L"PalmDB Name", pdbName });
    meta.rows.push_back({ L"MOBI Version", std::to_wstring(mobiVer) });
    meta.rows.push_back({ L"Compression",
        (compression == 1) ? L"Uncompressed (1)" : ((compression == 2) ? L"PalmDOC LZ77 (2)" :
        ((compression == 17480) ? L"HUFF/CDIC (17480)" : std::to_wstring(compression))) });
    meta.rows.push_back({ L"Uncompressed Text Size", PathUtils::FormatByteSize(textLength) });
    meta.rows.push_back({ L"PalmDB Records / Text Records",
        std::to_wstring(numRecords) + L" / " + std::to_wstring(textRecCnt) + L" (blk " + std::to_wstring(recSize) + L" B)" });
    meta.rows.push_back({ L"Encoding", (textEncoding == 65001) ? L"UTF-8 (65001)" : std::to_wstring(textEncoding) });
    if (encType != 0) meta.rows.push_back({ L"DRM Encryption", L"Encrypted (type " + std::to_wstring(encType) + L")" });
    for (const auto& kv : exthMeta) {
        meta.rows.push_back({ L"EXTH " + kv.first, kv.second });
    }
    FinalizeSheetCols(meta);
    out.sheets.push_back(std::move(meta));

    if (!chapters.empty()) {
        SpreadsheetSheet txtSh;
        txtSh.name = L"Book_Text";
        txtSh.rows.push_back({ L"Paragraph #", L"Decompressed E-Book Text" });
        for (size_t i = 0; i < chapters.size(); ++i) {
            txtSh.rows.push_back({ std::to_wstring(i + 1), chapters[i] });
            out.paragraphs.push_back({ chapters[i], 0, false, false });
        }
        FinalizeSheetCols(txtSh);
        out.sheets.push_back(std::move(txtSh));
        ComputeDocCounts(out);
    }

    out.valid = true;
    return true;
}

// ============================================================================
// 11. AT&T DjVu Document (.djvu / .djv — EA IFF 85 AT&TFORM)
// ============================================================================
inline bool ParseDjVuDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "AT&TFORM", 8) != 0) return false;
    std::string rootType(reinterpret_cast<const char*>(raw.data() + 12), 4);
    if (rootType != "DJVU" && rootType != "DJVM" && rootType != "DJVI" && rootType != "THUM") return false;

    uint32_t formSize = DetailSys100::RdU32BE(raw.data() + 8);
    uint16_t dirmFiles = 0;
    size_t pageCount = 0;

    SpreadsheetSheet pages;
    pages.name = L"Pages_&_Chunks";
    pages.rows.push_back({ L"Scope", L"Chunk", L"Size (B)", L"Decoded Attributes" });

    std::vector<std::wstring> ocrTexts;

    auto walkIff = [&](auto& self, size_t start, size_t limit, const std::wstring& scope, int depth) -> void {
        if (depth > 8) return;
        size_t pos = start;
        while (pos + 8 <= limit && pos + 8 <= raw.size() && pages.rows.size() < 2048) {
            std::string cid(reinterpret_cast<const char*>(raw.data() + pos), 4);
            uint32_t csz = DetailSys100::RdU32BE(raw.data() + pos + 4);
            size_t body = pos + 8;
            size_t bodyEnd = std::min<size_t>(raw.size(), std::min<size_t>(limit, body + csz));
            const uint8_t* p = raw.data() + body;
            size_t avail = bodyEnd - body;

            if (cid == "FORM" && avail >= 4) {
                std::string subForm(reinterpret_cast<const char*>(p), 4);
                std::wstring subScope = PathUtils::Utf8ToWide(subForm);
                if (subForm == "DJVU") {
                    ++pageCount;
                    subScope = L"Page " + std::to_wstring(pageCount);
                }
                self(self, body + 4, bodyEnd, subScope, depth + 1);
            } else {
                std::wstring desc;
                if (cid == "DIRM" && avail >= 3) {
                    dirmFiles = DetailSys100::RdU16BE(p + 1);
                    desc = ((p[0] & 0x80u) ? L"Bundled" : L"Indirect") + std::wstring(L", files=") + std::to_wstring(dirmFiles);
                } else if (cid == "INFO" && avail >= 10) {
                    uint16_t w = DetailSys100::RdU16BE(p);
                    uint16_t h = DetailSys100::RdU16BE(p + 2);
                    uint8_t minV = p[4], majV = p[5];
                    uint16_t dpi = DetailSys100::RdU16LE(p + 6);
                    uint8_t gamma = p[8];
                    if (pageCount == 0 && rootType == "DJVU") pageCount = 1;
                    wchar_t gbuf[128] = {};
                    swprintf_s(gbuf, L"%ux%u px, %u DPI, v%u.%u, gamma=%.1f", w, h, dpi, majV, minV, gamma / 10.0);
                    desc = gbuf;
                } else if (cid == "TXTa" && avail >= 3) {
                    uint32_t tLen = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
                    size_t take = std::min<size_t>(tLen, avail - 3);
                    std::wstring txt = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p + 3), take)));
                    desc = L"OCR Text (" + std::to_wstring(take) + L" B)";
                    if (!txt.empty() && ocrTexts.size() < 256) ocrTexts.push_back(scope + L": " + txt);
                } else if (cid == "TXTz") {
                    desc = L"BWT Compressed OCR Text";
                } else if (cid == "Sjbz") {
                    desc = L"JB2 Bi-tonal Foreground Mask";
                } else if (cid == "BG44" || cid == "FG44") {
                    desc = L"IW44 Wavelet Color Layer";
                } else if (cid == "INCL" && avail > 0) {
                    desc = L"Include: " + FixedAsciiField(p, avail);
                } else if (cid == "ANTa" && avail > 0) {
                    desc = FixedAsciiField(p, std::min<size_t>(avail, 120));
                }
                pages.rows.push_back({ scope, PathUtils::Utf8ToWide(cid), std::to_wstring(csz), desc });
            }
            pos = body + csz + (csz & 1u);
        }
    };

    size_t rootLimit = std::min<size_t>(raw.size(), 12ULL + formSize);
    walkIff(walkIff, 16, rootLimit, PathUtils::Utf8ToWide(rootType), 0);

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"AT&T DjVu Document (.djvu)";

    SpreadsheetSheet sum;
    sum.name = L"DjVu_Overview";
    sum.rows.push_back({ L"Property", L"Value" });
    sum.rows.push_back({ L"Root FORM Type", PathUtils::Utf8ToWide(rootType) });
    sum.rows.push_back({ L"Document Pages", std::to_wstring(std::max<size_t>(pageCount, dirmFiles)) });
    sum.rows.push_back({ L"File Size", PathUtils::FormatByteSize(raw.size()) });
    FinalizeSheetCols(sum);
    FinalizeSheetCols(pages);
    out.sheets.push_back(std::move(sum));
    out.sheets.push_back(std::move(pages));

    if (!ocrTexts.empty()) {
        SpreadsheetSheet tsh;
        tsh.name = L"OCR_Text";
        tsh.rows.push_back({ L"#", L"Extracted OCR Text" });
        for (size_t i = 0; i < ocrTexts.size(); ++i) {
            tsh.rows.push_back({ std::to_wstring(i + 1), ocrTexts[i] });
        }
        FinalizeSheetCols(tsh);
        out.sheets.push_back(std::move(tsh));
    }

    out.valid = true;
    return true;
}

// ============================================================================
// 12. Microsoft Compiled HTML Help (.chm — ITSF / ITSP / PMGL)
// ============================================================================
inline bool ReadChmEncInt(const uint8_t*& p, const uint8_t* end, uint64_t& outVal) {
    outVal = 0;
    int steps = 0;
    while (p < end && steps++ < 10) {
        uint8_t b = *p++;
        outVal = (outVal << 7) | static_cast<uint64_t>(b & 0x7Fu);
        if ((b & 0x80u) == 0) return true;
    }
    return false;
}

inline bool ParseChmHelp(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 88 || std::memcmp(raw.data(), "ITSF", 4) != 0) return false;

    uint32_t itsfVer = DetailSys100::RdU32LE(raw.data() + 4);
    uint32_t hdrLen  = DetailSys100::RdU32LE(raw.data() + 8);
    uint32_t langId  = DetailSys100::RdU32LE(raw.data() + 20);
    if (itsfVer < 2 || itsfVer > 4) return false;

    uint64_t dirOff  = DetailSys100::RdU64LE(raw.data() + 72);
    uint64_t dirLen  = DetailSys100::RdU64LE(raw.data() + 80);
    uint64_t dataOff = (itsfVer >= 3 && hdrLen >= 96 && raw.size() >= 96)
        ? DetailSys100::RdU64LE(raw.data() + 88)
        : (dirOff + dirLen);

    if (dirOff >= raw.size() || dirOff + 84 > raw.size() || std::memcmp(raw.data() + static_cast<size_t>(dirOff), "ITSP", 4) != 0) {
        return false;
    }

    const uint8_t* itsp = raw.data() + static_cast<size_t>(dirOff);
    uint32_t itspVer      = DetailSys100::RdU32LE(itsp + 4);
    uint32_t itspHdrLen   = DetailSys100::RdU32LE(itsp + 8);
    uint32_t dirBlockSize = DetailSys100::RdU32LE(itsp + 16);
    uint32_t numDirBlocks = DetailSys100::RdU32LE(itsp + 48);
    if (dirBlockSize < 64 || dirBlockSize > 1048576 || itspHdrLen < 84) return false;

    struct ChmEntry {
        std::wstring path;
        uint64_t section = 0;
        uint64_t offset = 0;
        uint64_t length = 0;
    };
    std::vector<ChmEntry> entries;

    size_t blocksStart = static_cast<size_t>(dirOff) + itspHdrLen;
    size_t maxBlocks = std::min<size_t>(numDirBlocks > 0 ? numDirBlocks : 1024, 4096);
    for (size_t b = 0; b < maxBlocks; ++b) {
        size_t bOff = blocksStart + b * dirBlockSize;
        if (bOff + 20 > raw.size()) break;
        size_t blkAvail = std::min<size_t>(dirBlockSize, raw.size() - bOff);
        const uint8_t* blk = raw.data() + bOff;
        if (std::memcmp(blk, "PMGL", 4) != 0) continue;

        uint32_t freeSpace = DetailSys100::RdU32LE(blk + 4);
        const uint8_t* p = blk + 20;
        const uint8_t* pEnd = (freeSpace <= blkAvail - 20) ? (blk + blkAvail - freeSpace) : (blk + blkAvail);

        while (p < pEnd && entries.size() < 4096) {
            uint64_t nameLen = 0, sec = 0, off = 0, len = 0;
            if (!ReadChmEncInt(p, pEnd, nameLen) || nameLen == 0 || nameLen > static_cast<uint64_t>(pEnd - p)) break;
            std::wstring path = PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(p), static_cast<size_t>(nameLen)));
            p += static_cast<size_t>(nameLen);
            if (!ReadChmEncInt(p, pEnd, sec) || !ReadChmEncInt(p, pEnd, off) || !ReadChmEncInt(p, pEnd, len)) break;
            entries.push_back({ std::move(path), sec, off, len });
        }
    }

    // Extract uncompressed Section 0 preview text (e.g. /#STRINGS or uncompressed HTML topic)
    std::vector<std::wstring> sec0Previews;
    for (const auto& e : entries) {
        if (e.section == 0 && e.length > 0 && dataOff + e.offset + e.length <= raw.size()) {
            const uint8_t* sp = raw.data() + static_cast<size_t>(dataOff + e.offset);
            size_t slen = static_cast<size_t>(std::min<uint64_t>(e.length, 4096));
            if (e.path == L"/#STRINGS") {
                size_t idx = 1;
                while (idx < slen && sec0Previews.size() < 8) {
                    size_t nul = idx;
                    while (nul < slen && sp[nul] != 0) ++nul;
                    if (nul > idx) {
                        std::wstring s = PathUtils::Trim(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp + idx), nul - idx)));
                        if (s.size() >= 2) sec0Previews.push_back(L"#STRINGS: " + s);
                    }
                    idx = nul + 1;
                }
            } else if (!e.path.empty() && e.path[0] == L'/' && e.path.find(L".htm") != std::wstring::npos && sec0Previews.size() < 8) {
                auto ps = StripHtmlToParagraphs(PathUtils::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(sp), slen)), 4);
                for (const auto& p : ps) sec0Previews.push_back(e.path + L": " + p);
            }
        }
    }

    out.kind = OfficeFileKind::SqliteDb;
    out.isSpreadsheet = true;
    out.formatTitle = L"Microsoft Compiled HTML Help (.chm)";

    SpreadsheetSheet hdr;
    hdr.name = L"CHM_Header";
    hdr.rows.push_back({ L"Property", L"Value" });
    hdr.rows.push_back({ L"ITSF / ITSP Version", std::to_wstring(itsfVer) + L" / " + std::to_wstring(itspVer) });
    hdr.rows.push_back({ L"Language LCID", DetailSys100::Hex32(langId) });
    hdr.rows.push_back({ L"Directory Block Size", std::to_wstring(dirBlockSize) + L" B (" + std::to_wstring(numDirBlocks) + L" blocks)" });
    hdr.rows.push_back({ L"Total Directory Entries", std::to_wstring(entries.size()) });
    for (const auto& pv : sec0Previews) {
        hdr.rows.push_back({ L"Embedded String / Topic", pv });
    }
    FinalizeSheetCols(hdr);
    out.sheets.push_back(std::move(hdr));

    SpreadsheetSheet dir;
    dir.name = L"CHM_Directory";
    dir.rows.push_back({ L"Path", L"Section", L"Offset", L"Uncompressed Size" });
    for (const auto& e : entries) {
        dir.rows.push_back({
            e.path,
            (e.section == 0) ? L"0 (Uncompressed)" : (L"1 (LZX #" + std::to_wstring(e.section) + L")"),
            std::to_wstring(e.offset),
            std::to_wstring(e.length)
        });
    }
    FinalizeSheetCols(dir);
    out.sheets.push_back(std::move(dir));
    out.valid = true;
    return true;
}

} // namespace DetailMedia100

inline bool ParseExtendedMediaDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    if (raw.empty()) return false;

    if (extLower == L".eml" || extLower == L".mbox")
        return DetailMedia100::ParseEmlOrMbox(raw, extLower, out);
    if (extLower == L".mp3")
        return DetailMedia100::ParseMp3Audio(raw, out);
    if (extLower == L".ogg" || extLower == L".opus" || extLower == L".spx")
        return DetailMedia100::ParseOggContainer(raw, extLower, out);
    if (extLower == L".mod")
        return DetailMedia100::ParseTrackerMod(raw, out);
    if (extLower == L".xm")
        return DetailMedia100::ParseTrackerXm(raw, out);
    if (extLower == L".s3m")
        return DetailMedia100::ParseTrackerS3m(raw, out);
    if (extLower == L".it")
        return DetailMedia100::ParseTrackerIt(raw, out);
    if (extLower == L".sf2")
        return DetailMedia100::ParseSoundFontSf2(raw, out);
    if (extLower == L".sfz")
        return DetailMedia100::ParseSoundFontSfz(raw, out);
    if (extLower == L".mscz")
        return DetailMedia100::ParseMuseScoreMscz(raw, out);
    if (extLower == L".als")
        return DetailMedia100::ParseAbletonAls(raw, out);
    if (extLower == L".flp")
        return DetailMedia100::ParseFlStudioFlp(raw, out);
    if (extLower == L".gp" || extLower == L".gp3" || extLower == L".gp4" || extLower == L".gp5")
        return DetailMedia100::ParseGuitarPro(raw, extLower, out);
    if (extLower == L".rpp")
        return DetailMedia100::ParseReaperRpp(raw, out);
    if (extLower == L".edl")
        return DetailMedia100::ParseCmxEdl(raw, out);
    if (extLower == L".fcpxml")
        return DetailMedia100::ParseFcpXml(raw, out);
    if (extLower == L".otio")
        return DetailMedia100::ParseOpenTimelineOtio(raw, out);
    if (extLower == L".idx")
        return DetailMedia100::ParseVobSubIdx(raw, out);
    if (extLower == L".sup")
        return DetailMedia100::ParseBluraySup(raw, out);
    if (extLower == L".sub")
        return DetailMedia100::ParseSubSubtitle(raw, out);
    if (extLower == L".msg")
        return DetailMedia100::ParseOutlookMsg(raw, out);
    if (extLower == L".mobi" || extLower == L".azw" || extLower == L".azw3" || extLower == L".prc")
        return DetailMedia100::ParseMobiPalmDb(raw, extLower, out);
    if (extLower == L".djvu" || extLower == L".djv")
        return DetailMedia100::ParseDjVuDocument(raw, out);
    if (extLower == L".chm")
        return DetailMedia100::ParseChmHelp(raw, out);

    return false;
}

} // namespace Formats
