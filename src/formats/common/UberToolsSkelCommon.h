#pragma once
// UberToolsSkelCommon.h — Shared 3x4 bone matrix, quaternion FK, weighted skinning, and skeleton
// mesh emission helpers for Ritual UberTools (.skb/.ska), MOHAA TIKI (.skd/.skc), and Raven Ghoul 2 (.gla).
// Included inside namespace Explorer2 after MeshBuild3D.h (do NOT put #include <...> here).

namespace Formats::UberToolsSkel {

struct BoneMat3x4 {
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };

    void Transform(float lx, float ly, float lz, float& wx, float& wy, float& wz) const noexcept {
        wx = m[0][0] * lx + m[0][1] * ly + m[0][2] * lz + m[0][3];
        wy = m[1][0] * lx + m[1][1] * ly + m[1][2] * lz + m[1][3];
        wz = m[2][0] * lx + m[2][1] * ly + m[2][2] * lz + m[2][3];
    }

    Vec3f Origin() const noexcept {
        return { m[0][3], m[1][3], m[2][3] };
    }
};

inline Vec3f ZUpToViewport(float x, float y, float z) noexcept {
    return { x, z, -y };
}

inline BoneMat3x4 MakeBoneQuatPos(float qx, float qy, float qz, float qw, float tx, float ty, float tz) noexcept {
    BoneMat3x4 out;
    const float lenSq = qx * qx + qy * qy + qz * qz + qw * qw;
    if (lenSq > 1e-12f && std::isfinite(lenSq)) {
        const float inv = 1.0f / std::sqrt(lenSq);
        qx *= inv; qy *= inv; qz *= inv; qw *= inv;
    } else {
        qx = 0.0f; qy = 0.0f; qz = 0.0f; qw = 1.0f;
    }

    const float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    const float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    const float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    out.m[0][0] = 1.0f - 2.0f * (yy + zz);
    out.m[0][1] = 2.0f * (xy - wz);
    out.m[0][2] = 2.0f * (xz + wy);
    out.m[0][3] = tx;

    out.m[1][0] = 2.0f * (xy + wz);
    out.m[1][1] = 1.0f - 2.0f * (xx + zz);
    out.m[1][2] = 2.0f * (yz - wx);
    out.m[1][3] = ty;

    out.m[2][0] = 2.0f * (xz - wy);
    out.m[2][1] = 2.0f * (yz + wx);
    out.m[2][2] = 1.0f - 2.0f * (xx + yy);
    out.m[2][3] = tz;
    return out;
}

inline BoneMat3x4 MulBone3x4(const BoneMat3x4& p, const BoneMat3x4& c) noexcept {
    BoneMat3x4 r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r.m[row][col] = p.m[row][0] * c.m[0][col] +
                            p.m[row][1] * c.m[1][col] +
                            p.m[row][2] * c.m[2][col];
        }
        r.m[row][3] = p.m[row][0] * c.m[0][3] +
                      p.m[row][1] * c.m[1][3] +
                      p.m[row][2] * c.m[2][3] +
                      p.m[row][3];
    }
    return r;
}

inline BoneMat3x4 InverseRigidBone3x4(const BoneMat3x4& a) noexcept {
    BoneMat3x4 inv;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            inv.m[r][c] = a.m[c][r];
        }
    }
    for (int r = 0; r < 3; ++r) {
        inv.m[r][3] = -(inv.m[r][0] * a.m[0][3] +
                        inv.m[r][1] * a.m[1][3] +
                        inv.m[r][2] * a.m[2][3]);
    }
    return inv;
}

// Cycle-safe hierarchical forward kinematics: World[i] = World[parent[i]] * Local[i].
// Returns false if any parent index is out of range or forms a cycle.
inline bool ComputeForwardKinematics(
    const std::vector<int32_t>& parents,
    const std::vector<BoneMat3x4>& localPoses,
    std::vector<BoneMat3x4>& outWorldPoses)
{
    const size_t n = parents.size();
    if (n == 0 || localPoses.size() != n || n > 1024) return false;
    outWorldPoses.resize(n);
    std::vector<uint8_t> state(n, 0); // 0 = unvisited, 1 = visiting, 2 = done

    for (size_t i = 0; i < n; ++i) {
        if (state[i] == 2) continue;
        std::vector<size_t> chain;
        size_t cur = i;
        while (state[cur] != 2) {
            if (state[cur] == 1) return false; // cycle detected
            const int32_t par = parents[cur];
            if (par < -1 || par >= static_cast<int32_t>(n) || par == static_cast<int32_t>(cur)) {
                return false;
            }
            state[cur] = 1;
            chain.push_back(cur);
            if (par == -1) break;
            cur = static_cast<size_t>(par);
            if (chain.size() > n) return false;
        }
        for (size_t k = chain.size(); k > 0; --k) {
            const size_t bi = chain[k - 1];
            const int32_t par = parents[bi];
            if (par == -1) {
                outWorldPoses[bi] = localPoses[bi];
            } else {
                outWorldPoses[bi] = MulBone3x4(outWorldPoses[static_cast<size_t>(par)], localPoses[bi]);
            }
            state[bi] = 2;
        }
    }
    return true;
}

// Parses standard id Tech 3 / UberTools variable-length weighted vertices:
// 24-byte header (normal[3], uv[2], int32 numWeights) + numWeights * 20-byte (int32 boneIndex, float weight, float offset[3]).
inline bool SkinWeightedVertices(
    const uint8_t* data,
    size_t surfEnd,
    size_t vPos,
    int32_t numVerts,
    const std::vector<BoneMat3x4>& worldBones,
    LoadedMesh3D& mesh,
    size_t extraBytesBeforeWeights = 0)
{
    if (numVerts <= 0 || numVerts > 500000 || worldBones.empty()) return false;
    const int32_t numBones = static_cast<int32_t>(worldBones.size());
    for (int32_t v = 0; v < numVerts; ++v) {
        if (vPos + 24u + extraBytesBeforeWeights > surfEnd) return false;
        for (int k = 0; k < 5; ++k) {
            if (!std::isfinite(ByteIO::RdF32LE(data + vPos + static_cast<size_t>(k) * 4u))) return false;
        }
        const int32_t nWeights = ByteIO::RdI32LE(data + vPos + 20u);
        vPos += 24u + extraBytesBeforeWeights;
        if (nWeights <= 0 || nWeights > 32) return false;
        if (vPos + static_cast<size_t>(nWeights) * 20u > surfEnd) return false;

        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        float wSum = 0.0f;
        for (int32_t w = 0; w < nWeights; ++w) {
            const int32_t boneIdx = ByteIO::RdI32LE(data + vPos + 0u);
            const float bw = ByteIO::RdF32LE(data + vPos + 4u);
            const float ox = ByteIO::RdF32LE(data + vPos + 8u);
            const float oy = ByteIO::RdF32LE(data + vPos + 12u);
            const float oz = ByteIO::RdF32LE(data + vPos + 16u);
            vPos += 20u;

            if (boneIdx < 0 || boneIdx >= numBones) return false;
            if (!std::isfinite(bw) || !std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz)) return false;
            if (std::fabs(bw) > 1e4f || std::fabs(ox) > 1e6f || std::fabs(oy) > 1e6f || std::fabs(oz) > 1e6f) return false;

            float wx = 0.0f, wy = 0.0f, wz = 0.0f;
            worldBones[static_cast<size_t>(boneIdx)].Transform(ox, oy, oz, wx, wy, wz);
            sx += wx * bw;
            sy += wy * bw;
            sz += wz * bw;
            wSum += bw;
        }
        if (wSum <= 1e-6f && nWeights > 0) return false;
        mesh.vertices.push_back(ZUpToViewport(sx, sy, sz));
    }
    return true;
}

// Emits articulated 3D skeleton geometry (bone struts + joint octahedra) for standalone animation files (.ska, .skc, .gla).
inline bool EmitSkeletonPoseMesh(
    const std::vector<int32_t>& parents,
    const std::vector<BoneMat3x4>& worldBones,
    LoadedMesh3D& mesh)
{
    const size_t n = worldBones.size();
    if (n == 0 || parents.size() != n) return false;

    std::vector<Vec3f> pts(n);
    float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
    float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
    for (size_t i = 0; i < n; ++i) {
        const Vec3f o = worldBones[i].Origin();
        if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z)) return false;
        pts[i] = ZUpToViewport(o.x, o.y, o.z);
        minX = std::min(minX, pts[i].x); maxX = std::max(maxX, pts[i].x);
        minY = std::min(minY, pts[i].y); maxY = std::max(maxY, pts[i].y);
        minZ = std::min(minZ, pts[i].z); maxZ = std::max(maxZ, pts[i].z);
    }

    const float dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    const float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float jointR = std::max(0.25f, diag * 0.025f);
    const float strutR = std::max(0.14f, diag * 0.014f);

    for (size_t i = 0; i < n; ++i) {
        MeshBuild3D::EmitOctahedron(mesh, pts[i], jointR);
        const int32_t par = parents[i];
        if (par >= 0 && par < static_cast<int32_t>(n)) {
            MeshBuild3D::EmitEdgeStrut(mesh, pts[static_cast<size_t>(par)], pts[i], strutR);
        }
    }
    return !mesh.tris.empty();
}

// Extracts Frame 0 bone poses from a Ritual UberTools "SKAN" v3/v4 buffer (shared by .ska and companion .skb).
inline bool ExtractUberToolsSkaFrame0(
    const uint8_t* data,
    size_t size,
    std::vector<int32_t>& outParents,
    std::vector<BoneMat3x4>& outWorldBones,
    int32_t* outNumFrames = nullptr)
{
    if (!data || size < 116) return false;
    if (std::memcmp(data, "SKAN", 4) != 0) return false;
    const int32_t ver = ByteIO::RdI32LE(data + 4);
    if (ver != 3 && ver != 4) return false;

    const int32_t numFrames = ByteIO::RdI32LE(data + 76);
    const int32_t numBones  = ByteIO::RdI32LE(data + 80);
    const float totalTime   = ByteIO::RdF32LE(data + 84);
    const float frameTime   = ByteIO::RdF32LE(data + 88);
    const int32_t ofsBones  = ByteIO::RdI32LE(data + 104);
    const int32_t ofsFrames = ByteIO::RdI32LE(data + 108);
    const int32_t ofsEnd    = ByteIO::RdI32LE(data + 112);

    if (numFrames < 1 || numFrames > 65536 || numBones < 1 || numBones > 512) return false;
    if (!std::isfinite(totalTime) || !std::isfinite(frameTime)) return false;
    if (ofsEnd < 116 || static_cast<size_t>(ofsEnd) > size) return false;
    const size_t fileEnd = static_cast<size_t>(ofsEnd);

    // skaBoneName_t is 40 bytes: int32 parent, int32 flags, char name[32]
    if (ofsBones < 116 || static_cast<size_t>(ofsBones) + static_cast<size_t>(numBones) * 40u > fileEnd) {
        return false;
    }
    if (ofsFrames < 116 || static_cast<size_t>(ofsFrames) >= fileEnd) return false;

    outParents.resize(static_cast<size_t>(numBones));
    std::vector<int32_t> flags(static_cast<size_t>(numBones), 0);
    for (int32_t b = 0; b < numBones; ++b) {
        const uint8_t* bp = data + static_cast<size_t>(ofsBones) + static_cast<size_t>(b) * 40u;
        outParents[static_cast<size_t>(b)] = ByteIO::RdI32LE(bp + 0);
        flags[static_cast<size_t>(b)]      = ByteIO::RdI32LE(bp + 4);
    }

    // Determine frame stride from the chunk boundary after ofsFrames
    const size_t framesEnd = (ofsBones > ofsFrames) ? static_cast<size_t>(ofsBones) : fileEnd;
    if (framesEnd <= static_cast<size_t>(ofsFrames)) return false;
    const size_t totalFrameBytes = framesEnd - static_cast<size_t>(ofsFrames);
    const size_t frameStride = totalFrameBytes / static_cast<size_t>(numFrames);

    // skaFrame_t header: bounds[2][3] (24B) + radius (4B) + delta[3] (12B) = 40 bytes (or 48B with scale/pad)
    size_t hdrBytes = 40u;
    size_t bonePoseBytes = 0u;
    if (frameStride >= 40u + static_cast<size_t>(numBones) * 16u &&
        (frameStride - 40u) == static_cast<size_t>(numBones) * 16u) {
        hdrBytes = 40u; bonePoseBytes = 16u;
    } else if (frameStride >= 48u + static_cast<size_t>(numBones) * 16u &&
               (frameStride - 48u) == static_cast<size_t>(numBones) * 16u) {
        hdrBytes = 48u; bonePoseBytes = 16u;
    } else if (frameStride >= 40u + static_cast<size_t>(numBones) * 28u &&
               (frameStride - 40u) == static_cast<size_t>(numBones) * 28u) {
        hdrBytes = 40u; bonePoseBytes = 28u;
    } else if (frameStride >= 40u + static_cast<size_t>(numBones) * 32u &&
               (frameStride - 40u) == static_cast<size_t>(numBones) * 32u) {
        hdrBytes = 40u; bonePoseBytes = 32u;
    } else {
        return false;
    }

    const size_t f0Pos = static_cast<size_t>(ofsFrames);
    if (f0Pos + hdrBytes + static_cast<size_t>(numBones) * bonePoseBytes > framesEnd) return false;
    for (int k = 0; k < 10; ++k) {
        if (!std::isfinite(ByteIO::RdF32LE(data + f0Pos + static_cast<size_t>(k) * 4u))) return false;
    }

    std::vector<BoneMat3x4> localPoses(static_cast<size_t>(numBones));
    const uint8_t* poseBase = data + f0Pos + hdrBytes;
    for (int32_t b = 0; b < numBones; ++b) {
        const uint8_t* pb = poseBase + static_cast<size_t>(b) * bonePoseBytes;
        float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        if (bonePoseBytes == 16u) {
            // FAKK2 / Alice 16-byte quantized skaBone_t: int16 quat[4] (/32767), int16 offset[3] (/64), uint16 pad
            qx = static_cast<float>(ByteIO::RdI16LE(pb + 0)) / 32767.0f;
            qy = static_cast<float>(ByteIO::RdI16LE(pb + 2)) / 32767.0f;
            qz = static_cast<float>(ByteIO::RdI16LE(pb + 4)) / 32767.0f;
            qw = static_cast<float>(ByteIO::RdI16LE(pb + 6)) / 32767.0f;
            tx = static_cast<float>(ByteIO::RdI16LE(pb + 8)) / 64.0f;
            ty = static_cast<float>(ByteIO::RdI16LE(pb + 10)) / 64.0f;
            tz = static_cast<float>(ByteIO::RdI16LE(pb + 12)) / 64.0f;
        } else {
            qx = ByteIO::RdF32LE(pb + 0);
            qy = ByteIO::RdF32LE(pb + 4);
            qz = ByteIO::RdF32LE(pb + 8);
            qw = ByteIO::RdF32LE(pb + 12);
            tx = ByteIO::RdF32LE(pb + 16);
            ty = ByteIO::RdF32LE(pb + 20);
            tz = ByteIO::RdF32LE(pb + 24);
            if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw) ||
                !std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz)) {
                return false;
            }
        }
        localPoses[static_cast<size_t>(b)] = MakeBoneQuatPos(qx, qy, qz, qw, tx, ty, tz);
    }

    // In UberTools SKAN, flag bit 0x1 indicates world-space pre-multiplied bone matrices; default 0 is parent-relative FK.
    bool allWorldSpace = true;
    for (int32_t b = 0; b < numBones; ++b) {
        if (outParents[static_cast<size_t>(b)] >= 0 && (flags[static_cast<size_t>(b)] & 1) == 0) {
            allWorldSpace = false;
            break;
        }
    }
    if (allWorldSpace) {
        // Still validate parent indices for cycle/range safety
        std::vector<BoneMat3x4> tmp;
        if (!ComputeForwardKinematics(outParents, localPoses, tmp)) return false;
        outWorldBones = std::move(localPoses);
    } else {
        if (!ComputeForwardKinematics(outParents, localPoses, outWorldBones)) return false;
    }

    if (outNumFrames) *outNumFrames = numFrames;
    return true;
}

} // namespace Formats::UberToolsSkel
