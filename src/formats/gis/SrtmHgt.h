#pragma once
// SrtmHgt.h — Void-Safe NASA SRTM Heightmap (.hgt) 3D terrain reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

// ============================================================================
// 1. Void-Safe NASA SRTM Heightmap (.hgt)
//    Strictly requires exact N*N*2 byte size, culls -32768 void cells cleanly
// ============================================================================
inline bool ParseVoidSafeSrtmHgt(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 18 || (size % 2) != 0) return false;
    size_t totalSamples = size / 2;
    size_t gridN = static_cast<size_t>(std::llround(std::sqrt(static_cast<double>(totalSamples))));
    if (gridN < 3 || gridN * gridN != totalSamples) return false;

    int step = static_cast<int>(std::max<size_t>(1, gridN / 256));
    std::vector<size_t> rows, cols;
    for (size_t r = 0; r < gridN; r += step) rows.push_back(r);
    if (rows.back() != gridN - 1) rows.push_back(gridN - 1);
    for (size_t c = 0; c < gridN; c += step) cols.push_back(c);
    if (cols.back() != gridN - 1) cols.push_back(gridN - 1);

    size_t outH = rows.size(), outW = cols.size();
    if (outW < 2 || outH < 2) return false;

    // Spacing scaled so terrain proportions look natural while preserving real relative meter heights
    float horizStep = (gridN >= 1201) ? (30.0f * static_cast<float>(step)) : 10.0f;
    float elevScale = (gridN >= 1201) ? 1.0f : 0.1f;

    mesh.vertices.clear();
    mesh.tris.clear();
    std::vector<int32_t> vertMap(outH * outW, -1);
    size_t validCount = 0;

    for (size_t ri = 0; ri < outH; ++ri) {
        for (size_t ci = 0; ci < outW; ++ci) {
            size_t off = (rows[ri] * gridN + cols[ci]) * 2u;
            int16_t raw = RdI16BE(data + off);
            if (raw == -32768) continue; // SRTM void sentinel: DO NOT fabricate 0m cliff!
            uint32_t vIdx = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back({
                static_cast<float>(ci) * horizStep,
                static_cast<float>(raw) * elevScale,
                static_cast<float>(ri) * horizStep
            });
            vertMap[ri * outW + ci] = static_cast<int32_t>(vIdx);
            ++validCount;
        }
    }
    if (validCount < 3) return false;

    for (size_t r = 0; r + 1 < outH; ++r) {
        for (size_t c = 0; c + 1 < outW; ++c) {
            int32_t v00 = vertMap[r * outW + c];
            int32_t v10 = vertMap[r * outW + c + 1];
            int32_t v01 = vertMap[(r + 1) * outW + c];
            int32_t v11 = vertMap[(r + 1) * outW + c + 1];
            if (v00 >= 0 && v01 >= 0 && v10 >= 0) {
                mesh.tris.push_back({ static_cast<uint32_t>(v00), static_cast<uint32_t>(v01), static_cast<uint32_t>(v10), {} });
            }
            if (v10 >= 0 && v01 >= 0 && v11 >= 0) {
                mesh.tris.push_back({ static_cast<uint32_t>(v10), static_cast<uint32_t>(v01), static_cast<uint32_t>(v11), {} });
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"NASA SRTM Void-Safe Terrain (.hgt)";
    return true;
}

} // namespace BspGeoDetail
} // namespace Formats
