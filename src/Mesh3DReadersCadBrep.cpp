// Mesh3DReadersCadBrep.cpp — Mechanical/BIM CAD, B-Rep Kernels (STEP/IGES/ACIS/Parasolid/OCC), DWG/DXF & Toolpath Readers
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

#include "formats/common/MeshFormatCommon.h"
#include "formats/cad/Scad.h"
#include "formats/cad/StepCad.h"
#include "formats/cad/IgesCad.h"
#include "formats/cad/Gcode3D.h"
#include "formats/cad/IfcBim.h"
#include "formats/cad/Catia3dxml.h"
#include "formats/cad/SiemensJt.h"
#include "formats/cad/OpenCascadeBrep.h"
#include "formats/cad/SpatialAcisSat.h"
#include "formats/cad/ParasolidXt.h"
#include "formats/cad/FreeCadFcstd.h"
#include "formats/cad/SpaceClaimScdoc.h"
#include "formats/cad/AutodeskInventor3D.h"
#include "formats/cad/SolidEdge3D.h"
#include "formats/cad/Rhino3dm.h"
#include "formats/cad/AutoCadDwg.h"
#include "formats/cad/AutoCadDxf3D.h"
#include "formats/cad/SolidWorks3D.h"
#include "formats/cad/LDrawCad.h"

namespace MeshReaders {

#if defined(_MSC_VER)
#define E2_MESHCAD_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_MESHCAD_NOINLINE __attribute__((noinline))
#else
#define E2_MESHCAD_NOINLINE
#endif

namespace {

E2_MESHCAD_NOINLINE bool DispatchIsoStepIgesAndBrep(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    LoadedMesh3D& mesh,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".scad") return Formats::ParseOpenScadMesh(sv, mesh);
    if (ext == L".step" || ext == L".stp" || ext == L".stpz" || ext == L".p21") {
        std::vector<uint8_t> stepInf;
        bool parsedMod = false;
        if (size >= 10 && data[0] == 0x1Fu && data[1] == 0x8Bu) {
            if (ZipDeflate::InflateGzip(data, size, stepInf, 128 * 1024 * 1024) && !stepInf.empty()) {
                parsedMod = Formats::ParseStepCadMesh(std::string_view(reinterpret_cast<const char*>(stepInf.data()), stepInf.size()), mesh);
                if (parsedMod && ext == L".stpz") mesh.formatName = L"Compressed ISO 10303-21 STEP (.stpz)";
            }
        } else if (size >= 30 && data[0] == 'P' && data[1] == 'K') {
            std::vector<ZipDeflate::ArchiveItemMeta> zEnts;
            if (ZipDeflate::ListZipEntriesFromMemory(data, size, zEnts)) {
                for (const auto& ze : zEnts) {
                    if (!ze.isDirectory && ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, stepInf, 128 * 1024 * 1024) && !stepInf.empty()) {
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
        return parsedMod;
    }
    if (ext == L".iges" || ext == L".igs") return Formats::ParseIgesCadMesh(sv, mesh);
    if (ext == L".gcode" || ext == L".nc") return Formats::ParseGcodeToolpathMesh(sv, mesh);
    if (ext == L".ifc") return Formats::ParseIfcBimMesh(sv, mesh);
    if (ext == L".3dxml") return Formats::ParseCatia3dxml(data, size, ext, mesh);
    if (ext == L".jt") return Formats::ParseSiemensJt(data, size, ext, mesh);
    if (ext == L".brep" || ext == L".brp") return Formats::ParseOpenCascadeBrep(data, size, ext, mesh);
    if (ext == L".sat" || ext == L".sab") return Formats::ParseSpatialAcisSat(data, size, ext, mesh);
    if (ext == L".x_t" || ext == L".x_b") return Formats::ParseParasolidXt(data, size, ext, mesh);
    outMatched = false;
    return false;
}

E2_MESHCAD_NOINLINE bool DispatchNativeMcdCadPackages(
    const std::wstring& ext,
    const uint8_t* data,
    size_t size,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    bool& outMatched)
{
    outMatched = true;
    if (ext == L".fcstd") return Formats::ParseFreeCadFcstd(data, size, mesh);
    if (ext == L".scdoc") return Formats::ParseSpaceClaimScdoc(data, size, mesh);
    if (ext == L".ipt" || ext == L".iam") return Formats::ParseAutodeskInventor3D(data, size, ext, fullPath, mesh);
    if (ext == L".par" || ext == L".psm") return Formats::ParseSolidEdge3D(data, size, ext, fullPath, mesh);
    if (ext == L".3dm") return Formats::ParseRhino3dmMesh(data, size, mesh);
    if (ext == L".dwg") return Formats::ParseAutoCadDwgMesh(data, size, mesh);
    if (ext == L".dxf") return Formats::ParseAutoCadDxf3DMesh(data, size, mesh);
    if (ext == L".sldprt" || ext == L".sldasm") return Formats::ParseSolidWorksPartOrAssembly(data, size, ext, fullPath, mesh);
    if (ext == L".ldr" || ext == L".mpd") return Formats::ParseLDrawMesh(data, size, fullPath, ext, mesh);
    outMatched = false;
    return false;
}

} // namespace

bool CheckLikely3DDxfHeader(const uint8_t* data, size_t size) {
    return Formats::IsLikely3DDxfBuffer(data, size);
}

bool DispatchMeshCadAndBrep(
    const std::wstring& extLower,
    const uint8_t* data,
    size_t size,
    std::string_view sv,
    const std::wstring& fullPath,
    LoadedMesh3D& outMesh,
    bool& outMatched)
{
    bool ok = DispatchIsoStepIgesAndBrep(extLower, data, size, sv, outMesh, outMatched);
    if (outMatched) return ok;
    return DispatchNativeMcdCadPackages(extLower, data, size, fullPath, outMesh, outMatched);
}

} // namespace MeshReaders
} // namespace Explorer2
