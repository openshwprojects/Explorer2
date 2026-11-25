#pragma once
// MeshFormatCommon.h — Shared 3D & CAD geometry, codec, and skeletal helpers.
// Included inside namespace Explorer2.
#include "../../ByteIO.h"
#include "../../Lz4Block.h"
#include "MeshBuild3D.h"
#include "FemSolidSurfaceCommon.h"
#include "../cad/Scad.h"
#include "../cad/StepCad.h"
#include "../3d/Vtk.h"

#ifndef E2_DRACO_NOINLINE
#if defined(_MSC_VER)
#define E2_DRACO_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define E2_DRACO_NOINLINE __attribute__((noinline))
#else
#define E2_DRACO_NOINLINE
#endif
#endif

namespace Formats {

bool ParseCadKernelArchiveOrStream(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh);

namespace DetailQuakeVmf {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdI32BE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64BE;

using ::Explorer2::ReaderText::TrimSv;
inline std::string ToLowerStr(std::string_view s) { return ::Explorer2::ReaderText::ToLowerAscii(s); }
inline std::string ToUpperStr(std::string_view s) { return ::Explorer2::ReaderText::ToUpperAscii(s); }
using ::Explorer2::ReaderText::NextTokenSv;
using ::Explorer2::ReaderText::ParseFloatSv;
using ::Explorer2::ReaderText::ParseDoubleSv;
using ::Explorer2::ReaderText::ParseIntSv;
using ::Explorer2::ReaderText::IsSafeRelativeSubFile;

struct CsgVec3d {
    double x = 0.0, y = 0.0, z = 0.0;
};

inline CsgVec3d CsgAdd(const CsgVec3d& a, const CsgVec3d& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline CsgVec3d CsgSub(const CsgVec3d& a, const CsgVec3d& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline CsgVec3d CsgMul(const CsgVec3d& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline double   CsgDot(const CsgVec3d& a, const CsgVec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline CsgVec3d CsgCross(const CsgVec3d& a, const CsgVec3d& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline double CsgLen(const CsgVec3d& a) { return std::sqrt(CsgDot(a, a)); }

// Z-up (Quake / Source) -> Explorer2 Y-up (x, z, -y), preserving right-handed outward winding (det = +1)
inline Vec3f CsgZUpToYUp(const CsgVec3d& p) {
    return { static_cast<float>(p.x), static_cast<float>(p.z), static_cast<float>(-p.y) };
}

struct VmfDispInfo {
    bool present = false;
    int power = 0; // 2..4 -> N = (1 << power) + 1
    CsgVec3d startPos = {};
    double elevation = 0.0;
    std::vector<CsgVec3d> normals;  // row-major N*N
    std::vector<double> distances;  // row-major N*N
    std::vector<CsgVec3d> offsets;  // row-major N*N
};

struct BrushSideDef {
    CsgVec3d p0 = {}, p1 = {}, p2 = {};
    bool isTool = false;
    VmfDispInfo disp;
};

struct ClippedBrushFace {
    size_t sideIndex = 0;
    CsgVec3d normal = {};
    std::vector<CsgVec3d> poly; // CCW when viewed from outside (+normal)
};

inline bool IsToolMaterialName(std::string_view mat) {
    std::string low = ToLowerStr(TrimSv(mat));
    for (char& c : low) if (c == '\\') c = '/';
    if (low.empty()) return false;
    if (low.find("tools/toolstrigger") != std::string::npos ||
        low.find("tools/toolsskybox") != std::string::npos ||
        low.find("tools/tools2dskybox") != std::string::npos ||
        low.find("tools/toolsclip") != std::string::npos ||
        low.find("tools/toolsplayerclip") != std::string::npos ||
        low.find("tools/toolsnodraw") != std::string::npos ||
        low.find("tools/toolsinvisible") != std::string::npos ||
        low.find("tools/toolsskip") != std::string::npos ||
        low.find("tools/toolshint") != std::string::npos ||
        low.find("tools/toolsareaportal") != std::string::npos ||
        low.find("tools/toolsoccluder") != std::string::npos)
    {
        return true;
    }
    size_t sl = low.find_last_of('/');
    std::string_view leaf = (sl != std::string::npos) ? std::string_view(low).substr(sl + 1) : std::string_view(low);
    return leaf == "clip" || leaf == "trigger" || leaf == "skip" || leaf == "hint" ||
           leaf == "nodraw" || leaf == "caulk" || leaf == "origin";
}

// Parse "( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 )" from start of a string_view; returns offset past 3rd ')' or npos
inline size_t ParseThreeParenPoints(std::string_view s, CsgVec3d& out0, CsgVec3d& out1, CsgVec3d& out2) {
    CsgVec3d* outs[3] = { &out0, &out1, &out2 };
    size_t p = 0;
    for (int g = 0; g < 3; ++g) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
        if (p >= s.size() || s[p] != '(') return std::string_view::npos;
        size_t closeP = s.find(')', p + 1u);
        if (closeP == std::string_view::npos) return std::string_view::npos;
        std::string_view inner = s.substr(p + 1u, closeP - (p + 1u));
        std::string_view t0, t1, t2, extra;
        double x = 0, y = 0, z = 0;
        if (!NextTokenSv(inner, t0) || !NextTokenSv(inner, t1) || !NextTokenSv(inner, t2) ||
            NextTokenSv(inner, extra) ||
            !ParseDoubleSv(t0, x) || !ParseDoubleSv(t1, y) || !ParseDoubleSv(t2, z))
        {
            return std::string_view::npos;
        }
        if (std::fabs(x) > 1e7 || std::fabs(y) > 1e7 || std::fabs(z) > 1e7) return std::string_view::npos;
        *outs[g] = { x, y, z };
        p = closeP + 1u;
    }
    return p;
}

// True 3D CSG Convex Brush Clipping via Sutherland-Hodgman 3D half-space intersection
inline bool ClipBrushPlanesCSG(
    const std::vector<BrushSideDef>& sides,
    bool flipPlaneOrientation,
    std::vector<ClippedBrushFace>& outFaces)
{
    outFaces.clear();
    if (sides.size() < 4u || sides.size() > 512u) return false;

    struct HalfSpace {
        CsgVec3d n = {};
        double d = 0.0; // inside brush: n . x - d <= 0
        size_t sideIdx = 0;
    };

    double maxAbsCoord = 1024.0;
    std::vector<HalfSpace> planes;
    planes.reserve(sides.size());

    for (size_t i = 0; i < sides.size(); ++i) {
        const auto& sd = sides[i];
        for (const auto& pt : { sd.p0, sd.p1, sd.p2 }) {
            maxAbsCoord = std::max({ maxAbsCoord, std::fabs(pt.x), std::fabs(pt.y), std::fabs(pt.z) });
        }
        // Standard Quake/Hammer CW 3-point outward normal: (p0 - p1) x (p2 - p1)
        CsgVec3d rawN = CsgCross(CsgSub(sd.p0, sd.p1), CsgSub(sd.p2, sd.p1));
        double len = CsgLen(rawN);
        if (len < 1e-9) continue;
        CsgVec3d n = CsgMul(rawN, (flipPlaneOrientation ? -1.0 : 1.0) / len);
        double d = CsgDot(n, sd.p0);

        bool duplicate = false;
        for (const auto& ex : planes) {
            if (CsgDot(ex.n, n) > 0.99999 && std::fabs(ex.d - d) < 1e-3) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            planes.push_back({ n, d, i });
        }
    }
    if (planes.size() < 4u) return false;

    const double kHugeR = std::max(262144.0, maxAbsCoord * 64.0);
    const double kEps   = 1e-3;

    for (size_t i = 0; i < planes.size(); ++i) {
        const CsgVec3d n = planes[i].n;
        const double d   = planes[i].d;

        // Orthonormal right-handed basis (u, v, n) on plane n . x = d
        CsgVec3d axis = (std::fabs(n.z) < 0.9) ? CsgVec3d{ 0.0, 0.0, 1.0 } : CsgVec3d{ 1.0, 0.0, 0.0 };
        CsgVec3d u = CsgCross(axis, n);
        double uLen = CsgLen(u);
        if (uLen < 1e-12) continue;
        u = CsgMul(u, 1.0 / uLen);
        CsgVec3d v = CsgCross(n, u);

        CsgVec3d origin = CsgMul(n, d);
        CsgVec3d ru = CsgMul(u, kHugeR);
        CsgVec3d rv = CsgMul(v, kHugeR);

        // Initial huge quad wound CCW with respect to outward normal +n: (u x v == +n)
        std::vector<CsgVec3d> poly = {
            CsgSub(CsgSub(origin, ru), rv),
            CsgSub(CsgAdd(origin, ru), rv),
            CsgAdd(CsgAdd(origin, ru), rv),
            CsgAdd(CsgSub(origin, ru), rv)
        };

        // Sutherland-Hodgman clip against every other half-space n_j . x - d_j <= 0
        for (size_t j = 0; j < planes.size() && poly.size() >= 3u; ++j) {
            if (j == i) continue;
            const CsgVec3d cn = planes[j].n;
            const double cd   = planes[j].d;

            std::vector<CsgVec3d> nextPoly;
            nextPoly.reserve(poly.size() + 2u);
            const size_t m = poly.size();
            for (size_t k = 0; k < m; ++k) {
                const CsgVec3d& a = poly[k];
                const CsgVec3d& b = poly[(k + 1u) % m];
                double fa = CsgDot(cn, a) - cd;
                double fb = CsgDot(cn, b) - cd;
                bool inA = (fa <= kEps);
                bool inB = (fb <= kEps);

                if (inA && inB) {
                    nextPoly.push_back(b);
                } else if (inA && !inB) {
                    double denom = fa - fb;
                    double t = (std::fabs(denom) > 1e-15) ? std::clamp(fa / denom, 0.0, 1.0) : 0.0;
                    nextPoly.push_back(CsgAdd(a, CsgMul(CsgSub(b, a), t)));
                } else if (!inA && inB) {
                    double denom = fa - fb;
                    double t = (std::fabs(denom) > 1e-15) ? std::clamp(fa / denom, 0.0, 1.0) : 0.0;
                    nextPoly.push_back(CsgAdd(a, CsgMul(CsgSub(b, a), t)));
                    nextPoly.push_back(b);
                }
            }

            // Deduplicate consecutive merged vertices within epsilon
            poly.clear();
            for (const auto& pt : nextPoly) {
                if (poly.empty() || CsgLen(CsgSub(pt, poly.back())) > kEps) {
                    poly.push_back(pt);
                }
            }
            if (poly.size() >= 2u && CsgLen(CsgSub(poly.front(), poly.back())) <= kEps) {
                poly.pop_back();
            }
        }

        if (poly.size() < 3u) continue;

        // Reject unbounded half-space intersections (vertices still near kHugeR boundary)
        bool bounded = true;
        for (const auto& pt : poly) {
            if (CsgLen(CsgSub(pt, origin)) > 0.5 * kHugeR) {
                bounded = false;
                break;
            }
        }
        if (!bounded) return false;

        // Verify non-degenerate polygon area and outward CCW normal alignment
        CsgVec3d areaVec = {};
        for (size_t k = 1u; k + 1u < poly.size(); ++k) {
            areaVec = CsgAdd(areaVec, CsgCross(CsgSub(poly[k], poly[0]), CsgSub(poly[k + 1u], poly[0])));
        }
        if (CsgLen(areaVec) < 1e-4) continue;
        if (CsgDot(areaVec, n) < 0.0) {
            std::reverse(poly.begin(), poly.end());
        }

        outFaces.push_back({ planes[i].sideIdx, n, std::move(poly) });
    }

    return outFaces.size() >= 4u;
}

inline void EmitVmfDisplacementFace(
    const ClippedBrushFace& face,
    const VmfDispInfo& disp,
    LoadedMesh3D& mesh)
{
    if (face.poly.size() < 4u || disp.power < 2 || disp.power > 4) return;
    const size_t N = (1u << disp.power) + 1u;
    if (mesh.tris.size() + (N - 1u) * (N - 1u) * 2u > 3000000u) return;

    // Source displacements sit on 4-corner brush faces; orient CW or CCW starting at startposition
    // Our face.poly is CCW from outside (+normal). Find closest corner to startPos.
    std::array<CsgVec3d, 4> q = { face.poly[0], face.poly[1], face.poly[2], face.poly[3] };
    size_t bestIdx = 0;
    double bestDist = CsgLen(CsgSub(q[0], disp.startPos));
    for (size_t i = 1; i < 4u; ++i) {
        double d = CsgLen(CsgSub(q[i], disp.startPos));
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    // Hammer orders quad corners CW when viewed from outside: c0=start(BL), c1=TL(along v/row), c2=TR, c3=BR(along u/col).
    // Since q is CCW from outside, CW order starting from bestIdx is: bestIdx, (bestIdx+3)%4, (bestIdx+2)%4, (bestIdx+1)%4!
    CsgVec3d cBL = q[bestIdx];
    CsgVec3d cTL = q[(bestIdx + 3u) & 3u];
    CsgVec3d cTR = q[(bestIdx + 2u) & 3u];
    CsgVec3d cBR = q[(bestIdx + 1u) & 3u];

    uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
    for (size_t r = 0; r < N; ++r) {
        double v = static_cast<double>(r) / static_cast<double>(N - 1u);
        CsgVec3d left  = CsgAdd(CsgMul(cBL, 1.0 - v), CsgMul(cTL, v));
        CsgVec3d right = CsgAdd(CsgMul(cBR, 1.0 - v), CsgMul(cTR, v));
        for (size_t c = 0; c < N; ++c) {
            double u = static_cast<double>(c) / static_cast<double>(N - 1u);
            CsgVec3d basePt = CsgAdd(CsgMul(left, 1.0 - u), CsgMul(right, u));
            size_t idx = r * N + c;
            CsgVec3d nrm = (idx < disp.normals.size()) ? disp.normals[idx] : face.normal;
            double dist  = (idx < disp.distances.size()) ? disp.distances[idx] : 0.0;
            CsgVec3d off = (idx < disp.offsets.size()) ? disp.offsets[idx] : CsgVec3d{};
            CsgVec3d p = CsgAdd(basePt, CsgMul(face.normal, disp.elevation));
            p = CsgAdd(p, off);
            p = CsgAdd(p, CsgMul(nrm, dist));
            mesh.vertices.push_back(CsgZUpToYUp(p));
        }
    }

    // Check outward winding: (cBR - cBL) x (cTL - cBL) points along +face.normal!
    for (size_t r = 0; r + 1u < N; ++r) {
        for (size_t c = 0; c + 1u < N; ++c) {
            uint32_t i00 = vBase + static_cast<uint32_t>(r * N + c);
            uint32_t i10 = vBase + static_cast<uint32_t>(r * N + (c + 1u));
            uint32_t i01 = vBase + static_cast<uint32_t>((r + 1u) * N + c);
            uint32_t i11 = vBase + static_cast<uint32_t>((r + 1u) * N + (c + 1u));
            mesh.tris.push_back({ i00, i10, i11, {} });
            mesh.tris.push_back({ i00, i11, i01, {} });
        }
    }
}

struct ResolvedSolid {
    std::vector<BrushSideDef> sides;
    std::vector<ClippedBrushFace> faces;
    bool hasDisp = false;
};

inline bool EmitBrushSolidsToMesh(const std::vector< std::vector<BrushSideDef> >& rawSolids, LoadedMesh3D& mesh) {
    std::vector<ResolvedSolid> resolved;
    resolved.reserve(rawSolids.size());
    bool anyNonToolFace = false;

    for (const auto& s : rawSolids) {
        if (s.size() < 4u) continue;
        ResolvedSolid rs;
        rs.sides = s;
        if (!ClipBrushPlanesCSG(rs.sides, false, rs.faces)) {
            if (!ClipBrushPlanesCSG(rs.sides, true, rs.faces)) {
                continue;
            }
        }
        for (const auto& fc : rs.faces) {
            const auto& sd = rs.sides[fc.sideIndex];
            if (sd.disp.present) {
                rs.hasDisp = true;
                anyNonToolFace = true;
            } else if (!sd.isTool) {
                anyNonToolFace = true;
            }
        }
        resolved.push_back(std::move(rs));
    }
    if (resolved.empty()) return false;

    // Weld vertices locally per solid so convex brushes share corner vertices cleanly
    for (const auto& rs : resolved) {
        if (mesh.tris.size() >= 3000000u) break;
        if (rs.hasDisp) {
            for (const auto& fc : rs.faces) {
                const auto& sd = rs.sides[fc.sideIndex];
                if (sd.disp.present) {
                    EmitVmfDisplacementFace(fc, sd.disp, mesh);
                }
            }
            continue;
        }

        std::vector<Vec3f> solidVerts;
        auto findOrAddVert = [&](const CsgVec3d& p3) -> uint32_t {
            Vec3f vp = CsgZUpToYUp(p3);
            for (size_t k = 0; k < solidVerts.size(); ++k) {
                float dx = solidVerts[k].x - vp.x;
                float dy = solidVerts[k].y - vp.y;
                float dz = solidVerts[k].z - vp.z;
                if (dx * dx + dy * dy + dz * dz < 1e-4f) return static_cast<uint32_t>(k);
            }
            solidVerts.push_back(vp);
            return static_cast<uint32_t>(solidVerts.size() - 1u);
        };

        std::vector<std::array<uint32_t, 3>> solidTris;
        for (const auto& fc : rs.faces) {
            const auto& sd = rs.sides[fc.sideIndex];
            if (anyNonToolFace && sd.isTool) continue;
            std::vector<uint32_t> fIdx;
            fIdx.reserve(fc.poly.size());
            for (const auto& pt : fc.poly) {
                uint32_t vi = findOrAddVert(pt);
                if (fIdx.empty() || fIdx.back() != vi) fIdx.push_back(vi);
            }
            if (fIdx.size() >= 2u && fIdx.front() == fIdx.back()) fIdx.pop_back();
            for (size_t k = 1u; k + 1u < fIdx.size(); ++k) {
                if (fIdx[0] != fIdx[k] && fIdx[k] != fIdx[k + 1u] && fIdx[0] != fIdx[k + 1u]) {
                    solidTris.push_back({ fIdx[0], fIdx[k], fIdx[k + 1u] });
                }
            }
        }

        if (!solidTris.empty()) {
            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), solidVerts.begin(), solidVerts.end());
            for (const auto& t : solidTris) {
                mesh.tris.push_back({ vBase + t[0], vBase + t[1], vBase + t[2], {} });
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailQuakeVmf

namespace DetailDccScene {

using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;
// float loads that take a non-finite value as 0 (ByteIO.h)
inline float RdF32BE(const uint8_t* p) { return ::Explorer2::ByteIO::RdFiniteF32BE(p); }

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
inline float RdF32LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdFiniteF32LE(p); }
inline double RdF64LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdFiniteF64LE(p); }

inline void AddLineStrut(LoadedMesh3D& mesh, Vec3f p0, Vec3f p1, float radius) {
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-6f) return;
    Vec3f ref = (std::fabs(d.y) < 0.9f * len) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f n1 = DetailStep::StepScale(DetailStep::StepNorm(DetailStep::StepCross(d, ref)), radius);
    Vec3f n2 = DetailStep::StepScale(DetailStep::StepNorm(DetailStep::StepCross(d, n1)), radius);
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(DetailStep::StepAdd(p0, n1));
    mesh.vertices.push_back(DetailStep::StepAdd(p0, n2));
    mesh.vertices.push_back(DetailStep::StepSub(p0, n1));
    mesh.vertices.push_back(DetailStep::StepSub(p0, n2));
    mesh.vertices.push_back(DetailStep::StepAdd(p1, n1));
    mesh.vertices.push_back(DetailStep::StepAdd(p1, n2));
    mesh.vertices.push_back(DetailStep::StepSub(p1, n1));
    mesh.vertices.push_back(DetailStep::StepSub(p1, n2));
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t ni = (i + 1) & 3u;
        mesh.tris.push_back({ b + i, b + ni, b + 4 + ni, {} });
        mesh.tris.push_back({ b + i, b + 4 + ni, b + 4 + i, {} });
    }
}

// ============================================================================
// Pixar OpenUSD Crate (PXR-USDC) LZ4 & Integer Delta/Bitpack Decompressors
// ============================================================================

inline bool TfFastDecompress(const uint8_t* comp, size_t compSize, size_t uncompSize, std::vector<uint8_t>& out) {
    out.clear();
    if (compSize == 0 || uncompSize > 128 * 1024 * 1024) return false;
    if (uncompSize == 0) return true;
    out.reserve(uncompSize);

    // TfFastCompression: chunk count byte, then either one LZ4 block or <u32 size><block> per chunk;
    // every chunk is an independent block.
    uint8_t nChunks = comp[0];
    if (nChunks == 0) {
        return ::Explorer2::Lz4::DecodeBlockAppend(comp + 1, compSize - 1, out, 0, uncompSize);
    }

    size_t sp = 1;
    for (uint8_t c = 0; c < nChunks; ++c) {
        if (sp + 4 > compSize) return false;
        uint32_t cSz = RdU32LE(comp + sp);
        sp += 4;
        if (sp + cSz > compSize) return false;
        if (!::Explorer2::Lz4::DecodeBlockAppend(comp + sp, cSz, out, out.size(), uncompSize)) return false;
        sp += cSz;
    }
    return true;
}

// OpenUSD Usd_IntegerCompression::DecompressFromBuffer (common delta + 2-bit codes + int8/int16/int32 deltas)
inline bool UsdDecompressInts32(const uint8_t* comp, size_t compSize, size_t count, std::vector<int32_t>& out) {
    out.clear();
    if (count == 0) return true;
    if (count > 16000000) return false;
    size_t maxWorkingSz = 4 + ((count * 2 + 7) / 8) + count * 4 + 64;
    std::vector<uint8_t> raw;
    if (!TfFastDecompress(comp, compSize, maxWorkingSz, raw)) return false;
    size_t codesBytes = (count * 2 + 7) / 8;
    if (raw.size() < 4 + codesBytes) return false;

    int32_t commonValue = static_cast<int32_t>(RdU32LE(raw.data()));
    const uint8_t* codes = raw.data() + 4;
    size_t vp = 4 + codesBytes;

    out.resize(count);
    int32_t prev = 0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t code = (codes[i >> 2] >> ((i & 3u) * 2u)) & 0x03u;
        int32_t delta = 0;
        if (code == 0) {
            delta = commonValue;
        } else if (code == 1) {
            if (vp + 1 > raw.size()) return false;
            delta = static_cast<int8_t>(raw[vp++]);
        } else if (code == 2) {
            if (vp + 2 > raw.size()) return false;
            delta = static_cast<int16_t>(RdU16LE(raw.data() + vp));
            vp += 2;
        } else {
            if (vp + 4 > raw.size()) return false;
            delta = static_cast<int32_t>(RdU32LE(raw.data() + vp));
            vp += 4;
        }
        prev = static_cast<int32_t>(static_cast<uint32_t>(prev) + static_cast<uint32_t>(delta));
        out[i] = prev;
    }
    return true;
}

} // namespace DetailDccScene


namespace DetailMaya {

struct MayaEdge { int32_t v0 = 0, v1 = 0; };

inline void EmitMayaMeshNode(
    const std::vector<Vec3f>& vt,
    const std::vector<Vec3f>& ptTweaks,
    const std::vector<MayaEdge>& edges,
    const std::vector<std::vector<int32_t>>& faceEdgeLoops,
    const DetailScad::Mat4& xform,
    LoadedMesh3D& mesh)
{
    if (vt.empty() || faceEdgeLoops.empty()) return;
    uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
    bool flip = (xform.Det3x3() < 0.0f);
    for (size_t i = 0; i < vt.size(); ++i) {
        Vec3f p = vt[i];
        if (i < ptTweaks.size()) {
            p.x += ptTweaks[i].x;
            p.y += ptTweaks[i].y;
            p.z += ptTweaks[i].z;
        }
        mesh.vertices.push_back(xform.TransformPoint(p));
    }

    for (const auto& eloop : faceEdgeLoops) {
        if (eloop.size() < 3) continue;
        std::vector<uint32_t> ring;
        ring.reserve(eloop.size());
        for (int32_t eref : eloop) {
            int32_t eIdx = (eref >= 0) ? eref : (-eref - 1);
            if (eIdx < 0 || static_cast<size_t>(eIdx) >= edges.size()) continue;
            int32_t vStart = (eref >= 0) ? edges[eIdx].v0 : edges[eIdx].v1;
            if (vStart >= 0 && static_cast<size_t>(vStart) < vt.size()) {
                ring.push_back(vBase + static_cast<uint32_t>(vStart));
            }
        }
        for (size_t k = 1; k + 1 < ring.size(); ++k) {
            if (flip) mesh.tris.push_back({ ring[0], ring[k + 1], ring[k], {} });
            else      mesh.tris.push_back({ ring[0], ring[k], ring[k + 1], {} });
        }
    }
}

} // namespace DetailMaya

namespace DetailCadDcc {
// AutoCAD Native Drawing (.dwg — unencrypted R11/R12 AC1009 3DFACE / LINE entities; returns false on compressed/encrypted/empty)
inline bool ParseAutoCadDwg(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailDccScene;
    if (size < 64 || std::memcmp(data, "AC10", 4) != 0) return false;
    mesh.formatName = L"AutoCAD Native Drawing (.dwg)";

    // AC1009 (AutoCAD R11/R12) stores unencrypted entity records between entities_start (0x14) and entities_end (0x18)
    if (std::memcmp(data, "AC1009", 6) == 0 && size >= 0x20) {
        uint32_t entStart = RdU32LE(data + 0x14);
        uint32_t entEnd   = RdU32LE(data + 0x18);
        if (entStart >= 0x20 && entEnd > entStart && entEnd <= size) {
            size_t p = entStart;
            while (p + 8 <= entEnd && mesh.tris.size() < 200000) {
                uint8_t eType = data[p];
                uint8_t flag  = data[p + 1];
                uint16_t rLen = RdU16LE(data + p + 2);
                if (rLen < 8 || p + rLen > entEnd) break;
                // Skip header fields: type(1)+flag(1)+len(2)+layer(2)+opts(2) [+color(1) if flag&1] [+ltype(1) if flag&2] [+elev(8) if (flag&4 && eType!=1 && eType!=22)]
                size_t ep = p + 8;
                if (flag & 0x01u) ++ep;
                if (flag & 0x02u) ++ep;
                if (eType == 1 && ep + 48 <= p + rLen) { // LINE (3D start + 3D end doubles when flag&4, or 2D doubles)
                    bool is3D = (flag & 0x04u) != 0;
                    if (is3D && ep + 48 <= p + rLen) {
                        Vec3f a = { static_cast<float>(RdF64LE(data + ep + 0)),  static_cast<float>(RdF64LE(data + ep + 16)), static_cast<float>(-RdF64LE(data + ep + 8)) };
                        Vec3f b = { static_cast<float>(RdF64LE(data + ep + 24)), static_cast<float>(RdF64LE(data + ep + 40)), static_cast<float>(-RdF64LE(data + ep + 32)) };
                        AddLineStrut(mesh, a, b, 0.04f);
                    } else if (!is3D && ep + 32 <= p + rLen) {
                        Vec3f a = { static_cast<float>(RdF64LE(data + ep + 0)),  0.0f, static_cast<float>(-RdF64LE(data + ep + 8)) };
                        Vec3f b = { static_cast<float>(RdF64LE(data + ep + 16)), 0.0f, static_cast<float>(-RdF64LE(data + ep + 24)) };
                        AddLineStrut(mesh, a, b, 0.04f);
                    }
                } else if (eType == 22 && ep + 96 <= p + rLen) { // 3DFACE: 4 * double[3] = 96 bytes
                    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                    for (int k = 0; k < 4; ++k) {
                        double x = RdF64LE(data + ep + k * 24 + 0);
                        double y = RdF64LE(data + ep + k * 24 + 8);
                        double z = RdF64LE(data + ep + k * 24 + 16);
                        mesh.vertices.push_back({ static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) });
                    }
                    mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
                    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
                }
                p += rLen;
            }
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailCadDcc


namespace DetailMeshSciCad {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;
inline float RdF32LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdFiniteF32LE(p); }
// float loads that take a non-finite value as 0 (ByteIO.h)
inline float RdF32BE(const uint8_t* p) { return ::Explorer2::ByteIO::RdFiniteF32BE(p); }
inline double RdF64LE(const uint8_t* p) { return ::Explorer2::ByteIO::RdFiniteF64LE(p); }

inline float HalfToFloat(uint16_t h) {
    float f = ::Explorer2::ByteIO::HalfToFloat(h);
    return std::isfinite(f) ? f : 0.0f; // this reader maps Inf/NaN halves to 0
}

inline void AddOctaSphere(LoadedMesh3D& mesh, float cx, float cy, float cz, float r) {
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ cx + r, cy,     cz     });
    mesh.vertices.push_back({ cx - r, cy,     cz     });
    mesh.vertices.push_back({ cx,     cy + r, cz     });
    mesh.vertices.push_back({ cx,     cy - r, cz     });
    mesh.vertices.push_back({ cx,     cy,     cz + r });
    mesh.vertices.push_back({ cx,     cy,     cz - r });
    static const uint32_t f[8][3] = {
        {0,2,4}, {4,2,1}, {1,2,5}, {5,2,0},
        {4,3,0}, {1,3,4}, {5,3,1}, {0,3,5}
    };
    for (int i = 0; i < 8; ++i) {
        mesh.tris.push_back({ b + f[i][0], b + f[i][1], b + f[i][2], {} });
    }
}

inline void AddPointTetra(LoadedMesh3D& mesh, Vec3f p, float r) {
    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
    mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
    mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
    mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
    mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
    mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
    mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
    mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
}

inline void AddStrut3D(LoadedMesh3D& mesh, Vec3f p0, Vec3f p1, float radius) {
    Vec3f d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-5f) return;
    Vec3f u = (std::fabs(d.y) < 0.9f * len) ? Vec3f{ 0.0f, 1.0f, 0.0f } : Vec3f{ 1.0f, 0.0f, 0.0f };
    Vec3f n1 = { d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x };
    float n1L = std::sqrt(n1.x * n1.x + n1.y * n1.y + n1.z * n1.z);
    if (n1L < 1e-6f) return;
    n1.x = (n1.x / n1L) * radius; n1.y = (n1.y / n1L) * radius; n1.z = (n1.z / n1L) * radius;
    Vec3f n2 = { d.y * n1.z - d.z * n1.y, d.z * n1.x - d.x * n1.z, d.x * n1.y - d.y * n1.x };
    float n2L = std::sqrt(n2.x * n2.x + n2.y * n2.y + n2.z * n2.z);
    if (n2L > 1e-6f) { n2.x = (n2.x / n2L) * radius; n2.y = (n2.y / n2L) * radius; n2.z = (n2.z / n2L) * radius; }

    uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
    Vec3f offs[4] = { n1, n2, { -n1.x, -n1.y, -n1.z }, { -n2.x, -n2.y, -n2.z } };
    for (int i = 0; i < 4; ++i) mesh.vertices.push_back({ p0.x + offs[i].x, p0.y + offs[i].y, p0.z + offs[i].z });
    for (int i = 0; i < 4; ++i) mesh.vertices.push_back({ p1.x + offs[i].x, p1.y + offs[i].y, p1.z + offs[i].z });
    for (uint32_t i = 0; i < 4; ++i) {
        uint32_t ni = (i + 1) & 3u;
        mesh.tris.push_back({ b + i, b + ni, b + 4 + ni, {} });
        mesh.tris.push_back({ b + i, b + 4 + ni, b + 4 + i, {} });
    }
}

inline float ComputePointCloudSpan(const std::vector<Vec3f>& pts) {
    if (pts.empty()) return 1.0f;
    Vec3f mn = pts[0], mx = pts[0];
    for (const auto& p : pts) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    return std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-3f });
}

} // namespace DetailMeshSciCad


namespace DetailAcisParasolid {

struct CadVec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
inline CadVec3 vAdd(CadVec3 a, CadVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline CadVec3 vSub(CadVec3 a, CadVec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline CadVec3 vMul(CadVec3 a, double s)  { return { a.x * s, a.y * s, a.z * s }; }
inline double  vDot(CadVec3 a, CadVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline CadVec3 vCross(CadVec3 a, CadVec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline double  vLen(CadVec3 a) { return std::sqrt(vDot(a, a)); }
inline CadVec3 vNorm(CadVec3 a) {
    double L = vLen(a);
    return (L > 1e-12) ? CadVec3{ a.x / L, a.y / L, a.z / L } : CadVec3{ 0.0, 0.0, 1.0 };
}
inline double  vDist(CadVec3 a, CadVec3 b) { return vLen(vSub(a, b)); }

struct CadXform {
    double r[9] = { 1.0, 0.0, 0.0,  0.0, 1.0, 0.0,  0.0, 0.0, 1.0 };
    double t[3] = { 0.0, 0.0, 0.0 };
    double scale = 1.0;
    bool valid = false;
};
inline CadVec3 applyXfPt(const CadXform& xf, CadVec3 p) {
    if (!xf.valid) return p;
    double s = xf.scale;
    return {
        s * (xf.r[0] * p.x + xf.r[1] * p.y + xf.r[2] * p.z) + xf.t[0],
        s * (xf.r[3] * p.x + xf.r[4] * p.y + xf.r[5] * p.z) + xf.t[1],
        s * (xf.r[6] * p.x + xf.r[7] * p.y + xf.r[8] * p.z) + xf.t[2]
    };
}
inline CadVec3 applyXfDir(const CadXform& xf, CadVec3 d) {
    if (!xf.valid) return d;
    return vNorm({
        xf.r[0] * d.x + xf.r[1] * d.y + xf.r[2] * d.z,
        xf.r[3] * d.x + xf.r[4] * d.y + xf.r[5] * d.z,
        xf.r[6] * d.x + xf.r[7] * d.y + xf.r[8] * d.z
    });
}

        enum class SurfKind { Unknown, Plane, CylinderCone, Sphere, Torus, Nurbs };
        struct BRepSurf {
            SurfKind kind = SurfKind::Unknown;
            CadVec3 origin;
            CadVec3 normal = { 0.0, 0.0, 1.0 };
            CadVec3 uAxis  = { 1.0, 0.0, 0.0 };
            double radius1 = 0.0;
            double radius2 = 0.0;
            double ratio   = 1.0;
            int uDeg = 0, vDeg = 0, nU = 0, nV = 0;
            bool isRational = false;
            std::vector<double> knotsU;
            std::vector<double> knotsV;
            std::vector<CadVec3> poles;
            std::vector<double> weights;
        };

        enum class CurveKind { Straight, Ellipse, Spline };
        struct BRepCurve {
            CurveKind kind = CurveKind::Straight;
            CadVec3 origin;
            CadVec3 normal    = { 0.0, 0.0, 1.0 };
            CadVec3 majorAxis = { 1.0, 0.0, 0.0 };
            double ratio      = 1.0;
            std::vector<CadVec3> ctrlPts;
        };

        struct BRepEdge {
            int32_t startVert = -1;
            int32_t endVert   = -1;
            int32_t curveId   = -1;
            int32_t bodyId    = -1;
            bool reversed     = false;
        };

        struct BRepCoedge {
            int32_t nextCoedge = -1;
            int32_t prevCoedge = -1;
            int32_t edgeId     = -1;
            int32_t headVert   = -1;
            bool reversed      = false;
        };

        struct BRepLoop {
            int32_t nextLoop    = -1;
            int32_t firstCoedge = -1;
            int32_t ownerFace   = -1;
            std::vector<int32_t> directEdges;
        };

        struct BRepFace {
            int32_t nextFace  = -1;
            int32_t firstLoop = -1;
            int32_t surfaceId = -1;
            int32_t bodyId    = -1;
            bool reversed     = false;
            std::vector<int32_t> loopIds;
        };

        struct BRepGraph {
            std::unordered_map<int32_t, CadVec3>    points;
            std::unordered_map<int32_t, int32_t>    vertexToPoint;
            std::unordered_map<int32_t, BRepCurve>  curves;
            std::unordered_map<int32_t, BRepSurf>   surfaces;
            std::unordered_map<int32_t, BRepEdge>   edges;
            std::unordered_map<int32_t, BRepCoedge> coedges;
            std::unordered_map<int32_t, BRepLoop>   loops;
            std::unordered_map<int32_t, BRepFace>   faces;
            std::unordered_map<int32_t, CadXform>   bodyTransforms;
            std::vector<int32_t> faceOrder;
            std::vector<int32_t> edgeOrder;
        };

// Shared B-Rep Topology -> Triangle Mesh / Wireframe Evaluator
E2_DRACO_NOINLINE inline bool EvaluateBRepGraph(const BRepGraph& g, LoadedMesh3D& mesh) {
            if (g.faces.empty() && g.edges.empty()) return false;

            auto getVertPt = [&](int32_t vertId, CadVec3& outPt) -> bool {
                if (vertId < 0) return false;
                auto vit = g.vertexToPoint.find(vertId);
                if (vit == g.vertexToPoint.end()) return false;
                auto pit = g.points.find(vit->second);
                if (pit == g.points.end()) return false;
                outPt = pit->second;
                return true;
            };

            auto getBodyXf = [&](int32_t bodyId) -> CadXform {
                auto it = g.bodyTransforms.find(bodyId);
                return (it != g.bodyTransforms.end()) ? it->second : CadXform{};
            };

            // Discretize an oriented edge traversal from vStart -> vEnd along curve
            auto sampleEdgePoints = [&](const BRepEdge& ed, int32_t vStart, int32_t vEnd, bool coedgeForward) -> std::vector<CadVec3> {
                std::vector<CadVec3> poly;
                CadVec3 p0{}, p1{};
                bool hasP0 = getVertPt(vStart, p0);
                bool hasP1 = getVertPt(vEnd,   p1);

                auto cit = g.curves.find(ed.curveId);
                if (cit != g.curves.end() && cit->second.kind == CurveKind::Ellipse) {
                    const BRepCurve& c = cit->second;
                    double rMaj = vLen(c.majorAxis);
                    double nLen = vLen(c.normal);
                    double ratio = std::fabs(c.ratio);
                    if (rMaj > 1e-9 && nLen > 1e-9 && ratio > 1e-6) {
                        CadVec3 nUnit = vNorm(c.normal);
                        CadVec3 uUnit = vMul(c.majorAxis, 1.0 / rMaj);
                        CadVec3 vUnit = vNorm(vCross(nUnit, uUnit));
                        double rMin = rMaj * ratio;
                        auto evalEll = [&](double ang) -> CadVec3 {
                            return vAdd(c.origin, vAdd(vMul(uUnit, rMaj * std::cos(ang)), vMul(vUnit, rMin * std::sin(ang))));
                        };
                        auto angleOf = [&](CadVec3 pt) -> double {
                            CadVec3 d = vSub(pt, c.origin);
                            return std::atan2(vDot(d, vUnit) / rMin, vDot(d, uUnit) / rMaj);
                        };
                        const double kPi = 3.14159265358979323846;
                        bool edgeForward = !ed.reversed;
                        double dirSign = (coedgeForward == edgeForward) ? 1.0 : -1.0;

                        bool isClosedFull = (!hasP0 && !hasP1) ||
                                            (hasP0 && hasP1 && (vStart == vEnd || vDist(p0, p1) < 1e-5 * rMaj));
                        if (isClosedFull) {
                            double t0 = hasP0 ? angleOf(p0) : 0.0;
                            const int segs = 24;
                            poly.reserve(static_cast<size_t>(segs) + 1);
                            for (int k = 0; k <= segs; ++k) {
                                if (k == 0 && hasP0) poly.push_back(p0);
                                else if (k == segs && hasP0) poly.push_back(p0);
                                else poly.push_back(evalEll(t0 + dirSign * (2.0 * kPi * k) / segs));
                            }
                            return poly;
                        }
                        if (hasP0 && hasP1) {
                            double t0 = angleOf(p0);
                            double t1 = angleOf(p1);
                            double sweep = t1 - t0;
                            if (dirSign > 0.0) {
                                while (sweep <= 1e-7) sweep += 2.0 * kPi;
                                while (sweep > 2.0 * kPi + 1e-5) sweep -= 2.0 * kPi;
                            } else {
                                while (sweep >= -1e-7) sweep -= 2.0 * kPi;
                                while (sweep < -2.0 * kPi - 1e-5) sweep += 2.0 * kPi;
                            }
                            int segs = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) / (2.0 * kPi / 24.0))), 3, 32);
                            poly.reserve(static_cast<size_t>(segs) + 1);
                            poly.push_back(p0);
                            for (int k = 1; k < segs; ++k) {
                                poly.push_back(evalEll(t0 + sweep * (static_cast<double>(k) / segs)));
                            }
                            poly.push_back(p1);
                            return poly;
                        }
                    }
                } else if (cit != g.curves.end() && cit->second.kind == CurveKind::Spline && !cit->second.ctrlPts.empty()) {
                    const auto& cp = cit->second.ctrlPts;
                    bool forwardAlongCurve = (coedgeForward == !ed.reversed);
                    if (hasP0) poly.push_back(p0);
                    if (forwardAlongCurve) {
                        for (const auto& q : cp) poly.push_back(q);
                    } else {
                        for (size_t i = cp.size(); i > 0; --i) poly.push_back(cp[i - 1]);
                    }
                    if (hasP1) poly.push_back(p1);
                    if (poly.size() >= 2) return poly;
                }

                if (hasP0 && hasP1) {
                    poly.push_back(p0);
                    poly.push_back(p1);
                }
                return poly;
            };

            // Spatial vertex welder so B-Rep solids share clean topological vertices
            double globalSpan = 1.0;
            if (!g.points.empty()) {
                auto it0 = g.points.begin();
                CadVec3 mn = it0->second, mx = it0->second;
                for (const auto& kv : g.points) {
                    mn.x = std::min(mn.x, kv.second.x); mn.y = std::min(mn.y, kv.second.y); mn.z = std::min(mn.z, kv.second.z);
                    mx.x = std::max(mx.x, kv.second.x); mx.y = std::max(mx.y, kv.second.y); mx.z = std::max(mx.z, kv.second.z);
                }
                globalSpan = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-4 });
            }
            const double invWeldCell = 1.0 / (globalSpan * 1e-5);
            struct WeldKey {
                int64_t x, y, z;
                bool operator==(const WeldKey& o) const { return x == o.x && y == o.y && z == o.z; }
            };
            struct WeldHash {
                size_t operator()(const WeldKey& k) const {
                    uint64_t h = static_cast<uint64_t>(k.x) * 11400714819323198485ull;
                    h ^= static_cast<uint64_t>(k.y) * 14029467366897019727ull + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                    h ^= static_cast<uint64_t>(k.z) * 1609587929392839161ull  + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                    return static_cast<size_t>(h);
                }
            };
            std::unordered_map<WeldKey, uint32_t, WeldHash> weldedVerts;
            auto emitVertex = [&](CadVec3 p) -> uint32_t {
                WeldKey key{
                    static_cast<int64_t>(std::llround(p.x * invWeldCell)),
                    static_cast<int64_t>(std::llround(p.y * invWeldCell)),
                    static_cast<int64_t>(std::llround(p.z * invWeldCell))
                };
                auto it = weldedVerts.find(key);
                if (it != weldedVerts.end()) return it->second;
                uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({
                    static_cast<float>(p.x),
                    static_cast<float>(p.y),
                    static_cast<float>(p.z)
                });
                weldedVerts.emplace(key, idx);
                return idx;
            };

            struct Poly2DVert {
                double u = 0.0;
                double v = 0.0;
                uint32_t vIdx = 0;
            };

            auto signedArea2D = [](const std::vector<Poly2DVert>& p) -> double {
                double a = 0.0;
                size_t n = p.size();
                for (size_t i = 0; i < n; ++i) {
                    size_t j = (i + 1) % n;
                    a += p[i].u * p[j].v - p[j].u * p[i].v;
                }
                return 0.5 * a;
            };

            auto pointInPoly2D = [](double u, double v, const std::vector<Poly2DVert>& poly) -> bool {
                bool inside = false;
                size_t n = poly.size();
                for (size_t i = 0, j = n - 1; i < n; j = i++) {
                    double ui = poly[i].u, vi = poly[i].v;
                    double uj = poly[j].u, vj = poly[j].v;
                    if (((vi > v) != (vj > v)) &&
                        (u < (uj - ui) * (v - vi) / (vj - vi) + ui))
                    {
                        inside = !inside;
                    }
                }
                return inside;
            };

            auto cross2D = [](Poly2DVert a, Poly2DVert b, Poly2DVert c) -> double {
                return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
            };

            // Merge CW holes into CCW outer polygon via horizontal +u ray bridge cuts, then ear-clip
            auto triangulateOuterWithHoles = [&](
                std::vector<Poly2DVert> outer,
                std::vector<std::vector<Poly2DVert>> holes,
                bool flipWinding)
            {
                if (outer.size() < 3) return;
                double minU = outer[0].u, maxU = outer[0].u;
                double minV = outer[0].v, maxV = outer[0].v;
                for (const auto& q : outer) {
                    minU = std::min(minU, q.u); maxU = std::max(maxU, q.u);
                    minV = std::min(minV, q.v); maxV = std::max(maxV, q.v);
                }
                double spanUV = std::max({ maxU - minU, maxV - minV, 1e-9 });
                double polyScale2 = spanUV * spanUV;

                auto same2D = [&](Poly2DVert a, Poly2DVert b) -> bool {
                    double du = a.u - b.u, dv = a.v - b.v;
                    return (du * du + dv * dv) <= 1e-18 * polyScale2;
                };
                auto ptInTri2D = [&](Poly2DVert p, Poly2DVert a, Poly2DVert b, Poly2DVert c) -> bool {
                    double eps = -1e-11 * polyScale2;
                    return (cross2D(a, b, p) >= eps && cross2D(b, c, p) >= eps && cross2D(c, a, p) >= eps);
                };

                if (signedArea2D(outer) < 0.0) std::reverse(outer.begin(), outer.end());
                for (auto& h : holes) {
                    if (signedArea2D(h) > 0.0) std::reverse(h.begin(), h.end());
                }

                // Sort holes by rightmost u descending (Eberly bridge ordering)
                std::sort(holes.begin(), holes.end(), [](const auto& a, const auto& b) {
                    double maxA = a[0].u, maxB = b[0].u;
                    for (const auto& q : a) maxA = std::max(maxA, q.u);
                    for (const auto& q : b) maxB = std::max(maxB, q.u);
                    return maxA > maxB;
                });

                const double linEps = 1e-9 * spanUV;
                for (const auto& hole : holes) {
                    if (hole.size() < 3) continue;
                    size_t hm = 0;
                    for (size_t i = 1; i < hole.size(); ++i) {
                        if (hole[i].u > hole[hm].u || (hole[i].u == hole[hm].u && hole[i].v < hole[hm].v)) hm = i;
                    }
                    const Poly2DVert M = hole[hm];

                    double bestIx = 1e300;
                    int bestEdge = -1;
                    size_t on = outer.size();
                    for (size_t i = 0; i < on; ++i) {
                        const Poly2DVert& A = outer[i];
                        const Poly2DVert& B = outer[(i + 1) % on];
                        if ((A.v <= M.v && M.v < B.v) || (B.v <= M.v && M.v < A.v)) {
                            double t = (M.v - A.v) / (B.v - A.v);
                            double ix = A.u + t * (B.u - A.u);
                            if (ix >= M.u - linEps && ix < bestIx) {
                                bestIx = ix;
                                bestEdge = static_cast<int>(i);
                            }
                        }
                    }

                    size_t bridgeIdx = 0;
                    if (bestEdge >= 0) {
                        size_t i0 = static_cast<size_t>(bestEdge);
                        size_t i1 = (i0 + 1) % on;
                        if (std::hypot(outer[i0].u - bestIx, outer[i0].v - M.v) < linEps) {
                            bridgeIdx = i0;
                        } else if (std::hypot(outer[i1].u - bestIx, outer[i1].v - M.v) < linEps) {
                            bridgeIdx = i1;
                        } else {
                            size_t cand = (outer[i0].u >= outer[i1].u) ? i0 : i1;
                            Poly2DVert I{ bestIx, M.v, 0 };
                            Poly2DVert P = outer[cand];
                            Poly2DVert tA = M, tB = I, tC = P;
                            if (cross2D(tA, tB, tC) < 0.0) std::swap(tB, tC);

                            double bestSlope = 1e300;
                            double bestDist  = 1e300;
                            bridgeIdx = cand;
                            for (size_t k = 0; k < on; ++k) {
                                const Poly2DVert& R = outer[k];
                                if (same2D(R, P) || R.u < M.u - linEps) continue;
                                // Check if R is a reflex vertex inside search triangle (M, I, P)
                                const Poly2DVert& rPrev = outer[(k + on - 1) % on];
                                const Poly2DVert& rNext = outer[(k + 1) % on];
                                if (cross2D(rPrev, R, rNext) <= 1e-11 * polyScale2 && ptInTri2D(R, tA, tB, tC)) {
                                    double dx = std::max(0.0, R.u - M.u);
                                    double dy = std::fabs(R.v - M.v);
                                    double dist = std::hypot(dx, dy);
                                    double slope = (dx > 1e-12 * spanUV) ? (dy / dx) : 1e200;
                                    if (slope < bestSlope - 1e-11 || (std::fabs(slope - bestSlope) <= 1e-11 && dist < bestDist)) {
                                        bestSlope = slope;
                                        bestDist  = dist;
                                        bridgeIdx = k;
                                    }
                                }
                            }
                        }
                    } else {
                        double bestD = 1e300;
                        for (size_t k = 0; k < on; ++k) {
                            double d = std::hypot(outer[k].u - M.u, outer[k].v - M.v);
                            if (d < bestD) { bestD = d; bridgeIdx = k; }
                        }
                    }

                    std::vector<Poly2DVert> merged;
                    merged.reserve(outer.size() + hole.size() + 2);
                    for (size_t i = 0; i <= bridgeIdx; ++i) merged.push_back(outer[i]);
                    for (size_t k = 0; k < hole.size(); ++k) merged.push_back(hole[(hm + k) % hole.size()]);
                    merged.push_back(hole[hm]);
                    merged.push_back(outer[bridgeIdx]);
                    for (size_t i = bridgeIdx + 1; i < outer.size(); ++i) merged.push_back(outer[i]);
                    outer = std::move(merged);
                }

                // Ear-clipping with bridge-vertex coincidence handling & recovery
                std::vector<Poly2DVert> work = std::move(outer);
                size_t guard = work.size() * work.size() + 64;
                while (work.size() >= 3 && guard-- > 0) {
                    size_t wn = work.size();
                    if (wn == 3) {
                        uint32_t a = work[0].vIdx, b = work[1].vIdx, c = work[2].vIdx;
                        if (a != b && b != c && a != c) {
                            if (flipWinding) mesh.tris.push_back({ a, c, b, {} });
                            else             mesh.tris.push_back({ a, b, c, {} });
                        }
                        break;
                    }

                    // Remove any adjacent zero-length collapsed edges first
                    bool removedDup = false;
                    for (size_t i = 0; i < wn; ++i) {
                        size_t ni = (i + 1) % wn;
                        if (same2D(work[i], work[ni])) {
                            work.erase(work.begin() + static_cast<std::ptrdiff_t>(ni));
                            removedDup = true;
                            break;
                        }
                    }
                    if (removedDup) continue;

                    int earIdx = -1;
                    for (int pass = 0; pass < 2 && earIdx < 0; ++pass) {
                        double areaEps = (pass == 0) ? (1e-11 * polyScale2) : (-1e-9 * polyScale2);
                        for (size_t i = 0; i < wn; ++i) {
                            const Poly2DVert& A = work[(i + wn - 1) % wn];
                            const Poly2DVert& B = work[i];
                            const Poly2DVert& C = work[(i + 1) % wn];
                            double cr = cross2D(A, B, C);
                            if (cr <= areaEps) continue;
                            bool blocked = false;
                            if (pass == 0) {
                                for (size_t k = 0; k < wn; ++k) {
                                    if (k == i || k == (i + wn - 1) % wn || k == (i + 1) % wn) continue;
                                    const Poly2DVert& P = work[k];
                                    if (same2D(P, A) || same2D(P, B) || same2D(P, C)) continue;
                                    if (ptInTri2D(P, A, B, C)) { blocked = true; break; }
                                }
                            }
                            if (!blocked) {
                                earIdx = static_cast<int>(i);
                                break;
                            }
                        }
                    }

                    if (earIdx < 0) earIdx = 0;
                    size_t ei = static_cast<size_t>(earIdx);
                    uint32_t a = work[(ei + wn - 1) % wn].vIdx;
                    uint32_t b = work[ei].vIdx;
                    uint32_t c = work[(ei + 1) % wn].vIdx;
                    if (a != b && b != c && a != c) {
                        if (flipWinding) mesh.tris.push_back({ a, c, b, {} });
                        else             mesh.tris.push_back({ a, b, c, {} });
                    }
                    work.erase(work.begin() + static_cast<std::ptrdiff_t>(ei));
                }
            };

            // Reconstruct & triangulate every B-Rep face
            const double kPi = 3.14159265358979323846;
            for (int32_t faceId : g.faceOrder) {
                auto fit = g.faces.find(faceId);
                if (fit == g.faces.end()) continue;
                const BRepFace& face = fit->second;
                CadXform xf = getBodyXf(face.bodyId);

                const BRepSurf* surf = nullptr;
                auto sit = g.surfaces.find(face.surfaceId);
                if (sit != g.surfaces.end()) surf = &sit->second;

                // Collect all loops on this face
                std::vector<int32_t> loopList = face.loopIds;
                if (loopList.empty() && face.firstLoop >= 0) {
                    int32_t curL = face.firstLoop;
                    std::set<int32_t> seenL;
                    while (curL >= 0 && seenL.insert(curL).second && seenL.size() <= 512) {
                        loopList.push_back(curL);
                        auto lit = g.loops.find(curL);
                        if (lit == g.loops.end()) break;
                        curL = lit->second.nextLoop;
                    }
                }

                // Build 3D polylines for each loop
                std::vector<std::vector<CadVec3>> loops3D;
                for (int32_t lid : loopList) {
                    auto lit = g.loops.find(lid);
                    if (lit == g.loops.end()) continue;
                    const BRepLoop& lp = lit->second;
                    std::vector<CadVec3> rawLoop;

                    if (lp.firstCoedge >= 0) {
                        std::vector<int32_t> ceChain;
                        int32_t curCe = lp.firstCoedge;
                        std::set<int32_t> seenCe;
                        while (curCe >= 0 && seenCe.insert(curCe).second && ceChain.size() < 4096) {
                            auto ceIt = g.coedges.find(curCe);
                            if (ceIt == g.coedges.end()) break;
                            ceChain.push_back(curCe);
                            int32_t nxt = ceIt->second.nextCoedge;
                            if (nxt == lp.firstCoedge) break;
                            curCe = nxt;
                        }

                        for (size_t ci = 0; ci < ceChain.size(); ++ci) {
                            auto ceCurIt = g.coedges.find(ceChain[ci]);
                            if (ceCurIt == g.coedges.end()) continue;
                            const BRepCoedge& ce = ceCurIt->second;
                            auto edIt = g.edges.find(ce.edgeId);
                            if (edIt == g.edges.end()) continue;
                            const BRepEdge& ed = edIt->second;

                            bool ceFwd = !ce.reversed;
                            int32_t vStart = ceFwd ? ed.startVert : ed.endVert;
                            int32_t vEnd   = ceFwd ? ed.endVert   : ed.startVert;
                            // Parasolid PK fin head-vertex fallback when edge doesn't store direct vertex refs
                            if (vStart < 0 && vEnd < 0 && ce.headVert >= 0 && !ceChain.empty()) {
                                auto prevIt = g.coedges.find(ceChain[(ci + ceChain.size() - 1) % ceChain.size()]);
                                if (prevIt != g.coedges.end()) {
                                    vStart = prevIt->second.headVert;
                                    vEnd   = ce.headVert;
                                }
                            }

                            auto segPts = sampleEdgePoints(ed, vStart, vEnd, ceFwd);
                            for (const auto& sp : segPts) {
                                CadVec3 wp = applyXfPt(xf, sp);
                                if (rawLoop.empty() || vDist(rawLoop.back(), wp) > globalSpan * 1e-6) {
                                    rawLoop.push_back(wp);
                                }
                            }
                        }
                    } else if (!lp.directEdges.empty()) {
                        for (int32_t eid : lp.directEdges) {
                            auto edIt = g.edges.find(eid);
                            if (edIt == g.edges.end()) continue;
                            const BRepEdge& ed = edIt->second;
                            auto segPts = sampleEdgePoints(ed, ed.startVert, ed.endVert, true);
                            if (!rawLoop.empty() && segPts.size() >= 2) {
                                CadVec3 wFirst = applyXfPt(xf, segPts.front());
                                CadVec3 wLast  = applyXfPt(xf, segPts.back());
                                if (vDist(rawLoop.back(), wLast) < vDist(rawLoop.back(), wFirst)) {
                                    std::reverse(segPts.begin(), segPts.end());
                                }
                            }
                            for (const auto& sp : segPts) {
                                CadVec3 wp = applyXfPt(xf, sp);
                                if (rawLoop.empty() || vDist(rawLoop.back(), wp) > globalSpan * 1e-6) {
                                    rawLoop.push_back(wp);
                                }
                            }
                        }
                    }

                    if (rawLoop.size() >= 2 && vDist(rawLoop.front(), rawLoop.back()) <= globalSpan * 1e-5) {
                        rawLoop.pop_back();
                    }
                    if (rawLoop.size() >= 3) {
                        loops3D.push_back(std::move(rawLoop));
                    }
                }

                // Untrimmed closed analytic Sphere / Torus faces (no boundary loops)
                if (loops3D.empty() && surf != nullptr &&
                    (surf->kind == SurfKind::Sphere || surf->kind == SurfKind::Torus)) {
                    if (surf->kind == SurfKind::Sphere && surf->radius1 > 1e-9) {
                        CadVec3 center = applyXfPt(xf, surf->origin);
                        double rad = surf->radius1 * (xf.valid ? std::fabs(xf.scale) : 1.0);
                        const int nLat = 16, nLon = 24;
                        std::vector<std::vector<uint32_t>> grid(nLat + 1, std::vector<uint32_t>(nLon));
                        for (int i = 0; i <= nLat; ++i) {
                            double phi = -0.5 * kPi + kPi * (static_cast<double>(i) / nLat);
                            double cp = std::cos(phi), sp = std::sin(phi);
                            for (int j = 0; j < nLon; ++j) {
                                double th = 2.0 * kPi * (static_cast<double>(j) / nLon);
                                CadVec3 p{
                                    center.x + rad * cp * std::cos(th),
                                    center.y + rad * cp * std::sin(th),
                                    center.z + rad * sp
                                };
                                grid[static_cast<size_t>(i)][static_cast<size_t>(j)] = emitVertex(p);
                            }
                        }
                        for (int i = 0; i < nLat; ++i) {
                            for (int j = 0; j < nLon; ++j) {
                                int nj = (j + 1) % nLon;
                                uint32_t a = grid[static_cast<size_t>(i)][static_cast<size_t>(j)];
                                uint32_t b = grid[static_cast<size_t>(i)][static_cast<size_t>(nj)];
                                uint32_t c = grid[static_cast<size_t>(i + 1)][static_cast<size_t>(nj)];
                                uint32_t d = grid[static_cast<size_t>(i + 1)][static_cast<size_t>(j)];
                                if (a != b && b != c && a != c) {
                                    if (face.reversed) mesh.tris.push_back({ a, c, b, {} });
                                    else               mesh.tris.push_back({ a, b, c, {} });
                                }
                                if (a != c && c != d && a != d) {
                                    if (face.reversed) mesh.tris.push_back({ a, d, c, {} });
                                    else               mesh.tris.push_back({ a, c, d, {} });
                                }
                            }
                        }
                    } else if (surf->kind == SurfKind::Torus && surf->radius1 > 1e-9 && std::fabs(surf->radius2) > 1e-9) {
                        CadVec3 center = applyXfPt(xf, surf->origin);
                        CadVec3 axis   = applyXfDir(xf, surf->normal);
                        CadVec3 ref    = (std::fabs(axis.z) < 0.9) ? CadVec3{ 0, 0, 1 } : CadVec3{ 1, 0, 0 };
                        CadVec3 uAx    = vNorm(vCross(ref, axis));
                        CadVec3 vAx    = vNorm(vCross(axis, uAx));
                        double rMaj = surf->radius1 * (xf.valid ? std::fabs(xf.scale) : 1.0);
                        double rMin = std::fabs(surf->radius2) * (xf.valid ? std::fabs(xf.scale) : 1.0);
                        const int nMaj = 24, nMin = 16;
                        std::vector<std::vector<uint32_t>> grid(nMaj, std::vector<uint32_t>(nMin));
                        for (int i = 0; i < nMaj; ++i) {
                            double u = 2.0 * kPi * (static_cast<double>(i) / nMaj);
                            CadVec3 radDir = vAdd(vMul(uAx, std::cos(u)), vMul(vAx, std::sin(u)));
                            for (int j = 0; j < nMin; ++j) {
                                double v = 2.0 * kPi * (static_cast<double>(j) / nMin);
                                CadVec3 p = vAdd(center, vAdd(vMul(radDir, rMaj + rMin * std::cos(v)), vMul(axis, rMin * std::sin(v))));
                                grid[static_cast<size_t>(i)][static_cast<size_t>(j)] = emitVertex(p);
                            }
                        }
                        for (int i = 0; i < nMaj; ++i) {
                            int ni = (i + 1) % nMaj;
                            for (int j = 0; j < nMin; ++j) {
                                int nj = (j + 1) % nMin;
                                uint32_t a = grid[static_cast<size_t>(i)][static_cast<size_t>(j)];
                                uint32_t b = grid[static_cast<size_t>(ni)][static_cast<size_t>(j)];
                                uint32_t c = grid[static_cast<size_t>(ni)][static_cast<size_t>(nj)];
                                uint32_t d = grid[static_cast<size_t>(i)][static_cast<size_t>(nj)];
                                if (face.reversed) {
                                    mesh.tris.push_back({ a, c, b, {} });
                                    mesh.tris.push_back({ a, d, c, {} });
                                } else {
                                    mesh.tris.push_back({ a, b, c, {} });
                                    mesh.tris.push_back({ a, c, d, {} });
                                }
                            }
                        }
                    }
                    continue;
                }

                // Rational / Non-Rational Cox-de Boor NURBS Surface (b_surface / spline-surface)
                if (surf != nullptr && surf->kind == SurfKind::Nurbs &&
                    surf->uDeg >= 1 && surf->vDeg >= 1 && surf->nU > surf->uDeg && surf->nV > surf->vDeg &&
                    static_cast<int>(surf->knotsU.size()) >= surf->nU + surf->uDeg + 1 &&
                    static_cast<int>(surf->knotsV.size()) >= surf->nV + surf->vDeg + 1 &&
                    surf->poles.size() >= static_cast<size_t>(surf->nU) * static_cast<size_t>(surf->nV))
                {
                    const auto& evalBasis = MeshBuild3D::EvalNurbsBasisAll;

                    double u0 = surf->knotsU[static_cast<size_t>(surf->uDeg)];
                    double u1 = surf->knotsU[static_cast<size_t>(surf->nU)];
                    double v0 = surf->knotsV[static_cast<size_t>(surf->vDeg)];
                    double v1 = surf->knotsV[static_cast<size_t>(surf->nV)];
                    if (u1 > u0 && v1 > v0) {
                        constexpr int kNu = 16, kNv = 16;
                        std::vector<std::vector<uint32_t>> grid(kNu + 1, std::vector<uint32_t>(kNv + 1));
                        std::vector<double> bu, bv;
                        bool hasWeights = surf->isRational && surf->weights.size() >= surf->poles.size();
                        for (int iu = 0; iu <= kNu; ++iu) {
                            double u = u0 + (u1 - u0) * (static_cast<double>(iu) / kNu);
                            evalBasis(surf->nU, surf->uDeg, surf->knotsU, u, bu);
                            for (int iv = 0; iv <= kNv; ++iv) {
                                double v = v0 + (v1 - v0) * (static_cast<double>(iv) / kNv);
                                evalBasis(surf->nV, surf->vDeg, surf->knotsV, v, bv);
                                double sx = 0.0, sy = 0.0, sz = 0.0, sw = 0.0;
                                for (int i = 0; i < surf->nU; ++i) {
                                    if (bu[static_cast<size_t>(i)] == 0.0) continue;
                                    for (int j = 0; j < surf->nV; ++j) {
                                        if (bv[static_cast<size_t>(j)] == 0.0) continue;
                                        size_t pidx = static_cast<size_t>(i * surf->nV + j);
                                        double w = hasWeights ? surf->weights[pidx] : 1.0;
                                        double b = bu[static_cast<size_t>(i)] * bv[static_cast<size_t>(j)] * w;
                                        sx += b * surf->poles[pidx].x;
                                        sy += b * surf->poles[pidx].y;
                                        sz += b * surf->poles[pidx].z;
                                        sw += b;
                                    }
                                }
                                if (std::fabs(sw) > 1e-12) { sx /= sw; sy /= sw; sz /= sw; }
                                grid[static_cast<size_t>(iu)][static_cast<size_t>(iv)] = emitVertex(applyXfPt(xf, { sx, sy, sz }));
                            }
                        }
                        for (int iu = 0; iu < kNu; ++iu) {
                            for (int iv = 0; iv < kNv; ++iv) {
                                uint32_t a = grid[static_cast<size_t>(iu)][static_cast<size_t>(iv)];
                                uint32_t b = grid[static_cast<size_t>(iu + 1)][static_cast<size_t>(iv)];
                                uint32_t c = grid[static_cast<size_t>(iu + 1)][static_cast<size_t>(iv + 1)];
                                uint32_t d = grid[static_cast<size_t>(iu)][static_cast<size_t>(iv + 1)];
                                if (a != b && b != c && a != c) {
                                    if (face.reversed) mesh.tris.push_back({ a, c, b, {} });
                                    else               mesh.tris.push_back({ a, b, c, {} });
                                }
                                if (a != c && c != d && a != d) {
                                    if (face.reversed) mesh.tris.push_back({ a, d, c, {} });
                                    else               mesh.tris.push_back({ a, c, d, {} });
                                }
                            }
                        }
                        continue;
                    }
                }

                if (loops3D.empty()) continue;

                // Cylindrical / Conical surface unwrap in (theta, axial_h)
                if (surf != nullptr && surf->kind == SurfKind::CylinderCone && vLen(surf->normal) > 1e-9) {
                    CadVec3 cOrig = applyXfPt(xf, surf->origin);
                    CadVec3 cAxis = applyXfDir(xf, surf->normal);
                    CadVec3 uHint = (vLen(surf->uAxis) > 1e-9) ? applyXfDir(xf, surf->uAxis)
                                  : ((std::fabs(cAxis.z) < 0.9) ? CadVec3{ 0, 0, 1 } : CadVec3{ 1, 0, 0 });
                    uHint = vSub(uHint, vMul(cAxis, vDot(uHint, cAxis)));
                    if (vLen(uHint) < 1e-9) uHint = (std::fabs(cAxis.x) < 0.9) ? CadVec3{ 1, 0, 0 } : CadVec3{ 0, 1, 0 };
                    CadVec3 u0 = vNorm(uHint);
                    CadVec3 v0 = vNorm(vCross(cAxis, u0));

                    struct UnwrappedCylLoop {
                        std::vector<Poly2DVert> pts;
                        double sweep = 0.0;
                        double meanH = 0.0;
                    };
                    std::vector<UnwrappedCylLoop> cylLoops;
                    for (const auto& lp3 : loops3D) {
                        UnwrappedCylLoop ul;
                        double prevTh = 0.0;
                        double sumH = 0.0;
                        for (size_t k = 0; k < lp3.size(); ++k) {
                            CadVec3 d = vSub(lp3[k], cOrig);
                            double h = vDot(d, cAxis);
                            double th = std::atan2(vDot(d, v0), vDot(d, u0));
                            if (k > 0) {
                                while (th - prevTh >  kPi) th -= 2.0 * kPi;
                                while (th - prevTh < -kPi) th += 2.0 * kPi;
                            }
                            prevTh = th;
                            sumH += h;
                            ul.pts.push_back({ th, h, emitVertex(lp3[k]) });
                        }
                        // Check closing delta to detect full 2*pi coaxial circle loops vs closed (theta,h) patches
                        CadVec3 d0 = vSub(lp3[0], cOrig);
                        double thClose = std::atan2(vDot(d0, v0), vDot(d0, u0));
                        while (thClose - prevTh >  kPi) thClose -= 2.0 * kPi;
                        while (thClose - prevTh < -kPi) thClose += 2.0 * kPi;
                        ul.sweep = thClose - ul.pts.front().u;
                        ul.meanH = sumH / static_cast<double>(lp3.size());
                        cylLoops.push_back(std::move(ul));
                    }

                    // Case 1: Seamless periodic cylinder/cone with 2 full-circle coaxial boundary loops
                    std::vector<size_t> fullCircleIdx;
                    std::vector<std::vector<Poly2DVert>> wallHoles;
                    for (size_t li = 0; li < cylLoops.size(); ++li) {
                        if (std::fabs(cylLoops[li].sweep) > 1.5 * kPi) fullCircleIdx.push_back(li);
                        else wallHoles.push_back(cylLoops[li].pts);
                    }
                    if (fullCircleIdx.size() == 2) {
                        size_t iLow  = fullCircleIdx[0];
                        size_t iHigh = fullCircleIdx[1];
                        if (cylLoops[iLow].meanH > cylLoops[iHigh].meanH) std::swap(iLow, iHigh);
                        const auto& lpLow  = loops3D[iLow];
                        const auto& lpHigh = loops3D[iHigh];

                        // Sort each coaxial ring by exact angle theta in [-pi, pi) so wall triangles share watertight cap vertices
                        auto buildSortedRing = [&](const std::vector<CadVec3>& ring) -> std::vector<Poly2DVert> {
                            std::vector<Poly2DVert> out;
                            out.reserve(ring.size() + 1);
                            for (const auto& pt : ring) {
                                CadVec3 d = vSub(pt, cOrig);
                                double th = std::atan2(vDot(d, v0), vDot(d, u0));
                                double h  = vDot(d, cAxis);
                                out.push_back({ th, h, emitVertex(pt) });
                            }
                            std::sort(out.begin(), out.end(), [](const Poly2DVert& a, const Poly2DVert& b) {
                                return a.u < b.u;
                            });
                            return out;
                        };

                        auto rLow  = buildSortedRing(lpLow);
                        auto rHigh = buildSortedRing(lpHigh);
                        if (rLow.size() >= 3 && rHigh.size() >= 3) {
                            double thBase = rLow.front().u;
                            // Shift rHigh angles into [thBase, thBase + 2*pi) and re-sort so seams align
                            for (auto& q : rHigh) {
                                while (q.u <  thBase - 1e-9)            q.u += 2.0 * kPi;
                                while (q.u >= thBase + 2.0 * kPi - 1e-9) q.u -= 2.0 * kPi;
                            }
                            std::sort(rHigh.begin(), rHigh.end(), [](const Poly2DVert& a, const Poly2DVert& b) {
                                return a.u < b.u;
                            });

                            std::vector<Poly2DVert> rectOuter;
                            rectOuter.reserve(rLow.size() + rHigh.size() + 2);
                            for (const auto& q : rLow) rectOuter.push_back(q);
                            rectOuter.push_back({ rLow.front().u + 2.0 * kPi, rLow.front().v, rLow.front().vIdx });
                            rectOuter.push_back({ rHigh.front().u + 2.0 * kPi, rHigh.front().v, rHigh.front().vIdx });
                            for (size_t k = rHigh.size(); k > 0; --k) rectOuter.push_back(rHigh[k - 1]);

                            // Normalize any wall hole theta centers into [thBase, thBase + 2*pi)
                            for (auto& wh : wallHoles) {
                                double cTh = 0.0;
                                for (const auto& q : wh) cTh += q.u;
                                cTh /= static_cast<double>(wh.size());
                                double shift = 0.0;
                                while (cTh + shift <  thBase)             shift += 2.0 * kPi;
                                while (cTh + shift >= thBase + 2.0 * kPi) shift -= 2.0 * kPi;
                                for (auto& q : wh) q.u += shift;
                            }
                            triangulateOuterWithHoles(std::move(rectOuter), std::move(wallHoles), face.reversed);
                            continue;
                        }
                    }

                    // Case 2: Bounded cylinder/cone patch(es) with seam or partial angular span
                    size_t outerIdx = 0;
                    double bestAbsArea = -1.0;
                    for (size_t li = 0; li < cylLoops.size(); ++li) {
                        double aa = std::fabs(signedArea2D(cylLoops[li].pts));
                        if (aa > bestAbsArea) { bestAbsArea = aa; outerIdx = li; }
                    }
                    if (bestAbsArea > 1e-10) {
                        std::vector<std::vector<Poly2DVert>> holes2D;
                        for (size_t li = 0; li < cylLoops.size(); ++li) {
                            if (li != outerIdx) holes2D.push_back(cylLoops[li].pts);
                        }
                        triangulateOuterWithHoles(cylLoops[outerIdx].pts, std::move(holes2D), face.reversed);
                        continue;
                    }
                }

                // Planar (or general curved) face: determine target 3D normal & tangent basis (uBasis, vBasis, nBasis)
                CadVec3 nBasis = { 0.0, 0.0, 0.0 };
                if (surf != nullptr && surf->kind == SurfKind::Plane && vLen(surf->normal) > 1e-9) {
                    nBasis = applyXfDir(xf, surf->normal);
                    if (face.reversed) nBasis = vMul(nBasis, -1.0);
                } else {
                    // Newell's method on the largest-perimeter loop
                    size_t bestL = 0;
                    for (size_t li = 1; li < loops3D.size(); ++li) {
                        if (loops3D[li].size() > loops3D[bestL].size()) bestL = li;
                    }
                    const auto& lp = loops3D[bestL];
                    CadVec3 nw{};
                    for (size_t i = 0; i < lp.size(); ++i) {
                        const CadVec3& c0 = lp[i];
                        const CadVec3& c1 = lp[(i + 1) % lp.size()];
                        nw.x += (c0.y - c1.y) * (c0.z + c1.z);
                        nw.y += (c0.z - c1.z) * (c0.x + c1.x);
                        nw.z += (c0.x - c1.x) * (c0.y + c1.y);
                    }
                    if (vLen(nw) < 1e-12) continue;
                    nBasis = vNorm(nw);
                    if (face.reversed) nBasis = vMul(nBasis, -1.0);
                }

                CadVec3 refAx = (std::fabs(nBasis.z) < 0.9) ? CadVec3{ 0.0, 0.0, 1.0 } : CadVec3{ 1.0, 0.0, 0.0 };
                CadVec3 uBasis = vNorm(vCross(refAx, nBasis));
                CadVec3 vBasis = vNorm(vCross(nBasis, uBasis)); // right-handed: uBasis x vBasis == +nBasis

                CadVec3 origin3D = loops3D[0][0];
                struct ProjectedLoop {
                    std::vector<Poly2DVert> pts;
                    double absArea = 0.0;
                };
                std::vector<ProjectedLoop> projLoops;
                for (const auto& lp3 : loops3D) {
                    ProjectedLoop pl;
                    pl.pts.reserve(lp3.size());
                    for (const auto& p3 : lp3) {
                        CadVec3 rel = vSub(p3, origin3D);
                        pl.pts.push_back({ vDot(rel, uBasis), vDot(rel, vBasis), emitVertex(p3) });
                    }
                    pl.absArea = std::fabs(signedArea2D(pl.pts));
                    if (pl.pts.size() >= 3 && pl.absArea > 1e-12) {
                        projLoops.push_back(std::move(pl));
                    }
                }
                if (projLoops.empty()) continue;

                // Sort projected loops by descending area so outer islands precede inner holes
                std::sort(projLoops.begin(), projLoops.end(), [](const ProjectedLoop& a, const ProjectedLoop& b) {
                    return a.absArea > b.absArea;
                });

                // Group inner hole loops into their enclosing outer island
                std::vector<int> parentIsland(projLoops.size(), -1);
                for (size_t i = 1; i < projLoops.size(); ++i) {
                    for (size_t j = i; j > 0; --j) {
                        size_t cand = j - 1;
                        if (parentIsland[cand] == -1 &&
                            pointInPoly2D(projLoops[i].pts[0].u, projLoops[i].pts[0].v, projLoops[cand].pts))
                        {
                            parentIsland[i] = static_cast<int>(cand);
                            break;
                        }
                    }
                }

                for (size_t i = 0; i < projLoops.size(); ++i) {
                    if (parentIsland[i] != -1) continue;
                    std::vector<std::vector<Poly2DVert>> myHoles;
                    for (size_t k = i + 1; k < projLoops.size(); ++k) {
                        if (parentIsland[k] == static_cast<int>(i)) {
                            myHoles.push_back(projLoops[k].pts);
                        }
                    }
                    // Since uBasis x vBasis == +nBasis (which already accounts for face.reversed), CCW in 2D == outward 3D!
                    triangulateOuterWithHoles(projLoops[i].pts, std::move(myHoles), false);
                }
            }

            // Pure wireframe B-Rep fallback (wire-body / topological edges without faces):
            // NEVER connect bare point soup — emit struts ONLY along genuine topological edge records!
            if (mesh.tris.empty() && !g.edges.empty()) {
                std::vector<std::vector<Vec3f>> edgePolylines;
                std::vector<Vec3f> allWirePts;
                for (int32_t eid : g.edgeOrder) {
                    auto eit = g.edges.find(eid);
                    if (eit == g.edges.end()) continue;
                    const BRepEdge& ed = eit->second;
                    auto segPts = sampleEdgePoints(ed, ed.startVert, ed.endVert, true);
                    if (segPts.size() < 2) continue;
                    CadXform xf = getBodyXf(ed.bodyId);
                    std::vector<Vec3f> wPoly;
                    wPoly.reserve(segPts.size());
                    for (const auto& sp : segPts) {
                        CadVec3 wp = applyXfPt(xf, sp);
                        Vec3f vf{ static_cast<float>(wp.x), static_cast<float>(wp.y), static_cast<float>(wp.z) };
                        wPoly.push_back(vf);
                        allWirePts.push_back(vf);
                    }
                    edgePolylines.push_back(std::move(wPoly));
                }
                if (!edgePolylines.empty() && allWirePts.size() >= 2) {
                    float span = DetailMeshSciCad::ComputePointCloudSpan(allWirePts);
                    float strutR = std::max(1e-4f, span * 0.016f);
                    for (const auto& wp : edgePolylines) {
                        for (size_t i = 0; i + 1 < wp.size(); ++i) {
                            DetailMeshSciCad::AddStrut3D(mesh, wp[i], wp[i + 1], strutR);
                        }
                    }
                }
            }

            return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailAcisParasolid

namespace BlizzDetail {

inline Vec3f BlizzZUpToViewport(float x, float y, float z) {
    return { x, z, -y };
}

} // namespace BlizzDetail

namespace BspGeoDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdI16BE;
using ::Explorer2::ByteIO::RdU32BE;

} // namespace BspGeoDetail

namespace CadPkgDetail {

// little-endian loads (ByteIO.h)
inline uint16_t RdU16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t RdU32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline float RdF32(const uint8_t* p) { return ::Explorer2::ByteIO::RdF32LE(p); }
inline double RdF64(const uint8_t* p) { return ::Explorer2::ByteIO::RdF64LE(p); }

struct Placement3D {
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;

    Vec3f Apply(const Vec3f& v) const {
        float n2 = qx * qx + qy * qy + qz * qz + qw * qw;
        float x = qx, y = qy, z = qz, w = qw;
        if (std::isfinite(n2) && n2 > 1e-12f) {
            float inv = 1.0f / std::sqrt(n2);
            x *= inv; y *= inv; z *= inv; w *= inv;
        } else {
            x = y = z = 0.0f; w = 1.0f;
        }
        float tx = 2.0f * (y * v.z - z * v.y);
        float ty = 2.0f * (z * v.x - x * v.z);
        float tz = 2.0f * (x * v.y - y * v.x);
        return {
            v.x + w * tx + (y * tz - z * ty) + px,
            v.y + w * ty + (z * tx - x * tz) + py,
            v.z + w * tz + (x * ty - y * tx) + pz
        };
    }
};

inline std::string_view XmlAttrValue(std::string_view tag, std::string_view keyName) {
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t eq = tag.find('=', pos);
        if (eq == std::string_view::npos) break;
        size_t ke = eq;
        while (ke > pos && (tag[ke - 1] == ' ' || tag[ke - 1] == '\t')) --ke;
        size_t ks = ke;
        while (ks > pos && (std::isalnum(static_cast<unsigned char>(tag[ks - 1])) || tag[ks - 1] == '_')) --ks;
        std::string_view k = tag.substr(ks, ke - ks);
        size_t vs = eq + 1;
        while (vs < tag.size() && (tag[vs] == ' ' || tag[vs] == '\t')) ++vs;
        if (vs < tag.size() && (tag[vs] == '"' || tag[vs] == '\'')) {
            char q = tag[vs++];
            size_t ve = tag.find(q, vs);
            if (ve == std::string_view::npos) break;
            if (k == keyName) return tag.substr(vs, ve - vs);
            pos = ve + 1;
        } else {
            pos = vs + 1;
        }
    }
    return {};
}

inline float ParseAttrFloat(std::string_view tag, std::string_view keyName, float defVal = 0.0f) {
    std::string_view sv = XmlAttrValue(tag, keyName);
    if (sv.empty()) return defVal;
    float v = defVal;
    auto r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    return (r.ec == std::errc{} && std::isfinite(v)) ? v : defVal;
}

inline void AppendSubMeshPlaced(const LoadedMesh3D& part, const Placement3D& pl, LoadedMesh3D& dst) {
    if (part.vertices.empty() || part.tris.empty()) return;
    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + part.vertices.size());
    for (const Vec3f& v : part.vertices) {
        dst.vertices.push_back(pl.Apply(v));
    }
    dst.tris.reserve(dst.tris.size() + part.tris.size());
    for (const auto& t : part.tris) {
        dst.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
    }
}

inline void AppendSubMeshMat4(const LoadedMesh3D& part, const double m[16], LoadedMesh3D& dst) {
    if (part.vertices.empty() || part.tris.empty()) return;
    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + part.vertices.size());
    for (const Vec3f& v : part.vertices) {
        // Row-major 4x4 [m0..m3; m4..m7; m8..m11; 0 0 0 1]
        double x = m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3];
        double y = m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7];
        double z = m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { x = y = z = 0.0; }
        dst.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
    }
    dst.tris.reserve(dst.tris.size() + part.tris.size());
    for (const auto& t : part.tris) {
        dst.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
    }
}

// Parse FreeCAD Binary Mesh (.bms: uint32 nPts, nPts*float3, uint32 nFacets, nFacets * uint32[6] (v0,v1,v2, n0,n1,n2))
inline bool ParseFreeCadBinaryMeshStream(const uint8_t* data, size_t size, LoadedMesh3D& out) {
    if (!data || size < 20) return false;
    uint32_t nPts = RdU32(data + 0);
    if (nPts < 3 || nPts > 4000000u) return false;
    size_t posAfterPts = 4u + static_cast<size_t>(nPts) * 12u;
    if (posAfterPts + 4u > size) return false;
    uint32_t nFacets = RdU32(data + posAfterPts);
    size_t facetStride = 24u; // 3 vertex indices + 3 neighbor indices
    if (nFacets < 1 || nFacets > 4000000u) return false;
    if (posAfterPts + 4u + static_cast<uint64_t>(nFacets) * facetStride > size) {
        facetStride = 12u; // compact 3-index variant
        if (posAfterPts + 4u + static_cast<uint64_t>(nFacets) * facetStride > size) return false;
    }
    out.vertices.reserve(nPts);
    for (uint32_t i = 0; i < nPts; ++i) {
        const uint8_t* vp = data + 4u + static_cast<size_t>(i) * 12u;
        float x = RdF32(vp + 0), y = RdF32(vp + 4), z = RdF32(vp + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        out.vertices.push_back({ x, y, z });
    }
    const uint8_t* fp = data + posAfterPts + 4u;
    for (uint32_t f = 0; f < nFacets; ++f) {
        uint32_t i0 = RdU32(fp + static_cast<size_t>(f) * facetStride + 0);
        uint32_t i1 = RdU32(fp + static_cast<size_t>(f) * facetStride + 4);
        uint32_t i2 = RdU32(fp + static_cast<size_t>(f) * facetStride + 8);
        if (i0 >= nPts || i1 >= nPts || i2 >= nPts) return false;
        if (i0 != i1 && i1 != i2 && i0 != i2) out.tris.push_back({ i0, i1, i2, {} });
    }
    return !out.tris.empty();
}

// Try inflating an RSe / Solid Edge OLE2 stream (raw zlib at offset 0, +4, +8, +12, or +16)
inline bool TryInflateCadStream(const std::vector<uint8_t>& raw, std::vector<uint8_t>& outInflated) {
    outInflated.clear();
    for (size_t off : { size_t(0), size_t(4), size_t(8), size_t(12), size_t(16) }) {
        if (off + 6 >= raw.size()) break;
        uint8_t b0 = raw[off], b1 = raw[off + 1];
        if ((b0 == 0x78) && ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u)) {
            if (ZipDeflate::InflateZlib(raw.data() + off, raw.size() - off, outInflated, 64 * 1024 * 1024) &&
                !outInflated.empty())
            {
                return true;
            }
        }
    }
    return false;
}

inline bool TryDecodeCadPayloadBuffer(const uint8_t* data, size_t size, LoadedMesh3D& dst) {
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    LoadedMesh3D sub;
    if (sv.find("DBRep_DrawableShape") != std::string_view::npos || sv.find("CASCADE Topology") != std::string_view::npos) {
        if (ParseCadKernelArchiveOrStream(data, size, L".brep", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if (size >= 15 && (std::memcmp(data, "ACIS BinaryFile", 15) == 0 || std::memcmp(data, "ASM BinaryFile", 14) == 0)) {
        if (ParseCadKernelArchiveOrStream(data, size, L".sab", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if (sv.substr(0, std::min<size_t>(size, 256)).find("ACIS ") != std::string_view::npos ||
        (size >= 8 && std::isdigit(static_cast<unsigned char>(data[0])) && sv.find("body ") != std::string_view::npos && sv.find("End-of-ACIS") != std::string_view::npos))
    {
        if (ParseCadKernelArchiveOrStream(data, size, L".sat", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if ((size >= 4 && data[0] == 'P' && data[1] == 'S' && data[2] == 0) ||
        sv.find("**END_OF_HEADER**") != std::string_view::npos)
    {
        if (ParseCadKernelArchiveOrStream(data, size, L".x_b", sub) ||
            ParseCadKernelArchiveOrStream(data, size, L".x_t", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if (sv.find("**PART") != std::string_view::npos || sv.find("T-_SCH") != std::string_view::npos ||
        sv.find("SCH_TXT") != std::string_view::npos || sv.find("PARASOLID") != std::string_view::npos)
    {
        if (ParseCadKernelArchiveOrStream(data, size, L".x_t", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    return false;
}

} // namespace CadPkgDetail


namespace CalCryDetail {

// little-endian loads (ByteIO.h)
inline uint16_t RdU16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t RdU32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline int32_t RdI32(const uint8_t* p) { return ::Explorer2::ByteIO::RdI32LE(p); }
inline float RdF32(const uint8_t* p) { return ::Explorer2::ByteIO::RdF32LE(p); }

using ::Explorer2::ByteIO::HalfToFloat;

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    static Quat Normalize(Quat q) {
        float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (!std::isfinite(n2) || n2 < 1e-12f) return { 0.0f, 0.0f, 0.0f, 1.0f };
        float inv = 1.0f / std::sqrt(n2);
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }
    static Quat Mul(const Quat& a, const Quat& b) { return MeshBuild3D::QuatMul(a, b); }
    Vec3f Rotate(const Vec3f& v) const {
        // v + 2*w*(q.xyz x v) + 2*(q.xyz x (q.xyz x v))
        float tx = 2.0f * (y * v.z - z * v.y);
        float ty = 2.0f * (z * v.x - x * v.z);
        float tz = 2.0f * (x * v.y - y * v.x);
        return {
            v.x + w * tx + (y * tz - z * ty),
            v.y + w * ty + (z * tx - x * tz),
            v.z + w * tz + (x * ty - y * tx)
        };
    }
};

struct CalBone {
    std::string name;
    int32_t parentId = -1;
    Vec3f localPos = { 0.0f, 0.0f, 0.0f };
    Quat  localRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3f bindSpacePos = { 0.0f, 0.0f, 0.0f }; // Cal3D localSpaceTranslation (model -> bone bind)
    Quat  bindSpaceRot = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3f worldPos = { 0.0f, 0.0f, 0.0f };
    Quat  worldRot = { 0.0f, 0.0f, 0.0f, 1.0f };
};

inline void EvaluateCalSkeletonWorld(std::vector<CalBone>& bones) {
    std::vector<uint8_t> state(bones.size(), 0);
    std::function<void(size_t, int)> eval = [&](size_t idx, int depth) {
        if (state[idx] == 2) return;
        if (state[idx] == 1 || depth > 256) {
            bones[idx].worldPos = bones[idx].localPos;
            bones[idx].worldRot = Quat::Normalize(bones[idx].localRot);
            state[idx] = 2;
            return;
        }
        state[idx] = 1;
        CalBone& b = bones[idx];
        Quat lr = Quat::Normalize(b.localRot);
        if (b.parentId >= 0 && static_cast<size_t>(b.parentId) < bones.size()) {
            size_t pIdx = static_cast<size_t>(b.parentId);
            eval(pIdx, depth + 1);
            b.worldRot = Quat::Normalize(Quat::Mul(bones[pIdx].worldRot, lr));
            Vec3f rp = bones[pIdx].worldRot.Rotate(b.localPos);
            b.worldPos = { bones[pIdx].worldPos.x + rp.x, bones[pIdx].worldPos.y + rp.y, bones[pIdx].worldPos.z + rp.z };
        } else {
            b.worldRot = lr;
            b.worldPos = b.localPos;
        }
        state[idx] = 2;
    };
    for (size_t i = 0; i < bones.size(); ++i) eval(i, 0);
}

inline bool ParseCal3DBinarySkeleton(const uint8_t* data, size_t size, std::vector<CalBone>& outBones) {
    outBones.clear();
    if (!data || size < 16 || std::memcmp(data, "CSF\0", 4) != 0) return false;
    int32_t ver = RdI32(data + 4);
    int32_t numBones = RdI32(data + 8);
    if (ver < 700 || ver > 1500 || numBones <= 0 || numBones > 8192) return false;

    size_t pos = 12;
    outBones.reserve(static_cast<size_t>(numBones));
    for (int32_t i = 0; i < numBones; ++i) {
        if (pos + 4 > size) return false;
        int32_t nameLen = RdI32(data + pos); pos += 4;
        if (nameLen <= 0 || nameLen > 1024 || pos + static_cast<size_t>(nameLen) > size) return false;
        CalBone b;
        b.name.assign(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(nameLen));
        while (!b.name.empty() && b.name.back() == '\0') b.name.pop_back();
        pos += static_cast<size_t>(nameLen);

        // 14 floats (translation[3], rotation[4], localSpaceTranslation[3], localSpaceRotation[4]) + parentId (4B)
        size_t fixedBytes = 14u * 4u + 4u + ((ver < 900) ? 12u : 0u) + 4u;
        if (pos + fixedBytes > size) return false;

        b.localPos = { RdF32(data + pos + 0), RdF32(data + pos + 4), RdF32(data + pos + 8) };
        b.localRot = { RdF32(data + pos + 12), RdF32(data + pos + 16), RdF32(data + pos + 20), RdF32(data + pos + 24) };
        b.bindSpacePos = { RdF32(data + pos + 28), RdF32(data + pos + 32), RdF32(data + pos + 36) };
        b.bindSpaceRot = { RdF32(data + pos + 40), RdF32(data + pos + 44), RdF32(data + pos + 48), RdF32(data + pos + 52) };
        b.parentId = RdI32(data + pos + 56);
        pos += 60;
        if (ver < 900) pos += 12; // legacy lighting RGB floats

        int32_t numChildren = RdI32(data + pos); pos += 4;
        if (numChildren < 0 || numChildren > 8192 || pos + static_cast<size_t>(numChildren) * 4u > size) return false;
        pos += static_cast<size_t>(numChildren) * 4u;

        if (!std::isfinite(b.localPos.x) || !std::isfinite(b.localPos.y) || !std::isfinite(b.localPos.z) ||
            !std::isfinite(b.bindSpacePos.x) || !std::isfinite(b.bindSpacePos.y) || !std::isfinite(b.bindSpacePos.z)) {
            return false;
        }
        outBones.push_back(std::move(b));
    }
    EvaluateCalSkeletonWorld(outBones);
    return !outBones.empty();
}

struct CalWeightedVert {
    Vec3f pos = {};
    std::vector<std::pair<int32_t, float>> infs;
};

inline void ApplyCal3DSkinning(
    const std::vector<CalBone>& bones,
    std::vector<CalWeightedVert>& verts,
    std::vector<Vec3f>& outPositions)
{
    outPositions.resize(verts.size());
    if (bones.empty()) {
        for (size_t i = 0; i < verts.size(); ++i) outPositions[i] = verts[i].pos;
        return;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const auto& wv = verts[i];
        Vec3f accum = { 0.0f, 0.0f, 0.0f };
        float wSum = 0.0f;
        for (const auto& [bId, w] : wv.infs) {
            if (bId >= 0 && static_cast<size_t>(bId) < bones.size() && std::isfinite(w) && w > 1e-6f) {
                const CalBone& b = bones[static_cast<size_t>(bId)];
                // Cal3D skinning equation: v_bone = bindSpaceRot * v_mesh + bindSpacePos; v_world = worldRot * v_bone + worldPos
                Quat br = Quat::Normalize(b.bindSpaceRot);
                Vec3f vb = br.Rotate(wv.pos);
                vb.x += b.bindSpacePos.x; vb.y += b.bindSpacePos.y; vb.z += b.bindSpacePos.z;
                Vec3f vw = b.worldRot.Rotate(vb);
                vw.x += b.worldPos.x; vw.y += b.worldPos.y; vw.z += b.worldPos.z;
                accum.x += vw.x * w;
                accum.y += vw.y * w;
                accum.z += vw.z * w;
                wSum += w;
            }
        }
        if (wSum > 1e-6f) {
            float invW = 1.0f / wSum;
            outPositions[i] = { accum.x * invW, accum.y * invW, accum.z * invW };
        } else {
            outPositions[i] = wv.pos;
        }
    }
}

inline void LoadCompanionCalSkeleton(const std::wstring& fullPath, std::vector<CalBone>& outBones) {
    outBones.clear();
    if (fullPath.empty()) return;
    std::wstring dir = PathUtils::GetParentPath(fullPath);
    std::wstring stem = PathUtils::GetStem(PathUtils::GetFileName(fullPath), false);
    std::vector<uint8_t> csfBytes;
    if (PathUtils::ReadFileBytes(dir + L"\\" + stem + L".csf", 16 * 1024 * 1024, csfBytes) && !csfBytes.empty()) {
        if (ParseCal3DBinarySkeleton(csfBytes.data(), csfBytes.size(), outBones)) return;
    }
    // Also check if a single .csf skeleton shares the directory (standard Cal3D character folder layout)
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW((dir + L"\\*.csf").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        std::wstring foundCsf;
        int count = 0;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                foundCsf = dir + L"\\" + fd.cFileName;
                ++count;
            }
        } while (FindNextFileW(hFind, &fd) && count <= 2);
        FindClose(hFind);
        if (count == 1) {
            csfBytes.clear();
            if (PathUtils::ReadFileBytes(foundCsf, 16 * 1024 * 1024, csfBytes) && !csfBytes.empty()) {
                ParseCal3DBinarySkeleton(csfBytes.data(), csfBytes.size(), outBones);
            }
        }
    }
}

inline void AppendOctahedralBonePrism(
    const Vec3f& a,
    const Vec3f& b,
    std::vector<Vec3f>& verts,
    std::vector<MeshTriangle>& tris)
{
    MeshBuild3D::AppendOctahedralBonePrism(verts, tris, a, b, 0.10f, 1e-4f);
}

inline void ParseXmlFloatsSimple(std::string_view sv, std::vector<float>& out) {
    const char* p = sv.data();
    const char* end = p + sv.size();
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) ++p;
        if (p >= end) break;
        if (*p == '+') ++p;
        float v = 0.0f;
        auto r = std::from_chars(p, end, v);
        if (r.ec != std::errc{} || r.ptr == p) {
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ',') ++p;
            continue;
        }
        p = r.ptr;
        if (std::isfinite(v)) out.push_back(v);
    }
}

inline std::string_view ExtractXmlAttr(std::string_view tag, std::string_view attrName) { // names in any case
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tag, attrName, v, true, /*ignoreCase=*/true) ? v : std::string_view();
}

} // namespace CalCryDetail


namespace DetailDccSculpt {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdU16BE;
using ::Explorer2::ByteIO::RdU32BE;
using ::Explorer2::ByteIO::RdU64BE;
using ::Explorer2::ByteIO::RdF32BE;
using ::Explorer2::ByteIO::RdF64BE;

inline bool IsValidCoord3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::fabs(x) < 1e8f && std::fabs(y) < 1e8f && std::fabs(z) < 1e8f;
}

// Read Modo / LWO2 variable-length big-endian index (VX):
//   if byte[0] != 0xFF -> 2-byte uint16BE (0 .. 65279)
//   if byte[0] == 0xFF -> 4-byte uint32BE & 0x00FFFFFF (0 .. 16777215)
inline bool ReadModoVx(const uint8_t* data, size_t endPos, size_t& pos, uint32_t& outIdx) {
    if (pos + 2 > endPos) return false;
    if (data[pos] != 0xFFu) {
        outIdx = RdU16BE(data + pos);
        pos += 2;
        return true;
    }
    if (pos + 4 > endPos) return false;
    outIdx = RdU32BE(data + pos) & 0x00FFFFFFu;
    pos += 4;
    return true;
}

inline bool TryFindAndInflateZlib(const uint8_t* data, size_t size, size_t maxScanOff, std::vector<uint8_t>& out) {
    out.clear();
    if (!data || size < 8) return false;
    size_t limit = std::min(size - 6, maxScanOff);
    for (size_t off = 0; off <= limit; ++off) {
        uint8_t b0 = data[off];
        uint8_t b1 = data[off + 1];
        if (b0 == 0x78u && (b1 == 0x01u || b1 == 0x5Eu || b1 == 0x9Cu || b1 == 0xDAu)) {
            if ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u) {
                std::vector<uint8_t> tmp;
                if (ZipDeflate::InflateZlib(data + off, size - off, tmp, 128 * 1024 * 1024) && tmp.size() >= 16) {
                    out = std::move(tmp);
                    return true;
                }
            }
        }
    }
    return false;
}


} // namespace DetailDccSculpt


namespace SplatVdbDetail {

// little-endian loads (ByteIO.h)
inline uint16_t RdU16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t RdU32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline int32_t RdI32(const uint8_t* p) { return ::Explorer2::ByteIO::RdI32LE(p); }
inline uint64_t RdU64(const uint8_t* p) { return ::Explorer2::ByteIO::RdU64LE(p); }
inline int64_t RdI64(const uint8_t* p) { return ::Explorer2::ByteIO::RdI64LE(p); }
inline float RdF32(const uint8_t* p) { return ::Explorer2::ByteIO::RdF32LE(p); }
inline double RdF64(const uint8_t* p) { return ::Explorer2::ByteIO::RdF64LE(p); }

inline float HalfToF32Splat(uint16_t h) { return ::Explorer2::ByteIO::HalfToFloat(h); }

struct GaussianPrim {
    Vec3f pos = { 0, 0, 0 };
    Vec3f scale = { 0.05f, 0.05f, 0.05f };
    float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
    uint8_t alpha = 255;
};

// Emits oriented 3D anisotropic Gaussian octahedra (6 verts, 8 tris per Gaussian)
inline void EmitGaussianEllipsoids(const std::vector<GaussianPrim>& splats, LoadedMesh3D& mesh) {
    if (splats.empty()) return;
    constexpr size_t kMaxEllipsoids = 35000;
    size_t step = std::max<size_t>(1, splats.size() / kMaxEllipsoids);

    static const Vec3f kOct[6] = {
        { 1.0f, 0.0f, 0.0f }, {-1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f,-1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f,-1.0f }
    };
    static const uint32_t kOctTris[8][3] = {
        {0,2,4}, {4,2,1}, {1,2,5}, {5,2,0},
        {4,3,0}, {1,3,4}, {5,3,1}, {0,3,5}
    };

    mesh.vertices.reserve(std::min(splats.size() / step, kMaxEllipsoids) * 6);
    mesh.tris.reserve(std::min(splats.size() / step, kMaxEllipsoids) * 8);

    for (size_t i = 0; i < splats.size() && mesh.tris.size() < kMaxEllipsoids * 8; i += step) {
        const GaussianPrim& g = splats[i];
        if (g.alpha <= 2) continue;

        float w = g.qw, x = g.qx, y = g.qy, z = g.qz;
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        if (n > 1e-6f) { w /= n; x /= n; y /= n; z /= n; }
        else { w = 1.0f; x = y = z = 0.0f; }

        // 3x3 rotation matrix columns multiplied by anisotropic Gaussian sigma scales (2.0 sigma visual hull)
        float sx = std::clamp(g.scale.x * 2.0f, 1e-4f, 1e5f);
        float sy = std::clamp(g.scale.y * 2.0f, 1e-4f, 1e5f);
        float sz = std::clamp(g.scale.z * 2.0f, 1e-4f, 1e5f);

        Vec3f ax = { (1.0f - 2.0f * (y * y + z * z)) * sx, (2.0f * (x * y + w * z)) * sx, (2.0f * (x * z - w * y)) * sx };
        Vec3f ay = { (2.0f * (x * y - w * z)) * sy, (1.0f - 2.0f * (x * x + z * z)) * sy, (2.0f * (y * z + w * x)) * sy };
        Vec3f az = { (2.0f * (x * z + w * y)) * sz, (2.0f * (y * z - w * x)) * sz, (1.0f - 2.0f * (x * x + y * y)) * sz };

        uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
        for (int k = 0; k < 6; ++k) {
            const Vec3f& u = kOct[k];
            mesh.vertices.push_back({
                g.pos.x + ax.x * u.x + ay.x * u.y + az.x * u.z,
                g.pos.y + ax.y * u.x + ay.y * u.y + az.y * u.z,
                g.pos.z + ax.z * u.x + ay.z * u.y + az.z * u.z
            });
        }
        for (int t = 0; t < 8; ++t) {
            mesh.tris.push_back({ vb + kOctTris[t][0], vb + kOctTris[t][1], vb + kOctTris[t][2], {} });
        }
    }
}

} // namespace SplatVdbDetail

namespace UsdAbcDetail {

using DetailScad::Mat4;

inline float HalfToFloat17(uint16_t h) {
    float f = ::Explorer2::ByteIO::HalfToFloat(h);
    return std::isfinite(f) ? f : 0.0f; // this reader maps Inf/NaN halves to 0
}

inline Mat4 QuatToMat4(float w, float x, float y, float z) {
    float n = std::sqrt(w * w + x * x + y * y + z * z);
    if (n < 1e-7f) return Mat4::Identity();
    w /= n; x /= n; y /= n; z /= n;
    Mat4 r;
    r.m[0]  = 1.0f - 2.0f * (y * y + z * z);
    r.m[1]  = 2.0f * (x * y - w * z);
    r.m[2]  = 2.0f * (x * z + w * y);
    r.m[4]  = 2.0f * (x * y + w * z);
    r.m[5]  = 1.0f - 2.0f * (x * x + z * z);
    r.m[6]  = 2.0f * (y * z - w * x);
    r.m[8]  = 2.0f * (x * z - w * y);
    r.m[9]  = 2.0f * (y * z + w * x);
    r.m[10] = 1.0f - 2.0f * (x * x + y * y);
    return r;
}

// Converts USD GfMatrix4d / Alembic M44d row-vector matrix (translation at [12..14]) to column-vector Mat4
inline Mat4 RowVecMatrix16ToMat4(const double d[16]) {
    Mat4 r;
    // Detect whether translation sits in row 3 (d[12..14], standard USD/Alembic row-vector) or col 3 (d[3,7,11])
    bool colVec = (std::fabs(d[12]) < 1e-9 && std::fabs(d[13]) < 1e-9 && std::fabs(d[14]) < 1e-9) &&
                  (std::fabs(d[3]) > 1e-7 || std::fabs(d[7]) > 1e-7 || std::fabs(d[11]) > 1e-7);
    if (colVec) {
        for (int i = 0; i < 16; ++i) r.m[i] = std::isfinite(d[i]) ? static_cast<float>(d[i]) : 0.0f;
    } else {
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                double v = d[col * 4 + row];
                r.m[row * 4 + col] = std::isfinite(v) ? static_cast<float>(v) : 0.0f;
            }
        }
    }
    r.m[12] = 0.0f; r.m[13] = 0.0f; r.m[14] = 0.0f; r.m[15] = 1.0f;
    return r;
}


} // namespace UsdAbcDetail

namespace DwgDxfDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdI16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdI32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF64LE;
using ::Explorer2::ByteIO::RdU16BE;

inline Vec3f CadZUpToYUp(double x, double y, double z) {
    return { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
}

inline bool ValidCadPt(double x, double y, double z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::fabs(x) < 1e9 && std::fabs(y) < 1e9 && std::fabs(z) < 1e9;
}

} // namespace DwgDxfDetail

// ============================================================================
// Khronos EXT_meshopt_compression (v0) & KHR_meshopt_compression (v1) Decoder
// ============================================================================
namespace MeshoptCodec {

inline uint8_t  Unzigzag8(uint8_t v)   { return static_cast<uint8_t>(static_cast<uint8_t>(0u - (v & 1u)) ^ (v >> 1)); }
inline uint16_t Unzigzag16(uint16_t v) { return static_cast<uint16_t>(static_cast<uint16_t>(0u - (v & 1u)) ^ (v >> 1)); }
inline uint32_t Unzigzag32(uint32_t v) { return (0u - (v & 1u)) ^ (v >> 1); }
inline uint32_t Rotate32(uint32_t v, int r) { return (v << r) | (v >> ((32 - r) & 31)); }

// Exact scalar decodeBytesGroup from meshoptimizer vertexcodec.cpp (handles 0/1/2/4/8-bit groups + sentinel escape bytes!)
inline const uint8_t* DecodeBytesGroup(const uint8_t* data, uint8_t* out, int bits) {
    if (bits == 0) {
        std::memset(out, 0, 16);
        return data;
    }
    if (bits == 8) {
        std::memcpy(out, data, 16);
        return data + 16;
    }
    if (bits == 1) {
        const uint8_t* dataVar = data + 2;
        for (int b = 0; b < 2; ++b) {
            uint8_t byte = *data++;
            byte = static_cast<uint8_t>(((static_cast<uint64_t>(byte) * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32);
            for (int k = 0; k < 8; ++k) {
                uint8_t enc = byte >> 7;
                byte = static_cast<uint8_t>(byte << 1);
                uint8_t encv = *dataVar;
                *out++ = (enc == 1u) ? encv : enc;
                dataVar += (enc == 1u) ? 1 : 0;
            }
        }
        return dataVar;
    }
    if (bits == 2) {
        const uint8_t* dataVar = data + 4;
        for (int b = 0; b < 4; ++b) {
            uint8_t byte = *data++;
            for (int k = 0; k < 4; ++k) {
                uint8_t enc = byte >> 6;
                byte = static_cast<uint8_t>(byte << 2);
                uint8_t encv = *dataVar;
                *out++ = (enc == 3u) ? encv : enc;
                dataVar += (enc == 3u) ? 1 : 0;
            }
        }
        return dataVar;
    }
    // bits == 4
    const uint8_t* dataVar = data + 8;
    for (int b = 0; b < 8; ++b) {
        uint8_t byte = *data++;
        for (int k = 0; k < 2; ++k) {
            uint8_t enc = byte >> 4;
            byte = static_cast<uint8_t>(byte << 4);
            uint8_t encv = *dataVar;
            *out++ = (enc == 15u) ? encv : enc;
            dataVar += (enc == 15u) ? 1 : 0;
        }
    }
    return dataVar;
}

inline const uint8_t* DecodeBytesStream(
    const uint8_t* data,
    const uint8_t* dataEnd,
    uint8_t* buffer,
    size_t bufferSizeAligned,
    const int* bitsTable)
{
    constexpr size_t kByteGroupSize = 16;
    constexpr size_t kByteGroupDecodeLimit = 24;
    size_t groups = bufferSizeAligned / kByteGroupSize;
    size_t headerSize = (groups + 3u) / 4u;
    if (static_cast<size_t>(dataEnd - data) < headerSize) return nullptr;

    const uint8_t* header = data;
    data += headerSize;
    for (size_t g = 0; g < groups; ++g) {
        if (static_cast<size_t>(dataEnd - data) < kByteGroupDecodeLimit) return nullptr;
        int bitsk = (header[g >> 2] >> ((g & 3u) * 2u)) & 3;
        data = DecodeBytesGroup(data, buffer + g * kByteGroupSize, bitsTable[bitsk]);
    }
    return data;
}

template <typename T, bool XorMode>
inline void DecodeDeltas4(
    const uint8_t* buffer,
    size_t laneStride,
    uint8_t* dst,
    size_t vertexCount,
    size_t vertexSize,
    const uint8_t* lastVertex,
    int rot)
{
    for (size_t k = 0; k < 4u; k += sizeof(T)) {
        size_t vertexOffset = k;
        T p = lastVertex[0];
        for (size_t j = 1; j < sizeof(T); ++j) {
            p = static_cast<T>(p | (static_cast<T>(lastVertex[j]) << (8u * j)));
        }
        for (size_t i = 0; i < vertexCount; ++i) {
            T v = buffer[i];
            for (size_t j = 1; j < sizeof(T); ++j) {
                v = static_cast<T>(v | (static_cast<T>(buffer[i + laneStride * j]) << (8u * j)));
            }
            if constexpr (XorMode) {
                v = static_cast<T>(Rotate32(static_cast<uint32_t>(v), rot) ^ p);
            } else if constexpr (sizeof(T) == 1) {
                v = static_cast<T>(Unzigzag8(static_cast<uint8_t>(v)) + p);
            } else if constexpr (sizeof(T) == 2) {
                v = static_cast<T>(Unzigzag16(static_cast<uint16_t>(v)) + p);
            } else {
                v = static_cast<T>(Unzigzag32(static_cast<uint32_t>(v)) + p);
            }
            for (size_t j = 0; j < sizeof(T); ++j) {
                dst[vertexOffset + j] = static_cast<uint8_t>(v >> (j * 8u));
            }
            p = v;
            vertexOffset += vertexSize;
        }
        buffer += laneStride * sizeof(T);
        lastVertex += sizeof(T);
    }
}

inline bool DecodeMeshoptAttributes(
    const uint8_t* src,
    size_t srcSize,
    size_t count,
    size_t stride,
    std::vector<uint8_t>& dst)
{
    if (!src || srcSize < 1 || count == 0 || stride == 0 || stride > 256 || (stride % 4u) != 0u) return false;
    if ((src[0] & 0xF0u) != 0xA0u) return false;
    int version = src[0] & 0x0Fu;
    if (version > 1) return false;

    const size_t tailSize = stride + (version == 0 ? 0u : (stride / 4u));
    const size_t tailSizeMin = (version == 0) ? 32u : 24u;
    const size_t tailSizePad = std::max(tailSize, tailSizeMin);
    if (srcSize - 1u < tailSizePad) return false;

    const uint8_t* data = src + 1;
    const uint8_t* dataEnd = src + srcSize;
    const uint8_t* tail = dataEnd - tailSize;

    uint8_t lastVertex[256] = {};
    std::memcpy(lastVertex, tail, stride);
    const uint8_t* channels = (version == 0) ? nullptr : (tail + stride);

    size_t vertexBlockSize = (8192u / stride) & ~size_t(15);
    vertexBlockSize = std::clamp<size_t>(vertexBlockSize, 16u, 256u);

    static constexpr int kBitsV0[4] = { 0, 2, 4, 8 };
    static constexpr int kBitsV1[5] = { 0, 1, 2, 4, 8 };

    dst.assign(count * stride, 0u);
    uint8_t buffer[256 * 4] = {};

    size_t vertexOffset = 0;
    while (vertexOffset < count) {
        size_t blockSize = (vertexOffset + vertexBlockSize <= count) ? vertexBlockSize : (count - vertexOffset);
        size_t blockSizeAligned = (blockSize + 15u) & ~size_t(15);
        size_t controlSize = (version == 0) ? 0u : (stride / 4u);
        if (static_cast<size_t>(dataEnd - data) < controlSize) return false;
        const uint8_t* control = data;
        data += controlSize;

        uint8_t* blockDst = dst.data() + vertexOffset * stride;

        for (size_t k = 0; k < stride; k += 4u) {
            uint8_t ctrlByte = (version == 0) ? 0u : control[k / 4u];
            for (size_t j = 0; j < 4u; ++j) {
                int ctrl = (ctrlByte >> (j * 2u)) & 3;
                uint8_t* lane = buffer + j * blockSizeAligned;
                if (ctrl == 3) {
                    if (static_cast<size_t>(dataEnd - data) < blockSize) return false;
                    std::memcpy(lane, data, blockSize);
                    data += blockSize;
                } else if (ctrl == 2) {
                    std::memset(lane, 0, blockSize);
                } else {
                    data = DecodeBytesStream(data, dataEnd, lane, blockSizeAligned, (version == 0) ? kBitsV0 : (kBitsV1 + ctrl));
                    if (!data) return false;
                }
            }

            int ch = (version == 0) ? 0 : channels[k / 4u];
            switch (ch & 3) {
                case 0:
                    DecodeDeltas4<uint8_t, false>(buffer, blockSizeAligned, blockDst + k, blockSize, stride, lastVertex + k, 0);
                    break;
                case 1:
                    DecodeDeltas4<uint16_t, false>(buffer, blockSizeAligned, blockDst + k, blockSize, stride, lastVertex + k, 0);
                    break;
                case 2:
                    DecodeDeltas4<uint32_t, true>(buffer, blockSizeAligned, blockDst + k, blockSize, stride, lastVertex + k, (32 - (ch >> 4)) & 31);
                    break;
                default:
                    return false;
            }
        }
        std::memcpy(lastVertex, blockDst + (blockSize - 1u) * stride, stride);
        vertexOffset += blockSize;
    }

    return static_cast<size_t>(dataEnd - data) == tailSizePad;
}

template <typename T>
inline void ApplyMeshoptFilterOctahedral(uint8_t* raw, size_t count) {
    const float maxVal = static_cast<float>((1 << (sizeof(T) * 8 - 1)) - 1);
    for (size_t i = 0; i < count; ++i) {
        T elem[4];
        std::memcpy(elem, raw + i * 4u * sizeof(T), sizeof(elem));
        float x = static_cast<float>(elem[0]);
        float y = static_cast<float>(elem[1]);
        float z = static_cast<float>(elem[2]) - std::fabs(x) - std::fabs(y);
        float t = (z >= 0.0f) ? 0.0f : z;
        x += (x >= 0.0f) ? t : -t;
        y += (y >= 0.0f) ? t : -t;
        float l = std::sqrt(x * x + y * y + z * z);
        float s = (l > 0.0f) ? (maxVal / l) : 0.0f;
        elem[0] = static_cast<T>(static_cast<int>(x * s + (x >= 0.0f ? 0.5f : -0.5f)));
        elem[1] = static_cast<T>(static_cast<int>(y * s + (y >= 0.0f ? 0.5f : -0.5f)));
        elem[2] = static_cast<T>(static_cast<int>(z * s + (z >= 0.0f ? 0.5f : -0.5f)));
        std::memcpy(raw + i * 4u * sizeof(T), elem, sizeof(elem));
    }
}

inline void ApplyMeshoptFilterQuaternion(uint8_t* raw, size_t count) {
    const float scale = 32767.0f / std::sqrt(2.0f);
    for (size_t i = 0; i < count; ++i) {
        int16_t elem[4];
        std::memcpy(elem, raw + i * 8u, 8);
        int sf = elem[3] | 3;
        float s = static_cast<float>(sf);
        float x = static_cast<float>(elem[0]);
        float y = static_cast<float>(elem[1]);
        float z = static_cast<float>(elem[2]);
        float ww = s * s * 2.0f - x * x - y * y - z * z;
        float w = std::sqrt(ww >= 0.0f ? ww : 0.0f);
        float ss = (s != 0.0f) ? (scale / s) : 0.0f;
        int xf = static_cast<int>(x * ss + (x >= 0.0f ? 0.5f : -0.5f));
        int yf = static_cast<int>(y * ss + (y >= 0.0f ? 0.5f : -0.5f));
        int zf = static_cast<int>(z * ss + (z >= 0.0f ? 0.5f : -0.5f));
        int wf = static_cast<int>(w * ss + 0.5f);
        int qc = elem[3] & 3;
        int16_t out[4];
        out[(qc + 1) & 3] = static_cast<int16_t>(xf);
        out[(qc + 2) & 3] = static_cast<int16_t>(yf);
        out[(qc + 3) & 3] = static_cast<int16_t>(zf);
        out[(qc + 0) & 3] = static_cast<int16_t>(wf);
        std::memcpy(raw + i * 8u, out, 8);
    }
}

inline void ApplyMeshoptFilterExponential(uint8_t* data, size_t count, size_t stride) {
    size_t totalWords = (count * stride) / 4u;
    for (size_t i = 0; i < totalWords; ++i) {
        int32_t v = 0;
        std::memcpy(&v, data + i * 4u, 4);
        int32_t exp = v >> 24;
        int32_t mant = (v << 8) >> 8;
        float f = std::ldexp(static_cast<float>(mant), exp);
        std::memcpy(data + i * 4u, &f, 4);
    }
}

template <typename ST, typename T>
inline void ApplyMeshoptFilterColor(uint8_t* raw, size_t count) {
    const float maxVal = static_cast<float>((1 << (sizeof(T) * 8)) - 1);
    for (size_t i = 0; i < count; ++i) {
        T elem[4];
        std::memcpy(elem, raw + i * 4u * sizeof(T), sizeof(elem));
        int as = elem[3];
        as |= as >> 1; as |= as >> 2; as |= as >> 4; as |= as >> 8;
        int y = elem[0], co = static_cast<ST>(elem[1]), cg = static_cast<ST>(elem[2]);
        int r = y + co - cg;
        int g = y + cg;
        int b = y - co - cg;
        int a = ((static_cast<int>(elem[3]) << 1) & as) | (static_cast<int>(elem[3]) & 1);
        float ss = (as > 0) ? (maxVal / static_cast<float>(as)) : 0.0f;
        elem[0] = static_cast<T>(static_cast<int>(static_cast<float>(r) * ss + 0.5f));
        elem[1] = static_cast<T>(static_cast<int>(static_cast<float>(g) * ss + 0.5f));
        elem[2] = static_cast<T>(static_cast<int>(static_cast<float>(b) * ss + 0.5f));
        elem[3] = static_cast<T>(static_cast<int>(static_cast<float>(a) * ss + 0.5f));
        std::memcpy(raw + i * 4u * sizeof(T), elem, sizeof(elem));
    }
}

inline bool ReadMeshoptVByte(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    out = 0;
    uint32_t shift = 0;
    for (int i = 0; i < 5; ++i) {
        if (p >= end) return false;
        uint8_t b = *p++;
        out |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u) return true;
        shift += 7;
    }
    return false;
}

inline bool DecodeMeshoptTriangles(
    const uint8_t* src,
    size_t srcSize,
    size_t indexCount,
    size_t indexSize,
    std::vector<uint8_t>& dst)
{
    if (!src || (indexCount % 3u) != 0u || (indexSize != 2u && indexSize != 4u)) return false;
    if (srcSize < 1u + (indexCount / 3u) + 16u) return false;
    if ((src[0] & 0xF0u) != 0xE0u) return false;
    uint8_t version = src[0] & 0x0Fu;
    if (version > 1u) return false;

    size_t triCount = indexCount / 3u;
    const uint8_t* code = src + 1;
    const uint8_t* data = code + triCount;
    const uint8_t* dataSafeEnd = src + srcSize - 16u;
    const uint8_t* streamEnd   = src + srcSize;
    const uint8_t* codeauxTable = dataSafeEnd;

    int fecmax = (version >= 1u) ? 13 : 15;
    if (version == 0u && (codeauxTable[14] != 0u || codeauxTable[15] != 0u)) return false;

    uint32_t edgefifo[16][2] = {};
    uint32_t vertexfifo[16] = {};
    size_t edgefifoOffset = 0;
    size_t vertexfifoOffset = 0;
    uint32_t next = 0;
    uint32_t last = 0;

    auto pushEdgeFifo = [&](uint32_t a, uint32_t b) {
        edgefifo[edgefifoOffset][0] = a;
        edgefifo[edgefifoOffset][1] = b;
        edgefifoOffset = (edgefifoOffset + 1u) & 15u;
    };
    auto pushVertexFifo = [&](uint32_t v, bool cond = true) {
        vertexfifo[vertexfifoOffset] = v;
        if (cond) vertexfifoOffset = (vertexfifoOffset + 1u) & 15u;
    };
    auto decodeIndexDelta = [&](uint32_t& outIdx) -> bool {
        uint32_t v = 0;
        if (!ReadMeshoptVByte(data, streamEnd, v)) return false;
        uint32_t d = Unzigzag32(v);
        last += d;
        outIdx = last;
        return true;
    };

    dst.resize(indexCount * indexSize);
    auto writeIdx = [&](size_t slot, uint32_t val) {
        if (indexSize == 2u) {
            uint16_t v16 = static_cast<uint16_t>(val);
            std::memcpy(dst.data() + slot * 2u, &v16, 2);
        } else {
            std::memcpy(dst.data() + slot * 4u, &val, 4);
        }
    };

    for (size_t i = 0; i < triCount; ++i) {
        uint8_t codetri = code[i];
        if (codetri < 0xF0u) {
            int fe = codetri >> 4;
            uint32_t a = edgefifo[(edgefifoOffset - 1u - static_cast<size_t>(fe)) & 15u][0];
            uint32_t b = edgefifo[(edgefifoOffset - 1u - static_cast<size_t>(fe)) & 15u][1];
            int fec = codetri & 15;
            uint32_t c = 0;

            if (fec < fecmax) {
                c = (fec == 0) ? next : vertexfifo[(vertexfifoOffset - 1u - static_cast<size_t>(fec)) & 15u];
                bool fec0 = (fec == 0);
                next += fec0 ? 1u : 0u;
                pushVertexFifo(c, fec0);
            } else {
                if (data > dataSafeEnd) return false;
                if (fec != 15) {
                    c = last + static_cast<uint32_t>(fec * 2 - 27);
                    last = c;
                } else {
                    if (!decodeIndexDelta(c)) return false;
                }
                pushVertexFifo(c);
            }
            pushEdgeFifo(c, b);
            pushEdgeFifo(a, c);
            writeIdx(i * 3u + 0, a);
            writeIdx(i * 3u + 1, b);
            writeIdx(i * 3u + 2, c);
        } else {
            if (codetri < 0xFEu) {
                uint8_t aux = codeauxTable[codetri & 15u];
                int feb = aux >> 4;
                int fec = aux & 15;
                uint32_t a = next++;

                uint32_t b = (feb == 0) ? next : vertexfifo[(vertexfifoOffset - static_cast<size_t>(feb)) & 15u];
                bool feb0 = (feb == 0);
                next += feb0 ? 1u : 0u;

                uint32_t c = (fec == 0) ? next : vertexfifo[(vertexfifoOffset - static_cast<size_t>(fec)) & 15u];
                bool fec0 = (fec == 0);
                next += fec0 ? 1u : 0u;

                pushVertexFifo(a);
                pushVertexFifo(b, feb0);
                pushVertexFifo(c, fec0);
                pushEdgeFifo(b, a);
                pushEdgeFifo(c, b);
                pushEdgeFifo(a, c);
                writeIdx(i * 3u + 0, a);
                writeIdx(i * 3u + 1, b);
                writeIdx(i * 3u + 2, c);
            } else {
                if (data > dataSafeEnd) return false;
                uint8_t extra = *data++;
                int fea = (codetri == 0xFEu) ? 0 : 15;
                int feb = extra >> 4;
                int fec = extra & 15;
                if (extra == 0u) next = 0u; // v1 reset code!

                uint32_t a = (fea == 0) ? next++ : 0u;
                uint32_t b = (feb == 0) ? next++ : vertexfifo[(vertexfifoOffset - static_cast<size_t>(feb)) & 15u];
                uint32_t c = (fec == 0) ? next++ : vertexfifo[(vertexfifoOffset - static_cast<size_t>(fec)) & 15u];

                if (fea == 15 && !decodeIndexDelta(a)) return false;
                if (feb == 15 && !decodeIndexDelta(b)) return false;
                if (fec == 15 && !decodeIndexDelta(c)) return false;

                pushVertexFifo(a);
                pushVertexFifo(b, feb == 0 || feb == 15);
                pushVertexFifo(c, fec == 0 || fec == 15);
                pushEdgeFifo(b, a);
                pushEdgeFifo(c, b);
                pushEdgeFifo(a, c);
                writeIdx(i * 3u + 0, a);
                writeIdx(i * 3u + 1, b);
                writeIdx(i * 3u + 2, c);
            }
        }
    }
    return data == dataSafeEnd;
}

// Meshopt INDICES (0xD0 / 0xD1): dual-baseline VByte zigzag delta index sequence (meshopt_decodeIndexSequence)
inline bool DecodeMeshoptIndices(
    const uint8_t* src,
    size_t srcSize,
    size_t indexCount,
    size_t indexSize,
    std::vector<uint8_t>& dst)
{
    if (!src || (indexSize != 2u && indexSize != 4u)) return false;
    if (srcSize < 1u + indexCount + 4u) return false;
    if ((src[0] & 0xF0u) != 0xD0u) return false;
    uint8_t version = src[0] & 0x0Fu;
    if (version > 1u) return false;

    const uint8_t* data = src + 1;
    const uint8_t* dataSafeEnd = src + srcSize - 4u;
    const uint8_t* streamEnd   = src + srcSize;
    uint32_t last[2] = { 0u, 0u };

    dst.resize(indexCount * indexSize);
    for (size_t i = 0; i < indexCount; ++i) {
        if (data >= dataSafeEnd) return false;
        uint32_t v = 0;
        if (!ReadMeshoptVByte(data, streamEnd, v)) return false;
        uint32_t current = v & 1u;
        v >>= 1;
        uint32_t d = Unzigzag32(v);
        uint32_t index = last[current] + d;
        last[current] = index;

        if (indexSize == 2u) {
            uint16_t v16 = static_cast<uint16_t>(index);
            std::memcpy(dst.data() + i * 2u, &v16, 2);
        } else {
            std::memcpy(dst.data() + i * 4u, &index, 4);
        }
    }
    return data == dataSafeEnd;
}

} // namespace MeshoptCodec

E2_DRACO_NOINLINE inline bool DecodeMeshoptBufferView(
    const uint8_t* src,
    size_t srcSize,
    size_t count,
    size_t byteStride,
    std::string_view mode,
    std::string_view filter,
    std::vector<uint8_t>& outDecoded)
{
    outDecoded.clear();
    if (!src || srcSize == 0 || count == 0 || byteStride == 0) return false;
    if (count > 16000000u || byteStride > 256u || count * byteStride > 256ULL * 1024ULL * 1024ULL) return false;

    if (mode == "ATTRIBUTES") {
        if (!MeshoptCodec::DecodeMeshoptAttributes(src, srcSize, count, byteStride, outDecoded)) return false;
        if (filter.empty() || filter == "NONE") {
            return true;
        }
        if (filter == "OCTAHEDRAL") {
            if (byteStride == 4u) MeshoptCodec::ApplyMeshoptFilterOctahedral<int8_t>(outDecoded.data(), count);
            else if (byteStride == 8u) MeshoptCodec::ApplyMeshoptFilterOctahedral<int16_t>(outDecoded.data(), count);
            else return false;
            return true;
        }
        if (filter == "QUATERNION") {
            if (byteStride != 8u) return false;
            MeshoptCodec::ApplyMeshoptFilterQuaternion(outDecoded.data(), count);
            return true;
        }
        if (filter == "EXPONENTIAL") {
            if ((byteStride % 4u) != 0u) return false;
            MeshoptCodec::ApplyMeshoptFilterExponential(outDecoded.data(), count, byteStride);
            return true;
        }
        if (filter == "COLOR") {
            if (byteStride == 4u) MeshoptCodec::ApplyMeshoptFilterColor<int8_t, uint8_t>(outDecoded.data(), count);
            else if (byteStride == 8u) MeshoptCodec::ApplyMeshoptFilterColor<int16_t, uint16_t>(outDecoded.data(), count);
            else return false;
            return true;
        }
        return false;
    }
    if (mode == "TRIANGLES") {
        return MeshoptCodec::DecodeMeshoptTriangles(src, srcSize, count, byteStride, outDecoded);
    }
    if (mode == "INDICES") {
        return MeshoptCodec::DecodeMeshoptIndices(src, srcSize, count, byteStride, outDecoded);
    }
    return false;
}


namespace Id3SkelDetail {

inline int32_t R3I32(const uint8_t* p) { int32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline uint16_t R3U16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline int16_t R3I16(const uint8_t* p) { int16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline float R3F32(const uint8_t* p) { float v = 0.0f; std::memcpy(&v, p, 4); return v; }

inline Vec3f Id3ZUpToViewport(float x, float y, float z) {
    return { x, z, -y };
}

struct Id3Bone3x4 {
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };

    void Transform(float ox, float oy, float oz, float& wx, float& wy, float& wz) const {
        wx = m[0][0] * ox + m[0][1] * oy + m[0][2] * oz + m[0][3];
        wy = m[1][0] * ox + m[1][1] * oy + m[1][2] * oz + m[1][3];
        wz = m[2][0] * ox + m[2][1] * oy + m[2][2] * oz + m[2][3];
    }
};

// Reconstruct world-space 3x4 bone matrices from RTCW .mds / WolfET .mdx compressed bone frames
inline bool DecodeWolfCompressedBones(
    const uint8_t* boneInfoPtr,
    size_t boneInfoStride, // 84 for MDS, 80 for MDX
    const uint8_t* framePtr,
    size_t frameAvailableBytes,
    int32_t numBones,
    std::vector<Id3Bone3x4>& outBones,
    std::vector<int32_t>& outParents)
{
    if (numBones <= 0 || numBones > 1024) return false;
    if (frameAvailableBytes < 52ULL + static_cast<size_t>(numBones) * 24ULL) return false;

    outBones.assign(static_cast<size_t>(numBones), Id3Bone3x4{});
    outParents.assign(static_cast<size_t>(numBones), -1);

    // Frame header: bounds[2](24) + localOrigin(12) + radius(4) + parentOffset(12) = 52 bytes
    const float rootOx = R3F32(framePtr + 40);
    const float rootOy = R3F32(framePtr + 44);
    const float rootOz = R3F32(framePtr + 48);

    constexpr float kShortToRad = 6.28318530718f / 65536.0f;

    for (int32_t b = 0; b < numBones; ++b) {
        const uint8_t* bi = boneInfoPtr + static_cast<size_t>(b) * boneInfoStride;
        int32_t parent = R3I32(bi + 64);
        float parentDist = R3F32(bi + 72);
        if (!std::isfinite(parentDist)) parentDist = 0.0f;
        outParents[static_cast<size_t>(b)] = parent;

        const uint8_t* cb = framePtr + 52 + static_cast<size_t>(b) * 24;
        const float pitch = static_cast<float>(R3I16(cb + 0)) * kShortToRad;
        const float yaw   = static_cast<float>(R3I16(cb + 2)) * kShortToRad;
        const float roll  = static_cast<float>(R3I16(cb + 4)) * kShortToRad;
        const float oPitch = static_cast<float>(R3I16(cb + 8)) * kShortToRad;
        const float oYaw   = static_cast<float>(R3I16(cb + 10)) * kShortToRad;

        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw),   sy = std::sin(yaw);
        float cr = std::cos(roll),  sr = std::sin(roll);

        Id3Bone3x4& bm = outBones[static_cast<size_t>(b)];
        // Quake / RTCW AnglesToAxis(pitch, yaw, roll): forward, right->left, up
        bm.m[0][0] = cp * cy;
        bm.m[1][0] = cp * sy;
        bm.m[2][0] = -sp;

        bm.m[0][1] = -( -1.0f * sr * sp * cy + -1.0f * cr * -sy );
        bm.m[1][1] = -( -1.0f * sr * sp * sy + -1.0f * cr * cy );
        bm.m[2][1] = -( -1.0f * sr * cp );

        bm.m[0][2] = ( cr * sp * cy + -sr * -sy );
        bm.m[1][2] = ( cr * sp * sy + -sr * cy );
        bm.m[2][2] = cr * cp;

        if (parent >= 0 && parent < b) {
            float cop = std::cos(oPitch), sop = std::sin(oPitch);
            float coy = std::cos(oYaw),   soy = std::sin(oYaw);
            bm.m[0][3] = outBones[static_cast<size_t>(parent)].m[0][3] + cop * coy * parentDist;
            bm.m[1][3] = outBones[static_cast<size_t>(parent)].m[1][3] + cop * soy * parentDist;
            bm.m[2][3] = outBones[static_cast<size_t>(parent)].m[2][3] - sop * parentDist;
        } else {
            bm.m[0][3] = rootOx;
            bm.m[1][3] = rootOy;
            bm.m[2][3] = rootOz;
        }
    }
    return true;
}

inline void AppendOctahedralBonePrism(const Vec3f& p0, const Vec3f& p1, LoadedMesh3D& mesh) {
    MeshBuild3D::AppendOctahedralBonePrism(mesh, p0, p1, 0.12f, 1e-4f);
}

inline bool ParseWolfMdxBonesOnly(
    const uint8_t* data,
    size_t size,
    std::vector<Id3Bone3x4>& outBones,
    std::vector<int32_t>& outParents,
    size_t& outFrames)
{
    if (!data || size < 96 || std::memcmp(data, "MDXW", 4) != 0) return false;
    int32_t ver = R3I32(data + 4);
    if (ver != 2) return false;
    int32_t numFrames = R3I32(data + 72);
    int32_t numBones  = R3I32(data + 76);
    int32_t ofsFrames = R3I32(data + 80);
    int32_t ofsBones  = R3I32(data + 84);
    if (numFrames <= 0 || numBones <= 0 || numBones > 1024 || ofsFrames < 88 || ofsBones < 88) return false;
    if (static_cast<uint64_t>(ofsBones) + static_cast<uint64_t>(numBones) * 80ULL > size) return false;
    if (static_cast<uint64_t>(ofsFrames) + 52ULL + static_cast<uint64_t>(numBones) * 24ULL > size) return false;
    outFrames = static_cast<size_t>(numFrames);
    return DecodeWolfCompressedBones(
        data + ofsBones, 80,
        data + ofsFrames, size - static_cast<size_t>(ofsFrames),
        numBones, outBones, outParents);
}

inline bool ParseWolfMdmWithBones(
    const uint8_t* data,
    size_t size,
    const std::vector<Id3Bone3x4>* optBones,
    LoadedMesh3D& mesh)
{
    if (!data || size < 100 || std::memcmp(data, "MDMW", 4) != 0) return false;
    if (R3I32(data + 4) != 3) return false;
    int32_t numSurfaces = R3I32(data + 80);
    int32_t ofsSurfaces = R3I32(data + 84);
    if (numSurfaces <= 0 || numSurfaces > 1024 || ofsSurfaces < 96 || static_cast<size_t>(ofsSurfaces) >= size) {
        return false;
    }

    size_t sPos = static_cast<size_t>(ofsSurfaces);
    for (int32_t s = 0; s < numSurfaces; ++s) {
        if (sPos + 176 > size) return false;
        int32_t numVerts = R3I32(data + sPos + 144);
        int32_t ofsVerts = R3I32(data + sPos + 148);
        int32_t numTris  = R3I32(data + sPos + 152);
        int32_t ofsTris  = R3I32(data + sPos + 156);
        int32_t ofsEnd   = R3I32(data + sPos + 172);
        if (ofsEnd <= 0 || sPos + static_cast<size_t>(ofsEnd) > size) return false;
        const size_t sEnd = sPos + static_cast<size_t>(ofsEnd);
        if (numVerts <= 0 || numVerts > 500000 || numTris <= 0 || numTris > 1000000 || ofsVerts < 176 || ofsTris < 176) {
            return false;
        }
        if (sPos + static_cast<uint64_t>(ofsTris) + static_cast<uint64_t>(numTris) * 12ULL > sEnd) return false;

        uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        size_t vPos = sPos + static_cast<size_t>(ofsVerts);
        for (int32_t v = 0; v < numVerts; ++v) {
            if (vPos + 24 > sEnd) return false;
            int32_t nWeights = R3I32(data + vPos + 20);
            vPos += 24;
            if (nWeights <= 0 || nWeights > 32 || vPos + static_cast<size_t>(nWeights) * 20 > sEnd) return false;

            float sx = 0.0f, sy = 0.0f, sz = 0.0f, wSum = 0.0f;
            for (int32_t w = 0; w < nWeights; ++w) {
                int32_t boneIdx = R3I32(data + vPos + 0);
                float bw        = R3F32(data + vPos + 4);
                float ox        = R3F32(data + vPos + 8);
                float oy        = R3F32(data + vPos + 12);
                float oz        = R3F32(data + vPos + 16);
                vPos += 20;
                if (!std::isfinite(bw) || !std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz)) return false;

                float wx = ox, wy = oy, wz = oz;
                if (optBones && boneIdx >= 0 && static_cast<size_t>(boneIdx) < optBones->size()) {
                    (*optBones)[static_cast<size_t>(boneIdx)].Transform(ox, oy, oz, wx, wy, wz);
                }
                sx += wx * bw;
                sy += wy * bw;
                sz += wz * bw;
                wSum += bw;
            }
            if (wSum > 1e-6f && std::fabs(wSum - 1.0f) > 1e-3f) {
                sx /= wSum; sy /= wSum; sz /= wSum;
            }
            mesh.vertices.push_back(Id3ZUpToViewport(sx, sy, sz));
        }

        const size_t tBase = sPos + static_cast<size_t>(ofsTris);
        for (int32_t t = 0; t < numTris; ++t) {
            int32_t i0 = R3I32(data + tBase + static_cast<size_t>(t) * 12 + 0);
            int32_t i1 = R3I32(data + tBase + static_cast<size_t>(t) * 12 + 4);
            int32_t i2 = R3I32(data + tBase + static_cast<size_t>(t) * 12 + 8);
            if (i0 < 0 || i0 >= numVerts || i1 < 0 || i1 >= numVerts || i2 < 0 || i2 >= numVerts) return false;
            if (i0 == i1 || i1 == i2 || i2 == i0) continue;
            mesh.tris.push_back({
                baseV + static_cast<uint32_t>(i0),
                baseV + static_cast<uint32_t>(i2),
                baseV + static_cast<uint32_t>(i1),
                {}
            });
        }
        sPos = sEnd;
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Id3SkelDetail


namespace MmdDetail {

inline Vec3f MmdToViewport(float x, float y, float z) {
    // MMD uses left-handed Y-up (+Z away from viewer); mirror Z for Explorer2's right-handed Y-up camera
    return { x, y, -z };
}

inline bool ReadPmxVarIndex(const uint8_t* data, size_t size, size_t& pos, uint8_t idxSize, bool isVertexUnsigned, int32_t& outIdx) {
    if (pos + idxSize > size) return false;
    if (idxSize == 1) {
        if (isVertexUnsigned) outIdx = static_cast<int32_t>(data[pos]);
        else outIdx = static_cast<int32_t>(static_cast<int8_t>(data[pos]));
        pos += 1;
        return true;
    }
    if (idxSize == 2) {
        if (isVertexUnsigned) {
            uint16_t u = 0;
            std::memcpy(&u, data + pos, 2);
            outIdx = static_cast<int32_t>(u);
        } else {
            int16_t s = 0;
            std::memcpy(&s, data + pos, 2);
            outIdx = static_cast<int32_t>(s);
        }
        pos += 2;
        return true;
    }
    if (idxSize == 4) {
        int32_t s = 0;
        std::memcpy(&s, data + pos, 4);
        outIdx = s;
        pos += 4;
        return true;
    }
    return false;
}

} // namespace MmdDetail



namespace Md5Detail {

struct Quat4f {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = -1.0f;
};

inline Quat4f BuildMd5Quat(float qx, float qy, float qz) {
    float t = 1.0f - (qx * qx + qy * qy + qz * qz);
    float qw = (t <= 0.0f) ? 0.0f : -std::sqrt(t);
    return { qx, qy, qz, qw };
}

inline Quat4f QuatNormalize(const Quat4f& q) {
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-7f) {
        float inv = 1.0f / len;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }
    return { 0.0f, 0.0f, 0.0f, -1.0f };
}

using MeshBuild3D::QuatMul;

using MeshBuild3D::QuatRotate;

// Convert id Tech Z-up right-handed coords to Explorer2 Y-up viewport
inline Vec3f IdTechToViewport(const Vec3f& p) {
    return { p.x, p.z, -p.y };
}

struct Md5Joint {
    std::string name;
    int parent = -1;
    Vec3f pos = {};
    Quat4f orient = {};
};

struct Md5Lexer {
    std::string_view src;
    size_t pos = 0;

    void SkipWsAndComments() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
                continue;
            }
            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
                pos += 2;
                while (pos < src.size() && src[pos] != '\n') ++pos;
                continue;
            }
            if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '/')) ++pos;
                if (pos + 1 < src.size()) pos += 2;
                else pos = src.size();
                continue;
            }
            break;
        }
    }

    bool Next(std::string_view& outTok, bool* wasQuoted = nullptr) {
        if (wasQuoted) *wasQuoted = false;
        SkipWsAndComments();
        if (pos >= src.size()) return false;
        char c = src[pos];
        if (c == '"') {
            if (wasQuoted) *wasQuoted = true;
            size_t start = ++pos;
            while (pos < src.size() && src[pos] != '"') ++pos;
            outTok = src.substr(start, pos - start);
            if (pos < src.size() && src[pos] == '"') ++pos;
            return true;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')') {
            outTok = src.substr(pos, 1);
            ++pos;
            return true;
        }
        size_t start = pos;
        while (pos < src.size()) {
            char ch = src[pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                ch == '{' || ch == '}' || ch == '(' || ch == ')' || ch == '"') {
                break;
            }
            if (ch == '/' && pos + 1 < src.size() && (src[pos + 1] == '/' || src[pos + 1] == '*')) {
                break;
            }
            ++pos;
        }
        outTok = src.substr(start, pos - start);
        return !outTok.empty();
    }

    bool Expect(std::string_view lit) {
        std::string_view tok;
        return Next(tok) && tok == lit;
    }

    bool NextInt(int& outVal) {
        std::string_view tok;
        if (!Next(tok) || tok.empty()) return false;
        char buf[64];
        if (tok.size() >= sizeof(buf)) return false;
        std::memcpy(buf, tok.data(), tok.size());
        buf[tok.size()] = '\0';
        char* endPtr = nullptr;
        long v = std::strtol(buf, &endPtr, 10);
        if (endPtr != buf + tok.size()) return false;
        outVal = static_cast<int>(v);
        return true;
    }

    bool NextFloat(float& outVal) {
        std::string_view tok;
        if (!Next(tok) || tok.empty()) return false;
        char buf[64];
        if (tok.size() >= sizeof(buf)) return false;
        std::memcpy(buf, tok.data(), tok.size());
        buf[tok.size()] = '\0';
        char* endPtr = nullptr;
        float v = std::strtof(buf, &endPtr);
        if (endPtr != buf + tok.size() || !std::isfinite(v)) return false;
        outVal = v;
        return true;
    }

    bool NextVec3Paren(Vec3f& v) {
        return Expect("(") && NextFloat(v.x) && NextFloat(v.y) && NextFloat(v.z) && Expect(")");
    }

    bool SkipBalancedBraces() {
        int depth = 1;
        std::string_view tok;
        bool quoted = false;
        while (depth > 0 && Next(tok, &quoted)) {
            if (!quoted) {
                if (tok == "{") ++depth;
                else if (tok == "}") --depth;
            }
        }
        return depth == 0;
    }
};

inline void AppendBoneOctahedron(LoadedMesh3D& mesh, const Vec3f& a, const Vec3f& b, float fallbackRadius) {
    Vec3f d = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-5f) {
        MeshBuild3D::EmitOctahedron(mesh, a, std::max(0.05f, fallbackRadius));
        return;
    }
    MeshBuild3D::AppendOctahedralBonePrism(mesh, a, b, 0.11f, fallbackRadius * 0.35f);
}

struct Md5SubMeshRaw {
    struct Vert {
        int startWeight = 0;
        int countWeight = 0;
    };
    struct Weight {
        int joint = 0;
        float bias = 0.0f;
        Vec3f pos = {};
    };
    struct Tri {
        int i0 = 0, i1 = 0, i2 = 0;
    };
    std::vector<Vert> verts;
    std::vector<Weight> weights;
    std::vector<Tri> tris;
};

inline bool ParseMd5MeshRaw(std::string_view sv, std::vector<Md5Joint>& outJoints, std::vector<Md5SubMeshRaw>& outMeshes) {
    Md5Lexer lex{ sv, 0 };
    std::string_view tok;
    if (!lex.Next(tok) || tok != "MD5Version") return false;
    int version = 0;
    if (!lex.NextInt(version) || version != 10) return false;

    int declaredJoints = -1;
    int declaredMeshes = -1;

    while (lex.Next(tok)) {
        if (tok == "commandline") {
            std::string_view dummy;
            if (!lex.Next(dummy)) return false;
        } else if (tok == "numJoints") {
            if (!lex.NextInt(declaredJoints) || declaredJoints <= 0 || declaredJoints > 65536) return false;
        } else if (tok == "numMeshes") {
            if (!lex.NextInt(declaredMeshes) || declaredMeshes <= 0 || declaredMeshes > 16384) return false;
        } else if (tok == "joints") {
            if (!lex.Expect("{")) return false;
            while (lex.Next(tok)) {
                if (tok == "}") break;
                Md5Joint j;
                j.name = std::string(tok);
                Vec3f qxyz = {};
                if (!lex.NextInt(j.parent) || !lex.NextVec3Paren(j.pos) || !lex.NextVec3Paren(qxyz)) {
                    return false;
                }
                if (j.parent >= static_cast<int>(outJoints.size()) || j.parent < -1) return false;
                j.orient = BuildMd5Quat(qxyz.x, qxyz.y, qxyz.z);
                outJoints.push_back(std::move(j));
                if (outJoints.size() > 65536) return false;
            }
        } else if (tok == "mesh") {
            if (!lex.Expect("{")) return false;
            Md5SubMeshRaw sm;
            bool closed = false;
            while (lex.Next(tok)) {
                if (tok == "}") { closed = true; break; }
                if (tok == "shader") {
                    std::string_view sh;
                    if (!lex.Next(sh)) return false;
                } else if (tok == "numverts") {
                    int nv = 0;
                    if (!lex.NextInt(nv) || nv < 0 || nv > 2000000) return false;
                    sm.verts.assign(static_cast<size_t>(nv), {});
                } else if (tok == "vert") {
                    int vIdx = 0, sw = 0, cw = 0;
                    float u = 0.0f, v = 0.0f;
                    if (!lex.NextInt(vIdx) || !lex.Expect("(") || !lex.NextFloat(u) || !lex.NextFloat(v) ||
                        !lex.Expect(")") || !lex.NextInt(sw) || !lex.NextInt(cw)) {
                        return false;
                    }
                    if (vIdx < 0 || static_cast<size_t>(vIdx) >= sm.verts.size() || sw < 0 || cw <= 0) return false;
                    sm.verts[static_cast<size_t>(vIdx)] = { sw, cw };
                } else if (tok == "numtris") {
                    int nt = 0;
                    if (!lex.NextInt(nt) || nt < 0 || nt > 4000000) return false;
                    sm.tris.assign(static_cast<size_t>(nt), {});
                } else if (tok == "tri") {
                    int tIdx = 0, a = 0, b = 0, c = 0;
                    if (!lex.NextInt(tIdx) || !lex.NextInt(a) || !lex.NextInt(b) || !lex.NextInt(c)) return false;
                    if (tIdx < 0 || static_cast<size_t>(tIdx) >= sm.tris.size()) return false;
                    sm.tris[static_cast<size_t>(tIdx)] = { a, b, c };
                } else if (tok == "numweights") {
                    int nw = 0;
                    if (!lex.NextInt(nw) || nw < 0 || nw > 4000000) return false;
                    sm.weights.assign(static_cast<size_t>(nw), {});
                } else if (tok == "weight") {
                    int wIdx = 0, jIdx = 0;
                    float bias = 0.0f;
                    Vec3f wp = {};
                    if (!lex.NextInt(wIdx) || !lex.NextInt(jIdx) || !lex.NextFloat(bias) || !lex.NextVec3Paren(wp)) {
                        return false;
                    }
                    if (wIdx < 0 || static_cast<size_t>(wIdx) >= sm.weights.size()) return false;
                    sm.weights[static_cast<size_t>(wIdx)] = { jIdx, bias, wp };
                }
            }
            if (!closed) return false;
            outMeshes.push_back(std::move(sm));
        }
    }

    if (outJoints.empty() || outMeshes.empty()) return false;
    if (declaredJoints > 0 && static_cast<int>(outJoints.size()) != declaredJoints) return false;
    if (declaredMeshes > 0 && static_cast<int>(outMeshes.size()) != declaredMeshes) return false;
    return true;
}

inline bool SkinMd5SubMeshes(
    const std::vector<Md5Joint>& joints,
    const std::vector<Md5SubMeshRaw>& subMeshes,
    LoadedMesh3D& mesh)
{
    const int nJoints = static_cast<int>(joints.size());
    for (const auto& sm : subMeshes) {
        if (sm.verts.empty() || sm.tris.empty() || sm.weights.empty()) continue;
        const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
        const int nVerts = static_cast<int>(sm.verts.size());
        const int nWeights = static_cast<int>(sm.weights.size());

        for (int vi = 0; vi < nVerts; ++vi) {
            const auto& v = sm.verts[static_cast<size_t>(vi)];
            if (v.countWeight <= 0 || v.startWeight < 0 || v.startWeight + v.countWeight > nWeights) {
                return false;
            }
            Vec3f skinned = { 0.0f, 0.0f, 0.0f };
            for (int k = 0; k < v.countWeight; ++k) {
                const auto& w = sm.weights[static_cast<size_t>(v.startWeight + k)];
                if (w.joint < 0 || w.joint >= nJoints) return false;
                const auto& jnt = joints[static_cast<size_t>(w.joint)];
                Vec3f rotPos = QuatRotate(jnt.orient, w.pos);
                skinned.x += (jnt.pos.x + rotPos.x) * w.bias;
                skinned.y += (jnt.pos.y + rotPos.y) * w.bias;
                skinned.z += (jnt.pos.z + rotPos.z) * w.bias;
            }
            mesh.vertices.push_back(IdTechToViewport(skinned));
        }

        for (const auto& t : sm.tris) {
            if (t.i0 < 0 || t.i0 >= nVerts || t.i1 < 0 || t.i1 >= nVerts || t.i2 < 0 || t.i2 >= nVerts) {
                return false;
            }
            // Doom 3 MD5 triangles use clockwise front-face winding in Z-up space; flip (i0, i2, i1) for CCW
            mesh.tris.push_back({
                baseV + static_cast<uint32_t>(t.i0),
                baseV + static_cast<uint32_t>(t.i2),
                baseV + static_cast<uint32_t>(t.i1),
                {}
            });
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Md5Detail

namespace ActorXDetail {

struct ChunkHdr {
    char id[21] = {};
    int32_t typeFlag = 0;
    int32_t dataSize = 0;
    int32_t dataCount = 0;
    size_t payloadOffset = 0;
    size_t payloadBytes = 0;
};

inline bool ReadChunkHeader(const uint8_t* data, size_t size, size_t& cursor, ChunkHdr& out) {
    if (cursor + 32 > size) return false;
    std::memcpy(out.id, data + cursor, 20);
    out.id[20] = '\0';
    std::memcpy(&out.typeFlag, data + cursor + 20, 4);
    std::memcpy(&out.dataSize, data + cursor + 24, 4);
    std::memcpy(&out.dataCount, data + cursor + 28, 4);
    if (out.dataSize < 0 || out.dataCount < 0) return false;
    uint64_t bytes64 = static_cast<uint64_t>(out.dataSize) * static_cast<uint64_t>(out.dataCount);
    if (bytes64 > size - (cursor + 32)) return false;
    out.payloadOffset = cursor + 32;
    out.payloadBytes = static_cast<size_t>(bytes64);
    cursor = out.payloadOffset + out.payloadBytes;
    return true;
}

inline bool ChunkPrefixIs(const ChunkHdr& h, const char* prefix) {
    size_t n = std::strlen(prefix);
    return std::strncmp(h.id, prefix, n) == 0;
}

struct Quat4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

inline Quat4 QuatNorm(const Quat4& q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 1e-7f) {
        float inv = 1.0f / l;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }
    return { 0.0f, 0.0f, 0.0f, 1.0f };
}

inline Quat4 QuatConj(const Quat4& q) {
    return { -q.x, -q.y, -q.z, q.w };
}

using MeshBuild3D::QuatMul;

inline Vec3f QuatRot(const Quat4& q, const Vec3f& v) { return MeshBuild3D::QuatRotate(q, v); }

// Unreal Z-up coords -> Explorer2 Y-up viewport
inline Vec3f UnrealToViewport(const Vec3f& p) {
    return { p.x, p.z, -p.y };
}

struct ActorXBone {
    std::string name;
    int32_t parent = -1;
    Vec3f localPos = {};
    Quat4 localQuat = {};
    Vec3f worldPos = {};
    Quat4 worldQuat = {};
};

inline void ComputeWorldSkeleton(std::vector<ActorXBone>& bones) {
    for (size_t i = 0; i < bones.size(); ++i) {
        auto& b = bones[i];
        // In ActorX, non-root bone quaternions store conjugated orientation relative to standard Hamilton
        Quat4 q = (i == 0) ? QuatNorm({ b.localQuat.x, b.localQuat.y, b.localQuat.z, -b.localQuat.w })
                           : QuatNorm(QuatConj(b.localQuat));
        int32_t p = (i == 0) ? -1 : b.parent;
        if (p < 0 || static_cast<size_t>(p) >= i) {
            b.worldPos = b.localPos;
            b.worldQuat = q;
        } else {
            const auto& par = bones[static_cast<size_t>(p)];
            Vec3f rp = QuatRot(par.worldQuat, b.localPos);
            b.worldPos = { par.worldPos.x + rp.x, par.worldPos.y + rp.y, par.worldPos.z + rp.z };
            b.worldQuat = QuatNorm(QuatMul(par.worldQuat, q));
        }
    }
}

struct PskRawData {
    std::vector<Vec3f> points;
    std::vector<uint32_t> wedgePointIndices;
    struct Tri { uint32_t w0, w1, w2; };
    std::vector<Tri> faces;
    std::vector<ActorXBone> refBones;
    struct Weight { float weight; uint32_t pointIdx; uint32_t boneIdx; };
    std::vector<Weight> weights;
};

inline bool ParsePskRaw(const uint8_t* data, size_t size, PskRawData& out) {
    if (!data || size < 32) return false;
    size_t cursor = 0;
    ChunkHdr first;
    if (!ReadChunkHeader(data, size, cursor, first) || !ChunkPrefixIs(first, "ACTRHEAD")) {
        return false;
    }

    while (cursor + 32 <= size) {
        ChunkHdr ch;
        if (!ReadChunkHeader(data, size, cursor, ch)) break;
        const uint8_t* pay = data + ch.payloadOffset;

        if (ChunkPrefixIs(ch, "PNTS0000") && ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 8000000) {
            out.points.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                Vec3f v = {};
                std::memcpy(&v.x, p + 0, 4);
                std::memcpy(&v.y, p + 4, 4);
                std::memcpy(&v.z, p + 8, 4);
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
                out.points[static_cast<size_t>(i)] = v;
            }
        } else if (ChunkPrefixIs(ch, "VTXW0000") && ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            out.wedgePointIndices.resize(static_cast<size_t>(ch.dataCount));
            const size_t nPts = out.points.size();
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                uint32_t idx32 = 0;
                uint16_t idx16 = 0;
                std::memcpy(&idx32, p, 4);
                std::memcpy(&idx16, p, 2);
                uint32_t chosen = idx32;
                if (nPts > 0 && nPts <= 65536 && idx32 >= nPts && idx16 < nPts) {
                    chosen = idx16;
                }
                out.wedgePointIndices[static_cast<size_t>(i)] = chosen;
            }
        } else if (ChunkPrefixIs(ch, "FACE0000") && ch.dataSize >= 6 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            out.faces.reserve(out.faces.size() + static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                uint16_t w0 = 0, w1 = 0, w2 = 0;
                std::memcpy(&w0, p + 0, 2);
                std::memcpy(&w1, p + 2, 2);
                std::memcpy(&w2, p + 4, 2);
                out.faces.push_back({ w0, w1, w2 });
            }
        } else if ((ChunkPrefixIs(ch, "FACE3200") || ChunkPrefixIs(ch, "FACE0032")) &&
                   ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 16000000) {
            out.faces.reserve(out.faces.size() + static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                uint32_t w0 = 0, w1 = 0, w2 = 0;
                std::memcpy(&w0, p + 0, 4);
                std::memcpy(&w1, p + 4, 4);
                std::memcpy(&w2, p + 8, 4);
                out.faces.push_back({ w0, w1, w2 });
            }
        } else if (ChunkPrefixIs(ch, "REFSKEL") && ch.dataSize >= 104 && ch.dataCount > 0 && ch.dataCount <= 65536) {
            out.refBones.resize(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                char nameBuf[65] = {};
                std::memcpy(nameBuf, p, 64);
                // Trim trailing spaces used by classic ActorX exporters
                size_t nLen = std::strlen(nameBuf);
                while (nLen > 0 && nameBuf[nLen - 1] == ' ') nameBuf[--nLen] = '\0';

                int32_t parentIdx = 0;
                std::memcpy(&parentIdx, p + 72, 4);
                Quat4 q = {};
                Vec3f pos = {};
                std::memcpy(&q.x, p + 76, 4);
                std::memcpy(&q.y, p + 80, 4);
                std::memcpy(&q.z, p + 84, 4);
                std::memcpy(&q.w, p + 88, 4);
                std::memcpy(&pos.x, p + 92, 4);
                std::memcpy(&pos.y, p + 96, 4);
                std::memcpy(&pos.z, p + 100, 4);

                auto& dst = out.refBones[static_cast<size_t>(i)];
                dst.name = nameBuf;
                dst.parent = (i == 0) ? -1 : parentIdx;
                dst.localQuat = q;
                dst.localPos = pos;
            }
            ComputeWorldSkeleton(out.refBones);
        } else if ((ChunkPrefixIs(ch, "RAWWEIGHTS") || ChunkPrefixIs(ch, "RAWW0000")) &&
                   ch.dataSize >= 12 && ch.dataCount > 0 && ch.dataCount <= 32000000) {
            out.weights.reserve(static_cast<size_t>(ch.dataCount));
            for (int32_t i = 0; i < ch.dataCount; ++i) {
                const uint8_t* p = pay + static_cast<size_t>(i) * static_cast<size_t>(ch.dataSize);
                float w = 0.0f;
                int32_t ptIdx = 0, boneIdx = 0;
                std::memcpy(&w, p + 0, 4);
                std::memcpy(&ptIdx, p + 4, 4);
                std::memcpy(&boneIdx, p + 8, 4);
                if (std::isfinite(w) && w > 0.0f && ptIdx >= 0 && boneIdx >= 0) {
                    out.weights.push_back({ w, static_cast<uint32_t>(ptIdx), static_cast<uint32_t>(boneIdx) });
                }
            }
        }
    }

    return !out.points.empty() && !out.wedgePointIndices.empty() && !out.faces.empty();
}

inline bool EmitPskGeometry(const PskRawData& psk, const std::vector<Vec3f>& worldPts, LoadedMesh3D& mesh) {
    if (worldPts.size() != psk.points.size() || psk.faces.empty()) return false;
    mesh.vertices.resize(worldPts.size());
    for (size_t i = 0; i < worldPts.size(); ++i) {
        mesh.vertices[i] = UnrealToViewport(worldPts[i]);
    }
    const size_t nWedges = psk.wedgePointIndices.size();
    const size_t nPts = worldPts.size();
    mesh.tris.reserve(psk.faces.size());
    for (const auto& f : psk.faces) {
        if (f.w0 >= nWedges || f.w1 >= nWedges || f.w2 >= nWedges) return false;
        uint32_t p0 = psk.wedgePointIndices[f.w0];
        uint32_t p1 = psk.wedgePointIndices[f.w1];
        uint32_t p2 = psk.wedgePointIndices[f.w2];
        if (p0 >= nPts || p1 >= nPts || p2 >= nPts) return false;
        if (p0 == p1 || p1 == p2 || p2 == p0) continue;
        mesh.tris.push_back({ p0, p2, p1, {} });
    }
    return !mesh.tris.empty();
}

} // namespace ActorXDetail

namespace GodotDetail {

// loads in the resource's byte order (ByteIO.h)
inline uint16_t GdU16(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU16(p, be); }

inline uint32_t GdU32(const uint8_t* p, bool be) { return ::Explorer2::ByteIO::RdU32(p, be); }

inline uint64_t GdU64(const uint8_t* p, bool be) {
    if (be) return (static_cast<uint64_t>(GdU32(p, true)) << 32) | GdU32(p + 4, true);
    return static_cast<uint64_t>(GdU32(p, false)) | (static_cast<uint64_t>(GdU32(p + 4, false)) << 32);
}

inline float GdF32(const uint8_t* p, bool be) {
    uint32_t u = GdU32(p, be);
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

inline double GdF64(const uint8_t* p, bool be) {
    uint64_t u = GdU64(p, be);
    double d = 0.0;
    std::memcpy(&d, &u, 8);
    return d;
}

inline bool DecodeBase64Bytes(std::string_view b64, std::vector<uint8_t>& out) {
    return ::Explorer2::Base64::Decode(b64, out, ::Explorer2::Base64::Mode::Strict);
}

struct GdXform3D {
    // Godot Transform3D: 3x3 basis columns/rows (xx, xy, xz, yx, yy, yz, zx, zy, zz) + origin (ox, oy, oz)
    // In Godot's text/binary serialization: Transform3D(basis.rows[0].x, rows[0].y, rows[0].z, rows[1]..., rows[2]..., origin.x, origin.y, origin.z)
    float m[3][4] = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }
    };

    static GdXform3D From12Floats(const float f[12]) {
        GdXform3D xf{};
        // Godot Transform3D constructor in .tscn/.tres takes (x_axis.x, x_axis.y, x_axis.z, y_axis.x, y_axis.y, y_axis.z, z_axis.x, z_axis.y, z_axis.z, origin.x, origin.y, origin.z)
        xf.m[0][0] = f[0]; xf.m[1][0] = f[1]; xf.m[2][0] = f[2];
        xf.m[0][1] = f[3]; xf.m[1][1] = f[4]; xf.m[2][1] = f[5];
        xf.m[0][2] = f[6]; xf.m[1][2] = f[7]; xf.m[2][2] = f[8];
        xf.m[0][3] = f[9]; xf.m[1][3] = f[10]; xf.m[2][3] = f[11];
        return xf;
    }

    static GdXform3D Mul(const GdXform3D& a, const GdXform3D& b) {
        GdXform3D c{};
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                c.m[r][col] = a.m[r][0] * b.m[0][col] + a.m[r][1] * b.m[1][col] + a.m[r][2] * b.m[2][col];
            }
            c.m[r][3] = a.m[r][0] * b.m[0][3] + a.m[r][1] * b.m[1][3] + a.m[r][2] * b.m[2][3] + a.m[r][3];
        }
        return c;
    }

    Vec3f Apply(const Vec3f& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]
        };
    }
};

struct GdLocalMesh {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> tris;
    bool empty() const { return verts.empty() || tris.empty(); }
};

inline void BuildBoxPrimitive(float sx, float sy, float sz, GdLocalMesh& out) {
    float hx = std::max(0.001f, sx * 0.5f);
    float hy = std::max(0.001f, sy * 0.5f);
    float hz = std::max(0.001f, sz * 0.5f);
    uint32_t b = static_cast<uint32_t>(out.verts.size());
    out.verts.push_back({ -hx, -hy, -hz });
    out.verts.push_back({  hx, -hy, -hz });
    out.verts.push_back({  hx,  hy, -hz });
    out.verts.push_back({ -hx,  hy, -hz });
    out.verts.push_back({ -hx, -hy,  hz });
    out.verts.push_back({  hx, -hy,  hz });
    out.verts.push_back({  hx,  hy,  hz });
    out.verts.push_back({ -hx,  hy,  hz });
    static const uint32_t kBoxTris[12][3] = {
        {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
        {0,1,5}, {0,5,4}, {2,3,7}, {2,7,6},
        {1,2,6}, {1,6,5}, {3,0,4}, {3,4,7}
    };
    for (const auto& t : kBoxTris) out.tris.push_back({ b + t[0], b + t[1], b + t[2] });
}

inline void BuildCylinderOrConePrimitive(float topR, float botR, float height, int segs, GdLocalMesh& out) {
    segs = std::clamp(segs, 6, 48);
    float hh = std::max(0.001f, height * 0.5f);
    topR = std::max(0.0f, topR);
    botR = std::max(0.0f, botR);
    uint32_t b = static_cast<uint32_t>(out.verts.size());
    for (int i = 0; i < segs; ++i) {
        float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(segs);
        float ca = std::cos(a), sa = std::sin(a);
        out.verts.push_back({ ca * botR, -hh, sa * botR });
        out.verts.push_back({ ca * topR,  hh, sa * topR });
    }
    uint32_t botC = static_cast<uint32_t>(out.verts.size());
    out.verts.push_back({ 0.0f, -hh, 0.0f });
    uint32_t topC = static_cast<uint32_t>(out.verts.size());
    out.verts.push_back({ 0.0f,  hh, 0.0f });
    for (int i = 0; i < segs; ++i) {
        int ni = (i + 1) % segs;
        uint32_t b0 = b + static_cast<uint32_t>(i * 2);
        uint32_t t0 = b0 + 1;
        uint32_t b1 = b + static_cast<uint32_t>(ni * 2);
        uint32_t t1 = b1 + 1;
        if (botR > 1e-4f || topR > 1e-4f) {
            if (botR > 1e-4f) out.tris.push_back({ b0, b1, t1 });
            if (topR > 1e-4f) out.tris.push_back({ b0, t1, t0 });
        }
        if (botR > 1e-4f) out.tris.push_back({ botC, b1, b0 });
        if (topR > 1e-4f) out.tris.push_back({ topC, t0, t1 });
    }
}

inline void BuildSpherePrimitive(float radius, float height, GdLocalMesh& out) {
    float rx = std::max(0.001f, radius);
    float ry = std::max(0.001f, height * 0.5f);
    const int rings = 10, segs = 16;
    uint32_t b = static_cast<uint32_t>(out.verts.size());
    for (int r = 0; r <= rings; ++r) {
        float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(rings);
        float y = std::cos(phi) * ry;
        float sr = std::sin(phi) * rx;
        for (int s = 0; s <= segs; ++s) {
            float th = 6.2831853f * static_cast<float>(s) / static_cast<float>(segs);
            out.verts.push_back({ std::cos(th) * sr, y, std::sin(th) * sr });
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segs; ++s) {
            uint32_t i0 = b + static_cast<uint32_t>(r * (segs + 1) + s);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + static_cast<uint32_t>(segs + 1);
            uint32_t i3 = i2 + 1;
            if (r > 0) out.tris.push_back({ i0, i2, i1 });
            if (r + 1 < rings) out.tris.push_back({ i1, i2, i3 });
        }
    }
}

inline bool DecodeGodot4SurfaceRawBytes(
    const std::vector<uint8_t>& vData,
    uint32_t vertexCount,
    const std::vector<uint8_t>& iData,
    uint32_t indexCount,
    const float aabb[6],
    bool hasAabb,
    GdLocalMesh& out)
{
    if (vertexCount < 3 || vertexCount > 2000000 || vData.empty()) return false;
    const uint32_t baseV = static_cast<uint32_t>(out.verts.size());

    // Godot 4 uncompressed position stream: 12B float3 (or interleaved >= 12B where vertex_data == vertexCount * 12 or 16)
    if (vData.size() >= static_cast<size_t>(vertexCount) * 12 &&
        (vData.size() % vertexCount == 0) && (vData.size() / vertexCount == 12 || vData.size() / vertexCount >= 16))
    {
        size_t stride = vData.size() / vertexCount;
        // Note: in Godot 4 ArrayMesh non-interleaved attribute streams, positions come FIRST as vertexCount * 12 bytes
        // followed by normals/tangents! Check if float3 at 12B tight packing matches AABB or finite bounds:
        bool useTight12 = (stride != 12);
        if (useTight12 && hasAabb) {
            float f0x = GdF32(vData.data(), false), f0y = GdF32(vData.data() + 4, false), f0z = GdF32(vData.data() + 8, false);
            float f1x = GdF32(vData.data() + 12, false), f1y = GdF32(vData.data() + 16, false), f1z = GdF32(vData.data() + 20, false);
            if (!std::isfinite(f0x) || !std::isfinite(f0y) || !std::isfinite(f0z) ||
                !std::isfinite(f1x) || !std::isfinite(f1y) || !std::isfinite(f1z)) {
                useTight12 = false;
            }
        }
        size_t posStride = useTight12 ? 12u : stride;
        for (uint32_t v = 0; v < vertexCount; ++v) {
            const uint8_t* vp = vData.data() + static_cast<size_t>(v) * posStride;
            float vx = GdF32(vp + 0, false), vy = GdF32(vp + 4, false), vz = GdF32(vp + 8, false);
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) return false;
            out.verts.push_back({ vx, vy, vz });
        }
    }
    // Godot 4.2+ compressed uint16x4 AABB-normalized position stream (8B per vertex + optional 8B normal/tangent stream)
    else if (vData.size() >= static_cast<size_t>(vertexCount) * 8 && (vData.size() % vertexCount == 0)) {
        float ox = hasAabb ? aabb[0] : -0.5f;
        float oy = hasAabb ? aabb[1] : -0.5f;
        float oz = hasAabb ? aabb[2] : -0.5f;
        float sx = hasAabb ? aabb[3] : 1.0f;
        float sy = hasAabb ? aabb[4] : 1.0f;
        float sz = hasAabb ? aabb[5] : 1.0f;
        for (uint32_t v = 0; v < vertexCount; ++v) {
            const uint8_t* vp = vData.data() + static_cast<size_t>(v) * 8;
            float ux = static_cast<float>(GdU16(vp + 0, false)) / 65535.0f;
            float uy = static_cast<float>(GdU16(vp + 2, false)) / 65535.0f;
            float uz = static_cast<float>(GdU16(vp + 4, false)) / 65535.0f;
            out.verts.push_back({ ox + ux * sx, oy + uy * sy, oz + uz * sz });
        }
    } else {
        return false;
    }

    if (indexCount >= 3 && !iData.empty()) {
        if (vertexCount <= 65535 && iData.size() >= static_cast<size_t>(indexCount) * 2 && iData.size() < static_cast<size_t>(indexCount) * 4) {
            for (uint32_t t = 0; t + 2 < indexCount; t += 3) {
                uint32_t i0 = GdU16(iData.data() + static_cast<size_t>(t + 0) * 2, false);
                uint32_t i1 = GdU16(iData.data() + static_cast<size_t>(t + 1) * 2, false);
                uint32_t i2 = GdU16(iData.data() + static_cast<size_t>(t + 2) * 2, false);
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) out.tris.push_back({ baseV + i0, baseV + i1, baseV + i2 });
            }
        } else if (iData.size() >= static_cast<size_t>(indexCount) * 4) {
            for (uint32_t t = 0; t + 2 < indexCount; t += 3) {
                uint32_t i0 = GdU32(iData.data() + static_cast<size_t>(t + 0) * 4, false);
                uint32_t i1 = GdU32(iData.data() + static_cast<size_t>(t + 1) * 4, false);
                uint32_t i2 = GdU32(iData.data() + static_cast<size_t>(t + 2) * 4, false);
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) return false;
                if (i0 != i1 && i1 != i2 && i2 != i0) out.tris.push_back({ baseV + i0, baseV + i1, baseV + i2 });
            }
        } else {
            return false;
        }
    } else {
        for (uint32_t t = 0; t + 2 < vertexCount; t += 3) {
            out.tris.push_back({ baseV + t, baseV + t + 1, baseV + t + 2 });
        }
    }

    return !out.tris.empty();
}

inline bool IsLikelyGodotBinaryHeader(const uint8_t* data, size_t size) {
    return data && size >= 12 && (std::memcmp(data, "RSRC", 4) == 0 || std::memcmp(data, "RSCC", 4) == 0);
}

inline bool IsLikelyGodotTextHeader(const uint8_t* data, size_t size) {
    if (!data || size < 12 || IsLikelyGodotBinaryHeader(data, size)) return false;
    size_t scan = std::min<size_t>(size, 256);
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n' ||
                           static_cast<uint8_t>(sv.front()) == 0xEF || static_cast<uint8_t>(sv.front()) == 0xBB || static_cast<uint8_t>(sv.front()) == 0xBF)) {
        sv.remove_prefix(1);
    }
    return sv.rfind("[gd_scene", 0) == 0 || sv.rfind("[gd_resource", 0) == 0;
}

} // namespace GodotDetail

inline bool IsLikelyGodotResourceHeader(const uint8_t* data, size_t size) {
    if (!data || size < 12) return false;
    if (std::memcmp(data, "RSRC", 4) == 0 || std::memcmp(data, "RSCC", 4) == 0) return true;
    size_t scan = std::min<size_t>(size, 256);
    std::string_view sv(reinterpret_cast<const char*>(data), scan);
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n' ||
                           static_cast<uint8_t>(sv.front()) == 0xEF || static_cast<uint8_t>(sv.front()) == 0xBB || static_cast<uint8_t>(sv.front()) == 0xBF)) {
        sv.remove_prefix(1);
    }
    return sv.rfind("[gd_scene", 0) == 0 || sv.rfind("[gd_resource", 0) == 0;
}

namespace VtkXmlDetail {

inline std::string GetXmlAttr(std::string_view tag, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tag, attrName, v, false) ? std::string(v) : std::string();
}

// Decodes VTK Base64 streams, explicitly supporting mid-stream '=' padding (VTK's split header+payload Base64)
// VTK writes each data block (header, then data) as its own padded base64 run
inline bool DecodeVtkBase64(std::string_view b64, std::vector<uint8_t>& out, size_t maxBytes = 128 * 1024 * 1024) {
    return ::Explorer2::Base64::Decode(b64, out, ::Explorer2::Base64::Mode::Concatenated, maxBytes);
}

enum class VtkXmlCompressor { None, Zlib, Lz4 };

struct VtkXmlDocConfig {
    bool bigEndian = false;
    bool hdr64 = false;
    VtkXmlCompressor comp = VtkXmlCompressor::None;
    bool appendedRaw = false;
    const uint8_t* appendedPtr = nullptr;
    size_t appendedLen = 0;
    std::string appendedBase64Clean;
};

inline uint64_t RdHdrWord(const uint8_t* p, bool bigEndian, bool hdr64) {
    if (hdr64) {
        return bigEndian ? DetailVtk::RdBE64Vtk(p) : DetailDccScene::RdU64LE(p);
    }
    return bigEndian ? DetailVtk::RdBE32Vtk(p) : DetailDccScene::RdU32LE(p);
}

inline bool UnpackFramedVtkPayload(
    const uint8_t* framed,
    size_t framedLen,
    const VtkXmlDocConfig& cfg,
    std::vector<uint8_t>& outRaw)
{
    outRaw.clear();
    const size_t wSz = cfg.hdr64 ? 8u : 4u;
    if (!framed || framedLen < wSz) return false;

    if (cfg.comp == VtkXmlCompressor::None) {
        uint64_t byteCnt = RdHdrWord(framed, cfg.bigEndian, cfg.hdr64);
        if (byteCnt > 128 * 1024 * 1024 || wSz + byteCnt > framedLen) return false;
        outRaw.assign(framed + wSz, framed + wSz + static_cast<size_t>(byteCnt));
        return true;
    }

    // Compressed framing: [numBlocks][blockSize][lastBlockSize][compSize_0 ... compSize_{numBlocks-1}][blocks...]
    if (framedLen < 3 * wSz) return false;
    uint64_t numBlocks = RdHdrWord(framed + 0 * wSz, cfg.bigEndian, cfg.hdr64);
    uint64_t blockSize = RdHdrWord(framed + 1 * wSz, cfg.bigEndian, cfg.hdr64);
    uint64_t lastSize  = RdHdrWord(framed + 2 * wSz, cfg.bigEndian, cfg.hdr64);
    if (numBlocks == 0) return true;
    if (numBlocks > 262144 || blockSize > 64 * 1024 * 1024 || lastSize > 64 * 1024 * 1024) return false;
    if ((3 + numBlocks) * wSz > framedLen) return false;

    uint64_t totalOut = (numBlocks - 1) * blockSize + ((lastSize > 0) ? lastSize : blockSize);
    if (totalOut > 128 * 1024 * 1024) return false;
    outRaw.resize(static_cast<size_t>(totalOut));

    size_t sp = static_cast<size_t>((3 + numBlocks) * wSz);
    size_t dp = 0;
    for (uint64_t b = 0; b < numBlocks; ++b) {
        uint64_t cSz = RdHdrWord(framed + static_cast<size_t>((3 + b) * wSz), cfg.bigEndian, cfg.hdr64);
        uint64_t uSz = (b + 1 < numBlocks) ? blockSize : ((lastSize > 0) ? lastSize : blockSize);
        if (sp + cSz > framedLen || dp + uSz > outRaw.size()) return false;

        if (cfg.comp == VtkXmlCompressor::Zlib) {
            std::vector<uint8_t> blk;
            if (!ZipDeflate::InflateZlib(framed + sp, static_cast<size_t>(cSz), blk, static_cast<size_t>(uSz) + 64) ||
                blk.size() < static_cast<size_t>(uSz)) return false;
            std::memcpy(outRaw.data() + dp, blk.data(), static_cast<size_t>(uSz));
        } else if (cfg.comp == VtkXmlCompressor::Lz4) {
            std::vector<uint8_t> blk;
            if (!::Explorer2::Lz4::DecodeBlockExact(framed + sp, static_cast<size_t>(cSz), static_cast<size_t>(uSz), blk))
                return false;
            std::memcpy(outRaw.data() + dp, blk.data(), static_cast<size_t>(uSz));
        }
        sp += static_cast<size_t>(cSz);
        dp += static_cast<size_t>(uSz);
    }
    return true;
}

inline DetailVtk::VtkScalarKind ParseXmlTypeAttr(std::string_view t) {
    std::string low = DetailVtk::ToLowerVtk(t);
    if (low == "float32") return DetailVtk::VtkScalarKind::F32;
    if (low == "float64") return DetailVtk::VtkScalarKind::F64;
    if (low == "int8")    return DetailVtk::VtkScalarKind::I8;
    if (low == "uint8")   return DetailVtk::VtkScalarKind::U8;
    if (low == "int16")   return DetailVtk::VtkScalarKind::I16;
    if (low == "uint16")  return DetailVtk::VtkScalarKind::U16;
    if (low == "int32")   return DetailVtk::VtkScalarKind::I32;
    if (low == "uint32")  return DetailVtk::VtkScalarKind::U32;
    if (low == "int64")   return DetailVtk::VtkScalarKind::I64;
    if (low == "uint64")  return DetailVtk::VtkScalarKind::U64;
    return DetailVtk::VtkScalarKind::Invalid;
}

inline double RdXmlScalar(const uint8_t* p, DetailVtk::VtkScalarKind k, bool bigEndian) {
    if (bigEndian) return DetailVtk::RdBinDoubleVtk(p, k);
    switch (k) {
    case DetailVtk::VtkScalarKind::F32: { float f = 0; std::memcpy(&f, p, 4); return f; }
    case DetailVtk::VtkScalarKind::F64: { double d = 0; std::memcpy(&d, p, 8); return d; }
    case DetailVtk::VtkScalarKind::I8:  { int8_t v = 0; std::memcpy(&v, p, 1); return v; }
    case DetailVtk::VtkScalarKind::U8:  return p[0];
    case DetailVtk::VtkScalarKind::I16: { int16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::U16: { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::I32: { int32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::U32: { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::I64: { int64_t v = 0; std::memcpy(&v, p, 8); return static_cast<double>(v); }
    case DetailVtk::VtkScalarKind::U64: { uint64_t v = 0; std::memcpy(&v, p, 8); return static_cast<double>(v); }
    default: return 0.0;
    }
}

inline int64_t RdXmlInt(const uint8_t* p, DetailVtk::VtkScalarKind k, bool bigEndian) {
    if (bigEndian) return DetailVtk::RdBinIntVtk(p, k);
    switch (k) {
    case DetailVtk::VtkScalarKind::I8:  { int8_t v = 0; std::memcpy(&v, p, 1); return v; }
    case DetailVtk::VtkScalarKind::U8:  return p[0];
    case DetailVtk::VtkScalarKind::I16: { int16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::U16: { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::I32: { int32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::U32: { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::I64: { int64_t v = 0; std::memcpy(&v, p, 8); return v; }
    case DetailVtk::VtkScalarKind::U64: { uint64_t v = 0; std::memcpy(&v, p, 8); return static_cast<int64_t>(v); }
    default: return static_cast<int64_t>(std::llround(RdXmlScalar(p, k, false)));
    }
}

inline bool DecodeDataArrayDoubles(
    std::string_view openTag,
    std::string_view innerText,
    const VtkXmlDocConfig& cfg,
    std::vector<double>& outVals)
{
    outVals.clear();
    auto kind = ParseXmlTypeAttr(GetXmlAttr(openTag, "type"));
    if (kind == DetailVtk::VtkScalarKind::Invalid) return false;
    std::string fmt = DetailVtk::ToLowerVtk(GetXmlAttr(openTag, "format"));
    if (fmt.empty()) fmt = "ascii";

    if (fmt == "ascii") {
        const char* cur = innerText.data();
        const char* end = cur + innerText.size();
        double v = 0.0;
        while (DetailVtk::NextDoubleAsciiVtk(cur, end, v)) {
            outVals.push_back(v);
            if (outVals.size() > 30000000) return false;
        }
        return true;
    }

    std::vector<uint8_t> raw;
    if (fmt == "binary") {
        std::vector<uint8_t> framed;
        if (!DecodeVtkBase64(innerText, framed) || framed.empty()) return false;
        if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
    } else if (fmt == "appended") {
        std::string offStr = GetXmlAttr(openTag, "offset");
        if (offStr.empty()) return false;
        uint64_t off = std::strtoull(offStr.c_str(), nullptr, 10);
        if (cfg.appendedRaw) {
            if (!cfg.appendedPtr || off >= cfg.appendedLen) return false;
            if (!UnpackFramedVtkPayload(cfg.appendedPtr + off, cfg.appendedLen - static_cast<size_t>(off), cfg, raw)) return false;
        } else {
            if (off >= cfg.appendedBase64Clean.size()) return false;
            std::string_view tail(cfg.appendedBase64Clean.data() + static_cast<size_t>(off),
                                  cfg.appendedBase64Clean.size() - static_cast<size_t>(off));
            std::vector<uint8_t> framed;
            if (!DecodeVtkBase64(tail, framed) || framed.empty()) return false;
            if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
        }
    } else {
        return false;
    }

    size_t bsz = DetailVtk::VtkScalarByteSize(kind);
    if (bsz == 0 || (raw.size() % bsz) != 0) return false;
    size_t n = raw.size() / bsz;
    outVals.resize(n);
    for (size_t i = 0; i < n; ++i) {
        outVals[i] = RdXmlScalar(raw.data() + i * bsz, kind, cfg.bigEndian);
    }
    return true;
}

inline bool DecodeDataArrayInts(
    std::string_view openTag,
    std::string_view innerText,
    const VtkXmlDocConfig& cfg,
    std::vector<int64_t>& outVals)
{
    outVals.clear();
    auto kind = ParseXmlTypeAttr(GetXmlAttr(openTag, "type"));
    if (kind == DetailVtk::VtkScalarKind::Invalid) return false;
    std::string fmt = DetailVtk::ToLowerVtk(GetXmlAttr(openTag, "format"));
    if (fmt.empty()) fmt = "ascii";

    if (fmt == "ascii") {
        const char* cur = innerText.data();
        const char* end = cur + innerText.size();
        int64_t v = 0;
        while (DetailVtk::NextIntAsciiVtk(cur, end, v)) {
            outVals.push_back(v);
            if (outVals.size() > 30000000) return false;
        }
        return true;
    }

    std::vector<uint8_t> raw;
    if (fmt == "binary") {
        std::vector<uint8_t> framed;
        if (!DecodeVtkBase64(innerText, framed) || framed.empty()) return false;
        if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
    } else if (fmt == "appended") {
        std::string offStr = GetXmlAttr(openTag, "offset");
        if (offStr.empty()) return false;
        uint64_t off = std::strtoull(offStr.c_str(), nullptr, 10);
        if (cfg.appendedRaw) {
            if (!cfg.appendedPtr || off >= cfg.appendedLen) return false;
            if (!UnpackFramedVtkPayload(cfg.appendedPtr + off, cfg.appendedLen - static_cast<size_t>(off), cfg, raw)) return false;
        } else {
            if (off >= cfg.appendedBase64Clean.size()) return false;
            std::string_view tail(cfg.appendedBase64Clean.data() + static_cast<size_t>(off),
                                  cfg.appendedBase64Clean.size() - static_cast<size_t>(off));
            std::vector<uint8_t> framed;
            if (!DecodeVtkBase64(tail, framed) || framed.empty()) return false;
            if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
        }
    } else {
        return false;
    }

    size_t bsz = DetailVtk::VtkScalarByteSize(kind);
    if (bsz == 0 || (raw.size() % bsz) != 0) return false;
    size_t n = raw.size() / bsz;
    outVals.resize(n);
    for (size_t i = 0; i < n; ++i) {
        outVals[i] = RdXmlInt(raw.data() + i * bsz, kind, cfg.bigEndian);
    }
    return true;
}

// Enumerates <DataArray ...>inner</DataArray> or <DataArray .../> inside an XML section
template <typename Callback>
inline void ForEachDataArrayInSection(std::string_view sec, Callback&& cb) {
    size_t pos = 0;
    while ((pos = sec.find("<DataArray", pos)) != std::string_view::npos) {
        size_t gt = sec.find('>', pos + 10);
        if (gt == std::string_view::npos) break;
        std::string_view openTag = sec.substr(pos, gt - pos + 1);
        if (gt >= 1 && sec[gt - 1] == '/') {
            cb(openTag, std::string_view{});
            pos = gt + 1;
        } else {
            size_t closePos = sec.find("</DataArray>", gt + 1);
            if (closePos == std::string_view::npos) break;
            cb(openTag, sec.substr(gt + 1, closePos - (gt + 1)));
            pos = closePos + 12;
        }
    }
}

inline std::string_view FindXmlElementBody(std::string_view doc, std::string_view elemName, std::string_view* outOpenTag = nullptr) {
    std::string needle = "<" + std::string(elemName);
    size_t p = 0;
    while ((p = doc.find(needle, p)) != std::string_view::npos) {
        size_t after = p + needle.size();
        if (after < doc.size() && (std::isspace(static_cast<unsigned char>(doc[after])) || doc[after] == '>' || doc[after] == '/')) {
            size_t gt = doc.find('>', after);
            if (gt == std::string_view::npos) return {};
            if (outOpenTag) *outOpenTag = doc.substr(p, gt - p + 1);
            if (doc[gt - 1] == '/') return std::string_view(doc.data() + gt, 0);
            std::string closeTag = "</" + std::string(elemName) + ">";
            size_t cp = doc.find(closeTag, gt + 1);
            if (cp == std::string_view::npos) return {};
            return doc.substr(gt + 1, cp - (gt + 1));
        }
        p = after;
    }
    return {};
}

inline bool BuildXmlCellList(
    std::string_view sectionXml,
    const VtkXmlDocConfig& cfg,
    DetailVtk::VtkCellList& outList,
    std::vector<int64_t>* outTypes = nullptr)
{
    outList.offsets.clear();
    outList.conn.clear();
    if (outTypes) outTypes->clear();
    if (sectionXml.empty()) return false;

    std::vector<int64_t> rawConn, rawOff;
    ForEachDataArrayInSection(sectionXml, [&](std::string_view oTag, std::string_view inner) {
        std::string nm = DetailVtk::ToLowerVtk(GetXmlAttr(oTag, "Name"));
        if (nm == "connectivity") {
            DecodeDataArrayInts(oTag, inner, cfg, rawConn);
        } else if (nm == "offsets") {
            DecodeDataArrayInts(oTag, inner, cfg, rawOff);
        } else if (nm == "types" && outTypes) {
            DecodeDataArrayInts(oTag, inner, cfg, *outTypes);
        }
    });

    if (rawConn.empty() || rawOff.empty()) return false;
    outList.offsets.reserve(rawOff.size() + 1);
    if (rawOff[0] != 0) outList.offsets.push_back(0);
    for (int64_t o : rawOff) {
        if (o < 0 || static_cast<size_t>(o) > rawConn.size()) return false;
        if (!outList.offsets.empty() && static_cast<size_t>(o) < outList.offsets.back()) return false;
        outList.offsets.push_back(static_cast<size_t>(o));
    }
    outList.conn.reserve(rawConn.size());
    for (int64_t idx : rawConn) {
        if (idx < 0) return false;
        outList.conn.push_back(static_cast<uint32_t>(idx));
    }
    return outList.offsets.size() >= 2;
}

inline bool ParseExtent6(std::string_view s, int ext[6]) {
    const char* cur = s.data();
    const char* end = cur + s.size();
    for (int i = 0; i < 6; ++i) {
        int64_t v = 0;
        if (!DetailVtk::NextIntAsciiVtk(cur, end, v)) return false;
        ext[i] = static_cast<int>(v);
    }
    return ext[1] >= ext[0] && ext[3] >= ext[2] && ext[5] >= ext[4];
}

inline bool ParseDoublesN(std::string_view s, double* dst, int n) {
    const char* cur = s.data();
    const char* end = cur + s.size();
    for (int i = 0; i < n; ++i) {
        if (!DetailVtk::NextDoubleAsciiVtk(cur, end, dst[i])) return false;
    }
    return true;
}

inline bool PrepareVtkXmlDatasetDoc(
    const uint8_t* data,
    size_t size,
    std::string_view expectedRootType,
    VtkXmlDocConfig& cfg,
    std::string_view& xmlHead)
{
    if (!data || size < 32) return false;
    std::string_view fullSv(reinterpret_cast<const char*>(data), size);
    size_t vfPos = fullSv.find("<VTKFile");
    if (vfPos == std::string_view::npos || vfPos > 4096) return false;
    size_t vfGt = fullSv.find('>', vfPos);
    if (vfGt == std::string_view::npos) return false;
    std::string_view vfTag = fullSv.substr(vfPos, vfGt - vfPos + 1);

    std::string rootType = GetXmlAttr(vfTag, "type");
    if (rootType.empty()) return false;
    if (rootType != expectedRootType) return false;
    cfg = VtkXmlDocConfig{};
    cfg.bigEndian = (GetXmlAttr(vfTag, "byte_order") == "BigEndian");
    cfg.hdr64     = (GetXmlAttr(vfTag, "header_type") == "UInt64");
    std::string compStr = GetXmlAttr(vfTag, "compressor");
    if (compStr.find("ZLib") != std::string::npos)      cfg.comp = VtkXmlCompressor::Zlib;
    else if (compStr.find("LZ4") != std::string::npos)  cfg.comp = VtkXmlCompressor::Lz4;
    else if (!compStr.empty())                          return false; // Unsupported compressor (e.g. LZMA without library)

    // Locate <AppendedData ...>_...</AppendedData> carefully (raw bytes may contain anything!)
    size_t xmlLimit = size;
    size_t appPos = fullSv.find("<AppendedData");
    if (appPos != std::string_view::npos) {
        xmlLimit = appPos;
        size_t appGt = fullSv.find('>', appPos);
        if (appGt == std::string_view::npos) return false;
        std::string_view appTag = fullSv.substr(appPos, appGt - appPos + 1);
        std::string enc = DetailVtk::ToLowerVtk(GetXmlAttr(appTag, "encoding"));
        size_t underscore = fullSv.find('_', appGt + 1);
        size_t closeApp = fullSv.rfind("</AppendedData>");
        if (underscore == std::string_view::npos || closeApp == std::string_view::npos || closeApp <= underscore) return false;

        if (enc == "raw") {
            cfg.appendedRaw = true;
            cfg.appendedPtr = data + underscore + 1;
            cfg.appendedLen = closeApp - (underscore + 1);
        } else {
            cfg.appendedRaw = false;
            cfg.appendedBase64Clean.reserve(closeApp - (underscore + 1));
            for (size_t i = underscore + 1; i < closeApp; ++i) {
                unsigned char c = data[i];
                if (c > 32) cfg.appendedBase64Clean.push_back(static_cast<char>(c));
            }
        }
    }
    xmlHead = std::string_view(reinterpret_cast<const char*>(data), xmlLimit);
    return true;
}

inline bool DecodePointsSection(
    std::string_view pieceXml,
    const VtkXmlDocConfig& cfg,
    std::vector<Vec3f>& outPts)
{
        outPts.clear();
        std::string_view ptsSec = FindXmlElementBody(pieceXml, "Points");
        if (ptsSec.empty()) return false;
        std::vector<double> coords;
        int nComp = 3;
        ForEachDataArrayInSection(ptsSec, [&](std::string_view oTag, std::string_view inner) {
            if (!coords.empty()) return;
            std::string nc = GetXmlAttr(oTag, "NumberOfComponents");
            if (!nc.empty()) nComp = std::max(1, std::atoi(nc.c_str()));
            DecodeDataArrayDoubles(oTag, inner, cfg, coords);
        });
        if (coords.size() < static_cast<size_t>(nComp)) return false;
        size_t nPts = coords.size() / static_cast<size_t>(nComp);
        outPts.resize(nPts);
        for (size_t i = 0; i < nPts; ++i) {
            double x = coords[i * nComp + 0];
            double y = (nComp >= 2) ? coords[i * nComp + 1] : 0.0;
            double z = (nComp >= 3) ? coords[i * nComp + 2] : 0.0;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            outPts[i] = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
        }
        return !outPts.empty();
}

} // namespace VtkXmlDetail

namespace DetailDem {

inline uint32_t ComputeHypsometricRgb(float normH) {
    float t = std::clamp(normH, 0.0f, 1.0f);
    // Clean hypsometric ramp: lowland green -> foothill gold -> alpine brown -> snow peak white
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (t < 0.33f) {
        float u = t / 0.33f;
        r = 0.22f + 0.50f * u;
        g = 0.56f + 0.22f * u;
        b = 0.24f - 0.06f * u;
    } else if (t < 0.70f) {
        float u = (t - 0.33f) / 0.37f;
        r = 0.72f - 0.14f * u;
        g = 0.78f - 0.34f * u;
        b = 0.18f + 0.08f * u;
    } else {
        float u = (t - 0.70f) / 0.30f;
        r = 0.58f + 0.38f * u;
        g = 0.44f + 0.52f * u;
        b = 0.26f + 0.70f * u;
    }
    return (static_cast<uint32_t>(r * 255.0f) << 16) |
           (static_cast<uint32_t>(g * 255.0f) << 8)  |
            static_cast<uint32_t>(b * 255.0f);
}

inline bool EqualsIgnoreCaseDem(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Parses Fortran 'D'/'E' double from fixed-width or token string_view
inline bool ParseFortranDouble(std::string_view sv, double& out) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    if (sv.empty() || sv.size() >= 64) return false;
    char buf[64];
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == 'D' || c == 'd') c = 'E';
        buf[i] = c;
    }
    buf[sv.size()] = '\0';
    char* next = nullptr;
    out = std::strtod(buf, &next);
    if (next == buf || !std::isfinite(out)) return false;
    while (*next == ' ') ++next;
    return *next == '\0';
}

inline bool ParseFixedInt(std::string_view sv, int64_t& out) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) sv.remove_suffix(1);
    if (sv.empty()) return false;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

// Streams next Fortran number token (splitting concatenated signs like "120-32767")
inline bool NextUsgsToken(const char*& cur, const char* end, std::string_view& outTok) {
    while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
    if (cur >= end) return false;
    const char* s = cur;
    ++cur;
    while (cur < end) {
        char c = *cur;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        if ((c == '+' || c == '-') && cur > s) {
            char prev = *(cur - 1);
            if (prev != 'E' && prev != 'e' && prev != 'D' && prev != 'd') {
                break;
            }
        }
        ++cur;
    }
    outTok = std::string_view(s, static_cast<size_t>(cur - s));
    return true;
}

inline bool EmitTerrainGrid(
    size_t rows, size_t cols,
    const std::vector<float>& elevGrid,
    const std::vector<uint8_t>& validMask,
    double dx, double dy,
    LoadedMesh3D& mesh)
{
    if (rows < 2 || cols < 2 || elevGrid.size() < rows * cols || validMask.size() < rows * cols) {
        return false;
    }

    float minE = 1e30f, maxE = -1e30f;
    for (size_t i = 0; i < rows * cols; ++i) {
        if (validMask[i]) {
            minE = std::min(minE, elevGrid[i]);
            maxE = std::max(maxE, elevGrid[i]);
        }
    }
    if (minE > maxE) return false;
    float invSpan = (maxE > minE + 1e-5f) ? (1.0f / (maxE - minE)) : 0.0f;

    std::vector<uint32_t> vertRemap(rows * cols, 0xFFFFFFFFu);
    auto getOrEmitVert = [&](size_t r, size_t c) -> uint32_t {
        size_t idx = r * cols + c;
        if (vertRemap[idx] != 0xFFFFFFFFu) return vertRemap[idx];
        uint32_t vId = static_cast<uint32_t>(mesh.vertices.size());
        float h = elevGrid[idx];
        (void)ComputeHypsometricRgb((h - minE) * invSpan);
        mesh.vertices.push_back({
            static_cast<float>(static_cast<double>(c) * dx),
            h,
            static_cast<float>(static_cast<double>(r) * dy)
        });
        vertRemap[idx] = vId;
        return vId;
    };

    for (size_t r = 0; r + 1 < rows; ++r) {
        for (size_t c = 0; c + 1 < cols; ++c) {
            const uint8_t v00 = validMask[r * cols + c];
            const uint8_t v10 = validMask[r * cols + (c + 1)];
            const uint8_t v01 = validMask[(r + 1) * cols + c];
            const uint8_t v11 = validMask[(r + 1) * cols + (c + 1)];

            if (v00 && v01 && v10) {
                uint32_t i00 = getOrEmitVert(r, c);
                uint32_t i01 = getOrEmitVert(r + 1, c);
                uint32_t i10 = getOrEmitVert(r, c + 1);
                mesh.tris.push_back({ i00, i01, i10, {} });
            }
            if (v10 && v01 && v11) {
                uint32_t i10 = getOrEmitVert(r, c + 1);
                uint32_t i01 = getOrEmitVert(r + 1, c);
                uint32_t i11 = getOrEmitVert(r + 1, c + 1);
                mesh.tris.push_back({ i10, i01, i11, {} });
            }
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailDem

} // namespace Formats
