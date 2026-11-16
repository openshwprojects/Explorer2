#pragma once
// LegacyOfficeOle.h — Native Binary Decoders for Legacy Microsoft Office OLE2 Documents
//   1. Microsoft Word 97–2003 & 6.0/95 ([MS-DOC]: .doc, .dot) — FIB + Clx Piece Table (CP1252 & UTF-16LE) + Fields + Tables
//   2. Microsoft PowerPoint 97–2003 ([MS-PPT] & [MS-ODRAW]: .ppt, .pps) — RecordHeader Tree + SlideListWithText + Escher Shapes

namespace Formats {

namespace LegacyOfficeDetail {

inline uint16_t RdU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline int16_t RdI16LE(const uint8_t* p) {
    return static_cast<int16_t>(RdU16LE(p));
}

inline uint32_t RdU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t RdI32LE(const uint8_t* p) {
    return static_cast<int32_t>(RdU32LE(p));
}

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

inline std::wstring TrimWideParagraph(std::wstring_view sv) {
    size_t first = 0;
    while (first < sv.size() && (sv[first] == L' ' || sv[first] == L'\t' || sv[first] == L'\r' || sv[first] == L'\n')) {
        ++first;
    }
    size_t last = sv.size();
    while (last > first && (sv[last - 1] == L' ' || sv[last - 1] == L'\t' || sv[last - 1] == L'\r' || sv[last - 1] == L'\n')) {
        --last;
    }
    return (first < last) ? std::wstring(sv.substr(first, last - first)) : std::wstring();
}

inline void PushOfficeParagraph(ParsedOfficeDocument& out, std::wstring text, int headingLevel, bool isBullet, bool isTableRow) {
    std::wstring clean = TrimWideParagraph(text);
    if (clean.empty()) return;
    out.charCount += clean.size();
    bool inWord = false;
    for (wchar_t ch : clean) {
        if (ch > L' ') {
            if (!inWord) {
                ++out.wordCount;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }
    out.paragraphs.push_back({ std::move(clean), headingLevel, isBullet, isTableRow });
}

// Decode Clx -> Pcdt -> PlcPcd piece table into UTF-16 character buffer
inline bool DecodeWordClxPieces(
    const std::vector<uint8_t>& wordStream,
    const uint8_t* clxData,
    size_t clxLen,
    bool isWord97UnicodePieces,
    std::wstring& outChars)
{
    outChars.clear();
    if (clxLen < 5) return false;

    size_t pos = 0;
    // Skip optional RgPrc entries (clxt == 0x01, followed by int16_t cbGrpprl and grpprl bytes)
    while (pos < clxLen && clxData[pos] == 0x01u) {
        if (pos + 3 > clxLen) return false;
        const int16_t cbGrpprl = RdI16LE(clxData + pos + 1);
        if (cbGrpprl < 0 || pos + 3 + static_cast<size_t>(cbGrpprl) > clxLen) return false;
        pos += 3 + static_cast<size_t>(cbGrpprl);
    }

    // Must arrive at Pcdt (clxt == 0x02)
    if (pos + 5 > clxLen || clxData[pos] != 0x02u) return false;
    const uint32_t lcbPcd = RdU32LE(clxData + pos + 1);
    pos += 5;
    if (lcbPcd < 16 || pos + lcbPcd > clxLen) return false;
    if (((lcbPcd - 4u) % 12u) != 0) return false;

    const uint32_t nPieces = (lcbPcd - 4u) / 12u;
    if (nPieces == 0 || nPieces > 262144u) return false;

    const uint8_t* aCpBase = clxData + pos;
    const uint8_t* pcdBase = aCpBase + (static_cast<size_t>(nPieces) + 1u) * 4u;

    std::vector<uint32_t> aCP(static_cast<size_t>(nPieces) + 1u);
    for (uint32_t i = 0; i <= nPieces; ++i) {
        aCP[i] = RdU32LE(aCpBase + static_cast<size_t>(i) * 4u);
        if (i > 0 && aCP[i] < aCP[i - 1]) return false;
    }

    constexpr size_t kMaxExtractedChars = 4u * 1024u * 1024u;
    const size_t reserveCap = std::min<size_t>(aCP[nPieces] - aCP[0], kMaxExtractedChars);
    outChars.reserve(std::min<size_t>(reserveCap, 262144u));

    for (uint32_t k = 0; k < nPieces && outChars.size() < kMaxExtractedChars; ++k) {
        uint32_t cpCount = aCP[k + 1] - aCP[k];
        if (cpCount == 0) continue;
        if (outChars.size() + cpCount > kMaxExtractedChars) {
            cpCount = static_cast<uint32_t>(kMaxExtractedChars - outChars.size());
        }

        const uint8_t* pcd = pcdBase + static_cast<size_t>(k) * 8u;
        const uint32_t fcRaw = RdU32LE(pcd + 2);

        if (!isWord97UnicodePieces) {
            // Word 6.0 / 95: fcRaw is a direct byte offset of 8-bit CP1252 text
            if (static_cast<uint64_t>(fcRaw) + cpCount > wordStream.size()) return false;
            for (uint32_t i = 0; i < cpCount; ++i) {
                outChars.push_back(Cp1252ToWide(wordStream[fcRaw + i]));
            }
        } else if ((fcRaw & 0x40000000u) != 0) {
            // Word 97–2003 compressed 8-bit CP1252 piece
            const uint32_t byteOff = (fcRaw & ~0x40000000u) / 2u;
            if (static_cast<uint64_t>(byteOff) + cpCount > wordStream.size()) return false;
            for (uint32_t i = 0; i < cpCount; ++i) {
                outChars.push_back(Cp1252ToWide(wordStream[byteOff + i]));
            }
        } else {
            // Word 97–2003 uncompressed 16-bit UTF-16LE piece
            const uint32_t byteOff = fcRaw & 0x3FFFFFFFu;
            if (static_cast<uint64_t>(byteOff) + static_cast<uint64_t>(cpCount) * 2ULL > wordStream.size()) return false;
            const uint8_t* src = wordStream.data() + byteOff;
            for (uint32_t i = 0; i < cpCount; ++i) {
                outChars.push_back(static_cast<wchar_t>(RdU16LE(src + static_cast<size_t>(i) * 2u)));
            }
        }
    }

    return true;
}

inline void FormatWordCharStream(std::wstring_view chars, ParsedOfficeDocument& out) {
    // Field code state stack: true = inside hidden instruction (0x13..0x14), false = inside visible result (0x14..0x15)
    std::vector<bool> fieldStack;
    std::wstring curPara;
    std::vector<std::wstring> curRowCells;
    SpreadsheetSheet tableSheet;
    tableSheet.name = L"Word_Tables";

    auto flushTableRow = [&]() {
        if (curRowCells.empty()) return;
        bool anyNonEmpty = false;
        std::wstring joined = L"| ";
        for (size_t c = 0; c < curRowCells.size(); ++c) {
            if (!curRowCells[c].empty()) anyNonEmpty = true;
            if (c > 0) joined += L" | ";
            joined += curRowCells[c];
        }
        joined += L" |";
        if (anyNonEmpty) {
            PushOfficeParagraph(out, std::move(joined), 0, false, true);
            if (curRowCells.size() > tableSheet.maxCols) tableSheet.maxCols = curRowCells.size();
            tableSheet.rows.push_back(curRowCells);
        }
        curRowCells.clear();
    };

    for (size_t i = 0; i < chars.size(); ++i) {
        const wchar_t ch = chars[i];

        if (ch == 0x0013) { // Field begin
            fieldStack.push_back(true);
            continue;
        }
        if (ch == 0x0014) { // Field separator (switch to visible result)
            if (!fieldStack.empty()) fieldStack.back() = false;
            continue;
        }
        if (ch == 0x0015) { // Field end
            if (!fieldStack.empty()) fieldStack.pop_back();
            continue;
        }

        bool hiddenInField = false;
        for (bool inInst : fieldStack) {
            if (inInst) { hiddenInField = true; break; }
        }
        if (hiddenInField) continue;

        // Skip inline object/picture anchors & control marks
        if (ch == 0x0000 || ch == 0x0001 || ch == 0x0002 || ch == 0x0005 || ch == 0x0008 || ch == 0x001F) {
            continue;
        }

        if (ch == 0x0007) { // Table cell / row end mark
            if (i + 1 < chars.size() && chars[i + 1] == 0x0007) {
                curRowCells.push_back(TrimWideParagraph(curPara));
                curPara.clear();
                ++i; // consume trailing row-end 0x07
                flushTableRow();
            } else if (!curRowCells.empty() && TrimWideParagraph(curPara).empty() &&
                       (i > 0 && (chars[i - 1] == 0x0007 || chars[i - 1] == 0x000D))) {
                curPara.clear();
                flushTableRow();
            } else {
                curRowCells.push_back(TrimWideParagraph(curPara));
                curPara.clear();
            }
            continue;
        }

        if (ch == 0x000D || ch == 0x000B || ch == 0x000C) {
            if (ch == 0x000D && i + 1 < chars.size() && chars[i + 1] == 0x0007 &&
                !curRowCells.empty() && TrimWideParagraph(curPara).empty())
            {
                ++i; // consume \r\x07 row-end sequence
                flushTableRow();
                continue;
            }
            // Check if we are mid-cell (upcoming 0x07 before another paragraph outside table)
            if (!curRowCells.empty()) {
                size_t look = i + 1;
                bool cellFollows = false;
                while (look < chars.size() && look < i + 256) {
                    if (chars[look] == 0x0007) { cellFollows = true; break; }
                    if (chars[look] == 0x000C) break;
                    ++look;
                }
                if (cellFollows) {
                    if (!curPara.empty() && curPara.back() != L' ') curPara.push_back(L' ');
                    continue;
                }
                flushTableRow();
            }
            PushOfficeParagraph(out, curPara, 0, false, false);
            curPara.clear();
            continue;
        }

        if (ch == 0x000A) continue; // Ignore bare LF paired with CR
        if (ch == 0x0009) { curPara += L"    "; continue; }
        if (ch == 0x001E) { curPara.push_back(L'-'); continue; }
        if (ch == 0x00A0) { curPara.push_back(L' '); continue; }
        if (ch < 0x0020) continue;

        curPara.push_back(ch);
    }

    if (!curRowCells.empty()) {
        if (!TrimWideParagraph(curPara).empty()) {
            curRowCells.push_back(TrimWideParagraph(curPara));
            curPara.clear();
        }
        flushTableRow();
    }
    if (!curPara.empty()) {
        PushOfficeParagraph(out, curPara, 0, false, false);
    }
    if (!tableSheet.rows.empty()) {
        out.sheets.push_back(std::move(tableSheet));
    }
}

inline bool ParseLegacyWordDoc(const Ole2::CompoundFile& cf, const std::wstring& extLower, ParsedOfficeDocument& out) {
    std::vector<uint8_t> wd;
    if (!ReadStreamByName(cf, L"WordDocument", wd) || wd.size() < 68) return false;

    const uint16_t wIdent = RdU16LE(wd.data());
    const uint16_t nFib   = RdU16LE(wd.data() + 2);
    const uint16_t flags  = RdU16LE(wd.data() + 10);

    // Reject encrypted documents (fEncrypted == bit 8)
    if ((flags & 0x0100u) != 0) return false;

    std::wstring decodedText;

    if (wIdent == 0xA5ECu && nFib >= 0x0065u) {
        // Word 97–2003 ([MS-DOC]): walk FibBase (32B) -> FibRgW97 -> FibRgLw97 -> FibRgFcLcb
        const wchar_t* tblStreamName = ((flags & 0x0200u) != 0) ? L"1Table" : L"0Table";
        std::vector<uint8_t> tbl;
        if (!ReadStreamByName(cf, tblStreamName, tbl)) {
            // Fallback try alternate table stream if flag was mis-set by third-party writer
            if (!ReadStreamByName(cf, ((flags & 0x0200u) != 0) ? L"0Table" : L"1Table", tbl)) return false;
        }

        const uint16_t csw = RdU16LE(wd.data() + 32);
        const size_t offCslw = 34u + static_cast<size_t>(csw) * 2u;
        if (offCslw + 2u > wd.size()) return false;

        const uint16_t cslw = RdU16LE(wd.data() + offCslw);
        const size_t offRgLw = offCslw + 2u;
        if (cslw < 4u || offRgLw + static_cast<size_t>(cslw) * 4u + 2u > wd.size()) return false;

        const int32_t ccpText = RdI32LE(wd.data() + offRgLw + 12u); // offset 76 when csw == 14
        const int32_t ccpFtn  = (cslw > 4u) ? RdI32LE(wd.data() + offRgLw + 16u) : 0;
        const int32_t ccpHdd  = (cslw > 5u) ? RdI32LE(wd.data() + offRgLw + 20u) : 0;
        if (ccpText < 0) return false;

        const size_t offCbRgFcLcb = offRgLw + static_cast<size_t>(cslw) * 4u;
        const uint16_t cbRgFcLcb  = RdU16LE(wd.data() + offCbRgFcLcb);
        const size_t offRgFcLcb   = offCbRgFcLcb + 2u;
        if (cbRgFcLcb <= 33u || offRgFcLcb + 34u * 8u > wd.size()) return false;

        const uint32_t fcClx  = RdU32LE(wd.data() + offRgFcLcb + 33u * 8u);
        const uint32_t lcbClx = RdU32LE(wd.data() + offRgFcLcb + 33u * 8u + 4u);
        if (lcbClx == 0 || static_cast<uint64_t>(fcClx) + lcbClx > tbl.size()) return false;

        std::wstring allChars;
        if (!DecodeWordClxPieces(wd, tbl.data() + fcClx, lcbClx, nFib >= 0x00C0u, allChars)) return false;

        if (ccpText > 0 && static_cast<size_t>(ccpText) <= allChars.size()) {
            decodedText.assign(allChars.data(), static_cast<size_t>(ccpText));
        } else {
            decodedText = std::move(allChars);
        }
        (void)ccpFtn;
        (void)ccpHdd;
    } else if (wIdent == 0xA5DCu || wIdent == 0xA5ECu) {
        // Word 6.0 / Word 95 ([MS-DOC] legacy FIB)
        const bool fComplex = ((flags & 0x0004u) != 0);
        if (fComplex && wd.size() >= 0x0168u) {
            const uint32_t fcClx  = RdU32LE(wd.data() + 0x0160u);
            const uint32_t lcbClx = RdU32LE(wd.data() + 0x0164u);
            if (lcbClx >= 5u && static_cast<uint64_t>(fcClx) + lcbClx <= wd.size()) {
                if (!DecodeWordClxPieces(wd, wd.data() + fcClx, lcbClx, false, decodedText)) return false;
            }
        }
        if (decodedText.empty()) {
            const uint32_t fcMin = RdU32LE(wd.data() + 24u);
            const uint32_t fcMac = RdU32LE(wd.data() + 28u);
            if (fcMin >= wd.size() || fcMac <= fcMin || fcMac > wd.size()) return false;
            const size_t span = std::min<size_t>(fcMac - fcMin, 4u * 1024u * 1024u);
            decodedText.reserve(span);
            for (size_t i = 0; i < span; ++i) {
                decodedText.push_back(Cp1252ToWide(wd[fcMin + i]));
            }
        }
    } else {
        return false;
    }

    out.kind = OfficeFileKind::WordDocx;
    out.isSpreadsheet = false;
    out.formatTitle = (extLower == L".dot")
        ? L"Microsoft Word 97-2003 Template (.dot)"
        : ((wIdent == 0xA5DCu) ? L"Microsoft Word 6.0/95 Document (.doc)" : L"Microsoft Word 97-2003 Document (.doc)");

    FormatWordCharStream(decodedText, out);
    if (out.paragraphs.empty()) {
        // Valid empty Word document still parses cleanly
        PushOfficeParagraph(out, L"(Empty Word Document)", 0, false, false);
    }
    out.valid = true;
    return true;
}

// ============================================================================
// Microsoft PowerPoint 97–2003 ([MS-PPT] & [MS-ODRAW]: .ppt, .pps)
// ============================================================================
struct PptSlideBucket {
    uint32_t persistRef = 0;
    std::vector<std::wstring> blocks;
    std::set<std::wstring> seenNormalized;

    void AddBlock(std::wstring text) {
        // Normalize line breaks to \n for deduplication across SlideListWithText and Slide shapes
        std::wstring norm;
        norm.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            wchar_t c = text[i];
            if (c == L'\r') {
                if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                norm.push_back(L'\n');
            } else if (c == 0x000B) {
                norm.push_back(L'\n');
            } else if (c >= 0x0020 || c == L'\n' || c == L'\t') {
                norm.push_back(c);
            }
        }
        std::wstring trimmed = TrimWideParagraph(norm);
        if (trimmed.empty()) return;
        if (seenNormalized.insert(trimmed).second) {
            blocks.push_back(std::move(norm));
        }
    }
};

inline std::wstring DecodePptUtf16Atom(const uint8_t* p, size_t byteLen) {
    const size_t nChars = std::min<size_t>(byteLen / 2u, 262144u);
    std::wstring s;
    s.reserve(nChars);
    for (size_t i = 0; i < nChars; ++i) {
        const wchar_t ch = static_cast<wchar_t>(RdU16LE(p + i * 2u));
        if (ch == 0) break;
        s.push_back(ch);
    }
    return s;
}

inline std::wstring DecodePptBytesAtom(const uint8_t* p, size_t byteLen) {
    const size_t nChars = std::min<size_t>(byteLen, 262144u);
    std::wstring s;
    s.reserve(nChars);
    for (size_t i = 0; i < nChars; ++i) {
        if (p[i] == 0) break;
        s.push_back(Cp1252ToWide(p[i]));
    }
    return s;
}

inline void WalkPptShapeRecords(const uint8_t* buf, size_t len, PptSlideBucket& slide, int depth = 0) {
    if (depth > 24 || len < 8) return;
    size_t pos = 0;
    while (pos + 8 <= len) {
        const uint16_t verInst = RdU16LE(buf + pos);
        const uint8_t recVer   = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recType = RdU16LE(buf + pos + 2);
        const uint32_t recLen  = RdU32LE(buf + pos + 4);
        if (recLen > len - (pos + 8)) break;

        const uint8_t* payload = buf + pos + 8;
        if (recType == 0x0FA0u) { // RT_TextCharsAtom (UTF-16LE)
            slide.AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recType == 0x0FA8u) { // RT_TextBytesAtom (CP1252)
            slide.AddBlock(DecodePptBytesAtom(payload, recLen));
        } else if (recType == 0x0FBAu) { // RT_CString (UTF-16LE)
            slide.AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recVer == 0x0Fu || recType == 0xF00Du) {
            // Recurse into containers AND Escher msofbtClientTextbox (0xF00D, which may use recVer 0x00 or 0x0F)
            WalkPptShapeRecords(payload, recLen, slide, depth + 1);
        }

        pos += 8u + static_cast<size_t>(recLen);
    }
}

inline void ParsePptSlideListWithText(const uint8_t* buf, size_t len, std::vector<PptSlideBucket>& slides) {
    size_t pos = 0;
    while (pos + 8 <= len) {
        const uint16_t verInst = RdU16LE(buf + pos);
        const uint8_t recVer   = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recType = RdU16LE(buf + pos + 2);
        const uint32_t recLen  = RdU32LE(buf + pos + 4);
        if (recLen > len - (pos + 8)) break;

        const uint8_t* payload = buf + pos + 8;
        if (recType == 0x03F3u && recLen >= 4u) { // RT_SlidePersistAtom starts a new slide entry
            PptSlideBucket sb;
            sb.persistRef = RdU32LE(payload);
            slides.push_back(std::move(sb));
        } else if (recType == 0x0FA0u) { // RT_TextCharsAtom
            if (slides.empty()) slides.emplace_back();
            slides.back().AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recType == 0x0FA8u) { // RT_TextBytesAtom
            if (slides.empty()) slides.emplace_back();
            slides.back().AddBlock(DecodePptBytesAtom(payload, recLen));
        } else if (recType == 0x0FBAu) { // RT_CString
            if (slides.empty()) slides.emplace_back();
            slides.back().AddBlock(DecodePptUtf16Atom(payload, recLen));
        } else if (recVer == 0x0Fu && !slides.empty()) {
            WalkPptShapeRecords(payload, recLen, slides.back(), 1);
        }

        pos += 8u + static_cast<size_t>(recLen);
    }
}

inline void ParsePptDocumentContainer(const uint8_t* buf, size_t len, std::vector<PptSlideBucket>& slides, int depth = 0) {
    if (depth > 8 || len < 8) return;
    size_t pos = 0;
    while (pos + 8 <= len) {
        const uint16_t verInst  = RdU16LE(buf + pos);
        const uint8_t recVer    = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recInst  = static_cast<uint16_t>(verInst >> 4);
        const uint16_t recType  = RdU16LE(buf + pos + 2);
        const uint32_t recLen   = RdU32LE(buf + pos + 4);
        if (recLen > len - (pos + 8)) break;

        const uint8_t* payload = buf + pos + 8;
        if (recType == 0x0FF0u && recVer == 0x0Fu) {
            // RT_SlideListWithText: recInstance == 0 -> presentation slides (1 = masters, 2 = notes)
            if (recInst == 0) {
                ParsePptSlideListWithText(payload, recLen, slides);
            }
        } else if (recVer == 0x0Fu && recType != 0x040Cu && recType != 0x03F8u) {
            // Recurse into Document sub-containers (skipping MainMaster 0x03F8)
            ParsePptDocumentContainer(payload, recLen, slides, depth + 1);
        }

        pos += 8u + static_cast<size_t>(recLen);
    }
}

inline bool ParseLegacyPowerPointPpt(const Ole2::CompoundFile& cf, const std::wstring& extLower, ParsedOfficeDocument& out) {
    // Reject encrypted PPT via Current User stream magic (0xF3D1C4DF)
    std::vector<uint8_t> curUser;
    if (ReadStreamByName(cf, L"Current User", curUser) && curUser.size() >= 16) {
        const uint32_t magic = RdU32LE(curUser.data() + 12);
        if (magic == 0xF3D1C4DFu) return false;
    }

    std::vector<uint8_t> ppt;
    if (!ReadStreamByName(cf, L"PowerPoint Document", ppt) || ppt.size() < 8) return false;

    std::vector<PptSlideBucket> slidesFromList;
    std::vector<PptSlideBucket> slideContainers;
    bool hasValidPptRoot = false;

    size_t pos = 0;
    while (pos + 8 <= ppt.size()) {
        const uint16_t verInst = RdU16LE(ppt.data() + pos);
        const uint8_t recVer   = static_cast<uint8_t>(verInst & 0x0Fu);
        const uint16_t recType = RdU16LE(ppt.data() + pos + 2);
        const uint32_t recLen  = RdU32LE(ppt.data() + pos + 4);
        if (recLen > ppt.size() - (pos + 8)) break;

        const uint8_t* payload = ppt.data() + pos + 8;
        if (recType == 0x2F14u) { // RT_CryptSession10Container (encrypted)
            return false;
        }
        if (recType == 0x03E8u && recVer == 0x0Fu) { // RT_Document
            hasValidPptRoot = true;
            ParsePptDocumentContainer(payload, recLen, slidesFromList, 0);
        } else if (recType == 0x03EEu && recVer == 0x0Fu) { // RT_Slide
            hasValidPptRoot = true;
            PptSlideBucket sb;
            WalkPptShapeRecords(payload, recLen, sb, 0);
            slideContainers.push_back(std::move(sb));
        }

        pos += 8u + static_cast<size_t>(recLen);
    }

    if (!hasValidPptRoot) return false;

    // Merge shape text from RT_Slide containers into SlideListWithText slides (deduplicating per slide)
    std::vector<PptSlideBucket> finalSlides;
    if (!slidesFromList.empty()) {
        finalSlides = std::move(slidesFromList);
        // Merge matching RT_Slide containers (aligned from latest/corresponding slide containers)
        const size_t offset = (slideContainers.size() > finalSlides.size())
            ? (slideContainers.size() - finalSlides.size())
            : 0;
        for (size_t i = 0; i < finalSlides.size() && (offset + i) < slideContainers.size(); ++i) {
            for (const auto& blk : slideContainers[offset + i].blocks) {
                finalSlides[i].AddBlock(blk);
            }
        }
    } else {
        finalSlides = std::move(slideContainers);
    }

    out.kind = OfficeFileKind::PowerPointPptx;
    out.isSpreadsheet = false;
    out.formatTitle = (extLower == L".pps")
        ? L"Microsoft PowerPoint 97-2003 Slide Show (.pps)"
        : L"Microsoft PowerPoint 97-2003 Presentation (.ppt)";

    for (size_t sIdx = 0; sIdx < finalSlides.size(); ++sIdx) {
        PushOfficeParagraph(out, L"--- Slide " + std::to_wstring(sIdx + 1) + L" ---", 1, false, false);
        bool firstLineOnSlide = true;
        for (const auto& blk : finalSlides[sIdx].blocks) {
            size_t start = 0;
            while (start <= blk.size()) {
                size_t nl = blk.find(L'\n', start);
                std::wstring_view line = (nl == std::wstring::npos)
                    ? std::wstring_view(blk).substr(start)
                    : std::wstring_view(blk).substr(start, nl - start);
                std::wstring cleanLine = TrimWideParagraph(line);
                if (!cleanLine.empty()) {
                    PushOfficeParagraph(out, std::move(cleanLine), firstLineOnSlide ? 2 : 0, !firstLineOnSlide, false);
                    firstLineOnSlide = false;
                }
                if (nl == std::wstring::npos) break;
                start = nl + 1;
            }
        }
    }

    if (out.paragraphs.empty()) {
        PushOfficeParagraph(out, L"(Empty PowerPoint Presentation)", 0, false, false);
    }
    out.valid = true;
    return true;
}

} // namespace LegacyOfficeDetail

inline bool ParseLegacyOfficeOleDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) return false;

    if (extLower == L".doc" || extLower == L".dot") {
        return LegacyOfficeDetail::ParseLegacyWordDoc(cf, extLower, out);
    }
    if (extLower == L".ppt" || extLower == L".pps") {
        return LegacyOfficeDetail::ParseLegacyPowerPointPpt(cf, extLower, out);
    }
    return false;
}

} // namespace Formats
