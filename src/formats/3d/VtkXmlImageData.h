#pragma once
// VtkXmlImageData.h — VTK XML ImageData Volume (.vti) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

E2_DRACO_NOINLINE inline bool ParseVtkXmlImageData(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace VtkXmlDetail;
    VtkXmlDocConfig cfg;
    std::string_view xmlHead;
    if (!PrepareVtkXmlDatasetDoc(data, size, "ImageData", cfg, xmlHead)) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
        std::string_view openTag;
        std::string_view imXml = FindXmlElementBody(xmlHead, "ImageData", &openTag);
        if (imXml.empty()) return false;
        int ext[6] = {};
        if (!ParseExtent6(GetXmlAttr(openTag, "WholeExtent"), ext)) return false;
        double origin[3] = { 0, 0, 0 }, spacing[3] = { 1, 1, 1 };
        ParseDoublesN(GetXmlAttr(openTag, "Origin"), origin, 3);
        ParseDoublesN(GetXmlAttr(openTag, "Spacing"), spacing, 3);

        size_t nx = static_cast<size_t>(ext[1] - ext[0] + 1);
        size_t ny = static_cast<size_t>(ext[3] - ext[2] + 1);
        size_t nz = static_cast<size_t>(ext[5] - ext[4] + 1);
        if (nx == 0 || ny == 0 || nz == 0 || nx * ny * nz > 16000000) return false;

        std::vector<double> scalars;
        std::string_view pdSec = FindXmlElementBody(imXml, "PointData");
        if (!pdSec.empty()) {
            ForEachDataArrayInSection(pdSec, [&](std::string_view oTag, std::string_view inner) {
                if (scalars.empty()) DecodeDataArrayDoubles(oTag, inner, cfg, scalars);
            });
        }

        // If 3D scalar field has non-trivial iso-values, extract voxel iso-boundary; else emit structured boundary!
        bool emittedIso = false;
        if (nx >= 2 && ny >= 2 && nz >= 2 && scalars.size() >= nx * ny * nz) {
            double mn = scalars[0], mx = scalars[0];
            for (size_t i = 0; i < nx * ny * nz; ++i) { mn = std::min(mn, scalars[i]); mx = std::max(mx, scalars[i]); }
            if (mx - mn > 1e-6) {
                double iso = 0.5 * (mn + mx);
                auto voxOn = [&](int ix, int iy, int iz) -> bool {
                    if (ix < 0 || iy < 0 || iz < 0 ||
                        static_cast<size_t>(ix) >= nx || static_cast<size_t>(iy) >= ny || static_cast<size_t>(iz) >= nz) return false;
                    return scalars[(static_cast<size_t>(iz) * ny + static_cast<size_t>(iy)) * nx + static_cast<size_t>(ix)] >= iso;
                };
                auto gridPt = [&](double gx, double gy, double gz) -> Vec3f {
                    return {
                        static_cast<float>(origin[0] + (ext[0] + gx) * spacing[0]),
                        static_cast<float>(origin[1] + (ext[2] + gy) * spacing[1]),
                        static_cast<float>(origin[2] + (ext[4] + gz) * spacing[2])
                    };
                };
                for (int iz = 0; iz < static_cast<int>(nz); ++iz) {
                    for (int iy = 0; iy < static_cast<int>(ny); ++iy) {
                        for (int ix = 0; ix < static_cast<int>(nx); ++ix) {
                            if (!voxOn(ix, iy, iz)) continue;
                            auto emitQuad = [&](Vec3f a, Vec3f b, Vec3f c, Vec3f d) {
                                uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                                mesh.vertices.push_back(a); mesh.vertices.push_back(b);
                                mesh.vertices.push_back(c); mesh.vertices.push_back(d);
                                mesh.tris.push_back({ vb + 0, vb + 1, vb + 2, {} });
                                mesh.tris.push_back({ vb + 0, vb + 2, vb + 3, {} });
                            };
                            double x0 = ix - 0.5, x1 = ix + 0.5;
                            double y0 = iy - 0.5, y1 = iy + 0.5;
                            double z0 = iz - 0.5, z1 = iz + 0.5;
                            if (!voxOn(ix - 1, iy, iz)) emitQuad(gridPt(x0,y0,z0), gridPt(x0,y0,z1), gridPt(x0,y1,z1), gridPt(x0,y1,z0));
                            if (!voxOn(ix + 1, iy, iz)) emitQuad(gridPt(x1,y0,z0), gridPt(x1,y1,z0), gridPt(x1,y1,z1), gridPt(x1,y0,z1));
                            if (!voxOn(ix, iy - 1, iz)) emitQuad(gridPt(x0,y0,z0), gridPt(x1,y0,z0), gridPt(x1,y0,z1), gridPt(x0,y0,z1));
                            if (!voxOn(ix, iy + 1, iz)) emitQuad(gridPt(x0,y1,z0), gridPt(x0,y1,z1), gridPt(x1,y1,z1), gridPt(x1,y1,z0));
                            if (!voxOn(ix, iy, iz - 1)) emitQuad(gridPt(x0,y0,z0), gridPt(x0,y1,z0), gridPt(x1,y1,z0), gridPt(x1,y0,z0));
                            if (!voxOn(ix, iy, iz + 1)) emitQuad(gridPt(x0,y0,z1), gridPt(x1,y0,z1), gridPt(x1,y1,z1), gridPt(x0,y1,z1));
                        }
                    }
                }
                emittedIso = !mesh.tris.empty();
            }
        }
        if (!emittedIso) {
            bool is2D = (nz == 1 && nx >= 2 && ny >= 2 && scalars.size() >= nx * ny);
            DetailVtk::EmitStructuredBoundaryVtk(mesh, nx, ny, nz, [&](size_t i, size_t j, size_t k) -> Vec3f {
                double x = origin[0] + (ext[0] + static_cast<double>(i)) * spacing[0];
                double y = origin[1] + (ext[2] + static_cast<double>(j)) * spacing[1];
                double z = origin[2] + (ext[4] + static_cast<double>(k)) * spacing[2];
                if (is2D) y += scalars[j * nx + i];
                return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
            });
        }
        mesh.formatName = L"VTK XML ImageData Volume (.vti)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
