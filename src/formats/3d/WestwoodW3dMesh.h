#pragma once
// WestwoodW3dMesh.h — Westwood Studios / EA SAGE 3D Model (.w3d) Reader
// Parses W3D_CHUNK_HIERARCHY (0x100 -> PIVOTS 0x102 parent-chain FK), W3D_CHUNK_HLOD (0x700 ->
// SUB_OBJECT 0x703 bone attachments), and W3D_CHUNK_MESH (0x000 -> MESH_HEADER3 0x1F, VERTICES 0x02,
// VERTEX_INFLUENCES 0x0E skinning, TRIANGLES 0x20 32-byte records). Also delegates HSF "(W3D V..." streams.
#include "../../ByteIO.h"
#include "../../ReaderTextUtils.h"
#include "../common/MeshBuild3D.h"
#include "../cad/AutodeskDwfCad.h"

namespace Formats {

namespace DetailWestwoodW3d {

struct W3dMat3x4 {
    float m[3][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 }
    };

    static W3dMat3x4 MakeQuatPos(float qx, float qy, float qz, float qw, float tx, float ty, float tz) {
        W3dMat3x4 r{};
        float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (n > 1e-6f) { qx /= n; qy /= n; qz /= n; qw /= n; }
        else { qx = qy = qz = 0.0f; qw = 1.0f; }

        float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        float wx = qw * qx, wy = qw * qy, wz = qw * qz;
        r.m[0][0] = 1.0f - 2.0f * (yy + zz);
        r.m[0][1] = 2.0f * (xy - wz);
        r.m[0][2] = 2.0f * (xz + wy);
        r.m[0][3] = tx;
        r.m[1][0] = 2.0f * (xy + wz);
        r.m[1][1] = 1.0f - 2.0f * (xx + zz);
        r.m[1][2] = 2.0f * (yz - wx);
        r.m[1][3] = ty;
        r.m[2][0] = 2.0f * (xz - wy);
        r.m[2][1] = 2.0f * (yz + wx);
        r.m[2][2] = 1.0f - 2.0f * (xx + yy);
        r.m[2][3] = tz;
        return r;
    }

    static W3dMat3x4 Multiply(const W3dMat3x4& a, const W3dMat3x4& b) {
        W3dMat3x4 r{};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
            }
            r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3] + a.m[i][2] * b.m[2][3] + a.m[i][3];
        }
        return r;
    }

    Vec3f Transform(float x, float y, float z) const {
        return {
            m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
            m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
            m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3]
        };
    }
};

struct W3dSubMesh {
    std::string fullNameLower;
    std::string shortNameLower;
    uint32_t attributes = 0;
    std::vector<Vec3f> verts;
    struct VertInf {
        uint16_t b0 = 0, b1 = 0;
        float w0 = 1.0f, w1 = 0.0f;
    };
    std::vector<VertInf> influences;
    std::vector<std::array<uint32_t, 3>> tris;
};

inline std::string ReadFixedStr(const uint8_t* p, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && p[n] != 0) ++n;
    return ReaderText::ToLowerAscii(ReaderText::TrimSv(std::string_view(reinterpret_cast<const char*>(p), n)));
}

inline void ParseHlodRecursive(
    const uint8_t* data, size_t start, size_t end,
    std::unordered_map<std::string, uint32_t>& outSubObjBone,
    int depth)
{
    if (depth > 8) return;
    size_t p = start;
    while (p + 8u <= end) {
        uint32_t cType = ByteIO::RdU32LE(data + p + 0);
        uint32_t cRaw  = ByteIO::RdU32LE(data + p + 4);
        uint32_t cSize = cRaw & 0x7FFFFFFFu;
        bool hasSub    = (cRaw & 0x80000000u) != 0u;
        p += 8u;
        if (cSize > end - p) break;

        if (cType == 0x00000703u && cSize >= 36u) { // W3D_CHUNK_HLOD_SUB_OBJECT
            uint32_t boneIdx = ByteIO::RdU32LE(data + p + 0);
            std::string fullName = ReadFixedStr(data + p + 4, 32u);
            if (!fullName.empty()) {
                outSubObjBone[fullName] = boneIdx;
                size_t dot = fullName.rfind('.');
                if (dot != std::string::npos && dot + 1 < fullName.size()) {
                    outSubObjBone.try_emplace(fullName.substr(dot + 1), boneIdx);
                }
            }
        } else if (hasSub || cType == 0x00000702u) { // LOD_ARRAY / AGGREGATE_ARRAY
            ParseHlodRecursive(data, p, p + cSize, outSubObjBone, depth + 1);
        }
        p += cSize;
    }
}

} // namespace DetailWestwoodW3d

inline bool ParseWestwoodW3dMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace DetailWestwoodW3d;
    if (!data || size < 16) return false;

    // Delegate Hoops Stream Format .w3d ("(W3D V00.xx)") to AutodeskDwfCad
    if (DetailDwfW3d::ParseHsfW3dStream(data, size, mesh)) {
        mesh.formatName = L"Hoops 3D Stream Format (.w3d)";
        return true;
    }

    mesh.vertices.clear();
    mesh.tris.clear();

    std::vector<W3dMat3x4> worldPivots;
    std::vector<int32_t> pivotParents;
    std::unordered_map<std::string, uint32_t> hlodSubObjBones;
    std::vector<W3dSubMesh> subMeshes;
    size_t topChunks = 0;

    size_t pos = 0;
    while (pos + 8u <= size) {
        uint32_t topType = ByteIO::RdU32LE(data + pos + 0);
        uint32_t topRaw  = ByteIO::RdU32LE(data + pos + 4);
        uint32_t topSize = topRaw & 0x7FFFFFFFu;
        pos += 8u;
        if (topSize > size - pos) {
            if (topChunks == 0) return false;
            break;
        }
        const size_t chunkEnd = pos + topSize;
        ++topChunks;

        if (topType == 0x00000100u) { // W3D_CHUNK_HIERARCHY
            size_t sp = pos;
            while (sp + 8u <= chunkEnd) {
                uint32_t st = ByteIO::RdU32LE(data + sp + 0);
                uint32_t sz = ByteIO::RdU32LE(data + sp + 4) & 0x7FFFFFFFu;
                sp += 8u;
                if (sz > chunkEnd - sp) break;
                if (st == 0x00000102u && sz >= 60u) { // W3D_CHUNK_PIVOTS (60B per W3dPivotStruct)
                    size_t nPiv = sz / 60u;
                    if (nPiv <= 4096u) {
                        std::vector<W3dMat3x4> local(nPiv);
                        pivotParents.assign(nPiv, -1);
                        for (size_t i = 0; i < nPiv; ++i) {
                            const uint8_t* pp = data + sp + i * 60u;
                            int32_t par = ByteIO::RdI32LE(pp + 16u);
                            float tx = ByteIO::RdF32LE(pp + 20u);
                            float ty = ByteIO::RdF32LE(pp + 24u);
                            float tz = ByteIO::RdF32LE(pp + 28u);
                            float qx = ByteIO::RdF32LE(pp + 44u);
                            float qy = ByteIO::RdF32LE(pp + 48u);
                            float qz = ByteIO::RdF32LE(pp + 52u);
                            float qw = ByteIO::RdF32LE(pp + 56u);
                            if (std::isfinite(tx) && std::isfinite(ty) && std::isfinite(tz) &&
                                std::isfinite(qx) && std::isfinite(qy) && std::isfinite(qz) && std::isfinite(qw))
                            {
                                local[i] = W3dMat3x4::MakeQuatPos(qx, qy, qz, qw, tx, ty, tz);
                            }
                            if (par >= 0 && static_cast<size_t>(par) < nPiv && static_cast<size_t>(par) != i) {
                                pivotParents[i] = par;
                            }
                        }
                        worldPivots.resize(nPiv);
                        std::vector<uint8_t> done(nPiv, 0u);
                        auto resolvePivot = [&](auto& self, size_t idx, int depth) -> const W3dMat3x4& {
                            if (done[idx] || depth > 64) return worldPivots[idx];
                            done[idx] = 1u;
                            if (pivotParents[idx] >= 0) {
                                const W3dMat3x4& pm = self(self, static_cast<size_t>(pivotParents[idx]), depth + 1);
                                worldPivots[idx] = W3dMat3x4::Multiply(pm, local[idx]);
                            } else {
                                worldPivots[idx] = local[idx];
                            }
                            return worldPivots[idx];
                        };
                        for (size_t i = 0; i < nPiv; ++i) resolvePivot(resolvePivot, i, 0);
                    }
                }
                sp += sz;
            }
        } else if (topType == 0x00000700u) { // W3D_CHUNK_HLOD
            ParseHlodRecursive(data, pos, chunkEnd, hlodSubObjBones, 0);
        } else if (topType == 0x00000000u) { // W3D_CHUNK_MESH
            W3dSubMesh sm;
            bool gotHeader = false;
            size_t sp = pos;
            while (sp + 8u <= chunkEnd) {
                uint32_t st = ByteIO::RdU32LE(data + sp + 0);
                uint32_t sz = ByteIO::RdU32LE(data + sp + 4) & 0x7FFFFFFFu;
                sp += 8u;
                if (sz > chunkEnd - sp) break;

                if (st == 0x0000001Fu && sz >= 48u) { // W3D_CHUNK_MESH_HEADER3 (116B standard)
                    gotHeader = true;
                    sm.attributes = ByteIO::RdU32LE(data + sp + 4u);
                    std::string mName = ReadFixedStr(data + sp + 8u, 16u);
                    std::string cName = ReadFixedStr(data + sp + 24u, 16u);
                    sm.shortNameLower = mName;
                    sm.fullNameLower = cName.empty() ? mName : (cName + "." + mName);
                } else if ((st == 0x00000002u || (st == 0x00000C00u && sm.verts.empty())) && sz >= 36u && (sz % 12u) == 0u) {
                    size_t nv = sz / 12u;
                    if (nv <= 1000000u) {
                        sm.verts.resize(nv);
                        bool okV = true;
                        for (size_t i = 0; i < nv; ++i) {
                            float x = ByteIO::RdF32LE(data + sp + i * 12u + 0);
                            float y = ByteIO::RdF32LE(data + sp + i * 12u + 4);
                            float z = ByteIO::RdF32LE(data + sp + i * 12u + 8);
                            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { okV = false; break; }
                            sm.verts[i] = { x, y, z };
                        }
                        if (!okV) sm.verts.clear();
                    }
                } else if (st == 0x0000000Eu && sz >= 8u && (sz % 8u) == 0u) { // W3D_CHUNK_VERTEX_INFLUENCES
                    size_t ni = sz / 8u;
                    if (ni <= 1000000u) {
                        sm.influences.resize(ni);
                        for (size_t i = 0; i < ni; ++i) {
                            const uint8_t* ip = data + sp + i * 8u;
                            uint16_t b0 = ByteIO::RdU16LE(ip + 0);
                            uint16_t b1 = ByteIO::RdU16LE(ip + 2);
                            uint16_t i0 = ByteIO::RdU16LE(ip + 4);
                            uint16_t i1 = ByteIO::RdU16LE(ip + 6);
                            float w0 = (i0 == 0 && i1 == 0) ? 1.0f : (static_cast<float>(i0) * 0.01f);
                            float w1 = static_cast<float>(i1) * 0.01f;
                            float wSum = w0 + w1;
                            if (wSum > 1e-5f) { w0 /= wSum; w1 /= wSum; }
                            else { w0 = 1.0f; w1 = 0.0f; }
                            sm.influences[i] = { b0, b1, w0, w1 };
                        }
                    }
                } else if (st == 0x00000020u && sz >= 32u && (sz % 32u) == 0u) { // W3D_CHUNK_TRIANGLES
                    size_t nt = sz / 32u;
                    if (nt <= 2000000u) {
                        sm.tris.reserve(nt);
                        for (size_t t = 0; t < nt; ++t) {
                            const uint8_t* tp = data + sp + t * 32u;
                            uint32_t i0 = ByteIO::RdU32LE(tp + 0);
                            uint32_t i1 = ByteIO::RdU32LE(tp + 4);
                            uint32_t i2 = ByteIO::RdU32LE(tp + 8);
                            if (i0 != i1 && i1 != i2 && i0 != i2) {
                                sm.tris.push_back({ i0, i1, i2 });
                            }
                        }
                    }
                }
                sp += sz;
            }
            if (gotHeader && !sm.verts.empty() && !sm.tris.empty()) {
                subMeshes.push_back(std::move(sm));
            }
        }
        pos = chunkEnd;
    }

    // Emit sub-meshes with skinning or HLOD bone attachment transforms
    for (const auto& sm : subMeshes) {
        // Skip hidden collision-only AABox/OBBox proxy meshes if visible render meshes exist
        const bool isHiddenCollision = ((sm.attributes & 0x00001000u) != 0u) && (subMeshes.size() > 1u);
        if (isHiddenCollision) continue;

        uint32_t rigidBone = 0xFFFFFFFFu;
        auto itFull = hlodSubObjBones.find(sm.fullNameLower);
        if (itFull != hlodSubObjBones.end()) rigidBone = itFull->second;
        else {
            auto itShort = hlodSubObjBones.find(sm.shortNameLower);
            if (itShort != hlodSubObjBones.end()) rigidBone = itShort->second;
        }

        const uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
        const size_t nv = sm.verts.size();
        for (size_t i = 0; i < nv; ++i) {
            Vec3f p = sm.verts[i];
            if (!sm.influences.empty() && i < sm.influences.size() && !worldPivots.empty()) {
                const auto& inf = sm.influences[i];
                Vec3f p0 = (inf.b0 < worldPivots.size()) ? worldPivots[inf.b0].Transform(p.x, p.y, p.z) : p;
                if (inf.w1 > 1e-4f && inf.b1 < worldPivots.size()) {
                    Vec3f p1 = worldPivots[inf.b1].Transform(p.x, p.y, p.z);
                    p = { p0.x * inf.w0 + p1.x * inf.w1,
                          p0.y * inf.w0 + p1.y * inf.w1,
                          p0.z * inf.w0 + p1.z * inf.w1 };
                } else {
                    p = p0;
                }
            } else if (rigidBone < worldPivots.size()) {
                p = worldPivots[rigidBone].Transform(p.x, p.y, p.z);
            }
            // Westwood Z-up -> Explorer2 Y-up
            mesh.vertices.push_back({ p.x, p.z, -p.y });
        }

        for (const auto& t : sm.tris) {
            if (t[0] < nv && t[1] < nv && t[2] < nv) {
                mesh.tris.push_back({ vb + t[0], vb + t[1], vb + t[2], {} });
            }
        }
    }

    // Skeleton-only W3D hierarchy file (.w3d containing only W3D_CHUNK_HIERARCHY)
    if (mesh.tris.empty() && worldPivots.size() >= 2) {
        for (size_t i = 1; i < worldPivots.size(); ++i) {
            int32_t par = pivotParents[i];
            if (par >= 0 && static_cast<size_t>(par) < worldPivots.size()) {
                Vec3f a = { worldPivots[static_cast<size_t>(par)].m[0][3],
                            worldPivots[static_cast<size_t>(par)].m[2][3],
                           -worldPivots[static_cast<size_t>(par)].m[1][3] };
                Vec3f b = { worldPivots[i].m[0][3], worldPivots[i].m[2][3], -worldPivots[i].m[1][3] };
                MeshBuild3D::AppendOctahedralBonePrism(mesh, a, b, 0.08f);
            }
        }
    }

    (void)fullPath;
    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Westwood / EA SAGE 3D Model (.w3d)";
    return true;
}

} // namespace Formats
