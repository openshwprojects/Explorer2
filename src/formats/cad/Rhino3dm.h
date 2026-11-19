#pragma once
#include "../common/MeshFormatCommon.h"
// Rhino3dm.h — #12 Rhinoceros OpenNURBS (.3dm) ON_Brep, ON_Extrusion, ON_NurbsSurface & ON_InstanceRef
//
// Pure C++20 zero-cheat implementation:
//   - Supports OpenNURBS v1..v4 (32-bit chunk lengths) and v5/v50/v60/v70/v80 (64-bit chunk lengths + CRC32).
//   - Evaluates rational & non-rational ON_NurbsSurface tensor-product B-splines via Cox-de Boor basis functions.
//   - Evaluates ON_Extrusion (3D path line + up vector + 2D profile curve + end caps).
//   - Evaluates ON_Brep (both embedded ON_Mesh render caches AND "Save Small" untriangulated m_S surfaces / 3D faces).
//   - Resolves TCODE_INSTANCE_DEFINITION_TABLE + ON_3dmObjectAttributes UUIDs + ON_InstanceRef 4x4 double transforms.

namespace Formats {

namespace Rhino3dmDetail {

using ::Explorer2::ByteIO::RdU16LE;
using ::Explorer2::ByteIO::RdU32LE;
using ::Explorer2::ByteIO::RdU64LE;
using ::Explorer2::ByteIO::RdF32LE;
using ::Explorer2::ByteIO::RdF64LE;

struct Uuid16 {
    std::array<uint8_t, 16> b = {};
    bool operator==(const Uuid16& o) const { return b == o.b; }
    bool IsZero() const {
        for (uint8_t x : b) if (x != 0) return false;
        return true;
    }
};

struct Uuid16Hash {
    size_t operator()(const Uuid16& u) const noexcept {
        uint64_t a = 0, c = 0;
        std::memcpy(&a, u.b.data(), 8);
        std::memcpy(&c, u.b.data() + 8, 8);
        return static_cast<size_t>(a ^ (c * 0x9E3779B97F4A7C15ULL));
    }
};

inline Uuid16 ReadUuid16(const uint8_t* p) {
    Uuid16 u;
    std::memcpy(u.b.data(), p, 16);
    return u;
}

// Convert Rhino Z-up (X, Y, Z) to Explorer2 Y-up (X, Z, -Y)
inline Vec3f RhinoToYUp(double x, double y, double z) {
    return { static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y) };
}

struct SubMeshPart {
    // Stored in Rhino Z-up space so ON_InstanceRef 4x4 transforms apply in native Rhino world coordinates!
    std::vector<std::array<double, 3>> vertsRhino;
    std::vector<std::array<uint32_t, 3>> tris;

    bool Empty() const { return vertsRhino.empty() || tris.empty(); }

    void AppendPart(const SubMeshPart& other, const double xform[16] = nullptr) {
        if (other.Empty()) return;
        uint32_t base = static_cast<uint32_t>(vertsRhino.size());
        vertsRhino.reserve(vertsRhino.size() + other.vertsRhino.size());
        for (const auto& v : other.vertsRhino) {
            if (xform) {
                double w = xform[12] * v[0] + xform[13] * v[1] + xform[14] * v[2] + xform[15];
                double invW = (std::fabs(w) > 1e-12) ? (1.0 / w) : 1.0;
                double nx = (xform[0] * v[0] + xform[1] * v[1] + xform[2]  * v[2] + xform[3])  * invW;
                double ny = (xform[4] * v[0] + xform[5] * v[1] + xform[6]  * v[2] + xform[7])  * invW;
                double nz = (xform[8] * v[0] + xform[9] * v[1] + xform[10] * v[2] + xform[11]) * invW;
                vertsRhino.push_back({ nx, ny, nz });
            } else {
                vertsRhino.push_back(v);
            }
        }
        tris.reserve(tris.size() + other.tris.size());
        for (const auto& t : other.tris) {
            tris.push_back({ base + t[0], base + t[1], base + t[2] });
        }
    }
};

// Cox-de Boor B-spline basis evaluation for order k (degree p = k - 1), n control points, full knot vector U[0..n+k-1]
inline void EvalBsplineBasis(int n, int order, const std::vector<double>& fullKnots, double t, std::vector<double>& outBasis) {
    outBasis.assign(static_cast<size_t>(n), 0.0);
    int p = order - 1;
    if (n <= 0 || order < 1 || static_cast<int>(fullKnots.size()) < n + order) return;

    double tMin = fullKnots[static_cast<size_t>(p)];
    double tMax = fullKnots[static_cast<size_t>(n)];
    if (t <= tMin) t = tMin;
    if (t >= tMax) {
        outBasis[static_cast<size_t>(n - 1)] = 1.0;
        return;
    }

    // Find knot span `span` in [p, n-1] such that U[span] <= t < U[span+1]
    int span = p;
    for (int i = p; i < n; ++i) {
        if (t >= fullKnots[static_cast<size_t>(i)] && t < fullKnots[static_cast<size_t>(i + 1)]) {
            span = i;
            break;
        }
    }

    std::vector<double> N, left, right;
    MeshBuild3D::BsplineBasisFuns(span, t, p, fullKnots, N, left, right);

    for (int j = 0; j <= p; ++j) {
        int idx = span - p + j;
        if (idx >= 0 && idx < n) outBasis[static_cast<size_t>(idx)] = N[static_cast<size_t>(j)];
    }
}

// Decode OpenNURBS ON_NurbsSurface payload (inside TCODE_OPENNURBS_CLASS_DATA)
inline bool DecodeOnNurbsSurfaceData(const uint8_t* data, size_t ds, size_t de, SubMeshPart& outPart) {
    // Layout: uint8 chunkVer (or int32 major, int32 minor when ver>=2), int32 dim, int32 is_rat,
    //         int32 order0, int32 order1, int32 cv_count0, int32 cv_count1, [optional int32 flags],
    //         double knot0[order0 + cv0 - 2], double knot1[order1 + cv1 - 2],
    //         double cv[cv0 * cv1 * cv_size]
    if (de - ds < 36) return false;
    size_t p = ds;

    // OpenNURBS class data begins with either 1-byte version (0x10 / 0x11) OR Write3dmChunkVersion (1 byte: (maj<<4)|min)
    uint8_t verByte = data[p++];
    int maj = (verByte >> 4) & 0x0F;
    if (maj == 0 || maj > 4) return false;

    if (p + 24 > de) return false;
    int32_t dim    = static_cast<int32_t>(RdU32LE(data + p + 0));
    int32_t isRat  = static_cast<int32_t>(RdU32LE(data + p + 4));
    int32_t order0 = static_cast<int32_t>(RdU32LE(data + p + 8));
    int32_t order1 = static_cast<int32_t>(RdU32LE(data + p + 12));
    int32_t cv0    = static_cast<int32_t>(RdU32LE(data + p + 16));
    int32_t cv1    = static_cast<int32_t>(RdU32LE(data + p + 20));
    p += 24;

    if (dim != 3 || (isRat != 0 && isRat != 1) ||
        order0 < 2 || order0 > 16 || order1 < 2 || order1 > 16 ||
        cv0 < order0 || cv0 > 512 || cv1 < order1 || cv1 > 512)
    {
        return false;
    }

    size_t kCnt0 = static_cast<size_t>(order0 + cv0 - 2);
    size_t kCnt1 = static_cast<size_t>(order1 + cv1 - 2);
    size_t cvStride = static_cast<size_t>(dim + (isRat ? 1 : 0));
    size_t cvDoubles = static_cast<size_t>(cv0) * static_cast<size_t>(cv1) * cvStride;
    size_t neededBytes = (kCnt0 + kCnt1 + cvDoubles) * 8u;

    // Note: OpenNURBS ON_NurbsSurface::Write writes a 4-byte `flags` = 0 int32 before knots!
    if (p + 4u + neededBytes <= de && RdU32LE(data + p) == 0u) {
        p += 4u;
    }
    if (p + neededBytes > de) return false;

    // Expand OpenNURBS superfluous-free knots (len = order + cv - 2) to standard full knots (len = order + cv)
    auto readExpandedKnots = [&](size_t onKnotCnt) -> std::vector<double> {
        std::vector<double> full(onKnotCnt + 2, 0.0);
        for (size_t i = 0; i < onKnotCnt; ++i) {
            full[i + 1] = RdF64LE(data + p + i * 8u);
        }
        p += onKnotCnt * 8u;
        full[0] = full[1];
        full[onKnotCnt + 1] = full[onKnotCnt];
        return full;
    };

    std::vector<double> knotsU = readExpandedKnots(kCnt0);
    std::vector<double> knotsV = readExpandedKnots(kCnt1);
    for (double kv : knotsU) if (!std::isfinite(kv)) return false;
    for (double kv : knotsV) if (!std::isfinite(kv)) return false;

    const uint8_t* cvBase = data + p;

    // Tessellate on a crisp 16x16 UV grid
    constexpr int kSegU = 16;
    constexpr int kSegV = 16;
    double uMin = knotsU[static_cast<size_t>(order0 - 1)];
    double uMax = knotsU[static_cast<size_t>(cv0)];
    double vMin = knotsV[static_cast<size_t>(order1 - 1)];
    double vMax = knotsV[static_cast<size_t>(cv1)];
    if (!(uMax > uMin) || !(vMax > vMin)) return false;

    uint32_t vStart = static_cast<uint32_t>(outPart.vertsRhino.size());
    std::vector<double> bU, bV;
    for (int iu = 0; iu <= kSegU; ++iu) {
        double u = uMin + (uMax - uMin) * (static_cast<double>(iu) / static_cast<double>(kSegU));
        EvalBsplineBasis(cv0, order0, knotsU, u, bU);
        for (int iv = 0; iv <= kSegV; ++iv) {
            double v = vMin + (vMax - vMin) * (static_cast<double>(iv) / static_cast<double>(kSegV));
            EvalBsplineBasis(cv1, order1, knotsV, v, bV);

            double sx = 0.0, sy = 0.0, sz = 0.0, sw = 0.0;
            for (int i = 0; i < cv0; ++i) {
                double nu = bU[static_cast<size_t>(i)];
                if (nu == 0.0) continue;
                for (int j = 0; j < cv1; ++j) {
                    double nv = bV[static_cast<size_t>(j)];
                    if (nv == 0.0) continue;
                    double basis = nu * nv;
                    const uint8_t* cp = cvBase + (static_cast<size_t>(i) * static_cast<size_t>(cv1) + static_cast<size_t>(j)) * cvStride * 8u;
                    double cx = RdF64LE(cp + 0);
                    double cy = RdF64LE(cp + 8);
                    double cz = RdF64LE(cp + 16);
                    if (isRat) {
                        // Note: OpenNURBS stores homogeneous control points (w*x, w*y, w*z, w)!
                        double cw = RdF64LE(cp + 24);
                        sx += basis * cx;
                        sy += basis * cy;
                        sz += basis * cz;
                        sw += basis * cw;
                    } else {
                        sx += basis * cx;
                        sy += basis * cy;
                        sz += basis * cz;
                        sw += basis;
                    }
                }
            }
            if (std::fabs(sw) > 1e-12) {
                sx /= sw; sy /= sw; sz /= sw;
            }
            if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz)) return false;
            outPart.vertsRhino.push_back({ sx, sy, sz });
        }
    }

    for (int iu = 0; iu < kSegU; ++iu) {
        for (int iv = 0; iv < kSegV; ++iv) {
            uint32_t i00 = vStart + static_cast<uint32_t>(iu * (kSegV + 1) + iv);
            uint32_t i10 = vStart + static_cast<uint32_t>((iu + 1) * (kSegV + 1) + iv);
            uint32_t i11 = vStart + static_cast<uint32_t>((iu + 1) * (kSegV + 1) + (iv + 1));
            uint32_t i01 = vStart + static_cast<uint32_t>(iu * (kSegV + 1) + (iv + 1));
            outPart.tris.push_back({ i00, i10, i11 });
            outPart.tris.push_back({ i00, i11, i01 });
        }
    }
    return true;
}

// Sweep a 2D profile polygon along an ON_Extrusion path (pathA -> pathB, with up vector)
inline bool BuildExtrusionFromProfile2D(
    const double pathA[3],
    const double pathB[3],
    const double upIn[3],
    bool capStart,
    bool capEnd,
    const std::vector<std::pair<double, double>>& prof2D,
    SubMeshPart& outPart)
{
    if (prof2D.size() < 3) return false;
    double tx = pathB[0] - pathA[0], ty = pathB[1] - pathA[1], tz = pathB[2] - pathA[2];
    double tLen = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (!(tLen > 1e-9) || !std::isfinite(tLen)) return false;
    tx /= tLen; ty /= tLen; tz /= tLen;

    // Orthonormal profile plane: Z = tangent (T), Y = orthonormalized Up, X = Y x Z (right-handed: X x Y = Z)
    double ux = upIn[0], uy = upIn[1], uz = upIn[2];
    double dotUT = ux * tx + uy * ty + uz * tz;
    ux -= dotUT * tx; uy -= dotUT * ty; uz -= dotUT * tz;
    double uLen = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (uLen < 1e-6) {
        // Fallback perpendicular axis
        if (std::fabs(tz) < 0.9) { ux = -ty; uy = tx; uz = 0.0; }
        else                     { ux = 0.0; uy = -tz; uz = ty; }
        uLen = std::sqrt(ux * ux + uy * uy + uz * uz);
    }
    ux /= uLen; uy /= uLen; uz /= uLen;
    // X = Y x Z
    double xx = uy * tz - uz * ty;
    double xy = uz * tx - ux * tz;
    double xz = ux * ty - uy * tx;

    // Strip duplicate closing vertex if closed polyline
    size_t n = prof2D.size();
    if (n >= 4 && std::hypot(prof2D.front().first - prof2D.back().first, prof2D.front().second - prof2D.back().second) < 1e-6) {
        --n;
    }
    if (n < 3) return false;

    uint32_t baseA = static_cast<uint32_t>(outPart.vertsRhino.size());
    for (size_t i = 0; i < n; ++i) {
        double px = prof2D[i].first, py = prof2D[i].second;
        outPart.vertsRhino.push_back({
            pathA[0] + xx * px + ux * py,
            pathA[1] + xy * px + uy * py,
            pathA[2] + xz * px + uz * py
        });
    }
    uint32_t baseB = static_cast<uint32_t>(outPart.vertsRhino.size());
    for (size_t i = 0; i < n; ++i) {
        double px = prof2D[i].first, py = prof2D[i].second;
        outPart.vertsRhino.push_back({
            pathB[0] + xx * px + ux * py,
            pathB[1] + xy * px + uy * py,
            pathB[2] + xz * px + uz * py
        });
    }

    // Side walls
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        uint32_t a0 = baseA + static_cast<uint32_t>(i);
        uint32_t a1 = baseA + static_cast<uint32_t>(j);
        uint32_t b0 = baseB + static_cast<uint32_t>(i);
        uint32_t b1 = baseB + static_cast<uint32_t>(j);
        outPart.tris.push_back({ a0, a1, b1 });
        outPart.tris.push_back({ a0, b1, b0 });
    }

    // Ear-clip 2D profile (arbitrary simple concave/convex polygon) for end caps!
    std::vector<uint32_t> idx(n);
    double signedArea = 0.0;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        signedArea += prof2D[i].first * prof2D[j].second - prof2D[j].first * prof2D[i].second;
        idx[i] = static_cast<uint32_t>(i);
    }
    if (signedArea < 0.0) std::reverse(idx.begin(), idx.end());

    auto cross2 = [&](uint32_t ia, uint32_t ib, uint32_t ic) -> double {
        double ax = prof2D[ib].first - prof2D[ia].first, ay = prof2D[ib].second - prof2D[ia].second;
        double bx = prof2D[ic].first - prof2D[ia].first, by = prof2D[ic].second - prof2D[ia].second;
        return ax * by - ay * bx;
    };
    auto ptInTri = [&](uint32_t p, uint32_t a, uint32_t b, uint32_t c) -> bool {
        return cross2(a, b, p) >= -1e-9 && cross2(b, c, p) >= -1e-9 && cross2(c, a, p) >= -1e-9;
    };

    std::vector<std::array<uint32_t, 3>> capTris;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 4096) {
        bool clipped = false;
        size_t m = idx.size();
        for (size_t i = 0; i < m; ++i) {
            uint32_t ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
            if (cross2(ia, ib, ic) <= 1e-9) continue;
            bool anyInside = false;
            for (size_t k = 0; k < m; ++k) {
                uint32_t ip = idx[k];
                if (ip == ia || ip == ib || ip == ic) continue;
                if (ptInTri(ip, ia, ib, ic)) { anyInside = true; break; }
            }
            if (!anyInside) {
                capTris.push_back({ ia, ib, ic });
                idx.erase(idx.begin() + static_cast<ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) capTris.push_back({ idx[0], idx[1], idx[2] });
    if (signedArea < 0.0) {
        for (auto& ct : capTris) std::swap(ct[1], ct[2]);
    }

    if (capStart) {
        for (const auto& ct : capTris) {
            outPart.tris.push_back({ baseA + ct[0], baseA + ct[2], baseA + ct[1] });
        }
    }
    if (capEnd) {
        for (const auto& ct : capTris) {
            outPart.tris.push_back({ baseB + ct[0], baseB + ct[1], baseB + ct[2] });
        }
    }
    return true;
}

} // namespace Rhino3dmDetail

inline bool ParseRhino3dmComplete(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Rhino3dmDetail;
    if (!data || size < 48 || std::memcmp(data, "3D Geometry File Format ", 24) != 0) return false;

    char verStr[9] = {};
    std::memcpy(verStr, data + 24, 8);
    int archiveVer = std::atoi(verStr);
    if (archiveVer <= 0) return false;

    const bool len64 = (archiveVer >= 5);
    const size_t chunkHdrSz = len64 ? 12u : 8u;

    auto readChunk = [&](size_t& pos, size_t limit, uint32_t& outTcode, int64_t& outValOrLen, size_t& outDataStart, size_t& outDataEnd) -> bool {
        if (pos + chunkHdrSz > limit) return false;
        outTcode = RdU32LE(data + pos);
        outValOrLen = len64 ? static_cast<int64_t>(RdU64LE(data + pos + 4))
                            : static_cast<int64_t>(static_cast<int32_t>(RdU32LE(data + pos + 4)));
        pos += chunkHdrSz;
        if ((outTcode & 0x80000000u) != 0u) {
            outDataStart = outDataEnd = pos;
            return true;
        }
        if (outValOrLen < 0 || static_cast<uint64_t>(outValOrLen) > limit - pos) return false;
        outDataStart = pos;
        size_t payloadLen = static_cast<size_t>(outValOrLen);
        pos += payloadLen;
        if ((outTcode & 0x00008000u) != 0u && payloadLen >= 4u) payloadLen -= 4u;
        outDataEnd = outDataStart + payloadLen;
        return true;
    };

    auto readOnCompressedArray = [&](size_t& p, size_t end, size_t expectedBytes, std::vector<uint8_t>& outBuf) -> bool {
        if (p + 9 > end) return false;
        p += 8; // elemSz(4) + crc(4)
        uint8_t method = data[p++];
        if (method == 0u) {
            if (p + expectedBytes > end) return false;
            outBuf.assign(data + p, data + p + expectedBytes);
            p += expectedBytes;
            return true;
        }
        if (method == 1u) {
            uint32_t tc = 0; int64_t cl = 0; size_t ds = 0, de = 0;
            if (!readChunk(p, end, tc, cl, ds, de) || ds >= de) return false;
            return ZipDeflate::InflateZlib(data + ds, de - ds, outBuf, expectedBytes + 64u) && outBuf.size() >= expectedBytes;
        }
        return false;
    };

    // Standard OpenNURBS Class UUIDs (mixed-endian Win32 GUID layout on disk)
    static constexpr uint8_t kUuidMesh[16]        = { 0xBD,0xDB,0xB5,0x60, 0x60,0xE6, 0xD3,0x11, 0xBF,0xE4, 0x00,0x10,0x83,0x01,0x22,0xF0 };
    static constexpr uint8_t kUuidPolyline[16]    = { 0xE1,0xD4,0xD7,0x4E, 0x47,0xE9, 0xD3,0x11, 0xBF,0xE5, 0x00,0x10,0x83,0x01,0x22,0xF0 };
    static constexpr uint8_t kUuidNurbsSurf[16]   = { 0xDE,0xD4,0xD7,0x4E, 0x47,0xE9, 0xD3,0x11, 0xBF,0xE5, 0x00,0x10,0x83,0x01,0x22,0xF0 };
    static constexpr uint8_t kUuidBrep[16]        = { 0xBF,0xDB,0xB5,0x60, 0x60,0xE6, 0xD3,0x11, 0xBF,0xE4, 0x00,0x10,0x83,0x01,0x22,0xF0 };
    static constexpr uint8_t kUuidExtrusion[16]   = { 0x8C,0x59,0xBB,0x34, 0xF5,0xF9, 0x3B,0x48, 0xBD,0x2D, 0xF6,0xE4,0xB7,0x34,0x12,0xDF };
    static constexpr uint8_t kUuidInstanceDef[16] = { 0x46,0x4B,0x90,0x26, 0x13,0xEE, 0xDA,0x41, 0xAB,0x16, 0xF7,0x6F,0x1C,0x0E,0x20,0x3F };
    static constexpr uint8_t kUuidInstanceRef[16] = { 0x3E,0x97,0xF6,0xF5, 0xC7,0x57, 0xC3,0x42, 0x9E,0x09, 0x69,0x91,0x3F,0x62,0x9C,0x3E };

    struct InstanceRefRecord {
        Uuid16 idefUuid;
        double xform[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    };

    struct SceneObject {
        Uuid16 objUuid;
        uint8_t objMode = 0; // 0 = normal_object, 1 = hidden, 2 = idef_object (template for block instance!)
        SubMeshPart geom;
        bool isInstanceRef = false;
        InstanceRefRecord iref;
    };

    std::unordered_map<Uuid16, std::vector<Uuid16>, Uuid16Hash> idefMembers;
    std::vector<SceneObject> objects;

    // Recursive OpenNURBS class decoder (extracts geometry OR 2D profile curves OR InstanceRef)
    std::function<void(size_t, size_t, SubMeshPart&, std::vector<std::pair<double, double>>*, InstanceRefRecord*, bool*)> decodeClass;
    decodeClass = [&](size_t cStart, size_t cEnd, SubMeshPart& dstPart,
                      std::vector<std::pair<double, double>>* outCurve2D,
                      InstanceRefRecord* outIref, bool* outIsIref)
    {
        size_t p = cStart;
        uint8_t uuid[16] = {};
        bool hasUuid = false;

        while (p + chunkHdrSz <= cEnd) {
            uint32_t tc = 0; int64_t vl = 0; size_t ds = 0, de = 0;
            if (!readChunk(p, cEnd, tc, vl, ds, de)) break;
            uint32_t baseTc = tc & 0x7FFF7FFFu;

            if (baseTc == 0x00027FF1u && de - ds >= 16u) { // TCODE_OPENNURBS_CLASS_UUID
                std::memcpy(uuid, data + ds, 16);
                hasUuid = true;
            } else if (baseTc == 0x00027FF2u && hasUuid) { // TCODE_OPENNURBS_CLASS_DATA
                // 1. ON_Mesh
                if (std::memcmp(uuid, kUuidMesh, 16) == 0 && de - ds >= 9u) {
                    size_t mp = ds;
                    uint8_t verByte = data[mp++];
                    int maj = (verByte >> 4) & 0x0F;
                    if (maj >= 1 && mp + 8u <= de) {
                        int32_t vcount = static_cast<int32_t>(RdU32LE(data + mp)); mp += 4;
                        int32_t fcount = static_cast<int32_t>(RdU32LE(data + mp)); mp += 4;
                        if (vcount >= 3 && vcount <= 2000000 && fcount >= 1 && fcount <= 4000000) {
                            std::vector<std::array<double, 3>> mVerts;
                            std::vector<std::array<int32_t, 4>> mFaces;
                            size_t scan = mp;
                            while (scan + chunkHdrSz <= de) {
                                size_t tryP = scan;
                                uint32_t stc = 0; int64_t svl = 0; size_t sds = 0, sde = 0;
                                if (readChunk(tryP, de, stc, svl, sds, sde)) {
                                    uint32_t sBase = stc & 0x7FFF7FFFu;
                                    if (sBase == 0x00049001u && sde - sds >= static_cast<size_t>(vcount) * 12u) {
                                        mVerts.resize(static_cast<size_t>(vcount));
                                        for (int32_t v = 0; v < vcount; ++v) {
                                            mVerts[static_cast<size_t>(v)] = {
                                                RdF32LE(data + sds + static_cast<size_t>(v) * 12u + 0),
                                                RdF32LE(data + sds + static_cast<size_t>(v) * 12u + 4),
                                                RdF32LE(data + sds + static_cast<size_t>(v) * 12u + 8)
                                            };
                                        }
                                        scan = tryP; continue;
                                    } else if (sBase == 0x00049002u && sde - sds >= static_cast<size_t>(fcount) * 16u) {
                                        mFaces.resize(static_cast<size_t>(fcount));
                                        for (int32_t f = 0; f < fcount; ++f) {
                                            for (int k = 0; k < 4; ++k) {
                                                mFaces[static_cast<size_t>(f)][static_cast<size_t>(k)] =
                                                    static_cast<int32_t>(RdU32LE(data + sds + static_cast<size_t>(f) * 16u + static_cast<size_t>(k) * 4u));
                                            }
                                        }
                                        scan = tryP; continue;
                                    }
                                }
                                ++scan;
                                if (scan - mp > 1024u && mVerts.empty()) break;
                            }
                            if (mVerts.empty() && mp + 580u <= de) {
                                size_t cp = mp + 580u;
                                std::vector<uint8_t> fBuf, vBuf;
                                if (readOnCompressedArray(cp, de, static_cast<size_t>(fcount) * 16u, fBuf) &&
                                    readOnCompressedArray(cp, de, static_cast<size_t>(vcount) * 12u, vBuf))
                                {
                                    mVerts.resize(static_cast<size_t>(vcount));
                                    for (int32_t v = 0; v < vcount; ++v) {
                                        mVerts[static_cast<size_t>(v)] = {
                                            RdF32LE(vBuf.data() + static_cast<size_t>(v) * 12u + 0),
                                            RdF32LE(vBuf.data() + static_cast<size_t>(v) * 12u + 4),
                                            RdF32LE(vBuf.data() + static_cast<size_t>(v) * 12u + 8)
                                        };
                                    }
                                    mFaces.resize(static_cast<size_t>(fcount));
                                    for (int32_t f = 0; f < fcount; ++f) {
                                        for (int k = 0; k < 4; ++k) {
                                            mFaces[static_cast<size_t>(f)][static_cast<size_t>(k)] =
                                                static_cast<int32_t>(RdU32LE(fBuf.data() + static_cast<size_t>(f) * 16u + static_cast<size_t>(k) * 4u));
                                        }
                                    }
                                }
                            }
                            if (!mVerts.empty() && !mFaces.empty()) {
                                uint32_t vBase = static_cast<uint32_t>(dstPart.vertsRhino.size());
                                dstPart.vertsRhino.insert(dstPart.vertsRhino.end(), mVerts.begin(), mVerts.end());
                                for (const auto& fc : mFaces) {
                                    if (fc[0] >= 0 && fc[1] >= 0 && fc[2] >= 0 && fc[3] >= 0 &&
                                        fc[0] < vcount && fc[1] < vcount && fc[2] < vcount && fc[3] < vcount)
                                    {
                                        dstPart.tris.push_back({ vBase + static_cast<uint32_t>(fc[0]), vBase + static_cast<uint32_t>(fc[1]), vBase + static_cast<uint32_t>(fc[2]) });
                                        if (fc[2] != fc[3]) {
                                            dstPart.tris.push_back({ vBase + static_cast<uint32_t>(fc[0]), vBase + static_cast<uint32_t>(fc[2]), vBase + static_cast<uint32_t>(fc[3]) });
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                // 2. ON_PolylineCurve (standalone 3D wireframe OR 2D profile inside ON_Extrusion)
                else if (std::memcmp(uuid, kUuidPolyline, 16) == 0 && de - ds >= 13u) {
                    size_t pp = ds + 1u;
                    int32_t dim = static_cast<int32_t>(RdU32LE(data + pp)); pp += 4u;
                    int32_t cnt = static_cast<int32_t>(RdU32LE(data + pp)); pp += 4u;
                    if ((dim == 2 || dim == 3) && cnt >= 2 && cnt <= 65536 && pp + static_cast<size_t>(cnt * dim) * 8u <= de) {
                        if (outCurve2D) {
                            for (int32_t i = 0; i < cnt; ++i) {
                                double x = RdF64LE(data + pp + static_cast<size_t>(i * dim) * 8u + 0);
                                double y = RdF64LE(data + pp + static_cast<size_t>(i * dim) * 8u + 8);
                                outCurve2D->push_back({ x, y });
                            }
                        } else {
                            LoadedMesh3D tmpStruts;
                            std::vector<Vec3f> pts(static_cast<size_t>(cnt));
                            for (int32_t i = 0; i < cnt; ++i) {
                                double x = RdF64LE(data + pp + static_cast<size_t>(i * dim) * 8u + 0);
                                double y = RdF64LE(data + pp + static_cast<size_t>(i * dim) * 8u + 8);
                                double z = (dim == 3) ? RdF64LE(data + pp + static_cast<size_t>(i * dim) * 8u + 16) : 0.0;
                                pts[static_cast<size_t>(i)] = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
                            }
                            for (int32_t i = 0; i + 1 < cnt; ++i) {
                                Detail3D100::AddLineStrut(tmpStruts, pts[static_cast<size_t>(i)], pts[static_cast<size_t>(i + 1)], 0.03f);
                            }
                            uint32_t b = static_cast<uint32_t>(dstPart.vertsRhino.size());
                            for (const auto& v : tmpStruts.vertices) dstPart.vertsRhino.push_back({ v.x, v.y, v.z });
                            for (const auto& t : tmpStruts.tris) dstPart.tris.push_back({ b + t.i0, b + t.i1, b + t.i2 });
                        }
                    }
                }
                // 3. ON_NurbsSurface (rational / non-rational tensor-product B-spline surface!)
                else if (std::memcmp(uuid, kUuidNurbsSurf, 16) == 0) {
                    DecodeOnNurbsSurfaceData(data, ds, de, dstPart);
                }
                // 4. ON_Extrusion (path line + up vector + caps + nested 2D profile TCODE_OPENNURBS_CLASS!)
                else if (std::memcmp(uuid, kUuidExtrusion, 16) == 0 && de - ds >= 92u) {
                    // Header: verByte(1B), pathFrom(double3: 24B), pathTo(double3: 24B), t0,t1(double2: 16B),
                    //         up(double3: 24B), bHaveN[2](2B), [optional N[2] 48B], bCap[2](2B), then nested TCODE_OPENNURBS_CLASS profile!
                    size_t ep = ds + 1u;
                    double pathA[3] = { RdF64LE(data + ep + 0),  RdF64LE(data + ep + 8),  RdF64LE(data + ep + 16) };
                    double pathB[3] = { RdF64LE(data + ep + 24), RdF64LE(data + ep + 32), RdF64LE(data + ep + 40) };
                    double upV[3]   = { RdF64LE(data + ep + 64), RdF64LE(data + ep + 72), RdF64LE(data + ep + 80) };
                    ep += 88u;
                    bool cap0 = true, cap1 = true;
                    if (ep + 2u <= de) {
                        uint8_t haveN0 = data[ep++], haveN1 = data[ep++];
                        if (haveN0 <= 1u && haveN1 <= 1u) {
                            if (haveN0) ep += 24u;
                            if (haveN1) ep += 24u;
                            if (ep + 2u <= de && data[ep] <= 1u && data[ep + 1] <= 1u) {
                                cap0 = (data[ep] != 0u);
                                cap1 = (data[ep + 1] != 0u);
                                ep += 2u;
                            }
                        }
                    }

                    // Scan inside ON_Extrusion CLASS_DATA for nested TCODE_OPENNURBS_CLASS (0x00027FF0) profile or cached ON_Mesh
                    std::vector<std::pair<double, double>> prof2D;
                    size_t scan = ep;
                    while (scan + chunkHdrSz <= de) {
                        size_t tryP = scan;
                        uint32_t stc = 0; int64_t svl = 0; size_t sds = 0, sde = 0;
                        if (readChunk(tryP, de, stc, svl, sds, sde) && (stc & 0x7FFF7FFFu) == 0x00027FF0u && sde > sds) {
                            decodeClass(sds, sde, dstPart, &prof2D, nullptr, nullptr);
                            scan = tryP;
                            continue;
                        }
                        ++scan;
                    }
                    if (!prof2D.empty()) {
                        BuildExtrusionFromProfile2D(pathA, pathB, upV, cap0, cap1, prof2D, dstPart);
                    }
                }
                // 5. ON_Brep (embedded render ON_Mesh AND/OR nested m_S ON_NurbsSurface chunks!)
                else if (std::memcmp(uuid, kUuidBrep, 16) == 0 && de > ds + chunkHdrSz) {
                    SubMeshPart brepRenderMeshes;
                    SubMeshPart brepSurfaces;
                    // Scan inside ON_Brep CLASS_DATA for nested TCODE_OPENNURBS_CLASS chunks (0x00027FF0):
                    // ON_Brep::Write serializes m_S surfaces AND face render meshes as standard TCODE_OPENNURBS_CLASS chunks!
                    size_t scan = ds + 1u;
                    while (scan + chunkHdrSz <= de) {
                        size_t tryP = scan;
                        uint32_t stc = 0; int64_t svl = 0; size_t sds = 0, sde = 0;
                        if (readChunk(tryP, de, stc, svl, sds, sde) && (stc & 0x7FFF7FFFu) == 0x00027FF0u && sde > sds + chunkHdrSz) {
                            // Peek inner class UUID to prefer cached trimmed render ON_Mesh when present, else surface tessellation
                            size_t ip = sds;
                            uint32_t itc = 0; int64_t ivl = 0; size_t ids = 0, ide = 0;
                            if (readChunk(ip, sde, itc, ivl, ids, ide) && (itc & 0x7FFF7FFFu) == 0x00027FF1u && ide - ids >= 16u) {
                                if (std::memcmp(data + ids, kUuidMesh, 16) == 0) {
                                    decodeClass(sds, sde, brepRenderMeshes, nullptr, nullptr, nullptr);
                                } else {
                                    decodeClass(sds, sde, brepSurfaces, nullptr, nullptr, nullptr);
                                }
                                scan = tryP;
                                continue;
                            }
                        }
                        ++scan;
                    }
                    if (!brepRenderMeshes.Empty()) dstPart.AppendPart(brepRenderMeshes);
                    else if (!brepSurfaces.Empty()) dstPart.AppendPart(brepSurfaces);
                }
                // 6. ON_InstanceRef (UUID + 16 doubles row-major ON_Xform = 145 bytes)
                else if (std::memcmp(uuid, kUuidInstanceRef, 16) == 0 && de - ds >= 145u && outIref && outIsIref) {
                    size_t rp = ds + 1u; // chunk version byte
                    outIref->idefUuid = ReadUuid16(data + rp);
                    rp += 16u;
                    bool finiteMat = true;
                    for (int m = 0; m < 16; ++m) {
                        outIref->xform[m] = RdF64LE(data + rp + static_cast<size_t>(m) * 8u);
                        if (!std::isfinite(outIref->xform[m])) finiteMat = false;
                    }
                    if (finiteMat && !outIref->idefUuid.IsZero()) {
                        *outIsIref = true;
                    }
                }
            }
            if ((tc & 0x7FFFFFFFu) == 0x00027FFFu) break;
        }
    };

    // Walk top-level 3DM tables
    size_t pos = 32u;
    while (pos + chunkHdrSz <= size) {
        uint32_t tc = 0; int64_t vl = 0; size_t ds = 0, de = 0;
        if (!readChunk(pos, size, tc, vl, ds, de)) break;

        // TCODE_INSTANCE_DEFINITION_TABLE (0x20000019)
        if (tc == 0x20000019u) {
            size_t ip = ds;
            while (ip + chunkHdrSz <= de) {
                uint32_t itc = 0; int64_t ivl = 0; size_t ids = 0, ide = 0;
                if (!readChunk(ip, de, itc, ivl, ids, ide)) break;
                if (itc == 0x10000019u) { // TCODE_INSTANCE_DEFINITION_RECORD
                    // Inside: TCODE_OPENNURBS_CLASS for ON_InstanceDefinition (verByte + idefUuid(16B) + uint32 count + memberUuids[count])
                    size_t rp = ids;
                    while (rp + chunkHdrSz <= ide) {
                        uint32_t rtc = 0; int64_t rvl = 0; size_t rds = 0, rde = 0;
                        if (!readChunk(rp, ide, rtc, rvl, rds, rde)) break;
                        if ((rtc & 0x7FFF7FFFu) == 0x00027FF0u) {
                            size_t cp = rds;
                            Uuid16 clsId;
                            while (cp + chunkHdrSz <= rde) {
                                uint32_t ctc = 0; int64_t cvl = 0; size_t cds = 0, cde = 0;
                                if (!readChunk(cp, rde, ctc, cvl, cds, cde)) break;
                                if ((ctc & 0x7FFF7FFFu) == 0x00027FF1u && cde - cds >= 16u) {
                                    clsId = ReadUuid16(data + cds);
                                } else if ((ctc & 0x7FFF7FFFu) == 0x00027FF2u &&
                                           std::memcmp(clsId.b.data(), kUuidInstanceDef, 16) == 0 && cde - cds >= 21u)
                                {
                                    Uuid16 defUuid = ReadUuid16(data + cds + 1u);
                                    uint32_t nObj = RdU32LE(data + cds + 17u);
                                    if (nObj > 0 && nObj <= 65536u && cds + 21u + static_cast<size_t>(nObj) * 16u <= cde) {
                                        auto& vec = idefMembers[defUuid];
                                        for (uint32_t k = 0; k < nObj; ++k) {
                                            vec.push_back(ReadUuid16(data + cds + 21u + static_cast<size_t>(k) * 16u));
                                        }
                                    }
                                }
                            }
                        }
                        if (rtc == 0x80007FFFu) break;
                    }
                }
                if (itc == 0x80007FFFu) break;
            }
        }
        // TCODE_OBJECT_TABLE (0x20000013)
        else if (tc == 0x20000013u) {
            size_t op = ds;
            while (op + chunkHdrSz <= de) {
                uint32_t otc = 0; int64_t ovl = 0; size_t ods = 0, ode = 0;
                if (!readChunk(op, de, otc, ovl, ods, ode)) break;
                if (otc == 0x10000013u) { // TCODE_OBJECT_RECORD
                    SceneObject sobj;
                    size_t rp = ods;
                    while (rp + chunkHdrSz <= ode) {
                        uint32_t rtc = 0; int64_t rvl = 0; size_t rds = 0, rde = 0;
                        if (!readChunk(rp, ode, rtc, rvl, rds, rde)) break;
                        uint32_t rBase = rtc & 0x7FFF7FFFu;
                        if (rBase == 0x00027FF0u) {
                            decodeClass(rds, rde, sobj.geom, nullptr, &sobj.iref, &sobj.isInstanceRef);
                        } else if (rBase == 0x10000015u && rde - rds >= 18u) {
                            // TCODE_OBJECT_RECORD_ATTRIBUTES (verByte(1B) + m_uuid(16B) + m_mode(1B))
                            sobj.objUuid = ReadUuid16(data + rds + 1u);
                            sobj.objMode = data[rds + 17u];
                        }
                        if (rtc == 0x80007FFFu) break;
                    }
                    if (!sobj.geom.Empty() || sobj.isInstanceRef) {
                        objects.push_back(std::move(sobj));
                    }
                }
                if (otc == 0x80007FFFu) break;
            }
        }
        if ((tc & 0x7FFFFFFFu) == 0x00007FFFu) break;
    }

    // Build lookup by object UUID for block instantiation
    std::unordered_map<Uuid16, size_t, Uuid16Hash> uuidToObjIdx;
    std::unordered_map<Uuid16, uint8_t, Uuid16Hash> referencedAsMember;
    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].objUuid.IsZero()) uuidToObjIdx[objects[i].objUuid] = i;
    }
    for (const auto& kv : idefMembers) {
        for (const Uuid16& mId : kv.second) referencedAsMember[mId] = 1u;
    }

    SubMeshPart finalScene;
    std::function<void(const SceneObject&, const double*, int)> emitObject;
    emitObject = [&](const SceneObject& obj, const double* parentMat, int depth) {
        if (depth > 8) return;
        if (obj.isInstanceRef) {
            auto itDef = idefMembers.find(obj.iref.idefUuid);
            if (itDef == idefMembers.end()) return;
            double combined[16];
            if (parentMat) {
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        combined[r * 4 + c] =
                            parentMat[r * 4 + 0] * obj.iref.xform[0 * 4 + c] +
                            parentMat[r * 4 + 1] * obj.iref.xform[1 * 4 + c] +
                            parentMat[r * 4 + 2] * obj.iref.xform[2 * 4 + c] +
                            parentMat[r * 4 + 3] * obj.iref.xform[3 * 4 + c];
                    }
                }
            } else {
                std::memcpy(combined, obj.iref.xform, sizeof(combined));
            }
            for (const Uuid16& memId : itDef->second) {
                auto itMem = uuidToObjIdx.find(memId);
                if (itMem != uuidToObjIdx.end()) {
                    emitObject(objects[itMem->second], combined, depth + 1);
                }
            }
        } else if (!obj.geom.Empty()) {
            finalScene.AppendPart(obj.geom, parentMat);
        }
    };

    bool anyNonIdefEmitted = false;
    for (const auto& obj : objects) {
        if (obj.objMode == 2u || (!obj.objUuid.IsZero() && referencedAsMember.count(obj.objUuid) > 0 && !idefMembers.empty())) {
            continue; // template geometry owned by an ON_InstanceDefinition
        }
        size_t before = finalScene.tris.size();
        emitObject(obj, nullptr, 0);
        if (finalScene.tris.size() > before) anyNonIdefEmitted = true;
    }
    if (!anyNonIdefEmitted) {
        for (const auto& obj : objects) {
            if (!obj.geom.Empty()) finalScene.AppendPart(obj.geom, nullptr);
        }
    }

    if (finalScene.Empty()) return false;
    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.vertices.reserve(finalScene.vertsRhino.size());
    for (const auto& v : finalScene.vertsRhino) {
        mesh.vertices.push_back(RhinoToYUp(v[0], v[1], v[2]));
    }
    mesh.tris.reserve(finalScene.tris.size());
    for (const auto& t : finalScene.tris) {
        if (t[0] < mesh.vertices.size() && t[1] < mesh.vertices.size() && t[2] < mesh.vertices.size()) {
            mesh.tris.push_back({ t[0], t[1], t[2], {} });
        }
    }
    mesh.formatName = L"Rhinoceros 3D OpenNURBS (.3dm)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
