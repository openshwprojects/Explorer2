#pragma once
// ValveVmf.h — Valve Hammer Source Map (.vmf) KeyValues & Displacement 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailQuakeVmf {

// Hierarchical Valve KeyValues Tokenizer for .vmf
struct VmfTokStream {
    std::string_view sv;
    size_t pos = 0;

    void SkipWsAndComments() {
        while (pos < sv.size()) {
            char c = sv[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
            } else if (c == '/' && pos + 1u < sv.size() && sv[pos + 1u] == '/') {
                pos += 2u;
                while (pos < sv.size() && sv[pos] != '\n') ++pos;
            } else {
                break;
            }
        }
    }

    // Returns: 0 = EOF, 1 = '{', 2 = '}', 3 = string/ident token in outVal
    int Next(std::string_view& outVal) {
        SkipWsAndComments();
        if (pos >= sv.size()) return 0;
        char c = sv[pos];
        if (c == '{') { ++pos; outVal = "{"; return 1; }
        if (c == '}') { ++pos; outVal = "}"; return 2; }
        if (c == '"') {
            ++pos;
            size_t st = pos;
            while (pos < sv.size() && sv[pos] != '"') {
                if (sv[pos] == '\\' && pos + 1u < sv.size()) pos += 2u;
                else ++pos;
            }
            outVal = sv.substr(st, pos - st);
            if (pos < sv.size() && sv[pos] == '"') ++pos;
            return 3;
        }
        size_t st = pos;
        while (pos < sv.size()) {
            char ch = sv[pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '{' || ch == '}' || ch == '"') break;
            if (ch == '/' && pos + 1u < sv.size() && sv[pos + 1u] == '/') break;
            ++pos;
        }
        outVal = sv.substr(st, pos - st);
        return 3;
    }

    int PeekType() {
        size_t saved = pos;
        std::string_view dummy;
        int t = Next(dummy);
        pos = saved;
        return t;
    }
};

inline void ParseFloatsFromRowString(std::string_view s, std::vector<double>& outNums) {
    outNums.clear();
    for (size_t i = 0; i < s.size(); ) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '[' || s[i] == ']' || s[i] == '(' || s[i] == ')')) ++i;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '[' && s[j] != ']' && s[j] != '(' && s[j] != ')') ++j;
        double v = 0.0;
        if (ParseDoubleSv(s.substr(i, j - i), v)) outNums.push_back(v);
        i = j;
    }
}

inline void SkipVmfBlock(VmfTokStream& ts, int depth = 0) {
    if (depth > 64) return;
    std::string_view tok;
    int brace = 1;
    while (brace > 0) {
        int t = ts.Next(tok);
        if (t == 0) break;
        if (t == 1) ++brace;
        else if (t == 2) --brace;
    }
}

inline void ParseVmfRowMapBlock(VmfTokStream& ts, std::unordered_map<int, std::vector<double>>& rows) {
    std::string_view k, v;
    while (true) {
        int tk = ts.Next(k);
        if (tk == 0 || tk == 2) break;
        if (tk == 1) { SkipVmfBlock(ts); continue; }
        int nextT = ts.PeekType();
        if (nextT == 1) {
            ts.Next(v);
            SkipVmfBlock(ts);
        } else if (nextT == 3) {
            ts.Next(v);
            std::string kl = ToLowerStr(k);
            if (kl.size() > 3 && kl.substr(0, 3) == "row") {
                int rIdx = -1;
                if (ParseIntSv(std::string_view(kl).substr(3), rIdx) && rIdx >= 0 && rIdx <= 64) {
                    ParseFloatsFromRowString(v, rows[rIdx]);
                }
            }
        }
    }
}

inline void ParseVmfDispInfoBlock(VmfTokStream& ts, VmfDispInfo& disp) {
    std::unordered_map<int, std::vector<double>> normRows, distRows, offRows;
    std::string_view k, v;
    while (true) {
        int tk = ts.Next(k);
        if (tk == 0 || tk == 2) break;
        if (tk == 1) { SkipVmfBlock(ts); continue; }
        std::string kl = ToLowerStr(k);
        int nextT = ts.PeekType();
        if (nextT == 1) {
            ts.Next(v); // consume '{'
            if (kl == "normals")        ParseVmfRowMapBlock(ts, normRows);
            else if (kl == "distances") ParseVmfRowMapBlock(ts, distRows);
            else if (kl == "offsets")   ParseVmfRowMapBlock(ts, offRows);
            else                        SkipVmfBlock(ts);
        } else if (nextT == 3) {
            ts.Next(v);
            if (kl == "power") {
                ParseIntSv(v, disp.power);
            } else if (kl == "elevation") {
                ParseDoubleSv(v, disp.elevation);
            } else if (kl == "startposition") {
                std::vector<double> sp;
                ParseFloatsFromRowString(v, sp);
                if (sp.size() >= 3u) disp.startPos = { sp[0], sp[1], sp[2] };
            }
        }
    }

    if (disp.power >= 2 && disp.power <= 4) {
        disp.present = true;
        size_t N = (1u << disp.power) + 1u;
        disp.normals.assign(N * N, CsgVec3d{ 0.0, 0.0, 1.0 });
        disp.distances.assign(N * N, 0.0);
        disp.offsets.assign(N * N, CsgVec3d{ 0.0, 0.0, 0.0 });
        for (size_t r = 0; r < N; ++r) {
            auto itN = normRows.find(static_cast<int>(r));
            if (itN != normRows.end()) {
                for (size_t c = 0; c < N && c * 3u + 2u < itN->second.size(); ++c) {
                    disp.normals[r * N + c] = {
                        itN->second[c * 3u + 0u],
                        itN->second[c * 3u + 1u],
                        itN->second[c * 3u + 2u]
                    };
                }
            }
            auto itD = distRows.find(static_cast<int>(r));
            if (itD != distRows.end()) {
                for (size_t c = 0; c < N && c < itD->second.size(); ++c) {
                    disp.distances[r * N + c] = itD->second[c];
                }
            }
            auto itO = offRows.find(static_cast<int>(r));
            if (itO != offRows.end()) {
                for (size_t c = 0; c < N && c * 3u + 2u < itO->second.size(); ++c) {
                    disp.offsets[r * N + c] = {
                        itO->second[c * 3u + 0u],
                        itO->second[c * 3u + 1u],
                        itO->second[c * 3u + 2u]
                    };
                }
            }
        }
    }
}

inline bool ParseVmfSideBlock(VmfTokStream& ts, BrushSideDef& outSide) {
    bool hasPlane = false;
    std::string_view k, v;
    while (true) {
        int tk = ts.Next(k);
        if (tk == 0 || tk == 2) break;
        if (tk == 1) { SkipVmfBlock(ts); continue; }
        std::string kl = ToLowerStr(k);
        int nextT = ts.PeekType();
        if (nextT == 1) {
            ts.Next(v); // '{'
            if (kl == "dispinfo") ParseVmfDispInfoBlock(ts, outSide.disp);
            else                  SkipVmfBlock(ts);
        } else if (nextT == 3) {
            ts.Next(v);
            if (kl == "plane") {
                if (ParseThreeParenPoints(v, outSide.p0, outSide.p1, outSide.p2) != std::string_view::npos) {
                    hasPlane = true;
                }
            } else if (kl == "material") {
                outSide.isTool = IsToolMaterialName(v);
            }
        }
    }
    return hasPlane;
}

inline void ParseVmfSolidBlock(VmfTokStream& ts, std::vector< std::vector<BrushSideDef> >& outSolids) {
    std::vector<BrushSideDef> sides;
    std::string_view k, v;
    while (true) {
        int tk = ts.Next(k);
        if (tk == 0 || tk == 2) break;
        if (tk == 1) { SkipVmfBlock(ts); continue; }
        std::string kl = ToLowerStr(k);
        int nextT = ts.PeekType();
        if (nextT == 1) {
            ts.Next(v); // '{'
            if (kl == "side") {
                BrushSideDef sd;
                if (ParseVmfSideBlock(ts, sd)) sides.push_back(std::move(sd));
            } else {
                SkipVmfBlock(ts);
            }
        } else if (nextT == 3) {
            ts.Next(v);
        }
    }
    if (sides.size() >= 4u) outSolids.push_back(std::move(sides));
}

inline void ParseVmfContainerBlock(VmfTokStream& ts, std::vector< std::vector<BrushSideDef> >& outSolids, int depth = 0) {
    if (depth > 16 || outSolids.size() >= 100000u) return;
    std::string_view k, v;
    while (outSolids.size() < 100000u) {
        int tk = ts.Next(k);
        if (tk == 0 || tk == 2) break;
        if (tk == 1) { SkipVmfBlock(ts); continue; }
        std::string kl = ToLowerStr(k);
        int nextT = ts.PeekType();
        if (nextT == 1) {
            ts.Next(v); // '{'
            if (kl == "solid") {
                ParseVmfSolidBlock(ts, outSolids);
            } else if (kl == "world" || kl == "entity" || kl == "group" || kl == "hidden") {
                ParseVmfContainerBlock(ts, outSolids, depth + 1);
            } else {
                SkipVmfBlock(ts);
            }
        } else if (nextT == 3) {
            ts.Next(v);
        }
    }
}

} // namespace DetailQuakeVmf


// ============================================================================
// Public Entry Point 2b: Valve Hammer Map (.vmf)
// ============================================================================
inline bool ParseValveVmfMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailQuakeVmf;
    if (!data || size < 32u) return false;
    for (size_t i = 0; i < std::min<size_t>(size, 4096u); ++i) {
        if (data[i] == 0u) return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    mesh.vertices.clear();
    mesh.tris.clear();

    VmfTokStream ts{ sv, 0u };
    std::vector< std::vector<BrushSideDef> > solids;
    ParseVmfContainerBlock(ts, solids, 0);
    if (!EmitBrushSolidsToMesh(solids, mesh)) return false;

    mesh.formatName = L"Valve Hammer Source Map (.vmf)";
    return true;
}

} // namespace Formats
