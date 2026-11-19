#pragma once
// Quake3Md4.h — Quake III Skeletal (.md4 IDP4) & Elite Force (.md4/.mdr RDM5) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// 1. Quake III Skeletal (.md4 "IDP4" v4) & Star Trek Elite Force (.md4/.mdr "RDM5" v2)
inline bool ParseIdTech3Md4(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    if (!data || size < 96) return false;
    const bool isIdp4 = (std::memcmp(data, "IDP4", 4) == 0);
    const bool isRdm5 = (std::memcmp(data, "RDM5", 4) == 0);
    if (!isIdp4 && !isRdm5) return false;

    int32_t ver = R3I32(data + 4);
    if ((isIdp4 && ver != 4) || (isRdm5 && ver != 2)) return false;

    int32_t numFrames = R3I32(data + 72);
    int32_t numBones  = R3I32(data + 76);
    int32_t ofsFrames = R3I32(data + 80);
    int32_t numLODs   = R3I32(data + 84);
    int32_t ofsLODs   = R3I32(data + 88);

    if (numFrames <= 0 || numBones <= 0 || numBones > 1024 || numLODs <= 0 || ofsLODs < 92 || static_cast<size_t>(ofsLODs) + 12 > size) {
        return false;
    }

    std::vector<Id3Bone3x4> bones(static_cast<size_t>(numBones));
    if (ofsFrames > 0) {
        size_t fOff = static_cast<size_t>(ofsFrames);
        if (fOff + 56ULL + static_cast<uint64_t>(numBones) * 48ULL > size) return false;
        const uint8_t* bp = data + fOff + 56;
        for (int32_t b = 0; b < numBones; ++b) {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    float mv = R3F32(bp + static_cast<size_t>(b) * 48 + static_cast<size_t>(r * 4 + c) * 4);
                    if (!std::isfinite(mv)) return false;
                    bones[static_cast<size_t>(b)].m[r][c] = mv;
                }
            }
        }
    } else if (isRdm5 && ofsFrames < 0) {
        // RDM5 compressed frame: 40-byte header + numBones * 24-byte mdrCompBone_t (quat/angles + pos*64)
        size_t cfOff = static_cast<size_t>(-ofsFrames);
        if (cfOff + 40ULL + static_cast<uint64_t>(numBones) * 24ULL > size) return false;
        const uint8_t* bp = data + cfOff + 40;
        for (int32_t b = 0; b < numBones; ++b) {
            const uint8_t* cb = bp + static_cast<size_t>(b) * 24;
            bones[static_cast<size_t>(b)].m[0][3] = static_cast<float>(R3I16(cb + 16)) / 64.0f;
            bones[static_cast<size_t>(b)].m[1][3] = static_cast<float>(R3I16(cb + 18)) / 64.0f;
            bones[static_cast<size_t>(b)].m[2][3] = static_cast<float>(R3I16(cb + 20)) / 64.0f;
        }
    } else {
        return false;
    }

    size_t lodPos = static_cast<size_t>(ofsLODs);
    int32_t numSurfaces = R3I32(data + lodPos + 0);
    int32_t ofsSurfaces = R3I32(data + lodPos + 4);
    if (numSurfaces <= 0 || numSurfaces > 1024 || ofsSurfaces < 12 || lodPos + static_cast<size_t>(ofsSurfaces) >= size) {
        return false;
    }

    size_t sPos = lodPos + static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (sPos + 168 > size) return false;
        int32_t numVerts = R3I32(data + sPos + 140);
        int32_t ofsVerts = R3I32(data + sPos + 144);
        int32_t numTris  = R3I32(data + sPos + 148);
        int32_t ofsTris  = R3I32(data + sPos + 152);
        int32_t ofsEnd   = R3I32(data + sPos + 164);
        if (ofsEnd <= 0 || sPos + static_cast<size_t>(ofsEnd) > size) return false;
        const size_t sEnd = sPos + static_cast<size_t>(ofsEnd);
        if (numVerts <= 0 || numVerts > 500000 || numTris <= 0 || numTris > 1000000 || ofsVerts < 168 || ofsTris < 168) {
            return false;
        }
        if (sPos + static_cast<uint64_t>(ofsTris) + static_cast<uint64_t>(numTris) * 12ULL > sEnd) return false;

        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t vPos = sPos + static_cast<size_t>(ofsVerts);
        for (int32_t v = 0; v < numVerts; ++v) {
            if (vPos + 24 > sEnd) return false;
            int32_t nWeights = R3I32(data + vPos + 20);
            vPos += 24;
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
    mesh.formatName = isRdm5 ? L"id Tech 3 Raven MDR (.md4)" : L"Quake III Skeletal Model (.md4)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
