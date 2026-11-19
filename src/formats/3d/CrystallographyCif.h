#pragma once
// CrystallographyCif.h — Crystallographic Information File (.cif) lattice & atom reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseCrystallographyCif(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    // #59. Crystallographic Information File (.cif — Unit-Cell + Fractional Crystal Lattice)
    if (ext == L".cif") {
        bool hasCell = (sv.find("_cell_length_a") != std::string_view::npos);
        bool hasAtoms = (sv.find("_atom_site_fract_x") != std::string_view::npos);
        if (!hasCell && !hasAtoms) return false;
        mesh.formatName = L"Crystallographic Lattice (.cif)";

        auto readTagFloat = [&](const char* tag, float defVal) -> float {
            size_t p = sv.find(tag);
            if (p == std::string_view::npos) return defVal;
            p += std::strlen(tag);
            while (p < sv.size() && (sv[p] == ' ' || sv[p] == '\t')) ++p;
            return std::strtof(sv.data() + p, nullptr);
        };
        float a = std::max(0.5f, readTagFloat("_cell_length_a", 5.43f));
        float b = std::max(0.5f, readTagFloat("_cell_length_b", a));
        float c = std::max(0.5f, readTagFloat("_cell_length_c", a));
        float alpha = readTagFloat("_cell_angle_alpha", 90.0f) * 3.14159265f / 180.0f;
        float beta  = readTagFloat("_cell_angle_beta",  90.0f) * 3.14159265f / 180.0f;
        float gamma = readTagFloat("_cell_angle_gamma", 90.0f) * 3.14159265f / 180.0f;

        Vec3f vA = { a, 0.0f, 0.0f };
        Vec3f vB = { b * std::cos(gamma), b * std::sin(gamma), 0.0f };
        float cx = c * std::cos(beta);
        float cy = c * (std::cos(alpha) - std::cos(beta) * std::cos(gamma)) / std::max(1e-4f, std::sin(gamma));
        float cz = std::sqrt(std::max(0.01f, c * c - cx * cx - cy * cy));
        Vec3f vC = { cx, cy, cz };

        auto fracToCart = [&](float fx, float fy, float fz) -> Vec3f {
            return {
                fx * vA.x + fy * vB.x + fz * vC.x,
                fx * vA.y + fy * vB.y + fz * vC.y,
                fx * vA.z + fy * vB.z + fz * vC.z
            };
        };

        float strutR = std::max({ a, b, c }) * 0.018f;
        if (hasCell) {
            Vec3f uc[8] = {
                fracToCart(0,0,0), fracToCart(1,0,0), fracToCart(1,1,0), fracToCart(0,1,0),
                fracToCart(0,0,1), fracToCart(1,0,1), fracToCart(1,1,1), fracToCart(0,1,1)
            };
            static const int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
            };
            for (const auto& e : edges) Detail3D200::AddStrut3D(mesh, uc[e[0]], uc[e[1]], strutR);
        }

        size_t fxPos = sv.find("_atom_site_fract_x");
        std::vector<Vec3f> atoms;
        if (fxPos != std::string_view::npos) {
            size_t loopStart = sv.rfind("loop_", fxPos);
            size_t pos = (loopStart != std::string_view::npos) ? (loopStart + 5) : fxPos;
            int colIdx = 0, colX = -1, colY = -1, colZ = -1;
            while (pos < sv.size()) {
                size_t eol = sv.find('\n', pos);
                if (eol == std::string_view::npos) eol = sv.size();
                std::string ln(sv.substr(pos, eol - pos));
                pos = eol + 1;
                size_t s = ln.find_first_not_of(" \t\r");
                if (s == std::string::npos || ln[s] == '#') continue;
                if (ln[s] == '_') {
                    if (ln.find("_atom_site_fract_x") != std::string::npos) colX = colIdx;
                    else if (ln.find("_atom_site_fract_y") != std::string::npos) colY = colIdx;
                    else if (ln.find("_atom_site_fract_z") != std::string::npos) colZ = colIdx;
                    ++colIdx;
                    continue;
                }
                if (ln.compare(s, 5, "loop_") == 0 || ln.compare(s, 5, "data_") == 0) break;
                if (colX >= 0 && colY >= 0 && colZ >= 0 && atoms.size() < 512) {
                    std::vector<std::string> cols;
                    size_t tp = s;
                    while (tp < ln.size()) {
                        while (tp < ln.size() && (ln[tp] == ' ' || ln[tp] == '\t' || ln[tp] == '\r')) ++tp;
                        size_t te = ln.find_first_of(" \t\r", tp);
                        if (te == std::string::npos) te = ln.size();
                        if (te > tp) cols.push_back(ln.substr(tp, te - tp));
                        tp = te;
                    }
                    int maxCol = std::max({ colX, colY, colZ });
                    if (static_cast<int>(cols.size()) > maxCol) {
                        float x = std::strtof(cols[static_cast<size_t>(colX)].c_str(), nullptr);
                        float y = std::strtof(cols[static_cast<size_t>(colY)].c_str(), nullptr);
                        float z = std::strtof(cols[static_cast<size_t>(colZ)].c_str(), nullptr);
                        atoms.push_back(fracToCart(x, y, z));
                    }
                }
            }
        }
        float atomR = std::max({ a, b, c }) * 0.075f;
        for (const auto& at : atoms) Detail3D200::AddOctaSphere(mesh, at.x, at.y, at.z, atomR);
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < atoms.size(); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.25f && d2 < 9.5f) {
                    Detail3D200::AddStrut3D(mesh, atoms[i], atoms[j], strutR * 1.4f);
                }
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
