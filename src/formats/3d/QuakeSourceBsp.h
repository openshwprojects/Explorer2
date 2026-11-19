#pragma once
// QuakeSourceBsp.h — Quake 1/2/3, GoldSrc & Valve Source (.bsp) 3D level geometry reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

// ============================================================================
// 5. Quake 1 (v29), GoldSrc (v30), Quake II (IBSP v38), Quake III (IBSP v46/47),
//    & Valve Source Engine (VBSP v19–21) .bsp Level Geometry
// ============================================================================
inline bool ParseBspLevelMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 128) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    auto emitSurfEdgeBsp = [&](
        size_t vOff, size_t vLen,
        size_t eOff, size_t eLen,
        size_t seOff, size_t seLen,
        size_t fOff, size_t fLen,
        size_t faceStride, size_t firstEdgeFieldOff, size_t numEdgesFieldOff,
        const wchar_t* fmtLabel) -> bool
    {
        if (vOff + vLen > size || eOff + eLen > size || seOff + seLen > size || fOff + fLen > size) return false;
        size_t nVerts = vLen / 12u;
        size_t nEdges = eLen / 4u;
        size_t nSurfE = seLen / 4u;
        size_t nFaces = fLen / faceStride;
        if (nVerts < 3 || nEdges < 3 || nSurfE < 3 || nFaces == 0 || nVerts > 2000000 || nFaces > 2000000) return false;

        mesh.vertices.resize(nVerts);
        for (size_t i = 0; i < nVerts; ++i) {
            float x = RdF32LE(data + vOff + i * 12u + 0);
            float y = RdF32LE(data + vOff + i * 12u + 4);
            float z = RdF32LE(data + vOff + i * 12u + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            mesh.vertices[i] = { x, z, -y }; // Quake/Source Z-up -> Explorer2 Y-up
        }

        for (size_t f = 0; f < nFaces; ++f) {
            const uint8_t* fp = data + fOff + f * faceStride;
            int32_t firstEdge = RdI32LE(fp + firstEdgeFieldOff);
            int16_t numEdges  = RdI16LE(fp + numEdgesFieldOff);
            if (firstEdge < 0 || numEdges < 3 || static_cast<size_t>(firstEdge + numEdges) > nSurfE) continue;

            std::vector<uint32_t> ring;
            ring.reserve(static_cast<size_t>(numEdges));
            for (int16_t k = 0; k < numEdges; ++k) {
                int32_t se = RdI32LE(data + seOff + static_cast<size_t>(firstEdge + k) * 4u);
                size_t eIdx = static_cast<size_t>(se >= 0 ? se : -se);
                if (eIdx >= nEdges) continue;
                uint16_t v0 = RdU16LE(data + eOff + eIdx * 4u + 0);
                uint16_t v1 = RdU16LE(data + eOff + eIdx * 4u + 2);
                uint16_t vi = (se >= 0) ? v0 : v1;
                if (vi < nVerts) ring.push_back(vi);
            }
            for (size_t k = 1; k + 1 < ring.size(); ++k) {
                if (ring[0] != ring[k] && ring[k] != ring[k + 1] && ring[0] != ring[k + 1]) {
                    mesh.tris.push_back({ ring[0], ring[k], ring[k + 1], {} });
                }
            }
        }
        if (mesh.tris.empty()) { mesh.vertices.clear(); return false; }
        mesh.formatName = fmtLabel;
        return true;
    };

    // 1. Quake 1 (v29) & Half-Life GoldSrc (v30)
    int32_t rawVer = RdI32LE(data);
    if ((rawVer == 29 || rawVer == 30) && size >= 4 + 15 * 8) {
        auto lumpOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 4 + l * 8)); };
        auto lumpLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 4 + l * 8 + 4)); };
        return emitSurfEdgeBsp(
            lumpOff(3), lumpLen(3), lumpOff(12), lumpLen(12),
            lumpOff(13), lumpLen(13), lumpOff(7), lumpLen(7),
            20u, 4u, 8u,
            (rawVer == 30) ? L"Half-Life GoldSrc Map (.bsp v30)" : L"Quake I Level Map (.bsp v29)");
    }

    // 2. id Tech 2 / 3 "IBSP" (v38 Quake II, v46 Quake III, v47 RTCW)
    if (std::memcmp(data, "IBSP", 4) == 0) {
        int32_t ver = RdI32LE(data + 4);
        if (ver == 38 && size >= 8 + 19 * 8) {
            auto lOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8)); };
            auto lLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8 + 4)); };
            return emitSurfEdgeBsp(lOff(2), lLen(2), lOff(11), lLen(11), lOff(12), lLen(12),
                                   lOff(6), lLen(6), 20u, 4u, 8u, L"Quake II IBSP Level (.bsp v38)");
        }
        if ((ver == 46 || ver == 47) && size >= 8 + 17 * 8) {
            auto lOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8)); };
            auto lLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 8 + 4)); };
            size_t dvOff = lOff(10), dvLen = lLen(10);
            size_t mvOff = lOff(11), mvLen = lLen(11);
            size_t fcOff = lOff(13), fcLen = lLen(13);
            if (dvOff + dvLen > size || mvOff + mvLen > size || fcOff + fcLen > size) return false;
            size_t nVerts = dvLen / 44u;
            size_t nMeshV = mvLen / 4u;
            size_t nFaces = fcLen / 104u;
            if (nVerts < 3 || nFaces == 0 || nVerts > 2000000 || nFaces > 2000000) return false;

            std::vector<Vec3f> drawVerts(nVerts);
            std::vector<int32_t> dvRemap(nVerts, -1);
            for (size_t i = 0; i < nVerts; ++i) {
                const uint8_t* vp = data + dvOff + i * 44u;
                drawVerts[i] = { RdF32LE(vp + 0), RdF32LE(vp + 8), -RdF32LE(vp + 4) };
            }
            auto emitDv = [&](size_t dvIdx) -> uint32_t {
                if (dvRemap[dvIdx] < 0) {
                    dvRemap[dvIdx] = static_cast<int32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(drawVerts[dvIdx]);
                }
                return static_cast<uint32_t>(dvRemap[dvIdx]);
            };

            for (size_t f = 0; f < nFaces; ++f) {
                const uint8_t* fp = data + fcOff + f * 104u;
                int32_t fType   = RdI32LE(fp + 8);
                int32_t firstV  = RdI32LE(fp + 12);
                int32_t numV    = RdI32LE(fp + 16);
                int32_t firstMV = RdI32LE(fp + 20);
                int32_t numMV   = RdI32LE(fp + 24);
                if (firstV < 0 || numV <= 0 || static_cast<size_t>(firstV + numV) > nVerts) continue;

                if ((fType == 1 || fType == 3) && firstMV >= 0 && numMV >= 3 && static_cast<size_t>(firstMV + numMV) <= nMeshV) {
                    for (int32_t k = 0; k + 2 < numMV; k += 3) {
                        int32_t o0 = RdI32LE(data + mvOff + static_cast<size_t>(firstMV + k + 0) * 4u);
                        int32_t o1 = RdI32LE(data + mvOff + static_cast<size_t>(firstMV + k + 1) * 4u);
                        int32_t o2 = RdI32LE(data + mvOff + static_cast<size_t>(firstMV + k + 2) * 4u);
                        if (o0 >= 0 && o1 >= 0 && o2 >= 0 && o0 < numV && o1 < numV && o2 < numV) {
                            uint32_t v0 = emitDv(static_cast<size_t>(firstV + o0));
                            uint32_t v2 = emitDv(static_cast<size_t>(firstV + o2));
                            uint32_t v1 = emitDv(static_cast<size_t>(firstV + o1));
                            mesh.tris.push_back({ v0, v2, v1, {} });
                        }
                    }
                } else if (fType == 2) { // Biquadratic Bezier Patch (pw x ph control grid)
                    int32_t pw = RdI32LE(fp + 96), ph = RdI32LE(fp + 100);
                    if (pw >= 3 && ph >= 3 && (pw & 1) == 1 && (ph & 1) == 1 && pw * ph <= numV) {
                        const int tess = 4;
                        for (int py = 0; py + 2 < ph; py += 2) {
                            for (int px = 0; px + 2 < pw; px += 2) {
                                auto cp = [&](int cx, int cy) -> Vec3f {
                                    return drawVerts[static_cast<size_t>(firstV + (py + cy) * pw + (px + cx))];
                                };
                                uint32_t pb = static_cast<uint32_t>(mesh.vertices.size());
                                for (int iy = 0; iy <= tess; ++iy) {
                                    float v = static_cast<float>(iy) / tess, iv = 1.0f - v;
                                    float bv[3] = { iv * iv, 2.0f * iv * v, v * v };
                                    for (int ix = 0; ix <= tess; ++ix) {
                                        float u = static_cast<float>(ix) / tess, iu = 1.0f - u;
                                        float bu[3] = { iu * iu, 2.0f * iu * u, u * u };
                                        Vec3f sum = { 0, 0, 0 };
                                        for (int cy = 0; cy < 3; ++cy) {
                                            for (int cx = 0; cx < 3; ++cx) {
                                                float w = bu[cx] * bv[cy];
                                                Vec3f p = cp(cx, cy);
                                                sum.x += w * p.x; sum.y += w * p.y; sum.z += w * p.z;
                                            }
                                        }
                                        mesh.vertices.push_back(sum);
                                    }
                                }
                                uint32_t stride = tess + 1;
                                for (uint32_t iy = 0; iy < static_cast<uint32_t>(tess); ++iy) {
                                    for (uint32_t ix = 0; ix < static_cast<uint32_t>(tess); ++ix) {
                                        uint32_t a = pb + iy * stride + ix, b = a + 1, c = a + stride, d = c + 1;
                                        mesh.tris.push_back({ a, c, b, {} });
                                        mesh.tris.push_back({ b, c, d, {} });
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (mesh.tris.empty()) { mesh.vertices.clear(); return false; }
            mesh.formatName = L"Quake III Arena IBSP Level (.bsp v46)";
            return true;
        }
    }

    // 3. Valve Source Engine "VBSP" (v19, v20, v21)
    if (std::memcmp(data, "VBSP", 4) == 0 && size >= 8 + 64 * 16) {
        int32_t ver = RdI32LE(data + 4);
        if (ver < 17 || ver > 25) return false;
        auto lOff = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 16)); };
        auto lLen = [&](int l) { return static_cast<size_t>(RdU32LE(data + 8 + l * 16 + 4)); };
        return emitSurfEdgeBsp(lOff(3), lLen(3), lOff(12), lLen(12), lOff(13), lLen(13),
                               lOff(7), lLen(7), 56u, 4u, 8u, L"Valve Source Engine VBSP Map (.bsp)");
    }

    return false;
}

} // namespace BspGeoDetail
} // namespace Formats
