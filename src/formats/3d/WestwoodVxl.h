#pragma once
// WestwoodVxl.h — Westwood Studios C&C Tiberian Sun / Red Alert 2 Voxel Model (.vxl) reader
// Parses "Voxel Animation" 802-byte header, 28-byte limb headers, compressed column span streams,
// and 92-byte limb tailers (MinBounds/MaxBounds + 3x4 limb transform matrices).

namespace Formats {

inline bool ParseWestwoodVxl(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    // Minimum: 802 header + 28 limb header + 92 limb tailer = 922 bytes
    if (!data || size < 922) return false;
    if (std::memcmp(data, "Voxel Animation", 15) != 0) return false;

    const uint32_t palCount = ByteIO::RdU32LE(data + 16);
    const uint32_t limbCount = ByteIO::RdU32LE(data + 20);
    const uint32_t tailerCount = ByteIO::RdU32LE(data + 24);
    const uint32_t bodySize = ByteIO::RdU32LE(data + 28);

    if (palCount == 0 || palCount > 16) return false;
    if (limbCount < 1 || limbCount > 256 || tailerCount != limbCount) return false;

    // Header has 34 bytes + palCount * 768 palette bytes (standard palCount == 1 -> 802 bytes)
    const uint64_t hdrSize = 34ULL + static_cast<uint64_t>(palCount) * 768ULL;
    const uint64_t limbHdrBytes = static_cast<uint64_t>(limbCount) * 28ULL;
    const uint64_t tailerBytes = static_cast<uint64_t>(tailerCount) * 92ULL;
    if (hdrSize + limbHdrBytes + static_cast<uint64_t>(bodySize) + tailerBytes > size) return false;

    const size_t bodyBase = static_cast<size_t>(hdrSize + limbHdrBytes);
    const size_t tailerBase = bodyBase + static_cast<size_t>(bodySize);

    for (uint32_t l = 0; l < limbCount; ++l) {
        const uint8_t* tp = data + tailerBase + static_cast<size_t>(l) * 92u;
        const uint32_t spanStartOff = ByteIO::RdU32LE(tp + 0);
        const uint32_t spanEndOff   = ByteIO::RdU32LE(tp + 4);
        const uint32_t spanDataOff  = ByteIO::RdU32LE(tp + 8);
        const float detScale        = ByteIO::RdF32LE(tp + 12);

        float xf[12] = {};
        for (int k = 0; k < 12; ++k) {
            xf[k] = ByteIO::RdF32LE(tp + 16u + static_cast<size_t>(k) * 4u);
            if (!std::isfinite(xf[k])) return false;
        }
        float minB[3] = {}, maxB[3] = {};
        for (int k = 0; k < 3; ++k) {
            minB[k] = ByteIO::RdF32LE(tp + 64u + static_cast<size_t>(k) * 4u);
            maxB[k] = ByteIO::RdF32LE(tp + 76u + static_cast<size_t>(k) * 4u);
            if (!std::isfinite(minB[k]) || !std::isfinite(maxB[k])) return false;
        }
        const uint8_t sx = tp[88];
        const uint8_t sy = tp[89];
        const uint8_t sz = tp[90];
        if (sx == 0 || sy == 0 || sz == 0) continue;

        const size_t colCount = static_cast<size_t>(sx) * static_cast<size_t>(sy);
        if (static_cast<uint64_t>(spanStartOff) + colCount * 4ULL > bodySize) return false;
        if (static_cast<uint64_t>(spanEndOff)   + colCount * 4ULL > bodySize) return false;
        if (spanDataOff > bodySize) return false;

        std::vector<VoxelMesh3D::VoxelCoord> limbVoxels;
        for (uint8_t y = 0; y < sy; ++y) {
            for (uint8_t x = 0; x < sx; ++x) {
                const size_t ci = static_cast<size_t>(x) + static_cast<size_t>(y) * static_cast<size_t>(sx);
                const int32_t st = ByteIO::RdI32LE(data + bodyBase + spanStartOff + ci * 4u);
                const int32_t en = ByteIO::RdI32LE(data + bodyBase + spanEndOff   + ci * 4u);
                if (st < 0 || en < 0) continue; // -1 = empty column
                if (en < st) return false;

                const uint64_t relStart = static_cast<uint64_t>(spanDataOff) + static_cast<uint64_t>(st);
                if (relStart >= bodySize) return false;
                size_t p = bodyBase + static_cast<size_t>(relStart);
                const size_t lim = bodyBase + bodySize;

                int z = 0;
                while (z < static_cast<int>(sz) && p < lim) {
                    const uint8_t skip = data[p++];
                    z += skip;
                    if (z >= static_cast<int>(sz)) break;
                    if (p >= lim) return false;
                    const uint8_t count = data[p++];
                    if (z + static_cast<int>(count) > static_cast<int>(sz)) return false;
                    if (p + static_cast<size_t>(count) * 2u + 1u > lim) return false;
                    for (uint8_t k = 0; k < count; ++k) {
                        limbVoxels.push_back({ static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z + k) });
                    }
                    p += static_cast<size_t>(count) * 2u;
                    const uint8_t countDup = data[p++];
                    if (countDup != count) return false;
                    z += count;
                }
            }
        }

        if (limbVoxels.empty()) continue;

        const float fallbackStep = (std::isfinite(detScale) && detScale > 1e-5f) ? detScale : 1.0f;
        const float stepX = (maxB[0] > minB[0] + 1e-5f) ? ((maxB[0] - minB[0]) / sx) : fallbackStep;
        const float stepY = (maxB[1] > minB[1] + 1e-5f) ? ((maxB[1] - minB[1]) / sy) : fallbackStep;
        const float stepZ = (maxB[2] > minB[2] + 1e-5f) ? ((maxB[2] - minB[2]) / sz) : fallbackStep;

        LoadedMesh3D limbMesh;
        if (!VoxelMesh3D::EmitExposedVoxelFaces(limbVoxels, limbMesh, minB[0], minB[1], minB[2], stepX, stepY, stepZ)) {
            continue;
        }

        // Transform limb vertices by 3x4 matrix (Westwood VXL translation is scaled by detScale when non-zero)
        const float trScale = (std::isfinite(detScale) && detScale > 1e-5f) ? detScale : 1.0f;
        const uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& lv : limbMesh.vertices) {
            const float wx = xf[0] * lv.x + xf[1] * lv.y + xf[2]  * lv.z + xf[3]  * trScale;
            const float wy = xf[4] * lv.x + xf[5] * lv.y + xf[6]  * lv.z + xf[7]  * trScale;
            const float wz = xf[8] * lv.x + xf[9] * lv.y + xf[10] * lv.z + xf[11] * trScale;
            // C&C VXL is right-handed Z-up -> map (wx, wz, -wy) to viewer Y-up
            mesh.vertices.push_back({ wx, wz, -wy });
        }
        for (const auto& lt : limbMesh.tris) {
            mesh.tris.push_back({ vBase + lt.i0, vBase + lt.i1, vBase + lt.i2, {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Westwood C&C Voxel (VXL)";
    return true;
}

} // namespace Formats
