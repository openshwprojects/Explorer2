#pragma once
// RtcwMds.h — Return to Castle Wolfenstein Skeletal Model (.mds MDSW) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 2. Return to Castle Wolfenstein Skeletal Model (.mds "MDSW" v4)
inline bool ParseRtcwMds(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    if (!data || size < 120 || std::memcmp(data, "MDSW", 4) != 0) return false;
    if (R3I32(data + 4) != 4) return false;

    int32_t numFrames   = R3I32(data + 80);
    int32_t numBones    = R3I32(data + 84);
    int32_t ofsFrames   = R3I32(data + 88);
    int32_t ofsBones    = R3I32(data + 92);
    int32_t numSurfaces = R3I32(data + 100);
    int32_t ofsSurfaces = R3I32(data + 104);

    if (numFrames <= 0 || numBones <= 0 || numBones > 1024 || numSurfaces <= 0 || numSurfaces > 1024) return false;
    if (ofsBones < 120 || ofsFrames < 120 || ofsSurfaces < 120) return false;
    size_t boneStride = 80;
    if (ofsFrames > ofsBones) {
        size_t span = static_cast<size_t>(ofsFrames - ofsBones) / static_cast<size_t>(numBones);
        if (span == 80 || span == 84) boneStride = span;
    }
    if (static_cast<uint64_t>(ofsBones) + static_cast<uint64_t>(numBones) * boneStride > size) return false;
    if (static_cast<uint64_t>(ofsFrames) + 52ULL + static_cast<uint64_t>(numBones) * 24ULL > size) return false;

    std::vector<Id3Bone3x4> bones;
    std::vector<int32_t> parents;
    if (!DecodeWolfCompressedBones(
            data + ofsBones, boneStride,
            data + ofsFrames, size - static_cast<size_t>(ofsFrames),
            numBones, bones, parents)) {
        return false;
    }

    size_t sPos = static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (sPos + 176 > size) return false;
        int32_t numVerts = R3I32(data + sPos + 144);
        int32_t ofsVerts = R3I32(data + sPos + 148);
        int32_t numTris  = R3I32(data + sPos + 152);
        int32_t ofsTris  = R3I32(data + sPos + 156);
        int32_t ofsEnd   = R3I32(data + sPos + 172);
        if (ofsEnd <= 0 || sPos + static_cast<size_t>(ofsEnd) > size) return false;
        const size_t sEnd = sPos + static_cast<size_t>(ofsEnd);
        if (numVerts <= 0 || numVerts > 500000 || numTris <= 0 || numTris > 1000000 || ofsVerts < 176 || ofsTris < 176) {
            return false;
        }
        if (sPos + static_cast<uint64_t>(ofsTris) + static_cast<uint64_t>(numTris) * 12ULL > sEnd) return false;

        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t vPos = sPos + static_cast<size_t>(ofsVerts);
        for (int32_t v = 0; v < numVerts; ++v) {
            // mdsVertex_t: normal(12) + texCoords(8) + numWeights(4) + fixedParent(4) + fixedDist(4) = 32 bytes
            if (vPos + 32 > sEnd) return false;
            int32_t nWeights = R3I32(data + vPos + 20);
            vPos += 32;
            if (nWeights <= 0 || nWeights > 32 || vPos + static_cast<size_t>(nWeights) * 20 > sEnd) return false;

            float sx = 0.0f, sy = 0.0f, sz = 0.0f;
            for (int32_t w = 0; w < nWeights; ++w) {
                int32_t boneIdx = R3I32(data + vPos + 0);
                float bw        = R3F32(data + vPos + 4);
                float ox        = R3F32(data + vPos + 8);
                float oy        = R3F32(data + vPos + 12);
                float oz        = R3F32(data + vPos + 16);
                vPos += 20;
                if (boneIdx < 0 || boneIdx >= numBones) return false;
                if (!std::isfinite(bw) || !std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz)) return false;

                float wx = 0.0f, wy = 0.0f, wz = 0.0f;
                bones[static_cast<size_t>(boneIdx)].Transform(ox, oy, oz, wx, wy, wz);
                sx += wx * bw;
                sy += wy * bw;
                sz += wz * bw;
            }
            mesh.vertices.push_back(Id3ZUpToViewport(sx, sy, sz));
        }

        const size_t tBase = sPos + static_cast<size_t>(ofsTris);
        for (int32_t t = 0; t < numTris; ++t) {
            int32_t i0 = R3I32(data + tBase + static_cast<size_t>(t) * 12 + 0);
            int32_t i1 = R3I32(data + tBase + static_cast<size_t>(t) * 12 + 4);
            int32_t i2 = R3I32(data + tBase + static_cast<size_t>(t) * 12 + 8);
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
    mesh.formatName = L"RTCW Skeletal Model (.mds)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
