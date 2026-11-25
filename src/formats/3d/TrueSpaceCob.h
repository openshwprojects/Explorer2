#pragma once
// TrueSpaceCob.h — Caligari TrueSpace 3D Object (.cob, ASCII "ALH" & Binary "BLH"/"BBH") reader
// Parses 32-byte Caligari header, PolH & Grou chunks, parent-child transform hierarchy,
// outer polygon faces, and F_HOLE (0x08) inner hole loops bridged via EmitPlanarPolygon.

namespace Formats {

namespace DetailTrueSpaceCob {

struct CobNodeXf {
    int32_t parentId = 0;
    float localM[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
};

inline void EvalCobWorldMat(
    int32_t id,
    const std::unordered_map<int32_t, CobNodeXf>& nodes,
    float outM[16])
{
    float acc[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
    int32_t cur = id;
    for (int depth = 0; depth < 32 && cur != 0; ++depth) {
        auto it = nodes.find(cur);
        if (it == nodes.end()) break;
        float next[16];
        MeshBuild3D::MatMul4RowMajor(it->second.localM, acc, next);
        std::memcpy(acc, next, sizeof(next));
        if (it->second.parentId == cur) break;
        cur = it->second.parentId;
    }
    std::memcpy(outM, acc, 16 * sizeof(float));
}

inline Vec3f TransformCobVert(const float m[16], Vec3f p) {
    const float wx = m[0] * p.x + m[1] * p.y + m[2]  * p.z + m[3];
    const float wy = m[4] * p.x + m[5] * p.y + m[6]  * p.z + m[7];
    const float wz = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
    // TrueSpace is Z-up right-handed -> map (wx, wz, -wy) to viewer Y-up
    return { wx, wz, -wy };
}

struct CobFace {
    std::vector<uint32_t> outer;
    std::vector<std::vector<uint32_t>> holes;
};

struct CobPolH {
    int32_t id = 0;
    int32_t parentId = 0;
    std::vector<Vec3f> verts;
    std::vector<CobFace> faces;
};

inline uint16_t RdU16Cob(const uint8_t* p, bool be) { return be ? ByteIO::RdU16BE(p) : ByteIO::RdU16LE(p); }
inline int16_t  RdI16Cob(const uint8_t* p, bool be) { return static_cast<int16_t>(RdU16Cob(p, be)); }
inline uint32_t RdU32Cob(const uint8_t* p, bool be) { return be ? ByteIO::RdU32BE(p) : ByteIO::RdU32LE(p); }
inline int32_t  RdI32Cob(const uint8_t* p, bool be) { return static_cast<int32_t>(RdU32Cob(p, be)); }
inline float    RdF32Cob(const uint8_t* p, bool be) { return be ? ByteIO::RdF32BE(p) : ByteIO::RdF32LE(p); }

inline bool SkipCobNameAndReadAxesMat(const uint8_t* p, size_t len, bool be, size_t& off, float outMat[16]) {
    if (off + 4u > len) return false;
    const int16_t strLen = RdI16Cob(p + off + 2u, be);
    off += 4u;
    if (strLen < 0 || off + static_cast<size_t>(strLen) + 96u > len) return false;
    off += static_cast<size_t>(strLen);
    off += 48u; // Skip local axes (center + x/y/z axis = 12 floats)
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float v = RdF32Cob(p + off, be);
            if (!std::isfinite(v)) return false;
            outMat[r * 4 + c] = v;
            off += 4u;
        }
    }
    outMat[12] = 0.0f; outMat[13] = 0.0f; outMat[14] = 0.0f; outMat[15] = 1.0f;
    return true;
}

inline bool ParseBinaryCob(const uint8_t* data, size_t size, bool be, LoadedMesh3D& mesh) {
    std::unordered_map<int32_t, CobNodeXf> nodes;
    std::vector<CobPolH> meshes;

    size_t pos = 32u;
    while (pos + 20u <= size) {
        const char c0 = static_cast<char>(data[pos + 0]);
        const char c1 = static_cast<char>(data[pos + 1]);
        const char c2 = static_cast<char>(data[pos + 2]);
        const char c3 = static_cast<char>(data[pos + 3]);
        const int32_t chunkId  = RdI32Cob(data + pos + 8u, be);
        const int32_t parentId = RdI32Cob(data + pos + 12u, be);
        const int32_t dataSize = RdI32Cob(data + pos + 16u, be);
        pos += 20u;

        if (c0 == 'E' && c1 == 'N' && c2 == 'D') break;
        if (dataSize < 0 || pos + static_cast<size_t>(dataSize) > size) return false;

        const uint8_t* pay = data + pos;
        const size_t payLen = static_cast<size_t>(dataSize);
        pos += payLen;

        if (c0 == 'G' && c1 == 'r' && c2 == 'o' && c3 == 'u') {
            size_t off = 0;
            CobNodeXf nx;
            nx.parentId = parentId;
            if (SkipCobNameAndReadAxesMat(pay, payLen, be, off, nx.localM) && chunkId != 0) {
                nodes[chunkId] = nx;
            }
        } else if (c0 == 'P' && c1 == 'o' && c2 == 'l' && c3 == 'H') {
            size_t off = 0;
            CobNodeXf nx;
            nx.parentId = parentId;
            if (!SkipCobNameAndReadAxesMat(pay, payLen, be, off, nx.localM)) return false;
            if (chunkId != 0) nodes[chunkId] = nx;

            if (off + 4u > payLen) return false;
            const int32_t nv = RdI32Cob(pay + off, be);
            off += 4u;
            if (nv < 3 || nv > 2000000 || off + static_cast<size_t>(nv) * 12u > payLen) return false;

            CobPolH pol;
            pol.id = chunkId;
            pol.parentId = parentId;
            pol.verts.reserve(static_cast<size_t>(nv));
            for (int32_t i = 0; i < nv; ++i) {
                const float x = RdF32Cob(pay + off + 0u, be);
                const float y = RdF32Cob(pay + off + 4u, be);
                const float z = RdF32Cob(pay + off + 8u, be);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
                pol.verts.push_back({ x, y, z });
                off += 12u;
            }

            if (off + 4u > payLen) return false;
            const int32_t nuv = RdI32Cob(pay + off, be);
            off += 4u;
            if (nuv < 0 || nuv > 4000000 || off + static_cast<size_t>(nuv) * 8u > payLen) return false;
            off += static_cast<size_t>(nuv) * 8u;

            if (off + 4u > payLen) return false;
            const int32_t nf = RdI32Cob(pay + off, be);
            off += 4u;
            if (nf < 1 || nf > 4000000) return false;

            for (int32_t f = 0; f < nf; ++f) {
                if (off + 3u > payLen) return false;
                const uint8_t flags = pay[off];
                const int16_t fvCount = RdI16Cob(pay + off + 1u, be);
                off += 3u;
                const bool isHole = (flags & 0x08u) != 0;
                if (!isHole) {
                    if (off + 2u > payLen) return false;
                    off += 2u; // material index
                }
                if (fvCount < 3 || fvCount > 4096 || off + static_cast<size_t>(fvCount) * 8u > payLen) return false;

                std::vector<uint32_t> loop;
                loop.reserve(static_cast<size_t>(fvCount));
                for (int16_t k = 0; k < fvCount; ++k) {
                    const int32_t vi = RdI32Cob(pay + off, be);
                    off += 8u; // (vertIdx, uvIdx)
                    if (vi < 0 || vi >= nv) return false;
                    loop.push_back(static_cast<uint32_t>(vi));
                }
                if (isHole) {
                    if (!pol.faces.empty()) pol.faces.back().holes.push_back(std::move(loop));
                } else {
                    CobFace cf;
                    cf.outer = std::move(loop);
                    pol.faces.push_back(std::move(cf));
                }
            }
            if (!pol.faces.empty()) meshes.push_back(std::move(pol));
        }
    }

    for (const auto& pol : meshes) {
        float worldM[16];
        EvalCobWorldMat(pol.id, nodes, worldM);
        std::vector<Vec3f> wv(pol.verts.size());
        for (size_t i = 0; i < pol.verts.size(); ++i) wv[i] = TransformCobVert(worldM, pol.verts[i]);

        for (const auto& cf : pol.faces) {
            if (cf.holes.empty() && cf.outer.size() == 3) {
                const uint32_t b = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(wv[cf.outer[0]]);
                mesh.vertices.push_back(wv[cf.outer[1]]);
                mesh.vertices.push_back(wv[cf.outer[2]]);
                mesh.tris.push_back({ b, b + 1, b + 2, {} });
            } else {
                std::vector<Vec3f> outer3;
                outer3.reserve(cf.outer.size());
                for (uint32_t vi : cf.outer) outer3.push_back(wv[vi]);
                std::vector<std::vector<Vec3f>> holes3;
                for (const auto& h : cf.holes) {
                    std::vector<Vec3f> hr;
                    hr.reserve(h.size());
                    for (uint32_t vi : h) hr.push_back(wv[vi]);
                    holes3.push_back(std::move(hr));
                }
                MeshBuild3D::EmitPlanarPolygon(mesh, outer3, holes3);
            }
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

inline bool ParseAsciiCob(std::string_view sv, LoadedMesh3D& mesh) {
    if (sv.find('\0') != std::string_view::npos) return false;
    std::unordered_map<int32_t, CobNodeXf> nodes;
    std::vector<CobPolH> meshes;

    size_t pos = 32u;
    std::string_view line;
    int32_t curId = 0, curParent = 0;
    std::string curChunk;
    CobPolH activePol;

    auto flushPol = [&]() {
        if (curChunk == "PolH" && !activePol.verts.empty() && !activePol.faces.empty()) {
            meshes.push_back(std::move(activePol));
        }
        activePol = CobPolH{};
    };

    while (ReaderText::NextLineSv(sv, pos, line)) {
        std::string_view tr = ReaderText::TrimSv(line);
        if (tr.empty()) continue;

        // Detect chunk header: "<4char> V<ver> Id <id> Parent <pid> Size <sz>"
        if (tr.find(" Parent ") != std::string_view::npos && tr.find(" Size ") != std::string_view::npos) {
            flushPol();
            std::string_view rem = tr, tType, tVer, kwId, tId, kwPar, tPar;
            ReaderText::NextTokenSv(rem, tType);
            ReaderText::NextTokenSv(rem, tVer);
            ReaderText::NextTokenSv(rem, kwId);
            ReaderText::NextTokenSv(rem, tId);
            ReaderText::NextTokenSv(rem, kwPar);
            ReaderText::NextTokenSv(rem, tPar);
            curChunk = std::string(tType);
            curId = 0; curParent = 0;
            ReaderText::ParseIntSv(tId, curId);
            ReaderText::ParseIntSv(tPar, curParent);
            if (curChunk == "END") break;
            if (curId != 0) {
                CobNodeXf nx;
                nx.parentId = curParent;
                nodes[curId] = nx;
            }
            if (curChunk == "PolH") {
                activePol.id = curId;
                activePol.parentId = curParent;
            }
            continue;
        }

        if (tr == "Transform" && curId != 0) {
            float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            for (int r = 0; r < 4; ++r) {
                std::string_view rl;
                if (!ReaderText::NextLineSv(sv, pos, rl)) break;
                for (int c = 0; c < 4; ++c) {
                    std::string_view tv;
                    if (ReaderText::NextTokenSv(rl, tv)) ReaderText::ParseFloatSv(tv, m[r * 4 + c]);
                }
            }
            nodes[curId].localM[0] = m[0]; nodes[curId].localM[1] = m[1]; nodes[curId].localM[2]  = m[2];  nodes[curId].localM[3]  = m[3];
            nodes[curId].localM[4] = m[4]; nodes[curId].localM[5] = m[5]; nodes[curId].localM[6]  = m[6];  nodes[curId].localM[7]  = m[7];
            nodes[curId].localM[8] = m[8]; nodes[curId].localM[9] = m[9]; nodes[curId].localM[10] = m[10]; nodes[curId].localM[11] = m[11];
        } else if (curChunk == "PolH" && ReaderText::StartsWithCI(tr, "World Vertices")) {
            std::string_view rem = tr.substr(14), tN;
            int nv = 0;
            if (!ReaderText::NextTokenSv(rem, tN) || !ReaderText::ParseIntSv(tN, nv) || nv < 3 || nv > 2000000) return false;
            activePol.verts.reserve(static_cast<size_t>(nv));
            for (int i = 0; i < nv; ++i) {
                std::string_view vl;
                if (!ReaderText::NextLineSv(sv, pos, vl)) return false;
                std::string_view tx, ty, tz;
                float x = 0, y = 0, z = 0;
                if (!ReaderText::NextTokenSv(vl, tx) || !ReaderText::ParseFloatSv(tx, x) ||
                    !ReaderText::NextTokenSv(vl, ty) || !ReaderText::ParseFloatSv(ty, y) ||
                    !ReaderText::NextTokenSv(vl, tz) || !ReaderText::ParseFloatSv(tz, z)) return false;
                activePol.verts.push_back({ x, y, z });
            }
        } else if (curChunk == "PolH" && (ReaderText::StartsWithCI(tr, "Face verts") || ReaderText::StartsWithCI(tr, "Hole verts"))) {
            const bool isHole = ReaderText::StartsWithCI(tr, "Hole");
            std::string_view rem = tr.substr(10), tCnt;
            int fvCnt = 0;
            if (!ReaderText::NextTokenSv(rem, tCnt) || !ReaderText::ParseIntSv(tCnt, fvCnt) || fvCnt < 3 || fvCnt > 4096) return false;
            std::string_view idxLine;
            if (!ReaderText::NextLineSv(sv, pos, idxLine)) return false;

            std::vector<uint32_t> loop;
            for (size_t k = 0; k < idxLine.size(); ) {
                const size_t lt = idxLine.find('<', k);
                if (lt == std::string_view::npos) break;
                const size_t gt = idxLine.find('>', lt + 1);
                if (gt == std::string_view::npos) break;
                std::string_view pairSv = idxLine.substr(lt + 1, gt - lt - 1);
                const size_t comma = pairSv.find(',');
                std::string_view vSv = (comma == std::string_view::npos) ? pairSv : pairSv.substr(0, comma);
                int vi = -1;
                if (!ReaderText::ParseIntSv(vSv, vi) || vi < 0 || static_cast<size_t>(vi) >= activePol.verts.size()) return false;
                loop.push_back(static_cast<uint32_t>(vi));
                k = gt + 1;
            }
            if (static_cast<int>(loop.size()) != fvCnt) return false;
            if (isHole) {
                if (!activePol.faces.empty()) activePol.faces.back().holes.push_back(std::move(loop));
            } else {
                CobFace cf;
                cf.outer = std::move(loop);
                activePol.faces.push_back(std::move(cf));
            }
        }
    }
    flushPol();

    for (const auto& pol : meshes) {
        float worldM[16];
        EvalCobWorldMat(pol.id, nodes, worldM);
        std::vector<Vec3f> wv(pol.verts.size());
        for (size_t i = 0; i < pol.verts.size(); ++i) wv[i] = TransformCobVert(worldM, pol.verts[i]);
        for (const auto& cf : pol.faces) {
            std::vector<Vec3f> outer3;
            for (uint32_t vi : cf.outer) outer3.push_back(wv[vi]);
            std::vector<std::vector<Vec3f>> holes3;
            for (const auto& h : cf.holes) {
                std::vector<Vec3f> hr;
                for (uint32_t vi : h) hr.push_back(wv[vi]);
                holes3.push_back(std::move(hr));
            }
            MeshBuild3D::EmitPlanarPolygon(mesh, outer3, holes3);
        }
    }
    return !mesh.vertices.empty() && !mesh.tris.empty();
}

} // namespace DetailTrueSpaceCob

inline bool ParseTrueSpaceCob(
    const uint8_t* data,
    size_t size,
    LoadedMesh3D& mesh)
{
    if (!data || size < 52 || size > 64u * 1024u * 1024u) return false;
    if (std::memcmp(data, "Caligari V00.01", 15) != 0) return false;

    const char mode = static_cast<char>(data[15]);
    const char end0 = static_cast<char>(data[16]);
    const char end1 = static_cast<char>(data[17]);
    if (end1 != 'H' || (end0 != 'L' && end0 != 'B')) return false;

    bool ok = false;
    if (mode == 'B') {
        ok = DetailTrueSpaceCob::ParseBinaryCob(data, size, (end0 == 'B'), mesh);
    } else if (mode == 'A') {
        ok = DetailTrueSpaceCob::ParseAsciiCob(std::string_view(reinterpret_cast<const char*>(data), size), mesh);
    }
    if (!ok) return false;

    mesh.formatName = (mode == 'B') ? L"Caligari TrueSpace Binary (COB)" : L"Caligari TrueSpace ASCII (COB)";
    return true;
}

} // namespace Formats
