#pragma once
// EmbroideryPes.h — Brother Industrial Embroidery PEC/PES Stitch Renderer (.pes)

namespace Formats {

inline bool DecodeEmbroideryPes(const uint8_t* data, size_t size, ExtendedImageInfo& outInfo) {
    if (!data || size < 64 || std::memcmp(data, "#PES", 4) != 0) return false;
    std::vector<DetailImg200::StitchPt> pts;
    std::wstring label;
        label = L"Brother Embroidery Stitch Design (.pes)";
        uint32_t pecOff = DetailImg100::RdU32LE(data + 8);
        if (pecOff == 0 || pecOff + 532 > size) return false;

        uint8_t numColors = data[pecOff + 48];
        std::vector<int> pecPalette;
        for (int i = 0; i <= numColors && pecOff + 49 + i < size; ++i) {
            pecPalette.push_back(data[pecOff + 49 + i] % 12);
        }
        if (pecPalette.empty()) pecPalette.push_back(0);

        size_t sp = pecOff + 532; // PEC stitch data starts right after 512-byte header + 20-byte graphic header
        float cx = 0.0f, cy = 0.0f;
        int colIdx = 0;
        while (sp < size && pts.size() < 200000) {
            uint8_t v1 = data[sp++];
            if (v1 == 0xFF && (sp >= size || data[sp] == 0x00)) break; // End of PEC
            if (v1 == 0xFE && sp + 1 < size && data[sp] == 0xB0) {
                sp += 2; // Color change
                colIdx = (colIdx + 1) % static_cast<int>(pecPalette.size());
                pts.push_back({ cx, cy, pecPalette[colIdx], true });
                continue;
            }
            bool jump = false;
            int dx = 0, dy = 0;
            if ((v1 & 0x80) == 0) {
                dx = (v1 >= 0x40) ? (static_cast<int>(v1) - 0x80) : static_cast<int>(v1);
            } else {
                if (sp >= size) break;
                uint8_t b2 = data[sp++];
                if (v1 & 0x20) jump = true;
                int d12 = ((v1 & 0x0F) << 8) | b2;
                if (d12 & 0x800) d12 -= 0x1000;
                dx = d12;
            }
            if (sp >= size) break;
            uint8_t v2 = data[sp++];
            if ((v2 & 0x80) == 0) {
                dy = (v2 >= 0x40) ? (static_cast<int>(v2) - 0x80) : static_cast<int>(v2);
            } else {
                if (sp >= size) break;
                uint8_t b2 = data[sp++];
                if (v2 & 0x20) jump = true;
                int d12 = ((v2 & 0x0F) << 8) | b2;
                if (d12 & 0x800) d12 -= 0x1000;
                dy = d12;
            }
            cx += dx * 0.1f;
            cy += dy * 0.1f;
            pts.push_back({ cx, cy, pecPalette[colIdx], jump });
        }
    return DetailImg200::RenderEmbroideryStitches(pts, label, outInfo);
}

} // namespace Formats
