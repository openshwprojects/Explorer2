#pragma once
// ValveSourceVvd.h — Valve Source Engine Vertex Data (.vvd, "IDSV" v4) reader
// Reconstructs LOD0 fixup vertices via ValveSourceCommon, pairs with companion .vtx (+ optional .mdl)
// when present on disk, and emits clean point-cloud markers (EmitOctahedron) for vertex-only files.
#include "../common/ValveSourceModelCommon.h"

namespace Formats {

inline bool ParseValveSourceVvd(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    std::vector<Vec3f> lod0Verts;
    int32_t checksum = 0;
    if (!ValveSourceCommon::DecodeValveVvdLod0(data, size, lod0Verts, checksum)) {
        return false;
    }

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.formatName = L"Valve Source Vertex Data (VVD v4)";

    // 1. Prefer exact companion .dx90.vtx / .dx80.vtx / .sw.vtx / .vtx topology (+ optional .mdl offsets) when present
    if (fullPath.size() >= 5u) {
        const std::wstring stem = fullPath.substr(0u, fullPath.size() - 4u);
        static constexpr const wchar_t* kSuffixes[] = {
            L".dx90.vtx", L".dx80.vtx", L".dx11.vtx", L".sw.vtx", L".vtx"
        };

        std::vector<uint8_t> vtx;
        for (const wchar_t* suf : kSuffixes) {
            if (PathUtils::ReadFileBytes(stem + suf, 64ULL * 1024ULL * 1024ULL, vtx) && vtx.size() >= 36u) {
                break;
            }
            vtx.clear();
        }

        if (vtx.size() >= 36u) {
            ValveSourceCommon::MdlBodyModelMeshTable mdlTable;
            const ValveSourceCommon::MdlBodyModelMeshTable* mdlPtr = nullptr;
            uint32_t mdlVer = 0;
            std::vector<uint8_t> mdlBytes;
            if (PathUtils::ReadFileBytes(stem + L".mdl", 64ULL * 1024ULL * 1024ULL, mdlBytes) && mdlBytes.size() >= 244u) {
                int32_t mdlChecksum = 0;
                if (ValveSourceCommon::ExtractValveMdlMeshSpans(mdlBytes.data(), mdlBytes.size(), mdlChecksum, mdlVer, mdlTable) &&
                    mdlChecksum == checksum) {
                    mdlPtr = &mdlTable;
                }
            }

            std::vector<MeshTriangle> vtxTris;
            if (ValveSourceCommon::DecodeValveVtxV7Topology(
                    vtx.data(), vtx.size(), checksum, lod0Verts.size(), mdlPtr, mdlVer, vtxTris)) {
                mesh.vertices = std::move(lod0Verts);
                mesh.tris     = std::move(vtxTris);
                return true;
            }
        }
    }

    // 2. Standalone vertex-only .vvd (no companion .vtx): emit clean point-cloud markers (never random sequential soup!)
    Vec3f bmin = lod0Verts[0], bmax = lod0Verts[0];
    for (const auto& p : lod0Verts) {
        bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
        bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
    }
    const float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
    if (!std::isfinite(span) || span <= 1e-6f) return false;

    const float r = std::max(1e-4f, span * 0.018f);
    const size_t stride = std::max<size_t>(1u, lod0Verts.size() / 25000u);
    for (size_t i = 0; i < lod0Verts.size(); i += stride) {
        MeshBuild3D::EmitOctahedron(mesh, lod0Verts[i], r);
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
