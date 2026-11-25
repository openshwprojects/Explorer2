#pragma once
// ValveSourceVtx.h — Valve Source Engine Optimized Model Strip/Index File (.vtx / .dx90.vtx / .dx80.vtx / .sw.vtx)
// Shares ValveSourceCommon (.vvd LOD0 fixups + optional .mdl mesh vertex-offset + StripHeader_t trilist/tristrip).
#include "../common/ValveSourceModelCommon.h"

namespace Formats {

namespace DetailValveVtx {

inline std::wstring StripVtxSuffix(const std::wstring& vtxPath) {
    if (vtxPath.size() < 5) return {};
    const std::wstring low = PathUtils::ToLower(vtxPath);
    static constexpr const wchar_t* kSuffixes[] = {
        L".dx90.vtx", L".dx80.vtx", L".dx11.vtx", L".sw.vtx", L".vtx"
    };
    for (const wchar_t* suf : kSuffixes) {
        const size_t slen = std::wcslen(suf);
        if (low.size() > slen && low.compare(low.size() - slen, slen, suf) == 0) {
            return vtxPath.substr(0, vtxPath.size() - slen);
        }
    }
    return {};
}

} // namespace DetailValveVtx

inline bool ParseValveSourceVtx(
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    if (!data || size < 36u || ByteIO::RdI32LE(data + 0u) != 7) return false;

    const std::wstring stem = DetailValveVtx::StripVtxSuffix(fullPath);
    if (stem.empty()) return false;

    std::vector<uint8_t> vvdBytes;
    if (!PathUtils::ReadFileBytes(stem + L".vvd", 64ULL * 1024ULL * 1024ULL, vvdBytes)) {
        return false;
    }

    std::vector<Vec3f> lod0Verts;
    int32_t vvdChecksum = 0;
    if (!ValveSourceCommon::DecodeValveVvdLod0(vvdBytes.data(), vvdBytes.size(), lod0Verts, vvdChecksum)) {
        return false;
    }

    // Load optional companion <stem>.mdl if present on disk for exact modelBaseVert + meshVertOffset
    ValveSourceCommon::MdlBodyModelMeshTable mdlTable;
    const ValveSourceCommon::MdlBodyModelMeshTable* mdlPtr = nullptr;
    uint32_t mdlVer = 0;
    std::vector<uint8_t> mdlBytes;
    if (PathUtils::ReadFileBytes(stem + L".mdl", 64ULL * 1024ULL * 1024ULL, mdlBytes) && mdlBytes.size() >= 244u) {
        int32_t mdlChecksum = 0;
        if (ValveSourceCommon::ExtractValveMdlMeshSpans(mdlBytes.data(), mdlBytes.size(), mdlChecksum, mdlVer, mdlTable) &&
            mdlChecksum == vvdChecksum) {
            mdlPtr = &mdlTable;
        }
    }

    std::vector<MeshTriangle> vtxTris;
    if (!ValveSourceCommon::DecodeValveVtxV7Topology(
            data, size, vvdChecksum, lod0Verts.size(), mdlPtr, mdlVer, vtxTris)) {
        return false;
    }

    mesh.vertices = std::move(lod0Verts);
    mesh.tris     = std::move(vtxTris);
    mesh.formatName = L"Valve Source Optimized Mesh (VTX v7)";
    return true;
}

} // namespace Formats
