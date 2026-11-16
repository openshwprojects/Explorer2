#pragma once

static void AddDocParagraph(ParsedOfficeDocument& out, std::wstring text, int headingLevel = 0, bool isBullet = false);

static bool ParseRtfDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    out.kind = OfficeFileKind::RichTextRtf;
    out.isSpreadsheet = false;
    out.formatTitle = L"Rich Text Document (.rtf)";

    std::string_view sv(reinterpret_cast<const char*>(raw.data()), raw.size());
    // Strip optional UTF-8 BOM and leading ASCII whitespace, then validate "{\rtf" signature
    if (sv.size() >= 3 && static_cast<uint8_t>(sv[0]) == 0xEFu &&
        static_cast<uint8_t>(sv[1]) == 0xBBu && static_cast<uint8_t>(sv[2]) == 0xBFu)
    {
        sv.remove_prefix(3);
    }
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    if (sv.size() < 6 || sv.substr(0, 5) != "{\\rtf") {
        return false;
    }

    struct RtfState {
        int ucSkip = 1;
        uint32_t codepage = 1252;
        int curFontId = -1;
        bool hasExplicitFontCpg = false;
        bool inFontTbl = false;
        bool skipDest = false;
    };

    std::vector<RtfState> stateStack;
    std::unordered_map<int, uint32_t> fontCodepages;
    uint32_t defaultCodepage = 1252;
    std::string pendingBytes;
    uint32_t pendingCodepage = 1252;
    std::wstring curText;

    int skipFallbackUnits = 0;
    int skipFallbackGroupDepth = -1;

    bool inTableRow = false;
    std::vector<std::wstring> curRowCells;
    SpreadsheetSheet tableSheet;
    tableSheet.name = L"RTF_Tables";

    auto trimWide = [](std::wstring_view w) -> std::wstring {
        size_t a = 0;
        while (a < w.size() && (w[a] == L' ' || w[a] == L'\t' || w[a] == L'\r' || w[a] == L'\n')) ++a;
        size_t b = w.size();
        while (b > a && (w[b - 1] == L' ' || w[b - 1] == L'\t' || w[b - 1] == L'\r' || w[b - 1] == L'\n')) --b;
        return (a < b) ? std::wstring(w.substr(a, b - a)) : std::wstring();
    };

    auto decodeBytesWithCodepage = [](const std::string& bytes, uint32_t cpg) -> std::wstring {
        if (bytes.empty()) return {};
        const UINT winCp = (cpg == 0) ? 1252u : static_cast<UINT>(cpg);
        int wlen = MultiByteToWideChar(winCp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        UINT usedCp = winCp;
        if (wlen <= 0 && winCp != 1252u) {
            usedCp = 1252u;
            wlen = MultiByteToWideChar(usedCp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        }
        if (wlen <= 0) {
            std::wstring fallback;
            fallback.reserve(bytes.size());
            for (unsigned char b : bytes) fallback.push_back(static_cast<wchar_t>(b));
            return fallback;
        }
        std::wstring res(static_cast<size_t>(wlen), L'\0');
        MultiByteToWideChar(usedCp, 0, bytes.data(), static_cast<int>(bytes.size()), res.data(), wlen);
        return res;
    };

    auto flushPendingBytes = [&]() {
        if (pendingBytes.empty()) return;
        curText += decodeBytesWithCodepage(pendingBytes, pendingCodepage);
        pendingBytes.clear();
    };

    auto pushByte = [&](uint8_t b, uint32_t cpg) {
        if (!pendingBytes.empty() && pendingCodepage != cpg) {
            flushPendingBytes();
        }
        pendingCodepage = cpg;
        pendingBytes.push_back(static_cast<char>(b));
    };

    auto flushParagraph = [&]() {
        flushPendingBytes();
        std::wstring clean = trimWide(curText);
        curText.clear();
        if (!clean.empty()) {
            AddDocParagraph(out, std::move(clean), 0, false);
        }
    };

    auto flushTableRow = [&]() {
        flushPendingBytes();
        std::wstring trailing = trimWide(curText);
        curText.clear();
        if (!trailing.empty()) {
            curRowCells.push_back(std::move(trailing));
        }
        if (!curRowCells.empty()) {
            bool anyNonEmpty = false;
            std::wstring joined = L"| ";
            for (size_t c = 0; c < curRowCells.size(); ++c) {
                if (!curRowCells[c].empty()) anyNonEmpty = true;
                if (c > 0) joined += L" | ";
                joined += curRowCells[c];
            }
            joined += L" |";
            if (anyNonEmpty) {
                out.charCount += joined.size();
                bool inW = false;
                for (wchar_t ch : joined) {
                    if (ch > L' ') { if (!inW) { ++out.wordCount; inW = true; } }
                    else inW = false;
                }
                out.paragraphs.push_back({ std::move(joined), 0, false, true });
                if (curRowCells.size() > tableSheet.maxCols) tableSheet.maxCols = curRowCells.size();
                tableSheet.rows.push_back(curRowCells);
            }
            curRowCells.clear();
        }
        inTableRow = false;
    };

    auto hexNibble = [](char c, uint8_t& v) -> bool {
        if (c >= '0' && c <= '9') { v = static_cast<uint8_t>(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { v = static_cast<uint8_t>(c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { v = static_cast<uint8_t>(c - 'A' + 10); return true; }
        return false;
    };

    for (size_t i = 0; i < sv.size(); ++i) {
        const char c = sv[i];

        if (c == '{') {
            flushPendingBytes();
            if (skipFallbackUnits > 0 && skipFallbackGroupDepth < 0) {
                skipFallbackGroupDepth = static_cast<int>(stateStack.size()) + 1;
            }
            RtfState next = stateStack.empty() ? RtfState{ 1, defaultCodepage, -1, false, false, false } : stateStack.back();
            stateStack.push_back(next);
            // Ignorable destination group "{\*\dest ...}" (keep fldrslt if ever marked with \*)
            if (i + 2 < sv.size() && sv[i + 1] == '\\' && sv[i + 2] == '*') {
                size_t pk = i + 3;
                while (pk < sv.size() && (sv[pk] == ' ' || sv[pk] == '\r' || sv[pk] == '\n')) ++pk;
                bool isFldRslt = (pk + 8 <= sv.size() && sv.substr(pk, 8) == "\\fldrslt");
                if (!isFldRslt) {
                    stateStack.back().skipDest = true;
                }
                i += 2; // consume \*
            }
            continue;
        }

        if (c == '}') {
            flushPendingBytes();
            if (!stateStack.empty()) {
                if (static_cast<int>(stateStack.size()) == skipFallbackGroupDepth) {
                    skipFallbackGroupDepth = -1;
                    if (skipFallbackUnits > 0) --skipFallbackUnits;
                }
                stateStack.pop_back();
            }
            if (stateStack.empty()) break; // End of root RTF group
            continue;
        }

        if (stateStack.empty()) continue;
        RtfState& st = stateStack.back();
        if ((st.skipDest && !st.inFontTbl) || skipFallbackGroupDepth >= 0) {
            // Still consume escaped braces/backslashes so group depth never desynchronizes!
            if (c == '\\' && i + 1 < sv.size()) {
                const char nx = sv[i + 1];
                if (nx == '\\' || nx == '{' || nx == '}') ++i;
            }
            continue;
        }
        if (st.inFontTbl && c != '\\') {
            // Ignore literal font names ("Arial;", "Calibri;") inside \fonttbl while still reading \fN & \fcharsetN
            continue;
        }

        if (c == '\r' || c == '\n') {
            continue;
        }

        if (c == '\\') {
            if (i + 1 >= sv.size()) break;
            const char nx = sv[i + 1];

            // Hex byte escape \'hh
            if (nx == '\'') {
                if (i + 3 < sv.size()) {
                    uint8_t hi = 0, lo = 0;
                    if (hexNibble(sv[i + 2], hi) && hexNibble(sv[i + 3], lo)) {
                        i += 3;
                        if (st.skipDest) {
                            continue;
                        }
                        if (skipFallbackUnits > 0) {
                            --skipFallbackUnits;
                        } else {
                            pushByte(static_cast<uint8_t>((hi << 4) | lo), st.codepage);
                        }
                        continue;
                    }
                }
                ++i;
                continue;
            }

            // Control symbols (\{, \}, \\, \~ nbsp, \_ non-breaking hyphen, \- soft hyphen, \* ignorable, \<CR>, \<LF>)
            if (!((nx >= 'a' && nx <= 'z') || (nx >= 'A' && nx <= 'Z'))) {
                ++i;
                if (nx == '*') {
                    st.skipDest = true;
                    st.inFontTbl = false;
                    continue;
                }
                if (st.skipDest) {
                    continue;
                }
                if (nx == '\r' || nx == '\n') {
                    if (nx == '\r' && i + 1 < sv.size() && sv[i + 1] == '\n') ++i;
                    if (inTableRow) {
                        flushPendingBytes();
                        if (!curText.empty() && curText.back() != L' ') curText.push_back(L' ');
                    } else {
                        flushParagraph();
                    }
                    continue;
                }
                if (skipFallbackUnits > 0) {
                    --skipFallbackUnits;
                    continue;
                }
                flushPendingBytes();
                if (nx == '\\' || nx == '{' || nx == '}') curText.push_back(static_cast<wchar_t>(nx));
                else if (nx == '~') curText.push_back(L' ');
                else if (nx == '_') curText.push_back(L'-');
                // '\-' (optional hyphen) and '\:' are intentionally silent
                continue;
            }

            // Alphabetic control word + optional signed integer parameter + optional space delimiter
            const size_t kwStart = ++i;
            while (i < sv.size() && ((sv[i] >= 'a' && sv[i] <= 'z') || (sv[i] >= 'A' && sv[i] <= 'Z'))) ++i;
            const std::string_view kw = sv.substr(kwStart, i - kwStart);

            bool hasParam = false;
            int32_t paramVal = 0;
            if (i < sv.size() && (sv[i] == '-' || (sv[i] >= '0' && sv[i] <= '9'))) {
                hasParam = true;
                bool neg = false;
                if (sv[i] == '-') { neg = true; ++i; }
                int64_t acc = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                    acc = acc * 10 + (sv[i] - '0');
                    if (acc > 0x7FFFFFFFLL) acc = 0x7FFFFFFFLL;
                    ++i;
                }
                paramVal = static_cast<int32_t>(neg ? -acc : acc);
            }
            if (i < sv.size() && sv[i] == ' ') ++i;
            --i; // outer loop will ++i

            // Non-body destinations to skip until closing '}'
            if (kw == "fonttbl") {
                flushPendingBytes();
                st.skipDest = true;
                st.inFontTbl = true;
                continue;
            }
            if (kw == "colortbl" || kw == "stylesheet" || kw == "info" ||
                kw == "pict" || kw == "shppict" || kw == "nonshppict" || kw == "object" ||
                kw == "objdata" || kw == "header" || kw == "footer" || kw == "headerl" ||
                kw == "headerr" || kw == "headerf" || kw == "footerl" || kw == "footerr" ||
                kw == "footerf" || kw == "footnote" || kw == "fldinst" || kw == "xmlnstbl" ||
                kw == "listtable" || kw == "listoverridetable" || kw == "revtbl" ||
                kw == "rsidtbl" || kw == "generator" || kw == "themedata" ||
                kw == "colorschememapping" || kw == "latentstyles" || kw == "datastore")
            {
                flushPendingBytes();
                st.skipDest = true;
                st.inFontTbl = false;
                continue;
            }
            if (st.inFontTbl) {
                if (kw == "f" && hasParam) {
                    st.curFontId = paramVal;
                } else if (kw == "cpg" && hasParam && paramVal > 0 && st.curFontId >= 0) {
                    fontCodepages[st.curFontId] = static_cast<uint32_t>(paramVal);
                } else if (kw == "fcharset" && hasParam && st.curFontId >= 0) {
                    uint32_t fcpg = 0;
                    switch (paramVal) {
                    case 128: fcpg = 932;  break;
                    case 129: fcpg = 949;  break;
                    case 134: fcpg = 936;  break;
                    case 136: fcpg = 950;  break;
                    case 161: fcpg = 1253; break;
                    case 162: fcpg = 1254; break;
                    case 163: fcpg = 1258; break;
                    case 177: fcpg = 1255; break;
                    case 178: fcpg = 1256; break;
                    case 186: fcpg = 1257; break;
                    case 204: fcpg = 1251; break;
                    case 222: fcpg = 874;  break;
                    case 238: fcpg = 1250; break;
                    default: break;
                    }
                    if (fcpg != 0) fontCodepages[st.curFontId] = fcpg;
                } else if (kw == "falt" || kw == "panose" || kw == "fname") {
                    st.inFontTbl = false; // skip nested fonttbl sub-destinations cleanly
                }
                continue;
            }
            if (kw == "f" && hasParam) {
                flushPendingBytes();
                st.curFontId = paramVal;
                auto it = fontCodepages.find(paramVal);
                if (it != fontCodepages.end()) {
                    st.codepage = it->second;
                    st.hasExplicitFontCpg = true;
                } else {
                    st.codepage = defaultCodepage;
                    st.hasExplicitFontCpg = false;
                }
                continue;
            }

            // Codepage & character-set controls
            if (kw == "ansicpg" && hasParam && paramVal > 0) {
                flushPendingBytes();
                defaultCodepage = static_cast<uint32_t>(paramVal);
                for (auto& s : stateStack) {
                    if (!s.hasExplicitFontCpg) s.codepage = defaultCodepage;
                }
                continue;
            }
            if (kw == "cpg" && hasParam && paramVal > 0) {
                flushPendingBytes();
                st.codepage = static_cast<uint32_t>(paramVal);
                st.hasExplicitFontCpg = true;
                continue;
            }
            if (kw == "ansi") { flushPendingBytes(); defaultCodepage = 1252; st.codepage = 1252; continue; }
            if (kw == "mac")  { flushPendingBytes(); defaultCodepage = 10000; st.codepage = 10000; continue; }
            if (kw == "pc")   { flushPendingBytes(); defaultCodepage = 437; st.codepage = 437; continue; }
            if (kw == "pca")  { flushPendingBytes(); defaultCodepage = 850; st.codepage = 850; continue; }
            if (kw == "fcharset" && hasParam) {
                flushPendingBytes();
                switch (paramVal) {
                case 128: st.codepage = 932;  st.hasExplicitFontCpg = true; break; // Shift-JIS
                case 129: st.codepage = 949;  st.hasExplicitFontCpg = true; break; // Hangul
                case 134: st.codepage = 936;  st.hasExplicitFontCpg = true; break; // GB2312
                case 136: st.codepage = 950;  st.hasExplicitFontCpg = true; break; // Big5
                case 161: st.codepage = 1253; st.hasExplicitFontCpg = true; break; // Greek
                case 162: st.codepage = 1254; st.hasExplicitFontCpg = true; break; // Turkish
                case 163: st.codepage = 1258; st.hasExplicitFontCpg = true; break; // Vietnamese
                case 177: st.codepage = 1255; st.hasExplicitFontCpg = true; break; // Hebrew
                case 178: st.codepage = 1256; st.hasExplicitFontCpg = true; break; // Arabic
                case 186: st.codepage = 1257; st.hasExplicitFontCpg = true; break; // Baltic
                case 204: st.codepage = 1251; st.hasExplicitFontCpg = true; break; // Russian / Cyrillic
                case 222: st.codepage = 874;  st.hasExplicitFontCpg = true; break; // Thai
                case 238: st.codepage = 1250; st.hasExplicitFontCpg = true; break; // Central European / Polish
                default: break;
                }
                continue;
            }

            // Unicode \ucN fallback count & \uN signed 16-bit UTF-16 code unit
            if (kw == "uc" && hasParam) {
                st.ucSkip = std::clamp(paramVal, 0, 32);
                continue;
            }
            if (kw == "u" && hasParam) {
                flushPendingBytes();
                const uint16_t codeUnit = static_cast<uint16_t>(static_cast<int16_t>(paramVal));
                if (codeUnit != 0) {
                    curText.push_back(static_cast<wchar_t>(codeUnit));
                }
                skipFallbackUnits = st.ucSkip;
                continue;
            }

            if (skipFallbackUnits > 0) {
                // Control word used as fallback representation (e.g. \u8212\emdash)
                --skipFallbackUnits;
                continue;
            }

            // Table structure controls
            if (kw == "trowd" || kw == "intbl") {
                inTableRow = true;
                continue;
            }
            if (kw == "cell" || kw == "nestcell") {
                flushPendingBytes();
                curRowCells.push_back(trimWide(curText));
                curText.clear();
                inTableRow = true;
                continue;
            }
            if (kw == "row" || kw == "nestrow") {
                flushTableRow();
                continue;
            }

            // Paragraph / line / typographic symbols
            if (kw == "par" || kw == "line" || kw == "sect" || kw == "page") {
                if (inTableRow && (kw == "par" || kw == "line")) {
                    flushPendingBytes();
                    if (!curText.empty() && curText.back() != L' ') curText.push_back(L' ');
                } else {
                    flushParagraph();
                }
            } else if (kw == "tab") {
                flushPendingBytes();
                curText += L"    ";
            } else if (kw == "emdash") {
                flushPendingBytes();
                curText.push_back(0x2014);
            } else if (kw == "endash") {
                flushPendingBytes();
                curText.push_back(0x2013);
            } else if (kw == "bullet") {
                flushPendingBytes();
                curText.push_back(0x2022);
            } else if (kw == "lquote") {
                flushPendingBytes();
                curText.push_back(0x2018);
            } else if (kw == "rquote") {
                flushPendingBytes();
                curText.push_back(0x2019);
            } else if (kw == "ldblquote") {
                flushPendingBytes();
                curText.push_back(0x201C);
            } else if (kw == "rdblquote") {
                flushPendingBytes();
                curText.push_back(0x201D);
            }
            continue;
        }

        // Literal character inside current group
        if (skipFallbackUnits > 0) {
            --skipFallbackUnits;
            continue;
        }

        const uint8_t ub = static_cast<uint8_t>(c);
        if (ub < 0x80u && pendingBytes.empty()) {
            if (ub == '\t') curText += L"    ";
            else if (ub >= 0x20u) curText.push_back(static_cast<wchar_t>(ub));
        } else {
            pushByte(ub, st.codepage);
        }
    }

    if (!curRowCells.empty()) {
        flushTableRow();
    }
    flushParagraph();

    if (!tableSheet.rows.empty()) {
        out.sheets.push_back(std::move(tableSheet));
    }
    if (out.paragraphs.empty()) {
        AddDocParagraph(out, L"(Empty RTF Document)", 0, false);
    }
    out.valid = true;
    return true;
}

static void AddDocParagraph(ParsedOfficeDocument& out, std::wstring text, int headingLevel, bool isBullet) {
    // Trim leading/trailing whitespace
    size_t first = 0;
    while (first < text.size() && (text[first] == L' ' || text[first] == L'\t' || text[first] == L'\r' || text[first] == L'\n')) ++first;
    size_t last = text.size();
    while (last > first && (text[last - 1] == L' ' || text[last - 1] == L'\t' || text[last - 1] == L'\r' || text[last - 1] == L'\n')) --last;
    if (first >= last) return;
    std::wstring clean = text.substr(first, last - first);
    out.charCount += clean.size();
    bool inW = false;
    for (wchar_t ch : clean) {
        if (ch > L' ') { if (!inW) { ++out.wordCount; inW = true; } }
        else inW = false;
    }
    out.paragraphs.push_back({ std::move(clean), headingLevel, isBullet, false });
}

static std::wstring StripXmlHtmlTags(std::string_view sv) {
    std::string plain;
    plain.reserve(sv.size());
    bool inTag = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '<') {
            inTag = true;
            if (i + 3 < sv.size() && (sv.substr(i, 3) == "<br" || sv.substr(i, 3) == "<BR")) {
                plain.push_back(' ');
            }
            continue;
        }
        if (c == '>') {
            inTag = false;
            continue;
        }
        if (!inTag) {
            if (c == '\r' || c == '\n' || c == '\t') plain.push_back(' ');
            else plain.push_back(c);
        }
    }
    return DecodeXmlEntities(PathUtils::Utf8ToWide(plain));
}
