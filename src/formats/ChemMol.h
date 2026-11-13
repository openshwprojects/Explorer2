#pragma once
// ChemMol.h — MDL Molfile / Structure Data File (.mol / .sdf) 3D Chemical Structure Parser

namespace Formats {

inline bool ParseChemMolMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"MDL Molfile / SDF Molecule (.mol)";
    std::vector<std::string_view> lines;
    size_t pos = 0;
    while (pos < sv.size() && lines.size() < 10000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        lines.push_back(ln);
        pos = eol + 1;
    }
    if (lines.size() < 5) return false;

    int numAtoms = std::atoi(std::string(lines[3].substr(0, std::min<size_t>(3, lines[3].size()))).c_str());
    int numBonds = (lines[3].size() >= 6) ? std::atoi(std::string(lines[3].substr(3, 3)).c_str()) : 0;
    if (numAtoms <= 0 || static_cast<size_t>(4 + numAtoms) > lines.size()) return false;

    std::vector<Vec3f> atoms;
    for (int i = 0; i < numAtoms; ++i) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (sscanf_s(std::string(lines[4 + i]).c_str(), "%f %f %f", &x, &y, &z) >= 2) {
            atoms.push_back({ x, y, z });
        }
    }
    if (atoms.empty()) return false;

    const float r = 0.35f;
    for (const auto& p : atoms) {
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y,     p.z     });
        mesh.vertices.push_back({ p.x - r, p.y,     p.z     });
        mesh.vertices.push_back({ p.x,     p.y,     p.z + r });
        mesh.vertices.push_back({ p.x,     p.y,     p.z - r });
        mesh.tris.push_back({ b + 0, b + 4, b + 2, {} });
        mesh.tris.push_back({ b + 0, b + 2, b + 5, {} });
        mesh.tris.push_back({ b + 0, b + 5, b + 3, {} });
        mesh.tris.push_back({ b + 0, b + 3, b + 4, {} });
        mesh.tris.push_back({ b + 1, b + 2, b + 4, {} });
        mesh.tris.push_back({ b + 1, b + 5, b + 2, {} });
        mesh.tris.push_back({ b + 1, b + 3, b + 5, {} });
        mesh.tris.push_back({ b + 1, b + 4, b + 3, {} });
    }

    for (int i = 0; i < numBonds && static_cast<size_t>(4 + numAtoms + i) < lines.size(); ++i) {
        std::string_view bl = lines[4 + numAtoms + i];
        if (bl.size() < 6) continue;
        int a1 = std::atoi(std::string(bl.substr(0, 3)).c_str()) - 1;
        int a2 = std::atoi(std::string(bl.substr(3, 3)).c_str()) - 1;
        if (a1 >= 0 && a1 < static_cast<int>(atoms.size()) && a2 >= 0 && a2 < static_cast<int>(atoms.size())) {
            const Vec3f& p1 = atoms[a1];
            const Vec3f& p2 = atoms[a2];
            uint32_t sb = static_cast<uint32_t>(mesh.vertices.size());
            float br = 0.10f;
            mesh.vertices.push_back({ p1.x - br, p1.y, p1.z });
            mesh.vertices.push_back({ p1.x + br, p1.y, p1.z });
            mesh.vertices.push_back({ p2.x + br, p2.y, p2.z });
            mesh.vertices.push_back({ p2.x - br, p2.y, p2.z });
            mesh.tris.push_back({ sb + 0, sb + 1, sb + 2, {} });
            mesh.tris.push_back({ sb + 0, sb + 2, sb + 3, {} });
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
