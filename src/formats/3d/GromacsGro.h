#pragma once
// GromacsGro.h — GROMACS Molecular Coordinates (.gro) 3D reader.
#include "../common/MeshFormatCommon.h"

namespace Formats {

inline bool ParseGromacsGro(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    if (!data || size < 16 || ext != L".gro") return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    size_t pos = 0;
    std::string_view titleLine, countLine;
    if (!ReaderText::NextLineSv(sv, pos, titleLine)) return false;
    if (!ReaderText::NextLineSv(sv, pos, countLine)) return false;

    std::string_view cntTrim = ReaderText::TrimSv(countLine);
    if (cntTrim.empty()) return false;
    int64_t nAtoms64 = 0;
    if (!ReaderText::ParseInt64Sv(cntTrim, nAtoms64) || nAtoms64 <= 0 || nAtoms64 > 500000) return false;
    const size_t nAtoms = static_cast<size_t>(nAtoms64);

    auto parseGroFloatSlice = [](std::string_view s, float& out) -> bool {
        s = ReaderText::TrimSv(s);
        if (s.empty() || s.size() >= 64) return false;
        char buf[64];
        std::memcpy(buf, s.data(), s.size());
        buf[s.size()] = '\0';
        char* endp = nullptr;
        float v = std::strtof(buf, &endp);
        if (endp == buf || *endp != '\0' || !std::isfinite(v)) return false;
        out = v;
        return true;
    };

    std::vector<Vec3f> allAtoms;
    allAtoms.reserve(std::min<size_t>(nAtoms, 65536));
    for (size_t i = 0; i < nAtoms; ++i) {
        std::string_view ln;
        if (!ReaderText::NextLineSv(sv, pos, ln)) return false; // truncated atom table
        if (ln.size() < 25) return false;

        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool ok = false;
        if (ln.size() >= 44 &&
            parseGroFloatSlice(ln.substr(20, 8), x) &&
            parseGroFloatSlice(ln.substr(28, 8), y) &&
            parseGroFloatSlice(ln.substr(36, 8), z))
        {
            ok = true;
        } else if (ln.size() > 20) {
            std::string_view rem = ln.substr(20), tx, ty, tz;
            if (ReaderText::NextTokenSv(rem, tx) && parseGroFloatSlice(tx, x) &&
                ReaderText::NextTokenSv(rem, ty) && parseGroFloatSlice(ty, y) &&
                ReaderText::NextTokenSv(rem, tz) && parseGroFloatSlice(tz, z))
            {
                ok = true;
            }
        }
        if (!ok) return false;
        allAtoms.push_back({ x * 10.0f, y * 10.0f, z * 10.0f });
    }

    mesh.formatName = L"GROMACS Molecular Coordinates (.gro)";
    constexpr size_t kMaxVisualAtoms = 25000;
    const size_t stride = std::max<size_t>(1, (allAtoms.size() + kMaxVisualAtoms - 1) / kMaxVisualAtoms);
    std::vector<Vec3f> atoms;
    atoms.reserve(std::min(allAtoms.size(), kMaxVisualAtoms));
    for (size_t i = 0; i < allAtoms.size() && atoms.size() < kMaxVisualAtoms; i += stride) {
        atoms.push_back(allAtoms[i]);
        DetailMeshSciCad::AddOctaSphere(mesh, allAtoms[i].x, allAtoms[i].y, allAtoms[i].z, 0.34f);
    }
    if (stride == 1 && atoms.size() <= 8000) {
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < std::min(atoms.size(), i + 28); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.3f && d2 < 3.6f) DetailMeshSciCad::AddStrut3D(mesh, atoms[i], atoms[j], 0.09f);
            }
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
