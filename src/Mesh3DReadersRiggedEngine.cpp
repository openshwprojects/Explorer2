// Mesh3DReadersRiggedEngine.cpp — Skeletal Game Meshes, Real-Time Engine Assets, USD/Alembic & VTK XML Readers
// Compiled in its own translation unit so Mesh3DReaders.cpp stays fast and low-memory on MSVC and Linux GCC.
#include "Mesh3DReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "StreamCodecs.h"
#include "Base64.h"
#include "Ole2Compound.h"
#include "CharConvCompat.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Explorer2 {

#if defined(_MSC_VER)
#define E2_DRACO_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DRACO_NOINLINE __attribute__((noinline))
#else
#define E2_DRACO_NOINLINE
#endif

#include "formats/common/MeshFormatCommon.h"
#include "formats/3d/QuakeAliasMdl.h"
#include "formats/3d/ValveSourceMdl.h"
#include "formats/3d/Md5Mesh.h"
#include "formats/3d/Md5Anim.h"
#include "formats/3d/ActorXPsk.h"
#include "formats/3d/ActorXPsa.h"
#include "formats/3d/MmdPmx.h"
#include "formats/3d/MmdPmd.h"
#include "formats/3d/MmdVmd.h"
#include "formats/3d/Warcraft3Mdx.h"
#include "formats/3d/Warcraft3Mdl.h"
#include "formats/3d/WorldOfWarcraftM2.h"
#include "formats/3d/WorldOfWarcraftWmo.h"
#include "formats/3d/RenderWareDff.h"
#include "formats/3d/GtaCol.h"
#include "formats/3d/Quake3Md4.h"
#include "formats/3d/RtcwMds.h"
#include "formats/3d/WolfEtMdm.h"
#include "formats/3d/WolfEtMdx.h"
#include "formats/3d/UberToolsTan.h"
#include "formats/3d/BioWareAuroraMdl.h"
#include "formats/3d/GodotTextScene.h"
#include "formats/3d/GodotBinaryRes.h"
#include "formats/3d/UnrealStaticMesh.h"
#include "formats/3d/Cal3DCmf.h"
#include "formats/3d/Cal3DXmf.h"
#include "formats/3d/Cal3DCsf.h"
#include "formats/3d/CryEngineGeometry.h"
#include "formats/3d/PixarUsd.h"
#include "formats/3d/AlembicOgawa.h"
#include "formats/3d/VtkXmlUnstructured.h"
#include "formats/3d/VtkXmlPolyData.h"
#include "formats/3d/VtkXmlImageData.h"
#include "formats/3d/VtkXmlRectilinear.h"
#include "formats/3d/VtkXmlStructured.h"
#include "formats/3d/ParaViewPvd.h"
#include "formats/3d/QuakeSourceBsp.h"
#include "formats/3d/OgreMesh.h"
#include "formats/3d/InriaMeditMesh.h"
#include "formats/3d/C3dMocap.h"
#include "formats/3d/NrrdVolume.h"

namespace MeshReaders {

namespace {

E2_DRACO_NOINLINE bool DispatchSkeletalAndGameEngines(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".bsp") {
        return Formats::BspGeoDetail::ParseBspLevelMesh(data, size, mesh) ||
               Formats::ParseRenderWareDffOrRws(data, size, ext, mesh);
    }
    if (ext == L".md5mesh") return Formats::ParseMd5Mesh(sv, mesh);
    if (ext == L".md5anim") return Formats::ParseMd5Anim(sv, fullPath, mesh);
    if (ext == L".psk" || ext == L".pskx") return Formats::ParseActorXPsk(data, size, ext, mesh);
    if (ext == L".psa") return Formats::ParseActorXPsa(data, size, fullPath, mesh);
    if (ext == L".pmx") return Formats::ParseMmdPmx(data, size, mesh);
    if (ext == L".pmd") return Formats::ParseMmdPmd(data, size, mesh);
    if (ext == L".vmd") return Formats::ParseMmdVmd(data, size, mesh);
    if (ext == L".m2")  return Formats::ParseWowM2(data, size, fullPath, mesh);
    if (ext == L".wmo") return Formats::ParseWowWmo(data, size, fullPath, mesh);
    if (ext == L".dff" || ext == L".rws") return Formats::ParseRenderWareDffOrRws(data, size, ext, mesh);
    if (ext == L".col") return Formats::ParseGtaCol(data, size, mesh);
    if (ext == L".md4" || ext == L".mdr") return Formats::ParseIdTech3Md4(data, size, mesh);
    if (ext == L".mds") return Formats::ParseRtcwMds(data, size, mesh);
    if (ext == L".mdm") return Formats::ParseWolfEtMdm(data, size, fullPath, mesh);
    if (ext == L".tan") return Formats::ParseUberToolsTan(data, size, mesh);
    if (ext == L".tscn" || ext == L".tres") return Formats::ParseGodotTextScene(sv, fullPath, mesh);
    if (ext == L".scn" || ext == L".res") return Formats::ParseGodotBinaryRes(data, size, mesh);
    if (ext == L".mesh" && Formats::IsLikelyGodotResourceHeader(data, size)) {
        return Formats::GodotDetail::IsLikelyGodotBinaryHeader(data, size)
            ? Formats::ParseGodotBinaryRes(data, size, mesh)
            : Formats::ParseGodotTextScene(sv, fullPath, mesh);
    }
    if (ext == L".mesh") {
        return Formats::BspGeoDetail::ParseOgreBinaryMesh(data, size, mesh) ||
               Formats::BspGeoDetail::ParseInriaMeditMesh(data, size, mesh);
    }
    if (ext == L".uasset" || ext == L".umap") return Formats::ParseUnrealStaticMeshPackage(data, size, fullPath, mesh);
    if (ext == L".cmf") return Formats::ParseCal3DCmf(data, size, fullPath, mesh);
    if (ext == L".xmf") return Formats::ParseCal3DXmf(sv, fullPath, mesh);
    if (ext == L".csf" || ext == L".xsf") return Formats::ParseCal3DCsf(data, size, fullPath, mesh);
    if (ext == L".cgf" || ext == L".chr" || ext == L".cga") return Formats::ParseCryEngineGeometry(data, size, ext, mesh);
    if (ext == L".skin") {
        if (Formats::IsLikelyCryEngineContainer(data, size)) {
            return Formats::ParseCryEngineGeometry(data, size, ext, mesh);
        }
        if (size >= 44 && std::memcmp(data, "SKIN", 4) == 0 && !fullPath.empty()) {
            std::wstring dir = PathUtils::GetParentPath(fullPath);
            std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
            std::vector<uint8_t> m2Bytes;
            std::wstring m2Path = dir + L"\\" + stem + L".m2";
            if (!PathUtils::ReadFileBytes(m2Path, 64 * 1024 * 1024, m2Bytes) &&
                stem.size() > 2 && iswdigit(stem[stem.size() - 1]) && iswdigit(stem[stem.size() - 2])) {
                m2Path = dir + L"\\" + stem.substr(0, stem.size() - 2) + L".m2";
                PathUtils::ReadFileBytes(m2Path, 64 * 1024 * 1024, m2Bytes);
            }
            if (!m2Bytes.empty()) {
                return Formats::ParseWowM2(m2Bytes.data(), m2Bytes.size(), m2Path, mesh);
            }
        }
        return false;
    }
    if (ext == L".mdx") {
        if (size >= 4 && std::memcmp(data, "MDLX", 4) == 0) return Formats::ParseWarcraftMdx(data, size, mesh);
        if (size >= 4 && std::memcmp(data, "MDXW", 4) == 0) return Formats::ParseWolfEtMdx(data, size, fullPath, mesh);
        return Formats::ParseBioWarePairedMdx(data, size, fullPath, mesh);
    }
    if (ext == L".mdl") {
        if (size >= 4 && std::memcmp(data, "MDLX", 4) == 0) return Formats::ParseWarcraftMdx(data, size, mesh);
        if (Formats::BioWare3DDetail::IsLikelyBioWareBinaryMdl(data, size)) return Formats::ParseBioWareBinaryMdl(data, size, fullPath, mesh);
        if (Formats::BioWare3DDetail::IsLikelyBioWareAsciiMdl(sv)) return Formats::ParseBioWareAsciiMdl(sv, mesh);
        if (sv.find("FormatVersion") != std::string_view::npos && sv.find("Geoset") != std::string_view::npos) return Formats::ParseWarcraftAsciiMdl(sv, mesh);
        if (size >= 4 && std::memcmp(data, "IDPO", 4) == 0) return Formats::ParseQuakeAliasMdl(data, size, mesh);
        return Formats::ParseValveSourceMdl(data, size, mesh);
    }
    outMatched = false;
    return false;
}

E2_DRACO_NOINLINE bool DispatchUsdAlembicAndVtkVolumes(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".usd" || ext == L".usda" || ext == L".usdc" || ext == L".usdz")
        return Formats::ParsePixarUsdStage(data, size, ext, fullPath, mesh);
    if (ext == L".abc") return Formats::ParseAlembicOgawaMesh(data, size, mesh);
    if (ext == L".vtu") return Formats::ParseVtkXmlUnstructured(data, size, mesh);
    if (ext == L".vtp") return Formats::ParseVtkXmlPolyData(data, size, mesh);
    if (ext == L".vti") return Formats::ParseVtkXmlImageData(data, size, mesh);
    if (ext == L".vtr") return Formats::ParseVtkXmlRectilinear(data, size, mesh);
    if (ext == L".vts") return Formats::ParseVtkXmlStructured(data, size, mesh);
    if (ext == L".pvd") return Formats::ParseParaViewPvd(data, size, fullPath, mesh);
    if (ext == L".c3d") return Formats::ParseC3dMocapMesh(data, size, mesh);
    if (ext == L".nrrd" || ext == L".nhdr") {
        bool ok = Formats::ParseNrrdVolumeMesh(data, size, fullPath, mesh);
        if (ok && ext == L".nhdr") mesh.formatName = L"NRRD Detached Volumetric Header (.nhdr)";
        return ok;
    }
    outMatched = false;
    return false;
}

} // namespace

bool DispatchMeshRiggedAndEngine(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh,
    bool& outMatched)
{
    bool ok = DispatchSkeletalAndGameEngines(extLower, data, size, sv, fullPath, outMesh, outMatched);
    if (outMatched) return ok;
    return DispatchUsdAlembicAndVtkVolumes(extLower, data, size, fullPath, outMesh, outMatched);
}

} // namespace MeshReaders
} // namespace Explorer2
