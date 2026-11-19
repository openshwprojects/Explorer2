#pragma once
// QuakeMap.h — Quake / Valve220 CSG Brush Map (.map) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool IsLikelyQuakeBrushMapMemory(const uint8_t* data, size_t size) {
    using namespace DetailQuakeVmf;
    if (!data || size < 32u) return false;
    size_t scanLen = std::min<size_t>(size, 262144u);
    std::string_view sv(reinterpret_cast<const char*>(data), scanLen);

    // Reject binary files and known non-Quake .map formats (MSVC/GNU linker maps, JS source maps)
    for (size_t i = 0; i < std::min<size_t>(scanLen, 4096u); ++i) {
        if (data[i] == 0u) return false;
    }
    std::string_view trimmed = TrimSv(sv);
    if (trimmed.empty() || trimmed.front() == '[') return false;
    if (trimmed.front() == '{' &&
        sv.find("\"mappings\"") != std::string_view::npos &&
        sv.find("\"sources\"") != std::string_view::npos)
    {
        return false; // JavaScript / CSS source map JSON
    }
    if (sv.find("Preferred load address is") != std::string_view::npos ||
        sv.find("Publics by Value") != std::string_view::npos ||
        sv.find("Start         Length     Name                   Class") != std::string_view::npos)
    {
        return false; // MSVC linker .map file
    }

    int braceDepth = 0;
    int planesInCurBrush = 0;
    size_t pos = 0;
    while (pos < sv.size()) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = TrimSv(sv.substr(pos, eol - pos));
        pos = eol + 1u;

        size_t cm = ln.find("//");
        if (cm != std::string_view::npos) ln = TrimSv(ln.substr(0, cm));
        if (ln.empty()) continue;

        if (ln.front() == '(' && braceDepth >= 1) {
            CsgVec3d p0, p1, p2;
            if (ParseThreeParenPoints(ln, p0, p1, p2) != std::string_view::npos) {
                if (++planesInCurBrush >= 4) return true;
            }
        }
        for (char c : ln) {
            if (c == '{') { ++braceDepth; planesInCurBrush = 0; }
            else if (c == '}') { braceDepth = std::max(0, braceDepth - 1); planesInCurBrush = 0; }
        }
    }
    return false;
}

inline bool ParseQuakeMapMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace DetailQuakeVmf;
    if (!IsLikelyQuakeBrushMapMemory(data, size)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    std::vector< std::vector<BrushSideDef> > solids;
    std::vector<BrushSideDef> curBrush;
    int braceDepth = 0;

    size_t pos = 0;
    while (pos < sv.size() && solids.size() < 100000u) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1u;

        size_t cm = ln.find("//");
        if (cm != std::string_view::npos) ln = ln.substr(0, cm);
        ln = TrimSv(ln);
        if (ln.empty()) continue;

        if (ln.front() == '(' && braceDepth >= 1) {
            BrushSideDef sd;
            size_t afterPts = ParseThreeParenPoints(ln, sd.p0, sd.p1, sd.p2);
            if (afterPts != std::string_view::npos) {
                std::string_view rest = TrimSv(ln.substr(afterPts));
                std::string_view texTok;
                if (!rest.empty() && rest.front() == '"') {
                    rest.remove_prefix(1);
                    size_t q = rest.find('"');
                    texTok = (q != std::string_view::npos) ? rest.substr(0, q) : rest;
                } else {
                    NextTokenSv(rest, texTok);
                }
                sd.isTool = IsToolMaterialName(texTok);
                curBrush.push_back(std::move(sd));
                continue;
            }
        }

        for (char c : ln) {
            if (c == '{') {
                ++braceDepth;
                curBrush.clear();
            } else if (c == '}') {
                if (!curBrush.empty()) {
                    if (curBrush.size() >= 4u) solids.push_back(std::move(curBrush));
                    curBrush.clear();
                }
                braceDepth = std::max(0, braceDepth - 1);
            }
        }
    }

    if (!EmitBrushSolidsToMesh(solids, mesh)) return false;
    mesh.formatName = L"Quake / Valve220 CSG Brush Map (.map)";
    return true;
}

} // namespace Formats
