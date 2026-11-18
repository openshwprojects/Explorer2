#pragma once
#include "MeshBuild3D.h"
#include <memory>
// Scad.h — OpenSCAD Parametric CSG Script (.scad) Recursive-Descent Parser & 4x4 CSG Mesh Evaluator

namespace Formats {

namespace DetailScad {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

struct Mat4 {
    // m[row * 4 + col]
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    static Mat4 Identity() { return Mat4{}; }

    static Mat4 Mul(const Mat4& a, const Mat4& b) {
        Mat4 r;
        MeshBuild3D::MatMul4RowMajor(a.m, b.m, r.m);
        return r;
    }

    static Mat4 Translate(float tx, float ty, float tz) {
        Mat4 r;
        r.m[3] = tx; r.m[7] = ty; r.m[11] = tz;
        return r;
    }

    static Mat4 Scale(float sx, float sy, float sz) {
        Mat4 r;
        r.m[0] = sx; r.m[5] = sy; r.m[10] = sz;
        return r;
    }

    static Mat4 RotateAxisAngle(float deg, float ax, float ay, float az) {
        float len = std::sqrt(ax * ax + ay * ay + az * az);
        if (len < 1e-7f) return Identity();
        ax /= len; ay /= len; az /= len;
        float rad = static_cast<float>(deg * kDegToRad);
        float c = std::cos(rad), s = std::sin(rad), ic = 1.0f - c;
        Mat4 r;
        r.m[0]  = c + ax * ax * ic;      r.m[1]  = ax * ay * ic - az * s; r.m[2]  = ax * az * ic + ay * s;
        r.m[4]  = ay * ax * ic + az * s; r.m[5]  = c + ay * ay * ic;      r.m[6]  = ay * az * ic - ax * s;
        r.m[8]  = az * ax * ic - ay * s; r.m[9]  = az * ay * ic + ax * s; r.m[10] = c + az * az * ic;
        return r;
    }

    static Mat4 RotateXYZ(float rxDeg, float ryDeg, float rzDeg) {
        // OpenSCAD rotate([rx, ry, rz]) applies Rx, then Ry, then Rz: M = Rz * Ry * Rx
        Mat4 rx = RotateAxisAngle(rxDeg, 1, 0, 0);
        Mat4 ry = RotateAxisAngle(ryDeg, 0, 1, 0);
        Mat4 rz = RotateAxisAngle(rzDeg, 0, 0, 1);
        return Mul(rz, Mul(ry, rx));
    }

    static Mat4 Mirror(float nx, float ny, float nz) {
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1e-7f) return Identity();
        nx /= len; ny /= len; nz /= len;
        Mat4 r;
        r.m[0]  = 1.0f - 2.0f * nx * nx; r.m[1]  = -2.0f * nx * ny;       r.m[2]  = -2.0f * nx * nz;
        r.m[4]  = -2.0f * ny * nx;       r.m[5]  = 1.0f - 2.0f * ny * ny; r.m[6]  = -2.0f * ny * nz;
        r.m[8]  = -2.0f * nz * nx;       r.m[9]  = -2.0f * nz * ny;       r.m[10] = 1.0f - 2.0f * nz * nz;
        return r;
    }

    Vec3f TransformPoint(Vec3f p) const {
        return {
            m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
        };
    }

    float Det3x3() const {
        return m[0] * (m[5] * m[10] - m[6] * m[9]) -
               m[1] * (m[4] * m[10] - m[6] * m[8]) +
               m[2] * (m[4] * m[9]  - m[5] * m[8]);
    }
};

// ============================================================================
// Lightweight BSP-Tree 3D Solid CSG (Union / Difference / Intersection) + 3D Convex Hull
// ============================================================================
struct CsgPoly {
    std::vector<Vec3f> verts;
    Vec3f nrm = { 0, 0, 1 };
    float w = 0.0f;

    void ComputePlane() {
        if (verts.size() < 3) return;
        Vec3f e1 = { verts[1].x - verts[0].x, verts[1].y - verts[0].y, verts[1].z - verts[0].z };
        Vec3f e2 = { verts[2].x - verts[0].x, verts[2].y - verts[0].y, verts[2].z - verts[0].z };
        Vec3f c = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
        float l = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
        if (l > 1e-7f) { nrm = { c.x / l, c.y / l, c.z / l }; }
        w = nrm.x * verts[0].x + nrm.y * verts[0].y + nrm.z * verts[0].z;
    }

    void Flip() {
        std::reverse(verts.begin(), verts.end());
        nrm = { -nrm.x, -nrm.y, -nrm.z };
        w = -w;
    }
};

struct BspNode {
    Vec3f nrm = { 0, 0, 1 };
    float w = 0.0f;
    bool hasPlane = false;
    std::vector<CsgPoly> polys;
    std::unique_ptr<BspNode> front;
    std::unique_ptr<BspNode> back;

    static void SplitPoly(const Vec3f& pn, float pw, const CsgPoly& poly,
                          std::vector<CsgPoly>& coplanarFront, std::vector<CsgPoly>& coplanarBack,
                          std::vector<CsgPoly>& outFront, std::vector<CsgPoly>& outBack)
    {
        constexpr float kEps = 1e-4f;
        enum { COPLANAR = 0, FRONT = 1, BACK = 2, SPANNING = 3 };
        int polyType = 0;
        std::vector<int> types(poly.verts.size());
        for (size_t i = 0; i < poly.verts.size(); ++i) {
            float t = (pn.x * poly.verts[i].x + pn.y * poly.verts[i].y + pn.z * poly.verts[i].z) - pw;
            int tp = (t < -kEps) ? BACK : ((t > kEps) ? FRONT : COPLANAR);
            polyType |= tp;
            types[i] = tp;
        }
        if (polyType == COPLANAR) {
            float d = pn.x * poly.nrm.x + pn.y * poly.nrm.y + pn.z * poly.nrm.z;
            if (d > 0.0f) coplanarFront.push_back(poly);
            else          coplanarBack.push_back(poly);
        } else if (polyType == FRONT) {
            outFront.push_back(poly);
        } else if (polyType == BACK) {
            outBack.push_back(poly);
        } else {
            CsgPoly f, b;
            f.nrm = poly.nrm; f.w = poly.w;
            b.nrm = poly.nrm; b.w = poly.w;
            size_t n = poly.verts.size();
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                int ti = types[i], tj = types[j];
                const Vec3f& vi = poly.verts[i];
                const Vec3f& vj = poly.verts[j];
                if (ti != BACK) f.verts.push_back(vi);
                if (ti != FRONT) b.verts.push_back(vi);
                if ((ti | tj) == SPANNING) {
                    Vec3f dv = { vj.x - vi.x, vj.y - vi.y, vj.z - vi.z };
                    float denom = pn.x * dv.x + pn.y * dv.y + pn.z * dv.z;
                    float t = (std::fabs(denom) > 1e-8f)
                        ? ((pw - (pn.x * vi.x + pn.y * vi.y + pn.z * vi.z)) / denom)
                        : 0.5f;
                    t = std::clamp(t, 0.0f, 1.0f);
                    Vec3f mid = { vi.x + dv.x * t, vi.y + dv.y * t, vi.z + dv.z * t };
                    f.verts.push_back(mid);
                    b.verts.push_back(mid);
                }
            }
            if (f.verts.size() >= 3) outFront.push_back(std::move(f));
            if (b.verts.size() >= 3) outBack.push_back(std::move(b));
        }
    }

    void Invert() {
        for (auto& p : polys) p.Flip();
        nrm = { -nrm.x, -nrm.y, -nrm.z };
        w = -w;
        if (front) front->Invert();
        if (back)  back->Invert();
        std::swap(front, back);
    }

    std::vector<CsgPoly> ClipPolygons(const std::vector<CsgPoly>& list, int depth = 0) const {
        if (!hasPlane || depth > 40) return list;
        std::vector<CsgPoly> listFront, listBack;
        for (const auto& p : list) {
            SplitPoly(nrm, w, p, listFront, listBack, listFront, listBack);
        }
        if (front) listFront = front->ClipPolygons(listFront, depth + 1);
        if (back)  listBack  = back->ClipPolygons(listBack, depth + 1);
        else       listBack.clear();
        listFront.insert(listFront.end(), listBack.begin(), listBack.end());
        return listFront;
    }

    void ClipTo(const BspNode& bsp, int depth = 0) {
        if (depth > 40) return;
        polys = bsp.ClipPolygons(polys);
        if (front) front->ClipTo(bsp, depth + 1);
        if (back)  back->ClipTo(bsp, depth + 1);
    }

    void AllPolygons(std::vector<CsgPoly>& out) const {
        out.insert(out.end(), polys.begin(), polys.end());
        if (front) front->AllPolygons(out);
        if (back)  back->AllPolygons(out);
    }

    void Build(const std::vector<CsgPoly>& list, int depth = 0) {
        if (list.empty()) return;
        if (!hasPlane) {
            nrm = list[0].nrm;
            w   = list[0].w;
            hasPlane = true;
        }
        if (depth >= 36) {
            polys.insert(polys.end(), list.begin(), list.end());
            return;
        }
        std::vector<CsgPoly> fList, bList;
        for (const auto& p : list) {
            SplitPoly(nrm, w, p, polys, polys, fList, bList);
        }
        if (!fList.empty()) {
            if (!front) front = std::make_unique<BspNode>();
            front->Build(fList, depth + 1);
        }
        if (!bList.empty()) {
            if (!back) back = std::make_unique<BspNode>();
            back->Build(bList, depth + 1);
        }
    }
};

struct ScadGeom {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> tris;
    std::vector<std::vector<std::pair<float, float>>> paths2D;

    bool Empty3D() const { return verts.empty() || tris.empty(); }

    void Append(const ScadGeom& o) {
        if (!o.Empty3D()) {
            uint32_t base = static_cast<uint32_t>(verts.size());
            verts.insert(verts.end(), o.verts.begin(), o.verts.end());
            for (const auto& t : o.tris) {
                tris.push_back({ base + t[0], base + t[1], base + t[2] });
            }
        }
        for (const auto& p : o.paths2D) paths2D.push_back(p);
    }

    void ApplyTransform(const Mat4& mat) {
        bool flip = (mat.Det3x3() < 0.0f);
        for (auto& v : verts) v = mat.TransformPoint(v);
        if (flip) {
            for (auto& t : tris) std::swap(t[1], t[2]);
        }
        float det2D = mat.m[0] * mat.m[5] - mat.m[1] * mat.m[4];
        for (auto& path : paths2D) {
            for (auto& pt : path) {
                Vec3f p3 = mat.TransformPoint({ pt.first, pt.second, 0.0f });
                pt = { p3.x, p3.y };
            }
            if (det2D < 0.0f) std::reverse(path.begin(), path.end());
        }
    }

    std::vector<CsgPoly> ToCsgPolys() const {
        std::vector<CsgPoly> out;
        out.reserve(tris.size());
        for (const auto& t : tris) {
            if (t[0] < verts.size() && t[1] < verts.size() && t[2] < verts.size()) {
                CsgPoly cp;
                cp.verts = { verts[t[0]], verts[t[1]], verts[t[2]] };
                cp.ComputePlane();
                float e1x = cp.verts[1].x - cp.verts[0].x, e1y = cp.verts[1].y - cp.verts[0].y, e1z = cp.verts[1].z - cp.verts[0].z;
                float e2x = cp.verts[2].x - cp.verts[0].x, e2y = cp.verts[2].y - cp.verts[0].y, e2z = cp.verts[2].z - cp.verts[0].z;
                float cx = e1y * e2z - e1z * e2y, cy = e1z * e2x - e1x * e2z, cz = e1x * e2y - e1y * e2x;
                if (cx * cx + cy * cy + cz * cz > 1e-12f) out.push_back(std::move(cp));
            }
        }
        return out;
    }

    static ScadGeom FromCsgPolys(const std::vector<CsgPoly>& polys) {
        ScadGeom g;
        for (const auto& p : polys) {
            if (p.verts.size() < 3) continue;
            uint32_t b = static_cast<uint32_t>(g.verts.size());
            for (const auto& v : p.verts) g.verts.push_back(v);
            for (size_t i = 1; i + 1 < p.verts.size(); ++i) {
                g.tris.push_back({ b, b + static_cast<uint32_t>(i), b + static_cast<uint32_t>(i + 1) });
            }
        }
        return g;
    }
};

inline ScadGeom CsgSubtract(const ScadGeom& a, const ScadGeom& b) {
    if (a.Empty3D() || b.Empty3D()) return a;
    if (a.tris.size() + b.tris.size() > 1600) {
        // High-poly fallback: combine base with inverted cutter shell
        ScadGeom res = a;
        uint32_t base = static_cast<uint32_t>(res.verts.size());
        res.verts.insert(res.verts.end(), b.verts.begin(), b.verts.end());
        for (const auto& t : b.tris) res.tris.push_back({ base + t[0], base + t[2], base + t[1] });
        return res;
    }
    BspNode ta, tb;
    ta.Build(a.ToCsgPolys());
    tb.Build(b.ToCsgPolys());
    ta.Invert();
    ta.ClipTo(tb);
    tb.ClipTo(ta);
    tb.Invert();
    tb.ClipTo(ta);
    tb.Invert();
    std::vector<CsgPoly> out;
    ta.AllPolygons(out);
    tb.AllPolygons(out);
    BspNode merged;
    merged.polys = std::move(out);
    merged.Invert();
    return ScadGeom::FromCsgPolys(merged.polys);
}

inline ScadGeom CsgIntersect(const ScadGeom& a, const ScadGeom& b) {
    if (a.Empty3D() || b.Empty3D()) return {};
    if (a.tris.size() + b.tris.size() > 1600) return a;
    BspNode ta, tb;
    ta.Build(a.ToCsgPolys());
    tb.Build(b.ToCsgPolys());
    ta.Invert();
    tb.ClipTo(ta);
    tb.Invert();
    ta.ClipTo(tb);
    tb.ClipTo(ta);
    std::vector<CsgPoly> out;
    ta.AllPolygons(out);
    tb.AllPolygons(out);
    BspNode merged;
    merged.polys = std::move(out);
    merged.Invert();
    return ScadGeom::FromCsgPolys(merged.polys);
}

// Incremental 3D Convex Hull (Horizon-Ridge algorithm)
inline ScadGeom ComputeConvexHull3D(const std::vector<Vec3f>& rawPts) {
    ScadGeom out;
    if (rawPts.size() < 4) return out;

    // Deduplicate and subsample if very dense
    std::vector<Vec3f> pts;
    size_t step = std::max<size_t>(1, rawPts.size() / 512);
    for (size_t i = 0; i < rawPts.size(); i += step) {
        bool dup = false;
        for (const auto& q : pts) {
            float dx = rawPts[i].x - q.x, dy = rawPts[i].y - q.y, dz = rawPts[i].z - q.z;
            if (dx * dx + dy * dy + dz * dz < 1e-8f) { dup = true; break; }
        }
        if (!dup) pts.push_back(rawPts[i]);
    }
    if (pts.size() < 4) return out;

    auto sub = [](Vec3f a, Vec3f b) -> Vec3f { return { a.x - b.x, a.y - b.y, a.z - b.z }; };
    auto cross = [](Vec3f a, Vec3f b) -> Vec3f {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    };
    auto dot = [](Vec3f a, Vec3f b) -> float { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto len2 = [&](Vec3f a) -> float { return dot(a, a); };

    // Find initial non-degenerate tetrahedron (i0, i1, i2, i3)
    size_t i0 = 0, i1 = 0;
    float maxD = -1.0f;
    for (size_t i = 1; i < pts.size(); ++i) {
        float d = len2(sub(pts[i], pts[i0]));
        if (d > maxD) { maxD = d; i1 = i; }
    }
    if (maxD < 1e-8f) return out;

    size_t i2 = 0;
    maxD = -1.0f;
    Vec3f dir01 = sub(pts[i1], pts[i0]);
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i == i0 || i == i1) continue;
        float d = len2(cross(dir01, sub(pts[i], pts[i0])));
        if (d > maxD) { maxD = d; i2 = i; }
    }
    if (maxD < 1e-8f) return out;

    Vec3f baseN = cross(sub(pts[i1], pts[i0]), sub(pts[i2], pts[i0]));
    size_t i3 = 0;
    maxD = -1.0f;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (i == i0 || i == i1 || i == i2) continue;
        float d = std::fabs(dot(baseN, sub(pts[i], pts[i0])));
        if (d > maxD) { maxD = d; i3 = i; }
    }
    if (maxD < 1e-6f) return out;

    struct HullFace {
        uint32_t a, b, c;
        Vec3f n;
        float d;
        bool alive = true;
    };
    auto makeFace = [&](uint32_t a, uint32_t b, uint32_t c, Vec3f interior) -> HullFace {
        Vec3f n = cross(sub(pts[b], pts[a]), sub(pts[c], pts[a]));
        float l = std::sqrt(len2(n));
        if (l > 1e-8f) { n.x /= l; n.y /= l; n.z /= l; }
        if (dot(n, sub(interior, pts[a])) > 0.0f) {
            std::swap(b, c);
            n = { -n.x, -n.y, -n.z };
        }
        return { a, b, c, n, dot(n, pts[a]), true };
    };

    Vec3f center = {
        0.25f * (pts[i0].x + pts[i1].x + pts[i2].x + pts[i3].x),
        0.25f * (pts[i0].y + pts[i1].y + pts[i2].y + pts[i3].y),
        0.25f * (pts[i0].z + pts[i1].z + pts[i2].z + pts[i3].z)
    };
    std::vector<HullFace> faces = {
        makeFace(static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i2), center),
        makeFace(static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i3), center),
        makeFace(static_cast<uint32_t>(i1), static_cast<uint32_t>(i2), static_cast<uint32_t>(i3), center),
        makeFace(static_cast<uint32_t>(i2), static_cast<uint32_t>(i0), static_cast<uint32_t>(i3), center)
    };

    for (size_t pi = 0; pi < pts.size(); ++pi) {
        if (pi == i0 || pi == i1 || pi == i2 || pi == i3) continue;
        const Vec3f& p = pts[pi];
        std::vector<size_t> visible;
        for (size_t f = 0; f < faces.size(); ++f) {
            if (faces[f].alive && (dot(faces[f].n, p) - faces[f].d) > 1e-4f) {
                visible.push_back(f);
            }
        }
        if (visible.empty()) continue;

        std::vector<std::pair<uint32_t, uint32_t>> dirEdges;
        for (size_t vf : visible) {
            faces[vf].alive = false;
            dirEdges.push_back({ faces[vf].a, faces[vf].b });
            dirEdges.push_back({ faces[vf].b, faces[vf].c });
            dirEdges.push_back({ faces[vf].c, faces[vf].a });
        }
        // Horizon edges are those whose opposite directed edge was NOT deleted
        for (const auto& e : dirEdges) {
            bool shared = false;
            for (const auto& o : dirEdges) {
                if (o.first == e.second && o.second == e.first) { shared = true; break; }
            }
            if (!shared) {
                faces.push_back(makeFace(e.first, e.second, static_cast<uint32_t>(pi), center));
            }
        }
    }

    out.verts = std::move(pts);
    for (const auto& f : faces) {
        if (f.alive) out.tris.push_back({ f.a, f.b, f.c });
    }
    return out;
}

// Ear-clipping helper for 2D OpenSCAD polygons & extrusion caps
inline void EarClip2DPoly(const std::vector<std::pair<float, float>>& poly, std::vector<std::array<uint32_t, 3>>& outTris, uint32_t baseIdx, bool reverse) {
    if (poly.size() < 3) return;
    double area2 = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        area2 += static_cast<double>(poly[i].first) * poly[j].second - static_cast<double>(poly[j].first) * poly[i].second;
    }
    std::vector<size_t> ring(poly.size());
    std::iota(ring.begin(), ring.end(), 0);
    bool flipped = (area2 < 0.0);
    if (flipped) std::reverse(ring.begin(), ring.end());

    auto emit = [&](size_t a, size_t b, size_t c) {
        uint32_t ia = baseIdx + static_cast<uint32_t>(a);
        uint32_t ib = baseIdx + static_cast<uint32_t>(b);
        uint32_t ic = baseIdx + static_cast<uint32_t>(c);
        if (reverse ^ flipped) outTris.push_back({ ia, ic, ib });
        else                   outTris.push_back({ ia, ib, ic });
    };

    auto cross2D = [&](size_t a, size_t b, size_t c) -> float {
        float ax = poly[b].first - poly[a].first, ay = poly[b].second - poly[a].second;
        float bx = poly[c].first - poly[a].first, by = poly[c].second - poly[a].second;
        return ax * by - ay * bx;
    };
    auto inTri = [&](size_t p, size_t a, size_t b, size_t c) -> bool {
        return cross2D(a, b, p) >= -1e-6f && cross2D(b, c, p) >= -1e-6f && cross2D(c, a, p) >= -1e-6f;
    };

    int guard = static_cast<int>(ring.size() * ring.size()) + 8;
    while (ring.size() > 3 && guard-- > 0) {
        bool ok = false;
        size_t m = ring.size();
        for (size_t i = 0; i < m; ++i) {
            size_t prev = ring[(i + m - 1) % m], curr = ring[i], next = ring[(i + 1) % m];
            if (cross2D(prev, curr, next) <= 1e-7f) continue;
            bool inside = false;
            for (size_t j = 0; j < m; ++j) {
                size_t t = ring[j];
                if (t != prev && t != curr && t != next && inTri(t, prev, curr, next)) { inside = true; break; }
            }
            if (!inside) {
                emit(prev, curr, next);
                ring.erase(ring.begin() + static_cast<ptrdiff_t>(i));
                ok = true;
                break;
            }
        }
        if (!ok) {
            emit(ring[0], ring[1], ring[2]);
            ring.erase(ring.begin() + 1);
        }
    }
    if (ring.size() == 3) emit(ring[0], ring[1], ring[2]);
}

// ============================================================================
// OpenSCAD Lexer, AST & Evaluator
// ============================================================================
enum class TokKind {
    End, Ident, Num, Str,
    LParen, RParen, LBrack, RBrack, LBrace, RBrace,
    Comma, Semi, Colon, Question, Assign,
    Plus, Minus, Star, Slash, Percent, Caret, Bang, Hash,
    Eq, Ne, Lt, Le, Gt, Ge, And, Or
};

struct Token {
    TokKind kind = TokKind::End;
    std::string text;
    double num = 0.0;
};

inline std::vector<Token> TokenizeScad(std::string_view sv) {
    std::vector<Token> toks;
    size_t i = 0;
    const size_t n = sv.size();
    while (i < n && toks.size() < 200000) {
        unsigned char ch = static_cast<unsigned char>(sv[i]);
        if (ch <= 32) { ++i; continue; }
        if (ch == '/' && i + 1 < n && sv[i + 1] == '/') {
            i += 2;
            while (i < n && sv[i] != '\n') ++i;
            continue;
        }
        if (ch == '/' && i + 1 < n && sv[i + 1] == '*') {
            size_t ce = sv.find("*/", i + 2);
            i = (ce == std::string_view::npos) ? n : (ce + 2);
            continue;
        }
        if (ch == '"') {
            ++i;
            std::string s;
            while (i < n && sv[i] != '"') {
                if (sv[i] == '\\' && i + 1 < n) { ++i; s.push_back(sv[i]); }
                else s.push_back(sv[i]);
                ++i;
            }
            if (i < n && sv[i] == '"') ++i;
            toks.push_back({ TokKind::Str, std::move(s), 0.0 });
            continue;
        }
        if ((ch >= '0' && ch <= '9') || (ch == '.' && i + 1 < n && sv[i + 1] >= '0' && sv[i + 1] <= '9')) {
            char buf[64] = {};
            size_t rem = std::min<size_t>(n - i, 63);
            std::memcpy(buf, sv.data() + i, rem);
            char* ep = nullptr;
            double v = std::strtod(buf, &ep);
            size_t consumed = (ep > buf) ? static_cast<size_t>(ep - buf) : 1;
            toks.push_back({ TokKind::Num, {}, std::isfinite(v) ? v : 0.0 });
            i += consumed;
            continue;
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '$') {
            size_t s = i++;
            while (i < n) {
                unsigned char c2 = static_cast<unsigned char>(sv[i]);
                if ((c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z') || (c2 >= '0' && c2 <= '9') || c2 == '_') ++i;
                else break;
            }
            toks.push_back({ TokKind::Ident, std::string(sv.substr(s, i - s)), 0.0 });
            continue;
        }
        if (i + 1 < n) {
            std::string_view two = sv.substr(i, 2);
            if (two == "==") { toks.push_back({ TokKind::Eq, {}, 0 }); i += 2; continue; }
            if (two == "!=") { toks.push_back({ TokKind::Ne, {}, 0 }); i += 2; continue; }
            if (two == "<=") { toks.push_back({ TokKind::Le, {}, 0 }); i += 2; continue; }
            if (two == ">=") { toks.push_back({ TokKind::Ge, {}, 0 }); i += 2; continue; }
            if (two == "&&") { toks.push_back({ TokKind::And, {}, 0 }); i += 2; continue; }
            if (two == "||") { toks.push_back({ TokKind::Or, {}, 0 }); i += 2; continue; }
        }
        TokKind k = TokKind::End;
        switch (ch) {
        case '(': k = TokKind::LParen; break;
        case ')': k = TokKind::RParen; break;
        case '[': k = TokKind::LBrack; break;
        case ']': k = TokKind::RBrack; break;
        case '{': k = TokKind::LBrace; break;
        case '}': k = TokKind::RBrace; break;
        case ',': k = TokKind::Comma; break;
        case ';': k = TokKind::Semi; break;
        case ':': k = TokKind::Colon; break;
        case '?': k = TokKind::Question; break;
        case '=': k = TokKind::Assign; break;
        case '+': k = TokKind::Plus; break;
        case '-': k = TokKind::Minus; break;
        case '*': k = TokKind::Star; break;
        case '/': k = TokKind::Slash; break;
        case '%': k = TokKind::Percent; break;
        case '^': k = TokKind::Caret; break;
        case '!': k = TokKind::Bang; break;
        case '#': k = TokKind::Hash; break;
        case '<': k = TokKind::Lt; break;
        case '>': k = TokKind::Gt; break;
        default: break;
        }
        if (k != TokKind::End) toks.push_back({ k, {}, 0.0 });
        ++i;
    }
    toks.push_back({ TokKind::End, {}, 0.0 });
    return toks;
}

struct ScadVal {
    enum Kind { Undef, Bool, Num, Str, Vec } kind = Undef;
    double num = 0.0;
    std::string str;
    std::vector<ScadVal> vec;

    static ScadVal MakeUndef() { return {}; }
    static ScadVal MakeBool(bool b) { ScadVal v; v.kind = Bool; v.num = b ? 1.0 : 0.0; return v; }
    static ScadVal MakeNum(double d) { ScadVal v; v.kind = Num; v.num = std::isfinite(d) ? d : 0.0; return v; }
    static ScadVal MakeStr(std::string s) { ScadVal v; v.kind = Str; v.str = std::move(s); return v; }
    static ScadVal MakeVec(std::vector<ScadVal> el) { ScadVal v; v.kind = Vec; v.vec = std::move(el); return v; }

    bool AsBool(bool defVal = false) const {
        if (kind == Bool) return num != 0.0;
        if (kind == Num)  return num != 0.0;
        if (kind == Str)  return !str.empty();
        if (kind == Vec)  return !vec.empty();
        return defVal;
    }
    double AsNum(double defVal = 0.0) const {
        if (kind == Num || kind == Bool) return num;
        return defVal;
    }
    Vec3f AsVec3(float defX = 0.0f, float defY = 0.0f, float defZ = 0.0f) const {
        if (kind == Num) {
            float v = static_cast<float>(num);
            return { v, v, v };
        }
        if (kind == Vec) {
            return {
                vec.size() > 0 ? static_cast<float>(vec[0].AsNum(defX)) : defX,
                vec.size() > 1 ? static_cast<float>(vec[1].AsNum(defY)) : defY,
                vec.size() > 2 ? static_cast<float>(vec[2].AsNum(defZ)) : defZ
            };
        }
        return { defX, defY, defZ };
    }
};

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct ArgItem {
    std::string name; // empty if positional
    ExprPtr expr;
};

struct Expr {
    enum Type { Lit, Var, Unary, Binary, Ternary, VecLit, Range, Index, Call } type = Lit;
    ScadVal lit;
    std::string name;
    TokKind op = TokKind::End;
    ExprPtr a, b, c;
    std::vector<ExprPtr> elems;
    std::vector<ArgItem> args;
};

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct ParamDecl {
    std::string name;
    ExprPtr defaultExpr;
};

struct Stmt {
    enum Type { Noop, Assign, Block, If, For, Let, ModDef, FuncDef, ModCall } type = Noop;
    bool disabled = false; // '*' prefix modifier in OpenSCAD
    std::string name;
    ExprPtr expr;
    std::vector<ParamDecl> params;
    std::vector<ArgItem> args;
    std::vector<StmtPtr> children;
    StmtPtr body;
    StmtPtr elseBody;
};

class ScadParser {
public:
    explicit ScadParser(const std::vector<Token>& t) : m_toks(t) {}

    std::vector<StmtPtr> ParseProgram() {
        std::vector<StmtPtr> out;
        while (!AtEnd()) {
            if (Match(TokKind::Semi)) continue;
            // Ignore include <...> / use <...> directives cleanly
            if (Peek().kind == TokKind::Ident && (Peek().text == "include" || Peek().text == "use")) {
                Advance();
                if (Match(TokKind::Lt)) {
                    while (!AtEnd() && !Match(TokKind::Gt)) Advance();
                }
                Match(TokKind::Semi);
                continue;
            }
            StmtPtr s = ParseStmt(0);
            if (s) out.push_back(std::move(s));
            else   Advance();
        }
        return out;
    }

private:
    const std::vector<Token>& m_toks;
    size_t m_pos = 0;

    const Token& Peek(size_t ahead = 0) const {
        size_t idx = std::min(m_pos + ahead, m_toks.size() - 1);
        return m_toks[idx];
    }
    bool AtEnd() const { return Peek().kind == TokKind::End; }
    const Token& Advance() {
        const Token& t = Peek();
        if (m_pos + 1 < m_toks.size()) ++m_pos;
        return t;
    }
    bool Match(TokKind k) {
        if (Peek().kind == k) { Advance(); return true; }
        return false;
    }

    std::vector<ParamDecl> ParseParamDecls() {
        std::vector<ParamDecl> out;
        Match(TokKind::LParen);
        while (!AtEnd() && Peek().kind != TokKind::RParen) {
            if (Peek().kind == TokKind::Ident) {
                ParamDecl pd;
                pd.name = Advance().text;
                if (Match(TokKind::Assign)) pd.defaultExpr = ParseExpr(0);
                out.push_back(std::move(pd));
            } else {
                Advance();
            }
            if (!Match(TokKind::Comma)) break;
        }
        Match(TokKind::RParen);
        return out;
    }

    std::vector<ArgItem> ParseCallArgs() {
        std::vector<ArgItem> out;
        Match(TokKind::LParen);
        while (!AtEnd() && Peek().kind != TokKind::RParen) {
            ArgItem ai;
            if (Peek().kind == TokKind::Ident && Peek(1).kind == TokKind::Assign) {
                ai.name = Advance().text;
                Advance(); // '='
            }
            ai.expr = ParseExpr(0);
            if (ai.expr) out.push_back(std::move(ai));
            if (!Match(TokKind::Comma)) break;
        }
        Match(TokKind::RParen);
        return out;
    }

    StmtPtr ParseStmt(int depth) {
        if (AtEnd() || depth > 64) return nullptr;

        bool disableMod = false;
        while (Peek().kind == TokKind::Star || Peek().kind == TokKind::Bang ||
               Peek().kind == TokKind::Hash || Peek().kind == TokKind::Percent)
        {
            if (Advance().kind == TokKind::Star) disableMod = true;
        }

        if (Match(TokKind::Semi)) {
            auto s = std::make_shared<Stmt>();
            s->type = Stmt::Noop;
            return s;
        }
        if (Match(TokKind::LBrace)) {
            auto blk = std::make_shared<Stmt>();
            blk->type = Stmt::Block;
            blk->disabled = disableMod;
            while (!AtEnd() && Peek().kind != TokKind::RBrace) {
                StmtPtr c = ParseStmt(depth + 1);
                if (c) blk->children.push_back(std::move(c));
                else   Advance();
            }
            Match(TokKind::RBrace);
            return blk;
        }

        if (Peek().kind == TokKind::Ident) {
            const std::string kw = Peek().text;
            if (kw == "module" && Peek(1).kind == TokKind::Ident) {
                Advance();
                auto md = std::make_shared<Stmt>();
                md->type = Stmt::ModDef;
                md->name = Advance().text;
                md->params = ParseParamDecls();
                md->body = ParseStmt(depth + 1);
                return md;
            }
            if (kw == "function" && Peek(1).kind == TokKind::Ident) {
                Advance();
                auto fd = std::make_shared<Stmt>();
                fd->type = Stmt::FuncDef;
                fd->name = Advance().text;
                fd->params = ParseParamDecls();
                Match(TokKind::Assign);
                fd->expr = ParseExpr(depth + 1);
                Match(TokKind::Semi);
                return fd;
            }
            if (kw == "if") {
                Advance();
                auto ifs = std::make_shared<Stmt>();
                ifs->type = Stmt::If;
                ifs->disabled = disableMod;
                Match(TokKind::LParen);
                ifs->expr = ParseExpr(depth + 1);
                Match(TokKind::RParen);
                ifs->body = ParseStmt(depth + 1);
                if (Peek().kind == TokKind::Ident && Peek().text == "else") {
                    Advance();
                    ifs->elseBody = ParseStmt(depth + 1);
                }
                return ifs;
            }
            if (kw == "for" || kw == "intersection_for") {
                Advance();
                auto fs = std::make_shared<Stmt>();
                fs->type = Stmt::For;
                fs->name = kw;
                fs->disabled = disableMod;
                fs->args = ParseCallArgs();
                fs->body = ParseStmt(depth + 1);
                return fs;
            }
            if (kw == "let") {
                Advance();
                auto ls = std::make_shared<Stmt>();
                ls->type = Stmt::Let;
                ls->disabled = disableMod;
                ls->args = ParseCallArgs();
                ls->body = ParseStmt(depth + 1);
                return ls;
            }
            if (Peek(1).kind == TokKind::Assign) {
                auto as = std::make_shared<Stmt>();
                as->type = Stmt::Assign;
                as->name = Advance().text;
                Advance(); // '='
                as->expr = ParseExpr(depth + 1);
                Match(TokKind::Semi);
                return as;
            }
            if (Peek(1).kind == TokKind::LParen) {
                auto mc = std::make_shared<Stmt>();
                mc->type = Stmt::ModCall;
                mc->disabled = disableMod;
                mc->name = Advance().text;
                mc->args = ParseCallArgs();
                if (!Match(TokKind::Semi)) {
                    mc->body = ParseStmt(depth + 1);
                }
                return mc;
            }
        }
        return nullptr;
    }

    ExprPtr ParseExpr(int depth) {
        if (depth > 64) return std::make_shared<Expr>();
        return ParseTernary(depth + 1);
    }

    ExprPtr ParseTernary(int depth) {
        auto cond = ParseOr(depth);
        if (Match(TokKind::Question)) {
            auto t = std::make_shared<Expr>();
            t->type = Expr::Ternary;
            t->a = std::move(cond);
            t->b = ParseExpr(depth + 1);
            Match(TokKind::Colon);
            t->c = ParseExpr(depth + 1);
            return t;
        }
        return cond;
    }

    ExprPtr ParseOr(int depth) {
        auto lhs = ParseAnd(depth);
        while (Match(TokKind::Or)) {
            auto e = std::make_shared<Expr>();
            e->type = Expr::Binary; e->op = TokKind::Or;
            e->a = std::move(lhs); e->b = ParseAnd(depth);
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprPtr ParseAnd(int depth) {
        auto lhs = ParseCmp(depth);
        while (Match(TokKind::And)) {
            auto e = std::make_shared<Expr>();
            e->type = Expr::Binary; e->op = TokKind::And;
            e->a = std::move(lhs); e->b = ParseCmp(depth);
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprPtr ParseCmp(int depth) {
        auto lhs = ParseAdd(depth);
        while (Peek().kind == TokKind::Eq || Peek().kind == TokKind::Ne ||
               Peek().kind == TokKind::Lt || Peek().kind == TokKind::Le ||
               Peek().kind == TokKind::Gt || Peek().kind == TokKind::Ge)
        {
            TokKind op = Advance().kind;
            auto e = std::make_shared<Expr>();
            e->type = Expr::Binary; e->op = op;
            e->a = std::move(lhs); e->b = ParseAdd(depth);
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprPtr ParseAdd(int depth) {
        auto lhs = ParseMul(depth);
        while (Peek().kind == TokKind::Plus || Peek().kind == TokKind::Minus) {
            TokKind op = Advance().kind;
            auto e = std::make_shared<Expr>();
            e->type = Expr::Binary; e->op = op;
            e->a = std::move(lhs); e->b = ParseMul(depth);
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprPtr ParseMul(int depth) {
        auto lhs = ParsePow(depth);
        while (Peek().kind == TokKind::Star || Peek().kind == TokKind::Slash || Peek().kind == TokKind::Percent) {
            TokKind op = Advance().kind;
            auto e = std::make_shared<Expr>();
            e->type = Expr::Binary; e->op = op;
            e->a = std::move(lhs); e->b = ParsePow(depth);
            lhs = std::move(e);
        }
        return lhs;
    }

    ExprPtr ParsePow(int depth) {
        auto lhs = ParseUnary(depth);
        if (Match(TokKind::Caret)) {
            auto e = std::make_shared<Expr>();
            e->type = Expr::Binary; e->op = TokKind::Caret;
            e->a = std::move(lhs); e->b = ParsePow(depth + 1);
            return e;
        }
        return lhs;
    }

    ExprPtr ParseUnary(int depth) {
        if (Peek().kind == TokKind::Minus || Peek().kind == TokKind::Plus || Peek().kind == TokKind::Bang) {
            TokKind op = Advance().kind;
            auto e = std::make_shared<Expr>();
            e->type = Expr::Unary; e->op = op;
            e->a = ParseUnary(depth + 1);
            return e;
        }
        return ParsePostfix(depth);
    }

    ExprPtr ParsePostfix(int depth) {
        auto base = ParsePrimary(depth);
        while (Match(TokKind::LBrack)) {
            auto idx = std::make_shared<Expr>();
            idx->type = Expr::Index;
            idx->a = std::move(base);
            idx->b = ParseExpr(depth + 1);
            Match(TokKind::RBrack);
            base = std::move(idx);
        }
        return base;
    }

    ExprPtr ParsePrimary(int depth) {
        if (Peek().kind == TokKind::Num) {
            auto e = std::make_shared<Expr>();
            e->type = Expr::Lit;
            e->lit = ScadVal::MakeNum(Advance().num);
            return e;
        }
        if (Peek().kind == TokKind::Str) {
            auto e = std::make_shared<Expr>();
            e->type = Expr::Lit;
            e->lit = ScadVal::MakeStr(Advance().text);
            return e;
        }
        if (Match(TokKind::LParen)) {
            auto inner = ParseExpr(depth + 1);
            Match(TokKind::RParen);
            return inner;
        }
        if (Match(TokKind::LBrack)) {
            if (Match(TokKind::RBrack)) {
                auto v = std::make_shared<Expr>();
                v->type = Expr::VecLit;
                return v;
            }
            ExprPtr first = ParseExpr(depth + 1);
            if (Match(TokKind::Colon)) {
                // Range: [start : end] or [start : step : end]
                auto r = std::make_shared<Expr>();
                r->type = Expr::Range;
                r->a = std::move(first);
                ExprPtr second = ParseExpr(depth + 1);
                if (Match(TokKind::Colon)) {
                    r->b = std::move(second);
                    r->c = ParseExpr(depth + 1);
                } else {
                    r->c = std::move(second);
                }
                Match(TokKind::RBrack);
                return r;
            }
            auto v = std::make_shared<Expr>();
            v->type = Expr::VecLit;
            if (first) v->elems.push_back(std::move(first));
            while (Match(TokKind::Comma)) {
                if (Peek().kind == TokKind::RBrack) break;
                ExprPtr el = ParseExpr(depth + 1);
                if (el) v->elems.push_back(std::move(el));
            }
            Match(TokKind::RBrack);
            return v;
        }
        if (Peek().kind == TokKind::Ident) {
            std::string id = Advance().text;
            if (id == "true" || id == "false") {
                auto e = std::make_shared<Expr>();
                e->type = Expr::Lit;
                e->lit = ScadVal::MakeBool(id == "true");
                return e;
            }
            if (id == "undef") {
                auto e = std::make_shared<Expr>();
                e->type = Expr::Lit;
                e->lit = ScadVal::MakeUndef();
                return e;
            }
            if (id == "PI") {
                auto e = std::make_shared<Expr>();
                e->type = Expr::Lit;
                e->lit = ScadVal::MakeNum(kPi);
                return e;
            }
            if (Peek().kind == TokKind::LParen) {
                auto c = std::make_shared<Expr>();
                c->type = Expr::Call;
                c->name = std::move(id);
                c->args = ParseCallArgs();
                return c;
            }
            auto v = std::make_shared<Expr>();
            v->type = Expr::Var;
            v->name = std::move(id);
            return v;
        }
        Advance();
        return std::make_shared<Expr>();
    }
};

struct EvalEnv {
    std::unordered_map<std::string, ScadVal> vars;
    std::unordered_map<std::string, const Stmt*> modules;
    std::unordered_map<std::string, const Stmt*> funcs;
};

class ScadEvaluator {
public:
    int opBudget = 80000;

    ScadVal EvalExpr(const Expr* e, const EvalEnv& env, int depth = 0) {
        if (!e || depth > 48 || --opBudget <= 0) return ScadVal::MakeUndef();

        switch (e->type) {
        case Expr::Lit:
            return e->lit;
        case Expr::Var: {
            auto it = env.vars.find(e->name);
            return (it != env.vars.end()) ? it->second : ScadVal::MakeUndef();
        }
        case Expr::Unary: {
            ScadVal v = EvalExpr(e->a.get(), env, depth + 1);
            if (e->op == TokKind::Minus) {
                if (v.kind == ScadVal::Vec) {
                    std::vector<ScadVal> neg;
                    neg.reserve(v.vec.size());
                    for (const auto& el : v.vec) neg.push_back(ScadVal::MakeNum(-el.AsNum()));
                    return ScadVal::MakeVec(std::move(neg));
                }
                return ScadVal::MakeNum(-v.AsNum());
            }
            if (e->op == TokKind::Bang) return ScadVal::MakeBool(!v.AsBool());
            return v;
        }
        case Expr::Binary: {
            if (e->op == TokKind::And) return ScadVal::MakeBool(EvalExpr(e->a.get(), env, depth + 1).AsBool() && EvalExpr(e->b.get(), env, depth + 1).AsBool());
            if (e->op == TokKind::Or)  return ScadVal::MakeBool(EvalExpr(e->a.get(), env, depth + 1).AsBool() || EvalExpr(e->b.get(), env, depth + 1).AsBool());
            ScadVal l = EvalExpr(e->a.get(), env, depth + 1);
            ScadVal r = EvalExpr(e->b.get(), env, depth + 1);

            if ((e->op == TokKind::Plus || e->op == TokKind::Minus) && l.kind == ScadVal::Vec && r.kind == ScadVal::Vec) {
                size_t m = std::min(l.vec.size(), r.vec.size());
                std::vector<ScadVal> res;
                res.reserve(m);
                for (size_t k = 0; k < m; ++k) {
                    double lv = l.vec[k].AsNum(), rv = r.vec[k].AsNum();
                    res.push_back(ScadVal::MakeNum(e->op == TokKind::Plus ? (lv + rv) : (lv - rv)));
                }
                return ScadVal::MakeVec(std::move(res));
            }
            if (e->op == TokKind::Star && (l.kind == ScadVal::Vec || r.kind == ScadVal::Vec)) {
                if (l.kind == ScadVal::Vec && r.kind == ScadVal::Num) {
                    std::vector<ScadVal> res;
                    for (const auto& el : l.vec) res.push_back(ScadVal::MakeNum(el.AsNum() * r.num));
                    return ScadVal::MakeVec(std::move(res));
                }
                if (l.kind == ScadVal::Num && r.kind == ScadVal::Vec) {
                    std::vector<ScadVal> res;
                    for (const auto& el : r.vec) res.push_back(ScadVal::MakeNum(l.num * el.AsNum()));
                    return ScadVal::MakeVec(std::move(res));
                }
            }
            if (e->op == TokKind::Slash && l.kind == ScadVal::Vec && r.kind == ScadVal::Num && std::fabs(r.num) > 1e-12) {
                std::vector<ScadVal> res;
                for (const auto& el : l.vec) res.push_back(ScadVal::MakeNum(el.AsNum() / r.num));
                return ScadVal::MakeVec(std::move(res));
            }

            double a = l.AsNum(), b = r.AsNum();
            switch (e->op) {
            case TokKind::Plus:    return ScadVal::MakeNum(a + b);
            case TokKind::Minus:   return ScadVal::MakeNum(a - b);
            case TokKind::Star:    return ScadVal::MakeNum(a * b);
            case TokKind::Slash:   return ScadVal::MakeNum(std::fabs(b) > 1e-12 ? (a / b) : 0.0);
            case TokKind::Percent: return ScadVal::MakeNum(std::fabs(b) > 1e-12 ? std::fmod(a, b) : 0.0);
            case TokKind::Caret:   return ScadVal::MakeNum(std::pow(a, b));
            case TokKind::Eq:      return ScadVal::MakeBool(std::fabs(a - b) <= 1e-9);
            case TokKind::Ne:      return ScadVal::MakeBool(std::fabs(a - b) > 1e-9);
            case TokKind::Lt:      return ScadVal::MakeBool(a < b);
            case TokKind::Le:      return ScadVal::MakeBool(a <= b);
            case TokKind::Gt:      return ScadVal::MakeBool(a > b);
            case TokKind::Ge:      return ScadVal::MakeBool(a >= b);
            default: break;
            }
            return ScadVal::MakeUndef();
        }
        case Expr::Ternary:
            return EvalExpr(e->a.get(), env, depth + 1).AsBool()
                ? EvalExpr(e->b.get(), env, depth + 1)
                : EvalExpr(e->c.get(), env, depth + 1);
        case Expr::VecLit: {
            std::vector<ScadVal> out;
            out.reserve(e->elems.size());
            for (const auto& el : e->elems) out.push_back(EvalExpr(el.get(), env, depth + 1));
            return ScadVal::MakeVec(std::move(out));
        }
        case Expr::Range: {
            double s = EvalExpr(e->a.get(), env, depth + 1).AsNum(0.0);
            double endV = EvalExpr(e->c.get(), env, depth + 1).AsNum(0.0);
            double step = e->b ? EvalExpr(e->b.get(), env, depth + 1).AsNum(1.0) : ((endV >= s) ? 1.0 : -1.0);
            std::vector<ScadVal> seq;
            if (std::fabs(step) > 1e-9) {
                int maxSteps = 2048;
                if (step > 0.0) {
                    for (double v = s; v <= endV + 1e-7 && maxSteps-- > 0; v += step) seq.push_back(ScadVal::MakeNum(v));
                } else {
                    for (double v = s; v >= endV - 1e-7 && maxSteps-- > 0; v += step) seq.push_back(ScadVal::MakeNum(v));
                }
            }
            return ScadVal::MakeVec(std::move(seq));
        }
        case Expr::Index: {
            ScadVal arr = EvalExpr(e->a.get(), env, depth + 1);
            int idx = static_cast<int>(std::floor(EvalExpr(e->b.get(), env, depth + 1).AsNum(-1.0) + 0.5));
            if (arr.kind == ScadVal::Vec && idx >= 0 && static_cast<size_t>(idx) < arr.vec.size()) {
                return arr.vec[idx];
            }
            return ScadVal::MakeUndef();
        }
        case Expr::Call: {
            std::vector<ScadVal> av;
            av.reserve(e->args.size());
            for (const auto& a : e->args) av.push_back(EvalExpr(a.expr.get(), env, depth + 1));
            const std::string& fn = e->name;
            auto a0 = [&]() { return av.empty() ? 0.0 : av[0].AsNum(); };
            auto a1 = [&]() { return av.size() < 2 ? 0.0 : av[1].AsNum(); };
            // OpenSCAD trig functions operate in degrees!
            if (fn == "sin")   return ScadVal::MakeNum(std::sin(a0() * kDegToRad));
            if (fn == "cos")   return ScadVal::MakeNum(std::cos(a0() * kDegToRad));
            if (fn == "tan")   return ScadVal::MakeNum(std::tan(a0() * kDegToRad));
            if (fn == "asin")  return ScadVal::MakeNum(std::asin(std::clamp(a0(), -1.0, 1.0)) * kRadToDeg);
            if (fn == "acos")  return ScadVal::MakeNum(std::acos(std::clamp(a0(), -1.0, 1.0)) * kRadToDeg);
            if (fn == "atan")  return ScadVal::MakeNum(std::atan(a0()) * kRadToDeg);
            if (fn == "atan2") return ScadVal::MakeNum(std::atan2(a0(), a1()) * kRadToDeg);
            if (fn == "sqrt")  return ScadVal::MakeNum(a0() >= 0.0 ? std::sqrt(a0()) : 0.0);
            if (fn == "abs")   return ScadVal::MakeNum(std::fabs(a0()));
            if (fn == "floor") return ScadVal::MakeNum(std::floor(a0()));
            if (fn == "ceil")  return ScadVal::MakeNum(std::ceil(a0()));
            if (fn == "round") return ScadVal::MakeNum(std::round(a0()));
            if (fn == "sign")  return ScadVal::MakeNum(a0() > 0.0 ? 1.0 : (a0() < 0.0 ? -1.0 : 0.0));
            if (fn == "pow")   return ScadVal::MakeNum(std::pow(a0(), a1()));
            if (fn == "exp")   return ScadVal::MakeNum(std::exp(a0()));
            if (fn == "ln")    return ScadVal::MakeNum(a0() > 0.0 ? std::log(a0()) : 0.0);
            if (fn == "log")   return ScadVal::MakeNum(a0() > 0.0 ? std::log10(a0()) : 0.0);
            if (fn == "len")   return (!av.empty() && av[0].kind == ScadVal::Vec) ? ScadVal::MakeNum(static_cast<double>(av[0].vec.size())) : ScadVal::MakeNum(0.0);
            if (fn == "norm") {
                if (!av.empty() && av[0].kind == ScadVal::Vec) {
                    double s = 0.0;
                    for (const auto& el : av[0].vec) { double x = el.AsNum(); s += x * x; }
                    return ScadVal::MakeNum(std::sqrt(s));
                }
                return ScadVal::MakeNum(std::fabs(a0()));
            }
            if (fn == "min" || fn == "max") {
                const std::vector<ScadVal>* src = &av;
                if (av.size() == 1 && av[0].kind == ScadVal::Vec) src = &av[0].vec;
                if (src->empty()) return ScadVal::MakeNum(0.0);
                double best = (*src)[0].AsNum();
                for (size_t i = 1; i < src->size(); ++i) {
                    double v = (*src)[i].AsNum();
                    best = (fn == "min") ? std::min(best, v) : std::max(best, v);
                }
                return ScadVal::MakeNum(best);
            }
            if (fn == "concat") {
                std::vector<ScadVal> out;
                for (const auto& item : av) {
                    if (item.kind == ScadVal::Vec) out.insert(out.end(), item.vec.begin(), item.vec.end());
                    else out.push_back(item);
                }
                return ScadVal::MakeVec(std::move(out));
            }
            // User-defined function
            auto fit = env.funcs.find(fn);
            if (fit != env.funcs.end() && fit->second && fit->second->expr) {
                EvalEnv fenv = BindCallEnv(fit->second->params, e->args, env, depth + 1);
                return EvalExpr(fit->second->expr.get(), fenv, depth + 1);
            }
            return ScadVal::MakeUndef();
        }
        }
        return ScadVal::MakeUndef();
    }

    EvalEnv BindCallEnv(const std::vector<ParamDecl>& params, const std::vector<ArgItem>& callArgs, const EvalEnv& callerEnv, int depth) {
        EvalEnv local = callerEnv;
        size_t posIdx = 0;
        std::set<std::string> explicitlySet;
        for (const auto& ca : callArgs) {
            ScadVal val = EvalExpr(ca.expr.get(), callerEnv, depth + 1);
            if (!ca.name.empty()) {
                local.vars[ca.name] = val;
                explicitlySet.insert(ca.name);
            } else if (posIdx < params.size()) {
                local.vars[params[posIdx].name] = val;
                explicitlySet.insert(params[posIdx].name);
                ++posIdx;
            }
        }
        for (const auto& pd : params) {
            if (explicitlySet.find(pd.name) == explicitlySet.end() && pd.defaultExpr) {
                local.vars[pd.name] = EvalExpr(pd.defaultExpr.get(), local, depth + 1);
            }
        }
        return local;
    }

    ScadGeom EvalStmtList(const std::vector<StmtPtr>& stmts, EvalEnv& env, int depth) {
        if (depth > 36 || opBudget <= 0) return {};
        // OpenSCAD hoists module/function declarations & variable assignments inside each lexical scope
        for (const auto& s : stmts) {
            if (!s) continue;
            if (s->type == Stmt::ModDef)  env.modules[s->name] = s.get();
            if (s->type == Stmt::FuncDef) env.funcs[s->name] = s.get();
        }
        for (const auto& s : stmts) {
            if (s && s->type == Stmt::Assign) {
                env.vars[s->name] = EvalExpr(s->expr.get(), env, depth + 1);
            }
        }
        ScadGeom acc;
        for (const auto& s : stmts) {
            if (!s || s->disabled || s->type == Stmt::Assign || s->type == Stmt::ModDef || s->type == Stmt::FuncDef) continue;
            acc.Append(EvalStmt(s.get(), env, depth + 1));
        }
        return acc;
    }

    std::vector<ScadGeom> EvalChildGeoms(const Stmt* body, const EvalEnv& env, int depth) {
        std::vector<ScadGeom> list;
        if (!body || body->disabled || depth > 36 || opBudget <= 0) return list;
        if (body->type == Stmt::Block) {
            EvalEnv sub = env;
            for (const auto& s : body->children) {
                if (!s) continue;
                if (s->type == Stmt::ModDef)  sub.modules[s->name] = s.get();
                if (s->type == Stmt::FuncDef) sub.funcs[s->name] = s.get();
            }
            for (const auto& s : body->children) {
                if (s && s->type == Stmt::Assign) sub.vars[s->name] = EvalExpr(s->expr.get(), sub, depth + 1);
            }
            for (const auto& s : body->children) {
                if (!s || s->disabled || s->type == Stmt::Assign || s->type == Stmt::ModDef || s->type == Stmt::FuncDef) continue;
                ScadGeom g = EvalStmt(s.get(), sub, depth + 1);
                if (!g.Empty3D() || !g.paths2D.empty()) list.push_back(std::move(g));
            }
        } else {
            EvalEnv sub = env;
            ScadGeom g = EvalStmt(body, sub, depth + 1);
            if (!g.Empty3D() || !g.paths2D.empty()) list.push_back(std::move(g));
        }
        return list;
    }

    ScadGeom EvalStmt(const Stmt* s, EvalEnv& env, int depth) {
        if (!s || s->disabled || depth > 36 || --opBudget <= 0) return {};

        if (s->type == Stmt::Block) {
            EvalEnv sub = env;
            return EvalStmtList(s->children, sub, depth + 1);
        }
        if (s->type == Stmt::If) {
            if (EvalExpr(s->expr.get(), env, depth + 1).AsBool()) {
                return EvalStmt(s->body.get(), env, depth + 1);
            } else if (s->elseBody) {
                return EvalStmt(s->elseBody.get(), env, depth + 1);
            }
            return {};
        }
        if (s->type == Stmt::Let) {
            EvalEnv sub = env;
            for (const auto& a : s->args) {
                if (!a.name.empty()) sub.vars[a.name] = EvalExpr(a.expr.get(), sub, depth + 1);
            }
            return EvalStmt(s->body.get(), sub, depth + 1);
        }
        if (s->type == Stmt::For) {
            ScadGeom out;
            bool isIntersect = (s->name == "intersection_for");
            bool firstIter = true;
            std::function<void(size_t, EvalEnv&)> runLoop = [&](size_t idx, EvalEnv& lenv) {
                if (opBudget <= 0) return;
                if (idx >= s->args.size()) {
                    ScadGeom g = EvalStmt(s->body.get(), lenv, depth + 1);
                    if (isIntersect) {
                        if (firstIter) { out = std::move(g); firstIter = false; }
                        else out = CsgIntersect(out, g);
                    } else {
                        out.Append(g);
                    }
                    return;
                }
                const auto& item = s->args[idx];
                ScadVal seq = EvalExpr(item.expr.get(), lenv, depth + 1);
                if (seq.kind == ScadVal::Vec) {
                    size_t cap = std::min<size_t>(seq.vec.size(), 512);
                    for (size_t k = 0; k < cap; ++k) {
                        EvalEnv nextEnv = lenv;
                        if (!item.name.empty()) nextEnv.vars[item.name] = seq.vec[k];
                        runLoop(idx + 1, nextEnv);
                    }
                }
            };
            EvalEnv startEnv = env;
            runLoop(0, startEnv);
            return out;
        }
        if (s->type != Stmt::ModCall) return {};

        // Evaluate call arguments (also apply $fn/$fa/$fs special variables into child scope)
        EvalEnv childEnv = env;
        std::vector<std::pair<std::string, ScadVal>> evalArgs;
        evalArgs.reserve(s->args.size());
        for (const auto& a : s->args) {
            ScadVal v = EvalExpr(a.expr.get(), env, depth + 1);
            if (!a.name.empty() && a.name[0] == '$') childEnv.vars[a.name] = v;
            evalArgs.emplace_back(a.name, std::move(v));
        }

        auto getArg = [&](size_t posIdx, std::string_view key) -> const ScadVal* {
            for (const auto& kv : evalArgs) {
                if (kv.first == key) return &kv.second;
            }
            size_t curPos = 0;
            for (const auto& kv : evalArgs) {
                if (kv.first.empty()) {
                    if (curPos == posIdx) return &kv.second;
                    ++curPos;
                }
            }
            return nullptr;
        };
        auto getFn = [&](int defSegs) -> int {
            auto it = childEnv.vars.find("$fn");
            if (it != childEnv.vars.end() && it->second.AsNum() >= 3.0) {
                return std::clamp(static_cast<int>(it->second.AsNum()), 3, 128);
            }
            return defSegs;
        };

        const std::string& m = s->name;

        // ---- Transforms ----
        if (m == "translate") {
            ScadGeom g = EvalStmt(s->body.get(), childEnv, depth + 1);
            const ScadVal* v = getArg(0, "v");
            Vec3f t = v ? v->AsVec3(0, 0, 0) : Vec3f{ 0, 0, 0 };
            g.ApplyTransform(Mat4::Translate(t.x, t.y, t.z));
            return g;
        }
        if (m == "scale") {
            ScadGeom g = EvalStmt(s->body.get(), childEnv, depth + 1);
            const ScadVal* v = getArg(0, "v");
            Vec3f sc = v ? v->AsVec3(1, 1, 1) : Vec3f{ 1, 1, 1 };
            g.ApplyTransform(Mat4::Scale(sc.x, sc.y, sc.z));
            return g;
        }
        if (m == "rotate") {
            ScadGeom g = EvalStmt(s->body.get(), childEnv, depth + 1);
            const ScadVal* a = getArg(0, "a");
            const ScadVal* v = getArg(1, "v");
            if (a && a->kind == ScadVal::Vec) {
                Vec3f r = a->AsVec3(0, 0, 0);
                g.ApplyTransform(Mat4::RotateXYZ(r.x, r.y, r.z));
            } else if (a) {
                float deg = static_cast<float>(a->AsNum(0.0));
                Vec3f axis = v ? v->AsVec3(0, 0, 1) : Vec3f{ 0, 0, 1 };
                g.ApplyTransform(Mat4::RotateAxisAngle(deg, axis.x, axis.y, axis.z));
            }
            return g;
        }
        if (m == "mirror") {
            ScadGeom g = EvalStmt(s->body.get(), childEnv, depth + 1);
            const ScadVal* v = getArg(0, "v");
            Vec3f n = v ? v->AsVec3(1, 0, 0) : Vec3f{ 1, 0, 0 };
            g.ApplyTransform(Mat4::Mirror(n.x, n.y, n.z));
            return g;
        }
        if (m == "multmatrix") {
            ScadGeom g = EvalStmt(s->body.get(), childEnv, depth + 1);
            const ScadVal* mv = getArg(0, "m");
            if (mv && mv->kind == ScadVal::Vec) {
                Mat4 mat;
                for (size_t r = 0; r < std::min<size_t>(4, mv->vec.size()); ++r) {
                    if (mv->vec[r].kind == ScadVal::Vec) {
                        for (size_t c = 0; c < std::min<size_t>(4, mv->vec[r].vec.size()); ++c) {
                            mat.m[r * 4 + c] = static_cast<float>(mv->vec[r].vec[c].AsNum());
                        }
                    }
                }
                g.ApplyTransform(mat);
            }
            return g;
        }
        if (m == "color" || m == "render" || m == "offset") {
            return EvalStmt(s->body.get(), childEnv, depth + 1);
        }

        // ---- CSG Set Operations ----
        if (m == "union" || m == "group") {
            ScadGeom u;
            for (const auto& cg : EvalChildGeoms(s->body.get(), childEnv, depth + 1)) u.Append(cg);
            return u;
        }
        if (m == "difference") {
            auto ch = EvalChildGeoms(s->body.get(), childEnv, depth + 1);
            if (ch.empty()) return {};
            ScadGeom base = std::move(ch[0]);
            if (!base.Empty3D() && ch.size() > 1) {
                ScadGeom cutters;
                for (size_t i = 1; i < ch.size(); ++i) cutters.Append(ch[i]);
                if (!cutters.Empty3D()) base = CsgSubtract(base, cutters);
            }
            return base;
        }
        if (m == "intersection") {
            auto ch = EvalChildGeoms(s->body.get(), childEnv, depth + 1);
            if (ch.empty()) return {};
            ScadGeom acc = std::move(ch[0]);
            for (size_t i = 1; i < ch.size(); ++i) acc = CsgIntersect(acc, ch[i]);
            return acc;
        }
        if (m == "hull") {
            auto ch = EvalChildGeoms(s->body.get(), childEnv, depth + 1);
            std::vector<Vec3f> all3D;
            std::vector<std::pair<float, float>> all2D;
            for (const auto& cg : ch) {
                all3D.insert(all3D.end(), cg.verts.begin(), cg.verts.end());
                for (const auto& p : cg.paths2D) all2D.insert(all2D.end(), p.begin(), p.end());
            }
            if (all3D.size() >= 4) return ComputeConvexHull3D(all3D);
            if (all2D.size() >= 3) {
                // Monotone chain 2D convex hull
                std::sort(all2D.begin(), all2D.end());
                auto cross2 = [](std::pair<float,float> o, std::pair<float,float> a, std::pair<float,float> b) {
                    return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
                };
                std::vector<std::pair<float, float>> h;
                for (const auto& p : all2D) {
                    while (h.size() >= 2 && cross2(h[h.size() - 2], h.back(), p) <= 0.0f) h.pop_back();
                    h.push_back(p);
                }
                size_t t = h.size() + 1;
                for (size_t i = all2D.size(); i-- > 0;) {
                    while (h.size() >= t && cross2(h[h.size() - 2], h.back(), all2D[i]) <= 0.0f) h.pop_back();
                    h.push_back(all2D[i]);
                }
                if (!h.empty()) h.pop_back();
                ScadGeom g2;
                if (h.size() >= 3) g2.paths2D.push_back(std::move(h));
                return g2;
            }
            return {};
        }

        // ---- Extrusions ----
        if (m == "linear_extrude") {
            ScadGeom child = EvalStmt(s->body.get(), childEnv, depth + 1);
            if (child.paths2D.empty()) return child;
            const ScadVal* hv = getArg(0, "height");
            float h = hv ? static_cast<float>(hv->AsNum(10.0)) : 10.0f;
            if (h <= 1e-6f) return {};
            const ScadVal* cv = getArg(100, "center");
            bool center = cv ? cv->AsBool(false) : false;
            const ScadVal* twv = getArg(100, "twist");
            float twistDeg = twv ? static_cast<float>(twv->AsNum(0.0)) : 0.0f;
            const ScadVal* scv = getArg(100, "scale");
            Vec3f endScale = scv ? scv->AsVec3(1.0f, scv->kind == ScadVal::Num ? static_cast<float>(scv->num) : 1.0f, 1.0f) : Vec3f{ 1.0f, 1.0f, 1.0f };
            const ScadVal* slv = getArg(100, "slices");
            int slices = slv ? std::clamp(static_cast<int>(slv->AsNum(1.0)), 1, 64)
                             : ((std::fabs(twistDeg) > 1.0f) ? std::clamp(static_cast<int>(std::ceil(std::fabs(twistDeg) / 12.0f)), 2, 48) : 1);

            float z0 = center ? -0.5f * h : 0.0f;
            ScadGeom out;
            for (auto path : child.paths2D) {
                if (path.size() < 3) continue;
                double area2 = 0.0;
                for (size_t i = 0; i < path.size(); ++i) {
                    size_t j = (i + 1) % path.size();
                    area2 += static_cast<double>(path[i].first) * path[j].second - static_cast<double>(path[j].first) * path[i].second;
                }
                if (area2 < 0.0) std::reverse(path.begin(), path.end());

                size_t n = path.size();
                uint32_t base = static_cast<uint32_t>(out.verts.size());
                for (int sl = 0; sl <= slices; ++sl) {
                    float t = static_cast<float>(sl) / static_cast<float>(slices);
                    float ang = static_cast<float>(-twistDeg * t * kDegToRad);
                    float ca = std::cos(ang), sa = std::sin(ang);
                    float sx = 1.0f + (endScale.x - 1.0f) * t;
                    float sy = 1.0f + (endScale.y - 1.0f) * t;
                    float z = z0 + h * t;
                    for (size_t i = 0; i < n; ++i) {
                        float x = path[i].first * sx;
                        float y = path[i].second * sy;
                        out.verts.push_back({ x * ca - y * sa, x * sa + y * ca, z });
                    }
                }
                for (int sl = 0; sl < slices; ++sl) {
                    uint32_t r0 = base + static_cast<uint32_t>(sl * n);
                    uint32_t r1 = base + static_cast<uint32_t>((sl + 1) * n);
                    for (size_t i = 0; i < n; ++i) {
                        size_t j = (i + 1) % n;
                        out.tris.push_back({ r0 + static_cast<uint32_t>(i), r0 + static_cast<uint32_t>(j), r1 + static_cast<uint32_t>(j) });
                        out.tris.push_back({ r0 + static_cast<uint32_t>(i), r1 + static_cast<uint32_t>(j), r1 + static_cast<uint32_t>(i) });
                    }
                }
                EarClip2DPoly(path, out.tris, base, true); // bottom cap (-Z)
                EarClip2DPoly(path, out.tris, base + static_cast<uint32_t>(slices * n), false); // top cap (+Z)
            }
            return out;
        }
        if (m == "rotate_extrude") {
            ScadGeom child = EvalStmt(s->body.get(), childEnv, depth + 1);
            if (child.paths2D.empty()) return child;
            const ScadVal* av = getArg(100, "angle");
            float angleDeg = av ? static_cast<float>(av->AsNum(360.0)) : 360.0f;
            bool closed = (std::fabs(angleDeg) >= 359.99f);
            int segs = getFn(24);
            int steps = closed ? segs : std::max(3, static_cast<int>(std::ceil(segs * std::fabs(angleDeg) / 360.0f)));

            ScadGeom out;
            for (const auto& path : child.paths2D) {
                if (path.size() < 3) continue;
                size_t n = path.size();
                uint32_t base = static_cast<uint32_t>(out.verts.size());
                int ringCnt = closed ? steps : (steps + 1);
                for (int sIdx = 0; sIdx < ringCnt; ++sIdx) {
                    float t = static_cast<float>(sIdx) / static_cast<float>(steps);
                    float ang = static_cast<float>((closed ? (360.0f * t) : (angleDeg * t)) * kDegToRad);
                    float ca = std::cos(ang), sa = std::sin(ang);
                    for (size_t i = 0; i < n; ++i) {
                        float r = path[i].first;
                        float z = path[i].second;
                        out.verts.push_back({ r * ca, r * sa, z });
                    }
                }
                for (int sIdx = 0; sIdx < steps; ++sIdx) {
                    uint32_t r0 = base + static_cast<uint32_t>(sIdx * n);
                    uint32_t r1 = base + static_cast<uint32_t>(((sIdx + 1) % ringCnt) * n);
                    for (size_t i = 0; i < n; ++i) {
                        size_t j = (i + 1) % n;
                        out.tris.push_back({ r0 + static_cast<uint32_t>(i), r1 + static_cast<uint32_t>(i), r1 + static_cast<uint32_t>(j) });
                        out.tris.push_back({ r0 + static_cast<uint32_t>(i), r1 + static_cast<uint32_t>(j), r0 + static_cast<uint32_t>(j) });
                    }
                }
                if (!closed) {
                    EarClip2DPoly(path, out.tris, base, true);
                    EarClip2DPoly(path, out.tris, base + static_cast<uint32_t>(steps * n), false);
                }
            }
            return out;
        }

        // ---- 3D Primitives ----
        if (m == "cube") {
            const ScadVal* szv = getArg(0, "size");
            Vec3f sz = szv ? szv->AsVec3(1.0f, 1.0f, 1.0f) : Vec3f{ 1.0f, 1.0f, 1.0f };
            if (sz.x <= 0.0f || sz.y <= 0.0f || sz.z <= 0.0f) return {};
            const ScadVal* cv = getArg(1, "center");
            bool center = cv ? cv->AsBool(false) : false;
            float x0 = center ? -0.5f * sz.x : 0.0f, x1 = x0 + sz.x;
            float y0 = center ? -0.5f * sz.y : 0.0f, y1 = y0 + sz.y;
            float z0 = center ? -0.5f * sz.z : 0.0f, z1 = z0 + sz.z;

            ScadGeom g;
            g.verts = {
                {x0,y0,z0}, {x1,y0,z0}, {x1,y1,z0}, {x0,y1,z0},
                {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}
            };
            static const uint32_t q[6][4] = {
                {0,3,2,1}, {4,5,6,7}, {0,1,5,4},
                {2,3,7,6}, {1,2,6,5}, {3,0,4,7}
            };
            for (const auto& f : q) {
                g.tris.push_back({ f[0], f[1], f[2] });
                g.tris.push_back({ f[0], f[2], f[3] });
            }
            return g;
        }

        if (m == "sphere") {
            float r = 1.0f;
            if (const ScadVal* dv = getArg(100, "d")) r = static_cast<float>(dv->AsNum(2.0) * 0.5);
            else if (const ScadVal* rv = getArg(0, "r")) r = static_cast<float>(rv->AsNum(1.0));
            if (r <= 1e-6f) return {};

            int segs = getFn(24);
            int rings = std::max(6, segs / 2);
            ScadGeom g;
            g.verts.push_back({ 0.0f, 0.0f, -r }); // south pole = 0
            for (int ri = 1; ri < rings; ++ri) {
                double phi = -0.5 * kPi + kPi * (static_cast<double>(ri) / static_cast<double>(rings));
                float cp = static_cast<float>(std::cos(phi)) * r;
                float sp = static_cast<float>(std::sin(phi)) * r;
                for (int si = 0; si < segs; ++si) {
                    double th = 2.0 * kPi * (static_cast<double>(si) / static_cast<double>(segs));
                    g.verts.push_back({ cp * static_cast<float>(std::cos(th)), cp * static_cast<float>(std::sin(th)), sp });
                }
            }
            uint32_t northIdx = static_cast<uint32_t>(g.verts.size());
            g.verts.push_back({ 0.0f, 0.0f, r }); // north pole

            for (int si = 0; si < segs; ++si) {
                uint32_t s0 = 1 + static_cast<uint32_t>(si);
                uint32_t s1 = 1 + static_cast<uint32_t>((si + 1) % segs);
                g.tris.push_back({ 0, s1, s0 });
            }
            for (int ri = 0; ri + 2 < rings; ++ri) {
                uint32_t b0 = 1 + static_cast<uint32_t>(ri * segs);
                uint32_t b1 = 1 + static_cast<uint32_t>((ri + 1) * segs);
                for (int si = 0; si < segs; ++si) {
                    uint32_t nsi = static_cast<uint32_t>((si + 1) % segs);
                    uint32_t v00 = b0 + static_cast<uint32_t>(si), v10 = b0 + nsi;
                    uint32_t v01 = b1 + static_cast<uint32_t>(si), v11 = b1 + nsi;
                    g.tris.push_back({ v00, v10, v11 });
                    g.tris.push_back({ v00, v11, v01 });
                }
            }
            uint32_t topBase = 1 + static_cast<uint32_t>((rings - 2) * segs);
            for (int si = 0; si < segs; ++si) {
                uint32_t t0 = topBase + static_cast<uint32_t>(si);
                uint32_t t1 = topBase + static_cast<uint32_t>((si + 1) % segs);
                g.tris.push_back({ t0, t1, northIdx });
            }
            return g;
        }

        if (m == "cylinder") {
            const ScadVal* hv = getArg(0, "h");
            float h = hv ? static_cast<float>(hv->AsNum(1.0)) : 1.0f;
            if (h <= 1e-6f) return {};

            float baseR = 1.0f;
            if (const ScadVal* dv = getArg(100, "d")) baseR = static_cast<float>(dv->AsNum(2.0) * 0.5);
            else if (const ScadVal* rv = getArg(100, "r")) baseR = static_cast<float>(rv->AsNum(1.0));
            else if (const ScadVal* p1 = getArg(1, "r1")) baseR = static_cast<float>(p1->AsNum(1.0));

            float r1 = baseR, r2 = baseR;
            if (const ScadVal* d1v = getArg(100, "d1")) r1 = static_cast<float>(d1v->AsNum() * 0.5);
            else if (const ScadVal* r1v = getArg(1, "r1")) r1 = static_cast<float>(r1v->AsNum());

            if (const ScadVal* d2v = getArg(100, "d2")) r2 = static_cast<float>(d2v->AsNum() * 0.5);
            else if (const ScadVal* r2v = getArg(2, "r2")) r2 = static_cast<float>(r2v->AsNum());

            r1 = std::max(0.0f, r1);
            r2 = std::max(0.0f, r2);
            if (r1 <= 1e-6f && r2 <= 1e-6f) return {};

            const ScadVal* cv = getArg(3, "center");
            bool center = cv ? cv->AsBool(false) : false;
            float z0 = center ? -0.5f * h : 0.0f;
            float z1 = z0 + h;
            int segs = getFn(24);

            ScadGeom g;
            for (int i = 0; i < segs; ++i) {
                double a = 2.0 * kPi * (static_cast<double>(i) / static_cast<double>(segs));
                float ca = static_cast<float>(std::cos(a)), sa = static_cast<float>(std::sin(a));
                g.verts.push_back({ ca * r1, sa * r1, z0 });
                g.verts.push_back({ ca * r2, sa * r2, z1 });
            }
            uint32_t cBot = static_cast<uint32_t>(g.verts.size());
            g.verts.push_back({ 0.0f, 0.0f, z0 });
            uint32_t cTop = static_cast<uint32_t>(g.verts.size());
            g.verts.push_back({ 0.0f, 0.0f, z1 });

            for (int i = 0; i < segs; ++i) {
                int ni = (i + 1) % segs;
                uint32_t b0 = static_cast<uint32_t>(i * 2),  t0 = b0 + 1;
                uint32_t b1 = static_cast<uint32_t>(ni * 2), t1 = b1 + 1;
                if (r1 > 1e-6f && r2 > 1e-6f) {
                    g.tris.push_back({ b0, b1, t1 });
                    g.tris.push_back({ b0, t1, t0 });
                } else if (r1 > 1e-6f) {
                    g.tris.push_back({ b0, b1, cTop });
                } else {
                    g.tris.push_back({ cBot, t1, t0 });
                }
                if (r1 > 1e-6f) g.tris.push_back({ cBot, b1, b0 });
                if (r2 > 1e-6f) g.tris.push_back({ cTop, t0, t1 });
            }
            return g;
        }

        if (m == "polyhedron") {
            const ScadVal* pv = getArg(0, "points");
            const ScadVal* fv = getArg(1, "faces");
            if (!fv) fv = getArg(1, "triangles");
            if (!pv || pv->kind != ScadVal::Vec || !fv || fv->kind != ScadVal::Vec) return {};
            ScadGeom g;
            g.verts.reserve(pv->vec.size());
            for (const auto& pt : pv->vec) g.verts.push_back(pt.AsVec3());
            for (const auto& fc : fv->vec) {
                if (fc.kind != ScadVal::Vec || fc.vec.size() < 3) continue;
                std::vector<uint32_t> idx;
                for (const auto& iv : fc.vec) {
                    int vi = static_cast<int>(std::floor(iv.AsNum(-1.0) + 0.5));
                    if (vi >= 0 && static_cast<size_t>(vi) < g.verts.size()) idx.push_back(static_cast<uint32_t>(vi));
                }
                // Note: OpenSCAD polyhedron faces use CW outward winding when viewed from outside -> flip to CCW
                for (size_t k = 1; k + 1 < idx.size(); ++k) {
                    g.tris.push_back({ idx[0], idx[k + 1], idx[k] });
                }
            }
            return g;
        }

        // ---- 2D Primitives (for linear_extrude / rotate_extrude) ----
        if (m == "square") {
            const ScadVal* szv = getArg(0, "size");
            Vec3f sz = szv ? szv->AsVec3(1.0f, szv->kind == ScadVal::Num ? static_cast<float>(szv->num) : 1.0f, 0.0f) : Vec3f{ 1.0f, 1.0f, 0.0f };
            if (sz.x <= 0.0f || sz.y <= 0.0f) return {};
            const ScadVal* cv = getArg(1, "center");
            bool center = cv ? cv->AsBool(false) : false;
            float x0 = center ? -0.5f * sz.x : 0.0f, x1 = x0 + sz.x;
            float y0 = center ? -0.5f * sz.y : 0.0f, y1 = y0 + sz.y;
            ScadGeom g;
            g.paths2D.push_back({ {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} });
            return g;
        }
        if (m == "circle") {
            float r = 1.0f;
            if (const ScadVal* dv = getArg(100, "d")) r = static_cast<float>(dv->AsNum(2.0) * 0.5);
            else if (const ScadVal* rv = getArg(0, "r")) r = static_cast<float>(rv->AsNum(1.0));
            if (r <= 1e-6f) return {};
            int segs = getFn(24);
            std::vector<std::pair<float, float>> ring;
            ring.reserve(segs);
            for (int i = 0; i < segs; ++i) {
                double a = 2.0 * kPi * (static_cast<double>(i) / static_cast<double>(segs));
                ring.push_back({ static_cast<float>(std::cos(a)) * r, static_cast<float>(std::sin(a)) * r });
            }
            ScadGeom g;
            g.paths2D.push_back(std::move(ring));
            return g;
        }
        if (m == "polygon") {
            const ScadVal* pv = getArg(0, "points");
            const ScadVal* pathsV = getArg(1, "paths");
            if (!pv || pv->kind != ScadVal::Vec || pv->vec.size() < 3) return {};
            std::vector<std::pair<float, float>> pts;
            pts.reserve(pv->vec.size());
            for (const auto& el : pv->vec) {
                Vec3f v = el.AsVec3();
                pts.push_back({ v.x, v.y });
            }
            ScadGeom g;
            if (pathsV && pathsV->kind == ScadVal::Vec && !pathsV->vec.empty()) {
                for (const auto& pathItem : pathsV->vec) {
                    if (pathItem.kind != ScadVal::Vec) continue;
                    std::vector<std::pair<float, float>> ring;
                    for (const auto& iv : pathItem.vec) {
                        int idx = static_cast<int>(std::floor(iv.AsNum(-1.0) + 0.5));
                        if (idx >= 0 && static_cast<size_t>(idx) < pts.size()) ring.push_back(pts[idx]);
                    }
                    if (ring.size() >= 3) g.paths2D.push_back(std::move(ring));
                }
            } else {
                g.paths2D.push_back(std::move(pts));
            }
            return g;
        }

        // ---- User-Defined Module Call ----
        auto mit = childEnv.modules.find(m);
        if (mit != childEnv.modules.end() && mit->second && mit->second->body) {
            EvalEnv modEnv = BindCallEnv(mit->second->params, s->args, childEnv, depth + 1);
            return EvalStmt(mit->second->body.get(), modEnv, depth + 1);
        }

        return {};
    }
};

} // namespace DetailScad

inline bool ParseOpenScadMesh(std::string_view sv, LoadedMesh3D& mesh) {
    using namespace DetailScad;
    mesh.formatName = L"OpenSCAD Parametric 3D (.scad)";

    std::vector<Token> toks = TokenizeScad(sv);
    if (toks.size() <= 1) return false;

    ScadParser parser(toks);
    std::vector<StmtPtr> prog = parser.ParseProgram();
    if (prog.empty()) return false;

    EvalEnv rootEnv;
    ScadEvaluator eval;
    ScadGeom result = eval.EvalStmtList(prog, rootEnv, 0);

    // If script only defined top-level 2D shapes without linear_extrude, extrude them as a thin 1-unit sheet
    if (result.Empty3D() && !result.paths2D.empty()) {
        for (const auto& path : result.paths2D) {
            if (path.size() < 3) continue;
            uint32_t b = static_cast<uint32_t>(result.verts.size());
            for (const auto& p : path) result.verts.push_back({ p.first, p.second, 0.0f });
            for (const auto& p : path) result.verts.push_back({ p.first, p.second, 1.0f });
            size_t n = path.size();
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                result.tris.push_back({ b + static_cast<uint32_t>(i), b + static_cast<uint32_t>(j), b + static_cast<uint32_t>(n + j) });
                result.tris.push_back({ b + static_cast<uint32_t>(i), b + static_cast<uint32_t>(n + j), b + static_cast<uint32_t>(n + i) });
            }
            EarClip2DPoly(path, result.tris, b, true);
            EarClip2DPoly(path, result.tris, b + static_cast<uint32_t>(n), false);
        }
    }

    if (result.Empty3D()) return false;

    mesh.vertices.reserve(result.verts.size());
    for (const auto& v : result.verts) {
        // OpenSCAD right-handed Z-up -> Explorer2 right-handed Y-up
        mesh.vertices.push_back({ v.x, v.z, -v.y });
    }
    mesh.tris.reserve(result.tris.size());
    for (const auto& t : result.tris) {
        mesh.tris.push_back({ t[0], t[1], t[2], {} });
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
