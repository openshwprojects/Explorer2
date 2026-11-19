#pragma once
// WordLegacyDoc.h — Microsoft Word 97-2003 & 6.0/95 ([MS-DOC]: .doc, .dot) OLE2 Binary Reader

namespace Formats {
namespace LegacyOfficeDetail {

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

} // namespace LegacyOfficeDetail

inline bool ParseWordLegacyDoc(const std::vector<uint8_t>& raw, const std::wstring& extLower, ParsedOfficeDocument& out) {
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(raw.data(), raw.size(), cf)) return false;
    return LegacyOfficeDetail::ParseLegacyWordDoc(cf, extLower, out);
}

} // namespace Formats
