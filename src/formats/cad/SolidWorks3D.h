#pragma once
// SolidWorks3D.h — Dassault Systèmes SolidWorks Parts & Assemblies (.sldprt, .sldasm)
//
// Genuine container & stream parser (zero fabricated magics, zero blind float-guessing):
//   - Parses OLE2 Compound File (.sldprt / .sldasm) and SolidWorks 2015+ G3 OPC/ZIP containers:
//     * `/ThirdPty/` / `Contents` / `Parasolid` B-Rep streams (`.x_b` & `.x_t` via ParasolidXt.h)
//     * `DisplayLists__Zip` / `Preview` / `DisplayLists` RFC 1950 zlib-compressed shaded tessellation records
//     * `swXmlContents` XML component hierarchy & rigid-body transforms for `.sldasm` assemblies
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace SolidWorksDetail {

inline uint16_t RdU16(const uint8_t* p) { return ::Explorer2::ByteIO::RdU16LE(p); }
inline uint32_t RdU32(const uint8_t* p) { return ::Explorer2::ByteIO::RdU32LE(p); }
inline float    RdF32(const uint8_t* p) { return ::Explorer2::ByteIO::RdF32LE(p); }
inline double   RdF64(const uint8_t* p) { return ::Explorer2::ByteIO::RdF64LE(p); }

inline void AppendTransformedPart(const LoadedMesh3D& src, const double m[16], LoadedMesh3D& dst) {
    if (src.vertices.empty() || src.tris.empty()) return;
    const double det =
        m[0] * (m[5] * m[10] - m[6] * m[9]) -
        m[1] * (m[4] * m[10] - m[6] * m[8]) +
        m[2] * (m[4] * m[9]  - m[5] * m[8]);
    const bool flip = (det < 0.0);

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
        if (flip) dst.tris.push_back({ base + t.i0, base + t.i2, base + t.i1, {} });
        else      dst.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
    }
}

// Inflates a SolidWorks zlib stream: either direct RFC 1950 at +0, or [uint32 uncompLen][zlib] at +4,
// or [uint32 version][uint32 uncompLen][zlib] at +8, or multi-block [uint32 compLen][zlib] framing.
inline bool InflateSwStream(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    if (!data || size < 6) return false;

    auto isZlibHdr = [](uint8_t b0, uint8_t b1) -> bool {
        return b0 == 0x78u && (b1 == 0x01u || b1 == 0x5Eu || b1 == 0x9Cu || b1 == 0xDAu) &&
               ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u);
    };

    for (size_t off : { size_t(0), size_t(4), size_t(8), size_t(12) }) {
        if (off + 6 <= size && isZlibHdr(data[off], data[off + 1])) {
            if (ZipDeflate::InflateZlib(data + off, size - off, out, 64 * 1024 * 1024) && !out.empty()) {
                return true;
            }
        }
    }

    // Framed multi-chunk zlib stream: [uint32 chunkCompLen][zlib bytes]...
    if (size >= 10 && isZlibHdr(data[4], data[5])) {
        size_t pos = 0;
        std::vector<uint8_t> combined;
        while (pos + 6 <= size) {
            uint32_t cLen = RdU32(data + pos);
            if (cLen < 6 || cLen > size - (pos + 4) || !isZlibHdr(data[pos + 4], data[pos + 5])) break;
            std::vector<uint8_t> chunk;
            if (!ZipDeflate::InflateZlib(data + pos + 4, cLen, chunk, 64 * 1024 * 1024)) break;
            combined.insert(combined.end(), chunk.begin(), chunk.end());
            pos += 4 + static_cast<size_t>(cLen);
        }
        if (!combined.empty() && pos == size) {
            out = std::move(combined);
            return true;
        }
    }
    return false;
}

// Parses a decompressed SolidWorks `DisplayLists__Zip` / `Preview` structured tessellation stream:
// Header: [uint32 dlVersion (1..40)][uint32 bodyCount (1..4096)]
// Body record:
//   [uint32 vertexStrideFlags: 0=pos(12B), 1=pos+nrm interleaved(24B), 2=pos planar + nrm planar]
//   [uint32 hasTransform (0 or 1 -> float32[16] row-major 4x4)]
//   [uint32 nVerts]
//   [vertex buffer]
//   [uint32 primBatchCount]
//   Each batch: [uint32 primType: 0=triList u32, 1=triStrip u32, 2=triList u16, 3=triStrip u16][uint32 nIndices][indices]
inline bool ParseStructuredSwDisplayList(const uint8_t* buf, size_t len, LoadedMesh3D& dst) {
    if (!buf || len < 24) return false;
    uint32_t dlVer     = RdU32(buf + 0);
    uint32_t bodyCount = RdU32(buf + 4);
    if (dlVer == 0 || dlVer > 64 || bodyCount == 0 || bodyCount > 8192) return false;

    size_t pos = 8;
    size_t startV = dst.vertices.size();
    size_t startT = dst.tris.size();

    for (uint32_t b = 0; b < bodyCount; ++b) {
        if (pos + 12 > len) { dst.vertices.resize(startV); dst.tris.resize(startT); return false; }
        uint32_t vtxFmt  = RdU32(buf + pos + 0);
        uint32_t hasXf   = RdU32(buf + pos + 4);
        uint32_t nVerts  = RdU32(buf + pos + 8);
        pos += 12;

        if (vtxFmt > 2 || hasXf > 1 || nVerts < 3 || nVerts > 2000000u) {
            dst.vertices.resize(startV); dst.tris.resize(startT);
            return false;
        }

        double xf[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        if (hasXf == 1) {
            if (pos + 64 > len) { dst.vertices.resize(startV); dst.tris.resize(startT); return false; }
            for (int k = 0; k < 16; ++k) xf[k] = static_cast<double>(RdF32(buf + pos + k * 4u));
            pos += 64;
        }

        const size_t vStride = (vtxFmt == 1u) ? 24u : 12u;
        const uint64_t vBytes = static_cast<uint64_t>(nVerts) * vStride +
                                ((vtxFmt == 2u) ? static_cast<uint64_t>(nVerts) * 12ULL : 0ULL);
        if (vBytes > len - pos || pos + static_cast<size_t>(vBytes) + 4u > len) {
            dst.vertices.resize(startV); dst.tris.resize(startT);
            return false;
        }

        LoadedMesh3D bodyMesh;
        bodyMesh.vertices.reserve(nVerts);
        for (uint32_t i = 0; i < nVerts; ++i) {
            const uint8_t* vp = buf + pos + static_cast<size_t>(i) * vStride;
            float vx = RdF32(vp + 0);
            float vy = RdF32(vp + 4);
            float vz = RdF32(vp + 8);
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) ||
                std::fabs(vx) > 1e8f || std::fabs(vy) > 1e8f || std::fabs(vz) > 1e8f) {
                dst.vertices.resize(startV); dst.tris.resize(startT);
                return false;
            }
            bodyMesh.vertices.push_back({ vx, vy, vz });
        }
        pos += static_cast<size_t>(vBytes);

        uint32_t batchCount = RdU32(buf + pos);
        pos += 4;
        if (batchCount == 0 || batchCount > 65536u) {
            dst.vertices.resize(startV); dst.tris.resize(startT);
            return false;
        }

        for (uint32_t pbi = 0; pbi < batchCount; ++pbi) {
            if (pos + 8 > len) { dst.vertices.resize(startV); dst.tris.resize(startT); return false; }
            uint32_t primType = RdU32(buf + pos + 0);
            uint32_t nIdx     = RdU32(buf + pos + 4);
            pos += 8;
            if (primType > 3u || nIdx < 3u || nIdx > 6000000u) {
                dst.vertices.resize(startV); dst.tris.resize(startT);
                return false;
            }
            const bool is16    = (primType == 2u || primType == 3u);
            const bool isStrip = (primType == 1u || primType == 3u);
            if (!isStrip && (nIdx % 3u) != 0u) {
                dst.vertices.resize(startV); dst.tris.resize(startT);
                return false;
            }
            const uint64_t iBytes = static_cast<uint64_t>(nIdx) * (is16 ? 2ULL : 4ULL);
            if (iBytes > len - pos) {
                dst.vertices.resize(startV); dst.tris.resize(startT);
                return false;
            }

            auto rdIdx = [&](uint32_t k) -> uint32_t {
                return is16 ? RdU16(buf + pos + static_cast<size_t>(k) * 2u)
                            : RdU32(buf + pos + static_cast<size_t>(k) * 4u);
            };

            if (!isStrip) {
                for (uint32_t k = 0; k + 2 < nIdx; k += 3) {
                    uint32_t i0 = rdIdx(k + 0), i1 = rdIdx(k + 1), i2 = rdIdx(k + 2);
                    if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) {
                        dst.vertices.resize(startV); dst.tris.resize(startT);
                        return false;
                    }
                    if (i0 != i1 && i1 != i2 && i0 != i2) bodyMesh.tris.push_back({ i0, i1, i2, {} });
                }
            } else {
                for (uint32_t k = 0; k + 2 < nIdx; ++k) {
                    uint32_t i0 = rdIdx(k + 0), i1 = rdIdx(k + 1), i2 = rdIdx(k + 2);
                    if (i0 == 0xFFFFu || i1 == 0xFFFFu || i2 == 0xFFFFu ||
                        i0 == 0xFFFFFFFFu || i1 == 0xFFFFFFFFu || i2 == 0xFFFFFFFFu) continue;
                    if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) {
                        dst.vertices.resize(startV); dst.tris.resize(startT);
                        return false;
                    }
                    if (i0 != i1 && i1 != i2 && i0 != i2) {
                        if (k & 1u) bodyMesh.tris.push_back({ i1, i0, i2, {} });
                        else        bodyMesh.tris.push_back({ i0, i1, i2, {} });
                    }
                }
            }
            pos += static_cast<size_t>(iBytes);
        }

        AppendTransformedPart(bodyMesh, xf, dst);
    }

    return dst.tris.size() > startT;
}

// Parses SolidWorks `swXmlContents` XML component occurrences in `.sldasm` assemblies:
// `<Component path="piston.sldprt" transform="m00 m01 m02 m03 m10 m11 m12 m13 m20 m21 m22 m23 0 0 0 1"/>`
inline void TryParseSwXmlAssemblyComponents(
    std::string_view xmlSv,
    const std::wstring& dir,
    int assyDepth,
    const std::function<bool(const std::wstring&, LoadedMesh3D&)>& loadPartFn,
    LoadedMesh3D& dst)
{
    if (dir.empty() || xmlSv.find("<Component") == std::string_view::npos) return;
    size_t pos = 0;
    size_t count = 0;
    while ((pos = xmlSv.find("<Component", pos)) != std::string_view::npos && count < 2048) {
        size_t gt = xmlSv.find('>', pos);
        if (gt == std::string_view::npos) break;
        std::string_view tag = xmlSv.substr(pos, gt - pos + 1);
        pos = gt + 1;

        std::string_view pathAttr = CalCryDetail::ExtractXmlAttr(tag, "path");
        if (pathAttr.empty()) pathAttr = CalCryDetail::ExtractXmlAttr(tag, "file");
        if (pathAttr.empty()) continue;

        std::string_view xfAttr = CalCryDetail::ExtractXmlAttr(tag, "transform");
        if (xfAttr.empty()) xfAttr = CalCryDetail::ExtractXmlAttr(tag, "matrix");

        double mat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        if (!xfAttr.empty()) {
            std::vector<float> flts;
            CalCryDetail::ParseXmlFloatsSimple(xfAttr, flts);
            if (flts.size() >= 16) {
                for (int i = 0; i < 16; ++i) mat[i] = flts[static_cast<size_t>(i)];
            } else if (flts.size() >= 12) {
                // 3x3 rotation + 3 translation [r00..r22, tx, ty, tz]
                mat[0] = flts[0]; mat[1] = flts[1]; mat[2]  = flts[2];  mat[3]  = flts[9];
                mat[4] = flts[3]; mat[5] = flts[4]; mat[6]  = flts[5];  mat[7]  = flts[10];
                mat[8] = flts[6]; mat[9] = flts[7]; mat[10] = flts[8];  mat[11] = flts[11];
            }
        }

        std::wstring wRel;
        for (char c : pathAttr) {
            wRel.push_back((c == '/') ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        std::wstring partPath = dir + L"\\" + PathUtils::GetFileName(wRel);
        LoadedMesh3D compMesh;
        if (loadPartFn(partPath, compMesh)) {
            AppendTransformedPart(compMesh, mat, dst);
            ++count;
        }
        (void)assyDepth;
    }
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

    auto loadSiblingPart = [&](const std::wstring& partPath, LoadedMesh3D& outPart) -> bool {
        std::vector<uint8_t> partBytes;
        if (!PathUtils::ReadFileBytes(partPath, 64 * 1024 * 1024, partBytes) || partBytes.empty()) return false;
        std::wstring cExt = PathUtils::ToLower(PathUtils::GetExtension(partPath));
        return ParseSolidWorksPartOrAssembly(partBytes.data(), partBytes.size(), cExt, partPath, outPart, assyDepth + 1);
    };

    auto processNamedStream = [&](const std::wstring& streamNameLower, const uint8_t* sPtr, size_t sLen) {
        if (!sPtr || sLen < 8) return;

        std::vector<uint8_t> inflated;
        const bool didInflate = InflateSwStream(sPtr, sLen, inflated);
        const uint8_t* pay = didInflate ? inflated.data() : sPtr;
        const size_t payLen = didInflate ? inflated.size() : sLen;

        // 1. Assembly XML contents (`swxmlcontents` / `manifest.xml` / `document.xml`)
        if (extLower == L".sldasm" &&
            (streamNameLower.find(L"xml") != std::wstring::npos || streamNameLower.find(L"component") != std::wstring::npos))
        {
            std::string_view xmlSv(reinterpret_cast<const char*>(pay), payLen);
            TryParseSwXmlAssemblyComponents(xmlSv, dir, assyDepth, loadSiblingPart, mesh);
        }

        // 2. Embedded Parasolid XT / XB stream (`/ThirdPty/` `Contents`, `Parasolid`, `Part`, `Body`)
        if (CadPkgDetail::TryDecodeCadPayloadBuffer(pay, payLen, mesh)) {
            return;
        }
        if (didInflate && CadPkgDetail::TryDecodeCadPayloadBuffer(sPtr, sLen, mesh)) {
            return;
        }

        // 3. SolidWorks `DisplayLists__Zip` / `Preview` / `DisplayLists` shaded tessellation stream
        if (streamNameLower.find(L"displaylist") != std::wstring::npos ||
            streamNameLower.find(L"preview") != std::wstring::npos ||
            streamNameLower.find(L"graphics") != std::wstring::npos)
        {
            if (ParseStructuredSwDisplayList(pay, payLen, mesh)) return;
            if (didInflate) ParseStructuredSwDisplayList(sPtr, sLen, mesh);
        }
    };

    // 1. OLE2 Compound File (.sldprt / .sldasm)
    Ole2::CompoundFile cf;
    if (Ole2::ParseCompoundFile(data, size, cf)) {
        for (const auto& entry : cf.entries) {
            if (entry.objectType != 2 || entry.size == 0) continue;
            std::vector<uint8_t> rawStream;
            if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;
            processNamedStream(PathUtils::ToLower(entry.name), rawStream.data(), rawStream.size());
        }
    }
    // 2. SolidWorks 2015+ G3 / 3DExperience ZIP-based package
    else if (size >= 30 && data[0] == 'P' && data[1] == 'K') {
        std::vector<ZipDeflate::ArchiveItemMeta> zEntries;
        if (ZipDeflate::ListZipEntriesFromMemory(data, size, zEntries)) {
            for (const auto& ze : zEntries) {
                if (ze.isDirectory) continue;
                std::vector<uint8_t> zBytes;
                if (ZipDeflate::ExtractZipEntryFromMemory(data, size, ze, zBytes, 64 * 1024 * 1024) && !zBytes.empty()) {
                    processNamedStream(PathUtils::ToLower(ze.pathInArchive), zBytes.data(), zBytes.size());
                }
            }
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".sldasm")
        ? L"Dassault SolidWorks 3D Assembly (.sldasm)"
        : L"Dassault SolidWorks 3D Part (.sldprt)";
    return true;
}

} // namespace Formats
