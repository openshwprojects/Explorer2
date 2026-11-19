#pragma once
// ValveSourceMdl.h — Valve GoldSrc & Source Engine StudioMDL ("IDST" .mdl + .vvd/.vtx) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailValveMdl200 {

struct BoneMat3x4 {
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };
};

inline BoneMat3x4 ConcatBoneMat(const BoneMat3x4& a, const BoneMat3x4& b) {
    BoneMat3x4 out = {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c];
        }
        out.m[r][3] = a.m[r][0] * b.m[0][3] + a.m[r][1] * b.m[1][3] + a.m[r][2] * b.m[2][3] + a.m[r][3];
    }
    return out;
}

inline BoneMat3x4 GoldSrcEulerPosToMat(float px, float py, float pz, float rx, float ry, float rz) {
    float sr = std::sin(rx * 0.5f), cr = std::cos(rx * 0.5f);
    float sp = std::sin(ry * 0.5f), cp = std::cos(ry * 0.5f);
    float sy = std::sin(rz * 0.5f), cy = std::cos(rz * 0.5f);

    float qx = sr * cp * cy - cr * sp * sy;
    float qy = cr * sp * cy + sr * cp * sy;
    float qz = cr * cp * sy - sr * sp * cy;
    float qw = cr * cp * cy + sr * sp * sy;

    BoneMat3x4 loc = {};
    loc.m[0][0] = 1.0f - 2.0f * (qy * qy + qz * qz);
    loc.m[0][1] = 2.0f * (qx * qy - qw * qz);
    loc.m[0][2] = 2.0f * (qx * qz + qw * qy);
    loc.m[0][3] = px;

    loc.m[1][0] = 2.0f * (qx * qy + qw * qz);
    loc.m[1][1] = 1.0f - 2.0f * (qx * qx + qz * qz);
    loc.m[1][2] = 2.0f * (qy * qz - qw * qx);
    loc.m[1][3] = py;

    loc.m[2][0] = 2.0f * (qx * qz - qw * qy);
    loc.m[2][1] = 2.0f * (qy * qz + qw * qx);
    loc.m[2][2] = 1.0f - 2.0f * (qx * qx + qy * qy);
    loc.m[2][3] = pz;
    return loc;
}

inline bool ReadSiblingBytes(const std::wstring& path, std::vector<uint8_t>& out, uint64_t maxCap = 64ULL * 1024ULL * 1024ULL) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || static_cast<uint64_t>(li.QuadPart) > maxCap) {
        CloseHandle(h);
        return false;
    }
    size_t sz = static_cast<size_t>(li.QuadPart);
    out.resize(sz);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(sz), &rd, nullptr);
    CloseHandle(h);
    if (!ok || rd != static_cast<DWORD>(sz)) {
        out.clear();
        return false;
    }
    return true;
}

using ::Explorer2::ByteIO::RdI32LE;

inline bool AssembleSourceVvdVtxMesh(
    const uint8_t* mdl, size_t mdlSize,
    const uint8_t* vvd, size_t vvdSize,
    const uint8_t* vtx, size_t vtxSize,
    uint32_t ver,
    LoadedMesh3D& mesh)
{
    if (mdlSize < 244 || vvdSize < 64 || vtxSize < 36) return false;
    uint32_t mdlChecksum = Detail3D200::RdU32LE(mdl + 8);

    // 1. Validate & decode .vvd (vertexFileHeader_t: "IDSV", version == 4)
    if (std::memcmp(vvd, "IDSV", 4) != 0) return false;
    if (RdI32LE(vvd + 4) != 4) return false;
    uint32_t vvdChecksum = Detail3D200::RdU32LE(vvd + 8);
    if (vvdChecksum != mdlChecksum) return false;

    int32_t numLODs       = RdI32LE(vvd + 12);
    int32_t numLOD0Verts  = RdI32LE(vvd + 16);
    int32_t numFixups     = RdI32LE(vvd + 48);
    int32_t fixupOff      = RdI32LE(vvd + 52);
    int32_t vertDataOff   = RdI32LE(vvd + 56);
    if (numLODs < 1 || numLODs > 8 || numLOD0Verts < 3 || numLOD0Verts > 2000000) return false;
    if (vertDataOff < 64 || static_cast<size_t>(vertDataOff) > vvdSize) return false;

    std::vector<Vec3f> lod0Verts;
    lod0Verts.reserve(static_cast<size_t>(numLOD0Verts));

    auto readVvdVertAt = [&](size_t idx) -> Vec3f {
        const uint8_t* vp = vvd + static_cast<size_t>(vertDataOff) + idx * 48;
        float x = Detail3D200::RdF32LE(vp + 16);
        float y = Detail3D200::RdF32LE(vp + 20);
        float z = Detail3D200::RdF32LE(vp + 24);
        return { x, z, -y };
    };

    if (numFixups == 0) {
        if (static_cast<uint64_t>(vertDataOff) + static_cast<uint64_t>(numLOD0Verts) * 48ULL > vvdSize) return false;
        for (int32_t i = 0; i < numLOD0Verts; ++i) {
            lod0Verts.push_back(readVvdVertAt(static_cast<size_t>(i)));
        }
    } else {
        if (numFixups < 0 || numFixups > 65536 || fixupOff < 64 ||
            static_cast<uint64_t>(fixupOff) + static_cast<uint64_t>(numFixups) * 12ULL > vvdSize)
        {
            return false;
        }
        for (int32_t f = 0; f < numFixups; ++f) {
            const uint8_t* fp = vvd + static_cast<size_t>(fixupOff) + static_cast<size_t>(f) * 12;
            int32_t lod     = RdI32LE(fp + 0);
            int32_t srcVert = RdI32LE(fp + 4);
            int32_t cnt     = RdI32LE(fp + 8);
            if (srcVert < 0 || cnt < 0 || cnt > 2000000) return false;
            if (static_cast<uint64_t>(vertDataOff) + (static_cast<uint64_t>(srcVert) + static_cast<uint64_t>(cnt)) * 48ULL > vvdSize) {
                return false;
            }
            if (lod >= 0) {
                if (lod0Verts.size() + static_cast<size_t>(cnt) > 2000000) return false;
                for (int32_t k = 0; k < cnt; ++k) {
                    lod0Verts.push_back(readVvdVertAt(static_cast<size_t>(srcVert + k)));
                }
            }
        }
    }
    if (lod0Verts.size() < 3) return false;

    // 2. Validate .vtx (OptimizedModel::FileHeader_t: version == 7, 36 bytes)
    if (RdI32LE(vtx + 0) != 7) return false;
    uint32_t vtxChecksum = Detail3D200::RdU32LE(vtx + 16);
    if (vtxChecksum != mdlChecksum) return false;

    int32_t vtxNumLODs  = RdI32LE(vtx + 20);
    int32_t vtxNumBp    = RdI32LE(vtx + 28);
    int32_t vtxBpOff    = RdI32LE(vtx + 32);
    int32_t mdlNumBp    = RdI32LE(mdl + 232);
    int32_t mdlBpOff    = RdI32LE(mdl + 236);
    if (vtxNumLODs < 1 || vtxNumLODs > 8) return false;
    if (mdlNumBp <= 0 || mdlNumBp > 256 || vtxNumBp != mdlNumBp) return false;
    if (mdlBpOff < 240 || static_cast<uint64_t>(mdlBpOff) + static_cast<uint64_t>(mdlNumBp) * 16ULL > mdlSize) return false;
    if (vtxBpOff < 36  || static_cast<uint64_t>(vtxBpOff) + static_cast<uint64_t>(vtxNumBp) * 8ULL  > vtxSize) return false;

    auto checkedRel = [](size_t base, int32_t rel, size_t limit, size_t& out) -> bool {
        int64_t v = static_cast<int64_t>(base) + static_cast<int64_t>(rel);
        if (v < 0 || static_cast<uint64_t>(v) > limit) return false;
        out = static_cast<size_t>(v);
        return true;
    };

    mesh.vertices = std::move(lod0Verts);
    const size_t totalVvdVerts = mesh.vertices.size();

    for (int32_t bp = 0; bp < mdlNumBp; ++bp) {
        size_t mdlBpPos = static_cast<size_t>(mdlBpOff) + static_cast<size_t>(bp) * 16;
        size_t vtxBpPos = static_cast<size_t>(vtxBpOff) + static_cast<size_t>(bp) * 8;

        int32_t mdlNumModels = RdI32LE(mdl + mdlBpPos + 4);
        int32_t mdlModelRel  = RdI32LE(mdl + mdlBpPos + 12);
        int32_t vtxNumModels = RdI32LE(vtx + vtxBpPos + 0);
        int32_t vtxModelRel  = RdI32LE(vtx + vtxBpPos + 4);
        if (mdlNumModels < 0 || mdlNumModels > 256 || vtxNumModels != mdlNumModels) {
            mesh.vertices.clear(); mesh.tris.clear();
            return false;
        }
        if (mdlNumModels == 0) continue;

        size_t mdlModelsBase = 0, vtxModelsBase = 0;
        if (!checkedRel(mdlBpPos, mdlModelRel, mdlSize, mdlModelsBase) ||
            mdlModelsBase + static_cast<uint64_t>(mdlNumModels) * 148ULL > mdlSize ||
            !checkedRel(vtxBpPos, vtxModelRel, vtxSize, vtxModelsBase) ||
            vtxModelsBase + static_cast<uint64_t>(vtxNumModels) * 8ULL > vtxSize)
        {
            mesh.vertices.clear(); mesh.tris.clear();
            return false;
        }

        for (int32_t mi = 0; mi < mdlNumModels; ++mi) {
            size_t mdlMdlPos = mdlModelsBase + static_cast<size_t>(mi) * 148;
            size_t vtxMdlPos = vtxModelsBase + static_cast<size_t>(mi) * 8;

            int32_t numMeshes   = RdI32LE(mdl + mdlMdlPos + 72);
            int32_t mdlMeshRel  = RdI32LE(mdl + mdlMdlPos + 76);
            int32_t mdlNumVerts = RdI32LE(mdl + mdlMdlPos + 80);
            int32_t mdlVertIdx  = RdI32LE(mdl + mdlMdlPos + 84);
            if (numMeshes <= 0 || mdlNumVerts <= 0) continue;
            if (numMeshes > 1024 || mdlVertIdx < 0) {
                mesh.vertices.clear(); mesh.tris.clear();
                return false;
            }

            size_t modelBaseVert = ((mdlVertIdx % 48) == 0)
                ? static_cast<size_t>(mdlVertIdx / 48)
                : static_cast<size_t>(mdlVertIdx);

            int32_t mNumLODs  = RdI32LE(vtx + vtxMdlPos + 0);
            int32_t mLodRel   = RdI32LE(vtx + vtxMdlPos + 4);
            size_t vtxLod0Pos = 0;
            if (mNumLODs < 1 || !checkedRel(vtxMdlPos, mLodRel, vtxSize, vtxLod0Pos) || vtxLod0Pos + 12 > vtxSize) {
                mesh.vertices.clear(); mesh.tris.clear();
                return false;
            }

            int32_t vtxNumMeshes = RdI32LE(vtx + vtxLod0Pos + 0);
            int32_t vtxMeshRel   = RdI32LE(vtx + vtxLod0Pos + 4);
            size_t mdlMeshesBase = 0, vtxMeshesBase = 0;
            if (vtxNumMeshes != numMeshes ||
                !checkedRel(mdlMdlPos, mdlMeshRel, mdlSize, mdlMeshesBase) ||
                mdlMeshesBase + static_cast<uint64_t>(numMeshes) * 116ULL > mdlSize ||
                !checkedRel(vtxLod0Pos, vtxMeshRel, vtxSize, vtxMeshesBase) ||
                vtxMeshesBase + static_cast<uint64_t>(numMeshes) * 9ULL > vtxSize)
            {
                mesh.vertices.clear(); mesh.tris.clear();
                return false;
            }

            for (int32_t m = 0; m < numMeshes; ++m) {
                size_t mdlMeshPos = mdlMeshesBase + static_cast<size_t>(m) * 116;
                size_t vtxMeshPos = vtxMeshesBase + static_cast<size_t>(m) * 9;

                int32_t meshNumVerts   = RdI32LE(mdl + mdlMeshPos + 8);
                int32_t meshVertOffset = RdI32LE(mdl + mdlMeshPos + 12);
                int32_t numStripGroups = RdI32LE(vtx + vtxMeshPos + 0);
                int32_t sgHeaderRel    = RdI32LE(vtx + vtxMeshPos + 4);
                if (meshNumVerts <= 0 || meshVertOffset < 0 || numStripGroups < 0 || numStripGroups > 4096) {
                    mesh.vertices.clear(); mesh.tris.clear();
                    return false;
                }
                if (numStripGroups == 0) continue;

                size_t sgBase = 0;
                if (!checkedRel(vtxMeshPos, sgHeaderRel, vtxSize, sgBase) || sgBase + 25 > vtxSize) {
                    mesh.vertices.clear(); mesh.tris.clear();
                    return false;
                }

                auto validateSgStride = [&](size_t stride) -> bool {
                    if (sgBase + static_cast<uint64_t>(numStripGroups) * stride > vtxSize) return false;
                    for (int32_t sg = 0; sg < numStripGroups; ++sg) {
                        size_t p = sgBase + static_cast<size_t>(sg) * stride;
                        int32_t nv = RdI32LE(vtx + p + 0), vo = RdI32LE(vtx + p + 4);
                        int32_t ni = RdI32LE(vtx + p + 8), io = RdI32LE(vtx + p + 12);
                        int32_t ns = RdI32LE(vtx + p + 16), so = RdI32LE(vtx + p + 20);
                        if (nv <= 0 || ni < 3 || ns <= 0 || nv > 500000 || ni > 2000000 || ns > 65536) return false;
                        size_t va = 0, ia = 0, sa = 0;
                        if (!checkedRel(p, vo, vtxSize, va) || va + static_cast<uint64_t>(nv) * 9ULL > vtxSize) return false;
                        if (!checkedRel(p, io, vtxSize, ia) || ia + static_cast<uint64_t>(ni) * 2ULL > vtxSize) return false;
                        if (!checkedRel(p, so, vtxSize, sa) || sa + static_cast<uint64_t>(ns) * 27ULL > vtxSize) return false;
                    }
                    return true;
                };

                int32_t sg0Vo = RdI32LE(vtx + sgBase + 4);
                int32_t sg0Io = RdI32LE(vtx + sgBase + 12);
                int32_t sg0So = RdI32LE(vtx + sgBase + 20);
                int32_t minSubOff = std::min({ sg0Vo, sg0Io, sg0So });
                size_t sgStride = 25;
                if (ver >= 49 && minSubOff >= numStripGroups * 33 && validateSgStride(33)) {
                    sgStride = 33;
                } else if (!validateSgStride(25)) {
                    if (validateSgStride(33)) sgStride = 33;
                    else { mesh.vertices.clear(); mesh.tris.clear(); return false; }
                }

                for (int32_t sg = 0; sg < numStripGroups; ++sg) {
                    size_t sgPos = sgBase + static_cast<size_t>(sg) * sgStride;
                    int32_t sgNumVerts  = RdI32LE(vtx + sgPos + 0);
                    int32_t sgVertRel   = RdI32LE(vtx + sgPos + 4);
                    int32_t sgNumIdx    = RdI32LE(vtx + sgPos + 8);
                    int32_t sgIdxRel    = RdI32LE(vtx + sgPos + 12);
                    int32_t sgNumStrips = RdI32LE(vtx + sgPos + 16);
                    int32_t sgStripRel  = RdI32LE(vtx + sgPos + 20);

                    size_t sgVertBase = sgPos + static_cast<size_t>(sgVertRel);
                    size_t sgIdxBase  = sgPos + static_cast<size_t>(sgIdxRel);
                    size_t sgStrBase  = sgPos + static_cast<size_t>(sgStripRel);

                    auto validateStripStride = [&](size_t stStride) -> bool {
                        if (sgStrBase + static_cast<uint64_t>(sgNumStrips) * stStride > vtxSize) return false;
                        for (int32_t st = 0; st < sgNumStrips; ++st) {
                            size_t sp = sgStrBase + static_cast<size_t>(st) * stStride;
                            int32_t sni = RdI32LE(vtx + sp + 0);
                            int32_t sio = RdI32LE(vtx + sp + 4);
                            uint8_t flg = vtx[sp + 18];
                            if (sni < 0 || sio < 0 || sio + sni > sgNumIdx) return false;
                            if (sni > 0 && (flg & 0x03u) == 0) return false;
                        }
                        return true;
                    };

                    size_t stripStride = 27;
                    if (sgStride == 33 && validateStripStride(35)) {
                        stripStride = 35;
                    } else if (!validateStripStride(27)) {
                        if (validateStripStride(35)) stripStride = 35;
                        else { mesh.vertices.clear(); mesh.tris.clear(); return false; }
                    }

                    auto resolveStripIdx = [&](int32_t elemIdx, uint32_t& outVvdIdx) -> bool {
                        uint16_t sgVertIdx = Detail3D200::RdU16LE(vtx + sgIdxBase + static_cast<size_t>(elemIdx) * 2);
                        if (sgVertIdx >= static_cast<uint32_t>(sgNumVerts)) return false;
                        uint16_t origVert = Detail3D200::RdU16LE(vtx + sgVertBase + static_cast<size_t>(sgVertIdx) * 9 + 4);
                        if (origVert >= static_cast<uint32_t>(meshNumVerts)) return false;
                        size_t finalIdx = modelBaseVert + static_cast<size_t>(meshVertOffset) + static_cast<size_t>(origVert);
                        if (finalIdx >= totalVvdVerts) return false;
                        outVvdIdx = static_cast<uint32_t>(finalIdx);
                        return true;
                    };

                    for (int32_t st = 0; st < sgNumStrips; ++st) {
                        size_t sp = sgStrBase + static_cast<size_t>(st) * stripStride;
                        int32_t stNumIdx   = RdI32LE(vtx + sp + 0);
                        int32_t stIdxStart = RdI32LE(vtx + sp + 4);
                        uint8_t stFlags    = vtx[sp + 18];

                        if ((stFlags & 0x01u) != 0) { // STRIP_IS_TRILIST
                            for (int32_t k = 0; k + 2 < stNumIdx; k += 3) {
                                uint32_t i0 = 0, i1 = 0, i2 = 0;
                                if (!resolveStripIdx(stIdxStart + k + 0, i0) ||
                                    !resolveStripIdx(stIdxStart + k + 1, i1) ||
                                    !resolveStripIdx(stIdxStart + k + 2, i2))
                                {
                                    mesh.vertices.clear(); mesh.tris.clear();
                                    return false;
                                }
                                if (i0 != i1 && i1 != i2 && i0 != i2) {
                                    mesh.tris.push_back({ i0, i2, i1, {} });
                                }
                            }
                        } else if ((stFlags & 0x02u) != 0) { // STRIP_IS_TRISTRIP
                            for (int32_t k = 0; k + 2 < stNumIdx; ++k) {
                                uint32_t i0 = 0, i1 = 0, i2 = 0;
                                if (!resolveStripIdx(stIdxStart + k + 0, i0) ||
                                    !resolveStripIdx(stIdxStart + k + 1, i1) ||
                                    !resolveStripIdx(stIdxStart + k + 2, i2))
                                {
                                    mesh.vertices.clear(); mesh.tris.clear();
                                    return false;
                                }
                                if (i0 != i1 && i1 != i2 && i0 != i2) {
                                    if (k & 1) mesh.tris.push_back({ i0, i1, i2, {} });
                                    else       mesh.tris.push_back({ i0, i2, i1, {} });
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        mesh.vertices.clear();
        mesh.tris.clear();
        return false;
    }
    mesh.formatName = L"Valve Source Engine Model v" + std::to_wstring(ver) + L" (.mdl+.vvd+.vtx)";
    return true;
}

} // namespace DetailValveMdl200

inline bool ParseValveSourceMdl(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // Valve GoldSrc (v10) / Source Engine (v44–v49) StudioMDL: "IDST"
    if (size >= 244 && std::memcmp(data, "IDST", 4) == 0) {
        uint32_t ver = Detail3D200::RdU32LE(data + 4);
        mesh.formatName = L"Valve StudioMDL v" + std::to_wstring(ver) + L" (.mdl)";

        if (ver == 10) {
            uint32_t hdrLen = Detail3D200::RdU32LE(data + 72);
            if (hdrLen > 0 && (hdrLen < 244 || hdrLen > size)) return false;

            // Decode GoldSrc mstudiobone_t[numBones] (112 bytes each at boneIndex)
            uint32_t numBones = Detail3D200::RdU32LE(data + 140);
            uint32_t boneIdx  = Detail3D200::RdU32LE(data + 144);
            std::vector<DetailValveMdl200::BoneMat3x4> worldBones;
            if (numBones > 0) {
                if (numBones > 256 || boneIdx < 244 || boneIdx + static_cast<uint64_t>(numBones) * 112ULL > size) {
                    return false;
                }
                std::vector<DetailValveMdl200::BoneMat3x4> localBones(numBones);
                std::vector<int32_t> parents(numBones, -1);
                for (uint32_t b = 0; b < numBones; ++b) {
                    const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 112;
                    int32_t parent = DetailValveMdl200::RdI32LE(bp + 32);
                    if (parent < -1 || parent >= static_cast<int32_t>(numBones) || parent == static_cast<int32_t>(b)) {
                        return false;
                    }
                    parents[b] = parent;
                    float px = Detail3D200::RdF32LE(bp + 64);
                    float py = Detail3D200::RdF32LE(bp + 68);
                    float pz = Detail3D200::RdF32LE(bp + 72);
                    float rx = Detail3D200::RdF32LE(bp + 76);
                    float ry = Detail3D200::RdF32LE(bp + 80);
                    float rz = Detail3D200::RdF32LE(bp + 84);
                    localBones[b] = DetailValveMdl200::GoldSrcEulerPosToMat(px, py, pz, rx, ry, rz);
                }
                worldBones.resize(numBones);
                std::vector<uint8_t> state(numBones, 0); // 0=unvisited, 1=visiting, 2=done
                auto solveWorld = [&](auto& self, uint32_t b) -> bool {
                    if (state[b] == 2) return true;
                    if (state[b] == 1) return false; // cycle
                    state[b] = 1;
                    int32_t p = parents[b];
                    if (p >= 0) {
                        if (!self(self, static_cast<uint32_t>(p))) return false;
                        worldBones[b] = DetailValveMdl200::ConcatBoneMat(worldBones[static_cast<size_t>(p)], localBones[b]);
                    } else {
                        worldBones[b] = localBones[b];
                    }
                    state[b] = 2;
                    return true;
                };
                for (uint32_t b = 0; b < numBones; ++b) {
                    if (!solveWorld(solveWorld, b)) return false;
                }
            }

            uint32_t numBodyparts = Detail3D200::RdU32LE(data + 204);
            uint32_t bodypartIdx  = Detail3D200::RdU32LE(data + 208);
            if (numBodyparts == 0 || numBodyparts > 64 || bodypartIdx + static_cast<uint64_t>(numBodyparts) * 76ULL > size) {
                return false;
            }
            for (uint32_t b = 0; b < numBodyparts; ++b) {
                const uint8_t* bp = data + bodypartIdx + static_cast<size_t>(b) * 76;
                uint32_t numModels = Detail3D200::RdU32LE(bp + 64);
                uint32_t modelIdx  = Detail3D200::RdU32LE(bp + 72);
                for (uint32_t mi = 0; mi < numModels && mi < 32 && modelIdx + static_cast<uint64_t>(mi + 1) * 112ULL <= size; ++mi) {
                    const uint8_t* mdl = data + modelIdx + static_cast<size_t>(mi) * 112;
                    uint32_t numMesh     = Detail3D200::RdU32LE(mdl + 72);
                    uint32_t meshIdx     = Detail3D200::RdU32LE(mdl + 76);
                    uint32_t numVerts    = Detail3D200::RdU32LE(mdl + 80);
                    uint32_t vertInfoIdx = Detail3D200::RdU32LE(mdl + 84);
                    uint32_t vertIdx     = Detail3D200::RdU32LE(mdl + 88);
                    if (numVerts > 0 && numVerts <= 65536 && vertIdx + static_cast<uint64_t>(numVerts) * 12ULL <= size) {
                        bool hasVertBones = (!worldBones.empty() && vertInfoIdx > 0 &&
                                             vertInfoIdx + static_cast<uint64_t>(numVerts) <= size);
                        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                        for (uint32_t v = 0; v < numVerts; ++v) {
                            const uint8_t* vp = data + vertIdx + static_cast<size_t>(v) * 12;
                            float lx = Detail3D200::RdF32LE(vp + 0);
                            float ly = Detail3D200::RdF32LE(vp + 4);
                            float lz = Detail3D200::RdF32LE(vp + 8);
                            if (hasVertBones) {
                                uint8_t bId = data[vertInfoIdx + v];
                                if (bId < worldBones.size()) {
                                    const auto& wb = worldBones[bId];
                                    float wx = wb.m[0][0] * lx + wb.m[0][1] * ly + wb.m[0][2] * lz + wb.m[0][3];
                                    float wy = wb.m[1][0] * lx + wb.m[1][1] * ly + wb.m[1][2] * lz + wb.m[1][3];
                                    float wz = wb.m[2][0] * lx + wb.m[2][1] * ly + wb.m[2][2] * lz + wb.m[2][3];
                                    lx = wx; ly = wy; lz = wz;
                                }
                            }
                            mesh.vertices.push_back({ lx, lz, -ly });
                        }
                        for (uint32_t m = 0; m < numMesh && meshIdx + static_cast<uint64_t>(m + 1) * 20ULL <= size; ++m) {
                            uint32_t triIdx = Detail3D200::RdU32LE(data + meshIdx + static_cast<size_t>(m) * 20 + 4);
                            size_t tp = triIdx;
                            while (tp + 2 <= size) {
                                int16_t cmd = static_cast<int16_t>(Detail3D200::RdU16LE(data + tp)); tp += 2;
                                if (cmd == 0) break;
                                bool isFan = (cmd < 0);
                                int cnt = std::abs(static_cast<int>(cmd));
                                if (tp + static_cast<size_t>(cnt) * 8 > size) break;
                                std::vector<uint32_t> strip(static_cast<size_t>(cnt));
                                for (int k = 0; k < cnt; ++k) {
                                    strip[static_cast<size_t>(k)] = vBase + Detail3D200::RdU16LE(data + tp + static_cast<size_t>(k) * 8);
                                }
                                tp += static_cast<size_t>(cnt) * 8;
                                for (int k = 0; k + 2 < cnt; ++k) {
                                    uint32_t a = strip[static_cast<size_t>(isFan ? 0 : k)];
                                    uint32_t bIdx = strip[static_cast<size_t>(k + 1)];
                                    uint32_t c = strip[static_cast<size_t>(k + 2)];
                                    if (a < mesh.vertices.size() && bIdx < mesh.vertices.size() && c < mesh.vertices.size() &&
                                        a != bIdx && bIdx != c && a != c)
                                    {
                                        if (!isFan && (k & 1)) mesh.tris.push_back({ bIdx, a, c, {} });
                                        else                   mesh.tris.push_back({ a, bIdx, c, {} });
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return !mesh.vertices.empty() && !mesh.tris.empty();
        }

        // Source Engine v44–v49: full .mdl + .vvd + .vtx triangle mesh assembly, with skeleton fallback ONLY when siblings absent
        if (ver >= 44 && ver <= 49) {
            uint32_t hdrLen = Detail3D200::RdU32LE(data + 76);
            if (hdrLen < 244 || hdrLen > size) return false;

            if (!mesh.filePath.empty()) {
                size_t lastSep = mesh.filePath.find_last_of(L"\\/");
                size_t lastDot = mesh.filePath.find_last_of(L'.');
                std::wstring stem = (lastDot != std::wstring::npos && (lastSep == std::wstring::npos || lastDot > lastSep))
                    ? mesh.filePath.substr(0, lastDot)
                    : mesh.filePath;

                std::wstring vvdPath = stem + L".vvd";
                bool hasVvdOnDisk = (GetFileAttributesW(vvdPath.c_str()) != INVALID_FILE_ATTRIBUTES);

                static constexpr const wchar_t* kVtxSuffixes[] = {
                    L".dx90.vtx", L".dx80.vtx", L".sw.vtx", L".vtx"
                };
                std::wstring vtxPath;
                for (const wchar_t* sfx : kVtxSuffixes) {
                    std::wstring cand = stem + sfx;
                    if (GetFileAttributesW(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        vtxPath = std::move(cand);
                        break;
                    }
                }
                bool hasVtxOnDisk = !vtxPath.empty();

                if (hasVvdOnDisk && hasVtxOnDisk) {
                    std::vector<uint8_t> vvdBytes, vtxBytes;
                    if (!DetailValveMdl200::ReadSiblingBytes(vvdPath, vvdBytes) ||
                        !DetailValveMdl200::ReadSiblingBytes(vtxPath, vtxBytes))
                    {
                        return false;
                    }
                    return DetailValveMdl200::AssembleSourceVvdVtxMesh(
                        data, size, vvdBytes.data(), vvdBytes.size(), vtxBytes.data(), vtxBytes.size(), ver, mesh);
                }
                // If one sibling exists on disk and is corrupt, reject rather than hiding corruption
                if (hasVvdOnDisk) {
                    std::vector<uint8_t> vvdBytes;
                    if (!DetailValveMdl200::ReadSiblingBytes(vvdPath, vvdBytes) || vvdBytes.size() < 64 ||
                        std::memcmp(vvdBytes.data(), "IDSV", 4) != 0 ||
                        DetailValveMdl200::RdI32LE(vvdBytes.data() + 4) != 4 ||
                        Detail3D200::RdU32LE(vvdBytes.data() + 8) != Detail3D200::RdU32LE(data + 8))
                    {
                        return false;
                    }
                }
                if (hasVtxOnDisk) {
                    std::vector<uint8_t> vtxBytes;
                    if (!DetailValveMdl200::ReadSiblingBytes(vtxPath, vtxBytes) || vtxBytes.size() < 36 ||
                        DetailValveMdl200::RdI32LE(vtxBytes.data()) != 7 ||
                        Detail3D200::RdU32LE(vtxBytes.data() + 16) != Detail3D200::RdU32LE(data + 8))
                    {
                        return false;
                    }
                }
            }

            // Fallback when sibling .vvd/.vtx files are absent: decode mstudiobone_t skeleton hierarchy
            if (size >= 400) {
                uint32_t numBones = Detail3D200::RdU32LE(data + 156);
                uint32_t boneIdx  = Detail3D200::RdU32LE(data + 160);
                if (numBones >= 2 && numBones <= 512 && boneIdx >= 244 &&
                    boneIdx + static_cast<uint64_t>(numBones) * 216ULL <= size)
                {
                    std::vector<DetailValveMdl200::BoneMat3x4> world(numBones);
                    std::vector<int32_t> parents(numBones, -1);
                    std::vector<Vec3f> joints(numBones);
                    for (uint32_t b = 0; b < numBones; ++b) {
                        const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 216;
                        int32_t parent = DetailValveMdl200::RdI32LE(bp + 4);
                        if (parent < -1 || parent >= static_cast<int32_t>(numBones) || parent == static_cast<int32_t>(b)) {
                            return false;
                        }
                        parents[b] = parent;
                        float px = Detail3D200::RdF32LE(bp + 32);
                        float py = Detail3D200::RdF32LE(bp + 36);
                        float pz = Detail3D200::RdF32LE(bp + 40);
                        float qx = Detail3D200::RdF32LE(bp + 44);
                        float qy = Detail3D200::RdF32LE(bp + 48);
                        float qz = Detail3D200::RdF32LE(bp + 52);
                        float qw = Detail3D200::RdF32LE(bp + 56);

                        DetailValveMdl200::BoneMat3x4 loc = {};
                        loc.m[0][0] = 1.0f - 2.0f * (qy * qy + qz * qz);
                        loc.m[0][1] = 2.0f * (qx * qy - qw * qz);
                        loc.m[0][2] = 2.0f * (qx * qz + qw * qy);
                        loc.m[0][3] = px;
                        loc.m[1][0] = 2.0f * (qx * qy + qw * qz);
                        loc.m[1][1] = 1.0f - 2.0f * (qx * qx + qz * qz);
                        loc.m[1][2] = 2.0f * (qy * qz - qw * qx);
                        loc.m[1][3] = py;
                        loc.m[2][0] = 2.0f * (qx * qz - qw * qy);
                        loc.m[2][1] = 2.0f * (qy * qz + qw * qx);
                        loc.m[2][2] = 1.0f - 2.0f * (qx * qx + qy * qy);
                        loc.m[2][3] = pz;

                        if (parent >= 0 && static_cast<uint32_t>(parent) < b) {
                            world[b] = DetailValveMdl200::ConcatBoneMat(world[static_cast<size_t>(parent)], loc);
                        } else {
                            world[b] = loc;
                        }
                        joints[b] = { world[b].m[0][3], world[b].m[2][3], -world[b].m[1][3] };
                    }
                    float span = Detail3D200::ComputePointCloudSpan(joints);
                    if (span > 1e-3f) {
                        float r = span * 0.015f;
                        for (uint32_t b = 0; b < numBones; ++b) {
                            Detail3D200::AddOctaSphere(mesh, joints[b].x, joints[b].y, joints[b].z, r * 1.3f);
                            if (parents[b] >= 0 && static_cast<uint32_t>(parents[b]) < numBones) {
                                Detail3D200::AddStrut3D(mesh, joints[static_cast<size_t>(parents[b])], joints[b], r * 0.7f);
                            }
                        }
                    }
                }
            }
            return !mesh.vertices.empty() && !mesh.tris.empty();
        }
        return false;
    }
    return false;
}

} // namespace Formats
