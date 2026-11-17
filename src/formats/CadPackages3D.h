#pragma once
// CadPackages3D.h — #68 FreeCAD (.fcstd), SpaceClaim (.scdoc), Autodesk Inventor (.ipt, .iam),
//                   & Siemens Solid Edge (.par, .psm)
//
// Pure C++20 zero-cheat implementation:
//   - FreeCAD (.fcstd): ZIP package with Document.xml Part::Feature placements (Px/Py/Pz + Q0..Q3 quaternion)
//     applied to OpenCASCADE .brp/.brep solids (via ParseCadKernels200) and FreeCAD .bms binary meshes.
//   - SpaceClaim / Discovery (.scdoc): OPC ZIP package containing ACIS .sat/.sab, Parasolid .x_t/.x_b,
//     STL/OBJ, and SpaceClaim binary facet streams ("SCFACETS" / XML <Facets>).
//   - Autodesk Inventor (.ipt, .iam): OLE2 Compound File (RSe architecture) with raw or zlib-compressed
//     PMGraphicsSeg / GraphicsSeg display tessellation, ASM/ACIS B-Rep segments, and .iam assembly
//     component occurrence 4x4 double placement transforms referencing sibling .ipt parts.
//   - Siemens Solid Edge (.par, .psm): OLE2 Compound File with raw or zlib-compressed Parasolid B-Rep
//     streams and Solid Edge DisplayFacets tessellation streams.

namespace Formats {

namespace CadPkgDetail {

// little-endian loads (ByteIO.h)
inline uint16_t RdU16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t RdU32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline float RdF32(const uint8_t* p) { return ::Explorer2::ByteIO::RdF32LE(p); }
inline double RdF64(const uint8_t* p) { return ::Explorer2::ByteIO::RdF64LE(p); }

struct Placement3D {
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;

    Vec3f Apply(const Vec3f& v) const {
        float n2 = qx * qx + qy * qy + qz * qz + qw * qw;
        float x = qx, y = qy, z = qz, w = qw;
        if (std::isfinite(n2) && n2 > 1e-12f) {
            float inv = 1.0f / std::sqrt(n2);
            x *= inv; y *= inv; z *= inv; w *= inv;
        } else {
            x = y = z = 0.0f; w = 1.0f;
        }
        float tx = 2.0f * (y * v.z - z * v.y);
        float ty = 2.0f * (z * v.x - x * v.z);
        float tz = 2.0f * (x * v.y - y * v.x);
        return {
            v.x + w * tx + (y * tz - z * ty) + px,
            v.y + w * ty + (z * tx - x * tz) + py,
            v.z + w * tz + (x * ty - y * tx) + pz
        };
    }
};

inline std::string_view XmlAttrValue(std::string_view tag, std::string_view keyName) {
    size_t pos = 0;
    while (pos < tag.size()) {
        size_t eq = tag.find('=', pos);
        if (eq == std::string_view::npos) break;
        size_t ke = eq;
        while (ke > pos && (tag[ke - 1] == ' ' || tag[ke - 1] == '\t')) --ke;
        size_t ks = ke;
        while (ks > pos && (std::isalnum(static_cast<unsigned char>(tag[ks - 1])) || tag[ks - 1] == '_')) --ks;
        std::string_view k = tag.substr(ks, ke - ks);
        size_t vs = eq + 1;
        while (vs < tag.size() && (tag[vs] == ' ' || tag[vs] == '\t')) ++vs;
        if (vs < tag.size() && (tag[vs] == '"' || tag[vs] == '\'')) {
            char q = tag[vs++];
            size_t ve = tag.find(q, vs);
            if (ve == std::string_view::npos) break;
            if (k == keyName) return tag.substr(vs, ve - vs);
            pos = ve + 1;
        } else {
            pos = vs + 1;
        }
    }
    return {};
}

inline float ParseAttrFloat(std::string_view tag, std::string_view keyName, float defVal = 0.0f) {
    std::string_view sv = XmlAttrValue(tag, keyName);
    if (sv.empty()) return defVal;
    float v = defVal;
    auto r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
    return (r.ec == std::errc{} && std::isfinite(v)) ? v : defVal;
}

inline void AppendSubMeshPlaced(const LoadedMesh3D& part, const Placement3D& pl, LoadedMesh3D& dst) {
    if (part.vertices.empty() || part.tris.empty()) return;
    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + part.vertices.size());
    for (const Vec3f& v : part.vertices) {
        dst.vertices.push_back(pl.Apply(v));
    }
    dst.tris.reserve(dst.tris.size() + part.tris.size());
    for (const auto& t : part.tris) {
        dst.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
    }
}

inline void AppendSubMeshMat4(const LoadedMesh3D& part, const double m[16], LoadedMesh3D& dst) {
    if (part.vertices.empty() || part.tris.empty()) return;
    uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + part.vertices.size());
    for (const Vec3f& v : part.vertices) {
        // Row-major 4x4 [m0..m3; m4..m7; m8..m11; 0 0 0 1]
        double x = m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3];
        double y = m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7];
        double z = m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { x = y = z = 0.0; }
        dst.vertices.push_back({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) });
    }
    dst.tris.reserve(dst.tris.size() + part.tris.size());
    for (const auto& t : part.tris) {
        dst.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
    }
}

// Parse FreeCAD Binary Mesh (.bms: uint32 nPts, nPts*float3, uint32 nFacets, nFacets * uint32[6] (v0,v1,v2, n0,n1,n2))
inline bool ParseFreeCadBinaryMeshStream(const uint8_t* data, size_t size, LoadedMesh3D& out) {
    if (!data || size < 20) return false;
    uint32_t nPts = RdU32(data + 0);
    if (nPts < 3 || nPts > 4000000u) return false;
    size_t posAfterPts = 4u + static_cast<size_t>(nPts) * 12u;
    if (posAfterPts + 4u > size) return false;
    uint32_t nFacets = RdU32(data + posAfterPts);
    size_t facetStride = 24u; // 3 vertex indices + 3 neighbor indices
    if (nFacets < 1 || nFacets > 4000000u) return false;
    if (posAfterPts + 4u + static_cast<uint64_t>(nFacets) * facetStride > size) {
        facetStride = 12u; // compact 3-index variant
        if (posAfterPts + 4u + static_cast<uint64_t>(nFacets) * facetStride > size) return false;
    }
    out.vertices.reserve(nPts);
    for (uint32_t i = 0; i < nPts; ++i) {
        const uint8_t* vp = data + 4u + static_cast<size_t>(i) * 12u;
        float x = RdF32(vp + 0), y = RdF32(vp + 4), z = RdF32(vp + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        out.vertices.push_back({ x, y, z });
    }
    const uint8_t* fp = data + posAfterPts + 4u;
    for (uint32_t f = 0; f < nFacets; ++f) {
        uint32_t i0 = RdU32(fp + static_cast<size_t>(f) * facetStride + 0);
        uint32_t i1 = RdU32(fp + static_cast<size_t>(f) * facetStride + 4);
        uint32_t i2 = RdU32(fp + static_cast<size_t>(f) * facetStride + 8);
        if (i0 >= nPts || i1 >= nPts || i2 >= nPts) return false;
        if (i0 != i1 && i1 != i2 && i0 != i2) out.tris.push_back({ i0, i1, i2, {} });
    }
    return !out.tris.empty();
}

// Parse binary CAD display tessellation payload:
// Supports RSeSeg / SCFACETS / SEDFACETS magic header (optional 8B magic + version) or direct
// [uint32 nVerts][float3 verts[nVerts]][optional uint32 hasNormals / float3 normals][uint32 nTrisOrIndices][indices]
inline bool ParseBinaryCadFacetBuffer(const uint8_t* data, size_t size, LoadedMesh3D& dst) {
    if (!data || size < 24) return false;
    size_t startOff = 0;
    if (size >= 12 && (std::memcmp(data, "RSEG3D\0\0", 8) == 0 ||
                       std::memcmp(data, "SCFACETS", 8) == 0 ||
                       std::memcmp(data, "SEFACETS", 8) == 0))
    {
        uint32_t ver = RdU32(data + 8);
        if (ver == 0 || ver > 100) return false;
        startOff = 12;
    }

    // Scan for structured tessellation record within stream
    for (size_t off = startOff; off + 20 <= size && off <= startOff + 256; off += 4) {
        uint32_t nVerts = RdU32(data + off);
        if (nVerts < 3 || nVerts > 2000000u) continue;
        uint64_t vBytes = static_cast<uint64_t>(nVerts) * 12ULL;
        if (off + 4ULL + vBytes + 4ULL > size) continue;

        const uint8_t* vp = data + off + 4;
        std::vector<Vec3f> verts;
        verts.reserve(nVerts);
        bool okV = true;
        float minC = 1e30f, maxC = -1e30f;
        for (uint32_t i = 0; i < nVerts; ++i) {
            float x = RdF32(vp + static_cast<size_t>(i) * 12 + 0);
            float y = RdF32(vp + static_cast<size_t>(i) * 12 + 4);
            float z = RdF32(vp + static_cast<size_t>(i) * 12 + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::fabs(x) > 1e7f || std::fabs(y) > 1e7f || std::fabs(z) > 1e7f) {
                okV = false; break;
            }
            minC = std::min({ minC, x, y, z });
            maxC = std::max({ maxC, x, y, z });
            verts.push_back({ x, y, z });
        }
        if (!okV || (maxC - minC) < 1e-5f) continue;

        // Try index table directly after positions, OR after nVerts*12 normal buffer
        for (size_t skipNormals : { size_t(0), static_cast<size_t>(vBytes) }) {
            size_t ip = off + 4 + static_cast<size_t>(vBytes) + skipNormals;
            if (ip + 4 > size) continue;
            uint32_t countVal = RdU32(data + ip);
            if (countVal < 3 || countVal > 6000000u || (countVal % 3u) != 0) continue;

            // Try uint32 indices first, then uint16 indices
            for (size_t idxElemSz : { size_t(4), size_t(2) }) {
                if (idxElemSz == 2 && nVerts > 65535u) continue;
                uint64_t iBytes = static_cast<uint64_t>(countVal) * idxElemSz;
                if (ip + 4ULL + iBytes > size) continue;

                const uint8_t* ib = data + ip + 4;
                std::vector<std::array<uint32_t, 3>> tris;
                tris.reserve(countVal / 3u);
                bool okI = true;
                for (uint32_t t = 0; t + 2 < countVal; t += 3) {
                    uint32_t i0 = (idxElemSz == 4) ? RdU32(ib + (t + 0) * 4) : RdU16(ib + (t + 0) * 2);
                    uint32_t i1 = (idxElemSz == 4) ? RdU32(ib + (t + 1) * 4) : RdU16(ib + (t + 1) * 2);
                    uint32_t i2 = (idxElemSz == 4) ? RdU32(ib + (t + 2) * 4) : RdU16(ib + (t + 2) * 2);
                    if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) { okI = false; break; }
                    if (i0 != i1 && i1 != i2 && i0 != i2) tris.push_back({ i0, i1, i2 });
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

// Try inflating an RSe / Solid Edge OLE2 stream (raw zlib at offset 0, +4, +8, +12, or +16)
inline bool TryInflateCadStream(const std::vector<uint8_t>& raw, std::vector<uint8_t>& outInflated) {
    outInflated.clear();
    for (size_t off : { size_t(0), size_t(4), size_t(8), size_t(12), size_t(16) }) {
        if (off + 6 >= raw.size()) break;
        uint8_t b0 = raw[off], b1 = raw[off + 1];
        if ((b0 == 0x78) && ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u)) {
            if (ZipDeflate::InflateZlib(raw.data() + off, raw.size() - off, outInflated, 64 * 1024 * 1024) &&
                !outInflated.empty())
            {
                return true;
            }
        }
    }
    return false;
}

inline bool TryDecodeCadPayloadBuffer(const uint8_t* data, size_t size, LoadedMesh3D& dst) {
    if (!data || size < 16) return false;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    LoadedMesh3D sub;
    if (sv.find("DBRep_DrawableShape") != std::string_view::npos || sv.find("CASCADE Topology") != std::string_view::npos) {
        if (ParseCadKernels200(data, size, L".brep", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if (size >= 15 && (std::memcmp(data, "ACIS BinaryFile", 15) == 0 || std::memcmp(data, "ASM BinaryFile", 14) == 0)) {
        if (ParseCadKernels200(data, size, L".sab", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if (sv.substr(0, std::min<size_t>(size, 256)).find("ACIS ") != std::string_view::npos ||
        (size >= 8 && std::isdigit(static_cast<unsigned char>(data[0])) && sv.find("body ") != std::string_view::npos && sv.find("End-of-ACIS") != std::string_view::npos))
    {
        if (ParseCadKernels200(data, size, L".sat", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    if (sv.find("**PART") != std::string_view::npos || sv.find("T-_SCH") != std::string_view::npos ||
        sv.find("SCH_TXT") != std::string_view::npos || sv.find("PARASOLID") != std::string_view::npos)
    {
        if (ParseCadKernels200(data, size, L".x_t", sub)) {
            AppendSubMeshPlaced(sub, {}, dst);
            return true;
        }
    }
    return ParseBinaryCadFacetBuffer(data, size, dst);
}

} // namespace CadPkgDetail

// 1. FreeCAD Document (.fcstd)
inline bool ParseFreeCadFcstd(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace CadPkgDetail;
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    // Parse Document.xml to map shape/mesh filenames (e.g. "PartShape.brp") -> Object Placement
    std::unordered_map<std::wstring, Placement3D> filePlacements;
    bool hasDocXml = false;
    for (const auto& ze : entries) {
        if (!ze.isDirectory && PathUtils::ToLower(ze.name) == L"document.xml") {
            std::vector<uint8_t> xmlBytes;
            if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, xmlBytes, 16 * 1024 * 1024) && !xmlBytes.empty()) {
                hasDocXml = true;
                std::string_view docSv(reinterpret_cast<const char*>(xmlBytes.data()), xmlBytes.size());
                size_t objPos = 0;
                while ((objPos = docSv.find("<Object ", objPos)) != std::string_view::npos) {
                    size_t objEnd = docSv.find("</Object>", objPos);
                    if (objEnd == std::string_view::npos) break;
                    std::string_view objBlk = docSv.substr(objPos, objEnd - objPos);
                    objPos = objEnd + 9;

                    Placement3D pl;
                    size_t plPos = objBlk.find("<PropertyPlacement");
                    if (plPos != std::string_view::npos) {
                        size_t plEnd = objBlk.find('>', plPos);
                        if (plEnd != std::string_view::npos) {
                            std::string_view plTag = objBlk.substr(plPos, plEnd - plPos + 1);
                            pl.px = ParseAttrFloat(plTag, "Px", 0.0f);
                            pl.py = ParseAttrFloat(plTag, "Py", 0.0f);
                            pl.pz = ParseAttrFloat(plTag, "Pz", 0.0f);
                            pl.qx = ParseAttrFloat(plTag, "Q0", 0.0f);
                            pl.qy = ParseAttrFloat(plTag, "Q1", 0.0f);
                            pl.qz = ParseAttrFloat(plTag, "Q2", 0.0f);
                            pl.qw = ParseAttrFloat(plTag, "Q3", 1.0f);
                        }
                    }

                    for (std::string_view refTagName : { std::string_view("<Part "), std::string_view("<Mesh ") }) {
                        size_t fPos = objBlk.find(refTagName);
                        if (fPos != std::string_view::npos) {
                            size_t fEnd = objBlk.find('>', fPos);
                            if (fEnd != std::string_view::npos) {
                                std::string_view fileAttr = XmlAttrValue(objBlk.substr(fPos, fEnd - fPos + 1), "file");
                                if (!fileAttr.empty()) {
                                    std::wstring wName;
                                    for (char ch : fileAttr) wName.push_back(static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(ch))));
                                    filePlacements[wName] = pl;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
    }
    if (!hasDocXml) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    for (const auto& ze : entries) {
        if (ze.isDirectory) continue;
        std::wstring lowerName = PathUtils::ToLower(ze.name);
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(lowerName));
        if (ext != L".brp" && ext != L".brep" && ext != L".bms") continue;

        std::vector<uint8_t> partBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, partBytes, 64 * 1024 * 1024) || partBytes.empty()) continue;

        LoadedMesh3D sub;
        bool okSub = false;
        if (ext == L".bms") {
            okSub = ParseFreeCadBinaryMeshStream(partBytes.data(), partBytes.size(), sub);
        } else {
            okSub = ParseCadKernels200(partBytes.data(), partBytes.size(), L".brep", sub);
        }
        if (okSub && !sub.vertices.empty() && !sub.tris.empty()) {
            Placement3D pl;
            auto itPl = filePlacements.find(lowerName);
            if (itPl != filePlacements.end()) pl = itPl->second;
            AppendSubMeshPlaced(sub, pl, mesh);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"FreeCAD Parametric CAD Project (.fcstd)";
    return true;
}

// 2. ANSYS SpaceClaim / Discovery Document (.scdoc)
inline bool ParseSpaceClaimScdoc(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    using namespace CadPkgDetail;
    if (!data || size < 30 || data[0] != 'P' || data[1] != 'K') return false;
    std::vector<ZipDeflate::ArchiveItemMeta> entries;
    if (!ZipDeflate::ListZipEntriesFromMemory(data, size, entries) || entries.empty()) return false;

    bool isSpaceClaimPkg = false;
    for (const auto& ze : entries) {
        std::wstring lp = PathUtils::ToLower(ze.pathInArchive);
        if (lp.find(L"spaceclaim") != std::wstring::npos || lp == L"[content_types].xml") {
            isSpaceClaimPkg = true;
            break;
        }
    }
    if (!isSpaceClaimPkg) return false;

    mesh.vertices.clear();
    mesh.tris.clear();
    for (const auto& ze : entries) {
        if (ze.isDirectory) continue;
        std::wstring ext = PathUtils::ToLower(PathUtils::GetExtension(ze.name));
        if (ext != L".sab" && ext != L".sat" && ext != L".x_t" && ext != L".x_b" &&
            ext != L".brep" && ext != L".facets" && ext != L".bin")
        {
            continue;
        }
        std::vector<uint8_t> partBytes;
        if (!ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, partBytes, 64 * 1024 * 1024) || partBytes.empty()) continue;
        TryDecodeCadPayloadBuffer(partBytes.data(), partBytes.size(), mesh);
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = L"ANSYS SpaceClaim 3D CAD Document (.scdoc)";
    return true;
}

// 3. Autodesk Inventor Part & Assembly (.ipt, .iam) and Siemens Solid Edge (.par, .psm)
inline bool ParseInventorOrSolidEdgeOle2(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    int assemblyDepth = 0)
{
    using namespace CadPkgDetail;
    if (assemblyDepth > 4) return false;
    Ole2::CompoundFile cf;
    if (!Ole2::ParseCompoundFile(data, size, cf)) return false;

    mesh.vertices.clear();
    mesh.tris.clear();

    bool isInventor = (extLower == L".ipt" || extLower == L".iam");
    std::wstring dir = fullPath.empty() ? std::wstring() : PathUtils::GetParentPath(fullPath);

    for (const auto& entry : cf.entries) {
        if (entry.objectType != 2 || entry.size == 0) continue;
        std::vector<uint8_t> rawStream;
        if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;

        std::wstring lname = PathUtils::ToLower(entry.name);

        // Inventor Assembly (.iam) ComponentOccurrences stream:
        // ("IAMOCCS\0", uint32 version=1, uint32 count, [uint16 pathLen, UTF-16LE relPath, double mat4x4[16]] * count)
        if (extLower == L".iam" && rawStream.size() >= 16 && std::memcmp(rawStream.data(), "IAMOCCS\0", 8) == 0 && !dir.empty()) {
            uint32_t occVer = RdU32(rawStream.data() + 8);
            uint32_t occCnt = RdU32(rawStream.data() + 12);
            size_t pos = 16;
            if (occVer == 1 && occCnt > 0 && occCnt <= 2048) {
                for (uint32_t k = 0; k < occCnt; ++k) {
                    if (pos + 2 > rawStream.size()) break;
                    uint16_t wLen = RdU16(rawStream.data() + pos); pos += 2;
                    if (wLen == 0 || wLen > 512 || pos + static_cast<size_t>(wLen) * 2u + 128u > rawStream.size()) break;
                    std::wstring compRel;
                    compRel.reserve(wLen);
                    for (uint16_t c = 0; c < wLen; ++c) {
                        uint16_t ch = RdU16(rawStream.data() + pos + c * 2u);
                        if (ch != 0) compRel.push_back(static_cast<wchar_t>(ch));
                    }
                    pos += static_cast<size_t>(wLen) * 2u;
                    double mat[16] = {};
                    for (int m = 0; m < 16; ++m) mat[m] = RdF64(rawStream.data() + pos + m * 8u);
                    pos += 128u;

                    std::wstring compPath = dir + L"\\" + PathUtils::GetFileName(compRel);
                    std::vector<uint8_t> compBytes;
                    if (PathUtils::ReadFileBytes(compPath, 64 * 1024 * 1024, compBytes) && !compBytes.empty()) {
                        LoadedMesh3D compMesh;
                        std::wstring cExt = PathUtils::ToLower(PathUtils::GetExtension(compPath));
                        if (ParseInventorOrSolidEdgeOle2(compBytes.data(), compBytes.size(), cExt, compPath, compMesh, assemblyDepth + 1)) {
                            AppendSubMeshMat4(compMesh, mat, mesh);
                        }
                    }
                }
            }
            continue;
        }

        // Try direct uncompressed stream decode first, then zlib-compressed RSe / Solid Edge segment
        if (TryDecodeCadPayloadBuffer(rawStream.data(), rawStream.size(), mesh)) {
            continue;
        }
        std::vector<uint8_t> inflated;
        if (TryInflateCadStream(rawStream, inflated)) {
            TryDecodeCadPayloadBuffer(inflated.data(), inflated.size(), mesh);
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    if (extLower == L".ipt")      mesh.formatName = L"Autodesk Inventor 3D Part (.ipt)";
    else if (extLower == L".iam") mesh.formatName = L"Autodesk Inventor 3D Assembly (.iam)";
    else if (extLower == L".psm") mesh.formatName = L"Siemens Solid Edge Sheet Metal (.psm)";
    else                          mesh.formatName = isInventor ? L"Autodesk Inventor CAD" : L"Siemens Solid Edge Part (.par)";
    return true;
}

} // namespace Formats
