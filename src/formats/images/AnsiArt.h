#pragma once
// AnsiArt.h — ANSI Escape CP437 BBS & SAUCE00 Terminal Art Renderer (.ans)

namespace Formats {

inline bool DecodeAnsiArt(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size == 0) return false;
    if (DecodeSixelGraphics(data, size, L".ans", outInfo)) return true;
    // Parse optional trailing 128-byte ACiD SAUCE00 record ("SAUCE00" at size - 128)
    size_t contentEnd = size;
    int cols = 80;
    bool hasSauce = false;
    bool iceColors = true;
    if (size >= 128 && std::memcmp(data + size - 128, "SAUCE00", 7) == 0) {
        const uint8_t* sauce = data + size - 128;
        hasSauce = true;
        contentEnd = size - 128;
        uint8_t dataType = sauce[94];
        uint8_t fileType = sauce[95];
        if (dataType == 1 && (fileType <= 2)) {
            uint16_t tInfo1 = DetailImg100::RdU16LE(sauce + 96);
            if (tInfo1 >= 1 && tInfo1 <= 320) cols = static_cast<int>(tInfo1);
            // ACiD SAUCE v00.5 places TFlags at offset 105; also check 106 when non-ASCII bitfield
            uint8_t tFlags = sauce[105];
            if (tFlags == 0 && sauce[106] > 0 && sauce[106] < 0x20) {
                tFlags = sauce[106];
            }
            iceColors = ((tFlags & 0x01u) != 0);
        }
        uint8_t commentLines = sauce[104];
        if (commentLines > 0) {
            size_t comntBytes = 5u + static_cast<size_t>(commentLines) * 64u;
            if (contentEnd >= comntBytes && std::memcmp(data + contentEnd - comntBytes, "COMNT", 5) == 0) {
                contentEnd -= comntBytes;
            }
        }
        if (contentEnd > 0 && data[contentEnd - 1] == 0x1A) {
            --contentEnd;
        }
    }

    // Scan up to 0x1A EOF and reject binary garbage (high NUL ratio or binary control noise with no ANSI escapes)
    size_t effectiveLen = 0;
    size_t nulCount = 0, escCount = 0, weirdCtrlCount = 0;
    while (effectiveLen < contentEnd && data[effectiveLen] != 0x1A) {
        uint8_t b = data[effectiveLen++];
        if (b == 0x00) ++nulCount;
        else if (b == 0x1B) ++escCount;
        else if (b < 0x20 && b != '\r' && b != '\n' && b != '\t' && b != '\b' && b != 0x0C) ++weirdCtrlCount;
    }
    if (effectiveLen == 0) return false;
    if (nulCount > 0 && (nulCount * 16 >= effectiveLen || (escCount == 0 && !hasSauce))) {
        return false;
    }
    if (escCount == 0 && !hasSauce && weirdCtrlCount * 8 >= effectiveLen) {
        return false;
    }

    static constexpr uint8_t kCga16[16][3] = {
        {   0,   0,   0 }, { 170,   0,   0 }, {   0, 170,   0 }, { 170,  85,   0 },
        {   0,   0, 170 }, { 170,   0, 170 }, {   0, 170, 170 }, { 170, 170, 170 },
        {  85,  85,  85 }, { 255,  85,  85 }, {  85, 255,  85 }, { 255, 255,  85 },
        {  85,  85, 255 }, { 255,  85, 255 }, {  85, 255, 255 }, { 255, 255, 255 }
    };

    auto xterm256ToRgb = [&](int idx, uint8_t out[3]) {
        idx = std::clamp(idx, 0, 255);
        if (idx < 16) {
            out[0] = kCga16[idx][0]; out[1] = kCga16[idx][1]; out[2] = kCga16[idx][2];
        } else if (idx < 232) {
            int c = idx - 16;
            int r = c / 36, g = (c / 6) % 6, b = c % 6;
            out[0] = static_cast<uint8_t>(r == 0 ? 0 : 55 + r * 40);
            out[1] = static_cast<uint8_t>(g == 0 ? 0 : 55 + g * 40);
            out[2] = static_cast<uint8_t>(b == 0 ? 0 : 55 + b * 40);
        } else {
            uint8_t v = static_cast<uint8_t>(8 + (idx - 232) * 10);
            out[0] = out[1] = out[2] = v;
        }
    };

    constexpr int kMaxAnsiRows = 512;
    struct AnsiCell {
        uint8_t ch = ' ';
        uint8_t fg[3] = { 170, 170, 170 };
        uint8_t bg[3] = { 0, 0, 0 };
    };

    int allocRows = 25;
    std::vector<AnsiCell> grid(static_cast<size_t>(cols) * allocRows);
    auto ensureRow = [&](int r) {
        if (r < allocRows) return;
        int nextRows = std::min(kMaxAnsiRows, std::max(r + 1, allocRows * 2));
        grid.resize(static_cast<size_t>(cols) * nextRows);
        allocRows = nextRows;
    };

    int cx = 0, cy = 0, savedX = 0, savedY = 0, maxUsedRow = 1;
    int fgIdx = 7, bgIdx = 0; // -1 when custom 256-color / 24-bit TrueColor is active
    uint8_t customFg[3] = { 170, 170, 170 };
    uint8_t customBg[3] = { 0, 0, 0 };
    bool bold = false, blink = false, inverse = false;
    size_t drawnCells = 0;

    auto resolveAttrColors = [&](uint8_t effFg[3], uint8_t effBg[3]) {
        uint8_t baseFg[3], baseBg[3];
        if (fgIdx >= 0) {
            int idx = (fgIdx < 8 && bold) ? (fgIdx + 8) : (fgIdx & 15);
            baseFg[0] = kCga16[idx][0]; baseFg[1] = kCga16[idx][1]; baseFg[2] = kCga16[idx][2];
        } else {
            baseFg[0] = customFg[0]; baseFg[1] = customFg[1]; baseFg[2] = customFg[2];
        }
        if (bgIdx >= 0) {
            int idx = (bgIdx < 8 && blink && iceColors) ? (bgIdx + 8) : (bgIdx & 15);
            baseBg[0] = kCga16[idx][0]; baseBg[1] = kCga16[idx][1]; baseBg[2] = kCga16[idx][2];
        } else {
            baseBg[0] = customBg[0]; baseBg[1] = customBg[1]; baseBg[2] = customBg[2];
        }
        if (inverse) {
            effFg[0] = baseBg[0]; effFg[1] = baseBg[1]; effFg[2] = baseBg[2];
            effBg[0] = baseFg[0]; effBg[1] = baseFg[1]; effBg[2] = baseFg[2];
        } else {
            effFg[0] = baseFg[0]; effFg[1] = baseFg[1]; effFg[2] = baseFg[2];
            effBg[0] = baseBg[0]; effBg[1] = baseBg[1]; effBg[2] = baseBg[2];
        }
    };

    size_t p = 0;
    while (p < effectiveLen) {
        uint8_t b = data[p++];
        if (b == 0x1B && p < effectiveLen && data[p] == '[') {
            ++p;
            uint8_t privPrefix = 0;
            if (p < effectiveLen && (data[p] == '?' || data[p] == '=' || data[p] == '>')) {
                privPrefix = data[p++];
            }
            std::vector<int> params;
            int cur = 0;
            bool hasNum = false;
            while (p < effectiveLen) {
                uint8_t c = data[p];
                if (c >= '0' && c <= '9') {
                    cur = std::min(100000, cur * 10 + (c - '0'));
                    hasNum = true;
                    ++p;
                } else if (c == ';' || c == ':') {
                    params.push_back(hasNum ? cur : 0);
                    cur = 0;
                    hasNum = false;
                    ++p;
                } else {
                    break;
                }
            }
            if (hasNum || !params.empty()) params.push_back(hasNum ? cur : 0);
            while (p < effectiveLen && data[p] >= 0x20 && data[p] <= 0x2F) ++p; // Intermediate bytes
            if (p >= effectiveLen) break;
            uint8_t cmd = data[p++];

            if (privPrefix != 0) {
                if (privPrefix == '?' && (cmd == 'h' || cmd == 'l') && !params.empty() && params[0] == 33) {
                    iceColors = (cmd == 'h'); // DEC/iCE private mode ?33h / ?33l
                }
                continue;
            }

            if (cmd == 'm') {
                if (params.empty()) params.push_back(0);
                for (size_t pi = 0; pi < params.size(); ++pi) {
                    int v = params[pi];
                    if (v == 0) { fgIdx = 7; bgIdx = 0; bold = false; blink = false; inverse = false; }
                    else if (v == 1) bold = true;
                    else if (v == 5 || v == 6) blink = true;
                    else if (v == 7) inverse = true;
                    else if (v == 22) bold = false;
                    else if (v == 25) blink = false;
                    else if (v == 27) inverse = false;
                    else if (v >= 30 && v <= 37) fgIdx = v - 30;
                    else if (v == 39) fgIdx = 7;
                    else if (v >= 40 && v <= 47) bgIdx = v - 40;
                    else if (v == 49) bgIdx = 0;
                    else if (v >= 90 && v <= 97) fgIdx = (v - 90) + 8;
                    else if (v >= 100 && v <= 107) bgIdx = (v - 100) + 8;
                    else if (v == 38 || v == 48) {
                        uint8_t* targetRgb = (v == 38) ? customFg : customBg;
                        int& targetIdx     = (v == 38) ? fgIdx : bgIdx;
                        if (pi + 1 < params.size() && params[pi + 1] == 5 && pi + 2 < params.size()) {
                            xterm256ToRgb(params[pi + 2], targetRgb);
                            targetIdx = -1;
                            pi += 2;
                        } else if (pi + 1 < params.size() && params[pi + 1] == 2 && pi + 4 < params.size()) {
                            targetRgb[0] = static_cast<uint8_t>(std::clamp(params[pi + 2], 0, 255));
                            targetRgb[1] = static_cast<uint8_t>(std::clamp(params[pi + 3], 0, 255));
                            targetRgb[2] = static_cast<uint8_t>(std::clamp(params[pi + 4], 0, 255));
                            targetIdx = -1;
                            pi += 4;
                        }
                    }
                }
            } else if (cmd == 'H' || cmd == 'f') {
                cy = std::clamp((params.size() >= 1 ? std::max(1, params[0]) : 1) - 1, 0, kMaxAnsiRows - 1);
                cx = std::clamp((params.size() >= 2 ? std::max(1, params[1]) : 1) - 1, 0, cols - 1);
                ensureRow(cy);
            } else if (cmd == 'A') {
                cy = std::max(0, cy - (params.empty() ? 1 : std::max(1, params[0])));
            } else if (cmd == 'B') {
                cy = std::min(kMaxAnsiRows - 1, cy + (params.empty() ? 1 : std::max(1, params[0])));
                ensureRow(cy);
            } else if (cmd == 'C') {
                cx = std::min(cols - 1, cx + (params.empty() ? 1 : std::max(1, params[0])));
            } else if (cmd == 'D') {
                cx = std::max(0, cx - (params.empty() ? 1 : std::max(1, params[0])));
            } else if (cmd == 'E') {
                cy = std::min(kMaxAnsiRows - 1, cy + (params.empty() ? 1 : std::max(1, params[0])));
                cx = 0;
                ensureRow(cy);
            } else if (cmd == 'F') {
                cy = std::max(0, cy - (params.empty() ? 1 : std::max(1, params[0])));
                cx = 0;
            } else if (cmd == 'G') {
                cx = std::clamp((params.empty() ? 1 : std::max(1, params[0])) - 1, 0, cols - 1);
            } else if (cmd == 'J') {
                int mode = params.empty() ? 0 : params[0];
                uint8_t eFg[3], eBg[3];
                resolveAttrColors(eFg, eBg);
                ensureRow(cy);
                AnsiCell blank{ ' ', { eFg[0], eFg[1], eFg[2] }, { eBg[0], eBg[1], eBg[2] } };
                if (mode == 2) {
                    std::fill(grid.begin(), grid.end(), blank);
                    cx = 0; cy = 0;
                } else if (mode == 0) {
                    size_t startIdx = static_cast<size_t>(cy) * cols + cx;
                    if (startIdx < grid.size()) std::fill(grid.begin() + startIdx, grid.end(), blank);
                } else if (mode == 1) {
                    size_t endIdx = std::min(grid.size(), static_cast<size_t>(cy) * cols + cx + 1);
                    std::fill(grid.begin(), grid.begin() + endIdx, blank);
                }
            } else if (cmd == 'K') {
                int mode = params.empty() ? 0 : params[0];
                uint8_t eFg[3], eBg[3];
                resolveAttrColors(eFg, eBg);
                ensureRow(cy);
                AnsiCell blank{ ' ', { eFg[0], eFg[1], eFg[2] }, { eBg[0], eBg[1], eBg[2] } };
                int c0 = (mode == 0) ? cx : 0;
                int c1 = (mode == 1) ? cx : (cols - 1);
                for (int c = c0; c <= c1 && c < cols; ++c) {
                    grid[static_cast<size_t>(cy) * cols + c] = blank;
                }
            } else if (cmd == 's') {
                savedX = cx; savedY = cy;
            } else if (cmd == 'u') {
                cx = savedX; cy = savedY;
                ensureRow(cy);
            }
            continue;
        }

        if (b == '\r') { cx = 0; continue; }
        if (b == '\n') {
            if (cy + 1 < kMaxAnsiRows) {
                ++cy;
                ensureRow(cy);
            }
            continue;
        }
        if (b == '\b') { cx = std::max(0, cx - 1); continue; }
        if (b == '\t') {
            int nextTab = std::min(cols - 1, (cx + 8) & ~7);
            cx = nextTab;
            continue;
        }
        if (b == 0x00) continue;

        if (cy < kMaxAnsiRows && cx < cols) {
            ensureRow(cy);
            AnsiCell& cell = grid[static_cast<size_t>(cy) * cols + cx];
            cell.ch = b;
            resolveAttrColors(cell.fg, cell.bg);
            ++drawnCells;
            maxUsedRow = std::max(maxUsedRow, cy + 1);
            if (++cx >= cols) {
                cx = 0;
                if (cy + 1 < kMaxAnsiRows) {
                    ++cy;
                    ensureRow(cy);
                }
            }
        }
    }

    if (drawnCells == 0 && escCount == 0) return false;

    int rows = std::clamp(maxUsedRow, 1, allocRows);
    uint32_t imgW = static_cast<uint32_t>(cols * 8);
    uint32_t imgH = static_cast<uint32_t>(rows * 16);
    outInfo.width = imgW;
    outInfo.height = imgH;
    outInfo.origWidth = imgW;
    outInfo.origHeight = imgH;
    outInfo.channels = 4;
    outInfo.bitsPerChannel = 8;
    outInfo.formatName = L"ANSI Escape CP437 Art (.ans)";
    outInfo.bgraPixels.resize(static_cast<size_t>(imgW) * imgH * 4u);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const AnsiCell& cell = grid[static_cast<size_t>(r) * cols + c];
            const uint8_t* glyph = DetailAnsiCp437::kVgaCp437Font[cell.ch];
            const uint32_t px0 = static_cast<uint32_t>(c * 8);
            const uint32_t py0 = static_cast<uint32_t>(r * 16);
            for (uint32_t gy = 0; gy < 16u; ++gy) {
                uint8_t rowBits = glyph[gy];
                uint8_t* dstRow = &outInfo.bgraPixels[(static_cast<size_t>(py0 + gy) * imgW + px0) * 4u];
                for (uint32_t gx = 0; gx < 8u; ++gx) {
                    const uint8_t* col = ((rowBits & (0x80u >> gx)) != 0) ? cell.fg : cell.bg;
                    WritePremulBGRA(dstRow + gx * 4u, col[0], col[1], col[2], 255);
                }
            }
        }
    }
    outInfo.valid = true;
    return true;
}

} // namespace Formats
