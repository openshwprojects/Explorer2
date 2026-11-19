#pragma once
// OpenCascadeBrep.h — OpenCASCADE Boundary Representation (.brep) 3D CAD reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseOpenCascadeBrep(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #52. OpenCASCADE Boundary Representation (.brep — "DBRep_DrawableShape" / "CASCADE Topology")
    if (ext == L".brep") {
        if (sv.find("DBRep_DrawableShape") == std::string_view::npos &&
            sv.find("CASCADE Topology") == std::string_view::npos)
        {
            return false;
        }
        mesh.formatName = L"OpenCASCADE B-Rep CAD Solid (.brep)";

        // 1. Parse cached "Triangulations" section if present (avoid matching "PolygonOnTriangulations")
        size_t triSec = 0;
        while ((triSec = sv.find("Triangulations", triSec)) != std::string_view::npos) {
            if (triSec == 0 || !std::isalpha(static_cast<unsigned char>(sv[triSec - 1]))) break;
            triSec += 14;
        }
        if (triSec != std::string_view::npos) {
            std::string nullSafeBrep(sv.substr(triSec + 14));
            const char* p = nullSafeBrep.c_str();
            const char* end = p + nullSafeBrep.size();
            char* ep = nullptr;
            long nTabs = std::strtol(p, &ep, 10); p = ep;
            for (long t = 0; t < nTabs && t < 4096 && p < end; ++t) {
                long nbNodes = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                long nbTris  = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                long hasUV   = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                double defl  = std::strtod(p, &ep); (void)defl; p = ep;
                if (nbNodes <= 0 || nbNodes > 200000 || nbTris <= 0 || nbTris > 400000) break;
                uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                for (long n = 0; n < nbNodes && p < end; ++n) {
                    float x = std::strtof(p, &ep); p = ep;
                    float y = std::strtof(p, &ep); p = ep;
                    float z = std::strtof(p, &ep); p = ep;
                    mesh.vertices.push_back({ x, y, z });
                }
                if (hasUV) {
                    for (long n = 0; n < nbNodes * 2 && p < end; ++n) { std::strtof(p, &ep); p = ep; }
                }
                for (long k = 0; k < nbTris && p < end; ++k) {
                    long i0 = std::strtol(p, &ep, 10) - 1; p = ep;
                    long i1 = std::strtol(p, &ep, 10) - 1; p = ep;
                    long i2 = std::strtol(p, &ep, 10) - 1; p = ep;
                    if (i0 >= 0 && i1 >= 0 && i2 >= 0 &&
                        i0 < nbNodes && i1 < nbNodes && i2 < nbNodes)
                    {
                        mesh.tris.push_back({
                            vBase + static_cast<uint32_t>(i0),
                            vBase + static_cast<uint32_t>(i1),
                            vBase + static_cast<uint32_t>(i2), {}
                        });
                    }
                }
            }
        }

        // 2. If untriangulated, parse Polygon3D polylines if present
        if (mesh.tris.empty()) {
            size_t polySec = sv.find("Polygon3D");
            if (polySec != std::string_view::npos) {
                const char* p = sv.data() + polySec + 9;
                const char* end = sv.data() + sv.size();
                char* ep = nullptr;
                long nPolys = std::strtol(p, &ep, 10); p = ep;
                for (long pl = 0; pl < nPolys && pl < 4096 && p < end; ++pl) {
                    long nbNodes = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                    long hasParam = std::strtol(p, &ep, 10); if (ep == p) break; p = ep;
                    std::strtod(p, &ep); p = ep; // deflection
                    if (nbNodes < 2 || nbNodes > 100000) break;
                    std::vector<Vec3f> pts;
                    for (long n = 0; n < nbNodes && p < end; ++n) {
                        float x = std::strtof(p, &ep); p = ep;
                        float y = std::strtof(p, &ep); p = ep;
                        float z = std::strtof(p, &ep); p = ep;
                        pts.push_back({ x, y, z });
                    }
                    if (hasParam) {
                        for (long n = 0; n < nbNodes && p < end; ++n) { std::strtod(p, &ep); p = ep; }
                    }
                    float r = Detail3D200::ComputePointCloudSpan(pts) * 0.015f;
                    for (size_t i = 0; i + 1 < pts.size(); ++i) {
                        Detail3D200::AddStrut3D(mesh, pts[i], pts[i + 1], r);
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
