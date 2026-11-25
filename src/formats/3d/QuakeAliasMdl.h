#pragma once
// QuakeAliasMdl.h — Quake 1 Alias 3D Model ("IDPO" v6 .mdl) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseQuakeAliasMdl(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // Quake 1 Alias MDL: "IDPO" + version 6 (84-byte header)
    if (size >= 84 && std::memcmp(data, "IDPO", 4) == 0 && DetailMeshSciCad::RdU32LE(data + 4) == 6) {
        mesh.formatName = L"Quake Alias 3D Model (.mdl)";
        float sx = DetailMeshSciCad::RdF32LE(data + 8),  sy = DetailMeshSciCad::RdF32LE(data + 12), sz = DetailMeshSciCad::RdF32LE(data + 16);
        float tx = DetailMeshSciCad::RdF32LE(data + 20), ty = DetailMeshSciCad::RdF32LE(data + 24), tz = DetailMeshSciCad::RdF32LE(data + 28);
        uint32_t numSkins  = DetailMeshSciCad::RdU32LE(data + 48);
        uint32_t skinW     = DetailMeshSciCad::RdU32LE(data + 52);
        uint32_t skinH     = DetailMeshSciCad::RdU32LE(data + 56);
        uint32_t numVerts  = DetailMeshSciCad::RdU32LE(data + 60);
        uint32_t numTris   = DetailMeshSciCad::RdU32LE(data + 64);
        uint32_t numFrames = DetailMeshSciCad::RdU32LE(data + 68);
        mesh.animFrameCount = std::max(1u, numFrames);
        if (numVerts == 0 || numTris == 0 || numVerts > 65536 || numTris > 131072) return false;

        size_t pos = 84;
        for (uint32_t s = 0; s < numSkins && pos + 4 <= size; ++s) {
            uint32_t group = DetailMeshSciCad::RdU32LE(data + pos); pos += 4;
            if (group == 0) pos += static_cast<size_t>(skinW) * skinH;
            else if (pos + 4 <= size) {
                uint32_t nb = DetailMeshSciCad::RdU32LE(data + pos);
                pos += 4 + static_cast<size_t>(nb) * (4 + static_cast<size_t>(skinW) * skinH);
            }
        }
        pos += static_cast<size_t>(numVerts) * 12; // stvert_t[numVerts]
        if (pos + static_cast<size_t>(numTris) * 16 + 4 + 24 + static_cast<size_t>(numVerts) * 4 > size) return false;

        for (uint32_t t = 0; t < numTris; ++t) {
            const uint8_t* tp = data + pos + static_cast<size_t>(t) * 16;
            uint32_t i0 = DetailMeshSciCad::RdU32LE(tp + 4);
            uint32_t i1 = DetailMeshSciCad::RdU32LE(tp + 8);
            uint32_t i2 = DetailMeshSciCad::RdU32LE(tp + 12);
            if (i0 < numVerts && i1 < numVerts && i2 < numVerts) mesh.tris.push_back({ i0, i2, i1, {} });
        }
        pos += static_cast<size_t>(numTris) * 16;

        uint32_t frameType = DetailMeshSciCad::RdU32LE(data + pos); pos += 4;
        if (frameType != 0) {
            if (pos + 12 > size) return false;
            uint32_t nb = DetailMeshSciCad::RdU32LE(data + pos);
            pos += 12 + 4 * static_cast<size_t>(nb);
        }
        pos += 24; // bboxmin(4) + bboxmax(4) + name(16)
        if (pos + static_cast<size_t>(numVerts) * 4 > size) return false;

        for (uint32_t v = 0; v < numVerts; ++v) {
            const uint8_t* vp = data + pos + static_cast<size_t>(v) * 4;
            mesh.vertices.push_back({
                vp[0] * sx + tx,
                vp[2] * sz + tz,
                -(vp[1] * sy + ty)
            });
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
