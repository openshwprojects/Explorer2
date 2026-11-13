#pragma once
// ProteinPdb.h — Protein Data Bank (.pdb) 3D Molecular Structure Parser

namespace Formats {

inline bool ParseProteinPdbMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"Protein Data Bank Molecule (.pdb)";
    std::vector<Vec3f> atoms;

    size_t pos = 0;
    while (pos < sv.size() && atoms.size() < 15000) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (ln.size() < 46) continue;
        if (ln.rfind("ATOM  ", 0) == 0 || ln.rfind("HETATM", 0) == 0) {
            float x = std::strtof(std::string(ln.substr(30, 8)).c_str(), nullptr);
            float y = std::strtof(std::string(ln.substr(38, 8)).c_str(), nullptr);
            float z = std::strtof(std::string(ln.substr(46, std::min<size_t>(8, ln.size() - 46))).c_str(), nullptr);
            if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                atoms.push_back({ x, y, z });
            }
        }
    }
    if (atoms.empty()) return false;

    const float r = 0.65f;
    for (size_t i = 0; i < atoms.size(); ++i) {
        const Vec3f& p = atoms[i];
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        // Octahedron per atom
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

        // Covalent bond struts to nearby atoms (< 1.95 Angstrom)
        for (size_t j = i + 1; j < std::min(atoms.size(), i + 12); ++j) {
            float dx = atoms[j].x - p.x, dy = atoms[j].y - p.y, dz = atoms[j].z - p.z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 0.2f && d2 < 3.8f) {
                uint32_t sb = static_cast<uint32_t>(mesh.vertices.size());
                float br = 0.16f;
                mesh.vertices.push_back({ p.x - br, p.y, p.z });
                mesh.vertices.push_back({ p.x + br, p.y, p.z });
                mesh.vertices.push_back({ atoms[j].x + br, atoms[j].y, atoms[j].z });
                mesh.vertices.push_back({ atoms[j].x - br, atoms[j].y, atoms[j].z });
                mesh.tris.push_back({ sb + 0, sb + 1, sb + 2, {} });
                mesh.tris.push_back({ sb + 0, sb + 2, sb + 3, {} });
            }
        }
    }
    return !mesh.tris.empty();
}

} // namespace Formats
