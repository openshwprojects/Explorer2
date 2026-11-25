#pragma once
// OpenGexMesh.h — Open Game Engine Exchange 3D Scene & Mesh (.ogex — OpenDDL)
// Parses `GeometryObject`, `Mesh (primitive = "triangles"|"quads"|"triangle_strip")`,
// `VertexArray (attrib = "position")`, `IndexArray`, and `GeometryNode` (`ObjectRef` + `Transform`/`Translation`).

namespace Formats {

namespace DetailOpenGex {

struct OgexTok {
    enum Kind : uint8_t { Ident, String, Number, Ref, Sym } kind = Ident;
    std::string text;
};

inline bool TokenizeOpenDdl(std::string_view sv, std::vector<OgexTok>& out) {
    out.clear();
    size_t i = 0u;
    const size_t n = sv.size();
    int braceDepth = 0;
    int parenDepth = 0;
    int brackDepth = 0;

    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(sv[i]);
        if (c == 0u) return false;
        if (c <= 0x20u) { ++i; continue; }

        if (c == '/' && i + 1u < n && sv[i + 1u] == '/') {
            i += 2u;
            while (i < n && sv[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1u < n && sv[i + 1u] == '*') {
            i += 2u;
            bool closed = false;
            while (i + 1u < n) {
                if (sv[i] == '*' && sv[i + 1u] == '/') { i += 2u; closed = true; break; }
                ++i;
            }
            if (!closed) return false;
            continue;
        }
        if (c == '"') {
            ++i;
            std::string s;
            bool closed = false;
            while (i < n) {
                if (sv[i] == '\\' && i + 1u < n) { s.push_back(sv[i + 1u]); i += 2u; }
                else if (sv[i] == '"') { ++i; closed = true; break; }
                else s.push_back(sv[i++]);
            }
            if (!closed) return false;
            out.push_back({ OgexTok::String, std::move(s) });
            continue;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']' || c == ',' || c == '=') {
            if (c == '{') ++braceDepth;
            else if (c == '}') { if (--braceDepth < 0) return false; }
            else if (c == '(') ++parenDepth;
            else if (c == ')') { if (--parenDepth < 0) return false; }
            else if (c == '[') ++brackDepth;
            else if (c == ']') { if (--brackDepth < 0) return false; }
            out.push_back({ OgexTok::Sym, std::string(1u, static_cast<char>(c)) });
            ++i;
            continue;
        }
        if (c == '$' || c == '%') {
            const size_t s = i++;
            while (i < n && (std::isalnum(static_cast<unsigned char>(sv[i])) || sv[i] == '_')) ++i;
            if (i == s + 1u) return false;
            out.push_back({ OgexTok::Ref, std::string(sv.substr(s, i - s)) });
            continue;
        }
        if (std::isdigit(c) || c == '+' || c == '-' || c == '.') {
            const size_t s = i++;
            while (i < n) {
                unsigned char ch = static_cast<unsigned char>(sv[i]);
                if (std::isalnum(ch) || ch == '+' || ch == '-' || ch == '.') ++i;
                else break;
            }
            out.push_back({ OgexTok::Number, std::string(sv.substr(s, i - s)) });
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            const size_t s = i++;
            while (i < n && (std::isalnum(static_cast<unsigned char>(sv[i])) || sv[i] == '_')) ++i;
            out.push_back({ OgexTok::Ident, std::string(sv.substr(s, i - s)) });
            if (out.size() > 3000000u) return false;
            continue;
        }
        return false;
    }
    return braceDepth == 0 && parenDepth == 0 && brackDepth == 0 && !out.empty();
}

struct OgexSubMesh {
    std::string primitive = "triangles";
    std::vector<Vec3f> positions;
    std::vector<uint32_t> indices;
    size_t indexTupleSize = 3u;
};

struct OgexGeomObj {
    std::vector<OgexSubMesh> meshes;
};

struct OgexNode {
    std::string targetGeoRef;
    Vec3f translation{ 0.0f, 0.0f, 0.0f };
    float mat[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 }; // column-major in OpenGEX
    bool hasMatrix = false;
};

inline size_t SkipBalancedBlock(const std::vector<OgexTok>& t, size_t openIdx, char openCh, char closeCh) {
    int depth = 0;
    for (size_t i = openIdx; i < t.size(); ++i) {
        if (t[i].kind == OgexTok::Sym && t[i].text.size() == 1u) {
            if (t[i].text[0] == openCh) ++depth;
            else if (t[i].text[0] == closeCh && --depth == 0) return i + 1u;
        }
    }
    return t.size();
}

inline std::string ParsePropValue(const std::vector<OgexTok>& t, size_t& idx, std::string_view key) {
    if (idx >= t.size() || t[idx].kind != OgexTok::Sym || t[idx].text != "(") return {};
    const size_t afterParen = SkipBalancedBlock(t, idx, '(', ')');
    std::string found;
    for (size_t k = idx + 1u; k + 2u < afterParen; ++k) {
        if (t[k].kind == OgexTok::Ident && t[k].text == key &&
            t[k + 1u].kind == OgexTok::Sym && t[k + 1u].text == "=") {
            found = t[k + 2u].text;
            break;
        }
    }
    idx = afterParen;
    return found;
}

} // namespace DetailOpenGex

inline bool ParseOpenGexMesh(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 24u, (64u << 20))) return false;

    std::vector<DetailOpenGex::OgexTok> toks;
    if (!DetailOpenGex::TokenizeOpenDdl(sv, toks)) return false;

    std::map<std::string, DetailOpenGex::OgexGeomObj> geomMap;
    std::vector<std::string> geomOrder;
    std::vector<DetailOpenGex::OgexNode> nodes;

    auto extractFloats = [&](size_t bStart, size_t bEnd, std::vector<float>& outF) -> bool {
        outF.clear();
        for (size_t k = bStart; k < bEnd; ++k) {
            if (toks[k].kind == DetailOpenGex::OgexTok::Number) {
                float f = 0.0f;
                if (!ReaderText::ParseFloatSv(toks[k].text, f) || !std::isfinite(f)) return false;
                outF.push_back(f);
            }
        }
        return true;
    };

    auto extractUints = [&](size_t bStart, size_t bEnd, std::vector<uint32_t>& outU) -> bool {
        outU.clear();
        for (size_t k = bStart; k < bEnd; ++k) {
            if (toks[k].kind == DetailOpenGex::OgexTok::Number) {
                uint64_t u = 0;
                char* endp = nullptr;
                u = std::strtoull(toks[k].text.c_str(), &endp, 0);
                if (endp == toks[k].text.c_str() || *endp != '\0' || u > 0x7FFFFFFFULL) return false;
                outU.push_back(static_cast<uint32_t>(u));
            }
        }
        return true;
    };

    size_t i = 0u;
    while (i < toks.size()) {
        if (toks[i].kind != DetailOpenGex::OgexTok::Ident) { ++i; continue; }
        const std::string& topId = toks[i].text;

        if (topId == "GeometryObject") {
            ++i;
            std::string geoName;
            if (i < toks.size() && toks[i].kind == DetailOpenGex::OgexTok::Ref) geoName = toks[i++].text;
            if (i < toks.size() && toks[i].kind == DetailOpenGex::OgexTok::Sym && toks[i].text == "(") {
                i = DetailOpenGex::SkipBalancedBlock(toks, i, '(', ')');
            }
            if (i >= toks.size() || toks[i].text != "{") return false;
            const size_t geoEnd = DetailOpenGex::SkipBalancedBlock(toks, i, '{', '}');
            if (geoName.empty()) geoName = "$__anon_" + std::to_string(geomOrder.size());

            DetailOpenGex::OgexGeomObj gobj{};
            size_t p = i + 1u;
            while (p + 1u < geoEnd) {
                if (toks[p].kind == DetailOpenGex::OgexTok::Ident && toks[p].text == "Mesh") {
                    ++p;
                    DetailOpenGex::OgexSubMesh sm{};
                    std::string prim = DetailOpenGex::ParsePropValue(toks, p, "primitive");
                    if (!prim.empty()) sm.primitive = prim;
                    if (p >= geoEnd || toks[p].text != "{") return false;
                    const size_t meshEnd = DetailOpenGex::SkipBalancedBlock(toks, p, '{', '}');

                    size_t mp = p + 1u;
                    while (mp + 1u < meshEnd) {
                        if (toks[mp].kind == DetailOpenGex::OgexTok::Ident && toks[mp].text == "VertexArray") {
                            ++mp;
                            std::string attrib = DetailOpenGex::ParsePropValue(toks, mp, "attrib");
                            if (mp >= meshEnd || toks[mp].text != "{") return false;
                            const size_t vaEnd = DetailOpenGex::SkipBalancedBlock(toks, mp, '{', '}');
                            if (attrib == "position") {
                                // Inside VertexArray: float[3] { {x,y,z}, ... }
                                size_t fp = mp + 1u;
                                int tupleDim = 3;
                                while (fp < vaEnd && toks[fp].text != "{") {
                                    if (toks[fp].text == "[" && fp + 1u < vaEnd && toks[fp + 1u].kind == DetailOpenGex::OgexTok::Number) {
                                        ReaderText::ParseIntSv(toks[fp + 1u].text, tupleDim);
                                    }
                                    ++fp;
                                }
                                if (tupleDim < 2 || tupleDim > 4 || fp >= vaEnd) return false;
                                std::vector<float> flts;
                                if (!extractFloats(fp, vaEnd - 1u, flts)) return false;
                                for (size_t k = 0u; k + static_cast<size_t>(tupleDim) <= flts.size(); k += static_cast<size_t>(tupleDim)) {
                                    sm.positions.push_back({
                                        flts[k + 0u],
                                        flts[k + 1u],
                                        tupleDim >= 3 ? flts[k + 2u] : 0.0f
                                    });
                                }
                            }
                            mp = vaEnd;
                        } else if (toks[mp].kind == DetailOpenGex::OgexTok::Ident && toks[mp].text == "IndexArray") {
                            ++mp;
                            if (mp < meshEnd && toks[mp].text == "(") {
                                mp = DetailOpenGex::SkipBalancedBlock(toks, mp, '(', ')');
                            }
                            if (mp >= meshEnd || toks[mp].text != "{") return false;
                            const size_t iaEnd = DetailOpenGex::SkipBalancedBlock(toks, mp, '{', '}');
                            size_t ip = mp + 1u;
                            int tupleSz = (sm.primitive == "quads") ? 4 : ((sm.primitive == "triangle_strip") ? 1 : 3);
                            while (ip < iaEnd && toks[ip].text != "{") {
                                if (toks[ip].text == "[" && ip + 1u < iaEnd && toks[ip + 1u].kind == DetailOpenGex::OgexTok::Number) {
                                    ReaderText::ParseIntSv(toks[ip + 1u].text, tupleSz);
                                }
                                ++ip;
                            }
                            if (tupleSz < 1 || tupleSz > 16 || ip >= iaEnd) return false;
                            sm.indexTupleSize = static_cast<size_t>(tupleSz);
                            if (!extractUints(ip, iaEnd - 1u, sm.indices)) return false;
                            mp = iaEnd;
                        } else {
                            ++mp;
                        }
                    }
                    if (!sm.positions.empty()) gobj.meshes.push_back(std::move(sm));
                    p = meshEnd;
                } else {
                    ++p;
                }
            }
            geomMap[geoName] = std::move(gobj);
            geomOrder.push_back(geoName);
            i = geoEnd;
        } else if (topId == "GeometryNode") {
            ++i;
            if (i < toks.size() && toks[i].kind == DetailOpenGex::OgexTok::Ref) ++i;
            if (i < toks.size() && toks[i].text == "(") i = DetailOpenGex::SkipBalancedBlock(toks, i, '(', ')');
            if (i >= toks.size() || toks[i].text != "{") return false;
            const size_t nodeEnd = DetailOpenGex::SkipBalancedBlock(toks, i, '{', '}');

            DetailOpenGex::OgexNode nd{};
            size_t np = i + 1u;
            while (np + 1u < nodeEnd) {
                if (toks[np].kind == DetailOpenGex::OgexTok::Ident && toks[np].text == "ObjectRef") {
                    const size_t orEnd = DetailOpenGex::SkipBalancedBlock(toks, np + 1u, '{', '}');
                    for (size_t k = np + 1u; k < orEnd; ++k) {
                        if (toks[k].kind == DetailOpenGex::OgexTok::Ref) { nd.targetGeoRef = toks[k].text; break; }
                    }
                    np = orEnd;
                } else if (toks[np].kind == DetailOpenGex::OgexTok::Ident && toks[np].text == "Transform") {
                    ++np;
                    if (np < nodeEnd && toks[np].text == "(") np = DetailOpenGex::SkipBalancedBlock(toks, np, '(', ')');
                    if (np < nodeEnd && toks[np].text == "{") {
                        const size_t trEnd = DetailOpenGex::SkipBalancedBlock(toks, np, '{', '}');
                        size_t fp = np + 1u;
                        while (fp < trEnd && toks[fp].text != "{") ++fp;
                        std::vector<float> m16;
                        if (fp < trEnd && extractFloats(fp, trEnd - 1u, m16) && m16.size() == 16u) {
                            for (size_t q = 0u; q < 16u; ++q) nd.mat[q] = m16[q];
                            nd.hasMatrix = true;
                        }
                        np = trEnd;
                    }
                } else if (toks[np].kind == DetailOpenGex::OgexTok::Ident && toks[np].text == "Translation") {
                    ++np;
                    if (np < nodeEnd && toks[np].text == "(") np = DetailOpenGex::SkipBalancedBlock(toks, np, '(', ')');
                    if (np < nodeEnd && toks[np].text == "{") {
                        const size_t tlEnd = DetailOpenGex::SkipBalancedBlock(toks, np, '{', '}');
                        size_t fp = np + 1u;
                        while (fp < tlEnd && toks[fp].text != "{") ++fp;
                        std::vector<float> t3;
                        if (fp < tlEnd && extractFloats(fp, tlEnd - 1u, t3) && t3.size() >= 3u) {
                            nd.translation = { t3[0], t3[1], t3[2] };
                        }
                        np = tlEnd;
                    }
                } else {
                    ++np;
                }
            }
            if (!nd.targetGeoRef.empty()) nodes.push_back(std::move(nd));
            i = nodeEnd;
        } else {
            ++i;
        }
    }

    if (geomMap.empty()) return false;

    outMesh = LoadedMesh3D{};
    auto instantiateGeo = [&](const DetailOpenGex::OgexGeomObj& gobj, const DetailOpenGex::OgexNode* nd) -> bool {
        for (const auto& sm : gobj.meshes) {
            const uint32_t baseV = static_cast<uint32_t>(outMesh.vertices.size());
            for (const auto& p : sm.positions) {
                Vec3f wp = p;
                if (nd && nd->hasMatrix) {
                    // OpenGEX 4x4 matrices are column-major: m[col*4 + row]
                    const float* m = nd->mat;
                    wp = {
                        m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
                        m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
                        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
                    };
                }
                if (nd) {
                    wp.x += nd->translation.x;
                    wp.y += nd->translation.y;
                    wp.z += nd->translation.z;
                }
                outMesh.vertices.push_back(wp);
            }

            const size_t nPos = sm.positions.size();
            for (uint32_t idx : sm.indices) {
                if (idx >= nPos) return false;
            }
            auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
                if (a != b && b != c && a != c) {
                    outMesh.tris.push_back({ baseV + a, baseV + b, baseV + c, {} });
                }
            };

            if (sm.primitive == "quads") {
                for (size_t k = 0u; k + 3u < sm.indices.size(); k += 4u) {
                    pushTri(sm.indices[k], sm.indices[k + 1u], sm.indices[k + 2u]);
                    pushTri(sm.indices[k], sm.indices[k + 2u], sm.indices[k + 3u]);
                }
            } else if (sm.primitive == "triangle_strip") {
                for (size_t k = 2u; k < sm.indices.size(); ++k) {
                    if ((k & 1u) == 0u) pushTri(sm.indices[k - 2u], sm.indices[k - 1u], sm.indices[k]);
                    else                pushTri(sm.indices[k - 1u], sm.indices[k - 2u], sm.indices[k]);
                }
            } else {
                for (size_t k = 0u; k + 2u < sm.indices.size(); k += 3u) {
                    pushTri(sm.indices[k], sm.indices[k + 1u], sm.indices[k + 2u]);
                }
            }
        }
        return true;
    };

    if (!nodes.empty()) {
        for (const auto& nd : nodes) {
            auto it = geomMap.find(nd.targetGeoRef);
            if (it == geomMap.end() || !instantiateGeo(it->second, &nd)) {
                outMesh = {};
                return false;
            }
        }
    } else {
        for (const auto& nm : geomOrder) {
            if (!instantiateGeo(geomMap[nm], nullptr)) {
                outMesh = {};
                return false;
            }
        }
    }

    if (outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"Open Game Engine Exchange (.ogex)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
