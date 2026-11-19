#pragma once
// InterQuakeIqm.h — Inter-Quake Model (.iqm) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// ============================================================================
// #43. Inter-Quake Model (.iqm — "INTERQUAKEMODEL\0")
// ============================================================================
inline bool ParseInterQuakeIqm(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (size < 124 || std::memcmp(data, "INTERQUAKEMODEL\0", 16) != 0) return false;
    mesh.formatName = L"Inter-Quake 3D Model (.iqm)";

    uint32_t numVar    = Detail3D200::RdU32LE(data + 36);
    uint32_t numVerts  = Detail3D200::RdU32LE(data + 40);
    uint32_t ofsVar    = Detail3D200::RdU32LE(data + 44);
    uint32_t numTris   = Detail3D200::RdU32LE(data + 48);
    uint32_t ofsTris   = Detail3D200::RdU32LE(data + 52);
    uint32_t numFrames = Detail3D200::RdU32LE(data + 76);
    if (numFrames > 0) mesh.animFrameCount = numFrames;

    if (numVerts == 0 || numTris == 0 || numVerts > 1000000 || numTris > 2000000) return false;
    if (ofsVar + static_cast<uint64_t>(numVar) * 20 > size) return false;
    if (ofsTris + static_cast<uint64_t>(numTris) * 12 > size) return false;

    bool foundPos = false;
    for (uint32_t a = 0; a < numVar && a < 64; ++a) {
        const uint8_t* va = data + ofsVar + a * 20;
        uint32_t vaType = Detail3D200::RdU32LE(va + 0);   // 0 = IQM_POSITION
        uint32_t vaFmt  = Detail3D200::RdU32LE(va + 8);   // 7 = IQM_FLOAT
        uint32_t vaSize = Detail3D200::RdU32LE(va + 12);  // 3 floats
        uint32_t vaOfs  = Detail3D200::RdU32LE(va + 16);
        if (vaType == 0 && vaFmt == 7 && vaSize >= 3 && vaOfs + static_cast<uint64_t>(numVerts) * vaSize * 4 <= size) {
            foundPos = true;
            for (uint32_t v = 0; v < numVerts; ++v) {
                const uint8_t* vp = data + vaOfs + static_cast<size_t>(v) * vaSize * 4;
                mesh.vertices.push_back({
                    Detail3D200::RdF32LE(vp + 0),
                    Detail3D200::RdF32LE(vp + 8), // Z-up -> Y-up
                    Detail3D200::RdF32LE(vp + 4)
                });
            }
            break;
        }
    }
    if (!foundPos) return false;

    for (uint32_t t = 0; t < numTris; ++t) {
        const uint8_t* tp = data + ofsTris + static_cast<size_t>(t) * 12;
        uint32_t i0 = Detail3D200::RdU32LE(tp + 0);
        uint32_t i1 = Detail3D200::RdU32LE(tp + 4);
        uint32_t i2 = Detail3D200::RdU32LE(tp + 8);
        if (i0 < numVerts && i1 < numVerts && i2 < numVerts) {
            mesh.tris.push_back({ i0, i2, i1, {} });
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
