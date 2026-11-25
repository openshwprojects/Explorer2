#pragma once
// VtkXmlUnstructured.h — VTK XML UnstructuredGrid FEM (.vtu) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseVtkXmlUnstructured(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace VtkXmlDetail;
    VtkXmlDocConfig cfg;
    std::string_view xmlHead;
    if (!PrepareVtkXmlDatasetDoc(data, size, "UnstructuredGrid", cfg, xmlHead)) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
        std::string_view ugXml = FindXmlElementBody(xmlHead, "UnstructuredGrid");
        if (ugXml.empty()) return false;

        // Loop over all <Piece> blocks inside UnstructuredGrid
        size_t pPos = 0;
        while ((pPos = ugXml.find("<Piece", pPos)) != std::string_view::npos) {
            size_t pGt = ugXml.find('>', pPos);
            size_t pClose = (pGt != std::string_view::npos) ? ugXml.find("</Piece>", pGt) : std::string_view::npos;
            if (pGt == std::string_view::npos || pClose == std::string_view::npos) break;
            std::string_view pieceXml = ugXml.substr(pGt + 1, pClose - (pGt + 1));
            pPos = pClose + 8;

            std::vector<Vec3f> pts;
            DetailVtk::VtkCellList uCells;
            std::vector<int64_t> cellTypes;
            if (!DecodePointsSection(pieceXml, cfg, pts)) continue;
            if (!BuildXmlCellList(FindXmlElementBody(pieceXml, "Cells"), cfg, uCells, &cellTypes)) continue;

            const size_t numCells = uCells.offsets.size() - 1;
            if (cellTypes.size() < numCells) continue;

            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), pts.begin(), pts.end());
            const size_t nv = pts.size();

            DetailVtk::VtkFaceMap faceMap;
            faceMap.reserve(numCells * 2);

            for (size_t c = 0; c < numCells; ++c) {
                size_t b = uCells.offsets[c], e = uCells.offsets[c + 1];
                size_t n = e - b;
                const uint32_t* cv = uCells.conn.data() + b;
                bool okIdx = (n > 0);
                for (size_t k = 0; k < n; ++k) { if (cv[k] >= nv) { okIdx = false; break; } }
                if (!okIdx) continue;

                if (DetailVtk::RecordVtkVolOrSurfaceCell(faceMap, cellTypes[c], cv, n)) continue;
                if (cellTypes[c] == 7) { // Polygon
                    for (size_t k = 1; k + 1 < n; ++k) {
                        mesh.tris.push_back({ vBase + cv[0], vBase + cv[k], vBase + cv[k + 1], {} });
                    }
                }
            }

            FemSolidSurface::EmitBoundaryFacesDirect(faceMap, mesh, vBase);
        }
        mesh.formatName = L"VTK XML Unstructured Grid FEM (.vtu)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
