#pragma once
// DarkPlacesDpm.h — DarkPlaces Quake Engine Skeletal Model (.dpm, "DARKPLACESMODEL\0" big-endian v2)
// Parses 80-byte big-endian dpmheader_t, dpmbone_t hierarchy, Frame 0 3x4 dpmbonepose_t FK matrices,
// and dpmmesh_t surfaces with homogeneous pre-weighted dpmbonevert_t vertices + triangle indices.

namespace Formats {

inline bool ParseDarkPlacesDpm(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 80) return false;
    if (std::memcmp(data, "DARKPLACESMODEL\0", 16) != 0) return false;

    const uint32_t type      = ByteIO::RdU32BE(data + 16u);
    const uint32_t fileSize  = ByteIO::RdU32BE(data + 20u);
    const uint32_t numBones  = ByteIO::RdU32BE(data + 56u);
    const uint32_t numMeshes = ByteIO::RdU32BE(data + 60u);
    const uint32_t numFrames = ByteIO::RdU32BE(data + 64u);
    const uint32_t ofsBones  = ByteIO::RdU32BE(data + 68u);
    const uint32_t ofsMeshes = ByteIO::RdU32BE(data + 72u);
    const uint32_t ofsFrames = ByteIO::RdU32BE(data + 76u);

    if (type != 2u || fileSize < 80u || fileSize > size) return false;
    if (numBones > 512u || numMeshes < 1u || numMeshes > 1024u || numFrames < 1u || numFrames > 65536u) {
        return false;
    }

    const size_t fileLim = static_cast<size_t>(fileSize);
    std::vector<UberToolsSkel::BoneMat3x4> worldBones;

    if (numBones > 0u) {
        if (ofsBones < 80u || static_cast<uint64_t>(ofsBones) + static_cast<uint64_t>(numBones) * 40ULL > fileLim) {
            return false;
        }
        if (ofsFrames < 80u || static_cast<uint64_t>(ofsFrames) + 68ULL > fileLim) return false;

        std::vector<int32_t> parents(numBones, -1);
        for (uint32_t b = 0; b < numBones; ++b) {
            const int32_t par = ByteIO::RdI32BE(data + static_cast<size_t>(ofsBones) + static_cast<size_t>(b) * 40u + 32u);
            if (par < -1 || par >= static_cast<int32_t>(numBones) || par == static_cast<int32_t>(b)) {
                return false;
            }
            parents[b] = par;
        }

        const uint32_t ofsBonePos = ByteIO::RdU32BE(data + static_cast<size_t>(ofsFrames) + 64u);
        if (ofsBonePos < 80u || static_cast<uint64_t>(ofsBonePos) + static_cast<uint64_t>(numBones) * 48ULL > fileLim) {
            return false;
        }

        std::vector<UberToolsSkel::BoneMat3x4> localPoses(numBones);
        for (uint32_t b = 0; b < numBones; ++b) {
            const uint8_t* mp = data + static_cast<size_t>(ofsBonePos) + static_cast<size_t>(b) * 48u;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    const float v = ByteIO::RdF32BE(mp + static_cast<size_t>(r * 4 + c) * 4u);
                    if (!std::isfinite(v) || std::fabs(v) > 1e6f) return false;
                    localPoses[b].m[r][c] = v;
                }
            }
        }
        if (!UberToolsSkel::ComputeForwardKinematics(parents, localPoses, worldBones)) return false;
    } else {
        worldBones.resize(1); // rigid 0-bone identity fallback
    }

    // dpmmesh_t is 56 bytes (or 60 with num_blends; stride determined cleanly by standard 56B dpmmesh_t in model_dpmodel.h)
    // Check model_dpmodel.h: shadername[32] + num_verts(4) + num_tris(4) + ofs_verts(4) + ofs_texcoords(4) + ofs_indices(4) + ofs_groupids(4) = 56 bytes!
    if (ofsMeshes < 80u || static_cast<uint64_t>(ofsMeshes) + static_cast<uint64_t>(numMeshes) * 56ULL > fileLim) {
        return false;
    }

    for (uint32_t m = 0; m < numMeshes; ++m) {
        const uint8_t* mh = data + static_cast<size_t>(ofsMeshes) + static_cast<size_t>(m) * 56u;
        const uint32_t numVerts   = ByteIO::RdU32BE(mh + 32u);
        const uint32_t numTris    = ByteIO::RdU32BE(mh + 36u);
        const uint32_t ofsVerts   = ByteIO::RdU32BE(mh + 40u);
        const uint32_t ofsIndices = ByteIO::RdU32BE(mh + 48u);

        if (numVerts == 0u || numTris == 0u) continue;
        if (numVerts > 250000u || numTris > 500000u) return false;
        if (ofsVerts < 80u || ofsIndices < 80u) return false;
        if (static_cast<uint64_t>(ofsIndices) + static_cast<uint64_t>(numTris) * 12ULL > fileLim) return false;

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t vp = static_cast<size_t>(ofsVerts);
        for (uint32_t v = 0; v < numVerts; ++v) {
            if (vp + 4u > fileLim) return false;
            const uint32_t nWeights = ByteIO::RdU32BE(data + vp);
            vp += 4u;
            if (nWeights < 1u || nWeights > 32u || vp + static_cast<size_t>(nWeights) * 32u > fileLim) {
                return false;
            }

            float sx = 0.0f, sy = 0.0f, sz = 0.0f;
            for (uint32_t w = 0; w < nWeights; ++w) {
                const uint8_t* bp = data + vp;
                vp += 32u;
                const float ox  = ByteIO::RdF32BE(bp + 0u);
                const float oy  = ByteIO::RdF32BE(bp + 4u);
                const float oz  = ByteIO::RdF32BE(bp + 8u);
                const float inf = ByteIO::RdF32BE(bp + 12u);
                const uint32_t boneIdx = ByteIO::RdU32BE(bp + 28u);
                if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz) || !std::isfinite(inf)) {
                    return false;
                }
                if (numBones > 0u && boneIdx >= numBones) return false;
                if (numBones == 0u && boneIdx != 0u) return false;

                const auto& bm = worldBones[boneIdx];
                sx += bm.m[0][0] * ox + bm.m[0][1] * oy + bm.m[0][2] * oz + bm.m[0][3] * inf;
                sy += bm.m[1][0] * ox + bm.m[1][1] * oy + bm.m[1][2] * oz + bm.m[1][3] * inf;
                sz += bm.m[2][0] * ox + bm.m[2][1] * oy + bm.m[2][2] * oz + bm.m[2][3] * inf;
            }
            mesh.vertices.push_back(UberToolsSkel::ZUpToViewport(sx, sy, sz));
        }

        const uint8_t* ip = data + static_cast<size_t>(ofsIndices);
        for (uint32_t t = 0; t < numTris; ++t) {
            const uint32_t i0 = ByteIO::RdU32BE(ip + static_cast<size_t>(t) * 12u + 0u);
            const uint32_t i1 = ByteIO::RdU32BE(ip + static_cast<size_t>(t) * 12u + 4u);
            const uint32_t i2 = ByteIO::RdU32BE(ip + static_cast<size_t>(t) * 12u + 8u);
            if (i0 >= numVerts || i1 >= numVerts || i2 >= numVerts) return false;
            if (i0 == i1 || i1 == i2 || i0 == i2) continue;
            mesh.tris.push_back({ baseV + i0, baseV + i2, baseV + i1, {} });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"DarkPlaces Quake Skeletal Model (DPM v2)";
    mesh.animFrameCount = static_cast<size_t>(numFrames);
    return true;
}

} // namespace Formats
