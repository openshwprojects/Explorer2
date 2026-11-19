#pragma once
// VtkXmlRectilinear.h — VTK XML RectilinearGrid (.vtr) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseVtkXmlRectilinear(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace VtkXmlDetail;
    VtkXmlDocConfig cfg;
    std::string_view xmlHead;
    if (!PrepareVtkXmlDatasetDoc(data, size, "RectilinearGrid", cfg, xmlHead)) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
        std::string_view openTag;
        std::string_view rgXml = FindXmlElementBody(xmlHead, "RectilinearGrid", &openTag);
        if (rgXml.empty()) return false;
        int ext[6] = {};
        if (!ParseExtent6(GetXmlAttr(openTag, "WholeExtent"), ext)) return false;
        size_t nx = static_cast<size_t>(ext[1] - ext[0] + 1);
        size_t ny = static_cast<size_t>(ext[3] - ext[2] + 1);
        size_t nz = static_cast<size_t>(ext[5] - ext[4] + 1);

        std::string_view coordSec = FindXmlElementBody(rgXml, "Coordinates");
        std::vector<double> coords[3];
        int ci = 0;
        ForEachDataArrayInSection(coordSec, [&](std::string_view oTag, std::string_view inner) {
            if (ci < 3) DecodeDataArrayDoubles(oTag, inner, cfg, coords[ci++]);
        });
        if (coords[0].size() < nx || coords[1].size() < ny || coords[2].size() < nz) return false;
        DetailVtk::EmitStructuredBoundaryVtk(mesh, nx, ny, nz, [&](size_t i, size_t j, size_t k) -> Vec3f {
            return { static_cast<float>(coords[0][i]), static_cast<float>(coords[1][j]), static_cast<float>(coords[2][k]) };
        });
        mesh.formatName = L"VTK XML RectilinearGrid (.vtr)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
