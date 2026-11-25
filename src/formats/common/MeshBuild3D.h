#pragma once
#include <numeric>
// MeshBuild3D.h — geometry builders shared by the 3D readers (needs Vec3f / LoadedMesh3D from Model3DViewer.h).

namespace Formats {

namespace MeshBuild3D {

// r = a * b for row-major 4x4 matrices (16 floats each; r must not alias a or b)
inline void MatMul4RowMajor(const float* a, const float* b, float* r) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            r[row * 4 + col] = a[row * 4 + 0] * b[0 * 4 + col] + a[row * 4 + 1] * b[1 * 4 + col] +
                               a[row * 4 + 2] * b[2 * 4 + col] + a[row * 4 + 3] * b[3 * 4 + col];
        }
    }
}

// Hamilton product a * b and rotation of v by unit quaternion q, for any quaternion type with members x, y, z, w
template <class Q>
inline Q QuatMul(const Q& a, const Q& b) {
    Q r;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return r;
}

template <class Q>
inline Vec3f QuatRotate(const Q& q, const Vec3f& v) {
    const float tx = 2.0f * (q.y * v.z - q.z * v.y);
    const float ty = 2.0f * (q.z * v.x - q.x * v.z);
    const float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return { v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz), v.z + q.w * tz + (q.x * ty - q.y * tx) };
}

// The p + 1 B-spline basis functions that are nonzero at u in knot span `span` of knot vector U, into N[0..p]
// (The NURBS Book, algorithm A2.2); left and right are scratch
inline void BsplineBasisFuns(int span, double u, int p, const std::vector<double>& U, std::vector<double>& N,
                             std::vector<double>& left, std::vector<double>& right) {
    N.assign(static_cast<size_t>(p + 1), 0.0);
    left.assign(static_cast<size_t>(p + 1), 0.0);
    right.assign(static_cast<size_t>(p + 1), 0.0);
    N[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[static_cast<size_t>(j)] = u - U[static_cast<size_t>(span + 1 - j)];
        right[static_cast<size_t>(j)] = U[static_cast<size_t>(span + j)] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double denom = right[static_cast<size_t>(r + 1)] + left[static_cast<size_t>(j - r)];
            const double temp = (std::abs(denom) > 1e-30) ? (N[static_cast<size_t>(r)] / denom) : 0.0;
            N[static_cast<size_t>(r)] = saved + right[static_cast<size_t>(r + 1)] * temp;
            saved = left[static_cast<size_t>(j - r)] * temp;
        }
        N[static_cast<size_t>(j)] = saved;
    }
}

// Evaluates all `n` Cox-de Boor B-spline basis values at parameter `t` over knot vector `U` (size >= n + deg + 1)
inline void EvalNurbsBasisAll(int n, int deg, const std::vector<double>& U, double t, std::vector<double>& outB) {
    outB.assign(static_cast<size_t>(n), 0.0);
    const int order = deg + 1;
    const double tMin = U[static_cast<size_t>(deg)];
    const double tMax = U[static_cast<size_t>(n)];
    if (t <= tMin) t = tMin;
    if (t >= tMax) {
        outB[static_cast<size_t>(n - 1)] = 1.0;
        return;
    }
    int span = deg;
    for (int i = deg; i < n; ++i) {
        if (t >= U[static_cast<size_t>(i)] && t < U[static_cast<size_t>(i + 1)]) {
            span = i;
            break;
        }
    }
    std::vector<double> N(static_cast<size_t>(order), 0.0);
    std::vector<double> L(static_cast<size_t>(order), 0.0);
    std::vector<double> R(static_cast<size_t>(order), 0.0);
    N[0] = 1.0;
    for (int j = 1; j <= deg; ++j) {
        L[static_cast<size_t>(j)] = t - U[static_cast<size_t>(span + 1 - j)];
        R[static_cast<size_t>(j)] = U[static_cast<size_t>(span + j)] - t;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double denom = R[static_cast<size_t>(r + 1)] + L[static_cast<size_t>(j - r)];
            const double tmp = (std::fabs(denom) > 1e-15) ? (N[static_cast<size_t>(r)] / denom) : 0.0;
            N[static_cast<size_t>(r)] = saved + R[static_cast<size_t>(r + 1)] * tmp;
            saved = L[static_cast<size_t>(j - r)] * tmp;
        }
        N[static_cast<size_t>(j)] = saved;
    }
    for (int j = 0; j <= deg; ++j) {
        const int idx = span - deg + j;
        if (idx >= 0 && idx < n) outB[static_cast<size_t>(idx)] = N[static_cast<size_t>(j)];
    }
}

// Thin triangular prism from a to b (half-width `rad`) so line-only data (edges, polylines, wire frames)
// shows up in the triangle renderer. Degenerate segments add nothing.
inline void EmitEdgeStrut(LoadedMesh3D& mesh, const Vec3f& a, const Vec3f& b, float rad) {
    Vec3f d = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-6f) return;
    Vec3f u = { d.x / len, d.y / len, d.z / len };
    Vec3f ref = (std::fabs(u.y) < 0.9f) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f s = { u.y * ref.z - u.z * ref.y, u.z * ref.x - u.x * ref.z, u.x * ref.y - u.y * ref.x };
    float sl = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    if (sl < 1e-6f) return;
    s = { s.x / sl * rad, s.y / sl * rad, s.z / sl * rad };
    Vec3f t = { u.y * s.z - u.z * s.y, u.z * s.x - u.x * s.z, u.x * s.y - u.y * s.x };

    uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ a.x + s.x, a.y + s.y, a.z + s.z });
    mesh.vertices.push_back({ a.x - s.x, a.y - s.y, a.z - s.z });
    mesh.vertices.push_back({ a.x + t.x, a.y + t.y, a.z + t.z });
    mesh.vertices.push_back({ b.x + s.x, b.y + s.y, b.z + s.z });
    mesh.vertices.push_back({ b.x - s.x, b.y - s.y, b.z - s.z });
    mesh.vertices.push_back({ b.x + t.x, b.y + t.y, b.z + t.z });
    mesh.tris.push_back({ base + 0, base + 3, base + 2, {} });
    mesh.tris.push_back({ base + 2, base + 3, base + 5, {} });
    mesh.tris.push_back({ base + 2, base + 5, base + 1, {} });
    mesh.tris.push_back({ base + 1, base + 5, base + 4, {} });
}

// Octahedron of "radius" r around p: a cheap marker for point-like items (atoms, joints, samples)
inline void EmitOctahedron(LoadedMesh3D& mesh, const Vec3f& p, float r) {
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
    mesh.vertices.push_back({ p.x,     p.y - r, p.z     });
    mesh.vertices.push_back({ p.x + r, p.y,     p.z     });
    mesh.vertices.push_back({ p.x - r, p.y,     p.z     });
    mesh.vertices.push_back({ p.x,     p.y,     p.z + r });
    mesh.vertices.push_back({ p.x,     p.y,     p.z - r });
    mesh.tris.push_back({ b + 0, b + 4, b + 2, {} });
    mesh.tris.push_back({ b + 0, b + 2, b + 5, {} });
    mesh.tris.push_back({ b + 0, b + 5, b + 3, {} });
    mesh.tris.push_back({ b + 0, b + 3, b + 4, {} });
    mesh.tris.push_back({ b + 1, b + 2, b + 4, {} });
    mesh.tris.push_back({ b + 1, b + 5, b + 2, {} });
    mesh.tris.push_back({ b + 1, b + 3, b + 5, {} });
    mesh.tris.push_back({ b + 1, b + 4, b + 3, {} });
}

// Raw counts, bounding box, fit into the [-1, 1] cube and per-triangle normals; marks the mesh loaded
// when it has vertices and triangles.
// Bridges inner hole loops into an outer 2D polygon via visible keyhole edges, then ear-clips
inline void EarClip2DWithHoles(
    const std::vector<std::pair<float, float>>& outer2D,
    const std::vector<uint32_t>& outer3DIdx,
    const std::vector<std::vector<std::pair<float, float>>>& holes2D,
    const std::vector<std::vector<uint32_t>>& holes3DIdx,
    bool reverseWinding,
    std::vector<MeshTriangle>& outTris)
{
    if (outer2D.size() < 3 || outer2D.size() != outer3DIdx.size()) return;

    auto signedArea2D = [](const std::vector<std::pair<float, float>>& poly) -> float {
        double a = 0.0;
        size_t n = poly.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            a += static_cast<double>(poly[i].first) * poly[j].second -
                 static_cast<double>(poly[j].first) * poly[i].second;
        }
        return static_cast<float>(0.5 * a);
    };

    std::vector<std::pair<float, float>> pts = outer2D;
    std::vector<uint32_t> idx = outer3DIdx;
    bool flipped2D = false;
    if (signedArea2D(pts) < 0.0f) {
        std::reverse(pts.begin(), pts.end());
        std::reverse(idx.begin(), idx.end());
        flipped2D = true;
    }

    // Splice holes sorted by rightmost X coordinate
    if (!holes2D.empty() && holes2D.size() == holes3DIdx.size()) {
        struct HoleInfo {
            size_t hIdx;
            size_t rightMostVert;
            float maxX;
        };
        std::vector<HoleInfo> order;
        for (size_t h = 0; h < holes2D.size(); ++h) {
            if (holes2D[h].size() < 3 || holes2D[h].size() != holes3DIdx[h].size()) continue;
            size_t rm = 0;
            for (size_t v = 1; v < holes2D[h].size(); ++v) {
                if (holes2D[h][v].first > holes2D[h][rm].first) rm = v;
            }
            order.push_back({ h, rm, holes2D[h][rm].first });
        }
        std::sort(order.begin(), order.end(), [](const HoleInfo& a, const HoleInfo& b) {
            return a.maxX > b.maxX;
        });

        for (const auto& hi : order) {
            std::vector<std::pair<float, float>> hp = holes2D[hi.hIdx];
            std::vector<uint32_t> hIdx = holes3DIdx[hi.hIdx];
            // Hole must be CW (negative signed area) when outer is CCW
            if (signedArea2D(hp) > 0.0f) {
                std::reverse(hp.begin(), hp.end());
                std::reverse(hIdx.begin(), hIdx.end());
            }
            size_t rm = 0;
            for (size_t v = 1; v < hp.size(); ++v) {
                if (hp[v].first > hp[rm].first) rm = v;
            }
            // Find nearest outer vertex to the right (or closest distance fallback)
            size_t bestOuter = 0;
            float bestDist = 1e30f;
            for (size_t o = 0; o < pts.size(); ++o) {
                float dx = pts[o].first - hp[rm].first;
                float dy = pts[o].second - hp[rm].second;
                float d = dx * dx + dy * dy + ((dx >= -1e-4f) ? 0.0f : 1e6f);
                if (d < bestDist) { bestDist = d; bestOuter = o; }
            }

            std::vector<std::pair<float, float>> mergedP;
            std::vector<uint32_t> mergedI;
            mergedP.reserve(pts.size() + hp.size() + 2);
            mergedI.reserve(idx.size() + hIdx.size() + 2);
            for (size_t o = 0; o <= bestOuter; ++o) {
                mergedP.push_back(pts[o]);
                mergedI.push_back(idx[o]);
            }
            for (size_t k = 0; k <= hp.size(); ++k) {
                size_t hv = (rm + k) % hp.size();
                mergedP.push_back(hp[hv]);
                mergedI.push_back(hIdx[hv]);
            }
            for (size_t o = bestOuter; o < pts.size(); ++o) {
                mergedP.push_back(pts[o]);
                mergedI.push_back(idx[o]);
            }
            pts = std::move(mergedP);
            idx = std::move(mergedI);
        }
    }

    auto emitTri = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a == b || b == c || a == c) return;
        bool rev = reverseWinding ^ flipped2D;
        if (rev) outTris.push_back({ a, c, b, {} });
        else     outTris.push_back({ a, b, c, {} });
    };

    if (pts.size() > 512) {
        for (size_t i = 1; i + 1 < idx.size(); ++i) emitTri(idx[0], idx[i], idx[i + 1]);
        return;
    }

    std::vector<size_t> ring(pts.size());
    std::iota(ring.begin(), ring.end(), 0);

    auto cross2D = [&](size_t ia, size_t ib, size_t ic) -> float {
        float ax = pts[ib].first - pts[ia].first, ay = pts[ib].second - pts[ia].second;
        float bx = pts[ic].first - pts[ia].first, by = pts[ic].second - pts[ia].second;
        return ax * by - ay * bx;
    };
    auto ptInTri = [&](size_t ip, size_t ia, size_t ib, size_t ic) -> bool {
        if (idx[ip] == idx[ia] || idx[ip] == idx[ib] || idx[ip] == idx[ic]) return false;
        float c1 = cross2D(ia, ib, ip);
        float c2 = cross2D(ib, ic, ip);
        float c3 = cross2D(ic, ia, ip);
        return (c1 >= -1e-6f && c2 >= -1e-6f && c3 >= -1e-6f);
    };

    int guard = static_cast<int>(ring.size() * ring.size()) + 16;
    while (ring.size() > 3 && guard-- > 0) {
        bool clipped = false;
        size_t m = ring.size();
        for (size_t i = 0; i < m; ++i) {
            size_t prev = ring[(i + m - 1) % m];
            size_t curr = ring[i];
            size_t next = ring[(i + 1) % m];
            if (cross2D(prev, curr, next) <= 1e-7f) continue;
            bool anyInside = false;
            for (size_t j = 0; j < m; ++j) {
                size_t test = ring[j];
                if (test == prev || test == curr || test == next) continue;
                if (ptInTri(test, prev, curr, next)) { anyInside = true; break; }
            }
            if (!anyInside) {
                emitTri(idx[prev], idx[curr], idx[next]);
                ring.erase(ring.begin() + static_cast<ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            // Sliver / self-touching keyhole fallback: relax convexity tolerance
            size_t bestI = 0;
            float bestC = -1e30f;
            for (size_t i = 0; i < m; ++i) {
                float c = cross2D(ring[(i + m - 1) % m], ring[i], ring[(i + 1) % m]);
                if (c > bestC) { bestC = c; bestI = i; }
            }
            emitTri(idx[ring[(bestI + m - 1) % m]], idx[ring[bestI]], idx[ring[(bestI + 1) % m]]);
            ring.erase(ring.begin() + static_cast<ptrdiff_t>(bestI));
        }
    }
    if (ring.size() == 3) {
        emitTri(idx[ring[0]], idx[ring[1]], idx[ring[2]]);
    }
}

// Fills a planar polygon (outer ring plus holes, in mesh coordinates, no closing duplicate): its vertices are
// appended to the mesh and it is ear-clipped in the coordinate plane it faces most (Newell normal). With `facing`
// the triangles are wound so that their front side points that way.
inline void EmitPlanarPolygon(LoadedMesh3D& mesh, const std::vector<Vec3f>& outer,
                              const std::vector<std::vector<Vec3f>>& holes, const Vec3f* facing = nullptr) {
    if (outer.size() < 3) return;
    Vec3f n = { 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < outer.size(); ++i) {
        const Vec3f& a = outer[i];
        const Vec3f& b = outer[(i + 1) % outer.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    const int drop = (ax >= ay && ax >= az) ? 0 : ((ay >= az) ? 1 : 2);
    // (y, z), (z, x), (x, y): each pair is oriented like the dropped axis, so windings carry over
    auto project = [drop](const Vec3f& p) -> std::pair<float, float> {
        if (drop == 0) return { p.y, p.z };
        if (drop == 1) return { p.z, p.x };
        return { p.x, p.y };
    };
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    std::vector<std::pair<float, float>> outer2D;
    std::vector<uint32_t> outerIdx;
    for (size_t i = 0; i < outer.size(); ++i) {
        mesh.vertices.push_back(outer[i]);
        outer2D.push_back(project(outer[i]));
        outerIdx.push_back(base + static_cast<uint32_t>(i));
    }
    std::vector<std::vector<std::pair<float, float>>> holes2D;
    std::vector<std::vector<uint32_t>> holesIdx;
    for (const auto& h : holes) {
        if (h.size() < 3) continue;
        holes2D.emplace_back();
        holesIdx.emplace_back();
        for (const auto& v : h) {
            holesIdx.back().push_back(static_cast<uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(v);
            holes2D.back().push_back(project(v));
        }
    }
    const bool reverse = facing && (n.x * facing->x + n.y * facing->y + n.z * facing->z) < 0.0f;
    EarClip2DWithHoles(outer2D, outerIdx, holes2D, holesIdx, reverse, mesh.tris);
}

inline void AppendOctahedralBonePrism(
    std::vector<Vec3f>& verts,
    std::vector<MeshTriangle>& tris,
    const Vec3f& p0,
    const Vec3f& p1,
    float halfWidthRatio = 0.12f,
    float minHalfW = 1e-4f)
{
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(len) || len < 1e-5f) return;

    Vec3f ax = { d.x / len, d.y / len, d.z / len };
    Vec3f ref = (std::fabs(ax.y) < 0.9f) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f u = {
        ax.y * ref.z - ax.z * ref.y,
        ax.z * ref.x - ax.x * ref.z,
        ax.x * ref.y - ax.y * ref.x
    };
    float uLen = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (uLen < 1e-6f) return;
    u = { u.x / uLen, u.y / uLen, u.z / uLen };
    Vec3f v = {
        ax.y * u.z - ax.z * u.y,
        ax.z * u.x - ax.x * u.z,
        ax.x * u.y - ax.y * u.x
    };

    Vec3f mid = { p0.x + d.x * 0.28f, p0.y + d.y * 0.28f, p0.z + d.z * 0.28f };
    float hw = std::max(len * halfWidthRatio, minHalfW);

    uint32_t b = static_cast<uint32_t>(verts.size());
    verts.push_back(p0);
    verts.push_back(p1);
    verts.push_back({ mid.x + u.x * hw, mid.y + u.y * hw, mid.z + u.z * hw });
    verts.push_back({ mid.x + v.x * hw, mid.y + v.y * hw, mid.z + v.z * hw });
    verts.push_back({ mid.x - u.x * hw, mid.y - u.y * hw, mid.z - u.z * hw });
    verts.push_back({ mid.x - v.x * hw, mid.y - v.y * hw, mid.z - v.z * hw });

    tris.push_back({ b + 0, b + 3, b + 2, {} });
    tris.push_back({ b + 0, b + 4, b + 3, {} });
    tris.push_back({ b + 0, b + 5, b + 4, {} });
    tris.push_back({ b + 0, b + 2, b + 5, {} });
    tris.push_back({ b + 2, b + 3, b + 1, {} });
    tris.push_back({ b + 3, b + 4, b + 1, {} });
    tris.push_back({ b + 4, b + 5, b + 1, {} });
    tris.push_back({ b + 5, b + 2, b + 1, {} });
}

inline void AppendOctahedralBonePrism(
    LoadedMesh3D& mesh,
    const Vec3f& p0,
    const Vec3f& p1,
    float halfWidthRatio = 0.12f,
    float minHalfW = 1e-4f)
{
    AppendOctahedralBonePrism(mesh.vertices, mesh.tris, p0, p1, halfWidthRatio, minHalfW);
}

inline void FinalizeMeshBoundsAndNormals(LoadedMesh3D& mesh) {
    if (mesh.loadedOk) return;
    mesh.rawVertexCount = mesh.vertices.size();
    mesh.rawTriangleCount = mesh.tris.size();
    if (mesh.vertices.empty() || mesh.tris.empty()) return;

    Vec3f bmin = mesh.vertices[0];
    Vec3f bmax = mesh.vertices[0];
    for (const auto& v : mesh.vertices) {
        bmin.x = std::min(bmin.x, v.x); bmin.y = std::min(bmin.y, v.y); bmin.z = std::min(bmin.z, v.z);
        bmax.x = std::max(bmax.x, v.x); bmax.y = std::max(bmax.y, v.y); bmax.z = std::max(bmax.z, v.z);
    }
    mesh.bboxMin = bmin;
    mesh.bboxMax = bmax;
    mesh.bboxSize = Vec3f{ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z };

    Vec3f center = {
        (bmin.x + bmax.x) * 0.5f,
        (bmin.y + bmax.y) * 0.5f,
        (bmin.z + bmax.z) * 0.5f
    };
    float maxSpan = std::max({ mesh.bboxSize.x, mesh.bboxSize.y, mesh.bboxSize.z, 1e-5f });
    float invScale = 2.0f / maxSpan;
    for (auto& v : mesh.vertices) {
        v.x = (v.x - center.x) * invScale;
        v.y = (v.y - center.y) * invScale;
        v.z = (v.z - center.z) * invScale;
    }

    // Recompute unit face normals in normalized space
    size_t vCount = mesh.vertices.size();
    for (auto& t : mesh.tris) {
        if (t.i0 < vCount && t.i1 < vCount && t.i2 < vCount) {
            const Vec3f& a = mesh.vertices[t.i0];
            const Vec3f& b = mesh.vertices[t.i1];
            const Vec3f& c = mesh.vertices[t.i2];
            Vec3f e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
            Vec3f e2 = { c.x - a.x, c.y - a.y, c.z - a.z };
            Vec3f n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
            float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            t.normal = (len > 1e-7f) ? Vec3f{ n.x / len, n.y / len, n.z / len } : Vec3f{ 0.0f, 1.0f, 0.0f };
        }
    }
    mesh.loadedOk = true;
}

} // namespace MeshBuild3D

} // namespace Formats
