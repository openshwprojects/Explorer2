#pragma once
#include "../common/MeshBuild3D.h"
// ProteinPdb.h — Protein Data Bank (.pdb): ATOM / HETATM coordinates of the first model (NMR ensembles end it
// with ENDMDL), alternate locations other than ' ' / 'A' skipped, bonds from CONECT records plus covalent
// distance (found with a spatial grid, not by file order). Atoms are octahedra, bonds thin struts.

namespace Formats {

inline bool ParseProteinPdbMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"Protein Data Bank Molecule (.pdb)";
    struct Atom { Vec3f p; int serial; };
    std::vector<Atom> atoms;
    std::vector<std::pair<int, int>> conect; // serial numbers
    constexpr size_t kMaxAtoms = 20000; // about 240k triangles with their bonds
    auto fieldF = [](std::string_view ln, size_t at, size_t len, float& out) {
        if (ln.size() < at + 1) return false;
        std::string t(ln.substr(at, std::min(len, ln.size() - at)));
        char* end = nullptr;
        out = std::strtof(t.c_str(), &end);
        return end != t.c_str() && std::isfinite(out);
    };
    auto fieldI = [](std::string_view ln, size_t at, size_t len) -> int {
        if (ln.size() <= at) return 0;
        std::string t(ln.substr(at, std::min(len, ln.size() - at)));
        return std::atoi(t.c_str());
    };

    for (size_t pos = 0; pos < sv.size();) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        if (ln.rfind("ENDMDL", 0) == 0) break; // first model only
        if ((ln.rfind("ATOM  ", 0) == 0 || ln.rfind("HETATM", 0) == 0) && ln.size() >= 54 && atoms.size() < kMaxAtoms) {
            char alt = ln[16];
            if (alt != ' ' && alt != 'A') continue;
            Atom a{};
            if (!fieldF(ln, 30, 8, a.p.x) || !fieldF(ln, 38, 8, a.p.y) || !fieldF(ln, 46, 8, a.p.z)) continue;
            a.serial = fieldI(ln, 6, 5);
            atoms.push_back(a);
        } else if (ln.rfind("CONECT", 0) == 0) {
            int from = fieldI(ln, 6, 5);
            for (size_t c = 11; c + 5 <= ln.size() && c <= 26; c += 5) {
                int to = fieldI(ln, c, 5);
                if (from > 0 && to > from) conect.emplace_back(from, to); // each bond once
            }
        }
    }
    if (atoms.empty()) return false;

    // Bonds: explicit CONECT pairs, then covalent distance (< 2.0 A) through a 2 A grid
    std::vector<std::pair<size_t, size_t>> bonds;
    std::unordered_map<int, size_t> bySerial;
    for (size_t i = 0; i < atoms.size(); ++i) if (atoms[i].serial > 0) bySerial.emplace(atoms[i].serial, i);
    std::set<std::pair<size_t, size_t>> seen;
    for (const auto& c : conect) {
        auto a = bySerial.find(c.first), b = bySerial.find(c.second);
        if (a != bySerial.end() && b != bySerial.end() && seen.insert({ std::min(a->second, b->second), std::max(a->second, b->second) }).second) {
            bonds.emplace_back(a->second, b->second);
        }
    }
    const float cell = 2.0f;
    auto key = [&](const Vec3f& p) {
        int64_t gx = static_cast<int64_t>(std::floor(p.x / cell)), gy = static_cast<int64_t>(std::floor(p.y / cell)),
                gz = static_cast<int64_t>(std::floor(p.z / cell));
        return ((gx & 0x1FFFFF) << 42) | ((gy & 0x1FFFFF) << 21) | (gz & 0x1FFFFF);
    };
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    for (size_t i = 0; i < atoms.size(); ++i) grid[key(atoms[i].p)].push_back(i);
    for (size_t i = 0; i < atoms.size() && bonds.size() < 400000; ++i) {
        const Vec3f& p = atoms[i].p;
        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
            Vec3f q = { p.x + dx * cell, p.y + dy * cell, p.z + dz * cell };
            auto it = grid.find(key(q));
            if (it == grid.end()) continue;
            for (size_t j : it->second) {
                if (j <= i) continue;
                float ex = atoms[j].p.x - p.x, ey = atoms[j].p.y - p.y, ez = atoms[j].p.z - p.z;
                float d2 = ex * ex + ey * ey + ez * ez;
                if (d2 > 0.16f && d2 < 4.0f && seen.insert({ i, j }).second) bonds.emplace_back(i, j);
            }
        }
    }

    for (const auto& a : atoms) MeshBuild3D::EmitOctahedron(mesh, a.p, 0.45f);
    for (const auto& b : bonds) MeshBuild3D::EmitEdgeStrut(mesh, atoms[b.first].p, atoms[b.second].p, 0.12f);
    return !mesh.tris.empty();
}

} // namespace Formats
