#pragma once
// Cal3DCmf.h — Cal3D Binary Skeletal Mesh (.cmf) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 1. Cal3D Binary Mesh (.cmf — "CMF\0")
E2_DRACO_NOINLINE inline bool ParseCal3DCmf(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace CalCryDetail;
    if (!data || size < 16 || std::memcmp(data, "CMF\0", 4) != 0) return false;
    int32_t ver = RdI32(data + 4);
    int32_t numSubmeshes = RdI32(data + 8);
    if (ver < 700 || ver > 1500 || numSubmeshes <= 0 || numSubmeshes > 4096) return false;

    size_t pos = 12;
    std::vector<CalWeightedVert> allVerts;
    mesh.tris.clear();

    for (int32_t sm = 0; sm < numSubmeshes; ++sm) {
        if (pos + 24 > size) return false;
        /*int32_t matId =*/ RdI32(data + pos + 0);
        int32_t nVerts   = RdI32(data + pos + 4);
        int32_t nFaces   = RdI32(data + pos + 8);
        int32_t nLod     = RdI32(data + pos + 12);
        int32_t nSprings = RdI32(data + pos + 16);
        int32_t nTexMaps = RdI32(data + pos + 20);
        pos += 24;
        (void)nLod;

        if (nVerts <= 0 || nVerts > 2000000 || nFaces <= 0 || nFaces > 4000000 ||
            nSprings < 0 || nSprings > 2000000 || nTexMaps < 0 || nTexMaps > 16) {
            return false;
        }

        uint32_t baseVert = static_cast<uint32_t>(allVerts.size());
        allVerts.reserve(allVerts.size() + static_cast<size_t>(nVerts));
        for (int32_t v = 0; v < nVerts; ++v) {
            // pos(12) + norm(12) + collapseId(4) + faceCollapseCnt(4) + texCoords(nTexMaps * 8) + numInfluences(4)
            size_t fixedV = 32u + static_cast<size_t>(nTexMaps) * 8u + 4u;
            if (pos + fixedV > size) return false;
            float px = RdF32(data + pos + 0);
            float py = RdF32(data + pos + 4);
            float pz = RdF32(data + pos + 8);
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return false;

            size_t infPos = pos + 32u + static_cast<size_t>(nTexMaps) * 8u;
            int32_t nInf = RdI32(data + infPos);
            pos = infPos + 4u;
            if (nInf < 0 || nInf > 128 || pos + static_cast<size_t>(nInf) * 8u > size) return false;

            CalWeightedVert wv;
            wv.pos = { px, py, pz };
            wv.infs.reserve(static_cast<size_t>(nInf));
            for (int32_t k = 0; k < nInf; ++k) {
                int32_t bId = RdI32(data + pos + 0);
                float   wt  = RdF32(data + pos + 4);
                pos += 8;
                wv.infs.emplace_back(bId, wt);
            }
            if (nSprings > 0) {
                if (pos + 4 > size) return false;
                pos += 4; // float physicalProperty
            }
            allVerts.push_back(std::move(wv));
        }

        // Springs: vertexId[2] (8B) + springCoef (4B) + idleLength (4B) = 16B
        size_t springBytes = static_cast<size_t>(nSprings) * 16u;
        if (pos + springBytes > size) return false;
        pos += springBytes;

        // Faces: 3 * int32 (12B each; or 3 * uint16 if v < 800, though v700+ standard loader uses 32-bit VertexId)
        size_t faceBytes = static_cast<size_t>(nFaces) * 12u;
        if (pos + faceBytes > size) return false;
        mesh.tris.reserve(mesh.tris.size() + static_cast<size_t>(nFaces));
        for (int32_t f = 0; f < nFaces; ++f) {
            int32_t i0 = RdI32(data + pos + 0);
            int32_t i1 = RdI32(data + pos + 4);
            int32_t i2 = RdI32(data + pos + 8);
            pos += 12;
            if (i0 < 0 || i0 >= nVerts || i1 < 0 || i1 >= nVerts || i2 < 0 || i2 >= nVerts) return false;
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                mesh.tris.push_back({
                    baseVert + static_cast<uint32_t>(i0),
                    baseVert + static_cast<uint32_t>(i1),
                    baseVert + static_cast<uint32_t>(i2),
                    {}
                });
            }
        }
    }

    if (allVerts.empty() || mesh.tris.empty()) return false;
    std::vector<CalBone> bones;
    LoadCompanionCalSkeleton(fullPath, bones);
    ApplyCal3DSkinning(bones, allVerts, mesh.vertices);
    mesh.formatName = L"Cal3D Binary Skeletal Mesh (.cmf)";
    return true;
}

} // namespace Formats
