#pragma once
// IdTech3Skeletal.h — id Tech 3 / UberTools / RTCW / WolfET Skeletal & Morph Models
//   .md4 ("IDP4" v4 & Elite Force "RDM5" v2)
//   .mds ("MDSW" v4 — Return to Castle Wolfenstein)
//   .mdm ("MDMW" v3 — Wolfenstein: Enemy Territory Skeletal Mesh)
//   .mdx ("MDXW" v2 — Wolfenstein: Enemy Territory Skeletal Animation)
//   .tan ("TAN " v2 — Ritual UberTools / Heavy Metal: F.A.K.K. 2 / Alice)

namespace Formats {

namespace Id3SkelDetail {

inline int32_t R3I32(const uint8_t* p) { int32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline uint16_t R3U16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline int16_t R3I16(const uint8_t* p) { int16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline float R3F32(const uint8_t* p) { float v = 0.0f; std::memcpy(&v, p, 4); return v; }

inline Vec3f Id3ZUpToViewport(float x, float y, float z) {
    return { x, z, -y };
}

struct Id3Bone3x4 {
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };

    void Transform(float ox, float oy, float oz, float& wx, float& wy, float& wz) const {
        wx = m[0][0] * ox + m[0][1] * oy + m[0][2] * oz + m[0][3];
        wy = m[1][0] * ox + m[1][1] * oy + m[1][2] * oz + m[1][3];
        wz = m[2][0] * ox + m[2][1] * oy + m[2][2] * oz + m[2][3];
    }
};

// Reconstruct world-space 3x4 bone matrices from RTCW .mds / WolfET .mdx compressed bone frames
inline bool DecodeWolfCompressedBones(
    const uint8_t* boneInfoPtr,
    size_t boneInfoStride, // 84 for MDS, 80 for MDX
    const uint8_t* framePtr,
    size_t frameAvailableBytes,
    int32_t numBones,
    std::vector<Id3Bone3x4>& outBones,
    std::vector<int32_t>& outParents)
{
    if (numBones <= 0 || numBones > 1024) return false;
    if (frameAvailableBytes < 52ULL + static_cast<size_t>(numBones) * 24ULL) return false;

    outBones.assign(static_cast<size_t>(numBones), Id3Bone3x4{});
    outParents.assign(static_cast<size_t>(numBones), -1);

    // Frame header: bounds[2](24) + localOrigin(12) + radius(4) + parentOffset(12) = 52 bytes
    const float rootOx = R3F32(framePtr + 40);
    const float rootOy = R3F32(framePtr + 44);
    const float rootOz = R3F32(framePtr + 48);

    constexpr float kShortToRad = 6.28318530718f / 65536.0f;

    for (int32_t b = 0; b < numBones; ++b) {
        const uint8_t* bi = boneInfoPtr + static_cast<size_t>(b) * boneInfoStride;
        int32_t parent = R3I32(bi + 64);
        float parentDist = R3F32(bi + 72);
        if (!std::isfinite(parentDist)) parentDist = 0.0f;
        outParents[static_cast<size_t>(b)] = parent;

        const uint8_t* cb = framePtr + 52 + static_cast<size_t>(b) * 24;
        const float pitch = static_cast<float>(R3I16(cb + 0)) * kShortToRad;
        const float yaw   = static_cast<float>(R3I16(cb + 2)) * kShortToRad;
        const float roll  = static_cast<float>(R3I16(cb + 4)) * kShortToRad;
        const float oPitch = static_cast<float>(R3I16(cb + 8)) * kShortToRad;
        const float oYaw   = static_cast<float>(R3I16(cb + 10)) * kShortToRad;

        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw),   sy = std::sin(yaw);
        float cr = std::cos(roll),  sr = std::sin(roll);

        Id3Bone3x4& bm = outBones[static_cast<size_t>(b)];
        // Quake / RTCW AnglesToAxis(pitch, yaw, roll): forward, right->left, up
        bm.m[0][0] = cp * cy;
        bm.m[1][0] = cp * sy;
        bm.m[2][0] = -sp;

        bm.m[0][1] = -( -1.0f * sr * sp * cy + -1.0f * cr * -sy );
        bm.m[1][1] = -( -1.0f * sr * sp * sy + -1.0f * cr * cy );
        bm.m[2][1] = -( -1.0f * sr * cp );

        bm.m[0][2] = ( cr * sp * cy + -sr * -sy );
        bm.m[1][2] = ( cr * sp * sy + -sr * cy );
        bm.m[2][2] = cr * cp;

        if (parent >= 0 && parent < b) {
            float cop = std::cos(oPitch), sop = std::sin(oPitch);
            float coy = std::cos(oYaw),   soy = std::sin(oYaw);
            bm.m[0][3] = outBones[static_cast<size_t>(parent)].m[0][3] + cop * coy * parentDist;
            bm.m[1][3] = outBones[static_cast<size_t>(parent)].m[1][3] + cop * soy * parentDist;
            bm.m[2][3] = outBones[static_cast<size_t>(parent)].m[2][3] - sop * parentDist;
        } else {
            bm.m[0][3] = rootOx;
            bm.m[1][3] = rootOy;
            bm.m[2][3] = rootOz;
        }
    }
    return true;
}

inline void AppendOctahedralBonePrism(const Vec3f& p0, const Vec3f& p1, LoadedMesh3D& mesh) {
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-4f) return;
    Vec3f dir = { d.x / len, d.y / len, d.z / len };
    Vec3f up = (std::fabs(dir.y) < 0.9f) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f s1 = {
        dir.y * up.z - dir.z * up.y,
        dir.z * up.x - dir.x * up.z,
        dir.x * up.y - dir.y * up.x
    };
    float s1L = std::sqrt(s1.x * s1.x + s1.y * s1.y + s1.z * s1.z);
    if (s1L < 1e-6f) return;
    s1.x /= s1L; s1.y /= s1L; s1.z /= s1L;
    Vec3f s2 = {
        dir.y * s1.z - dir.z * s1.y,
        dir.z * s1.x - dir.x * s1.z,
        dir.x * s1.y - dir.y * s1.x
    };

    float rad = len * 0.12f;
    Vec3f mid = { p0.x + d.x * 0.28f, p0.y + d.y * 0.28f, p0.z + d.z * 0.28f };
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(p0);
    mesh.vertices.push_back(p1);
    mesh.vertices.push_back({ mid.x + s1.x * rad, mid.y + s1.y * rad, mid.z + s1.z * rad });
    mesh.vertices.push_back({ mid.x + s2.x * rad, mid.y + s2.y * rad, mid.z + s2.z * rad });
    mesh.vertices.push_back({ mid.x - s1.x * rad, mid.y - s1.y * rad, mid.z - s1.z * rad });
    mesh.vertices.push_back({ mid.x - s2.x * rad, mid.y - s2.y * rad, mid.z - s2.z * rad });

    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
    mesh.tris.push_back({ b + 0, b + 3, b + 4, {} });
    mesh.tris.push_back({ b + 0, b + 4, b + 5, {} });
    mesh.tris.push_back({ b + 0, b + 5, b + 2, {} });
    mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
    mesh.tris.push_back({ b + 1, b + 4, b + 3, {} });
    mesh.tris.push_back({ b + 1, b + 5, b + 4, {} });
    mesh.tris.push_back({ b + 1, b + 2, b + 5, {} });
}

inline bool ParseWolfMdxBonesOnly(
    const uint8_t* data,
    size_t size,
    std::vector<Id3Bone3x4>& outBones,
    std::vector<int32_t>& outParents,
    size_t& outFrames)
{
    if (!data || size < 96 || std::memcmp(data, "MDXW", 4) != 0) return false;
    int32_t ver = R3I32(data + 4);
    if (ver != 2) return false;
    int32_t numFrames = R3I32(data + 72);
    int32_t numBones  = R3I32(data + 76);
    int32_t ofsFrames = R3I32(data + 80);
    int32_t ofsBones  = R3I32(data + 84);
    if (numFrames <= 0 || numBones <= 0 || numBones > 1024 || ofsFrames < 88 || ofsBones < 88) return false;
    if (static_cast<uint64_t>(ofsBones) + static_cast<uint64_t>(numBones) * 80ULL > size) return false;
    if (static_cast<uint64_t>(ofsFrames) + 52ULL + static_cast<uint64_t>(numBones) * 24ULL > size) return false;
    outFrames = static_cast<size_t>(numFrames);
    return DecodeWolfCompressedBones(
        data + ofsBones, 80,
        data + ofsFrames, size - static_cast<size_t>(ofsFrames),
        numBones, outBones, outParents);
}

inline bool ParseWolfMdmWithBones(
    const uint8_t* data,
    size_t size,
    const std::vector<Id3Bone3x4>* optBones,
    LoadedMesh3D& mesh)
{
    if (!data || size < 100 || std::memcmp(data, "MDMW", 4) != 0) return false;
    if (R3I32(data + 4) != 3) return false;
    int32_t numSurfaces = R3I32(data + 80);
    int32_t ofsSurfaces = R3I32(data + 84);
    if (numSurfaces <= 0 || numSurfaces > 1024 || ofsSurfaces < 96 || static_cast<size_t>(ofsSurfaces) >= size) {
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
            if (vPos + 24 > sEnd) return false;
            int32_t nWeights = R3I32(data + vPos + 20);
            vPos += 24;
            if (nWeights <= 0 || nWeights > 32 || vPos + static_cast<size_t>(nWeights) * 20 > sEnd) return false;

            float sx = 0.0f, sy = 0.0f, sz = 0.0f, wSum = 0.0f;
            for (int32_t w = 0; w < nWeights; ++w) {
                int32_t boneIdx = R3I32(data + vPos + 0);
                float bw        = R3F32(data + vPos + 4);
                float ox        = R3F32(data + vPos + 8);
                float oy        = R3F32(data + vPos + 12);
                float oz        = R3F32(data + vPos + 16);
                vPos += 20;
                if (!std::isfinite(bw) || !std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz)) return false;

                float wx = ox, wy = oy, wz = oz;
                if (optBones && boneIdx >= 0 && static_cast<size_t>(boneIdx) < optBones->size()) {
                    (*optBones)[static_cast<size_t>(boneIdx)].Transform(ox, oy, oz, wx, wy, wz);
                }
                sx += wx * bw;
                sy += wy * bw;
                sz += wz * bw;
                wSum += bw;
            }
            if (wSum > 1e-6f && std::fabs(wSum - 1.0f) > 1e-3f) {
                sx /= wSum; sy /= wSum; sz /= wSum;
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
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Id3SkelDetail

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

// 3. Wolfenstein: Enemy Territory Skeletal Mesh (.mdm "MDMW" v3, optionally paired with companion .mdx "MDXW")
inline bool ParseWolfEtMdm(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    std::vector<Id3Bone3x4> bones;
    std::vector<int32_t> parents;
    size_t animFrames = 1;
    bool haveBones = false;

    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        std::vector<uint8_t> mdxBytes;
        if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".mdx", 32 * 1024 * 1024, mdxBytes) &&
            ParseWolfMdxBonesOnly(mdxBytes.data(), mdxBytes.size(), bones, parents, animFrames)) {
            haveBones = true;
        }
        if (!haveBones && !dir.empty()) {
            WIN32_FIND_DATAW fd = {};
            HANDLE hFind = FindFirstFileW((dir + L"\\*.mdx").c_str(), &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (PathUtils::ReadFileBytes(dir + L"\\" + fd.cFileName, 32 * 1024 * 1024, mdxBytes) &&
                            ParseWolfMdxBonesOnly(mdxBytes.data(), mdxBytes.size(), bones, parents, animFrames)) {
                            haveBones = true;
                            break;
                        }
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        }
    }

    if (!ParseWolfMdmWithBones(data, size, haveBones ? &bones : nullptr, mesh)) {
        return false;
    }
    mesh.formatName = L"Wolfenstein: ET Skeletal Mesh (.mdm)";
    mesh.animFrameCount = animFrames;
    return true;
}

// 4. Wolfenstein: Enemy Territory Skeletal Animation (.mdx "MDXW" v2 — skins companion .mdm or renders articulated bone rig)
inline bool ParseWolfEtMdx(const uint8_t* data, size_t size, const std::wstring& fullPath, LoadedMesh3D& mesh) {
    using namespace Id3SkelDetail;
    std::vector<Id3Bone3x4> bones;
    std::vector<int32_t> parents;
    size_t animFrames = 1;
    if (!ParseWolfMdxBonesOnly(data, size, bones, parents, animFrames)) return false;

    // 1. Deform companion .mdm in the same folder if available
    if (!fullPath.empty()) {
        std::wstring dir = PathUtils::GetParentPath(fullPath);
        std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
        std::vector<uint8_t> mdmBytes;
        if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".mdm", 64 * 1024 * 1024, mdmBytes) &&
            ParseWolfMdmWithBones(mdmBytes.data(), mdmBytes.size(), &bones, mesh))
        {
            mesh.formatName = L"Wolfenstein: ET Skeletal Anim (.mdx + .mdm)";
            mesh.animFrameCount = animFrames;
            return true;
        }
    }

    // 2. Standalone .mdx: build articulated 3D octahedral skeleton from Frame 0 bones
    for (size_t b = 0; b < bones.size(); ++b) {
        int32_t p = parents[b];
        Vec3f curPos = Id3ZUpToViewport(bones[b].m[0][3], bones[b].m[1][3], bones[b].m[2][3]);
        if (p >= 0 && static_cast<size_t>(p) < bones.size() && static_cast<size_t>(p) != b) {
            Vec3f parPos = Id3ZUpToViewport(bones[static_cast<size_t>(p)].m[0][3], bones[static_cast<size_t>(p)].m[1][3], bones[static_cast<size_t>(p)].m[2][3]);
            AppendOctahedralBonePrism(parPos, curPos, mesh);
        } else {
            // Root stub along bone forward axis
            Vec3f tip = Id3ZUpToViewport(
                bones[b].m[0][3] + bones[b].m[0][0] * 4.0f,
                bones[b].m[1][3] + bones[b].m[1][0] * 4.0f,
                bones[b].m[2][3] + bones[b].m[2][0] * 4.0f);
            AppendOctahedralBonePrism(curPos, tip, mesh);
        }
    }
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Wolfenstein: ET Skeletal Rig (.mdx)";
    mesh.animFrameCount = animFrames;
    return true;
}

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
