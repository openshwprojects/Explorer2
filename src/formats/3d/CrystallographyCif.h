#pragma once
// CrystallographyCif.h — Crystallographic Information File & PDBx/mmCIF (.cif) reader.
//
// Supports:
//   - Small-molecule CIF 1.1 (`_cell_length_a/b/c`, `_atom_site_fract_x/y/z`) and
//     macromolecular PDBx/mmCIF (`_cell.length_a/b/c`, `_atom_site.Cartn_x/y/z`, `_atom_site.fract_x/y/z`).
//   - `_space_group_symop_operation_xyz` / `_space_group_symop.operation_xyz` / `_symmetry_equiv_pos_as_xyz`
//     3D symmetry operation evaluation & fractional unit-cell deduplication.
//   - Crystallographic standard uncertainty stripping (`0.1250(4)` -> `0.1250`), up to 25,000 atoms,
//     and strict rejection of 0-atom unit cells!
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace DetailCif {

inline bool ParseCifNumber(std::string_view tok, float& outVal) {
    tok = ReaderText::TrimSv(tok);
    if (tok.empty() || tok == "." || tok == "?") return false;
    // Strip standard uncertainty in parentheses, e.g. "5.4309(2)" -> "5.4309"
    size_t paren = tok.find('(');
    if (paren != std::string_view::npos) tok = tok.substr(0, paren);
    if (tok.empty()) return false;
    std::string tmp(tok);
    char* ep = nullptr;
    float v = std::strtof(tmp.c_str(), &ep);
    if (ep == tmp.c_str() || *ep != '\0' || !std::isfinite(v)) return false;
    outVal = v;
    return true;
}

inline void TokenizeCifRow(std::string_view line, std::vector<std::string_view>& cols) {
    cols.clear();
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        if (i >= line.size() || line[i] == '#') break;
        if (line[i] == '\'' || line[i] == '"') {
            char q = line[i++];
            size_t start = i;
            while (i < line.size()) {
                if (line[i] == q && (i + 1 == line.size() || line[i + 1] == ' ' || line[i + 1] == '\t' || line[i + 1] == '\r')) {
                    break;
                }
                ++i;
            }
            cols.push_back(line.substr(start, i - start));
            if (i < line.size() && line[i] == q) ++i;
        } else {
            size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
            cols.push_back(line.substr(start, i - start));
        }
    }
}

// Affine symmetry operator: f_out[axis] = m[axis][0]*x + m[axis][1]*y + m[axis][2]*z + t[axis]
struct SymOp3D {
    float m[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    float t[3]    = { 0, 0, 0 };
};

inline bool ParseSymOpComponent(std::string_view expr, float row[3], float& trans) {
    row[0] = row[1] = row[2] = 0.0f;
    trans = 0.0f;

    std::string s;
    for (char c : expr) {
        if (c != ' ' && c != '\t' && c != '\'' && c != '"') {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (s.empty()) return false;

    size_t i = 0;
    while (i < s.size()) {
        float sign = 1.0f;
        if (s[i] == '+') { ++i; }
        else if (s[i] == '-') { sign = -1.0f; ++i; }
        if (i >= s.size()) return false;

        // Optional numeric coefficient or fraction a/b
        float coeff = 1.0f;
        bool hasNum = false;
        if ((s[i] >= '0' && s[i] <= '9') || s[i] == '.') {
            hasNum = true;
            char* ep = nullptr;
            float num = std::strtof(s.c_str() + i, &ep);
            if (ep == s.c_str() + i) return false;
            i = static_cast<size_t>(ep - s.c_str());
            if (i < s.size() && s[i] == '/') {
                ++i;
                char* ep2 = nullptr;
                float den = std::strtof(s.c_str() + i, &ep2);
                if (ep2 == s.c_str() + i || std::fabs(den) < 1e-6f) return false;
                i = static_cast<size_t>(ep2 - s.c_str());
                num /= den;
            }
            coeff = num;
        }

        if (i < s.size() && s[i] == '*') ++i;

        if (i < s.size() && (s[i] == 'x' || s[i] == 'y' || s[i] == 'z')) {
            int ax = (s[i] == 'x') ? 0 : ((s[i] == 'y') ? 1 : 2);
            row[ax] += sign * coeff;
            ++i;
        } else if (hasNum) {
            trans += sign * coeff;
        } else {
            return false;
        }
    }
    return true;
}

inline bool ParseSymOpXyz(std::string_view opStr, SymOp3D& outOp) {
    size_t c1 = opStr.find(',');
    if (c1 == std::string_view::npos) return false;
    size_t c2 = opStr.find(',', c1 + 1);
    if (c2 == std::string_view::npos) return false;
    return ParseSymOpComponent(opStr.substr(0, c1), outOp.m[0], outOp.t[0]) &&
           ParseSymOpComponent(opStr.substr(c1 + 1, c2 - (c1 + 1)), outOp.m[1], outOp.t[1]) &&
           ParseSymOpComponent(opStr.substr(c2 + 1), outOp.m[2], outOp.t[2]);
}

inline float WrapUnitCell(float f) {
    float w = f - std::floor(f);
    if (w >= 0.9995f || w < 0.0f) w = 0.0f;
    return w;
}

} // namespace DetailCif

inline bool ParseCrystallographyCif(const uint8_t* data, size_t size, const std::wstring& ext, LoadedMesh3D& mesh) {
    using namespace DetailCif;
    if (ext != L".cif" || !data || size < 16) return false;

    const size_t nulScan = std::min<size_t>(size, 4096u);
    for (size_t i = 0; i < nulScan; ++i) {
        if (data[i] == 0) return false;
    }

    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.find("_atom_site") == std::string_view::npos) return false;

    auto findScalarTag = [&](std::initializer_list<const char*> tags, float defVal, bool& foundOut) -> float {
        for (const char* tag : tags) {
            size_t p = 0;
            size_t tlen = std::strlen(tag);
            while ((p = sv.find(tag, p)) != std::string_view::npos) {
                bool prevOk = (p == 0 || sv[p - 1] == '\n' || sv[p - 1] == '\r' || sv[p - 1] == ' ' || sv[p - 1] == '\t');
                bool nextOk = (p + tlen < sv.size() && (sv[p + tlen] == ' ' || sv[p + tlen] == '\t'));
                if (prevOk && nextOk) {
                    size_t vp = p + tlen;
                    while (vp < sv.size() && (sv[vp] == ' ' || sv[vp] == '\t')) ++vp;
                    size_t ve = vp;
                    while (ve < sv.size() && sv[ve] != ' ' && sv[ve] != '\t' && sv[ve] != '\r' && sv[ve] != '\n') ++ve;
                    float val = 0.0f;
                    if (ParseCifNumber(sv.substr(vp, ve - vp), val)) {
                        foundOut = true;
                        return val;
                    }
                }
                p += tlen;
            }
        }
        return defVal;
    };

    bool hasA = false, hasB = false, hasC = false, dummy = false;
    float a = std::max(0.5f, findScalarTag({ "_cell_length_a", "_cell.length_a" }, 5.43f, hasA));
    float b = std::max(0.5f, findScalarTag({ "_cell_length_b", "_cell.length_b" }, a, hasB));
    float c = std::max(0.5f, findScalarTag({ "_cell_length_c", "_cell.length_c" }, a, hasC));
    bool hasCell = (hasA && hasB && hasC);

    float alpha = findScalarTag({ "_cell_angle_alpha", "_cell.angle_alpha" }, 90.0f, dummy) * 3.14159265f / 180.0f;
    float beta  = findScalarTag({ "_cell_angle_beta",  "_cell.angle_beta"  }, 90.0f, dummy) * 3.14159265f / 180.0f;
    float gamma = findScalarTag({ "_cell_angle_gamma", "_cell.angle_gamma" }, 90.0f, dummy) * 3.14159265f / 180.0f;

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

    // Walk CIF `loop_` tables to collect symmetry operations AND `_atom_site` coordinates (fractional or Cartesian)
    std::vector<SymOp3D> symOps;
    std::vector<Vec3f> asymFractAtoms;
    std::vector<Vec3f> cartAtoms;
    constexpr size_t kMaxCifAtoms = 25000u;

    size_t linePos = 0;
    std::string_view rawLine;
    std::vector<std::string> loopHeaders;
    bool inLoopHeaders = false;
    bool inLoopRows = false;
    std::vector<std::string_view> rowToks;

    int symOpCol = -1;
    int colFx = -1, colFy = -1, colFz = -1;
    int colCx = -1, colCy = -1, colCz = -1;

    auto setupLoopColumns = [&]() {
        symOpCol = -1;
        colFx = colFy = colFz = -1;
        colCx = colCy = colCz = -1;
        for (size_t i = 0; i < loopHeaders.size(); ++i) {
            const std::string& h = loopHeaders[i];
            if (h == "_space_group_symop_operation_xyz" ||
                h == "_space_group_symop.operation_xyz" ||
                h == "_symmetry_equiv_pos_as_xyz") {
                symOpCol = static_cast<int>(i);
            } else if (h == "_atom_site_fract_x" || h == "_atom_site.fract_x") colFx = static_cast<int>(i);
            else if (h == "_atom_site_fract_y" || h == "_atom_site.fract_y")   colFy = static_cast<int>(i);
            else if (h == "_atom_site_fract_z" || h == "_atom_site.fract_z")   colFz = static_cast<int>(i);
            else if (h == "_atom_site_cartn_x" || h == "_atom_site.cartn_x")   colCx = static_cast<int>(i);
            else if (h == "_atom_site_cartn_y" || h == "_atom_site.cartn_y")   colCy = static_cast<int>(i);
            else if (h == "_atom_site_cartn_z" || h == "_atom_site.cartn_z")   colCz = static_cast<int>(i);
        }
    };

    while (ReaderText::NextLineSv(sv, linePos, rawLine)) {
        std::string_view line = ReaderText::TrimSv(rawLine);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;

        if (ReaderText::AsciiEqCI(line, "loop_")) {
            loopHeaders.clear();
            inLoopHeaders = true;
            inLoopRows = false;
            continue;
        }

        if (inLoopHeaders) {
            if (line.front() == '_') {
                std::string_view tagTok = line;
                std::string_view firstTok;
                ReaderText::NextTokenSv(tagTok, firstTok);
                loopHeaders.push_back(ReaderText::ToLowerAscii(firstTok));
                continue;
            }
            inLoopHeaders = false;
            inLoopRows = !loopHeaders.empty();
            setupLoopColumns();
        }

        if (inLoopRows) {
            if (line.front() == '_' || ReaderText::StartsWithCI(line, "data_") ||
                ReaderText::StartsWithCI(line, "save_") || ReaderText::AsciiEqCI(line, "stop_"))
            {
                inLoopRows = false;
                continue;
            }

            if (symOpCol >= 0) {
                TokenizeCifRow(line, rowToks);
                if (static_cast<int>(rowToks.size()) > symOpCol && symOps.size() < 256u) {
                    SymOp3D op{};
                    if (ParseSymOpXyz(rowToks[static_cast<size_t>(symOpCol)], op)) {
                        symOps.push_back(op);
                    }
                }
            } else if ((colCx >= 0 && colCy >= 0 && colCz >= 0) || (colFx >= 0 && colFy >= 0 && colFz >= 0)) {
                TokenizeCifRow(line, rowToks);
                if (colCx >= 0 && colCy >= 0 && colCz >= 0 &&
                    static_cast<int>(rowToks.size()) > std::max({ colCx, colCy, colCz }) &&
                    cartAtoms.size() < kMaxCifAtoms)
                {
                    float x = 0, y = 0, z = 0;
                    if (ParseCifNumber(rowToks[static_cast<size_t>(colCx)], x) &&
                        ParseCifNumber(rowToks[static_cast<size_t>(colCy)], y) &&
                        ParseCifNumber(rowToks[static_cast<size_t>(colCz)], z))
                    {
                        cartAtoms.push_back({ x, y, z });
                        continue;
                    }
                }
                if (colFx >= 0 && colFy >= 0 && colFz >= 0 &&
                    static_cast<int>(rowToks.size()) > std::max({ colFx, colFy, colFz }) &&
                    asymFractAtoms.size() < kMaxCifAtoms)
                {
                    float x = 0, y = 0, z = 0;
                    if (ParseCifNumber(rowToks[static_cast<size_t>(colFx)], x) &&
                        ParseCifNumber(rowToks[static_cast<size_t>(colFy)], y) &&
                        ParseCifNumber(rowToks[static_cast<size_t>(colFz)], z))
                    {
                        asymFractAtoms.push_back({ x, y, z });
                    }
                }
            }
        }
    }

    std::vector<Vec3f> atoms;
    if (!asymFractAtoms.empty()) {
        if (symOps.empty()) {
            for (const auto& f : asymFractAtoms) {
                atoms.push_back(fracToCart(f.x, f.y, f.z));
            }
        } else {
            // Expand fractional asymmetric unit via space-group symmetry operations and deduplicate in [0, 1)^3
            std::unordered_set<uint64_t> seenQuant;
            for (const auto& f : asymFractAtoms) {
                for (const auto& op : symOps) {
                    if (atoms.size() >= kMaxCifAtoms) break;
                    float gx = WrapUnitCell(op.m[0][0] * f.x + op.m[0][1] * f.y + op.m[0][2] * f.z + op.t[0]);
                    float gy = WrapUnitCell(op.m[1][0] * f.x + op.m[1][1] * f.y + op.m[1][2] * f.z + op.t[1]);
                    float gz = WrapUnitCell(op.m[2][0] * f.x + op.m[2][1] * f.y + op.m[2][2] * f.z + op.t[2]);
                    uint64_t qx = static_cast<uint64_t>(std::llround(gx * 500.0f)) & 0xFFFFULL;
                    uint64_t qy = static_cast<uint64_t>(std::llround(gy * 500.0f)) & 0xFFFFULL;
                    uint64_t qz = static_cast<uint64_t>(std::llround(gz * 500.0f)) & 0xFFFFULL;
                    uint64_t key = (qx << 32u) | (qy << 16u) | qz;
                    if (seenQuant.insert(key).second) {
                        atoms.push_back(fracToCart(gx, gy, gz));
                    }
                }
            }
        }
    } else if (!cartAtoms.empty()) {
        atoms = std::move(cartAtoms);
    }

    // Never return true on an empty unit-cell wireframe box with 0 atoms!
    if (atoms.empty()) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    mesh.formatName = asymFractAtoms.empty()
        ? L"Macromolecular Structure (PDBx/mmCIF .cif)"
        : L"Crystallographic Lattice (.cif)";

    Vec3f bmin = atoms[0], bmax = atoms[0];
    for (const auto& at : atoms) {
        bmin.x = std::min(bmin.x, at.x); bmin.y = std::min(bmin.y, at.y); bmin.z = std::min(bmin.z, at.z);
        bmax.x = std::max(bmax.x, at.x); bmax.y = std::max(bmax.y, at.y); bmax.z = std::max(bmax.z, at.z);
    }
    const float span = std::max({ hasCell ? std::max({ a, b, c }) : 0.0f, bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 2.0f });
    const float strutR = std::clamp(span * 0.012f, 0.04f, 0.25f);
    const float atomR  = std::clamp(span * (atoms.size() > 500 ? 0.015f : 0.055f), 0.12f, 0.65f);

    if (hasCell && asymFractAtoms.size() > 0) {
        Vec3f uc[8] = {
            fracToCart(0,0,0), fracToCart(1,0,0), fracToCart(1,1,0), fracToCart(0,1,0),
            fracToCart(0,0,1), fracToCart(1,0,1), fracToCart(1,1,1), fracToCart(0,1,1)
        };
        static const int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
        };
        for (const auto& e : edges) DetailMeshSciCad::AddStrut3D(mesh, uc[e[0]], uc[e[1]], strutR);
    }

    for (const auto& at : atoms) {
        DetailMeshSciCad::AddOctaSphere(mesh, at.x, at.y, at.z, atomR);
    }

    if (atoms.size() <= 1200u) {
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < atoms.size(); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.25f && d2 < 9.5f) {
                    DetailMeshSciCad::AddStrut3D(mesh, atoms[i], atoms[j], strutR * 1.3f);
                }
            }
        }
    } else {
        // Sequential polymer/protein chain window for large mmCIF macromolecules (>1,200 atoms)
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < std::min(atoms.size(), i + 12u); ++j) {
                float dx = atoms[i].x - atoms[j].x, dy = atoms[i].y - atoms[j].y, dz = atoms[i].z - atoms[j].z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.36f && d2 < 3.8f) {
                    DetailMeshSciCad::AddStrut3D(mesh, atoms[i], atoms[j], strutR);
                }
            }
        }
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
