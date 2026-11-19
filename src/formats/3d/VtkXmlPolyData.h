#pragma once
// VtkXmlPolyData.h — VTK XML PolyData Surface (.vtp) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseVtkXmlPolyData(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace VtkXmlDetail;
    VtkXmlDocConfig cfg;
    std::string_view xmlHead;
    if (!PrepareVtkXmlDatasetDoc(data, size, "PolyData", cfg, xmlHead)) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
        std::string_view pdXml = FindXmlElementBody(xmlHead, "PolyData");
        if (pdXml.empty()) return false;
        size_t pPos = 0;
        while ((pPos = pdXml.find("<Piece", pPos)) != std::string_view::npos) {
            size_t pGt = pdXml.find('>', pPos);
            size_t pClose = (pGt != std::string_view::npos) ? pdXml.find("</Piece>", pGt) : std::string_view::npos;
            if (pGt == std::string_view::npos || pClose == std::string_view::npos) break;
            std::string_view pieceXml = pdXml.substr(pGt + 1, pClose - (pGt + 1));
            pPos = pClose + 8;

            std::vector<Vec3f> pts;
            if (!DecodePointsSection(pieceXml, cfg, pts)) continue;
            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), pts.begin(), pts.end());
            size_t nv = pts.size();

            DetailVtk::VtkCellList polys, strips, lines;
            if (BuildXmlCellList(FindXmlElementBody(pieceXml, "Polys"), cfg, polys)) {
                for (size_t c = 0; c + 1 < polys.offsets.size(); ++c) {
                    size_t b = polys.offsets[c], e = polys.offsets[c + 1];
                    if (e - b >= 3 && polys.conn[b] < nv) {
                        for (size_t k = b + 1; k + 1 < e; ++k) {
                            if (polys.conn[k] < nv && polys.conn[k + 1] < nv) {
                                mesh.tris.push_back({ vBase + polys.conn[b], vBase + polys.conn[k], vBase + polys.conn[k + 1], {} });
                            }
                        }
                    }
                }
            }
            if (BuildXmlCellList(FindXmlElementBody(pieceXml, "Strips"), cfg, strips)) {
                for (size_t c = 0; c + 1 < strips.offsets.size(); ++c) {
                    size_t b = strips.offsets[c], e = strips.offsets[c + 1];
                    for (size_t k = 0; b + k + 2 < e; ++k) {
                        uint32_t i0 = strips.conn[b + k], i1 = strips.conn[b + k + 1], i2 = strips.conn[b + k + 2];
                        if (i0 < nv && i1 < nv && i2 < nv && i0 != i1 && i1 != i2 && i0 != i2) {
                            if ((k & 1u) == 0u) mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                            else                mesh.tris.push_back({ vBase + i1, vBase + i0, vBase + i2, {} });
                        }
                    }
                }
            }
            if (mesh.tris.empty() && BuildXmlCellList(FindXmlElementBody(pieceXml, "Lines"), cfg, lines)) {
                mesh.vertices.resize(vBase);
                for (size_t c = 0; c + 1 < lines.offsets.size(); ++c) {
                    size_t b = lines.offsets[c], e = lines.offsets[c + 1];
                    for (size_t k = b; k + 1 < e; ++k) {
                        if (lines.conn[k] < nv && lines.conn[k + 1] < nv) {
                            MeshBuild3D::EmitEdgeStrut(mesh, pts[lines.conn[k]], pts[lines.conn[k + 1]], 0.05f);
                        }
                    }
                }
            }
        }
        mesh.formatName = L"VTK XML PolyData Surface (.vtp)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
