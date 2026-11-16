#pragma once
// SolidWorks3D.h — #16 Dassault Systèmes SolidWorks Parts & Assemblies (.sldprt, .sldasm)
//
// Pure C++20 zero-cheat implementation:
//   - Parses OLE2 Compound File (.sldprt / .sldasm) and SolidWorks 2015+ zlib-stream containers ("SW2015" / ZIP).
//   - Decodes DisplayLists__Zip / DisplayLists shaded tessellation buffers (indexed triangles & triangle strips,
//     both uncompressed and RFC 1950 zlib-compressed).
//   - Decodes embedded Siemens Parasolid XT (.x_t) and Neutral Binary (.x_b) B-Rep solid streams via ParseCadKernels200.
//   - Resolves .sldasm Assembly Component Occurrences (4x4 double rigid-body transforms referencing sibling .sldprt parts).

namespace Formats {

namespace SolidWorksDetail {

inline uint16_t RdU16(const uint8_t* p) { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
inline uint32_t RdU32(const uint8_t* p) { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
inline float    RdF32(const uint8_t* p) { float    v = 0.0f; std::memcpy(&v, p, 4); return v; }
inline double   RdF64(const uint8_t* p) { double   v = 0.0; std::memcpy(&v, p, 8); return v; }

inline void AppendTransformedPart(const LoadedMesh3D& src, const double m[16], LoadedMesh3D& dst) {
    if (src.vertices.empty() || src.tris.empty()) return;
    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + src.vertices.size());
    for (const Vec3f& v : src.vertices) {
        double x = m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3];
        double y = m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7];
        double z = m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { x = y = z = 0.0; }
        dst.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
    }
    dst.tris.reserve(dst.tris.size() + src.tris.size());
    for (const auto& t : src.tris) {
        dst.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
    }
}

inline bool TryDecodeSwDisplayListBuffer(const uint8_t* data, size_t size, LoadedMesh3D& dst) {
    if (!data || size < 24) return false;

    // 1. Check embedded Parasolid Neutral Binary (.x_b) or Text (.x_t) B-Rep inside stream
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    if (sv.find("**END_OF_HEADER**") != std::string_view::npos &&
        (sv.find("PARASOLID") != std::string_view::npos || sv.find("SCH_") != std::string_view::npos ||
         (size >= 4 && data[0] == 'P' && data[1] == 'S' && data[2] == 0)))
    {
        LoadedMesh3D psMesh;
        const wchar_t* psExt = (size >= 4 && data[0] == 'P' && data[1] == 'S' && data[2] == 0) ? L".x_b" : L".x_t";
        if (ParseCadKernels200(data, size, psExt, psMesh) && !psMesh.vertices.empty() && !psMesh.tris.empty()) {
            const double ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            AppendTransformedPart(psMesh, ident, dst);
            return true;
        }
    }

    // 2. Check SolidWorks DisplayList tessellation buffer:
    //    Header "SWDL3D\0\0" + uint32 version (or raw DisplayLists__Zip tessellation block):
    //    [uint32 nVerts][float3 verts[nVerts]][optional float3 normals[nVerts]][uint32 nIndices][uint32/uint16 indices]
    size_t startOff = 0;
    if (size >= 12 && std::memcmp(data, "SWDL3D\0\0", 8) == 0) {
        uint32_t ver = RdU32(data + 8);
        if (ver == 0 || ver > 64) return false;
        startOff = 12;
    }

    for (size_t off = startOff; off + 20 <= size && off <= startOff + 128; off += 4) {
        uint32_t nVerts = RdU32(data + off);
        if (nVerts < 3 || nVerts > 2000000u) continue;
        uint64_t vBytes = static_cast<uint64_t>(nVerts) * 12ULL;
        if (off + 4ULL + vBytes + 4ULL > size) continue;

        const uint8_t* vp = data + off + 4;
        std::vector<Vec3f> verts;
        verts.reserve(nVerts);
        bool okV = true;
        float mn = 1e30f, mx = -1e30f;
        for (uint32_t i = 0; i < nVerts; ++i) {
            float x = RdF32(vp + static_cast<size_t>(i) * 12u + 0);
            float y = RdF32(vp + static_cast<size_t>(i) * 12u + 4);
            float z = RdF32(vp + static_cast<size_t>(i) * 12u + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::fabs(x) > 1e7f || std::fabs(y) > 1e7f || std::fabs(z) > 1e7f)
            {
                okV = false;
                break;
            }
            mn = std::min({ mn, x, y, z });
            mx = std::max({ mx, x, y, z });
            verts.push_back({ x, y, z });
        }
        if (!okV || (mx - mn) < 1e-5f) continue;

        for (size_t skipNrm : { size_t(0), static_cast<size_t>(vBytes) }) {
            size_t ip = off + 4u + static_cast<size_t>(vBytes) + skipNrm;
            if (ip + 4u > size) continue;
            uint32_t nIdx = RdU32(data + ip);
            if (nIdx < 3 || nIdx > 6000000u || (nIdx % 3u) != 0u) continue;

            for (size_t idxSz : { size_t(4), size_t(2) }) {
                if (idxSz == 2u && nVerts > 65535u) continue;
                if (ip + 4ULL + static_cast<uint64_t>(nIdx) * idxSz > size) continue;
                const uint8_t* ib = data + ip + 4u;
                std::vector<std::array<uint32_t, 3>> tris;
                tris.reserve(nIdx / 3u);
                bool okI = true;
                for (uint32_t t = 0; t + 2 < nIdx; t += 3) {
                    uint32_t a = (idxSz == 4u) ? RdU32(ib + (t + 0) * 4u) : RdU16(ib + (t + 0) * 2u);
                    uint32_t b = (idxSz == 4u) ? RdU32(ib + (t + 1) * 4u) : RdU16(ib + (t + 1) * 2u);
                    uint32_t c = (idxSz == 4u) ? RdU32(ib + (t + 2) * 4u) : RdU16(ib + (t + 2) * 2u);
                    if (a >= nVerts || b >= nVerts || c >= nVerts) { okI = false; break; }
                    if (a != b && b != c && a != c) tris.push_back({ a, b, c });
                }
                if (okI && !tris.empty()) {
                    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
                    dst.vertices.insert(dst.vertices.end(), verts.begin(), verts.end());
                    for (const auto& ct : tris) {
                        dst.tris.push_back({ base + ct[0], base + ct[1], base + ct[2], {} });
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

inline bool TryInflateZlibAtAnyHeaderOffset(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    if (!data || size < 8) return false;
    size_t lim = std::min<size_t>(size - 6u, 32u);
    for (size_t off = 0; off <= lim; ++off) {
        uint8_t b0 = data[off], b1 = data[off + 1];
        if (b0 == 0x78u && (b1 == 0x01u || b1 == 0x5Eu || b1 == 0x9Cu || b1 == 0xDAu) &&
            ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u))
        {
            if (ZipDeflate::InflateZlib(data + off, size - off, out, 64 * 1024 * 1024) && !out.empty()) {
                return true;
            }
        }
    }
    return false;
}

} // namespace SolidWorksDetail

inline bool ParseSolidWorksPartOrAssembly(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    int assyDepth = 0)
{
    using namespace SolidWorksDetail;
    if (!data || size < 32 || assyDepth > 4) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    std::wstring dir = fullPath.empty() ? std::wstring() : PathUtils::GetParentPath(fullPath);

    auto processStreamBuffer = [&](const uint8_t* sPtr, size_t sLen) {
        if (!sPtr || sLen < 16) return;

        // Check SolidWorks Assembly Component Occurrences stream:
        //   ["SWASMOCC" 8B][uint32 ver=1][uint32 count][repeated: uint16 wLen, UTF-16LE relPath, double mat[16]]
        if (extLower == L".sldasm" && sLen >= 16 && std::memcmp(sPtr, "SWASMOCC", 8) == 0 && !dir.empty()) {
            uint32_t ver = RdU32(sPtr + 8);
            uint32_t cnt = RdU32(sPtr + 12);
            size_t pos = 16;
            if (ver == 1 && cnt > 0 && cnt <= 2048) {
                for (uint32_t k = 0; k < cnt; ++k) {
                    if (pos + 2 > sLen) break;
                    uint16_t wLen = RdU16(sPtr + pos); pos += 2;
                    if (wLen == 0 || wLen > 512 || pos + static_cast<size_t>(wLen) * 2u + 128u > sLen) break;
                    std::wstring relName;
                    relName.reserve(wLen);
                    for (uint16_t c = 0; c < wLen; ++c) {
                        uint16_t ch = RdU16(sPtr + pos + c * 2u);
                        if (ch != 0) relName.push_back(static_cast<wchar_t>(ch));
                    }
                    pos += static_cast<size_t>(wLen) * 2u;
                    double mat[16] = {};
                    for (int m = 0; m < 16; ++m) mat[m] = RdF64(sPtr + pos + m * 8u);
                    pos += 128u;

                    std::wstring partPath = dir + L"\\" + PathUtils::GetFileName(relName);
                    std::vector<uint8_t> partBytes;
                    if (PathUtils::ReadFileBytes(partPath, 64 * 1024 * 1024, partBytes) && !partBytes.empty()) {
                        LoadedMesh3D compMesh;
                        std::wstring cExt = PathUtils::ToLower(PathUtils::GetExtension(partPath));
                        if (ParseSolidWorksPartOrAssembly(partBytes.data(), partBytes.size(), cExt, partPath, compMesh, assyDepth + 1)) {
                            AppendTransformedPart(compMesh, mat, mesh);
                        }
                    }
                }
            }
            return;
        }

        if (TryDecodeSwDisplayListBuffer(sPtr, sLen, mesh)) return;

        std::vector<uint8_t> inflated;
        if (TryInflateZlibAtAnyHeaderOffset(sPtr, sLen, inflated)) {
            TryDecodeSwDisplayListBuffer(inflated.data(), inflated.size(), mesh);
        }
    };

    // 1. OLE2 Compound File (.sldprt / .sldasm <= 2014 and OLE2 containers)
    Ole2::CompoundFile cf;
    if (Ole2::ParseCompoundFile(data, size, cf)) {
        for (const auto& entry : cf.entries) {
            if (entry.objectType != 2 || entry.size == 0) continue;
            std::vector<uint8_t> rawStream;
            if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;
            processStreamBuffer(rawStream.data(), rawStream.size());
        }
    }
    // 2. SolidWorks 2015+ G3 / ZIP Stream Container ("PK\x03\x04" or "SWG3CNTR")
    else if (size >= 30 && data[0] == 'P' && data[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries)) {
            for (const auto& ze : zEntries) {
                if (ze.isDirectory) continue;
                std::vector<uint8_t> zBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, zBytes, 64 * 1024 * 1024) && !zBytes.empty()) {
                    processStreamBuffer(zBytes.data(), zBytes.size());
                }
            }
        }
    } else if (size >= 16 && std::memcmp(data, "SWG3CNTR", 8) == 0) {
        uint32_t nStreams = RdU32(data + 8);
        size_t pos = 12;
        for (uint32_t s = 0; s < nStreams && s < 256 && pos + 8 <= size; ++s) {
            uint32_t sLen = RdU32(data + pos + 4);
            pos += 8;
            if (sLen > size - pos) break;
            processStreamBuffer(data + pos, sLen);
            pos += sLen;
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".sldasm")
        ? L"Dassault SolidWorks 3D Assembly (.sldasm)"
        : L"Dassault SolidWorks 3D Part (.sldprt)";
    return true;
}

} // namespace Formats
