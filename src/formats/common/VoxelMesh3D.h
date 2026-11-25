#pragma once
// VoxelMesh3D.h — Canonical 6-neighbor hidden-face-culled 3D voxel grid mesher
// Shared across .qb, .kvx, .kv6, .vxl, .gox, .binvox, .mts, .schem, .schematic, and .nbt readers.
// Included inside namespace Explorer2 after MeshBuild3D.h (do NOT put #include <...> here).

namespace Formats::VoxelMesh3D {

struct VoxelCoord {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const VoxelCoord& o) const noexcept {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct VoxelCoordHash {
    size_t operator()(const VoxelCoord& v) const noexcept {
        uint64_t ux = static_cast<uint32_t>(v.x);
        uint64_t uy = static_cast<uint32_t>(v.y);
        uint64_t uz = static_cast<uint32_t>(v.z);
        uint64_t h = (ux * 73856093ULL) ^ (uy * 19349663ULL) ^ (uz * 83492791ULL);
        return static_cast<size_t>(h ^ (h >> 32));
    }
};

inline bool EmitCulledSparseVoxelsScaled(
    const std::unordered_set<VoxelCoord, VoxelCoordHash>& solidVoxels,
    LoadedMesh3D& outMesh,
    Vec3f origin,
    Vec3f scale)
{
    if (solidVoxels.empty() ||
        !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) ||
        scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
        return false;
    }

    static constexpr int32_t kNeighborDirs[6][3] = {
        { -1,  0,  0 }, // -X
        {  1,  0,  0 }, // +X
        {  0, -1,  0 }, // -Y
        {  0,  1,  0 }, // +Y
        {  0,  0, -1 }, // -Z
        {  0,  0,  1 }, // +Z
    };
    static constexpr float kFaceCorners[6][4][3] = {
        { {0,0,0}, {0,0,1}, {0,1,1}, {0,1,0} }, // -X
        { {1,0,0}, {1,1,0}, {1,1,1}, {1,0,1} }, // +X
        { {0,0,0}, {1,0,0}, {1,0,1}, {0,0,1} }, // -Y
        { {0,1,0}, {0,1,1}, {1,1,1}, {1,1,0} }, // +Y
        { {0,0,0}, {0,1,0}, {1,1,0}, {1,0,0} }, // -Z
        { {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1} }, // +Z
    };

    for (const auto& v : solidVoxels) {
        for (int f = 0; f < 6; ++f) {
            const VoxelCoord nb{
                v.x + kNeighborDirs[f][0],
                v.y + kNeighborDirs[f][1],
                v.z + kNeighborDirs[f][2]
            };
            if (solidVoxels.find(nb) != solidVoxels.end()) continue;
            if (outMesh.vertices.size() + 4u > 3000000u || outMesh.tris.size() + 2u > 3000000u) {
                return !outMesh.tris.empty();
            }
            const uint32_t b = static_cast<uint32_t>(outMesh.vertices.size());
            for (int c = 0; c < 4; ++c) {
                outMesh.vertices.push_back({
                    origin.x + (static_cast<float>(v.x) + kFaceCorners[f][c][0]) * scale.x,
                    origin.y + (static_cast<float>(v.y) + kFaceCorners[f][c][1]) * scale.y,
                    origin.z + (static_cast<float>(v.z) + kFaceCorners[f][c][2]) * scale.z
                });
            }
            outMesh.tris.push_back({ b + 0u, b + 1u, b + 2u, {} });
            outMesh.tris.push_back({ b + 0u, b + 2u, b + 3u, {} });
        }
    }
    return !outMesh.tris.empty();
}

inline bool EmitCulledSparseVoxels(
    const std::unordered_set<VoxelCoord, VoxelCoordHash>& solidVoxels,
    LoadedMesh3D& outMesh,
    Vec3f origin = { 0.0f, 0.0f, 0.0f },
    float voxelSize = 1.0f)
{
    return EmitCulledSparseVoxelsScaled(solidVoxels, outMesh, origin, { voxelSize, voxelSize, voxelSize });
}

inline bool EmitExposedVoxelFaces(
    const std::vector<VoxelCoord>& voxels,
    LoadedMesh3D& mesh,
    float ox = 0.0f, float oy = 0.0f, float oz = 0.0f,
    float sx = 1.0f, float sy = 1.0f, float sz = 1.0f)
{
    if (voxels.empty() || voxels.size() > 1500000u) return false;
    std::unordered_set<VoxelCoord, VoxelCoordHash> occ;
    occ.reserve(voxels.size() * 2u);
    for (const auto& v : voxels) {
        occ.insert(v);
    }
    return EmitCulledSparseVoxelsScaled(occ, mesh, { ox, oy, oz }, { sx, sy, sz });
}

inline bool EmitCulledSparseVoxels(
    const std::vector<VoxelCoord>& voxels,
    LoadedMesh3D& outMesh,
    Vec3f origin = { 0.0f, 0.0f, 0.0f },
    float voxelSize = 1.0f)
{
    return EmitExposedVoxelFaces(voxels, outMesh, origin.x, origin.y, origin.z, voxelSize, voxelSize, voxelSize);
}

} // namespace Formats::VoxelMesh3D
