#pragma once
// ValveSourceMdl.h — Valve GoldSrc & Source Engine StudioMDL ("IDST" .mdl + .vvd/.vtx) reader.
#include "../common/MeshFormatCommon.h"
#include "../common/ValveSourceModelCommon.h"

namespace Formats {

namespace DetailValveMdl {

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
    int32_t mdlChecksum = 0;
    uint32_t parsedVer = 0;
    ValveSourceCommon::MdlBodyModelMeshTable mdlTable;
    if (!ValveSourceCommon::ExtractValveMdlMeshSpans(mdl, mdlSize, mdlChecksum, parsedVer, mdlTable)) {
        return false;
    }

    std::vector<Vec3f> lod0Verts;
    int32_t vvdChecksum = 0;
    if (!ValveSourceCommon::DecodeValveVvdLod0(vvd, vvdSize, lod0Verts, vvdChecksum) || vvdChecksum != mdlChecksum) {
        return false;
    }

    std::vector<MeshTriangle> tris;
    if (!ValveSourceCommon::DecodeValveVtxV7Topology(
            vtx, vtxSize, mdlChecksum, lod0Verts.size(), &mdlTable, ver, tris)) {
        return false;
    }

    mesh.vertices = std::move(lod0Verts);
    mesh.tris = std::move(tris);
    mesh.formatName = L"Valve Source Engine Model v" + std::to_wstring(ver) + L" (.mdl+.vvd+.vtx)";
    return true;
}

} // namespace DetailValveMdl

inline bool ParseValveSourceMdl(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    // Valve GoldSrc (v10) / Source Engine (v44–v49) StudioMDL: "IDST"
    if (size >= 244 && std::memcmp(data, "IDST", 4) == 0) {
        uint32_t ver = DetailMeshSciCad::RdU32LE(data + 4);
        mesh.formatName = L"Valve StudioMDL v" + std::to_wstring(ver) + L" (.mdl)";

        if (ver == 10) {
            uint32_t hdrLen = DetailMeshSciCad::RdU32LE(data + 72);
            if (hdrLen > 0 && (hdrLen < 244 || hdrLen > size)) return false;

            // Decode GoldSrc mstudiobone_t[numBones] (112 bytes each at boneIndex)
            uint32_t numBones = DetailMeshSciCad::RdU32LE(data + 140);
            uint32_t boneIdx  = DetailMeshSciCad::RdU32LE(data + 144);
            std::vector<DetailValveMdl::BoneMat3x4> worldBones;
            if (numBones > 0) {
                if (numBones > 256 || boneIdx < 244 || boneIdx + static_cast<uint64_t>(numBones) * 112ULL > size) {
                    return false;
                }
                std::vector<DetailValveMdl::BoneMat3x4> localBones(numBones);
                std::vector<int32_t> parents(numBones, -1);
                for (uint32_t b = 0; b < numBones; ++b) {
                    const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 112;
                    int32_t parent = DetailValveMdl::RdI32LE(bp + 32);
                    if (parent < -1 || parent >= static_cast<int32_t>(numBones) || parent == static_cast<int32_t>(b)) {
                        return false;
                    }
                    parents[b] = parent;
                    float px = DetailMeshSciCad::RdF32LE(bp + 64);
                    float py = DetailMeshSciCad::RdF32LE(bp + 68);
                    float pz = DetailMeshSciCad::RdF32LE(bp + 72);
                    float rx = DetailMeshSciCad::RdF32LE(bp + 76);
                    float ry = DetailMeshSciCad::RdF32LE(bp + 80);
                    float rz = DetailMeshSciCad::RdF32LE(bp + 84);
                    localBones[b] = DetailValveMdl::GoldSrcEulerPosToMat(px, py, pz, rx, ry, rz);
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
                        worldBones[b] = DetailValveMdl::ConcatBoneMat(worldBones[static_cast<size_t>(p)], localBones[b]);
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

            uint32_t numBodyparts = DetailMeshSciCad::RdU32LE(data + 204);
            uint32_t bodypartIdx  = DetailMeshSciCad::RdU32LE(data + 208);
            if (numBodyparts == 0 || numBodyparts > 64 || bodypartIdx + static_cast<uint64_t>(numBodyparts) * 76ULL > size) {
                return false;
            }
            for (uint32_t b = 0; b < numBodyparts; ++b) {
                const uint8_t* bp = data + bodypartIdx + static_cast<size_t>(b) * 76;
                uint32_t numModels = DetailMeshSciCad::RdU32LE(bp + 64);
                uint32_t modelIdx  = DetailMeshSciCad::RdU32LE(bp + 72);
                for (uint32_t mi = 0; mi < numModels && mi < 32 && modelIdx + static_cast<uint64_t>(mi + 1) * 112ULL <= size; ++mi) {
                    const uint8_t* mdl = data + modelIdx + static_cast<size_t>(mi) * 112;
                    uint32_t numMesh     = DetailMeshSciCad::RdU32LE(mdl + 72);
                    uint32_t meshIdx     = DetailMeshSciCad::RdU32LE(mdl + 76);
                    uint32_t numVerts    = DetailMeshSciCad::RdU32LE(mdl + 80);
                    uint32_t vertInfoIdx = DetailMeshSciCad::RdU32LE(mdl + 84);
                    uint32_t vertIdx     = DetailMeshSciCad::RdU32LE(mdl + 88);
                    if (numVerts > 0 && numVerts <= 65536 && vertIdx + static_cast<uint64_t>(numVerts) * 12ULL <= size) {
                        bool hasVertBones = (!worldBones.empty() && vertInfoIdx > 0 &&
                                             vertInfoIdx + static_cast<uint64_t>(numVerts) <= size);
                        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                        for (uint32_t v = 0; v < numVerts; ++v) {
                            const uint8_t* vp = data + vertIdx + static_cast<size_t>(v) * 12;
                            float lx = DetailMeshSciCad::RdF32LE(vp + 0);
                            float ly = DetailMeshSciCad::RdF32LE(vp + 4);
                            float lz = DetailMeshSciCad::RdF32LE(vp + 8);
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
                            uint32_t triIdx = DetailMeshSciCad::RdU32LE(data + meshIdx + static_cast<size_t>(m) * 20 + 4);
                            size_t tp = triIdx;
                            while (tp + 2 <= size) {
                                int16_t cmd = static_cast<int16_t>(DetailMeshSciCad::RdU16LE(data + tp)); tp += 2;
                                if (cmd == 0) break;
                                bool isFan = (cmd < 0);
                                int cnt = std::abs(static_cast<int>(cmd));
                                if (tp + static_cast<size_t>(cnt) * 8 > size) break;
                                std::vector<uint32_t> strip(static_cast<size_t>(cnt));
                                for (int k = 0; k < cnt; ++k) {
                                    strip[static_cast<size_t>(k)] = vBase + DetailMeshSciCad::RdU16LE(data + tp + static_cast<size_t>(k) * 8);
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
            uint32_t hdrLen = DetailMeshSciCad::RdU32LE(data + 76);
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
                    if (!DetailValveMdl::ReadSiblingBytes(vvdPath, vvdBytes) ||
                        !DetailValveMdl::ReadSiblingBytes(vtxPath, vtxBytes))
                    {
                        return false;
                    }
                    return DetailValveMdl::AssembleSourceVvdVtxMesh(
                        data, size, vvdBytes.data(), vvdBytes.size(), vtxBytes.data(), vtxBytes.size(), ver, mesh);
                }
                // If one sibling exists on disk and is corrupt, reject rather than hiding corruption
                if (hasVvdOnDisk) {
                    std::vector<uint8_t> vvdBytes;
                    if (!DetailValveMdl::ReadSiblingBytes(vvdPath, vvdBytes) || vvdBytes.size() < 64 ||
                        std::memcmp(vvdBytes.data(), "IDSV", 4) != 0 ||
                        DetailValveMdl::RdI32LE(vvdBytes.data() + 4) != 4 ||
                        DetailMeshSciCad::RdU32LE(vvdBytes.data() + 8) != DetailMeshSciCad::RdU32LE(data + 8))
                    {
                        return false;
                    }
                }
                if (hasVtxOnDisk) {
                    std::vector<uint8_t> vtxBytes;
                    if (!DetailValveMdl::ReadSiblingBytes(vtxPath, vtxBytes) || vtxBytes.size() < 36 ||
                        DetailValveMdl::RdI32LE(vtxBytes.data()) != 7 ||
                        DetailMeshSciCad::RdU32LE(vtxBytes.data() + 16) != DetailMeshSciCad::RdU32LE(data + 8))
                    {
                        return false;
                    }
                }
            }

            // Fallback when sibling .vvd/.vtx files are absent: decode mstudiobone_t skeleton hierarchy
            if (size >= 400) {
                uint32_t numBones = DetailMeshSciCad::RdU32LE(data + 156);
                uint32_t boneIdx  = DetailMeshSciCad::RdU32LE(data + 160);
                if (numBones >= 2 && numBones <= 512 && boneIdx >= 244 &&
                    boneIdx + static_cast<uint64_t>(numBones) * 216ULL <= size)
                {
                    std::vector<DetailValveMdl::BoneMat3x4> world(numBones);
                    std::vector<int32_t> parents(numBones, -1);
                    std::vector<Vec3f> joints(numBones);
                    for (uint32_t b = 0; b < numBones; ++b) {
                        const uint8_t* bp = data + boneIdx + static_cast<size_t>(b) * 216;
                        int32_t parent = DetailValveMdl::RdI32LE(bp + 4);
                        if (parent < -1 || parent >= static_cast<int32_t>(numBones) || parent == static_cast<int32_t>(b)) {
                            return false;
                        }
                        parents[b] = parent;
                        float px = DetailMeshSciCad::RdF32LE(bp + 32);
                        float py = DetailMeshSciCad::RdF32LE(bp + 36);
                        float pz = DetailMeshSciCad::RdF32LE(bp + 40);
                        float qx = DetailMeshSciCad::RdF32LE(bp + 44);
                        float qy = DetailMeshSciCad::RdF32LE(bp + 48);
                        float qz = DetailMeshSciCad::RdF32LE(bp + 52);
                        float qw = DetailMeshSciCad::RdF32LE(bp + 56);

                        DetailValveMdl::BoneMat3x4 loc = {};
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
                            world[b] = DetailValveMdl::ConcatBoneMat(world[static_cast<size_t>(parent)], loc);
                        } else {
                            world[b] = loc;
                        }
                        joints[b] = { world[b].m[0][3], world[b].m[2][3], -world[b].m[1][3] };
                    }
                    float span = DetailMeshSciCad::ComputePointCloudSpan(joints);
                    if (span > 1e-3f) {
                        float r = span * 0.015f;
                        for (uint32_t b = 0; b < numBones; ++b) {
                            DetailMeshSciCad::AddOctaSphere(mesh, joints[b].x, joints[b].y, joints[b].z, r * 1.3f);
                            if (parents[b] >= 0 && static_cast<uint32_t>(parents[b]) < numBones) {
                                DetailMeshSciCad::AddStrut3D(mesh, joints[static_cast<size_t>(parents[b])], joints[b], r * 0.7f);
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
