#pragma once
// MohaaSkd.h — 2015 Inc. Medal of Honor: Allied Assault / Spearhead / Breakthrough Skeletal Mesh (.skd, "SKMD" v5/v6)
// Parses skdHeader_t, variable-length boneFileData_t hierarchy (parent by name + baseData offset),
// skdSurface_t surfaces, skeletorVertex_t (with numMorphs * 16B + numWeights * 20B), and triangles.

namespace Formats {

inline bool ParseMohaaSkdWithBones(
    const uint8_t* data,
    size_t size,
    const std::vector<UberToolsSkel::BoneMat3x4>* externalBones,
    LoadedMesh3D& mesh)
{
    if (!data || size < 148) return false;
    if (std::memcmp(data, "SKMD", 4) != 0) return false;

    const int32_t version = ByteIO::RdI32LE(data + 4);
    if (version != 5 && version != 6) return false;
    const size_t minHdr = (version == 6) ? 152u : 148u;
    if (size < minHdr) return false;

    const int32_t numSurfaces = ByteIO::RdI32LE(data + 72);
    const int32_t numBones    = ByteIO::RdI32LE(data + 76);
    const int32_t ofsBones    = ByteIO::RdI32LE(data + 80);
    const int32_t ofsSurfaces = ByteIO::RdI32LE(data + 84);
    const int32_t ofsEnd      = ByteIO::RdI32LE(data + 88);

    float modelScale = 1.0f;
    if (version == 6) {
        const float sc = ByteIO::RdF32LE(data + 148);
        if (!std::isfinite(sc) || sc < 0.0f || sc > 1e4f) return false;
        if (sc > 1e-5f) modelScale = sc;
    }

    if (numSurfaces < 1 || numSurfaces > 1024 || numBones < 1 || numBones > 512) return false;
    if (ofsEnd < static_cast<int32_t>(minHdr) || static_cast<size_t>(ofsEnd) > size) return false;
    const size_t fileEnd = static_cast<size_t>(ofsEnd);
    if (ofsBones < static_cast<int32_t>(minHdr) || ofsSurfaces < static_cast<int32_t>(minHdr)) return false;

    // Parse variable-length boneFileData_t list (84-byte minimum per bone)
    std::vector<std::string> boneNames(static_cast<size_t>(numBones));
    std::vector<std::string> parentNames(static_cast<size_t>(numBones));
    std::unordered_map<std::string, int32_t> nameToIdx;
    std::vector<UberToolsSkel::BoneMat3x4> localPoses(static_cast<size_t>(numBones));

    auto readFixedStr32 = [](const uint8_t* p) -> std::string {
        size_t len = 0;
        while (len < 32u && p[len] != 0) ++len;
        return ReaderText::ToLowerAscii(std::string_view(reinterpret_cast<const char*>(p), len));
    };

    size_t bPos = static_cast<size_t>(ofsBones);
    for (int32_t b = 0; b < numBones; ++b) {
        if (bPos + 84u > fileEnd) return false;
        const std::string bName = readFixedStr32(data + bPos + 0u);
        const std::string pName = readFixedStr32(data + bPos + 32u);
        const int32_t ofsBaseData = ByteIO::RdI32LE(data + bPos + 68u);
        const int32_t boneLen     = ByteIO::RdI32LE(data + bPos + 80u);
        if (boneLen < 84 || bPos + static_cast<size_t>(boneLen) > fileEnd) return false;
        if (bName.empty()) return false;

        boneNames[static_cast<size_t>(b)]   = bName;
        parentNames[static_cast<size_t>(b)] = pName;
        nameToIdx.emplace(bName, b);

        float tx = 0.0f, ty = 0.0f, tz = 0.0f;
        if (ofsBaseData >= 84 && static_cast<size_t>(ofsBaseData) + 12u <= static_cast<size_t>(boneLen)) {
            tx = ByteIO::RdF32LE(data + bPos + static_cast<size_t>(ofsBaseData) + 0u);
            ty = ByteIO::RdF32LE(data + bPos + static_cast<size_t>(ofsBaseData) + 4u);
            tz = ByteIO::RdF32LE(data + bPos + static_cast<size_t>(ofsBaseData) + 8u);
            if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) ||
                std::fabs(tx) > 1e6f || std::fabs(ty) > 1e6f || std::fabs(tz) > 1e6f) {
                return false;
            }
        }
        localPoses[static_cast<size_t>(b)] = UberToolsSkel::MakeBoneQuatPos(0.0f, 0.0f, 0.0f, 1.0f, tx, ty, tz);
        bPos += static_cast<size_t>(boneLen);
    }

    std::vector<int32_t> parents(static_cast<size_t>(numBones), -1);
    for (int32_t b = 0; b < numBones; ++b) {
        const std::string& pn = parentNames[static_cast<size_t>(b)];
        if (pn.empty() || pn == "worldbone") {
            parents[static_cast<size_t>(b)] = -1;
        } else {
            auto it = nameToIdx.find(pn);
            if (it == nameToIdx.end() || it->second == b) return false;
            parents[static_cast<size_t>(b)] = it->second;
        }
    }

    std::vector<UberToolsSkel::BoneMat3x4> worldBones;
    if (!UberToolsSkel::ComputeForwardKinematics(parents, localPoses, worldBones)) return false;
    if (externalBones && externalBones->size() == static_cast<size_t>(numBones)) {
        worldBones = *externalBones;
    }

    size_t sPos = static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (sPos + 100u > fileEnd) return false;
        const int32_t surfIdent = ByteIO::RdI32LE(data + sPos + 0u);
        if (std::memcmp(data + sPos, "SKMD", 4) != 0 && std::memcmp(data + sPos, "SKL ", 4) != 0 && surfIdent != 5) {
            return false;
        }

        const int32_t numTris  = ByteIO::RdI32LE(data + sPos + 68u);
        const int32_t numVerts = ByteIO::RdI32LE(data + sPos + 72u);
        const int32_t ofsTris  = ByteIO::RdI32LE(data + sPos + 80u);
        const int32_t ofsVerts = ByteIO::RdI32LE(data + sPos + 84u);
        const int32_t sRelEnd  = ByteIO::RdI32LE(data + sPos + 92u);

        if (sRelEnd < 100 || sPos + static_cast<size_t>(sRelEnd) > fileEnd) return false;
        const size_t sEnd = sPos + static_cast<size_t>(sRelEnd);
        if (numVerts < 0 || numVerts > 250000 || numTris < 0 || numTris > 500000) return false;
        if (numVerts == 0 || numTris == 0) {
            sPos = sEnd;
            continue;
        }
        if (ofsTris < 100 || ofsVerts < 100) return false;
        if (sPos + static_cast<size_t>(ofsTris) + static_cast<size_t>(numTris) * 12u > sEnd) return false;

        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t vPos = sPos + static_cast<size_t>(ofsVerts);
        for (int32_t v = 0; v < numVerts; ++v) {
            if (vPos + 28u > sEnd) return false;
            for (int k = 0; k < 5; ++k) {
                if (!std::isfinite(ByteIO::RdF32LE(data + vPos + static_cast<size_t>(k) * 4u))) return false;
            }
            const int32_t nWeights = ByteIO::RdI32LE(data + vPos + 20u);
            const int32_t nMorphs  = ByteIO::RdI32LE(data + vPos + 24u);
            vPos += 28u;
            if (nWeights <= 0 || nWeights > 32 || nMorphs < 0 || nMorphs > 256) return false;
            const size_t morphBytes  = static_cast<size_t>(nMorphs) * 16u;
            const size_t weightBytes = static_cast<size_t>(nWeights) * 20u;
            if (vPos + morphBytes + weightBytes > sEnd) return false;

            float mx = 0.0f, my = 0.0f, mz = 0.0f;
            for (int32_t m = 0; m < nMorphs; ++m) {
                const int32_t mIdx = ByteIO::RdI32LE(data + vPos + 0u);
                const float dx = ByteIO::RdF32LE(data + vPos + 4u);
                const float dy = ByteIO::RdF32LE(data + vPos + 8u);
                const float dz = ByteIO::RdF32LE(data + vPos + 12u);
                vPos += 16u;
                if (mIdx < 0 || !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) return false;
                (void)mx; (void)my; (void)mz; // morphs are neutral (weight 0) in bind pose
            }

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
                float wx = 0.0f, wy = 0.0f, wz = 0.0f;
                worldBones[static_cast<size_t>(boneIdx)].Transform(ox, oy, oz, wx, wy, wz);
                sx += wx * bw * modelScale;
                sy += wy * bw * modelScale;
                sz += wz * bw * modelScale;
                wSum += bw;
            }
            if (wSum <= 1e-6f) return false;
            mesh.vertices.push_back(UberToolsSkel::ZUpToViewport(sx, sy, sz));
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
    mesh.formatName = (version == 6)
        ? L"MOHAA Skeletal Mesh (SKD v6)"
        : L"MOHAA Skeletal Mesh (SKD v5)";
    return true;
}

inline bool ParseMohaaSkd(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    (void)fullPath;
    return ParseMohaaSkdWithBones(data, size, nullptr, mesh);
}

} // namespace Formats
