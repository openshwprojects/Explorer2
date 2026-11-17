#pragma once
#include "MeshBuild3D.h"
// Ase3dsMax.h — Autodesk 3ds Max ASCII Scene Export (.ase) 3D Parser
// Parses *3DSMAX_ASCIIEXPORT scenes: *MATERIAL_LIST, *GROUP, *GEOMOBJECT (*NODE_TM,
// *MESH_VERTEX_LIST, *MESH_FACE_LIST, *MESH_VERTCOL, *MESH_NORMALS, *WIREFRAME_COLOR),
// and *SHAPEOBJECT (*SHAPE_LINE) fallback splines.

namespace Formats {

namespace DetailAse {

struct AseToken {
    std::string_view text;
    bool isBraceOpen = false;
    bool isBraceClose = false;
    bool isQuoted = false;
};

inline bool NextAseToken(const char*& cur, const char* end, AseToken& out) {
    while (cur < end) {
        char c = *cur;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++cur;
            continue;
        }
        break;
    }
    if (cur >= end) return false;

    out = {};
    if (*cur == '{') {
        out.text = std::string_view(cur, 1);
        out.isBraceOpen = true;
        ++cur;
        return true;
    }
    if (*cur == '}') {
        out.text = std::string_view(cur, 1);
        out.isBraceClose = true;
        ++cur;
        return true;
    }
    if (*cur == '"') {
        ++cur;
        const char* s = cur;
        while (cur < end && *cur != '"') ++cur;
        out.text = std::string_view(s, static_cast<size_t>(cur - s));
        out.isQuoted = true;
        if (cur < end && *cur == '"') ++cur;
        return true;
    }

    const char* s = cur;
    while (cur < end) {
        char c = *cur;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"') break;
        ++cur;
    }
    out.text = std::string_view(s, static_cast<size_t>(cur - s));
    return true;
}

inline void SkipAseBlock(const char*& cur, const char* end) {
    AseToken tok;
    // Consume up to opening '{' on same statement if not yet consumed
    while (NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) break;
        if (tok.isBraceClose) return;
        if (!tok.text.empty() && tok.text.front() == '*') {
            // Was a scalar property without '{ ... }'; rewind to start of next '*KEYWORD'
            cur = tok.text.data();
            return;
        }
    }
    int depth = 1;
    while (depth > 0 && NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) ++depth;
        else if (tok.isBraceClose) --depth;
    }
}

inline bool ParseAseFloat(std::string_view sv, float& out) {
    if (sv.empty() || sv.size() >= 64) return false;
    char buf[64];
    std::memcpy(buf, sv.data(), sv.size());
    buf[sv.size()] = '\0';
    char* next = nullptr;
    double v = std::strtod(buf, &next);
    if (next == buf || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

inline bool ParseAseInt(std::string_view sv, int64_t& out) {
    while (!sv.empty() && sv.back() == ':') sv.remove_suffix(1);
    if (sv.empty()) return false;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc{} && ptr == sv.data() + sv.size();
}

inline bool NextAseFloat(const char*& cur, const char* end, float& out) {
    AseToken t;
    if (!NextAseToken(cur, end, t) || t.isBraceOpen || t.isBraceClose) return false;
    return ParseAseFloat(t.text, out);
}

inline bool NextAseInt(const char*& cur, const char* end, int64_t& out) {
    AseToken t;
    if (!NextAseToken(cur, end, t) || t.isBraceOpen || t.isBraceClose) return false;
    return ParseAseInt(t.text, out);
}

struct AseMaterialInfo {
    float diffuse[3] = { 0.8f, 0.8f, 0.8f };
};

inline void ParseAseNodeTm(const char*& cur, const char* end, float tm[4][3], bool& hasTm) {
    AseToken tok;
    if (!NextAseToken(cur, end, tok) || !tok.isBraceOpen) return;
    int depth = 1;
    while (depth > 0 && NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) { ++depth; continue; }
        if (tok.isBraceClose) { --depth; continue; }
        if (depth == 1) {
            int row = -1;
            if (tok.text == "*TM_ROW0") row = 0;
            else if (tok.text == "*TM_ROW1") row = 1;
            else if (tok.text == "*TM_ROW2") row = 2;
            else if (tok.text == "*TM_ROW3") row = 3;
            if (row >= 0) {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (NextAseFloat(cur, end, x) && NextAseFloat(cur, end, y) && NextAseFloat(cur, end, z)) {
                    tm[row][0] = x; tm[row][1] = y; tm[row][2] = z;
                    hasTm = true;
                }
            }
        }
    }
}

inline void ParseAseMeshBlock(
    const char*& cur, const char* end,
    bool flipWinding,
    LoadedMesh3D& outMesh)
{
    AseToken tok;
    if (!NextAseToken(cur, end, tok) || !tok.isBraceOpen) return;

    size_t declVerts = 0;
    size_t declFaces = 0;
    std::vector<Vec3f> localVerts;
    std::vector<bool> vertPresent;
    struct LocalTri { uint32_t a, b, c; };
    std::vector<LocalTri> localTris;

    int depth = 1;
    while (depth > 0 && NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) { ++depth; continue; }
        if (tok.isBraceClose) { --depth; continue; }
        if (depth != 1) continue;

        if (tok.text == "*MESH_NUMVERTEX") {
            int64_t nv = 0;
            if (NextAseInt(cur, end, nv) && nv > 0 && nv <= 10000000) {
                declVerts = static_cast<size_t>(nv);
                localVerts.assign(declVerts, { 0.0f, 0.0f, 0.0f });
                vertPresent.assign(declVerts, false);
            }
        } else if (tok.text == "*MESH_NUMFACES") {
            int64_t nf = 0;
            if (NextAseInt(cur, end, nf) && nf > 0 && nf <= 20000000) {
                declFaces = static_cast<size_t>(nf);
                localTris.reserve(std::min<size_t>(declFaces, 1000000));
            }
        } else if (tok.text == "*MESH_VERTEX_LIST") {
            AseToken bt;
            if (!NextAseToken(cur, end, bt) || !bt.isBraceOpen) continue;
            int vDepth = 1;
            while (vDepth > 0 && NextAseToken(cur, end, bt)) {
                if (bt.isBraceOpen) { ++vDepth; continue; }
                if (bt.isBraceClose) { --vDepth; continue; }
                if (vDepth == 1 && bt.text == "*MESH_VERTEX") {
                    int64_t idx = -1;
                    float x = 0.0f, y = 0.0f, z = 0.0f;
                    if (NextAseInt(cur, end, idx) &&
                        NextAseFloat(cur, end, x) &&
                        NextAseFloat(cur, end, y) &&
                        NextAseFloat(cur, end, z) &&
                        idx >= 0 && idx < 10000000)
                    {
                        size_t ui = static_cast<size_t>(idx);
                        if (ui >= localVerts.size()) {
                            localVerts.resize(ui + 1, { 0.0f, 0.0f, 0.0f });
                            vertPresent.resize(ui + 1, false);
                        }
                        // Convert 3ds Max Z-up world coordinates (x, y, z) -> studio Y-up (x, z, -y)
                        localVerts[ui] = { x, z, -y };
                        vertPresent[ui] = true;
                    }
                }
            }
        } else if (tok.text == "*MESH_FACE_LIST") {
            AseToken bt;
            if (!NextAseToken(cur, end, bt) || !bt.isBraceOpen) continue;
            int fDepth = 1;
            while (fDepth > 0 && NextAseToken(cur, end, bt)) {
                if (bt.isBraceOpen) { ++fDepth; continue; }
                if (bt.isBraceClose) { --fDepth; continue; }
                if (fDepth == 1 && bt.text == "*MESH_FACE") {
                    // Read tokens until next *MESH_FACE or closing '}'
                    int64_t ia = -1, ib = -1, ic = -1;
                    AseToken ft;
                    // Face index token (e.g. "0:" or "0" + ":")
                    if (!NextAseToken(cur, end, ft)) break;
                    if (ft.isBraceClose) { --fDepth; break; }
                    if (ft.text.back() != ':') {
                        const char* save = cur;
                        AseToken maybeColon;
                        if (NextAseToken(cur, end, maybeColon) && maybeColon.text != ":") {
                            cur = save;
                        }
                    }
                    while (cur < end) {
                        const char* peekPos = cur;
                        AseToken kv;
                        if (!NextAseToken(cur, end, kv)) break;
                        if (kv.isBraceClose) {
                            --fDepth;
                            break;
                        }
                        if (kv.isBraceOpen) {
                            ++fDepth;
                            break;
                        }
                        if (kv.text == "*MESH_FACE") {
                            cur = peekPos;
                            break;
                        }
                        auto parseLabeledIdx = [&](std::string_view prefix, int64_t& dst) -> bool {
                            if (kv.text == prefix) {
                                return NextAseInt(cur, end, dst);
                            }
                            if (kv.text.size() > prefix.size() && kv.text.substr(0, prefix.size()) == prefix) {
                                return ParseAseInt(kv.text.substr(prefix.size()), dst);
                            }
                            return false;
                        };
                        (void)(parseLabeledIdx("A:", ia) ||
                               parseLabeledIdx("B:", ib) ||
                               parseLabeledIdx("C:", ic));
                    }
                    if (ia >= 0 && ib >= 0 && ic >= 0) {
                        localTris.push_back({
                            static_cast<uint32_t>(ia),
                            static_cast<uint32_t>(ib),
                            static_cast<uint32_t>(ic)
                        });
                    }
                }
            }
        } else if (tok.text == "*MESH_CVERTLIST" || tok.text == "*MESH_CFACELIST" ||
                   tok.text == "*MESH_TVERTLIST" || tok.text == "*MESH_TFACELIST" ||
                   tok.text == "*MESH_MAPPINGCHANNEL" || tok.text == "*MESH_NORMALS") {
            SkipAseBlock(cur, end);
        }
    }

    if (localVerts.empty() || localTris.empty()) return;
    const uint32_t base = static_cast<uint32_t>(outMesh.vertices.size());
    outMesh.vertices.insert(outMesh.vertices.end(), localVerts.begin(), localVerts.end());
    const size_t nv = localVerts.size();
    for (const auto& t : localTris) {
        if (t.a < nv && t.b < nv && t.c < nv) {
            if (!flipWinding) {
                outMesh.tris.push_back({ base + t.a, base + t.b, base + t.c, {} });
            } else {
                outMesh.tris.push_back({ base + t.a, base + t.c, base + t.b, {} });
            }
        }
    }
}

inline void ParseAseGeomObject(const char*& cur, const char* end, LoadedMesh3D& outMesh) {
    AseToken tok;
    if (!NextAseToken(cur, end, tok) || !tok.isBraceOpen) return;

    float tm[4][3] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f }
    };
    bool hasTm = false;
    bool flipWinding = false;
    float wireColor[3] = { 0.7f, 0.7f, 0.7f };
    int64_t matRef = -1;

    int depth = 1;
    while (depth > 0 && NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) { ++depth; continue; }
        if (tok.isBraceClose) { --depth; continue; }
        if (depth != 1) continue;

        if (tok.text == "*NODE_TM") {
            ParseAseNodeTm(cur, end, tm, hasTm);
            if (hasTm) {
                float det = tm[0][0] * (tm[1][1] * tm[2][2] - tm[1][2] * tm[2][1]) -
                            tm[0][1] * (tm[1][0] * tm[2][2] - tm[1][2] * tm[2][0]) +
                            tm[0][2] * (tm[1][0] * tm[2][1] - tm[1][1] * tm[2][0]);
                flipWinding = (det < 0.0f);
            }
        } else if (tok.text == "*MESH") {
            ParseAseMeshBlock(cur, end, flipWinding, outMesh);
        } else if (tok.text == "*WIREFRAME_COLOR") {
            (void)NextAseFloat(cur, end, wireColor[0]);
            (void)NextAseFloat(cur, end, wireColor[1]);
            (void)NextAseFloat(cur, end, wireColor[2]);
        } else if (tok.text == "*MATERIAL_REF") {
            (void)NextAseInt(cur, end, matRef);
        } else if (!tok.text.empty() && tok.text.front() == '*') {
            const char* save = cur;
            AseToken nextTok;
            if (NextAseToken(cur, end, nextTok)) {
                if (nextTok.isBraceOpen) {
                    int subD = 1;
                    while (subD > 0 && NextAseToken(cur, end, nextTok)) {
                        if (nextTok.isBraceOpen) ++subD;
                        else if (nextTok.isBraceClose) --subD;
                    }
                } else if (nextTok.isBraceClose) {
                    --depth;
                } else if (!nextTok.isQuoted && !nextTok.text.empty() && nextTok.text.front() == '*') {
                    cur = save;
                }
            }
        }
    }
    (void)wireColor;
    (void)matRef;
}

inline void ParseAseShapeObject(const char*& cur, const char* end, std::vector<std::vector<Vec3f>>& outPolylines) {
    AseToken tok;
    if (!NextAseToken(cur, end, tok) || !tok.isBraceOpen) return;

    int depth = 1;
    while (depth > 0 && NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) { ++depth; continue; }
        if (tok.isBraceClose) { --depth; continue; }
        if (depth == 1 && tok.text == "*SHAPE_LINE") {
            int64_t lineIdx = 0;
            (void)NextAseInt(cur, end, lineIdx);
            AseToken bt;
            if (!NextAseToken(cur, end, bt) || !bt.isBraceOpen) continue;
            std::vector<Vec3f> poly;
            bool closed = false;
            int lDepth = 1;
            while (lDepth > 0 && NextAseToken(cur, end, bt)) {
                if (bt.isBraceOpen) { ++lDepth; continue; }
                if (bt.isBraceClose) { --lDepth; continue; }
                if (lDepth == 1) {
                    if (bt.text == "*SHAPE_CLOSED") {
                        closed = true;
                    } else if (bt.text == "*SHAPE_VERTEX_KNOT" || bt.text == "*SHAPE_VERTEX_INTERP") {
                        int64_t vIdx = 0;
                        float x = 0.0f, y = 0.0f, z = 0.0f;
                        if (NextAseInt(cur, end, vIdx) &&
                            NextAseFloat(cur, end, x) &&
                            NextAseFloat(cur, end, y) &&
                            NextAseFloat(cur, end, z)) {
                            poly.push_back({ x, z, -y });
                        }
                    }
                }
            }
            if (closed && poly.size() >= 3) {
                poly.push_back(poly.front());
            }
            if (poly.size() >= 2) {
                outPolylines.push_back(std::move(poly));
            }
        }
    }
}

inline void ParseAseSceneContainer(
    const char*& cur, const char* end, bool insideBraces,
    std::vector<AseMaterialInfo>& materials,
    std::vector<std::vector<Vec3f>>& shapeLines,
    LoadedMesh3D& outMesh)
{
    AseToken tok;
    int depth = insideBraces ? 1 : 0;
    while (NextAseToken(cur, end, tok)) {
        if (tok.isBraceOpen) { ++depth; continue; }
        if (tok.isBraceClose) {
            if (insideBraces && --depth <= 0) return;
            continue;
        }
        if (insideBraces && depth != 1) continue;

        if (tok.text == "*GEOMOBJECT") {
            ParseAseGeomObject(cur, end, outMesh);
        } else if (tok.text == "*SHAPEOBJECT") {
            ParseAseShapeObject(cur, end, shapeLines);
        } else if (tok.text == "*GROUP") {
            // Optional group name string before '{'
            AseToken gt;
            while (NextAseToken(cur, end, gt)) {
                if (gt.isBraceOpen) {
                    ParseAseSceneContainer(cur, end, true, materials, shapeLines, outMesh);
                    break;
                }
                if (gt.isBraceClose) break;
            }
        } else if (tok.text == "*MATERIAL_LIST") {
            AseToken bt;
            if (!NextAseToken(cur, end, bt) || !bt.isBraceOpen) continue;
            int mDepth = 1;
            while (mDepth > 0 && NextAseToken(cur, end, bt)) {
                if (bt.isBraceOpen) { ++mDepth; continue; }
                if (bt.isBraceClose) { --mDepth; continue; }
                if (bt.text == "*MATERIAL_DIFFUSE") {
                    AseMaterialInfo mi;
                    if (NextAseFloat(cur, end, mi.diffuse[0]) &&
                        NextAseFloat(cur, end, mi.diffuse[1]) &&
                        NextAseFloat(cur, end, mi.diffuse[2])) {
                        materials.push_back(mi);
                    }
                }
            }
        } else if (tok.text == "*SCENE" || tok.text == "*HELPEROBJECT" ||
                   tok.text == "*LIGHTOBJECT" || tok.text == "*CAMERAOBJECT") {
            SkipAseBlock(cur, end);
        }
    }
}

} // namespace DetailAse

inline bool ParseAseMesh(std::string_view sv, LoadedMesh3D& outMesh) {
    if (sv.size() < 20) return false;
    std::string_view head = sv.substr(0, std::min<size_t>(1024, sv.size()));
    if (head.find("*3DSMAX_ASCIIEXPORT") == std::string_view::npos) {
        return false;
    }

    outMesh.formatName = L"3ds Max ASCII Scene Export (.ase)";
    const char* cur = sv.data();
    const char* const end = cur + sv.size();

    std::vector<DetailAse::AseMaterialInfo> materials;
    std::vector<std::vector<Vec3f>> shapeLines;
    DetailAse::ParseAseSceneContainer(cur, end, false, materials, shapeLines, outMesh);

    if (outMesh.tris.empty() && !shapeLines.empty()) {
        Vec3f bmin = shapeLines[0][0], bmax = bmin;
        for (const auto& poly : shapeLines) {
            for (const auto& p : poly) {
                bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
            }
        }
        float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
        float rad = span * 0.008f;
        for (const auto& poly : shapeLines) {
            for (size_t i = 0; i + 1 < poly.size(); ++i) {
                MeshBuild3D::EmitEdgeStrut(outMesh, poly[i], poly[i + 1], rad);
            }
        }
    }

    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
