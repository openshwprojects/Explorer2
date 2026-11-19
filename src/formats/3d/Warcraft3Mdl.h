#pragma once
// Warcraft3Mdl.h — Warcraft III ASCII Model (.mdl) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

// Warcraft III ASCII Model (.mdl — "Version { FormatVersion 800, }" + "Geoset { Vertices ... Faces ... }")
inline bool ParseWarcraftAsciiMdl(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("FormatVersion") == std::string_view::npos || sv.find("Geoset") == std::string_view::npos) {
        return false;
    }

    struct Wc3Lex {
        std::string_view s;
        size_t p = 0;
        void Skip() {
            while (p < s.size()) {
                char c = s[p];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { ++p; continue; }
                if (c == '/' && p + 1 < s.size() && s[p + 1] == '/') {
                    p += 2;
                    while (p < s.size() && s[p] != '\n') ++p;
                    continue;
                }
                break;
            }
        }
        bool Next(std::string_view& tok) {
            Skip();
            if (p >= s.size()) return false;
            char c = s[p];
            if (c == '"') {
                size_t st = ++p;
                while (p < s.size() && s[p] != '"') ++p;
                tok = s.substr(st, p - st);
                if (p < s.size()) ++p;
                return true;
            }
            if (c == '{' || c == '}') {
                tok = s.substr(p++, 1);
                return true;
            }
            size_t st = p;
            while (p < s.size()) {
                char ch = s[p];
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',' || ch == '{' || ch == '}' || ch == '"') break;
                if (ch == '/' && p + 1 < s.size() && s[p + 1] == '/') break;
                ++p;
            }
            tok = s.substr(st, p - st);
            return !tok.empty();
        }
        bool NextInt(int& v) {
            std::string_view t;
            if (!Next(t) || t.size() >= 64) return false;
            char b[64];
            std::memcpy(b, t.data(), t.size());
            b[t.size()] = '\0';
            char* ep = nullptr;
            long lv = std::strtol(b, &ep, 10);
            if (ep != b + t.size()) return false;
            v = static_cast<int>(lv);
            return true;
        }
        bool NextFloat(float& v) {
            std::string_view t;
            if (!Next(t) || t.size() >= 64) return false;
            char b[64];
            std::memcpy(b, t.data(), t.size());
            b[t.size()] = '\0';
            char* ep = nullptr;
            float fv = std::strtof(b, &ep);
            if (ep != b + t.size() || !std::isfinite(fv)) return false;
            v = fv;
            return true;
        }
        bool SkipBlock() {
            int d = 1;
            std::string_view t;
            while (d > 0 && Next(t)) {
                if (t == "{") ++d;
                else if (t == "}") --d;
            }
            return d == 0;
        }
    };

    Wc3Lex lex{ sv, 0 };
    std::string_view tok;
    int fmtVer = 0;
    size_t seqCount = 1;

    while (lex.Next(tok)) {
        if (tok == "Version") {
            if (!lex.Next(tok) || tok != "{") return false;
            while (lex.Next(tok) && tok != "}") {
                if (tok == "FormatVersion") lex.NextInt(fmtVer);
            }
        } else if (tok == "Sequences") {
            int nSeq = 0;
            if (lex.NextInt(nSeq) && nSeq > 0) seqCount = static_cast<size_t>(nSeq);
            if (lex.Next(tok) && tok == "{") lex.SkipBlock();
        } else if (tok == "Geoset") {
            if (!lex.Next(tok) || tok != "{") return false;
            std::vector<Vec3f> verts;
            std::vector<int> indices;
            int gDepth = 1;
            while (gDepth > 0 && lex.Next(tok)) {
                if (tok == "{") { ++gDepth; continue; }
                if (tok == "}") { --gDepth; continue; }
                if (gDepth == 1 && tok == "Vertices") {
                    int nV = 0;
                    if (!lex.NextInt(nV) || nV <= 0 || nV > 1000000) return false;
                    if (!lex.Next(tok) || tok != "{") return false;
                    verts.reserve(static_cast<size_t>(nV));
                    for (int i = 0; i < nV; ++i) {
                        float x = 0.0f, y = 0.0f, z = 0.0f;
                        if (!lex.Next(tok) || tok != "{" ||
                            !lex.NextFloat(x) || !lex.NextFloat(y) || !lex.NextFloat(z) ||
                            !lex.Next(tok) || tok != "}") {
                            return false;
                        }
                        verts.push_back(BlizzDetail::BlizzZUpToViewport(x, y, z));
                    }
                    if (!lex.Next(tok) || tok != "}") return false;
                } else if (gDepth == 1 && tok == "Faces") {
                    int nGroups = 0, nIdx = 0;
                    if (!lex.NextInt(nGroups) || !lex.NextInt(nIdx) || nIdx < 3) return false;
                    if (!lex.Next(tok) || tok != "{") return false;
                    int fDepth = 1;
                    while (fDepth > 0 && lex.Next(tok)) {
                        if (tok == "{") ++fDepth;
                        else if (tok == "}") --fDepth;
                        else if (tok != "Triangles") {
                            char b[64];
                            if (tok.size() < sizeof(b)) {
                                std::memcpy(b, tok.data(), tok.size());
                                b[tok.size()] = '\0';
                                char* ep = nullptr;
                                long iv = std::strtol(b, &ep, 10);
                                if (ep == b + tok.size()) indices.push_back(static_cast<int>(iv));
                            }
                        }
                    }
                } else if (gDepth == 1) {
                    // Skip Normals, TVertices, VertexGroup, Faces-unrelated blocks
                    std::string_view nx;
                    while (lex.Next(nx)) {
                        if (nx == "{") { lex.SkipBlock(); break; }
                        if (nx == "}") { --gDepth; break; }
                    }
                }
            }

            if (!verts.empty() && indices.size() >= 3) {
                uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
                for (const auto& v : verts) mesh.vertices.push_back(v);
                for (size_t t = 0; t + 2 < indices.size(); t += 3) {
                    int a = indices[t], b = indices[t + 1], c = indices[t + 2];
                    if (a < 0 || a >= static_cast<int>(verts.size()) ||
                        b < 0 || b >= static_cast<int>(verts.size()) ||
                        c < 0 || c >= static_cast<int>(verts.size())) {
                        return false;
                    }
                    if (a == b || b == c || c == a) continue;
                    mesh.tris.push_back({
                        baseV + static_cast<uint32_t>(a),
                        baseV + static_cast<uint32_t>(c),
                        baseV + static_cast<uint32_t>(b),
                        {}
                    });
                }
            }
        }
    }

    if (fmtVer < 700 || mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"Warcraft III ASCII Model (.mdl)";
    mesh.animFrameCount = seqCount;
    return true;
}

} // namespace Formats
