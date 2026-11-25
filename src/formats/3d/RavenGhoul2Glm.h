#pragma once
// RavenGhoul2Glm.h — Raven Software Ghoul 2 Skeletal Mesh (.glm, "2LGM" v6) reader
// Parses mdxmHeader_t, LOD 0 mdxmLOD_t + mdxmLODSurfOffset_t, multi-surface mdxmSurface_t
// geometry (32-byte mdxmVertex_t + mdxmTriangle_t + trailing UV array), and surface hierarchy.

namespace Formats {

inline bool ParseRavenGhoul2Glm(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    // mdxmHeader_t is 164 bytes
    if (!data || size < 164) return false;
    // MDXM_IDENT = (('M'<<24)+('G'<<16)+('L'<<8)+'2') -> bytes "2LGM"
    if (std::memcmp(data, "2LGM", 4) != 0) return false;

    const int32_t version = ByteIO::RdI32LE(data + 4);
    if (version != 6) return false;

    const int32_t numBones = ByteIO::RdI32LE(data + 140);
    const int32_t numLODs = ByteIO::RdI32LE(data + 144);
    const int32_t ofsLODs = ByteIO::RdI32LE(data + 148);
    const int32_t numSurfaces = ByteIO::RdI32LE(data + 152);
    const int32_t ofsSurfHierarchy = ByteIO::RdI32LE(data + 156);
    const int32_t ofsEnd = ByteIO::RdI32LE(data + 160);

    if (numBones < 1 || numBones > 512) return false;
    if (numLODs < 1 || numLODs > 16) return false;
    if (numSurfaces < 1 || numSurfaces > 1024) return false;
    if (ofsEnd < 164 || static_cast<size_t>(ofsEnd) > size) return false;
    if (ofsLODs < 164 || static_cast<size_t>(ofsLODs) + 4u + static_cast<size_t>(numSurfaces) * 4u > static_cast<size_t>(ofsEnd)) {
        return false;
    }
    if (ofsSurfHierarchy < 164 || static_cast<size_t>(ofsSurfHierarchy) > static_cast<size_t>(ofsEnd)) {
        return false;
    }

    const size_t fileLim = static_cast<size_t>(ofsEnd);
    const size_t lod0Pos = static_cast<size_t>(ofsLODs);
    const int32_t lod0RelEnd = ByteIO::RdI32LE(data + lod0Pos);
    if (lod0RelEnd <= 4 || lod0Pos + static_cast<size_t>(lod0RelEnd) > fileLim) return false;
    const size_t lod0End = lod0Pos + static_cast<size_t>(lod0RelEnd);
    const size_t surfOffBase = lod0Pos + 4u; // sizeof(mdxmLOD_t) == 4

    for (int32_t s = 0; s < numSurfaces; ++s) {
        const int32_t relSurf = ByteIO::RdI32LE(data + surfOffBase + static_cast<size_t>(s) * 4u);
        if (relSurf <= 0) continue;

        // Standard OpenJK: offsets[s] is relative to mdxmLODSurfOffset_t (lod0Pos + 4).
        size_t surfPos = surfOffBase + static_cast<size_t>(relSurf);
        if (surfPos + 40u > lod0End) return false;

        const int32_t surfIndex = ByteIO::RdI32LE(data + surfPos + 4);
        const int32_t numVerts = ByteIO::RdI32LE(data + surfPos + 12);
        const int32_t ofsVerts = ByteIO::RdI32LE(data + surfPos + 16);
        const int32_t numTris = ByteIO::RdI32LE(data + surfPos + 20);
        const int32_t ofsTris = ByteIO::RdI32LE(data + surfPos + 24);
        const int32_t numBoneRefs = ByteIO::RdI32LE(data + surfPos + 28);
        const int32_t surfRelEnd = ByteIO::RdI32LE(data + surfPos + 36);

        if (surfIndex < 0 || surfIndex >= numSurfaces) return false;
        if (numVerts < 0 || numVerts > 500000 || numTris < 0 || numTris > 1000000) return false;
        if (numBoneRefs < 0 || numBoneRefs > numBones) return false;
        if (surfRelEnd < 40 || surfPos + static_cast<size_t>(surfRelEnd) > lod0End) return false;
        if (numVerts == 0 || numTris == 0) continue;

        const size_t surfEnd = surfPos + static_cast<size_t>(surfRelEnd);
        if (ofsVerts < 40 || ofsTris < 40) return false;

        // mdxmVertex_t is 32 bytes, followed by numVerts * 8-byte UV coords
        const size_t vPos = surfPos + static_cast<size_t>(ofsVerts);
        const size_t tPos = surfPos + static_cast<size_t>(ofsTris);
        if (vPos + static_cast<size_t>(numVerts) * 32u > surfEnd) return false;
        if (tPos + static_cast<size_t>(numTris) * 12u > surfEnd) return false;

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        for (int32_t v = 0; v < numVerts; ++v) {
            const uint8_t* vp = data + vPos + static_cast<size_t>(v) * 32u;
            const float nx = ByteIO::RdF32LE(vp + 0);
            const float ny = ByteIO::RdF32LE(vp + 4);
            const float nz = ByteIO::RdF32LE(vp + 8);
            const float px = ByteIO::RdF32LE(vp + 12);
            const float py = ByteIO::RdF32LE(vp + 16);
            const float pz = ByteIO::RdF32LE(vp + 20);
            if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) ||
                !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) {
                return false;
            }
            // Quake 3 / Ghoul 2 uses Z-up; map (x, z, -y) to viewer Y-up
            mesh.vertices.push_back({ px, pz, -py });
        }

        for (int32_t t = 0; t < numTris; ++t) {
            const uint8_t* tp = data + tPos + static_cast<size_t>(t) * 12u;
            const int32_t i0 = ByteIO::RdI32LE(tp + 0);
            const int32_t i1 = ByteIO::RdI32LE(tp + 4);
            const int32_t i2 = ByteIO::RdI32LE(tp + 8);
            if (i0 < 0 || i0 >= numVerts || i1 < 0 || i1 >= numVerts || i2 < 0 || i2 >= numVerts) {
                return false;
            }
            if (i0 == i1 || i1 == i2 || i0 == i2) continue;
            // Quake 3 / Ghoul 2 clockwise front-face convention -> swap (i1, i2) for CCW outward normal
            mesh.tris.push_back({
                baseV + static_cast<uint32_t>(i0),
                baseV + static_cast<uint32_t>(i2),
                baseV + static_cast<uint32_t>(i1),
                {}
            });
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Raven Ghoul 2 Mesh (GLM v6)";
    return true;
}

} // namespace Formats
