#pragma once
#include "../Lz4Block.h"
// VtkXml3D.h — #18 Visualization Toolkit XML Scientific / FEM Suite
//              (.vtu, .vtp, .vti, .vtr, .vts, and ParaView Collection .pvd)
//
// Pure C++20 zero-cheat implementation:
//   - Parses <VTKFile> XML schemas: UnstructuredGrid (.vtu), PolyData (.vtp), ImageData (.vti),
//     RectilinearGrid (.vtr), StructuredGrid (.vts), and ParaView Data Collection (.pvd).
//   - Supports ASCII, inline Base64 (both single-stream and VTK split-header "=" padded Base64),
//     AppendedData raw ("_..."), and AppendedData base64 ("_...").
//   - Supports UInt32 and UInt64 header_type, LittleEndian and BigEndian byte_order,
//     uncompressed, vtkZLibDataCompressor (RFC 1950), and vtkLZ4DataCompressor blocks.
//   - Extracts exterior FEM boundary surfaces (culling shared interior faces) across Tetrahedra,
//     Hexahedra, Voxels, Wedges, Pyramids, Triangles, Quads, Polygons, Strips, Lines, and Iso-Volumes.

namespace Formats {

namespace VtkXmlDetail {

inline std::string GetXmlAttr(std::string_view tag, std::string_view attrName) {
    std::string_view v;
    return ::Explorer2::XmlTagScan::FindAttr(tag, attrName, v, false) ? std::string(v) : std::string();
}

// Decodes VTK Base64 streams, explicitly supporting mid-stream '=' padding (VTK's split header+payload Base64)
// VTK writes each data block (header, then data) as its own padded base64 run
inline bool DecodeVtkBase64(std::string_view b64, std::vector<uint8_t>& out, size_t maxBytes = 128 * 1024 * 1024) {
    return ::Explorer2::Base64::Decode(b64, out, ::Explorer2::Base64::Mode::Concatenated, maxBytes);
}

enum class VtkXmlCompressor { None, Zlib, Lz4 };

struct VtkXmlDocConfig {
    bool bigEndian = false;
    bool hdr64 = false;
    VtkXmlCompressor comp = VtkXmlCompressor::None;
    bool appendedRaw = false;
    const uint8_t* appendedPtr = nullptr;
    size_t appendedLen = 0;
    std::string appendedBase64Clean;
};

inline uint64_t RdHdrWord(const uint8_t* p, bool bigEndian, bool hdr64) {
    if (hdr64) {
        return bigEndian ? DetailVtk::RdBE64Vtk(p) : Detail3D100::RdU64LE(p);
    }
    return bigEndian ? DetailVtk::RdBE32Vtk(p) : Detail3D100::RdU32LE(p);
}

inline bool UnpackFramedVtkPayload(
    const uint8_t* framed,
    size_t framedLen,
    const VtkXmlDocConfig& cfg,
    std::vector<uint8_t>& outRaw)
{
    outRaw.clear();
    const size_t wSz = cfg.hdr64 ? 8u : 4u;
    if (!framed || framedLen < wSz) return false;

    if (cfg.comp == VtkXmlCompressor::None) {
        uint64_t byteCnt = RdHdrWord(framed, cfg.bigEndian, cfg.hdr64);
        if (byteCnt > 128 * 1024 * 1024 || wSz + byteCnt > framedLen) return false;
        outRaw.assign(framed + wSz, framed + wSz + static_cast<size_t>(byteCnt));
        return true;
    }

    // Compressed framing: [numBlocks][blockSize][lastBlockSize][compSize_0 ... compSize_{numBlocks-1}][blocks...]
    if (framedLen < 3 * wSz) return false;
    uint64_t numBlocks = RdHdrWord(framed + 0 * wSz, cfg.bigEndian, cfg.hdr64);
    uint64_t blockSize = RdHdrWord(framed + 1 * wSz, cfg.bigEndian, cfg.hdr64);
    uint64_t lastSize  = RdHdrWord(framed + 2 * wSz, cfg.bigEndian, cfg.hdr64);
    if (numBlocks == 0) return true;
    if (numBlocks > 262144 || blockSize > 64 * 1024 * 1024 || lastSize > 64 * 1024 * 1024) return false;
    if ((3 + numBlocks) * wSz > framedLen) return false;

    uint64_t totalOut = (numBlocks - 1) * blockSize + ((lastSize > 0) ? lastSize : blockSize);
    if (totalOut > 128 * 1024 * 1024) return false;
    outRaw.resize(static_cast<size_t>(totalOut));

    size_t sp = static_cast<size_t>((3 + numBlocks) * wSz);
    size_t dp = 0;
    for (uint64_t b = 0; b < numBlocks; ++b) {
        uint64_t cSz = RdHdrWord(framed + static_cast<size_t>((3 + b) * wSz), cfg.bigEndian, cfg.hdr64);
        uint64_t uSz = (b + 1 < numBlocks) ? blockSize : ((lastSize > 0) ? lastSize : blockSize);
        if (sp + cSz > framedLen || dp + uSz > outRaw.size()) return false;

        if (cfg.comp == VtkXmlCompressor::Zlib) {
            std::vector<uint8_t> blk;
            if (!ZipDeflate::InflateZlib(framed + sp, static_cast<size_t>(cSz), blk, static_cast<size_t>(uSz) + 64) ||
                blk.size() < static_cast<size_t>(uSz)) return false;
            std::memcpy(outRaw.data() + dp, blk.data(), static_cast<size_t>(uSz));
        } else if (cfg.comp == VtkXmlCompressor::Lz4) {
            std::vector<uint8_t> blk;
            if (!::Explorer2::Lz4::DecodeBlockExact(framed + sp, static_cast<size_t>(cSz), static_cast<size_t>(uSz), blk))
                return false;
            std::memcpy(outRaw.data() + dp, blk.data(), static_cast<size_t>(uSz));
        }
        sp += static_cast<size_t>(cSz);
        dp += static_cast<size_t>(uSz);
    }
    return true;
}

inline DetailVtk::VtkScalarKind ParseXmlTypeAttr(std::string_view t) {
    std::string low = DetailVtk::ToLowerVtk(t);
    if (low == "float32") return DetailVtk::VtkScalarKind::F32;
    if (low == "float64") return DetailVtk::VtkScalarKind::F64;
    if (low == "int8")    return DetailVtk::VtkScalarKind::I8;
    if (low == "uint8")   return DetailVtk::VtkScalarKind::U8;
    if (low == "int16")   return DetailVtk::VtkScalarKind::I16;
    if (low == "uint16")  return DetailVtk::VtkScalarKind::U16;
    if (low == "int32")   return DetailVtk::VtkScalarKind::I32;
    if (low == "uint32")  return DetailVtk::VtkScalarKind::U32;
    if (low == "int64")   return DetailVtk::VtkScalarKind::I64;
    if (low == "uint64")  return DetailVtk::VtkScalarKind::U64;
    return DetailVtk::VtkScalarKind::Invalid;
}

inline double RdXmlScalar(const uint8_t* p, DetailVtk::VtkScalarKind k, bool bigEndian) {
    if (bigEndian) return DetailVtk::RdBinDoubleVtk(p, k);
    switch (k) {
    case DetailVtk::VtkScalarKind::F32: { float f = 0; std::memcpy(&f, p, 4); return f; }
    case DetailVtk::VtkScalarKind::F64: { double d = 0; std::memcpy(&d, p, 8); return d; }
    case DetailVtk::VtkScalarKind::I8:  { int8_t v = 0; std::memcpy(&v, p, 1); return v; }
    case DetailVtk::VtkScalarKind::U8:  return p[0];
    case DetailVtk::VtkScalarKind::I16: { int16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::U16: { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::I32: { int32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::U32: { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::I64: { int64_t v = 0; std::memcpy(&v, p, 8); return static_cast<double>(v); }
    case DetailVtk::VtkScalarKind::U64: { uint64_t v = 0; std::memcpy(&v, p, 8); return static_cast<double>(v); }
    default: return 0.0;
    }
}

inline int64_t RdXmlInt(const uint8_t* p, DetailVtk::VtkScalarKind k, bool bigEndian) {
    if (bigEndian) return DetailVtk::RdBinIntVtk(p, k);
    switch (k) {
    case DetailVtk::VtkScalarKind::I8:  { int8_t v = 0; std::memcpy(&v, p, 1); return v; }
    case DetailVtk::VtkScalarKind::U8:  return p[0];
    case DetailVtk::VtkScalarKind::I16: { int16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::U16: { uint16_t v = 0; std::memcpy(&v, p, 2); return v; }
    case DetailVtk::VtkScalarKind::I32: { int32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::U32: { uint32_t v = 0; std::memcpy(&v, p, 4); return v; }
    case DetailVtk::VtkScalarKind::I64: { int64_t v = 0; std::memcpy(&v, p, 8); return v; }
    case DetailVtk::VtkScalarKind::U64: { uint64_t v = 0; std::memcpy(&v, p, 8); return static_cast<int64_t>(v); }
    default: return static_cast<int64_t>(std::llround(RdXmlScalar(p, k, false)));
    }
}

inline bool DecodeDataArrayDoubles(
    std::string_view openTag,
    std::string_view innerText,
    const VtkXmlDocConfig& cfg,
    std::vector<double>& outVals)
{
    outVals.clear();
    auto kind = ParseXmlTypeAttr(GetXmlAttr(openTag, "type"));
    if (kind == DetailVtk::VtkScalarKind::Invalid) return false;
    std::string fmt = DetailVtk::ToLowerVtk(GetXmlAttr(openTag, "format"));
    if (fmt.empty()) fmt = "ascii";

    if (fmt == "ascii") {
        const char* cur = innerText.data();
        const char* end = cur + innerText.size();
        double v = 0.0;
        while (DetailVtk::NextDoubleAsciiVtk(cur, end, v)) {
            outVals.push_back(v);
            if (outVals.size() > 30000000) return false;
        }
        return true;
    }

    std::vector<uint8_t> raw;
    if (fmt == "binary") {
        std::vector<uint8_t> framed;
        if (!DecodeVtkBase64(innerText, framed) || framed.empty()) return false;
        if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
    } else if (fmt == "appended") {
        std::string offStr = GetXmlAttr(openTag, "offset");
        if (offStr.empty()) return false;
        uint64_t off = std::strtoull(offStr.c_str(), nullptr, 10);
        if (cfg.appendedRaw) {
            if (!cfg.appendedPtr || off >= cfg.appendedLen) return false;
            if (!UnpackFramedVtkPayload(cfg.appendedPtr + off, cfg.appendedLen - static_cast<size_t>(off), cfg, raw)) return false;
        } else {
            if (off >= cfg.appendedBase64Clean.size()) return false;
            std::string_view tail(cfg.appendedBase64Clean.data() + static_cast<size_t>(off),
                                  cfg.appendedBase64Clean.size() - static_cast<size_t>(off));
            std::vector<uint8_t> framed;
            if (!DecodeVtkBase64(tail, framed) || framed.empty()) return false;
            if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
        }
    } else {
        return false;
    }

    size_t bsz = DetailVtk::VtkScalarByteSize(kind);
    if (bsz == 0 || (raw.size() % bsz) != 0) return false;
    size_t n = raw.size() / bsz;
    outVals.resize(n);
    for (size_t i = 0; i < n; ++i) {
        outVals[i] = RdXmlScalar(raw.data() + i * bsz, kind, cfg.bigEndian);
    }
    return true;
}

inline bool DecodeDataArrayInts(
    std::string_view openTag,
    std::string_view innerText,
    const VtkXmlDocConfig& cfg,
    std::vector<int64_t>& outVals)
{
    outVals.clear();
    auto kind = ParseXmlTypeAttr(GetXmlAttr(openTag, "type"));
    if (kind == DetailVtk::VtkScalarKind::Invalid) return false;
    std::string fmt = DetailVtk::ToLowerVtk(GetXmlAttr(openTag, "format"));
    if (fmt.empty()) fmt = "ascii";

    if (fmt == "ascii") {
        const char* cur = innerText.data();
        const char* end = cur + innerText.size();
        int64_t v = 0;
        while (DetailVtk::NextIntAsciiVtk(cur, end, v)) {
            outVals.push_back(v);
            if (outVals.size() > 30000000) return false;
        }
        return true;
    }

    std::vector<uint8_t> raw;
    if (fmt == "binary") {
        std::vector<uint8_t> framed;
        if (!DecodeVtkBase64(innerText, framed) || framed.empty()) return false;
        if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
    } else if (fmt == "appended") {
        std::string offStr = GetXmlAttr(openTag, "offset");
        if (offStr.empty()) return false;
        uint64_t off = std::strtoull(offStr.c_str(), nullptr, 10);
        if (cfg.appendedRaw) {
            if (!cfg.appendedPtr || off >= cfg.appendedLen) return false;
            if (!UnpackFramedVtkPayload(cfg.appendedPtr + off, cfg.appendedLen - static_cast<size_t>(off), cfg, raw)) return false;
        } else {
            if (off >= cfg.appendedBase64Clean.size()) return false;
            std::string_view tail(cfg.appendedBase64Clean.data() + static_cast<size_t>(off),
                                  cfg.appendedBase64Clean.size() - static_cast<size_t>(off));
            std::vector<uint8_t> framed;
            if (!DecodeVtkBase64(tail, framed) || framed.empty()) return false;
            if (!UnpackFramedVtkPayload(framed.data(), framed.size(), cfg, raw)) return false;
        }
    } else {
        return false;
    }

    size_t bsz = DetailVtk::VtkScalarByteSize(kind);
    if (bsz == 0 || (raw.size() % bsz) != 0) return false;
    size_t n = raw.size() / bsz;
    outVals.resize(n);
    for (size_t i = 0; i < n; ++i) {
        outVals[i] = RdXmlInt(raw.data() + i * bsz, kind, cfg.bigEndian);
    }
    return true;
}

// Enumerates <DataArray ...>inner</DataArray> or <DataArray .../> inside an XML section
template <typename Callback>
inline void ForEachDataArrayInSection(std::string_view sec, Callback&& cb) {
    size_t pos = 0;
    while ((pos = sec.find("<DataArray", pos)) != std::string_view::npos) {
        size_t gt = sec.find('>', pos + 10);
        if (gt == std::string_view::npos) break;
        std::string_view openTag = sec.substr(pos, gt - pos + 1);
        if (gt >= 1 && sec[gt - 1] == '/') {
            cb(openTag, std::string_view{});
            pos = gt + 1;
        } else {
            size_t closePos = sec.find("</DataArray>", gt + 1);
            if (closePos == std::string_view::npos) break;
            cb(openTag, sec.substr(gt + 1, closePos - (gt + 1)));
            pos = closePos + 12;
        }
    }
}

inline std::string_view FindXmlElementBody(std::string_view doc, std::string_view elemName, std::string_view* outOpenTag = nullptr) {
    std::string needle = "<" + std::string(elemName);
    size_t p = 0;
    while ((p = doc.find(needle, p)) != std::string_view::npos) {
        size_t after = p + needle.size();
        if (after < doc.size() && (std::isspace(static_cast<unsigned char>(doc[after])) || doc[after] == '>' || doc[after] == '/')) {
            size_t gt = doc.find('>', after);
            if (gt == std::string_view::npos) return {};
            if (outOpenTag) *outOpenTag = doc.substr(p, gt - p + 1);
            if (doc[gt - 1] == '/') return std::string_view(doc.data() + gt, 0);
            std::string closeTag = "</" + std::string(elemName) + ">";
            size_t cp = doc.find(closeTag, gt + 1);
            if (cp == std::string_view::npos) return {};
            return doc.substr(gt + 1, cp - (gt + 1));
        }
        p = after;
    }
    return {};
}

inline bool BuildXmlCellList(
    std::string_view sectionXml,
    const VtkXmlDocConfig& cfg,
    DetailVtk::VtkCellList& outList,
    std::vector<int64_t>* outTypes = nullptr)
{
    outList.offsets.clear();
    outList.conn.clear();
    if (outTypes) outTypes->clear();
    if (sectionXml.empty()) return false;

    std::vector<int64_t> rawConn, rawOff;
    ForEachDataArrayInSection(sectionXml, [&](std::string_view oTag, std::string_view inner) {
        std::string nm = DetailVtk::ToLowerVtk(GetXmlAttr(oTag, "Name"));
        if (nm == "connectivity") {
            DecodeDataArrayInts(oTag, inner, cfg, rawConn);
        } else if (nm == "offsets") {
            DecodeDataArrayInts(oTag, inner, cfg, rawOff);
        } else if (nm == "types" && outTypes) {
            DecodeDataArrayInts(oTag, inner, cfg, *outTypes);
        }
    });

    if (rawConn.empty() || rawOff.empty()) return false;
    outList.offsets.reserve(rawOff.size() + 1);
    if (rawOff[0] != 0) outList.offsets.push_back(0);
    for (int64_t o : rawOff) {
        if (o < 0 || static_cast<size_t>(o) > rawConn.size()) return false;
        if (!outList.offsets.empty() && static_cast<size_t>(o) < outList.offsets.back()) return false;
        outList.offsets.push_back(static_cast<size_t>(o));
    }
    outList.conn.reserve(rawConn.size());
    for (int64_t idx : rawConn) {
        if (idx < 0) return false;
        outList.conn.push_back(static_cast<uint32_t>(idx));
    }
    return outList.offsets.size() >= 2;
}

inline bool ParseExtent6(std::string_view s, int ext[6]) {
    const char* cur = s.data();
    const char* end = cur + s.size();
    for (int i = 0; i < 6; ++i) {
        int64_t v = 0;
        if (!DetailVtk::NextIntAsciiVtk(cur, end, v)) return false;
        ext[i] = static_cast<int>(v);
    }
    return ext[1] >= ext[0] && ext[3] >= ext[2] && ext[5] >= ext[4];
}

inline bool ParseDoublesN(std::string_view s, double* dst, int n) {
    const char* cur = s.data();
    const char* end = cur + s.size();
    for (int i = 0; i < n; ++i) {
        if (!DetailVtk::NextDoubleAsciiVtk(cur, end, dst[i])) return false;
    }
    return true;
}

} // namespace VtkXmlDetail

inline bool ParseVtkXmlMesh(
    const uint8_t* data,
    size_t size,
    const std::wstring& extLower,
    const std::wstring& fullPath,
    LoadedMesh3D& mesh,
    int pvdDepth = 0)
{
    using namespace VtkXmlDetail;
    if (!data || size < 32 || pvdDepth > 2) return false;

    std::string_view fullSv(reinterpret_cast<const char*>(data), size);
    size_t vfPos = fullSv.find("<VTKFile");
    if (vfPos == std::string_view::npos || vfPos > 4096) return false;
    size_t vfGt = fullSv.find('>', vfPos);
    if (vfGt == std::string_view::npos) return false;
    std::string_view vfTag = fullSv.substr(vfPos, vfGt - vfPos + 1);

    std::string rootType = GetXmlAttr(vfTag, "type");
    if (rootType.empty()) return false;

    // 1. ParaView Data Collection (.pvd): <VTKFile type="Collection">
    if (rootType == "Collection") {
        if (fullPath.empty()) return false;
        std::wstring baseDir = PathUtils::GetParentPath(fullPath);
        std::string_view collXml = FindXmlElementBody(fullSv, "Collection");
        if (collXml.empty()) return false;

        struct PvdEntry { double ts = 0.0; std::string file; };
        std::vector<PvdEntry> dsList;
        std::set<double> distinctTimesteps;

        size_t dp = 0;
        while ((dp = collXml.find("<DataSet", dp)) != std::string_view::npos) {
            size_t gt = collXml.find('>', dp + 8);
            if (gt == std::string_view::npos) break;
            std::string_view dTag = collXml.substr(dp, gt - dp + 1);
            std::string fAttr = GetXmlAttr(dTag, "file");
            std::string tsAttr = GetXmlAttr(dTag, "timestep");
            double ts = tsAttr.empty() ? 0.0 : std::strtod(tsAttr.c_str(), nullptr);
            if (!fAttr.empty()) {
                dsList.push_back({ ts, fAttr });
                distinctTimesteps.insert(ts);
            }
            dp = gt + 1;
        }
        if (dsList.empty()) return false;

        mesh.vertices.clear();
        mesh.tris.clear();
        double targetTs = dsList[0].ts;
        for (const auto& de : dsList) {
            if (std::fabs(de.ts - targetTs) > 1e-9) continue; // Load all multi-block parts of first timestep
            std::wstring wRel;
            for (char c : de.file) wRel.push_back((c == '/') ? L'\\' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
            std::wstring subPath = baseDir + L"\\" + wRel;
            std::vector<uint8_t> subBytes;
            if (!PathUtils::ReadFileBytes(subPath, 128 * 1024 * 1024, subBytes) || subBytes.empty()) continue;
            LoadedMesh3D partMesh;
            std::wstring subExt = PathUtils::ToLower(PathUtils::GetExtension(subPath));
            if (ParseVtkXmlMesh(subBytes.data(), subBytes.size(), subExt, subPath, partMesh, pvdDepth + 1) &&
                !partMesh.vertices.empty() && !partMesh.tris.empty())
            {
                uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), partMesh.vertices.begin(), partMesh.vertices.end());
                for (const auto& t : partMesh.tris) {
                    mesh.tris.push_back({ base + t.i0, base + t.i1, base + t.i2, {} });
                }
            }
        }
        if (mesh.vertices.empty() || mesh.tris.empty()) return false;
        mesh.animFrameCount = distinctTimesteps.size();
        mesh.formatName = L"ParaView Data Multi-Block Collection (.pvd)";
        return true;
    }

    // 2. Configure endianness, header word size, compressor, and AppendedData section
    VtkXmlDocConfig cfg;
    cfg.bigEndian = (GetXmlAttr(vfTag, "byte_order") == "BigEndian");
    cfg.hdr64     = (GetXmlAttr(vfTag, "header_type") == "UInt64");
    std::string compStr = GetXmlAttr(vfTag, "compressor");
    if (compStr.find("ZLib") != std::string::npos)      cfg.comp = VtkXmlCompressor::Zlib;
    else if (compStr.find("LZ4") != std::string::npos)  cfg.comp = VtkXmlCompressor::Lz4;
    else if (!compStr.empty())                          return false; // Unsupported compressor (e.g. LZMA without library)

    // Locate <AppendedData ...>_...</AppendedData> carefully (raw bytes may contain anything!)
    size_t xmlLimit = size;
    size_t appPos = fullSv.find("<AppendedData");
    if (appPos != std::string_view::npos) {
        xmlLimit = appPos;
        size_t appGt = fullSv.find('>', appPos);
        if (appGt == std::string_view::npos) return false;
        std::string_view appTag = fullSv.substr(appPos, appGt - appPos + 1);
        std::string enc = DetailVtk::ToLowerVtk(GetXmlAttr(appTag, "encoding"));
        size_t underscore = fullSv.find('_', appGt + 1);
        size_t closeApp = fullSv.rfind("</AppendedData>");
        if (underscore == std::string_view::npos || closeApp == std::string_view::npos || closeApp <= underscore) return false;

        if (enc == "raw") {
            cfg.appendedRaw = true;
            cfg.appendedPtr = data + underscore + 1;
            cfg.appendedLen = closeApp - (underscore + 1);
        } else {
            cfg.appendedRaw = false;
            cfg.appendedBase64Clean.reserve(closeApp - (underscore + 1));
            for (size_t i = underscore + 1; i < closeApp; ++i) {
                unsigned char c = data[i];
                if (c > 32) cfg.appendedBase64Clean.push_back(static_cast<char>(c));
            }
        }
    }

    std::string_view xmlHead(reinterpret_cast<const char*>(data), xmlLimit);
    mesh.vertices.clear();
    mesh.tris.clear();

    auto decodePointsSection = [&](std::string_view pieceXml, std::vector<Vec3f>& outPts) -> bool {
        outPts.clear();
        std::string_view ptsSec = FindXmlElementBody(pieceXml, "Points");
        if (ptsSec.empty()) return false;
        std::vector<double> coords;
        int nComp = 3;
        ForEachDataArrayInSection(ptsSec, [&](std::string_view oTag, std::string_view inner) {
            if (!coords.empty()) return;
            std::string nc = GetXmlAttr(oTag, "NumberOfComponents");
            if (!nc.empty()) nComp = std::max(1, std::atoi(nc.c_str()));
            DecodeDataArrayDoubles(oTag, inner, cfg, coords);
        });
        if (coords.size() < static_cast<size_t>(nComp)) return false;
        size_t nPts = coords.size() / static_cast<size_t>(nComp);
        outPts.resize(nPts);
        for (size_t i = 0; i < nPts; ++i) {
            double x = coords[i * nComp + 0];
            double y = (nComp >= 2) ? coords[i * nComp + 1] : 0.0;
            double z = (nComp >= 3) ? coords[i * nComp + 2] : 0.0;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            outPts[i] = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
        }
        return !outPts.empty();
    };

    // 3. Dispatch by VTK XML Dataset Schema
    if (rootType == "UnstructuredGrid") {
        std::string_view ugXml = FindXmlElementBody(xmlHead, "UnstructuredGrid");
        if (ugXml.empty()) return false;

        // Loop over all <Piece> blocks inside UnstructuredGrid
        size_t pPos = 0;
        while ((pPos = ugXml.find("<Piece", pPos)) != std::string_view::npos) {
            size_t pGt = ugXml.find('>', pPos);
            size_t pClose = (pGt != std::string_view::npos) ? ugXml.find("</Piece>", pGt) : std::string_view::npos;
            if (pGt == std::string_view::npos || pClose == std::string_view::npos) break;
            std::string_view pieceXml = ugXml.substr(pGt + 1, pClose - (pGt + 1));
            pPos = pClose + 8;

            std::vector<Vec3f> pts;
            DetailVtk::VtkCellList uCells;
            std::vector<int64_t> cellTypes;
            if (!decodePointsSection(pieceXml, pts)) continue;
            if (!BuildXmlCellList(FindXmlElementBody(pieceXml, "Cells"), cfg, uCells, &cellTypes)) continue;

            const size_t numCells = uCells.offsets.size() - 1;
            if (cellTypes.size() < numCells) continue;

            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), pts.begin(), pts.end());
            const size_t nv = pts.size();

            std::unordered_map<DetailVtk::SortedFaceKey, DetailVtk::BoundaryFaceRec, DetailVtk::SortedFaceKeyHash> faceMap;
            faceMap.reserve(numCells * 2);

            for (size_t c = 0; c < numCells; ++c) {
                size_t b = uCells.offsets[c], e = uCells.offsets[c + 1];
                size_t n = e - b;
                const uint32_t* cv = uCells.conn.data() + b;
                bool okIdx = (n > 0);
                for (size_t k = 0; k < n; ++k) { if (cv[k] >= nv) { okIdx = false; break; } }
                if (!okIdx) continue;

                switch (cellTypes[c]) {
                case 10: case 24: // Tetra
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
                case 11: case 12: case 25: case 29: // Voxel / Hexahedron
                    if (n >= 8) {
                        uint32_t h[8];
                        if (cellTypes[c] == 11) {
                            h[0]=cv[0]; h[1]=cv[1]; h[2]=cv[3]; h[3]=cv[2];
                            h[4]=cv[4]; h[5]=cv[5]; h[6]=cv[7]; h[7]=cv[6];
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
                case 13: case 26: // Wedge
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
                case 14: case 27: // Pyramid
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
                case 5: case 22: // Triangle
                    if (n >= 3) DetailVtk::RecordFace3DVtk(faceMap, cv, 3, false);
                    break;
                case 8: // Pixel
                    if (n >= 4) { const uint32_t q[4] = { cv[0], cv[1], cv[3], cv[2] }; DetailVtk::RecordFace3DVtk(faceMap, q, 4, false); }
                    break;
                case 9: case 23: case 28: // Quad
                    if (n >= 4) DetailVtk::RecordFace3DVtk(faceMap, cv, 4, false);
                    break;
                case 7: // Polygon
                    for (size_t k = 1; k + 1 < n; ++k) mesh.tris.push_back({ vBase + cv[0], vBase + cv[k], vBase + cv[k + 1], {} });
                    break;
                default: break;
                }
            }

            for (const auto& kv : faceMap) {
                const auto& f = kv.second;
                if (f.volCount == 1 || (f.volCount == 0 && f.explicit2D)) {
                    if (f.n == 3) {
                        mesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[1], vBase + f.orig[2], {} });
                    } else if (f.n == 4) {
                        mesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[1], vBase + f.orig[2], {} });
                        mesh.tris.push_back({ vBase + f.orig[0], vBase + f.orig[2], vBase + f.orig[3], {} });
                    }
                }
            }
        }
        mesh.formatName = L"VTK XML Unstructured Grid FEM (.vtu)";
    }
    else if (rootType == "PolyData") {
        std::string_view pdXml = FindXmlElementBody(xmlHead, "PolyData");
        if (pdXml.empty()) return false;
        size_t pPos = 0;
        while ((pPos = pdXml.find("<Piece", pPos)) != std::string_view::npos) {
            size_t pGt = pdXml.find('>', pPos);
            size_t pClose = (pGt != std::string_view::npos) ? pdXml.find("</Piece>", pGt) : std::string_view::npos;
            if (pGt == std::string_view::npos || pClose == std::string_view::npos) break;
            std::string_view pieceXml = pdXml.substr(pGt + 1, pClose - (pGt + 1));
            pPos = pClose + 8;

            std::vector<Vec3f> pts;
            if (!decodePointsSection(pieceXml, pts)) continue;
            uint32_t vBase = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.insert(mesh.vertices.end(), pts.begin(), pts.end());
            size_t nv = pts.size();

            DetailVtk::VtkCellList polys, strips, lines;
            if (BuildXmlCellList(FindXmlElementBody(pieceXml, "Polys"), cfg, polys)) {
                for (size_t c = 0; c + 1 < polys.offsets.size(); ++c) {
                    size_t b = polys.offsets[c], e = polys.offsets[c + 1];
                    if (e - b >= 3 && polys.conn[b] < nv) {
                        for (size_t k = b + 1; k + 1 < e; ++k) {
                            if (polys.conn[k] < nv && polys.conn[k + 1] < nv) {
                                mesh.tris.push_back({ vBase + polys.conn[b], vBase + polys.conn[k], vBase + polys.conn[k + 1], {} });
                            }
                        }
                    }
                }
            }
            if (BuildXmlCellList(FindXmlElementBody(pieceXml, "Strips"), cfg, strips)) {
                for (size_t c = 0; c + 1 < strips.offsets.size(); ++c) {
                    size_t b = strips.offsets[c], e = strips.offsets[c + 1];
                    for (size_t k = 0; b + k + 2 < e; ++k) {
                        uint32_t i0 = strips.conn[b + k], i1 = strips.conn[b + k + 1], i2 = strips.conn[b + k + 2];
                        if (i0 < nv && i1 < nv && i2 < nv && i0 != i1 && i1 != i2 && i0 != i2) {
                            if ((k & 1u) == 0u) mesh.tris.push_back({ vBase + i0, vBase + i1, vBase + i2, {} });
                            else                mesh.tris.push_back({ vBase + i1, vBase + i0, vBase + i2, {} });
                        }
                    }
                }
            }
            if (mesh.tris.empty() && BuildXmlCellList(FindXmlElementBody(pieceXml, "Lines"), cfg, lines)) {
                mesh.vertices.resize(vBase);
                for (size_t c = 0; c + 1 < lines.offsets.size(); ++c) {
                    size_t b = lines.offsets[c], e = lines.offsets[c + 1];
                    for (size_t k = b; k + 1 < e; ++k) {
                        if (lines.conn[k] < nv && lines.conn[k + 1] < nv) {
                            MeshBuild3D::EmitEdgeStrut(mesh, pts[lines.conn[k]], pts[lines.conn[k + 1]], 0.05f);
                        }
                    }
                }
            }
        }
        mesh.formatName = L"VTK XML PolyData Surface (.vtp)";
    }
    else if (rootType == "ImageData") {
        std::string_view openTag;
        std::string_view imXml = FindXmlElementBody(xmlHead, "ImageData", &openTag);
        if (imXml.empty()) return false;
        int ext[6] = {};
        if (!ParseExtent6(GetXmlAttr(openTag, "WholeExtent"), ext)) return false;
        double origin[3] = { 0, 0, 0 }, spacing[3] = { 1, 1, 1 };
        ParseDoublesN(GetXmlAttr(openTag, "Origin"), origin, 3);
        ParseDoublesN(GetXmlAttr(openTag, "Spacing"), spacing, 3);

        size_t nx = static_cast<size_t>(ext[1] - ext[0] + 1);
        size_t ny = static_cast<size_t>(ext[3] - ext[2] + 1);
        size_t nz = static_cast<size_t>(ext[5] - ext[4] + 1);
        if (nx == 0 || ny == 0 || nz == 0 || nx * ny * nz > 16000000) return false;

        std::vector<double> scalars;
        std::string_view pdSec = FindXmlElementBody(imXml, "PointData");
        if (!pdSec.empty()) {
            ForEachDataArrayInSection(pdSec, [&](std::string_view oTag, std::string_view inner) {
                if (scalars.empty()) DecodeDataArrayDoubles(oTag, inner, cfg, scalars);
            });
        }

        // If 3D scalar field has non-trivial iso-values, extract voxel iso-boundary; else emit structured boundary!
        bool emittedIso = false;
        if (nx >= 2 && ny >= 2 && nz >= 2 && scalars.size() >= nx * ny * nz) {
            double mn = scalars[0], mx = scalars[0];
            for (size_t i = 0; i < nx * ny * nz; ++i) { mn = std::min(mn, scalars[i]); mx = std::max(mx, scalars[i]); }
            if (mx - mn > 1e-6) {
                double iso = 0.5 * (mn + mx);
                auto voxOn = [&](int ix, int iy, int iz) -> bool {
                    if (ix < 0 || iy < 0 || iz < 0 ||
                        static_cast<size_t>(ix) >= nx || static_cast<size_t>(iy) >= ny || static_cast<size_t>(iz) >= nz) return false;
                    return scalars[(static_cast<size_t>(iz) * ny + static_cast<size_t>(iy)) * nx + static_cast<size_t>(ix)] >= iso;
                };
                auto gridPt = [&](double gx, double gy, double gz) -> Vec3f {
                    return {
                        static_cast<float>(origin[0] + (ext[0] + gx) * spacing[0]),
                        static_cast<float>(origin[1] + (ext[2] + gy) * spacing[1]),
                        static_cast<float>(origin[2] + (ext[4] + gz) * spacing[2])
                    };
                };
                for (int iz = 0; iz < static_cast<int>(nz); ++iz) {
                    for (int iy = 0; iy < static_cast<int>(ny); ++iy) {
                        for (int ix = 0; ix < static_cast<int>(nx); ++ix) {
                            if (!voxOn(ix, iy, iz)) continue;
                            auto emitQuad = [&](Vec3f a, Vec3f b, Vec3f c, Vec3f d) {
                                uint32_t vb = static_cast<uint32_t>(mesh.vertices.size());
                                mesh.vertices.push_back(a); mesh.vertices.push_back(b);
                                mesh.vertices.push_back(c); mesh.vertices.push_back(d);
                                mesh.tris.push_back({ vb + 0, vb + 1, vb + 2, {} });
                                mesh.tris.push_back({ vb + 0, vb + 2, vb + 3, {} });
                            };
                            double x0 = ix - 0.5, x1 = ix + 0.5;
                            double y0 = iy - 0.5, y1 = iy + 0.5;
                            double z0 = iz - 0.5, z1 = iz + 0.5;
                            if (!voxOn(ix - 1, iy, iz)) emitQuad(gridPt(x0,y0,z0), gridPt(x0,y0,z1), gridPt(x0,y1,z1), gridPt(x0,y1,z0));
                            if (!voxOn(ix + 1, iy, iz)) emitQuad(gridPt(x1,y0,z0), gridPt(x1,y1,z0), gridPt(x1,y1,z1), gridPt(x1,y0,z1));
                            if (!voxOn(ix, iy - 1, iz)) emitQuad(gridPt(x0,y0,z0), gridPt(x1,y0,z0), gridPt(x1,y0,z1), gridPt(x0,y0,z1));
                            if (!voxOn(ix, iy + 1, iz)) emitQuad(gridPt(x0,y1,z0), gridPt(x0,y1,z1), gridPt(x1,y1,z1), gridPt(x1,y1,z0));
                            if (!voxOn(ix, iy, iz - 1)) emitQuad(gridPt(x0,y0,z0), gridPt(x0,y1,z0), gridPt(x1,y1,z0), gridPt(x1,y0,z0));
                            if (!voxOn(ix, iy, iz + 1)) emitQuad(gridPt(x0,y0,z1), gridPt(x1,y0,z1), gridPt(x1,y1,z1), gridPt(x0,y1,z1));
                        }
                    }
                }
                emittedIso = !mesh.tris.empty();
            }
        }
        if (!emittedIso) {
            bool is2D = (nz == 1 && nx >= 2 && ny >= 2 && scalars.size() >= nx * ny);
            DetailVtk::EmitStructuredBoundaryVtk(mesh, nx, ny, nz, [&](size_t i, size_t j, size_t k) -> Vec3f {
                double x = origin[0] + (ext[0] + static_cast<double>(i)) * spacing[0];
                double y = origin[1] + (ext[2] + static_cast<double>(j)) * spacing[1];
                double z = origin[2] + (ext[4] + static_cast<double>(k)) * spacing[2];
                if (is2D) y += scalars[j * nx + i];
                return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) };
            });
        }
        mesh.formatName = L"VTK XML ImageData Volume (.vti)";
    }
    else if (rootType == "RectilinearGrid") {
        std::string_view openTag;
        std::string_view rgXml = FindXmlElementBody(xmlHead, "RectilinearGrid", &openTag);
        if (rgXml.empty()) return false;
        int ext[6] = {};
        if (!ParseExtent6(GetXmlAttr(openTag, "WholeExtent"), ext)) return false;
        size_t nx = static_cast<size_t>(ext[1] - ext[0] + 1);
        size_t ny = static_cast<size_t>(ext[3] - ext[2] + 1);
        size_t nz = static_cast<size_t>(ext[5] - ext[4] + 1);

        std::string_view coordSec = FindXmlElementBody(rgXml, "Coordinates");
        std::vector<double> coords[3];
        int ci = 0;
        ForEachDataArrayInSection(coordSec, [&](std::string_view oTag, std::string_view inner) {
            if (ci < 3) DecodeDataArrayDoubles(oTag, inner, cfg, coords[ci++]);
        });
        if (coords[0].size() < nx || coords[1].size() < ny || coords[2].size() < nz) return false;
        DetailVtk::EmitStructuredBoundaryVtk(mesh, nx, ny, nz, [&](size_t i, size_t j, size_t k) -> Vec3f {
            return { static_cast<float>(coords[0][i]), static_cast<float>(coords[1][j]), static_cast<float>(coords[2][k]) };
        });
        mesh.formatName = L"VTK XML RectilinearGrid (.vtr)";
    }
    else if (rootType == "StructuredGrid") {
        std::string_view openTag;
        std::string_view sgXml = FindXmlElementBody(xmlHead, "StructuredGrid", &openTag);
        if (sgXml.empty()) return false;
        int ext[6] = {};
        if (!ParseExtent6(GetXmlAttr(openTag, "WholeExtent"), ext)) return false;
        size_t nx = static_cast<size_t>(ext[1] - ext[0] + 1);
        size_t ny = static_cast<size_t>(ext[3] - ext[2] + 1);
        size_t nz = static_cast<size_t>(ext[5] - ext[4] + 1);

        std::vector<Vec3f> pts;
        if (!decodePointsSection(sgXml, pts) || pts.size() < nx * ny * nz) return false;
        DetailVtk::EmitStructuredBoundaryVtk(mesh, nx, ny, nz, [&](size_t i, size_t j, size_t k) -> Vec3f {
            return pts[(k * ny + j) * nx + i];
        });
        mesh.formatName = L"VTK XML StructuredGrid (.vts)";
    }
    else {
        return false;
    }

    (void)extLower;
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace Formats
