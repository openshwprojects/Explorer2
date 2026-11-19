#pragma once
// SixelGraphics.h — DEC VT340 Sixel Terminal Graphics Decoder (.sixel)

namespace Formats {

inline bool DecodeSixelGraphics(const uint8_t* data, size_t size, const std::wstring& extLower, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;

    const size_t scanLen = std::min<size_t>(size, 4 * 1024 * 1024);
    std::string_view sv(reinterpret_cast<const char*>(data), scanLen);

    // Check DEC VT340 Sixel Graphics (ESC P [P1;P2;P3] q  or  DCS 0x90 [P1;P2;P3] q)
    auto findSixelDataStart = [&](size_t& outQPos, int& outP2BgMode) -> bool {
        outP2BgMode = 0;
        size_t introEnd = std::string_view::npos;
        size_t escP = sv.find("\x1BP");
        size_t dcsC1 = sv.find('\x90');
        if (escP != std::string_view::npos && (dcsC1 == std::string_view::npos || escP < dcsC1)) {
            introEnd = escP + 2;
        } else if (dcsC1 != std::string_view::npos) {
            introEnd = dcsC1 + 1;
        } else if (extLower == L".sixel") {
            introEnd = 0;
        } else {
            return false;
        }

        int params[3] = { 0, 0, 0 };
        int pIdx = 0;
        for (size_t k = introEnd; k < sv.size() && k < introEnd + 64; ++k) {
            uint8_t ch = static_cast<uint8_t>(sv[k]);
            if (ch == 'q') {
                outQPos = k;
                outP2BgMode = params[1];
                return true;
            }
            if (ch >= '0' && ch <= '9') {
                if (pIdx < 3) params[pIdx] = params[pIdx] * 10 + (ch - '0');
            } else if (ch == ';') {
                ++pIdx;
            } else if (ch != ' ' && ch != '\r' && ch != '\n') {
                break;
            }
        }
        return false;
    };

    size_t qPos = 0;
    int sixelBgMode = 0;
    bool hasSixelStream = findSixelDataStart(qPos, sixelBgMode);
    if (extLower == L".sixel" && !hasSixelStream) {
        return false;
    }

    if (hasSixelStream) {
        // Initialize standard DEC VT340 16-color default registers (0..15); registers 16..1023 default to {0,0,0}
        std::vector<std::array<uint8_t, 3>> pal(1024, { 0, 0, 0 });
        static constexpr uint8_t kVt340Default16[16][3] = {
            {   0,   0,   0 }, //  0: Black
            {  51,  51, 204 }, //  1: Blue
            { 204,  33,  33 }, //  2: Red
            {  51, 204,  51 }, //  3: Green
            { 204,  51, 204 }, //  4: Magenta
            {  51, 204, 204 }, //  5: Cyan
            { 204, 204,  51 }, //  6: Yellow
            { 135, 135, 135 }, //  7: Gray 50%
            {  66,  66,  66 }, //  8: Gray 25%
            {  84,  84, 249 }, //  9: Light Blue
            { 249,  66,  66 }, // 10: Light Red
            {  84, 249,  84 }, // 11: Light Green
            { 249,  84, 249 }, // 12: Light Magenta
            {  84, 249, 249 }, // 13: Light Cyan
            { 249, 249,  84 }, // 14: Light Yellow
            { 255, 255, 255 }  // 15: White
        };
        for (int r = 0; r < 16; ++r) {
            pal[r] = { kVt340Default16[r][0], kVt340Default16[r][1], kVt340Default16[r][2] };
        }

        constexpr uint32_t kMaxSixelDim = 4096;
        uint32_t capW = 64, capH = 64;
        uint32_t explicitW = 0, explicitH = 0;
        const uint8_t initAlpha = (sixelBgMode == 1) ? 0 : 255;
        std::vector<uint8_t> canvas(static_cast<size_t>(capW) * capH * 4, 0);
        if (initAlpha == 255) {
            for (size_t px = 0; px < static_cast<size_t>(capW) * capH; ++px) canvas[px * 4 + 3] = 255;
        }

        auto ensureCanvas = [&](uint32_t needW, uint32_t needH) {
            needW = std::min(needW, kMaxSixelDim);
            needH = std::min(needH, kMaxSixelDim);
            if (needW <= capW && needH <= capH) return;
            uint32_t newW = capW, newH = capH;
            while (newW < needW) newW = std::min(kMaxSixelDim, newW * 2);
            while (newH < needH) newH = std::min(kMaxSixelDim, newH * 2);
            std::vector<uint8_t> next(static_cast<size_t>(newW) * newH * 4, 0);
            if (initAlpha == 255) {
                for (size_t px = 0; px < static_cast<size_t>(newW) * newH; ++px) {
                    next[px * 4 + 0] = pal[0][2];
                    next[px * 4 + 1] = pal[0][1];
                    next[px * 4 + 2] = pal[0][0];
                    next[px * 4 + 3] = 255;
                }
            }
            for (uint32_t y = 0; y < capH; ++y) {
                std::memcpy(&next[static_cast<size_t>(y) * newW * 4],
                            &canvas[static_cast<size_t>(y) * capW * 4],
                            static_cast<size_t>(capW) * 4);
            }
            canvas.swap(next);
            capW = newW;
            capH = newH;
        };

        uint32_t curX = 0, bandY = 0, usedW = 0, usedH = 0;
        size_t renderedSixelCols = 0;
        uint32_t curCol = 1;
        size_t i = qPos + 1;

        while (i < sv.size()) {
            uint8_t c = static_cast<uint8_t>(sv[i]);
            if (c == 0x00) return false; // Reject binary NUL garbage inside Sixel stream
            if (c == 0x1B || c == 0x9C) break; // ESC \ or C1 ST terminator
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') { ++i; continue; }
            if (c == '$') { curX = 0; ++i; continue; }
            if (c == '-') { curX = 0; bandY += 6; ++i; continue; }

            if (c == '"') {
                // Raster attributes: "Pan;Pad;Ph;Pv
                ++i;
                uint32_t ras[4] = { 0, 0, 0, 0 };
                for (int k = 0; k < 4; ++k) {
                    while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                        ras[k] = std::min<uint32_t>(65535u, ras[k] * 10u + static_cast<uint32_t>(sv[i++] - '0'));
                    }
                    if (k < 3 && i < sv.size() && sv[i] == ';') ++i;
                    else break;
                }
                if (ras[2] > 0) explicitW = std::min(ras[2], kMaxSixelDim);
                if (ras[3] > 0) explicitH = std::min(ras[3], kMaxSixelDim);
                if (explicitW > 0 || explicitH > 0) {
                    ensureCanvas(std::max(capW, explicitW), std::max(capH, explicitH));
                }
                continue;
            }

            if (c == '#') {
                // Color introducer: #Pc  or  #Pc;Pu;Px;Py;Pz
                ++i;
                uint32_t reg = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                    reg = std::min<uint32_t>(65535u, reg * 10u + static_cast<uint32_t>(sv[i++] - '0'));
                }
                curCol = (reg < 1024u) ? reg : (reg & 1023u);
                if (i < sv.size() && sv[i] == ';') {
                    int vals[4] = { 0, 0, 0, 0 };
                    int parsed = 0;
                    for (int k = 0; k < 4 && i < sv.size() && sv[i] == ';'; ++k) {
                        ++i;
                        while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                            vals[k] = std::min(10000, vals[k] * 10 + (sv[i++] - '0'));
                        }
                        ++parsed;
                    }
                    if (parsed >= 4) {
                        if (vals[0] == 2) {
                            // RGB percentages 0..100
                            pal[curCol][0] = static_cast<uint8_t>(std::clamp((vals[1] * 255 + 50) / 100, 0, 255));
                            pal[curCol][1] = static_cast<uint8_t>(std::clamp((vals[2] * 255 + 50) / 100, 0, 255));
                            pal[curCol][2] = static_cast<uint8_t>(std::clamp((vals[3] * 255 + 50) / 100, 0, 255));
                        } else if (vals[0] == 1) {
                            // DEC VT340 HLS (Blue=0°, Red=120°, Green=240°)
                            DetailAnsiCp437::Vt340HlsToRgb(vals[1], vals[2], vals[3], pal[curCol].data());
                        }
                    }
                }
                continue;
            }

            uint32_t repeat = 1;
            if (c == '!') {
                ++i;
                repeat = 0;
                while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
                    repeat = std::min<uint32_t>(kMaxSixelDim, repeat * 10u + static_cast<uint32_t>(sv[i++] - '0'));
                }
                if (repeat == 0) repeat = 1;
                if (i >= sv.size()) break;
                c = static_cast<uint8_t>(sv[i]);
            }

            if (c >= '?' && c <= '~') {
                uint8_t bits = static_cast<uint8_t>(c - '?');
                if (curX < kMaxSixelDim && bandY < kMaxSixelDim) {
                    uint32_t drawCnt = std::min<uint32_t>(repeat, kMaxSixelDim - curX);
                    ensureCanvas(curX + drawCnt, std::min(kMaxSixelDim, bandY + 6u));
                    const auto& rgb = pal[curCol];
                    for (uint32_t r = 0; r < drawCnt; ++r) {
                        uint32_t x = curX + r;
                        for (uint32_t b = 0; b < 6u; ++b) {
                            if ((bits & (1u << b)) && (bandY + b) < capH) {
                                uint8_t* dst = &canvas[(static_cast<size_t>(bandY + b) * capW + x) * 4u];
                                WritePremulBGRA(dst, rgb[0], rgb[1], rgb[2], 255);
                                usedH = std::max(usedH, bandY + b + 1u);
                            }
                        }
                    }
                    usedW = std::max(usedW, curX + drawCnt);
                    if (usedH < std::min(kMaxSixelDim, bandY + 6u) && bits == 0) {
                        usedH = std::max(usedH, std::min(kMaxSixelDim, bandY + 6u));
                    }
                    renderedSixelCols += drawCnt;
                }
                curX = std::min<uint32_t>(kMaxSixelDim, curX + repeat);
                ++i;
                continue;
            }

            // Reject unexpected high-bit binary bytes inside Sixel stream
            if (c < 0x20 || c > 0x7E) return false;
            ++i;
        }

        if (renderedSixelCols == 0) return false;

        uint32_t outW = (explicitW > 0) ? std::max(explicitW, usedW) : std::max(1u, usedW);
        uint32_t outH = (explicitH > 0) ? std::max(explicitH, usedH) : std::max(1u, usedH);
        outW = std::min(outW, capW);
        outH = std::min(outH, capH);

        outInfo.width = outW;
        outInfo.height = outH;
        outInfo.origWidth = outW;
        outInfo.origHeight = outH;
        outInfo.channels = 4;
        outInfo.bitsPerChannel = 8;
        outInfo.formatName = L"DEC VT340 Sixel Graphics (.sixel)";
        outInfo.bgraPixels.resize(static_cast<size_t>(outW) * outH * 4u);
        for (uint32_t y = 0; y < outH; ++y) {
            std::memcpy(&outInfo.bgraPixels[static_cast<size_t>(y) * outW * 4u],
                        &canvas[static_cast<size_t>(y) * capW * 4u],
                        static_cast<size_t>(outW) * 4u);
        }
        outInfo.valid = true;
        return true;
    }
    return false;
}

} // namespace Formats
