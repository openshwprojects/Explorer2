#pragma once
// MovieByuMesh.h — Brigham Young University Movie.BYU (.byu) engineering polygon mesh reader
// Supports free-format and Fortran 6E12.5 fixed-format floats (including glued negative exponents),
// optional single-line title header, 1-based part range validation, and negative-index-terminated
// polygon rings ear-clipped via EmitPlanarPolygon.

namespace Formats {

namespace DetailMovieByu {

// Splits whitespace-separated tokens AND Fortran fixed-width glued floats (e.g. "1.2500E+01-3.5000E+00")
inline void TokenizeByuNumbers(std::string_view sv, std::vector<std::string_view>& out) {
    const size_t n = sv.size();
    size_t i = 0;
    while (i < n) {
        while (i < n && (sv[i] == ' ' || sv[i] == '\t' || sv[i] == '\r' || sv[i] == '\n')) ++i;
        if (i >= n) break;
        const size_t start = i;
        ++i;
        while (i < n) {
            const char c = sv[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
            // Split glued sign if not immediately preceded by E/e/D/d
            if ((c == '+' || c == '-') && i > start) {
                const char prev = sv[i - 1];
                if (prev != 'e' && prev != 'E' && prev != 'd' && prev != 'D') break;
            }
            ++i;
        }
        out.push_back(sv.substr(start, i - start));
        if (out.size() > 12000000) break;
    }
}

} // namespace DetailMovieByu

inline bool ParseMovieByuMesh(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;
    std::string_view sv = ReaderText::StripUtf8Bom(std::string_view(reinterpret_cast<const char*>(data), size));
    if (sv.find('\0') != std::string_view::npos) return false;

    // Check whether line 0 is the 4-int header or a 1-line title preceding the 4-int header
    size_t linePos = 0;
    std::string_view firstLine;
    while (ReaderText::NextLineSv(sv, linePos, firstLine)) {
        if (!ReaderText::TrimSv(firstLine).empty()) break;
    }
    if (firstLine.empty()) return false;

    std::vector<std::string_view> l0Toks;
    DetailMovieByu::TokenizeByuNumbers(firstLine, l0Toks);
    int hdrVals[4] = {};
    bool line0IsHdr = (l0Toks.size() == 4);
    if (line0IsHdr) {
        for (int k = 0; k < 4; ++k) {
            if (!ReaderText::ParseIntSv(l0Toks[k], hdrVals[k])) { line0IsHdr = false; break; }
        }
    }

    std::string_view bodySv = line0IsHdr ? sv : sv.substr(linePos);
    std::vector<std::string_view> toks;
    DetailMovieByu::TokenizeByuNumbers(bodySv, toks);
    if (toks.size() < 12) return false;

    size_t idx = 0;
    int nParts = 0, nVerts = 0, nPolys = 0, nEdges = 0;
    if (!ReaderText::ParseIntSv(toks[idx++], nParts) ||
        !ReaderText::ParseIntSv(toks[idx++], nVerts) ||
        !ReaderText::ParseIntSv(toks[idx++], nPolys) ||
        !ReaderText::ParseIntSv(toks[idx++], nEdges)) {
        return false;
    }

    if (nParts < 1 || nParts > 65536) return false;
    if (nVerts < 3 || nVerts > 2000000) return false;
    if (nPolys < 1 || nPolys > 4000000) return false;
    if (nEdges < nPolys * 3 || nEdges > 12000000) return false;

    const uint64_t neededToks = 4ULL + static_cast<uint64_t>(nParts) * 2ULL +
                                static_cast<uint64_t>(nVerts) * 3ULL +
                                static_cast<uint64_t>(nEdges);
    if (toks.size() < neededToks) return false;

    // Validate 1-based part polygon ranges
    for (int p = 0; p < nParts; ++p) {
        int pStart = 0, pEnd = 0;
        if (!ReaderText::ParseIntSv(toks[idx++], pStart) ||
            !ReaderText::ParseIntSv(toks[idx++], pEnd)) {
            return false;
        }
        if (pStart < 1 || pEnd < pStart || pEnd > nPolys) return false;
    }

    mesh.vertices.reserve(static_cast<size_t>(nVerts));
    for (int v = 0; v < nVerts; ++v) {
        float x = 0, y = 0, z = 0;
        if (!ReaderText::ParseFloatSv(toks[idx++], x) ||
            !ReaderText::ParseFloatSv(toks[idx++], y) ||
            !ReaderText::ParseFloatSv(toks[idx++], z)) {
            return false;
        }
        mesh.vertices.push_back({ x, y, z });
    }

    std::vector<uint32_t> curLoop;
    int completedPolys = 0;
    for (int e = 0; e < nEdges; ++e) {
        int rawV = 0;
        if (!ReaderText::ParseIntSv(toks[idx++], rawV) || rawV == 0) return false;
        const bool isLast = (rawV < 0);
        const int absV = isLast ? -rawV : rawV;
        if (absV < 1 || absV > nVerts) return false;
        curLoop.push_back(static_cast<uint32_t>(absV - 1));

        if (isLast) {
            if (curLoop.size() < 3) return false;
            if (curLoop.size() == 3) {
                if (curLoop[0] != curLoop[1] && curLoop[1] != curLoop[2] && curLoop[0] != curLoop[2]) {
                    mesh.tris.push_back({ curLoop[0], curLoop[1], curLoop[2], {} });
                }
            } else {
                std::vector<Vec3f> ring;
                ring.reserve(curLoop.size());
                for (uint32_t vi : curLoop) ring.push_back(mesh.vertices[vi]);
                MeshBuild3D::EmitPlanarPolygon(mesh, ring, {});
            }
            curLoop.clear();
            ++completedPolys;
        }
    }

    if (!curLoop.empty() || completedPolys != nPolys || mesh.tris.empty()) return false;
    mesh.formatName = L"Movie.BYU Polygon Mesh (.byu)";
    return true;
}

} // namespace Formats
