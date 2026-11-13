#pragma once
// ValveSmd.h — Valve Studiomdl Data (.smd) Source Engine 3D Triangle Mesh Parser

namespace Formats {

inline bool ParseValveSmdMesh(std::string_view sv, LoadedMesh3D& mesh) {
    size_t triPos = sv.find("triangles");
    if (triPos == std::string_view::npos) return false;
    mesh.formatName = L"Valve Studiomdl Data (.smd)";

    size_t pos = sv.find('\n', triPos);
    if (pos == std::string_view::npos) return false;
    ++pos;

    std::vector<Vec3f> triVerts;
    while (pos < sv.size() && mesh.tris.size() < 40000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        while (!ln.empty() && (ln.front() == ' ' || ln.front() == '\t')) ln.remove_prefix(1);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.remove_suffix(1);
        if (ln.empty()) continue;
        if (ln == "end") break;

        // Vertex lines start with a bone integer ID: <bone> <x> <y> <z> <nx> <ny> <nz> <u> <v>
        int bone = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (sscanf_s(std::string(ln).c_str(), "%d %f %f %f", &bone, &x, &y, &z) == 4) {
            triVerts.push_back({ x, z, -y });
            if (triVerts.size() == 3) {
                uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(triVerts[0]);
                mesh.vertices.push_back(triVerts[1]);
                mesh.vertices.push_back(triVerts[2]);
                mesh.tris.push_back({ b, b + 1, b + 2, {} });
                triVerts.clear();
            }
        } else {
            // Material name line resets current triangle accumulator
            triVerts.clear();
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
