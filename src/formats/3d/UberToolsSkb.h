#pragma once
// UberToolsSkb.h — Ritual Entertainment UberTools Skeletal Base Mesh (.skb, "SKL " v3/v4) reader
// Covers Heavy Metal: F.A.K.K. 2, American McGee's Alice, and Star Trek: Elite Force II.

namespace Formats {

inline bool ParseUberToolsSkbWithBones(
    const uint8_t* data,
    size_t size,
    const std::vector<UberToolsSkel::BoneMat3x4>* externalBones,
    LoadedMesh3D& mesh)
{
    if (!data || size < 92) return false;
    if (std::memcmp(data, "SKL ", 4) != 0) return false;

    const int32_t version = ByteIO::RdI32LE(data + 4);
    if (version != 3 && version != 4) return false;

    const size_t minHdr = (version == 4) ? 140u : 92u;
    if (size < minHdr) return false;

    const int32_t numSurfaces = ByteIO::RdI32LE(data + 72);
    const int32_t numBones    = ByteIO::RdI32LE(data + 76);
    const int32_t ofsBones    = ByteIO::RdI32LE(data + 80);
    const int32_t ofsSurfaces = ByteIO::RdI32LE(data + 84);
    const int32_t ofsEnd      = ByteIO::RdI32LE(data + 88);

    if (numSurfaces < 1 || numSurfaces > 1024 || numBones < 1 || numBones > 512) return false;
    if (ofsEnd < static_cast<int32_t>(minHdr) || static_cast<size_t>(ofsEnd) > size) return false;
    const size_t fileEnd = static_cast<size_t>(ofsEnd);

    if (ofsBones < static_cast<int32_t>(minHdr) || ofsSurfaces < static_cast<int32_t>(minHdr)) return false;

    // Validate bone table: v3 uses 40-byte skbBone_t (parent, flags, name[32]); v4 uses 40B or 80B (with basepose)
    const size_t boneChunkEnd = (ofsSurfaces > ofsBones) ? static_cast<size_t>(ofsSurfaces) : fileEnd;
    if (boneChunkEnd < static_cast<size_t>(ofsBones) + static_cast<size_t>(numBones) * 40u) return false;
    const size_t boneStride = (version == 4 &&
        (boneChunkEnd - static_cast<size_t>(ofsBones)) >= static_cast<size_t>(numBones) * 80u &&
        ((boneChunkEnd - static_cast<size_t>(ofsBones)) / static_cast<size_t>(numBones)) == 80u) ? 80u : 40u;

    std::vector<int32_t> parents(static_cast<size_t>(numBones), -1);
    std::vector<UberToolsSkel::BoneMat3x4> localPoses(static_cast<size_t>(numBones));
    for (int32_t b = 0; b < numBones; ++b) {
        const uint8_t* bp = data + static_cast<size_t>(ofsBones) + static_cast<size_t>(b) * boneStride;
        const int32_t par = ByteIO::RdI32LE(bp + 0);
        if (par < -1 || par >= numBones || par == b) return false;
        parents[static_cast<size_t>(b)] = par;
        if (boneStride == 80u) {
            // EF2 v4 optional embedded basepose: quat[4] @ 40 + offset[3] @ 56
            const float qx = ByteIO::RdF32LE(bp + 40);
            const float qy = ByteIO::RdF32LE(bp + 44);
            const float qz = ByteIO::RdF32LE(bp + 48);
            const float qw = ByteIO::RdF32LE(bp + 52);
            const float tx = ByteIO::RdF32LE(bp + 56);
            const float ty = ByteIO::RdF32LE(bp + 60);
            const float tz = ByteIO::RdF32LE(bp + 64);
            if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw) ||
                !std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz)) {
                return false;
            }
            localPoses[static_cast<size_t>(b)] = UberToolsSkel::MakeBoneQuatPos(qx, qy, qz, qw, tx, ty, tz);
        }
    }

    std::vector<UberToolsSkel::BoneMat3x4> worldBones;
    if (!UberToolsSkel::ComputeForwardKinematics(parents, localPoses, worldBones)) return false;
    if (externalBones && externalBones->size() == static_cast<size_t>(numBones)) {
        worldBones = *externalBones;
    }

    const size_t minSurfHdr = (version == 4) ? 100u : 96u;
    size_t sPos = static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (sPos + minSurfHdr > fileEnd) return false;
        const int32_t surfIdent = ByteIO::RdI32LE(data + sPos + 0);
        // SKB_SURFACE_IDENT is "SKL " (0x204C4B53) or SF_SKB (5)
        if (std::memcmp(data + sPos, "SKL ", 4) != 0 && surfIdent != 5) return false;

        const int32_t numTris     = ByteIO::RdI32LE(data + sPos + 68);
        const int32_t numVerts    = ByteIO::RdI32LE(data + sPos + 72);
        const int32_t ofsTris     = ByteIO::RdI32LE(data + sPos + 80);
        const int32_t ofsVerts    = ByteIO::RdI32LE(data + sPos + 84);
        const int32_t ofsCollapse = ByteIO::RdI32LE(data + sPos + 88);
        const int32_t sRelEnd     = ByteIO::RdI32LE(data + sPos + 92);

        if (sRelEnd < static_cast<int32_t>(minSurfHdr) || sPos + static_cast<size_t>(sRelEnd) > fileEnd) {
            return false;
        }
        const size_t sEnd = sPos + static_cast<size_t>(sRelEnd);
        if (numVerts < 0 || numVerts > 250000 || numTris < 0 || numTris > 500000) return false;
        if (numVerts == 0 || numTris == 0) {
            sPos = sEnd;
            continue;
        }
        if (ofsTris < static_cast<int32_t>(minSurfHdr) || ofsVerts < static_cast<int32_t>(minSurfHdr)) return false;
        if (ofsCollapse > 0 && (ofsCollapse < static_cast<int32_t>(minSurfHdr) ||
            sPos + static_cast<size_t>(ofsCollapse) + static_cast<size_t>(numVerts) * 4u > sEnd)) {
            return false;
        }
        if (sPos + static_cast<size_t>(ofsTris) + static_cast<size_t>(numTris) * 12u > sEnd) return false;

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        if (!UberToolsSkel::SkinWeightedVertices(data, sEnd, sPos + static_cast<size_t>(ofsVerts), numVerts, worldBones, mesh)) {
            return false;
        }

        const size_t tBase = sPos + static_cast<size_t>(ofsTris);
        for (int32_t t = 0; t < numTris; ++t) {
            const int32_t i0 = ByteIO::RdI32LE(data + tBase + static_cast<size_t>(t) * 12u + 0u);
            const int32_t i1 = ByteIO::RdI32LE(data + tBase + static_cast<size_t>(t) * 12u + 4u);
            const int32_t i2 = ByteIO::RdI32LE(data + tBase + static_cast<size_t>(t) * 12u + 8u);
            if (i0 < 0 || i0 >= numVerts || i1 < 0 || i1 >= numVerts || i2 < 0 || i2 >= numVerts) return false;
            if (i0 == i1 || i1 == i2 || i0 == i2) continue;
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
    mesh.formatName = (version == 4)
        ? L"UberTools Skeletal Mesh (SKB v4)"
        : L"UberTools Skeletal Mesh (SKB v3)";
    return true;
}

inline bool ParseUberToolsSkb(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    std::vector<UberToolsSkel::BoneMat3x4> companionBones;
    const std::vector<UberToolsSkel::BoneMat3x4>* extPtr = nullptr;

    if (!fullPath.empty()) {
        const std::wstring dir  = PathUtils::GetParentPath(fullPath);
        const std::wstring stem = PathUtils::GetStem(fullPath, false);
        if (!dir.empty() && !stem.empty()) {
            const std::wstring skaPath = dir + L"\\" + stem + L".ska";
            std::vector<uint8_t> skaBuf;
            if (PathUtils::ReadFileBytes(skaPath, 32u * 1024u * 1024u, skaBuf) && skaBuf.size() >= 116) {
                std::vector<int32_t> skaParents;
                int32_t skaFrames = 0;
                if (UberToolsSkel::ExtractUberToolsSkaFrame0(skaBuf.data(), skaBuf.size(), skaParents, companionBones, &skaFrames)) {
                    extPtr = &companionBones;
                    mesh.animFrameCount = static_cast<size_t>(skaFrames);
                }
            }
        }
    }

    return ParseUberToolsSkbWithBones(data, size, extPtr, mesh);
}

} // namespace Formats
