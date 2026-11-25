#pragma once
// AngelCodeBmfontDoc.h — AngelCode BMFont v3 Binary Font Metrics & Kerning Inspector (.bmf — "BMF\x03")
// Parses typed blocks: 1 (`info`), 2 (`common`), 3 (`pages`), 4 (`chars` — 20B glyph metrics),
// and 5 (`kerningPairs` — 10B kerning records) with exact EOF boundary checking.

namespace Formats {

inline bool ParseAngelCodeBmfontDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& outDoc) {
    const size_t size = raw.size();
    const uint8_t* data = raw.data();
    if (!data || size < 32u || size > (16u << 20)) return false;
    if (data[0] != 'B' || data[1] != 'M' || data[2] != 'F' || data[3] != 3u) return false;

    std::string fontFace;
    int16_t fontSize = 0;
    uint16_t lineHeight = 0u, scaleW = 0u, scaleH = 0u;
    bool sawInfo = false, sawCommon = false, sawChars = false;

    SpreadsheetSheet glyphSheet;
    glyphSheet.name = L"Glyphs";
    glyphSheet.rows.push_back({ L"Codepoint", L"Char", L"Atlas_XY", L"Size_WH", L"Offset_XY", L"XAdvance", L"Page" });

    SpreadsheetSheet kernSheet;
    kernSheet.name = L"Kerning";
    kernSheet.rows.push_back({ L"First_CP", L"Second_CP", L"Amount_Px" });

    size_t pos = 4u;
    while (pos < size) {
        if (size - pos < 5u) return false;
        const uint8_t  blkId = data[pos];
        const uint32_t blkSz = ByteIO::RdU32LE(data + pos + 1u);
        pos += 5u;
        if (blkId < 1u || blkId > 5u || blkSz > size - pos) return false;
        const uint8_t* bp = data + pos;

        if (blkId == 1u) { // info (14B + NUL-terminated fontName)
            if (blkSz < 15u || bp[blkSz - 1u] != 0u) return false;
            fontSize = ByteIO::RdI16LE(bp + 0u);
            fontFace.assign(reinterpret_cast<const char*>(bp + 14u), strnlen(reinterpret_cast<const char*>(bp + 14u), blkSz - 14u));
            sawInfo = true;
        } else if (blkId == 2u) { // common (15B)
            if (blkSz < 15u) return false;
            lineHeight = ByteIO::RdU16LE(bp + 0u);
            scaleW     = ByteIO::RdU16LE(bp + 4u);
            scaleH     = ByteIO::RdU16LE(bp + 6u);
            if (scaleW == 0u || scaleH == 0u) return false;
            sawCommon = true;
        } else if (blkId == 3u) { // pages (NUL-terminated page strings)
            if (blkSz == 0u || bp[blkSz - 1u] != 0u) return false;
        } else if (blkId == 4u) { // chars (n * 20B)
            if (blkSz == 0u || (blkSz % 20u) != 0u) return false;
            sawChars = true;
            const size_t nChars = blkSz / 20u;
            for (size_t i = 0u; i < nChars && glyphSheet.rows.size() < 16385u; ++i) {
                const uint8_t* cp = bp + i * 20u;
                const uint32_t id   = ByteIO::RdU32LE(cp + 0u);
                const uint16_t gx   = ByteIO::RdU16LE(cp + 4u);
                const uint16_t gy   = ByteIO::RdU16LE(cp + 6u);
                const uint16_t gw   = ByteIO::RdU16LE(cp + 8u);
                const uint16_t gh   = ByteIO::RdU16LE(cp + 10u);
                const int16_t  xoff = ByteIO::RdI16LE(cp + 12u);
                const int16_t  yoff = ByteIO::RdI16LE(cp + 14u);
                const int16_t  xadv = ByteIO::RdI16LE(cp + 16u);
                const uint8_t  page = cp[18u];

                wchar_t uHex[16] = {};
                swprintf_s(uHex, L"U+%04X", id);
                std::wstring chPreview;
                if (id >= 0x20u && id != 0x7Fu && id <= 0x10FFFFu) {
                    PathUtils::AppendUtf16(chPreview, id);
                }
                glyphSheet.rows.push_back({
                    uHex,
                    std::move(chPreview),
                    std::to_wstring(gx) + L"," + std::to_wstring(gy),
                    std::to_wstring(gw) + L"x" + std::to_wstring(gh),
                    std::to_wstring(xoff) + L"," + std::to_wstring(yoff),
                    std::to_wstring(xadv),
                    std::to_wstring(page)
                });
            }
        } else if (blkId == 5u) { // kerningPairs (n * 10B)
            if ((blkSz % 10u) != 0u) return false;
            const size_t nPairs = blkSz / 10u;
            for (size_t i = 0u; i < nPairs && kernSheet.rows.size() < 8193u; ++i) {
                const uint8_t* kp = bp + i * 10u;
                const uint32_t first  = ByteIO::RdU32LE(kp + 0u);
                const uint32_t second = ByteIO::RdU32LE(kp + 4u);
                const int16_t  amt    = ByteIO::RdI16LE(kp + 8u);
                wchar_t fHex[16]{}, sHex[16]{};
                swprintf_s(fHex, L"U+%04X", first);
                swprintf_s(sHex, L"U+%04X", second);
                kernSheet.rows.push_back({ fHex, sHex, std::to_wstring(amt) });
            }
        }
        pos += blkSz;
    }

    if (!sawInfo || !sawCommon || !sawChars || glyphSheet.rows.size() <= 1u) return false;

    outDoc.sheets.clear();
    outDoc.paragraphs.clear();
    FinalizeSheetCols(glyphSheet);
    outDoc.sheets.push_back(std::move(glyphSheet));
    if (kernSheet.rows.size() > 1u) {
        FinalizeSheetCols(kernSheet);
        outDoc.sheets.push_back(std::move(kernSheet));
    }

    wchar_t title[200] = {};
    swprintf_s(title, L"AngelCode BMFont v3 Binary (.bmf %ls %dpx, %ux%u, lh=%u)",
               PathUtils::Utf8ToWide(fontFace.empty() ? "Font" : fontFace).c_str(),
               std::abs(static_cast<int>(fontSize)), scaleW, scaleH, lineHeight);
    outDoc.formatTitle = title;
    outDoc.kind = OfficeFileKind::CsvTable;
    outDoc.isSpreadsheet = true;
    outDoc.valid = true;
    return true;
}

} // namespace Formats
