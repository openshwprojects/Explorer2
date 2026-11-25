#pragma once
// EmbroideryDst.h — Tajima Industrial Embroidery Stitch Renderer (.dst)

namespace Formats {

inline bool DecodeEmbroideryDst(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 515) return false;
    // Tajima DST 512-byte ASCII header starts with "LA:" (or printable field labels)
    if (std::memcmp(data, "LA:", 3) != 0 && std::memcmp(data, "la:", 3) != 0) {
        bool hasColonLabel = false;
        for (size_t i = 0; i + 3 < 64; ++i) {
            if (data[i] >= 'A' && data[i] <= 'Z' && data[i + 1] >= 'A' && data[i + 1] <= 'Z' && data[i + 2] == ':') {
                hasColonLabel = true;
                break;
            }
        }
        if (!hasColonLabel) return false;
    }

    std::vector<DetailVectorAndRawImg::StitchPt> pts;
    const std::wstring label = L"Tajima Embroidery Stitch Design (.dst)";
    float cx = 0.0f, cy = 0.0f;
    int col = 0;
    size_t drawnStitches = 0;

    for (size_t p = 512; p + 3 <= size; p += 3) {
        const uint8_t b0 = data[p + 0], b1 = data[p + 1], b2 = data[p + 2];
        if (b0 == 0x00 && b1 == 0x00 && b2 == 0xF3) break; // End of stitch stream
        if ((b2 & 0x03u) != 0x03u) break;                  // Every valid Tajima 3-byte stitch sets low 2 bits of byte 2

        int dx = 0, dy = 0;
        if (b0 & 0x01) dx += 1;
        if (b0 & 0x02) dx -= 1;
        if (b0 & 0x04) dx += 9;
        if (b0 & 0x08) dx -= 9;
        if (b0 & 0x80) dy += 1;
        if (b0 & 0x40) dy -= 1;
        if (b0 & 0x20) dy += 9;
        if (b0 & 0x10) dy -= 9;

        if (b1 & 0x01) dx += 3;
        if (b1 & 0x02) dx -= 3;
        if (b1 & 0x04) dx += 27;
        if (b1 & 0x08) dx -= 27;
        if (b1 & 0x80) dy += 3;
        if (b1 & 0x40) dy -= 3;
        if (b1 & 0x20) dy += 27;
        if (b1 & 0x10) dy -= 27;

        if (b2 & 0x04) dx += 81;
        if (b2 & 0x08) dx -= 81;
        if (b2 & 0x20) dy += 81;
        if (b2 & 0x10) dy -= 81;

        const bool colorChange = ((b2 & 0xC0) == 0xC0);
        const bool jump = ((b2 & 0x80) != 0) && !colorChange;
        if (pts.empty()) {
            pts.push_back({ cx, cy, col, true });
        }
        cx += dx * 0.1f; // 0.1 mm units
        cy -= dy * 0.1f;
        if (colorChange) {
            col = (col + 1) % 12;
            pts.push_back({ cx, cy, col, true });
        } else {
            if (!jump) ++drawnStitches;
            pts.push_back({ cx, cy, col, jump });
        }
    }
    if (drawnStitches == 0 || pts.size() < 2u) return false;
    return DetailVectorAndRawImg::RenderEmbroideryStitches(pts, label, outInfo);
}

} // namespace Formats
