#pragma once
// Shapefile3D.h — 3D ESRI Shapefile (.shp — PointZ/PolyLineZ/PolygonZ/MultiPatch) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

// ============================================================================
// 3. 3D ESRI Shapefile (.shp — PointZ=11, PolyLineZ=13, PolygonZ=15, MultiPatch=31)
//    & 3D GeoJSON (.geojson — 3D [lon, lat, elev] + extruded buildings)
// ============================================================================
inline bool IsLikely3DShapefileBuffer(const uint8_t* data, size_t size) {
    if (!data || size < 100 || RdU32BE(data) != 9994u) return false;
    int32_t shapeType = RdI32LE(data + 32);
    return (shapeType == 11 || shapeType == 13 || shapeType == 15 || shapeType == 18 || shapeType == 31);
}

inline bool ParseShapefile3DMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 100 || RdU32BE(data) != 9994u) return false;
    int32_t version = RdI32LE(data + 28);
    if (version != 1000) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    size_t pos = 100;
    while (pos + 8 <= size && mesh.tris.size() < 2000000) {
        uint32_t recWords = RdU32BE(data + pos + 4);
        pos += 8;
        size_t recBytes = static_cast<size_t>(recWords) * 2u;
        if (recBytes < 4 || recBytes > size - pos) break;
        const uint8_t* rp = data + pos;
        pos += recBytes;

        int32_t st = RdI32LE(rp);
        if (st == 0) continue; // Null shape

        if ((st == 1 || st == 11 || st == 21) && recBytes >= ((st == 11) ? 28u : 20u)) {
            float x = static_cast<float>(RdF64LE(rp + 4));
            float y = static_cast<float>(RdF64LE(rp + 12));
            float z = (st == 11) ? static_cast<float>(RdF64LE(rp + 20)) : 0.0f;
            Detail3D200::AddOctaSphere(mesh, x, z, -y, 0.5f);
        }
        else if ((st == 3 || st == 5 || st == 13 || st == 15 || st == 31) && recBytes >= 44) {
            int32_t numParts  = RdI32LE(rp + 36);
            int32_t numPoints = RdI32LE(rp + 40);
            if (numParts <= 0 || numPoints <= 0 || numParts > 100000 || numPoints > 1000000) continue;

            size_t partsOff = 44;
            size_t partTypesOff = (st == 31) ? (partsOff + static_cast<size_t>(numParts) * 4u) : partsOff;
            size_t ptsOff = partTypesOff + ((st == 31) ? static_cast<size_t>(numParts) * 4u : static_cast<size_t>(numParts) * 4u);
            if (st != 31) ptsOff = partsOff + static_cast<size_t>(numParts) * 4u;

            bool hasZ = (st == 13 || st == 15 || st == 31);
            size_t zMinMaxOff = ptsOff + static_cast<size_t>(numPoints) * 16u;
            size_t zArrOff    = zMinMaxOff + 16u;
            size_t needed = hasZ ? (zArrOff + static_cast<size_t>(numPoints) * 8u) : (ptsOff + static_cast<size_t>(numPoints) * 16u);
            if (needed > recBytes) continue;

            std::vector<Vec3f> pts(numPoints);
            float minZ = hasZ ? static_cast<float>(RdF64LE(rp + zMinMaxOff)) : 0.0f;
            for (int32_t i = 0; i < numPoints; ++i) {
                double x = RdF64LE(rp + ptsOff + static_cast<size_t>(i) * 16u + 0);
                double y = RdF64LE(rp + ptsOff + static_cast<size_t>(i) * 16u + 8);
                double z = hasZ ? RdF64LE(rp + zArrOff + static_cast<size_t>(i) * 8u) : 0.0;
                pts[i] = { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
            }

            for (int32_t p = 0; p < numParts; ++p) {
                int32_t startIdx = RdI32LE(rp + partsOff + static_cast<size_t>(p) * 4u);
                int32_t endIdx   = (p + 1 < numParts) ? RdI32LE(rp + partsOff + static_cast<size_t>(p + 1) * 4u) : numPoints;
                if (startIdx < 0 || endIdx > numPoints || startIdx >= endIdx) continue;
                int32_t count = endIdx - startIdx;

                if (st == 3 || st == 13) {
                    for (int32_t k = startIdx; k + 1 < endIdx; ++k) {
                        Detail3D200::AddStrut3D(mesh, pts[k], pts[k + 1], 0.25f);
                    }
                } else if (st == 31) {
                    int32_t pType = RdI32LE(rp + partTypesOff + static_cast<size_t>(p) * 4u);
                    uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                    for (int32_t k = startIdx; k < endIdx; ++k) mesh.vertices.push_back(pts[k]);
                    if (pType == 0) { // Triangle Strip
                        for (int32_t k = 0; k + 2 < count; ++k) {
                            if ((k & 1) == 0) mesh.tris.push_back({ vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), vb + static_cast<uint32_t>(k + 2), {} });
                            else              mesh.tris.push_back({ vb + static_cast<uint32_t>(k + 1), vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 2), {} });
                        }
                    } else { // Triangle Fan or Ring
                        int32_t ringCnt = count;
                        if (pType >= 2 && ringCnt >= 4 &&
                            std::fabs(pts[startIdx].x - pts[endIdx - 1].x) < 1e-5f &&
                            std::fabs(pts[startIdx].z - pts[endIdx - 1].z) < 1e-5f) --ringCnt;
                        for (int32_t k = 1; k + 1 < ringCnt; ++k) {
                            mesh.tris.push_back({ vb, vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), {} });
                        }
                    }
                } else { // Polygon / PolygonZ (extrude vertical walls if elevated above 0!)
                    int32_t ringCnt = count;
                    if (ringCnt >= 4 &&
                        std::fabs(pts[startIdx].x - pts[endIdx - 1].x) < 1e-5f &&
                        std::fabs(pts[startIdx].z - pts[endIdx - 1].z) < 1e-5f) --ringCnt;
                    if (ringCnt >= 3) {
                        uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                        for (int32_t k = 0; k < ringCnt; ++k) mesh.vertices.push_back(pts[startIdx + k]);
                        for (int32_t k = 1; k + 1 < ringCnt; ++k) {
                            mesh.tris.push_back({ vb, vb + static_cast<uint32_t>(k), vb + static_cast<uint32_t>(k + 1), {} });
                        }
                        // If PolygonZ has positive roof height and flat/elevated base, emit 3D building walls down to min(0, minZ)
                        float baseElev = std::min(0.0f, minZ);
                        bool hasWall = false;
                        for (int32_t k = 0; k < ringCnt; ++k) if (pts[startIdx + k].y > baseElev + 1e-3f) hasWall = true;
                        if (hasZ && hasWall) {
                            uint32_t bb = static_cast<uint32_t>(mesh.vertices.size());
                            for (int32_t k = 0; k < ringCnt; ++k) {
                                mesh.vertices.push_back({ pts[startIdx + k].x, baseElev, pts[startIdx + k].z });
                            }
                            for (int32_t k = 0; k < ringCnt; ++k) {
                                uint32_t nk = static_cast<uint32_t>((k + 1) % ringCnt);
                                mesh.tris.push_back({ bb + static_cast<uint32_t>(k), bb + nk, vb + nk, {} });
                                mesh.tris.push_back({ bb + static_cast<uint32_t>(k), vb + nk, vb + static_cast<uint32_t>(k), {} });
                            }
                        }
                    }
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"ESRI 3D Shapefile (.shp)";
    return true;
}

} // namespace BspGeoDetail
} // namespace Formats
