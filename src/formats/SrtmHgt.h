#pragma once
// SrtmHgt.h — NASA SRTM Heightmap (.hgt) Big-Endian 16-bit Signed Elevation 3D Terrain Parser

namespace Formats {

inline bool ParseSrtmHgtMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 18) return false;
    size_t totalSamples = size / 2;
    int gridN = static_cast<int>(std::round(std::sqrt(static_cast<double>(totalSamples))));
    if (gridN < 3 || static_cast<size_t>(gridN * gridN * 2) > size) return false;

    mesh.formatName = L"NASA SRTM Elevation Terrain (.hgt)";
    int step = std::max(1, gridN / 120);
    int outW = 0, outH = 0;

    for (int r = 0; r < gridN; r += step) {
        int rowCols = 0;
        for (int c = 0; c < gridN; c += step) {
            size_t idx = static_cast<size_t>(r * gridN + c) * 2;
            int16_t raw = static_cast<int16_t>((static_cast<uint16_t>(data[idx]) << 8) | data[idx + 1]);
            float elev = (raw == -32768) ? 0.0f : static_cast<float>(raw) * 0.05f;
            mesh.vertices.push_back({ static_cast<float>(c), elev, static_cast<float>(r) });
            ++rowCols;
        }
        outW = rowCols;
        ++outH;
    }
    if (outW < 2 || outH < 2) return false;

    for (int r = 0; r + 1 < outH; ++r) {
        for (int c = 0; c + 1 < outW; ++c) {
            uint32_t i00 = static_cast<uint32_t>(r * outW + c);
            uint32_t i10 = static_cast<uint32_t>(r * outW + c + 1);
            uint32_t i01 = static_cast<uint32_t>((r + 1) * outW + c);
            uint32_t i11 = static_cast<uint32_t>((r + 1) * outW + c + 1);
            mesh.tris.push_back({ i00, i01, i10, {} });
            mesh.tris.push_back({ i10, i01, i11, {} });
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
