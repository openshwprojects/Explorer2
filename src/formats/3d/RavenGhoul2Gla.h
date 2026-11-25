#pragma once
// RavenGhoul2Gla.h — Raven Software Ghoul 2 Skeletal Animation (.gla, "2LGA" v6) reader
// Parses mdxaHeader_t, mdxaSkelOffsets_t -> mdxaSkel_t hierarchy, Frame 0 24-bit pool indices,
// and 14-byte MC_UnCompressQuat compressed bones, emitting the articulated 3D skeleton.

namespace Formats {

inline bool ParseRavenGhoul2Gla(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    (void)fullPath;
    // mdxaHeader_t is 100 bytes
    if (!data || size < 100) return false;
    if (std::memcmp(data, "2LGA", 4) != 0) return false;

    const int32_t version = ByteIO::RdI32LE(data + 4);
    if (version != 6) return false;

    float fScale = ByteIO::RdF32LE(data + 72);
    if (!std::isfinite(fScale) || fScale < 0.0f || fScale > 1e4f) return false;
    if (fScale <= 1e-6f) fScale = 1.0f;

    const int32_t numFrames       = ByteIO::RdI32LE(data + 76);
    const int32_t ofsFrames       = ByteIO::RdI32LE(data + 80);
    const int32_t numBones        = ByteIO::RdI32LE(data + 84);
    const int32_t ofsCompBonePool = ByteIO::RdI32LE(data + 88);
    const int32_t ofsSkel         = ByteIO::RdI32LE(data + 92);
    const int32_t ofsEnd          = ByteIO::RdI32LE(data + 96);

    if (numFrames < 1 || numFrames > 131072 || numBones < 1 || numBones > 512) return false;
    if (ofsEnd < 100 || static_cast<size_t>(ofsEnd) > size) return false;
    const size_t fileEnd = static_cast<size_t>(ofsEnd);

    if (ofsSkel < 100 || static_cast<size_t>(ofsSkel) + static_cast<size_t>(numBones) * 4u > fileEnd) {
        return false;
    }
    if (ofsFrames < 100 || static_cast<size_t>(ofsFrames) + static_cast<size_t>(numBones) * 3u > fileEnd) {
        return false;
    }
    if (ofsCompBonePool < 100 || static_cast<size_t>(ofsCompBonePool) + 14u > fileEnd) {
        return false;
    }

    std::vector<int32_t> parents(static_cast<size_t>(numBones), -1);
    std::vector<UberToolsSkel::BoneMat3x4> baseWorld(static_cast<size_t>(numBones));
    for (int32_t b = 0; b < numBones; ++b) {
        const int32_t relSkel = ByteIO::RdI32LE(data + static_cast<size_t>(ofsSkel) + static_cast<size_t>(b) * 4u);
        if (relSkel < static_cast<int32_t>(numBones) * 4) return false;
        const size_t skPos = static_cast<size_t>(ofsSkel) + static_cast<size_t>(relSkel);
        if (skPos + 172u > fileEnd) return false;

        const int32_t par = ByteIO::RdI32LE(data + skPos + 68u);
        const int32_t nCh = ByteIO::RdI32LE(data + skPos + 168u);
        if (par < -1 || par >= numBones || par == b) return false;
        if (nCh < 0 || nCh > numBones || skPos + 172u + static_cast<size_t>(nCh) * 4u > fileEnd) return false;
        parents[static_cast<size_t>(b)] = par;

        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                const float mv = ByteIO::RdF32LE(data + skPos + 72u + static_cast<size_t>(r * 4 + c) * 4u);
                if (!std::isfinite(mv)) return false;
                baseWorld[static_cast<size_t>(b)].m[r][c] = mv;
            }
        }
    }

    // Unpack Frame 0 24-bit mdxaIndex_t entries -> 14-byte MC_UnCompressQuat compressed bones
    std::vector<UberToolsSkel::BoneMat3x4> localPoses(static_cast<size_t>(numBones));
    const size_t poolStart = static_cast<size_t>(ofsCompBonePool);
    for (int32_t b = 0; b < numBones; ++b) {
        const uint8_t* ip = data + static_cast<size_t>(ofsFrames) + static_cast<size_t>(b) * 3u;
        const uint32_t poolIdx = static_cast<uint32_t>(ip[0]) |
                                 (static_cast<uint32_t>(ip[1]) << 8) |
                                 (static_cast<uint32_t>(ip[2]) << 16);
        const uint64_t compPos = static_cast<uint64_t>(poolStart) + static_cast<uint64_t>(poolIdx) * 14ULL;
        if (compPos + 14ULL > fileEnd) return false;

        const uint8_t* cp = data + static_cast<size_t>(compPos);
        const float qw = static_cast<float>(ByteIO::RdU16LE(cp + 0)) / 16383.0f - 2.0f;
        const float qx = static_cast<float>(ByteIO::RdU16LE(cp + 2)) / 16383.0f - 2.0f;
        const float qy = static_cast<float>(ByteIO::RdU16LE(cp + 4)) / 16383.0f - 2.0f;
        const float qz = static_cast<float>(ByteIO::RdU16LE(cp + 6)) / 16383.0f - 2.0f;
        const float tx = (static_cast<float>(ByteIO::RdU16LE(cp + 8))  / 64.0f - 512.0f) * fScale;
        const float ty = (static_cast<float>(ByteIO::RdU16LE(cp + 10)) / 64.0f - 512.0f) * fScale;
        const float tz = (static_cast<float>(ByteIO::RdU16LE(cp + 12)) / 64.0f - 512.0f) * fScale;
        localPoses[static_cast<size_t>(b)] = UberToolsSkel::MakeBoneQuatPos(qx, qy, qz, qw, tx, ty, tz);
    }

    std::vector<UberToolsSkel::BoneMat3x4> worldBones;
    if (!UberToolsSkel::ComputeForwardKinematics(parents, localPoses, worldBones)) return false;

    mesh.animFrameCount = static_cast<size_t>(numFrames);
    if (!UberToolsSkel::EmitSkeletonPoseMesh(parents, worldBones, mesh)) return false;
    mesh.formatName = L"Raven Ghoul 2 Skeleton (GLA v6)";
    return true;
}

} // namespace Formats
