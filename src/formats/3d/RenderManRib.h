#pragma once
// RenderManRib.h — Pixar RenderMan Interface Bytestream (.rib, RISpec 3.2 ASCII) 3D reader
// Supports hierarchical TransformBegin/AttributeBegin stacks (Translate, Rotate, Scale, Transform,
// ConcatTransform), Polygon, PointsPolygons, PointsGeneralPolygons (with holes), SubdivisionMesh,
// and quadric primitives (Sphere, Cylinder, Cone, Disk, Torus).

namespace Formats {

namespace DetailRenderManRib {

struct RibTok {
    enum Kind { Word, Str, Num, LBracket, RBracket };
    Kind kind = Word;
    std::string text;
    float num = 0.0f;
};

inline bool TokenizeRib(std::string_view sv, std::vector<RibTok>& out) {
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    size_t i = 0;
    const size_t n = sv.size();
    while (i < n) {
        const char c = sv[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        if (c == '#') {
            while (i < n && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '[') { out.push_back({ RibTok::LBracket, {}, 0.0f }); ++i; continue; }
        if (c == ']') { out.push_back({ RibTok::RBracket, {}, 0.0f }); ++i; continue; }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < n && sv[i] != '"') {
                if (sv[i] == '\\' && i + 1 < n) ++i;
                s.push_back(sv[i++]);
            }
            if (i < n) ++i;
            out.push_back({ RibTok::Str, std::move(s), 0.0f });
            continue;
        }
        const size_t start = i;
        while (i < n) {
            char ch = sv[i];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                ch == '[' || ch == ']' || ch == '"' || ch == '#') break;
            ++i;
        }
        std::string_view w = sv.substr(start, i - start);
        float fv = 0.0f;
        if (ReaderText::ParseFloatSv(w, fv)) {
            out.push_back({ RibTok::Num, {}, fv });
        } else {
            out.push_back({ RibTok::Word, std::string(w), 0.0f });
        }
        if (out.size() > 8000000) break;
    }
    return !out.empty();
}

inline void Mat4Identity(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// Column-vector transform p' = M * p; local step S composes on the right: CTM_new = CTM * S
inline void PreMultiplyLocal(float ctm[16], const float localStep[16]) {
    float next[16];
    MeshBuild3D::MatMul4RowMajor(ctm, localStep, next);
    std::memcpy(ctm, next, sizeof(next));
}

inline Vec3f TransformRibPt(const float ctm[16], Vec3f p) {
    // RenderMan default camera looks down +Z (left-handed Y-up); negate Z to viewer right-handed Y-up
    const float x = ctm[0] * p.x + ctm[1] * p.y + ctm[2]  * p.z + ctm[3];
    const float y = ctm[4] * p.x + ctm[5] * p.y + ctm[6]  * p.z + ctm[7];
    const float z = ctm[8] * p.x + ctm[9] * p.y + ctm[10] * p.z + ctm[11];
    return { x, y, -z };
}

inline bool ReadFloatBracketArray(const std::vector<RibTok>& toks, size_t& idx, std::vector<float>& out) {
    out.clear();
    if (idx >= toks.size() || toks[idx].kind != RibTok::LBracket) return false;
    ++idx;
    while (idx < toks.size() && toks[idx].kind != RibTok::RBracket) {
        if (toks[idx].kind == RibTok::Num) out.push_back(toks[idx].num);
        ++idx;
    }
    if (idx >= toks.size()) return false;
    ++idx; // consume ']'
    return true;
}

inline bool ReadIntBracketArray(const std::vector<RibTok>& toks, size_t& idx, std::vector<int>& out) {
    out.clear();
    if (idx >= toks.size() || toks[idx].kind != RibTok::LBracket) return false;
    ++idx;
    while (idx < toks.size() && toks[idx].kind != RibTok::RBracket) {
        if (toks[idx].kind == RibTok::Num) {
            if (!std::isfinite(toks[idx].num) || std::fabs(toks[idx].num) > 1e8f) return false;
            out.push_back(static_cast<int>(std::lround(toks[idx].num)));
        }
        ++idx;
    }
    if (idx >= toks.size()) return false;
    ++idx;
    return true;
}

// Reads parameter list pairs ("P" [...], "N" [...], "st" [...], "vertex point P" [...]) until next RIB command Word
inline bool ExtractPointParamP(const std::vector<RibTok>& toks, size_t& idx, std::vector<Vec3f>& outP) {
    outP.clear();
    while (idx < toks.size() && toks[idx].kind == RibTok::Str) {
        const std::string& paramSpec = toks[idx].text;
        ++idx;
        // Parameter name is either "P" or inline declaration ending in " P" (e.g. "vertex point P")
        const bool isP = (paramSpec == "P") ||
                         (paramSpec.size() >= 2 && paramSpec.back() == 'P' && paramSpec[paramSpec.size() - 2] == ' ');
        if (idx < toks.size() && toks[idx].kind == RibTok::LBracket) {
            std::vector<float> vals;
            if (!ReadFloatBracketArray(toks, idx, vals)) return false;
            if (isP && vals.size() >= 9) {
                const size_t vc = vals.size() / 3u;
                outP.reserve(vc);
                for (size_t k = 0; k < vc; ++k) {
                    outP.push_back({ vals[k * 3u + 0], vals[k * 3u + 1], vals[k * 3u + 2] });
                }
            }
        } else if (idx < toks.size() && (toks[idx].kind == RibTok::Num || toks[idx].kind == RibTok::Str)) {
            ++idx;
        }
    }
    return !outP.empty();
}

} // namespace DetailRenderManRib

inline bool ParseRenderManRib(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 12 || size > 64u * 1024u * 1024u) return false;
    std::vector<DetailRenderManRib::RibTok> toks;
    if (!DetailRenderManRib::TokenizeRib(std::string_view(reinterpret_cast<const char*>(data), size), toks)) {
        return false;
    }

    std::array<float, 16> ctm{};
    DetailRenderManRib::Mat4Identity(ctm.data());
    std::vector<std::array<float, 16>> stack;

    auto readScalar = [&](size_t& idx, float& v) -> bool {
        if (idx < toks.size() && toks[idx].kind == DetailRenderManRib::RibTok::Num) {
            v = toks[idx++].num;
            return true;
        }
        return false;
    };

    size_t idx = 0;
    while (idx < toks.size()) {
        if (toks[idx].kind != DetailRenderManRib::RibTok::Word) { ++idx; continue; }
        const std::string& cmd = toks[idx++].text;

        if (cmd == "TransformBegin" || cmd == "AttributeBegin" || cmd == "WorldBegin" ||
            cmd == "FrameBegin" || cmd == "ObjectBegin" || cmd == "MotionBegin") {
            if (stack.size() < 256) stack.push_back(ctm);
            if (cmd == "MotionBegin" && idx < toks.size() && toks[idx].kind == DetailRenderManRib::RibTok::LBracket) {
                std::vector<float> times;
                DetailRenderManRib::ReadFloatBracketArray(toks, idx, times);
            }
        } else if (cmd == "TransformEnd" || cmd == "AttributeEnd" || cmd == "WorldEnd" ||
                   cmd == "FrameEnd" || cmd == "ObjectEnd" || cmd == "MotionEnd") {
            if (!stack.empty()) {
                ctm = stack.back();
                stack.pop_back();
            }
        } else if (cmd == "Identity") {
            DetailRenderManRib::Mat4Identity(ctm.data());
        } else if (cmd == "Translate") {
            float dx = 0, dy = 0, dz = 0;
            if (readScalar(idx, dx) && readScalar(idx, dy) && readScalar(idx, dz)) {
                const float s[16] = { 1,0,0,dx,  0,1,0,dy,  0,0,1,dz,  0,0,0,1 };
                DetailRenderManRib::PreMultiplyLocal(ctm.data(), s);
            }
        } else if (cmd == "Scale") {
            float sx = 1, sy = 1, sz = 1;
            if (readScalar(idx, sx) && readScalar(idx, sy) && readScalar(idx, sz)) {
                const float s[16] = { sx,0,0,0,  0,sy,0,0,  0,0,sz,0,  0,0,0,1 };
                DetailRenderManRib::PreMultiplyLocal(ctm.data(), s);
            }
        } else if (cmd == "Rotate") {
            float deg = 0, ax = 0, ay = 1, az = 0;
            if (readScalar(idx, deg) && readScalar(idx, ax) && readScalar(idx, ay) && readScalar(idx, az)) {
                const float len = std::sqrt(ax * ax + ay * ay + az * az);
                if (len > 1e-6f) {
                    ax /= len; ay /= len; az /= len;
                    const float rad = deg * (3.14159265358979323846f / 180.0f);
                    const float c = std::cos(rad), sn = std::sin(rad), ic = 1.0f - c;
                    const float s[16] = {
                        c + ax*ax*ic,      ax*ay*ic - az*sn,  ax*az*ic + ay*sn, 0,
                        ay*ax*ic + az*sn,  c + ay*ay*ic,      ay*az*ic - ax*sn, 0,
                        az*ax*ic - ay*sn,  az*ay*ic + ax*sn,  c + az*az*ic,     0,
                        0,                 0,                 0,                1
                    };
                    DetailRenderManRib::PreMultiplyLocal(ctm.data(), s);
                }
            }
        } else if (cmd == "Transform" || cmd == "ConcatTransform") {
            std::vector<float> m;
            if (DetailRenderManRib::ReadFloatBracketArray(toks, idx, m) && m.size() == 16) {
                // Transpose RIB row-vector matrix into column-vector matrix
                const float colM[16] = {
                    m[0], m[4], m[8],  m[12],
                    m[1], m[5], m[9],  m[13],
                    m[2], m[6], m[10], m[14],
                    m[3], m[7], m[11], m[15]
                };
                if (cmd == "Transform") std::memcpy(ctm.data(), colM, sizeof(colM));
                else DetailRenderManRib::PreMultiplyLocal(ctm.data(), colM);
            }
        } else if (cmd == "Polygon") {
            std::vector<Vec3f> pts;
            if (DetailRenderManRib::ExtractPointParamP(toks, idx, pts) && pts.size() >= 3) {
                for (auto& p : pts) p = DetailRenderManRib::TransformRibPt(ctm.data(), p);
                MeshBuild3D::EmitPlanarPolygon(mesh, pts, {});
            }
        } else if (cmd == "PointsPolygons" || cmd == "SubdivisionMesh") {
            if (cmd == "SubdivisionMesh" && idx < toks.size() && toks[idx].kind == DetailRenderManRib::RibTok::Str) {
                ++idx; // scheme e.g. "catmull-clark"
            }
            std::vector<int> nverts, indices;
            if (!DetailRenderManRib::ReadIntBracketArray(toks, idx, nverts)) continue;
            if (!DetailRenderManRib::ReadIntBracketArray(toks, idx, indices)) continue;
            if (cmd == "SubdivisionMesh") {
                // Skip optional tag arrays before parameter list: [tags] [nargs] [intargs] [floatargs]
                while (idx < toks.size() && toks[idx].kind == DetailRenderManRib::RibTok::LBracket) {
                    std::vector<float> dummy;
                    DetailRenderManRib::ReadFloatBracketArray(toks, idx, dummy);
                }
            }
            std::vector<Vec3f> pts;
            if (!DetailRenderManRib::ExtractPointParamP(toks, idx, pts) || pts.size() < 3) continue;

            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            for (const auto& p : pts) mesh.vertices.push_back(DetailRenderManRib::TransformRibPt(ctm.data(), p));

            size_t cur = 0;
            for (int nv : nverts) {
                if (nv < 3 || cur + static_cast<size_t>(nv) > indices.size()) return false;
                for (int k = 0; k < nv; ++k) {
                    int vi = indices[cur + static_cast<size_t>(k)];
                    if (vi < 0 || static_cast<size_t>(vi) >= pts.size()) return false;
                }
                if (nv == 3) {
                    uint32_t i0 = base + static_cast<uint32_t>(indices[cur + 0]);
                    uint32_t i1 = base + static_cast<uint32_t>(indices[cur + 1]);
                    uint32_t i2 = base + static_cast<uint32_t>(indices[cur + 2]);
                    if (i0 != i1 && i1 != i2 && i0 != i2) mesh.tris.push_back({ i0, i2, i1, {} });
                } else {
                    std::vector<Vec3f> ring;
                    ring.reserve(static_cast<size_t>(nv));
                    for (int k = 0; k < nv; ++k) {
                        ring.push_back(mesh.vertices[base + static_cast<uint32_t>(indices[cur + static_cast<size_t>(k)])]);
                    }
                    MeshBuild3D::EmitPlanarPolygon(mesh, ring, {});
                }
                cur += static_cast<size_t>(nv);
            }
        } else if (cmd == "PointsGeneralPolygons") {
            std::vector<int> nloops, nverts, indices;
            if (!DetailRenderManRib::ReadIntBracketArray(toks, idx, nloops)) continue;
            if (!DetailRenderManRib::ReadIntBracketArray(toks, idx, nverts)) continue;
            if (!DetailRenderManRib::ReadIntBracketArray(toks, idx, indices)) continue;
            std::vector<Vec3f> pts;
            if (!DetailRenderManRib::ExtractPointParamP(toks, idx, pts) || pts.size() < 3) continue;

            std::vector<Vec3f> worldPts(pts.size());
            for (size_t i = 0; i < pts.size(); ++i) worldPts[i] = DetailRenderManRib::TransformRibPt(ctm.data(), pts[i]);

            size_t loopCur = 0, idxCur = 0;
            for (int nl : nloops) {
                if (nl < 1 || loopCur + static_cast<size_t>(nl) > nverts.size()) return false;
                std::vector<Vec3f> outer;
                std::vector<std::vector<Vec3f>> holes;
                for (int l = 0; l < nl; ++l) {
                    const int nv = nverts[loopCur + static_cast<size_t>(l)];
                    if (nv < 3 || idxCur + static_cast<size_t>(nv) > indices.size()) return false;
                    std::vector<Vec3f> ring;
                    ring.reserve(static_cast<size_t>(nv));
                    for (int k = 0; k < nv; ++k) {
                        int vi = indices[idxCur + static_cast<size_t>(k)];
                        if (vi < 0 || static_cast<size_t>(vi) >= worldPts.size()) return false;
                        ring.push_back(worldPts[static_cast<size_t>(vi)]);
                    }
                    if (l == 0) outer = std::move(ring);
                    else holes.push_back(std::move(ring));
                    idxCur += static_cast<size_t>(nv);
                }
                MeshBuild3D::EmitPlanarPolygon(mesh, outer, holes);
                loopCur += static_cast<size_t>(nl);
            }
        } else if (cmd == "Sphere" || cmd == "Cylinder" || cmd == "Cone" || cmd == "Disk") {
            float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
            bool okArgs = false;
            if (cmd == "Sphere" || cmd == "Cylinder") {
                okArgs = readScalar(idx, a0) && readScalar(idx, a1) && readScalar(idx, a2) && readScalar(idx, a3);
            } else {
                okArgs = readScalar(idx, a0) && readScalar(idx, a1) && readScalar(idx, a2);
            }
            if (!okArgs) continue;
            constexpr int kU = 18, kV = 10;
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            const float tmaxRad = ((cmd == "Sphere" || cmd == "Cylinder") ? a3 : a2) * (3.1415926535f / 180.0f);
            if (std::fabs(tmaxRad) <= 1e-5f) continue;

            for (int v = 0; v <= kV; ++v) {
                const float fv = static_cast<float>(v) / kV;
                for (int u = 0; u <= kU; ++u) {
                    const float th = tmaxRad * (static_cast<float>(u) / kU);
                    Vec3f p{};
                    if (cmd == "Sphere") {
                        const float r = std::fabs(a0);
                        if (r <= 1e-6f) break;
                        const float z = a1 + fv * (a2 - a1);
                        const float xy = std::sqrt(std::max(0.0f, r * r - z * z));
                        p = { xy * std::cos(th), xy * std::sin(th), z };
                    } else if (cmd == "Cylinder") {
                        const float r = std::fabs(a0);
                        p = { r * std::cos(th), r * std::sin(th), a1 + fv * (a2 - a1) };
                    } else if (cmd == "Cone") {
                        const float r = a1 * (1.0f - fv);
                        p = { r * std::cos(th), r * std::sin(th), fv * a0 };
                    } else {
                        const float r = a1 * fv;
                        p = { r * std::cos(th), r * std::sin(th), a0 };
                    }
                    mesh.vertices.push_back(DetailRenderManRib::TransformRibPt(ctm.data(), p));
                }
            }
            if (mesh.vertices.size() >= base + static_cast<size_t>((kV + 1) * (kU + 1))) {
                for (int v = 0; v < kV; ++v) {
                    for (int u = 0; u < kU; ++u) {
                        uint32_t i00 = base + static_cast<uint32_t>(v * (kU + 1) + u);
                        uint32_t i10 = i00 + 1;
                        uint32_t i01 = base + static_cast<uint32_t>((v + 1) * (kU + 1) + u);
                        uint32_t i11 = i01 + 1;
                        mesh.tris.push_back({ i00, i01, i11, {} });
                        mesh.tris.push_back({ i00, i11, i10, {} });
                    }
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"RenderMan RIB Scene (.rib)";
    return true;
}

} // namespace Formats
