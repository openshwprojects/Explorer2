#pragma once
// VtkXmlStructured.h — VTK XML StructuredGrid (.vts) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseVtkXmlStructured(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace VtkXmlDetail;
    VtkXmlDocConfig cfg;
    std::string_view xmlHead;
    if (!PrepareVtkXmlDatasetDoc(data, size, "StructuredGrid", cfg, xmlHead)) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
        std::string_view openTag;
        std::string_view sgXml = FindXmlElementBody(xmlHead, "StructuredGrid", &openTag);
        if (sgXml.empty()) return false;
        int ext[6] = {};
        if (!ParseExtent6(GetXmlAttr(openTag, "WholeExtent"), ext)) return false;
        size_t nx = static_cast<size_t>(ext[1] - ext[0] + 1);
        size_t ny = static_cast<size_t>(ext[3] - ext[2] + 1);
        size_t nz = static_cast<size_t>(ext[5] - ext[4] + 1);

        std::vector<Vec3f> pts;
        if (!DecodePointsSection(sgXml, cfg, pts) || pts.size() < nx * ny * nz) return false;
        DetailVtk::EmitStructuredBoundaryVtk(mesh, nx, ny, nz, [&](size_t i, size_t j, size_t k) -> Vec3f {
            return pts[(k * ny + j) * nx + i];
        });
        mesh.formatName = L"VTK XML StructuredGrid (.vts)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
