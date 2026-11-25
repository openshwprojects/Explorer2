#pragma once
// CelestiaCmod.h — Celestia 3D Astronomy & Space Modding Mesh (.cmod) reader
// Supports both "#celmodel__ascii" and "#celmodel_binary" formats (vertexdesc, trilist, tristrip, trifan).

namespace Formats {

namespace DetailCelestiaCmod {

inline void EmitPrimGroup(
    uint16_t primType, // 0 = trilist, 1 = tristrip, 2 = trifan
    const std::vector<uint32_t>& idx,
    uint32_t baseV,
    LoadedMesh3D& mesh)
{
    if (primType == 0u) {
        for (size_t i = 0; i + 2u < idx.size(); i += 3u) {
            const uint32_t a = idx[i], b = idx[i + 1u], c = idx[i + 2u];
            if (a != b && b != c && a != c) {
                mesh.tris.push_back({ baseV + a, baseV + b, baseV + c, {} });
            }
        }
    } else if (primType == 1u) {
        for (size_t i = 0; i + 2u < idx.size(); ++i) {
            const uint32_t a = idx[i];
            const uint32_t b = (i & 1u) ? idx[i + 2u] : idx[i + 1u];
            const uint32_t c = (i & 1u) ? idx[i + 1u] : idx[i + 2u];
            if (a != b && b != c && a != c) {
                mesh.tris.push_back({ baseV + a, baseV + b, baseV + c, {} });
            }
        }
    } else if (primType == 2u && idx.size() >= 3u) {
        for (size_t i = 1u; i + 1u < idx.size(); ++i) {
            const uint32_t a = idx[0], b = idx[i], c = idx[i + 1u];
            if (a != b && b != c && a != c) {
                mesh.tris.push_back({ baseV + a, baseV + b, baseV + c, {} });
            }
        }
    }
}

inline bool ParseAsciiCmod(std::string_view sv, LoadedMesh3D& mesh) {
    // Tokenize stripping '#' line comments
    std::vector<std::string_view> toks;
    toks.reserve(1024);
    size_t pos = 0;
    std::string_view line;
    while (ReaderText::NextLineSv(sv, pos, line)) {
        const size_t hp = line.find('#');
        if (hp != std::string_view::npos) line = line.substr(0, hp);
        std::string_view tok;
        while (ReaderText::NextTokenSv(line, tok)) {
            toks.push_back(tok);
            if (toks.size() > 8000000u) return false;
        }
    }

    size_t ti = 0;
    while (ti < toks.size()) {
        const std::string_view kw = toks[ti++];
        if (kw == "material") {
            while (ti < toks.size() && toks[ti] != "end_material") ++ti;
            if (ti >= toks.size()) return false;
            ++ti; // consume end_material
        } else if (kw == "mesh") {
            int posFloatOffset = -1;
            int posCompCount = 0;
            int totalScalarsPerVert = 0;
            uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t meshVertCount = 0;

            while (ti < toks.size() && toks[ti] != "end_mesh") {
                const std::string_view mkw = toks[ti++];
                if (mkw == "vertexdesc") {
                    posFloatOffset = -1;
                    posCompCount = 0;
                    totalScalarsPerVert = 0;
                    while (ti < toks.size() && toks[ti] != "end_vertexdesc") {
                        if (ti + 1u >= toks.size()) return false;
                        const std::string_view sem = toks[ti++];
                        const std::string_view fmt = toks[ti++];
                        int nScalars = 0;
                        if (fmt == "f1") nScalars = 1;
                        else if (fmt == "f2") nScalars = 2;
                        else if (fmt == "f3") nScalars = 3;
                        else if (fmt == "f4" || fmt == "ub4") nScalars = 4;
                        else return false;
                        if (sem == "position") {
                            if (fmt != "f3" && fmt != "f4") return false;
                            posFloatOffset = totalScalarsPerVert;
                            posCompCount = nScalars;
                        }
                        totalScalarsPerVert += nScalars;
                    }
                    if (ti >= toks.size() || posFloatOffset < 0 || totalScalarsPerVert <= 0) return false;
                    ++ti; // end_vertexdesc
                } else if (mkw == "vertices") {
                    if (ti >= toks.size() || posFloatOffset < 0 || posCompCount < 3) return false;
                    int nv = 0;
                    if (!ReaderText::ParseIntSv(toks[ti++], nv) || nv < 3 || nv > 1000000) return false;
                    const size_t needed = static_cast<size_t>(nv) * static_cast<size_t>(totalScalarsPerVert);
                    if (ti + needed > toks.size()) return false;
                    baseV = static_cast<uint32_t>(mesh.vertices.size());
                    meshVertCount = static_cast<uint32_t>(nv);
                    for (int v = 0; v < nv; ++v) {
                        float x = 0.0f, y = 0.0f, z = 0.0f;
                        for (int s = 0; s < totalScalarsPerVert; ++s) {
                            float val = 0.0f;
                            if (!ReaderText::ParseFloatSv(toks[ti++], val)) return false;
                            if (s == posFloatOffset + 0) x = val;
                            else if (s == posFloatOffset + 1) y = val;
                            else if (s == posFloatOffset + 2) z = val;
                        }
                        mesh.vertices.push_back({ x, y, z });
                    }
                } else if (mkw == "trilist" || mkw == "tristrip" || mkw == "trifan" ||
                           mkw == "linelist" || mkw == "linestrip" || mkw == "points") {
                    if (ti + 1u >= toks.size() || meshVertCount == 0u) return false;
                    int matIdx = 0, nIdx = 0;
                    if (!ReaderText::ParseIntSv(toks[ti++], matIdx) ||
                        !ReaderText::ParseIntSv(toks[ti++], nIdx) || nIdx < 0 || nIdx > 3000000) {
                        return false;
                    }
                    (void)matIdx;
                    if (ti + static_cast<size_t>(nIdx) > toks.size()) return false;
                    std::vector<uint32_t> idxs(static_cast<size_t>(nIdx));
                    for (int k = 0; k < nIdx; ++k) {
                        int iv = -1;
                        if (!ReaderText::ParseIntSv(toks[ti++], iv) || iv < 0 || static_cast<uint32_t>(iv) >= meshVertCount) {
                            return false;
                        }
                        idxs[static_cast<size_t>(k)] = static_cast<uint32_t>(iv);
                    }
                    if (mkw == "trilist")       EmitPrimGroup(0u, idxs, baseV, mesh);
                    else if (mkw == "tristrip") EmitPrimGroup(1u, idxs, baseV, mesh);
                    else if (mkw == "trifan")   EmitPrimGroup(2u, idxs, baseV, mesh);
                } else {
                    return false;
                }
            }
            if (ti >= toks.size() || toks[ti] != "end_mesh") return false;
            ++ti;
        } else {
            return false;
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

inline bool ParseBinaryCmod(const uint8_t* data, size_t size, LoadedMesh3D& mesh) {
    size_t p = 16u;
    while (p + 2u <= size) {
        const uint16_t tok = ByteIO::RdU16LE(data + p);
        p += 2u;
        if (tok == 1001u) { // CMOD_Material
            while (p + 2u <= size) {
                const uint16_t mtok = ByteIO::RdU16LE(data + p);
                p += 2u;
                if (mtok == 1002u) break; // CMOD_EndMaterial
                if (mtok == 1008u) { // CMOD_Texture: uint16 semantic + CMOD_String (u16 type=5, u16 len, bytes)
                    if (p + 6u > size) return false;
                    const uint16_t dtype = ByteIO::RdU16LE(data + p + 2u);
                    if (dtype != 5u) return false;
                    const uint16_t slen = ByteIO::RdU16LE(data + p + 4u);
                    p += 6u;
                    if (p + slen > size) return false;
                    p += slen;
                    continue;
                }
                if (p + 2u > size) return false;
                const uint16_t dtype = ByteIO::RdU16LE(data + p);
                p += 2u;
                size_t skip = 0;
                if (dtype == 1u || dtype == 6u) skip = 4u;
                else if (dtype == 2u) skip = 8u;
                else if (dtype == 3u || dtype == 7u) skip = 12u;
                else if (dtype == 4u) skip = 16u;
                else if (dtype == 5u) {
                    if (p + 2u > size) return false;
                    skip = 2u + ByteIO::RdU16LE(data + p);
                } else return false;
                if (p + skip > size) return false;
                p += skip;
            }
        } else if (tok == 1003u) { // CMOD_Mesh
            if (p + 2u > size || ByteIO::RdU16LE(data + p) != 1005u) return false; // CMOD_VertexDesc
            p += 2u;
            size_t vStride = 0;
            int posByteOff = -1;
            while (p + 2u <= size) {
                const uint16_t sem = ByteIO::RdU16LE(data + p);
                p += 2u;
                if (sem == 1006u) break; // CMOD_EndVertexDesc
                if (p + 2u > size) return false;
                const uint16_t fmt = ByteIO::RdU16LE(data + p);
                p += 2u;
                size_t attrSz = 0;
                if (fmt == 0u || fmt == 4u) attrSz = 4u;
                else if (fmt == 1u) attrSz = 8u;
                else if (fmt == 2u) attrSz = 12u;
                else if (fmt == 3u) attrSz = 16u;
                else return false;
                if (sem == 0u) { // Position
                    if (fmt != 2u && fmt != 3u) return false;
                    posByteOff = static_cast<int>(vStride);
                }
                vStride += attrSz;
            }
            if (posByteOff < 0 || vStride < 12u || p + 6u > size) return false;
            if (ByteIO::RdU16LE(data + p) != 1007u) return false; // CMOD_Vertices
            p += 2u;
            const uint32_t nv = ByteIO::RdU32LE(data + p);
            p += 4u;
            if (nv < 3u || nv > 2000000u || p + static_cast<uint64_t>(nv) * vStride > size) return false;

            const uint32_t baseV = static_cast<uint32_t>(mesh.vertices.size());
            for (uint32_t i = 0; i < nv; ++i) {
                const uint8_t* vp = data + p + static_cast<size_t>(i) * vStride + static_cast<size_t>(posByteOff);
                const float x = ByteIO::RdF32LE(vp + 0u);
                const float y = ByteIO::RdF32LE(vp + 4u);
                const float z = ByteIO::RdF32LE(vp + 8u);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                mesh.vertices.push_back({ x, y, z });
            }
            p += static_cast<size_t>(nv) * vStride;

            while (p + 2u <= size) {
                const uint16_t ptok = ByteIO::RdU16LE(data + p);
                p += 2u;
                if (ptok == 1004u) break; // CMOD_EndMesh
                if (ptok > 5u || p + 8u > size) return false;
                const uint32_t nIdx = ByteIO::RdU32LE(data + p + 4u);
                p += 8u;
                if (nIdx > 4000000u || p + static_cast<uint64_t>(nIdx) * 4ULL > size) return false;
                std::vector<uint32_t> idxs(nIdx);
                for (uint32_t k = 0; k < nIdx; ++k) {
                    const uint32_t iv = ByteIO::RdU32LE(data + p + static_cast<size_t>(k) * 4u);
                    if (iv >= nv) return false;
                    idxs[k] = iv;
                }
                p += static_cast<size_t>(nIdx) * 4u;
                EmitPrimGroup(ptok, idxs, baseV, mesh);
            }
        } else {
            return false;
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailCelestiaCmod

inline bool ParseCelestiaCmod(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 24) return false;
    if (std::memcmp(data, "#celmodel__ascii", 16) == 0) {
        std::string_view sv(reinterpret_cast<const char*>(data + 16u), size - 16u);
        if (!DetailCelestiaCmod::ParseAsciiCmod(sv, mesh)) return false;
        mesh.formatName = L"Celestia 3D Model (CMOD ASCII)";
        return true;
    }
    if (std::memcmp(data, "#celmodel_binary", 16) == 0) {
        if (!DetailCelestiaCmod::ParseBinaryCmod(data, size, mesh)) return false;
        mesh.formatName = L"Celestia 3D Model (CMOD Binary)";
        return true;
    }
    return false;
}

} // namespace Formats
