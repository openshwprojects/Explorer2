// Mesh3DReaders.cpp - the 3D model readers behind Model3DViewer: src/formats/*.h, dispatched by extension.
// Moved out of Model3DViewer.cpp so that they build without the viewer, also on Linux (see linux/).
#include "Mesh3DReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include "CharConvCompat.h"
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <string_view>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <array>
#include <functional>
#include <numeric>
#include <limits>
#include <thread>

namespace Explorer2 {

#include "formats/Stl.h"
#include "formats/Obj.h"
#include "formats/Off.h"
#include "formats/Md2.h"
#include "formats/Md3.h"
#include "formats/Autodesk3ds.h"
#include "formats/Blender.h"
#include "formats/Amf.h"
#include "formats/ThreeMf.h"
#include "formats/ColladaDae.h"
#include "formats/DracoEdgebreaker.h"
#include "formats/Glb.h"
#include "formats/Wrl.h"
#include "formats/X3d.h"
#include "formats/Scad.h"
#include "formats/Pcd.h"
#include "formats/Ply.h"
#include "formats/Vtk.h"
#include "formats/Ase3dsMax.h"
#include "formats/Xyz.h"
#include "formats/StepCad.h"
#include "formats/IgesCad.h"
#include "formats/Gcode3D.h"
#include "formats/ProteinPdb.h"
#include "formats/ChemMol.h"
#include "formats/GmshMsh.h"
#include "formats/BvhMocap.h"
#include "formats/ValveSmd.h"
#include "formats/EsriDem.h"
#include "formats/LasLidar.h"
#include "formats/MagicaVox.h"
#include "formats/Extended3D100.h"
#include "formats/Extended3D200.h"
#include "formats/Fbx.h"
#include "formats/Md5Skeletal.h"
#include "formats/ActorXPsk.h"
#include "formats/MmdPmxPmd.h"
#include "formats/Blizzard3D.h"
#define E2_RW_ENABLE_3D 1
#include "formats/RenderWare3D.h"
#undef E2_RW_ENABLE_3D
#include "formats/IdTech3Skeletal.h"
#define E2_AURORA_ENABLE_3D 1
#include "formats/BioWareAurora.h"
#undef E2_AURORA_ENABLE_3D
#include "formats/GodotMeshScene.h"
#define E2_UE_ENABLE_3D 1
#include "formats/UnrealAsset3DTex.h"
#undef E2_UE_ENABLE_3D
#include "formats/Cal3DCryEngine.h"
#include "formats/CadPackages3D.h"
#include "formats/ModoZBrushMaxC4d.h"
#include "formats/Rhino3dmUpgrade.h"
#include "formats/DwgDxf3DUpgrade.h"
#include "formats/SolidWorks3D.h"
#include "formats/UsdAlembicUpgrade.h"
#include "formats/VtkXml3D.h"
#include "formats/SplatOpenVdb3D.h"
#include "formats/BspOgreGeo3D.h"

namespace MeshReaders {

bool Is3DModelExtension(const std::wstring& extLower) {
    return extLower == L".obj"   || extLower == L".stl"   || extLower == L".ply"   ||
           extLower == L".off"   || extLower == L".md2"   || extLower == L".md3"   ||
           extLower == L".blend" || extLower == L".3ds"   || extLower == L".dae"   ||
           extLower == L".gltf"  || extLower == L".glb"   || extLower == L".3mf"   ||
           extLower == L".fbx"   || extLower == L".ase"   || extLower == L".step"  ||
           extLower == L".stp"   || extLower == L".stpz"  || extLower == L".p21"   || extLower == L".iges"  ||
           extLower == L".igs"   ||
           extLower == L".vtk"   || extLower == L".amf"   || extLower == L".x3d"   ||
           extLower == L".wrl"   || extLower == L".scad"  || extLower == L".pcd"   ||
           extLower == L".xyz"   || extLower == L".pts"   || extLower == L".gcode" ||
           extLower == L".nc"    || extLower == L".pdb"   || extLower == L".mol"   ||
           extLower == L".sdf"   || extLower == L".msh"   || extLower == L".bvh"   ||
           extLower == L".smd"   || extLower == L".dem"   || extLower == L".ascgrid" ||
           extLower == L".hgt"   || extLower == L".las"   || extLower == L".vox"   ||
           extLower == L".ifc"   || extLower == L".dwg"   || extLower == L".3dm"   ||
           extLower == L".usd"   || extLower == L".usda"  || extLower == L".usdc"  ||
           extLower == L".usdz"  || extLower == L".abc"   || extLower == L".lwo"   ||
           extLower == L".lws"   || extLower == L".c4d"   || extLower == L".max"   ||
           extLower == L".ma"    || extLower == L".mb"    || extLower == L".x"     ||
           extLower == L".ms3d"  || extLower == L".iqm"   || extLower == L".mdl"   ||
           extLower == L".nif"   || extLower == L".b3d"   || extLower == L".ac"    ||
           extLower == L".ctm"   || extLower == L".drc"   || extLower == L".3dxml" ||
           extLower == L".jt"    || extLower == L".brep"  || extLower == L".sat"   ||
           extLower == L".sab"   || extLower == L".x_t"   || extLower == L".x_b"   ||
           extLower == L".e57"   || extLower == L".laz"   || extLower == L".urdf"  ||
           extLower == L".vrm"   || extLower == L".cif"   || extLower == L".mol2"  ||
           extLower == L".gro"   || extLower == L".md5mesh" || extLower == L".md5anim" ||
           extLower == L".psk"   || extLower == L".pskx"    || extLower == L".psa"     ||
           extLower == L".pmx"   || extLower == L".pmd"     || extLower == L".vmd"     ||
           extLower == L".mdx"   || extLower == L".m2"      || extLower == L".wmo"     ||
           extLower == L".dff"   || extLower == L".rws"     || extLower == L".md4"     ||
           extLower == L".mdr"   || extLower == L".mds"     || extLower == L".mdm"     ||
           extLower == L".tan"   || extLower == L".tscn"    || extLower == L".scn"     ||
           extLower == L".tres"  || extLower == L".res"     || extLower == L".mesh"    ||
           extLower == L".uasset"|| extLower == L".umap"    || extLower == L".cmf"     ||
           extLower == L".xmf"   || extLower == L".csf"     || extLower == L".xsf"     ||
           extLower == L".cgf"   || extLower == L".chr"     || extLower == L".skin"    ||
           extLower == L".cga"   || extLower == L".fcstd"   || extLower == L".scdoc"   ||
           extLower == L".ipt"   || extLower == L".iam"     || extLower == L".par"     ||
           extLower == L".psm"   || extLower == L".lxo"     || extLower == L".zpr"     ||
           extLower == L".ztl"   || extLower == L".dxf"     || extLower == L".sldprt"  ||
           extLower == L".sldasm"|| extLower == L".vtu"     || extLower == L".vtp"     ||
           extLower == L".vti"   || extLower == L".vtr"     || extLower == L".vts"     ||
           extLower == L".pvd"   || extLower == L".splat"   || extLower == L".ksplat"  ||
           extLower == L".spz"   || extLower == L".vdb"     || extLower == L".bsp"     ||
           extLower == L".shp"   || extLower == L".geojson" || extLower == L".geotiff";
}

bool IsLikely3DDxfFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 131072, hdr) || hdr.empty()) return false;
    return Formats::IsLikely3DDxfBuffer(hdr.data(), hdr.size());
}

bool IsLikely3DGeoOrDemFile(const std::wstring& fullPath, const std::wstring& extLower) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 65536, hdr) || hdr.empty()) return false;
    if (extLower == L".shp") return Formats::BspGeoDetail::IsLikely3DShapefileBuffer(hdr.data(), hdr.size());
    if (extLower == L".geojson") return Formats::BspGeoDetail::IsLikely3DGeoJsonBuffer(hdr.data(), hdr.size());
    if (extLower == L".tif" || extLower == L".tiff" || extLower == L".geotiff") {
        return Formats::BspGeoDetail::IsLikelyGeoTiffDemBuffer(hdr.data(), hdr.size());
    }
    return false;
}

bool InspectMeshFile(const std::wstring& fullPath, LoadedMesh3D& outMesh) {
    outMesh = ParseMeshFromDisk(fullPath, 0, nullptr);
    return outMesh.loadedOk;
}

using Formats::MeshBuild3D::FinalizeMeshBoundsAndNormals;

LoadedMesh3D ParseMeshFromDisk(
    const std::wstring& fullPath,
    uint64_t generation,
    const std::atomic<uint64_t>* activeGen)
try {
    LoadedMesh3D mesh;
    mesh.filePath = fullPath;
    std::wstring ext;
    {
        const wchar_t* dot = PathFindExtensionW(fullPath.c_str());
        if (dot && *dot) ext = PathUtils::ToLower(dot);
    }

    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        mesh.statusMessage = L"Nie można otworzyć pliku 3D";
        return mesh;
    }

    LARGE_INTEGER liSize = {};
    GetFileSizeEx(hFile, &liSize);
    uint64_t fileSize = static_cast<uint64_t>(std::max<LONGLONG>(0, liSize.QuadPart));
    const uint64_t readCap = (ext == L".fbx") ? 256ULL * 1024ULL * 1024ULL : 64ULL * 1024ULL * 1024ULL;
    size_t maxRead = static_cast<size_t>(std::min<uint64_t>(fileSize, readCap));
    std::vector<uint8_t> buf(maxRead);
    DWORD bytesRead = 0;
    if (maxRead > 0) {
        ReadFile(hFile, buf.data(), static_cast<DWORD>(maxRead), &bytesRead, nullptr);
        buf.resize(bytesRead);
    }
    CloseHandle(hFile);

    if (buf.empty()) {
        mesh.statusMessage = L"Pusty plik (0 B)";
        return mesh;
    }
    // Another file was selected while this one was being read: skip the parse (the result would be dropped)
    if (activeGen && activeGen->load() != generation) return mesh;

    if (Formats::ParseStlFile(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (Formats::ParseObjFile(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (ext == L".ply") {
        if (Formats::Parse3DGaussianPlyIfPresent(buf.data(), buf.size(), mesh) ||
            Formats::ParsePlyMesh(buf.data(), buf.size(), mesh)) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
        LoadedMesh3D unreadable;
        unreadable.filePath = fullPath;
        unreadable.formatName = L"Stanford PLY (.ply)";
        unreadable.statusMessage = L"Nie udało się odczytać modelu PLY";
        return unreadable;
    }
    if (Formats::ParseOffFile(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (Formats::ParseMd2File(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (Formats::ParseMd3File(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (Formats::Parse3dsFile(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (Formats::ParseBlendFile(ext, buf, fileSize, mesh, generation, activeGen)) return mesh;
    if (ext == L".glb" || ext == L".gltf") {
        if (Formats::ParseGltfOrGlbMesh(buf.data(), buf.size(), fullPath, ext, mesh)) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
        LoadedMesh3D unreadable;
        unreadable.filePath = fullPath;
        unreadable.formatName = (ext == L".gltf") ? L"JSON glTF 2.0 (.gltf)" : L"Binary glTF 2.0 (.glb)";
        unreadable.statusMessage = L"Nie udało się odczytać modelu glTF 2.0";
        return unreadable;
    }
    if (ext == L".3mf") {
        if (Formats::ParseThreeMfMesh(buf.data(), buf.size(), fullPath, mesh)) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
        LoadedMesh3D unreadable;
        unreadable.filePath = fullPath;
        unreadable.formatName = L"3D Manufacturing Format (.3mf)";
        unreadable.statusMessage = L"Nie udało się odczytać modelu 3MF";
        return unreadable;
    }
    if (ext == L".dae") {
        std::string_view daeSv(reinterpret_cast<const char*>(buf.data()), buf.size());
        if (Formats::ParseColladaDaeMesh(daeSv, mesh)) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
        LoadedMesh3D unreadable;
        unreadable.filePath = fullPath;
        unreadable.formatName = L"COLLADA 3D (.dae)";
        unreadable.statusMessage = L"Nie udało się odczytać modelu COLLADA";
        return unreadable;
    }
    if (ext == L".vtk") {
        if (Formats::ParseVtkMesh(buf.data(), buf.size(), mesh)) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
        LoadedMesh3D unreadable;
        unreadable.filePath = fullPath;
        unreadable.formatName = L"Visualization Toolkit 3D (.vtk)";
        unreadable.statusMessage = L"Nie udało się odczytać modelu VTK";
        return unreadable;
    }

    // 11b. Autodesk FBX, binary and text (formats/Fbx.h)
    if (ext == L".fbx") {
        if (Formats::ParseFbxMesh(buf.data(), buf.size(), mesh)) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
        LoadedMesh3D unreadable;
        unreadable.filePath = fullPath;
        unreadable.formatName = L"Autodesk FBX";
        unreadable.statusMessage = L"Nie udało się odczytać siatki z tego pliku FBX";
        return unreadable;
    }

    // 11c. Modular 3D & Point Cloud Formats (.amf, .x3d, .wrl, .ase, .scad, .pcd, .xyz / .pts, + Extended 100)
    {
        std::string_view sv(reinterpret_cast<const char*>(buf.data()), buf.size());
        bool parsedMod = false;
        if (ext == L".amf")  parsedMod = Formats::ParseAmfMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".x3d")  parsedMod = Formats::ParseX3dMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".wrl")  parsedMod = Formats::ParseWrlMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".ase")  parsedMod = Formats::ParseAseMesh(sv, mesh);
        else if (ext == L".scad") parsedMod = Formats::ParseOpenScadMesh(sv, mesh);
        else if (ext == L".pcd")  parsedMod = Formats::ParsePcdPointCloud(buf.data(), buf.size(), mesh);
        else if (ext == L".xyz" || ext == L".pts") parsedMod = Formats::ParseXyzPointCloud(sv, mesh);
        else if (ext == L".step" || ext == L".stp" || ext == L".stpz" || ext == L".p21") {
            std::vector<uint8_t> stepInf;
            if (buf.size() >= 10 && buf[0] == 0x1Fu && buf[1] == 0x8Bu) {
                if (ZipDeflate::InflateGzip(buf.data(), buf.size(), stepInf, 128 * 1024 * 1024) && !stepInf.empty()) {
                    parsedMod = Formats::ParseStepCadMesh(std::string_view(reinterpret_cast<const char*>(stepInf.data()), stepInf.size()), mesh);
                    if (parsedMod && ext == L".stpz") mesh.formatName = L"Compressed ISO 10303-21 STEP (.stpz)";
                }
            } else if (buf.size() >= 30 && buf[0] == 'P' && buf[1] == 'K') {
                std::vector<ZipDeflate::ArchiveItemMeta> zEnts;
                if (ZipDeflate::ListZipEntriesFromMemory(buf.data(), buf.size(), zEnts)) {
                    for (const auto& ze : zEnts) {
                        if (!ze.isDirectory && ZipDeflate::ExtractZipEntryFromMemory(buf.data(), buf.size(), ze, stepInf, 128 * 1024 * 1024) && !stepInf.empty()) {
                            if (Formats::ParseStepCadMesh(std::string_view(reinterpret_cast<const char*>(stepInf.data()), stepInf.size()), mesh)) {
                                parsedMod = true;
                                mesh.formatName = L"Compressed ISO 10303-21 STEP (.stpz)";
                                break;
                            }
                        }
                    }
                }
            } else if (ext != L".stpz") {
                parsedMod = Formats::ParseStepCadMesh(sv, mesh);
            }
        }
        else if (ext == L".iges" || ext == L".igs") parsedMod = Formats::ParseIgesCadMesh(sv, mesh);
        else if (ext == L".gcode" || ext == L".nc") parsedMod = Formats::ParseGcodeToolpathMesh(sv, mesh);
        else if (ext == L".pdb") parsedMod = Formats::ParseProteinPdbMesh(sv, mesh);
        else if (ext == L".mol" || ext == L".sdf") parsedMod = Formats::ParseChemMolMesh(sv, mesh);
        else if (ext == L".msh") parsedMod = Formats::ParseGmshMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".bvh") parsedMod = Formats::ParseBvhSkeletonMesh(sv, mesh);
        else if (ext == L".smd") parsedMod = Formats::ParseValveSmdMesh(sv, mesh);
        else if (ext == L".dem" || ext == L".ascgrid") parsedMod = Formats::ParseEsriDemMesh(sv, mesh);
        else if (ext == L".hgt") parsedMod = Formats::BspGeoDetail::ParseVoidSafeSrtmHgt(buf.data(), buf.size(), mesh);
        else if (ext == L".geotiff" || ext == L".tif" || ext == L".tiff")
            parsedMod = Formats::BspGeoDetail::ParseGeoTiffDemMesh(buf.data(), buf.size(), ext != L".geotiff", mesh);
        else if (ext == L".shp") parsedMod = Formats::BspGeoDetail::ParseShapefile3DMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".geojson") parsedMod = Formats::BspGeoDetail::ParseGeoJson3DMesh(sv, mesh);
        else if (ext == L".ctm") parsedMod = Formats::BspGeoDetail::ParseOpenCtmComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".bsp") parsedMod = Formats::BspGeoDetail::ParseBspLevelMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".las") parsedMod = Formats::ParseLasLidarMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".vox") parsedMod = Formats::ParseMagicaVoxMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".ifc") parsedMod = Formats::ParseIfcBimMesh(sv, mesh);
        else if (ext == L".usd" || ext == L".usda" || ext == L".usdc" || ext == L".usdz")
            parsedMod = Formats::ParsePixarUsdComplete(buf.data(), buf.size(), ext, fullPath, mesh);
        else if (ext == L".abc")
            parsedMod = Formats::ParseAlembicOgawaComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".md5mesh") parsedMod = Formats::ParseMd5Mesh(sv, mesh);
        else if (ext == L".md5anim") parsedMod = Formats::ParseMd5Anim(sv, fullPath, mesh);
        else if (ext == L".psk" || ext == L".pskx") parsedMod = Formats::ParseActorXPsk(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".psa") parsedMod = Formats::ParseActorXPsa(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".pmx") parsedMod = Formats::ParseMmdPmx(buf.data(), buf.size(), mesh);
        else if (ext == L".pmd") parsedMod = Formats::ParseMmdPmd(buf.data(), buf.size(), mesh);
        else if (ext == L".vmd") parsedMod = Formats::ParseMmdVmd(buf.data(), buf.size(), mesh);
        else if (ext == L".m2")  parsedMod = Formats::ParseWowM2(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".wmo") parsedMod = Formats::ParseWowWmo(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".dff" || ext == L".rws")
            parsedMod = Formats::ParseRenderWareDffOrRws(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".md4" || ext == L".mdr")
            parsedMod = Formats::ParseIdTech3Md4(buf.data(), buf.size(), mesh);
        else if (ext == L".mds")
            parsedMod = Formats::ParseRtcwMds(buf.data(), buf.size(), mesh);
        else if (ext == L".mdm")
            parsedMod = Formats::ParseWolfEtMdm(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".tan")
            parsedMod = Formats::ParseUberToolsTan(buf.data(), buf.size(), mesh);
        else if (ext == L".tscn" || ext == L".scn" || ext == L".tres" || ext == L".res" ||
                 (ext == L".mesh" && Formats::IsLikelyGodotResourceHeader(buf.data(), buf.size())))
            parsedMod = Formats::ParseGodotMeshOrScene(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".mesh")
            parsedMod = Formats::BspGeoDetail::ParseOgreOrMeditMesh(buf.data(), buf.size(), mesh) ||
                        Formats::ParseExtended3D200(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".uasset" || ext == L".umap")
            parsedMod = Formats::ParseUnrealStaticMeshPackage(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".cmf")
            parsedMod = Formats::ParseCal3DCmf(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".xmf")
            parsedMod = Formats::ParseCal3DXmf(sv, fullPath, mesh);
        else if (ext == L".csf" || ext == L".xsf")
            parsedMod = Formats::ParseCal3DCsf(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".cgf" || ext == L".chr" || ext == L".cga")
            parsedMod = Formats::ParseCryEngineGeometry(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".fcstd")
            parsedMod = Formats::ParseFreeCadFcstd(buf.data(), buf.size(), mesh);
        else if (ext == L".scdoc")
            parsedMod = Formats::ParseSpaceClaimScdoc(buf.data(), buf.size(), mesh);
        else if (ext == L".ipt" || ext == L".iam" || ext == L".par" || ext == L".psm")
            parsedMod = Formats::ParseInventorOrSolidEdgeOle2(buf.data(), buf.size(), ext, fullPath, mesh);
        else if (ext == L".skin") {
            if (Formats::IsLikelyCryEngineContainer(buf.data(), buf.size())) {
                parsedMod = Formats::ParseCryEngineGeometry(buf.data(), buf.size(), ext, mesh);
            } else if (buf.size() >= 44 && std::memcmp(buf.data(), "SKIN", 4) == 0 && !fullPath.empty()) {
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
                    parsedMod = Formats::ParseWowM2(m2Bytes.data(), m2Bytes.size(), m2Path, mesh);
                }
            }
        }
        else if (ext == L".mdx") {
            if (buf.size() >= 4 && std::memcmp(buf.data(), "MDLX", 4) == 0)
                parsedMod = Formats::ParseWarcraftMdx(buf.data(), buf.size(), mesh);
            else if (buf.size() >= 4 && std::memcmp(buf.data(), "MDXW", 4) == 0)
                parsedMod = Formats::ParseWolfEtMdx(buf.data(), buf.size(), fullPath, mesh);
            else
                parsedMod = Formats::ParseBioWarePairedMdx(buf.data(), buf.size(), fullPath, mesh);
        }
        else if (ext == L".mdl") {
            if (buf.size() >= 4 && std::memcmp(buf.data(), "MDLX", 4) == 0)
                parsedMod = Formats::ParseWarcraftMdx(buf.data(), buf.size(), mesh);
            else if (Formats::BioWare3DDetail::IsLikelyBioWareBinaryMdl(buf.data(), buf.size()))
                parsedMod = Formats::ParseBioWareBinaryMdl(buf.data(), buf.size(), fullPath, mesh);
            else if (Formats::BioWare3DDetail::IsLikelyBioWareAsciiMdl(sv))
                parsedMod = Formats::ParseBioWareAsciiMdl(sv, mesh);
            else if (sv.find("FormatVersion") != std::string_view::npos && sv.find("Geoset") != std::string_view::npos)
                parsedMod = Formats::ParseWarcraftAsciiMdl(sv, mesh);
            else
                parsedMod = Formats::ParseExtended3D200(buf.data(), buf.size(), ext, mesh);
        }
        else if (ext == L".lwo") parsedMod = Formats::ParseLightWaveLwo(buf.data(), buf.size(), mesh);
        else if (ext == L".lxo") parsedMod = Formats::ParseModoLxo(buf.data(), buf.size(), mesh);
        else if (ext == L".max") parsedMod = Formats::Parse3dsMaxEnhanced(buf.data(), buf.size(), mesh);
        else if (ext == L".c4d") parsedMod = Formats::ParseCinema4dEnhanced(buf.data(), buf.size(), mesh);
        else if (ext == L".zpr" || ext == L".ztl") parsedMod = Formats::ParseZBrushZprZtl(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".drc") parsedMod = Formats::ParseDracoDrcComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".3dm") parsedMod = Formats::ParseRhino3dmComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".dwg") parsedMod = Formats::ParseAutoCadDwgComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".dxf") parsedMod = Formats::ParseAutoCadDxf3DMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".sldprt" || ext == L".sldasm")
            parsedMod = Formats::ParseSolidWorksPartOrAssembly(buf.data(), buf.size(), ext, fullPath, mesh);
        else if (ext == L".vtu" || ext == L".vtp" || ext == L".vti" ||
                 ext == L".vtr" || ext == L".vts" || ext == L".pvd")
            parsedMod = Formats::ParseVtkXmlMesh(buf.data(), buf.size(), ext, fullPath, mesh);
        else if (ext == L".splat" || ext == L".ksplat" || ext == L".spz" || ext == L".vdb")
            parsedMod = Formats::ParseSplatOrOpenVdb(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".lws" || ext == L".ma" || ext == L".mb")
            parsedMod = Formats::ParseMayaOrCadContainer(buf.data(), buf.size(), ext, mesh, fullPath);
        else
            parsedMod = Formats::ParseExtended3D200(buf.data(), buf.size(), ext, mesh);
        if (parsedMod) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
    }

    // 12. No reader could read the file. Nothing is shown in its place (this used to be a made-up crystal
    // shape, which looked like the model): the view says that the file could not be read.
    LoadedMesh3D unreadable;
    unreadable.filePath = fullPath;
    std::wstring extUp = (ext.size() > 1) ? ext.substr(1) : ext;
    for (wchar_t& ch : extUp) ch = static_cast<wchar_t>(towupper(ch));
    unreadable.formatName = extUp.empty() ? std::wstring(L"3D") : extUp;
    unreadable.statusMessage = L"Nie udało się odczytać modelu 3D z tego pliku";
    // A scene (.lws) whose objects could not be found says which files are missing, instead of the generic line
    if (ext == L".lws" && !mesh.statusMessage.empty()) unreadable.statusMessage = mesh.statusMessage;
    return unreadable;
} E2_READER_CATCH(L"3D model reader", fullPath, { LoadedMesh3D unreadable; unreadable.filePath = fullPath; return unreadable; })

} // namespace MeshReaders

} // namespace Explorer2
