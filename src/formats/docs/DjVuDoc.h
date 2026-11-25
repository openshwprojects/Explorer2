#pragma once
// DjVuDoc.h — AT&T DjVu Document (.djvu / .djv) Reader

namespace Formats {
namespace DetailMediaAndScoreDoc {

// ============================================================================
// 11. AT&T DjVu Document (.djvu / .djv — EA IFF 85 AT&TFORM)
// ============================================================================
inline bool ParseDjVuDocument(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "AT&TFORM", 8) != 0) return false;
    std::string rootType(reinterpret_cast<const char*>(raw.data() + 12), 4);
    if (rootType != "DJVU" && rootType != "DJVM" && rootType != "DJVI" && rootType != "THUM") return false;

    uint32_t formSize = DetailBinarySysDoc::RdU32BE(raw.data() + 8);
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
            uint32_t csz = DetailBinarySysDoc::RdU32BE(raw.data() + pos + 4);
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
                    dirmFiles = DetailBinarySysDoc::RdU16BE(p + 1);
                    desc = ((p[0] & 0x80u) ? L"Bundled" : L"Indirect") + std::wstring(L", files=") + std::to_wstring(dirmFiles);
                } else if (cid == "INFO" && avail >= 10) {
                    uint16_t w = DetailBinarySysDoc::RdU16BE(p);
                    uint16_t h = DetailBinarySysDoc::RdU16BE(p + 2);
                    uint8_t minV = p[4], majV = p[5];
                    uint16_t dpi = DetailBinarySysDoc::RdU16LE(p + 6);
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
    sum.rows.push_back({ L"File Size", PathUtils::FormatByteSize(std::max<uint64_t>(out.sourceFileSize, raw.size())) });
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

} // namespace DetailMediaAndScoreDoc

inline bool ParseDjVuDoc(const std::vector<uint8_t>& raw, ParsedOfficeDocument& out) { return DetailMediaAndScoreDoc::ParseDjVuDocument(raw, out); }

} // namespace Formats
