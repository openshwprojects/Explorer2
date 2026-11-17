#pragma once
#include "MeshBuild3D.h"
// ChemMol.h — MDL Molfile / SD file (.mol / .sdf, first record): V2000 fixed-column atom and bond blocks and
// V3000 ("M  V30") CTAB blocks. Bonds keep pointing at the right atoms even when an atom line is damaged.
// Atoms are octahedra, bonds thin struts.

namespace Formats {

inline bool ParseChemMolMesh(std::string_view sv, LoadedMesh3D& mesh) {
    mesh.formatName = L"MDL Molfile / SDF Molecule (.mol)";
    std::vector<std::string_view> lines;
    for (size_t pos = 0; pos < sv.size() && lines.size() < 200000;) {
        size_t eol = sv.find('\n', pos);
        if (eol == std::string_view::npos) eol = sv.size();
        std::string_view ln = sv.substr(pos, eol - pos);
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        lines.push_back(ln);
        pos = eol + 1;
        if (ln == "$$$$") break; // end of the first SD record
    }
    if (lines.size() < 4) return false;

    struct Atom { Vec3f p; bool ok = false; };
    std::vector<Atom> atoms;
    std::vector<std::pair<int, int>> bonds; // 1-based atom numbers
    auto words = [](std::string_view s) {
        std::vector<std::string_view> w;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            size_t st = i;
            while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
            if (i > st) w.push_back(s.substr(st, i - st));
        }
        return w;
    };
    auto toF = [](std::string_view s, float& v) {
        std::string t(s);
        char* end = nullptr;
        v = std::strtof(t.c_str(), &end);
        return end != t.c_str() && std::isfinite(v);
    };

    const std::string_view counts = lines[3];
    if (counts.find("V3000") != std::string_view::npos) {
        // V3000: "M  V30 BEGIN ATOM" / "M  V30 index type x y z ..." / "M  V30 END ATOM", same for BOND
        enum { None, InAtoms, InBonds } block = None;
        std::unordered_map<int, size_t> byIndex;
        for (size_t i = 4; i < lines.size(); ++i) {
            std::string_view ln = lines[i];
            if (ln.rfind("M  END", 0) == 0) break;
            if (ln.rfind("M  V30 ", 0) != 0) continue;
            std::string_view body = ln.substr(7);
            if (body.rfind("BEGIN ATOM", 0) == 0) { block = InAtoms; continue; }
            if (body.rfind("BEGIN BOND", 0) == 0) { block = InBonds; continue; }
            if (body.rfind("END", 0) == 0) { block = None; continue; }
            auto w = words(body);
            if (block == InAtoms && w.size() >= 5 && atoms.size() < 100000) {
                Atom a;
                a.ok = toF(w[2], a.p.x) && toF(w[3], a.p.y) && toF(w[4], a.p.z);
                byIndex[std::atoi(std::string(w[0]).c_str())] = atoms.size();
                atoms.push_back(a);
            } else if (block == InBonds && w.size() >= 4) {
                auto a = byIndex.find(std::atoi(std::string(w[2]).c_str()));
                auto b = byIndex.find(std::atoi(std::string(w[3]).c_str()));
                if (a != byIndex.end() && b != byIndex.end()) bonds.emplace_back(static_cast<int>(a->second) + 1, static_cast<int>(b->second) + 1);
            }
        }
    } else {
        // V2000 counts line: aaabbb... (3-character fields)
        int numAtoms = std::atoi(std::string(counts.substr(0, std::min<size_t>(3, counts.size()))).c_str());
        int numBonds = (counts.size() >= 6) ? std::atoi(std::string(counts.substr(3, 3)).c_str()) : 0;
        if (numAtoms <= 0 || numAtoms > 100000 || static_cast<size_t>(4 + numAtoms) > lines.size()) return false;
        for (int i = 0; i < numAtoms; ++i) {
            std::string_view ln = lines[4 + static_cast<size_t>(i)];
            Atom a;
            // x, y, z in columns 1-10, 11-20, 21-30; fall back to whitespace-separated values
            if (ln.size() >= 30) a.ok = toF(ln.substr(0, 10), a.p.x) && toF(ln.substr(10, 10), a.p.y) && toF(ln.substr(20, 10), a.p.z);
            if (!a.ok) {
                auto w = words(ln);
                a.ok = w.size() >= 3 && toF(w[0], a.p.x) && toF(w[1], a.p.y) && toF(w[2], a.p.z);
            }
            atoms.push_back(a); // kept even when damaged, so bond atom numbers stay right
        }
        for (int i = 0; i < numBonds && static_cast<size_t>(4 + numAtoms + i) < lines.size(); ++i) {
            std::string_view bl = lines[4 + static_cast<size_t>(numAtoms + i)];
            if (bl.size() < 6) continue;
            bonds.emplace_back(std::atoi(std::string(bl.substr(0, 3)).c_str()), std::atoi(std::string(bl.substr(3, 3)).c_str()));
        }
    }
    size_t good = 0;
    for (const auto& a : atoms) {
        if (!a.ok) continue;
        MeshBuild3D::EmitOctahedron(mesh, a.p, 0.3f);
        ++good;
    }
    if (good == 0) return false;
    for (const auto& b : bonds) {
        if (b.first < 1 || b.second < 1 || b.first > static_cast<int>(atoms.size()) || b.second > static_cast<int>(atoms.size())) continue;
        const Atom& a1 = atoms[static_cast<size_t>(b.first - 1)];
        const Atom& a2 = atoms[static_cast<size_t>(b.second - 1)];
        if (a1.ok && a2.ok) MeshBuild3D::EmitEdgeStrut(mesh, a1.p, a2.p, 0.08f);
    }
    return !mesh.tris.empty();
}

} // namespace Formats
