#pragma once
// Bdf.h — Adobe Glyph Bitmap Distribution Font (.bdf) Arbitrary-Dimension Full Atlas Renderer

namespace Formats {

inline bool DecodeBDF(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 32) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), std::min<size_t>(size, 8 * 1024 * 1024));
    if (sv.find("STARTFONT") == std::string_view::npos || sv.find("BITMAP") == std::string_view::npos) return false;

    auto hexNibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    auto parseTwoInts = [](std::string_view text, int& a, int& b) -> bool {
        std::string tmp(text);
        const char* p = tmp.c_str();
        char* end1 = nullptr;
        long v1 = std::strtol(p, &end1, 10);
        if (end1 == p) return false;
        char* end2 = nullptr;
        long v2 = std::strtol(end1, &end2, 10);
        if (end2 == end1) return false;
        a = static_cast<int>(v1);
        b = static_cast<int>(v2);
        return true;
    };

    uint32_t fontW = 8, fontH = 16;
    if (size_t fbb = sv.find("FONTBOUNDINGBOX"); fbb != std::string_view::npos) {
        size_t eol = sv.find('\n', fbb);
        std::string_view line = sv.substr(fbb + 15, (eol == std::string_view::npos ? sv.size() : eol) - (fbb + 15));
        int w = 0, h = 0;
        if (parseTwoInts(line, w, h) && w > 0 && w <= 128 && h > 0 && h <= 128) {
            fontW = static_cast<uint32_t>(w);
            fontH = static_cast<uint32_t>(h);
        }
    }

    struct BdfGlyph {
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t bytesPerRow = 0;
        std::vector<uint8_t> rows;
    };
    std::vector<BdfGlyph> glyphs;
    glyphs.reserve(256);

    uint32_t maxGlyphW = fontW;
    uint32_t maxGlyphH = fontH;

    size_t pos = 0;
    while ((pos = sv.find("STARTCHAR", pos)) != std::string_view::npos && glyphs.size() < 512u) {
        size_t endChar = sv.find("ENDCHAR", pos);
        if (endChar == std::string_view::npos) break;
        std::string_view block = sv.substr(pos, endChar - pos);

        uint32_t gw = fontW, gh = fontH;
        if (size_t bbx = block.find("BBX"); bbx != std::string_view::npos) {
            size_t eol = block.find('\n', bbx);
            std::string_view line = block.substr(bbx + 3, (eol == std::string_view::npos ? block.size() : eol) - (bbx + 3));
            int bw = 0, bh = 0;
            if (parseTwoInts(line, bw, bh) && bw >= 0 && bw <= 256 && bh >= 0 && bh <= 256) {
                gw = static_cast<uint32_t>(bw);
                gh = static_cast<uint32_t>(bh);
            }
        }

        size_t bmpTag = block.find("BITMAP");
        if (bmpTag != std::string_view::npos) {
            size_t eol = block.find('\n', bmpTag);
            if (eol != std::string_view::npos) {
                BdfGlyph g;
                g.w = gw;
                g.h = gh;
                g.bytesPerRow = std::max<uint32_t>(1u, (gw + 7u) / 8u);
                size_t lPos = eol + 1u;
                uint32_t parsedRows = 0;
                while (lPos < block.size() && parsedRows < 256u) {
                    size_t lEnd = block.find('\n', lPos);
                    if (lEnd == std::string_view::npos) lEnd = block.size();
                    std::string_view hexLine = block.substr(lPos, lEnd - lPos);
                    while (!hexLine.empty() && (hexLine.front() == ' ' || hexLine.front() == '\t')) hexLine.remove_prefix(1);
                    while (!hexLine.empty() && (hexLine.back() == '\r' || hexLine.back() == ' ' || hexLine.back() == '\t')) hexLine.remove_suffix(1);
                    if (!hexLine.empty()) {
                        const uint32_t lineBytes = static_cast<uint32_t>((hexLine.size() + 1u) / 2u);
                        g.bytesPerRow = std::max(g.bytesPerRow, lineBytes);
                        if (g.w == 0) g.w = lineBytes * 8u;
                        for (uint32_t bi = 0; bi < g.bytesPerRow; ++bi) {
                            int hi = (bi * 2u < hexLine.size()) ? hexNibble(hexLine[bi * 2u]) : 0;
                            int lo = (bi * 2u + 1u < hexLine.size()) ? hexNibble(hexLine[bi * 2u + 1u]) : 0;
                            if (hi < 0) hi = 0;
                            if (lo < 0) lo = 0;
                            g.rows.push_back(static_cast<uint8_t>((hi << 4) | lo));
                        }
                        ++parsedRows;
                    }
                    lPos = lEnd + 1u;
                }
                g.h = parsedRows;
                maxGlyphW = std::min<uint32_t>(128u, std::max(maxGlyphW, g.w));
                maxGlyphH = std::min<uint32_t>(128u, std::max(maxGlyphH, g.h));
                glyphs.push_back(std::move(g));
            }
        }
        pos = endChar + 7u;
    }

    if (glyphs.empty()) return false;

    const uint32_t cols = 16u;
    const uint32_t rows = std::max<uint32_t>(1u, (static_cast<uint32_t>(glyphs.size()) + cols - 1u) / cols);
    const uint32_t cellW = std::max<uint32_t>(8u, maxGlyphW + 4u);
    const uint32_t cellH = std::max<uint32_t>(8u, maxGlyphH + 4u);
    const uint32_t imgW = cols * cellW;
    const uint32_t imgH = rows * cellH;

    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = imgW;
    outInfo.origHeight = imgH;
    outInfo.channels = 4u;
    outInfo.bitsPerChannel = 8u;
    outInfo.formatName = L"Adobe BDF Bitmap Font (.bdf)";
    outInfo.bgraPixels.assign(static_cast<size_t>(imgW) * imgH * 4u, 248u);
    for (size_t i = 0; i < static_cast<size_t>(imgW) * imgH; ++i) outInfo.bgraPixels[i * 4u + 3u] = 255u;

    for (size_t gi = 0; gi < glyphs.size(); ++gi) {
        const BdfGlyph& g = glyphs[gi];
        if (g.w == 0 || g.h == 0 || g.bytesPerRow == 0) continue;
        const uint32_t drawW = std::min(g.w, maxGlyphW);
        const uint32_t drawH = std::min(g.h, maxGlyphH);
        const uint32_t gx = static_cast<uint32_t>(gi % cols) * cellW + (cellW - drawW) / 2u;
        const uint32_t gy = static_cast<uint32_t>(gi / cols) * cellH + (cellH - drawH) / 2u;
        for (uint32_t r = 0; r < drawH; ++r) {
            const uint8_t* rowPtr = g.rows.data() + static_cast<size_t>(r) * g.bytesPerRow;
            for (uint32_t x = 0; x < drawW; ++x) {
                if ((rowPtr[x >> 3] & (0x80u >> (x & 7u))) != 0u) {
                    uint8_t* dst = &outInfo.bgraPixels[(static_cast<size_t>(gy + r) * imgW + (gx + x)) * 4u];
                    dst[0] = 45u; dst[1] = 25u; dst[2] = 15u; dst[3] = 255u;
                }
            }
        }
    }

    outInfo.valid = true;
    return true;
}

} // namespace Formats
