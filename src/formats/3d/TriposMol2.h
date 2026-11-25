#pragma once
// TriposMol2.h — Tripos Sybyl 3D Molecular Structure (.mol2) reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseTriposMol2(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #60. Tripos Sybyl (.mol2) & GROMACS (.gro) 3D Molecular Structures
    if (ext == L".mol2") {
        size_t atomSec = sv.find("@<TRIPOS>ATOM");
        if (atomSec == std::string_view::npos) return false;
        mesh.formatName = L"Tripos Sybyl 3D Molecule (.mol2)";

        std::vector<Vec3f> atoms;
        size_t pos = sv.find('\n', atomSec);
        if (pos == std::string_view::npos) return false;
        ++pos;
        while (pos < sv.size() && atoms.size() < 4000) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            size_t s = ln.find_first_not_of(" \t\r");
            if (s == std::string::npos) continue;
            if (ln[s] == '@') break;
            int id = 0; char sym[64] = {}; float x = 0, y = 0, z = 0;
            if (sscanf_s(ln.c_str() + s, "%d %63s %f %f %f", &id, sym, static_cast<unsigned>(sizeof(sym)), &x, &y, &z) == 5) {
                atoms.push_back({ x, y, z });
                DetailMeshSciCad::AddOctaSphere(mesh, x, y, z, 0.34f);
            }
        }

        size_t bondSec = sv.find("@<TRIPOS>BOND");
        if (bondSec != std::string_view::npos) {
            size_t bp = sv.find('\n', bondSec);
            if (bp != std::string_view::npos) {
                ++bp;
                while (bp < sv.size()) {
                    size_t eol = bp;
                    while (eol < sv.size() && sv[eol] != '\n') ++eol;
                    std::string bln(sv.substr(bp, eol - bp));
                    bp = eol + 1;
                    size_t s = bln.find_first_not_of(" \t\r");
                    if (s == std::string::npos) continue;
                    if (bln[s] == '@') break;
                    int bid = 0, a1 = 0, a2 = 0;
                    if (sscanf_s(bln.c_str() + s, "%d %d %d", &bid, &a1, &a2) == 3) {
                        if (a1 >= 1 && a2 >= 1 && static_cast<size_t>(a1) <= atoms.size() && static_cast<size_t>(a2) <= atoms.size()) {
                            DetailMeshSciCad::AddStrut3D(mesh, atoms[static_cast<size_t>(a1 - 1)], atoms[static_cast<size_t>(a2 - 1)], 0.10f);
                        }
                    }
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
