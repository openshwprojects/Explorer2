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
#include "formats/cad/Scad.h"
#include "formats/3d/Pcd.h"
#include "formats/3d/Ply.h"
#include "formats/3d/Vtk.h"
#include "formats/3d/Ase3dsMax.h"
#include "formats/3d/Xyz.h"
#include "formats/cad/StepCad.h"
#include "formats/cad/IgesCad.h"
#include "formats/cad/Gcode3D.h"
#include "formats/3d/ProteinPdb.h"
#include "formats/3d/ChemMol.h"
#include "formats/3d/GmshMsh.h"
#include "formats/3d/BvhMocap.h"
#include "formats/3d/ValveSmd.h"
#include "formats/gis/EsriAsciiGrid.h"
#include "formats/gis/UsgsDem.h"
#include "formats/3d/LasLidar.h"
#include "formats/3d/MagicaVox.h"
#include "formats/cad/IfcBim.h"
#include "formats/3d/LightWaveLwo.h"
#include "formats/3d/MayaAsciiMa.h"
#include "formats/3d/MayaBinaryMb.h"
#include "formats/3d/LightWaveSceneLws.h"
#include "formats/3d/DirectXMeshX.h"
#include "formats/3d/MilkShapeMs3d.h"
#include "formats/3d/InterQuakeIqm.h"
#include "formats/3d/QuakeAliasMdl.h"
#include "formats/3d/ValveSourceMdl.h"
#include "formats/3d/NetImmerseNif.h"
#include "formats/3d/Blitz3DB3d.h"
#include "formats/3d/Ac3dMesh.h"
#include "formats/cad/Catia3dxml.h"
#include "formats/cad/SiemensJt.h"
#include "formats/cad/OpenCascadeBrep.h"
#include "formats/cad/SpatialAcisSat.h"
#include "formats/cad/ParasolidXt.h"
#include "formats/3d/AstmE57.h"
#include "formats/cad/RosUrdf.h"
#include "formats/3d/CrystallographyCif.h"
#include "formats/3d/TriposMol2.h"
#include "formats/3d/GromacsGro.h"
#include "formats/3d/Fbx.h"
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
#include "formats/cad/FreeCadFcstd.h"
#include "formats/cad/SpaceClaimScdoc.h"
#include "formats/cad/AutodeskInventor3D.h"
#include "formats/cad/SolidEdge3D.h"
#include "formats/3d/FoundryModoLxo.h"
#include "formats/3d/Autodesk3dsMax.h"
#include "formats/3d/MaxonCinema4d.h"
#include "formats/3d/ZBrushZpr.h"
#include "formats/3d/ZBrushZtl.h"
#include "formats/cad/Rhino3dm.h"
#include "formats/cad/AutoCadDwg.h"
#include "formats/cad/AutoCadDxf3D.h"
#include "formats/cad/SolidWorks3D.h"
#include "formats/3d/PixarUsd.h"
#include "formats/3d/AlembicOgawa.h"
#include "formats/3d/VtkXmlUnstructured.h"
#include "formats/3d/VtkXmlPolyData.h"
#include "formats/3d/VtkXmlImageData.h"
#include "formats/3d/VtkXmlRectilinear.h"
#include "formats/3d/VtkXmlStructured.h"
#include "formats/3d/ParaViewPvd.h"
#include "formats/3d/GaussianSplat.h"
#include "formats/3d/KelloggKsplat.h"
#include "formats/3d/NianticSpz.h"
#include "formats/3d/OpenVdbVolume.h"
#include "formats/gis/SrtmHgt.h"
#include "formats/gis/GeoTiffDem.h"
#include "formats/gis/Shapefile3D.h"
#include "formats/gis/GeoJson3D.h"
#include "formats/3d/OpenCtm.h"
#include "formats/3d/QuakeSourceBsp.h"
#include "formats/3d/OgreMesh.h"
#include "formats/3d/InriaMeditMesh.h"
#include "formats/cad/LDrawCad.h"
#include "formats/3d/QuakeMap.h"
#include "formats/3d/ValveVmf.h"
#include "formats/3d/C3dMocap.h"
#include "formats/3d/NrrdVolume.h"

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
           extLower == L".shp"   || extLower == L".geojson" || extLower == L".geotiff" ||
           extLower == L".ldr"   || extLower == L".mpd"     || extLower == L".vmf"     ||
           extLower == L".map"   || extLower == L".c3d"     || extLower == L".nrrd"    ||
           extLower == L".col";
}

bool IsLikelyQuakeMapFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 262144, hdr) || hdr.empty()) return false;
    return Formats::IsLikelyQuakeBrushMapMemory(hdr.data(), hdr.size());
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
        else if (ext == L".ascgrid") parsedMod = Formats::ParseEsriAsciiGridMesh(sv, mesh);
        else if (ext == L".dem") parsedMod = Formats::ParseEsriAsciiGridMesh(sv, mesh) || Formats::ParseUsgsDemMesh(sv, mesh);
        else if (ext == L".hgt") parsedMod = Formats::BspGeoDetail::ParseVoidSafeSrtmHgt(buf.data(), buf.size(), mesh);
        else if (ext == L".geotiff" || ext == L".tif" || ext == L".tiff")
            parsedMod = Formats::BspGeoDetail::ParseGeoTiffDemMesh(buf.data(), buf.size(), ext != L".geotiff", mesh);
        else if (ext == L".shp") parsedMod = Formats::BspGeoDetail::ParseShapefile3DMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".geojson") parsedMod = Formats::BspGeoDetail::ParseGeoJson3DMesh(sv, mesh);
        else if (ext == L".ctm") parsedMod = Formats::BspGeoDetail::ParseOpenCtmComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".bsp") parsedMod = Formats::BspGeoDetail::ParseBspLevelMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".las" || ext == L".laz") {
            parsedMod = Formats::ParseLasLidarMesh(buf.data(), buf.size(), mesh);
            if (parsedMod && ext == L".laz" && mesh.formatName.find(L".laz") == std::wstring::npos) {
                mesh.formatName = L"LAS / LASzip LiDAR (.laz)";
            }
        }
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
        else if (ext == L".col")
            parsedMod = Formats::ParseGtaCol(buf.data(), buf.size(), mesh);
        else if (ext == L".md4" || ext == L".mdr")
            parsedMod = Formats::ParseIdTech3Md4(buf.data(), buf.size(), mesh);
        else if (ext == L".mds")
            parsedMod = Formats::ParseRtcwMds(buf.data(), buf.size(), mesh);
        else if (ext == L".mdm")
            parsedMod = Formats::ParseWolfEtMdm(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".tan")
            parsedMod = Formats::ParseUberToolsTan(buf.data(), buf.size(), mesh);
        else if (ext == L".tscn" || ext == L".tres")
            parsedMod = Formats::ParseGodotTextScene(sv, fullPath, mesh);
        else if (ext == L".scn" || ext == L".res")
            parsedMod = Formats::ParseGodotBinaryRes(buf.data(), buf.size(), mesh);
        else if (ext == L".mesh" && Formats::IsLikelyGodotResourceHeader(buf.data(), buf.size()))
            parsedMod = Formats::GodotDetail::IsLikelyGodotBinaryHeader(buf.data(), buf.size())
                ? Formats::ParseGodotBinaryRes(buf.data(), buf.size(), mesh)
                : Formats::ParseGodotTextScene(sv, fullPath, mesh);
        else if (ext == L".mesh")
            parsedMod = Formats::BspGeoDetail::ParseOgreBinaryMesh(buf.data(), buf.size(), mesh) ||
                        Formats::BspGeoDetail::ParseInriaMeditMesh(buf.data(), buf.size(), mesh);
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
        else if (ext == L".ipt" || ext == L".iam")
            parsedMod = Formats::ParseAutodeskInventor3D(buf.data(), buf.size(), ext, fullPath, mesh);
        else if (ext == L".par" || ext == L".psm")
            parsedMod = Formats::ParseSolidEdge3D(buf.data(), buf.size(), ext, fullPath, mesh);
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
            else if (buf.size() >= 4 && std::memcmp(buf.data(), "IDPO", 4) == 0)
                parsedMod = Formats::ParseQuakeAliasMdl(buf.data(), buf.size(), mesh);
            else
                parsedMod = Formats::ParseValveSourceMdl(buf.data(), buf.size(), mesh);
        }
        else if (ext == L".lwo") parsedMod = Formats::ParseLightWaveLwo(buf.data(), buf.size(), mesh);
        else if (ext == L".lxo") parsedMod = Formats::ParseModoLxo(buf.data(), buf.size(), mesh);
        else if (ext == L".max") parsedMod = Formats::Parse3dsMaxEnhanced(buf.data(), buf.size(), mesh);
        else if (ext == L".c4d") parsedMod = Formats::ParseCinema4dEnhanced(buf.data(), buf.size(), mesh);
        else if (ext == L".zpr") parsedMod = Formats::ParseZBrushZpr(buf.data(), buf.size(), mesh);
        else if (ext == L".ztl") parsedMod = Formats::ParseZBrushZtl(buf.data(), buf.size(), mesh);
        else if (ext == L".drc") parsedMod = Formats::ParseDracoDrcComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".3dm") parsedMod = Formats::ParseRhino3dmComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".dwg") parsedMod = Formats::ParseAutoCadDwgComplete(buf.data(), buf.size(), mesh);
        else if (ext == L".dxf") parsedMod = Formats::ParseAutoCadDxf3DMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".sldprt" || ext == L".sldasm")
            parsedMod = Formats::ParseSolidWorksPartOrAssembly(buf.data(), buf.size(), ext, fullPath, mesh);
        else if (ext == L".vtu")
            parsedMod = Formats::ParseVtkXmlUnstructured(buf.data(), buf.size(), mesh);
        else if (ext == L".vtp")
            parsedMod = Formats::ParseVtkXmlPolyData(buf.data(), buf.size(), mesh);
        else if (ext == L".vti")
            parsedMod = Formats::ParseVtkXmlImageData(buf.data(), buf.size(), mesh);
        else if (ext == L".vtr")
            parsedMod = Formats::ParseVtkXmlRectilinear(buf.data(), buf.size(), mesh);
        else if (ext == L".vts")
            parsedMod = Formats::ParseVtkXmlStructured(buf.data(), buf.size(), mesh);
        else if (ext == L".pvd")
            parsedMod = Formats::ParseParaViewPvd(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".splat")
            parsedMod = Formats::ParseGaussianSplat(buf.data(), buf.size(), mesh);
        else if (ext == L".ksplat")
            parsedMod = Formats::ParseKelloggKsplatFile(buf.data(), buf.size(), mesh);
        else if (ext == L".spz")
            parsedMod = Formats::ParseNianticSpzFile(buf.data(), buf.size(), mesh);
        else if (ext == L".vdb")
            parsedMod = Formats::ParseOpenVdbVolumeFile(buf.data(), buf.size(), mesh);
        else if (ext == L".ldr" || ext == L".mpd")
            parsedMod = Formats::ParseLDrawMesh(buf.data(), buf.size(), fullPath, ext, mesh);
        else if (ext == L".vmf")
            parsedMod = Formats::ParseValveVmfMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".map")
            parsedMod = Formats::ParseQuakeMapMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".c3d")
            parsedMod = Formats::ParseC3dMocapMesh(buf.data(), buf.size(), mesh);
        else if (ext == L".nrrd")
            parsedMod = Formats::ParseNrrdVolumeMesh(buf.data(), buf.size(), fullPath, mesh);
        else if (ext == L".ma")
            parsedMod = Formats::ParseMayaAsciiMa(sv, mesh);
        else if (ext == L".mb")
            parsedMod = Formats::ParseMayaBinaryMb(buf.data(), buf.size(), mesh);
        else if (ext == L".lws")
            parsedMod = Formats::ParseLightWaveSceneLws(sv, fullPath, mesh);
        else if (ext == L".x")
            parsedMod = Formats::ParseDirectXMeshX(buf.data(), buf.size(), mesh);
        else if (ext == L".ms3d")
            parsedMod = Formats::ParseMilkShapeMs3d(buf.data(), buf.size(), mesh);
        else if (ext == L".iqm")
            parsedMod = Formats::ParseInterQuakeIqm(buf.data(), buf.size(), mesh);
        else if (ext == L".nif")
            parsedMod = Formats::ParseNetImmerseNif(buf.data(), buf.size(), mesh);
        else if (ext == L".b3d")
            parsedMod = Formats::ParseBlitz3DB3d(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".ac")
            parsedMod = Formats::ParseAc3dMesh(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".3dxml")
            parsedMod = Formats::ParseCatia3dxml(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".jt")
            parsedMod = Formats::ParseSiemensJt(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".brep")
            parsedMod = Formats::ParseOpenCascadeBrep(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".sat" || ext == L".sab")
            parsedMod = Formats::ParseSpatialAcisSat(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".x_t" || ext == L".x_b")
            parsedMod = Formats::ParseParasolidXt(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".e57")
            parsedMod = Formats::ParseAstmE57(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".urdf")
            parsedMod = Formats::ParseRosUrdf(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".vrm")
            parsedMod = Formats::ParseVroidVrm(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".cif")
            parsedMod = Formats::ParseCrystallographyCif(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".mol2")
            parsedMod = Formats::ParseTriposMol2(buf.data(), buf.size(), ext, mesh);
        else if (ext == L".gro")
            parsedMod = Formats::ParseGromacsGro(buf.data(), buf.size(), ext, mesh);
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
