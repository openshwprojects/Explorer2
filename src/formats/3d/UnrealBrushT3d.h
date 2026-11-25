#pragma once
// UnrealBrushT3d.h — Unreal Engine UnrealEd CSG Brush & Polygon Map (.t3d)
// Parses `Begin Map` / `Begin Actor` (`Location=(X=...,Y=...,Z=...)`) / `Begin Brush` /
// `Begin PolyList` / `Begin Polygon` (`Vertex +x,+y,+z`) with strict Begin..End block validation.

namespace Formats {

namespace DetailUnrealT3d {

inline bool ParseSignedCoordTriplet(std::string_view s, Vec3f& outPt) {
    s = ReaderText::TrimSv(s);
    const size_t c1 = s.find(',');
    if (c1 == std::string_view::npos) return false;
    const size_t c2 = s.find(',', c1 + 1u);
    if (c2 == std::string_view::npos) return false;

    auto parseSigned = [](std::string_view tok, float& outF) -> bool {
        tok = ReaderText::TrimSv(tok);
        if (!tok.empty() && tok.front() == '+') tok.remove_prefix(1u);
        return ReaderText::ParseFloatSv(tok, outF) && std::isfinite(outF);
    };
    return parseSigned(s.substr(0u, c1), outPt.x) &&
           parseSigned(s.substr(c1 + 1u, c2 - c1 - 1u), outPt.y) &&
           parseSigned(s.substr(c2 + 1u), outPt.z);
}

inline void ParseLocationProp(std::string_view line, Vec3f& ioLoc) {
    // Location=(X=128.0,Y=-64.0,Z=32.0)
    const size_t lp = line.find('(');
    const size_t rp = line.find(')', lp == std::string_view::npos ? 0u : lp);
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp + 1u) return;
    std::string_view body = line.substr(lp + 1u, rp - lp - 1u);
    size_t p = 0u;
    while (p < body.size()) {
        const size_t comma = body.find(',', p);
        std::string_view kv = (comma == std::string_view::npos) ? body.substr(p) : body.substr(p, comma - p);
        const size_t eq = kv.find('=');
        if (eq != std::string_view::npos) {
            std::string_view k = ReaderText::TrimSv(kv.substr(0u, eq));
            std::string_view v = ReaderText::TrimSv(kv.substr(eq + 1u));
            if (!v.empty() && v.front() == '+') v.remove_prefix(1u);
            float f = 0.0f;
            if (ReaderText::ParseFloatSv(v, f) && std::isfinite(f)) {
                if (ReaderText::AsciiEqCI(k, "X")) ioLoc.x = f;
                else if (ReaderText::AsciiEqCI(k, "Y")) ioLoc.y = f;
                else if (ReaderText::AsciiEqCI(k, "Z")) ioLoc.z = f;
            }
        }
        if (comma == std::string_view::npos) break;
        p = comma + 1u;
    }
}

} // namespace DetailUnrealT3d

inline bool ParseUnrealBrushT3d(const uint8_t* data, size_t size, LoadedMesh3D& outMesh) {
    std::string_view sv;
    if (!FormatPrologue::PrepareUtf8TextNoNul(data, size, sv, 28u, (64u << 20))) return false;

    std::vector<std::string> blockStack;
    bool sawPolyList = false;
    bool inPolygon   = false;
    Vec3f actorLoc{ 0.0f, 0.0f, 0.0f };
    std::vector<Vec3f> curPolyVerts;

    outMesh = LoadedMesh3D{};

    size_t linePos = 0u;
    std::string_view rawLine;
    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty()) continue;

        std::string_view rest = line;
        std::string_view kw;
        if (!ReaderText::NextTokenSv(rest, kw)) continue;

        if (ReaderText::AsciiEqCI(kw, "Begin")) {
            std::string_view kind;
            if (!ReaderText::NextTokenSv(rest, kind)) { outMesh = {}; return false; }
            std::string upKind = ReaderText::ToUpperAscii(kind);
            if (blockStack.size() >= 32u) { outMesh = {}; return false; }
            blockStack.push_back(upKind);
            if (upKind == "ACTOR") {
                actorLoc = { 0.0f, 0.0f, 0.0f };
            } else if (upKind == "POLYLIST") {
                sawPolyList = true;
            } else if (upKind == "POLYGON") {
                inPolygon = true;
                curPolyVerts.clear();
            }
            continue;
        }

        if (ReaderText::AsciiEqCI(kw, "End")) {
            std::string_view kind;
            if (!ReaderText::NextTokenSv(rest, kind) || blockStack.empty()) {
                outMesh = {};
                return false;
            }
            std::string upKind = ReaderText::ToUpperAscii(kind);
            if (blockStack.back() != upKind) {
                outMesh = {};
                return false; // Mismatched Begin..End!
            }
            blockStack.pop_back();
            if (upKind == "ACTOR") {
                actorLoc = { 0.0f, 0.0f, 0.0f };
            } else if (upKind == "POLYGON") {
                if (curPolyVerts.size() < 3u) {
                    outMesh = {};
                    return false;
                }
                MeshBuild3D::EmitPlanarPolygon(outMesh, curPolyVerts, {});
                curPolyVerts.clear();
                inPolygon = false;
            }
            continue;
        }

        if (inPolygon && ReaderText::AsciiEqCI(kw, "Vertex")) {
            Vec3f p{};
            if (!DetailUnrealT3d::ParseSignedCoordTriplet(rest, p)) {
                outMesh = {};
                return false;
            }
            if (curPolyVerts.size() < 256u) {
                curPolyVerts.push_back({ p.x + actorLoc.x, p.y + actorLoc.y, p.z + actorLoc.z });
            }
        } else if (!inPolygon && ReaderText::StartsWithCI(line, "Location=")) {
            DetailUnrealT3d::ParseLocationProp(line, actorLoc);
        }
    }

    if (!blockStack.empty() || !sawPolyList || outMesh.vertices.empty() || outMesh.tris.empty()) {
        outMesh = {};
        return false;
    }

    outMesh.formatName = L"UnrealEd CSG Brush Map (.t3d)";
    return !outMesh.vertices.empty() && !outMesh.tris.empty();
}

} // namespace Formats
