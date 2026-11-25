#pragma once
// Cart3dTriMesh.h — NASA Ames Cart3D Aerodynamic Surface Triangulation (.tri) reader
// Supports both ASCII Cart3D (.tri / .triq) and Fortran sequential unformatted binary
// Cart3D files (little-endian & big-endian, single & double precision vertex records).

namespace Formats {

namespace DetailCart3dTri {

inline uint32_t RdU32(const uint8_t* p, bool be) { return be ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p); }
inline int32_t  RdI32(const uint8_t* p, bool be) { return static_cast<int32_t>(RdU32(p, be)); }
inline float    RdF32(const uint8_t* p, bool be) { return be ? ByteIO::RdF32BE(p) : ByteIO::RdF32LE(p); }
inline double   RdF64(const uint8_t* p, bool be) { return be ? ByteIO::RdF64BE(p) : ByteIO::RdF64LE(p); }

inline bool TryParseBinaryCart3d(const uint8_t* data, size_t size, bool be, LoadedMesh3D& mesh) {
    if (size < 44) return false;
    const uint32_t r1 = RdU32(data + 0, be);
    if (r1 != 8u && r1 != 12u) return false;
    if (8u + r1 > size || RdU32(data + 4u + r1, be) != r1) return false;

    const int32_t nVerts = RdI32(data + 4u, be);
    const int32_t nTris  = RdI32(data + 8u, be);
    if (nVerts < 3 || nVerts > 2000000 || nTris < 1 || nTris > 4000000) return false;

    size_t pos = 8u + r1;
    if (pos + 8u > size) return false;
    const uint32_t r2 = RdU32(data + pos, be);
    const uint64_t f32Len = static_cast<uint64_t>(nVerts) * 12ULL;
    const uint64_t f64Len = static_cast<uint64_t>(nVerts) * 24ULL;
    if (r2 != f32Len && r2 != f64Len) return false;
    if (pos + 8ULL + r2 > size || RdU32(data + pos + 4u + r2, be) != r2) return false;

    const bool isF64 = (r2 == f64Len);
    const uint8_t* vp = data + pos + 4u;
    mesh.vertices.reserve(static_cast<size_t>(nVerts));
    for (int32_t i = 0; i < nVerts; ++i) {
        float x = 0, y = 0, z = 0;
        if (!isF64) {
            x = RdF32(vp + static_cast<size_t>(i) * 12u + 0u, be);
            y = RdF32(vp + static_cast<size_t>(i) * 12u + 4u, be);
            z = RdF32(vp + static_cast<size_t>(i) * 12u + 8u, be);
        } else {
            x = static_cast<float>(RdF64(vp + static_cast<size_t>(i) * 24u + 0u,  be));
            y = static_cast<float>(RdF64(vp + static_cast<size_t>(i) * 24u + 8u,  be));
            z = static_cast<float>(RdF64(vp + static_cast<size_t>(i) * 24u + 16u, be));
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        mesh.vertices.push_back({ x, z, -y });
    }
    pos += 8u + r2;

    if (pos + 8u > size) return false;
    const uint32_t r3 = RdU32(data + pos, be);
    const uint64_t triBytes = static_cast<uint64_t>(nTris) * 12ULL;
    if (r3 != triBytes || pos + 8ULL + r3 > size || RdU32(data + pos + 4u + r3, be) != r3) return false;

    const uint8_t* tp = data + pos + 4u;
    mesh.tris.reserve(static_cast<size_t>(nTris));
    for (int32_t t = 0; t < nTris; ++t) {
        const int32_t i0 = RdI32(tp + static_cast<size_t>(t) * 12u + 0u, be);
        const int32_t i1 = RdI32(tp + static_cast<size_t>(t) * 12u + 4u, be);
        const int32_t i2 = RdI32(tp + static_cast<size_t>(t) * 12u + 8u, be);
        if (i0 < 1 || i0 > nVerts || i1 < 1 || i1 > nVerts || i2 < 1 || i2 > nVerts) return false;
        if (i0 != i1 && i1 != i2 && i0 != i2) {
            mesh.tris.push_back({
                static_cast<uint32_t>(i0 - 1),
                static_cast<uint32_t>(i1 - 1),
                static_cast<uint32_t>(i2 - 1),
                {}
            });
        }
    }
    if (mesh.tris.empty()) return false;
    mesh.formatName = be ? L"NASA Cart3D Binary BE (.tri)" : L"NASA Cart3D Binary LE (.tri)";
    return true;
}

inline bool TryParseAsciiCart3d(std::string_view sv, LoadedMesh3D& mesh) {
    sv = ReaderText::StripUtf8Bom(sv);
    if (sv.find('\0') != std::string_view::npos) return false;

    size_t pos = 0;
    std::string_view hdrLine;
    while (ReaderText::NextLineSv(sv, pos, hdrLine)) {
        const size_t hp = hdrLine.find('#');
        if (hp != std::string_view::npos) hdrLine = hdrLine.substr(0, hp);
        hdrLine = ReaderText::TrimSv(hdrLine);
        if (!hdrLine.empty()) break;
    }
    if (hdrLine.empty()) return false;

    // Header must have 2 or 3 integers: nVerts nTris [nScalars]
    std::string_view rem = hdrLine, tNv, tNt, tNs, tExtra;
    int nVerts = 0, nTris = 0, nScalars = 0;
    if (!ReaderText::NextTokenSv(rem, tNv) || !ReaderText::ParseIntSv(tNv, nVerts)) return false;
    if (!ReaderText::NextTokenSv(rem, tNt) || !ReaderText::ParseIntSv(tNt, nTris))  return false;
    if (ReaderText::NextTokenSv(rem, tNs)) {
        if (!ReaderText::ParseIntSv(tNs, nScalars) || nScalars < 0) return false;
        if (ReaderText::NextTokenSv(rem, tExtra)) return false;
    }
    if (nVerts < 3 || nVerts > 2000000 || nTris < 1 || nTris > 4000000) return false;

    std::vector<std::string_view> toks;
    const uint64_t needToks = static_cast<uint64_t>(nVerts) * 3ULL + static_cast<uint64_t>(nTris) * 3ULL;
    toks.reserve(static_cast<size_t>(std::min<uint64_t>(needToks, 6000000ULL)));

    std::string_view line;
    while (toks.size() < needToks && ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        std::string_view lr = line, tk;
        while (toks.size() < needToks && ReaderText::NextTokenSv(lr, tk)) {
            toks.push_back(tk);
        }
    }
    if (toks.size() < needToks) return false;

    size_t idx = 0;
    mesh.vertices.reserve(static_cast<size_t>(nVerts));
    for (int i = 0; i < nVerts; ++i) {
        float x = 0, y = 0, z = 0;
        if (!ReaderText::ParseFloatSv(toks[idx++], x) ||
            !ReaderText::ParseFloatSv(toks[idx++], y) ||
            !ReaderText::ParseFloatSv(toks[idx++], z)) {
            return false;
        }
        mesh.vertices.push_back({ x, z, -y });
    }

    mesh.tris.reserve(static_cast<size_t>(nTris));
    for (int t = 0; t < nTris; ++t) {
        int i0 = 0, i1 = 0, i2 = 0;
        if (!ReaderText::ParseIntSv(toks[idx++], i0) ||
            !ReaderText::ParseIntSv(toks[idx++], i1) ||
            !ReaderText::ParseIntSv(toks[idx++], i2)) {
            return false;
        }
        if (i0 < 1 || i0 > nVerts || i1 < 1 || i1 > nVerts || i2 < 1 || i2 > nVerts) return false;
        if (i0 != i1 && i1 != i2 && i0 != i2) {
            mesh.tris.push_back({
                static_cast<uint32_t>(i0 - 1),
                static_cast<uint32_t>(i1 - 1),
                static_cast<uint32_t>(i2 - 1),
                {}
            });
        }
    }

    if (mesh.tris.empty()) return false;
    mesh.formatName = L"NASA Cart3D Surface Mesh (.tri)";
    return true;
}

} // namespace DetailCart3dTri

inline bool ParseCart3dTriMesh(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 16 || size > 64u * 1024u * 1024u) return false;

    if (DetailCart3dTri::TryParseBinaryCart3d(data, size, false, mesh)) return true;
    mesh = LoadedMesh3D{};
    if (DetailCart3dTri::TryParseBinaryCart3d(data, size, true, mesh)) return true;
    mesh = LoadedMesh3D{};
    return DetailCart3dTri::TryParseAsciiCart3d(std::string_view(reinterpret_cast<const char*>(data), size), mesh);
}

} // namespace Formats
