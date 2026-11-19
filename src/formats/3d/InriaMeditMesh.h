#pragma once
// InriaMeditMesh.h — INRIA Medit / Gmsh-Medit Scientific FEM Mesh (.mesh) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {
namespace BspGeoDetail {

inline bool ParseInriaMeditMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 16) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    // B. INRIA Medit / Gmsh-Medit ASCII .mesh ("MeshVersionFormatted")
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.find("MeshVersionFormatted") != std::string_view::npos) {
        const char* cur = sv.data();
        const char* end = cur + sv.size();
        int dim = 3;
        std::string_view tok;

        std::unordered_map<DetailVtk::SortedFaceKey, DetailVtk::BoundaryFaceRec, DetailVtk::SortedFaceKeyHash> faceMap;

        while (DetailVtk::NextTokenVtk(cur, end, tok)) {
            if (!tok.empty() && tok[0] == '#') {
                std::string_view dummy;
                DetailVtk::ReadLineVtk(cur, end, dummy);
                continue;
            }
            std::string kw = DetailVtk::ToLowerVtk(tok);
            if (kw == "end") break;
            if (kw == "dimension") {
                int64_t d = 3;
                if (DetailVtk::NextIntAsciiVtk(cur, end, d) && (d == 2 || d == 3)) dim = static_cast<int>(d);
            } else if (kw == "vertices") {
                int64_t nv = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nv) || nv <= 0 || nv > 2000000) return false;
                mesh.vertices.reserve(static_cast<size_t>(nv));
                for (int64_t i = 0; i < nv; ++i) {
                    double x = 0, y = 0, z = 0, ref = 0;
                    if (!DetailVtk::NextDoubleAsciiVtk(cur, end, x) || !DetailVtk::NextDoubleAsciiVtk(cur, end, y)) return false;
                    if (dim == 3 && !DetailVtk::NextDoubleAsciiVtk(cur, end, z)) return false;
                    DetailVtk::NextDoubleAsciiVtk(cur, end, ref); // entity ref tag
                    mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
                }
            } else if (kw == "triangles") {
                int64_t nt = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nt) || nt < 0 || nt > 4000000) return false;
                for (int64_t i = 0; i < nt; ++i) {
                    int64_t a = 0, b = 0, c = 0, ref = 0;
                    if (!DetailVtk::NextIntAsciiVtk(cur, end, a) || !DetailVtk::NextIntAsciiVtk(cur, end, b) ||
                        !DetailVtk::NextIntAsciiVtk(cur, end, c)) return false;
                    DetailVtk::NextIntAsciiVtk(cur, end, ref);
                    if (a >= 1 && b >= 1 && c >= 1 &&
                        static_cast<size_t>(a) <= mesh.vertices.size() &&
                        static_cast<size_t>(b) <= mesh.vertices.size() &&
                        static_cast<size_t>(c) <= mesh.vertices.size())
                    {
                        uint32_t f[3] = { static_cast<uint32_t>(a - 1), static_cast<uint32_t>(b - 1), static_cast<uint32_t>(c - 1) };
                        DetailVtk::RecordFace3DVtk(faceMap, f, 3, false);
                    }
                }
            } else if (kw == "quadrilaterals") {
                int64_t nq = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nq) || nq < 0 || nq > 4000000) return false;
                for (int64_t i = 0; i < nq; ++i) {
                    int64_t a = 0, b = 0, c = 0, d = 0, ref = 0;
                    if (!DetailVtk::NextIntAsciiVtk(cur, end, a) || !DetailVtk::NextIntAsciiVtk(cur, end, b) ||
                        !DetailVtk::NextIntAsciiVtk(cur, end, c) || !DetailVtk::NextIntAsciiVtk(cur, end, d)) return false;
                    DetailVtk::NextIntAsciiVtk(cur, end, ref);
                    if (a >= 1 && b >= 1 && c >= 1 && d >= 1 &&
                        static_cast<size_t>(std::max({ a, b, c, d })) <= mesh.vertices.size())
                    {
                        uint32_t q[4] = { static_cast<uint32_t>(a - 1), static_cast<uint32_t>(b - 1), static_cast<uint32_t>(c - 1), static_cast<uint32_t>(d - 1) };
                        DetailVtk::RecordFace3DVtk(faceMap, q, 4, false);
                    }
                }
            } else if (kw == "tetrahedra") {
                int64_t nt = 0;
                if (!DetailVtk::NextIntAsciiVtk(cur, end, nt) || nt < 0 || nt > 4000000) return false;
                for (int64_t i = 0; i < nt; ++i) {
                    int64_t v[4] = {}, ref = 0;
                    if (!DetailVtk::NextIntAsciiVtk(cur, end, v[0]) || !DetailVtk::NextIntAsciiVtk(cur, end, v[1]) ||
                        !DetailVtk::NextIntAsciiVtk(cur, end, v[2]) || !DetailVtk::NextIntAsciiVtk(cur, end, v[3])) return false;
                    DetailVtk::NextIntAsciiVtk(cur, end, ref);
                    if (v[0] >= 1 && v[1] >= 1 && v[2] >= 1 && v[3] >= 1 &&
                        static_cast<size_t>(std::max({ v[0], v[1], v[2], v[3] })) <= mesh.vertices.size())
                    {
                        uint32_t c0 = static_cast<uint32_t>(v[0] - 1), c1 = static_cast<uint32_t>(v[1] - 1);
                        uint32_t c2 = static_cast<uint32_t>(v[2] - 1), c3 = static_cast<uint32_t>(v[3] - 1);
                        const uint32_t f0[3] = { c0, c2, c1 }, f1[3] = { c0, c1, c3 }, f2[3] = { c1, c2, c3 }, f3[3] = { c2, c0, c3 };
                        DetailVtk::RecordFace3DVtk(faceMap, f0, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f1, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f2, 3, true);
                        DetailVtk::RecordFace3DVtk(faceMap, f3, 3, true);
                    }
                }
            }
        }

        for (const auto& kv : faceMap) {
            const auto& f = kv.second;
            if (f.volCount == 1 || (f.volCount == 0 && f.explicit2D)) {
                if (f.n == 3) mesh.tris.push_back({ f.orig[0], f.orig[1], f.orig[2], {} });
                else if (f.n == 4) {
                    mesh.tris.push_back({ f.orig[0], f.orig[1], f.orig[2], {} });
                    mesh.tris.push_back({ f.orig[0], f.orig[2], f.orig[3], {} });
                }
            }
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) return false;
        mesh.formatName = L"INRIA Medit Scientific FEM Mesh (.mesh)";
        return true;
    }

    return false;
}

} // namespace BspGeoDetail
} // namespace Formats
