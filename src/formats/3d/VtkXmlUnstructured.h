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

            std::unordered_map<DetailVtk::SortedFaceKey, DetailVtk::BoundaryFaceRec, DetailVtk::SortedFaceKeyHash> faceMap;
            faceMap.reserve(numCells * 2);

            for (size_t c = 0; c < numCells; ++c) {
                size_t b = uCells.offsets[c], e = uCells.offsets[c + 1];
                size_t n = e - b;
                const uint32_t* cv = uCells.conn.data() + b;
                bool okIdx = (n > 0);
                for (size_t k = 0; k < n; ++k) { if (cv[k] >= nv) { okIdx = false; break; } }
                if (!okIdx) continue;

                switch (cellTypes[c]) {
                case 10: case 24: // Tetra
                    if (n >= 4) {
                        const uint32_t f0[3] = { cv[0], cv[2], cv[1] };
                        const uint32_t f1[3] = { cv[0], cv[1], cv[3] };
                        const uint32_t f2[3] = { cv[1], cv[2], cv[3] };
                        const uint32_t f3[3] = { cv[2], cv[0], cv[3] };
                        DetailVtk::RecordFace3DVtk(faceMap, f0, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f1, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f2, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f3, 3, true);
                    }
                    break;
                case 11: case 12: case 25: case 29: // Voxel / Hexahedron
                    if (n >= 8) {
                        uint32_t h[8];
                        if (cellTypes[c] == 11) {
                            h[0]=cv[0]; h[1]=cv[1]; h[2]=cv[3]; h[3]=cv[2];
                            h[4]=cv[4]; h[5]=cv[5]; h[6]=cv[7]; h[7]=cv[6];
                        } else {
                            for (int k = 0; k < 8; ++k) h[k] = cv[k];
                        }
                        const uint32_t q0[4] = { h[0], h[3], h[2], h[1] };
                        const uint32_t q1[4] = { h[4], h[5], h[6], h[7] };
                        const uint32_t q2[4] = { h[0], h[1], h[5], h[4] };
                        const uint32_t q3[4] = { h[1], h[2], h[6], h[5] };
                        const uint32_t q4[4] = { h[2], h[3], h[7], h[6] };
                        const uint32_t q5[4] = { h[3], h[0], h[4], h[7] };
                        DetailVtk::RecordFace3DVtk(faceMap, q0, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q1, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q2, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q3, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q4, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q5, 4, true);
                    }
                    break;
                case 13: case 26: // Wedge
                    if (n >= 6) {
                        const uint32_t t0[3] = { cv[0], cv[1], cv[2] };
                        const uint32_t t1[3] = { cv[3], cv[5], cv[4] };
                        const uint32_t q0[4] = { cv[0], cv[3], cv[4], cv[1] };
                        const uint32_t q1[4] = { cv[1], cv[4], cv[5], cv[2] };
                        const uint32_t q2[4] = { cv[2], cv[5], cv[3], cv[0] };
                        DetailVtk::RecordFace3DVtk(faceMap, t0, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, t1, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q0, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q1, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, q2, 4, true);
                    }
                    break;
                case 14: case 27: // Pyramid
                    if (n >= 5) {
                        const uint32_t q0[4] = { cv[0], cv[3], cv[2], cv[1] };
                        const uint32_t t0[3] = { cv[0], cv[1], cv[4] };
                        const uint32_t t1[3] = { cv[1], cv[2], cv[4] };
                        const uint32_t t2[3] = { cv[2], cv[3], cv[4] };
                        const uint32_t t3[3] = { cv[3], cv[0], cv[4] };
                        DetailVtk::RecordFace3DVtk(faceMap, q0, 4, true);
                        DetailVtk::RecordFace3DVtk(faceMap, t0, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, t1, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, t2, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, t3, 3, true);
                    }
                    break;
                case 5: case 22: // Triangle
                    if (n >= 3) DetailVtk::RecordFace3DVtk(faceMap, cv, 3, false);
                    break;
                case 8: // Pixel
                    if (n >= 4) { const uint32_t q[4] = { cv[0], cv[1], cv[3], cv[2] }; DetailVtk::RecordFace3DVtk(faceMap, q, 4, false); }
                    break;
                case 9: case 23: case 28: // Quad
                    if (n >= 4) DetailVtk::RecordFace3DVtk(faceMap, cv, 4, false);
                    break;
                case 7: // Polygon
                    for (size_t k = 1; k + 1 < n; ++k) mesh.tris.push_back({ vBase + cv[0], vBase + cv[k], vBase + cv[k + 1], {} });
                    break;
                default: break;
                }
            }

            for (const auto& kv : faceMap) {
                const auto& f = kv.second;
                if (f.volCount == 1 || (f.volCount == 0 && f.explicit2D)) {
                    if (f.n == 3) {
                        mesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[1], vBase + f.orig[2], {} });
                    } else if (f.n == 4) {
                        mesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[1], vBase + f.orig[2], {} });
                        mesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[2], vBase + f.orig[3], {} });
                    }
                }
            }
        }
        mesh.formatName = L"VTK XML Unstructured Grid FEM (.vtu)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
