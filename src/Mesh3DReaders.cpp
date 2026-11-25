// Mesh3DReaders.cpp - the 3D model readers behind Model3DViewer: src/formats/*.h, dispatched by extension.
// Moved out of Model3DViewer.cpp so that they build without the viewer, also on Linux (see linux/).
// CAD B-Rep readers compile in Mesh3DReadersCadBrep.cpp and rigged/engine/VTK readers in Mesh3DReadersRiggedEngine.cpp.
#include "Mesh3DReaders.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "StreamCodecs.h"
#include "Base64.h"
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
#include <memory>
#include <thread>

namespace Explorer2 {

#include "formats/common/MeshFormatCommon.h"
#include "formats/3d/Stl.h"
#include "formats/3d/Obj.h"
#include "formats/3d/Off.h"
#include "formats/3d/Md2.h"
#include "formats/3d/Md3.h"
#include "formats/3d/Autodesk3ds.h"
#include "formats/3d/Blender.h"
#include "formats/cad/Amf.h"
#include "formats/cad/ThreeMf.h"
#include "formats/3d/ColladaDae.h"
#include "formats/3d/GoogleDraco.h"
#include "formats/3d/Glb.h"
#include "formats/3d/VroidVrm.h"
#include "formats/3d/Wrl.h"
#include "formats/3d/X3d.h"
#include "formats/3d/GaussianSplat.h"
#include "formats/3d/KelloggKsplat.h"
#include "formats/3d/NianticSpz.h"
#include "formats/3d/OpenVdbVolume.h"
#include "formats/3d/Pcd.h"
#include "formats/3d/Ply.h"
#include "formats/3d/Vtk.h"
#include "formats/3d/Ase3dsMax.h"
#include "formats/3d/Xyz.h"
#include "formats/3d/ProteinPdb.h"
#include "formats/3d/ChemMol.h"
#include "formats/3d/GmshMsh.h"
#include "formats/3d/BvhMocap.h"
#include "formats/3d/ValveSmd.h"
#include "formats/gis/EsriAsciiGrid.h"
#include "formats/gis/UsgsDem.h"
#include "formats/3d/LasLidar.h"
#include "formats/3d/MagicaVox.h"
#include "formats/3d/LightWaveLwo.h"
#include "formats/3d/MayaAsciiMa.h"
#include "formats/3d/MayaBinaryMb.h"
#include "formats/3d/LightWaveSceneLws.h"
#include "formats/3d/DirectXMeshX.h"
#include "formats/3d/MilkShapeMs3d.h"
#include "formats/3d/InterQuakeIqm.h"
#include "formats/3d/NetImmerseNif.h"
#include "formats/3d/Blitz3DB3d.h"
#include "formats/3d/Ac3dMesh.h"
#include "formats/3d/AstmE57.h"
#include "formats/cad/RosUrdf.h"
#include "formats/3d/CrystallographyCif.h"
#include "formats/3d/TriposMol2.h"
#include "formats/3d/GromacsGro.h"
#include "formats/3d/Fbx.h"
#include "formats/3d/FoundryModoLxo.h"
#include "formats/3d/Autodesk3dsMax.h"
#include "formats/3d/MaxonCinema4d.h"
#include "formats/3d/ZBrushZpr.h"
#include "formats/3d/ZBrushZtl.h"
#include "formats/gis/SrtmHgt.h"
#include "formats/gis/GeoTiffDem.h"
#include "formats/gis/Shapefile3D.h"
#include "formats/gis/GeoJson3D.h"
#include "formats/3d/OpenCtm.h"
#include "formats/3d/QuakeMap.h"
#include "formats/3d/ValveVmf.h"

namespace MeshReaders {

bool Is3DModelExtension(const std::wstring& extLower) {
    return extLower == L".obj"   || extLower == L".stl"   || extLower == L".ply"   ||
           extLower == L".off"   || extLower == L".md2"   || extLower == L".md3"   ||
           extLower == L".blend" || extLower == L".3ds"   || extLower == L".dae"   ||
           extLower == L".gltf"  || extLower == L".glb"   || extLower == L".3mf"   ||
           extLower == L".fbx"   || extLower == L".ase"   || extLower == L".step"  ||
           extLower == L".stp"   || extLower == L".stpz"  || extLower == L".p21"   || extLower == L".iges"  ||
           extLower == L".igs"   ||
           extLower == L".vtk"   || extLower == L".amf"   || extLower == L".x3d"   || extLower == L".x3dz" ||
           extLower == L".wrl"   || extLower == L".wrz"   || extLower == L".scad"  || extLower == L".pcd"  ||
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
           extLower == L".jt"    || extLower == L".brep"  || extLower == L".brp"   || extLower == L".sat"  ||
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
           extLower == L".shp"   || extLower == L".geojson" || extLower == L".geotiff" ||
           extLower == L".ldr"   || extLower == L".mpd"     || extLower == L".vmf"     ||
           extLower == L".map"   || extLower == L".c3d"     || extLower == L".nrrd"    || extLower == L".nhdr" ||
           extLower == L".col"   || IsModular3DModelExtension(extLower);
}

bool IsLikelyQuakeMapFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 262144, hdr) || hdr.empty()) return false;
    return Formats::IsLikelyQuakeBrushMapMemory(hdr.data(), hdr.size());
}

bool IsLikely3DDxfFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 131072, hdr) || hdr.empty()) return false;
    return CheckLikely3DDxfHeader(hdr.data(), hdr.size());
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

namespace {

E2_DRACO_NOINLINE bool DispatchDccChemAndGeoTerrain(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".amf")  return Formats::ParseAmfMesh(data, size, mesh);
    if (ext == L".x3d" || ext == L".x3dz") {
        bool ok = Formats::ParseX3dMesh(data, size, mesh);
        if (ok && ext == L".x3dz") mesh.formatName = L"Compressed Extensible 3D (.x3dz)";
        return ok;
    }
    if (ext == L".wrl" || ext == L".wrz") {
        bool ok = Formats::ParseWrlMesh(data, size, mesh);
        if (ok && ext == L".wrz") mesh.formatName = L"Compressed VRML World (.wrz)";
        return ok;
    }
    if (ext == L".ase")  return Formats::ParseAseMesh(sv, mesh);
    if (ext == L".pcd")  return Formats::ParsePcdPointCloud(data, size, mesh);
    if (ext == L".xyz" || ext == L".pts") return Formats::ParseXyzPointCloud(sv, mesh);
    if (ext == L".pdb") return Formats::ParseProteinPdbMesh(sv, mesh);
    if (ext == L".mol" || ext == L".sdf") return Formats::ParseChemMolMesh(sv, mesh);
    if (ext == L".msh") return Formats::ParseGmshMesh(data, size, mesh);
    if (ext == L".bvh") return Formats::ParseBvhSkeletonMesh(sv, mesh);
    if (ext == L".smd") return Formats::ParseValveSmdMesh(sv, mesh);
    if (ext == L".ascgrid") return Formats::ParseEsriAsciiGridMesh(sv, mesh);
    if (ext == L".dem") return Formats::ParseEsriAsciiGridMesh(sv, mesh) || Formats::ParseUsgsDemMesh(sv, mesh);
    if (ext == L".hgt") return Formats::BspGeoDetail::ParseVoidSafeSrtmHgt(data, size, mesh);
    if (ext == L".geotiff" || ext == L".tif" || ext == L".tiff")
        return Formats::BspGeoDetail::ParseGeoTiffDemMesh(data, size, ext != L".geotiff", mesh);
    if (ext == L".shp") return Formats::BspGeoDetail::ParseShapefile3DMesh(data, size, mesh);
    if (ext == L".geojson") return Formats::BspGeoDetail::ParseGeoJson3DMesh(sv, mesh);
    if (ext == L".ctm") return Formats::BspGeoDetail::ParseOpenCtmMesh(data, size, mesh);
    if (ext == L".las" || ext == L".laz") {
        bool ok = Formats::ParseLasLidarMesh(data, size, mesh);
        if (ok && ext == L".laz" && mesh.formatName.find(L".laz") == std::wstring::npos) {
            mesh.formatName = L"LAS / LASzip LiDAR (.laz)";
        }
        return ok;
    }
    if (ext == L".vox") return Formats::ParseMagicaVoxMesh(data, size, mesh);
    outMatched = false;
    return false;
}

E2_DRACO_NOINLINE bool DispatchSculptSplatsAndStudioDcc(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".lwo") return Formats::ParseLightWaveLwo(data, size, mesh);
    if (ext == L".lxo") return Formats::ParseModoLxo(data, size, mesh);
    if (ext == L".max") return Formats::Parse3dsMaxEnhanced(data, size, mesh);
    if (ext == L".c4d") return Formats::ParseCinema4dEnhanced(data, size, mesh);
    if (ext == L".zpr") return Formats::ParseZBrushZpr(data, size, mesh);
    if (ext == L".ztl") return Formats::ParseZBrushZtl(data, size, mesh);
    if (ext == L".drc") return Formats::ParseGoogleDracoMesh(data, size, mesh);
    if (ext == L".splat") return Formats::ParseGaussianSplat(data, size, mesh);
    if (ext == L".ksplat") return Formats::ParseKelloggKsplatFile(data, size, mesh);
    if (ext == L".spz") return Formats::ParseNianticSpzFile(data, size, mesh);
    if (ext == L".vdb") return Formats::ParseOpenVdbVolumeFile(data, size, mesh);
    if (ext == L".vmf") return Formats::ParseValveVmfMesh(data, size, mesh);
    if (ext == L".map") return Formats::ParseQuakeMapMesh(data, size, mesh);
    if (ext == L".ma")  return Formats::ParseMayaAsciiMa(sv, mesh);
    if (ext == L".mb")  return Formats::ParseMayaBinaryMb(data, size, mesh);
    if (ext == L".lws") return Formats::ParseLightWaveSceneLws(sv, fullPath, mesh);
    if (ext == L".x")   return Formats::ParseDirectXMeshX(data, size, mesh);
    if (ext == L".ms3d") return Formats::ParseMilkShapeMs3d(data, size, mesh);
    if (ext == L".iqm") return Formats::ParseInterQuakeIqm(data, size, mesh);
    if (ext == L".nif") return Formats::ParseNetImmerseNif(data, size, mesh);
    if (ext == L".b3d") return Formats::ParseBlitz3DB3d(data, size, ext, mesh);
    if (ext == L".ac")  return Formats::ParseAc3dMesh(data, size, ext, mesh);
    if (ext == L".e57") return Formats::ParseAstmE57(data, size, ext, mesh);
    if (ext == L".urdf") return Formats::ParseRosUrdf(data, size, ext, mesh);
    if (ext == L".vrm") return Formats::ParseVroidVrm(data, size, ext, mesh);
    if (ext == L".cif") return Formats::ParseCrystallographyCif(data, size, ext, mesh);
    if (ext == L".mol2") return Formats::ParseTriposMol2(data, size, ext, mesh);
    if (ext == L".gro") return Formats::ParseGromacsGro(data, size, ext, mesh);
    outMatched = false;
    return false;
}

} // namespace

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

    {
        std::string_view sv(reinterpret_cast<const char*>(buf.data()), buf.size());
        bool matched = false;
        bool parsedMod = DispatchDccChemAndGeoTerrain(ext, buf.data(), buf.size(), sv, fullPath, mesh, matched);
        if (!matched) parsedMod = DispatchSculptSplatsAndStudioDcc(ext, buf.data(), buf.size(), sv, fullPath, mesh, matched);
        if (!matched) parsedMod = DispatchMeshCadAndBrep(ext, buf.data(), buf.size(), sv, fullPath, mesh, matched);
        if (!matched) parsedMod = DispatchMeshRiggedAndEngine(ext, buf.data(), buf.size(), sv, fullPath, mesh, matched);
        if (!matched) parsedMod = TryParseModularMesh(ext, buf.data(), buf.size(), fullPath, mesh);
        if (parsedMod) {
            FinalizeMeshBoundsAndNormals(mesh);
            if (mesh.loadedOk) return mesh;
        }
    }

    LoadedMesh3D unreadable;
    unreadable.filePath = fullPath;
    std::wstring extUp = (ext.size() > 1) ? ext.substr(1) : ext;
    for (wchar_t& ch : extUp) ch = static_cast<wchar_t>(towupper(ch));
    unreadable.formatName = extUp.empty() ? std::wstring(L"3D") : extUp;
    unreadable.statusMessage = L"Nie udało się odczytać modelu 3D z tego pliku";
    if (ext == L".lws" && !mesh.statusMessage.empty()) unreadable.statusMessage = mesh.statusMessage;
    return unreadable;
} E2_READER_CATCH(L"3D model reader", fullPath, { LoadedMesh3D unreadable; unreadable.filePath = fullPath; return unreadable; })

} // namespace MeshReaders

} // namespace Explorer2
