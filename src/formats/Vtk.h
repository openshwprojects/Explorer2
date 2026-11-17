#pragma once
#include "MeshBuild3D.h"
// Vtk.h — Visualization Toolkit Legacy 3D Format (.vtk v1.0 – v5.1) Parser
// Supports ASCII & Big-Endian BINARY encodings across POLYDATA, UNSTRUCTURED_GRID,
// STRUCTURED_GRID, STRUCTURED_POINTS, and RECTILINEAR_GRID, VTK 5.x OFFSETS/CONNECTIVITY,
// and 3D volumetric FEM cell boundary-face extraction (interior shared faces culled).

namespace Formats {

namespace DetailVtk {

enum class VtkScalarKind : uint8_t {
    Invalid = 0,
    F32,
    F64,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64
};

inline std::string ToLowerVtk(std::string_view sv) { return ::Explorer2::PathUtils::ToLowerAscii(sv); }

inline VtkScalarKind ParseVtkScalarKind(std::string_view raw) {
    std::string t = ToLowerVtk(raw);
    if (t == "float" || t == "vtktypefloat32") return VtkScalarKind::F32;
    if (t == "double" || t == "vtktypefloat64") return VtkScalarKind::F64;
    if (t == "int" || t == "vtktypeint32" || t == "long") return VtkScalarKind::I32;
    if (t == "unsigned_int" || t == "vtktypeuint32" || t == "unsigned_long") return VtkScalarKind::U32;
    if (t == "vtktypeint64" || t == "long_long") return VtkScalarKind::I64;
    if (t == "vtktypeuint64" || t == "unsigned_long_long" || t == "vtkidtype") return VtkScalarKind::U64;
    if (t == "short" || t == "vtktypeint16") return VtkScalarKind::I16;
    if (t == "unsigned_short" || t == "vtktypeuint16") return VtkScalarKind::U16;
    if (t == "char" || t == "signed_char" || t == "vtktypeint8") return VtkScalarKind::I8;
    if (t == "unsigned_char" || t == "uchar" || t == "vtktypeuint8" || t == "bit") return VtkScalarKind::U8;
    return VtkScalarKind::Invalid;
}

inline size_t VtkScalarByteSize(VtkScalarKind k) {
    switch (k) {
    case VtkScalarKind::I8:
    case VtkScalarKind::U8:  return 1;
    case VtkScalarKind::I16:
    case VtkScalarKind::U16: return 2;
    case VtkScalarKind::I32:
    case VtkScalarKind::U32:
    case VtkScalarKind::F32: return 4;
    case VtkScalarKind::I64:
    case VtkScalarKind::U64:
    case VtkScalarKind::F64: return 8;
    default:                 return 0;
    }
}

inline uint16_t RdBE16Vtk(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

inline uint32_t RdBE32Vtk(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32BE(p); }

inline uint64_t RdBE64Vtk(const uint8_t* p) {
    return (static_cast<uint64_t>(RdBE32Vtk(p)) << 32) | static_cast<uint64_t>(RdBE32Vtk(p + 4));
}

inline double RdBinDoubleVtk(const uint8_t* p, VtkScalarKind k) {
    switch (k) {
    case VtkScalarKind::F32: {
        uint32_t u = RdBE32Vtk(p);
        float f = 0.0f; std::memcpy(&f, &u, 4);
        return static_cast<double>(f);
    }
    case VtkScalarKind::F64: {
        uint64_t u = RdBE64Vtk(p);
        double d = 0.0; std::memcpy(&d, &u, 8);
        return d;
    }
    case VtkScalarKind::I8: {
        int8_t v = 0; std::memcpy(&v, p, 1); return static_cast<double>(v);
    }
    case VtkScalarKind::U8:
        return static_cast<double>(p[0]);
    case VtkScalarKind::I16:
        return static_cast<double>(static_cast<int16_t>(RdBE16Vtk(p)));
    case VtkScalarKind::U16:
        return static_cast<double>(RdBE16Vtk(p));
    case VtkScalarKind::I32:
        return static_cast<double>(static_cast<int32_t>(RdBE32Vtk(p)));
    case VtkScalarKind::U32:
        return static_cast<double>(RdBE32Vtk(p));
    case VtkScalarKind::I64:
        return static_cast<double>(static_cast<int64_t>(RdBE64Vtk(p)));
    case VtkScalarKind::U64:
        return static_cast<double>(RdBE64Vtk(p));
    default:
        return 0.0;
    }
}

inline int64_t RdBinIntVtk(const uint8_t* p, VtkScalarKind k) {
    switch (k) {
    case VtkScalarKind::I8: { int8_t v = 0; std::memcpy(&v, p, 1); return v; }
    case VtkScalarKind::U8:  return p[0];
    case VtkScalarKind::I16: return static_cast<int16_t>(RdBE16Vtk(p));
    case VtkScalarKind::U16: return RdBE16Vtk(p);
    case VtkScalarKind::I32: return static_cast<int32_t>(RdBE32Vtk(p));
    case VtkScalarKind::U32: return RdBE32Vtk(p);
    case VtkScalarKind::I64: return static_cast<int64_t>(RdBE64Vtk(p));
    case VtkScalarKind::U64: return static_cast<int64_t>(RdBE64Vtk(p));
    case VtkScalarKind::F32:
    case VtkScalarKind::F64: return static_cast<int64_t>(std::llround(RdBinDoubleVtk(p, k)));
    default:                 return 0;
    }
}

inline void SkipWsVtk(const char*& cur, const char* end) {
    while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) ++cur;
}

inline bool NextTokenVtk(const char*& cur, const char* end, std::string_view& out) {
    SkipWsVtk(cur, end);
    if (cur >= end) return false;
    const char* s = cur;
    while (cur < end && *cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') ++cur;
    out = std::string_view(s, static_cast<size_t>(cur - s));
    return true;
}

inline bool NextDoubleAsciiVtk(const char*& cur, const char* end, double& out) {
    std::string_view tok;
    if (!NextTokenVtk(cur, end, tok) || tok.size() >= 64) return false;
    char buf[64];
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* next = nullptr;
    out = std::strtod(buf, &next);
    return next != buf;
}

inline bool NextIntAsciiVtk(const char*& cur, const char* end, int64_t& out) {
    std::string_view tok;
    if (!NextTokenVtk(cur, end, tok) || tok.size() >= 64) return false;
    auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), out);
    if (ec == std::errc{} && ptr == tok.data() + tok.size()) return true;
    char buf[64];
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* next = nullptr;
    out = std::strtoll(buf, &next, 10);
    return next != buf;
}

inline bool ReadLineVtk(const char*& cur, const char* end, std::string_view& outLine) {
    if (cur >= end) return false;
    const char* nl = static_cast<const char*>(std::memchr(cur, '\n', static_cast<size_t>(end - cur)));
    const char* lineEnd = nl ? nl : end;
    size_t len = static_cast<size_t>(lineEnd - cur);
    if (len > 0 && cur[len - 1] == '\r') --len;
    outLine = std::string_view(cur, len);
    cur = nl ? (nl + 1) : end;
    return true;
}

inline void SplitLineTokensVtk(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    const char* c = line.data();
    const char* e = c + line.size();
    std::string_view t;
    while (NextTokenVtk(c, e, t)) out.push_back(t);
}

inline bool ReadScalarArrayVtk(
    const char*& cur, const char* end, bool isBinary,
    size_t count, VtkScalarKind kind, std::vector<double>& out)
{
    if (kind == VtkScalarKind::Invalid) return false;
    if (count > 50000000u) return false;
    out.resize(count);
    if (isBinary) {
        size_t bsz = VtkScalarByteSize(kind);
        if (bsz == 0 || static_cast<uint64_t>(end - cur) < static_cast<uint64_t>(count) * bsz) return false;
        const uint8_t* bp = reinterpret_cast<const uint8_t*>(cur);
        for (size_t i = 0; i < count; ++i) {
            out[i] = RdBinDoubleVtk(bp + i * bsz, kind);
        }
        cur += count * bsz;
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (!NextDoubleAsciiVtk(cur, end, out[i])) return false;
        }
    }
    return true;
}

inline bool ReadIntArrayVtk(
    const char*& cur, const char* end, bool isBinary,
    size_t count, VtkScalarKind kind, std::vector<int64_t>& out)
{
    if (kind == VtkScalarKind::Invalid || count > 50000000u) return false;
    out.resize(count);
    if (isBinary) {
        size_t bsz = VtkScalarByteSize(kind);
        if (bsz == 0 || static_cast<uint64_t>(end - cur) < static_cast<uint64_t>(count) * bsz) return false;
        const uint8_t* bp = reinterpret_cast<const uint8_t*>(cur);
        for (size_t i = 0; i < count; ++i) {
            out[i] = RdBinIntVtk(bp + i * bsz, kind);
        }
        cur += count * bsz;
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (!NextIntAsciiVtk(cur, end, out[i])) return false;
        }
    }
    return true;
}

struct VtkCellList {
    std::vector<size_t> offsets; // size = numCells + 1
    std::vector<uint32_t> conn;
};

// Reads either VTK 1.0–4.2 legacy cell list (n i0 i1 ... per cell) OR VTK 5.0/5.1 OFFSETS + CONNECTIVITY!
inline bool ReadCellArrayVtk(
    const char*& cur, const char* end, bool isBinary,
    size_t hdrFirst, size_t hdrSecond, VtkCellList& out)
{
    out.offsets.clear();
    out.conn.clear();

    // Peek ahead (skipping whitespace) to see if VTK 5.x "OFFSETS" keyword follows
    const char* peek = cur;
    SkipWsVtk(peek, end);
    if (static_cast<size_t>(end - peek) >= 7 &&
        ToLowerVtk(std::string_view(peek, 7)) == "offsets" &&
        (peek + 7 == end || peek[7] == ' ' || peek[7] == '\t' || peek[7] == '\r' || peek[7] == '\n'))
    {
        cur = peek;
        std::string_view offLine;
        if (!ReadLineVtk(cur, end, offLine)) return false;
        std::vector<std::string_view> toks;
        SplitLineTokensVtk(offLine, toks);
        if (toks.size() < 2) return false;

        // Support both "OFFSETS <type>" (count = hdrFirst) and "OFFSETS <count> <type>"
        size_t numOffsets = hdrFirst;
        VtkScalarKind offKind = ParseVtkScalarKind(toks[1]);
        if (offKind == VtkScalarKind::Invalid && toks.size() >= 3) {
            uint64_t k = 0;
            auto [p1, ec1] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), k);
            if (ec1 != std::errc{}) return false;
            numOffsets = static_cast<size_t>(k);
            offKind = ParseVtkScalarKind(toks[2]);
        }
        if (offKind == VtkScalarKind::Invalid || numOffsets == 0) return false;

        std::vector<int64_t> rawOff;
        if (!ReadIntArrayVtk(cur, end, isBinary, numOffsets, offKind, rawOff)) return false;

        SkipWsVtk(cur, end);
        std::string_view connLine;
        if (!ReadLineVtk(cur, end, connLine)) return false;
        SplitLineTokensVtk(connLine, toks);
        if (toks.empty() || ToLowerVtk(toks[0]) != "connectivity" || toks.size() < 2) return false;

        size_t numConn = hdrSecond;
        VtkScalarKind connKind = ParseVtkScalarKind(toks[1]);
        if (connKind == VtkScalarKind::Invalid && toks.size() >= 3) {
            uint64_t m = 0;
            auto [p2, ec2] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), m);
            if (ec2 != std::errc{}) return false;
            numConn = static_cast<size_t>(m);
            connKind = ParseVtkScalarKind(toks[2]);
        }
        if (connKind == VtkScalarKind::Invalid) return false;

        std::vector<int64_t> rawConn;
        if (!ReadIntArrayVtk(cur, end, isBinary, numConn, connKind, rawConn)) return false;

        out.offsets.reserve(rawOff.size());
        for (int64_t o : rawOff) {
            if (o < 0 || static_cast<size_t>(o) > rawConn.size()) return false;
            if (!out.offsets.empty() && static_cast<size_t>(o) < out.offsets.back()) return false;
            out.offsets.push_back(static_cast<size_t>(o));
        }
        out.conn.reserve(rawConn.size());
        for (int64_t id : rawConn) {
            if (id < 0) return false;
            out.conn.push_back(static_cast<uint32_t>(id));
        }
        return true;
    }

    // Legacy VTK 1.0–4.2 format: hdrFirst = numCells, hdrSecond = total integer entries
    const size_t numCells = hdrFirst;
    out.offsets.reserve(std::min<size_t>(numCells + 1, 2000000));
    out.conn.reserve(std::min<size_t>(hdrSecond, 8000000));
    out.offsets.push_back(0);

    if (isBinary) {
        for (size_t c = 0; c < numCells; ++c) {
            if (static_cast<size_t>(end - cur) < 4) return false;
            uint32_t n = RdBE32Vtk(reinterpret_cast<const uint8_t*>(cur));
            cur += 4;
            if (n > 1000000u || static_cast<uint64_t>(end - cur) < static_cast<uint64_t>(n) * 4ULL) return false;
            for (uint32_t k = 0; k < n; ++k) {
                out.conn.push_back(RdBE32Vtk(reinterpret_cast<const uint8_t*>(cur)));
                cur += 4;
            }
            out.offsets.push_back(out.conn.size());
        }
    } else {
        for (size_t c = 0; c < numCells; ++c) {
            int64_t n = 0;
            if (!NextIntAsciiVtk(cur, end, n) || n < 0 || n > 1000000) return false;
            for (int64_t k = 0; k < n; ++k) {
                int64_t idx = 0;
                if (!NextIntAsciiVtk(cur, end, idx) || idx < 0) return false;
                out.conn.push_back(static_cast<uint32_t>(idx));
            }
            out.offsets.push_back(out.conn.size());
        }
    }
    return true;
}

struct SortedFaceKey {
    uint32_t v[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    uint8_t n = 0;
    bool operator==(const SortedFaceKey& o) const noexcept {
        return n == o.n && v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2] && v[3] == o.v[3];
    }
};

struct SortedFaceKeyHash {
    size_t operator()(const SortedFaceKey& k) const noexcept {
        uint64_t h = 1469598103934665603ULL;
        for (uint8_t i = 0; i < k.n; ++i) {
            h ^= static_cast<uint64_t>(k.v[i]);
            h *= 1099511628211ULL;
        }
        return static_cast<size_t>(h ^ (h >> 32));
    }
};

struct BoundaryFaceRec {
    uint32_t orig[4] = {};
    uint8_t n = 0;
    uint32_t volCount = 0;
    bool explicit2D = false;
};

inline void RecordFace3DVtk(
    std::unordered_map<SortedFaceKey, BoundaryFaceRec, SortedFaceKeyHash>& map,
    const uint32_t* verts, uint8_t n, bool isVolumetric)
{
    SortedFaceKey key;
    key.n = n;
    for (uint8_t i = 0; i < n; ++i) key.v[i] = verts[i];
    std::sort(key.v, key.v + n);
    // Ignore degenerate faces with duplicate corner indices
    for (uint8_t i = 1; i < n; ++i) {
        if (key.v[i] == key.v[i - 1]) return;
    }

    auto [it, inserted] = map.try_emplace(key);
    if (inserted) {
        it->second.n = n;
        for (uint8_t i = 0; i < n; ++i) it->second.orig[i] = verts[i];
    }
    if (isVolumetric) {
        ++it->second.volCount;
    } else {
        it->second.explicit2D = true;
        if (it->second.volCount == 0) {
            for (uint8_t i = 0; i < n; ++i) it->second.orig[i] = verts[i];
        }
    }
}

inline void EmitPointSplatsVtk(LoadedMesh3D& mesh, const std::vector<Vec3f>& baseVerts, const std::vector<uint32_t>& ptIndices) {
    if (ptIndices.empty() || baseVerts.empty()) return;
    constexpr size_t kMaxSplats = 25000;
    const size_t step = std::max<size_t>(1, ptIndices.size() / kMaxSplats);

    Vec3f bmin = baseVerts[ptIndices[0]], bmax = bmin;
    for (uint32_t idx : ptIndices) {
        if (idx >= baseVerts.size()) continue;
        const Vec3f& p = baseVerts[idx];
        bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
        bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
    }
    float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z });
    float r = std::max(0.015f, (span > 1e-5f) ? (span * 0.018f) : 0.02f);

    for (size_t i = 0; i < ptIndices.size() && mesh.tris.size() < kMaxSplats * 4; i += step) {
        uint32_t vi = ptIndices[i];
        if (vi >= baseVerts.size()) continue;
        const Vec3f& p = baseVerts[vi];
        uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ p.x,     p.y + r, p.z     });
        mesh.vertices.push_back({ p.x + r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x - r, p.y - r, p.z + r });
        mesh.vertices.push_back({ p.x,     p.y - r, p.z - r });
        mesh.tris.push_back({ b + 0, b + 1, b + 2, {} });
        mesh.tris.push_back({ b + 0, b + 2, b + 3, {} });
        mesh.tris.push_back({ b + 0, b + 3, b + 1, {} });
        mesh.tris.push_back({ b + 1, b + 3, b + 2, {} });
    }
}

// Emits exterior boundary quads for an nx * ny * nz structured grid (either explicit 3D points or coordinate arrays).
template <typename PosFn>
inline void EmitStructuredBoundaryVtk(LoadedMesh3D& mesh, size_t nx, size_t ny, size_t nz, PosFn&& getPos) {
    auto emitSheet = [&](size_t nu, size_t nv, auto&& mapIJ, bool flip) {
        if (nu < 2 || nv < 2) return;
        const size_t maxDim = 512;
        const size_t su = std::max<size_t>(1, (nu - 1 + maxDim - 1) / maxDim);
        const size_t sv = std::max<size_t>(1, (nv - 1 + maxDim - 1) / maxDim);
        std::vector<size_t> uList, vList;
        for (size_t u = 0; u < nu; u += su) uList.push_back(u);
        if (uList.back() != nu - 1) uList.push_back(nu - 1);
        for (size_t v = 0; v < nv; v += sv) vList.push_back(v);
        if (vList.back() != nv - 1) vList.push_back(nv - 1);

        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const size_t cols = uList.size();
        const size_t rows = vList.size();
        for (size_t rv = 0; rv < rows; ++rv) {
            for (size_t cu = 0; cu < cols; ++cu) {
                mesh.vertices.push_back(mapIJ(uList[cu], vList[rv]));
            }
        }
        for (size_t rv = 0; rv + 1 < rows; ++rv) {
            for (size_t cu = 0; cu + 1 < cols; ++cu) {
                uint32_t i0 = base + static_cast<uint32_t>(rv * cols + cu);
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + static_cast<uint32_t>(cols) + 1;
                uint32_t i3 = i0 + static_cast<uint32_t>(cols);
                if (!flip) {
                    mesh.tris.push_back({ i0, i1, i2, {} });
                    mesh.tris.push_back({ i0, i2, i3, {} });
                } else {
                    mesh.tris.push_back({ i0, i2, i1, {} });
                    mesh.tris.push_back({ i0, i3, i2, {} });
                }
            }
        }
    };

    if (nz == 1 && nx >= 2 && ny >= 2) {
        emitSheet(nx, ny, [&](size_t i, size_t j) { return getPos(i, j, 0); }, false);
    } else if (ny == 1 && nx >= 2 && nz >= 2) {
        emitSheet(nx, nz, [&](size_t i, size_t k) { return getPos(i, 0, k); }, false);
    } else if (nx == 1 && ny >= 2 && nz >= 2) {
        emitSheet(ny, nz, [&](size_t j, size_t k) { return getPos(0, j, k); }, false);
    } else if (nx >= 2 && ny >= 2 && nz >= 2) {
        emitSheet(nx, ny, [&](size_t i, size_t j) { return getPos(i, j, 0); }, true);
        emitSheet(nx, ny, [&](size_t i, size_t j) { return getPos(i, j, nz - 1); }, false);
        emitSheet(nx, nz, [&](size_t i, size_t k) { return getPos(i, 0, k); }, false);
        emitSheet(nx, nz, [&](size_t i, size_t k) { return getPos(i, ny - 1, k); }, true);
        emitSheet(ny, nz, [&](size_t j, size_t k) { return getPos(0, j, k); }, true);
        emitSheet(ny, nz, [&](size_t j, size_t k) { return getPos(nx - 1, j, k); }, false);
    }
}

} // namespace DetailVtk

inline bool ParseVtkMesh(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    if (!data || size < 24) return false;
    const char* cur = reinterpret_cast<const char*>(data);
    const char* const end = cur + size;

    // 1. Validate "# vtk DataFile Version <x.y>" header
    std::string_view line1, titleLine, fmtLine;
    if (size >= 27 && static_cast<uint8_t>(cur[0]) == 0xEF && static_cast<uint8_t>(cur[1]) == 0xBB && static_cast<uint8_t>(cur[2]) == 0xBF) {
        cur += 3;
    }
    while (cur < end) {
        if (!DetailVtk::ReadLineVtk(cur, end, line1)) return false;
        if (!line1.empty()) break;
    }
    std::string hdrLower = DetailVtk::ToLowerVtk(line1);
    if (hdrLower.find("# vtk datafile version") == std::string::npos) {
        return false;
    }
    if (!DetailVtk::ReadLineVtk(cur, end, titleLine)) return false;

    while (cur < end) {
        if (!DetailVtk::ReadLineVtk(cur, end, fmtLine)) return false;
        std::vector<std::string_view> ft;
        DetailVtk::SplitLineTokensVtk(fmtLine, ft);
        if (!ft.empty()) {
            fmtLine = ft[0];
            break;
        }
    }
    std::string encLower = DetailVtk::ToLowerVtk(fmtLine);
    bool isBinary = false;
    if (encLower == "binary") isBinary = true;
    else if (encLower == "ascii") isBinary = false;
    else return false;

    std::string datasetType;
    std::vector<std::string_view> toks;
    while (cur < end) {
        std::string_view ln;
        if (!DetailVtk::ReadLineVtk(cur, end, ln)) return false;
        DetailVtk::SplitLineTokensVtk(ln, toks);
        if (toks.empty()) continue;
        if (DetailVtk::ToLowerVtk(toks[0]) == "dataset" && toks.size() >= 2) {
            datasetType = DetailVtk::ToLowerVtk(toks[1]);
            break;
        }
    }
    if (datasetType.empty()) return false;

    mesh.formatName = isBinary ? L"Visualization Toolkit 3D (.vtk Binary)" : L"Visualization Toolkit 3D (.vtk)";

    std::vector<Vec3f> pts;
    DetailVtk::VtkCellList polyCells, stripCells, lineCells, vertCells, ugridCells;
    std::vector<int64_t> cellTypes;
    size_t dimX = 0, dimY = 0, dimZ = 0;
    bool hasDims = false;
    double origin[3] = { 0.0, 0.0, 0.0 };
    double spacing[3] = { 1.0, 1.0, 1.0 };
    std::vector<double> xCoords, yCoords, zCoords;
    std::vector<double> pointScalars;
    bool inPointData = false;

    while (cur < end) {
        DetailVtk::SkipWsVtk(cur, end);
        if (cur >= end) break;
        std::string_view ln;
        if (!DetailVtk::ReadLineVtk(cur, end, ln)) break;
        DetailVtk::SplitLineTokensVtk(ln, toks);
        if (toks.empty()) continue;

        std::string kw = DetailVtk::ToLowerVtk(toks[0]);

        if (kw == "points" && toks.size() >= 3) {
            uint64_t nPts = 0;
            auto [p, ec] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), nPts);
            if (ec != std::errc{} || nPts > 20000000ULL) return false;
            DetailVtk::VtkScalarKind pk = DetailVtk::ParseVtkScalarKind(toks[2]);
            std::vector<double> coords;
            if (!DetailVtk::ReadScalarArrayVtk(cur, end, isBinary, static_cast<size_t>(nPts) * 3, pk, coords)) return false;
            pts.resize(static_cast<size_t>(nPts));
            for (size_t i = 0; i < static_cast<size_t>(nPts); ++i) {
                double x = coords[i * 3 + 0], y = coords[i * 3 + 1], z = coords[i * 3 + 2];
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                pts[i] = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
            }
        } else if ((kw == "polygons" || kw == "triangle_strips" || kw == "lines" || kw == "vertices" || kw == "cells") && toks.size() >= 3) {
            uint64_t a = 0, b = 0;
            auto [p1, ec1] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), a);
            auto [p2, ec2] = std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), b);
            if (ec1 != std::errc{} || ec2 != std::errc{}) return false;
            DetailVtk::VtkCellList parsed;
            if (!DetailVtk::ReadCellArrayVtk(cur, end, isBinary, static_cast<size_t>(a), static_cast<size_t>(b), parsed)) {
                return false;
            }
            if (kw == "polygons") polyCells = std::move(parsed);
            else if (kw == "triangle_strips") stripCells = std::move(parsed);
            else if (kw == "lines") lineCells = std::move(parsed);
            else if (kw == "vertices") vertCells = std::move(parsed);
            else if (kw == "cells") ugridCells = std::move(parsed);
        } else if (kw == "cell_types" && toks.size() >= 2) {
            uint64_t nTypes = 0;
            auto [p, ec] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), nTypes);
            if (ec != std::errc{}) return false;
            if (!DetailVtk::ReadIntArrayVtk(cur, end, isBinary, static_cast<size_t>(nTypes), DetailVtk::VtkScalarKind::I32, cellTypes)) {
                return false;
            }
        } else if (kw == "dimensions" && toks.size() >= 4) {
            uint64_t dx = 0, dy = 0, dz = 0;
            auto [p1, ec1] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), dx);
            auto [p2, ec2] = std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), dy);
            auto [p3, ec3] = std::from_chars(toks[3].data(), toks[3].data() + toks[3].size(), dz);
            if (ec1 != std::errc{} || ec2 != std::errc{} || ec3 != std::errc{} || dx == 0 || dy == 0 || dz == 0) return false;
            dimX = static_cast<size_t>(dx);
            dimY = static_cast<size_t>(dy);
            dimZ = static_cast<size_t>(dz);
            hasDims = true;
        } else if (kw == "origin" && toks.size() >= 4) {
            for (int i = 0; i < 3; ++i) {
                const char* tc = toks[i + 1].data();
                const char* te = tc + toks[i + 1].size();
                DetailVtk::NextDoubleAsciiVtk(tc, te, origin[i]);
            }
        } else if ((kw == "spacing" || kw == "aspect_ratio") && toks.size() >= 4) {
            for (int i = 0; i < 3; ++i) {
                const char* tc = toks[i + 1].data();
                const char* te = tc + toks[i + 1].size();
                DetailVtk::NextDoubleAsciiVtk(tc, te, spacing[i]);
            }
        } else if ((kw == "x_coordinates" || kw == "y_coordinates" || kw == "z_coordinates") && toks.size() >= 3) {
            uint64_t nC = 0;
            auto [p, ec] = std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), nC);
            if (ec != std::errc{}) return false;
            DetailVtk::VtkScalarKind ck = DetailVtk::ParseVtkScalarKind(toks[2]);
            std::vector<double> arr;
            if (!DetailVtk::ReadScalarArrayVtk(cur, end, isBinary, static_cast<size_t>(nC), ck, arr)) return false;
            if (kw[0] == 'x') xCoords = std::move(arr);
            else if (kw[0] == 'y') yCoords = std::move(arr);
            else zCoords = std::move(arr);
        } else if (kw == "metadata") {
            // VTK 5.x METADATA block ends with an empty line!
            while (cur < end) {
                std::string_view mln;
                if (!DetailVtk::ReadLineVtk(cur, end, mln)) break;
                bool allWs = true;
                for (char ch : mln) {
                    if (ch != ' ' && ch != '\t' && ch != '\r') { allWs = false; break; }
                }
                if (allWs) break;
            }
        } else if (kw == "point_data") {
            inPointData = true;
        } else if (kw == "cell_data") {
            inPointData = false;
        } else if (inPointData && kw == "scalars" && toks.size() >= 3 && pointScalars.empty()) {
            DetailVtk::VtkScalarKind sk = DetailVtk::ParseVtkScalarKind(toks[2]);
            size_t numComp = 1;
            if (toks.size() >= 4) {
                uint64_t nc = 1;
                auto [p, ec] = std::from_chars(toks[3].data(), toks[3].data() + toks[3].size(), nc);
                if (ec == std::errc{} && nc >= 1 && nc <= 16) numComp = static_cast<size_t>(nc);
            }
            // Check optional LOOKUP_TABLE line
            const char* saveCur = cur;
            DetailVtk::SkipWsVtk(cur, end);
            std::string_view ltLine;
            if (DetailVtk::ReadLineVtk(cur, end, ltLine)) {
                std::vector<std::string_view> ltToks;
                DetailVtk::SplitLineTokensVtk(ltLine, ltToks);
                if (ltToks.empty() || DetailVtk::ToLowerVtk(ltToks[0]) != "lookup_table") {
                    cur = saveCur;
                }
            }
            size_t nPtsExpected = !pts.empty() ? pts.size() : (hasDims ? (dimX * dimY * dimZ) : 0);
            if (nPtsExpected > 0 && sk != DetailVtk::VtkScalarKind::Invalid) {
                std::vector<double> rawSc;
                if (DetailVtk::ReadScalarArrayVtk(cur, end, isBinary, nPtsExpected * numComp, sk, rawSc)) {
                    pointScalars.resize(nPtsExpected);
                    for (size_t i = 0; i < nPtsExpected; ++i) {
                        pointScalars[i] = std::isfinite(rawSc[i * numComp]) ? rawSc[i * numComp] : 0.0;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        } else if (kw == "field" && toks.size() >= 3) {
            // Skip FIELD <name> <numArrays> so following geometry sections (if any) aren't confused
            uint64_t numArr = 0;
            auto [p, ec] = std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), numArr);
            if (ec == std::errc{}) {
                for (uint64_t a = 0; a < numArr && cur < end; ++a) {
                    DetailVtk::SkipWsVtk(cur, end);
                    std::string_view fln;
                    if (!DetailVtk::ReadLineVtk(cur, end, fln)) break;
                    std::vector<std::string_view> ft;
                    DetailVtk::SplitLineTokensVtk(fln, ft);
                    if (ft.size() >= 4 && DetailVtk::ToLowerVtk(ft[0]) == "metadata") {
                        while (cur < end) {
                            std::string_view mln;
                            if (!DetailVtk::ReadLineVtk(cur, end, mln)) break;
                            if (mln.empty()) break;
                        }
                        --a;
                        continue;
                    }
                    if (ft.size() < 4) break;
                    uint64_t nc = 0, nt = 0;
                    std::from_chars(ft[1].data(), ft[1].data() + ft[1].size(), nc);
                    std::from_chars(ft[2].data(), ft[2].data() + ft[2].size(), nt);
                    DetailVtk::VtkScalarKind fk = DetailVtk::ParseVtkScalarKind(ft[3]);
                    std::vector<double> skipArr;
                    if (!DetailVtk::ReadScalarArrayVtk(cur, end, isBinary, static_cast<size_t>(nc * nt), fk, skipArr)) break;
                }
            }
        }
    }

    // Assemble geometry strictly according to DATASET topology (NEVER connect arbitrary point triplets!)
    if (datasetType == "polydata") {
        if (pts.empty()) return false;
        mesh.vertices = pts;
        const size_t nv = mesh.vertices.size();

        if (polyCells.offsets.size() >= 2) {
            for (size_t c = 0; c + 1 < polyCells.offsets.size(); ++c) {
                size_t b = polyCells.offsets[c], e = polyCells.offsets[c + 1];
                if (e - b >= 3) {
                    uint32_t i0 = polyCells.conn[b];
                    if (i0 >= nv) continue;
                    for (size_t k = b + 1; k + 1 < e; ++k) {
                        uint32_t i1 = polyCells.conn[k], i2 = polyCells.conn[k + 1];
                        if (i1 < nv && i2 < nv) mesh.tris.push_back({ i0, i1, i2, {} });
                    }
                }
            }
        }
        if (stripCells.offsets.size() >= 2) {
            for (size_t c = 0; c + 1 < stripCells.offsets.size(); ++c) {
                size_t b = stripCells.offsets[c], e = stripCells.offsets[c + 1];
                for (size_t k = 0; b + k + 2 < e; ++k) {
                    uint32_t v0 = stripCells.conn[b + k];
                    uint32_t v1 = stripCells.conn[b + k + 1];
                    uint32_t v2 = stripCells.conn[b + k + 2];
                    if (v0 < nv && v1 < nv && v2 < nv && v0 != v1 && v1 != v2 && v2 != v0) {
                        if ((k & 1u) == 0u) mesh.tris.push_back({ v0, v1, v2, {} });
                        else                mesh.tris.push_back({ v1, v0, v2, {} });
                    }
                }
            }
        }

        if (mesh.tris.empty() && lineCells.offsets.size() >= 2) {
            mesh.vertices.clear();
            Vec3f bmin = pts[0], bmax = pts[0];
            for (const auto& p : pts) {
                bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
            }
            float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
            float rad = span * 0.008f;
            for (size_t c = 0; c + 1 < lineCells.offsets.size(); ++c) {
                size_t b = lineCells.offsets[c], e = lineCells.offsets[c + 1];
                for (size_t k = b; k + 1 < e; ++k) {
                    uint32_t a = lineCells.conn[k], d = lineCells.conn[k + 1];
                    if (a < pts.size() && d < pts.size()) {
                        MeshBuild3D::EmitEdgeStrut(mesh, pts[a], pts[d], rad);
                    }
                }
            }
        } else if (mesh.tris.empty() && vertCells.offsets.size() >= 2) {
            mesh.vertices.clear();
            std::vector<uint32_t> referencedVerts;
            for (size_t c = 0; c + 1 < vertCells.offsets.size(); ++c) {
                for (size_t k = vertCells.offsets[c]; k < vertCells.offsets[c + 1]; ++k) {
                    if (vertCells.conn[k] < pts.size()) referencedVerts.push_back(vertCells.conn[k]);
                }
            }
            DetailVtk::EmitPointSplatsVtk(mesh, pts, referencedVerts);
        }
    } else if (datasetType == "unstructured_grid") {
        if (pts.empty() || ugridCells.offsets.size() < 2) return false;
        const size_t numCells = ugridCells.offsets.size() - 1;
        if (cellTypes.size() < numCells) return false;

        mesh.vertices = pts;
        const size_t nv = pts.size();
        std::unordered_map<DetailVtk::SortedFaceKey, DetailVtk::BoundaryFaceRec, DetailVtk::SortedFaceKeyHash> faceMap;
        faceMap.reserve(numCells * 2);
        std::vector<std::pair<uint32_t, uint32_t>> fallbackLines;
        std::vector<uint32_t> fallbackVerts;

        for (size_t c = 0; c < numCells; ++c) {
            size_t b = ugridCells.offsets[c], e = ugridCells.offsets[c + 1];
            size_t n = e - b;
            const uint32_t* cv = ugridCells.conn.data() + b;
            bool validIdx = true;
            for (size_t k = 0; k < n; ++k) {
                if (cv[k] >= nv) { validIdx = false; break; }
            }
            if (!validIdx || n == 0) continue;

            const int64_t ct = cellTypes[c];
            switch (ct) {
            case 10: // VTK_TETRA
            case 24: // VTK_QUADRATIC_TETRA
                if (n >= 4) {
                    const uint32_t f0[3] = { cv[0], cv[2], cv[1] };
                    const uint32_t f1[3] = { cv[0], cv[1], cv[3] };
                    const uint32_t f2[3] = { cv[1], cv[2], cv[3] };
                    const uint32_t f3[3] = { cv[2], cv[0], cv[3] };
                    DetailVtk::RecordFace3DVtk(faceMap, f0, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, f1, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, f2, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, f3, 3, true);
                }
                break;
            case 11: // VTK_VOXEL (convert Z-order 0,1,3,2,4,5,7,6 -> hex order)
            case 12: // VTK_HEXAHEDRON
            case 25: // VTK_QUADRATIC_HEXAHEDRON
            case 29: // VTK_TRIQUADRATIC_HEXAHEDRON
                if (n >= 8) {
                    uint32_t h[8];
                    if (ct == 11) {
                        h[0] = cv[0]; h[1] = cv[1]; h[2] = cv[3]; h[3] = cv[2];
                        h[4] = cv[4]; h[5] = cv[5]; h[6] = cv[7]; h[7] = cv[6];
                    } else {
                        for (int k = 0; k < 8; ++k) h[k] = cv[k];
                    }
                    const uint32_t q0[4] = { h[0], h[3], h[2], h[1] };
                    const uint32_t q1[4] = { h[4], h[5], h[6], h[7] };
                    const uint32_t q2[4] = { h[0], h[1], h[5], h[4] };
                    const uint32_t q3[4] = { h[1], h[2], h[6], h[5] };
                    const uint32_t q4[4] = { h[2], h[3], h[7], h[6] };
                    const uint32_t q5[4] = { h[3], h[0], h[4], h[7] };
                    DetailVtk::RecordFace3DVtk(faceMap, q0, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q1, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q2, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q3, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q4, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q5, 4, true);
                }
                break;
            case 13: // VTK_WEDGE
            case 26: // VTK_QUADRATIC_WEDGE
                if (n >= 6) {
                    const uint32_t t0[3] = { cv[0], cv[1], cv[2] };
                    const uint32_t t1[3] = { cv[3], cv[5], cv[4] };
                    const uint32_t q0[4] = { cv[0], cv[3], cv[4], cv[1] };
                    const uint32_t q1[4] = { cv[1], cv[4], cv[5], cv[2] };
                    const uint32_t q2[4] = { cv[2], cv[5], cv[3], cv[0] };
                    DetailVtk::RecordFace3DVtk(faceMap, t0, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, t1, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q0, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q1, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, q2, 4, true);
                }
                break;
            case 14: // VTK_PYRAMID
            case 27: // VTK_QUADRATIC_PYRAMID
                if (n >= 5) {
                    const uint32_t q0[4] = { cv[0], cv[3], cv[2], cv[1] };
                    const uint32_t t0[3] = { cv[0], cv[1], cv[4] };
                    const uint32_t t1[3] = { cv[1], cv[2], cv[4] };
                    const uint32_t t2[3] = { cv[2], cv[3], cv[4] };
                    const uint32_t t3[3] = { cv[3], cv[0], cv[4] };
                    DetailVtk::RecordFace3DVtk(faceMap, q0, 4, true);
                    DetailVtk::RecordFace3DVtk(faceMap, t0, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, t1, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, t2, 3, true);
                    DetailVtk::RecordFace3DVtk(faceMap, t3, 3, true);
                }
                break;
            case 5:  // VTK_TRIANGLE
            case 22: // VTK_QUADRATIC_TRIANGLE
                if (n >= 3) DetailVtk::RecordFace3DVtk(faceMap, cv, 3, false);
                break;
            case 8:  // VTK_PIXEL (0,1,3,2)
                if (n >= 4) {
                    const uint32_t q[4] = { cv[0], cv[1], cv[3], cv[2] };
                    DetailVtk::RecordFace3DVtk(faceMap, q, 4, false);
                }
                break;
            case 9:  // VTK_QUAD
            case 23: // VTK_QUADRATIC_QUAD
            case 28: // VTK_BIQUADRATIC_QUAD
                if (n >= 4) DetailVtk::RecordFace3DVtk(faceMap, cv, 4, false);
                break;
            case 7:  // VTK_POLYGON
                if (n == 3) DetailVtk::RecordFace3DVtk(faceMap, cv, 3, false);
                else if (n == 4) DetailVtk::RecordFace3DVtk(faceMap, cv, 4, false);
                else if (n > 4) {
                    for (size_t k = 1; k + 1 < n; ++k) {
                        mesh.tris.push_back({ cv[0], cv[k], cv[k + 1], {} });
                    }
                }
                break;
            case 6:  // VTK_TRIANGLE_STRIP
                for (size_t k = 0; k + 2 < n; ++k) {
                    if ((k & 1u) == 0u) mesh.tris.push_back({ cv[k], cv[k + 1], cv[k + 2], {} });
                    else                mesh.tris.push_back({ cv[k + 1], cv[k], cv[k + 2], {} });
                }
                break;
            case 3:  // VTK_LINE
            case 4:  // VTK_POLY_LINE
            case 21: // VTK_QUADRATIC_EDGE
                if (ct == 21 && n >= 2) {
                    fallbackLines.emplace_back(cv[0], cv[1]);
                } else {
                    for (size_t k = 0; k + 1 < n; ++k) fallbackLines.emplace_back(cv[k], cv[k + 1]);
                }
                break;
            case 1:  // VTK_VERTEX
            case 2:  // VTK_POLY_VERTEX
                for (size_t k = 0; k < n; ++k) fallbackVerts.push_back(cv[k]);
                break;
            default:
                break;
            }
        }

        for (const auto& kv : faceMap) {
            const auto& f = kv.second;
            if (f.volCount == 1 || (f.volCount == 0 && f.explicit2D)) {
                if (f.n == 3) {
                    mesh.tris.push_back({ f.orig[0], f.orig[1], f.orig[2], {} });
                } else if (f.n == 4) {
                    mesh.tris.push_back({ f.orig[0], f.orig[1], f.orig[2], {} });
                    mesh.tris.push_back({ f.orig[0], f.orig[2], f.orig[3], {} });
                }
            }
        }

        if (mesh.tris.empty() && !fallbackLines.empty()) {
            mesh.vertices.clear();
            Vec3f bmin = pts[0], bmax = pts[0];
            for (const auto& p : pts) {
                bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
                bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
            }
            float span = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 1e-4f });
            float rad = span * 0.008f;
            for (const auto& ln : fallbackLines) {
                MeshBuild3D::EmitEdgeStrut(mesh, pts[ln.first], pts[ln.second], rad);
            }
        } else if (mesh.tris.empty() && !fallbackVerts.empty()) {
            mesh.vertices.clear();
            DetailVtk::EmitPointSplatsVtk(mesh, pts, fallbackVerts);
        }
    } else if (datasetType == "structured_grid") {
        if (!hasDims || pts.size() < dimX * dimY * dimZ) return false;
        DetailVtk::EmitStructuredBoundaryVtk(mesh, dimX, dimY, dimZ, [&](size_t i, size_t j, size_t k) -> Vec3f {
            return pts[(k * dimY + j) * dimX + i];
        });
    } else if (datasetType == "structured_points") {
        if (!hasDims) return false;
        const bool is2DXY = (dimZ == 1 && dimX >= 2 && dimY >= 2);
        const bool hasHeight = (pointScalars.size() >= dimX * dimY * dimZ);
        DetailVtk::EmitStructuredBoundaryVtk(mesh, dimX, dimY, dimZ, [&](size_t i, size_t j, size_t k) -> Vec3f {
            double x = origin[0] + static_cast<double>(i) * spacing[0];
            double y = origin[1] + static_cast<double>(j) * spacing[1];
            double z = origin[2] + static_cast<double>(k) * spacing[2];
            if (is2DXY && hasHeight) {
                z += pointScalars[j * dimX + i];
            }
            return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
        });
    } else if (datasetType == "rectilinear_grid") {
        if (!hasDims || xCoords.size() < dimX || yCoords.size() < dimY || zCoords.size() < dimZ) return false;
        const bool is2DXY = (dimZ == 1 && dimX >= 2 && dimY >= 2);
        const bool hasHeight = (pointScalars.size() >= dimX * dimY * dimZ);
        DetailVtk::EmitStructuredBoundaryVtk(mesh, dimX, dimY, dimZ, [&](size_t i, size_t j, size_t k) -> Vec3f {
            double x = xCoords[i];
            double y = yCoords[j];
            double z = zCoords[k];
            if (is2DXY && hasHeight) {
                z += pointScalars[j * dimX + i];
            }
            return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
        });
    } else {
        return false;
    }

    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
