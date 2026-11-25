#pragma once
// EmbroideryPes.h — Brother Industrial Embroidery PEC/PES Stitch Renderer (.pes)

namespace Formats {

inline bool DecodeEmbroideryPes(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64 || std::memcmp(data, "#PES", 4) != 0) return false;
    const std::wstring label = L"Brother Embroidery Stitch Design (.pes)";
    const uint32_t pecOff = DetailLayeredAndHdrImg::RdU32LE(data + 8);
    if (pecOff == 0 || pecOff + 532u > size) return false;

    const uint8_t numColors = data[pecOff + 48];
    std::vector<int> pecPalette;
    for (int i = 0; i <= numColors && pecOff + 49u + static_cast<size_t>(i) < size; ++i) {
        pecPalette.push_back(data[pecOff + 49u + static_cast<size_t>(i)] % 12);
    }
    if (pecPalette.empty()) pecPalette.push_back(0);

    std::vector<DetailVectorAndRawImg::StitchPt> pts;
    size_t sp = pecOff + 532u; // PEC stitch data starts right after 512-byte header + 20-byte graphic header
    float cx = 0.0f, cy = 0.0f;
    int colIdx = 0;
    size_t drawnStitches = 0;

    while (sp < size && pts.size() < 200000u) {
        const uint8_t v1 = data[sp++];
        if (v1 == 0xFFu && (sp >= size || data[sp] == 0x00u)) break; // End of PEC
        if (v1 == 0xFEu && sp + 1u < size && data[sp] == 0xB0u) {
            sp += 2u; // Color change
            colIdx = (colIdx + 1) % static_cast<int>(pecPalette.size());
            pts.push_back({ cx, cy, pecPalette[colIdx], true });
            continue;
        }
        bool jump = false;
        int dx = 0, dy = 0;
        if ((v1 & 0x80u) == 0u) {
            dx = (v1 >= 0x40u) ? (static_cast<int>(v1) - 0x80) : static_cast<int>(v1);
        } else {
            if (sp >= size) break;
            const uint8_t b2 = data[sp++];
            if (v1 & 0x30u) jump = true;
            int d12 = ((v1 & 0x0Fu) << 8) | b2;
            if (d12 & 0x800) d12 -= 0x1000;
            dx = d12;
        }
        if (sp >= size) break;
        const uint8_t v2 = data[sp++];
        if ((v2 & 0x80u) == 0u) {
            dy = (v2 >= 0x40u) ? (static_cast<int>(v2) - 0x80) : static_cast<int>(v2);
        } else {
            if (sp >= size) break;
            const uint8_t b2 = data[sp++];
            if (v2 & 0x30u) jump = true;
            int d12 = ((v2 & 0x0Fu) << 8) | b2;
            if (d12 & 0x800) d12 -= 0x1000;
            dy = d12;
        }
        if (pts.empty()) {
            pts.push_back({ cx, cy, pecPalette[colIdx], true });
        }
        cx += dx * 0.1f;
        cy += dy * 0.1f;
        if (!jump) ++drawnStitches;
        pts.push_back({ cx, cy, pecPalette[colIdx], jump });
    }
    if (drawnStitches == 0 || pts.size() < 2u) return false;
    return DetailVectorAndRawImg::RenderEmbroideryStitches(pts, label, outInfo);
}

} // namespace Formats
