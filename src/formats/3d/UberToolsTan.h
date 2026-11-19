#pragma once
// UberToolsTan.h — Ritual Entertainment UberTools Morph Model (.tan) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 5. Ritual Entertainment UberTools Morph Model (.tan "TAN " v2 — Heavy Metal: F.A.K.K. 2 / Alice)
inline bool ParseUberToolsTan(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    if (!data || size < 116 || std::memcmp(data, "TAN ", 4) != 0) return false;
    if (R3I32(data + 4) != 2) return false;

    int32_t numFrames   = R3I32(data + 72);
    int32_t numSurfaces = R3I32(data + 80);
    int32_t ofsFrames   = R3I32(data + 100);
    int32_t ofsSurfaces = R3I32(data + 104);
    if (numFrames <= 0 || numSurfaces <= 0 || numSurfaces > 1024 || ofsFrames < 112 || ofsSurfaces < 112) {
        return false;
    }
    if (static_cast<uint64_t>(ofsFrames) + 48ULL > size) return false;

    // Frame 0 scale & translate
    const uint8_t* fp = data + static_cast<size_t>(ofsFrames);
    float sx = R3F32(fp + 24), sy = R3F32(fp + 28), sz = R3F32(fp + 32);
    float tx = R3F32(fp + 36), ty = R3F32(fp + 40), tz = R3F32(fp + 44);
    if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) ||
        !std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz)) {
        return false;
    }

    size_t sPos = static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (sPos + 100 > size) return false;
        int32_t numVerts = R3I32(data + sPos + 72);
        int32_t numTris  = R3I32(data + sPos + 76);
        int32_t ofsTris  = R3I32(data + sPos + 80);
        int32_t ofsXyz   = R3I32(data + sPos + 92);
        int32_t ofsEnd   = R3I32(data + sPos + 96);
        if (ofsEnd <= 0 || sPos + static_cast<size_t>(ofsEnd) > size) return false;
        const size_t sEnd = sPos + static_cast<size_t>(ofsEnd);
        if (numVerts <= 0 || numVerts > 500000 || numTris <= 0 || numTris > 1000000 || ofsTris < 96 || ofsXyz < 96) {
            return false;
        }
        if (sPos + static_cast<uint64_t>(ofsTris) + static_cast<uint64_t>(numTris) * 12ULL > sEnd) return false;
        if (sPos + static_cast<uint64_t>(ofsXyz) + static_cast<uint64_t>(numVerts) * 8ULL > sEnd) return false;

        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        const uint8_t* vp = data + sPos + static_cast<size_t>(ofsXyz);
        for (int32_t v = 0; v < numVerts; ++v) {
            float x = static_cast<float>(R3U16(vp + static_cast<size_t>(v) * 8 + 0)) * sx + tx;
            float y = static_cast<float>(R3U16(vp + static_cast<size_t>(v) * 8 + 2)) * sy + ty;
            float z = static_cast<float>(R3U16(vp + static_cast<size_t>(v) * 8 + 4)) * sz + tz;
            mesh.vertices.push_back(Id3ZUpToViewport(x, y, z));
        }

        const uint8_t* tp = data + sPos + static_cast<size_t>(ofsTris);
        for (int32_t t = 0; t < numTris; ++t) {
            int32_t i0 = R3I32(tp + static_cast<size_t>(t) * 12 + 0);
            int32_t i1 = R3I32(tp + static_cast<size_t>(t) * 12 + 4);
            int32_t i2 = R3I32(tp + static_cast<size_t>(t) * 12 + 8);
            if (i0 < 0 || i0 >= numVerts || i1 < 0 || i1 >= numVerts || i2 < 0 || i2 >= numVerts) return false;
            if (i0 == i1 || i1 == i2 || i2 == i0) continue;
            mesh.tris.push_back({
                baseV + static_cast<uint32_t>(i0),
                baseV + static_cast<uint32_t>(i2),
                baseV + static_cast<uint32_t>(i1),
                {}
            });
        }
        sPos = sEnd;
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"UberTools TIKI Model (.tan)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
