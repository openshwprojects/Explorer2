#include "Model3DViewer.h"
#include "FormatSupport.h"
#include "ReaderGuard.h"
#include "PathUtils.h"
#include "ZipDeflate.h"
#include "Ole2Compound.h"
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <charconv>
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

static D2D1_COLOR_F M3DColor(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

static inline float VecDot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3f VecNormalize(const Vec3f& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 1e-7f) {
        float inv = 1.0f / len;
        return { v.x * inv, v.y * inv, v.z * inv };
    }
    return { 0.0f, 1.0f, 0.0f };
}

Model3DViewer::~Model3DViewer() {
    Shutdown();
}

void Model3DViewer::Initialize(HWND notifyHwnd, ID2D1Factory* d2dFactory) {
    m_notifyHwnd = notifyHwnd;
    m_d2dFactory = d2dFactory;
}

void Model3DViewer::Shutdown() {
    ++m_load->generation;
}

bool Model3DViewer::Is3DModelExtension(const std::wstring& extLower) {
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

bool Model3DViewer::IsLikely3DDxfFile(const std::wstring& fullPath) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 131072, hdr) || hdr.empty()) return false;
    return Formats::IsLikely3DDxfBuffer(hdr.data(), hdr.size());
}

bool Model3DViewer::IsLikely3DGeoOrDemFile(const std::wstring& fullPath, const std::wstring& extLower) {
    std::vector<uint8_t> hdr;
    if (!PathUtils::ReadFileBytes(fullPath, 65536, hdr) || hdr.empty()) return false;
    if (extLower == L".shp") return Formats::BspGeoDetail::IsLikely3DShapefileBuffer(hdr.data(), hdr.size());
    if (extLower == L".geojson") return Formats::BspGeoDetail::IsLikely3DGeoJsonBuffer(hdr.data(), hdr.size());
    if (extLower == L".tif" || extLower == L".tiff" || extLower == L".geotiff") {
        return Formats::BspGeoDetail::IsLikelyGeoTiffDemBuffer(hdr.data(), hdr.size());
    }
    return false;
}

bool Model3DViewer::InspectMeshFile(const std::wstring& fullPath, LoadedMesh3D& outMesh) {
    outMesh = ParseMeshFromDisk(fullPath, 0, nullptr);
    return outMesh.loadedOk;
}

void Model3DViewer::Clear() {
    ++m_load->generation;
    m_requestedPath.clear();
    m_load->loading.store(false);
    std::lock_guard<std::mutex> lk(m_load->mutex);
    m_load->mesh = LoadedMesh3D{};
}

void Model3DViewer::ResetCamera() {
    m_yaw = 0.62f;
    m_pitch = 0.38f;
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;
}

void Model3DViewer::StepAnimation() {
    if (!m_autoRotate) return;
    ULONGLONG now = GetTickCount64();
    float dt = (m_lastAnimTick > 0 && now > m_lastAnimTick)
        ? std::min(0.1f, static_cast<float>(now - m_lastAnimTick) * 0.001f)
        : 0.016f;
    m_lastAnimTick = now;
    m_yaw += dt * 0.85f;
    if (m_yaw > 6.2831853f) m_yaw -= 6.2831853f;
}

void Model3DViewer::LoadFile(const std::wstring& fullPath, bool forceReload) {
    if (!forceReload && m_requestedPath == fullPath && !m_requestedPath.empty()) {
        return;
    }
    m_requestedPath = fullPath;
    ResetCamera();

    uint64_t gen = ++m_load->generation;
    m_load->loading.store(true);
    {
        std::lock_guard<std::mutex> lk(m_load->mutex);
        m_load->mesh = LoadedMesh3D{};
        m_load->mesh.filePath = fullPath;
        m_load->mesh.isLoading = true;
    }

    // The thread works on the shared block only, never on the viewer itself (see LoadState)
    HWND notifyWnd = m_notifyHwnd;
    std::thread([state = m_load, fullPath, gen, notifyWnd]() {
        LoadedMesh3D parsed = ParseMeshFromDisk(fullPath, gen, &state->generation);
        if (state->generation.load() != gen) return;
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (state->generation.load() != gen) return;
            state->mesh = std::move(parsed);
            state->mesh.isLoading = false;
        }
        state->loading.store(false);
        if (notifyWnd && IsWindow(notifyWnd)) {
            PostMessageW(notifyWnd, WM_APP_THUMB_READY, 0, 0);
        }
    }).detach();
}


using Formats::MeshBuild3D::FinalizeMeshBoundsAndNormals;

LoadedMesh3D Model3DViewer::ParseMeshFromDisk(
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

namespace {

// Fills a triangle into the picture where it is nearer than what is there (edge functions, depth = 1/distance,
// which is linear on screen). Both windings are filled.
void FillTriangleZ(Model3DViewer::RasterScratch& s, int W, int H, uint32_t a, uint32_t b, uint32_t c, uint32_t color) {
    float x0 = s.sx[a], y0 = s.sy[a], z0 = s.iw[a];
    float x1 = s.sx[b], y1 = s.sy[b], z1 = s.iw[b];
    float x2 = s.sx[c], y2 = s.sy[c], z2 = s.iw[c];
    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (std::fabs(area) < 1e-6f) return;
    if (area < 0.0f) {
        std::swap(x1, x2);
        std::swap(y1, y2);
        std::swap(z1, z2);
        area = -area;
    }
    const int minX = std::max(0, static_cast<int>(std::floor(std::min({ x0, x1, x2 }))));
    const int maxX = std::min(W - 1, static_cast<int>(std::ceil(std::max({ x0, x1, x2 }))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({ y0, y1, y2 }))));
    const int maxY = std::min(H - 1, static_cast<int>(std::ceil(std::max({ y0, y1, y2 }))));
    if (minX > maxX || minY > maxY) return;
    const float invArea = 1.0f / area;
    const float d0 = -(y2 - y1), d1 = -(y0 - y2), d2 = -(y1 - y0);     // change per pixel to the right
    // A pixel centre that lies on an edge two triangles share is drawn by both (the depth test picks one),
    // so rounding never opens a gap between them
    const float t0 = -1e-3f * (std::fabs(x2 - x1) + std::fabs(y2 - y1));
    const float t1 = -1e-3f * (std::fabs(x0 - x2) + std::fabs(y0 - y2));
    const float t2 = -1e-3f * (std::fabs(x1 - x0) + std::fabs(y1 - y0));
    const float px0 = static_cast<float>(minX) + 0.5f;
    for (int y = minY; y <= maxY; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        float w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px0 - x1);     // weight of vertex 0 (times area)
        float w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px0 - x2);
        float w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px0 - x0);
        size_t idx = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(minX);
        for (int x = minX; x <= maxX; ++x, ++idx, w0 += d0, w1 += d1, w2 += d2) {
            if (w0 < t0 || w1 < t1 || w2 < t2) continue;
            const float z = (w0 * z0 + w1 * z1 + w2 * z2) * invArea;
            if (z > s.depth[idx]) {
                s.depth[idx] = z;
                s.pixels[idx] = color;
            }
        }
    }
}

// Lays a colour over a premultiplied pixel ("source over")
inline void BlendPixel(uint32_t& dst, uint32_t rgb, float alpha) {
    const float keep = 1.0f - alpha;
    const float da = static_cast<float>(dst >> 24), dr = static_cast<float>((dst >> 16) & 0xFF);
    const float dg = static_cast<float>((dst >> 8) & 0xFF), db = static_cast<float>(dst & 0xFF);
    const uint32_t oa = static_cast<uint32_t>(std::min(255.0f, alpha * 255.0f + da * keep + 0.5f));
    const uint32_t orr = static_cast<uint32_t>(std::min(255.0f, static_cast<float>((rgb >> 16) & 0xFF) * alpha + dr * keep + 0.5f));
    const uint32_t og = static_cast<uint32_t>(std::min(255.0f, static_cast<float>((rgb >> 8) & 0xFF) * alpha + dg * keep + 0.5f));
    const uint32_t ob = static_cast<uint32_t>(std::min(255.0f, static_cast<float>(rgb & 0xFF) * alpha + db * keep + 0.5f));
    dst = (oa << 24) | (orr << 16) | (og << 8) | ob;
}

// An edge of the wireframe. With depthTest only where it is not behind the surface drawn there.
void DrawEdgeZ(Model3DViewer::RasterScratch& s, int W, int H, uint32_t a, uint32_t b, uint32_t rgb, float alpha, bool depthTest) {
    const float x0 = s.sx[a], y0 = s.sy[a], z0 = s.iw[a];
    const float x1 = s.sx[b], y1 = s.sy[b], z1 = s.iw[b];
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::max(std::fabs(dx), std::fabs(dy));
    if (len > 16384.0f) return;
    const int steps = static_cast<int>(std::ceil(len));
    for (int k = 0; k <= steps; ++k) {
        const float t = (steps > 0) ? static_cast<float>(k) / static_cast<float>(steps) : 0.0f;
        const int x = static_cast<int>(std::floor(x0 + dx * t));
        const int y = static_cast<int>(std::floor(y0 + dy * t));
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
        if (depthTest && (z0 + (z1 - z0) * t) < s.depth[idx] * 0.995f) continue;
        BlendPixel(s.pixels[idx], rgb, alpha);
    }
}

} // namespace

void Model3DViewer::RasterizeMesh(const LoadedMesh3D& mesh, const RasterView& view, RasterScratch& s) {
    const int W = std::max(0, view.width);
    const int H = std::max(0, view.height);
    const size_t pixelCount = static_cast<size_t>(W) * static_cast<size_t>(H);
    s.pixels.assign(pixelCount, 0u);
    s.depth.assign(pixelCount, 0.0f);
    const auto& verts = mesh.vertices;
    const auto& tris = mesh.tris;
    if (pixelCount == 0 || verts.empty() || tris.empty()) return;

    // Every vertex is projected once, as project3D in Draw does it
    const float cosY = std::cos(view.yaw), sinY = std::sin(view.yaw);
    const float cosP = std::cos(view.pitch), sinP = std::sin(view.pitch);
    const float camDist = 3.6f;
    s.sx.resize(verts.size());
    s.sy.resize(verts.size());
    s.iw.resize(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vec3f& p = verts[i];
        const float rx =  p.x * cosY + p.z * sinY;
        const float rz = -p.x * sinY + p.z * cosY;
        const float ry =  p.y * cosP - rz * sinP;
        const float fz =  p.y * sinP + rz * cosP;
        const float denom = camDist - fz;
        if (denom < 0.25f) {        // too near the camera, or behind it
            s.iw[i] = 0.0f;
            continue;
        }
        const float inv = 1.0f / denom;
        const float persp = camDist * inv * view.scale;
        s.sx[i] = view.centerX + rx * persp;
        s.sy[i] = view.centerY - ry * persp;
        s.iw[i] = inv;
    }

    const size_t stride = (view.maxTriangles > 0 && tris.size() > view.maxTriangles)
        ? (tris.size() + view.maxTriangles - 1) / view.maxTriangles : 1;
    const size_t vCount = verts.size();
    auto usable = [&](const MeshTriangle& t) {
        return t.i0 < vCount && t.i1 < vCount && t.i2 < vCount &&
               s.iw[t.i0] > 0.0f && s.iw[t.i1] > 0.0f && s.iw[t.i2] > 0.0f;
    };

    if (view.mode != RenderMode3D::Wireframe) {
        // Studio lighting: a key light, a fill light and a rim, on both sides of every face
        const Vec3f keyLight  = VecNormalize({  0.45f,  0.75f,  0.50f });
        const Vec3f fillLight = VecNormalize({ -0.60f, -0.25f,  0.40f });
        for (size_t ti = 0; ti < tris.size(); ti += stride) {
            const MeshTriangle& t = tris[ti];
            if (!usable(t)) continue;
            const Vec3f& n = t.normal;
            const float rnx =  n.x * cosY + n.z * sinY;
            const float rnz = -n.x * sinY + n.z * cosY;
            const float rny =  n.y * cosP - rnz * sinP;
            const float fnz =  n.y * sinP + rnz * cosP;
            Vec3f camN = { rnx, rny, fnz };
            if (camN.z < 0.0f) {
                camN.x = -camN.x; camN.y = -camN.y; camN.z = -camN.z;
            }
            const float ndotl1 = std::max(0.0f, VecDot(camN, keyLight));
            const float ndotl2 = std::max(0.0f, VecDot(camN, fillLight));
            const float rim    = std::pow(1.0f - std::clamp(camN.z, 0.0f, 1.0f), 2.2f) * 0.25f;
            const float shade  = std::clamp(0.24f + ndotl1 * 0.62f + ndotl2 * 0.20f + rim, 0.15f, 1.0f);
            const uint32_t r = static_cast<uint32_t>(std::clamp(70.0f  * shade + rim * 90.0f,  0.0f, 255.0f));
            const uint32_t g = static_cast<uint32_t>(std::clamp(152.0f * shade + rim * 100.0f, 0.0f, 255.0f));
            const uint32_t b = static_cast<uint32_t>(std::clamp(235.0f * shade + rim * 110.0f, 0.0f, 255.0f));
            FillTriangleZ(s, W, H, t.i0, t.i1, t.i2, 0xFF000000u | (r << 16) | (g << 8) | b);
        }
    }
    if (view.mode != RenderMode3D::Shaded) {
        const bool seeThrough = (view.mode == RenderMode3D::Wireframe);
        const uint32_t rgb = seeThrough ? 0x58A6FFu : 0x0D1117u;
        const float alpha = seeThrough ? 0.78f : 0.42f;
        for (size_t ti = 0; ti < tris.size(); ti += stride) {
            const MeshTriangle& t = tris[ti];
            if (!usable(t)) continue;
            DrawEdgeZ(s, W, H, t.i0, t.i1, rgb, alpha, !seeThrough);
            DrawEdgeZ(s, W, H, t.i1, t.i2, rgb, alpha, !seeThrough);
            DrawEdgeZ(s, W, H, t.i2, t.i0, rgb, alpha, !seeThrough);
        }
    }
}

void Model3DViewer::Draw(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    IDWriteTextFormat* fmtRegular,
    IDWriteTextFormat* fmtSmall,
    IDWriteTextFormat* fmtBold,
    const D2D1_RECT_F& rect,
    Lang lang,
    D2D1_POINT_2F /*mousePt*/)
{
    const bool pl = (lang == Lang::PL);
    m_toolbarHits.clear();

    float tbH = 30.0f;
    float statusH = 26.0f;
    D2D1_RECT_F tbR = D2D1::RectF(rect.left, rect.top, rect.right, rect.top + tbH);
    brush->SetColor(M3DColor(0xF8FAFC));
    rt->FillRectangle(tbR, brush);
    brush->SetColor(M3DColor(0xE2E8F0));
    rt->DrawLine(D2D1::Point2F(tbR.left, tbR.bottom - 0.5f), D2D1::Point2F(tbR.right, tbR.bottom - 0.5f), brush, 1.0f);

    float bx = tbR.left + 6.0f;
    auto drawBtn = [&](int id, float w, const wchar_t* label, bool active = false) {
        if (bx + w > tbR.right - 4.0f) return;
        D2D1_RECT_F br = D2D1::RectF(bx, tbR.top + 4.0f, bx + w, tbR.bottom - 4.0f);
        m_toolbarHits.push_back({ id, br });
        bool hov = (m_hoverBtnId == id);
        brush->SetColor(active ? M3DColor(0xDBEAFE) : (hov ? M3DColor(0xE2E8F0) : M3DColor(0xFFFFFF)));
        rt->FillRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush);
        brush->SetColor(active ? M3DColor(0x3B82F6) : M3DColor(0xCBD5E1));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(br, 3.0f, 3.0f), brush, 1.0f);
        brush->SetColor(active ? M3DColor(0x1D4ED8) : M3DColor(0x1E293B));
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmtSmall, br, brush);
        fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        bx += w + 4.0f;
    };

    const wchar_t* modeLbl = (m_renderMode == RenderMode3D::Shaded)
        ? (pl ? L"\xD83E\xDDCA Cieniowany" : L"\xD83E\xDDCA Shaded")
        : ((m_renderMode == RenderMode3D::ShadedWireframe)
            ? (pl ? L"\xD83E\xDDCA Bryła + Siatka" : L"\xD83E\xDDCA Solid + Wire")
            : (pl ? L"\xD83D\xDD78 Siatka (Wire)" : L"\xD83D\xDD78 Wireframe"));

    drawBtn(BTN_MODE_CYCLE, 106.0f, modeLbl, true);
    drawBtn(BTN_AUTOROTATE, 88.0f,  pl ? L"\xD83D\xDD04 Auto-obrót" : L"\xD83D\xDD04 Turntable", m_autoRotate);
    drawBtn(BTN_GRID,       68.0f,  pl ? L"\x25A6 Podłoga" : L"\x25A6 Grid", m_showGrid);
    drawBtn(BTN_RESET_CAM,  68.0f,  pl ? L"\xD83C\xDFAF Resetuj" : L"\xD83C\xDFAF Reset");

    // 2. 3D Studio Viewport Canvas
    m_viewportRect = D2D1::RectF(rect.left, tbR.bottom, rect.right, std::max(tbR.bottom + 40.0f, rect.bottom - statusH));
    brush->SetColor(M3DColor(0x161B22));
    rt->FillRectangle(m_viewportRect, brush);

    rt->PushAxisAlignedClip(m_viewportRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float vpW = m_viewportRect.right - m_viewportRect.left;
    float vpH = m_viewportRect.bottom - m_viewportRect.top;
    float cx = (m_viewportRect.left + m_viewportRect.right) * 0.5f + m_panX;
    float cy = (m_viewportRect.top + m_viewportRect.bottom) * 0.5f + m_panY;
    float scale = std::min(vpW, vpH) * 0.42f * m_zoom;

    float cosY = std::cos(m_yaw), sinY = std::sin(m_yaw);
    float cosP = std::cos(m_pitch), sinP = std::sin(m_pitch);

    auto project3D = [&](const Vec3f& p, float& outX, float& outY, float& outZ) -> bool {
        // Rotate around Y (Yaw), then around X (Pitch)
        float rx =  p.x * cosY + p.z * sinY;
        float rz = -p.x * sinY + p.z * cosY;
        float ry =  p.y * cosP - rz * sinP;
        float fz =  p.y * sinP + rz * cosP;

        float camDist = 3.6f;
        float denom = camDist - fz;
        if (denom < 0.25f) return false;
        float persp = (camDist / denom) * scale;
        outX = cx + rx * persp;
        outY = cy - ry * persp;
        outZ = fz;
        return true;
    };

    // Draw 3D Ground Grid at Y = -1.0f
    if (m_showGrid) {
        brush->SetColor(M3DColor(0x30363D, 0.75f));
        for (int g = -5; g <= 5; ++g) {
            float coord = g * 0.3f;
            float x1, y1, z1, x2, y2, z2;
            if (project3D({ coord, -1.0f, -1.5f }, x1, y1, z1) &&
                project3D({ coord, -1.0f,  1.5f }, x2, y2, z2)) {
                rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, (g == 0) ? 1.4f : 0.8f);
            }
            if (project3D({ -1.5f, -1.0f, coord }, x1, y1, z1) &&
                project3D({  1.5f, -1.0f, coord }, x2, y2, z2)) {
                rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, (g == 0) ? 1.4f : 0.8f);
            }
        }
    }

    size_t rawV = 0, rawT = 0;
    Vec3f bboxSz = {};
    std::wstring fmtName;
    bool isLoading = m_load->loading.load();
    bool unreadable = false;

    {
        std::lock_guard<std::mutex> lk(m_load->mutex);
        rawV = m_load->mesh.rawVertexCount;
        rawT = m_load->mesh.rawTriangleCount;
        bboxSz = m_load->mesh.bboxSize;
        fmtName = m_load->mesh.formatName;
        unreadable = !isLoading && !m_load->mesh.loadedOk && !m_load->mesh.filePath.empty();

        if (!isLoading && m_load->mesh.loadedOk && !m_load->mesh.tris.empty() && !m_load->mesh.vertices.empty()) {
            // The model is drawn by RasterizeMesh into a picture with a depth buffer: every triangle, the
            // nearest in front. At rest the picture has twice the pixels each way and is smoothed as it is
            // shown; a large model that is moving is drawn with fewer pixels and triangles, to stay fluid.
            float dpiX = 96.0f, dpiY = 96.0f;
            rt->GetDpi(&dpiX, &dpiY);
            const float pxPerDip = std::max(0.5f, dpiX / 96.0f);
            const bool moving = m_draggingOrbit || m_draggingPan || m_autoRotate;
            const size_t triCount = m_load->mesh.tris.size();
            float res = pxPerDip;
            const float basePixels = vpW * vpH * pxPerDip * pxPerDip;
            if (!moving && basePixels * 4.0f <= 5.0e6f) res *= 2.0f;
            else if (moving && triCount > 250000) res *= 0.5f;

            RasterView view;
            view.width = std::max(1, static_cast<int>(std::ceil(vpW * res)));
            view.height = std::max(1, static_cast<int>(std::ceil(vpH * res)));
            view.centerX = (cx - m_viewportRect.left) * res;
            view.centerY = (cy - m_viewportRect.top) * res;
            view.scale = scale * res;
            view.yaw = m_yaw;
            view.pitch = m_pitch;
            view.mode = m_renderMode;
            view.maxTriangles = moving ? 400000 : 0;

            const UINT32 w = static_cast<UINT32>(view.width);
            const UINT32 h = static_cast<UINT32>(view.height);
            const uint64_t generation = m_load->generation.load();
            const bool sameView = m_rasterBitmap && m_rasterTarget == rt && m_rasterW == w && m_rasterH == h &&
                m_rasterKey.generation == generation &&
                m_rasterKey.meshData == m_load->mesh.vertices.data() && m_rasterKey.triCount == triCount &&
                m_rasterKey.view.centerX == view.centerX && m_rasterKey.view.centerY == view.centerY &&
                m_rasterKey.view.scale == view.scale && m_rasterKey.view.yaw == view.yaw &&
                m_rasterKey.view.pitch == view.pitch && m_rasterKey.view.mode == view.mode &&
                m_rasterKey.view.maxTriangles == view.maxTriangles;
            if (!sameView) {
                RasterizeMesh(m_load->mesh, view, m_raster);
                if (!m_rasterBitmap || m_rasterTarget != rt || m_rasterW != w || m_rasterH != h) {
                    m_rasterBitmap.Reset();
                    m_rasterTarget = nullptr;
                    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
                    if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(w, h), m_raster.pixels.data(), w * 4, &props, &m_rasterBitmap))) {
                        m_rasterTarget = rt;
                        m_rasterW = w;
                        m_rasterH = h;
                    }
                } else {
                    m_rasterBitmap->CopyFromMemory(nullptr, m_raster.pixels.data(), w * 4);
                }
                m_rasterKey.generation = generation;
                m_rasterKey.meshData = m_load->mesh.vertices.data();
                m_rasterKey.triCount = triCount;
                m_rasterKey.view = view;
            }
            if (m_rasterBitmap) {
                rt->DrawBitmap(m_rasterBitmap.Get(), m_viewportRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }
    }

    if (unreadable) {
        const wchar_t* msg = pl ? L"Nie udało się odczytać modelu 3D z tego pliku."
                                : L"Could not read a 3D model from this file.";
        brush->SetColor(M3DColor(0xE2E8F0));
        fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(msg, static_cast<UINT32>(wcslen(msg)), fmtRegular,
            D2D1::RectF(m_viewportRect.left + 16.0f, m_viewportRect.top + vpH * 0.42f, m_viewportRect.right - 16.0f, m_viewportRect.bottom), brush);
        fmtRegular->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    if (isLoading) {
        std::wstring loadTxt = pl ? L"\x23F3 Wczytywanie modelu 3D w tle..." : L"\x23F3 Loading 3D model in background...";
        brush->SetColor(M3DColor(0xE2E8F0));
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt->DrawTextW(loadTxt.c_str(), static_cast<UINT32>(loadTxt.size()), fmtBold, m_viewportRect, brush);
        fmtBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // 3. Top-Right XYZ Orientation Axis Gizmo + Controls Legend
    {
        D2D1_POINT_2F gc = D2D1::Point2F(m_viewportRect.right - 36.0f, m_viewportRect.top + 36.0f);
        float axisLen = 22.0f;
        auto drawAxis = [&](Vec3f dir, uint32_t colHex, const wchar_t* lbl) {
            float rx =  dir.x * cosY + dir.z * sinY;
            float rz = -dir.x * sinY + dir.z * cosY;
            float ry =  dir.y * cosP - rz * sinP;
            D2D1_POINT_2F tip = D2D1::Point2F(gc.x + rx * axisLen, gc.y - ry * axisLen);
            brush->SetColor(M3DColor(colHex));
            rt->DrawLine(gc, tip, brush, 2.0f);
            D2D1_RECT_F lr = D2D1::RectF(tip.x - 6.0f, tip.y - 6.0f, tip.x + 8.0f, tip.y + 8.0f);
            rt->DrawTextW(lbl, 1, fmtSmall, lr, brush);
        };
        drawAxis({ 1.0f, 0.0f, 0.0f }, 0xF85149, L"X");
        drawAxis({ 0.0f, 1.0f, 0.0f }, 0x3FB950, L"Y");
        drawAxis({ 0.0f, 0.0f, 1.0f }, 0x58A6FF, L"Z");

        const wchar_t* hint = pl
            ? L"LPM = Obrót 3D   \x2022   PPM = Przesuń   \x2022   Kółko = Zoom"
            : L"LMB = Orbit 3D   \x2022   RMB = Pan   \x2022   Wheel = Zoom";
        D2D1_RECT_F hintR = D2D1::RectF(m_viewportRect.left + 10.0f, m_viewportRect.bottom - 24.0f, m_viewportRect.right - 10.0f, m_viewportRect.bottom - 4.0f);
        brush->SetColor(M3DColor(0x8B949E));
        rt->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), fmtSmall, hintR, brush);
    }

    rt->PopAxisAlignedClip();

    // 4. Bottom Telemetry Bar (Vertices, Triangles, Bounding Box)
    D2D1_RECT_F sbR = D2D1::RectF(rect.left, rect.bottom - statusH, rect.right, rect.bottom);
    brush->SetColor(M3DColor(0xF1F5F9));
    rt->FillRectangle(sbR, brush);
    brush->SetColor(M3DColor(0xCBD5E1));
    rt->DrawLine(D2D1::Point2F(sbR.left, sbR.top), D2D1::Point2F(sbR.right, sbR.top), brush, 1.0f);

    wchar_t statBuf[256] = {};
    swprintf_s(statBuf,
        pl ? L"%s   |   Wierzchołki: %zu   |   Trójkąty: %zu   |   Wymiary: %.2f \xD7 %.2f \xD7 %.2f"
           : L"%s   |   Vertices: %zu   |   Triangles: %zu   |   BBox: %.2f \xD7 %.2f \xD7 %.2f",
        fmtName.empty() ? L"3D" : fmtName.c_str(),
        rawV, rawT, bboxSz.x, bboxSz.y, bboxSz.z);

    D2D1_RECT_F stR = D2D1::RectF(sbR.left + 8.0f, sbR.top, sbR.right - 8.0f, sbR.bottom);
    brush->SetColor(M3DColor(0x334155));
    rt->DrawTextW(statBuf, static_cast<UINT32>(wcslen(statBuf)), fmtSmall, stR, brush);
}

bool Model3DViewer::OnMouseDown(float x, float y, bool rightButton, Lang /*lang*/, std::wstring* /*outStatusToast*/) {
    if (!rightButton) {
        for (const auto& hit : m_toolbarHits) {
            if (PathUtils::PtInRectF(x, y, hit.rect)) {
                switch (hit.id) {
                case BTN_MODE_CYCLE:
                    m_renderMode = static_cast<RenderMode3D>((static_cast<int>(m_renderMode) + 1) % 3);
                    break;
                case BTN_AUTOROTATE:
                    m_autoRotate = !m_autoRotate;
                    m_lastAnimTick = GetTickCount64();
                    break;
                case BTN_GRID:
                    m_showGrid = !m_showGrid;
                    break;
                case BTN_RESET_CAM:
                    ResetCamera();
                    break;
                }
                return true;
            }
        }
    }

    if (PathUtils::PtInRectF(x, y, m_viewportRect)) {
        m_dragLastPt = D2D1::Point2F(x, y);
        if (rightButton || (GetKeyState(VK_SHIFT) & 0x8000)) {
            m_draggingPan = true;
        } else {
            m_draggingOrbit = true;
        }
        return true;
    }
    return false;
}

bool Model3DViewer::OnMouseMove(float x, float y, WPARAM /*keyFlags*/) {
    if (m_draggingOrbit) {
        float dx = x - m_dragLastPt.x;
        float dy = y - m_dragLastPt.y;
        m_dragLastPt = D2D1::Point2F(x, y);
        m_yaw   += dx * 0.011f;
        m_pitch  = std::clamp(m_pitch + dy * 0.011f, -1.52f, 1.52f);
        return true;
    }
    if (m_draggingPan) {
        float dx = x - m_dragLastPt.x;
        float dy = y - m_dragLastPt.y;
        m_dragLastPt = D2D1::Point2F(x, y);
        m_panX += dx;
        m_panY += dy;
        return true;
    }

    int newHov = 0;
    for (const auto& hit : m_toolbarHits) {
        if (PathUtils::PtInRectF(x, y, hit.rect)) {
            newHov = hit.id;
            break;
        }
    }
    if (newHov != m_hoverBtnId) {
        m_hoverBtnId = newHov;
        return true;
    }
    return false;
}

bool Model3DViewer::OnMouseUp() {
    if (m_draggingOrbit || m_draggingPan) {
        m_draggingOrbit = false;
        m_draggingPan = false;
        return true;
    }
    return false;
}

bool Model3DViewer::OnMouseWheel(short delta, float x, float y) {
    if (!PathUtils::PtInRectF(x, y, m_viewportRect)) return false;
    float factor = (delta > 0) ? 1.14f : (1.0f / 1.14f);
    m_zoom = std::clamp(m_zoom * factor, 0.20f, 8.0f);
    return true;
}

} // namespace Explorer2
