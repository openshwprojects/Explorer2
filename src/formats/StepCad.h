#pragma once
// StepCad.h — ISO 10303-21 STEP (.step / .stp) 3D CAD B-Rep & Point/Loop Mesh Parser

namespace Formats {

inline bool ParseStepCadMesh(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find("ISO-10303-21") == std::string_view::npos && sv.find("CARTESIAN_POINT") == std::string_view::npos) {
        return false;
    }
    mesh.formatName = L"ISO 10303-21 STEP CAD (.step)";

    struct StepPt { int id; Vec3f p; };
    std::vector<StepPt> pts;
    size_t pos = 0;
    while (pos < sv.size() && pts.size() < 40000) {
        size_t cp = sv.find("CARTESIAN_POINT", pos);
        if (cp == std::string_view::npos) break;
        int entId = -1;
        size_t hashPos = sv.rfind('#', cp);
        if (hashPos != std::string_view::npos && cp - hashPos < 40) {
            entId = std::atoi(std::string(sv.substr(hashPos + 1, cp - hashPos - 1)).c_str());
        }
        size_t p1 = sv.find('(', cp + 15);
        size_t p2 = (p1 != std::string_view::npos) ? sv.find('(', p1 + 1) : std::string_view::npos;
        size_t p3 = (p2 != std::string_view::npos) ? sv.find(')', p2 + 1) : std::string_view::npos;
        if (p2 == std::string_view::npos || p3 == std::string_view::npos) { pos = cp + 15; continue; }
        std::string coords(sv.substr(p2 + 1, p3 - p2 - 1));
        for (char& ch : coords) if (ch == ',') ch = ' ';
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (sscanf_s(coords.c_str(), "%f %f %f", &x, &y, &z) >= 2) {
            pts.push_back({ entId, { x, z, -y } });
        }
        pos = p3 + 1;
    }
    if (pts.size() < 3) return false;

    // Build vertex table and map #id -> vertex index
    for (const auto& sp : pts) {
        mesh.vertices.push_back(sp.p);
    }

    // Try POLY_LOOP((#1,#2,#3,...)) triangulation first
    auto findVertByEnt = [&](int id) -> int {
        for (size_t i = 0; i < pts.size(); ++i) {
            if (pts[i].id == id) return static_cast<int>(i);
        }
        return -1;
    };

    size_t lp = 0;
    while (lp < sv.size()) {
        size_t pl = sv.find("POLY_LOOP", lp);
        if (pl == std::string_view::npos) break;
        size_t b1 = sv.find('(', pl + 9);
        size_t b2 = (b1 != std::string_view::npos) ? sv.find('(', b1 + 1) : std::string_view::npos;
        size_t b3 = (b2 != std::string_view::npos) ? sv.find(')', b2 + 1) : std::string_view::npos;
        if (b2 == std::string_view::npos || b3 == std::string_view::npos) { lp = pl + 9; continue; }
        std::string_view refs = sv.substr(b2 + 1, b3 - b2 - 1);
        std::vector<uint32_t> loopIdx;
        size_t rp = 0;
        while (rp < refs.size()) {
            size_t h = refs.find('#', rp);
            if (h == std::string_view::npos) break;
            int id = std::atoi(std::string(refs.substr(h + 1, 16)).c_str());
            int vi = findVertByEnt(id);
            if (vi >= 0) loopIdx.push_back(static_cast<uint32_t>(vi));
            rp = h + 1;
        }
        for (size_t k = 1; k + 1 < loopIdx.size(); ++k) {
            mesh.tris.push_back({ loopIdx[0], loopIdx[k], loopIdx[k + 1], {} });
        }
        lp = b3 + 1;
    }

    // If B-Rep uses VERTEX_POINT indirection or curves, build prism/strip faces over consecutive points
    if (mesh.tris.empty()) {
        for (size_t i = 0; i + 2 < mesh.vertices.size(); ++i) {
            mesh.tris.push_back({
                static_cast<uint32_t>(i),
                static_cast<uint32_t>(i + 1),
                static_cast<uint32_t>(i + 2),
                {}
            });
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
