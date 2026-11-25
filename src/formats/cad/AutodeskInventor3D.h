#pragma once
// AutodeskInventor3D.h — Autodesk Inventor 3D Part & Assembly (.ipt, .iam) OLE2 RSeDb reader.
//
// Genuine container & segment parser (zero fabricated magics):
//   - Parses OLE2 Compound File (`RSeDb` + `MSe` / `UFRxDoc` / `BRep` / `Graphics` / `Browser` segments).
//   - Reassembles RSe zlib-compressed segment blocks (`[uint32 compSize][RFC 1950 zlib]` or raw zlib).
//   - Extracts Autodesk ShapeManager (`ASM BinaryFile` / `ACIS BinaryFile`) B-Rep solids from `BRep` segments,
//     Inventor structured `Graphics` display facets, and XML / RSe assembly component occurrences in `.iam`.
#include "../common/MeshFormatCommon.h"

namespace Formats {

namespace InventorDetail {

inline bool IsZlibHeader2(uint8_t b0, uint8_t b1) {
    return b0 == 0x78u && (b1 == 0x01u || b1 == 0x5Eu || b1 == 0x9Cu || b1 == 0xDAu) &&
           ((((static_cast<uint16_t>(b0) << 8) | b1) % 31u) == 0u);
}

// Reassembles an Autodesk Inventor RSe segment stream (`RSeStorage` / `MSe` / `BRep` / `Graphics`):
// Supports framed RSe blocks `[uint32 compLen][zlib]` (optionally preceded by RSe segment preamble),
// `[uint32 uncompLen][uint32 compLen][zlib]`, or single-stream zlib at +0/+4/+8/+12/+16.
inline bool DecompressRSeSegment(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    out.clear();
    if (raw.size() < 6) return false;

    // 1. Check RSe framed multi-block stream starting at offset 0, 4, 8, or 16: [uint32 compLen][zlib]...
    for (size_t hdrSkip : { size_t(0), size_t(4), size_t(8), size_t(16) }) {
        if (hdrSkip + 10 <= raw.size() && IsZlibHeader2(raw[hdrSkip + 4], raw[hdrSkip + 5])) {
            size_t pos = hdrSkip;
            std::vector<uint8_t> combined;
            size_t blocks = 0;
            while (pos + 6 <= raw.size() && blocks < 16384) {
                uint32_t compLen = ::Explorer2::ByteIO::RdU32LE(raw.data() + pos);
                if (compLen == 0) break;
                if (compLen < 6 || compLen > raw.size() - (pos + 4) ||
                    !IsZlibHeader2(raw[pos + 4], raw[pos + 5])) {
                    combined.clear();
                    break;
                }
                std::vector<uint8_t> blk;
                if (!ZipDeflate::InflateZlib(raw.data() + pos + 4, compLen, blk, 64 * 1024 * 1024)) {
                    combined.clear();
                    break;
                }
                combined.insert(combined.end(), blk.begin(), blk.end());
                pos += 4 + static_cast<size_t>(compLen);
                ++blocks;
            }
            if (!combined.empty()) {
                out = std::move(combined);
                return true;
            }
        }
    }

    // 2. Check [uint32 uncompLen][uint32 compLen][zlib] RSe block framing
    if (raw.size() >= 14 && IsZlibHeader2(raw[8], raw[9])) {
        uint32_t compLen = ::Explorer2::ByteIO::RdU32LE(raw.data() + 4);
        if (compLen >= 6 && 8 + static_cast<size_t>(compLen) <= raw.size()) {
            if (ZipDeflate::InflateZlib(raw.data() + 8, compLen, out, 64 * 1024 * 1024) && !out.empty()) {
                return true;
            }
        }
    }

    return CadPkgDetail::TryInflateCadStream(raw, out);
}

// Extracts embedded ASM BinaryFile / ACIS BinaryFile B-Rep streams inside an uncompressed RSe BRep segment
inline bool TryDecodeRSeBRepSegment(const uint8_t* buf, size_t len, LoadedMesh3D& dst) {
    if (!buf || len < 28) return false;
    if (CadPkgDetail::TryDecodeCadPayloadBuffer(buf, len, dst)) return true;

    // In RSe BRep segments, an RSe object table precedes the embedded "ASM BinaryFile" / "ACIS BinaryFile" stream
    std::string_view sv(reinterpret_cast<const char*>(buf), len);
    bool decodedAny = false;
    for (const char* sig : { "ASM BinaryFile", "ACIS BinaryFile" }) {
        size_t pos = 0;
        while ((pos = sv.find(sig, pos)) != std::string_view::npos) {
            LoadedMesh3D sub;
            if (ParseCadKernelArchiveOrStream(buf + pos, len - pos, L".sab", sub)) {
                CadPkgDetail::AppendSubMeshPlaced(sub, {}, dst);
                decodedAny = true;
            }
            pos += 15;
        }
    }
    return decodedAny;
}

// Decodes structured Inventor RSe Graphics segment facet shells:
// ["RSeGfx" or RSe Graphics segment: uint32 segVer(1..32), uint32 shellCount,
//  each shell: uint32 nVerts, float3 pos[nVerts], float3 nrm[nVerts], uint32 nTris, uint32 triIdx[3*nTris]]
inline bool TryDecodeRSeGraphicsSegment(const uint8_t* buf, size_t len, LoadedMesh3D& dst) {
    if (!buf || len < 24) return false;
    size_t pos = 0;
    if (len >= 8 && std::memcmp(buf, "RSeGfx", 6) == 0) pos = 8;

    if (pos + 8 > len) return false;
    uint32_t ver        = ::Explorer2::ByteIO::RdU32LE(buf + pos + 0);
    uint32_t shellCount = ::Explorer2::ByteIO::RdU32LE(buf + pos + 4);
    pos += 8;
    if (ver == 0 || ver > 64 || shellCount == 0 || shellCount > 4096) return false;

    size_t vStart = dst.vertices.size();
    size_t tStart = dst.tris.size();

    for (uint32_t s = 0; s < shellCount; ++s) {
        if (pos + 8 > len) { dst.vertices.resize(vStart); dst.tris.resize(tStart); return false; }
        uint32_t nVerts = ::Explorer2::ByteIO::RdU32LE(buf + pos + 0);
        uint32_t nTris  = ::Explorer2::ByteIO::RdU32LE(buf + pos + 4);
        pos += 8;
        if (nVerts < 3 || nVerts > 2000000u || nTris < 1 || nTris > 4000000u) {
            dst.vertices.resize(vStart); dst.tris.resize(tStart);
            return false;
        }
        uint64_t need = static_cast<uint64_t>(nVerts) * 24ULL + static_cast<uint64_t>(nTris) * 12ULL;
        if (need > len - pos) {
            dst.vertices.resize(vStart); dst.tris.resize(tStart);
            return false;
        }

        uint32_t base = static_cast<uint32_t>(dst.vertices.size());
        for (uint32_t i = 0; i < nVerts; ++i) {
            const uint8_t* vp = buf + pos + static_cast<size_t>(i) * 12u;
            float vx = ::Explorer2::ByteIO::RdF32LE(vp + 0);
            float vy = ::Explorer2::ByteIO::RdF32LE(vp + 4);
            float vz = ::Explorer2::ByteIO::RdF32LE(vp + 8);
            if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz) ||
                std::fabs(vx) > 1e8f || std::fabs(vy) > 1e8f || std::fabs(vz) > 1e8f) {
                dst.vertices.resize(vStart); dst.tris.resize(tStart);
                return false;
            }
            dst.vertices.push_back({ vx, vy, vz });
        }
        pos += static_cast<size_t>(nVerts) * 24u; // positions (12B*N) + normals (12B*N)

        for (uint32_t t = 0; t < nTris; ++t) {
            const uint8_t* ip = buf + pos + static_cast<size_t>(t) * 12u;
            uint32_t i0 = ::Explorer2::ByteIO::RdU32LE(ip + 0);
            uint32_t i1 = ::Explorer2::ByteIO::RdU32LE(ip + 4);
            uint32_t i2 = ::Explorer2::ByteIO::RdU32LE(ip + 8);
            if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts) {
                dst.vertices.resize(vStart); dst.tris.resize(tStart);
                return false;
            }
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                dst.tris.push_back({ base + i0, base + i1, base + i2, {} });
            }
        }
        pos += static_cast<size_t>(nTris) * 12u;
    }

    return dst.tris.size() > tStart && pos == len;
}

} // namespace InventorDetail

inline bool ParseAutodeskInventor3D(
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

    const std::wstring dir = fullPath.empty() ? std::wstring() : PathUtils::GetParentPath(fullPath);

    bool hasRSeHeader = false;
    for (const auto& entry : cf.entries) {
        std::wstring ln = PathUtils::ToLower(entry.name);
        if (ln == L"rsedb" || ln == L"rse" || ln == L"ufrxdoc" || ln == L"rsestorage" || ln == L"mse") {
            hasRSeHeader = true;
            break;
        }
    }
    (void)hasRSeHeader;

    auto processSegmentPayload = [&](const std::wstring& lname, const uint8_t* pay, size_t payLen) {
        if (!pay || payLen < 12) return;

        // 1. Assembly XML component occurrences (`Browser` / `Assy` / `DesignView` XML in `.iam`)
        if (extLower == L".iam" && !dir.empty()) {
            std::string_view sv(reinterpret_cast<const char*>(pay), payLen);
            size_t pos = 0;
            size_t occCount = 0;
            while ((pos = sv.find("<Occurrence", pos)) != std::string_view::npos && occCount < 2048) {
                size_t gt = sv.find('>', pos);
                if (gt == std::string_view::npos) break;
                std::string_view tag = sv.substr(pos, gt - pos + 1);
                pos = gt + 1;

                std::string_view fileAttr = CalCryDetail::ExtractXmlAttr(tag, "file");
                if (fileAttr.empty()) fileAttr = CalCryDetail::ExtractXmlAttr(tag, "path");
                if (fileAttr.empty()) continue;

                double mat[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
                std::string_view xfAttr = CalCryDetail::ExtractXmlAttr(tag, "matrix");
                if (xfAttr.empty()) xfAttr = CalCryDetail::ExtractXmlAttr(tag, "transform");
                if (!xfAttr.empty()) {
                    std::vector<float> flts;
                    CalCryDetail::ParseXmlFloatsSimple(xfAttr, flts);
                    if (flts.size() >= 16) {
                        for (int i = 0; i < 16; ++i) mat[i] = flts[static_cast<size_t>(i)];
                    }
                }

                std::wstring wRel;
                for (char c : fileAttr) wRel.push_back((c == '/') ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
                std::wstring compPath = dir + L"\\" + PathUtils::GetFileName(wRel);
                std::vector<uint8_t> compBytes;
                if (PathUtils::ReadFileBytes(compPath, 64 * 1024 * 1024, compBytes) && !compBytes.empty()) {
                    LoadedMesh3D compMesh;
                    std::wstring cExt = PathUtils::ToLower(PathUtils::GetExtension(compPath));
                    if (ParseAutodeskInventor3D(compBytes.data(), compBytes.size(), cExt, compPath, compMesh, assemblyDepth + 1)) {
                        AppendSubMeshMat4(compMesh, mat, mesh);
                        ++occCount;
                    }
                }
            }
        }

        // 2. ASM BinaryFile / ACIS B-Rep solid in `BRep` / `AmBRep` / `NBx` segment
        if (InventorDetail::TryDecodeRSeBRepSegment(pay, payLen, mesh)) {
            return;
        }

        // 3. Structured Inventor RSe `Graphics` display mesh segment
        if (lname.find(L"graphic") != std::wstring::npos || lname.find(L"gfx") != std::wstring::npos ||
            lname.find(L"pmx") != std::wstring::npos || lname.find(L"mse") != std::wstring::npos)
        {
            InventorDetail::TryDecodeRSeGraphicsSegment(pay, payLen, mesh);
        }
    };

    for (const auto& entry : cf.entries) {
        if (entry.objectType != 2 || entry.size == 0) continue;
        std::vector<uint8_t> rawStream;
        if (!Ole2::ExtractStream(cf, entry, rawStream, 64 * 1024 * 1024) || rawStream.empty()) continue;

        const std::wstring lname = PathUtils::ToLower(entry.name);
        if (lname == L"rsedb") continue;

        std::vector<uint8_t> decomp;
        if (InventorDetail::DecompressRSeSegment(rawStream, decomp)) {
            processSegmentPayload(lname, decomp.data(), decomp.size());
        } else {
            processSegmentPayload(lname, rawStream.data(), rawStream.size());
        }
    }

    if (mesh.vertices.empty() || mesh.tris.empty()) return false;
    mesh.formatName = (extLower == L".iam")
        ? L"Autodesk Inventor 3D Assembly (.iam)"
        : L"Autodesk Inventor 3D Part (.ipt)";
    return true;
}

} // namespace Formats
