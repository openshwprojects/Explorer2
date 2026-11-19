#pragma once
// GromacsGro.h — GROMACS Molecular Coordinates (.gro) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseGromacsGro(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (ext == L".gro") {
        size_t nl1 = sv.find('\n');
        if (nl1 == std::string_view::npos) return false;
        size_t nl2 = sv.find('\n', nl1 + 1);
        if (nl2 == std::string_view::npos) return false;
        int nAtoms = std::atoi(std::string(sv.substr(nl1 + 1, nl2 - nl1 - 1)).c_str());
        if (nAtoms <= 0 || nAtoms > 200000) return false;
        mesh.formatName = L"GROMACS Molecular Coordinates (.gro)";

        std::vector<Vec3f> atoms;
        size_t pos = nl2 + 1;
        for (int i = 0; i < nAtoms && pos < sv.size() && i < 4000; ++i) {
            size_t eol = sv.find('\n', pos);
            if (eol == std::string_view::npos) eol = sv.size();
            std::string ln(sv.substr(pos, eol - pos));
            pos = eol + 1;
            if (ln.size() >= 44) {
                float x = std::strtof(ln.substr(20, 8).c_str(), nullptr) * 10.0f;
                float y = std::strtof(ln.substr(28, 8).c_str(), nullptr) * 10.0f;
                float z = std::strtof(ln.substr(36, 8).c_str(), nullptr) * 10.0f;
                atoms.push_back({ x, y, z });
                Detail3D200::AddOctaSphere(mesh, x, y, z, 0.34f);
            }
        }
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < std::min(atoms.size(), i + 24); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.3f && d2 < 3.6f) Detail3D200::AddStrut3D(mesh, atoms[i], atoms[j], 0.09f);
            }
        }
        return !mesh.vertices.empty() && !mesh.tris.empty();
    }
    return false;
}

} // namespace Formats
