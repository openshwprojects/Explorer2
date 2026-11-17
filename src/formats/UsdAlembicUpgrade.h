#pragma once
#include "../ByteIO.h"
// UsdAlembicUpgrade.h — #17 Pixar OpenUSD (.usd, .usda, .usdc, .usdz) & Alembic Ogawa (.abc)
//                       Full Xform Scene-Graph Hierarchy, Sublayer/USDZ References & PointInstancer
//
// Pure C++20 zero-cheat implementation:
//   1. OpenUSD ASCII (.usda / .usd): recursive Prim scene graph (Xform, Scope, Mesh, Cube, Sphere, Cylinder,
//      Cone, PointInstancer), ordered xformOp stack (transform, translate, rotateXYZ/X/Y/Z, orient, scale,
//      !resetXformStack!), cross-layer @asset@ references, and PointInstancer (prototypes, protoIndices,
//      positions, orientations, scales, invisibleIds).
//   2. OpenUSD Binary Crate ("PXR-USDC"): full PATHS + SPECS prim hierarchy, inherited xformOp matrices/vectors,
//      and binary PointInstancer evaluation (including PathListOp / PathVector prototype relationships and
//      Quath/Quatf/Quatd instance orientations).
//   3. OpenUSD Package (.usdz): multi-entry stage table resolving cross-file references between .usda and .usdc.
//   4. Alembic Ogawa (.abc): full AbcGeom_Xform_v3 (.xform: .ops, .vals, .inherits) hierarchical world-matrix
//      composition and Ogawa DAG multi-parent instancing (ancestor-cycle guard instead of global visited filter).

namespace Formats {

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

struct UsdLocalMesh {
    std::vector<Vec3f> verts;
    std::vector<std::array<uint32_t, 3>> tris;
};

inline void AppendTransformedLocalMesh(const UsdLocalMesh& src, const Mat4& xf, LoadedMesh3D& dst) {
    if (src.verts.empty() || src.tris.empty()) return;
    bool flip = (xf.Det3x3() < 0.0f);
    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + src.verts.size());
    for (const Vec3f& v : src.verts) {
        Vec3f p = xf.TransformPoint(v);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) p = { 0, 0, 0 };
        dst.vertices.push_back(p);
    }
    dst.tris.reserve(dst.tris.size() + src.tris.size());
    for (const auto& t : src.tris) {
        if (flip) dst.tris.push_back({ base + t[0], base + t[2], base + t[1], {} });
        else      dst.tris.push_back({ base + t[0], base + t[1], base + t[2], {} });
    }
}

inline void AppendLocalToLocal(const UsdLocalMesh& src, const Mat4& xf, UsdLocalMesh& dst) {
    if (src.verts.empty() || src.tris.empty()) return;
    bool flip = (xf.Det3x3() < 0.0f);
    uint32_t base = static_cast<uint32_t>(dst.verts.size());
    for (const Vec3f& v : src.verts) dst.verts.push_back(xf.TransformPoint(v));
    for (const auto& t : src.tris) {
        if (flip) dst.tris.push_back({ base + t[0], base + t[2], base + t[1] });
        else      dst.tris.push_back({ base + t[0], base + t[1], base + t[2] });
    }
}

inline UsdLocalMesh BuildPolygonalUsdMesh(
    const std::vector<Vec3f>& pts,
    const std::vector<int32_t>& counts,
    const std::vector<int32_t>& indices)
{
    UsdLocalMesh out;
    if (pts.empty() || indices.empty()) return out;
    out.verts = pts;
    if (counts.empty()) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            int32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            if (a >= 0 && b >= 0 && c >= 0 &&
                static_cast<size_t>(a) < pts.size() &&
                static_cast<size_t>(b) < pts.size() &&
                static_cast<size_t>(c) < pts.size())
            {
                out.tris.push_back({ static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c) });
            }
        }
    } else {
        size_t k = 0;
        for (int32_t cnt : counts) {
            if (cnt >= 3 && k + static_cast<size_t>(cnt) <= indices.size()) {
                int32_t i0 = indices[k];
                if (i0 >= 0 && static_cast<size_t>(i0) < pts.size()) {
                    for (int32_t t = 1; t + 1 < cnt; ++t) {
                        int32_t i1 = indices[k + t], i2 = indices[k + t + 1];
                        if (i1 >= 0 && i2 >= 0 &&
                            static_cast<size_t>(i1) < pts.size() &&
                            static_cast<size_t>(i2) < pts.size())
                        {
                            out.tris.push_back({ static_cast<uint32_t>(i0), static_cast<uint32_t>(i1), static_cast<uint32_t>(i2) });
                        }
                    }
                }
            }
            if (cnt > 0) k += static_cast<size_t>(cnt);
        }
    }
    return out;
}

inline UsdLocalMesh BuildUsdCubePrim(float size) {
    UsdLocalMesh m;
    float h = 0.5f * ((size > 1e-6f) ? size : 2.0f);
    m.verts = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}
    };
    static const uint32_t q[6][4] = {
        {0,3,2,1}, {4,5,6,7}, {0,1,5,4}, {2,3,7,6}, {0,4,7,3}, {1,2,6,5}
    };
    for (int f = 0; f < 6; ++f) {
        m.tris.push_back({ q[f][0], q[f][1], q[f][2] });
        m.tris.push_back({ q[f][0], q[f][2], q[f][3] });
    }
    return m;
}

inline UsdLocalMesh BuildUsdSpherePrim(float radius) {
    UsdLocalMesh m;
    float r = (radius > 1e-6f) ? radius : 1.0f;
    const int nLat = 10, nLon = 16;
    for (int i = 0; i <= nLat; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(nLat);
        float phi = (v - 0.5f) * 3.14159265f;
        float cp = std::cos(phi), sp = std::sin(phi);
        for (int j = 0; j <= nLon; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(nLon);
            float th = u * 6.2831853f;
            m.verts.push_back({ r * cp * std::cos(th), r * sp, r * cp * std::sin(th) });
        }
    }
    for (int i = 0; i < nLat; ++i) {
        for (int j = 0; j < nLon; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (nLon + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = a + static_cast<uint32_t>(nLon + 1);
            uint32_t d = c + 1;
            m.tris.push_back({ a, c, b });
            m.tris.push_back({ b, c, d });
        }
    }
    return m;
}

// ============================================================================
// Scene-Graph Representation Shared by USDA & Binary Crate (PXR-USDC)
// ============================================================================
struct UsdPrimNode {
    std::string name;
    std::string fullPath;
    std::string specifier = "def"; // "def", "over", "class"
    std::string typeName;          // "Xform", "Mesh", "PointInstancer", "Cube", "Sphere", "Scope", ...
    bool resetXformStack = false;
    Mat4 localXform = Mat4::Identity();

    UsdLocalMesh geom;
    std::vector<std::string> references; // @./sub.usda@ asset paths

    // PointInstancer fields
    std::vector<std::string> protoPaths;
    std::vector<int32_t> protoIndices;
    std::vector<Vec3f> piPositions;
    std::vector<std::array<float, 4>> piOrientsWXYZ; // (w, x, y, z)
    std::vector<Vec3f> piScales;
    std::set<int64_t> piInvisibleIds;

    std::vector<int> children; // indices into stage nodes vector
};

// Strips comments (# outside quotes) from USDA text so parser sees clean tokens
inline std::string StripUsdaComments(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    bool inStr = false;
    char quoteCh = 0;
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (inStr) {
            out.push_back(c);
            if (c == '\\' && i + 1 < sv.size()) {
                out.push_back(sv[++i]);
            } else if (c == quoteCh) {
                inStr = false;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            inStr = true;
            quoteCh = c;
            out.push_back(c);
        } else if (c == '#') {
            while (i < sv.size() && sv[i] != '\n') ++i;
            out.push_back('\n');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

inline void ParseUsdaPrimBodyAttributes(std::string_view bodyNoChildren, UsdPrimNode& node) {
    // 1. Parse xformOp attributes & xformOpOrder
    std::unordered_map<std::string, Mat4> opMats;
    std::vector<std::string> implicitOrder;

    auto parseTupleAfterEq = [](std::string_view s, size_t eqPos, std::vector<double>& vals) {
        vals.clear();
        size_t lp = s.find('(', eqPos);
        if (lp == std::string_view::npos) return;
        int d = 0;
        size_t rp = lp;
        for (; rp < s.size(); ++rp) {
            if (s[rp] == '(') ++d;
            else if (s[rp] == ')') { if (--d == 0) break; }
        }
        if (rp >= s.size()) return;
        std::string_view blk = s.substr(lp, rp - lp + 1);
        const char* cur = blk.data();
        const char* end = blk.data() + blk.size();
        while (cur < end) {
            while (cur < end && !((*cur >= '0' && *cur <= '9') || *cur == '-' || *cur == '+' || *cur == '.')) ++cur;
            if (cur >= end) break;
            char* ep = nullptr;
            double v = std::strtod(cur, &ep);
            if (ep == cur) { ++cur; continue; }
            vals.push_back(v);
            cur = ep;
        }
    };

    size_t xp = 0;
    while ((xp = bodyNoChildren.find("xformOp:", xp)) != std::string_view::npos) {
        size_t ns = xp;
        size_t ne = xp;
        while (ne < bodyNoChildren.size() &&
               (std::isalnum(static_cast<unsigned char>(bodyNoChildren[ne])) ||
                bodyNoChildren[ne] == ':' || bodyNoChildren[ne] == '_')) {
            ++ne;
        }
        std::string opName(bodyNoChildren.substr(ns, ne - ns));
        // Ignore if inside quotes (i.e. inside xformOpOrder = ["xformOp:..."])
        bool inQuotes = (ns > 0 && bodyNoChildren[ns - 1] == '"');
        size_t eq = bodyNoChildren.find('=', ne);
        size_t nl = bodyNoChildren.find('\n', ne);
        if (!inQuotes && eq != std::string_view::npos && (nl == std::string_view::npos || eq < nl)) {
            std::vector<double> vals;
            if (opName.rfind("xformOp:rotateX", 0) == 0 && opName.rfind("xformOp:rotateXYZ", 0) != 0) {
                char* ep = nullptr;
                double deg = std::strtod(bodyNoChildren.data() + eq + 1, &ep);
                opMats[opName] = Mat4::RotateAxisAngle(static_cast<float>(deg), 1, 0, 0);
                implicitOrder.push_back(opName);
            } else if (opName.rfind("xformOp:rotateY", 0) == 0) {
                char* ep = nullptr;
                double deg = std::strtod(bodyNoChildren.data() + eq + 1, &ep);
                opMats[opName] = Mat4::RotateAxisAngle(static_cast<float>(deg), 0, 1, 0);
                implicitOrder.push_back(opName);
            } else if (opName.rfind("xformOp:rotateZ", 0) == 0) {
                char* ep = nullptr;
                double deg = std::strtod(bodyNoChildren.data() + eq + 1, &ep);
                opMats[opName] = Mat4::RotateAxisAngle(static_cast<float>(deg), 0, 0, 1);
                implicitOrder.push_back(opName);
            } else {
                parseTupleAfterEq(bodyNoChildren, eq + 1, vals);
                if (opName.rfind("xformOp:transform", 0) == 0 && vals.size() >= 16) {
                    opMats[opName] = RowVecMatrix16ToMat4(vals.data());
                    implicitOrder.push_back(opName);
                } else if (opName.rfind("xformOp:translate", 0) == 0 && vals.size() >= 3) {
                    opMats[opName] = Mat4::Translate(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]));
                    implicitOrder.push_back(opName);
                } else if (opName.rfind("xformOp:scale", 0) == 0 && vals.size() >= 3) {
                    opMats[opName] = Mat4::Scale(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]));
                    implicitOrder.push_back(opName);
                } else if (opName.rfind("xformOp:rotateXYZ", 0) == 0 && vals.size() >= 3) {
                    opMats[opName] = Mat4::RotateXYZ(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]));
                    implicitOrder.push_back(opName);
                } else if (opName.rfind("xformOp:orient", 0) == 0 && vals.size() >= 4) {
                    // In USDA text, GfQuat is (real, i, j, k) = (w, x, y, z)
                    opMats[opName] = QuatToMat4(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]), static_cast<float>(vals[3]));
                    implicitOrder.push_back(opName);
                }
            }
        }
        xp = ne;
    }

    std::vector<std::string> finalOrder;
    size_t ordPos = bodyNoChildren.find("xformOpOrder");
    if (ordPos != std::string_view::npos) {
        size_t b1 = bodyNoChildren.find('[', ordPos);
        size_t b2 = (b1 != std::string_view::npos) ? bodyNoChildren.find(']', b1) : std::string_view::npos;
        if (b1 != std::string_view::npos && b2 != std::string_view::npos) {
            std::string_view arr = bodyNoChildren.substr(b1 + 1, b2 - b1 - 1);
            size_t q = 0;
            while ((q = arr.find('"', q)) != std::string_view::npos) {
                size_t qe = arr.find('"', q + 1);
                if (qe == std::string_view::npos) break;
                std::string tok(arr.substr(q + 1, qe - q - 1));
                if (tok == "!resetXformStack!") node.resetXformStack = true;
                else finalOrder.push_back(tok);
                q = qe + 1;
            }
        }
    }
    if (finalOrder.empty()) finalOrder = implicitOrder;
    Mat4 accum = Mat4::Identity();
    for (const std::string& opNm : finalOrder) {
        auto it = opMats.find(opNm);
        if (it != opMats.end()) accum = Mat4::Mul(accum, it->second);
    }
    node.localXform = accum;

    // Helpers for bracket arrays
    auto findKeyBracket = [&](const char* key) -> std::string_view {
        size_t kp = 0;
        size_t kLen = std::strlen(key);
        while ((kp = bodyNoChildren.find(key, kp)) != std::string_view::npos) {
            bool prevOk = (kp == 0) || (!std::isalnum(static_cast<unsigned char>(bodyNoChildren[kp - 1])) && bodyNoChildren[kp - 1] != ':' && bodyNoChildren[kp - 1] != '_');
            bool nextOk = (kp + kLen >= bodyNoChildren.size()) || (!std::isalnum(static_cast<unsigned char>(bodyNoChildren[kp + kLen])) && bodyNoChildren[kp + kLen] != '_');
            if (prevOk && nextOk) {
                size_t eq = bodyNoChildren.find('=', kp + kLen);
                if (eq != std::string_view::npos && eq - (kp + kLen) < 32) {
                    // Support timeSamples = { 0: [ ... ] } as well as = [ ... ]
                    size_t b1 = bodyNoChildren.find('[', eq);
                    if (b1 != std::string_view::npos && b1 - eq < 64) {
                        size_t b2 = bodyNoChildren.find(']', b1);
                        if (b2 != std::string_view::npos) {
                            return bodyNoChildren.substr(b1 + 1, b2 - b1 - 1);
                        }
                    }
                }
            }
            kp += kLen;
        }
        return {};
    };

    auto parseVec3Bracket = [&](const char* key, std::vector<Vec3f>& out) {
        std::string_view blk = findKeyBracket(key);
        if (blk.empty()) return;
        size_t p = 0;
        while ((p = blk.find('(', p)) != std::string_view::npos && out.size() < 500000) {
            size_t pe = blk.find(')', p);
            if (pe == std::string_view::npos) break;
            std::vector<double> c;
            DetailStep::ParseFloatTuple(blk.substr(p, pe - p + 1), c);
            if (c.size() >= 3) {
                out.push_back({ static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]) });
            }
            p = pe + 1;
        }
    };

    auto parseIntBracket = [&](const char* key, std::vector<int32_t>& out) {
        std::string_view blk = findKeyBracket(key);
        if (blk.empty()) return;
        const char* cur = blk.data();
        const char* end = blk.data() + blk.size();
        while (cur < end && out.size() < 2000000) {
            while (cur < end && (*cur < '0' || *cur > '9') && *cur != '-') ++cur;
            if (cur >= end) break;
            char* ep = nullptr;
            long v = std::strtol(cur, &ep, 10);
            if (ep == cur) { ++cur; continue; }
            out.push_back(static_cast<int32_t>(v));
            cur = ep;
        }
    };

    auto findScalarDouble = [&](const char* key, double defVal) -> double {
        size_t kp = 0;
        size_t kLen = std::strlen(key);
        while ((kp = bodyNoChildren.find(key, kp)) != std::string_view::npos) {
            bool prevOk = (kp == 0) || !std::isalnum(static_cast<unsigned char>(bodyNoChildren[kp - 1]));
            bool nextOk = (kp + kLen >= bodyNoChildren.size()) || !std::isalnum(static_cast<unsigned char>(bodyNoChildren[kp + kLen]));
            if (prevOk && nextOk) {
                size_t eq = bodyNoChildren.find('=', kp + kLen);
                if (eq != std::string_view::npos && eq - (kp + kLen) < 16) {
                    char* ep = nullptr;
                    double v = std::strtod(bodyNoChildren.data() + eq + 1, &ep);
                    if (ep != bodyNoChildren.data() + eq + 1 && std::isfinite(v)) return v;
                }
            }
            kp += kLen;
        }
        return defVal;
    };

    // 2. Mesh / Procedural Prim geometry
    if (node.typeName == "Mesh" || bodyNoChildren.find("faceVertexIndices") != std::string_view::npos) {
        std::vector<Vec3f> pts;
        std::vector<int32_t> counts, indices;
        parseVec3Bracket("points", pts);
        parseIntBracket("faceVertexCounts", counts);
        parseIntBracket("faceVertexIndices", indices);
        if (!pts.empty() && !indices.empty()) {
            node.geom = BuildPolygonalUsdMesh(pts, counts, indices);
        }
    } else if (node.typeName == "Cube") {
        node.geom = BuildUsdCubePrim(static_cast<float>(findScalarDouble("size", 2.0)));
    } else if (node.typeName == "Sphere") {
        node.geom = BuildUsdSpherePrim(static_cast<float>(findScalarDouble("radius", 1.0)));
    }

    // 3. PointInstancer attributes
    if (node.typeName == "PointInstancer" || bodyNoChildren.find("protoIndices") != std::string_view::npos) {
        parseIntBracket("protoIndices", node.protoIndices);
        parseVec3Bracket("positions", node.piPositions);
        parseVec3Bracket("scales", node.piScales);

        std::vector<int32_t> invIds;
        parseIntBracket("invisibleIds", invIds);
        for (int32_t id : invIds) node.piInvisibleIds.insert(id);

        std::string_view oriBlk = findKeyBracket("orientations");
        if (!oriBlk.empty()) {
            size_t p = 0;
            while ((p = oriBlk.find('(', p)) != std::string_view::npos && node.piOrientsWXYZ.size() < 500000) {
                size_t pe = oriBlk.find(')', p);
                if (pe == std::string_view::npos) break;
                std::vector<double> c;
                DetailStep::ParseFloatTuple(oriBlk.substr(p, pe - p + 1), c);
                if (c.size() >= 4) {
                    node.piOrientsWXYZ.push_back({
                        static_cast<float>(c[0]), static_cast<float>(c[1]),
                        static_cast<float>(c[2]), static_cast<float>(c[3])
                    });
                }
                p = pe + 1;
            }
        }

        // Parse rel prototypes = [</World/ProtoA>, </World/ProtoB>] or rel prototypes = </World/ProtoA>
        size_t prKey = bodyNoChildren.find("prototypes");
        if (prKey != std::string_view::npos) {
            size_t eq = bodyNoChildren.find('=', prKey);
            size_t nl = bodyNoChildren.find('\n', prKey);
            if (eq != std::string_view::npos) {
                size_t endScan = bodyNoChildren.find(']', eq);
                if (endScan == std::string_view::npos || (nl != std::string_view::npos && bodyNoChildren.find('[', eq) > nl)) {
                    endScan = (nl != std::string_view::npos) ? nl : bodyNoChildren.size();
                }
                std::string_view relSpan = bodyNoChildren.substr(eq, endScan - eq);
                size_t lt = 0;
                while ((lt = relSpan.find('<', lt)) != std::string_view::npos) {
                    size_t gt = relSpan.find('>', lt + 1);
                    if (gt == std::string_view::npos) break;
                    std::string sp(relSpan.substr(lt + 1, gt - lt - 1));
                    if (!sp.empty()) node.protoPaths.push_back(sp);
                    lt = gt + 1;
                }
            }
        }
    }
}

inline bool ParseUsdaSceneTree(std::string_view rawSv, std::vector<UsdPrimNode>& nodes) {
    if (rawSv.rfind("#usda", 0) != 0 &&
        rawSv.find("def ") == std::string_view::npos &&
        rawSv.find("over ") == std::string_view::npos)
    {
        return false;
    }

    std::string clean = StripUsdaComments(rawSv);
    std::string_view s(clean);

    nodes.clear();
    UsdPrimNode root;
    root.name = "";
    root.fullPath = "/";
    root.specifier = "def";
    root.typeName = "Scope";
    nodes.push_back(std::move(root));

    auto findMatchingBrace = [&](size_t openPos, char openCh, char closeCh) -> size_t {
        int d = 0;
        bool inQ = false;
        for (size_t i = openPos; i < s.size(); ++i) {
            char c = s[i];
            if (c == '"' && (i == 0 || s[i - 1] != '\\')) inQ = !inQ;
            if (inQ) continue;
            if (c == openCh) ++d;
            else if (c == closeCh) {
                if (--d == 0) return i;
            }
        }
        return std::string_view::npos;
    };

    std::function<void(size_t, size_t, int, std::string)> parseScope =
        [&](size_t start, size_t end, int parentIdx, std::string parentPath)
    {
        std::string ownAttrText;
        size_t p = start;
        while (p < end) {
            while (p < end && static_cast<unsigned char>(s[p]) <= 32) {
                ownAttrText.push_back(s[p++]);
            }
            if (p >= end) break;

            // Check if a child prim starts here: "def ", "over ", or "class " at statement boundary
            auto matchSpec = [&](const char* kw) -> bool {
                size_t kl = std::strlen(kw);
                if (p + kl >= end || s.compare(p, kl, kw) != 0) return false;
                if (!std::isspace(static_cast<unsigned char>(s[p + kl]))) return false;
                // Look ahead for "PrimName" followed by optional (...) and '{'
                size_t scan = p + kl;
                while (scan < end && std::isspace(static_cast<unsigned char>(s[scan]))) ++scan;
                // Optional TypeName identifier
                if (scan < end && (std::isalpha(static_cast<unsigned char>(s[scan])) || s[scan] == '_')) {
                    while (scan < end && (std::isalnum(static_cast<unsigned char>(s[scan])) || s[scan] == '_')) ++scan;
                    while (scan < end && std::isspace(static_cast<unsigned char>(s[scan]))) ++scan;
                }
                return (scan < end && s[scan] == '"');
            };

            std::string specKw;
            if (matchSpec("def"))        specKw = "def";
            else if (matchSpec("over"))  specKw = "over";
            else if (matchSpec("class")) specKw = "class";

            if (!specKw.empty()) {
                size_t sp = p + specKw.size();
                while (sp < end && std::isspace(static_cast<unsigned char>(s[sp]))) ++sp;
                std::string typeNm;
                if (sp < end && s[sp] != '"') {
                    size_t t0 = sp;
                    while (sp < end && (std::isalnum(static_cast<unsigned char>(s[sp])) || s[sp] == '_')) ++sp;
                    typeNm = std::string(s.substr(t0, sp - t0));
                    while (sp < end && std::isspace(static_cast<unsigned char>(s[sp]))) ++sp;
                }
                if (sp < end && s[sp] == '"') {
                    size_t qEnd = s.find('"', sp + 1);
                    if (qEnd != std::string_view::npos && qEnd < end) {
                        std::string primName(s.substr(sp + 1, qEnd - sp - 1));
                        size_t afterName = qEnd + 1;
                        while (afterName < end && std::isspace(static_cast<unsigned char>(s[afterName]))) ++afterName;

                        std::vector<std::string> refs;
                        if (afterName < end && s[afterName] == '(') {
                            size_t metaClose = findMatchingBrace(afterName, '(', ')');
                            if (metaClose != std::string_view::npos && metaClose < end) {
                                std::string_view metaSv = s.substr(afterName + 1, metaClose - afterName - 1);
                                size_t at1 = 0;
                                while ((at1 = metaSv.find('@', at1)) != std::string_view::npos) {
                                    size_t at2 = metaSv.find('@', at1 + 1);
                                    if (at2 == std::string_view::npos) break;
                                    std::string asset(metaSv.substr(at1 + 1, at2 - at1 - 1));
                                    if (!asset.empty()) refs.push_back(asset);
                                    at1 = at2 + 1;
                                }
                                afterName = metaClose + 1;
                                while (afterName < end && std::isspace(static_cast<unsigned char>(s[afterName]))) ++afterName;
                            }
                        }

                        if (afterName < end && s[afterName] == '{') {
                            size_t bodyClose = findMatchingBrace(afterName, '{', '}');
                            if (bodyClose != std::string_view::npos && bodyClose <= end) {
                                int myIdx = static_cast<int>(nodes.size());
                                UsdPrimNode child;
                                child.name = primName;
                                child.fullPath = (parentPath == "/") ? ("/" + primName) : (parentPath + "/" + primName);
                                child.specifier = specKw;
                                child.typeName = typeNm;
                                child.references = std::move(refs);
                                nodes.push_back(std::move(child));
                                nodes[parentIdx].children.push_back(myIdx);

                                parseScope(afterName + 1, bodyClose, myIdx, nodes[myIdx].fullPath);
                                p = bodyClose + 1;
                                continue;
                            }
                        }
                    }
                }
            }

            // Copy one line or bracketed block to ownAttrText
            if (s[p] == '{') {
                size_t bc = findMatchingBrace(p, '{', '}');
                if (bc != std::string_view::npos && bc < end) {
                    ownAttrText.append(s.data() + p, bc - p + 1);
                    p = bc + 1;
                    continue;
                }
            }
            size_t eol = s.find('\n', p);
            if (eol == std::string_view::npos || eol > end) eol = end;
            ownAttrText.append(s.data() + p, eol - p);
            if (eol < end) { ownAttrText.push_back('\n'); p = eol + 1; }
            else p = end;
        }

        if (parentIdx > 0 && !ownAttrText.empty()) {
            ParseUsdaPrimBodyAttributes(ownAttrText, nodes[parentIdx]);
        }
    };

    // Skip top-level stage metadata (...) if present right after #usda 1.0
    size_t scanStart = 0;
    size_t nl0 = s.find('\n');
    if (nl0 != std::string_view::npos && s.substr(0, nl0).find("usda") != std::string_view::npos) {
        scanStart = nl0 + 1;
        while (scanStart < s.size() && std::isspace(static_cast<unsigned char>(s[scanStart]))) ++scanStart;
        if (scanStart < s.size() && s[scanStart] == '(') {
            size_t mc = findMatchingBrace(scanStart, '(', ')');
            if (mc != std::string_view::npos) {
                // Also extract subLayers = [@...@] if present in stage header!
                std::string_view hdrMeta = s.substr(scanStart + 1, mc - scanStart - 1);
                size_t at1 = 0;
                while ((at1 = hdrMeta.find('@', at1)) != std::string_view::npos) {
                    size_t at2 = hdrMeta.find('@', at1 + 1);
                    if (at2 == std::string_view::npos) break;
                    std::string subAsset(hdrMeta.substr(at1 + 1, at2 - at1 - 1));
                    if (!subAsset.empty()) nodes[0].references.push_back(subAsset);
                    at1 = at2 + 1;
                }
                scanStart = mc + 1;
            }
        }
    }

    parseScope(scanStart, s.size(), 0, "/");
    return nodes.size() > 1 || !nodes[0].references.empty();
}

// ============================================================================
// Binary Crate ("PXR-USDC") Scene-Graph Decoder (PATHS + SPECS + Xform + PointInstancer)
// ============================================================================
inline bool ParseUsdcSceneTree(const uint8_t* data, size_t size, std::vector<UsdPrimNode>& nodes) {
    using namespace Detail3D100;
    if (size < 88 || std::memcmp(data, "PXR-USDC", 8) != 0) return false;

    const uint8_t verMaj = data[8];
    const uint8_t verMin = data[9];
    const uint64_t tocOff = RdU64LE(data + 16);
    if (tocOff < 88 || tocOff + 8 > size) return false;

    const uint64_t numSecs = RdU64LE(data + static_cast<size_t>(tocOff));
    if (numSecs == 0 || numSecs > 64 || tocOff + 8 + numSecs * 32 > size) return false;

    struct CrateSec { uint64_t start = 0, len = 0; bool present = false; };
    CrateSec secTokens, secFields, secFieldSets, secPaths, secSpecs;
    for (uint64_t s = 0; s < numSecs; ++s) {
        const uint8_t* sp = data + static_cast<size_t>(tocOff + 8 + s * 32);
        char nm[17] = {};
        std::memcpy(nm, sp, 16);
        uint64_t st = RdU64LE(sp + 16);
        uint64_t ln = RdU64LE(sp + 24);
        if (st > size || ln > size - st) continue;
        std::string_view name(nm);
        if (name == "TOKENS")         secTokens    = { st, ln, true };
        else if (name == "FIELDS")    secFields    = { st, ln, true };
        else if (name == "FIELDSETS") secFieldSets = { st, ln, true };
        else if (name == "PATHS")     secPaths     = { st, ln, true };
        else if (name == "SPECS")     secSpecs     = { st, ln, true };
    }
    if (!secTokens.present || !secFields.present || !secPaths.present || !secSpecs.present || !secFieldSets.present) {
        return false;
    }

    // 1. TOKENS
    std::vector<std::string> tokens;
    {
        const uint8_t* tp = data + static_cast<size_t>(secTokens.start);
        size_t tLen = static_cast<size_t>(secTokens.len);
        if (tLen < 16) return false;
        uint64_t numToks = RdU64LE(tp);
        if (numToks > 2000000) return false;
        std::vector<uint8_t> tokBuf;
        if (verMaj > 0 || verMin >= 4) {
            if (tLen < 24) return false;
            uint64_t uncompSz = RdU64LE(tp + 8);
            uint64_t compSz   = RdU64LE(tp + 16);
            if (24 + compSz > tLen) return false;
            if (!TfFastDecompress(tp + 24, static_cast<size_t>(compSz), static_cast<size_t>(uncompSz), tokBuf)) return false;
        } else {
            uint64_t uncompSz = RdU64LE(tp + 8);
            if (16 + uncompSz > tLen) return false;
            tokBuf.assign(tp + 16, tp + 16 + static_cast<size_t>(uncompSz));
        }
        tokens.reserve(static_cast<size_t>(numToks));
        size_t p = 0;
        while (p < tokBuf.size() && tokens.size() < numToks) {
            size_t s = p;
            while (p < tokBuf.size() && tokBuf[p] != 0) ++p;
            tokens.emplace_back(reinterpret_cast<const char*>(tokBuf.data() + s), p - s);
            ++p;
        }
    }

    // 2. FIELDS & FIELDSETS
    struct CrateField { uint32_t tokIdx = 0; uint64_t rep = 0; };
    std::vector<CrateField> fields;
    {
        const uint8_t* fp = data + static_cast<size_t>(secFields.start);
        size_t fLen = static_cast<size_t>(secFields.len);
        if (fLen < 24) return false;
        uint64_t numFields = RdU64LE(fp);
        if (numFields == 0 || numFields > 4000000) return false;
        size_t off = 8;
        uint64_t tokCompSz = RdU64LE(fp + off); off += 8;
        if (off + tokCompSz + 8 > fLen) return false;
        std::vector<int32_t> tokIdxs;
        if (!UsdDecompressInts32(fp + off, static_cast<size_t>(tokCompSz), static_cast<size_t>(numFields), tokIdxs)) return false;
        off += static_cast<size_t>(tokCompSz);
        uint64_t repCompSz = RdU64LE(fp + off); off += 8;
        if (off + repCompSz > fLen) return false;
        std::vector<uint8_t> repBytes;
        if (!TfFastDecompress(fp + off, static_cast<size_t>(repCompSz), static_cast<size_t>(numFields) * 8, repBytes) ||
            repBytes.size() < static_cast<size_t>(numFields) * 8) return false;
        fields.resize(static_cast<size_t>(numFields));
        for (size_t i = 0; i < static_cast<size_t>(numFields); ++i) {
            fields[i].tokIdx = static_cast<uint32_t>(tokIdxs[i]);
            fields[i].rep    = RdU64LE(repBytes.data() + i * 8);
        }
    }

    std::vector<int32_t> fieldSets;
    {
        if (secFieldSets.len < 16) return false;
        const uint8_t* fsp = data + static_cast<size_t>(secFieldSets.start);
        uint64_t nFs = RdU64LE(fsp);
        uint64_t cSz = RdU64LE(fsp + 8);
        if (nFs == 0 || nFs > 8000000 || 16 + cSz > secFieldSets.len) return false;
        if (!UsdDecompressInts32(fsp + 16, static_cast<size_t>(cSz), static_cast<size_t>(nFs), fieldSets)) return false;
    }

    // 3. PATHS -> full prim tree + property path map
    const uint8_t* pp = data + static_cast<size_t>(secPaths.start);
    size_t pLen = static_cast<size_t>(secPaths.len);
    if (pLen < 24) return false;
    size_t poff = 0;
    uint64_t totalPaths = RdU64LE(pp + poff); poff += 8;
    uint64_t nEncPaths = totalPaths;
    if (verMaj > 0 || verMin >= 4) {
        if (poff + 8 > pLen) return false;
        nEncPaths = RdU64LE(pp + poff); poff += 8;
    }
    std::vector<int32_t> pathIdxs, elemToks, jumps;
    auto rdPathVec = [&](std::vector<int32_t>& dst) -> bool {
        if (poff + 8 > pLen) return false;
        uint64_t cSz = RdU64LE(pp + poff); poff += 8;
        if (poff + cSz > pLen) return false;
        bool ok = UsdDecompressInts32(pp + poff, static_cast<size_t>(cSz), static_cast<size_t>(nEncPaths), dst);
        poff += static_cast<size_t>(cSz);
        return ok;
    };
    if (nEncPaths == 0 || nEncPaths > 4000000 || !rdPathVec(pathIdxs) || !rdPathVec(elemToks) || !rdPathVec(jumps)) {
        return false;
    }

    nodes.clear();
    UsdPrimNode root;
    root.fullPath = "/";
    root.specifier = "def";
    root.typeName = "Scope";
    nodes.push_back(std::move(root));

    std::unordered_map<int32_t, int> pathIdxToNode;
    std::unordered_map<int32_t, std::string> pathIdxToFullStr;
    std::unordered_map<int32_t, std::pair<int, std::string>> propPathToNodeAttr;

    pathIdxToNode[0] = 0;
    pathIdxToFullStr[0] = "/";

    std::function<void(size_t, int, int)> walkPaths = [&](size_t cur, int parentNodeIdx, int depth) {
        if (cur >= nEncPaths || depth > 512) return;
        while (cur < nEncPaths) {
            int32_t thisPathIdx = pathIdxs[cur];
            int32_t et = elemToks[cur];
            int nextParentNodeIdx = parentNodeIdx;

            if (thisPathIdx == 0) {
                nextParentNodeIdx = 0;
            } else if (et < 0) {
                uint32_t tIdx = static_cast<uint32_t>(-et);
                if (tIdx < tokens.size()) {
                    propPathToNodeAttr[thisPathIdx] = { parentNodeIdx, tokens[tIdx] };
                }
            } else {
                uint32_t tIdx = static_cast<uint32_t>(et);
                std::string pName = (tIdx < tokens.size()) ? tokens[tIdx] : ("prim_" + std::to_string(thisPathIdx));
                int myIdx = static_cast<int>(nodes.size());
                UsdPrimNode nd;
                nd.name = pName;
                const std::string& parFull = nodes[parentNodeIdx].fullPath;
                nd.fullPath = (parFull == "/") ? ("/" + pName) : (parFull + "/" + pName);
                nodes.push_back(std::move(nd));
                nodes[parentNodeIdx].children.push_back(myIdx);
                pathIdxToNode[thisPathIdx] = myIdx;
                pathIdxToFullStr[thisPathIdx] = nodes[myIdx].fullPath;
                nextParentNodeIdx = myIdx;
            }

            int32_t jmp = jumps[cur];
            bool hasChild   = (jmp > 0 || jmp == -1);
            bool hasSibling = (jmp >= 0);
            if (hasChild) walkPaths(cur + 1, nextParentNodeIdx, depth + 1);
            if (!hasSibling) break;
            cur = (jmp > 0) ? (cur + static_cast<size_t>(jmp)) : (cur + 1);
        }
    };
    walkPaths(0, 0, 0);

    // 4. ValueRep Decoders
    const bool use64BitArrayCount = (verMaj > 0 || verMin >= 5);

    auto derefTimeSampleFirst = [&](uint64_t rep) -> uint64_t {
        bool isInlined = (rep & (1ULL << 62)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        if (tEnum == 46 && !isInlined && pay + 24 <= size) {
            size_t valBlock = static_cast<size_t>(pay) + 8;
            int64_t rel2 = static_cast<int64_t>(RdU64LE(data + valBlock));
            size_t arrPos = valBlock + static_cast<size_t>(rel2);
            if (arrPos + 16 <= size && RdU64LE(data + arrPos) >= 1) {
                return RdU64LE(data + arrPos + 8);
            }
        }
        return rep;
    };

    auto decodeVec3Array = [&](uint64_t repIn, std::vector<Vec3f>& outPts) -> bool {
        uint64_t rep   = derefTimeSampleFirst(repIn);
        bool isArray   = (rep & (1ULL << 63)) != 0;
        bool isInlined = (rep & (1ULL << 62)) != 0;
        bool isComp    = (rep & (1ULL << 61)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        if (!isArray || isInlined || (tEnum != 19 && tEnum != 20)) return false;
        if (pay + 8 > size) return false;
        size_t p = static_cast<size_t>(pay);
        uint64_t nElem = use64BitArrayCount ? RdU64LE(data + p) : RdU32LE(data + p);
        p += use64BitArrayCount ? 8 : 4;
        if (nElem == 0 || nElem > 2000000) return false;
        size_t elemSz = (tEnum == 19) ? 12 : 24;
        std::vector<uint8_t> decomp;
        const uint8_t* rawPtr = nullptr;
        if (isComp) {
            if (p + 8 > size) return false;
            uint64_t cSz = RdU64LE(data + p); p += 8;
            if (p + cSz > size) return false;
            if (!TfFastDecompress(data + p, static_cast<size_t>(cSz), static_cast<size_t>(nElem) * elemSz, decomp) ||
                decomp.size() < static_cast<size_t>(nElem) * elemSz) return false;
            rawPtr = decomp.data();
        } else {
            if (p + static_cast<size_t>(nElem) * elemSz > size) return false;
            rawPtr = data + p;
        }
        outPts.resize(static_cast<size_t>(nElem));
        for (size_t i = 0; i < static_cast<size_t>(nElem); ++i) {
            if (tEnum == 19) {
                const uint8_t* vp = rawPtr + i * 12;
                outPts[i] = { RdF32LE(vp + 0), RdF32LE(vp + 4), RdF32LE(vp + 8) };
            } else {
                const uint8_t* vp = rawPtr + i * 24;
                outPts[i] = { static_cast<float>(RdF64LE(vp + 0)), static_cast<float>(RdF64LE(vp + 8)), static_cast<float>(RdF64LE(vp + 16)) };
            }
        }
        return true;
    };

    auto decodeQuatArray = [&](uint64_t repIn, std::vector<std::array<float, 4>>& outWXYZ) -> bool {
        uint64_t rep   = derefTimeSampleFirst(repIn);
        bool isArray   = (rep & (1ULL << 63)) != 0;
        bool isInlined = (rep & (1ULL << 62)) != 0;
        bool isComp    = (rep & (1ULL << 61)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        // Crate TypeEnum: Quatd=26(32B), Quatf=27(16B), Quath=28(8B)
        if (!isArray || isInlined || (tEnum != 26 && tEnum != 27 && tEnum != 28)) return false;
        if (pay + 8 > size) return false;
        size_t p = static_cast<size_t>(pay);
        uint64_t nElem = use64BitArrayCount ? RdU64LE(data + p) : RdU32LE(data + p);
        p += use64BitArrayCount ? 8 : 4;
        if (nElem == 0 || nElem > 2000000) return false;
        size_t elemSz = (tEnum == 26) ? 32 : ((tEnum == 27) ? 16 : 8);
        std::vector<uint8_t> decomp;
        const uint8_t* rawPtr = nullptr;
        if (isComp) {
            if (p + 8 > size) return false;
            uint64_t cSz = RdU64LE(data + p); p += 8;
            if (p + cSz > size) return false;
            if (!TfFastDecompress(data + p, static_cast<size_t>(cSz), static_cast<size_t>(nElem) * elemSz, decomp) ||
                decomp.size() < static_cast<size_t>(nElem) * elemSz) return false;
            rawPtr = decomp.data();
        } else {
            if (p + static_cast<size_t>(nElem) * elemSz > size) return false;
            rawPtr = data + p;
        }
        outWXYZ.resize(static_cast<size_t>(nElem));
        for (size_t i = 0; i < static_cast<size_t>(nElem); ++i) {
            const uint8_t* qp = rawPtr + i * elemSz;
            // Pixar GfQuat binary memory layout is (i, j, k, real) = (x, y, z, w)!
            if (tEnum == 26) {
                outWXYZ[i] = { static_cast<float>(RdF64LE(qp + 24)), static_cast<float>(RdF64LE(qp + 0)), static_cast<float>(RdF64LE(qp + 8)), static_cast<float>(RdF64LE(qp + 16)) };
            } else if (tEnum == 27) {
                outWXYZ[i] = { RdF32LE(qp + 12), RdF32LE(qp + 0), RdF32LE(qp + 4), RdF32LE(qp + 8) };
            } else {
                outWXYZ[i] = { HalfToFloat17(RdU16LE(qp + 6)), HalfToFloat17(RdU16LE(qp + 0)), HalfToFloat17(RdU16LE(qp + 2)), HalfToFloat17(RdU16LE(qp + 4)) };
            }
        }
        return true;
    };

    auto decodeIntArray = [&](uint64_t repIn, std::vector<int32_t>& outInts) -> bool {
        uint64_t rep   = derefTimeSampleFirst(repIn);
        bool isArray   = (rep & (1ULL << 63)) != 0;
        bool isInlined = (rep & (1ULL << 62)) != 0;
        bool isComp    = (rep & (1ULL << 61)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        if (!isArray || isInlined || (tEnum != 3 && tEnum != 4)) return false;
        if (pay + 8 > size) return false;
        size_t p = static_cast<size_t>(pay);
        uint64_t nElem = use64BitArrayCount ? RdU64LE(data + p) : RdU32LE(data + p);
        p += use64BitArrayCount ? 8 : 4;
        if (nElem == 0 || nElem > 6000000) return false;
        if (isComp) {
            if (p + 8 > size) return false;
            uint64_t cSz = RdU64LE(data + p); p += 8;
            if (p + cSz > size) return false;
            return UsdDecompressInts32(data + p, static_cast<size_t>(cSz), static_cast<size_t>(nElem), outInts);
        } else {
            if (p + static_cast<size_t>(nElem) * 4 > size) return false;
            outInts.resize(static_cast<size_t>(nElem));
            for (size_t i = 0; i < static_cast<size_t>(nElem); ++i) {
                outInts[i] = static_cast<int32_t>(RdU32LE(data + p + i * 4));
            }
            return true;
        }
    };

    auto decodeSingleVec3 = [&](uint64_t repIn, Vec3f& outV) -> bool {
        uint64_t rep   = derefTimeSampleFirst(repIn);
        bool isArray   = (rep & (1ULL << 63)) != 0;
        bool isInlined = (rep & (1ULL << 62)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        if (isArray) return false;
        // Crate TypeEnum: Vec3i=18, Vec3h=19, Vec3f=20, Vec3d=21 (note: array flag bit 63 distinguishes Vec3f vs Vec3f[]!)
        // Wait: in Pixar Crate TypeEnum (crateDataTypes.h):
        // Bool=1, UChar=2, Int=3, UInt=4, Int64=5, UInt64=6, Half=7, Float=8, Double=9,
        // String=10, Token=11, AssetPath=12, Matrix2d=13, Matrix3d=14, Matrix4d=15,
        // Quatd=16, Quatf=17, Quath=18, Vec2d=19.. wait! Let's check Extended3D100.h line 1164!
        // In Extended3D100.h line 1164, Vec3f=19, Vec3d=20! Let's support BOTH inlined int8[3] AND out-of-line 12B/24B!
        if (isInlined) {
            int8_t b0 = static_cast<int8_t>(pay & 0xFFu);
            int8_t b1 = static_cast<int8_t>((pay >> 8) & 0xFFu);
            int8_t b2 = static_cast<int8_t>((pay >> 16) & 0xFFu);
            outV = { static_cast<float>(b0), static_cast<float>(b1), static_cast<float>(b2) };
            return true;
        }
        if (tEnum == 19 && pay + 12 <= size) {
            outV = { RdF32LE(data + pay + 0), RdF32LE(data + pay + 4), RdF32LE(data + pay + 8) };
            return true;
        }
        if (tEnum == 20 && pay + 24 <= size) {
            outV = { static_cast<float>(RdF64LE(data + pay + 0)), static_cast<float>(RdF64LE(data + pay + 8)), static_cast<float>(RdF64LE(data + pay + 16)) };
            return true;
        }
        return false;
    };

    auto decodeSingleMatrix4d = [&](uint64_t repIn, Mat4& outM) -> bool {
        uint64_t rep   = derefTimeSampleFirst(repIn);
        bool isArray   = (rep & (1ULL << 63)) != 0;
        bool isInlined = (rep & (1ULL << 62)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        if (isArray) return false;
        // Matrix4d in Crate: either inlined diagonal int8[4] or 16 doubles (128 bytes) at payload offset (tEnum 15 or 13)
        if (isInlined && (tEnum == 15 || tEnum == 13)) {
            int8_t d0 = static_cast<int8_t>(pay & 0xFFu);
            int8_t d1 = static_cast<int8_t>((pay >> 8) & 0xFFu);
            int8_t d2 = static_cast<int8_t>((pay >> 16) & 0xFFu);
            outM = Mat4::Scale(static_cast<float>(d0), static_cast<float>(d1), static_cast<float>(d2));
            return true;
        }
        if (!isInlined && (tEnum == 15 || tEnum == 13) && pay + 128 <= size) {
            double m16[16] = {};
            for (int k = 0; k < 16; ++k) m16[k] = RdF64LE(data + pay + k * 8);
            outM = RowVecMatrix16ToMat4(m16);
            return true;
        }
        return false;
    };

    auto decodeTargetPathList = [&](uint64_t rep, std::vector<std::string>& outPaths) {
        bool isInlined = (rep & (1ULL << 62)) != 0;
        uint8_t tEnum  = static_cast<uint8_t>((rep >> 48) & 0xFFu);
        uint64_t pay   = rep & ((1ULL << 48) - 1ULL);
        if (isInlined || pay + 8 > size) return;

        // PathListOp (ListOpHeader 1B bitmask + vector<uint32_t pathIndex> per bit) OR PathVector (uint64 n + uint32[n])
        auto appendPathVecAt = [&](size_t& pos) -> bool {
            if (pos + 8 > size) return false;
            uint64_t n = RdU64LE(data + pos); pos += 8;
            if (n > 65536 || pos + n * 4 > size) return false;
            for (uint64_t i = 0; i < n; ++i) {
                int32_t pidx = static_cast<int32_t>(RdU32LE(data + pos + i * 4));
                auto it = pathIdxToFullStr.find(pidx);
                if (it != pathIdxToFullStr.end()) outPaths.push_back(it->second);
            }
            pos += static_cast<size_t>(n) * 4;
            return true;
        };

        // Check PathVector first (tEnum == 42 or direct count) vs PathListOp (1-byte bitmask: bit0=IsExplicit, bit1=ExplicitItems, bit2=Added, bit3=Deleted, bit4=Ordered, bit5=Prepended, bit6=Appended)
        if (tEnum == 42) {
            size_t pos = static_cast<size_t>(pay);
            appendPathVecAt(pos);
        } else {
            size_t pos = static_cast<size_t>(pay);
            if (pos + 1 <= size) {
                uint8_t mask = data[pos++];
                for (int bit = 1; bit <= 6; ++bit) {
                    if ((mask & (1u << bit)) != 0) {
                        if (!appendPathVecAt(pos)) break;
                    }
                }
            }
        }
    };

    // 5. Walk SPECS to populate Prim types, Xform ops, Meshes, and PointInstancers
    const uint8_t* sp = data + static_cast<size_t>(secSpecs.start);
    size_t sLen = static_cast<size_t>(secSpecs.len);
    if (sLen < 32) return false;
    size_t soff = 0;
    uint64_t nSpecs = RdU64LE(sp + soff); soff += 8;
    std::vector<int32_t> specPaths, specFs, specTypes;
    auto rdSpecVec = [&](std::vector<int32_t>& dst) -> bool {
        if (soff + 8 > sLen) return false;
        uint64_t cSz = RdU64LE(sp + soff); soff += 8;
        if (soff + cSz > sLen) return false;
        bool ok = UsdDecompressInts32(sp + soff, static_cast<size_t>(cSz), static_cast<size_t>(nSpecs), dst);
        soff += static_cast<size_t>(cSz);
        return ok;
    };
    if (nSpecs == 0 || nSpecs > 4000000 || !rdSpecVec(specPaths) || !rdSpecVec(specFs) || !rdSpecVec(specTypes)) {
        return false;
    }

    struct RawPrimProps {
        uint64_t pointsRep = 0, countsRep = 0, indicesRep = 0;
        std::vector<std::pair<std::string, uint64_t>> xformOps;
        std::vector<std::string> xformOrder;
        bool resetXform = false;
    };
    std::unordered_map<int, RawPrimProps> rawProps;

    for (size_t i = 0; i < static_cast<size_t>(nSpecs); ++i) {
        int32_t pIdx = specPaths[i];
        int32_t fsIdx = specFs[i];
        if (fsIdx < 0 || static_cast<size_t>(fsIdx) >= fieldSets.size()) continue;

        auto primIt = pathIdxToNode.find(pIdx);
        if (primIt != pathIdxToNode.end()) {
            UsdPrimNode& nd = nodes[primIt->second];
            for (size_t k = static_cast<size_t>(fsIdx); k < fieldSets.size() && fieldSets[k] >= 0; ++k) {
                uint32_t fIdx = static_cast<uint32_t>(fieldSets[k]);
                if (fIdx >= fields.size()) continue;
                uint32_t ft = fields[fIdx].tokIdx;
                if (ft >= tokens.size()) continue;
                const std::string& fn = tokens[ft];
                uint64_t rep = fields[fIdx].rep;
                uint64_t pay = rep & ((1ULL << 48) - 1ULL);
                if (fn == "typeName" && (rep & (1ULL << 62)) != 0 && pay < tokens.size()) {
                    nd.typeName = tokens[static_cast<size_t>(pay)];
                } else if (fn == "specifier" && (rep & (1ULL << 62)) != 0) {
                    uint32_t spv = static_cast<uint32_t>(pay & 0xFFu);
                    nd.specifier = (spv == 1) ? "over" : ((spv == 2) ? "class" : "def");
                }
            }
            continue;
        }

        auto propIt = propPathToNodeAttr.find(pIdx);
        if (propIt == propPathToNodeAttr.end()) continue;
        int nodeIdx = propIt->second.first;
        const std::string& attrName = propIt->second.second;
        UsdPrimNode& nd = nodes[nodeIdx];
        RawPrimProps& rp = rawProps[nodeIdx];

        uint64_t valRep = 0;
        for (size_t k = static_cast<size_t>(fsIdx); k < fieldSets.size() && fieldSets[k] >= 0; ++k) {
            uint32_t fIdx = static_cast<uint32_t>(fieldSets[k]);
            if (fIdx >= fields.size()) continue;
            uint32_t ft = fields[fIdx].tokIdx;
            if (ft >= tokens.size()) continue;
            const std::string& fn = tokens[ft];
            if (fn == "default" || fn == "timeSamples") {
                valRep = fields[fIdx].rep;
                if (fn == "default") break;
            } else if (attrName == "prototypes" && (fn == "targetPaths" || fn == " pathListOp")) {
                decodeTargetPathList(fields[fIdx].rep, nd.protoPaths);
            }
        }
        // Also check any field on "prototypes" relationship for PathListOp / PathVector
        if (attrName == "prototypes" && nd.protoPaths.empty()) {
            for (size_t k = static_cast<size_t>(fsIdx); k < fieldSets.size() && fieldSets[k] >= 0; ++k) {
                uint32_t fIdx = static_cast<uint32_t>(fieldSets[k]);
                if (fIdx < fields.size() && tokens[fields[fIdx].tokIdx].find("Paths") != std::string::npos) {
                    decodeTargetPathList(fields[fIdx].rep, nd.protoPaths);
                }
            }
        }
        if (valRep == 0) continue;

        if (attrName == "points")                 rp.pointsRep  = valRep;
        else if (attrName == "faceVertexCounts")  rp.countsRep  = valRep;
        else if (attrName == "faceVertexIndices") rp.indicesRep = valRep;
        else if (attrName.rfind("xformOp:", 0) == 0) {
            rp.xformOps.push_back({ attrName, valRep });
        } else if (attrName == "xformOpOrder") {
            // TokenVector (uint64 n + uint32 tokIdx[n]) or Token[] array
            uint64_t pay = valRep & ((1ULL << 48) - 1ULL);
            bool isInlined = (valRep & (1ULL << 62)) != 0;
            if (!isInlined && pay + 8 <= size) {
                uint64_t n = RdU64LE(data + pay);
                if (n <= 64 && pay + 8 + n * 4 <= size) {
                    for (uint64_t t = 0; t < n; ++t) {
                        uint32_t tid = RdU32LE(data + pay + 8 + t * 4);
                        if (tid < tokens.size()) {
                            if (tokens[tid] == "!resetXformStack!") rp.resetXform = true;
                            else rp.xformOrder.push_back(tokens[tid]);
                        }
                    }
                }
            }
        } else if (attrName == "protoIndices") {
            decodeIntArray(valRep, nd.protoIndices);
        } else if (attrName == "positions") {
            decodeVec3Array(valRep, nd.piPositions);
        } else if (attrName == "scales") {
            decodeVec3Array(valRep, nd.piScales);
        } else if (attrName == "orientations") {
            decodeQuatArray(valRep, nd.piOrientsWXYZ);
        }
    }

    for (auto& [nodeIdx, rp] : rawProps) {
        UsdPrimNode& nd = nodes[nodeIdx];
        nd.resetXformStack = rp.resetXform;

        std::unordered_map<std::string, Mat4> opMap;
        std::vector<std::string> impOrder;
        for (const auto& [opNm, rep] : rp.xformOps) {
            Mat4 m = Mat4::Identity();
            Vec3f v = { 0, 0, 0 };
            if (opNm.rfind("xformOp:transform", 0) == 0 && decodeSingleMatrix4d(rep, m)) {
                opMap[opNm] = m; impOrder.push_back(opNm);
            } else if (opNm.rfind("xformOp:translate", 0) == 0 && decodeSingleVec3(rep, v)) {
                opMap[opNm] = Mat4::Translate(v.x, v.y, v.z); impOrder.push_back(opNm);
            } else if (opNm.rfind("xformOp:scale", 0) == 0 && decodeSingleVec3(rep, v)) {
                opMap[opNm] = Mat4::Scale(v.x, v.y, v.z); impOrder.push_back(opNm);
            } else if (opNm.rfind("xformOp:rotateXYZ", 0) == 0 && decodeSingleVec3(rep, v)) {
                opMap[opNm] = Mat4::RotateXYZ(v.x, v.y, v.z); impOrder.push_back(opNm);
            }
        }
        const auto& ord = rp.xformOrder.empty() ? impOrder : rp.xformOrder;
        Mat4 accum = Mat4::Identity();
        for (const std::string& nm : ord) {
            auto it = opMap.find(nm);
            if (it != opMap.end()) accum = Mat4::Mul(accum, it->second);
        }
        nd.localXform = accum;

        if (rp.pointsRep != 0 && rp.indicesRep != 0) {
            std::vector<Vec3f> pts;
            std::vector<int32_t> counts, indices;
            if (decodeVec3Array(rp.pointsRep, pts) && decodeIntArray(rp.indicesRep, indices)) {
                if (rp.countsRep != 0) decodeIntArray(rp.countsRep, counts);
                nd.geom = BuildPolygonalUsdMesh(pts, counts, indices);
            }
        }
    }

    return nodes.size() > 1;
}

// ============================================================================
// Stage Evaluator (Resolves Cross-Layer @refs@, Xform Hierarchy & PointInstancer)
// ============================================================================
inline void EvaluateUsdStageNodes(
    std::vector<UsdPrimNode>& nodes,
    const std::function<bool(const std::string&, std::vector<UsdPrimNode>&)>& loadExternalStage,
    LoadedMesh3D& outMesh,
    int refDepth = 0)
{
    if (nodes.empty() || refDepth > 4) return;

    // 1. Graft any @reference@ stages onto referencing prims
    if (loadExternalStage) {
        size_t origCount = nodes.size();
        for (size_t i = 0; i < origCount; ++i) {
            if (nodes[i].references.empty()) continue;
            std::vector<std::string> refs = nodes[i].references;
            for (const std::string& refAsset : refs) {
                std::vector<UsdPrimNode> subNodes;
                if (!loadExternalStage(refAsset, subNodes) || subNodes.size() <= 1) continue;
                // Find default/first concrete root child in subNodes (or graft all root children)
                int baseOffset = static_cast<int>(nodes.size()) - 1;
                // Copy subNodes[1..end] into nodes, remapping child indices
                for (size_t k = 1; k < subNodes.size(); ++k) {
                    UsdPrimNode cp = std::move(subNodes[k]);
                    for (int& ch : cp.children) ch += baseOffset;
                    nodes.push_back(std::move(cp));
                }
                if (i == 0) {
                    for (int rootCh : subNodes[0].children) {
                        nodes[0].children.push_back(rootCh + baseOffset);
                    }
                } else if (subNodes[0].children.size() == 1) {
                    // Single root prim in referenced asset merges into referencing prim (classic USD reference!)
                    int srcRoot = subNodes[0].children[0] + baseOffset;
                    if (nodes[i].geom.verts.empty() && !nodes[srcRoot].geom.verts.empty()) {
                        nodes[i].geom = nodes[srcRoot].geom;
                    }
                    for (int subCh : nodes[srcRoot].children) {
                        nodes[i].children.push_back(subCh);
                    }
                } else {
                    for (int rootCh : subNodes[0].children) {
                        nodes[i].children.push_back(rootCh + baseOffset);
                    }
                }
            }
        }
    }

    // 2. Build path -> node index lookup and identify PointInstancer prototype subtrees
    std::unordered_map<std::string, int> pathMap;
    for (size_t i = 0; i < nodes.size(); ++i) {
        pathMap[nodes[i].fullPath] = static_cast<int>(i);
    }

    std::set<int> prototypeNodeIndices;
    std::function<void(int)> markProtoSubtree = [&](int idx) {
        if (idx <= 0 || idx >= static_cast<int>(nodes.size())) return;
        if (!prototypeNodeIndices.insert(idx).second) return;
        for (int ch : nodes[idx].children) markProtoSubtree(ch);
    };

    for (size_t i = 1; i < nodes.size(); ++i) {
        UsdPrimNode& nd = nodes[i];
        if (nd.protoIndices.empty() && nd.piPositions.empty()) continue;
        // If prototypes relationship was empty or pointed to a Scope container, resolve prototypes:
        if (nd.protoPaths.empty()) {
            // Check child named "Prototypes" or direct children of PointInstancer
            for (int ch : nd.children) {
                if (nodes[ch].name == "Prototypes" && !nodes[ch].children.empty()) {
                    for (int pch : nodes[ch].children) nd.protoPaths.push_back(nodes[pch].fullPath);
                } else {
                    nd.protoPaths.push_back(nodes[ch].fullPath);
                }
            }
        }
        for (const std::string& pp : nd.protoPaths) {
            auto it = pathMap.find(pp);
            if (it != pathMap.end()) markProtoSubtree(it->second);
        }
    }

    // Helper to bake a prototype subtree in prototype-root-relative space (including prototype's own localXform)
    std::function<void(int, const Mat4&, UsdLocalMesh&, int)> bakeSubtreeLocal =
        [&](int idx, const Mat4& parentMat, UsdLocalMesh& dst, int depth)
    {
        if (idx <= 0 || idx >= static_cast<int>(nodes.size()) || depth > 48) return;
        const UsdPrimNode& nd = nodes[idx];
        Mat4 curMat = nd.resetXformStack ? nd.localXform : Mat4::Mul(parentMat, nd.localXform);
        if (!nd.geom.verts.empty() && !nd.geom.tris.empty()) {
            AppendLocalToLocal(nd.geom, curMat, dst);
        }
        for (int ch : nd.children) {
            bakeSubtreeLocal(ch, curMat, dst, depth + 1);
        }
    };

    // 3. Traverse stage hierarchy, emitting transformed meshes and expanding PointInstancers
    std::function<void(int, const Mat4&, int)> walkStage =
        [&](int idx, const Mat4& parentWorld, int depth)
    {
        if (idx < 0 || idx >= static_cast<int>(nodes.size()) || depth > 64) return;
        const UsdPrimNode& nd = nodes[idx];
        if (idx > 0) {
            if (nd.specifier == "class" || prototypeNodeIndices.count(idx) > 0) return;
        }

        Mat4 worldMat = (idx == 0)
            ? Mat4::Identity()
            : (nd.resetXformStack ? nd.localXform : Mat4::Mul(parentWorld, nd.localXform));

        if (idx > 0 && nd.specifier == "def") {
            if (!nd.geom.verts.empty() && !nd.geom.tris.empty()) {
                AppendTransformedLocalMesh(nd.geom, worldMat, outMesh);
            }

            // Expand PointInstancer!
            if (!nd.piPositions.empty() && !nd.protoPaths.empty()) {
                std::vector<UsdLocalMesh> bakedProtos(nd.protoPaths.size());
                for (size_t p = 0; p < nd.protoPaths.size(); ++p) {
                    auto it = pathMap.find(nd.protoPaths[p]);
                    if (it != pathMap.end()) {
                        bakeSubtreeLocal(it->second, Mat4::Identity(), bakedProtos[p], 0);
                    }
                }

                size_t nInst = nd.piPositions.size();
                if (!nd.protoIndices.empty()) nInst = std::min(nInst, nd.protoIndices.size());
                nInst = std::min<size_t>(nInst, 100000);

                for (size_t k = 0; k < nInst; ++k) {
                    if (nd.piInvisibleIds.count(static_cast<int64_t>(k)) > 0) continue;
                    int32_t pIdx = nd.protoIndices.empty() ? 0 : nd.protoIndices[k];
                    if (pIdx < 0 || static_cast<size_t>(pIdx) >= bakedProtos.size()) continue;
                    const UsdLocalMesh& protoMesh = bakedProtos[static_cast<size_t>(pIdx)];
                    if (protoMesh.verts.empty()) continue;

                    Mat4 instMat = Mat4::Translate(nd.piPositions[k].x, nd.piPositions[k].y, nd.piPositions[k].z);
                    if (k < nd.piOrientsWXYZ.size()) {
                        const auto& q = nd.piOrientsWXYZ[k];
                        instMat = Mat4::Mul(instMat, QuatToMat4(q[0], q[1], q[2], q[3]));
                    }
                    if (k < nd.piScales.size()) {
                        instMat = Mat4::Mul(instMat, Mat4::Scale(nd.piScales[k].x, nd.piScales[k].y, nd.piScales[k].z));
                    }
                    AppendTransformedLocalMesh(protoMesh, Mat4::Mul(worldMat, instMat), outMesh);
                }
            }
        }

        for (int ch : nd.children) {
            walkStage(ch, worldMat, depth + 1);
        }
    };

    walkStage(0, Mat4::Identity(), 0);
}

inline bool ParseSingleUsdBufferToNodes(const uint8_t* data, size_t size, std::vector<UsdPrimNode>& outNodes) {
    if (!data || size < 6) return false;
    if (size >= 88 && std::memcmp(data, "PXR-USDC", 8) == 0) {
        return ParseUsdcSceneTree(data, size, outNodes);
    }
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    return ParseUsdaSceneTree(sv, outNodes);
}

} // namespace UsdAbcDetail

// ============================================================================
// Public Entry Point 1: Pixar OpenUSD (.usd, .usda, .usdc, .usdz)
// ============================================================================
inline bool ParsePixarUsdComplete(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh)
{
    using namespace UsdAbcDetail;
    if (!data || size < 6) return false;
    mesh.vertices.clear();
    mesh.tris.clear();

    // 1. USDZ Archive Package ("PK\x03\x04"): extract all .usd/.usda/.usdc layers for cross-layer composition
    if (size >= 4 && data[0] == 'P' && data[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> entries;
        if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

        struct LayerBlob {
            std::string normName;
            std::string baseName;
            std::vector<uint8_t> bytes;
        };
        std::vector<LayerBlob> layers;
        for (const auto& e : entries) {
            if (e.isDirectory) continue;
            std::wstring iExt = PathUtils::ToLower(PathUtils::GetExtension(e.pathInArchive));
            if (iExt == L".usda" || iExt == L".usd" || iExt == L".usdc") {
                std::vector<uint8_t> ib;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, e, ib, 128 * 1024 * 1024) && !ib.empty()) {
                    std::string pUtf8;
                    for (wchar_t wc : e.pathInArchive) {
                        char c = (wc == L'\\') ? '/' : static_cast<char>(towlower(wc));
                        pUtf8.push_back(c);
                    }
                    size_t sl = pUtf8.find_last_of('/');
                    std::string bName = (sl == std::string::npos) ? pUtf8 : pUtf8.substr(sl + 1);
                    layers.push_back({ pUtf8, bName, std::move(ib) });
                }
            }
        }
        if (layers.empty()) return false;

        auto resolveZipLayer = [&](const std::string& assetRef, std::vector<UsdPrimNode>& outSub) -> bool {
            std::string cleanRef;
            for (char c : assetRef) cleanRef.push_back((c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            while (cleanRef.rfind("./", 0) == 0) cleanRef.erase(0, 2);
            size_t sl = cleanRef.find_last_of('/');
            std::string refBase = (sl == std::string::npos) ? cleanRef : cleanRef.substr(sl + 1);
            for (const auto& ly : layers) {
                if (ly.normName == cleanRef || ly.baseName == refBase) {
                    return ParseSingleUsdBufferToNodes(ly.bytes.data(), ly.bytes.size(), outSub);
                }
            }
            return false;
        };

        std::vector<UsdPrimNode> rootNodes;
        if (ParseSingleUsdBufferToNodes(layers[0].bytes.data(), layers[0].bytes.size(), rootNodes)) {
            EvaluateUsdStageNodes(rootNodes, resolveZipLayer, mesh, 0);
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) {
            if (!ParsePixarUsdMesh(data, size, extLower, mesh)) return false;
        }
        mesh.formatName = L"Pixar USDZ Package (.usdz)";
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }

    // 2. Standalone .usda / .usdc / .usd with optional sibling @asset@ resolution on disk
    std::wstring baseDir = fullPath.empty() ? std::wstring() : PathUtils::GetParentPath(fullPath);
    auto resolveDiskLayer = [&](const std::string& assetRef, std::vector<UsdPrimNode>& outSub) -> bool {
        if (baseDir.empty() || assetRef.empty()) return false;
        std::string cleanRef = assetRef;
        while (cleanRef.rfind("./", 0) == 0) cleanRef.erase(0, 2);
        std::wstring wRel;
        for (char c : cleanRef) wRel.push_back((c == '/') ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
        // Prevent parent traversal outside folder unless safe relative path
        std::wstring cand = baseDir + L"\\" + PathUtils::GetFileName(wRel);
        std::vector<uint8_t> subBytes;
        if (!PathUtils::ReadFileBytes(cand, 64 * 1024 * 1024, subBytes) || subBytes.empty()) return false;
        return ParseSingleUsdBufferToNodes(subBytes.data(), subBytes.size(), outSub);
    };

    const bool isCrate = (size >= 88 && std::memcmp(data, "PXR-USDC", 8) == 0);
    std::vector<UsdPrimNode> stageNodes;
    if (ParseSingleUsdBufferToNodes(data, size, stageNodes)) {
        EvaluateUsdStageNodes(stageNodes, resolveDiskLayer, mesh, 0);
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) {
        if (!ParsePixarUsdMesh(data, size, extLower, mesh)) return false;
    }

    mesh.formatName = isCrate
        ? L"Pixar OpenUSD Binary Crate (.usdc)"
        : L"Pixar OpenUSD ASCII Stage (.usda)";
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

// ============================================================================
// Public Entry Point 2: Alembic Ogawa (.abc) Hierarchical Xform + DAG Instancing
// ============================================================================
inline bool ParseAlembicOgawaComplete(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace Detail3D100;
    using DetailScad::Mat4;

    if (!data || size < 24 || std::memcmp(data, "Ogawa", 5) != 0) return false;
    size_t rootPos = 0;
    if ((data[5] == 0xFF || data[5] == 0x00) && data[6] == 0x00 && data[7] == 0x01) {
        rootPos = 8;
    } else if (data[5] == 0x00 && data[6] == 0xFF && data[7] == 0x00 && data[8] == 0x01) {
        rootPos = 9;
    } else {
        return false;
    }
    uint64_t rootGroupOff = RdU64LE(data + rootPos);
    if (rootGroupOff < rootPos + 8 || rootGroupOff + 8 > size) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.formatName = L"Alembic Ogawa 3D Cache (.abc)";

    auto getDataStream = [&](uint64_t childEntry, const uint8_t*& outPtr, size_t& outLen) -> bool {
        if ((childEntry & (1ULL << 63)) == 0) return false;
        uint64_t off = childEntry & ~(1ULL << 63);
        if (off == 0 || off + 8 > size) return false;
        uint64_t bc = RdU64LE(data + static_cast<size_t>(off));
        if (bc > size - static_cast<size_t>(off) - 8) return false;
        outPtr = data + static_cast<size_t>(off) + 8;
        outLen = static_cast<size_t>(bc);
        return true;
    };

    // Reads first sample bytes from either ScalarProperty (no 16B digest or 16B digest) or ArrayProperty
    auto readPropSampleBytes = [&](uint64_t groupOff, const uint8_t*& outRaw, size_t& outRawLen, uint64_t& outExplicitDims) -> bool {
        outRaw = nullptr; outRawLen = 0; outExplicitDims = 0;
        if (groupOff == 0 || (groupOff & (1ULL << 63)) != 0 || groupOff + 8 > size) return false;
        uint64_t nCh = RdU64LE(data + static_cast<size_t>(groupOff));
        if (nCh == 0 || nCh > 65536 || groupOff + 8 + nCh * 8 > size) return false;
        uint64_t ch0 = RdU64LE(data + static_cast<size_t>(groupOff) + 8);
        if (!getDataStream(ch0, outRaw, outRawLen)) return false;
        if (nCh >= 2) {
            uint64_t ch1 = RdU64LE(data + static_cast<size_t>(groupOff) + 16);
            const uint8_t* dimPtr = nullptr; size_t dimLen = 0;
            if (getDataStream(ch1, dimPtr, dimLen) && dimLen >= 24) {
                outExplicitDims = RdU64LE(dimPtr + 16);
            }
        }
        return true;
    };

    auto readTypedArrayProp = [&](uint64_t groupOff, size_t elemByteWidth, const uint8_t*& outBytes, size_t& outCount) -> bool {
        const uint8_t* raw = nullptr; size_t rawLen = 0; uint64_t expDims = 0;
        if (!readPropSampleBytes(groupOff, raw, rawLen, expDims)) return false;
        if (expDims > 0) {
            if (rawLen == 16 + expDims * elemByteWidth) { outBytes = raw + 16; outCount = static_cast<size_t>(expDims); return true; }
            if (rawLen == expDims * elemByteWidth)      { outBytes = raw;      outCount = static_cast<size_t>(expDims); return true; }
        }
        if (rawLen > 16 && ((rawLen - 16) % elemByteWidth) == 0) {
            outBytes = raw + 16;
            outCount = (rawLen - 16) / elemByteWidth;
            return true;
        }
        if (rawLen >= elemByteWidth && (rawLen % elemByteWidth) == 0) {
            outBytes = raw;
            outCount = rawLen / elemByteWidth;
            return true;
        }
        return false;
    };

    auto parseCompoundPropMap = [&](uint64_t gOff, std::unordered_map<std::string, uint64_t>& propMap) -> bool {
        propMap.clear();
        if (gOff == 0 || (gOff & (1ULL << 63)) != 0 || gOff + 8 > size) return false;
        uint64_t nCh = RdU64LE(data + static_cast<size_t>(gOff));
        if (nCh < 2 || nCh > 65536 || gOff + 8 + nCh * 8 > size) return false;
        uint64_t lastCh = RdU64LE(data + static_cast<size_t>(gOff) + 8 + (static_cast<size_t>(nCh) - 1) * 8);
        const uint8_t* hdrPtr = nullptr; size_t hdrLen = 0;
        if (!getDataStream(lastCh, hdrPtr, hdrLen) || hdrLen < 5) return false;

        size_t hp = 0;
        size_t nProps = static_cast<size_t>(nCh) - 1;
        for (size_t i = 0; i < nProps && hp + 4 <= hdrLen; ++i) {
            uint32_t info = RdU32LE(hdrPtr + hp); hp += 4;
            uint32_t propType = info & 0x03u;
            uint32_t szHint   = (info >> 2) & 0x03u;
            bool ok = true;
            auto rdVarU = [&]() -> uint32_t {
                if (szHint == 0 && hp + 1 <= hdrLen) return hdrPtr[hp++];
                if (szHint == 1 && hp + 2 <= hdrLen) { uint32_t v = RdU16LE(hdrPtr + hp); hp += 2; return v; }
                if (szHint == 2 && hp + 4 <= hdrLen) { uint32_t v = RdU32LE(hdrPtr + hp); hp += 4; return v; }
                ok = false; return 0;
            };
            if (propType != 0) {
                bool hasTsIdx     = (info & 0x0100u) != 0;
                bool hasFirstLast = (info & 0x0800u) == 0;
                rdVarU();
                if (hasFirstLast) { rdVarU(); rdVarU(); }
                if (hasTsIdx) rdVarU();
            }
            uint32_t nameSz = rdVarU();
            if (!ok || nameSz == 0 || nameSz > 512 || hp + nameSz + 1 > hdrLen) return false;
            std::string pName(reinterpret_cast<const char*>(hdrPtr + hp), nameSz);
            hp += nameSz;
            uint8_t metaIdx = hdrPtr[hp++];
            if (metaIdx == 0xFFu) {
                uint32_t metaSz = rdVarU();
                if (!ok || hp + metaSz > hdrLen) return false;
                hp += metaSz;
            }
            uint64_t chEntry = RdU64LE(data + static_cast<size_t>(gOff) + 8 + i * 8);
            propMap[pName] = chEntry;
        }
        return !propMap.empty();
    };

    // Evaluates an AbcGeom_Xform_v3 ".xform" CompoundProperty group into local Mat4 + inherits flag
    auto decodeAbcXformGroup = [&](uint64_t xfGroupOff, Mat4& outLocalMat, bool& outInherits) -> bool {
        outLocalMat = Mat4::Identity();
        outInherits = true;
        std::unordered_map<std::string, uint64_t> xfProps;
        if (!parseCompoundPropMap(xfGroupOff, xfProps)) return false;

        auto itInh = xfProps.find(".inherits");
        if (itInh != xfProps.end()) {
            const uint8_t* ib = nullptr; size_t ilen = 0; uint64_t idim = 0;
            if (readPropSampleBytes(itInh->second, ib, ilen, idim)) {
                if (ilen == 17) outInherits = (ib[16] != 0);
                else if (ilen == 1) outInherits = (ib[0] != 0);
            }
        }

        auto itOps  = xfProps.find(".ops");
        auto itVals = xfProps.find(".vals");
        if (itOps == xfProps.end() || itVals == xfProps.end()) return false;

        const uint8_t* opRaw = nullptr; size_t opRawLen = 0; uint64_t opDim = 0;
        const uint8_t* vlRaw = nullptr; size_t vlRawLen = 0; uint64_t vlDim = 0;
        if (!readPropSampleBytes(itOps->second, opRaw, opRawLen, opDim) ||
            !readPropSampleBytes(itVals->second, vlRaw, vlRawLen, vlDim)) return false;

        auto numChansForOp = [](uint8_t opCode) -> size_t {
            switch (opCode & 0x0Fu) {
            case 0: return 3;  // kScaleOperation
            case 1: return 3;  // kTranslateOperation
            case 2: return 4;  // kRotateOperation (axisX, axisY, axisZ, deg)
            case 3: return 16; // kMatrixOperation (row-major M44d)
            case 4: return 1;  // kRotateXOperation
            case 5: return 1;  // kRotateYOperation
            case 6: return 1;  // kRotateZOperation
            default: return 0;
            }
        };

        // Match exact (opBytes, valBytes) combination with or without 16-byte AbcCoreOgawa Murmur3 digest
        const uint8_t* opsPtr = nullptr; size_t nOps = 0;
        const uint8_t* valsPtr = nullptr; size_t nVals = 0;
        for (size_t opSkip : { size_t(16), size_t(0) }) {
            if (opRawLen <= opSkip && !(opSkip == 0 && opRawLen > 0)) continue;
            size_t candOps = opRawLen - opSkip;
            if (candOps == 0 || candOps > 64) continue;
            size_t reqDoubles = 0;
            bool allValid = true;
            for (size_t k = 0; k < candOps; ++k) {
                size_t nc = numChansForOp(opRaw[opSkip + k]);
                if (nc == 0) { allValid = false; break; }
                reqDoubles += nc;
            }
            if (!allValid || reqDoubles == 0) continue;
            if (vlRawLen == 16 + reqDoubles * 8) {
                opsPtr = opRaw + opSkip; nOps = candOps;
                valsPtr = vlRaw + 16; nVals = reqDoubles;
                break;
            }
            if (vlRawLen == reqDoubles * 8) {
                opsPtr = opRaw + opSkip; nOps = candOps;
                valsPtr = vlRaw; nVals = reqDoubles;
                break;
            }
        }
        if (!opsPtr || !valsPtr) return false;

        // In Alembic XformSample, operations in .ops are accumulated from outermost to innermost (same as USD xformOpOrder!)
        Mat4 accum = Mat4::Identity();
        size_t vp = 0;
        for (size_t k = 0; k < nOps && vp < nVals; ++k) {
            uint8_t opType = opsPtr[k] & 0x0Fu;
            Mat4 opM = Mat4::Identity();
            if (opType == 0 && vp + 3 <= nVals) {
                opM = Mat4::Scale(static_cast<float>(RdF64LE(valsPtr + (vp + 0) * 8)),
                                  static_cast<float>(RdF64LE(valsPtr + (vp + 1) * 8)),
                                  static_cast<float>(RdF64LE(valsPtr + (vp + 2) * 8)));
                vp += 3;
            } else if (opType == 1 && vp + 3 <= nVals) {
                opM = Mat4::Translate(static_cast<float>(RdF64LE(valsPtr + (vp + 0) * 8)),
                                      static_cast<float>(RdF64LE(valsPtr + (vp + 1) * 8)),
                                      static_cast<float>(RdF64LE(valsPtr + (vp + 2) * 8)));
                vp += 3;
            } else if (opType == 2 && vp + 4 <= nVals) {
                float ax  = static_cast<float>(RdF64LE(valsPtr + (vp + 0) * 8));
                float ay  = static_cast<float>(RdF64LE(valsPtr + (vp + 1) * 8));
                float az  = static_cast<float>(RdF64LE(valsPtr + (vp + 2) * 8));
                float deg = static_cast<float>(RdF64LE(valsPtr + (vp + 3) * 8));
                opM = Mat4::RotateAxisAngle(deg, ax, ay, az);
                vp += 4;
            } else if (opType == 3 && vp + 16 <= nVals) {
                double d16[16] = {};
                for (int m = 0; m < 16; ++m) d16[m] = RdF64LE(valsPtr + (vp + m) * 8);
                opM = UsdAbcDetail::RowVecMatrix16ToMat4(d16);
                vp += 16;
            } else if (opType == 4 && vp + 1 <= nVals) {
                opM = Mat4::RotateAxisAngle(static_cast<float>(RdF64LE(valsPtr + vp * 8)), 1, 0, 0);
                vp += 1;
            } else if (opType == 5 && vp + 1 <= nVals) {
                opM = Mat4::RotateAxisAngle(static_cast<float>(RdF64LE(valsPtr + vp * 8)), 0, 1, 0);
                vp += 1;
            } else if (opType == 6 && vp + 1 <= nVals) {
                opM = Mat4::RotateAxisAngle(static_cast<float>(RdF64LE(valsPtr + vp * 8)), 0, 0, 1);
                vp += 1;
            }
            accum = Mat4::Mul(accum, opM);
        }
        outLocalMat = accum;
        return true;
    };

    auto emitAbcGeomProps = [&](const std::unordered_map<std::string, uint64_t>& propMap, const Mat4& worldMat) {
        auto itP  = propMap.find("P");
        auto itFI = propMap.find(".faceIndices");
        auto itFC = propMap.find(".faceCounts");
        if (itP == propMap.end() || itFI == propMap.end()) return;

        const uint8_t* pBytes = nullptr;  size_t nVerts = 0;
        const uint8_t* fiBytes = nullptr; size_t nIdx = 0;
        const uint8_t* fcBytes = nullptr; size_t nFaces = 0;
        if (!readTypedArrayProp(itP->second, 12, pBytes, nVerts) ||
            !readTypedArrayProp(itFI->second, 4, fiBytes, nIdx) ||
            nVerts < 3 || nIdx < 3 || nVerts > 2000000 || nIdx > 6000000) return;

        if (itFC != propMap.end()) readTypedArrayProp(itFC->second, 4, fcBytes, nFaces);

        bool flip = (worldMat.Det3x3() < 0.0f);
        uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
        for (size_t v = 0; v < nVerts; ++v) {
            Vec3f lp = {
                RdF32LE(pBytes + v * 12 + 0),
                RdF32LE(pBytes + v * 12 + 4),
                RdF32LE(pBytes + v * 12 + 8)
            };
            mesh.vertices.push_back(worldMat.TransformPoint(lp));
        }

        if (fcBytes && nFaces > 0) {
            size_t k = 0;
            for (size_t f = 0; f < nFaces; ++f) {
                int32_t cnt = static_cast<int32_t>(RdU32LE(fcBytes + f * 4));
                if (cnt >= 3 && k + static_cast<size_t>(cnt) <= nIdx) {
                    int32_t i0 = static_cast<int32_t>(RdU32LE(fiBytes + k * 4));
                    if (i0 >= 0 && static_cast<size_t>(i0) < nVerts) {
                        for (int32_t t = 1; t + 1 < cnt; ++t) {
                            int32_t i1 = static_cast<int32_t>(RdU32LE(fiBytes + (k + t) * 4));
                            int32_t i2 = static_cast<int32_t>(RdU32LE(fiBytes + (k + t + 1) * 4));
                            if (i1 >= 0 && i2 >= 0 && static_cast<size_t>(i1) < nVerts && static_cast<size_t>(i2) < nVerts) {
                                // Alembic PolyMesh uses CW face winding; reverse unless worldMat also flips handedness
                                if (flip) mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i1), vBase + static_cast<uint32_t>(i2), {} });
                                else      mesh.tris.push_back({ vBase + static_cast<uint32_t>(i0), vBase + static_cast<uint32_t>(i2), vBase + static_cast<uint32_t>(i1), {} });
                            }
                        }
                    }
                }
                if (cnt > 0) k += static_cast<size_t>(cnt);
            }
        } else {
            for (size_t k = 0; k + 2 < nIdx; k += 3) {
                uint32_t i0 = RdU32LE(fiBytes + k * 4 + 0);
                uint32_t i1 = RdU32LE(fiBytes + k * 4 + 4);
                uint32_t i2 = RdU32LE(fiBytes + k * 4 + 8);
                if (i0 < nVerts && i1 < nVerts && i2 < nVerts) {
                    if (flip) mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                    else      mesh.tris.push_back({ vBase + i0, vBase + i2, vBase + i1, {} });
                }
            }
        }
    };

    size_t totalVisits = 0;
    std::vector<uint64_t> ancestorStack;
    ancestorStack.reserve(40);

    auto walkOgawa = [&](auto& self, uint64_t gOff, const Mat4& parentWorldMat, int depth) -> void {
        if (depth > 32 || totalVisits > 32768 || gOff == 0 || (gOff & (1ULL << 63)) != 0 || gOff + 8 > size) return;
        for (uint64_t anc : ancestorStack) {
            if (anc == gOff) return; // Guard against corrupt cyclic offsets while allowing DAG multi-parent instancing!
        }
        ++totalVisits;
        ancestorStack.push_back(gOff);

        uint64_t nCh = RdU64LE(data + static_cast<size_t>(gOff));
        if (nCh > 0 && nCh <= 65536 && gOff + 8 + nCh * 8 <= size) {
            std::vector<uint64_t> children(static_cast<size_t>(nCh));
            for (size_t i = 0; i < static_cast<size_t>(nCh); ++i) {
                children[i] = RdU64LE(data + static_cast<size_t>(gOff) + 8 + i * 8);
            }

            Mat4 curWorldMat = parentWorldMat;

            // 1. Check if this group is an Object Group whose child[0] is its top-level CompoundProperty (.prop)
            //    containing ".xform", so child Object Groups [1..nCh-2] inherit curWorldMat!
            std::unordered_map<std::string, uint64_t> ownProps;
            if (parseCompoundPropMap(gOff, ownProps)) {
                auto itXf = ownProps.find(".xform");
                if (itXf != ownProps.end()) {
                    Mat4 loc; bool inh = true;
                    if (decodeAbcXformGroup(itXf->second, loc, inh)) {
                        curWorldMat = inh ? Mat4::Mul(parentWorldMat, loc) : loc;
                    }
                }
                emitAbcGeomProps(ownProps, curWorldMat);
                for (const auto& [nm, chOff] : ownProps) {
                    if (nm != ".xform" && chOff != 0 && (chOff & (1ULL << 63)) == 0) {
                        self(self, chOff, curWorldMat, depth + 1);
                    }
                }
            } else {
                // Object Group or Archive Root Group: inspect child[0] first to update curWorldMat if it holds .xform!
                if (!children.empty() && children[0] != 0 && (children[0] & (1ULL << 63)) == 0) {
                    std::unordered_map<std::string, uint64_t> topProps;
                    if (parseCompoundPropMap(children[0], topProps)) {
                        auto itXf = topProps.find(".xform");
                        if (itXf != topProps.end()) {
                            Mat4 loc; bool inh = true;
                            if (decodeAbcXformGroup(itXf->second, loc, inh)) {
                                curWorldMat = inh ? Mat4::Mul(parentWorldMat, loc) : loc;
                            }
                        }
                    }
                }
                for (uint64_t ch : children) {
                    if (ch != 0 && (ch & (1ULL << 63)) == 0) {
                        self(self, ch, curWorldMat, depth + 1);
                    }
                }
            }
        }

        ancestorStack.pop_back();
    };

    walkOgawa(walkOgawa, rootGroupOff, Mat4::Identity(), 0);
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
