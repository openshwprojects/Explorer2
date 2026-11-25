#pragma once
// OpenCascadeBrep.h — OpenCASCADE Boundary Representation (.brep) 3D CAD reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseOpenCascadeBrep(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #52. OpenCASCADE Boundary Representation (.brep — "DBRep_DrawableShape" / "CASCADE Topology")
    if (ext == L".brep" || ext == L".brp") {
        if (sv.find("DBRep_DrawableShape") == std::string_view::npos &&
            sv.find("CASCADE Topology") == std::string_view::npos)
        {
            return false;
        }
        mesh.formatName = (ext == L".brp")
            ? L"OpenCASCADE B-Rep CAD Solid (.brp)"
            : L"OpenCASCADE B-Rep CAD Solid (.brep)";

        // 1. Parse cached "Triangulations" section if present (avoid matching "PolygonOnTriangulations")
        size_t triSec = 0;
        while ((triSec = sv.find("Triangulations", triSec)) != std::string_view::npos) {
            if (triSec == 0 || !std::isalpha(static_cast<unsigned char>(sv[triSec - 1]))) break;
            triSec += 14;
        }
        auto nextWsTok = [](std::string_view& s, std::string_view& out) -> bool {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
            if (s.empty()) return false;
            size_t i = 0;
            while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') ++i;
            out = s.substr(0, i);
            s.remove_prefix(i);
            return true;
        };

        if (triSec != std::string_view::npos) {
            std::string_view rem = sv.substr(triSec + 14), tok;
            int64_t nTabs = 0;
            if (nextWsTok(rem, tok) && ReaderText::ParseInt64Sv(tok, nTabs)) {
                for (int64_t t = 0; t < nTabs && t < 4096; ++t) {
                    int64_t nbNodes = 0, nbTris = 0, hasUV = 0;
                    double defl = 0.0;
                    if (!nextWsTok(rem, tok) || !ReaderText::ParseInt64Sv(tok, nbNodes)) break;
                    if (!nextWsTok(rem, tok) || !ReaderText::ParseInt64Sv(tok, nbTris)) break;
                    if (!nextWsTok(rem, tok) || !ReaderText::ParseInt64Sv(tok, hasUV)) break;
                    if (!nextWsTok(rem, tok) || !ReaderText::ParseDoubleSv(tok, defl)) break;
                    (void)defl;
                    if (nbNodes <= 0 || nbNodes > 200000 || nbTris <= 0 || nbTris > 400000) break;
                    uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
                    bool okTab = true;
                    for (int64_t n = 0; n < nbNodes; ++n) {
                        float x = 0, y = 0, z = 0;
                        std::string_view tx, ty, tz;
                        if (!nextWsTok(rem, tx) || !ReaderText::ParseFloatSv(tx, x) ||
                            !nextWsTok(rem, ty) || !ReaderText::ParseFloatSv(ty, y) ||
                            !nextWsTok(rem, tz) || !ReaderText::ParseFloatSv(tz, z)) {
                            okTab = false;
                            break;
                        }
                        mesh.vertices.push_back({ x, y, z });
                    }
                    if (!okTab) break;
                    if (hasUV) {
                        for (int64_t n = 0; n < nbNodes * 2; ++n) {
                            float dummy = 0;
                            if (!nextWsTok(rem, tok) || !ReaderText::ParseFloatSv(tok, dummy)) {
                                okTab = false;
                                break;
                            }
                        }
                    }
                    if (!okTab) break;
                    for (int64_t k = 0; k < nbTris; ++k) {
                        int64_t i0 = 0, i1 = 0, i2 = 0;
                        std::string_view t0, t1, t2;
                        if (!nextWsTok(rem, t0) || !ReaderText::ParseInt64Sv(t0, i0) ||
                            !nextWsTok(rem, t1) || !ReaderText::ParseInt64Sv(t1, i1) ||
                            !nextWsTok(rem, t2) || !ReaderText::ParseInt64Sv(t2, i2)) {
                            okTab = false;
                            break;
                        }
                        --i0; --i1; --i2;
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
                    if (!okTab) break;
                }
            }
        }

        // 2. If untriangulated, parse Polygon3D polylines if present
        if (mesh.tris.empty()) {
            size_t polySec = sv.find("Polygon3D");
            if (polySec != std::string_view::npos) {
                std::string_view rem = sv.substr(polySec + 9), tok;
                int64_t nPolys = 0;
                if (nextWsTok(rem, tok) && ReaderText::ParseInt64Sv(tok, nPolys)) {
                    for (int64_t pl = 0; pl < nPolys && pl < 4096; ++pl) {
                        int64_t nbNodes = 0, hasParam = 0;
                        double defl = 0.0;
                        if (!nextWsTok(rem, tok) || !ReaderText::ParseInt64Sv(tok, nbNodes)) break;
                        if (!nextWsTok(rem, tok) || !ReaderText::ParseInt64Sv(tok, hasParam)) break;
                        if (!nextWsTok(rem, tok) || !ReaderText::ParseDoubleSv(tok, defl)) break;
                        (void)defl;
                        if (nbNodes < 2 || nbNodes > 100000) break;
                        std::vector<Vec3f> pts;
                        pts.reserve(static_cast<size_t>(std::min<int64_t>(nbNodes, 4096)));
                        bool okPoly = true;
                        for (int64_t n = 0; n < nbNodes; ++n) {
                            float x = 0, y = 0, z = 0;
                            std::string_view tx, ty, tz;
                            if (!nextWsTok(rem, tx) || !ReaderText::ParseFloatSv(tx, x) ||
                                !nextWsTok(rem, ty) || !ReaderText::ParseFloatSv(ty, y) ||
                                !nextWsTok(rem, tz) || !ReaderText::ParseFloatSv(tz, z)) {
                                okPoly = false;
                                break;
                            }
                            pts.push_back({ x, y, z });
                        }
                        if (hasParam && okPoly) {
                            for (int64_t n = 0; n < nbNodes; ++n) {
                                double dummy = 0;
                                if (!nextWsTok(rem, tok) || !ReaderText::ParseDoubleSv(tok, dummy)) {
                                    okPoly = false;
                                    break;
                                }
                            }
                        }
                        if (pts.size() >= 2) {
                            float r = DetailMeshSciCad::ComputePointCloudSpan(pts) * 0.015f;
                            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                                DetailMeshSciCad::AddStrut3D(mesh, pts[i], pts[i + 1], r);
                            }
                        }
                        if (!okPoly) break;
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
