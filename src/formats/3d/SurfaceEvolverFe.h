#pragma once
// SurfaceEvolverFe.h — Ken Brakke's Surface Evolver (.fe) minimal-surface & soap-film 3D reader
// Strips // and /* */ comments, evaluates PARAMETER constants (including unary +/-), parses
// `vertices`, `edges`, and signed oriented-edge `faces` loops (stopping at `bodies` / `read`),
// and ear-clips N-gon soap films or emits wireframe struts for string-mode models.

namespace Formats {

namespace DetailSurfaceEvolver {

inline std::string StripFeComments(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        if (i + 1 < raw.size() && raw[i] == '/' && raw[i + 1] == '/') {
            i += 2;
            while (i < raw.size() && raw[i] != '\n') ++i;
        } else if (i + 1 < raw.size() && raw[i] == '/' && raw[i + 1] == '*') {
            i += 2;
            while (i + 1 < raw.size() && !(raw[i] == '*' && raw[i + 1] == '/')) ++i;
            i = std::min(raw.size(), i + 2);
            out.push_back(' ');
        } else {
            out.push_back(raw[i++]);
        }
    }
    return out;
}

inline bool EvalFeScalar(
    std::string_view tok,
    const std::unordered_map<std::string, float>& params,
    float& outVal)
{
    tok = ReaderText::TrimSv(tok);
    if (tok.empty()) return false;
    if (ReaderText::ParseFloatSv(tok, outVal)) return true;

    float sign = 1.0f;
    if (tok.front() == '-') { sign = -1.0f; tok.remove_prefix(1); }
    else if (tok.front() == '+') { tok.remove_prefix(1); }

    // Support simple param*const or param/const or bare param
    const size_t mul = tok.find('*');
    const size_t div = tok.find('/');
    if (mul != std::string_view::npos) {
        float a = 0.0f, b = 0.0f;
        if (EvalFeScalar(tok.substr(0, mul), params, a) && EvalFeScalar(tok.substr(mul + 1), params, b)) {
            outVal = sign * a * b;
            return true;
        }
        return false;
    }
    if (div != std::string_view::npos) {
        float a = 0.0f, b = 0.0f;
        if (EvalFeScalar(tok.substr(0, div), params, a) && EvalFeScalar(tok.substr(div + 1), params, b) && std::fabs(b) > 1e-12f) {
            outVal = sign * (a / b);
            return true;
        }
        return false;
    }

    auto it = params.find(ReaderText::ToLowerAscii(tok));
    if (it != params.end()) {
        outVal = sign * it->second;
        return true;
    }
    return false;
}

struct FeEdge {
    uint32_t tail = 0;
    uint32_t head = 0;
};

} // namespace DetailSurfaceEvolver

inline bool ParseSurfaceEvolverFe(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;
    std::string_view rawSv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (rawSv.find('\0') != std::string_view::npos) return false;

    const std::string clean = DetailSurfaceEvolver::StripFeComments(rawSv);
    std::string_view sv(clean);

    enum class Sec { Preamble, Vertices, Edges, Faces, Done };
    Sec sec = Sec::Preamble;
    int spaceDim = 3;

    std::unordered_map<std::string, float> params;
    params["pi"] = 3.14159265358979323846f;

    std::unordered_map<int, uint32_t> vertMap;
    std::unordered_map<int, DetailSurfaceEvolver::FeEdge> edgeMap;
    std::vector<std::vector<int>> rawFaceLoops;

    bool sawVerticesKw = false;
    bool sawEdgesKw = false;

    size_t pos = 0;
    std::string_view line;
    while (sec != Sec::Done && ReaderText::NextLineSv(sv, pos, line)) {
        // Line continuation with trailing backslash in Surface Evolver
        std::string joined;
        std::string_view cur = ReaderText::TrimSv(line);
        while (!cur.empty() && cur.back() == '\\') {
            joined.append(cur.substr(0, cur.size() - 1));
            joined.push_back(' ');
            std::string_view nextL;
            if (!ReaderText::NextLineSv(sv, pos, nextL)) { cur = {}; break; }
            cur = ReaderText::TrimSv(nextL);
        }
        if (!joined.empty()) {
            joined.append(cur);
            cur = joined;
        }
        if (cur.empty()) continue;

        std::string_view probe = cur, firstTok;
        if (!ReaderText::NextTokenSv(probe, firstTok)) continue;

        if (ReaderText::AsciiEqCI(firstTok, "vertices")) {
            sec = Sec::Vertices;
            sawVerticesKw = true;
            continue;
        }
        if (ReaderText::AsciiEqCI(firstTok, "edges")) {
            sec = Sec::Edges;
            sawEdgesKw = true;
            continue;
        }
        if (ReaderText::AsciiEqCI(firstTok, "faces") || ReaderText::AsciiEqCI(firstTok, "facets")) {
            sec = Sec::Faces;
            continue;
        }
        if (ReaderText::AsciiEqCI(firstTok, "bodies") || ReaderText::AsciiEqCI(firstTok, "read")) {
            sec = Sec::Done;
            break;
        }

        if (sec == Sec::Preamble) {
            if (ReaderText::AsciiEqCI(firstTok, "parameter") || ReaderText::AsciiEqCI(firstTok, "define")) {
                std::string_view pName;
                if (ReaderText::NextTokenSv(probe, pName)) {
                    const size_t eq = probe.find('=');
                    std::string_view valPart = (eq != std::string_view::npos) ? probe.substr(eq + 1) : probe;
                    std::string_view vTok;
                    float pv = 0.0f;
                    if (ReaderText::NextTokenSv(valPart, vTok) && DetailSurfaceEvolver::EvalFeScalar(vTok, params, pv)) {
                        params[ReaderText::ToLowerAscii(pName)] = pv;
                    }
                }
            } else if (ReaderText::AsciiEqCI(firstTok, "space_dimension")) {
                std::string_view dTok;
                int d = 3;
                if (ReaderText::NextTokenSv(probe, dTok) && ReaderText::ParseIntSv(dTok, d) && (d == 2 || d == 3)) {
                    spaceDim = d;
                }
            }
        } else if (sec == Sec::Vertices) {
            int vid = 0;
            if (!ReaderText::ParseIntSv(firstTok, vid) || vid <= 0) continue;
            std::string_view tx, ty, tz;
            float x = 0, y = 0, z = 0;
            if (!ReaderText::NextTokenSv(probe, tx) || !DetailSurfaceEvolver::EvalFeScalar(tx, params, x) ||
                !ReaderText::NextTokenSv(probe, ty) || !DetailSurfaceEvolver::EvalFeScalar(ty, params, y)) {
                return false;
            }
            if (spaceDim >= 3) {
                if (!ReaderText::NextTokenSv(probe, tz) || !DetailSurfaceEvolver::EvalFeScalar(tz, params, z)) {
                    return false;
                }
            }
            if (mesh.vertices.size() >= 2000000) break;
            const uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
            // Surface Evolver convention is Z-up -> viewer Y-up
            mesh.vertices.push_back({ x, z, -y });
            vertMap[vid] = idx;
        } else if (sec == Sec::Edges) {
            int eid = 0;
            if (!ReaderText::ParseIntSv(firstTok, eid) || eid <= 0) continue;
            std::string_view tTail, tHead;
            int v0 = 0, v1 = 0;
            if (!ReaderText::NextTokenSv(probe, tTail) || !ReaderText::ParseIntSv(tTail, v0) ||
                !ReaderText::NextTokenSv(probe, tHead) || !ReaderText::ParseIntSv(tHead, v1)) {
                return false;
            }
            auto it0 = vertMap.find(v0);
            auto it1 = vertMap.find(v1);
            if (it0 == vertMap.end() || it1 == vertMap.end()) return false;
            edgeMap[eid] = { it0->second, it1->second };
        } else if (sec == Sec::Faces) {
            int fid = 0;
            if (!ReaderText::ParseIntSv(firstTok, fid) || fid <= 0) continue;
            std::vector<int> signedEdges;
            std::string_view et;
            while (ReaderText::NextTokenSv(probe, et)) {
                int se = 0;
                if (et == "-" || et == "+") {
                    const bool neg = (et == "-");
                    std::string_view numTok;
                    if (!ReaderText::NextTokenSv(probe, numTok) || !ReaderText::ParseIntSv(numTok, se) || se <= 0) break;
                    if (neg) se = -se;
                } else {
                    if (!ReaderText::ParseIntSv(et, se) || se == 0) break; // stop at attributes e.g. `color`, `tension`
                }
                signedEdges.push_back(se);
            }
            if (signedEdges.size() < 3) return false;
            rawFaceLoops.push_back(std::move(signedEdges));
        }
    }

    if (!sawVerticesKw || !sawEdgesKw || mesh.vertices.size() < 2 || edgeMap.empty()) return false;

    if (!rawFaceLoops.empty()) {
        for (const auto& loop : rawFaceLoops) {
            std::vector<uint32_t> ring;
            ring.reserve(loop.size());
            for (int se : loop) {
                const int absE = std::abs(se);
                auto it = edgeMap.find(absE);
                if (it == edgeMap.end()) return false;
                const uint32_t startV = (se > 0) ? it->second.tail : it->second.head;
                ring.push_back(startV);
            }
            if (ring.size() == 3) {
                if (ring[0] != ring[1] && ring[1] != ring[2] && ring[0] != ring[2]) {
                    mesh.tris.push_back({ ring[0], ring[1], ring[2], {} });
                }
            } else {
                std::vector<Vec3f> polyPts;
                polyPts.reserve(ring.size());
                for (uint32_t vi : ring) polyPts.push_back(mesh.vertices[vi]);
                MeshBuild3D::EmitPlanarPolygon(mesh, polyPts, {});
            }
        }
    } else {
        // String / wire-frame Evolver model (vertices + edges, no faces)
        Vec3f bmin = mesh.vertices[0], bmax = mesh.vertices[0];
        for (const auto& v : mesh.vertices) {
            bmin.x = std::min(bmin.x, v.x); bmin.y = std::min(bmin.y, v.y); bmin.z = std::min(bmin.z, v.z);
            bmax.x = std::max(bmax.x, v.x); bmax.y = std::max(bmax.y, v.y); bmax.z = std::max(bmax.z, v.z);
        }
        const float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
        if (span <= 1e-6f) return false;
        const std::vector<Vec3f> origVerts = mesh.vertices;
        mesh.vertices.clear();
        for (const auto& kv : edgeMap) {
            MeshBuild3D::EmitEdgeStrut(mesh, origVerts[kv.second.tail], origVerts[kv.second.head], span * 0.015f);
        }
    }

    if (mesh.tris.empty()) return false;
    mesh.formatName = L"Surface Evolver Mesh (.fe)";
    return true;
}

} // namespace Formats
