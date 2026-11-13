#pragma once
// GmshMsh.h — Gmsh Finite Element Mesh (.msh v2/v4) 3D Parser

namespace Formats {

inline bool ParseGmshMesh(std::string_view sv, LoadedMesh3D& mesh) {
    size_t nodesPos = sv.find("$Nodes");
    size_t elemsPos = sv.find("$Elements");
    if (nodesPos == std::string_view::npos || elemsPos == std::string_view::npos) return false;
    mesh.formatName = L"Gmsh Finite Element Mesh (.msh)";

    size_t p = sv.find('\n', nodesPos);
    if (p == std::string_view::npos) return false;
    ++p;
    size_t endNodes = sv.find("$EndNodes", p);
    if (endNodes == std::string_view::npos) return false;

    // Skip count line
    size_t nl = sv.find('\n', p);
    if (nl != std::string_view::npos && nl < endNodes) p = nl + 1;

    while (p < endNodes) {
        size_t eol = sv.find('\n', p);
        if (eol == std::string_view::npos || eol > endNodes) eol = endNodes;
        std::string ln(sv.substr(p, eol - p));
        p = eol + 1;
        int id = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (sscanf_s(ln.c_str(), "%d %f %f %f", &id, &x, &y, &z) == 4) {
            mesh.vertices.push_back({ x, y, z });
        }
    }
    if (mesh.vertices.empty()) return false;

    size_t ep = sv.find('\n', elemsPos);
    if (ep == std::string_view::npos) return false;
    ++ep;
    size_t endElems = sv.find("$EndElements", ep);
    if (endElems == std::string_view::npos) endElems = sv.size();
    size_t enl = sv.find('\n', ep);
    if (enl != std::string_view::npos && enl < endElems) ep = enl + 1;

    uint32_t nv = static_cast<uint32_t>(mesh.vertices.size());
    while (ep < endElems) {
        size_t eol = sv.find('\n', ep);
        if (eol == std::string_view::npos || eol > endElems) eol = endElems;
        std::string ln(sv.substr(ep, eol - ep));
        ep = eol + 1;

        std::vector<int> vals;
        const char* cur = ln.c_str();
        char* next = nullptr;
        while (*cur) {
            int v = static_cast<int>(std::strtol(cur, &next, 10));
            if (next == cur) break;
            vals.push_back(v);
            cur = next;
        }
        // Gmsh v2: elm-number elm-type number-of-tags <tags...> node-number-list
        if (vals.size() >= 6) {
            int elmType = vals[1];
            int numTags = vals[2];
            size_t nodeStart = static_cast<size_t>(3 + std::max(0, numTags));
            if (elmType == 2 && nodeStart + 3 <= vals.size()) {
                uint32_t i0 = static_cast<uint32_t>(vals[nodeStart] - 1);
                uint32_t i1 = static_cast<uint32_t>(vals[nodeStart + 1] - 1);
                uint32_t i2 = static_cast<uint32_t>(vals[nodeStart + 2] - 1);
                if (i0 < nv && i1 < nv && i2 < nv) mesh.tris.push_back({ i0, i1, i2, {} });
            } else if ((elmType == 3 || elmType == 4) && nodeStart + 4 <= vals.size()) {
                uint32_t i0 = static_cast<uint32_t>(vals[nodeStart] - 1);
                uint32_t i1 = static_cast<uint32_t>(vals[nodeStart + 1] - 1);
                uint32_t i2 = static_cast<uint32_t>(vals[nodeStart + 2] - 1);
                uint32_t i3 = static_cast<uint32_t>(vals[nodeStart + 3] - 1);
                if (i0 < nv && i1 < nv && i2 < nv && i3 < nv) {
                    mesh.tris.push_back({ i0, i1, i2, {} });
                    mesh.tris.push_back({ i0, i2, i3, {} });
                }
            }
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
